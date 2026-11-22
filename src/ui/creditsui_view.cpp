// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/creditsui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "i18n/tr.h"
#include "ui/fullscreen.h"
#include "ui/ornaments.h"
#include "ui/patronui.h"
#include "ui/widgets.h"

using namespace ui;

namespace creditsui {
namespace {

constexpr int kRailW = 232, kColumnMaxW = 690, kFade = 60;
// The space above the first line, and so where a line of the index lands when it is chosen.
constexpr int kLead = 72;
constexpr int kColumns = 3, kRailLines = 2;

const COLORREF kSubtitle = pal::textDim, kPlain = pal::body, kNoteColor = pal::caption;
const COLORREF kRailHot = pal::raised, kRailCurrent = pal::selected;

enum RowKind { kRowLine, kRowCells, kRowRule };

// Where the rail and the column go, in the units of the roll.
struct RollFrame {
	int railX = 0, top = 0, bottom = 0, areaX = 0, areaW = 0;
};

struct Row {
	RowKind kind = kRowLine;
	int y = 0, h = 0;
	int font = 0, spacing = 0;
	COLORREF color = pal::text;
	std::wstring text;
	// In pixels, measured with the spacing of its letters.
	int width = 0;
	std::vector<std::wstring> cells;
	// One per cell, or the line's, when they are patrons.
	std::vector<PatronTier> tiers;
};

struct Layout {
	std::vector<Row> rows;
	std::vector<int> entryY;
	std::vector<std::string> labels;
	int height = 0;
};

enum Last { kLastNone, kLastHeading, kLastSubtitle, kLastRole, kLastNote, kLastNames, kLastText, kLastRule };

std::wstring Upper(std::wstring w)
{
	if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	return w;
}

std::string Utf8(const std::wstring &w)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
}

int PixelWidth(Painter &p, const std::wstring &w, int font, int spacing)
{
	p.Font(font);
	SetTextCharacterExtra(p.dc, p.S(spacing));
	SIZE sz = {};
	GetTextExtentPoint32W(p.dc, w.c_str(), static_cast<int>(w.size()), &sz);
	SetTextCharacterExtra(p.dc, 0);
	// The spacing also follows the last letter.
	return static_cast<int>(sz.cx) - (w.empty() ? 0 : p.S(spacing));
}

struct Builder {
	Painter &p;
	int width;
	Layout &out;
	int y = kLead;
	Last last = kLastNone;

	int Fits() const { return p.S(width); }

	void Space(Last next)
	{
		int gap = 0;
		switch (next) {
		case kLastHeading: gap = last == kLastNone || last == kLastRule ? 0 : 30; break;
		case kLastSubtitle: gap = 2; break;
		case kLastRole: gap = last == kLastHeading || last == kLastSubtitle ? 16 : last == kLastNone || last == kLastRule ? 0 : 24; break;
		case kLastNote: gap = last == kLastNames || last == kLastText ? 8 : 2; break;
		case kLastNames: gap = last == kLastHeading || last == kLastSubtitle ? 12 : last == kLastNote ? 10 : last == kLastRole ? 4 : last == kLastNames ? 0 : 12; break;
		case kLastText: gap = last == kLastNone || last == kLastRule ? 0 : last == kLastText ? 0 : 10; break;
		default: break;
		}
		y += gap;
		last = next;
	}

	void Line(const std::wstring &text, int font, int spacing, COLORREF color, int h, PatronTier tier = PatronTier::None)
	{
		Row r;
		r.y = y;
		r.h = h;
		r.font = font;
		r.spacing = spacing;
		r.color = color;
		r.text = text;
		r.width = PixelWidth(p, text, font, spacing);
		if (tier != PatronTier::None) r.tiers.push_back(tier);
		out.rows.push_back(std::move(r));
		y += h;
	}

	void Wrapped(const std::string &text, int font, COLORREF color, int lineH, int wrapW = 0)
	{
		p.Font(font);
		for (const std::wstring &l : p.WrapLines(text, 0, wrapW > 0 ? wrapW : width)) Line(l, font, 0, color, lineH);
	}

	// One line when it fits, wrapped otherwise.
	void Fitted(const std::string &text, int font, COLORREF color, int h)
	{
		const std::wstring w = W(text);
		if (PixelWidth(p, w, font, 0) <= Fits()) Line(w, font, 0, color, h);
		else Wrapped(text, font, color, h - 2);
	}

	void Entry(const std::string &label)
	{
		out.entryY.push_back(y);
		out.labels.push_back(label);
	}

	void Heading(const std::string &text)
	{
		Space(kLastHeading);
		const std::wstring up = Upper(W(text));
		if (PixelWidth(p, up, 5, 4) <= Fits()) {
			Line(up, 5, 4, pal::text, 48);
		}
		else if (PixelWidth(p, up, 2, 2) <= Fits()) {
			Line(up, 2, 2, pal::text, 36);
		}
		else {
			// A sentence: in its own letters, over a few lines.
			p.Font(2);
			if (p.WrapLines(text, 0, width).size() <= 3) Wrapped(text, 2, pal::text, 30);
			else Wrapped(text, 1, pal::text, 22);
		}
	}

	void Subtitle(const std::string &text, COLORREF color = kSubtitle)
	{
		Space(kLastSubtitle);
		Fitted(text, 0, color, 24);
	}

	void Role(const std::string &text)
	{
		Space(kLastRole);
		const std::wstring up = Upper(W(text));
		if (PixelWidth(p, up, 4, 2) <= Fits()) Line(up, 4, 2, pal::redText, 22);
		else Wrapped(Utf8(up), 4, pal::redText, 20);
	}

	void Note(const std::string &text)
	{
		Space(kLastNote);
		Wrapped(text, 3, kNoteColor, 19);
	}

	void Text(const std::string &text)
	{
		Space(kLastText);
		Wrapped(text, 0, kPlain, 23);
	}

	void Rule()
	{
		if (last == kLastRule || last == kLastNone) return;
		last = kLastRule;
		Row r;
		r.kind = kRowRule;
		r.y = y + 18;
		r.h = 12;
		out.rows.push_back(r);
		y += 50;
	}

	void Names(const std::vector<std::string> &names, const std::vector<PatronTier> &tiers = {})
	{
		Space(kLastNames);
		const bool patrons = tiers.size() == names.size();
		if (names.size() <= kCreditsColumnsFrom) {
			for (size_t i = 0; i < names.size(); i++) {
				const std::wstring w = W(names[i]);
				if (PixelWidth(p, w, 0, 0) <= Fits() - (patrons ? p.S(28) : 0)) Line(w, 0, 0, pal::text, 25, patrons ? tiers[i] : PatronTier::None);
				else Wrapped(names[i], 0, pal::text, 23);
			}
			return;
		}
		for (size_t i = 0; i < names.size(); i += kColumns) {
			Row r;
			r.kind = kRowCells;
			r.y = y;
			r.h = 24;
			r.font = 0;
			r.color = pal::text;
			for (size_t c = i; c < names.size() && c < i + kColumns; c++) {
				r.cells.push_back(W(names[c]));
				if (patrons) r.tiers.push_back(tiers[c]);
			}
			out.rows.push_back(std::move(r));
			y += 24;
		}
	}

	void Item(const CreditsItem &it)
	{
		if (it.kind == kCreditsRule) {
			Rule();
			return;
		}
		if (it.kind == kCreditsNames) {
			Names(it.lines);
			return;
		}
		for (const std::string &l : it.lines) {
			switch (it.kind) {
			case kCreditsHeading: Heading(l); break;
			case kCreditsSubtitle: Subtitle(l); break;
			case kCreditsRole: Role(l); break;
			case kCreditsNote: Note(l); break;
			default: Text(l); break;
			}
		}
	}
};

void BuildLayout(Painter &p, const Content &c, int width, Layout &out)
{
	out = Layout();
	Builder b{p, width, out};
	b.Entry(c.mod.name);
	b.Heading(c.mod.name);
	b.Subtitle(Tr("Version ") + c.mod.version);
	b.Subtitle(c.mod.tagline, kPlain);
	b.Role(Tr("Created by"));
	b.Space(kLastNames);
	b.Line(W(c.mod.author), 2, 3, pal::text, 34);
	std::string summary;
	for (const std::string &l : c.mod.summary) {
		std::string part = l;
		while (!part.empty() && (part.back() == ',' || part.back() == ' ')) part.pop_back();
		summary += (summary.empty() ? "" : ", ") + part;
	}
	b.Space(kLastText);
	b.Wrapped(summary, 0, kPlain, 23, std::min(width, 560));
	for (const ModCreditsGroup &g : c.mod.groups) {
		b.Role(g.title);
		b.Names(g.names);
	}
	if (!c.patrons.empty()) {
		b.Rule();
		const std::string title = Tr("Patrons of Imperivm Resurrected");
		b.Entry(title);
		b.Heading(title);
		std::vector<std::string> names;
		std::vector<PatronTier> tiers;
		for (const Patron &pa : c.patrons) {
			names.push_back(pa.name);
			tiers.push_back(pa.tier);
		}
		b.Names(names, tiers);
	}
	b.Rule();
	size_t entry = 0;
	for (size_t i = 0; i < c.game.items.size(); i++) {
		for (; entry < c.game.index.size() && c.game.index[entry].item == i; entry++) b.Entry(c.game.index[entry].label);
		b.Item(c.game.items[i]);
	}
	out.height = b.y + 24;
}

const Layout &CachedLayout(Painter &p, const Content &c, int width)
{
	static Layout layout;
	static unsigned stamp = 0;
	static int cachedW = 0;
	static float cachedS = 0;
	if (stamp != c.stamp || cachedW != width || cachedS != p.s) {
		BuildLayout(p, c, width, layout);
		stamp = c.stamp;
		cachedW = width;
		cachedS = p.s;
	}
	return layout;
}

// A premultiplied band of the colour, opaque at one end and clear at the other, stretched over the edges of the column.
HBITMAP FadeBitmap(COLORREF color, bool opaqueTop)
{
	static std::map<std::pair<COLORREF, bool>, HBITMAP> cache;
	const auto key = std::make_pair(color, opaqueTop);
	const auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	const int n = 64;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = 1;
	bi.bmiHeader.biHeight = -n;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (bmp && bits) {
		uint32_t *px = static_cast<uint32_t *>(bits);
		for (int i = 0; i < n; i++) {
			const double t = static_cast<double>(opaqueTop ? n - 1 - i : i) / (n - 1);
			// Smooth at both ends, so that the edge of the band does not show.
			const uint32_t a = static_cast<uint32_t>(255 * t * t * (3 - 2 * t) + 0.5);
			px[i] = (a << 24) | ((GetRValue(color) * a / 255) << 16) | ((GetGValue(color) * a / 255) << 8) | (GetBValue(color) * a / 255);
		}
	}
	cache[key] = bmp;
	return bmp;
}

void Blit(Painter &p, HBITMAP bmp, int srcW, int srcH, const RECT &r)
{
	if (!bmp || r.right <= r.left || r.bottom <= r.top) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem, 0, 0, srcW, srcH, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

void DrawLine(Painter &p, const Row &r, int centerX, int top)
{
	p.Font(r.font);
	SetTextCharacterExtra(p.dc, p.S(r.spacing));
	SetTextColor(p.dc, r.color);
	int x = centerX - r.width / 2;
	if (!r.tiers.empty()) {
		const PatronColors c = PatronColorsOf(r.tiers[0]);
		const int laurel = p.S(20), gap = p.S(8);
		x += (laurel + gap) / 2;
		SetTextColor(p.dc, c.name);
		if (r.tiers[0] != PatronTier::None) {
			const RECT lr = {x - gap - laurel, top + (p.S(r.h) - laurel) / 2, x - gap, top + (p.S(r.h) - laurel) / 2 + laurel};
			Blit(p, OrnamentBitmap(kOrnamentLaurel, laurel, laurel, false, c.laurel), laurel, laurel, lr);
		}
	}
	RECT rc = {x, top, x + r.width + p.S(r.spacing) + 2, top + p.S(r.h)};
	DrawTextW(p.dc, r.text.c_str(), static_cast<int>(r.text.size()), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
	SetTextCharacterExtra(p.dc, 0);
}

void DrawCells(Painter &p, const Row &r, int left, int width, int top)
{
	const int cell = width / kColumns;
	for (size_t i = 0; i < r.cells.size(); i++) {
		const std::wstring &text = r.cells[i];
		RECT rc = {left + static_cast<int>(i) * cell + p.S(4), top, left + (static_cast<int>(i) + 1) * cell - p.S(4), top + p.S(r.h)};
		p.Font(r.font);
		SIZE sz = {};
		GetTextExtentPoint32W(p.dc, text.c_str(), static_cast<int>(text.size()), &sz);
		// The few names too long for their column get a smaller letter before they are cut.
		if (sz.cx > rc.right - rc.left) SelectObject(p.dc, BrandFont(BrandFace::Body, 13, p.s));
		SetTextColor(p.dc, i < r.tiers.size() ? PatronColorsOf(r.tiers[i]).name : r.color);
		DrawTextW(p.dc, text.c_str(), static_cast<int>(text.size()), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	p.Font(r.font);
}

void DrawRule(Painter &p, int centerX, int top, int columnW)
{
	const int w = p.S(std::min(columnW, 460)), y = top + p.S(6);
	const RECT r = {centerX - w / 2, y, centerX - w / 2 + w, y + std::max(1, p.S(1))};
	HBRUSH b = CreateSolidBrush(pal::lineStrong);
	FillRect(p.dc, &r, b);
	DeleteObject(b);
}

void PaintColumn(Painter &p, const Layout &L, const RollFrame &f, int x, int w, double scroll, double period)
{
	const int top = p.S(f.top), bottom = p.S(f.bottom);
	const int left = p.S(x), width = p.S(w), centerX = left + width / 2;
	p.Clip(x - 12, f.top, w + 24, f.bottom - f.top);
	for (int copy = 0; copy < 2; copy++) {
		// One whole offset for every row, so that they move together pixel by pixel.
		const int offset = static_cast<int>(std::floor((copy * period - scroll) * p.s + 0.5));
		for (const Row &r : L.rows) {
			const int rowTop = p.S(f.top + r.y) + offset;
			if (rowTop >= bottom) break;
			if (rowTop + p.S(r.h) <= top) continue;
			if (r.kind == kRowRule) DrawRule(p, centerX, rowTop, w);
			else if (r.kind == kRowCells) DrawCells(p, r, left, width, rowTop);
			else DrawLine(p, r, centerX, rowTop);
		}
	}
	p.Unclip();
	const int fade = p.S(kFade), areaLeft = p.S(x - 12), areaRight = p.S(x + w + 12);
	Blit(p, FadeBitmap(pal::panel, true), 1, 64, {areaLeft, top, areaRight, top + fade});
	Blit(p, FadeBitmap(pal::panel, false), 1, 64, {areaLeft, bottom - fade, areaRight, bottom});
}

void PaintRail(Painter &p, const Layout &L, const RollFrame &f, int current, const View &v)
{
	const int railX = f.railX;
	p.Font(0);
	struct Item {
		std::vector<std::wstring> lines;
		int h;
	};
	std::vector<Item> items;
	int total = 0;
	const int textX = railX + 30, textW = kRailW - 38;
	// Wrapped in the bold of the current line, so that each line fits either way.
	p.Font(1);
	for (const std::string &label : L.labels) {
		Item it;
		it.lines = p.WrapLines(label, 0, textW);
		if (it.lines.size() > kRailLines) {
			it.lines.resize(kRailLines);
			std::wstring &end = it.lines.back();
			while (!end.empty()) {
				SIZE sz = {};
				const std::wstring with = end + L"…";
				GetTextExtentPoint32W(p.dc, with.c_str(), static_cast<int>(with.size()), &sz);
				if (sz.cx <= p.S(textW)) break;
				end.pop_back();
			}
			while (!end.empty() && end.back() == L' ') end.pop_back();
			end += L"…";
		}
		it.h = static_cast<int>(it.lines.size()) * 19 + 14;
		total += it.h + 4;
		items.push_back(std::move(it));
	}
	const int railTop = f.top + 6, railH = f.bottom - railTop;
	int y = railTop;
	// Too many lines for the rail: it follows the current one.
	if (total > railH && current >= 0) {
		int before = 0;
		for (int i = 0; i < current; i++) before += items[i].h + 4;
		y -= std::max(0, std::min(before - railH / 3, total - railH));
	}
	p.Clip(railX - 4, railTop, kRailW + 8, railH);
	for (size_t i = 0; i < items.size(); i++) {
		const Item &it = items[i];
		const bool now = static_cast<int>(i) == current;
		const bool hot = !now && p.Hover(railX, y, kRailW, it.h, v.mouseX, v.mouseY);
		if (now) {
			p.Fill(railX, y, kRailW, it.h, kRailCurrent);
			p.Fill(railX, y, 3, it.h, pal::red);
		}
		else if (hot) {
			p.Fill(railX, y, kRailW, it.h, kRailHot);
		}
		p.Font(now ? 1 : 0);
		for (size_t l = 0; l < it.lines.size(); l++) {
			p.LineW(textX, y + 7 + static_cast<int>(l) * 19, textW, 19, it.lines[l], now ? pal::text : pal::textDim);
		}
		const int hitTop = std::max(y, railTop), hitBottom = std::min(y + it.h, railTop + railH);
		if (hitBottom > hitTop) p.AddHit(railX, hitTop, kRailW, hitBottom - hitTop, kActJump, static_cast<int>(i));
		y += it.h + 4;
	}
	p.Unclip();
}

}

float ScreenScale(int frameW, int frameH)
{
	(void)frameW;
	return std::max(kMinScale, frameH / static_cast<float>(kScreenH));
}

int CurrentEntry(const std::vector<double> &entries, double scroll, double period, int viewport)
{
	if (entries.empty()) return -1;
	double at = period > 0 ? std::fmod(scroll, period) : scroll;
	if (at < 0) at += period;
	at += viewport * 0.3;
	int current = 0;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i] <= at) current = static_cast<int>(i);
	}
	return current;
}

void PaintCreditsScreen(Painter &p, int viewW, int viewH, float rollScale, const View &v, PaintOut &out)
{
	UseScreenFonts(p);
	const ScreenLayout l = ScreenLayoutFor(viewW, viewH);
	int right = PaintHeaderActions(p, l, {{v.paused ? "Resume" : "Pause", kActPause, false, true}, {"Close", kActClose, true, true}}, v.mouseX, v.mouseY);
	if (v.fast) {
		const int bw = 56, bh = 28;
		right -= 16 + bw;
		p.raw = true;
		PaintBadge(p, right, l.margin + (kHeaderButtonH - bh) / 2, bw, bh, "\xC3\x97" "2", pal::hairline, pal::redText);
		p.raw = false;
	}
	PaintScreenTitle(p, l, "Credits", {}, right, v.mouseX, v.mouseY);
	p.raw = true;
	PaintScreenInfo(p, l, Tr("Wheel or drag to scroll \xC2\xB7 Space pauses \xC2\xB7 Tab doubles the speed \xC2\xB7 Esc closes"));
	p.raw = false;
	const int cardX = l.margin, cardY = l.top, cardW = viewW - 2 * l.margin, cardH = viewH - l.margin - l.top;
	PaintGlassCard(p, cardX, cardY, cardW, cardH);

	// The roll keeps the size of its letters on every screen: it is laid out in its own units, of rollScale pixels, in the type the
	// shared set has at that scale.
	Painter q = p;
	q.s = rollScale;
	q.fonts[0] = BrandFont(BrandFace::Body, 15, q.s);
	q.fonts[1] = BrandFont(BrandFace::BodyBold, 15, q.s);
	q.fonts[2] = BrandFont(BrandFace::Display, 22, q.s);
	q.fonts[3] = BrandFont(BrandFace::Ui, 13, q.s);
	q.fonts[4] = BrandFont(BrandFace::UiStrong, 13, q.s);
	q.fonts[5] = BrandFont(BrandFace::Display, 30, q.s);
	const float k = p.s / q.s;
	auto roll = [k](int design) { return static_cast<int>(design * k); };
	RollFrame f;
	f.railX = roll(cardX) + 20;
	f.top = roll(cardY) + 12;
	f.bottom = roll(cardY + cardH) - 12;
	f.areaX = f.railX + kRailW + 30;
	f.areaW = roll(cardX + cardW) - 24 - f.areaX;
	const int columnW = std::min(kColumnMaxW, f.areaW - 24), columnX = f.areaX + (f.areaW - columnW) / 2;
	q.Fill(f.railX + kRailW + 14, f.top + 8, 1, f.bottom - f.top - 16, pal::hairline);
	out = PaintOut();
	out.viewport = f.bottom - f.top;
	RECT column = q.R(f.areaX, f.top, f.areaW, f.bottom - f.top);
	OffsetRect(&column, q.ox, q.oy);
	out.column = column;
	if (v.content) {
		const Layout &L = CachedLayout(q, *v.content, columnW);
		out.period = L.height + out.viewport * 0.5;
		double scroll = std::fmod(v.scroll, out.period);
		if (scroll < 0) scroll += out.period;
		for (int y : L.entryY) out.entries.push_back(std::max(0, y - kLead));
		PaintColumn(q, L, f, columnX, columnW, scroll, out.period);
		PaintRail(q, L, f, CurrentEntry(out.entries, scroll, out.period, out.viewport), v);
	}
}

}
