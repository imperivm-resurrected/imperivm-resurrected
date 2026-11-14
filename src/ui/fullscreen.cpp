// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/fullscreen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "ui/brandart.h"

namespace ui {

ScreenView ScreenViewFor(int screenW, int screenH)
{
	ScreenView v;
	// Below 0.8 the captions would shrink past reading at 720p: shorter screens get fewer design units instead, and the layout tightens.
	v.s = std::max(0.8f, std::min(1.6f, screenH / 1080.0f));
	v.s = std::max(0.6f, std::min(v.s, screenW / 1200.0f));
	v.w = static_cast<int>(std::ceil(screenW / v.s - 0.01f));
	v.h = static_cast<int>(std::ceil(screenH / v.s - 0.01f));
	return v;
}

namespace {
struct Backdrop {
	Picture map;
	// 0 not read yet, 1 read, -1 missing.
	int state = 0;
	std::vector<uint32_t> scaled;
	int w = 0, h = 0;
};
Backdrop g_backdrop;

// Cover-scaled with bilinear sampling: the map is a little smaller than 1080p and much smaller than 4K, so most screens enlarge it.
void ScaleBackdrop(Backdrop &b, int w, int h)
{
	const Picture &m = b.map;
	b.scaled.assign(static_cast<size_t>(w) * h, 0);
	b.w = w;
	b.h = h;
	const float scale = std::max(static_cast<float>(w) / m.w, static_cast<float>(h) / m.h);
	const float offX = (m.w - w / scale) / 2, offY = (m.h - h / scale) / 2;
	std::vector<int> x0(w), x1(w);
	std::vector<uint32_t> fx(w);
	for (int x = 0; x < w; x++) {
		const float sx = std::max(0.0f, std::min(m.w - 1.0f, offX + (x + 0.5f) / scale - 0.5f));
		x0[x] = static_cast<int>(sx);
		x1[x] = std::min(m.w - 1, x0[x] + 1);
		fx[x] = static_cast<uint32_t>((sx - x0[x]) * 256);
	}
	for (int y = 0; y < h; y++) {
		const float sy = std::max(0.0f, std::min(m.h - 1.0f, offY + (y + 0.5f) / scale - 0.5f));
		const int y0 = static_cast<int>(sy), y1 = std::min(m.h - 1, y0 + 1);
		const uint32_t fy = static_cast<uint32_t>((sy - y0) * 256);
		const uint32_t *r0 = &m.px[static_cast<size_t>(y0) * m.w], *r1 = &m.px[static_cast<size_t>(y1) * m.w];
		uint32_t *out = &b.scaled[static_cast<size_t>(y) * w];
		for (int x = 0; x < w; x++) {
			const uint32_t a = r0[x0[x]], bb = r0[x1[x]], c = r1[x0[x]], d = r1[x1[x]], wx = fx[x];
			uint32_t px = 0;
			for (int shift = 0; shift < 24; shift += 8) {
				const uint32_t top = ((a >> shift) & 255) * (256 - wx) + ((bb >> shift) & 255) * wx;
				const uint32_t bottom = ((c >> shift) & 255) * (256 - wx) + ((d >> shift) & 255) * wx;
				px |= ((top * (256 - fy) + bottom * fy) >> 16) << shift;
			}
			out[x] = px;
		}
	}
}

HBITMAP NewDot(uint32_t *&bits)
{
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = 1;
	bi.bmiHeader.biHeight = -1;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *raw = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
	if (bmp && !raw) {
		DeleteObject(bmp);
		bmp = nullptr;
	}
	bits = static_cast<uint32_t *>(raw);
	return bmp;
}

struct Isotype {
	Picture pic;
	// 0 not read yet, 1 read, -1 missing.
	int state = 0;
	// Premultiplied for GdiAlphaBlend, one per size in pixels; a screen only asks for a few.
	std::map<std::pair<int, int>, HBITMAP> scaled;
};
Isotype g_isotype;
std::mutex g_isotypeLock;

// Each target pixel is the average of the source pixels it covers, weighted by their alpha.
HBITMAP ScaledIsotype(const Picture &pic, int w, int h)
{
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *raw = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
	if (!bmp || !raw) {
		if (bmp) DeleteObject(bmp);
		return nullptr;
	}
	uint32_t *dst = static_cast<uint32_t *>(raw);
	for (int y = 0; y < h; y++) {
		const int y0 = y * pic.h / h, y1 = std::max(y0 + 1, (y + 1) * pic.h / h);
		for (int x = 0; x < w; x++) {
			const int x0 = x * pic.w / w, x1 = std::max(x0 + 1, (x + 1) * pic.w / w);
			uint64_t a = 0, r = 0, g = 0, b = 0, count = 0;
			for (int yy = y0; yy < y1 && yy < pic.h; yy++) {
				for (int xx = x0; xx < x1 && xx < pic.w; xx++) {
					const uint32_t px = pic.px[static_cast<size_t>(yy) * pic.w + xx], pa = px >> 24;
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
	return bmp;
}
}

void PaintIsotype(Painter &p, int cx, int cy, int h, int alpha)
{
	std::lock_guard<std::mutex> lock(g_isotypeLock);
	Isotype &iso = g_isotype;
	if (!iso.state) {
		iso.state = LoadPicture(BrandFile(L"isotype.png"), iso.pic) ? 1 : -1;
		if (iso.state < 0) LogW("ui", "brand/isotype.png is missing or unreadable; the missing pictures stay plain");
	}
	if (iso.state < 0 || h <= 0 || iso.pic.h <= 0) return;
	const int w = std::max(1, h * iso.pic.w / iso.pic.h);
	const RECT r = p.R(cx - w / 2, cy - h / 2, w, h);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	if (pw <= 0 || ph <= 0) return;
	HBITMAP &bmp = iso.scaled[{pw, ph}];
	if (!bmp) bmp = ScaledIsotype(iso.pic, pw, ph);
	if (!bmp) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, static_cast<BYTE>(std::max(0, std::min(255, alpha))), AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

bool PaintBackdrop(Canvas &canvas)
{
	Backdrop &b = g_backdrop;
	if (!b.state) {
		b.state = LoadPicture(BrandFile(L"backdrop-map.jpg"), b.map) ? 1 : -1;
		if (b.state > 0) LogI("ui", "screen backdrop %dx%d", b.map.w, b.map.h);
		else LogW("ui", "screen backdrop: brand/backdrop-map.jpg is missing or unreadable; the full screens keep their flat background");
	}
	if (b.state < 0 || !canvas.bits || canvas.w <= 0 || canvas.h <= 0) return false;
	if (b.w != canvas.w || b.h != canvas.h) ScaleBackdrop(b, canvas.w, canvas.h);
	GdiFlush();
	std::copy(b.scaled.begin(), b.scaled.end(), canvas.bits);
	return true;
}

bool BeginFullScreen(PanelRender &r, const ScreenView &view)
{
	if (!r.Begin(view.w, view.h, 0, false, view.s)) return false;
	if (!PaintBackdrop(r.canvas)) r.p.Fill(0, 0, view.w, view.h, pal::bg);
	return true;
}

void PaintGlassCard(Painter &p, int x, int y, int w, int h)
{
	static uint32_t *bits = nullptr;
	static HBITMAP dot = NewDot(bits);
	if (!dot) {
		PaintCard(p, x, y, w, h);
		return;
	}
	const RECT r = p.R(x, y, w, h);
	GdiFlush();
	*bits = PixelOf(pal::panel);
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, dot);
	// The panel at 88 %, so the engraved map shows faintly through the cards.
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 224, 0};
	GdiAlphaBlend(p.dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem, 0, 0, 1, 1, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
	p.Frame(x, y, w, h, pal::hairline);
}

void UseScreenFonts(Painter &p)
{
	p.fonts[0] = BrandFont(BrandFace::Body, 17, p.s);
	p.fonts[1] = BrandFont(BrandFace::BodyBold, 17, p.s);
	p.fonts[2] = BrandFont(BrandFace::Display, 30, p.s);
	p.fonts[3] = BrandFont(BrandFace::Ui, 15, p.s);
	p.fonts[4] = BrandFont(BrandFace::UiStrong, 13, p.s);
	p.fonts[5] = BrandFont(BrandFace::Display, 72, p.s);
}

void UseSharedFonts(Painter &p)
{
	std::copy(std::begin(g_fonts), std::end(g_fonts), std::begin(p.fonts));
}

ScreenLayout ScreenLayoutFor(int w, int h)
{
	ScreenLayout l;
	l.w = w;
	l.h = h;
	l.margin = std::max(32, std::min(64, std::min(l.w * 64 / 1920, l.h * 64 / 1080)));
	l.roomy = l.h >= 1000;
	l.titleSize = l.roomy ? 84 : 68;
	l.titleH = l.titleSize + 20;
	l.titleY = l.margin - 14;
	l.infoY = l.titleY + l.titleH;
	l.top = l.infoY + l.infoH + (l.roomy ? 36 : 24);
	return l;
}

void PaintBackArrow(Painter &p, int x, int cy, COLORREF ink)
{
	const int size = 14;
	PaintOrnament(p, kOrnamentBack, x, cy - size / 2, size, size, ink);
}

int GhostButtonWidth(Painter &p, const std::string &label, bool arrow)
{
	p.Font(4);
	return p.CapsWidth(label, 2) + 48 + (arrow ? kBackArrowW : 0);
}

void PaintGhostButton(Painter &p, int x, int y, int w, int h, const std::string &label, int action, bool arrow, bool enabled, int mx, int my, int arg)
{
	const bool hot = enabled && p.Hover(x, y, w, h, mx, my);
	const bool down = enabled && p.pressAction == action && p.pressArg == arg;
	if (down) p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, !enabled ? pal::hairline : hot || down ? pal::edgeHot : pal::edge);
	const COLORREF ink = !enabled ? pal::disabled : hot || down ? pal::text : pal::textDim;
	p.Font(4);
	// A long label in a narrow button closes its letter spacing, and is shortened only if it still doesn't fit.
	const int room = w - 20 - (arrow ? kBackArrowW : 0);
	const int spacing = p.CapsWidth(label, 2) <= room ? 2 : 1;
	const int textW = std::min(p.CapsWidth(label, spacing), room);
	int tx = x + (w - textW - (arrow ? kBackArrowW : 0)) / 2;
	if (arrow) {
		PaintBackArrow(p, tx, y + h / 2, ink);
		tx += kBackArrowW;
	}
	p.TextCaps(tx, y, textW + 2, h, label, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE, spacing);
	if (enabled) p.AddHit(x, y, w, h, action, arg);
}

int PaintHeaderActions(Painter &p, const ScreenLayout &l, const std::vector<GhostButton> &buttons, int mx, int my)
{
	int left = l.w - l.margin + 12;
	for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
		const int w = GhostButtonWidth(p, it->label, it->arrow);
		left -= 12 + w;
		PaintGhostButton(p, left, l.margin, w, kHeaderButtonH, it->label, it->action, it->arrow, it->enabled, mx, my);
	}
	return std::min(left, l.w - l.margin);
}

int PaintScreenTitle(Painter &p, const ScreenLayout &l, const std::string &title, const std::vector<TabSpec> &tabs, int right, int mx, int my)
{
	const int m = l.margin, tabGap = 48, tabH = 52;
	p.Font(2);
	int tabW = 0;
	for (const TabSpec &t : tabs) tabW = std::max(tabW, p.CapsWidth(t.label, 2) + 44);
	const int count = static_cast<int>(tabs.size()), tabsW = count ? count * tabW + (count - 1) * 6 + tabGap : 0;
	SelectObject(p.dc, BrandFont(BrandFace::Display, l.titleSize, p.s));
	const int titleMax = std::max(160, right - 32 - tabsW - m);
	const int titleW = std::min(titleMax, p.CapsWidth(title, 1) + 4);
	p.TextCaps(m, l.titleY, titleW, l.titleH, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	if (count) PaintTabs(p, m + titleW + tabGap, l.titleY + l.titleH - tabH - 4, tabs, mx, my, tabW, tabH, 2);
	return m + titleW + tabsW;
}

void PaintScreenInfo(Painter &p, const ScreenLayout &l, const std::string &text, COLORREF color)
{
	p.Font(0);
	p.Text(l.margin, l.infoY, l.w - 2 * l.margin, l.infoH, text, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

bool BeginWindow(PanelRender &r, int designW, int designH, int margin, float forceScale)
{
	if (!r.Begin(designW, designH, margin, true, forceScale)) return false;
	// Without what lies under it, the glass would blend over the window's previous frame and keep its ghost.
	const Canvas &c = r.canvas;
	const uint32_t outside = PixelOf(pal::bg);
	GdiFlush();
	for (int y = 0; y < c.h; y++) {
		uint32_t *row = c.bits + static_cast<size_t>(y) * c.w;
		const int sy = r.oy + y;
		if (sy < 0 || sy >= r.h) {
			std::fill(row, row + c.w, outside);
			continue;
		}
		const uint32_t *src = r.buf + static_cast<size_t>(sy) * r.w;
		for (int x = 0; x < c.w; x++) {
			const int sx = r.ox + x;
			row[x] = sx >= 0 && sx < r.w ? src[sx] : outside;
		}
	}
	return true;
}

void PaintWindowCard(Painter &p, int w, int h)
{
	PaintGlassCard(p, 0, 0, w, h);
}

int PaintWindowTitle(Painter &p, int x, int y, int w, int h, const std::string &title, int size, int letterSpacing)
{
	SelectObject(p.dc, BrandFont(BrandFace::Display, size, p.s));
	const int titleW = std::min(w, p.CapsWidth(title, letterSpacing) + 4);
	p.TextCaps(x, y, titleW, h, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, letterSpacing);
	return titleW;
}

int PaintWindowHeading(Painter &p, int w, const std::string &title, const std::string &caption, const std::vector<TabSpec> &tabs, int right, int mx,
	int my, int height, int letterSpacing)
{
	constexpr int m = 24, tabGap = 32, tabH = 34, captionH = 20, ruleGap = 4;
	if (right <= 0) right = w - m;
	const int titleSize = height >= 76 ? 44 : 38, titleH = titleSize + 4;
	const int titleY = caption.empty() ? (height - ruleGap - titleH) / 2 : std::max(4, height - ruleGap - 4 - captionH - titleH);
	p.Font(1);
	int tabW = 0;
	for (const TabSpec &t : tabs) tabW = std::max(tabW, p.CapsWidth(t.label, 1) + 32);
	const int count = static_cast<int>(tabs.size()), tabsW = count ? count * tabW + (count - 1) * 6 + tabGap : 0;
	const int titleW = PaintWindowTitle(p, m, titleY, std::max(120, right - m - tabsW), titleH, title, titleSize, letterSpacing);
	if (count) PaintTabs(p, m + titleW + tabGap, titleY + titleH - tabH, tabs, mx, my, tabW, tabH, 1);
	if (!caption.empty()) {
		p.Font(0);
		p.Text(m, titleY + titleH, right - m, captionH, caption, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	p.Fill(m, height - ruleGap, w - 2 * m, 1, pal::hairline);
	return height;
}

void PaintSectionLabel(Painter &p, int x, int y, int w, int h, const std::string &text)
{
	p.Font(4);
	p.TextCaps(x, y, w, h, text, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
}

}
