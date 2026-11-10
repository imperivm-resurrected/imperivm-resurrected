// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>

#include "game/civs.h"
#include "game/mappreview.h"
#include "i18n/tr.h"
#include "mods/mods.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
const int kDlgW = 980, kDlgH = 620, kRowH = 44, kRows = 10, kListW = 420, kPreview = 224;

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string StemKey(const std::string &path)
{
	std::string p = Lower(path);
	for (char &c : p) {
		if (c == '\\') c = '/';
	}
	if (p.size() > 5 && p.compare(p.size() - 5, 5, ".bfhp") == 0) p.resize(p.size() - 5);
	return p;
}

std::string FileStem(const std::string &path)
{
	const std::string k = StemKey(path);
	const size_t slash = k.find_last_of('/');
	return slash == std::string::npos ? k : k.substr(slash + 1);
}

int CompareNames(const std::string &a, const std::string &b)
{
	std::wstring wa = ui::W(a), wb = ui::W(b);
	return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, wa.c_str(), static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size())) - CSTR_EQUAL;
}

std::string OriginText(const MapPreviewData &d)
{
	switch (d.origin.kind) {
	case kMapMod: return d.origin.modName;
	case kMapReceived: return Tr("Received");
	case kMapLoose: return Tr("Yours");
	default: return Tr("Base game");
	}
}

std::string CivText(const MapPlayer &p, const std::vector<std::pair<int, std::string>> &named)
{
	for (const auto &n : named) {
		if (n.first == p.slot + 1) return n.second;
	}
	if (_stricmp(p.race.c_str(), "Mutable") == 0) return Tr("Any civilization");
	if (_stricmp(p.race.c_str(), "Random") == 0) return Tr("Random");
	if (const Civ *c = CivByName(p.race)) return CivDisplayName(*c);
	return p.race;
}

std::string RowSubtitle(const MapPreviewData &d)
{
	std::string s;
	if (d.humans > 0) s += std::to_string(d.humans) + " " + Tr(d.humans == 1 ? "player" : "players");
	const std::string size = Tr(MapSizeName(d.info.units));
	if (!size.empty()) s += (s.empty() ? "" : " · ") + size;
	return s;
}

COLORREF Rgb(uint32_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

COLORREF SlotColor(const GameRoomSnapshot &g, const MapPlayer &p)
{
	if (p.human && p.slot >= 0 && p.slot < 8 && g.slots[p.slot].used) return PlayerColor(g, g.slots[p.slot].color);
	return Rgb(p.color);
}

bool Deletable(const MapPreviewData *d) { return d && (d->origin.kind == kMapLoose || d->origin.kind == kMapReceived); }
}

std::vector<int> PickView(const State &st)
{
	struct Item {
		int index;
		std::string name;
		int humans;
		int units;
	};
	std::vector<Item> items;
	const std::string q = Lower(st.pickSearch);
	const std::vector<std::string> &names = st.game.pickerItems;
	for (size_t i = 0; i < names.size(); i++) {
		if (names[i].empty() || i >= st.game.pickerPaths.size()) continue;
		const std::string &path = st.game.pickerPaths[i];
		if (st.pickGone.count(StemKey(path))) continue;
		const std::shared_ptr<const MapPreviewData> d = MapPreviewGet(path);
		if (!q.empty()) {
			std::string hay = Lower(names[i] + " " + FileStem(path));
			if (d && !d->failed) hay += " " + Lower(OriginText(*d));
			if (hay.find(q) == std::string::npos) continue;
		}
		items.push_back({static_cast<int>(i), names[i], d && !d->failed ? d->humans : -1, d && !d->failed ? d->info.units : -1});
	}
	const auto byName = [](const Item &a, const Item &b) { return CompareNames(a.name, b.name) < 0; };
	if (st.pickSort == 1) {
		std::stable_sort(items.begin(), items.end(), [&](const Item &a, const Item &b) {
			if (a.humans != b.humans) return (a.humans < 0 ? 99 : a.humans) < (b.humans < 0 ? 99 : b.humans);
			return byName(a, b);
		});
	}
	else if (st.pickSort == 2) {
		std::stable_sort(items.begin(), items.end(), [&](const Item &a, const Item &b) {
			if (a.units != b.units) return (a.units < 0 ? 1 << 30 : a.units) < (b.units < 0 ? 1 << 30 : b.units);
			return byName(a, b);
		});
	}
	else {
		std::stable_sort(items.begin(), items.end(), byName);
	}
	std::vector<int> out;
	for (const Item &it : items) out.push_back(it.index);
	return out;
}

bool PickInUse(const State &st, int index)
{
	if (st.game.scenario.empty() || index < 0 || index >= static_cast<int>(st.game.pickerPaths.size())) return false;
	return StemKey(st.game.pickerPaths[index]) == StemKey(st.game.scenario) || FileStem(st.game.pickerPaths[index]) == FileStem(st.game.scenario);
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
	pos += key == VK_UP ? -1 : key == VK_DOWN ? 1 : key == VK_PRIOR ? -kRows : key == VK_NEXT ? kRows : key == VK_HOME ? -n : n;
	pos = std::max(0, std::min(n - 1, pos));
	g_s.pickSel = view[pos];
	GameRoomPickerSelect(view[pos]);
	if (pos < g_s.pick.scroll) g_s.pick.scroll = pos;
	if (pos >= g_s.pick.scroll + kRows) g_s.pick.scroll = pos - kRows + 1;
}

int PickRowsShown() { return kRows; }

void PaintScenarioPicker(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const Dialog dlg = DialogBegin(p, kW, kH, kDlgW, kDlgH, "Choose scenario");
	const int x = dlg.x, y = dlg.y;
	const int lx = x + 20, ly = y + 98;
	const bool confirming = st.pickDelete >= 0;
	p.noHover = confirming || st.dropOpen;

	PaintField(p, st, lx, y + 60, 270, 28, st.pickSearch, "Search maps...", st.focus == kPickSearchField, kFieldPickSearch);
	static const char *const kSortNames[] = {"Name", "Players", "Size"};
	PaintPickCombo(p, st, lx + 278, y + 60, kListW - 278, 28, Tr("Sort: ") + Tr(kSortNames[st.pickSort % 3]));

	p.Fill(lx, ly, kListW, kRows * kRowH + 4, pal::well);
	p.Frame(lx, ly, kListW, kRows * kRowH + 4, pal::line);
	const std::vector<int> view = PickView(st);
	const int count = static_cast<int>(view.size());
	const int scroll = std::max(0, std::min(st.pick.scroll, count - kRows));
	p.Font(0);
	if (!g.pickerItems.size()) p.Text(lx + 12, ly + 8, kListW - 24, 28, "There are no scenarios for multiplayer", kDim);
	else if (!count) p.Text(lx + 12, ly + 8, kListW - 24, 28, "No map matches the search.", kDim);
	for (int r = 0; r < kRows && scroll + r < count; r++) {
		const int i = view[scroll + r], ry = ly + 2 + r * kRowH;
		const bool hov = p.Hover(lx + 2, ry, kListW - 20, kRowH, mx, my);
		if (i == st.pickSel) p.Fill(lx + 2, ry, kListW - 4, kRowH, pal::rowSelected);
		else if (hov) p.Fill(lx + 2, ry, kListW - 4, kRowH, pal::rowHover);
		const std::shared_ptr<const MapPreviewData> d = MapPreviewGet(g.pickerPaths[i]);
		const bool inUse = PickInUse(st, i);
		int textRight = lx + kListW - 22;
		p.Font(3);
		if (inUse) {
			const std::string tag = Tr("In use");
			const int w = p.TextWidth(tag) + 14;
			textRight -= w + 6;
			PaintBadge(p, textRight + 6, ry + 6, w, 18, tag, RGB(40, 80, 44), kOk);
		}
		p.Font(0);
		p.Text(lx + 12, ry + 2, textRight - lx - 18, 22, g.pickerItems[i].empty() ? "-" : g.pickerItems[i], i == st.pickSel ? pal::focus : kText,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		if (!d) {
			p.Text(lx + 12, ry + 24, 200, 16, "Reading...", kDim);
		}
		else if (!d->failed) {
			const std::string sub = RowSubtitle(*d), origin = OriginText(*d);
			p.Text(lx + 12, ry + 24, 200, 16, sub, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Text(lx + 200, ry + 24, kListW - 224, 16, origin, d->origin.kind == kMapMod ? kGold : kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		p.AddHit(lx + 2, ry, kListW - 20, kRowH, kPickRow, i);
	}
	{
		ListState viewState = st.pick;
		viewState.scroll = scroll;
		ListGeom geom;
		ScrollbarPaint(p, lx + kListW - 10, ly + 3, kRows * kRowH - 2, count, kRows, viewState, geom, mx, my);
		g_pickGeomOut = geom;
	}

	const int dx = lx + kListW + 20, dw = x + kDlgW - 20 - dx;
	PaintCard(p, dx, ly, dw, kRows * kRowH + 4);
	const bool has = st.pickSel >= 0 && st.pickSel < static_cast<int>(g.pickerPaths.size());
	const std::shared_ptr<const MapPreviewData> d = has ? MapPreviewGet(g.pickerPaths[st.pickSel]) : nullptr;
	if (!has) {
		p.Font(0);
		p.TextWrapped(dx + 16, ly + 14, dw - 32, 80, "Pick a scenario from the list to see its preview and description.", kDim);
	}
	else {
		const int px = dx + 14, py = ly + 14;
		p.Fill(px, py, kPreview, kPreview, RGB(22, 17, 12));
		int ix = px, iy = py, iw = kPreview, ih = kPreview;
		if (d && !d->failed && d->image.w > 0) {
			ih = kPreview * d->image.h / d->image.w;
			iw = kPreview;
			if (ih > kPreview) {
				iw = kPreview * d->image.w / d->image.h;
				ih = kPreview;
			}
			ix = px + (kPreview - iw) / 2;
			iy = py + (kPreview - ih) / 2;
			p.Image(ix, iy, iw, ih, d->image.bgra.data(), d->image.w, d->image.h);
			for (const MapPlayer &pl : d->info.players) {
				if (!pl.hasStart || !pl.human || d->info.units <= 0) continue;
				const int cx = ix + static_cast<int>(static_cast<long long>(pl.x) * iw / d->info.units);
				const int cy = iy + static_cast<int>(static_cast<long long>(pl.y) * ih / d->info.units);
				p.Ellipse(cx - 6, cy - 6, 12, 12, RGB(10, 8, 6));
				p.Ellipse(cx - 5, cy - 5, 10, 10, SlotColor(g, pl));
			}
		}
		else {
			p.Font(3);
			p.Text(px, py, kPreview, kPreview, !d ? "Reading..." : "No preview", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		p.Frame(px, py, kPreview, kPreview, pal::line);

		const int tx = px + kPreview + 16, tw = x + kDlgW - 34 - tx;
		p.Font(1);
		const int nameH = p.TextWrapped(tx, py, tw, 56, g.pickerItems[st.pickSel], kGold);
		int ty = py + std::max(24, nameH) + 6;
		p.Font(0);
		if (d && !d->failed) {
			if (d->humans > 0) {
				p.Text(tx, ty, tw, 22, std::to_string(d->humans) + " " + Tr(d->humans == 1 ? "player" : "players"), kText);
				ty += 24;
			}
			if (d->info.units > 0) {
				p.Text(tx, ty, tw, 22, Tr("Size: ") + Tr(MapSizeName(d->info.units)), kText);
				ty += 24;
			}
			p.Text(tx, ty, tw, 22, Tr("From: ") + OriginText(*d), d->origin.kind == kMapMod ? kGold : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			ty += 24;
			if (PickInUse(st, st.pickSel)) {
				p.Text(tx, ty, tw, 22, "In use in this room", kOk);
				ty += 24;
			}
		}
		int py2 = py + kPreview + 12;
		if (d && !d->failed && !d->info.players.empty()) {
			p.Font(3);
			const int colW = (dw - 28) / 2;
			int n = 0;
			const std::vector<std::pair<int, std::string>> named = MapPlayerLines(g.pickerDescription);
			for (const MapPlayer &pl : d->info.players) {
				const int col = n % 2, row = n / 2;
				const int cx = px + col * colW, cy = py2 + row * 20;
				p.Fill(cx, cy + 4, 12, 12, SlotColor(g, pl));
				p.Frame(cx, cy + 4, 12, 12, RGB(10, 8, 6));
				p.Text(cx + 18, cy, colW - 22, 20, TrFmt("Player %s", {std::to_string(pl.slot + 1)}) + ": " + CivText(pl, named),
					pl.human ? kText : kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				n++;
			}
			py2 += ((n + 1) / 2) * 20 + 8;
		}
		const int descH = ly + kRows * kRowH + 4 - 10 - py2;
		if (descH > 30) {
			p.Fill(px, py2 - 4, dw - 28, 1, pal::lineDim);
			p.Font(3);
			const std::string body = MapDescriptionBody(g.pickerDescription, d && !d->failed && !d->info.players.empty());
			p.Clip(px, py2, dw - 28, descH);
			p.TextWrapped(px, py2 + 2, dw - 28, descH, body.empty() ? "No description." : body, body.empty() ? kDim : kText);
			p.Unclip();
		}
	}

	const int footer = dlg.FooterTop();
	const bool canDelete = has && Deletable(d.get()) && !PickInUse(st, st.pickSel);
	p.Button(lx, footer, 190, kDialogBtnH, "Delete map...", kPickDelete, 0, mx, my, canDelete && !confirming, false, 1);
	std::string note;
	COLORREF noteColor = kDim;
	if (has && d && !d->failed && !canDelete) {
		if (d->origin.kind == kMapMod) note = TrFmt("This map comes from the content pack %s: uninstall that pack to remove it", {d->origin.modName});
		else if (d->origin.kind == kMapBase) note = "This map is part of the game and cannot be deleted";
		else if (PickInUse(st, st.pickSel)) note = "This map is in use in the room: choose another one first";
	}
	if (note.empty() && !st.pickNote.empty() && GetTickCount() < st.pickNoteUntil) {
		note = st.pickNote;
		noteColor = kOk;
	}
	if (!note.empty()) {
		p.Font(3);
		p.TextWrapped(lx + 200, footer + 2, 360, kDialogBtnH, note, noteColor);
	}
	DialogButtons(p, dlg, {{"Choose this scenario", kPickOk, 0, true, st.pickSel >= 0 && !confirming}, {"Cancel", kPickCancel}}, mx, my);

	if (confirming) {
		p.noHover = false;
		const std::string name = st.pickDelete < static_cast<int>(g.pickerItems.size()) ? g.pickerItems[st.pickDelete] : "";
		const Dialog c = DialogBegin(p, kW, kH, 560, 220, "Delete map");
		DialogText(p, c, 60, TrFmt("\"%s\" will be moved to the recycle bin. You can restore it from there.", {name}));
		DialogButtons(p, c, {{"Delete", kPickDeleteYes, 0, true}, {"Cancel", kPickDeleteNo}}, mx, my);
	}
}
}
