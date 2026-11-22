// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <mutex>
#include "core/guard.h"
#include "i18n/tr.h"

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
std::string IoTagsText(const GbMod &m)
{
	std::vector<std::string> parts;
	if (m.facts.category >= 0) parts.push_back(Tr(CatalogCategoryLabel(m.facts.category)));
	else if (!m.category.empty()) parts.push_back(m.category);
	const uint32_t extra = CatalogCategoriesOf(m.facts) & ~(m.facts.category >= 0 ? 1u << m.facts.category : 0u);
	for (int c = 0; c < kCatCount; c++) {
		if ((extra >> c) & 1) parts.push_back(Tr(CatalogCategoryLabel(c)));
	}
	if (m.facts.players) parts.push_back(CatalogPlayersText(m.facts.players) + Tr(" players"));
	for (int l = 0; l < kLangCount; l++) {
		if ((m.facts.languages >> l) & 1) parts.push_back(Tr(CatalogLanguageLabel(l)));
	}
	for (const std::string &t : m.tags) {
		if (t == m.category || CatalogTagIsFacet(t)) continue;
		parts.push_back(Tr(t));
	}
	std::string out;
	for (const std::string &s : parts) out += (out.empty() ? "" : ", ") + s;
	return out;
}
}

namespace modui_impl {
std::mutex g_lock;
State g_s;

RichTextState g_rich;
ListGeom g_listGeomOut;
DropListState g_drop;
}

namespace modui_impl {
namespace {
std::string LowerAscii(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string ShortPath(const std::string &path)
{
	const size_t kMax = 48;
	if (path.size() <= kMax) return path;
	size_t cut = path.size() - (kMax - 3);
	while (cut < path.size() && (static_cast<unsigned char>(path[cut]) & 0xC0) == 0x80) cut++;
	return "..." + path.substr(cut);
}
}
}

namespace modui_impl {
const ModInfo *FindModIn(const std::vector<ModInfo> &mods, const std::string &folder)
{
	for (const ModInfo &m : mods) {
		if (m.folder == folder) return &m;
	}
	return nullptr;
}

const ModInfo *FindMod(const std::string &folder) { return FindModIn(g_s.mods, folder); }

std::string OverlapText(const ModOverlap &ov)
{
	const std::string files = std::to_string(ov.files);
	const std::string choice = ov.chosen > 0 ? Tr(" (your choice)") : "";
	if (ov.mine == ov.files) return TrFmt("Same files as %s (%s): the ones from this pack are used", {ov.name, files}) + choice;
	if (ov.theirs == ov.files) return TrFmt("Same files as %s (%s): the ones from %s are used", {ov.name, files, ov.name}) + choice;
	return TrFmt("Same files as %s (%s): %s from this pack and %s from the other are used", {ov.name, files, std::to_string(ov.mine), std::to_string(ov.theirs)}) + choice;
}

const Row *SelectedRow(const State &st)
{
	return st.selected >= 0 && st.selected < static_cast<int>(st.rows.size()) ? &st.rows[st.selected] : nullptr;
}

const ModIssues *SelectedIssues(const State &st)
{
	const Row *sel = SelectedRow(st);
	if (!sel) return nullptr;
	const auto it = st.issues->find(sel->folder);
	return it == st.issues->end() ? nullptr : &it->second;
}

bool Pending(const State &st, const std::string &folder)
{
	return std::find(st.pendingUninstall.begin(), st.pendingUninstall.end(), folder) != st.pendingUninstall.end();
}

CatalogFacts ModFactsOf(const ModInfo &m)
{
	CatalogFacts facts = CatalogFactsOf(m.category, m.tags);
	if (!CatalogCategoriesOf(facts)) facts.category = m.gameplay ? kCatGameplay : m.kind == kModContent ? kCatScenarios : kCatGraphics;
	return facts;
}

uint32_t ModCategoriesOf(const ModInfo &m)
{
	return CatalogCategoriesOf(ModFactsOf(m));
}

int ModMainCategory(const ModInfo &m)
{
	const CatalogFacts f = ModFactsOf(m);
	if (f.category >= 0) return f.category;
	const uint32_t cats = CatalogCategoriesOf(f);
	for (int c = 0; c < kCatCount; c++) {
		if ((cats >> c) & 1) return c;
	}
	return kCatGameplay;
}

bool RowHasProblems(const State &st, const Row &r)
{
	if (!FindModIn(st.mods, r.folder)) return true;
	const auto it = st.issues->find(r.folder);
	return it != st.issues->end() && !it->second.problems.empty();
}

ClashSummary ClashSummaryOf(const State &st, const std::string &folder)
{
	ClashSummary out;
	const auto it = st.issues->find(folder);
	if (it == st.issues->end()) return out;
	for (const ModOverlap &ov : it->second.overlaps) {
		out.pairs++;
		out.wins += ov.mine;
		out.loses += ov.theirs;
		if (ov.problem && !out.problem) {
			out.problem = true;
			out.problemWith = ov.name;
		}
	}
	out.values = static_cast<int>(it->second.values.size());
	return out;
}

const ModOrderEntry *OrderEntryOf(const State &st, const std::string &folder)
{
	for (const ModOrderEntry &e : st.order) {
		if (_stricmp(e.folder.c_str(), folder.c_str()) == 0) return &e;
	}
	return nullptr;
}

const char *OrderGroupName(int group)
{
	switch (group) {
	case kModCosmetic: return "Cosmetic";
	case kModContent: return "Maps and campaigns";
	case kModRules: return "Gameplay";
	default: return "Disabled";
	}
}

std::string OrderReasonText(const State &st, const ModOrderEntry &e)
{
	std::string names;
	for (size_t i = 0; i < e.related.size(); i++) {
		const ModInfo *m = FindModIn(st.mods, e.related[i]);
		names += (i ? ", " : "") + (m ? m->name : e.related[i]);
	}
	switch (e.reason) {
	case kOrderNeeds: return TrFmt("Above %s, which it needs.", {names});
	case kOrderNeeded: return TrFmt("Below %s, which needs it.", {names});
	case kOrderAuthor: return TrFmt("Its author asks for it to load next to %s.", {names});
	default: break;
	}
	const std::string kind = e.kind == kModCosmetic ? Tr("Cosmetic: it goes above maps and gameplay.")
		: e.kind == kModContent						? Tr("Maps and campaigns: below the cosmetic packs, above gameplay.")
													: Tr("Gameplay: it goes at the end, below everything else.");
	return e.reason == kOrderByName ? kind + " " + Tr("Within its group, by name.") : kind;
}

bool RowInState(const State &st, const Row &r, int state)
{
	switch (state) {
	case kStateActive: return r.active;
	case kStateDisabled: return !r.active;
	case kStateConflicts: {
		if (!r.active) return false;
		const ClashSummary c = ClashSummaryOf(st, r.folder);
		return c.pairs > 0 || c.values > 0;
	}
	case kStateProblems: return RowHasProblems(st, r);
	default: return true;
	}
}

bool IsMarked(const State &st, const std::string &folder)
{
	return std::find(st.marked.begin(), st.marked.end(), folder) != st.marked.end();
}

std::vector<std::string> Targets(const State &st)
{
	if (!st.marked.empty()) return st.marked;
	if (const Row *sel = SelectedRow(st)) return {sel->folder};
	return {};
}

// The search `q` comes in lower case.
bool RowMatches(const State &st, const Row &r, const std::string &q, const CatalogFilter &filter)
{
	const ModInfo *m = FindModIn(st.mods, r.folder);
	if (!q.empty()) {
		std::string hay = r.folder;
		if (m) {
			hay += " " + m->name + " " + m->description;
			for (const std::string &a : m->authors) hay += " " + a;
		}
		if (LowerAscii(hay).find(q) == std::string::npos) return false;
	}
	return CatalogFilterEmpty(filter) || (m && CatalogMatches(ModFactsOf(*m), filter));
}

void CountStates(const State &st, const CatalogFilter &filter, int counts[kStateCount])
{
	const std::string q = LowerAscii(st.searchText);
	for (int k = 0; k < kStateCount; k++) counts[k] = 0;
	for (const Row &r : st.rows) {
		if (!RowMatches(st, r, q, filter)) continue;
		for (int k = 0; k < kStateCount; k++) {
			if (RowInState(st, r, k)) counts[k]++;
		}
	}
}

std::vector<int> VisibleRows(const State &st, int state, const CatalogFilter &filter)
{
	std::vector<int> out;
	const std::string q = LowerAscii(st.searchText);
	for (size_t i = 0; i < st.rows.size(); i++) {
		const Row &r = st.rows[i];
		if (RowInState(st, r, state) && RowMatches(st, r, q, filter)) out.push_back(static_cast<int>(i));
	}
	const auto mod = [&](int i) { return FindModIn(st.mods, st.rows[i].folder); };
	if (st.sort == kSortPriority) {
		const auto rank = [&](int i) {
			const ModOrderEntry *e = st.rows[i].active ? OrderEntryOf(st, st.rows[i].folder) : nullptr;
			return e ? e->position : 1 << 20;
		};
		std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return rank(a) < rank(b); });
	}
	else if (st.sort == kSortActiveFirst) {
		std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return st.rows[a].active && !st.rows[b].active; });
	}
	else if (st.sort == kSortRecent) {
		std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
			const ModInfo *ma = mod(a), *mb = mod(b);
			return (ma ? ma->installedTime : 0) > (mb ? mb->installedTime : 0);
		});
	}
	else if (st.sort == kSortSize) {
		std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
			const ModInfo *ma = mod(a), *mb = mod(b);
			return (ma ? ma->totalBytes : 0) > (mb ? mb->totalBytes : 0);
		});
	}
	return out;
}

std::vector<int> VisibleRows(const State &st)
{
	return VisibleRows(st, st.stateFilter, st.catFilter);
}

CatalogCounts InstalledCounts(const State &st)
{
	CatalogCounts counts;
	for (const ModInfo &m : st.mods) CatalogCount(ModFactsOf(m), counts);
	return counts;
}

void BuildRows()
{
	g_s.rows.clear();
	const auto isActive = [](const std::string &folder) {
		return std::find(g_s.profile.mods.begin(), g_s.profile.mods.end(), folder) != g_s.profile.mods.end();
	};
	for (const ModInfo &m : g_s.mods) {
		Row r;
		r.folder = m.folder;
		r.active = isActive(m.folder);
		g_s.rows.push_back(r);
	}
	for (const std::string &folder : g_s.profile.mods) {
		if (FindMod(folder)) continue;
		Row r;
		r.folder = folder;
		g_s.rows.push_back(r);
	}
	const auto key = [](const Row &r) {
		const ModInfo *m = FindMod(r.folder);
		std::string k = (m ? m->name : r.folder) + "\x01" + r.folder;
		for (char &c : k) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return k;
	};
	std::sort(g_s.rows.begin(), g_s.rows.end(), [&](const Row &a, const Row &b) { return key(a) < key(b); });
	if (g_s.selected >= static_cast<int>(g_s.rows.size())) {
		g_s.selected = static_cast<int>(g_s.rows.size()) - 1;
	}
	if (g_s.selected < 0) {
		g_s.selected = 0;
	}
}

void BuildUsage()
{
	g_s.usedBy.clear();
	g_s.pendingUninstall.clear();
	for (const ModInfo &m : g_s.mods) {
		g_s.usedBy[m.folder] = ModsProfilesUsing(m.folder);
		if (ModsUninstallPending(m.folder)) g_s.pendingUninstall.push_back(m.folder);
	}
	g_s.issues = std::make_shared<const std::map<std::string, ModIssues>>(ModsIssuesForAll(g_s.profile));
	g_s.problems = ModsProblemsFrom(g_s.profile, *g_s.issues);
	g_s.order = ModsLoadOrderExplained(g_s.profile);
}

void Reload()
{
	g_s.listSerial = ModsListSerial();
	g_s.mods = ModsSummaries();
	g_s.profileNames = ModsProfileNames();
	g_s.profile = ModsLoadProfile(ModsActiveProfileName());
	BuildRows();
	BuildUsage();
	g_s.lastRefresh = GetTickCount();
	g_s.restartNeeded = ModsRestartNeeded();
}

int FieldOfAction(int action)
{
	return action == kFocusSearch ? kSearchField
		: action == kIoFocusSearch ? kIoSearchField
		: action == kFocusName	   ? kNameField
		: action == kFocusImport   ? kImportField
								   : kNoField;
}
int ActionOfField(int field)
{
	return field == kSearchField ? kFocusSearch
		: field == kIoSearchField ? kIoFocusSearch
		: field == kNameField	  ? kFocusName
		: field == kImportField	  ? kFocusImport
								  : -1;
}
std::string &FieldRef(int field)
{
	return field == kSearchField ? g_s.searchText : field == kIoSearchField ? g_s.ioSearch : field == kImportField ? g_s.importText : g_s.nameText;
}

void ImportTick()
{
	// The packs of an imported profile that were missing: their GameBanana page is asked for first, then they install, and each
	// one that arrives joins the profile.
	for (size_t i = 0; i < g_s.importWant.size();) {
		const std::shared_ptr<const GbMod> info = GbInfo(g_s.importWant[i]);
		if (!info) {
			i++;
			continue;
		}
		if (info->hasFile) {
			GbInstall(*info);
			g_s.importLoading.push_back(g_s.importWant[i]);
		}
		else {
			SetStatus(TrFmt("%s has no file to download on GameBanana", {info->name}));
		}
		g_s.importWant.erase(g_s.importWant.begin() + static_cast<std::ptrdiff_t>(i));
	}
	bool added = false;
	for (size_t i = 0; i < g_s.importLoading.size();) {
		const auto it = std::find_if(g_s.mods.begin(), g_s.mods.end(), [&](const ModInfo &m) { return m.gbId == g_s.importLoading[i]; });
		if (it == g_s.mods.end()) {
			i++;
			continue;
		}
		added = ModsAddToProfile(g_s.importProfile, it->folder) || added;
		g_s.importLoading.erase(g_s.importLoading.begin() + static_cast<std::ptrdiff_t>(i));
	}
	if (added && g_s.profile.name == g_s.importProfile) {
		g_s.profile = ModsLoadProfile(g_s.importProfile);
		ModsApplyWithoutRestart();
		BuildRows();
		BuildUsage();
		g_s.restartNeeded = ModsRestartNeeded();
	}
	if (g_s.importWant.empty() && g_s.importLoading.empty()) g_s.importProfile.clear();
}
void FocusField(int field)
{
	g_s.focus = field;
	g_s.tabAction = -1;
	if (field != kNoField) EditToEnd(FieldRef(field), g_s.edit);
}

void SetStatus(const std::string &s)
{
	if (s.empty()) return;
	g_s.toast = s;
	g_s.toastErr = false;
	g_s.toastLen = 6000;
	g_s.toastUntil = GetTickCount() + g_s.toastLen;
	LogI("ui", "notice: %s", TrSource(s).c_str());
}

bool FindUpdateFor(const State &st, const ModInfo &m, CatalogEntry &out)
{
	return DlUpdateFor(m, st.mods, out);
}
}

namespace modui_impl {
namespace {
bool ProfileExists(const std::string &name)
{
	for (const std::string &n : g_s.profileNames) {
		if (_stricmp(n.c_str(), name.c_str()) == 0) return true;
	}
	return false;
}

std::string UniqueProfileName(const std::string &base)
{
	for (int n = 1;; n++) {
		std::string name = n == 1 ? base : base + " " + std::to_string(n);
		if (!ProfileExists(name)) return name;
	}
}
}
}

namespace modui_impl {
void SwitchProfile(const std::string name)
{
	ModsSetActiveProfileName(name);
	ModsApplyWithoutRestart();
	Reload();
	SetStatus(ModsRestartNeeded() ? Tr("Profile \"") + ModsProfileLabel(name) + Tr("\" selected: applied on restart") : "");
}

void CommitProfile()
{
	if (g_s.profile.name == kOriginalName) {
		ModProfile mine = ModsLoadProfile(kNoProfileName);
		for (const std::string &f : g_s.profile.mods) {
			const bool has = std::find_if(mine.mods.begin(), mine.mods.end(), [&](const std::string &m) { return _stricmp(m.c_str(), f.c_str()) == 0; }) != mine.mods.end();
			if (!has) mine.mods.push_back(f);
		}
		g_s.profile = mine;
		SetStatus("Enabled in \"My content\". The Original always stays without content.");
	}
	ModsSaveProfile(g_s.profile);
	ModsSetActiveProfileName(g_s.profile.name);
	ModsApplyWithoutRestart();
	g_s.profileNames = ModsProfileNames();
	BuildRows();
	BuildUsage();
}

void SelectFolder(const std::string &folder)
{
	for (size_t i = 0; i < g_s.rows.size(); i++) {
		if (g_s.rows[i].folder == folder) g_s.selected = static_cast<int>(i);
	}
}

void FinishName()
{
	std::string name = g_s.nameText;
	name.erase(0, name.find_first_not_of(" \t"));
	name.erase(name.find_last_not_of(" \t") + 1);
	if (name.empty()) {
		SetStatus("Enter a name for the profile");
		return;
	}
	if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
		SetStatus("The name cannot contain \\ / : * ? \" < > |");
		return;
	}
	if (ModsIsReservedProfileName(name) ||
		(ProfileExists(name) && !(g_s.nameMode == kNameRename && _stricmp(name.c_str(), g_s.profile.name.c_str()) == 0))) {
		SetStatus(Tr("There is already a profile named \"") + name + "\"");
		return;
	}
	const int mode = g_s.nameMode;
	g_s.nameMode = kNameNone;
	g_s.focus = kNoField;
	if (mode == kNameRename) {
		const std::string old = g_s.profile.name;
		if (old == name) return;
		if (ModsRenameProfile(old, name)) {
			Reload();
			SetStatus(Tr("Profile \"") + old + Tr("\" renamed to \"") + name + "\"");
		}
		else {
			SetStatus("Could not rename the profile");
		}
		return;
	}
	ModProfile p;
	p.name = name;
	if (mode == kNameSaveAs) {
		p.mods = g_s.profile.mods;
		p.choices = g_s.profile.choices;
		p.files = g_s.profile.files;
	}
	ModsSaveProfile(p);
	SwitchProfile(name);
	SetStatus(mode == kNameNew ? Tr("Profile \"") + name + Tr("\" created (empty): enable its content in the list")
		: Tr("Profile \"") + name + Tr("\" saved"));
}

void StartName(int mode)
{
	g_s.nameMode = mode;
	g_s.focus = kNameField;
	g_s.tabAction = -1;
	g_s.drop = kDropNone;
	g_s.nameText = mode == kNameRename ? g_s.profile.name
		: UniqueProfileName(Tr("Profile"));
	EditSelectAll(g_s.nameText, g_s.edit);
}
}

namespace modui_impl {
namespace {
const ModInfo *InstalledVersionOf(const State &st, const GbMod &m, const std::string &version)
{
	for (const ModInfo &mi : st.mods) {
		const bool same = m.id && mi.gbId == m.id;
		if (same && mi.version == version) return &mi;
	}
	return nullptr;
}

std::string DateText(uint64_t unix)
{
	if (!unix) return "";
	const __time64_t t = static_cast<__time64_t>(unix);
	tm local = {};
	if (_localtime64_s(&local, &t) != 0) return "";
	char buf[16];
	strftime(buf, sizeof(buf), "%d/%m/%Y", &local);
	return buf;
}
}
}

namespace modui_impl {
std::vector<std::string> DropItems(const State &st)
{
	std::vector<std::string> items;
	if (st.drop == kDropVersions) {
		if (!st.verHas) return {"Loading versions..."};
		const GbFiles f = GbGetFiles(st.verMod.id);
		if (f.loading) return {"Loading versions..."};
		if (!f.error.empty()) return {"Could not read the versions"};
		if (f.files.empty()) return {"This content pack has no published versions"};
		const GbFile *latest = GbLatestFile(f.files);
		for (size_t i = 0; i < f.files.size(); i++) {
			const GbFile &v = f.files[i];
			std::string s = v.version.empty() ? std::string("No version") : v.version;
			if (v.bytes) s += "  ·  " + HumanBytes(v.bytes);
			const std::string d = DateText(v.date);
			if (!d.empty()) s += "  ·  " + d;
			if (&v == latest) s += "  ·  latest";
			if (InstalledVersionOf(st, st.verMod, v.version)) s += "  ·  installed";
			items.push_back(s);
		}
		return items;
	}
	if (st.drop == kDropClashFiles) {
		if (const ModIssues *is = SelectedIssues(st)) {
			for (const ModOverlapFile &f : is->files) items.push_back(ShortPath(f.path) + "  \xE2\x86\x92  " + f.usedName + (f.chosen ? Tr(" (your choice)") : ""));
			for (const ModOverlapValue &v : is->values) items.push_back(v.file + "  " + ShortPath(v.label) + "  \xE2\x86\x92  " + v.usedName);
		}
		return items;
	}
	if (st.drop == kDropProfile) {
		for (const std::string &n : st.profileNames) items.push_back(ModsProfileLabel(n));
		items.push_back("+ New profile");
	}
	else if (st.drop == kDropMenu) {
		items = {"Rename", "Remove"};
	}
	else if (st.drop == kDropSort) {
		for (int i = 0; i < kSortCount; i++) items.push_back(kSortNames[i]);
	}
	else if (st.drop == kDropModMenu || st.drop == kDropRowMenu) {
		items = RowMenuItems(st);
	}
	else if (st.drop == kDropIoSort) {
		for (const char *s : kIoSortNames) items.push_back(s);
	}
	return items;
}

void IoSearch()
{
	GbQuery q;
	q.search = g_s.ioSearch;
	q.filter = g_s.ioFilter;
	q.sort = g_s.ioSort;
	q.page = g_s.ioPage;
	q.perPage = kIoPerPage;
	g_s.ioDetail = -1;
	g_s.ioRequested = true;
	GbSearch(q);
}
}

namespace modui_impl {
namespace {
HWND GameWindow()
{
	HWND h = GetForegroundWindow();
	DWORD pid = 0;
	GetWindowThreadProcessId(h, &pid);
	return pid == GetCurrentProcessId() ? h : nullptr;
}
}
}

namespace modui_impl {
void OpenWeb(const std::string &url)
{
	if (url.compare(0, 8, "https://") != 0) return;
	HWND game = GameWindow();
	SafeThread([url, game] {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (game && !(GetWindowLongA(game, GWL_STYLE) & WS_CAPTION)) PostMessageA(game, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		CoUninitialize();
	}).detach();
}

void OpenFolderAsync(const std::string &folder)
{
	HWND game = GameWindow();
	SafeThread([folder, game] {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (game && !(GetWindowLongA(game, GWL_STYLE) & WS_CAPTION)) PostMessageA(game, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		ModsOpenFolder(folder);
		CoUninitialize();
	}).detach();
}

void PickDrop(int index)
{
	const int drop = g_s.drop;
	g_s.drop = kDropNone;
	if (drop == kDropProfile) {
		const int n = static_cast<int>(g_s.profileNames.size());
		if (index == n) StartName(kNameNew);
		else if (index >= 0 && index < n) SwitchProfile(g_s.profileNames[index]);
	}
	else if (drop == kDropMenu) {
		if (index == 0) StartName(kNameRename);
		else if (index == 1) {
			g_s.confirm = true;
			g_s.confirmKind = kConfirmDelete;
		}
	}
	else if (drop == kDropSort) {
		g_s.sort = std::max(0, std::min(kSortCount - 1, index));
		g_s.list.scroll = 0;
	}
	else if (drop == kDropModMenu || drop == kDropRowMenu) {
		g_s.drop = drop;
		RunRowMenu(index);
	}
	else if (drop == kDropVersions) {
		if (!g_s.verHas) return;
		const GbFiles f = GbGetFiles(g_s.verMod.id);
		if (index < 0 || index >= static_cast<int>(f.files.size())) return;
		const GbFile &v = f.files[index];
		if (InstalledVersionOf(g_s, g_s.verMod, v.version)) {
			SetStatus(Tr("You already have version ") + v.version + Tr(" of ") + g_s.verMod.name);
			return;
		}
		GbInstallVersion(g_s.verMod, v);
	}
	else if (drop == kDropIoSort) {
		g_s.ioSort = std::max(0, std::min(3, index));
		g_s.ioPage = 0;
		IoSearch();
	}
}


ListGeom g_confirmGeomOut;

Canvas g_canvas;
Canvas g_toastCanvas;
}

namespace modui_impl {
HoverState g_hover;

SpectatorBarState g_spec;
}

void ModUiDrawHoverCard(uint32_t *buf, int w, int h, const std::string &text, COLORREF accent, int atX, int centerY, const std::vector<ControlKey> &keys)
{
	static Canvas canvas;
	const float s = std::max(kMinScale, std::min(MaxWindowScale(w, h), h / 1080.0f * 1.2f));
	Card card;
	card.text = text;
	card.accent = accent;
	card.atX = atX;
	card.atCenterY = centerY;
	card.keys = keys;
	DrawCard(canvas, buf, w, h, card, s, 235);
}

void ModUiOpen()
{
	ModsReadPatchedFiles();
	std::lock_guard<std::mutex> lock(g_lock);
	Reload();
	g_s.open = true;
	g_s.confirm = false, g_s.confirmItems.clear();
	g_s.confirmOnly = false;
	g_s.drop = kDropNone;
	g_s.nameMode = kNameNone;
	g_s.dlFinished = DlStatus().finished;
	if (!g_s.catalogRequested) {
		g_s.catalogRequested = true;
		DlRefreshCatalogs();
	}
	g_s.restartReason.clear();
	const std::vector<std::string> pruned = ModsTakePruned();
	if (!pruned.empty()) {
		std::string names;
		for (const std::string &f : pruned) names += (names.empty() ? "" : ", ") + f;
		SetStatus(TrFmt("Removed from the profiles because they are no longer in the content folder: %s", {names}));
	}
	ModsWatchStart();
	LogI("content", "mod manager: open (profile \"%s\", %u mods installed)", g_s.profile.name.c_str(),
		static_cast<unsigned>(g_s.mods.size()));
}

void ModUiAskRestart(const std::string &reason, int largeAddress, const std::vector<std::string> &items, const std::string &itemsTitle)
{
	std::lock_guard<std::mutex> lock(g_lock);
	Reload();
	if (!g_s.open) {
		g_s.open = true;
		g_s.confirmOnly = true;
	}
	g_s.confirm = true;
	g_s.confirmKind = kConfirmRestart;
	g_s.restartReason = reason;
	g_s.restartLargeAddress = largeAddress;
	g_s.confirmItems = items;
	g_s.confirmItemsTitle = itemsTitle;
	g_s.confirmList = ListState();
}

void ModUiNotice(const std::string &title, const std::string &text)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.open) {
		g_s.open = true;
		g_s.confirmOnly = true;
	}
	g_s.confirm = true;
	g_s.confirmKind = kConfirmNotice;
	g_s.noticeTitle = title;
	g_s.noticeText = text;
	g_s.confirmItems.clear();
}

void ModUiAsk(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> onYes)
{
	ModUiChoose(title, text, yes, std::move(onYes), std::string(), nullptr);
}

void ModUiChoose(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> onYes, const std::string &alt,
	std::function<void()> onAlt)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.open) {
		g_s.open = true;
		g_s.confirmOnly = true;
	}
	g_s.confirm = true;
	g_s.confirmKind = kConfirmAction;
	g_s.noticeTitle = title;
	g_s.noticeText = text;
	g_s.actionYes = yes;
	g_s.action = std::move(onYes);
	g_s.actionAlt = alt;
	g_s.alt = std::move(onAlt);
	g_s.confirmDeadline = 0;
	g_s.confirmItems.clear();
}

void ModUiChooseTimed(const std::string &title, const std::string &text, const std::string &yes, void (*onYes)(), const std::string &alt, void (*onAlt)(),
	unsigned deadline)
{
	ModUiChoose(title, text, yes, onYes, alt, onAlt);
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.confirmDeadline = deadline ? deadline : 1;
}

void ModUiCancelTimed()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.confirm || !g_s.confirmDeadline) return;
	g_s.action = nullptr;
	g_s.alt = nullptr;
	DoAction(kConfirmNo, 0);
}

int ModUiUpdatesAvailable()
{
	return UpdatesAvailable();
}

void ModUiResetUpdates()
{
	ResetUpdatesCache();
}

bool ModUiIsOpen()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.open;
}

void ModUiToast(const std::string &text, bool error)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.toast = text;
	g_s.toastErr = error;
	g_s.toastLen = error ? 10000 : 6000;
	g_s.toastUntil = GetTickCount() + g_s.toastLen;
	LogI("ui", "notice: %s", TrSource(text).c_str());
}

namespace modui_impl {
namespace {
enum MenuCode { kMenuOn, kMenuOff, kMenuFolder, kMenuPage, kMenuVersions, kMenuUninstall };

std::vector<std::string> MenuTargets(const State &st)
{
	if (st.menuRow < 0 || st.menuRow >= static_cast<int>(st.rows.size())) return {};
	const std::string &folder = st.rows[st.menuRow].folder;
	if (IsMarked(st, folder)) return st.marked;
	return {folder};
}

std::vector<std::pair<std::string, int>> RowMenu(const State &st)
{
	std::vector<std::pair<std::string, int>> out;
	const std::vector<std::string> targets = MenuTargets(st);
	if (targets.empty()) return out;
	int on = 0;
	for (const Row &r : st.rows) {
		if (std::find(targets.begin(), targets.end(), r.folder) != targets.end() && r.active) on++;
	}
	const int n = static_cast<int>(targets.size());
	if (on < n) out.push_back({n == 1 ? "Enable" : TrFmt("Enable (%s)", {std::to_string(n - on)}), kMenuOn});
	if (on > 0) out.push_back({n == 1 ? "Disable" : TrFmt("Disable (%s)", {std::to_string(on)}), kMenuOff});
	if (n == 1) {
		out.push_back({"Open folder", kMenuFolder});
		const ModInfo *m = FindModIn(st.mods, targets[0]);
		if (m && m->gbId) out.push_back({"GameBanana page", kMenuPage});
	}
	out.push_back({n == 1 ? "Uninstall..." : TrFmt("Uninstall %s...", {std::to_string(n)}), kMenuUninstall});
	return out;
}

std::vector<std::pair<std::string, int>> ModMenu(const State &st)
{
	std::vector<std::pair<std::string, int>> out;
	const Row *sel = SelectedRow(st);
	if (!sel) return out;
	const ModInfo *m = FindModIn(st.mods, sel->folder);
	if (m && m->gbId) out.push_back({"Other versions...", kMenuVersions});
	out.push_back({"Uninstall...", kMenuUninstall});
	return out;
}

void RunMenu(int code, const std::vector<std::string> &targets)
{
	switch (code) {
	case kMenuOn: SetActive(targets, true); break;
	case kMenuOff: SetActive(targets, false); break;
	case kMenuFolder:
		if (targets.size() == 1) OpenFolderAsync(targets[0]);
		break;
	case kMenuPage:
		if (targets.size() == 1) {
			SelectFolder(targets[0]);
			DoAction(kOpenSource, 0);
		}
		break;
	case kMenuVersions:
		g_s.drop = kDropNone;
		DoAction(kDropOpen, kDropVersions);
		break;
	case kMenuUninstall: AskUninstall(targets); break;
	}
}
}

std::vector<std::string> RowMenuItems(const State &st)
{
	std::vector<std::string> out;
	for (const auto &item : st.drop == kDropModMenu ? ModMenu(st) : RowMenu(st)) out.push_back(item.first);
	return out;
}

void RunRowMenu(int index)
{
	const bool modMenu = g_s.drop == kDropModMenu;
	const std::vector<std::pair<std::string, int>> items = modMenu ? ModMenu(g_s) : RowMenu(g_s);
	std::vector<std::string> targets = MenuTargets(g_s);
	if (modMenu) {
		targets.clear();
		if (const Row *sel = SelectedRow(g_s)) targets.push_back(sel->folder);
	}
	g_s.drop = kDropNone;
	if (index < 0 || index >= static_cast<int>(items.size())) return;
	RunMenu(items[index].second, targets);
}
}
