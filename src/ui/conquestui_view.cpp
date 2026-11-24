// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/conquestui_view.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "core/log.h"
#include "i18n/tr.h"
#include "ui/conquestshield.h"
#include "ui/fullscreen.h"
#include "ui/medal.h"
#include "ui/ornaments.h"
#include "ui/widgets.h"

using namespace ui;

namespace conquestui {
namespace {

// CONQUESTVICTORY.INI: a 390x180 box centred on the screen, OK at (135, 115) and 121x30. Ours is larger and keeps that button where
// the game has it, so the click goes to the game's.
const int kVictoryW = 440, kVictoryH = 200, kNativeVictoryW = 390, kNativeVictoryH = 180, kOkX = 135, kOkY = 115, kOkW = 121, kOkH = 30;
const int kLineH = 19;

const int kCardPad = 20, kLabelY = 16, kDescLineH = 23, kBonusLineH = 20, kSegmentH = 34;
const int kShieldW = 74, kShieldH = 79;
const int kPathLabelW = 230, kSlotMin = 70, kSlotMax = 110, kConnectorMin = 26, kConnectorMax = 60, kStepShieldW = 84, kMaxSlots = 8;
const int kSwatch = 16, kLegendRow = 30, kMedalSmall = 18, kMedalCard = 22;
const int kQuestionW = 546, kQuestionH = 246;

// The game's map pictures have a gold border of their own, left out of ours: 10 to 14 pixels a side in global.bmp, thicker at the bottom.
const int kBorderLeft = 12, kBorderTop = 15, kBorderRight = 12, kBorderBottom = 16;
// And the IMPERIVM RTC plate at the bottom left of the 1024x768 ones, over desert: it is covered with the desert above it, mirrored, when
// no territory reaches there (a mod's map may have land in that corner). The sides fade into the picture over kPlateFeather pixels; the
// bottom is the end of what is shown.
const int kPlateX0 = 26, kPlateX1 = 526, kPlateY0 = 662, kPlateY1 = 768 - kBorderBottom, kPlateFeather = 16;

const COLORREF kFrameGoldDim = RGB(127, 98, 48), kProgressGold = RGB(214, 170, 84), kConnectorDone = RGB(170, 135, 70);
const COLORREF kConquerText = pal::redText;

// A shield in the units of its painter, drawn into the canvas after the GDI work.
struct Pending {
	std::shared_ptr<const ConquestImage> img;
	int x, y, w, h, alpha;
};

struct Layer {
	Painter &p;
	std::vector<Pending> images;
	// What goes over the shields, drawn once they are in: their numbers and medals.
	std::vector<std::function<void(Painter &)>> after;
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

void RawCaps(Painter &p, int x, int y, int w, int h, const std::string &s, COLORREF c)
{
	std::wstring ws = W(s);
	if (!ws.empty()) CharUpperBuffW(&ws[0], static_cast<DWORD>(ws.size()));
	RECT r = p.R(x, y, w, h);
	SetTextColor(p.dc, c);
	DrawTextW(p.dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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

// The lines of a text as last wrapped: the same text, width and font are not measured again each frame.
struct Wrapped {
	std::string text;
	int width = -1;
	float s = 0;
	HFONT font = nullptr;
	std::vector<std::wstring> lines;
};

const std::vector<std::wstring> &WrapOnce(Wrapped &cache, Painter &p, int font, const std::string &text, int width)
{
	if (cache.width != width || cache.s != p.s || cache.font != p.fonts[font] || cache.text != text) {
		cache.text = text;
		cache.width = width;
		cache.s = p.s;
		cache.font = p.fonts[font];
		cache.lines = Wrap(p, text, width);
	}
	return cache.lines;
}

Wrapped g_descLines, g_bonusLines;

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
	ShieldShape(l.p, x + 2, y + 1, w - 4, h - 2, alpha == 255 ? c : RGB(GetRValue(c) / 2, GetGValue(c) / 2, GetBValue(c) / 2), pal::redText, true);
}

// `d` in canvas pixels.
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

void FlushImages(Canvas &c, Layer &l)
{
	GdiFlush();
	Painter &p = l.p;
	for (const Pending &d : l.images) {
		const int x = p.S(d.x), y = p.S(d.y);
		DrawImage(c, {d.img, x, y, p.S(d.x + d.w) - x, p.S(d.y + d.h) - y, d.alpha});
	}
	for (const auto &draw : l.after) draw(p);
}

// The game's name of a difficulty (1 easy to 3 hard), as its list gives it.
std::string DifficultyName(const ConquestSnapshot &g, int level)
{
	static const char *const kNames[3] = {"Easy", "Normal", "Hard"};
	if (level >= 1 && level <= static_cast<int>(g.difficulties.size())) return g.difficulties[level - 1];
	return level >= 1 && level <= 3 ? Tr(kNames[level - 1]) : std::string();
}

// One region of the screen: the game's pixels are copied in first, so whatever is not painted keeps showing the game.
template <typename F>
void Render(Canvas &c, uint32_t *buf, int w, int h, const RECT &r, int pressAction, int pressArg, std::vector<Hit> &hits, F paint)
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
	EnsureFonts(1.0f);
	Painter p{c.dc, 1.0f, r.left, r.top, &hits};
	std::copy(std::begin(g_fonts), std::end(g_fonts), std::begin(p.fonts));
	p.pressAction = pressAction;
	p.pressArg = pressArg;
	Layer layer{p, {}};
	paint(layer);
	FlushImages(c, layer);
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
		if (t.standing == kStandingOwned) return {"You can start here", "Press Select to begin your conquest here.", pal::text};
		return {"You cannot start here", "Choose one of the territories marked as yours.", pal::textDim};
	}
	if (t.standing == kStandingOwned) return {"Your territory", "It is already part of your empire.", pal::text};
	if (t.standing == kStandingConquerable) return {"You can conquer it", "It borders your empire.", kConquerText};
	return {"Out of reach", "Conquer a neighbouring territory first.", pal::textDim};
}

RECT CropOf(const ConquestImage &m)
{
	if (m.w < 400 || m.h < 300) return {0, 0, m.w, m.h};
	return {kBorderLeft, kBorderTop, m.w - kBorderRight, m.h - kBorderBottom};
}

bool CoversPlate(const ConquestImage &m, const ConquestRegions *r)
{
	if (m.w != 1024 || m.h != 768 || !r || r->w != m.w || r->h != m.h) return false;
	for (int y = 2 * kPlateY0 - kPlateY1; y < kPlateY1; y++) {
		for (int x = kPlateX0; x < kPlateX1; x++) {
			if (r->territory[static_cast<size_t>(y) * r->w + x] != kConquestNoTerritory) return false;
		}
	}
	return true;
}

uint32_t Mix(uint32_t a, uint32_t b, int t)
{
	const auto one = [t](uint32_t x, uint32_t y) { return (x * (255 - t) + y * t) / 255; };
	return one((a >> 16) & 255, (b >> 16) & 255) << 16 | one((a >> 8) & 255, (b >> 8) & 255) << 8 | one(a & 255, b & 255);
}

void CoverPlate(std::vector<uint32_t> &px, int w)
{
	for (int y = kPlateY0; y < kPlateY1; y++) {
		const int from = 2 * kPlateY0 - 1 - y;
		for (int x = kPlateX0; x < kPlateX1; x++) {
			const int edge = std::min(x - kPlateX0, kPlateX1 - 1 - x);
			const int t = edge >= kPlateFeather ? 255 : edge * 255 / kPlateFeather;
			uint32_t &dst = px[static_cast<size_t>(y) * w + x];
			dst = Mix(dst, px[static_cast<size_t>(from) * w + x], t);
		}
	}
}

// The picture as the screen shows it, without its border and plate.
std::vector<uint32_t> Compose(const ConquestSnapshot &g, const RECT &crop)
{
	const ConquestImage &m = *g.map;
	std::vector<uint32_t> px(m.argb);
	if (CoversPlate(m, g.regions.get())) CoverPlate(px, m.w);
	const int cw = crop.right - crop.left, ch = crop.bottom - crop.top;
	std::vector<uint32_t> out(static_cast<size_t>(cw) * ch);
	for (int y = 0; y < ch; y++) {
		const uint32_t *from = px.data() + static_cast<size_t>(y + crop.top) * m.w + crop.left;
		std::copy(from, from + cw, out.begin() + static_cast<size_t>(y) * cw);
	}
	return out;
}

// Scaled once to its size on the screen with GDI's halftone, so each frame only copies it.
bool Scale(const std::vector<uint32_t> &src, int sw, int sh, int dw, int dh, std::vector<uint32_t> &out)
{
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = dw;
	bi.bmiHeader.biHeight = -dh;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HDC dc = CreateCompatibleDC(nullptr);
	HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
	bool ok = false;
	if (bmp && bits) {
		HGDIOBJ old = SelectObject(dc, bmp);
		SetStretchBltMode(dc, HALFTONE);
		SetBrushOrgEx(dc, 0, 0, nullptr);
		BITMAPINFO si = bi;
		si.bmiHeader.biWidth = sw;
		si.bmiHeader.biHeight = -sh;
		ok = StretchDIBits(dc, 0, 0, dw, dh, 0, 0, sw, sh, src.data(), &si, DIB_RGB_COLORS, SRCCOPY) != 0;
		GdiFlush();
		if (ok) out.assign(static_cast<uint32_t *>(bits), static_cast<uint32_t *>(bits) + static_cast<size_t>(dw) * dh);
		SelectObject(dc, old);
	}
	if (bmp) DeleteObject(bmp);
	if (dc) DeleteDC(dc);
	return ok;
}

// The map at its size on the screen, built again only when the map or that size changes: its pixels, the territory under each one and
// where each territory lies, so that the chosen one and the one under the mouse are drawn over it each frame without building it again.
struct Picture {
	std::shared_ptr<const ConquestImage> map;
	std::shared_ptr<const ConquestRegions> regions;
	int w = 0, h = 0;
	std::vector<uint32_t> px;
	std::vector<uint8_t> owner;
	std::vector<RECT> bounds;
};
Picture g_picture;

bool BuildPicture(const ConquestSnapshot &g, const RECT &crop, int dw, int dh)
{
	Picture &pic = g_picture;
	if (pic.map == g.map && pic.regions == g.regions && pic.w == dw && pic.h == dh) return !pic.px.empty();
	const DWORD started = GetTickCount();
	pic = Picture();
	pic.map = g.map;
	pic.regions = g.regions;
	pic.w = dw;
	pic.h = dh;
	const int cw = crop.right - crop.left, ch = crop.bottom - crop.top;
	if (dw <= 0 || dh <= 0 || cw <= 0 || ch <= 0 || !Scale(Compose(g, crop), cw, ch, dw, dh, pic.px)) {
		pic.px.clear();
		return false;
	}
	if (const ConquestRegions *r = g.regions.get()) {
		pic.owner.resize(static_cast<size_t>(dw) * dh);
		pic.bounds.assign(256, RECT{dw, dh, -1, -1});
		for (int y = 0; y < dh; y++) {
			const int sy = crop.top + y * ch / dh;
			const uint8_t *row = r->territory.data() + static_cast<size_t>(sy) * r->w + crop.left;
			uint8_t *out = pic.owner.data() + static_cast<size_t>(y) * dw;
			for (int x = 0; x < dw; x++) {
				const uint8_t t = row[x * cw / dw];
				out[x] = t;
				if (t == kConquestNoTerritory) continue;
				RECT &b = pic.bounds[t];
				b.left = std::min<LONG>(b.left, x);
				b.top = std::min<LONG>(b.top, y);
				b.right = std::max<LONG>(b.right, x + 1);
				b.bottom = std::max<LONG>(b.bottom, y + 1);
			}
		}
	}
	LogD("ui", "conquest: the map was built at %dx%d in %u ms", dw, dh, static_cast<unsigned>(GetTickCount() - started));
	return true;
}

// Over the map already in the canvas at (ox, oy): the territory lit by `light` (0-255 towards white) and its edge, two pixels across
// the border as the game outlines it, at `edge`. Only the territory's own box is visited.
void MarkTerritory(Canvas &c, int ox, int oy, int territory, int light, int edge)
{
	const Picture &pic = g_picture;
	if (pic.owner.empty() || territory < 0 || territory >= static_cast<int>(pic.bounds.size())) return;
	const RECT &b = pic.bounds[territory];
	if (b.right <= b.left) return;
	const uint8_t t = static_cast<uint8_t>(territory);
	const int x0 = std::max<int>(0, b.left - 1), x1 = std::min<int>(pic.w, b.right + 1), y0 = std::max<int>(0, b.top - 1), y1 = std::min<int>(pic.h, b.bottom + 1);
	for (int y = y0; y < y1; y++) {
		const int cy = oy + y;
		if (cy < 0 || cy >= c.h) continue;
		const uint8_t *row = pic.owner.data() + static_cast<size_t>(y) * pic.w;
		for (int x = x0; x < x1; x++) {
			const int cx = ox + x;
			if (cx < 0 || cx >= c.w) continue;
			const bool in = row[x] == t;
			const bool border = (x > 0 && (row[x - 1] == t) != in) || (x + 1 < pic.w && (row[x + 1] == t) != in) ||
				(y > 0 && (row[x - pic.w] == t) != in) || (y + 1 < pic.h && (row[x + pic.w] == t) != in);
			if (!in && !border) continue;
			uint32_t &dst = c.bits[static_cast<size_t>(cy) * c.w + cx];
			if (border && edge > 0) dst = Mix(dst, 0xffffffu, edge);
			else if (in && light > 0) dst = Mix(dst, 0xffffffu, light);
		}
	}
}

void PaintMap(Layer &l, Canvas &canvas, const Layout &L, const View &v, const RECT &crop, int hovered)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	PaintGlassCard(p, L.mapCard.x, L.mapCard.y, L.mapCard.w, L.mapCard.h);
	const Box &b = L.map;
	const RECT r = p.R(b.x, b.y, b.w, b.h);
	const int dw = r.right - r.left, dh = r.bottom - r.top;
	if (!g.map || !BuildPicture(g, crop, dw, dh)) {
		p.Fill(b.x, b.y, b.w, b.h, pal::bg);
		PaintIsotype(p, b.x + b.w / 2, b.y + b.h / 2, b.h * 35 / 100, 46);
		return;
	}
	// Straight into the canvas, which GDI is done with up to here.
	GdiFlush();
	const Picture &pic = g_picture;
	const int x0 = std::max<int>(0, -r.left), x1 = std::min<int>(dw, canvas.w - r.left);
	for (int y = 0; y < dh && x1 > x0; y++) {
		const int cy = r.top + y;
		if (cy < 0 || cy >= canvas.h) continue;
		const uint32_t *from = pic.px.data() + static_cast<size_t>(y) * dw;
		std::copy(from + x0, from + x1, canvas.bits + static_cast<size_t>(cy) * canvas.w + r.left + x0);
	}
	if (hovered >= 0 && hovered != g.selected) MarkTerritory(canvas, r.left, r.top, hovered, 36, 150);
	if (Valid(g, g.selected)) MarkTerritory(canvas, r.left, r.top, g.selected, hovered == g.selected ? 36 : 0, 255);
	p.Frame(b.x - 1, b.y - 1, b.w + 2, b.h + 2, kFrameGoldDim);
}

// The name of the territory under the mouse, beside it and inside the map.
void PaintMapTag(Painter &p, const Layout &L, const View &v, int hovered)
{
	const ConquestSnapshot &g = v.game;
	const ConquestTerritory &t = g.territories[hovered];
	const Standing s = Describe(g, t);
	p.Font(1);
	std::wstring name = W(t.name);
	if (!name.empty()) CharUpperBuffW(&name[0], static_cast<DWORD>(name.size()));
	SIZE nameSz = {};
	GetTextExtentPoint32W(p.dc, name.c_str(), static_cast<int>(name.size()), &nameSz);
	p.Font(3);
	const int lineW = p.TextWidth(s.line);
	const int medal = t.laurel > 0 ? kMedalSmall + 10 : 0;
	const int w = std::min(L.map.w - 16, std::max(static_cast<int>(nameSz.cx / p.s) + 2 + medal, lineW + 18) + 28), h = 52;
	const int mx = static_cast<int>((v.mouseX - p.ox) / p.s), my = static_cast<int>((v.mouseY - p.oy) / p.s);
	int x = mx + 18, y = my + 20;
	if (x + w > L.map.Right() - 8) x = mx - 14 - w;
	if (y + h > L.map.Bottom() - 8) y = my - 14 - h;
	x = std::max(L.map.x + 8, x);
	y = std::max(L.map.y + 8, y);
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, pal::edge);
	p.Font(1);
	RawCaps(p, x + 14, y + 6, w - 28 - medal, 22, t.name, pal::text);
	if (medal) PaintMedal(p, x + w - 14 - kMedalSmall, y + 8, kMedalSmall, t.laurel);
	p.Diamond(x + 18, y + 37, 4, ToColorRef(g.colors[t.standing]));
	p.Font(3);
	p.Text(x + 28, y + 27, w - 42, 20, s.line, s.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// The steps' empty shields, made once for each size and look.
std::shared_ptr<const ConquestImage> ShieldArt(int w, int h, int style)
{
	static std::map<std::tuple<int, int, int>, std::shared_ptr<const ConquestImage>> cache;
	const auto key = std::make_tuple(w, h, style);
	const auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	if (cache.size() > 32) cache.clear();
	auto img = std::make_shared<ConquestImage>();
	img->w = w;
	img->h = h;
	img->argb = ConquestShieldArt(w, h, style);
	return cache[key] = img;
}

// A line between two steps with a chevron in its middle, smoothed, in the colour of how far the path has come.
bool InUnitChevron(double u, double v) { return std::fabs(u - (0.75 - std::fabs(v - 0.5) * 1.3)) < 0.22; }

void PaintConnector(Painter &p, int x0, int x1, int cy, COLORREF c)
{
	if (x1 - x0 < 12) return;
	const int mid = (x0 + x1) / 2;
	p.Fill(x0, cy - 1, mid - 6 - x0, 2, c);
	p.Fill(mid + 6, cy - 1, x1 - mid - 6, 2, c);
	p.Shape(mid - 5, cy - 7, 10, 14, InUnitChevron, c);
}

void PaintPath(Layer &l, const Box &b, const View &v)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	// The label, how far the path has come and, once the conquest is won whole, its medal.
	const int medal = g.laurel > 0 ? kMedalCard + 8 : 0;
	const int labelY = b.y + (b.h - 78) / 2;
	PaintSectionLabel(p, b.x + 24, labelY, kPathLabelW - 32 - medal, 18, "Your path");
	if (medal) PaintMedal(p, b.x + kPathLabelW - 16 - kMedalCard, labelY - 2, kMedalCard, g.laurel);
	int total = 0;
	for (const ConquestTerritory &t : g.territories) total += t.disabled ? 0 : 1;
	const int done = static_cast<int>(g.path.size());
	total = std::max(total, done);
	p.Font(3);
	Raw(p, b.x + 24, labelY + 24, kPathLabelW - 32, 22, TrFmt("%s of %s conquered", {std::to_string(done), std::to_string(total)}), pal::textDim);
	const int barW = kPathLabelW - 56;
	p.Fill(b.x + 24, labelY + 62, barW, 3, pal::hairline);
	if (total > 0 && done > 0) p.Fill(b.x + 24, labelY + 62, barW * std::min(done, total) / total, 3, kProgressGold);
	p.Fill(b.x + kPathLabelW, b.y + 20, 1, b.h - 40, pal::hairline);
	// The territory chosen on the map, when it is the one the path can take next.
	int candidate = -1;
	if (Valid(g, g.selected) && std::find(g.path.begin(), g.path.end(), g.selected) == g.path.end()) {
		const int standing = g.territories[g.selected].standing;
		if (g.choosing ? standing == kStandingOwned && g.path.empty() : standing == kStandingConquerable) candidate = g.selected;
	}
	const int steps = done + (candidate >= 0 ? 1 : 0);
	// The steps share the strip: as wide as they can be up to kSlotMax, with room for the connectors; a path longer than the strip shows
	// its newest steps.
	const int roomX = b.x + kPathLabelW + 20, roomW = b.Right() - 20 - roomX;
	const int fits = std::max(1, (roomW + kConnectorMin) / (kSlotMin + kConnectorMin));
	const int slots = std::max(1, std::min({kMaxSlots, fits, std::max(total, steps)}));
	const int slotW = std::max(kSlotMin, std::min(kSlotMax, (roomW - (slots - 1) * kConnectorMin) / slots));
	const int gap = slots > 1 ? std::min(kConnectorMax, (roomW - slots * slotW) / (slots - 1)) : 0;
	const int sw = std::min(kStepShieldW, slotW - 12), sh = sw * 112 / 100;
	const int span = slots * slotW + (slots - 1) * gap, x0 = roomX + (roomW - span) / 2, first = std::max(0, steps - slots);
	const int top = b.y + (b.h - (sh + 34)) / 2, cy = top + sh * 45 / 100, nameY = top + sh + 6;
	const int artW = p.S(sw), artH = p.S(sh);
	for (int slot = 0; slot < slots; slot++) {
		const int i = first + slot;
		const int sx = x0 + slot * (slotW + gap), shx = sx + (slotW - sw) / 2;
		if (slot > 0) {
			const int prevRight = sx - gap - (slotW - sw) / 2;
			PaintConnector(p, prevRight + 8, shx - 8, cy, i < done ? kConnectorDone : i == done ? pal::caption : pal::lineStrong);
		}
		const int index = i < done && i < static_cast<int>(g.path.size()) ? g.path[i] : i == done ? candidate : -1;
		const bool current = i == done;
		// The step the path takes now: a short accent bar under its name.
		if (current) p.Fill(sx + slotW / 2 - 14, nameY + 26, 28, 2, pal::red);
		if (index < 0) {
			l.images.push_back({ShieldArt(artW, artH, current ? kShieldNext : kShieldAhead), shx, top, sw, sh, 255});
			const std::string number = std::to_string(i + 1);
			const COLORREF ink = current ? pal::text : pal::caption;
			l.after.push_back([=](Painter &q) {
				q.Font(2);
				q.raw = true;
				q.Text(shx, top + 2, sw, sh * 80 / 100, number, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				q.raw = false;
			});
			continue;
		}
		// A territory's emblem is drawn on its own, with the shape the game gives it.
		Shield(l, g, index, shx, top, sw, sh, 255);
		const ConquestTerritory &t = g.territories[index];
		if (i < done && t.laurel > 0) {
			const int level = t.laurel;
			l.after.push_back([=](Painter &q) { PaintMedal(q, shx + sw - 16, top + sh - 20, kMedalCard, level); });
		}
		p.Font(3);
		Raw(p, sx - 6, nameY, slotW + 12, 22, t.name, current ? pal::text : pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

const char *const kLegendLabels[kStandingCount] = {"Your territory", "You can conquer it", "Out of reach"};

void PaintSwatch(Painter &p, int x, int y, const View &v, int standing)
{
	p.Fill(x, y, kSwatch, kSwatch, ToColorRef(v.game.colors[standing]));
	p.Frame(x, y, kSwatch, kSwatch, pal::lineStrong);
}

void PaintLegend(Painter &p, const Layout &L, const View &v)
{
	const Box &b = L.legend;
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	p.Font(3);
	if (!L.legendBeside) {
		const int cell = (b.w - 2 * kCardPad) / kStandingCount;
		for (int i = 0; i < kStandingCount; i++) {
			const int x = b.x + kCardPad + i * cell;
			PaintSwatch(p, x, b.y + (b.h - kSwatch) / 2, v, i);
			p.Text(x + kSwatch + 10, b.y, cell - kSwatch - 16, b.h, kLegendLabels[i], pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		return;
	}
	PaintSectionLabel(p, b.x + kCardPad, b.y + kLabelY, b.w - 2 * kCardPad, 18, "Legend");
	p.Font(3);
	for (int i = 0; i < kStandingCount; i++) {
		const int y = b.y + 48 + i * kLegendRow;
		PaintSwatch(p, b.x + kCardPad, y + 3, v, i);
		p.Text(b.x + kCardPad + kSwatch + 12, y, b.w - 2 * kCardPad - kSwatch - 12, 22, kLegendLabels[i], pal::text,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintHint(Painter &p, const Box &b, const View &v)
{
	if (b.h < 80 || b.w <= 2 * kCardPad) return;
	const char *text = v.game.choosing ? "Click one of your territories on the map and press Select, or double-click it."
									   : "Click a territory that borders yours and press Attack, or double-click it.";
	p.Font(3);
	const int textW = b.w - 2 * kCardPad, h = std::min(b.h, 46 + p.Measure(textW, text) + kCardPad);
	PaintGlassCard(p, b.x, b.y, b.w, h);
	PaintSectionLabel(p, b.x + kCardPad, b.y + kLabelY, textW, 18, "Next step");
	p.Font(3);
	p.TextWrapped(b.x + kCardPad, b.y + 46, textW, h - 46 - kCardPad / 2, text, pal::textDim);
}

void PaintTerritoryCard(Layer &l, const Box &b, const View &v)
{
	Painter &p = l.p;
	const ConquestSnapshot &g = v.game;
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	// A territory won shows its medal and the difficulty at the right of the label.
	const int laurel = Valid(g, g.selected) ? g.territories[g.selected].laurel : 0;
	int labelW = b.w - 2 * kCardPad;
	if (laurel > 0) {
		const int mx = b.Right() - kCardPad - kMedalCard;
		PaintMedal(p, mx, b.y + kLabelY - 2, kMedalCard, laurel);
		p.Font(4);
		const std::string won = TrFmt("Won on %s", {DifficultyName(g, laurel)});
		p.raw = true;
		const int wonW = std::min(labelW / 2, p.CapsWidth(won, 1) + 4);
		p.TextCaps(mx - 8 - wonW, b.y + kLabelY, wonW, 18, won, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
		p.raw = false;
		labelW -= wonW + kMedalCard + 16;
	}
	PaintSectionLabel(p, b.x + kCardPad, b.y + kLabelY, labelW, 18, g.choosing ? "Select starting area" : "Territory");
	const int sx = b.x + kCardPad, sy = b.y + 48, tx = sx + kShieldW + 20, tw = b.Right() - kCardPad - tx;
	if (!Valid(g, g.selected)) {
		l.images.push_back({ShieldArt(p.S(sx + kShieldW) - p.S(sx), p.S(sy + kShieldH) - p.S(sy), kShieldAhead), sx, sy, kShieldW, kShieldH, 255});
		p.Font(2);
		p.Text(tx, sy - 4, tw, 36, "Choose a territory", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.TextWrapped(tx, sy + 38, tw, 52, g.choosing ? "Click a territory on the map to see where you can begin." : "Click a territory on the map to see it here.",
			pal::textDim);
		return;
	}
	const ConquestTerritory &t = g.territories[g.selected];
	Shield(l, g, g.selected, sx, sy, kShieldW, kShieldH, 255);
	p.Font(2);
	RawCaps(p, tx, sy - 4, tw, 36, t.name, pal::text);
	const Standing s = Describe(g, t);
	p.Diamond(tx + 6, sy + 45, 5, pal::panel);
	p.Diamond(tx + 6, sy + 45, 4, ToColorRef(g.colors[t.standing]));
	p.Font(4);
	p.Text(tx + 18, sy + 35, tw - 18, 20, s.line, s.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.TextWrapped(tx, sy + 58, tw, b.Bottom() - 4 - (sy + 58), s.detail, pal::textDim);
}

void PaintDescription(Painter &p, const Box &b, const View &v, Painted &out)
{
	const ConquestSnapshot &g = v.game;
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	PaintSectionLabel(p, b.x + kCardPad, b.y + kLabelY, b.w - 2 * kCardPad, 18, "Description");
	const int x = b.x + kCardPad, w = b.w - 2 * kCardPad, top = b.y + 46, bottom = b.Bottom() - 14;
	p.Font(0);
	const std::string text = Valid(g, g.selected) ? g.territories[g.selected].description : std::string();
	if (text.empty()) {
		out.maxScroll = 0;
		if (Valid(g, g.selected)) p.Text(x, top, w, kDescLineH, "No description.", pal::textDim);
		return;
	}
	const std::vector<std::wstring> &lines = WrapOnce(g_descLines, p, 0, text, w - 14);
	const int visible = std::max(1, (bottom - top) / kDescLineH);
	out.maxScroll = std::max(0, static_cast<int>(lines.size()) - visible);
	const int first = std::min(std::max(0, v.scroll), out.maxScroll);
	p.Clip(x, top, w, bottom - top);
	for (int i = 0; i < visible && first + i < static_cast<int>(lines.size()); i++) p.LineW(x, top + i * kDescLineH, w - 14, kDescLineH, lines[first + i], pal::body);
	p.Unclip();
	if (out.maxScroll > 0) {
		ScrollbarLook look;
		look.width = 4;
		look.minThumb = 18;
		look.grip = 6;
		look.track = pal::bg;
		PaintScrollbar(p, kBarDescription, x + w - 5, top, bottom - top, {first, out.maxScroll, visible}, v.bar, v.mouseX, v.mouseY, look);
	}
}

void PaintBonus(Painter &p, const Box &b, const View &v)
{
	const ConquestSnapshot &g = v.game;
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	const bool owned = Valid(g, g.selected) && g.territories[g.selected].standing == kStandingOwned;
	PaintSectionLabel(p, b.x + kCardPad, b.y + kLabelY, b.w - 2 * kCardPad, 18, owned ? "Bonus of this territory" : "Bonus when conquered");
	const int x = b.x + kCardPad, w = b.w - 2 * kCardPad, top = b.y + 44;
	const std::string bonus = Valid(g, g.selected) ? g.territories[g.selected].bonus : std::string();
	p.Font(3);
	if (bonus.empty()) {
		p.Text(x, top, w, kBonusLineH, Valid(g, g.selected) ? "No bonus" : "Choose a territory to see its bonus.", pal::textDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	const std::vector<std::wstring> &lines = WrapOnce(g_bonusLines, p, 3, bonus, w);
	const int maxLines = std::max(1, (b.Bottom() - 10 - top) / kBonusLineH);
	for (int i = 0; i < maxLines && i < static_cast<int>(lines.size()); i++) {
		std::wstring line = lines[i];
		if (i == maxLines - 1 && static_cast<int>(lines.size()) > maxLines) line += L"\x2026";
		p.LineW(x, top + i * kBonusLineH, w, kBonusLineH, line, pal::text);
	}
}

void PaintDifficulty(Painter &p, const Box &b, const View &v)
{
	const ConquestSnapshot &g = v.game;
	p.Font(4);
	p.TextCaps(b.x, b.y, b.w, 18, "Difficulty", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	const int count = static_cast<int>(g.difficulties.size());
	if (!count) return;
	const int y = b.y + 22;
	for (int i = 0; i < count; i++) {
		const int sx = b.x + b.w * i / count, sw = b.x + b.w * (i + 1) / count - sx;
		const bool on = i == g.difficulty;
		const bool hot = !on && p.Hover(sx, y, sw, kSegmentH, v.mouseX, v.mouseY);
		p.Fill(sx, y, sw, kSegmentH, on ? pal::text : hot ? pal::button : pal::field);
		p.Frame(sx, y, sw, kSegmentH, on ? pal::text : hot ? pal::edgeHot : pal::edge);
		p.Font(on ? 1 : 0);
		Raw(p, sx + 2, y, sw - 4, kSegmentH, g.difficulties[i], on ? pal::onLight : pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!on) p.AddHit(sx, y, sw, kSegmentH, kActDifficulty, i);
	}
}

void PaintButtons(Painter &p, const Layout &L, const View &v)
{
	const ConquestSnapshot &g = v.game;
	const bool choose = g.choosing || (g.select.visible && !g.start.visible);
	const ConquestButton main = choose ? g.select : g.start;
	p.Button(L.main.x, L.main.y, L.main.w, L.main.h, choose ? "Select" : "Attack", kActNative, choose ? kConquestSelect : kConquestStart, v.mouseX, v.mouseY,
		main.enabled, true);
	const Box &b = L.second;
	if (g.save.visible) {
		const int half = (b.w - 12) / 2;
		p.Button(b.x, b.y, half, b.h, "Save", kActNative, kConquestSave, v.mouseX, v.mouseY, g.save.enabled);
		PaintGhostButton(p, b.Right() - half, b.y, half, b.h, "Quit", kActNative, false, g.quit.enabled, v.mouseX, v.mouseY, kConquestQuit);
	}
	else {
		PaintGhostButton(p, b.x, b.y, b.w, b.h, "Quit", kActNative, false, g.quit.enabled, v.mouseX, v.mouseY, kConquestQuit);
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
		// A longer text ends its last line in an ellipsis: the box keeps the game's size so OK stays over the game's button.
		if (i == count - 1 && static_cast<int>(lines.size()) > count) {
			std::wstring rest = lines[i];
			for (size_t k = i + 1; k < lines.size(); k++) rest += L" " + lines[k];
			RECT r = p.R(30, top + i * kLineH, kVictoryW - 60, kLineH);
			SetTextColor(p.dc, pal::text);
			DrawTextW(p.dc, rest.c_str(), static_cast<int>(rest.size()), &r, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			break;
		}
		SIZE sz = {};
		GetTextExtentPoint32W(p.dc, lines[i].c_str(), static_cast<int>(lines[i].size()), &sz);
		p.LineW((kVictoryW - sz.cx) / 2, top + i * kLineH, kVictoryW, kLineH, lines[i], pal::text);
	}
	const int okX = kOkX + (kVictoryW - kNativeVictoryW) / 2, okY = kOkY + (kVictoryH - kNativeVictoryH) / 2;
	p.Button(okX, okY, kOkW, kOkH, "OK", kActOk, 0, v.mouseX, v.mouseY, true, true);
}

RECT At(int x, int y, int w, int h) { return {x, y, x + w, y + h}; }

RECT ScreenRect(const Painter &p, const Box &b)
{
	RECT r = p.R(b.x, b.y, b.w, b.h);
	OffsetRect(&r, p.ox, p.oy);
	return r;
}

Canvas g_screen, g_victory;

void PaintScreen(uint32_t *buf, int w, int h, const View &v, int pressAction, int pressArg, Painted &out)
{
	const ConquestSnapshot &g = v.game;
	// The game's question (quit?) is drawn over the screen by the game menu's screen. Without it, the game's own box is let through,
	// and only the box (CONFIRM.INI: 522x222 with its frame, centred), never the game's dialog around it.
	RECT hole = {};
	std::vector<uint32_t> saved;
	if (g.confirm && !v.questionDrawn) {
		hole = {std::max(0, w / 2 - kQuestionW / 2), std::max(0, h / 2 - kQuestionH / 2), std::min(w, w / 2 + kQuestionW / 2), std::min(h, h / 2 + kQuestionH / 2)};
		for (int y = hole.top; y < hole.bottom; y++) saved.insert(saved.end(), buf + static_cast<size_t>(y) * w + hole.left, buf + static_cast<size_t>(y) * w + hole.right);
	}
	const ScreenView screen = ScreenViewFor(w, h);
	PanelRender r(g_screen, buf, w, h);
	if (!BeginFullScreen(r, screen)) return;
	Painter &p = r.p;
	p.pressAction = pressAction;
	p.pressArg = pressArg;
	p.noHover = g.confirm || v.victory;
	UseScreenFonts(p);
	const RECT crop = g.map ? CropOf(*g.map) : RECT{0, 0, 4, 3};
	const Layout L = LayoutFor(screen.w, screen.h, crop.right - crop.left, crop.bottom - crop.top);
	if (g.map) {
		out.map = ScreenRect(p, L.map);
		out.crop = crop;
	}
	out.desc = ScreenRect(p, L.desc);
	int hovered = -1;
	POINT pixel = {};
	if (!p.noHover && g.regions && MapPixelAt(out, {v.mouseX, v.mouseY}, pixel)) {
		const uint8_t t = g.regions->territory[static_cast<size_t>(pixel.y) * g.regions->w + pixel.x];
		if (t != kConquestNoTerritory && Valid(g, t)) hovered = t;
	}
	Layer layer{p, {}};
	const int right = PaintHeaderActions(p, L.head, {{"Back", kActBack, true, g.quit.enabled}}, v.mouseX, v.mouseY);
	PaintScreenTitle(p, L.head, "Conquest", {}, right, v.mouseX, v.mouseY);
	PaintPath(layer, L.path, v);
	PaintMap(layer, r.canvas, L, v, crop, hovered);
	PaintLegend(p, L, v);
	if (L.legendBeside) PaintHint(p, L.hint, v);
	PaintTerritoryCard(layer, L.territory, v);
	PaintDescription(p, L.desc, v, out);
	PaintBonus(p, L.bonus, v);
	PaintDifficulty(p, L.difficulty, v);
	PaintButtons(p, L, v);
	if (hovered >= 0) PaintMapTag(p, L, v, hovered);
	FlushImages(r.canvas, layer);
	r.End();
	out.hits = std::move(r.hits);
	if (!saved.empty()) {
		DimBackdrop(buf, w, h);
		size_t i = 0;
		for (int y = hole.top; y < hole.bottom; y++, i += hole.right - hole.left) std::copy(saved.begin() + i, saved.begin() + i + (hole.right - hole.left), buf + static_cast<size_t>(y) * w + hole.left);
	}
}
}

void PaintConquest(uint32_t *buf, int w, int h, const View &v, int pressAction, int pressArg, Painted &out)
{
	out = Painted();
	// Without the map's picture the game's own dialog stays in view and takes the input.
	if (v.game.open && v.game.map) PaintScreen(buf, w, h, v, pressAction, pressArg, out);
	if (v.victory) {
		DimBackdrop(buf, w, h);
		out.hits.clear();
		const int cx = w / 2, cy = h / 2;
		const RECT box = At(cx - kVictoryW / 2, cy - kVictoryH / 2, kVictoryW, kVictoryH);
		Render(g_victory, buf, w, h, box, pressAction, pressArg, out.hits, [&](Layer &l) { PaintVictory(l, v); });
		const int nx = (w - kNativeVictoryW) / 2, ny = (h - kNativeVictoryH) / 2;
		out.ok = At(nx + kOkX, ny + kOkY, kOkW, kOkH);
	}
}

}
