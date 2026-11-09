// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <set>
#include <algorithm>
#include "MinHook.h"
#include "mods/downloads.h"
#include "mods/lang.h"
#include "i18n/tr.h"
#include "core/log.h"
#include "mods/features.h"
#include "mods/patch.h"
#include "addr/engine.h"
#include "hooks/media.h"
#include "mods/hidefiles.h"
#include "mods/vfs.h"
#include "mods/volumes.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

namespace mods_impl {
namespace {
void *__fastcall RoOpen(void *self, void *edx, const char *path, unsigned mode, void *err)
{
	if (mode & 2) {
		return nullptr;
	}
	typedef void *(__fastcall * Open_t)(void *, void *, const char *, unsigned, void *);
	return reinterpret_cast<Open_t>(g_readOnly.origTransOpen)(self, edx, path, mode, err);
}
const char *__fastcall RoFail1(void *, void *, const char *) { return kReadOnlyError; }
const char *__fastcall RoFail2(void *, void *, const char *, const void *) { return kReadOnlyError; }
const char *__fastcall RoFail3(void *, void *, const char *, const void *, const void *) { return kReadOnlyError; }

void *GameNew(size_t n) { return reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(n); }

void *CreateLooseLayer(const std::string &prefix)
{
	void *winfs = GameNew(4);
	void *trans = GameNew(0x108);
	if (!winfs || !trans || prefix.size() >= 0x100) {
		return nullptr;
	}
	reinterpret_cast<Ctor0_t>(addr::kCWinFileServiceCtor)(winfs);
	reinterpret_cast<CTransFSCtor_t>(addr::kCTransFSCtor)(trans, nullptr, prefix.c_str(), winfs);
	if (!g_readOnly.vtableReady) {
		memcpy(g_readOnly.vtable, *reinterpret_cast<void ***>(trans), sizeof(g_readOnly.vtable));
		g_readOnly.origTransOpen = g_readOnly.vtable[3];
		g_readOnly.vtable[3] = reinterpret_cast<void *>(&RoOpen);
		g_readOnly.vtable[5] = reinterpret_cast<void *>(&RoFail1);
		g_readOnly.vtable[6] = reinterpret_cast<void *>(&RoFail2);
		g_readOnly.vtable[7] = reinterpret_cast<void *>(&RoFail2);
		g_readOnly.vtable[8] = reinterpret_cast<void *>(&RoFail2);
		g_readOnly.vtable[13] = reinterpret_cast<void *>(&RoFail3);
		g_readOnly.vtableReady = true;
	}
	*reinterpret_cast<void ***>(trans) = g_readOnly.vtable;
	return trans;
}

void ListPushFront(void *list, void *value)
{
	auto node = static_cast<void **>(GameNew(8));
	if (!node) {
		return;
	}
	char *l = static_cast<char *>(list);
	node[0] = value;
	node[1] = *reinterpret_cast<void **>(l + 0x10);
	*reinterpret_cast<void **>(l + 0x10) = node;
	*reinterpret_cast<int *>(l + 0xc) += 1;
}

bool ListUnlink(void *list, void *value)
{
	char *l = static_cast<char *>(list);
	void **link = reinterpret_cast<void **>(l + 0x10);
	while (*link) {
		void **node = static_cast<void **>(*link);
		if (node[0] == value) {
			*link = node[1];
			*reinterpret_cast<int *>(l + 0xc) -= 1;
			return true;
		}
		link = &node[1];
	}
	return false;
}

bool ContainsFolder(const std::vector<std::string> &list, const std::string &folder)
{
	return std::find_if(list.begin(), list.end(), [&](const std::string &f) { return _stricmp(f.c_str(), folder.c_str()) == 0; }) != list.end();
}

std::string ReadGameLanguage()
{
	const char *base = reinterpret_cast<const char *>(kLanguageName);
	const unsigned cap = *reinterpret_cast<const unsigned *>(base + 0x18);
	const unsigned size = *reinterpret_cast<const unsigned *>(base + 0x14);
	const char *text = cap < 0x10 ? base + 4 : *reinterpret_cast<const char *const *>(base + 4);
	if (!text || size == 0 || size > 64) {
		return "";
	}
	return Lower(std::string(text, size));
}

std::string ShowStartupPicker(const std::string &current);

std::atomic<const std::map<std::string, std::string> *> g_chosenFiles{nullptr};

void PublishChosenFiles(const ModProfile &p)
{
	const std::map<std::string, std::string> chosen = ChosenFolders(p, ModsList());
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_mount.chosen = chosen;
	}
	if (chosen.empty()) return;
	auto *open = new std::map<std::string, std::string>();
	const std::string lang = LanguageLower();
	std::string layerFolder, layerPath;
	{
		ModInfo owner;
		if (!p.ruleset.Empty() && ModsFind(p.ruleset.pack, owner)) {
			if (const ModLayer *layer = ModsActiveLayer(p, owner)) {
				layerFolder = owner.folder;
				layerPath = layer->path;
			}
		}
	}
	for (const auto &c : chosen) {
		std::vector<std::string> tries;
		if (StartsWith(c.first, "currentlang/")) tries.push_back("mods/" + c.second + "/local/" + lang + "/" + c.first.substr(12));
		if (!layerPath.empty() && _stricmp(layerFolder.c_str(), c.second.c_str()) == 0) tries.push_back("mods/" + c.second + "/" + layerPath + "/" + c.first);
		tries.push_back("mods/" + c.second + "/" + c.first);
		for (const std::string &t : tries) {
			std::string disk = GameDir() + "\\" + t;
			std::replace(disk.begin(), disk.end(), '/', '\\');
			const DWORD a = GetFileAttributesA(disk.c_str());
			if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			(*open)[Lower(c.first)] = t;
			LogD("content", "%s comes from %s (chosen by the player)", c.first.c_str(), c.second.c_str());
			break;
		}
	}
	LogI("content", "%u of %u shared files come from the pack the player chose", static_cast<unsigned>(open->size()), static_cast<unsigned>(chosen.size()));
	g_chosenFiles.store(open);
}

std::atomic<const std::map<std::string, std::string> *> g_volumeFiles{nullptr};
}

void PublishVolumeFiles(const ModProfile &p)
{
	auto *files = new std::map<std::string, std::string>();
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (!ModsFind(folder, m) || !m.forbidden.empty()) continue;
		// The first pack of the profile wins, and a ruleset before its package, as the loose layers are put in front of the engine's list.
		auto add = [&](const std::vector<std::string> &loose, const std::string &sub) {
			for (const std::string &path : loose) {
				if (VolumeOf(path)) files->emplace(path, "mods/" + m.folder + "/" + sub + path);
			}
		};
		if (const ModLayer *layer = ModsActiveLayer(p, m)) add(layer->loosePaths, layer->path + "/");
		add(m.loosePaths, "");
	}
	LogI("content", "%u files of the packs answer to a name with a volume (gameres/, gameini/...)", static_cast<unsigned>(files->size()));
	g_volumeFiles.store(files);
}

void PublishLayerDiag(const ModProfile &p)
{
	ModInfo owner;
	const ModLayer *layer = !p.ruleset.Empty() && ModsFind(p.ruleset.pack, owner) ? ModsActiveLayer(p, owner) : nullptr;
	if (!layer) {
		VfsLayerDiagReset("", "", {});
		return;
	}
	std::string dir = ModsDir() + "\\" + owner.folder + "\\" + layer->path;
	std::replace(dir.begin(), dir.end(), '/', '\\');
	VfsLayerDiagReset(owner.folder + "/" + layer->id, dir, layer->loosePaths);
}

namespace {
void LogValueClashes(const ModProfile &p)
{
	for (const ValueClash &c : ClashesOf(p, ModsList()).values) {
		LogI("patch", "%s and %s set %u of the same values (such as %s in %s): the ones from %s are used", c.used.c_str(), c.lost.c_str(),
			static_cast<unsigned>(c.values.size()), c.values[0].label.c_str(), c.values[0].file.c_str(), c.used.c_str());
	}
}

void MountProfileLayers(void *list)
{
	ModsApplyPendingSwaps();
	DlApplyPendingInstalls();
	DlCleanLeftovers();
	ModsApplyPendingUninstalls();
	ModsScan();
	ModsPruneMissing();
	std::string name = ModsActiveProfileName();
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
		name = ShowStartupPicker(name);
		ModsSetActiveProfileName(name);
	}
	ModProfile p = ModsLoadProfile(name);
	p.mods = ModsLoadOrder(p.mods);
	{
		const ModRuleset effective = ModsEffectiveRuleset(p);
		if (!p.ruleset.pack.empty() && effective.pack.empty()) {
			LogW("content", "the profile asks for ruleset \"%s\" of \"%s\" but it cannot be mounted (the pack is not enabled or installed, or it has no such ruleset)",
				p.ruleset.id.empty() ? "(none)" : p.ruleset.id.c_str(), p.ruleset.pack.c_str());
		}
		p.ruleset = effective;
	}
	std::map<std::string, std::string> signatures;
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (ModsFind(folder, m)) signatures[Lower(folder)] = ModSignature(m);
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_mount.profile = p;
		g_mount.choice = p.ruleset;
		g_mount.known = true;
		g_mount.signatures = signatures;
		g_mount.layerList = list;
	}
	if (p.mods.empty()) {
		LogI("content", "profile \"%s\" (no mods)", p.name.c_str());
		PatchRebuild();
		ModsRebuildFeatures();
		return;
	}
	const std::string lang = LanguageLower();
	std::vector<std::string> looseLayers;
	// sub is "" for a package's root or "rulesets/<id>/" for one of its rulesets: both are mounted the same way.
	auto mountRoot = [&](const std::string &folder, const std::string &sub, bool hasLoose) {
		std::string diskSub = sub;
		std::replace(diskSub.begin(), diskSub.end(), '/', '\\');
		const std::string vfsBase = "mods/" + folder + "/" + sub;
		const std::string diskBase = ModsDir() + "\\" + folder + "\\" + diskSub;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_mount.dirs.push_back(diskSub.empty() ? ModsDir() + "\\" + folder : diskBase.substr(0, diskBase.size() - 1));
		}
		if (IsDir(diskBase + "Local")) {
			g_origMountPakFilesInDir((vfsBase + "Local/").c_str(), list, lang.c_str());
			g_mount.localDirs.push_back(diskBase + "Local");
		}
		if (IsDir(diskBase + "Patches")) {
			g_origMountPakFilesInDir((vfsBase + "Patches/").c_str(), list, nullptr);
		}
		if (IsDir(diskBase + "Packs")) {
			g_origMountPakFilesInDir((vfsBase + "Packs/").c_str(), list, nullptr);
		}
		if (hasLoose) {
			looseLayers.push_back(vfsBase);
		}
	};
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (!ModsFind(folder, m)) {
			LogW("content", "the profile uses \"%s\" but it is not in the mods folder", folder.c_str());
			continue;
		}
		if (!m.forbidden.empty()) {
			LogW("content", "%s is NOT loaded: it brings disallowed files (%s...)", folder.c_str(), m.forbidden[0].c_str());
			continue;
		}
		// The chosen ruleset goes right above its package: where both carry a file, the ruleset's is the one used.
		if (!p.ruleset.Empty() && _stricmp(folder.c_str(), p.ruleset.pack.c_str()) == 0) {
			if (const ModLayer *layer = ModsFindLayer(m, p.ruleset.id)) {
				mountRoot(folder, layer->path + "/", layer->hasLoose);
				LogI("content", "ruleset \"%s\" of %s mounted (%u files%s)", layer->id.c_str(), m.name.c_str(), layer->fileCount,
					layer->gameplay ? ", rules type" : "");
			}
		}
		mountRoot(folder, "", m.hasLoose);
		LogI("content", "loaded %s %s (%u files%s%s)", m.name.c_str(), m.version.c_str(), m.fileCount,
			m.hasLoose ? ", with loose files" : "", ModsIsRulesPack(m) ? ", rules type" : m.kind == kModContent ? ", content type" : ", cosmetic type");
	}
	for (auto it = looseLayers.rbegin(); it != looseLayers.rend(); ++it) {
		if (void *layer = CreateLooseLayer(*it)) {
			ListPushFront(list, layer);
			std::lock_guard<std::mutex> lock(g_lock);
			g_mount.looseLayers[Lower(*it)] = layer;
		}
	}
	LogI("content", "profile \"%s\" mounted (%u mods)", p.name.c_str(), static_cast<unsigned>(p.mods.size()));
	PrepareRleMmp(p, ModsList());
	ApplyHiddenFiles(p);
	{
		std::vector<std::string> dirs;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			dirs = g_mount.dirs;
		}
		MediaSetModDirs(dirs);
	}
	PublishChosenFiles(p);
	PublishVolumeFiles(p);
	PublishLayerDiag(p);
	PatchRebuild();
	LogValueClashes(p);
	ModsRebuildFeatures();
}
}
}

const std::string *ModsVolumeFile(const std::string &rel)
{
	const std::map<std::string, std::string> *files = g_volumeFiles.load();
	if (!files || files->empty()) return nullptr;
	const auto it = files->find(rel);
	return it == files->end() ? nullptr : &it->second;
}

const std::string *ModsChosenFile(const std::string &rel)
{
	const std::map<std::string, std::string> *files = g_chosenFiles.load();
	if (!files) return nullptr;
	const auto it = files->find(rel);
	return it == files->end() ? nullptr : &it->second;
}

bool ModsApplyWithoutRestart()
{
	void *list = nullptr;
	ModProfile mounted;
	std::map<std::string, std::string> chosen;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_mount.known || !g_mount.layerList) return false;
		list = g_mount.layerList;
		mounted = g_mount.profile;
		chosen = g_mount.chosen;
	}
	ModProfile active = ModsLoadProfile(ModsActiveProfileName());
	active.mods.erase(std::remove_if(active.mods.begin(), active.mods.end(), [](const std::string &f) {
		ModInfo m;
		return !ModsFind(f, m);
	}), active.mods.end());
	std::vector<std::string> added, removed;
	for (const std::string &f : active.mods) {
		if (!ContainsFolder(mounted.mods, f)) added.push_back(f);
	}
	for (const std::string &f : mounted.mods) {
		if (!ContainsFolder(active.mods, f)) removed.push_back(f);
	}
	if (added.empty() && removed.empty()) return false;
	if (!ModsSameRuleset(ModsEffectiveRuleset(active), mounted.ruleset)) return false;
	if (!active.choices.empty() && ChosenFolders(active, ModsList()) != chosen) return false;
	std::vector<ModInfo> addedInfo;
	for (const std::string &f : added) {
		ModInfo m;
		if (!ModsLoadsWithoutRestart(f) || !ModsFind(f, m) || !m.hasLoose) return false;
		addedInfo.push_back(m);
	}
	for (const std::string &f : removed) {
		if (!ModsLoadsWithoutRestart(f)) return false;
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_mount.looseLayers.count(Lower("mods/" + f + "/"))) return false;
	}
	for (const std::string &f : removed) {
		void *layer = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			const auto it = g_mount.looseLayers.find(Lower("mods/" + f + "/"));
			layer = it->second;
			g_mount.looseLayers.erase(it);
			g_mount.signatures.erase(Lower(f));
			const std::string dir = ModsDir() + "\\" + f;
			auto &dirs = g_mount.dirs;
			dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const std::string &d) { return _stricmp(d.c_str(), dir.c_str()) == 0; }), dirs.end());
		}
		ListUnlink(list, layer);
		LogI("content", "%s unloaded without restarting", f.c_str());
	}
	std::vector<std::string> failed;
	for (const ModInfo &m : addedInfo) {
		void *layer = CreateLooseLayer("mods/" + m.folder + "/");
		if (!layer) {
			failed.push_back(m.folder);
			LogW("content", "%s could not be loaded without restarting", m.folder.c_str());
			continue;
		}
		ListPushFront(list, layer);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_mount.looseLayers[Lower("mods/" + m.folder + "/")] = layer;
			g_mount.signatures[Lower(m.folder)] = ModSignature(m);
			g_mount.dirs.push_back(ModsDir() + "\\" + m.folder);
		}
		LogI("content", "loaded %s %s without restarting", m.name.c_str(), m.version.c_str());
	}
	for (const std::string &f : failed) active.mods.erase(std::remove(active.mods.begin(), active.mods.end(), f), active.mods.end());
	active.mods = ModsLoadOrder(active.mods);
	std::vector<std::string> dirs;
	active.ruleset = mounted.ruleset;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_mount.profile = active;
		dirs = g_mount.dirs;
	}
	MediaSetModDirs(dirs);
	PublishVolumeFiles(active);
	return failed.empty();
}

namespace mods_impl {
void __cdecl HookMountPakFilesInDir(const char *dir, void *list, const char *lang)
{
	static bool done = false;
	if (!done && dir && _stricmp(dir, "Patches/") == 0 && list) {
		done = true;
		std::string gameLang = ReadGameLanguage();
		if (!gameLang.empty()) {
			g_language = gameLang;
		}
		MountProfileLayers(list);
	}
	g_origMountPakFilesInDir(dir, list, lang);
	if (dir && list && lang && _stricmp(dir, "Local/") == 0) {
		const std::string base = LangFallbackFor(Lower(lang));
		if (!base.empty()) {
			g_origMountPakFilesInDir(dir, list, base.c_str());
			LogI("language", "%s does not come with the game; whatever it lacks is taken from %s", Lower(lang).c_str(), base.c_str());
		}
	}
}
}

namespace mods_impl {
namespace {
struct PickerState {
	HWND list = nullptr;
	bool done = false;
	int result = -1;
};
PickerState *g_picker = nullptr;

LRESULT CALLBACK PickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_COMMAND:
		if (LOWORD(wp) == 1 || (LOWORD(wp) == 10 && HIWORD(wp) == LBN_DBLCLK)) {
			g_picker->result = static_cast<int>(SendMessageA(g_picker->list, LB_GETCURSEL, 0, 0));
			g_picker->done = true;
		}
		else if (LOWORD(wp) == 2) {
			g_picker->done = true;
		}
		return 0;
	case WM_CLOSE:
		g_picker->done = true;
		return 0;
	}
	return DefWindowProcA(hwnd, msg, wp, lp);
}

std::string ShowStartupPicker(const std::string &current)
{
	const std::vector<std::string> names = ModsProfileNames();
	WNDCLASSA wc = {};
	wc.lpfnWndProc = PickerProc;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
	wc.lpszClassName = "ImperivmModPicker";
	RegisterClassA(&wc);
	const int w = 380, h = 330;
	HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, Tr("Imperivm - Content profile").c_str(),
		WS_CAPTION | WS_SYSMENU | WS_VISIBLE, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, wc.hInstance, nullptr);
	if (!hwnd) {
		return current;
	}
	HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	PickerState st;
	g_picker = &st;
	HWND label = CreateWindowA("STATIC", Tr("Choose which content profile to start with:").c_str(), WS_CHILD | WS_VISIBLE, 12, 10, 340,
		20, hwnd, nullptr, nullptr, nullptr);
	st.list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 12, 34,
		340, 210, hwnd, reinterpret_cast<HMENU>(10), nullptr, nullptr);
	HWND ok = CreateWindowA("BUTTON", Tr("Play").c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 152, 252, 96, 28, hwnd,
		reinterpret_cast<HMENU>(1), nullptr, nullptr);
	HWND cancel = CreateWindowA("BUTTON", Tr("No changes").c_str(), WS_CHILD | WS_VISIBLE, 256, 252, 96, 28, hwnd,
		reinterpret_cast<HMENU>(2), nullptr, nullptr);
	for (HWND c : {label, st.list, ok, cancel}) {
		SendMessageA(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	}
	int sel = 0;
	for (size_t i = 0; i < names.size(); i++) {
		const std::string label = ModsProfileLabel(names[i]);
		SendMessageA(st.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
		if (names[i] == current) {
			sel = static_cast<int>(i);
		}
	}
	SendMessageA(st.list, LB_SETCURSEL, sel, 0);
	SetForegroundWindow(hwnd);
	SetFocus(st.list);
	MSG m;
	while (!st.done && GetMessageA(&m, nullptr, 0, 0) > 0) {
		if (!IsDialogMessageA(hwnd, &m)) {
			TranslateMessage(&m);
			DispatchMessageA(&m);
		}
	}
	DestroyWindow(hwnd);
	g_picker = nullptr;
	if (st.result >= 0 && st.result < static_cast<int>(names.size())) {
		LogD("content", "profile chosen at startup: %s", names[st.result].c_str());
		return names[st.result];
	}
	return current;
}
}
}

namespace mods_impl {
bool MountLooseFolder(const std::string &vfsPrefix)
{
	void *list = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_mount.known || !g_mount.layerList || g_mount.looseLayers.count(Lower(vfsPrefix))) return false;
		list = g_mount.layerList;
	}
	void *layer = CreateLooseLayer(vfsPrefix);
	if (!layer) return false;
	ListPushFront(list, layer);
	std::lock_guard<std::mutex> lock(g_lock);
	g_mount.looseLayers[Lower(vfsPrefix)] = layer;
	return true;
}

// The layer object is left allocated (the game has no call to free it, and the engine may still hold a name it resolved with it).
bool UnmountLooseFolder(const std::string &vfsPrefix)
{
	void *list = nullptr, *layer = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const auto it = g_mount.looseLayers.find(Lower(vfsPrefix));
		if (it == g_mount.looseLayers.end() || !g_mount.layerList) return false;
		list = g_mount.layerList;
		layer = it->second;
		g_mount.looseLayers.erase(it);
	}
	return ListUnlink(list, layer);
}
}

namespace mods_impl {
// The game files the packs of the profile hide (their hidden.txt, the ruleset's too), except the ones a pack provides itself.
void ApplyHiddenFiles(const ModProfile &p)
{
	std::vector<std::vector<std::string>> hides;
	std::set<std::string> provided;
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (!ModsFind(folder, m) || !m.forbidden.empty()) continue;
		if (const ModLayer *layer = ModsActiveLayer(p, m)) {
			if (!layer->hidden.empty()) hides.push_back(layer->hidden);
			provided.insert(layer->paths.begin(), layer->paths.end());
		}
		if (!m.hidden.empty()) hides.push_back(m.hidden);
		provided.insert(m.paths.begin(), m.paths.end());
	}
	const std::set<std::string> hidden = HiddenEffective(hides, provided);
	VfsSetHidden(hidden);
	if (!hidden.empty()) LogI("content", "%u game files hidden by the active content packs", static_cast<unsigned>(hidden.size()));
}
}
