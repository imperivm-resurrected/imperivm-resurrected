// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include "addr/state.h"

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
namespace {
std::vector<ListLine> ConfirmLines(const State &st)
{
	std::vector<ListLine> lines;
	if (st.confirmItems.empty()) return lines;
	if (!st.confirmItemsTitle.empty()) lines.push_back({Tr(st.confirmItemsTitle), pal::redText, true});
	for (const std::string &item : st.confirmItems) {
		if (item.compare(0, 2, "##") == 0) lines.push_back({Tr(item.substr(2)), pal::redText, true});
		else lines.push_back({"•  " + item, pal::text, false});
	}
	return lines;
}
}
}

namespace modui_impl {
void ConfirmSize(const State &st, int &cw, int &ch)
{
	cw = kConfirmW;
	ch = kConfirmH;
	if (st.confirmItems.empty()) return;
	const int rows = std::min<int>(kConfirmListRows, static_cast<int>(ConfirmLines(st).size()));
	cw = 600;
	ch = kConfirmH + rows * kConfirmRowH + 8;
}

void PaintConfirm(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	g_confirmGeomOut = ListGeom();
	if (!st.confirm) return;
	int cw, ch;
	ConfirmSize(st, cw, ch);
	std::string title, text, yes;
	if (st.confirmKind == kConfirmNotice || st.confirmKind == kConfirmAction) {
		title = st.noticeTitle;
		text = Tr(st.noticeText);
		yes = st.actionYes;
	}
	else if (st.confirmKind == kConfirmUninstall) {
		const std::vector<std::string> &targets = st.uninstallTargets;
		bool mounted = false;
		for (const std::string &f : ModsMountedProfile().mods) {
			if (std::find(targets.begin(), targets.end(), f) != targets.end()) mounted = true;
		}
		if (targets.size() == 1) {
			const ModInfo *m = FindModIn(st.mods, targets[0]);
			title = Tr("Uninstall ") + (m ? m->name : targets[0]);
			text = Tr("Its folder will be deleted from disk (no recycle bin)") + std::string(mounted ? " when the game restarts (it is currently loaded)." : ".");
		}
		else {
			title = TrFmt("Uninstall %s content packs", {std::to_string(targets.size())});
			text = Tr("Their folders will be deleted from disk (no recycle bin)") + std::string(mounted ? " when the game restarts (some are loaded)." : ".");
		}
		yes = "Uninstall";
	}
	else if (st.confirmKind == kConfirmDelete) {
		title = "Delete profile";
		text = Tr("Delete the profile \"") + st.profile.name + Tr("\"? Content is not uninstalled, only the profile's list.");
		yes = "Remove";
	}
	else {
		title = "Restart the game";
		const bool inMatch = *reinterpret_cast<void **>(addr::kGame) != nullptr;
		std::string why;
		if (!st.restartReason.empty()) why = Tr(st.restartReason);
		else if (st.profile.name == kOriginalName) why = Tr("The game will restart without additional content.");
		else if (st.profile.name == kNoProfileName) why = Tr("The game will restart to apply the content changes.");
		else why = Tr("The game will restart to apply the profile ") + "\"" + st.profile.name + "\".";
		text = why + (inMatch ? Tr(" Anything you have not saved will be lost.") : std::string());
		yes = "Restart now";
	}
	const Dialog d = DialogBegin(p, st.confirmOnly ? cw : st.viewW, st.confirmOnly ? ch : st.viewH, cw, ch, title);
	p.Font(0);
	const int textH = st.confirmItems.empty() ? 70 : 62;
	p.TextWrapped(d.BodyX(), d.BodyTop(), d.BodyW(), textH, text, pal::text);
	if (!st.confirmItems.empty()) {
		const std::vector<ListLine> lines = ConfirmLines(st);
		const int rows = std::min<int>(kConfirmListRows, static_cast<int>(lines.size()));
		ListState ls = st.confirmList;
		ListPaint(p, d.BodyX(), d.BodyTop() + textH + 4, d.BodyW(), rows * kConfirmRowH + 8, lines, ls, g_confirmGeomOut, 0, kConfirmRowH, mx, my);
	}
	if (st.confirmKind == kConfirmNotice) DialogButtons(p, d, {{"OK", kConfirmNo, 0, true}}, mx, my);
	else DialogButtons(p, d, {{yes, kConfirmYes, 0, true}, {st.confirmKind == kConfirmRestart ? "Later" : "Cancel", kConfirmNo}}, mx, my);
}
}
