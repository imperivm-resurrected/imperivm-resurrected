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

// What the question says: its title, its text and the word of its yes.
void ConfirmTexts(const State &st, std::string &title, std::string &text, std::string &yes)
{
	if (st.confirmKind == kConfirmNotice || st.confirmKind == kConfirmAction) {
		title = st.noticeTitle;
		text = Tr(st.noticeText);
		if (st.confirmDeadline) {
			const int left = std::max(0, static_cast<int>(st.confirmDeadline - GetTickCount()));
			text = TrFmt(st.noticeText, {std::to_string((left + 999) / 1000)});
		}
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
}

// How tall the text of the question is in the shared body type, and the room the dialog gives it before growing.
constexpr int kConfirmTextMost = 330;

int ConfirmTextRoom(const State &st) { return !st.confirmItems.empty() ? 62 : st.confirmKind == kConfirmAction && !st.actionAlt.empty() ? 94 : 70; }

int ConfirmTextHeight(const State &st, int width)
{
	std::string title, text, yes;
	ConfirmTexts(st, title, text, yes);
	HDC dc = CreateCompatibleDC(nullptr);
	Painter pm{dc, 1.0f, 0, 0, nullptr};
	pm.fonts[0] = BrandFont(BrandFace::Body, 15, 1.0f);
	pm.Font(0);
	const int h = pm.Measure(width, text);
	DeleteDC(dc);
	return h;
}
}
}

namespace modui_impl {
void ConfirmBaseSize(const State &st, int &cw, int &ch);

void ConfirmSize(const State &st, int &cw, int &ch)
{
	ConfirmBaseSize(st, cw, ch);
	// A long question makes the dialog taller rather than losing its last lines.
	const int textH = std::min(kConfirmTextMost, ConfirmTextHeight(st, cw - 48) + 4);
	ch += std::max(0, textH - ConfirmTextRoom(st));
}

void ConfirmBaseSize(const State &st, int &cw, int &ch)
{
	cw = kConfirmW;
	ch = kConfirmH;
	if (st.confirmKind == kConfirmAction && !st.actionAlt.empty()) {
		cw = 600;
		ch = kConfirmH + 24;
	}
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
	ConfirmTexts(st, title, text, yes);
	const Dialog d = DialogBegin(p, st.confirmOnly ? cw : st.viewW, st.confirmOnly ? ch : st.viewH, cw, ch, title);
	p.Font(0);
	const int textH = std::max(ConfirmTextRoom(st), std::min(kConfirmTextMost, p.Measure(d.BodyW(), text)));
	p.TextWrapped(d.BodyX(), d.BodyTop(), d.BodyW(), textH, text, pal::text);
	if (!st.confirmItems.empty()) {
		const std::vector<ListLine> lines = ConfirmLines(st);
		const int rows = std::min<int>(kConfirmListRows, static_cast<int>(lines.size()));
		ListState ls = st.confirmList;
		ListPaint(p, d.BodyX(), d.BodyTop() + textH + 4, d.BodyW(), rows * kConfirmRowH + 8, lines, ls, g_confirmGeomOut, 0, kConfirmRowH, mx, my);
	}
	if (st.confirmKind == kConfirmNotice) DialogButtons(p, d, {{"OK", kConfirmNo, 0, true}}, mx, my);
	else if (st.confirmKind == kConfirmAction && st.confirmDeadline) DialogButtons(p, d, {{yes, kConfirmYes, 0, true}, {st.actionAlt, kConfirmAlt}}, mx, my);
	else if (st.confirmKind == kConfirmAction && !st.actionAlt.empty()) DialogButtons(p, d, {{yes, kConfirmYes, 0, true}, {st.actionAlt, kConfirmAlt}, {"Later", kConfirmNo}}, mx, my);
	else DialogButtons(p, d, {{yes, kConfirmYes, 0, true}, {st.confirmKind == kConfirmRestart ? "Later" : "Cancel", kConfirmNo}}, mx, my);
}
}
