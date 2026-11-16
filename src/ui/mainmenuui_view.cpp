// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/mainmenuui_view.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "ui/brandart.h"

using namespace ui;

namespace mainmenuui {
namespace {

// Laid out in pixels of a 1920x1080 screen and scaled with its height.
constexpr int kLeft = 80, kLogoTop = 64, kIsoW = 212, kLogoGap = 32, kLogoW = 620, kTaglineGap = 16, kTaglineH = 18;
// The list fills the height under the logo: eight rows end some 150 px over the bottom, clear of the corner's icons.
constexpr int kListTop = 372, kRowH = 70, kListFont = 58, kChevronGap = 16, kChevronW = 26;
constexpr int kDividerMin = 560, kDividerGap = 52, kSubGap = 44, kSubW = 440, kSubCaptionH = 50, kSubRowH = 64, kSubFont = 24;
// The submenu opens with its rows sliding in from the left one after another, and closes fading out.
constexpr int kOpenMs = 220, kStaggerMs = 35, kCloseMs = 140, kSlide = 24;
const char kChevron[] = "\xE2\x80\xBA";

struct ScaledPicture {
	HBITMAP bmp = nullptr;
	int w = 0, h = 0;
};

Picture g_iso, g_logo;
std::atomic<int> g_artState{0};
std::mutex g_artLock;

// Only the render thread touches these.
ScaledPicture g_isoScaled, g_logoScaled;
Canvas g_canvas;
HDC g_measure = nullptr;
struct Fonts {
	float s = 0;
	HFONT list = nullptr, sub = nullptr;
} g_f;

// Averages the source pixels each target pixel covers, weighted by their alpha, and premultiplies the result for AlphaBlend.
HBITMAP ScaledBitmap(const Picture &pic, ScaledPicture &cache, int w, int h)
{
	if (cache.bmp && cache.w == w && cache.h == h) return cache.bmp;
	if (cache.bmp) DeleteObject(cache.bmp);
	cache = ScaledPicture();
	if (w <= 0 || h <= 0 || pic.px.empty()) return nullptr;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp || !bits) {
		if (bmp) DeleteObject(bmp);
		return nullptr;
	}
	uint32_t *dst = static_cast<uint32_t *>(bits);
	for (int y = 0; y < h; y++) {
		const int y0 = y * pic.h / h, y1 = std::max(y0 + 1, (y + 1) * pic.h / h);
		for (int x = 0; x < w; x++) {
			const int x0 = x * pic.w / w, x1 = std::max(x0 + 1, (x + 1) * pic.w / w);
			uint64_t a = 0, r = 0, g = 0, b = 0, count = 0;
			for (int yy = y0; yy < y1 && yy < pic.h; yy++) {
				for (int xx = x0; xx < x1 && xx < pic.w; xx++) {
					const uint32_t px = pic.px[static_cast<size_t>(yy) * pic.w + xx];
					const uint32_t pa = px >> 24;
					a += pa;
					r += ((px >> 16) & 0xff) * pa;
					g += ((px >> 8) & 0xff) * pa;
					b += (px & 0xff) * pa;
					count++;
				}
			}
			uint32_t out = 0;
			if (count && a) {
				const uint64_t div = count * 255;
				out = static_cast<uint32_t>(a / count) << 24 | static_cast<uint32_t>(r / div) << 16 | static_cast<uint32_t>(g / div) << 8 |
					static_cast<uint32_t>(b / div);
			}
			dst[static_cast<size_t>(y) * w + x] = out;
		}
	}
	cache.bmp = bmp;
	cache.w = w;
	cache.h = h;
	return bmp;
}

void DrawPicture(Painter &p, const Picture &pic, ScaledPicture &cache, int x, int y, int w)
{
	if (pic.w <= 0) return;
	const RECT r = p.R(x, y, w, w * pic.h / pic.w);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	HBITMAP bmp = ScaledBitmap(pic, cache, pw, ph);
	if (!bmp) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

HFONT MakeFont(float s, int size, const wchar_t *face)
{
	// Grey antialiasing: ClearType's coloured fringes show over the painted background.
	return CreateFontW(-static_cast<int>(size * s + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}

void EnsureMenuFonts(float s)
{
	EnsureFonts(s);
	if (g_f.list && g_f.s == s) return;
	// DeleteObject refuses a font still selected into a DC: the measuring DC always holds the list's, the canvas the last one it drew with.
	for (HDC dc : {g_canvas.dc, g_measure}) {
		if (dc) SelectObject(dc, GetStockObject(SYSTEM_FONT));
	}
	for (HFONT f : {g_f.list, g_f.sub}) {
		if (f) DeleteObject(f);
	}
	// League Gothic has one weight: asking it bold would make Windows smear a fake one.
	g_f.list = MakeFont(s, kListFont, LoadBrandFonts() ? pal::kFontDisplay : pal::kFontDisplayFallback);
	g_f.sub = MakeFont(s, kSubFont, UiFace());
	g_f.s = s;
}

struct Layout {
	int textX = kLeft;
	int divider = kDividerMin, subX = 0;
	std::vector<int> labelW;
	int regionW = 0;
};

float Clamp01(float t)
{
	return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
}

float EaseOut(float t)
{
	const float u = 1.0f - t;
	return 1.0f - u * u * u;
}

// Paints with `paint` at `alpha` (0 to 1) inside the 1080p rectangle (x, y, w, h): what was there shows through.
template <typename F> void Faded(const Painter &p, int x, int y, int w, int h, float alpha, F paint)
{
	if (alpha <= 0.0f) return;
	if (alpha >= 1.0f) {
		paint();
		return;
	}
	RECT r = p.R(x, y, w, h);
	r.left = std::max<LONG>(r.left, 0);
	r.top = std::max<LONG>(r.top, 0);
	r.right = std::min<LONG>(r.right, g_canvas.w);
	r.bottom = std::min<LONG>(r.bottom, g_canvas.h);
	if (r.right <= r.left || r.bottom <= r.top) return;
	const int cw = r.right - r.left;
	std::vector<uint32_t> before(static_cast<size_t>(cw) * (r.bottom - r.top));
	for (int yy = r.top; yy < r.bottom; yy++)
		memcpy(&before[static_cast<size_t>(yy - r.top) * cw], g_canvas.bits + static_cast<size_t>(yy) * g_canvas.w + r.left, cw * sizeof(uint32_t));
	paint();
	GdiFlush();
	const uint32_t a = static_cast<uint32_t>(alpha * 256.0f), b = 256 - a;
	for (int yy = r.top; yy < r.bottom; yy++) {
		uint32_t *row = g_canvas.bits + static_cast<size_t>(yy) * g_canvas.w + r.left;
		const uint32_t *old = &before[static_cast<size_t>(yy - r.top) * cw];
		for (int xx = 0; xx < cw; xx++) {
			const uint32_t now = row[xx], was = old[xx];
			const uint32_t rb = ((now & 0xff00ff) * a + (was & 0xff00ff) * b) >> 8;
			const uint32_t g = ((now & 0x00ff00) * a + (was & 0x00ff00) * b) >> 8;
			row[xx] = (rb & 0xff00ff) | (g & 0x00ff00);
		}
	}
}

Layout Measure(const View &v, float s)
{
	Layout l;
	if (!g_measure) g_measure = CreateCompatibleDC(nullptr);
	Painter m{g_measure, s, 0, 0, nullptr};
	m.raw = true;
	SelectObject(g_measure, g_f.list);
	int widest = 0;
	for (const Item &item : v.main) {
		l.labelW.push_back(m.CapsWidth(item.label));
		widest = std::max(widest, l.labelW.back());
	}
	l.divider = std::max(kDividerMin, l.textX + widest + kChevronGap + kChevronW + kDividerGap);
	l.subX = l.divider + kSubGap;
	l.regionW = std::max(l.subX + kSubW + 24, kLeft + kIsoW + kLogoGap + kLogoW + 24);
	return l;
}

int HotIndex(const Painter &p, const View &v, int keyboardIndex, int x, int top, int w, int rowH, int count)
{
	if (!v.interactive) return -1;
	if (v.keyboard) return keyboardIndex;
	for (int i = 0; i < count; i++) {
		if (p.Hover(x, top + i * rowH, w, rowH, v.mouseX, v.mouseY)) return i;
	}
	return -1;
}

void PaintLogo(Painter &p, const View &v)
{
	DrawPicture(p, g_iso, g_isoScaled, kLeft, kLogoTop, kIsoW);
	const int isoH = g_iso.w > 0 ? kIsoW * g_iso.h / g_iso.w : 0;
	const int logoH = g_logo.w > 0 ? kLogoW * g_logo.h / g_logo.w : 0;
	const int logoX = kLeft + kIsoW + kLogoGap, logoY = kLogoTop + std::max(0, (isoH - logoH - kTaglineGap - kTaglineH) / 2);
	DrawPicture(p, g_logo, g_logoScaled, logoX, logoY, kLogoW);
	p.Font(4);
	p.TextCaps(logoX, logoY + logoH + kTaglineGap, kLogoW, kTaglineH, v.tagline, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 4);
}

void PaintList(Painter &p, const View &v, const Layout &l)
{
	const int count = static_cast<int>(v.main.size()), rowW = l.divider - kLeft - 16;
	const int hot = HotIndex(p, v, v.subOpen && v.focusSub >= 0 ? -1 : v.focusMain, kLeft, kListTop, rowW, kRowH, count);
	for (int i = 0; i < count; i++) {
		const Item &item = v.main[i];
		const int y = kListTop + i * kRowH;
		const bool open = IsGroup(item.id) && item.id == v.openGroup && v.subOpen, lit = item.enabled && (open || i == hot);
		SelectObject(p.dc, g_f.list);
		p.TextCaps(l.textX, y, l.divider - l.textX - 8, kRowH, item.label, !item.enabled ? pal::disabled : lit ? pal::text : pal::textDim);
		const int after = l.textX + l.labelW[i] + kChevronGap;
		if (open) p.Text(after, y, kChevronW + 8, kRowH, kChevron, pal::redText);
		if (item.badge > 0) {
			const int dot = 9;
			p.Ellipse(after, y + (kRowH - dot) / 2, dot, dot, pal::red);
			p.Font(4);
			p.Text(after + dot + 7, y, 48, kRowH, std::to_string(item.badge), pal::redText);
		}
		if (item.enabled && v.interactive) p.AddHit(kLeft, y, rowW, kRowH, kActMain, i);
	}
}

// `since`: milliseconds since the submenu opened or closed.
void PaintSubmenu(Painter &p, const View &v, const Layout &l, unsigned since)
{
	const int count = static_cast<int>(v.sub.size()), top = kListTop + kSubCaptionH;
	const int bottom = top + count * kSubRowH;
	const float closing = 1.0f - Clamp01(static_cast<float>(since) / kCloseMs);
	// Opening, the caption (step 0) and each row (steps 1...) start a little after the one above.
	auto shown = [&](int step) {
		return v.subOpen ? EaseOut(Clamp01((static_cast<float>(since) - step * kStaggerMs) / kOpenMs)) : closing;
	};
	const float line = shown(0);
	const int lineH = static_cast<int>((bottom - kListTop) * (v.subOpen ? line : 1.0f) + 0.5f);
	Faded(p, l.divider, kListTop, 1, lineH, v.subOpen ? 1.0f : closing, [&] { p.Fill(l.divider, kListTop, 1, lineH, pal::lineStrong); });
	auto slide = [&](float t) { return v.subOpen ? static_cast<int>((1.0f - t) * kSlide + 0.5f) : 0; };
	const float caption = shown(0);
	Faded(p, l.subX - kSlide, kListTop, kSubW + kSlide, kSubCaptionH, caption, [&] {
		p.Font(4);
		p.TextCaps(l.subX - slide(caption), kListTop, kSubW, kSubCaptionH - 6, v.groupCaption, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 3);
	});
	const int hot = HotIndex(p, v, v.focusSub, l.subX, top, kSubW, kSubRowH, count);
	for (int i = 0; i < count; i++) {
		const Item &item = v.sub[i];
		const int y = top + i * kSubRowH;
		const bool lit = item.enabled && v.subOpen && i == hot;
		const float t = shown(i + 1);
		const int x = l.subX - slide(t);
		Faded(p, l.subX - kSlide, y, kSubW + kSlide, kSubRowH, t, [&] {
			p.Fill(x, y, kSubW, 1, pal::hairline);
			SelectObject(p.dc, g_f.sub);
			p.Text(x, y, kSubW - 36, kSubRowH, item.label, !item.enabled ? pal::disabled : lit ? pal::text : pal::textDim);
			if (lit) p.Text(x + kSubW - 24, y, 24, kSubRowH, kChevron, pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		});
		if (item.enabled && v.interactive && v.subOpen) p.AddHit(l.subX, y, kSubW, kSubRowH, kActSub, i);
	}
}

}

bool LoadBrandArt()
{
	const int state = g_artState.load();
	if (state) return state > 0;
	std::lock_guard<std::mutex> lock(g_artLock);
	if (g_artState.load()) return g_artState.load() > 0;
	const bool ok = LoadPicture(BrandFile(L"isotype.png"), g_iso) && LoadPicture(BrandFile(L"logotype-light.png"), g_logo);
	if (ok) LogI("ui", "main menu: brand art %dx%d and %dx%d", g_iso.w, g_iso.h, g_logo.w, g_logo.h);
	g_artState.store(ok ? 1 : -1);
	return ok;
}

void Paint(uint32_t *buf, int w, int h, const View &v, std::vector<Hit> &hits)
{
	if (g_artState.load() <= 0 || w <= 0 || h <= 0 || v.main.empty()) return;
	const float s = std::max(0.5f, std::min(1.6f, std::min(h / 1080.0f, w / 1440.0f)));
	EnsureMenuFonts(s);
	const unsigned since = v.subChangedAt ? GetTickCount() - v.subChangedAt : 0x40000000u;
	const float subShown = v.subOpen ? EaseOut(Clamp01(static_cast<float>(since) / kOpenMs)) : 1.0f - Clamp01(static_cast<float>(since) / kCloseMs);
	const Layout l = Measure(v, s);
	const int regionW = std::min(w, static_cast<int>(l.regionW * s + 0.5f));
	if (regionW <= 0 || !g_canvas.Ensure(regionW, h)) return;
	for (int y = 0; y < h; y++) memcpy(g_canvas.bits + static_cast<size_t>(y) * regionW, buf + static_cast<size_t>(y) * w, regionW * sizeof(uint32_t));
	Painter p{g_canvas.dc, s, 0, 0, &hits};
	std::copy(std::begin(g_fonts), std::end(g_fonts), std::begin(p.fonts));
	p.raw = true;
	p.noHover = !v.interactive;
	PaintLogo(p, v);
	PaintList(p, v, l);
	if (!v.sub.empty() && (v.subOpen || subShown > 0.0f)) PaintSubmenu(p, v, l, since);
	GdiFlush();
	for (int y = 0; y < h; y++) {
		const uint32_t *from = g_canvas.bits + static_cast<size_t>(y) * regionW;
		uint32_t *to = buf + static_cast<size_t>(y) * w;
		for (int x = 0; x < regionW; x++) to[x] = 0xff000000u | from[x];
	}
}

}
