// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <mutex>
#include "core/guard.h"

#include "ui/modui_internal.h"
#include "ui/textedit.h"

using namespace modui_impl;

namespace modui_impl {
void SetActive(const std::vector<std::string> &folders, bool on)
{
	auto &mods = g_s.profile.mods;
	std::vector<std::string> changed;
	for (const std::string &folder : folders) {
		if (Pending(g_s, folder)) continue;
		const bool active = std::find(mods.begin(), mods.end(), folder) != mods.end();
		if (active == on || (on && !FindMod(folder))) continue;
		if (on) mods.push_back(folder);
		else mods.erase(std::remove(mods.begin(), mods.end(), folder), mods.end());
		changed.push_back(folder);
	}
	if (changed.empty()) return;
	CommitProfile();
	if (changed.size() > 1) {
		SetStatus(TrFmt(on ? "%s content packs enabled" : "%s content packs disabled", {std::to_string(changed.size())}));
		return;
	}
	if (!on) return;
	const auto found = g_s.issues->find(changed[0]);
	if (found == g_s.issues->end()) return;
	const ModIssues &is = found->second;
	const ModInfo *m = FindMod(changed[0]);
	const std::string name = m ? m->name : changed[0];
	if (!is.problems.empty()) SetStatus(name + ": " + is.problems[0]);
	else if (!is.overlaps.empty()) SetStatus(name + ": " + OverlapText(is.overlaps[0]));
	else if (!is.notices.empty()) SetStatus(name + ": " + is.notices[0]);
}

void AskUninstall(const std::vector<std::string> &folders)
{
	std::vector<std::string> targets;
	for (const std::string &folder : folders) {
		if (!Pending(g_s, folder) && FindMod(folder)) targets.push_back(folder);
	}
	if (targets.empty()) return;
	g_s.uninstallTargets = targets;
	g_s.confirm = true;
	g_s.confirmKind = kConfirmUninstall;
	g_s.confirmItems.clear();
	g_s.confirmItemsTitle.clear();
	g_s.confirmList = ListState();
	std::vector<std::string> profiles;
	for (const std::string &folder : targets) {
		if (targets.size() > 1) {
			const ModInfo *m = FindMod(folder);
			g_s.confirmItems.push_back(m ? m->name : folder);
		}
		const auto used = g_s.usedBy.find(folder);
		if (used == g_s.usedBy.end()) continue;
		for (const std::string &n : used->second) {
			if (std::find(profiles.begin(), profiles.end(), n) == profiles.end()) profiles.push_back(n);
		}
	}
	if (!profiles.empty()) {
		if (targets.size() > 1) g_s.confirmItems.push_back("##They will be removed from these profiles:");
		else g_s.confirmItemsTitle = "It will be removed from these profiles:";
		for (const std::string &n : profiles) g_s.confirmItems.push_back(ModsProfileLabel(n));
	}
}

void DoAction(int action, int arg)
{
	switch (action) {
	case kDropOpen:
		if (arg == kDropVersions && g_s.drop != kDropVersions) {
			g_s.verHas = false;
			if (g_s.tab == 2) {
				if (g_s.ioDetail >= 0 && g_s.ioDetail < static_cast<int>(g_s.io.mods.size())) {
					g_s.verMod = g_s.io.mods[g_s.ioDetail];
					g_s.verHas = true;
				}
			}
			else if (const Row *sel = SelectedRow(g_s)) {
				const ModInfo *m = FindMod(sel->folder);
				const std::shared_ptr<const GbMod> io = m && m->gbId ? GbInfo(m->gbId) : nullptr;
				if (io) {
					g_s.verMod = *io;
					g_s.verHas = true;
				}
			}
			if (g_s.verHas) GbGetFiles(g_s.verMod.id);
		}
		g_s.drop = g_s.drop == arg ? kDropNone : arg;
		g_s.dropScroll = std::max(0, DropCurrent(g_s) - kMenuMaxRows / 2);
		break;
	case kDropClose: g_s.drop = kDropNone; break;
	case kDropPick: PickDrop(arg); break;
	case kSaveAs: StartName(kNameSaveAs); break;
	case kFocusName: FocusField(kNameField); break;
	case kNameOk: FinishName(); break;
	case kNameCancel:
		g_s.nameMode = kNameNone;
		g_s.focus = kNoField;
		break;
	case kFocusSearch: FocusField(kSearchField); break;
	case kDelProfile:
		if (g_s.profile.name == kOriginalName || g_s.profile.name == kNoProfileName) {
			SetStatus("That profile cannot be deleted");
		}
		else {
			const std::string old = g_s.profile.name;
			ModsDeleteProfile(old);
			SwitchProfile(kOriginalName);
			SetStatus(Tr("Profile \"") + old + Tr("\" deleted"));
		}
		break;
	case kRowClick: {
		if (arg < 0 || arg >= static_cast<int>(g_s.rows.size())) break;
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		const std::string folder = g_s.rows[arg].folder;
		if (shift && g_s.anchor >= 0 && g_s.anchor < static_cast<int>(g_s.rows.size())) {
			const std::vector<int> view = VisibleRows(g_s);
			const auto at = [&](int row) { return static_cast<int>(std::find(view.begin(), view.end(), row) - view.begin()); };
			int a = at(g_s.anchor), b = at(arg);
			if (a >= static_cast<int>(view.size())) a = b;
			if (a > b) std::swap(a, b);
			if (!ctrl) g_s.marked.clear();
			for (int k = a; k <= b && k < static_cast<int>(view.size()); k++) {
				const std::string &f = g_s.rows[view[k]].folder;
				if (!IsMarked(g_s, f)) g_s.marked.push_back(f);
			}
		}
		else if (ctrl) {
			const Row *sel = SelectedRow(g_s);
			if (g_s.marked.empty() && sel && g_s.selected != arg) g_s.marked.push_back(sel->folder);
			const auto it = std::find(g_s.marked.begin(), g_s.marked.end(), folder);
			if (it == g_s.marked.end()) g_s.marked.push_back(folder);
			else g_s.marked.erase(it);
			g_s.anchor = arg;
		}
		else {
			g_s.marked.clear();
			g_s.anchor = arg;
		}
		g_s.selected = arg;
		break;
	}
	case kMark: {
		if (arg < 0 || arg >= static_cast<int>(g_s.rows.size())) break;
		const std::string folder = g_s.rows[arg].folder;
		const auto it = std::find(g_s.marked.begin(), g_s.marked.end(), folder);
		if (it == g_s.marked.end()) g_s.marked.push_back(folder);
		else g_s.marked.erase(it);
		g_s.selected = arg;
		g_s.anchor = arg;
		break;
	}
	case kClearMarks: g_s.marked.clear(); break;
	case kStateFilter:
		g_s.stateFilter = std::max(0, std::min(kStateCount - 1, arg));
		g_s.list.scroll = 0;
		break;
	case kCatPanel: g_s.catOpen = !g_s.catOpen; break;
	case kCatClear:
		g_s.catFilter = CatalogFilter();
		g_s.list.scroll = 0;
		break;
	case kCatChip: {
		const int group = arg / kIoFilterGroupStride, bit = arg % kIoFilterGroupStride;
		if (arg < 0 || group > kIoFilterLanguages || bit >= 32) break;
		uint32_t &mask = group == kIoFilterTypes ? g_s.catFilter.types : group == kIoFilterPlayers ? g_s.catFilter.players : g_s.catFilter.languages;
		mask ^= 1u << bit;
		g_s.catFilter = CatalogNormalize(g_s.catFilter);
		g_s.list.scroll = 0;
		break;
	}
	case kBulkOn: SetActive(g_s.marked, true); break;
	case kBulkOff: SetActive(g_s.marked, false); break;
	case kBulkUninstall: AskUninstall(g_s.marked); break;
	case kSetActive:
		if (const Row *sel = SelectedRow(g_s)) {
			const Row r = *sel;
			SetActive({r.folder}, !r.active);
		}
		break;
	case kToggle: {
		if (arg < 0 || arg >= static_cast<int>(g_s.rows.size())) break;
		const Row r = g_s.rows[arg];
		g_s.selected = arg;
		SetActive({r.folder}, !r.active);
		break;
	}
	case kForget: {
		if (arg < 0 || arg >= static_cast<int>(g_s.rows.size())) break;
		const std::string folder = g_s.rows[arg].folder;
		if (FindMod(folder)) break;
		auto &mods = g_s.profile.mods;
		mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const std::string &f) { return _stricmp(f.c_str(), folder.c_str()) == 0; }), mods.end());
		CommitProfile();
		SetStatus(TrFmt("%s removed from the profile", {folder}));
		break;
	}
	case kUseFiles: {
		const ModIssues *is = SelectedIssues(g_s);
		const size_t k = static_cast<size_t>(arg / 2);
		if (!is || arg < 0 || k >= is->overlaps.size()) break;
		const std::string self = g_s.rows[g_s.selected].folder;
		const std::string other = is->overlaps[k].folder;
		const std::string use = arg % 2 == 0 ? self : other;
		ModsChooseFiles(g_s.profile, use, arg % 2 == 0 ? other : self);
		CommitProfile();
		const ModInfo *u = FindMod(use);
		const std::string name = u ? u->name : use;
		SetStatus(ModsRestartNeeded() ? TrFmt("The files of %s will be used after restarting", {name}) : TrFmt("The files of %s are used", {name}));
		break;
	}
	case kUseFile: {
		const ModIssues *is = SelectedIssues(g_s);
		const bool mine = arg % 2 == 1;
		const size_t k = static_cast<size_t>(arg / 2 / 4096), file = static_cast<size_t>(arg / 2 % 4096);
		if (!is || arg < 0 || k >= is->overlaps.size() || file >= is->files.size()) break;
		const std::string self = g_s.rows[g_s.selected].folder;
		const std::string use = mine ? self : is->overlaps[k].folder;
		const std::string path = is->files[file].path;
		ModsChooseFile(g_s.profile, path, use);
		CommitProfile();
		const ModInfo *u = FindMod(use);
		SetStatus(TrFmt("%s: the copy of %s is used", {path, u ? u->name : use}));
		break;
	}
	case kDisableOther: {
		const ModIssues *is = SelectedIssues(g_s);
		const Row *sel = SelectedRow(g_s);
		if (!sel) break;
		std::string folder = sel->folder;
		if (arg >= 0) {
			if (!is || arg >= static_cast<int>(is->overlaps.size())) break;
			folder = is->overlaps[static_cast<size_t>(arg)].folder;
		}
		SetActive({folder}, false);
		break;
	}
	case kClearChoices:
		if (const Row *sel = SelectedRow(g_s)) {
			ModsClearChoices(g_s.profile, sel->folder);
			CommitProfile();
			SetStatus("Back to the load order: the pack higher up wins");
		}
		break;
	case kTabOrder:
		g_s.tab = kTabListOrder;
		g_s.scroll.Reset();
		g_s.catOpen = false;
		g_s.ioFilterOpen = false;
		break;
	case kCardTab: g_s.cardTab = std::max(0, std::min(static_cast<int>(kCardFiles), arg)); break;
	case kCopyCode:
		ClipboardSet(ModsProfileCode(g_s.profile));
		SetStatus("Code copied: send it to whoever you want to share the profile with");
		break;
	case kPasteCode:
		g_s.importText = ClipboardGet();
		FocusField(kImportField);
		break;
	case kFocusImport: FocusField(kImportField); break;
	case kImportClear:
		// Also stops waiting for the packs of an import: what already started downloading still installs, but no longer joins it.
		g_s.importText.clear();
		g_s.importProfile.clear();
		g_s.importWant.clear();
		g_s.importLoading.clear();
		g_s.focus = kNoField;
		break;
	case kImportCode: {
		ModsSharedProfile sp;
		std::string error;
		if (!ModsParseProfileCode(g_s.importText, sp, error)) {
			SetStatus(error);
			break;
		}
		const std::string name = ModsImportProfile(sp);
		if (name.empty()) {
			SetStatus("The profile could not be saved");
			break;
		}
		g_s.importProfile = name;
		g_s.importWant.clear();
		g_s.importLoading.clear();
		for (const ModsSharedMod &m : sp.mods) {
			if (!m.installed && m.gbId) g_s.importWant.push_back(m.gbId);
		}
		g_s.importText.clear();
		SwitchProfile(name);
		SetStatus(g_s.importWant.empty() ? TrFmt("Profile \"%s\" imported", {ModsProfileLabel(name)})
										 : TrFmt("Profile \"%s\" imported: downloading %s packs", {ModsProfileLabel(name), std::to_string(g_s.importWant.size())}));
		break;
	}
	case kOpenMod: {
		const Row *sel = SelectedRow(g_s);
		OpenFolderAsync(sel ? sel->folder : "");
		break;
	}
	case kApply:
		if (ModsRestartNeeded()) {
			g_s.confirm = true;
			g_s.confirmKind = kConfirmRestart;
		}
		else {
			SetStatus("No changes to apply: the active profile is already loaded");
		}
		break;
	case kClose:
		g_s.open = false;
		g_s.catOpen = false;
		break;
	case kConfirmYes:
		if (g_s.confirmKind == kConfirmNotice) {
			DoAction(kConfirmNo, 0);
			break;
		}
		if (g_s.confirmKind == kConfirmAction) {
			std::function<void()> action = std::move(g_s.action);
			g_s.action = nullptr;
			g_s.alt = nullptr;
			DoAction(kConfirmNo, 0);
			if (action) action();
			break;
		}
		if (g_s.confirmKind == kConfirmUninstall) {
			g_s.confirm = false, g_s.confirmItems.clear();
			const std::vector<std::string> targets = g_s.uninstallTargets;
			g_s.uninstallTargets.clear();
			if (!targets.empty()) {
				g_s.busy = true;
				SafeThread([targets] {
					std::string last;
					for (const std::string &folder : targets) last = ModsUninstall(folder);
					std::lock_guard<std::mutex> lock(g_lock);
					for (const std::string &folder : targets) {
						g_s.marked.erase(std::remove(g_s.marked.begin(), g_s.marked.end(), folder), g_s.marked.end());
					}
					Reload();
					g_s.busy = false;
					SetStatus(targets.size() == 1 ? last : TrFmt("%s content packs uninstalled", {std::to_string(targets.size())}));
				}).detach();
			}
			break;
		}
		if (g_s.confirmKind == kConfirmDelete) {
			g_s.confirm = false, g_s.confirmItems.clear();
			DoAction(kDelProfile, 0);
			break;
		}
		LogI("content", "mod manager: restart to apply profile \"%s\"", g_s.profile.name.c_str());
		ModsRestartGame(g_s.restartLargeAddress);
		break;
	case kConfirmAlt: {
		std::function<void()> alt = std::move(g_s.alt);
		g_s.alt = nullptr;
		g_s.action = nullptr;
		DoAction(kConfirmNo, 0);
		if (alt) alt();
		break;
	}
	case kConfirmNo:
		g_s.confirm = false, g_s.confirmItems.clear();
		if ((g_s.confirmKind == kConfirmRestart || g_s.confirmKind == kConfirmNotice || g_s.confirmKind == kConfirmAction) && g_s.confirmOnly) {
			g_s.open = false;
		}
		g_s.confirmOnly = false;
		break;
	case kIoFilterOpen: g_s.ioFilterOpen = !g_s.ioFilterOpen; break;
	case kIoFilterClose: g_s.ioFilterOpen = false; break;
	case kIoFilterToggle: {
		const int group = arg / kIoFilterGroupStride, bit = arg % kIoFilterGroupStride;
		if (arg < 0 || group > kIoFilterLanguages || bit >= 32) break;
		uint32_t &mask = group == kIoFilterTypes ? g_s.ioFilter.types : group == kIoFilterPlayers ? g_s.ioFilter.players : g_s.ioFilter.languages;
		mask ^= 1u << bit;
		g_s.ioFilter = CatalogNormalize(g_s.ioFilter);
		g_s.ioPage = 0;
		IoSearch();
		break;
	}
	case kIoFilterClear:
		g_s.ioFilter = CatalogFilter();
		g_s.ioPage = 0;
		IoSearch();
		break;
	case kTabInstalled:
		g_s.tab = 0;
		g_s.scroll.Reset();
		g_s.catOpen = false;
		g_s.ioFilterOpen = false;
		break;
	case kTabExplore:
		g_s.catOpen = false;
		g_s.scroll.Reset();
		if (g_s.tab != 2) {
			g_s.tab = 2;
			IoSearch();
		}
		g_s.tab = 2;
		break;
	case kIoFocusSearch: FocusField(kIoSearchField); break;
	case kIoRetry:
		g_s.ioPage = 0;
		IoSearch();
		break;
	case kIoRefresh:
		GbForceRefresh();
		g_s.ioRefreshing = g_s.ioPage == 0;
		g_s.ioPage = 0;
		IoSearch();
		break;
	case kIoPrev:
		if (g_s.ioPage > 0) {
			g_s.ioPage--;
			IoSearch();
		}
		break;
	case kIoNext:
		if ((g_s.ioPage + 1) * kIoPerPage < g_s.io.total) {
			g_s.ioPage++;
			IoSearch();
		}
		break;
	case kIoOpen:
		g_s.ioDetail = arg;
		g_s.ioShot = -1;
		break;
	case kIoBack:
		g_s.ioDetail = -1;
		g_s.ioShot = -1;
		break;
	case kIoShotOpen: g_s.ioShot = arg; break;
	case kIoShotClose: g_s.ioShot = -1; break;
	case kIoShotStep:
		if (g_s.ioDetail >= 0 && g_s.ioDetail < static_cast<int>(g_s.io.mods.size()) && g_s.ioShot >= 0) {
			const int n = static_cast<int>(g_s.io.mods[g_s.ioDetail].shots.size());
			if (n > 0) g_s.ioShot = ((g_s.ioShot + arg) % n + n) % n;
		}
		break;
	case kIoInstall:
		if (arg >= 0 && arg < static_cast<int>(g_s.io.mods.size())) {
			const GbMod &m = g_s.io.mods[arg];
			GbInstall(m);
		}
		break;
	case kOpenSource:
		if (const Row *sel = SelectedRow(g_s)) {
			const ModInfo *m = FindMod(sel->folder);
			const std::shared_ptr<const GbMod> io = m && m->gbId ? GbInfo(m->gbId) : nullptr;
			if (io) OpenWeb(io->page);
		}
		break;
	case kIoPage:
		if (arg >= 0 && arg < static_cast<int>(g_s.io.mods.size())) OpenWeb(g_s.io.mods[arg].page);
		break;
	case kRichLink: {
		std::string link;
		{
			std::lock_guard<std::mutex> lock(g_rich.mx);
			if (arg >= 0 && arg < static_cast<int>(g_rich.blocks.size())) link = g_rich.blocks[arg].link;
		}
		if (!link.empty()) OpenWeb(link);
		break;
	}
	case kShowProblems: {
		const std::vector<std::string> &problems = g_s.problems;
		std::string text;
		for (size_t i = 0; i < problems.size() && i < 5; i++) text += (i ? "\n" : "") + problems[i];
		if (problems.size() > 5) text += "\n(" + Tr("and ") + std::to_string(problems.size() - 5) + Tr(" more)");
		if (!text.empty()) {
			g_s.toast = text;
			g_s.toastErr = true;
			g_s.toastLen = 12000;
			g_s.toastUntil = GetTickCount() + g_s.toastLen;
		}
		break;
	}
	case kUpdateAll: {
		std::vector<const ModInfo *> targets;
		std::vector<CatalogEntry> entries;
		for (const ModInfo &m : g_s.mods) {
			CatalogEntry e;
			if (!FindUpdateFor(g_s, m, e)) continue;
			bool merged = false;
			for (size_t k = 0; k < targets.size(); k++) {
				if (!ModsSameMod(*targets[k], m)) continue;
				merged = true;
				if (ModsCompareVersions(m.version, targets[k]->version) > 0) targets[k] = &m;
			}
			if (!merged) {
				targets.push_back(&m);
				entries.push_back(e);
			}
		}
		for (size_t k = 0; k < targets.size(); k++) DlUpdateMod(targets[k]->folder, entries[k]);
		if (targets.empty()) SetStatus("No pending updates");
		break;
	}
	case kReinstall: {
		const Row *sel = SelectedRow(g_s);
		if (!sel) break;
		const ModInfo *m = FindMod(sel->folder);
		if (!m || !m->gbId) break;
		CatalogEntry e;
		e.id = m->id;
		e.name = m->name;
		e.version = m->version;
		e.gbId = m->gbId;
		DlUpdateMod(m->folder, e);
		SetStatus(TrFmt("Downloading %s again from GameBanana", {m->name}));
		break;
	}
	case kUpdateMod: {
		const Row *sel = SelectedRow(g_s);
		if (!sel) break;
		const ModInfo *m = FindMod(sel->folder);
		CatalogEntry e;
		if (m && FindUpdateFor(g_s, *m, e)) DlUpdateMod(m->folder, e);
		break;
	}
	}
	const bool keepsFocus =
		action == kFocusName || action == kFocusSearch || action == kIoFocusSearch || action == kFocusImport || action == kPasteCode ||
		action == kNone || action == kSaveAs ||
		(action == kNameOk && g_s.nameMode != kNameNone) ||
		(action == kDropPick && g_s.nameMode != kNameNone);
	if (!keepsFocus) {
		g_s.focus = kNoField;
	}
	if (action != kDropOpen && action != kNone) {
		if (action != kDropPick) g_s.drop = kDropNone;
	}
	const int maxScroll = std::max(0, static_cast<int>(VisibleRows(g_s).size()) - g_installedRows.load());
	g_s.list.scroll = std::max(0, std::min(g_s.list.scroll, maxScroll));
	const int maxOrder = std::max(0, static_cast<int>(g_s.order.size()) - g_orderRows.load());
	g_s.orderList.scroll = std::max(0, std::min(g_s.orderList.scroll, maxOrder));
	if (action != kNone) {
		g_s.restartNeeded = ModsRestartNeeded();
	}
}
}
