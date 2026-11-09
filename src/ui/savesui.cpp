// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "ui/modui.h"
#include "game/autosave.h"
#include "mods/mods.h"
#include "net/mpsave.h"
#include "ui/mpui.h"
#include "mods/savelib.h"
#include "ui/savesui.h"
#include "ui/textedit.h"
#include "ui/widgets.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"

using namespace ui;

namespace {
const int kDesignW = 1000, kDesignH = 640;
const int kLX = 16, kLY = 140, kLW = 580, kLH = 436;
const int kDX = 612, kDY = 140, kDW = 372, kDH = 436;
const int kRowSingle = 46, kRowHeader = 34, kRowChild = 32;
const int kTabCount = 5;
const char *const kTabNames[kTabCount] = {"Single player", "Adventure", "Conquest", "Replays", "Multiplayer"};

typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef unsigned(__fastcall *GetText_t)(void *self, void *edx, char *buf, unsigned size);
typedef const char *(__fastcall *SetText_t)(void *self, void *edx, const char *text, int flag);
typedef const char *(__fastcall *DoSave_t)(void *dlg);
typedef void(__fastcall *Close_t)(void *dlg, void *edx, int arg);
typedef const char *(__cdecl *ShowSave_t)(int, int);
typedef void *(__fastcall *Assign_t)(void *self, void *edx, const char *text, unsigned len);

enum Action {
	kNone = 0,
	kClose,
	kTab,
	kRow,
	kHeader,
	kSort,
	kShowAuto,
	kLoad,
	kRename,
	kDelete,
	kRenameOk,
	kRenameCancel,
	kDelOne,
	kHeaderToggle,
	kBlank,
	kDelNo,
	kSearchField,
	kRenameField,
	kClearSearch,
	kSave,
	kSaveOver,
	kNameField,
};

enum Focus { kFocusNone = 0, kFocusSearch = 1, kFocusRename = 2, kFocusName = 3 };

struct ModCheck {
	std::vector<std::string> missing;
	std::vector<std::string> other;
	std::vector<std::string> inactive;
	std::vector<std::string> extra;
	bool differs = false;
	bool blocked = false;
	std::vector<std::string> rulesFolders;
	ModRuleset ruleset;
	bool needsActivation = false;
	// Every package the game needs is enabled and only the ruleset is not the one it was saved with.
	bool rulesetOnly = true;
	bool warn = false;
};

struct State {
	bool open = false;
	int tab = 0;
	bool showAuto = false;
	int sort = 0;
	std::vector<SaveRec> recs;
	std::string selPath;
	std::string selGroup;
	std::set<std::string> sel;
	std::string anchor;
	std::vector<SaveRec> delList;
	int delRows = 0;
	std::string delTitle;
	std::set<std::string> collapsed;
	ListState list;
	ListGeom listGeom;
	bool mouseDown = false;
	std::string search, renameText, error;
	int focus = kFocusNone;
	EditState edit;
	bool renaming = false;
	bool renameGroup = false;
	std::map<std::string, std::string> gameNames;
	int confirm = 0;
	std::string pendingOnlineRoom;
	int mode = 0;
	void *dlg = nullptr;
	uintptr_t dlgGlobal = 0;
	int saveKind = kSaveStrategy;
	std::vector<SaveRec> pendingDelete;
	bool busy = false;
	std::string nameText, pendingSave;
	bool pendingSaveOnline = false;
	bool escArmed = false, escFire = false;
	std::string defaultName;
	bool pendingCancel = false;
	int mouseX = 0, mouseY = 0;
	MpSaveInfo info;
	std::string activeHash, activeProfile;
	mutable std::map<std::string, ModCheck> modChecks;
	mutable DWORD modChecksTick = 0;
	int tabAction = -1, tabArg = 0;
	std::vector<Hit> hits;
	std::vector<std::string> rowPaths, hdrKeys, hdrPaths;
	std::string pendingLoad;
	DWORD closeAt = 0;
	ListState modList;
	ListGeom modGeom;
	std::string modListKey;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string ModLabel(const SaveMod &m)
{
	std::string s = !m.name.empty() ? m.name : !m.id.empty() ? m.id : m.folder;
	if (!m.version.empty() && Lower(s).find(Lower(m.version)) == std::string::npos) s += " " + m.version;
	return s;
}

ModCheck ComputeModCheck(const State &st, const SaveRec &r)
{
	ModCheck c;
	const bool sameSchema = r.modSchema == ModsRulesSchema();
	c.differs = sameSchema && !r.modHash.empty() && !st.activeHash.empty() && r.modHash != st.activeHash;
	const std::vector<ModInfo> installed = ModsList();
	const ModProfile &mounted = ModsMountedProfile();
	if (c.differs) {
		for (const std::string &f : mounted.mods) {
			const ModInfo *on = nullptr;
			for (const ModInfo &i : installed) {
				if (_stricmp(i.folder.c_str(), f.c_str()) == 0) on = &i;
			}
			if (!on || !(on->gameplay || !ModsRulesetOf(mounted.ruleset, on->folder).empty())) continue;
			bool inSave = false;
			for (const SaveMod &m : r.mods) {
				inSave = inSave || (!m.id.empty() && _stricmp(on->id.c_str(), m.id.c_str()) == 0) ||
					(!m.folder.empty() && _stricmp(on->folder.c_str(), m.folder.c_str()) == 0);
			}
			if (!inSave) c.extra.push_back(on->name + (on->version.empty() ? "" : " " + on->version));
		}
	}
	if (r.mods.empty()) {
		c.warn = c.differs || !c.extra.empty();
		return c;
	}
	for (const SaveMod &m : r.mods) {
		const ModInfo *found = nullptr;
		for (const ModInfo &i : installed) {
			if ((!m.id.empty() && _stricmp(i.id.c_str(), m.id.c_str()) == 0) || (!m.folder.empty() && _stricmp(i.folder.c_str(), m.folder.c_str()) == 0)) {
				found = &i;
				break;
			}
		}
		const bool rules = !m.ruleset.empty() || (m.kind >= 0 ? m.kind == kModRules : m.oldGameplay);
		if (!found) {
			if (rules) c.missing.push_back(ModLabel(m));
			continue;
		}
		if (!found->gameplay && m.ruleset.empty() && ModsRulesetOf(mounted.ruleset, found->folder).empty()) continue;
		c.rulesFolders.push_back(found->folder);
		const ModLayer *savedLayer = ModsFindLayer(*found, m.ruleset);
		if (savedLayer) {
			c.ruleset.pack = found->folder;
			c.ruleset.id = savedLayer->id;
		}
		bool active = false;
		for (const std::string &f : mounted.mods) active = active || _stricmp(f.c_str(), found->folder.c_str()) == 0;
		// Loaded with the ruleset it was saved with, or with none if it was saved with none.
		const bool sameRuleset = _stricmp(ModsRulesetOf(mounted.ruleset, found->folder).c_str(), m.ruleset.c_str()) == 0;
		const bool otherVersion = !m.version.empty() && !found->version.empty() && ModsCompareVersions(m.version, found->version) != 0;
		const bool otherFiles = sameSchema && !m.hash.empty() && !found->gameplayHash.empty() && m.hash != found->gameplayHash;
		const bool otherLayerFiles = sameSchema && savedLayer && !m.rulesetHash.empty() && !savedLayer->gameplayHash.empty() && m.rulesetHash != savedLayer->gameplayHash;
		if (otherVersion) c.other.push_back(ModLabel(m) + Tr(" (you have ") + found->version + ")");
		else if (!m.ruleset.empty() && !savedLayer) c.other.push_back(ModLabel(m) + Tr(" (without its ruleset)"));
		else if (otherFiles || otherLayerFiles) c.other.push_back(ModLabel(m) + Tr(" (different files)"));
		else if (!active || !sameRuleset) {
			std::string label = ModLabel(m);
			if (!sameRuleset) label += " \xC2\xB7 " + (savedLayer ? savedLayer->name : Tr("Original (HD)"));
			if (!active) c.rulesetOnly = false;
			c.inactive.push_back(label);
		}
	}
	c.blocked = !c.missing.empty();
	c.needsActivation = c.missing.empty() && !c.inactive.empty();
	c.warn = c.differs || c.needsActivation || !c.other.empty() || !c.extra.empty();
	return c;
}

ModCheck CheckFor(const State &st, const SaveRec &r)
{
	const DWORD now = GetTickCount();
	if (now - st.modChecksTick > 2000) {
		st.modChecks.clear();
		st.modChecksTick = now;
	}
	auto it = st.modChecks.find(r.vfsPath);
	if (it == st.modChecks.end()) it = st.modChecks.emplace(r.vfsPath, ComputeModCheck(st, r)).first;
	return it->second;
}

std::string Join(const std::vector<std::string> &v, const char *sep = ", ")
{
	std::string s;
	for (const std::string &x : v) s += (s.empty() ? "" : sep) + x;
	return s;
}

std::string CampaignOf(const State &st, const SaveRec &rec)
{
	if (!rec.campaign.empty() || (rec.kind != kSaveAdventure && rec.kind != kSaveConquest)) return rec.campaign;
	const std::string key = SaveLibGroupKey(rec);
	for (const SaveRec &r : st.recs) {
		if (r.kind == rec.kind && !r.campaign.empty() && SaveLibGroupKey(r) == key) return r.campaign;
	}
	return "";
}

std::string TimeText(uint64_t ft)
{
	FILETIME f = {static_cast<DWORD>(ft), static_cast<DWORD>(ft >> 32)}, l;
	SYSTEMTIME s;
	if (!FileTimeToLocalFileTime(&f, &l) || !FileTimeToSystemTime(&l, &s)) return "";
	char buf[48];
	sprintf_s(buf, "%02d/%02d/%04d %02d:%02d", s.wDay, s.wMonth, s.wYear, s.wHour, s.wMinute);
	return buf;
}

bool TabMatches(const SaveRec &r, int tab)
{
	switch (tab) {
	case 0: return r.kind == kSaveStrategy && !r.online;
	case 1: return r.kind == kSaveAdventure;
	case 2: return r.kind == kSaveConquest;
	case 3: return r.kind == kSaveReplay;
	default: return r.kind == kSaveStrategy && r.online;
	}
}

bool ListMatches(const State &, const SaveRec &r, int tab)
{
	return TabMatches(r, tab);
}

const SaveRec *FindRec(const State &st, const std::string &path)
{
	const std::string want = Lower(path);
	for (const SaveRec &r : st.recs) {
		if (Lower(r.vfsPath) == want) return &r;
	}
	return nullptr;
}

struct Row {
	int kind = 0;
	int rec = -1;
	std::string key;
	std::string title;
	int count = 0;
	bool collapsed = false;
	int y = 0, h = 0;
};

struct View {
	std::vector<Row> rows;
	int total = 0;
	int counts[kTabCount] = {};
};

std::string PartidaTitle(const State &st, const std::string &key, int kind)
{
	const auto it = st.gameNames.find(key);
	if (it != st.gameNames.end()) return it->second;
	for (auto r = st.recs.rbegin(); r != st.recs.rend(); ++r) {
		if (r->kind == kind && SaveLibGroupKey(*r) == key) return Tr("Game from ") + TimeText(r->time);
	}
	return "";
}

View BuildView(const State &st)
{
	View v;
	for (const SaveRec &r : st.recs) {
		if (!st.showAuto && r.autosave) continue;
		for (int t = 0; t < kTabCount; t++) {
			if (ListMatches(st, r, t)) v.counts[t]++;
		}
	}
	struct Group {
		std::string key, title;
		std::vector<int> recs;
	};
	std::vector<Group> groups;
	std::map<std::string, size_t> index;
	const std::string needle = Lower(st.search);
	for (size_t i = 0; i < st.recs.size(); i++) {
		const SaveRec &r = st.recs[i];
		if (!ListMatches(st, r, st.tab) || (!st.showAuto && r.autosave)) continue;
		if (!needle.empty() && Lower(r.name).find(needle) == std::string::npos && Lower(r.campaign).find(needle) == std::string::npos) continue;
		const std::string key = SaveLibGroupKey(r);
		auto it = index.find(key);
		if (it == index.end()) {
			index[key] = groups.size();
			groups.push_back({key, "", {}});
			it = index.find(key);
		}
		groups[it->second].recs.push_back(static_cast<int>(i));
	}
	for (Group &g : groups) g.title = PartidaTitle(st, g.key, st.recs[g.recs[0]].kind);
	if (st.sort == 1) {
		std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) { return Lower(a.title) < Lower(b.title); });
	}
	int y = 0;
	auto add = [&](Row r, int h) {
		r.y = y;
		r.h = h;
		y += h;
		v.rows.push_back(r);
	};
	for (const Group &g : groups) {
		Row r;
		r.key = g.key;
		r.count = static_cast<int>(g.recs.size());
		const bool legacy = g.key.compare(0, 2, "l:") == 0;
		if (g.recs.size() == 1 && legacy) {
			r.kind = 0;
			r.rec = g.recs[0];
			add(r, kRowSingle);
			continue;
		}
		r.kind = 1;
		r.title = g.title;
		r.rec = g.recs[0];
		r.collapsed = st.collapsed.count(g.key) != 0;
		add(r, kRowHeader);
		if (r.collapsed) continue;
		for (int i : g.recs) {
			Row c;
			c.kind = 2;
			c.rec = i;
			c.key = g.key;
			add(c, kRowChild);
		}
	}
	v.total = y;
	return v;
}

std::string RowId(const State &st, const Row &r)
{
	return r.kind == 1 ? "g:" + r.key : "p:" + Lower(st.recs[r.rec].vfsPath);
}

std::string PrimaryId(const State &st)
{
	if (!st.selGroup.empty()) return "g:" + st.selGroup;
	return st.selPath.empty() ? "" : "p:" + st.selPath;
}

bool RowSelected(const State &st, const Row &r)
{
	return r.rec >= 0 && st.sel.count(RowId(st, r)) != 0;
}

void FixSelection(State &st)
{
	const View v = BuildView(st);
	std::set<std::string> visible;
	for (const Row &r : v.rows) visible.insert(RowId(st, r));
	for (auto it = st.sel.begin(); it != st.sel.end();) it = visible.count(*it) ? std::next(it) : st.sel.erase(it);
	if (!visible.count(st.anchor)) st.anchor.clear();
	const std::string prim = PrimaryId(st);
	if (visible.count(prim)) {
		if (st.sel.empty()) st.sel.insert(prim);
		if (st.sel.count(prim)) return;
	}
	st.selPath.clear();
	st.selGroup.clear();
	st.info = MpSaveInfo();
	if (st.mode == 1 && st.sel.empty()) {
		st.anchor.clear();
		return;
	}
	for (const Row &r : v.rows) {
		if (!st.sel.empty() && !RowSelected(st, r)) continue;
		st.selPath = Lower(st.recs[r.rec].vfsPath);
		if (r.kind == 1) st.selGroup = r.key;
		st.sel.insert(RowId(st, r));
		break;
	}
	if (st.anchor.empty()) st.anchor = PrimaryId(st);
	st.renaming = false;
	st.error.clear();
}

void ClearSelection(State &st)
{
	st.selPath.clear();
	st.selGroup.clear();
	st.sel.clear();
	st.anchor.clear();
	st.info = MpSaveInfo();
}

void UpdatePlayers(State &st)
{
	st.info = MpSaveInfo();
	const SaveRec *r = FindRec(st, st.selPath);
	if (!r || r->kind != kSaveStrategy) return;
	st.info = MpSaveReadInfo(r->vfsPath);
}

void SetPrimary(State &st, const std::string &path, const std::string &group)
{
	const std::string want = Lower(path);
	st.selGroup = group;
	if (want == st.selPath) return;
	st.selPath = want;
	st.renaming = false;
	st.error.clear();
	if (st.focus == kFocusRename) st.focus = kFocusNone;
	UpdatePlayers(st);
}

void Select(State &st, const std::string &path)
{
	SetPrimary(st, path, "");
	st.sel = {"p:" + st.selPath};
	st.anchor = "p:" + st.selPath;
}

void SelectGroup(State &st, const std::string &key, const std::string &latestPath)
{
	SetPrimary(st, latestPath, key);
	st.sel = {"g:" + key};
	st.anchor = "g:" + key;
	st.renaming = false;
}

void SelectRow(State &st, const Row &r)
{
	if (r.kind == 1) SelectGroup(st, r.key, st.recs[r.rec].vfsPath);
	else Select(st, st.recs[r.rec].vfsPath);
}

void SetPrimaryRow(State &st, const Row &r)
{
	SetPrimary(st, st.recs[r.rec].vfsPath, r.kind == 1 ? r.key : "");
}

void SelectRange(State &st, const View &v, int a, int b, bool keepOthers)
{
	if (!keepOthers) st.sel.clear();
	for (int i = std::min(a, b); i <= std::max(a, b); i++) st.sel.insert(RowId(st, v.rows[i]));
}

int RowIndexOf(const State &st, const View &v, const std::string &id)
{
	for (size_t i = 0; i < v.rows.size(); i++) {
		if (RowId(st, v.rows[i]) == id) return static_cast<int>(i);
	}
	return -1;
}

void ClickRow(State &st, const Row &r, bool ctrl, bool shift)
{
	const View v = BuildView(st);
	const std::string id = RowId(st, r);
	const int anchor = RowIndexOf(st, v, st.anchor);
	const int at = RowIndexOf(st, v, id);
	if (shift && anchor >= 0 && at >= 0) {
		SelectRange(st, v, anchor, at, ctrl);
		SetPrimaryRow(st, r);
		return;
	}
	if (ctrl) {
		if (st.sel.count(id) && st.sel.size() > 1) {
			st.sel.erase(id);
			if (PrimaryId(st) == id) {
				for (const Row &o : v.rows) {
					if (RowSelected(st, o)) {
						SetPrimaryRow(st, o);
						break;
					}
				}
			}
		}
		else {
			st.sel.insert(id);
			SetPrimaryRow(st, r);
		}
		st.anchor = id;
		return;
	}
	SelectRow(st, r);
}

void Rescan(State &st)
{
	st.recs = SaveLibScan();
	st.gameNames = SaveLibLoadGameNames(SaveLibActiveProfile());
	st.activeProfile = ModsMountedProfile().name;
	st.activeHash = ModsProfileGameplayHash(ModsMountedProfile());
	const std::string before = st.selPath;
	FixSelection(st);
	if (st.selPath != before) UpdatePlayers(st);
}

void ScrollToSelection(State &st)
{
	const View v = BuildView(st);
	const std::string prim = PrimaryId(st);
	for (const Row &r : v.rows) {
		if (RowId(st, r) != prim) continue;
		if (r.y < st.list.scroll) st.list.scroll = r.y;
		else if (r.y + r.h > st.list.scroll + kLH - 2) st.list.scroll = r.y + r.h - (kLH - 2);
		break;
	}
	st.list.scroll = std::max(0, std::min(st.list.scroll, std::max(0, v.total - (kLH - 2))));
}

void MoveSelection(State &st, int delta, bool toEnd = false, bool extend = false)
{
	const View v = BuildView(st);
	if (v.rows.empty()) return;
	const int count = static_cast<int>(v.rows.size());
	const int found = RowIndexOf(st, v, PrimaryId(st));
	const int cur = found < 0 ? 0 : found;
	int next = toEnd ? (delta < 0 ? 0 : count - 1) : cur + delta;
	next = std::max(0, std::min(count - 1, next));
	const Row &row = v.rows[next];
	const int anchor = RowIndexOf(st, v, st.anchor);
	if (extend && anchor >= 0) {
		SelectRange(st, v, anchor, next, false);
		SetPrimaryRow(st, row);
	}
	else {
		SelectRow(st, row);
	}
	ScrollToSelection(st);
}

void SelectAll(State &st)
{
	const View v = BuildView(st);
	if (v.rows.empty()) return;
	SelectRange(st, v, 0, static_cast<int>(v.rows.size()) - 1, false);
	if (st.anchor.empty()) st.anchor = RowId(st, v.rows[0]);
}

bool InGame() { return *reinterpret_cast<void **>(addr::kGame) != nullptr; }

void ExecuteLoad(const SaveRec &rec)
{
	LogI("saves", "load \"%s\"%s", rec.vfsPath.c_str(), rec.online ? " (online)" : "");
	if (rec.online && rec.kind == kSaveStrategy && !InGame() && MpUiHostSave(rec.vfsPath)) return;
	const std::string &path = rec.vfsPath;
	reinterpret_cast<Assign_t>(addr::kStringAssign)(reinterpret_cast<void *>(addr::kSaveName), nullptr, path.c_str(), static_cast<unsigned>(path.size()));
	*reinterpret_cast<int *>(rec.kind == kSaveReplay ? addr::kReplayRequest : addr::kLoadRequest) = 1;
	if (char *frame = *reinterpret_cast<char **>(addr::kMenuFrame)) *reinterpret_cast<int *>(frame + 0x6c) = 1;
}

bool DialogAlive(void *dlg, uintptr_t global) { return dlg && *reinterpret_cast<void **>(global) == dlg; }

void CloseDialog(void *dlg, uintptr_t global)
{
	if (!DialogAlive(dlg, global)) return;
	void **vt = *reinterpret_cast<void ***>(dlg);
	reinterpret_cast<Close_t>(vt[0x6c / 4])(dlg, nullptr, 0);
}

void ExecuteCancel(void *dlg, uintptr_t global)
{
	LogW("saves", "cancelled");
	CloseDialog(dlg, global);
}

void ExecuteSave(void *dlg, const std::string &name, bool online)
{
	if (!DialogAlive(dlg, addr::kSaveDlg)) {
		ModUiToast("The save window no longer exists", true);
		return;
	}
	void **vt = *reinterpret_cast<void ***>(dlg);
	void *ctrl = reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, 0x1004, *reinterpret_cast<const char **>(addr::kClassNameCUITextW));
	if (!ctrl) {
		LogW("saves", "the save dialog has no name box");
		ModUiToast("Couldn't save the game", true);
		CloseDialog(dlg, addr::kSaveDlg);
		return;
	}
	if (online) {
		char *folderStr = static_cast<char *>(dlg) + 0x60;
		const unsigned size = *reinterpret_cast<unsigned *>(folderStr + 0x14);
		const unsigned cap = *reinterpret_cast<unsigned *>(folderStr + 0x18);
		const char *text = cap < 0x10 ? folderStr + 4 : *reinterpret_cast<const char *const *>(folderStr + 4);
		const std::string folder = text && size < 1024 ? std::string(text, size) : std::string();
		const std::string sub = std::string(kSaveOnlineFolder) + "/";
		std::string profile = folder.empty() ? "" : folder.substr(0, folder.size() - 1);
		profile = profile.substr(profile.find_last_of("/\\") + 1);
		if (folder.empty() || (folder.back() != '/' && folder.back() != '\\')) {
			LogW("saves", "unexpected save dialog folder (\"%s\"); saving to the profile's", folder.c_str());
		}
		else if (folder.size() < sub.size() || _stricmp(folder.c_str() + folder.size() - sub.size(), sub.c_str()) != 0) {
			if (SaveLibEnsureOnlineDir(profile)) {
				const std::string online = folder + sub;
				reinterpret_cast<Assign_t>(addr::kStringAssign)(folderStr, nullptr, online.c_str(), static_cast<unsigned>(online.size()));
			}
			else {
				LogW("saves", "could not create folder %s of profile \"%s\"; saving to the profile's", kSaveOnlineFolder, profile.c_str());
			}
		}
	}
	LogI("saves", "save \"%s\"%s", name.c_str(), online ? " (online)" : "");
	reinterpret_cast<SetText_t>(addr::kSetTextUtf8)(ctrl, nullptr, name.c_str(), 1);
	const char *err = reinterpret_cast<DoSave_t>(vt[0xa0 / 4])(dlg);
	if (!err) {
		*reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x80) = 0;
		reinterpret_cast<Close_t>(vt[0x6c / 4])(dlg, nullptr, 0);
		return;
	}
	LogW("saves", "the game could not save: %s", err);
	ModUiToast("Couldn't save the game", true);
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.busy = false;
	g_s.closeAt = 0;
	g_s.open = DialogAlive(dlg, addr::kSaveDlg);
}

std::vector<SaveRec> GroupRecs(const State &st, const SaveRec &rec)
{
	std::vector<SaveRec> out;
	const std::string key = SaveLibGroupKey(rec);
	for (const SaveRec &r : st.recs) {
		if (r.kind == rec.kind && SaveLibGroupKey(r) == key) out.push_back(r);
	}
	return out;
}

std::string CountsText(const std::vector<SaveRec> &list)
{
	int autos = 0;
	for (const SaveRec &r : list) autos += r.autosave ? 1 : 0;
	const int manual = static_cast<int>(list.size()) - autos;
	return (manual ? std::to_string(manual) + Tr(manual == 1 ? " save" : " saves") + (autos ? Tr(" and ") : "") : std::string()) +
		(autos ? std::to_string(autos) + Tr(autos == 1 ? " autosave" : " autosaves") : std::string());
}

struct DelPlan {
	std::vector<SaveRec> list;
	int rows = 0;
	std::string title;
};

DelPlan PlanDelete(const State &st)
{
	DelPlan plan;
	std::set<std::string> seen;
	auto add = [&](const SaveRec &r) {
		if (seen.insert(Lower(r.vfsPath)).second) plan.list.push_back(r);
	};
	for (const Row &r : BuildView(st).rows) {
		if (!RowSelected(st, r)) continue;
		plan.rows++;
		const SaveRec &rec = st.recs[r.rec];
		if (r.kind == 2) {
			add(rec);
			continue;
		}
		const std::vector<SaveRec> all = GroupRecs(st, rec);
		if (r.kind == 1 || all.size() > 1) {
			for (const SaveRec &g : all) add(g);
			plan.title = PartidaTitle(st, SaveLibGroupKey(rec), rec.kind);
		}
		else {
			add(rec);
		}
	}
	return plan;
}

void DoAction(State &st, int action, int arg)
{
	if (st.renaming && action != kRenameField && action != kRenameOk && action != kRenameCancel) {
		if (action == kRow || action == kHeader || action == kBlank) {
			DoAction(st, kRenameOk, 0);
			if (st.renaming) DoAction(st, kRenameCancel, 0);
			return;
		}
		DoAction(st, kRenameCancel, 0);
	}
	const SaveRec *sel = FindRec(st, st.selPath);
	switch (action) {
	case kClose:
		if (st.dlg) {
			st.pendingCancel = true;
			st.busy = true;
			st.closeAt = GetTickCount();
		}
		else {
			st.open = false;
		}
		break;
	case kTab:
		if (arg >= 0 && arg < kTabCount && arg != st.tab) {
			st.tab = arg;
			st.list.scroll = 0;
			st.renaming = false;
			st.focus = kFocusNone;
			FixSelection(st);
			UpdatePlayers(st);
		}
		break;
	case kRow:
	case kHeader: {
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		const bool header = action == kHeader;
		const std::vector<std::string> &ids = header ? st.hdrKeys : st.rowPaths;
		if (arg < 0 || arg >= static_cast<int>(ids.size())) break;
		const View v = BuildView(st);
		const int at = RowIndexOf(st, v, header ? "g:" + st.hdrKeys[arg] : "p:" + Lower(st.rowPaths[arg]));
		if (at >= 0) ClickRow(st, v.rows[at], ctrl, shift);
		else if (header) SelectGroup(st, st.hdrKeys[arg], st.hdrPaths[arg]);
		else Select(st, st.rowPaths[arg]);
		if (st.mode == 1 && !header && st.sel.size() == 1) {
			if (const SaveRec *r = FindRec(st, st.selPath)) st.nameText = r->baseName;
		}
		break;
	}
	case kHeaderToggle:
		if (arg >= 0 && arg < static_cast<int>(st.hdrKeys.size())) {
			const std::string key = st.hdrKeys[arg];
			if (st.collapsed.erase(key) == 0) {
				st.collapsed.insert(key);
				if (sel && st.selGroup.empty() && SaveLibGroupKey(*sel) == key) SelectGroup(st, key, st.hdrPaths[arg]);
			}
		}
		break;
	case kBlank:
		if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000)) break;
		ClearSelection(st);
		st.renaming = false;
		st.error.clear();
		if (st.mode == 1) {
			st.nameText = st.defaultName;
			st.focus = kFocusName;
			EditSelectAll(st.nameText, st.edit);
		}
		break;
	case kSort:
		st.sort ^= 1;
		st.list.scroll = 0;
		break;
	case kShowAuto:
		st.showAuto = !st.showAuto;
		st.list.scroll = 0;
		FixSelection(st);
		UpdatePlayers(st);
		break;
	case kNameField:
		st.focus = kFocusName;
		break;
	case kSave:
	case kSaveOver: {
		if (st.mode != 1) break;
		std::string name = st.nameText;
		while (!name.empty() && name.back() == ' ') name.pop_back();
		while (!name.empty() && name.front() == ' ') name.erase(0, 1);
		if (!SaveLibValidName(name) || name.find('-') != std::string::npos) {
			ModUiToast("That name is not valid (no - < > : \" / \\ | ? *)", true);
			break;
		}
		bool exists = false;
		for (const SaveRec &r : st.recs) exists = exists || (TabMatches(r, st.tab) && _stricmp(r.baseName.c_str(), name.c_str()) == 0);
		if (exists && action == kSave) {
			st.confirm = 2;
			break;
		}
		st.confirm = 0;
		st.pendingSave = name;
		st.pendingSaveOnline = st.tab == 4;
		st.busy = true;
		st.closeAt = GetTickCount();
		break;
	}
	case kLoad:
		if (st.mode == 1) break;
		if (sel && st.sel.size() == 1) {
			ModCheck mc = CheckFor(st, *sel);
			if (mc.blocked) {
				LogW("saves", "\"%s\" not loaded: missing mods (%s)", sel->vfsPath.c_str(), Join(mc.missing).c_str());
				ModUiToast(TrFmt("You can't load this game: you are missing content (%s). See the list in the game's details.", {ShortList(mc.missing)}), true);
				break;
			}
			// Only the ruleset differs and it can be mounted in place: no restart (the game reads its data again when the game loads).
			if (mc.needsActivation && mc.rulesetOnly && mc.extra.empty() && ModsCanSwitchWithoutRestart(mc.ruleset) && ModsSwitchRulesetAndRemember(mc.ruleset)) {
				LogI("saves", "\"%s\" needs other rules; switched in place without a restart", sel->vfsPath.c_str());
				st.modChecks.clear();
				mc.needsActivation = false;
			}
			if (mc.needsActivation) {
				const ModProfile loose = ModsLooseProfileWith(mc.rulesFolders, mc.ruleset);
				if (!ModsSaveProfile(loose)) {
					ModUiToast("The content of the game could not be enabled.", true);
					break;
				}
				ModsSetActiveProfileName(loose.name);
				LogI("saves", "\"%s\" needs %s enabled; profile \"%s\" ready, asking to restart", sel->vfsPath.c_str(), Join(mc.inactive).c_str(), loose.name.c_str());
				std::vector<std::string> items;
				std::vector<std::string> off;
				for (const std::string &f : ModsMountedProfile().mods) {
					ModInfo m;
					if (!ModsFind(f, m) || !ModsIsRulesPack(m)) continue;
					if (std::find_if(mc.rulesFolders.begin(), mc.rulesFolders.end(), [&](const std::string &r) { return _stricmp(r.c_str(), f.c_str()) == 0; }) != mc.rulesFolders.end()) continue;
					off.push_back(m.name + (m.version.empty() || m.name.find(m.version) != std::string::npos ? "" : " " + m.version));
				}
				if (off.empty()) {
					items = mc.inactive;
				}
				else {
					items.push_back("##" + Tr("Will be enabled:"));
					items.insert(items.end(), mc.inactive.begin(), mc.inactive.end());
					items.push_back("##" + Tr("Will be disabled (this game does not use them):"));
					items.insert(items.end(), off.begin(), off.end());
				}
				ModUiAskRestart("This game needs content that is not enabled. The game will restart to enable it; afterwards, load it again.", -1,
					items, off.empty() ? "Content that will be enabled:" : "");
				break;
			}
			if (sel->online && sel->kind == kSaveStrategy && InGame()) {
				if (MpSaveInMultiplayer()) {
					ModUiToast("You are in an online game: leave it to load another one.");
					break;
				}
				st.pendingOnlineRoom = sel->vfsPath;
				if (st.dlg) {
					st.busy = true;
					st.closeAt = GetTickCount();
				}
				else {
					st.open = false;
				}
				break;
			}
			st.pendingLoad = sel->vfsPath;
			if (st.dlg) {
				st.busy = true;
				st.closeAt = GetTickCount();
			}
			else {
				st.open = false;
			}
		}
		break;
	case kRename:
		if (sel && st.sel.size() == 1) {
			st.renameGroup = !st.selGroup.empty();
			st.renaming = true;
			st.renameText = st.renameGroup ? PartidaTitle(st, st.selGroup, sel->kind) : sel->name;
			st.error.clear();
			st.focus = kFocusRename;
			EditSelectAll(st.renameText, st.edit);
			ScrollToSelection(st);
		}
		break;
	case kRenameField:
		if (st.renaming) st.focus = kFocusRename;
		break;
	case kRenameCancel:
		st.renaming = false;
		st.error.clear();
		if (st.focus == kFocusRename) st.focus = kFocusNone;
		break;
	case kRenameOk:
		if (sel && st.renaming && st.renameGroup) {
			std::string name = st.renameText;
			while (!name.empty() && name.back() == ' ') name.pop_back();
			while (!name.empty() && name.front() == ' ') name.erase(0, 1);
			if (name != PartidaTitle(st, SaveLibGroupKey(*sel), sel->kind)) {
				SaveLibSetGameName(sel->profile, SaveLibGroupKey(*sel), name);
				st.gameNames = SaveLibLoadGameNames(sel->profile);
			}
			st.renaming = false;
			st.renameGroup = false;
			if (st.focus == kFocusRename) st.focus = kFocusNone;
			break;
		}
		if (sel && st.renaming) {
			SaveRec rec = *sel;
			std::string name = st.renameText;
			while (!name.empty() && name.back() == ' ') name.pop_back();
			const std::string err = SaveLibRename(rec, name);
			if (!err.empty()) {
				st.error = err;
				ModUiToast(err, true);
				break;
			}
			const std::string ext = rec.vfsPath.substr(rec.vfsPath.size() - 4);
			const std::string newPath = rec.vfsPath.substr(0, rec.vfsPath.find_last_of('/') + 1) + name + (rec.campaign.empty() ? "" : "-" + rec.campaign) + ext;
			st.renaming = false;
			st.error.clear();
			if (st.focus == kFocusRename) st.focus = kFocusNone;
			st.recs = SaveLibScan();
			st.selPath = Lower(newPath);
			FixSelection(st);
			if (st.mode == 1) st.nameText = name + (rec.campaign.empty() ? "" : "-" + rec.campaign);
			ScrollToSelection(st);
		}
		break;
	case kDelete:
		if (sel) {
			const DelPlan plan = PlanDelete(st);
			if (plan.list.empty()) break;
			st.delList = plan.list;
			st.delRows = plan.rows;
			st.delTitle = plan.title;
			st.confirm = plan.rows == 1 && plan.list.size() == 1 ? 1 : 3;
		}
		break;
	case kDelNo:
		st.confirm = 0;
		break;

	case kDelOne:
		st.confirm = 0;
		if (!st.delList.empty()) {
			st.pendingDelete = std::move(st.delList);
			st.busy = true;
		}
		st.delList.clear();
		break;
	case kSearchField:
		st.focus = kFocusSearch;
		break;
	case kClearSearch:
		st.search.clear();
		st.edit = EditState();
		st.list.scroll = 0;
		FixSelection(st);
		UpdatePlayers(st);
		break;
	}
}

std::string &FieldText(State &st) { return st.focus == kFocusRename ? st.renameText : st.focus == kFocusName ? st.nameText : st.search; }
int FieldAction(int focus) { return focus == kFocusRename ? kRenameField : focus == kFocusName ? kNameField : kSearchField; }

void FieldKey(State &st, UINT msg, WPARAM wp)
{
	std::string &text = FieldText(st);
	bool changed = false;
	if (msg == WM_CHAR) {
		changed = EditChar(text, st.edit, wp, 60);
	}
	else if (wp == VK_RETURN) {
		if (st.focus == kFocusRename) DoAction(st, kRenameOk, 0);
		else if (st.focus == kFocusName) DoAction(st, kSave, 0);
		else st.focus = kFocusNone;
		return;
	}
	else if (wp == VK_ESCAPE) {
		if (st.focus == kFocusRename) DoAction(st, kRenameCancel, 0);
		else if (st.focus == kFocusName) DoAction(st, kClose, 0);
		else if (!st.search.empty()) DoAction(st, kClearSearch, 0);
		else st.focus = kFocusNone;
		return;
	}
	else if (st.focus == kFocusSearch && (wp == VK_DOWN || wp == VK_UP)) {
		st.focus = kFocusNone;
		MoveSelection(st, wp == VK_DOWN ? 1 : -1);
		return;
	}
	else {
		changed = EditKey(text, st.edit, wp, 60) == 2;
	}
	if (changed && st.focus == kFocusSearch) {
		st.list.scroll = 0;
		FixSelection(st);
		UpdatePlayers(st);
	}
	if (changed && st.focus == kFocusRename) st.error.clear();
}

bool TabSkip(int action)
{
	return action == kRow || action == kHeader || action == kHeaderToggle || action == kBlank || action == kNone;
}

void TabStep(State &st, bool back)
{
	FocusItem cur = {st.tabAction, st.tabArg};
	if (st.focus != kFocusNone) cur = {FieldAction(st.focus), 0};
	const FocusItem next = FocusStep(FocusOrder(st.hits, TabSkip), cur, back);
	if (next.action < 0) return;
	st.focus = kFocusNone;
	st.tabAction = next.action;
	st.tabArg = next.arg;
	if (next.action == kSearchField || next.action == kRenameField || next.action == kNameField) {
		st.focus = next.action == kRenameField ? kFocusRename : next.action == kNameField ? kFocusName : kFocusSearch;
		EditSelectAll(FieldText(st), st.edit);
	}
}

void PaintRowText(Painter &p, const State &st, const SaveRec &r, int x, int y, int w, int h, bool child, const std::string &title = "", bool editing = false)
{
	const ModCheck mc = st.mode == 0 ? CheckFor(st, r) : ModCheck();
	const bool warn = mc.warn || mc.blocked;
	const std::string &nm = !title.empty() ? title : r.name;
	if (child) {
		p.Font(0);
		if (!editing) p.Text(x + 30, y, w - 190 - (warn ? 26 : 0), h, nm, r.autosave ? kDim : kText);
		p.Font(3);
		const std::string when = TimeText(r.time);
		p.Text(x + w - 160, y, 150, h, when, kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		if (warn) PaintAlertIcon(p, x + w - 10 - p.TextWidth(when) - 24, y + (h - 16) / 2, mc.blocked);
		return;
	}
	p.Font(1);
	if (!editing) p.Text(x + 12, y + 4, w - 130, 22, nm, r.autosave ? kDim : kText);
	p.Font(3);
	std::string sub = TimeText(r.time);
	if (r.autosave) sub += "  ·  Autosave";
	if (const std::string camp = CampaignOf(st, r); !camp.empty()) sub += "  ·  " + camp;
	if (mc.blocked) sub += "  ·  missing content";
	else if (warn) sub += "  ·  different content";
	p.Text(x + 12, y + 25, w - 24, 18, sub, warn ? kBad : kDim);
	p.Text(x + w - 116, y + 4, 104, 22, HumanBytes(r.size), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	if (warn) PaintAlertIcon(p, x + w - 12 - p.TextWidth(HumanBytes(r.size)) - 24, y + 7, mc.blocked);
}

void PaintList(Painter &p, State &st, const View &v)
{
	const int mx = st.mouseX, my = st.mouseY;
	p.Fill(kLX, kLY, kLW, kLH, RGB(20, 15, 10));
	p.Frame(kLX, kLY, kLW, kLH, pal::line);
	p.AddHit(kLX, kLY, kLW, kLH, kBlank, 0);
	st.rowPaths.clear();
	st.hdrKeys.clear();
	st.hdrPaths.clear();
	if (v.rows.empty()) {
		p.Font(0);
		const bool filtered = !st.search.empty();
		const std::string text = filtered ? "No games match the search." : "There are no saved games.";
		p.Text(kLX + 10, kLY + 14, kLW - 20, 24, text, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		bool hiddenAutos = false;
		for (const SaveRec &r : st.recs) hiddenAutos = hiddenAutos || (r.autosave && ListMatches(st, r, st.tab));
		if (!st.showAuto && !filtered && hiddenAutos) {
			p.Font(3);
			p.Text(kLX + 10, kLY + 40, kLW - 20, 20, "Check \"Show autosaves\" to see the automatic ones too.", kDim,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		st.listGeom = ListGeom();
		return;
	}
	const int innerH = kLH - 2;
	const int maxScroll = ListMaxScroll(v.total, innerH);
	st.list.scroll = std::max(0, std::min(st.list.scroll, maxScroll));
	const bool bar = maxScroll > 0;
	const int rx = kLX + 1, rw = kLW - 2 - (bar ? 14 : 0);
	p.Clip(kLX + 1, kLY + 1, kLW - 2, innerH);
	for (const Row &r : v.rows) {
		const int ry = kLY + 1 + r.y - st.list.scroll;
		if (ry + r.h < kLY + 1 || ry > kLY + kLH - 1) continue;
		const SaveRec &rec = st.recs[r.rec];
		const bool selected = RowSelected(st, r);
		const bool editing = st.renaming && st.sel.size() == 1 && RowId(st, r) == PrimaryId(st);
		const int top = std::max(ry, kLY + 1), bottom = std::min(ry + r.h, kLY + kLH - 1);
		if (r.kind == 1) {
			const bool hovH = p.Hover(rx, top, rw, std::max(0, bottom - top), mx, my);
			p.Fill(rx, ry, rw, r.h, selected ? RGB(86, 64, 32) : hovH ? RGB(48, 38, 26) : pal::field);
			if (selected) p.Frame(rx, ry, rw, r.h, kGold);
			else p.Fill(rx, ry + r.h - 1, rw, 1, pal::lineDim);
			p.Font(1);
			const COLORREF arrow = p.Hover(rx, top, 30, std::max(0, bottom - top), mx, my) ? pal::focus : kGold;
			if (r.collapsed) p.ArrowRight(rx + 12, ry + r.h / 2 - 5, 7, 10, arrow);
			else p.ArrowDown(rx + 10, ry + r.h / 2 - 3, 10, 6, arrow);
			if (!editing) p.Text(rx + 28, ry, rw - 300, r.h, r.title, kGold);
			p.Font(3);
			if (!editing) p.Text(rx + rw - 260, ry, 250, r.h, std::to_string(r.count) + (r.count == 1 ? Tr(" save  ·  ") : Tr(" saves  ·  ")) + TimeText(rec.time), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			if (bottom > top) {
				st.hdrKeys.push_back(r.key);
				st.hdrPaths.push_back(rec.vfsPath);
				const int idx = static_cast<int>(st.hdrKeys.size()) - 1;
				p.AddHit(rx, top, rw, bottom - top, kHeader, idx);
				p.AddHit(rx, top, 30, bottom - top, kHeaderToggle, idx);
			}
			if (editing) {
				PaintEditField(p, rx + 26, ry + 3, rw - 34, r.h - 6, st.renameText, "Game name", st.focus == kFocusRename, false, st.edit, kRenameField);
				p.Clip(kLX + 1, kLY + 1, kLW - 2, innerH);
			}
			continue;
		}
		const bool hov = p.Hover(rx, top, rw, std::max(0, bottom - top), mx, my);
		p.Fill(rx, ry, rw, r.h, selected ? RGB(86, 64, 32) : hov ? RGB(48, 38, 26) : (r.kind == 2 ? RGB(26, 20, 14) : RGB(22, 17, 12)));
		if (selected) p.Frame(rx, ry, rw, r.h, kGold);
		else p.Fill(rx, ry + r.h - 1, rw, 1, RGB(44, 34, 22));
		PaintRowText(p, st, rec, rx, ry, rw, r.h, r.kind == 2, r.title, editing);
		if (bottom > top) {
			st.rowPaths.push_back(rec.vfsPath);
			p.AddHit(rx, top, rw, bottom - top, kRow, static_cast<int>(st.rowPaths.size()) - 1);
		}
		if (editing) {
			const int fx = r.kind == 2 ? rx + 26 : rx + 8, fw = r.kind == 2 ? rw - 26 - 170 : rw - 8 - 130;
			PaintEditField(p, fx, ry + 3, fw, r.kind == 2 ? r.h - 6 : 24, st.renameText, "Name", st.focus == kFocusRename, false, st.edit, kRenameField);
			p.Clip(kLX + 1, kLY + 1, kLW - 2, innerH);
		}
	}
	p.Unclip();
	ScrollbarPaint(p, kLX + kLW - 10, kLY + 3, kLH - 6, v.total, innerH, st.list, st.listGeom, mx, my);
}

struct Thumb {
	std::string path;
	uint64_t time = 0;
	std::vector<uint32_t> px;
	int w = 0, h = 0;
} g_thumb;

std::string PlayTimeText(int secs)
{
	const int h = secs / 3600, m = secs % 3600 / 60;
	if (h > 0) return std::to_string(h) + " h " + (m < 10 ? "0" : "") + std::to_string(m) + Tr(" min");
	return m > 0 ? std::to_string(m) + Tr(" min") : "less than 1 min";
}

std::string MapLabel(const MpSaveInfo &info, const SaveRec &rec)
{
	if (!info.valid) return "";
	std::string name = Tr("random");
	if (!info.random) {
		name = info.map;
		const size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos) name.erase(0, slash + 1);
		const size_t dot = name.find_last_of('.');
		if (dot != std::string::npos && dot > 0) name.erase(dot);
		if (_stricmp(name.c_str(), rec.baseName.c_str()) == 0) name.clear();
	}
	const bool size = info.sizeX > 0 && info.sizeY > 0 && info.sizeX < 4096 && info.sizeY < 4096;
	const std::string dim = size ? std::to_string(info.sizeX) + "×" + std::to_string(info.sizeY) : "";
	if (name.empty()) return dim;
	return dim.empty() ? name : name + " (" + dim + ")";
}

int PaintPlayers(Painter &p, const MpSaveInfo &info, const std::vector<int> &states, int x, int y, int w, int maxH)
{
	p.Font(3);
	const int rowH = 20;
	int cx = x, cy = y;
	bool any = false;
	for (size_t i = 0; i < info.players.size() && i < 8; i++) {
		const MpSavePlayer &pl = info.players[i];
		if (!pl.used()) continue;
		const int tw = p.TextWidth(pl.name);
		if (cx > x && cx + 15 + tw > x + w) {
			cx = x;
			cy += rowH;
		}
		if (cy + rowH > y + maxH) break;
		const int c = pl.color555;
		const bool defeated = i < states.size() && states[i] == 2;
		const int r = ((c >> 10) & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = (c & 31) * 255 / 31;
		p.Fill(cx, cy + 5, 10, 10, defeated ? RGB(r / 3 + 30, g / 3 + 30, b / 3 + 30) : RGB(r, g, b));
		p.Frame(cx, cy + 5, 10, 10, RGB(20, 15, 10));
		p.Text(cx + 15, cy, tw + 4, rowH, pl.name, defeated ? kDim : kText);
		if (defeated) p.Fill(cx + 13, cy + rowH / 2, tw + 4, 1, kDim);
		cx += 15 + tw + 16;
		any = true;
	}
	return any ? cy + rowH - y : 0;
}

void PaintDetailBody(Painter &p, State &st)
{
	p.Fill(kDX, kDY, kDW, kDH, pal::wellRaised);
	p.Frame(kDX, kDY, kDW, kDH, pal::line);
	if (st.sel.size() > 1) {
		const DelPlan plan = PlanDelete(st);
		uint64_t bytes = 0;
		for (const SaveRec &r : plan.list) bytes += r.size;
		const int x = kDX + 12, w = kDW - 24;
		p.Font(1);
		p.Text(x, kDY + 16, w, 24, std::to_string(plan.rows) + Tr(" items selected"), kGold);
		p.Font(3);
		p.Text(x, kDY + 48, w, 18, CountsText(plan.list) + "  ·  " + HumanBytes(bytes), kDim);
		return;
	}
	const SaveRec *rec = FindRec(st, st.selPath);
	if (!rec) {
		p.Font(0);
		p.Text(kDX + 10, kDY + 10, kDW - 20, 24, "Select a game", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const int tw = 256, th = tw * 3 / 4, tx = kDX + (kDW - tw) / 2, ty = kDY + 12;
	p.Fill(tx, ty, tw, th, RGB(10, 8, 6));
	if (g_thumb.path != st.selPath || g_thumb.time != rec->time) {
		g_thumb.path = st.selPath;
		g_thumb.time = rec->time;
		g_thumb.px.clear();
		g_thumb.w = g_thumb.h = 0;
		if (rec->hasThumb) SaveLibLoadThumb(*rec, g_thumb.px, g_thumb.w, g_thumb.h);
	}
	if (!g_thumb.px.empty()) {
		p.Image(tx, ty, tw, th, g_thumb.px.data(), g_thumb.w, g_thumb.h);
	}
	else {
		p.Font(3);
		p.Text(tx, ty, tw, th, rec->kind == kSaveReplay ? "Replay" : "No thumbnail", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	p.Frame(tx, ty, tw, th, pal::line);
	int y = ty + th + 10;
	const int x = kDX + 12, w = kDW - 24;
	const bool group = !st.selGroup.empty();
	const std::string key = SaveLibGroupKey(*rec);
	const std::string title = PartidaTitle(st, key, rec->kind);
	p.Font(1);
	p.Text(x, y, w, 24, group ? title : rec->name, kGold);
	y += 26;
	p.Font(3);
	std::string line = (group ? Tr("Last saved: ") : "") + TimeText(rec->time) + "  ·  " + HumanBytes(rec->size);
	p.Text(x, y, w, 18, line, kDim);
	y += 19;
	if (const std::string camp = CampaignOf(st, *rec); !camp.empty()) {
		p.Text(x, y, w, 18, (rec->kind == kSaveAdventure ? Tr("Adventure: ") : Tr("Conquest: ")) + camp, kText);
		y += 19;
	}
	const std::string map = MapLabel(st.info, *rec);
	if (!map.empty()) {
		p.Text(x, y, w, 18, Tr("Map: ") + map, kText);
		y += 19;
	}
	if (rec->playSeconds >= 0) {
		p.Text(x, y, w, 18, Tr("Play time: ") + PlayTimeText(rec->playSeconds), kText);
		y += 19;
	}
	if (rec->online) {
		p.Text(x, y, w, 18, "Online game", kOk);
		y += 19;
	}
	if (st.info.valid) y += PaintPlayers(p, st.info, rec->playerStates, x, y + 2, w, kDY + kDH - 70 - y) + 2;
	const int bottom = kDY + kDH - 8;
	if (st.mode == 0 && y + 30 <= bottom) {
		const ModCheck mc = CheckFor(st, *rec);
		std::vector<ListLine> lines;
		auto group = [&](const std::string &title, const std::vector<std::string> &names, COLORREF col) {
			if (names.empty()) return;
			lines.push_back({title, col, true});
			for (const std::string &n : names) lines.push_back({"\xE2\x80\xA2  " + n, col, false});
		};
		const std::string many = std::to_string(mc.missing.size());
		group(mc.missing.size() == 1 ? "You are missing 1 content pack (it cannot be loaded):" : Tr("You are missing ") + many + Tr(" content packs (they cannot be loaded):"), mc.missing, kBad);
		group(mc.inactive.size() == 1 ? "You need to enable 1 content pack:" : Tr("You need to enable ") + std::to_string(mc.inactive.size()) + Tr(" content packs:"), mc.inactive, kGold);
		group("Another version or different files:", mc.other, kGold);
		group(rec->mods.empty() ? "It was saved without additional content. You have enabled:" : "It was saved without this content you have enabled:", mc.extra, kGold);
		if (lines.empty() && mc.warn) lines.push_back({"The content of this game does not match the one you have enabled.", kGold, false});
		if (st.modListKey != rec->vfsPath) {
			st.modListKey = rec->vfsPath;
			st.modList = ListState();
		}
		st.modGeom = ListGeom();
		if (!lines.empty()) {
			p.Fill(x, y + 2, w, 1, pal::lineDim);
			y += 8;
			const int rowH = 19;
			const int h = std::min(bottom - y, static_cast<int>(lines.size()) * rowH + 8);
			if (h >= rowH + 8) ListPaint(p, x, y, w, h, lines, st.modList, st.modGeom, 3, rowH);
		}
	}
}

void PaintDetail(Painter &p, State &st)
{
	p.Clip(kDX, kDY, kDW, kDH);
	PaintDetailBody(p, st);
	p.Unclip();
}

void PaintConfirm(Painter &p, State &st)
{
	if (!st.confirm) return;
	const int mx = st.mouseX, my = st.mouseY;
	if (st.confirm == 2) {
		std::string name = st.nameText;
		while (!name.empty() && name.back() == ' ') name.pop_back();
		const Dialog d = DialogBegin(p, kDesignW, kDesignH, 560, 210, "Overwrite game");
		DialogText(p, d, 60, Tr("A save named \"") + name + Tr("\" already exists. Overwrite it?"));
		DialogButtons(p, d, {{"Overwrite", kSaveOver, 0, true}, {"Cancel", kDelNo}}, mx, my);
		return;
	}
	const SaveRec *rec = FindRec(st, st.selPath);
	if (!rec || st.delList.empty()) return;
	const bool whole = st.confirm == 3;
	const bool several = whole && st.delRows > 1;
	std::string question;
	if (several) {
		question = TrFmt("Delete the %s selected items? This will delete %s.", {std::to_string(st.delRows), CountsText(st.delList)});
	}
	else if (whole) {
		question = TrFmt("Delete the game \"%s\"? This will delete %s.", {st.delTitle, CountsText(st.delList)});
	}
	else {
		question = TrFmt("Delete \"%s\"?", {rec->name});
	}
	const Dialog d = DialogBegin(p, kDesignW, kDesignH, 560, 210, several ? "Delete selection" : whole ? "Delete game" : "Delete save");
	DialogText(p, d, 60, question);
	DialogButtons(p, d, {{whole ? "Delete all" : "Delete", kDelOne, 0, true}, {"Cancel", kDelNo}}, mx, my);
}

void PaintPanel(Painter &p, State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	PaintWindowFrame(p, kDesignW, kDesignH);
	p.noHover = st.confirm != 0;
	const View v = BuildView(st);

	const bool save = st.mode == 1;
	PaintTitle(p, 20, 12, 300, save ? "Save game" : "Load game");
	p.Font(3);
	PaintRule(p, 54, kDesignW);

	if (save) {
		std::string kind = kTabNames[st.tab];
		for (char &c : kind) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		p.Font(0);
		p.Text(18, 66, 300, 26, "Save name", kDim);
		p.Text(330, 66, 600, 26, Tr("Saved ") + kind + Tr(" games") + (v.counts[st.tab] ? " (" + std::to_string(v.counts[st.tab]) + ")" : ""), kDim);
		p.Font(3);
	}
	else {
		std::vector<TabSpec> tabs;
		for (int t = 0; t < kTabCount; t++) {
			const std::string label = Tr(kTabNames[t]) + (v.counts[t] ? " (" + std::to_string(v.counts[t]) + ")" : "");
			tabs.push_back({label, kTab, t, st.tab == t, true});
		}
		PaintTabBar(p, kDesignW, tabs, mx, my, 150);
	}

	if (save) {
		PaintEditField(p, 16, 100, 300, 28, st.nameText, "E.g.: Before the siege", st.focus == kFocusName, false, st.edit, kNameField);
		PaintEditField(p, 328, 100, 200, 28, st.search, "Search...", st.focus == kFocusSearch, false, st.edit, kSearchField);
		if (!st.search.empty()) {
			p.Font(1);
			p.Button(502, 104, 22, 20, "\xC3\x97", kClearSearch, 0, mx, my, true, false, 1);
		}
		p.Button(540, 100, 150, 28, st.sort == 0 ? "Sort: date" : "Sort: name", kSort, 0, mx, my);
		PaintSwitch(p, 706, 102, st.showAuto, "Show autosaves", true, kShowAuto, 0, mx, my);
	}
	else {
		PaintEditField(p, 16, 100, 250, 28, st.search, "Search...", st.focus == kFocusSearch, false, st.edit, kSearchField);
		if (!st.search.empty()) {
			p.Font(1);
			p.Button(240, 104, 22, 20, "\xC3\x97", kClearSearch, 0, mx, my, true, false, 1);
		}
		p.Button(278, 100, 150, 28, st.sort == 0 ? "Sort: date" : "Sort: name", kSort, 0, mx, my);
		PaintSwitch(p, 446, 102, st.showAuto, "Show autosaves", true, kShowAuto, 0, mx, my);
	}

	PaintList(p, st, v);
	PaintDetail(p, st);

	const SaveRec *rec = FindRec(st, st.selPath);
	if (save) {
		std::string name = st.nameText;
		while (!name.empty() && name.back() == ' ') name.pop_back();
		bool exists = false;
		for (const SaveRec &r : st.recs) exists = exists || (r.kind == st.saveKind && _stricmp(r.baseName.c_str(), name.c_str()) == 0);
		p.Button(16, 590, 170, 36, exists ? "Overwrite" : "Save", kSave, 0, mx, my, !name.empty(), true);
		p.Button(196, 590, 120, 36, "Rename", kRename, 0, mx, my, rec != nullptr && !st.renaming && st.sel.size() <= 1);
		p.Button(326, 590, 120, 36, "Delete", kDelete, 0, mx, my, rec != nullptr && !st.renaming);
		p.Button(kDesignW - 16 - 120, 590, 120, 36, "Back", kClose, 0, mx, my);
		PaintConfirm(p, st);
		return;
	}
	const bool room = rec && rec->online && rec->kind == kSaveStrategy && !InGame();
	const ModCheck loadCheck = rec ? CheckFor(st, *rec) : ModCheck();
	const bool blocked = loadCheck.blocked;
	const bool one = st.sel.size() <= 1;
	p.Button(16, 590, room ? 190 : 170, 36, loadCheck.needsActivation ? "Enable and restart" : room ? "Load in a room" : "Load", kLoad, 0, mx, my,
		rec != nullptr && !st.renaming && !blocked && one, true);
	int bx = 16 + (room ? 190 : 170) + 10;
	p.Button(bx, 590, 120, 36, "Rename", kRename, 0, mx, my, rec != nullptr && !st.renaming && one);
	bx += 130;
	p.Button(bx, 590, 120, 36, "Delete", kDelete, 0, mx, my, rec != nullptr && !st.renaming);
	p.Button(kDesignW - 16 - 120, 590, 120, 36, "Back", kClose, 0, mx, my);

	PaintConfirm(p, st);
}

}

namespace {
void OpenImpl(void *dlg);
bool HandleImpl(UINT msg, WPARAM wp, LPARAM lp);
void DrawImpl(uint32_t *buf, int w, int h);

int ExcFilter(EXCEPTION_POINTERS *e, const char *where)
{
	HMODULE m = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCSTR>(e->ExceptionRecord->ExceptionAddress), &m);
	LogE("crash", "EXCEPTION %08lx in %s, address %p (module %p, offset %lx), access to %p", e->ExceptionRecord->ExceptionCode, where,
		e->ExceptionRecord->ExceptionAddress, static_cast<void *>(m),
		static_cast<unsigned long>(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) - reinterpret_cast<uintptr_t>(m)),
		e->ExceptionRecord->NumberParameters > 1 ? reinterpret_cast<void *>(e->ExceptionRecord->ExceptionInformation[1]) : nullptr);
	return EXCEPTION_CONTINUE_SEARCH;
}
}

void SavesUiOpen()
{
	__try {
		OpenImpl(nullptr);
	}
	__except (ExcFilter(GetExceptionInformation(), "open")) {
	}
}

void SavesUiOpenLoad(void *dlg)
{
	__try {
		OpenImpl(dlg);
	}
	__except (ExcFilter(GetExceptionInformation(), "open load")) {
	}
}

bool SavesUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (ModUiIsOpen()) return false;
	__try {
		return HandleImpl(msg, wp, lp);
	}
	__except (ExcFilter(GetExceptionInformation(), "message")) {
		return false;
	}
}

bool SavesUiIsOpen()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.open;
}

bool SavesUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.open && g_s.dlg && !DialogAlive(g_s.dlg, g_s.dlgGlobal)) g_s.open = false;
	if (g_s.open && g_s.closeAt && GetTickCount() - g_s.closeAt > 2000) g_s.open = false;
	return g_s.open;
}

namespace {
void OpenSaveImpl(void *dlg)
{
	const int type = *reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x7c);
	const int kind = type == 1 ? kSaveConquest : type == 2 ? kSaveAdventure : type == 4 ? kSaveReplay : kSaveStrategy;
	std::string def;
	void **vt = *reinterpret_cast<void ***>(dlg);
	if (void *ctrl = reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, 0x1004, *reinterpret_cast<const char **>(addr::kClassNameCUITextW))) {
		static char buf[0x800];
		buf[0] = 0;
		if (reinterpret_cast<GetText_t>(addr::kGetTextUtf8)(ctrl, nullptr, buf, sizeof(buf)) == 0) def = buf;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_s = State();
	g_s.open = true;
	g_s.mode = 1;
	g_s.dlg = dlg;
	g_s.dlgGlobal = addr::kSaveDlg;
	g_s.saveKind = kind;
	g_s.tab = kind == kSaveAdventure ? 1 : kind == kSaveConquest ? 2 : kind == kSaveReplay ? 3 : (MpSaveInMultiplayer() ? 4 : 0);
	SaveLibCleanOrphans(SaveLibActiveProfile());
	g_s.recs = SaveLibScan();
	g_s.gameNames = SaveLibLoadGameNames(SaveLibActiveProfile());
	g_s.nameText.clear();
	g_s.defaultName = g_s.nameText;
	g_s.focus = kFocusName;
	EditSelectAll(g_s.nameText, g_s.edit);
	g_s.activeProfile = ModsMountedProfile().name;
	g_s.activeHash = ModsProfileGameplayHash(ModsMountedProfile());
	FixSelection(g_s);
	ClearSelection(g_s);
	LogD("saves", "save screen open (type %d, name \"%s\")", type, def.c_str());
}

ShowSave_t g_origShowSave = nullptr;

const char *__cdecl HookShowSave(int a, int b)
{
	const char *r = g_origShowSave(a, b);
	void *dlg = *reinterpret_cast<void **>(addr::kSaveDlg);
	if (!r && dlg) SavesUiOpenSave(dlg);
	return r;
}

ShowSave_t g_origShowLoad = nullptr;

const char *__cdecl HookShowLoad(int a, int b)
{
	const char *r = g_origShowLoad(a, b);
	void *dlg = *reinterpret_cast<void **>(addr::kLoadDlg);
	if (!r && dlg) SavesUiOpenLoad(dlg);
	return r;
}
}

void SavesUiOpenSave(void *dlg)
{
	__try {
		OpenSaveImpl(dlg);
	}
	__except (ExcFilter(GetExceptionInformation(), "open save")) {
	}
}

void InstallSavesUi(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kShowSaveDialog);
	if (HookCreate(target, reinterpret_cast<void *>(&HookShowSave), reinterpret_cast<void **>(&g_origShowSave)) == MH_OK &&
		HookEnable(target) == MH_OK) {
		LogI("saves", "save screen ready");
		void *load = reinterpret_cast<void *>(addr::kShowLoadDialog);
		if (HookCreate(load, reinterpret_cast<void *>(&HookShowLoad), reinterpret_cast<void **>(&g_origShowLoad)) == MH_OK &&
			HookEnable(load) == MH_OK) {
			LogI("saves", "game load screen ready");
		}
	}
	else {
		g_origShowSave = nullptr;
		LogE("saves", "could not hook the save dialog");
	}
}

void SavesUiDraw(uint32_t *buf, int w, int h)
{
	__try {
		DrawImpl(buf, w, h);
	}
	__except (ExcFilter(GetExceptionInformation(), "drawing")) {
	}
}

namespace {
void OpenImpl(void *dlg)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_s = State();
	g_s.open = true;
	g_s.dlg = dlg;
	g_s.dlgGlobal = addr::kLoadDlg;
	SaveLibCleanOrphans(SaveLibActiveProfile());
	g_s.recs = SaveLibScan();
	g_s.gameNames = SaveLibLoadGameNames(SaveLibActiveProfile());
	g_s.activeProfile = ModsMountedProfile().name;
	g_s.activeHash = ModsProfileGameplayHash(ModsMountedProfile());
	if (!g_s.recs.empty()) {
		for (int t = 0; t < kTabCount; t++) {
			if (TabMatches(g_s.recs[0], t)) {
				g_s.tab = t;
				break;
			}
		}
	}
	FixSelection(g_s);
	UpdatePlayers(g_s);
	LogD("saves", "load screen open (%d games)", static_cast<int>(g_s.recs.size()));
}

bool HandleImpl(UINT msg, WPARAM wp, LPARAM lp)
{
	std::unique_lock<std::mutex> lock(g_lock);
	if (!g_s.open) return false;
	State &st = g_s;
	if (st.busy) return msg != WM_MOUSEMOVE && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST));
	bool consumed = true;
	switch (msg) {
	case WM_MOUSEMOVE:
		st.mouseX = static_cast<short>(LOWORD(lp));
		st.mouseY = static_cast<short>(HIWORD(lp));
		{
			const POINT at = {st.mouseX, st.mouseY};
			ListMove(st.list, st.listGeom, at);
			ListMove(st.modList, st.modGeom, at);
		}
		if (st.edit.dragging && st.focus != kFocusNone) {
			FieldGeom geom;
			if (FieldGet(FieldAction(st.focus), geom)) st.edit.caret = EditIndexAt(geom, FieldText(st), st.mouseX);
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		st.mouseX = static_cast<short>(LOWORD(lp));
		st.mouseY = static_cast<short>(HIWORD(lp));
		st.mouseDown = true;
		st.tabAction = -1;
		const POINT pt = {st.mouseX, st.mouseY};
		if (!st.confirm && (ListDown(st.modList, st.modGeom, pt) || ListDown(st.list, st.listGeom, pt))) break;
		bool hitSomething = false;
		for (auto it = st.hits.rbegin(); it != st.hits.rend(); ++it) {
			if (!PtInRect(&it->r, pt)) continue;
			const Hit hit = *it;
			hitSomething = true;
			if (!(msg == WM_LBUTTONDBLCLK && hit.action == kHeaderToggle)) DoAction(st, hit.action, hit.arg);
			if (msg == WM_LBUTTONDBLCLK && hit.action == kRow && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) DoAction(st, st.mode == 1 ? kSave : kLoad, 0);
			if (msg == WM_LBUTTONDBLCLK && hit.action == kHeader) DoAction(st, kHeaderToggle, hit.arg);
			break;
		}
		if (!hitSomething) st.focus = kFocusNone;
		else if (st.focus != kFocusNone) {
			FieldGeom geom;
			if (FieldGet(FieldAction(st.focus), geom) && PtInRect(&geom.r, pt)) {
				std::string &text = FieldText(st);
				st.edit.caret = EditIndexAt(geom, text, st.mouseX);
				if (!(GetKeyState(VK_SHIFT) & 0x8000)) st.edit.anchor = st.edit.caret;
				if (msg == WM_LBUTTONDBLCLK) EditSelectWord(text, st.edit);
				else st.edit.dragging = true;
			}
		}
		break;
	}
	case WM_LBUTTONUP:
		st.mouseDown = false;
		ListUp(st.list);
		ListUp(st.modList);
		st.edit.dragging = false;
		break;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
		break;
	case WM_MOUSEWHEEL:
		if (!st.confirm && !ListWheel(st.modList, st.modGeom, POINT{st.mouseX, st.mouseY}, GET_WHEEL_DELTA_WPARAM(wp))) {
			st.list.scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 60;
			st.list.scroll = std::max(0, std::min(st.list.scroll, st.listGeom.maxScroll));
		}
		break;
	case WM_CHAR:
		if (st.focus != kFocusNone && !st.confirm) FieldKey(st, msg, wp);
		break;
	case WM_KEYDOWN: {
		if (wp == VK_ESCAPE && !st.escFire) {
			st.escArmed = true;
			break;
		}
		st.escFire = false;
		if (st.confirm) {
			if (wp == VK_ESCAPE) DoAction(st, kDelNo, 0);
			else if (wp == VK_RETURN) DoAction(st, st.confirm == 2 ? kSaveOver : kDelOne, 0);
			break;
		}
		if (wp == VK_TAB) {
			TabStep(st, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
			break;
		}
		if (st.focus != kFocusNone) {
			FieldKey(st, msg, wp);
			break;
		}
		if (st.tabAction >= 0 && (wp == VK_RETURN || wp == VK_SPACE)) {
			DoAction(st, st.tabAction, st.tabArg);
			break;
		}
		const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		switch (wp) {
		case VK_ESCAPE:
			if (st.renaming) DoAction(st, kRenameCancel, 0);
			else DoAction(st, kClose, 0);
			break;
		case VK_RETURN: DoAction(st, st.mode == 1 ? kSave : kLoad, 0); break;
		case VK_UP: MoveSelection(st, -1, false, shift); break;
		case VK_DOWN: MoveSelection(st, 1, false, shift); break;
		case VK_PRIOR: MoveSelection(st, -8, false, shift); break;
		case VK_NEXT: MoveSelection(st, 8, false, shift); break;
		case VK_HOME: MoveSelection(st, -1, true, shift); break;
		case VK_END: MoveSelection(st, 1, true, shift); break;
		case 'A':
			if (GetKeyState(VK_CONTROL) & 0x8000) SelectAll(st);
			break;
		case VK_DELETE: DoAction(st, kDelete, 0); break;
		case VK_F2: DoAction(st, kRename, 0); break;
		case VK_LEFT: case VK_RIGHT: {
			const int next = st.tab + (wp == VK_RIGHT ? 1 : -1);
			if (next >= 0 && next < kTabCount) DoAction(st, kTab, next);
			break;
		}
		case 'F':
			if (GetKeyState(VK_CONTROL) & 0x8000) {
				st.focus = kFocusSearch;
				EditSelectAll(st.search, st.edit);
			}
			break;
		}
		break;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (wp == VK_ESCAPE && st.escArmed) {
			st.escArmed = false;
			st.escFire = true;
			lock.unlock();
			return HandleImpl(WM_KEYDOWN, VK_ESCAPE, 0);
		}
		break;
	default:
		consumed = false;
	}
	if (!st.pendingDelete.empty()) {
		const std::vector<SaveRec> list = std::move(st.pendingDelete);
		st.pendingDelete.clear();
		lock.unlock();
		bool ok = true;
		for (const SaveRec &r : list) ok = SaveLibDelete(r) && ok;
		ModUiToast(ok ? (list.size() == 1 ? "Save deleted" : Tr("Game deleted (") + std::to_string(list.size()) + Tr(" saves)")) : "Couldn't delete", !ok);
		lock.lock();
		st.busy = false;
		st.recs = SaveLibScan();
		FixSelection(st);
		UpdatePlayers(st);
		ScrollToSelection(st);
		return true;
	}
	if (!st.pendingSave.empty() || st.pendingCancel) {
		void *dlg = st.dlg;
		const uintptr_t global = st.dlgGlobal;
		const std::string name = st.pendingSave;
		const bool online = st.pendingSaveOnline;
		const bool cancel = st.pendingCancel;
		st.pendingSave.clear();
		st.pendingCancel = false;
		lock.unlock();
		if (cancel) ExecuteCancel(dlg, global);
		else ExecuteSave(dlg, name, online);
		return true;
	}
	if (!st.pendingOnlineRoom.empty()) {
		const std::string path = st.pendingOnlineRoom;
		void *dlg = st.dlg;
		const uintptr_t global = st.dlgGlobal;
		st.pendingOnlineRoom.clear();
		lock.unlock();
		LogI("saves", "\"%s\" is online: leaving the game to open a room with it", path.c_str());
		if (DialogAlive(dlg, global)) {
			*reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x80) = 0;
			reinterpret_cast<Close_t>((*reinterpret_cast<void ***>(dlg))[0x6c / 4])(dlg, nullptr, 0);
		}
		MpUiHostSaveAfterGame(path);
		typedef void(__cdecl * RequestQuitToMenu_t)();
		reinterpret_cast<RequestQuitToMenu_t>(addr::kRequestQuitToMenu)();
		return true;
	}
	if (!st.pendingLoad.empty()) {
		const SaveRec *rec = FindRec(st, st.pendingLoad);
		const SaveRec copy = rec ? *rec : SaveRec();
		void *dlg = st.dlg;
		const uintptr_t global = st.dlgGlobal;
		st.pendingLoad.clear();
		lock.unlock();
		if (!copy.vfsPath.empty()) {
			ExecuteLoad(copy);
			if (DialogAlive(dlg, global)) {
				*reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x80) = 0;
				reinterpret_cast<Close_t>((*reinterpret_cast<void ***>(dlg))[0x6c / 4])(dlg, nullptr, 0);
			}
		}
		return true;
	}
	return consumed;
}

void DrawImpl(uint32_t *buf, int w, int h)
{
	State st;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.open) return;
		st = g_s;
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kDesignW, kDesignH)) return;
	r.Focus(st.tabAction, st.tabArg);
	r.Press(st.hits, st.mouseDown, POINT{st.mouseX, st.mouseY});
	st.modGeom = ListGeom();
	PaintPanel(r.p, st);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.open) return;
	g_s.hits = std::move(r.hits);
	g_s.rowPaths = std::move(st.rowPaths);
	g_s.hdrKeys = std::move(st.hdrKeys);
	g_s.hdrPaths = std::move(st.hdrPaths);
	g_s.listGeom = st.listGeom;
	g_s.modGeom = st.modGeom;
	if (g_s.modListKey != st.modListKey) {
		g_s.modListKey = st.modListKey;
		g_s.modList = ListState();
	}
	g_s.list.scroll = std::max(0, std::min(g_s.list.scroll, g_s.listGeom.maxScroll));
}
}
