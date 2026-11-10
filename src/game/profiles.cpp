// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/profiles.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MinHook.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/engine.h"
#include "core/log.h"
#include "game/civs.h"
#include "game/cmdcheck.h"
#include "game/gametext.h"
#include "hooks/hookcount.h"

namespace {

// menuini/profile.ini
const int kIdNew = 0x1001, kIdRename = 0x1002, kIdDelete = 0x1003, kIdSelect = 0x1004, kIdList = 0x1015, kIdEdit = 0x1101;
// CVXUIProfile: +0x60 the CProfileList it loaded, +0x68 the index chosen in its list.
const int kDlgProfiles = 0x60, kDlgSelected = 0x68;
// CPlayerProfile: +0 the name, +0x1b4 the folder ("profiles/<folder>"). CProfileList: +4 the head of a std::list, +8 its size.
const int kProfileName = 0, kProfileDir = 0x1b4;
// CUISList: +0x58 the number of items.
const int kListCount = 0x58;
const unsigned kMsgButtonClick = 0x12345604;
const int kMaxProfiles = 256;

typedef void(__fastcall *DialogFn_t)(void *dlg);
typedef int(__fastcall *OnShow_t)(void *dlg);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *SelectExclusive_t)(void *list, void *edx, unsigned index, int notify, int redraw);
typedef const char *(__fastcall *SetText_t)(void *self, void *edx, const char *text, int flag);
typedef void(__fastcall *SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int extra);
typedef const char *(__fastcall *IniGetInt_t)(void *ini, void *edx, const char *section, const char *key, int *out);
typedef const char *(__fastcall *IniGetString_t)(void *ini, void *edx, const char *section, const char *key, char *out, size_t size);

std::atomic<void *> g_dialog{nullptr};
OnShow_t g_origOnShow = nullptr;
DialogFn_t g_origDtor = nullptr;

int __fastcall HookOnShow(void *dlg)
{
	const int r = g_origOnShow(dlg);
	if (g_dialog.exchange(dlg) != dlg) LogI("profile", "the player screen opens");
	return r;
}

void __fastcall HookDtor(void *dlg)
{
	void *expected = dlg;
	g_dialog.compare_exchange_strong(expected, nullptr);
	g_origDtor(dlg);
}

void *Child(void *dlg, int id, uintptr_t className)
{
	void **vt = *reinterpret_cast<void ***>(dlg);
	return reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(className));
}

bool Click(void *dlg, int id)
{
	void *button = Child(dlg, id, addr::kClassNameCUIButton);
	if (!button) {
		LogW("profile", "the player screen has no button 0x%x", id);
		return false;
	}
	char *b = static_cast<char *>(button);
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(button))[0x6c / 4])(button, nullptr, *reinterpret_cast<int *>(b + 0x34),
		kMsgButtonClick, *reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
	return true;
}

std::vector<char *> Profiles(void *dlg)
{
	std::vector<char *> out;
	char *list = *reinterpret_cast<char **>(static_cast<char *>(dlg) + kDlgProfiles);
	char *head = list ? *reinterpret_cast<char **>(list + 4) : nullptr;
	if (!head) return out;
	for (char *node = *reinterpret_cast<char **>(head); node && node != head && out.size() < kMaxProfiles; node = *reinterpret_cast<char **>(node)) {
		out.push_back(*reinterpret_cast<char **>(node + 8));
	}
	return out;
}

bool SameDir(const std::string &a, const std::string &b) { return !a.empty() && _stricmp(a.c_str(), b.c_str()) == 0; }

bool Select(void *dlg, int index)
{
	void *list = Child(dlg, kIdList, addr::kClassNameCUISList);
	if (!list || index < 0 || index >= *reinterpret_cast<int *>(static_cast<char *>(list) + kListCount)) return false;
	reinterpret_cast<SelectExclusive_t>(addr::kListSelectExclusive)(list, nullptr, static_cast<unsigned>(index), 0, 1);
	reinterpret_cast<DialogFn_t>(addr::kProfileSelectionChanged)(dlg);
	return *reinterpret_cast<int *>(static_cast<char *>(dlg) + kDlgSelected) == index;
}

// The game's own rename: its button opens the edit box over the chosen row, and finishing it renames the profile or, after
// New, creates it.
bool FinishName(void *dlg, const std::string &name)
{
	void *edit = Child(dlg, kIdEdit, addr::kClassNameCUITextW);
	if (!edit) {
		LogW("profile", "the player screen has no name box");
		return false;
	}
	reinterpret_cast<SetText_t>(addr::kSetTextUtf8)(edit, nullptr, name.c_str(), 1);
	reinterpret_cast<DialogFn_t>(addr::kProfileApplyRename)(dlg);
	return true;
}

bool IniInt(void *ini, const char *key, int &out)
{
	__try {
		return reinterpret_cast<IniGetInt_t>(addr::kHashIniGetInt)(ini, nullptr, "Ranks", key, &out) == nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool IniString(void *ini, const char *key, char *out, size_t size)
{
	out[0] = 0;
	__try {
		return reinterpret_cast<IniGetString_t>(addr::kHashIniGetString)(ini, nullptr, "Ranks", key, out, size) == nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

bool HookAt(uintptr_t at, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(at), detour, orig) == MH_OK && HookEnable(reinterpret_cast<void *>(at)) == MH_OK) return true;
	LogE("profile", "could not hook %s", what);
	return false;
}

}

void *ProfileDialogOpen()
{
	return g_dialog;
}

bool ProfileDialogRead(ProfileDialogState &out)
{
	out = ProfileDialogState();
	void *dlg = g_dialog;
	if (!dlg) return false;
	const char *current = *reinterpret_cast<const char **>(addr::kPlayerProfile);
	const std::string activeDir = current ? GameStringAt(current + kProfileDir) : std::string();
	for (char *profile : Profiles(dlg)) {
		ProfileSlot slot;
		if (profile) {
			slot.name = GameStringAt(profile + kProfileName);
			slot.dir = GameStringAt(profile + kProfileDir);
		}
		if (out.active < 0 && SameDir(slot.dir, activeDir)) out.active = static_cast<int>(out.slots.size());
		out.slots.push_back(slot);
	}
	const int selected = *reinterpret_cast<int *>(static_cast<char *>(dlg) + kDlgSelected);
	out.selected = selected >= 0 && selected < static_cast<int>(out.slots.size()) ? selected : -1;
	return true;
}

bool ProfileDialogSelect(int index)
{
	void *dlg = g_dialog;
	return dlg && Select(dlg, index);
}

bool ProfileDialogCreate(const std::string &name)
{
	void *dlg = g_dialog;
	if (!dlg || name.empty() || !Click(dlg, kIdNew)) return false;
	const size_t before = Profiles(dlg).size();
	const bool ok = FinishName(dlg, name) && Profiles(dlg).size() > before;
	LogAt(ok ? kLogInfo : kLogWarn, "profile", "%s", ok ? "a player was created" : "the game did not create the player");
	return ok;
}

bool ProfileDialogRename(int index, const std::string &name)
{
	void *dlg = g_dialog;
	if (!dlg || name.empty() || !Select(dlg, index) || !Click(dlg, kIdRename)) return false;
	const bool ok = FinishName(dlg, name);
	LogI("profile", "player %d renamed", index + 1);
	return ok;
}

bool ProfileDialogDelete(int index)
{
	void *dlg = g_dialog;
	const size_t before = dlg ? Profiles(dlg).size() : 0;
	// The game deletes the last one too and would leave no player.
	if (before < 2 || !Select(dlg, index) || !Click(dlg, kIdDelete)) return false;
	const bool ok = Profiles(dlg).size() < before;
	LogAt(ok ? kLogInfo : kLogWarn, "profile", "player %d of %d %s", index + 1, static_cast<int>(before), ok ? "deleted" : "not deleted by the game");
	return ok;
}

bool ProfileDialogPlay(int index)
{
	void *dlg = g_dialog;
	if (!dlg || !Select(dlg, index)) return false;
	LogI("profile", "player %d chosen", index + 1);
	return Click(dlg, kIdSelect);
}

std::string ProfileIniPath(const std::string &dir)
{
	std::string rel = dir;
	for (char &c : rel) {
		if (c == '/') c = '\\';
	}
	return GameDir() + "\\" + rel + "\\player.ini";
}

int ProfileSaveCount(const std::string &dir)
{
	const std::string ini = ProfileIniPath(dir);
	const std::string root = ini.substr(0, ini.size() - strlen("player.ini"));
	int count = 0;
	std::vector<std::string> folders = {root};
	for (size_t f = 0; f < folders.size() && f < 16; f++) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((folders[f] + "*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			const std::string file = fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (f == 0 && file != "." && file != "..") folders.push_back(root + file + "\\");
				continue;
			}
			const size_t dot = file.find_last_of('.');
			const std::string ext = dot == std::string::npos ? std::string() : file.substr(dot);
			if (!_stricmp(ext.c_str(), ".snr") || !_stricmp(ext.c_str(), ".adv") || !_stricmp(ext.c_str(), ".con") || !_stricmp(ext.c_str(), ".dmo")) count++;
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	return count;
}

std::vector<RankStep> ProfileGameRanks()
{
	std::vector<RankStep> out;
	void *ini = *reinterpret_cast<void **>(addr::kConstIni);
	int count = 0;
	if (!ini || !IniInt(ini, "RanksCount", count)) return out;
	for (int i = 0; i < count && i < 64; i++) {
		char key[32], name[256];
		RankStep step;
		sprintf_s(key, "RankPoints%d", i);
		if (!IniInt(ini, key, step.points)) break;
		sprintf_s(key, "RankName%d", i);
		if (IniString(ini, key, name, sizeof(name))) step.name = GameText(name);
		out.push_back(step);
	}
	return out;
}

std::string ProfileCivName(int race)
{
	if (race >= 0 && race < 8) return GameRaceText(race);
	if (const Civ *civ = race >= 10 ? CivByIndex(race) : nullptr) return CivDisplayName(*civ);
	return std::string();
}

std::string ProfileUnitName(const std::string &name)
{
	return GameText(name);
}

void InstallProfileHooks(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookAt(addr::kProfileOnShow, reinterpret_cast<void *>(&HookOnShow), reinterpret_cast<void **>(&g_origOnShow), "CVXUIProfile::OnShow") &&
		HookAt(addr::kProfileDtor, reinterpret_cast<void *>(&HookDtor), reinterpret_cast<void **>(&g_origDtor), "CVXUIProfile destructor");
	LogAt(ok ? kLogInfo : kLogError, "profile", "player screen %s", ok ? "connected to the mod's screen" : "not connected");
}
