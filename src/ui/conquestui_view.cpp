// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/conquestui_view.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "i18n/tr.h"
#include "ui/ornaments.h"
#include "ui/widgets.h"

using namespace ui;

namespace conquestui {
namespace {

// The game's dialog is laid out from the centre of the screen (ConquestGame.ini): the map is 1024x768 around it, the panel
// of the territory to the right (x +138..+480, y -346..+326) and the shields above the map (x -479..+113, y -346..-256).
const int kPanelX = 134, kPanelY = -350, kPanelW = 352, kPanelH = 684;
const int kPathX = -482, kPathY = -350, kPathW = 598, kPathH = 96;
const int kLegendW = 206, kLegendH = 80, kLegendMargin = 12, kMapHalfW = 512, kMapHalfH = 384;
// CONQUESTVICTORY.INI: a 390x180 box centred on the screen, OK at (135, 115) and 121x30. Ours is larger and keeps that button where
// the game has it, so the click goes to the game's.
const int kVictoryW = 440, kVictoryH = 200, kNativeVictoryW = 390, kNativeVictoryH = 180, kOkX = 135, kOkY = 115, kOkW = 121, kOkH = 30;

const int kPad = 20, kCardW = kPanelW - 2 * kPad;
const int kShieldW = 74, kShieldH = 79, kTerritoryH = 92;
const int kButtonsY = kPanelH - kPad - 34, kMainY = kButtonsY - 10 - 46, kDifficultyY = kMainY - 14 - 52, kBonusY = kDifficultyY - 14 - 96, kBonusH = 96;
const int kLineH = 19;
const int kSlotW = 64, kSlotGap = 18, kSlotShieldW = 44, kSlotShieldH = 47, kMaxSlots = 8;

const COLORREF kConquerText = RGB(236, 150, 110);

struct Pending {
	std::shared_ptr<const ConquestImage> img;
	int x, y, w, h, alpha;
};

struct Layer {
	Painter &p;
	std::vector<Pending> images;
};

COLORREF ToColorRef(uint32_t rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

// Text of the game (names, descriptions) is shown as it is: Tr() would look each one up as a key.
void Raw(Painter &p, int x, int y, int w, int h, const std::string &s, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	RECT r = p.R(x, y, w, h);
	SetTextColor(p.dc, c);
	const std::wstring ws = W(s);
	DrawTextW(p.dc, ws.c_str(), static_cast<int>(ws.size()), &r, fmt | DT_NOPREFIX);
}

std::vector<std::wstring> Wrap(Painter &p, const std::string &text, int width)
{
	std::vector<std::wstring> out;
	size_t from = 0;
	while (from <= text.size()) {
		size_t end = text.find('\n', from);
		if (end == std::string::npos) end = text.size();
		std::string para = text.substr(from, end - from);
		if (!para.empty() && para.back() == '\r') para.pop_back();
		for (const std::wstring &line : p.WrapLines(para, 0, width)) out.push_back(line);
		from = end + 1;
	}
	while (!out.empty() && out.back().empty()) out.pop_back();
	return out;
}

void SectionLabel(Painter &p, int x, int y, int w, const std::string &label, COLORREF c = kGold)
{
	p.Font(4);
	std::wstring ws = W(Tr(label));
	CharUpperBuffW(&ws[0], static_cast<DWORD>(ws.size()));
	SetTextCharacterExtra(p.dc, p.S(1));
	RECT r = p.R(x, y, w, 18);
	SetTextColor(p.dc, c);
	DrawTextW(p.dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	SetTextCharacterExtra(p.dc, 0);
}

// A heater shield: flat top, straight sides down to the middle, curving to a point.
void ShieldShape(Painter &p, int x, int y, int w, int h, COLORREF fill, COLORREF edge, bool filled)
{
	std::vector<POINT> pts;
	const auto add = [&](double px, double py) { pts.push_back({p.S(x) + static_cast<int>(px * p.S(w)), p.S(y) + static_cast<int>(py * p.S(h))}); };
	add(0, 0);
	add(1, 0);
	add(1, 0.48);
	for (int i = 1; i <= 8; i++) {
		const double t = i / 8.0, u = 1 - t;
		add(u * u * 1 + 2 * u * t * 0.98 + t * t * 0.5, u * u * 0.48 + 2 * u * t * 0.86 + t * t * 1);
	}
	for (int i = 1; i <= 7; i++) {
		const double t = i / 8.0, u = 1 - t;
		add(u * u * 0.5 + 2 * u * t * 0.02 + t * t * 0, u * u * 1 + 2 * u * t * 0.86 + t * t * 0.48);
	}
	HBRUSH b = filled ? CreateSolidBrush(fill) : static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
	HPEN pen = CreatePen(PS_SOLID, std::max(1, p.S(1)), edge);
	HGDIOBJ ob = SelectObject(p.dc, b), op = SelectObject(p.dc, pen);
	Polygon(p.dc, pts.data(), static_cast<int>(pts.size()));
	SelectObject(p.dc, ob);
	SelectObject(p.dc, op);
	if (filled) DeleteObject(b);
	DeleteObject(pen);
}

void Shield(Layer &l, const ConquestSnapshot &g, int index, int x, int y, int w, int h, int alpha)
{
	const ConquestTerritory &t = g.territories[index];
	if (t.shield) {
		l.images.push_back({t.shield, x, y, w, h, alpha});
		return;
	}
	const COLORREF c = ToColorRef(g.colors[t.standing]);
	ShieldShape(l.p, x + 2, y + 1, w - 4, h - 2, alpha == 255 ? c : RGB(GetRValue(c) / 2, GetGValue(c) / 2, GetBValue(c) / 2), kGold, true);
}

void DrawImage(Canvas &c, const Pending &d)
{
	const ConquestImage &img = *d.img;
	if (img.w <= 0 || img.h <= 0 || d.w <= 0 || d.h <= 0) return;
	for (int y = 0; y < d.h; y++) {
		const int ty = d.y + y;
		if (ty < 0 || ty >= c.h) continue;
		const int sy0 = y * img.h / d.h, sy1 = std::max(sy0 + 1, (y + 1) * img.h / d.h);
		for (int x = 0; x < d.w; x++) {
			const int tx = d.x + x;
			if (tx < 0 || tx >= c.w) continue;
			const int sx0 = x * img.w / d.w, sx1 = std::max(sx0 + 1, (x + 1) * img.w / d.w);
			uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					const uint32_t px = img.argb[static_cast<size_t>(sy) * img.w + sx], pa = px >> 24;
					r += ((px >> 16) & 255) * pa;
					g += ((px >> 8) & 255) * pa;
					b += (px & 255) * pa;
					a += pa;
					n++;
				}
			}
			if (!a) continue;
			const uint32_t alpha = a / n * d.alpha / 255;
			uint32_t &dst = c.bits[static_cast<size_t>(ty) * c.w + tx];
			const uint32_t sr = r / a, sg = g / a, sb = b / a;
			const uint32_t dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
			dst = ((sr * alpha + dr * (255 - alpha)) / 255) << 16 | ((sg * alpha + dg * (255 - alpha)) / 255) << 8 | ((sb * alpha + db * (255 - alpha)) / 255);
		}
	}
}

// One region of the screen: the game's pixels are copied in first, so whatever is not painted keeps showing the game.
template <typename F>
void Render(Canvas &c, uint32_t *buf, int w, int h, const RECT &r, int pressAction, int pressArg, std::vector<Hit> &hits, int glass, F paint)
{
	const int rw = r.right - r.left, rh = r.bottom - r.top;
	if (rw <= 0 || rh <= 0 || !c.Ensure(rw, rh)) return;
	for (int y = 0; y < rh; y++) {
		const int sy = r.top + y;
		uint32_t *row = c.bits + static_cast<size_t>(y) * rw;
		for (int x = 0; x < rw; x++) {
			const int sx = r.left + x;
			row[x] = sy >= 0 && sy < h && sx >= 0 && sx < w ? buf[static_cast<size_t>(sy) * w + sx] & 0xffffff : 0;
		}
	}
	if (glass) {
		const uint32_t tr = GetRValue(pal::panel), tg = GetGValue(pal::panel), tb = GetBValue(pal::panel);
		for (size_t i = 0, n = static_cast<size_t>(rw) * rh; i < n; i++) {
			const uint32_t px = c.bits[i];
			c.bits[i] = (((px >> 16) & 255) * (255 - glass) + tr * glass) / 255 << 16 | (((px >> 8) & 255) * (255 - glass) + tg * glass) / 255 << 8 |
				((px & 255) * (255 - glass) + tb * glass) / 255;
		}
	}
	EnsureFonts(1.0f);
	Painter p{c.dc, 1.0f, r.left, r.top, &hits};
	std::copy(std::begin(g_fonts), std::end(g_fonts), std::begin(p.fonts));
	p.pressAction = pressAction;
	p.pressArg = pressArg;
	Layer layer{p, {}};
	paint(layer);
	GdiFlush();
	for (const Pending &d : layer.images) DrawImage(c, d);
	for (int y = 0; y < rh; y++) {
		const int ty = r.top + y;
		if (ty < 0 || ty >= h) continue;
		const uint32_t *row = c.bits + static_cast<size_t>(y) * rw;
		for (int x = 0; x < rw; x++) {
			const int tx = r.left + x;
			if (tx >= 0 && tx < w) buf[static_cast<size_t>(ty) * w + tx] = 0xff000000u | (row[x] & 0xffffff);
		}
	}
}

bool Valid(const ConquestSnapshot &g, int index) { return index >= 0 && index < static_cast<int>(g.territories.size()); }

struct Standing {
	const char *line, *detail;
	COLORREF color;
};

Standing Describe(const ConquestSnapshot &g, const ConquestTerritory &t)
{
	if (g.choosing) {
		if (t.standing == kStandingOwned) return {"You can start here", "Press Select to begin your conquest here.", pal::goldLight};
		return {"You cannot start here", "Choose one of the territories marked as yours.", kDim};
	}
	if (t.standing == kStandingOwned) return {"Your territory", "It is already part of your empire.", pal::goldLight};
	if (t.standing == kStandingConquerable) return {"You can conquer it", "It borders your empire.", kConquerText};
	return {"Out of reach", "Conquer a neighbouring territory first.", kDim};
}

void PaintTerritoryCard(Layer &l, const View &v, int y)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	const int x = kPad;
	if (!Valid(g, g.selected)) {
		ShieldShape(p, x + 4, y + 2, kShieldW - 8, kShieldH - 4, pal::well, pal::lineDim, true);
		p.Font(1);
		p.Text(x + 88, y + 10, kCardW - 88, 22, "Choose a territory", pal::goldLight);
		p.Font(3);
		p.TextWrapped(x + 88, y + 36, kCardW - 88, 40, g.choosing ? "Click a territory on the map to see where you can begin." : "Click a territory on the map to see it here.", kDim);
		return;
	}
	const ConquestTerritory &t = g.territories[g.selected];
	Shield(l, g, g.selected, x, y, kShieldW, kShieldH, 255);
	p.Font(2);
	Raw(p, x + 88, y + 2, kCardW - 88, 30, t.name, pal::goldLight);
	const Standing s = Describe(g, t);
	p.Diamond(x + 94, y + 43, 5, RGB(20, 15, 10));
	p.Diamond(x + 94, y + 43, 4, ToColorRef(g.colors[t.standing]));
	p.Font(4);
	p.Text(x + 106, y + 34, kCardW - 106, 18, s.line, s.color);
	p.Font(3);
	p.TextWrapped(x + 88, y + 55, kCardW - 88, 34, s.detail, kDim);
}

void PaintDescription(Layer &l, const View &v, int top, int bottom, int &maxScroll)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	const int x = kPad, w = kCardW - 14;
	p.Font(0);
	const std::string text = Valid(g, g.selected) ? g.territories[g.selected].description : std::string();
	if (text.empty()) {
		if (Valid(g, g.selected)) p.Text(x, top, kCardW, kLineH, "No description.", kDim);
		maxScroll = 0;
		return;
	}
	const std::vector<std::wstring> lines = Wrap(p, text, w);
	const int visible = std::max(1, (bottom - top) / kLineH);
	maxScroll = std::max(0, static_cast<int>(lines.size()) - visible);
	const int first = std::min(std::max(0, v.scroll), maxScroll);
	p.Clip(x, top, kCardW, bottom - top);
	for (int i = 0; i < visible && first + i < static_cast<int>(lines.size()); i++) p.LineW(x, top + i * kLineH, w, kLineH, lines[first + i], kText);
	p.Unclip();
	if (maxScroll > 0) {
		const int trackX = x + kCardW - 5, trackH = bottom - top;
		const int thumbH = std::max(18, trackH * visible / static_cast<int>(lines.size()));
		const int thumbY = top + (trackH - thumbH) * first / maxScroll;
		p.Fill(trackX, top, 4, trackH, pal::well);
		p.Fill(trackX, thumbY, 4, thumbH, p.Hover(trackX - 6, top, 16, trackH, v.mouseX, v.mouseY) ? pal::thumbHot : pal::thumb);
		p.AddHit(trackX - 6, top, 16, (thumbY - top) + thumbH / 2, kActScroll, -visible);
		p.AddHit(trackX - 6, thumbY + thumbH / 2, 16, bottom - thumbY - thumbH / 2, kActScroll, visible);
	}
}

void PaintBonus(Layer &l, const View &v)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	const int x = kPad, y = kBonusY;
	PaintCard(p, x, y, kCardW, kBonusH);
	const bool owned = Valid(g, g.selected) && g.territories[g.selected].standing == kStandingOwned;
	SectionLabel(p, x + 12, y + 8, kCardW - 24, owned ? "Bonus of this territory" : "Bonus when conquered");
	const std::string bonus = Valid(g, g.selected) ? g.territories[g.selected].bonus : std::string();
	p.Font(3);
	if (bonus.empty()) {
		p.Text(x + 12, y + 30, kCardW - 24, 18, Valid(g, g.selected) ? "No bonus" : "Choose a territory to see its bonus.", kDim);
		return;
	}
	const std::vector<std::wstring> lines = Wrap(p, bonus, kCardW - 24);
	const int lineH = 16, maxLines = (kBonusH - 34) / lineH;
	for (int i = 0; i < maxLines && i < static_cast<int>(lines.size()); i++) {
		std::wstring line = lines[i];
		if (i == maxLines - 1 && static_cast<int>(lines.size()) > maxLines) line += L"…";
		p.LineW(x + 12, y + 30 + i * lineH, kCardW - 24, lineH, line, kText);
	}
}

void PaintDifficulty(Layer &l, const View &v)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	const int x = kPad, y = kDifficultyY;
	SectionLabel(p, x, y, kCardW, "Difficulty", kDim);
	const int count = static_cast<int>(g.difficulties.size());
	if (!count) return;
	const int segY = y + 22, segH = 30;
	for (int i = 0; i < count; i++) {
		const int sx = x + kCardW * i / count, sw = x + kCardW * (i + 1) / count - sx;
		const bool on = i == g.difficulty;
		const bool hot = !on && p.Hover(sx, segY, sw, segH, v.mouseX, v.mouseY);
		if (on) p.FillV(sx, segY, sw, segH, pal::goldLight, kGold);
		else p.Fill(sx, segY, sw, segH, hot ? pal::fieldHover : pal::well);
		p.Frame(sx, segY, sw, segH, on ? pal::goldLight : pal::edge);
		p.Font(on ? 1 : 0);
		Raw(p, sx, segY, sw, segH, g.difficulties[i], on ? RGB(34, 22, 8) : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!on) p.AddHit(sx, segY, sw, segH, kActDifficulty, i);
	}
}

void PaintButtons(Layer &l, const View &v)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	const int x = kPad;
	const bool choose = g.choosing || (g.select.visible && !g.start.visible);
	const ConquestButton main = choose ? g.select : g.start;
	p.Button(x, kMainY, kCardW, 46, choose ? "Select" : "Attack", kActNative, choose ? kConquestSelect : kConquestStart, v.mouseX, v.mouseY, main.enabled, true, 2);
	const int half = (kCardW - 10) / 2;
	if (g.save.visible) {
		p.Button(x, kButtonsY, half, 34, "Save", kActNative, kConquestSave, v.mouseX, v.mouseY, g.save.enabled);
		p.Button(x + kCardW - half, kButtonsY, half, 34, "Quit", kActNative, kConquestQuit, v.mouseX, v.mouseY, g.quit.enabled);
	}
	else {
		p.Button(x, kButtonsY, kCardW, 34, "Quit", kActNative, kConquestQuit, v.mouseX, v.mouseY, g.quit.enabled);
	}
}

void PaintPanel(Layer &l, const View &v, int &maxScroll)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	PaintWindowFrame(p, kPanelW, kPanelH);
	int y = 22;
	if (g.choosing) {
		PaintCard(p, kPad, y, kCardW, 86);
		SectionLabel(p, kPad + 12, y + 9, kCardW - 24, "Select starting area");
		p.Font(3);
		p.TextWrapped(kPad + 12, y + 30, kCardW - 24, 52, "Click one of your territories on the map and press Select, or double-click it.", kText);
		y += 98;
	}
	PaintTerritoryCard(l, v, y);
	y += kTerritoryH;
	PaintOrnateRule(p, y + 4, kPanelW);
	y += 16;
	PaintDescription(l, v, y, kBonusY - 12, maxScroll);
	PaintBonus(l, v);
	PaintDifficulty(l, v);
	PaintButtons(l, v);
}

void PaintPath(Layer &l, const View &v)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	p.FillV(0, 0, kPathW, kPathH, pal::panelTop, pal::panel);
	p.Frame(0, 0, kPathW, kPathH, kGold);
	p.Frame(4, 4, kPathW - 8, kPathH - 8, pal::lineDim);
	PaintFrameCorners(p, 0, 0, kPathW, kPathH, 14);
	SectionLabel(p, 22, 7, 240, "Your path");
	int total = 0;
	for (const ConquestTerritory &t : g.territories) total += t.disabled ? 0 : 1;
	const int done = static_cast<int>(g.path.size());
	total = std::max(total, done);
	p.Font(3);
	Raw(p, kPathW - 22 - 240, 7, 240, 18, TrFmt("%s of %s conquered", {std::to_string(done), std::to_string(total)}), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	int candidate = -1;
	if (Valid(g, g.selected) && std::find(g.path.begin(), g.path.end(), g.selected) == g.path.end()) {
		const int standing = g.territories[g.selected].standing;
		if (g.choosing ? standing == kStandingOwned && g.path.empty() : standing == kStandingConquerable) candidate = g.selected;
	}
	const int slots = std::max(1, std::min(kMaxSlots, std::max(total, done + (candidate >= 0 ? 1 : 0))));
	const int span = slots * kSlotW + (slots - 1) * kSlotGap, x0 = (kPathW - span) / 2, top = 26;
	for (int i = 0; i < slots; i++) {
		const int sx = x0 + i * (kSlotW + kSlotGap), shieldX = sx + (kSlotW - kSlotShieldW) / 2;
		if (i > 0) p.ArrowRight(sx - kSlotGap / 2 - 3, top + 18, 7, 11, i < done ? kGold : i == done && candidate >= 0 ? pal::bronze : pal::lineDim);
		const int index = i < done && i < static_cast<int>(g.path.size()) ? g.path[i] : i == done ? candidate : -1;
		if (index < 0) {
			ShieldShape(p, shieldX + 4, top + 3, kSlotShieldW - 8, kSlotShieldH - 6, pal::well, pal::lineDim, true);
			continue;
		}
		const bool next = i >= done;
		if (next) p.Frame(shieldX - 3, top - 2, kSlotShieldW + 6, kSlotShieldH + 4, pal::edgeHot);
		Shield(l, g, index, shieldX, top, kSlotShieldW, kSlotShieldH, next ? 120 : 255);
		p.Font(3);
		Raw(p, sx - 6, top + kSlotShieldH + 2, kSlotW + 12, 15, g.territories[index].name, next ? kGold : i == done - 1 ? pal::goldLight : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintLegend(Layer &l, const View &v)
{
	Painter &p = l.p;
	p.Frame(0, 0, kLegendW, kLegendH, pal::lineDim);
	PaintFrameCorners(p, 0, 0, kLegendW, kLegendH, 9);
	static const char *const kLabels[kStandingCount] = {"Your territory", "You can conquer it", "Out of reach"};
	for (int i = 0; i < kStandingCount; i++) {
		const int y = 10 + i * 21;
		p.Fill(14, y + 3, 14, 14, ToColorRef(v.game.colors[i]));
		p.Frame(14, y + 3, 14, 14, RGB(16, 12, 8));
		p.Font(3);
		p.Text(36, y, kLegendW - 46, 20, kLabels[i], kText);
	}
}

void PaintVictory(Layer &l, const View &v)
{
	Painter &p = l.p;
	PaintWindowFrame(p, kVictoryW, kVictoryH);
	PaintWindowHeader(p, kVictoryW, "Victory", 4);
	p.Font(0);
	const std::string text = v.victoryText.empty() ? Tr("Congratulations! You have been victorious.") : v.victoryText;
	const std::vector<std::wstring> lines = Wrap(p, text, kVictoryW - 60);
	const int top = kWindowHeaderH - 2, count = std::min(2, static_cast<int>(lines.size()));
	for (int i = 0; i < count; i++) {
		SIZE sz = {};
		GetTextExtentPoint32W(p.dc, lines[i].c_str(), static_cast<int>(lines[i].size()), &sz);
		p.LineW((kVictoryW - sz.cx) / 2, top + i * kLineH, kVictoryW, kLineH, lines[i], kText);
	}
	const int okX = kOkX + (kVictoryW - kNativeVictoryW) / 2, okY = kOkY + (kVictoryH - kNativeVictoryH) / 2;
	p.Button(okX, okY, kOkW, kOkH, "OK", kActOk, 0, v.mouseX, v.mouseY, true, true);
}

RECT At(int x, int y, int w, int h) { return {x, y, x + w, y + h}; }

Canvas g_panel, g_path, g_legend, g_victory;
}

void PaintConquest(uint32_t *buf, int w, int h, const View &v, int pressAction, int pressArg, Painted &out)
{
	out = Painted();
	const int cx = w / 2, cy = h / 2;
	const ConquestSnapshot &g = v.game;
	if (g.open) {
		out.panel = At(cx + kPanelX, cy + kPanelY, kPanelW, kPanelH);
		out.path = At(cx + kPathX, cy + kPathY, kPathW, kPathH);
		out.legend = At(cx - kMapHalfW + kLegendMargin, cy + kMapHalfH - kLegendMargin - kLegendH, kLegendW, kLegendH);
		// The game's question (quit?) opens over the panel: its pixels are put back on top.
		RECT hole = {};
		std::vector<uint32_t> saved;
		if (g.confirm) {
			hole = {std::max(0, g.confirmRect[0]), std::max(0, g.confirmRect[1]), std::min(w, g.confirmRect[2] + 1), std::min(h, g.confirmRect[3] + 1)};
			for (int y = hole.top; y < hole.bottom; y++) saved.insert(saved.end(), buf + static_cast<size_t>(y) * w + hole.left, buf + static_cast<size_t>(y) * w + hole.right);
		}
		Render(g_path, buf, w, h, out.path, pressAction, pressArg, out.hits, 0, [&](Layer &l) { PaintPath(l, v); });
		Render(g_legend, buf, w, h, out.legend, pressAction, pressArg, out.hits, 200, [&](Layer &l) { PaintLegend(l, v); });
		Render(g_panel, buf, w, h, out.panel, pressAction, pressArg, out.hits, 0, [&](Layer &l) { PaintPanel(l, v, out.maxScroll); });
		if (!saved.empty()) {
			size_t i = 0;
			for (int y = hole.top; y < hole.bottom; y++, i += hole.right - hole.left) std::copy(saved.begin() + i, saved.begin() + i + (hole.right - hole.left), buf + static_cast<size_t>(y) * w + hole.left);
		}
	}
	if (v.victory) {
		DimBackdrop(buf, w, h);
		out.hits.clear();
		const RECT box = At(cx - kVictoryW / 2, cy - kVictoryH / 2, kVictoryW, kVictoryH);
		Render(g_victory, buf, w, h, box, pressAction, pressArg, out.hits, 0, [&](Layer &l) { PaintVictory(l, v); });
		const int nx = (w - kNativeVictoryW) / 2, ny = (h - kNativeVictoryH) / 2;
		out.ok = At(nx + kOkX, ny + kOkY, kOkW, kOkH);
	}
}

}
