// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/chatui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "i18n/tr.h"
#include "ui/textedit.h"
#include "ui/uikit.h"
#include "ui/widgets.h"

namespace chatui {
namespace {

using namespace ui;

constexpr int kWidth = 500, kBarH = 28, kRowH = 19, kGap = 6, kPad = 8, kIdleRows = 8, kTypingRows = 12, kPinW = 12;
constexpr COLORREF kCream = RGB(242, 232, 206), kClock = RGB(178, 166, 140), kAllies = RGB(146, 214, 122), kAll = RGB(228, 188, 98),
				   kPrivate = RGB(224, 156, 234), kNote = RGB(214, 202, 172), kPanel = RGB(14, 10, 6), kAttack = RGB(240, 98, 66),
				   kDanger = RGB(246, 166, 52);

struct Fonts {
	HFONT text = nullptr, name = nullptr, meta = nullptr, chip = nullptr;
	float s = 0;
};
Fonts g_f;
Canvas g_black, g_white, g_menuBlack, g_menuWhite;

int PingOf(const Line &l) { return l.ping >= 0 ? l.ping : l.location && l.text.empty() ? kPingHere : -1; }
COLORREF PingColor(int type) { return type == kPingAttack ? kAttack : type == kPingDanger ? kDanger : kAll; }
Ornament PingOrnament(int type) { return type == kPingAttack ? kOrnamentSwords : type == kPingDanger ? kOrnamentFlag : kOrnamentPin; }
std::string PingText(const Line &l)
{
	const int type = PingOf(l);
	if (type == kPingHere && l.text.empty()) return Tr("marked a point on the map");
	return Tr(ChatPingPhrase(type));
}

HFONT MakeFont(float s, int size, int weight, const wchar_t *face)
{
	// Grey antialiasing: ClearType's coloured fringes show over the terrain.
	return CreateFontW(-static_cast<int>(size * s + 0.5f), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}

void EnsureChatFonts(float s)
{
	if (g_f.text && g_f.s == s) return;
	for (HFONT f : {g_f.text, g_f.name, g_f.meta, g_f.chip}) {
		if (f) DeleteObject(f);
	}
	g_f.text = MakeFont(s, 15, FW_NORMAL, L"Georgia");
	g_f.name = MakeFont(s, 15, FW_BOLD, L"Georgia");
	g_f.meta = MakeFont(s, 12, FW_SEMIBOLD, L"Segoe UI");
	g_f.chip = MakeFont(s, 13, FW_BOLD, L"Georgia");
	g_f.s = s;
}

// Dark player colours (the blue, the black) would vanish on the dark panel.
COLORREF Readable(COLORREF c)
{
	const int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
	const int luma = (299 * r + 587 * g + 114 * b) / 1000;
	if (luma >= 120) return c;
	const int t = std::min(150, (120 - luma) * 255 / 160);
	return RGB(r + (255 - r) * t / 255, g + (255 - g) * t / 255, b + (255 - b) * t / 255);
}

const char *TagOf(int scope) { return scope == kChatAllies ? "Allies" : scope == kChatPrivate ? "Private" : "All"; }
COLORREF TagColor(int scope) { return scope == kChatAllies ? kAllies : scope == kChatPrivate ? kPrivate : kAll; }

struct Measured {
	int clockX = 0, tagX = 0, nameX = 0, toX = 0, pinX = 0, textX = 0, indent = 0;
	std::wstring clock, tag, name, to;
	bool note = false;
};

struct Row {
	int line = 0;
	bool first = false;
	std::wstring text;
};

int WidthOf(HDC dc, HFONT font, const std::wstring &t, float s)
{
	SelectObject(dc, font);
	SIZE sz = {};
	GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
	return static_cast<int>(sz.cx / s + 0.99f);
}

Measured Measure(Painter &p, const Line &l)
{
	Measured m;
	m.clock = W(l.clock);
	m.tag = W("[" + Tr(TagOf(l.scope)) + "]");
	m.name = W(l.name);
	if (!l.toName.empty()) m.to = W(" \xe2\x86\x92 " + l.toName);
	m.note = PingOf(l) >= 0;
	int x = kPad;
	m.clockX = x;
	if (!m.clock.empty()) x += WidthOf(p.dc, g_f.meta, m.clock, p.s) + 6;
	m.indent = x;
	m.tagX = x;
	x += WidthOf(p.dc, g_f.meta, m.tag, p.s) + 6;
	m.nameX = x;
	x += WidthOf(p.dc, g_f.name, m.name, p.s);
	m.toX = x;
	if (!m.to.empty()) x += WidthOf(p.dc, g_f.name, m.to, p.s);
	m.pinX = x + 6;
	if (l.location) x += kPinW + 9;
	x += m.note ? 6 : WidthOf(p.dc, g_f.text, L": ", p.s);
	m.textX = x;
	return m;
}

// Over the bare map a line needs a full outline to read on grass, roads and snow alike; on the panel a shadow is enough.
bool g_outline = false;

void ShadowLine(Painter &p, int x, int y, const std::wstring &t, COLORREF c)
{
	const RECT r = p.R(x, y, 400, kRowH);
	const auto draw = [&](int dx, int dy, COLORREF color) {
		RECT q = r;
		OffsetRect(&q, dx, dy);
		SetTextColor(p.dc, color);
		DrawTextW(p.dc, t.c_str(), static_cast<int>(t.size()), &q, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
	};
	if (g_outline) {
		const int around[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
		for (const auto &o : around) draw(o[0], o[1], RGB(0, 0, 0));
	}
	else {
		draw(1, 1, RGB(0, 0, 0));
	}
	draw(0, 0, c);
}

// A dark rim one pixel wider all round keeps the pin readable on any ground.
void PaintPin(Painter &p, int x, int y, int size, COLORREF c, COLORREF rim = RGB(0, 0, 0))
{
	PaintOrnament(p, kOrnamentPin, x - 1, y - 1, size + 2, size + 2, rim);
	PaintOrnament(p, kOrnamentPin, x, y, size, size, c);
}

struct Layout {
	int w = kWidth;
	bool typing = false;
	std::vector<Measured> measured;
	std::vector<Row> rows;
	int first = 0, shown = 0;
	int rowsTop = 0, barY = 0, height = 0;
	int maxScroll = 0, scroll = 0;
};

Layout Plan(Painter &p, const View &v, int widthDesign, int heightDesign)
{
	Layout L;
	L.typing = v.typing;
	L.w = widthDesign;
	const int textRight = L.w - kPad - (v.typing ? 6 : 0);
	for (size_t i = 0; i < v.lines.size(); i++) {
		const Line &l = v.lines[i];
		L.measured.push_back(Measure(p, l));
		const Measured &m = L.measured.back();
		SelectObject(p.dc, g_f.text);
		const std::string body = m.note ? PingText(l) : l.text;
		const std::vector<std::wstring> wrapped = p.WrapLines(body, m.textX - m.indent, std::max(40, textRight - m.indent));
		for (size_t k = 0; k < wrapped.size(); k++) L.rows.push_back({static_cast<int>(i), k == 0, wrapped[k]});
	}
	const int total = static_cast<int>(L.rows.size());
	const int room = std::max(1, (heightDesign - kBarH - kGap - 2 * kPad - 40) / kRowH);
	const int limit = std::min(v.typing ? kTypingRows : kIdleRows, room);
	L.shown = std::min(total, limit);
	L.maxScroll = v.typing ? std::max(0, total - L.shown) : 0;
	L.scroll = std::max(0, std::min(v.scroll, L.maxScroll));
	L.first = total - L.shown - L.scroll;
	const int rowsH = L.shown * kRowH;
	if (v.typing) {
		L.rowsTop = kPad;
		L.barY = kPad + rowsH + (L.shown ? kGap : 0);
		L.height = L.barY + kBarH + kPad;
	}
	else {
		// Idle lines stay where they are while typing, so nothing moves when the bar opens.
		L.rowsTop = 0;
		L.height = rowsH + kGap + kBarH + kPad;
	}
	return L;
}

void PaintRow(Painter &p, const View &v, const Layout &L, int r, int y)
{
	const Row &row = L.rows[r];
	const Line &l = v.lines[row.line];
	const Measured &m = L.measured[row.line];
	if (!row.first) {
		SelectObject(p.dc, g_f.text);
		ShadowLine(p, m.indent, y, row.text, m.note ? (PingOf(l) == kPingHere ? kNote : PingColor(PingOf(l))) : kCream);
		return;
	}
	SelectObject(p.dc, g_f.meta);
	if (!m.clock.empty()) ShadowLine(p, m.clockX, y + 3, m.clock, kClock);
	ShadowLine(p, m.tagX, y + 3, m.tag, TagColor(l.scope));
	SelectObject(p.dc, g_f.name);
	const COLORREF nameColor = Readable(l.color);
	ShadowLine(p, m.nameX, y, m.name, nameColor);
	if (!m.to.empty()) ShadowLine(p, m.toX, y, m.to, kPrivate);
	if (l.location) {
		const int type = std::max(0, PingOf(l));
		PaintOrnament(p, PingOrnament(type), m.pinX - 3, y, kPinW + 4, kPinW + 4, RGB(0, 0, 0));
		PaintOrnament(p, PingOrnament(type), m.pinX - 2, y + 1, kPinW + 2, kPinW + 2, PingColor(type));
	}
	SelectObject(p.dc, g_f.text);
	if (!m.note) ShadowLine(p, m.textX - WidthOf(p.dc, g_f.text, L": ", p.s), y, L": ", kCream);
	SelectObject(p.dc, g_f.text);
	ShadowLine(p, m.textX, y, row.text, m.note ? (PingOf(l) == kPingHere ? kNote : PingColor(PingOf(l))) : kCream);
}

void PaintChip(Painter &p, const View &v, int x, int y, int w, int h)
{
	const bool hot = p.Hover(x, y, w, h, v.mouseX, v.mouseY);
	if (v.targetScope == kChatAllies) p.FillV(x, y, w, h, hot ? RGB(64, 110, 54) : RGB(52, 92, 44), RGB(26, 52, 22));
	else if (v.targetScope == kChatAll) p.FillV(x, y, w, h, hot ? RGB(190, 138, 56) : RGB(168, 119, 44), RGB(104, 70, 24));
	else p.FillV(x, y, w, h, hot ? RGB(70, 54, 34) : RGB(56, 42, 26), RGB(30, 22, 14));
	const COLORREF edge = v.targetScope == kChatAllies ? kAllies : v.targetScope == kChatAll ? pal::goldLight : Readable(v.targetColor);
	p.Frame(x, y, w, h, hot ? pal::focus : edge);
	p.Fill(x + 1, y + 1, w - 2, 1, v.targetScope == kChatAll ? RGB(226, 186, 104) : RGB(92, 73, 46));
	SelectObject(p.dc, g_f.chip);
	const std::string label = v.targetScope == kChatPrivate ? v.targetName : Tr(TagOf(v.targetScope));
	const COLORREF text = v.targetScope == kChatPrivate ? Readable(v.targetColor) : v.targetScope == kChatAll ? RGB(40, 24, 8) : RGB(236, 248, 226);
	p.Text(x + 8, y, w - 22, h, label, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	PaintOrnament(p, kOrnamentChevron, x + w - 15, y + h / 2 - 5, 10, 10, v.targetScope == kChatAll ? RGB(70, 44, 14) : edge);
	p.AddHit(x, y, w, h, kActChip);
}

void PaintPinButton(Painter &p, const View &v, int x, int y, int size)
{
	const bool hot = p.Hover(x, y, size, size, v.mouseX, v.mouseY);
	if (v.withLocation) p.FillV(x, y, size, size, hot ? RGB(190, 138, 56) : RGB(168, 119, 44), RGB(104, 70, 24));
	else p.FillV(x, y, size, size, hot ? RGB(82, 63, 38) : RGB(62, 47, 28), hot ? RGB(54, 41, 24) : RGB(36, 27, 16));
	p.Frame(x, y, size, size, hot ? pal::focus : v.withLocation ? pal::goldLight : pal::edge);
	const int icon = size - 8;
	if (v.withLocation) PaintPin(p, x + (size - icon) / 2, y + 4, icon, RGB(52, 30, 8), RGB(226, 186, 104));
	else PaintPin(p, x + (size - icon) / 2, y + 4, icon, hot ? pal::focus : pal::goldLight);
	p.AddHit(x, y, size, size, kActPin);
}

void PaintBar(Painter &p, const View &v, const Layout &L)
{
	const int y = L.barY;
	SelectObject(p.dc, g_f.chip);
	const std::string label = v.targetScope == kChatPrivate ? v.targetName : Tr(TagOf(v.targetScope));
	const int chipW = std::min(150, std::max(64, p.TextWidth(label) + 26));
	PaintChip(p, v, kPad, y, chipW, kBarH);
	const int pinX = L.w - kPad - kBarH;
	const int fieldX = kPad + chipW + 4, fieldW = pinX - 4 - fieldX;
	p.fonts[0] = g_f.text;
	PaintEditField(p, fieldX, y, fieldW, kBarH, v.text, "", true, false, v.edit, kActField);
	if (v.text.empty()) {
		p.Font(0);
		p.Text(fieldX + 12, y, fieldW - 16, kBarH, Tr("Tab changes who reads it"), RGB(120, 108, 86), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	PaintPinButton(p, v, pinX, y, kBarH);
}

void PaintPass(Painter &p, const View &v, const Layout &L, int pw, int ph, bool white)
{
	RECT all = {0, 0, pw, ph};
	HBRUSH bg = CreateSolidBrush(white ? RGB(255, 255, 255) : RGB(0, 0, 0));
	FillRect(p.dc, &all, bg);
	DeleteObject(bg);
	g_outline = !L.typing;
	if (L.typing) {
		p.Fill(0, 0, L.w, 1, kGold);
		p.Fill(0, L.height - 1, L.w, 1, pal::lineDim);
		p.Diamond(0, 0, 3, pal::goldLight);
		p.Diamond(L.w - 1, 0, 3, pal::goldLight);
	}
	for (int i = 0; i < L.shown; i++) PaintRow(p, v, L, L.first + i, L.rowsTop + i * kRowH);
	if (L.typing && L.maxScroll > 0) {
		const int trackY = L.rowsTop, trackH = L.shown * kRowH, x = L.w - 6;
		const int total = static_cast<int>(L.rows.size());
		const int thumbH = std::max(10, trackH * L.shown / total);
		const int thumbY = trackY + (trackH - thumbH) * (L.maxScroll - L.scroll) / L.maxScroll;
		p.Fill(x, trackY, 2, trackH, pal::track);
		p.Fill(x - 1, thumbY, 4, thumbH, pal::thumb);
	}
	if (L.typing) PaintBar(p, v, L);
}

// Each pass painted the same thing over black and over white: where a pixel came out the same it is solid, where it moved
// with the background it lets the game through, exactly in the share GDI blended it. Fade multiplies that share.
void Composite(uint32_t *buf, int w, int h, int ox, int oy, const RECT &r, int alpha, const Canvas &onBlack = g_black, const Canvas &onWhite = g_white)
{
	if (alpha <= 0) return;
	const int cw = onBlack.w;
	for (int y = std::max<int>(0, r.top); y < std::min<int>(onBlack.h, r.bottom); y++) {
		const int ty = oy + y;
		if (ty < 0 || ty >= h) continue;
		const uint32_t *a = onBlack.bits + static_cast<size_t>(y) * cw, *b = onWhite.bits + static_cast<size_t>(y) * cw;
		uint32_t *d = buf + static_cast<size_t>(ty) * w;
		for (int x = std::max<int>(0, r.left); x < std::min<int>(cw, r.right); x++) {
			const int tx = ox + x;
			if (tx < 0 || tx >= w) continue;
			const uint32_t A = a[x] & 0xffffff, B = b[x] & 0xffffff;
			if (A == 0 && B == 0xffffff) continue;
			const uint32_t D = d[tx];
			uint32_t out = 0;
			for (int sh = 0; sh <= 16; sh += 8) {
				const int ac = (A >> sh) & 255, bc = std::max<int>(ac, (B >> sh) & 255), dc = (D >> sh) & 255;
				const int blended = ac + dc * (bc - ac) / 255;
				const int c = dc + (blended - dc) * alpha / 255;
				out |= static_cast<uint32_t>(std::max(0, std::min(255, c))) << sh;
			}
			d[tx] = 0xff000000u | out;
		}
	}
}

void Darken(uint32_t *buf, int w, int h, int x0, int y0, int x1, int y1, int opacity)
{
	const int pr = GetRValue(kPanel), pg = GetGValue(kPanel), pb = GetBValue(kPanel);
	for (int y = std::max(0, y0); y < std::min(h, y1); y++) {
		uint32_t *row = buf + static_cast<size_t>(y) * w;
		for (int x = std::max(0, x0); x < std::min(w, x1); x++) {
			const uint32_t c = row[x];
			const int r = (((c >> 16) & 255) * (255 - opacity) + pr * opacity) / 255;
			const int g = (((c >> 8) & 255) * (255 - opacity) + pg * opacity) / 255;
			const int b = ((c & 255) * (255 - opacity) + pb * opacity) / 255;
			row[x] = 0xff000000u | (r << 16) | (g << 8) | b;
		}
	}
}

constexpr int kMenuRadius = 50, kMenuButton = 21, kMenuSize = 2 * (kMenuRadius + kMenuButton) + 120;
const char *const kMenuLabels[kPingTypes] = {"On my way", "Attack here", "Danger"};

void PaintMenuPass(Painter &p, const PingMenuView &v, int size, bool white)
{
	RECT all = {0, 0, p.S(size), p.S(size)};
	HBRUSH bg = CreateSolidBrush(white ? RGB(255, 255, 255) : RGB(0, 0, 0));
	FillRect(p.dc, &all, bg);
	DeleteObject(bg);
	g_outline = true;
	const int c = size / 2;
	p.Diamond(c, c, 5, RGB(0, 0, 0));
	p.Diamond(c, c, 4, pal::goldLight);
	for (int t = 0; t < kPingTypes; t++) {
		int dx = 0, dy = 0;
		ChatPingMenuOffset(t, kMenuRadius, dx, dy);
		const bool hot = v.hover == t;
		const int x = c + dx, y = c + dy, r = kMenuButton + (hot ? 3 : 0);
		p.Ellipse(x - r - 1, y - r - 1, 2 * r + 2, 2 * r + 2, RGB(0, 0, 0));
		p.Ellipse(x - r, y - r, 2 * r, 2 * r, hot ? pal::focus : kGold);
		p.Ellipse(x - r + 2, y - r + 2, 2 * r - 4, 2 * r - 4, hot ? RGB(112, 78, 32) : RGB(26, 20, 13));
		const int icon = hot ? 30 : 26;
		PaintOrnament(p, PingOrnament(t), x - icon / 2, y - icon / 2, icon, icon, PingColor(t));
		SelectObject(p.dc, g_f.chip);
		const std::wstring label = W(Tr(kMenuLabels[t]));
		const int lw = WidthOf(p.dc, g_f.chip, label, p.s);
		const int ly = t == kPingHere ? y - r - 20 : y + r + 3;
		ShadowLine(p, x - lw / 2, ly, label, hot ? pal::focus : kText);
	}
}

}

float Scale(int, int h) { return std::max(0.85f, std::min(1.3f, h / 880.0f)); }

void PaintChat(uint32_t *buf, int w, int h, const View &v, Painted &out)
{
	out = Painted();
	if (!v.typing && v.lines.empty()) return;
	const float s = Scale(w, h);
	EnsureChatFonts(s);
	const int widthDesign = std::max(220, std::min(kWidth, static_cast<int>((v.maxWidth > 0 ? v.maxWidth : w) / s)));
	const int pw = static_cast<int>(widthDesign * s + 0.5f);
	const int maxPh = static_cast<int>(std::max(80.0f, static_cast<float>(v.bottom)) + 0.5f);
	if (!g_black.Ensure(pw, std::max(1, maxPh)) || !g_white.Ensure(pw, std::max(1, maxPh))) return;
	Painter p{g_black.dc, s, 0, 0, &out.hits};
	p.raw = true;
	p.fonts[0] = g_f.text;
	Layout L = Plan(p, v, widthDesign, static_cast<int>(v.bottom / s));
	out.maxScroll = L.maxScroll;
	out.pageRows = std::max(1, L.shown - 1);
	const int ph = std::min(maxPh, static_cast<int>(L.height * s + 0.5f));
	const int oy = v.bottom - ph, ox = v.left;
	FieldsBeginFrame();
	Painter first{g_black.dc, s, ox, oy, &out.hits};
	first.raw = true;
	first.fonts[0] = g_f.text;
	std::vector<Hit> ignored;
	Painter second{g_white.dc, s, ox, oy, &ignored};
	second.raw = true;
	second.fonts[0] = g_f.text;
	PaintPass(first, v, L, pw, ph, false);
	PaintPass(second, v, L, pw, ph, true);
	GdiFlush();
	if (L.typing) {
		out.panel = {ox, oy, ox + pw, oy + ph};
		Darken(buf, w, h, ox, oy, ox + pw, oy + ph, 196);
		if (L.shown) first.AddHit(0, L.rowsTop, L.w, L.shown * kRowH, kActHistory);
		Composite(buf, w, h, ox, oy, {0, 0, pw, ph}, 255);
		return;
	}
	for (int i = 0; i < L.shown; i++) {
		const RECT r = first.R(0, L.rowsTop + i * kRowH, L.w, kRowH);
		Composite(buf, w, h, ox, oy, r, v.lines[L.rows[L.first + i].line].alpha);
	}
}


int PingMenuDeadZone(int w, int h) { return static_cast<int>(16 * Scale(w, h) + 0.5f); }

void PaintPingMenu(uint32_t *buf, int w, int h, const PingMenuView &v)
{
	const float s = Scale(w, h);
	EnsureChatFonts(s);
	const int px = static_cast<int>(kMenuSize * s + 0.5f);
	if (!g_menuBlack.Ensure(px, px) || !g_menuWhite.Ensure(px, px)) return;
	std::vector<Hit> ignored;
	Painter a{g_menuBlack.dc, s, 0, 0, &ignored}, b{g_menuWhite.dc, s, 0, 0, &ignored};
	a.raw = b.raw = true;
	PaintMenuPass(a, v, kMenuSize, false);
	PaintMenuPass(b, v, kMenuSize, true);
	GdiFlush();
	Composite(buf, w, h, v.x - px / 2, v.y - px / 2, {0, 0, px, px}, 255, g_menuBlack, g_menuWhite);
}

}
