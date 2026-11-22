// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
// The room's scenario selector: the cup's map chooser without its tabs (a room picks its random map with the map type), over the mod's
// own list of scenarios.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/cupmaps.h"
#include "game/mappreview.h"
#include "i18n/tr.h"
#include "mods/mods.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

// The rows the list showed last: the keys and the wheel move by it.
int g_pickRows = 10;

std::string StemKey(const std::string &path)
{
	std::string p = path;
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (p.size() > 5 && p.compare(p.size() - 5, 5, ".bfhp") == 0) p.resize(p.size() - 5);
	return p;
}

std::string FileStem(const std::string &path)
{
	const std::string k = StemKey(path);
	const size_t slash = k.find_last_of('/');
	return slash == std::string::npos ? k : k.substr(slash + 1);
}

COLORREF Rgb(uint32_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

COLORREF SlotColor(const GameRoomSnapshot &g, const MapPlayer &p)
{
	if (p.human && p.slot >= 0 && p.slot < 8 && g.slots[p.slot].used) return PlayerColor(g, g.slots[p.slot].color);
	return Rgb(p.color);
}

bool Deletable(const MapPreviewData *d) { return d && (d->origin.kind == kMapLoose || d->origin.kind == kMapReceived); }
}

std::vector<CupScenario> PickScenarios()
{
	const CupScenariosView v = CupScenarios();
	std::vector<CupScenario> all = v.list;
	all.insert(all.end(), v.rules.begin(), v.rules.end());
	return all;
}

std::vector<int> PickView(const State &st)
{
	const std::vector<CupScenario> list = PickScenarios();
	std::vector<int> rows;
	for (size_t i = 0; i < list.size(); i++) {
		if (!st.pickGone.count(StemKey(list[i].path))) rows.push_back(static_cast<int>(i));
	}
	return ScenarioOrder(list, rows, st.pickSearch, st.pickSort);
}

bool PickInUse(const State &st, int index)
{
	const std::vector<CupScenario> list = PickScenarios();
	if (st.game.scenario.empty() || index < 0 || index >= static_cast<int>(list.size())) return false;
	return StemKey(list[index].path) == StemKey(st.game.scenario) || FileStem(list[index].path) == FileStem(st.game.scenario);
}

void PickMove(WPARAM key)
{
	const std::vector<int> view = PickView(g_s);
	if (view.empty()) return;
	int pos = 0;
	for (size_t i = 0; i < view.size(); i++) {
		if (view[i] == g_s.pickSel) pos = static_cast<int>(i);
	}
	if (g_s.pickSel < 0) pos = key == VK_UP || key == VK_PRIOR || key == VK_END ? static_cast<int>(view.size()) : -1;
	const int n = static_cast<int>(view.size());
	pos += key == VK_UP ? -1 : key == VK_DOWN ? 1 : key == VK_PRIOR ? -g_pickRows : key == VK_NEXT ? g_pickRows : key == VK_HOME ? -n : n;
	pos = std::max(0, std::min(n - 1, pos));
	g_s.pickSel = view[pos];
	if (pos < g_s.pick.scroll) g_s.pick.scroll = pos;
	if (pos >= g_s.pick.scroll + g_pickRows) g_s.pick.scroll = pos - g_pickRows + 1;
}

int PickRowsShown() { return g_pickRows; }

void PaintScenarioPicker(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const int w = std::min(1120, st.viewW - 60), h = std::min(720, st.viewH - 60);
	const Dialog dlg = DialogBegin(p, st.viewW, st.viewH, w, h, "Choose scenario");
	const bool confirming = st.pickDelete >= 0;
	p.noHover = confirming || st.dropOpen;

	const std::vector<CupScenario> list = PickScenarios();
	const bool listed = CupScenarios().loaded;
	const int listX = dlg.BodyX(), barY = dlg.y + 62, listW = std::min(440, dlg.BodyW() * 42 / 100);
	PaintScenarioToolbar(p, st, listX, barY, listW, 36);
	const int listY = barY + 50, listH = dlg.FooterTop() - 18 - listY;
	const int dx = listX + listW + 22, dw = dlg.x + dlg.w - dlg.pad - dx;

	p.Fill(listX, listY, listW, listH, pal::bg);
	p.Frame(listX, listY, listW, listH, pal::lineStrong);
	const std::vector<int> view = PickView(st);
	const int count = static_cast<int>(view.size()), rowH = kScenarioRowH;
	g_pickRows = std::max(1, (listH - 4) / rowH);
	const int scroll = std::max(0, std::min(st.pick.scroll, count - g_pickRows));
	p.Font(0);
	if (!listed) {
		// The scenarios still being read: rows of the list's shape (picture, name, details), shimmering.
		for (int r = 0; r < g_pickRows; r++) {
			const int ry = listY + 2 + r * rowH, thumb = rowH - 12, tx = listX + 12 + thumb + 12, room = listW - 16 - (tx - listX) - 14;
			PaintLoadingThumb(p, listX + 12, ry + 6, thumb);
			PaintSkeleton(p, tx, ry + 11, room * (55 + (r * 7 % 4) * 10) / 100, 12);
			PaintSkeleton(p, tx, ry + 34, room * (40 + (r * 5 % 3) * 10) / 100, 10);
		}
	}
	else if (list.empty()) p.Text(listX + 16, listY + 12, listW - 32, 28, "There are no scenarios for multiplayer", pal::textDim);
	else if (!count) p.Text(listX + 16, listY + 12, listW - 32, 28, "No map matches the search.", pal::textDim);
	p.Clip(listX + 1, listY + 1, listW - 2, listH - 2);
	for (int r = 0; r < g_pickRows && scroll + r < count; r++) {
		const int i = view[scroll + r], ry = listY + 2 + r * rowH;
		const bool on = i == st.pickSel, hot = !on && p.Hover(listX + 2, ry, listW - 16, rowH, mx, my);
		PaintScenarioRow(p, list[i], listX + 2, ry, listW - 16, rowH, on, hot, PickInUse(st, i) ? Tr("In use") : std::string(), pal::ok, false);
		p.AddHit(listX + 2, ry, listW - 16, rowH, kPickRow, i);
	}
	p.Unclip();
	{
		ListState viewState = st.pick;
		viewState.scroll = scroll;
		ListGeom geom;
		ScrollbarPaint(p, listX + listW - 10, listY + 3, listH - 6, count, g_pickRows, viewState, geom, mx, my);
		g_pickGeomOut = geom;
	}

	PaintCard(p, dx, listY, dw, listH);
	const bool has = st.pickSel >= 0 && st.pickSel < static_cast<int>(list.size());
	const std::shared_ptr<const MapPreviewData> d = has ? MapPreviewGet(list[st.pickSel].path) : nullptr;
	if (!has) {
		p.Font(0);
		p.TextWrapped(dx + 18, listY + 18, dw - 36, 80, "Pick a scenario from the list to see its preview and description.", pal::textDim);
	}
	else {
		std::string note;
		COLORREF ink = pal::body;
		if (!g.pickerRefused.empty() && StemKey(g.pickerRefused) == StemKey(list[st.pickSel].path)) {
			note = Tr("The game does not let this scenario be played in multiplayer");
			ink = pal::bad;
		}
		else if (PickInUse(st, st.pickSel)) {
			note = Tr("In use in this room");
			ink = pal::ok;
		}
		PaintScenarioCard(p, list[st.pickSel], dx, listY, dw, listH, {}, note, ink, [&g](const MapPlayer &pl) { return SlotColor(g, pl); });
	}

	const int footer = dlg.FooterTop();
	const bool canDelete = has && Deletable(d.get()) && !PickInUse(st, st.pickSel);
	p.Button(listX, footer, 190, kDialogBtnH, "Delete map...", kPickDelete, 0, mx, my, canDelete && !confirming, false, 1);
	std::string note;
	COLORREF noteColor = pal::textDim;
	if (has && d && !d->failed && !canDelete) {
		if (d->origin.kind == kMapMod) note = TrFmt("This map comes from the content pack %s: uninstall that pack to remove it", {d->origin.modName});
		else if (d->origin.kind == kMapBase) note = "This map is part of the game and cannot be deleted";
		else if (PickInUse(st, st.pickSel)) note = "This map is in use in the room: choose another one first";
	}
	if (note.empty() && !st.pickNote.empty() && GetTickCount() < st.pickNoteUntil) {
		note = st.pickNote;
		noteColor = pal::ok;
	}
	if (!note.empty()) {
		p.Font(1);
		int buttonsW = 12;
		for (const char *label : {"Use this map", "Cancel"}) buttonsW += std::max(120, p.CapsWidth(label, 1) + 44);
		const int noteX = listX + 200, noteW = std::max(120, dlg.x + dlg.w - dlg.pad - buttonsW - 16 - noteX);
		p.Font(3);
		const int noteH = std::max(kDialogBtnH, std::min(kDialogBtnH + 20, p.Measure(noteW, note)));
		p.TextWrapped(noteX, footer + 2 + kDialogBtnH - noteH, noteW, noteH, note, noteColor);
	}
	DialogButtons(p, dlg, {{"Use this map", kPickOk, 0, true, has && !confirming}, {"Cancel", kPickCancel}}, mx, my);

	if (confirming) {
		p.noHover = false;
		const std::string name = st.pickDelete < static_cast<int>(list.size()) ? list[st.pickDelete].title : "";
		const Dialog c = DialogBegin(p, st.viewW, st.viewH, 560, 220, "Delete map");
		DialogText(p, c, 60, TrFmt("\"%s\" will be moved to the recycle bin. You can restore it from there.", {name}));
		DialogButtons(p, c, {{"Delete", kPickDeleteYes, 0, true}, {"Cancel", kPickDeleteNo}}, mx, my);
	}
}
}
