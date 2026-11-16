// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/objectives.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "addr/script.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/log.h"
#include "game/cmdcheck.h"
#include "game/gamemenu.h"
#include "game/gamehook.h"
#include "hooks/gameload.h"
#include "core/winpath.h"

namespace {

const char kFileSuffix[] = ".objectives";
// The VS2003 std::map<std::string, CNote> nodes: +0 left, +4 parent, +8 right, +0xc the id, +0x28 the CNote; the head is its own
// sentinel and the leaves point back to it.
const size_t kNodeLeft = 0, kNodeRight = 8, kNodeKey = 0xc, kNodeNote = 0x28;
// CNote (CNote::CNote_copy 00470fc0, CNote::Serialize 005534a0): +8 title, +0x24 text, +0x5c/+0x60 location (-1 without one),
// +0x68 the name of its map.
const size_t kNoteTitle = 8, kNoteText = 0x24, kNoteX = 0x5c, kNoteY = 0x60, kNoteMap = 0x68;
// CNoteManager (its constructor 005583a0): +0xc the head of the notes it holds, +0x18 the head of the std::list of ids in the order
// GiveNote added them; the list's nodes are {next, prev, std::string}.
const size_t kManagerHead = 0xc, kManagerOrder = 0x18, kListNodeValue = 8;
// CNoteDatabase (its constructor 00557c00), the notes of the map and of the game: +4 the head of its map.
const size_t kDatabaseHead = 4;
// g_pGame+0x198 the name of the map being played; g_pMap+0xb4 its rectangle (left, top, right, bottom), as CVXUINotes::SelectNote
// (006e67d0) checks before it centres the view.
const size_t kGameMapName = 0x198, kMapRect = 0xb4;
// menuini/Notes.ini: 0x1000 "View location", 0x1001 Close (also Escape); CVXUINotes keeps its CUISList at +0x74 (OnShow 006e7020),
// whose +0x58 is the count and +0x94 the 0x14-byte items, each starting with its text, which is the note's id (FillNoteList 006e6550).
const int kIdView = 0x1000, kIdClose = 0x1001;
const size_t kDlgList = 0x74, kListCount = 0x58, kListItems = 0x94, kListItemSize = 0x14;
const unsigned kFlagVisible = 2;
const size_t kMaxNodes = 4000;

typedef void *(__fastcall *ManagerCtor_t)(void *self, void *edx, int mostDerived);
typedef int(__fastcall *RemoveNote_t)(void *self, void *edx, const void *id);
typedef int(__cdecl *Native_t)(int *sp);
typedef bool(__fastcall *HasFlags_t)(void *window, void *edx, unsigned flags);
typedef void(__fastcall *SelectExclusive_t)(void *list, void *edx, unsigned index, int notify, int redraw);

ManagerCtor_t g_origCtor = nullptr;
RemoveNote_t g_origRemove = nullptr;
Native_t g_origClear = nullptr;

std::mutex g_lock;
ObjectiveLog g_log;
uint32_t g_version = 0;

struct Held {
	std::string id;
	const char *note;
};

void Collect(const char *node, const char *head, std::vector<Held> &out, int depth)
{
	if (!node || node == head || depth > 64 || out.size() >= kMaxNodes) return;
	Collect(*reinterpret_cast<const char *const *>(node + kNodeLeft), head, out, depth + 1);
	out.push_back({GameStringAt(node + kNodeKey), node + kNodeNote});
	Collect(*reinterpret_cast<const char *const *>(node + kNodeRight), head, out, depth + 1);
}

std::vector<Held> TreeNotes(const char *head)
{
	std::vector<Held> out;
	if (head) Collect(*reinterpret_cast<const char *const *>(head + 4), head, out, 0);
	return out;
}

const char *Manager() { return *reinterpret_cast<const char *const *>(addr::kNoteManager); }

std::vector<Held> HeldNotes()
{
	const char *manager = Manager();
	return manager ? TreeNotes(*reinterpret_cast<const char *const *>(manager + kManagerHead)) : std::vector<Held>();
}

std::vector<std::string> GivenOrder()
{
	std::vector<std::string> out;
	const char *manager = Manager();
	const char *head = manager ? *reinterpret_cast<const char *const *>(manager + kManagerOrder) : nullptr;
	if (!head) return out;
	size_t guard = 0;
	for (const char *n = *reinterpret_cast<const char *const *>(head); n && n != head && guard < kMaxNodes; n = *reinterpret_cast<const char *const *>(n), guard++) {
		out.push_back(GameStringAt(n + kListNodeValue));
	}
	return out;
}

// The definition the game's dialog draws (FindNoteDef 00557df0): the map's notes first, then the game's.
const char *Definition(const std::string &id)
{
	for (const uintptr_t db : {addr::kMapNotes, addr::kGameNotes}) {
		const char *base = *reinterpret_cast<const char *const *>(db);
		if (!base) continue;
		for (const Held &h : TreeNotes(*reinterpret_cast<const char *const *>(base + kDatabaseHead))) {
			if (h.id == id) return h.note;
		}
	}
	return nullptr;
}

bool OnThisMap(const char *note)
{
	const char *game = *reinterpret_cast<const char *const *>(addr::kGame);
	const char *map = *reinterpret_cast<const char *const *>(addr::kMap);
	if (!game || !map) return false;
	const int x = *reinterpret_cast<const int *>(note + kNoteX), y = *reinterpret_cast<const int *>(note + kNoteY);
	if (x == -1 || y == -1) return false;
	const int *rect = reinterpret_cast<const int *>(map + kMapRect);
	if (x < rect[0] || x > rect[2] || y < rect[1] || y > rect[3]) return false;
	return GameStringAt(note + kNoteMap) == GameStringAt(game + kGameMapName);
}

ObjectiveNote Read(const Held &held)
{
	ObjectiveNote n;
	n.id = held.id;
	// The words of the definition, as the dialog shows them; the note the manager holds is a copy from when it was given, and the one
	// left when its map's notes are no longer loaded.
	const char *def = Definition(held.id);
	const char *words = def ? def : held.note;
	n.title = GameStringAt(words + kNoteTitle);
	n.text = GameStringAt(words + kNoteText);
	if (n.title.empty()) n.title = GameStringAt(held.note + kNoteTitle);
	if (n.text.empty()) n.text = GameStringAt(held.note + kNoteText);
	// Where SelectNote centres the view: the manager's copy.
	n.onMap = OnThisMap(held.note);
	return n;
}

void Complete(const std::vector<ObjectiveNote> &notes)
{
	if (notes.empty()) return;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ObjectiveNote &n : notes) g_log.Complete(n);
	g_version++;
}

void *__fastcall HookCtor(void *self, void *edx, int mostDerived)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_log.Empty()) LogD("game", "objectives: a new game starts with no completed notes");
		g_log.Clear();
		g_version++;
	}
	return g_origCtor(self, edx, mostDerived);
}

int __fastcall HookRemoveNote(void *self, void *edx, const void *id)
{
	if (self && self == Manager()) {
		const std::string name = GameStringAt(id);
		std::vector<ObjectiveNote> gone;
		for (const Held &h : HeldNotes()) {
			if (h.id == name) gone.push_back(Read(h));
		}
		Complete(gone);
	}
	return g_origRemove(self, edx, id);
}

int __cdecl HookClearNotes(int *sp)
{
	std::vector<ObjectiveNote> gone;
	const std::vector<Held> held = HeldNotes();
	std::vector<std::string> ids;
	for (const Held &h : held) ids.push_back(h.id);
	for (const std::string &id : ObjectiveActiveOrder(GivenOrder(), ids)) {
		for (const Held &h : held) {
			if (h.id == id) {
				gone.push_back(Read(h));
				break;
			}
		}
	}
	Complete(gone);
	return g_origClear(sp);
}

std::string ReadWholeFile(const std::string &path)
{
	std::string data;
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return data;
	char buffer[16384];
	for (size_t got = fread(buffer, 1, sizeof(buffer), f); got; got = fread(buffer, 1, sizeof(buffer), f)) data.append(buffer, got);
	fclose(f);
	return data;
}

std::string DiskPath(const std::string &gamePath)
{
	std::string file = winpath::GameDir() + "\\" + gamePath;
	for (char &c : file) {
		if (c == '/') c = '\\';
	}
	return file;
}

void AfterGameLoad(const char *path, int result)
{
	if (result != 0 || !path) return;
	const std::string data = ReadWholeFile(DiskPath(path) + kFileSuffix);
	std::lock_guard<std::mutex> lock(g_lock);
	g_version++;
	if (data.empty()) {
		g_log.Clear();
		return;
	}
	if (g_log.Decode(data)) LogD("saves", "objectives: %d completed notes restored with the game", static_cast<int>(g_log.Completed().size()));
	else LogW("saves", "the completed objectives of the loaded game could not be read");
}

bool HookAt(uintptr_t at, void *detour, void **orig, const char *what)
{
	if (HookFunction(at, detour, orig)) return true;
	LogE("game", "objectives: could not hook %s", what);
	return false;
}

}

void InstallObjectives(bool knownExe)
{
	if (!knownExe) return;
	// The constructor first: without it the notes of one game would show as completed in the next.
	const bool ok = HookAt(addr::kNoteManagerCtor, reinterpret_cast<void *>(&HookCtor), reinterpret_cast<void **>(&g_origCtor), "CNoteManager::CNoteManager") &&
		HookAt(addr::kNoteManagerRemoveNote, reinterpret_cast<void *>(&HookRemoveNote), reinterpret_cast<void **>(&g_origRemove), "CNoteManager::RemoveNote") &&
		HookAt(addr::kScriptFnClearNotes, reinterpret_cast<void *>(&HookClearNotes), reinterpret_cast<void **>(&g_origClear), "ClearNotes");
	if (!ok) {
		LogE("game", "objectives: the objectives screen will not list completed notes");
		return;
	}
	if (InstallGameLoadHook(knownExe)) OnGameLoadedFrom(&AfterGameLoad);
	else LogW("game", "objectives: the completed notes are not restored when a game is loaded");
	LogD("game", "objectives: keeping the notes the scripts remove");
}

void ObjectivesWriteFile(const std::string &saveFile)
{
	const std::string file = saveFile + kFileSuffix;
	std::string bytes;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_log.Empty()) bytes = g_log.Encode();
	}
	if (bytes.empty()) {
		DeleteFileA(file.c_str());
		return;
	}
	FILE *f = nullptr;
	if (fopen_s(&f, file.c_str(), "wb") != 0 || !f) {
		LogW("saves", "could not write the completed objectives next to the save");
		return;
	}
	const bool written = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	fclose(f);
	if (!written) LogW("saves", "could not write the completed objectives next to the save");
}

std::vector<ObjectiveNote> ObjectivesActive()
{
	std::vector<ObjectiveNote> out;
	const std::vector<Held> held = HeldNotes();
	std::vector<std::string> ids;
	for (const Held &h : held) ids.push_back(h.id);
	for (const std::string &id : ObjectiveActiveOrder(GivenOrder(), ids)) {
		for (const Held &h : held) {
			if (h.id == id) {
				out.push_back(Read(h));
				break;
			}
		}
	}
	return out;
}

ObjectiveLog ObjectivesLog()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_log;
}

uint32_t ObjectivesLogVersion()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_version;
}

void ObjectivesMarkSeen(const std::string &id)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_log.Seen(id)) return;
	g_log.MarkSeen(id);
	g_version++;
}

void *NotesDialogOpen()
{
	void *dlg = *reinterpret_cast<void **>(addr::kNotesDialog);
	if (!dlg) return nullptr;
	return reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(dlg, nullptr, kFlagVisible) ? dlg : nullptr;
}

bool NotesDialogClose(void *dlg)
{
	if (!dlg) return false;
	if (!NativeButton(dlg, kIdClose).exists) {
		LogW("ui", "the game's notes dialog has no Close button; the objectives screen gives way to it");
		return false;
	}
	NativeButtonClick(dlg, kIdClose);
	LogI("ui", "the objectives screen closes");
	return true;
}

bool NotesDialogShowOnMap(void *dlg, const std::string &id)
{
	char *list = dlg ? *reinterpret_cast<char **>(static_cast<char *>(dlg) + kDlgList) : nullptr;
	if (!list || !NativeButton(dlg, kIdView).exists) {
		LogW("ui", "objectives: the game's notes dialog has no list or no View location button");
		return false;
	}
	const unsigned count = *reinterpret_cast<unsigned *>(list + kListCount);
	const char *items = *reinterpret_cast<const char *const *>(list + kListItems);
	if (!items || count > kMaxNodes) return false;
	for (unsigned i = 0; i < count; i++) {
		const char *text = *reinterpret_cast<const char *const *>(items + i * kListItemSize);
		if (!text || id != text) continue;
		reinterpret_cast<SelectExclusive_t>(addr::kListSelectExclusive)(list, nullptr, i, 1, 1);
		NativeButtonClick(dlg, kIdView);
		LogI("ui", "objectives: the view goes to a note's place on the map");
		return true;
	}
	LogW("ui", "objectives: the note to show on the map is not in the game's list");
	return false;
}
