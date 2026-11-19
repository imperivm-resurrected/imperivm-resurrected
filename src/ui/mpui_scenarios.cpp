// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
// What the room's scenario selector and the cup's map chooser share: one list, one search and order, one card for the chosen scenario.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/civs.h"
#include "game/cupmaps.h"
#include "game/mapinfo.h"
#include "game/mappreview.h"
#include "i18n/tr.h"
#include "mods/mods.h"

#include "ui/mpui_internal.h"

namespace mpui_impl {
namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

int CompareNames(const std::string &a, const std::string &b)
{
	std::wstring wa = ui::W(a), wb = ui::W(b);
	return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, wa.c_str(), static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size())) - CSTR_EQUAL;
}

COLORREF Rgb(uint32_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

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

}

std::string ScenarioOriginLine(const CupScenario &s)
{
	if (s.game) return Tr("Scenario of the game");
	switch (s.origin.kind) {
	case kMapMod: return TrFmt("From the content pack %s", {s.origin.modName.empty() ? s.origin.modFolder : s.origin.modName});
	case kMapReceived: return Tr("Received from another player");
	default: return Tr("Yours");
	}
}

std::string ScenarioSizeLine(const std::string &path)
{
	const std::shared_ptr<const MapPreviewData> d = MapPreviewGet(path);
	return d && !d->failed && d->info.units > 0 ? Tr(MapSizeName(d->info.units)) : std::string();
}

void PaintScenarioThumb(Painter &p, const std::string &path, int x, int y, int size, bool starts, const ScenarioSeat &seat)
{
	p.Fill(x, y, size, size, pal::bg);
	if (path.empty()) {
		const int dot = size / 9, a = x + size / 4, b = x + size * 3 / 4, t = y + size / 4, u = y + size * 3 / 4;
		for (const int px : {a, b}) {
			for (const int py : {t, u}) p.Ellipse(px - dot / 2, py - dot / 2, dot, dot, pal::edgeHot);
		}
		p.Ellipse(x + size / 2 - dot / 2, y + size / 2 - dot / 2, dot, dot, pal::edgeHot);
		p.Frame(x, y, size, size, pal::lineStrong);
		return;
	}
	const std::shared_ptr<const MapPreviewData> d = MapPreviewGet(path);
	if (d && !d->failed && d->image.w > 0 && d->image.h > 0) {
		int iw = size, ih = size * d->image.h / d->image.w;
		if (ih > size) {
			iw = size * d->image.w / d->image.h;
			ih = size;
		}
		const int ix = x + (size - iw) / 2, iy = y + (size - ih) / 2;
		p.Image(ix, iy, iw, ih, d->image.bgra.data(), d->image.w, d->image.h);
		// Only where the picture is big enough to read them: the list's thumbnails leave them to the card.
		const int dot = std::max(8, std::min(14, size / 20));
		for (const MapPlayer &pl : d->info.players) {
			if (!starts) break;
			if (!pl.hasStart || !pl.human || d->info.units <= 0) continue;
			const int cx = ix + static_cast<int>(static_cast<long long>(pl.x) * iw / d->info.units);
			const int cy = iy + static_cast<int>(static_cast<long long>(pl.y) * ih / d->info.units);
			p.Ellipse(cx - dot / 2 - 1, cy - dot / 2 - 1, dot + 2, dot + 2, pal::bg);
			p.Ellipse(cx - dot / 2, cy - dot / 2, dot, dot, seat ? seat(pl) : Rgb(pl.color));
		}
	}
	else {
		p.Font(3);
		p.Text(x, y, size, size, Tr(!d ? "Reading..." : "No preview"), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	p.Frame(x, y, size, size, pal::lineStrong);
}

std::vector<int> ScenarioOrder(const std::vector<CupScenario> &list, const std::vector<int> &rows, const std::string &search, int sort)
{
	struct Item {
		int index;
		int humans, units;
	};
	std::vector<Item> items;
	const std::string q = Lower(search);
	for (int i : rows) {
		if (i < 0 || i >= static_cast<int>(list.size())) continue;
		const CupScenario &s = list[static_cast<size_t>(i)];
		const std::shared_ptr<const MapPreviewData> d = MapPreviewGet(s.path);
		if (!q.empty()) {
			const std::string hay = Lower(s.title + " " + s.file + " " + ScenarioOriginLine(s));
			if (hay.find(q) == std::string::npos) continue;
		}
		items.push_back({i, s.humans, d && !d->failed ? d->info.units : -1});
	}
	const auto byName = [&](const Item &a, const Item &b) { return CompareNames(list[static_cast<size_t>(a.index)].title, list[static_cast<size_t>(b.index)].title) < 0; };
	if (sort == 1) {
		std::stable_sort(items.begin(), items.end(), [&](const Item &a, const Item &b) { return a.humans != b.humans ? a.humans < b.humans : byName(a, b); });
	}
	else if (sort == 2) {
		std::stable_sort(items.begin(), items.end(), [&](const Item &a, const Item &b) {
			if (a.units != b.units) return (a.units < 0 ? 1 << 30 : a.units) < (b.units < 0 ? 1 << 30 : b.units);
			return byName(a, b);
		});
	}
	else std::stable_sort(items.begin(), items.end(), byName);
	std::vector<int> out;
	for (const Item &it : items) out.push_back(it.index);
	return out;
}

void PaintScenarioToolbar(Painter &p, const State &st, int x, int y, int w, int h)
{
	static const char *const kSortNames[] = {"Name", "Players", "Size"};
	const int sortW = std::min(200, w / 3);
	PaintField(p, st, x, y, w - sortW - 8, h, st.pickSearch, Tr("Search maps..."), st.focus == kPickSearchField, kFieldPickSearch);
	PaintPickCombo(p, st, x + w - sortW, y, sortW, h, Tr("Sort: ") + Tr(kSortNames[st.pickSort % 3]));
}

void PaintScenarioRow(Painter &p, const CupScenario &s, int x, int y, int w, int h, bool on, bool hot, const std::string &tag, COLORREF tagInk, bool warn)
{
	if (on) p.Fill(x, y, w, h, pal::selected);
	else if (hot) p.Fill(x, y, w, h, pal::raised);
	if (on) p.Fill(x, y, 3, h, pal::red);
	PaintScenarioThumb(p, s.path, x + 10, y + 6, h - 12, false);
	const int tx = x + 10 + h - 12 + 12;
	int right = x + w - 14;
	if (!tag.empty()) {
		p.Font(3);
		const int tw = p.TextWidth(tag) + 14;
		right -= tw;
		PaintBadge(p, right, y + 8, tw, 18, tag, RGB(40, 80, 44), tagInk);
		right -= 8;
	}
	p.Font(0);
	p.Text(tx, y + 5, right - tx, 22, s.title.empty() ? "-" : s.title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const std::string size = ScenarioSizeLine(s.path);
	p.Font(3);
	p.Text(tx, y + 29, x + w - 14 - tx, 18,
		TrFmt("%s players", {std::to_string(s.humans)}) + (size.empty() ? std::string() : " \xC2\xB7 " + size) + " \xC2\xB7 " + ScenarioOriginLine(s),
		warn ? pal::bad : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintScenarioCard(Painter &p, const CupScenario &s, int x, int y, int w, int h, const std::vector<std::string> &extra, const std::string &note, COLORREF noteInk,
	const ScenarioSeat &seat)
{
	const int pad = 18;
	const int size = std::min(260, std::min(w - 2 * pad, h - 2 * pad - 140));
	PaintScenarioThumb(p, s.path, x + pad, y + pad, size, true, seat);
	const int tx = x + pad + size + 18, tw = x + w - pad - tx;
	SelectObject(p.dc, BrandFont(BrandFace::UiStrong, 20, p.s));
	const int titleH = std::min(56, p.Measure(tw, s.title));
	p.TextWrapped(tx, y + pad, tw, titleH + 2, s.title, pal::text);
	int ty = y + pad + titleH + 10;
	const std::shared_ptr<const MapPreviewData> d = MapPreviewGet(s.path);
	const std::string size2 = ScenarioSizeLine(s.path);
	std::vector<std::string> lines = {TrFmt("%s players", {std::to_string(s.humans)}), size2.empty() ? std::string() : Tr("Size: ") + size2, ScenarioOriginLine(s)};
	lines.insert(lines.end(), extra.begin(), extra.end());
	p.Font(0);
	for (const std::string &line : lines) {
		if (line.empty() || ty + 24 > y + pad + size) continue;
		p.Text(tx, ty, tw, 24, line, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ty += 26;
	}
	// Under the picture: each seat with its civilization, the note, and the scenario's own text in what is left.
	int by = y + pad + size + 14;
	const int bw = w - 2 * pad, bottom = y + h - pad;
	const std::string text = d && !d->failed ? d->info.description : std::string();
	if (d && !d->failed && !d->info.players.empty()) {
		p.Font(3);
		const int colW = bw / 2;
		const std::vector<std::pair<int, std::string>> named = MapPlayerLines(text);
		int n = 0;
		for (const MapPlayer &pl : d->info.players) {
			const int cx = x + pad + (n % 2) * colW, cy = by + (n / 2) * 20;
			if (cy + 20 > bottom) break;
			p.Fill(cx, cy + 4, 12, 12, seat ? seat(pl) : Rgb(pl.color));
			p.Frame(cx, cy + 4, 12, 12, pal::bg);
			p.Text(cx + 18, cy, colW - 22, 20, TrFmt("Player %s", {std::to_string(pl.slot + 1)}) + ": " + CivText(pl, named), pl.human ? pal::text : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			n++;
		}
		by += ((n + 1) / 2) * 20 + 10;
	}
	if (!note.empty() && by + 24 < bottom) {
		p.Fill(x + pad, by, bw, 1, pal::hairline);
		p.Font(3);
		by += 10;
		by += p.TextWrapped(x + pad, by, bw, std::min(60, bottom - by), note, noteInk) + 8;
	}
	const std::string body = MapDescriptionBody(text, d && !d->failed && !d->info.players.empty());
	if (!body.empty() && by + 30 < bottom) {
		p.Fill(x + pad, by, bw, 1, pal::hairline);
		p.Font(3);
		p.Clip(x + pad, by + 8, bw, bottom - by - 8);
		p.TextWrapped(x + pad, by + 10, bw, bottom - by - 10, body, pal::text);
		p.Unclip();
	}
}

}
