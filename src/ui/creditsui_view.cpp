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
#include "ui/ornaments.h"
#include "ui/patronui.h"
#include "ui/widgets.h"

using namespace ui;

namespace creditsui {
namespace {

constexpr int kMargin = 24, kRailX = 24, kRailW = 232, kBodyTop = 92, kBodyBottom = 696, kFooterTop = 704, kFooterY = 716, kFooterH = 38;
constexpr int kColumnX = kRailX + kRailW + 30, kColumnMaxW = 690, kFade = 60, kButtonW = 150;
// The space above the first line, and so where a line of the index lands when it is chosen.
constexpr int kLead = 72;
constexpr int kColumns = 3, kRailLines = 2;

const COLORREF kSubtitle = RGB(196, 176, 132), kPlain = RGB(208, 194, 158), kNoteColor = RGB(150, 136, 108);
const COLORREF kRailHot = RGB(38, 30, 20), kRailCurrent = RGB(60, 43, 22);

enum RowKind { kRowLine, kRowCells, kRowRule };

struct Row {
	RowKind kind = kRowLine;
	int y = 0, h = 0;
	int font = 0, spacing = 0;
	COLORREF color = kText;
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
			Line(up, 5, 4, pal::goldLight, 48);
		}
		else if (PixelWidth(p, up, 2, 2) <= Fits()) {
			Line(up, 2, 2, pal::goldLight, 36);
		}
		else {
			// A sentence: in its own letters, over a few lines.
			p.Font(2);
			if (p.WrapLines(text, 0, width).size() <= 3) Wrapped(text, 2, pal::goldLight, 30);
			else Wrapped(text, 1, pal::goldLight, 22);
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
		if (PixelWidth(p, up, 4, 2) <= Fits()) Line(up, 4, 2, kGold, 22);
		else Wrapped(Utf8(up), 4, kGold, 20);
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
				if (PixelWidth(p, w, 0, 0) <= Fits() - (patrons ? p.S(28) : 0)) Line(w, 0, 0, kText, 25, patrons ? tiers[i] : PatronTier::None);
				else Wrapped(names[i], 0, kText, 23);
			}
			return;
		}
		for (size_t i = 0; i < names.size(); i += kColumns) {
			Row r;
			r.kind = kRowCells;
			r.y = y;
			r.h = 24;
			r.font = 0;
			r.color = kText;
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
	b.Line(W(c.mod.author), 2, 3, pal::goldLight, 34);
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

// The few names too long for their column get a smaller letter before they are cut.
HFONT SmallFont(float s)
{
	static HFONT font = nullptr;
	static float scale = 0;
	if (font && scale == s) return font;
	if (font) DeleteObject(font);
	font = CreateFontW(-static_cast<int>(13 * s + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Georgia");
	scale = s;
	return font;
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
		if (sz.cx > rc.right - rc.left) SelectObject(p.dc, SmallFont(p.s));
		SetTextColor(p.dc, i < r.tiers.size() ? PatronColorsOf(r.tiers[i]).name : r.color);
		DrawTextW(p.dc, text.c_str(), static_cast<int>(text.size()), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	p.Font(r.font);
}

void DrawRule(Painter &p, int centerX, int top, int columnW)
{
	const int w = p.S(std::min(columnW, 460)), h = p.S(12);
	const RECT r = {centerX - w / 2, top, centerX - w / 2 + w, top + h};
	Blit(p, OrnamentBitmap(kOrnamentRule, w, h, false, kGold), w, h, r);
}

void PaintColumn(Painter &p, const Layout &L, int x, int w, double scroll, double period)
{
	const int top = p.S(kBodyTop), bottom = p.S(kBodyBottom);
	const int left = p.S(x), width = p.S(w), centerX = left + width / 2;
	p.Clip(x - 12, kBodyTop, w + 24, kBodyBottom - kBodyTop);
	for (int copy = 0; copy < 2; copy++) {
		// One whole offset for every row, so that they move together pixel by pixel.
		const int offset = static_cast<int>(std::floor((copy * period - scroll) * p.s + 0.5));
		for (const Row &r : L.rows) {
			const int rowTop = p.S(kBodyTop + r.y) + offset;
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

void PaintRail(Painter &p, const Layout &L, int current, const View &v)
{
	p.Font(0);
	struct Item {
		std::vector<std::wstring> lines;
		int h;
	};
	std::vector<Item> items;
	int total = 0;
	const int textX = kRailX + 30, textW = kRailW - 38;
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
	const int railTop = kBodyTop + 6, railH = kBodyBottom - railTop;
	int y = railTop;
	// Too many lines for the rail: it follows the current one.
	if (total > railH && current >= 0) {
		int before = 0;
		for (int i = 0; i < current; i++) before += items[i].h + 4;
		y -= std::max(0, std::min(before - railH / 3, total - railH));
	}
	p.Clip(kRailX - 4, railTop, kRailW + 8, railH);
	for (size_t i = 0; i < items.size(); i++) {
		const Item &it = items[i];
		const bool now = static_cast<int>(i) == current;
		const bool hot = !now && p.Hover(kRailX, y, kRailW, it.h, v.mouseX, v.mouseY);
		if (now) {
			p.FillH(kRailX, y, kRailW, it.h, kRailCurrent, pal::panel);
			p.Fill(kRailX + 6, y + 7 + 9, 16, 2, kGold);
		}
		else if (hot) {
			p.FillH(kRailX, y, kRailW, it.h, kRailHot, pal::panel);
		}
		p.Font(now ? 1 : 0);
		for (size_t l = 0; l < it.lines.size(); l++) {
			p.LineW(textX, y + 7 + static_cast<int>(l) * 19, textW, 19, it.lines[l], now ? pal::goldLight : hot ? kText : kDim);
		}
		const int hitTop = std::max(y, railTop), hitBottom = std::min(y + it.h, railTop + railH);
		if (hitBottom > hitTop) p.AddHit(kRailX, hitTop, kRailW, hitBottom - hitTop, kActJump, static_cast<int>(i));
		y += it.h + 4;
	}
	p.Unclip();
}

// The frame of the other screens with its glow kept above the column, whose edges fade into the plain colour below it.
void PaintFrame(Painter &p, int w, int h)
{
	constexpr int glow = kBodyTop - 12;
	p.FillV(0, 0, w, glow, pal::panelTop, pal::panel);
	p.Fill(0, glow, w, h - glow, pal::panel);
	p.Frame(0, 0, w, h, kGold);
	p.Frame(4, 4, w - 8, h - 8, pal::lineDim);
	PaintFrameCorners(p, 0, 0, w, h);
}

void PaintFooter(Painter &p, int panelW, const View &v)
{
	p.Fill(18, kFooterTop, panelW - 36, 1, pal::line);
	int bx = panelW - kMargin - kButtonW;
	p.Button(bx, kFooterY, kButtonW, kFooterH, "Close", kActClose, 0, v.mouseX, v.mouseY);
	bx -= 10 + kButtonW;
	p.Button(bx, kFooterY, kButtonW, kFooterH, v.paused ? "Resume" : "Pause", kActPause, 0, v.mouseX, v.mouseY);
	int right = bx - 16;
	if (v.fast) {
		const int bw = 40;
		p.raw = true;
		PaintBadge(p, bx - 12 - bw, kFooterY + 10, bw, 18, "\xC3\x97" "2", RGB(70, 46, 18), RGB(236, 186, 110));
		p.raw = false;
		right = bx - 12 - bw - 14;
	}
	p.Diamond(kMargin + 4, kFooterY + kFooterH / 2, 4, kGold);
	p.Font(3);
	const std::string hint = Tr("Wheel or drag to scroll \xC2\xB7 Space pauses \xC2\xB7 Tab doubles the speed \xC2\xB7 Esc closes");
	const int x = kMargin + 16, w = right - x;
	p.raw = true;
	if (p.TextWidth(hint) <= w) {
		p.Text(x, kFooterY, w, kFooterH, hint, RGB(186, 170, 134));
	}
	else {
		const int h = std::min(kFooterH, p.Measure(w, hint));
		p.TextWrapped(x, kFooterY + (kFooterH - h) / 2, w, h, hint, RGB(186, 170, 134));
	}
	p.raw = false;
}

}

float ScreenScale(int frameW, int frameH)
{
	(void)frameW;
	return std::max(0.6f, std::min(1.6f, frameH / static_cast<float>(kScreenH)));
}

int PanelWidth(int frameW, int frameH)
{
	const int screenW = static_cast<int>(frameW / ScreenScale(frameW, frameH));
	return std::min(screenW, std::min(1180, std::max(900, screenW - 160)));
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

void PaintCreditsScreen(Painter &p, int panelW, const View &v, PaintOut &out)
{
	PaintFrame(p, panelW, kScreenH);
	PaintWindowHeader(p, panelW, Utf8(Upper(W(Tr("Credits")))));
	const int areaX = kColumnX, areaW = panelW - kMargin - areaX;
	const int columnW = std::min(kColumnMaxW, areaW - 24), columnX = areaX + (areaW - columnW) / 2;
	p.Fill(kRailX + kRailW + 14, kBodyTop + 8, 1, kBodyBottom - kBodyTop - 16, pal::lineDim);
	out = PaintOut();
	out.viewport = kBodyBottom - kBodyTop;
	RECT column = p.R(areaX, kBodyTop, areaW, kBodyBottom - kBodyTop);
	OffsetRect(&column, p.ox, p.oy);
	out.column = column;
	if (v.content) {
		const Layout &L = CachedLayout(p, *v.content, columnW);
		out.period = L.height + out.viewport * 0.5;
		double scroll = std::fmod(v.scroll, out.period);
		if (scroll < 0) scroll += out.period;
		for (int y : L.entryY) out.entries.push_back(std::max(0, y - kLead));
		PaintColumn(p, L, columnX, columnW, scroll, out.period);
		PaintRail(p, L, CurrentEntry(out.entries, scroll, out.period, out.viewport), v);
	}
	PaintFooter(p, panelW, v);
}

}
