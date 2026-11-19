// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <atomic>

#include "core/versioncmp.h"
#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
int DropCurrent(const State &st)
{
	if (st.drop == kDropProfile) {
		for (size_t i = 0; i < st.profileNames.size(); i++) {
			if (st.profileNames[i] == st.profile.name) return static_cast<int>(i);
		}
		return -1;
	}
	if (st.drop == kDropSort) return st.sort;
	if (st.drop == kDropIoSort) return st.ioSort;
	return -1;
}
}

namespace modui_impl {
std::atomic<int> g_installedRows{12};
}

namespace modui_impl {
namespace {
bool IsDateVersion(const std::string &v)
{
	if (v.size() != 10 || v[4] != '.' || v[7] != '.') return false;
	for (size_t i = 0; i < v.size(); i++) {
		if (i != 4 && i != 7 && !isdigit(static_cast<unsigned char>(v[i]))) return false;
	}
	return true;
}

struct Tip {
	std::string text;
	int x = 0, y = 0;
};
Tip g_tip;

void SetTip(const std::string &text, int x, int y)
{
	g_tip.text = text;
	g_tip.x = x;
	g_tip.y = y;
}

void PaintTip(Painter &p, const State &st)
{
	if (g_tip.text.empty()) return;
	p.Font(3);
	const int w = std::min(460, p.TextWidth(g_tip.text) + 20), h = 30;
	const int x = std::max(8, std::min(st.viewW - w - 8, g_tip.x)), y = std::min(st.viewH - h - 6, g_tip.y + 2);
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, pal::edge);
	p.Text(x + 10, y, w - 14, h, g_tip.text, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	g_tip.text.clear();
}

void Combo(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, int kind)
{
	PaintCombo(p, x, y, w, h, text, st.drop == kind, true, st.mouseX, st.mouseY, kDropOpen, kind, &g_drop.anchor);
}

void PaintDropList(Painter &p, const State &st)
{
	if (st.drop == kDropNone) return;
	const std::vector<std::string> texts = DropItems(st);
	if (texts.empty()) return;
	std::vector<ui::DropItem> items = ToDropItems(texts);
	const int n = static_cast<int>(items.size());
	RECT anchor = g_drop.anchor;
	if (st.drop == kDropRowMenu) {
		const int ax = static_cast<int>((st.menuX - p.ox) / p.s), ay = static_cast<int>((st.menuY - p.oy) / p.s);
		anchor = {ax, ay - 1, ax + 1, ay};
	}
	for (int i = 0; i < n; i++) {
		if ((st.drop == kDropModMenu || st.drop == kDropRowMenu) && i + 1 == n) items[i].color = pal::bad;
		if (st.drop == kDropMenu && i + 1 == n) items[i].color = pal::bad;
		if (st.drop == kDropProfile && (i == 1 || i + 1 == n)) items[i].color = pal::redText;
	}
	if (st.drop == kDropClashFiles) {
		if (const ModIssues *is = SelectedIssues(st)) {
			const int files = static_cast<int>(is->files.size());
			for (int i = 0; i < n && i < files + static_cast<int>(is->values.size()); i++) {
				const int kind = i < files ? is->files[i].kind : is->values[i - files].kind;
				items[i].color = kind == kModRules ? pal::redText : kind == kModContent ? RGB(140, 196, 176) : pal::text;
			}
		}
	}
	const ui::DropLayout d = PaintDropMenu(p, anchor, items, DropCurrent(st), st.dropScroll, st.viewW, st.viewH, kDropPick, kDropClose,
		st.mouseX, st.mouseY, st.drop == kDropClashFiles ? PanelLayoutFor(st).detailW - 48 : st.drop == kDropVersions ? 360 : 180, st.dropBar);
	g_drop.rows = d.rows;
	g_drop.count = d.count;
}

int IoInstalledState(const State &st, const GbMod &m)
{
	int state = 0;
	for (const ModInfo &mi : st.mods) {
		if (m.id && mi.gbId == m.id) {
			if (!m.details || VersionIsCurrent(mi.version, m.entry.version)) return 1;
			state = 2;
		}
	}
	return state;
}

std::string Thousands(uint64_t n)
{
	std::string s = std::to_string(n);
	for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, ".");
	return s;
}

std::string UpperText(const std::string &s)
{
	std::wstring w = ui::W(s);
	if (w.empty()) return s;
	CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
}

std::mutex g_spinMx;
std::map<std::string, DWORD> g_spinSeen;

bool SpinnerDue(const std::string &key)
{
	std::lock_guard<std::mutex> lock(g_spinMx);
	const DWORD now = GetTickCount();
	const auto it = g_spinSeen.find(key);
	if (it == g_spinSeen.end()) {
		if (g_spinSeen.size() > 256) g_spinSeen.clear();
		g_spinSeen[key] = now;
		return false;
	}
	return now - it->second > 200;
}

void SpinnerDone(const std::string &key)
{
	std::lock_guard<std::mutex> lock(g_spinMx);
	g_spinSeen.erase(key);
}

void PaintIoImage(Painter &p, int x, int y, int w, int h, const std::string &url)
{
	const std::shared_ptr<const GbImage> img = GbLogo(url);
	if (img) {
		p.Image(x, y, w, h, img->bgra.data(), img->width, img->height);
	}
	else if (url.empty()) {
		p.Fill(x, y, w, h, pal::panel);
		p.Font(3);
		p.Text(x, y, w, h, "No image", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else {
		PaintSkeleton(p, x, y, w, h);
	}
}

// The cards of Browse content while a page loads, in the shape the real ones will take: the picture, its category and three lines.
// The tags over a card's picture (its category at the bottom left, installed or to update at the top right): as far from the
// picture's side as from its edge.
const int kIoTagH = 26, kIoTagInset = 8;

void PaintSkeletonCard(Painter &p, int x, int y, int cw, int ch, int imgW, int imgH)
{
	const int imgX = x + (cw - imgW) / 2, imgY = y + 1;
	PaintGlassCard(p, x, y, cw, ch);
	p.Fill(x + 1, imgY, cw - 2, imgH, pal::panel);
	PaintSkeleton(p, imgX, imgY, imgW, imgH);
	p.Fill(imgX + kIoTagInset, imgY + imgH - kIoTagH - kIoTagInset, std::min(imgW - 2 * kIoTagInset, 96), kIoTagH, pal::panel);
	p.Frame(x, y, cw, ch, pal::hairline);
	PaintSkeleton(p, x + 14, y + imgH + 16, (cw - 28) * 62 / 100, 18);
	PaintSkeleton(p, x + 14, y + imgH + 46, (cw - 28) * 38 / 100, 12);
	PaintSkeleton(p, x + 14, y + imgH + 68, (cw - 28) * 28 / 100, 12);
}

void PaintRich(Painter &p, const State &st, int x, int y, int w, int h, const std::string &html, const std::string &plain,
	const std::string &noteTitle, const std::string &note, int baseFont)
{
	std::lock_guard<std::mutex> lock(g_rich.mx);
	const std::string src = (html.empty() ? "P" + plain : "H" + html) + '\x01' + noteTitle + '\x01' + note;
	if (src != g_rich.src) {
		g_rich.src = src;
		g_rich.list.scroll = 0;
		g_rich.blocks = html.empty() ? RichFromPlain(plain) : RichFromHtml(html);
		if (!note.empty()) {
			RichBlock gap;
			gap.kind = 5;
			if (!g_rich.blocks.empty()) g_rich.blocks.push_back(gap);
			RichBlock title;
			title.kind = 2;
			title.text = noteTitle;
			g_rich.blocks.push_back(title);
			const std::vector<RichBlock> nb = RichFromPlain(note);
			g_rich.blocks.insert(g_rich.blocks.end(), nb.begin(), nb.end());
		}
		g_rich.measured = RichMeasure();
	}
	g_rich.view = h;
	g_rich.rect = p.R(x, y, w, h);
	OffsetRect(&g_rich.rect, p.ox, p.oy);
	g_rich.rectValid = true;
	g_rich.rectTick = GetTickCount();
	if (g_rich.blocks.empty()) {
		p.Font(baseFont);
		p.Text(x, y, w, 20, "No description.", pal::textDim);
		g_rich.geom = ListGeom();
		return;
	}
	const int barW = 6, tw = w - barW - 8;
	RichMeasure measure;
	measure.width = tw;
	measure.baseFont = baseFont;
	measure.scale = p.s;
	measure.raw = p.raw;
	measure.fonts[0] = p.fonts[1];
	measure.fonts[1] = p.fonts[2];
	measure.fonts[2] = p.fonts[baseFont];
	if (!(g_rich.measured == measure)) {
		g_rich.pos.clear();
		int total = 0;
		for (const RichBlock &b : g_rich.blocks) {
			const int indent = b.kind == 3 ? 16 + b.indent * 14 : 0;
			const int font = b.kind == 1 ? 2 : b.kind == 2 || b.bold ? 1 : baseFont;
			int bh;
			if (b.kind == 4) bh = 9;
			else if (b.kind == 5) bh = 6;
			else {
				p.Font(font);
				bh = p.Measure(tw - indent, b.text);
			}
			const int before = (b.kind == 1 || b.kind == 2) && total > 0 ? 8 : 0;
			const int after = b.kind == 1 || b.kind == 2 ? 3 : b.kind == 3 ? 2 : 6;
			g_rich.pos.push_back({total + before, bh, indent});
			total += before + bh + after;
		}
		g_rich.total = total;
		g_rich.measured = measure;
	}
	const std::vector<RichPos> &pos = g_rich.pos;
	const int total = g_rich.total;
	const int maxScroll = std::max(0, total - h);
	g_rich.list.scroll = std::max(0, std::min(g_rich.list.scroll, maxScroll));
	const int scroll = g_rich.list.scroll;
	p.Clip(x, y, w, h);
	for (size_t i = 0; i < g_rich.blocks.size(); i++) {
		const RichBlock &b = g_rich.blocks[i];
		const RichPos &ps = pos[i];
		if (ps.y + ps.h < scroll || ps.y > scroll + h) continue;
		const int by = y + ps.y - scroll;
		if (b.kind == 4) {
			p.Fill(x, by + 4, tw, 1, pal::lineStrong);
			continue;
		}
		if (b.kind == 5) continue;
		const int font = b.kind == 1 ? 2 : b.kind == 2 || b.bold ? 1 : baseFont;
		const int top = std::max(by, y), bottom = std::min(by + ps.h, y + h);
		const bool hov = !b.link.empty() && bottom > top && p.Hover(x + ps.indent, top, tw - ps.indent, bottom - top, st.mouseX, st.mouseY);
		const COLORREF col = !b.link.empty() ? (hov ? RGB(180, 214, 255) : kLink) : b.kind == 1 || b.kind == 2 ? pal::redText : pal::text;
		p.Font(font);
		if (b.kind == 3) p.Text(x + ps.indent - 14, by, 14, ps.h, b.marker, pal::redText, DT_LEFT | DT_TOP | DT_SINGLELINE);
		p.TextWrapped(x + ps.indent, by, tw - ps.indent, ps.h + 2, b.text, col);
		if (!b.link.empty() && bottom > top) p.AddHit(x + ps.indent, top, tw - ps.indent, bottom - top, kRichLink, static_cast<int>(i));
	}
	p.Unclip();
	{
		ListGeom geom;
		ScrollbarPaint(p, x + w - barW, y, h, total, h, g_rich.list, geom, st.mouseX, st.mouseY);
		g_rich.geom = geom;
	}
}

RECT PaintIoImageFit(Painter &p, int x, int y, int w, int h, const std::string &url, bool big = false)
{
	const std::shared_ptr<const GbImage> img = big ? GbBigImage(url) : GbLogo(url);
	if (!img || img->width <= 0 || img->height <= 0) return {0, 0, 0, 0};
	int dw = w, dh = w * img->height / img->width;
	if (dh > h) {
		dh = h;
		dw = h * img->width / img->height;
	}
	const int dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
	p.Image(dx, dy, dw, dh, img->bgra.data(), img->width, img->height);
	return {dx, dy, dx + dw, dy + dh};
}

void PaintIoDetail(Painter &p, const State &st, const GbMod &m, int index)
{
	const int mx = st.mouseX, my = st.mouseY;
	const PanelLayout l = PanelLayoutFor(st);
	const int x = l.x, y = l.top, w = l.inner, h = l.cardBottom - l.top, pad = 24;
	PaintGlassCard(p, x, y, w, h);
	const int iw = 320, ih = 180;
	PaintIoImage(p, x + pad, y + pad, iw, ih, m.logoUrl);
	p.Frame(x + pad, y + pad, iw, ih, pal::lineStrong);
	const int dx = x + pad + iw + 32, dw = x + w - pad - dx;
	int ty = y + pad - 4;
	p.Font(2);
	p.Text(dx, ty, dw, 40, m.name, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	ty += 44;
	p.Font(0);
	p.Text(dx, ty, dw, 26, Tr("Author: ") + (m.author.empty() ? Tr("unknown") : m.author), pal::text);
	ty += 30;
	p.Font(3);
	p.Text(dx, ty, dw, 22, (m.entry.version.empty() ? Tr("No version") : Tr("Version ") + m.entry.version) +
		(m.entry.bytes ? "  ·  " + Tr("download") + " " + HumanBytes(m.entry.bytes) : ""), pal::textDim);
	ty += 24;
	p.Text(dx, ty, dw, 22, Thousands(m.downloads) + " " + Tr("downloads") + "  ·  " + Thousands(static_cast<uint64_t>(m.likes)) +
		" " + Tr("likes") + "  ·  " + Thousands(m.views) + " " + Tr("views"), pal::textDim);
	ty += 24;
	const std::string tags = IoTagsText(m);
	if (!tags.empty()) {
		ty += 4 + p.TextWrapped(dx, ty + 4, dw, 46, tags, pal::redText);
	}
	if (m.dependencies) {
		ty += 6;
		p.Text(dx, ty, dw, 22, "Needs other content: it is installed with it", pal::redText);
		ty += 22;
	}
	int descY = std::max(y + pad + ih, ty) + 16;
	p.Fill(x + pad, descY - 8, w - 2 * pad, 1, pal::hairline);
	if (m.shots.size() > 1) {
		const int tw = 176, th = 99, tgap = 12;
		const int perRow = std::max(1, (w - 2 * pad + tgap) / (tw + tgap));
		for (size_t i = 0; i < m.shots.size() && static_cast<int>(i) < perRow; i++) {
			const int sx = x + pad + static_cast<int>(i) * (tw + tgap);
			const bool hov = p.Hover(sx, descY, tw, th, mx, my);
			PaintIoImage(p, sx, descY, tw, th, m.shots[i].thumb);
			p.Frame(sx, descY, tw, th, hov ? pal::edgeHot : pal::lineStrong);
			p.AddHit(sx, descY, tw, th, kIoShotOpen, static_cast<int>(i));
		}
		descY += th + 16;
		p.Fill(x + pad, descY - 8, w - 2 * pad, 1, pal::hairline);
	}
	const int bh = kBarH, by = y + h - pad - bh;
	std::string notes, noteVersion;
	if (m.id) GbChangelog(m.id, "", notes, noteVersion);
	PaintRich(p, st, x + pad, descY, w - 2 * pad, by - 16 - descY, m.descriptionHtml, !m.description.empty() ? m.description : m.summary,
		Tr("What's new in version ") + noteVersion, notes, 0);
	const int state = IoInstalledState(st, m);
	const std::string label = !m.hasFile ? "No file" : state == 1 ? "Already installed" : state == 2 ? Tr("Update to ") + m.entry.version : "Install";
	const int backW = 180, pageW = 260, installW = 260, versionsW = 240, gap = 12;
	const int backX = x + w - pad - backW, pageX = backX - gap - pageW, installX = pageX - gap - installW, versionsX = installX - gap - versionsW;
	p.Button(installX, by, installW, bh, label, kIoInstall, index, mx, my, m.hasFile && state != 1 && !st.dl.active, true);
	Combo(p, st, versionsX, by, versionsW, bh, "Other versions", kDropVersions);
	p.Button(pageX, by, pageW, bh, "View on GameBanana", kIoPage, index, mx, my, !m.page.empty());
	p.Button(backX, by, backW, bh, "Back", kIoBack, 0, mx, my);
}

void PaintShotViewer(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	if (st.tab != 2 || st.ioDetail < 0 || st.ioDetail >= static_cast<int>(st.io.mods.size())) return;
	const GbMod &m = st.io.mods[st.ioDetail];
	if (st.ioShot >= 0 && st.ioShot < static_cast<int>(m.shots.size())) {
		const int n = static_cast<int>(m.shots.size());
		const int vw = st.viewW, vh = st.viewH, margin = PanelLayoutFor(st).margin;
		p.hits->clear();
		p.AddHit(0, 0, vw, vh, kIoShotClose, 0);
		p.Fill(0, 0, vw, vh, pal::bg);
		p.Font(2);
		p.Text(margin, margin - 4, vw - 2 * margin - 300, 44, m.name, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Button(vw - margin - 140, margin, 140, kBarH, "Close", kIoShotClose, 0, mx, my);
		if (n > 1) {
			const int cw = 96, cx = vw - margin - 140 - 12 - cw;
			p.Fill(cx, margin, cw, kBarH, pal::raised);
			p.Frame(cx, margin, cw, kBarH, pal::hairline);
			p.Font(3);
			p.Text(cx, margin, cw, kBarH, std::to_string(st.ioShot + 1) + " / " + std::to_string(n), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		p.Fill(margin, margin + kBarH + 12, vw - 2 * margin, 1, pal::lineStrong);
		const int tw = 140, th = 79, tgap = 12;
		const int ix = margin, iy = margin + kBarH + 28, iw = vw - 2 * margin, ih = vh - margin - th - 20 - iy;
		const GbShot &shot = m.shots[st.ioShot];
		RECT r = PaintIoImageFit(p, ix, iy, iw, ih, shot.big, true);
		const bool loaded = r.right > r.left;
		if (!loaded) {
			p.Fill(ix, iy, iw, ih, pal::bg);
			r = PaintIoImageFit(p, ix, iy, iw, ih, shot.thumb);
		}
		if (r.right > r.left) {
			p.Frame(r.left - 1, r.top - 1, r.right - r.left + 2, r.bottom - r.top + 2, pal::edge);
			const int half = (r.left + r.right) / 2;
			if (n > 1) {
				p.AddHit(r.left, r.top, half - r.left, r.bottom - r.top, kIoShotStep, -1);
				p.AddHit(half, r.top, r.right - half, r.bottom - r.top, kIoShotStep, 1);
			}
		}
		if (loaded) SpinnerDone("B|" + shot.big);
		if (!loaded && SpinnerDue("B|" + shot.big)) {
			const int box = 80, bx = ix + (iw - box) / 2, by = iy + (ih - box) / 2;
			p.Fill(bx, by, box, box, pal::bg);
			p.Frame(bx, by, box, box, pal::hairline);
			p.Spinner(bx + box / 2, by + box / 2, 22, 8, GetTickCount());
		}
		if (n > 1) {
			const int bw = 52, bh = 116, by = iy + ih / 2 - bh / 2;
			const COLORREF arrow = pal::body;
			p.Button(ix + 12, by, bw, bh, "", kIoShotStep, -1, mx, my);
			p.ArrowLeft(ix + 12 + bw / 2 - 8, by + bh / 2 - 13, 16, 26, arrow);
			p.Button(ix + iw - 12 - bw, by, bw, bh, "", kIoShotStep, 1, mx, my);
			p.ArrowRight(ix + iw - 12 - bw + bw / 2 - 8, by + bh / 2 - 13, 16, 26, arrow);
		}
		const int shown = std::min(n, std::max(1, (vw - 2 * margin + tgap) / (tw + tgap)));
		const int stripW = shown * tw + (shown - 1) * tgap;
		const int tx0 = (vw - stripW) / 2, ty = iy + ih + 20;
		for (int i = 0; i < shown; i++) {
			const int sx = tx0 + i * (tw + tgap);
			const bool cur = i == st.ioShot, hov = p.Hover(sx, ty, tw, th, mx, my);
			PaintIoImage(p, sx, ty, tw, th, m.shots[i].thumb);
			p.Frame(sx, ty, tw, th, cur ? pal::body : hov ? pal::edgeHot : pal::lineStrong);
			if (cur) p.Frame(sx + 1, ty + 1, tw - 2, th - 2, pal::body);
			p.AddHit(sx, ty, tw, th, kIoShotOpen, i);
		}
	}
}
}
}

namespace modui_impl {
void PaintFacetPanel(Painter &p, const PanelLayout &l, const CatalogFilter &f, const CatalogCounts &counts, int toggleAction, int clearAction, int closeAction, int mx, int my)
{
	struct Chip {
		int arg, x, y, w;
		std::string text;
		bool on;
		bool enabled;
	};
	struct Line {
		const char *label;
		int y;
		const char *empty;
	};
	const int x = l.x, y = l.top + kBarH + 12, w = l.inner, labelW = 260, chipH = 36, rowStep = 44;
	std::vector<Chip> chips;
	std::vector<Line> lines;
	int cy = y + 24;
	p.Font(0);
	const auto addLine = [&](const char *label, int group, uint32_t mask, const std::vector<std::pair<int, std::string>> &items, const char *empty,
							 uint32_t disabled = 0) {
		lines.push_back({label, cy, items.empty() ? empty : nullptr});
		int cx = x + labelW;
		for (const auto &it : items) {
			const int cw = std::max(44, p.TextWidth(it.second) + 36);
			if (cx + cw > x + w - 24) {
				cx = x + labelW;
				cy += rowStep;
			}
			chips.push_back({group * kIoFilterGroupStride + it.first, cx, cy, cw, it.second, ((mask >> it.first) & 1) != 0, !((disabled >> it.first) & 1)});
			cx += cw + 10;
		}
		cy += rowStep + 10;
	};
	static const char *const kGroupNames[] = {"Gameplay", "Maps and campaigns", "Cosmetic"};
	bool players = false, language = false;
	for (int g = 0; g < 3; g++) {
		std::vector<std::pair<int, std::string>> items;
		uint32_t empty = 0;
		for (int c = g * 3; c < g * 3 + 3; c++) {
			const bool on = (f.types >> c) & 1;
			if (!counts.types[c] && !on) empty |= 1u << c;
			items.push_back({c, Tr(CatalogCategoryLabel(c)) + " (" + std::to_string(counts.types[c]) + ")"});
			players = players || (on && CatalogHasPlayers(c));
			language = language || (on && CatalogHasLanguage(c));
		}
		addLine(kGroupNames[g], kIoFilterTypes, f.types, items, nullptr, empty);
	}
	if (players) {
		std::vector<std::pair<int, std::string>> items;
		for (int n = kCatalogMinPlayers; n <= kCatalogMaxPlayers; n++) {
			if (counts.players[n] || ((f.players >> n) & 1)) items.push_back({n, std::to_string(n)});
		}
		addLine("Players", kIoFilterPlayers, f.players, items, "No map says how many players it is for yet");
	}
	if (language) {
		std::vector<std::pair<int, std::string>> items;
		for (int l = 0; l < kLangCount; l++) {
			if (CatalogLanguageCount(counts, f.types, l) || ((f.languages >> l) & 1)) items.push_back({l, Tr(CatalogLanguageLabel(l))});
		}
		addLine("Language", kIoFilterLanguages, f.languages, items, "None of them says its language yet");
	}
	const int h = cy - y + 60;
	p.hits->clear();
	p.AddHit(0, 0, l.w, l.h, closeAction, 0);
	p.Fill(x + 4, y + 4, w, h, pal::shadow);
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, pal::edgeHot);
	p.AddHit(x, y, w, h, kNone, 0);
	for (const Line &line : lines) {
		p.Font(1);
		p.Text(x + 24, line.y, labelW - 32, chipH, line.label, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (line.empty) {
			p.Font(3);
			p.Text(x + labelW, line.y, w - labelW - 24, chipH, line.empty, pal::textDim);
		}
	}
	for (const Chip &c : chips) {
		const bool hov = c.enabled && p.Hover(c.x, c.y, c.w, chipH, mx, my);
		p.Fill(c.x, c.y, c.w, chipH, c.on ? (hov ? pal::red : pal::red) : (hov ? pal::button : pal::field));
		p.Frame(c.x, c.y, c.w, chipH, c.on ? pal::text : hov ? pal::edgeHot : c.enabled ? pal::edge : pal::lineStrong);
		p.Font(c.on ? 1 : 0);
		p.Text(c.x, c.y, c.w, chipH, c.text, c.on ? pal::text : c.enabled ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (c.enabled) p.AddHit(c.x, c.y, c.w, chipH, toggleAction, c.arg);
	}
	const int n = CatalogFilterSize(f);
	p.Button(x + w - 404, y + h - 56, 200, kBarH, "Clear filters", clearAction, 0, mx, my, n > 0);
	p.Button(x + w - 196, y + h - 56, 172, kBarH, "Close", closeAction, 0, mx, my, true, true);
}

void PaintFilterPanel(Painter &p, const State &st)
{
	if (!st.ioFilterOpen || st.tab != 2) return;
	PaintFacetPanel(p, PanelLayoutFor(st), st.ioFilter, st.io.counts, kIoFilterToggle, kIoFilterClear, kIoFilterClose, st.mouseX, st.mouseY);
}
}

namespace modui_impl {
namespace {
void PaintExplore(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GbResults &io = st.io;
	if (st.ioDetail >= 0 && st.ioDetail < static_cast<int>(io.mods.size())) {
		PaintIoDetail(p, st, io.mods[st.ioDetail], st.ioDetail);
		return;
	}
	const PanelLayout l = PanelLayoutFor(st);
	const int nFilters = CatalogFilterSize(st.ioFilter);
	const int y0 = l.top, gap = 12, filtersW = 200, sortW = 240, refreshW = 160;
	const int refreshX = l.x + l.inner - refreshW, sortX = refreshX - gap - sortW, filtersX = sortX - gap - filtersW;
	PaintField(p, st, l.x, y0, filtersX - gap - l.x, kBarH, st.ioSearch, "Search by name, author or category", st.focus == kIoSearchField, false, kIoFocusSearch, mx, my);
	p.Button(filtersX, y0, filtersW, kBarH, nFilters ? Tr("Filters (") + std::to_string(nFilters) + ")" : "Filters", kIoFilterOpen, 0, mx, my, true, nFilters > 0, 0);
	Combo(p, st, sortX, y0, sortW, kBarH, kIoSortNames[st.ioSort], kDropIoSort);
	p.Button(refreshX, y0, refreshW, kBarH, st.ioRefreshing && io.loading ? "Updating..." : "Refresh", kIoRefresh, 0, mx, my, !io.loading, false, 0);

	const int gx = l.x, gy = y0 + kBarH + 16, gw = l.inner, gh = l.cardBottom - gy, cardGap = 16;
	// A page of kIoPerPage in 4 columns and 2 rows; the pictures keep the shape of GameBanana's cards and the text takes the rest.
	const int cw = (gw - 3 * cardGap) / 4, ch = (gh - cardGap) / 2;
	const int imgH = std::max(60, ch - 96), imgW = std::min(cw - 2, imgH * 235 / 152);
	// A new search, page or filter shows the shape of the cards until they arrive; refreshing keeps the ones on screen meanwhile.
	if ((io.loading && (!st.ioRefreshing || io.mods.empty())) || (io.mods.empty() && !st.ioRequested)) {
		for (int i = 0; i < kIoPerPage; i++) {
			PaintSkeletonCard(p, gx + (i % 4) * (cw + cardGap), gy + (i / 4) * (ch + cardGap), cw, ch, imgW, imgH);
		}
		return;
	}
	if (io.mods.empty()) {
		PaintGlassCard(p, gx, gy, gw, gh);
		p.Font(0);
		std::string text = !io.error.empty() ? Tr("Could not connect to GameBanana: ") + Tr(io.error)
			: !st.ioSearch.empty() || nFilters ? "No content matches the search."
												: "There is no content published for Imperivm on GameBanana yet. Be the first to upload some!";
		p.TextWrapped(gx + 28, gy + 28, gw - 56, 80, text, io.error.empty() ? pal::textDim : pal::bad);
		if (!io.error.empty() && !io.loading) p.Button(gx + 28, gy + 110, 200, kBarH, "Retry", kIoRetry, 0, mx, my);
		return;
	}
	for (size_t i = 0; i < io.mods.size() && i < static_cast<size_t>(kIoPerPage); i++) {
		const GbMod &m = io.mods[i];
		const int x = gx + static_cast<int>(i % 4) * (cw + cardGap), y = gy + static_cast<int>(i / 4) * (ch + cardGap);
		const bool hov = p.Hover(x, y, cw, ch, mx, my);
		PaintGlassCard(p, x, y, cw, ch);
		if (hov) p.Fill(x + 1, y + 1, cw - 2, ch - 2, pal::field);
		const int imgX = x + (cw - imgW) / 2, imgY = y + 1;
		p.Fill(x + 1, imgY, cw - 2, imgH, pal::panel);
		PaintIoImage(p, imgX, imgY, imgW, imgH, m.logoUrl);
		p.Frame(x, y, cw, ch, hov ? pal::edgeHot : pal::hairline);
		const std::string catText = m.facts.category >= 0 ? Tr(CatalogCategoryLabel(m.facts.category)) : m.category;
		if (!catText.empty()) {
			p.Font(4);
			const std::string cat = UpperText(catText);
			const int chipW = std::min(imgW - 2 * kIoTagInset, p.TextWidth(cat) + 20), chipX = imgX + kIoTagInset, chipY = imgY + imgH - kIoTagH - kIoTagInset;
			p.Fill(chipX, chipY, chipW, kIoTagH, pal::panel);
			p.Text(chipX + 10, chipY, chipW - 14, kIoTagH, cat, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		const int state = IoInstalledState(st, m);
		if (state == 1 || state == 2) {
			p.Font(4);
			const std::string badge = UpperText(Tr(state == 1 ? "Already installed" : "Update available"));
			const int badgeW = p.TextWidth(badge) + 20, badgeX = imgX + imgW - kIoTagInset - badgeW, badgeY = imgY + kIoTagInset;
			p.Fill(badgeX, badgeY, badgeW, kIoTagH, pal::panel);
			p.Text(badgeX, badgeY, badgeW, kIoTagH, badge, state == 1 ? pal::ok : pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		p.Font(1);
		p.Text(x + 14, y + imgH + 12, cw - 28, 26, UpperText(m.name), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(x + 14, y + imgH + 42, cw - 28, 20, Tr("By") + " " + m.author, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(x + 14, y + imgH + 64, cw - 28, 20, Thousands(m.downloads) + " " + Tr(m.downloads == 1 ? "download" : "downloads"), pal::textDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.AddHit(x, y, cw, ch, kIoOpen, static_cast<int>(i));
	}
}

const int kProfileComboW = 260, kProfileNameW = 300, kProfileButtonW = 130, kProfileGap = 8;

int ProfileExtraWidth(Painter &p, const State &st)
{
	if (st.profile.name == kNoProfileName) {
		p.Font(1);
		return p.CapsWidth("Save as profile", 1) + 44;
	}
	return st.profile.name != kOriginalName ? 44 : 0;
}

// The profile being edited, at the start of the tools: its menu, or its name while it is named. Returns where it ends.
int PaintProfileBar(Painter &p, const State &st, int x, int y, int h)
{
	const int mx = st.mouseX, my = st.mouseY, gap = kProfileGap;
	p.Font(3);
	const int labelW = p.TextWidth("Profile") + 14;
	p.Text(x, y, labelW, h, "Profile", pal::caption);
	x += labelW;
	if (st.nameMode != kNameNone) {
		const char *what = st.nameMode == kNameRename ? "New name" : "Profile name";
		PaintField(p, st, x, y, kProfileNameW, h, st.nameText, what, st.focus == kNameField, false, kFocusName, mx, my);
		x += kProfileNameW + gap;
		p.Button(x, y, kProfileButtonW, h, st.nameMode == kNameRename ? "Rename" : "Save", kNameOk, 0, mx, my, true, true, 0);
		x += kProfileButtonW + gap;
		p.Button(x, y, kProfileButtonW, h, "Cancel", kNameCancel, 0, mx, my, true, false, 0);
		return x + kProfileButtonW;
	}
	Combo(p, st, x, y, kProfileComboW, h, ModsProfileLabel(st.profile.name), kDropProfile);
	x += kProfileComboW;
	const int extraW = ProfileExtraWidth(p, st);
	if (st.profile.name == kNoProfileName) {
		p.Button(x + gap, y, extraW, h, "Save as profile", kSaveAs, 0, mx, my, !st.profile.mods.empty());
		x += gap + extraW;
	}
	else if (extraW) {
		p.Button(x + gap, y, extraW, h, "...", kDropOpen, kDropMenu, mx, my);
		if (st.drop == kDropMenu) g_drop.anchor = {x + gap, y, x + gap + extraW, y + h};
		x += gap + extraW;
	}
	return x;
}

// The packs with an update, counted again when the list or a download changes and at least once a second: each check reads the
// GameBanana sources from disk, too slow for every frame, and the catalog it compares with can arrive at any time.
struct UpdateCount {
	bool counted = false;
	uint64_t listSerial = 0, dlFinished = 0;
	DWORD refreshed = 0, at = 0;
	size_t mods = 0;
	int count = 0;
};
UpdateCount g_updateCount;

int CountUpdates(const State &st)
{
	UpdateCount &c = g_updateCount;
	const DWORD now = GetTickCount();
	if (c.counted && c.listSerial == st.listSerial && c.dlFinished == st.dlFinished && c.refreshed == st.lastRefresh && c.mods == st.mods.size() &&
		now - c.at < 1000) {
		return c.count;
	}
	int n = 0;
	for (const ModInfo &m : st.mods) {
		CatalogEntry e;
		if (FindUpdateFor(st, m, e)) n++;
	}
	c.counted = true;
	c.listSerial = st.listSerial;
	c.dlFinished = st.dlFinished;
	c.refreshed = st.lastRefresh;
	c.at = now;
	c.mods = st.mods.size();
	c.count = n;
	return n;
}

// The bolt of the shared files, as a polygon in a box 12 wide and 16 tall.
bool InBolt(double x, double y)
{
	static const double px[] = {7.5, 1.5, 6.0, 5.0, 11.5, 7.0}, py[] = {1.0, 9.0, 9.0, 15.0, 6.5, 6.5};
	const double tx = x * 12.0, ty = y * 16.0;
	bool in = false;
	for (int i = 0, j = 5; i < 6; j = i++) {
		if ((py[i] > ty) != (py[j] > ty) && tx < (px[j] - px[i]) * (ty - py[i]) / (py[j] - py[i]) + px[i]) in = !in;
	}
	return in;
}

// The text tabs of the card, on one line from `x`, underlined in red when on.
int PaintCardTabs(Painter &p, const State &st, int x, int y, int w, const std::vector<std::pair<std::string, int>> &tabs)
{
	const int h = 40;
	p.Fill(x, y + h, w, 1, pal::hairline);
	int tx = x;
	for (size_t k = 0; k < tabs.size(); k++) {
		const bool on = st.cardTab == static_cast<int>(k);
		std::string label = Tr(tabs[k].first);
		if (tabs[k].second > 0) label += " (" + std::to_string(tabs[k].second) + ")";
		p.Font(4);
		const int tw = p.CapsWidth(label, 1) + 8;
		const bool hov = p.Hover(tx, y, tw, h, st.mouseX, st.mouseY);
		p.TextCaps(tx, y, tw, h, label, on || hov ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
		if (on) p.Fill(tx, y + h - 3, tw - 8, 3, pal::red);
		p.AddHit(tx, y, tw, h, kCardTab, static_cast<int>(k));
		tx += tw + 24;
	}
	return y + h + 1;
}

int GroupOf(const Row &r, const ModInfo *m)
{
	return !r.active || !m ? 3 : std::max(0, std::min(2, m->kind));
}

const char *GroupNote(int group)
{
	switch (group) {
	case kModCosmetic: return "higher up: they win when they clash";
	case kModRules: return "everyone in the game must have it";
	case 3: return "they don't load";
	default: return "";
	}
}

std::string AuthorsOf(const ModInfo &m)
{
	std::string by;
	for (size_t i = 0; i < m.authors.size(); i++) by += (i ? ", " : "") + m.authors[i];
	return by;
}

// Where the Details tab of the card puts the pack in the load order, and why.
int PaintOrderBox(Painter &p, const State &st, const ModOrderEntry &e, int x, int y, int w)
{
	p.Font(3);
	const std::string why = OrderReasonText(st, e);
	const int textW = w - 96;
	const int textH = std::min(40, p.Measure(textW, why));
	const int h = std::max(64, 22 + 22 + textH + 4);
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::hairline);
	p.Font(2);
	p.Text(x + 10, y, 70, h, "#" + std::to_string(e.position), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Font(1);
	p.Text(x + 86, y + 10, textW, 22, "Position in the load order", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.TextWrapped(x + 86, y + 34, textW, textH + 2, why, pal::textDim);
	return y + h;
}

// The summary of what the pack shares, with the way to its Conflicts tab.
int PaintClashBox(Painter &p, const State &st, const ClashSummary &c, int x, int y, int w)
{
	std::string text;
	if (c.problem) text = TrFmt("It changes the same gameplay files as %s: only one of the two can be active.", {c.problemWith});
	else if (c.wins || c.loses) {
		text = TrFmt("It shares %s files with %s packs: it wins %s and loses %s.",
			{std::to_string(c.wins + c.loses), std::to_string(c.pairs), std::to_string(c.wins), std::to_string(c.loses)});
	}
	else text = TrFmt("It changes %s values that other packs change too.", {std::to_string(c.values)});
	p.Font(3);
	const int textW = w - 46;
	const int textH = std::min(40, p.Measure(textW, text));
	const int h = 12 + textH + 6 + 22 + 10;
	p.Fill(x, y, w, h, c.problem ? RGB(0x21, 0x15, 0x16) : pal::raised);
	p.Frame(x, y, w, h, c.problem ? RGB(0x6e, 0x20, 0x24) : pal::hairline);
	if (c.problem) PaintAlertIcon(p, x + 12, y + 13, true, 16);
	else PaintBolt(p, x + 14, y + 13, 16, c.loses || c.values ? pal::warn : pal::ok);
	p.TextWrapped(x + 36, y + 12, textW, textH + 2, text, pal::text);
	const std::string link = Tr("See the conflicts");
	p.Font(4);
	const int lw = p.CapsWidth(link, 1) + 8;
	const int lx = x + w - 14 - lw, ly = y + 12 + textH + 6;
	const bool hov = p.Hover(lx, ly, lw, 22, st.mouseX, st.mouseY);
	p.TextCaps(lx, ly, lw, 22, link, hov ? pal::text : pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
	p.AddHit(lx, ly, lw, 22, kCardTab, kCardConflicts);
	return y + h;
}

bool HasFolderIn(const std::vector<std::string> &list, const std::string &folder)
{
	for (const std::string &f : list) {
		if (_stricmp(f.c_str(), folder.c_str()) == 0) return true;
	}
	return false;
}

// The Conflicts tab: each pack it shares files with, problems first. Gameplay is settled by the order; cosmetic files are chosen
// for the whole pair or one by one.
void PaintCardConflicts(Painter &p, const State &st, const ModInfo &m, bool active, const ModIssues *is, int dx, int y, int dw, int bottom)
{
	const int mx = st.mouseX, my = st.mouseY;
	if (!active || !is || (is->overlaps.empty() && is->values.empty())) {
		p.Font(0);
		p.TextWrapped(dx, y, dw, 80, active ? "It shares no files or values with the other active packs." : "Disabled: it doesn't load, so it clashes with nothing.",
			pal::textDim);
		return;
	}
	p.Font(3);
	y += p.TextWrapped(dx, y, dw, 60, "What changes the game is settled by the load order; what is cosmetic, you choose.", pal::textDim) + 8;
	const int footH = 40, limit = bottom - footH - 12, pad = 12, inner = dw - 2 * pad;
	std::vector<size_t> order;
	for (int pass = 0; pass < 3; pass++) {
		for (size_t k = 0; k < is->overlaps.size(); k++) {
			const ModOverlap &ov = is->overlaps[k];
			const int rank = ov.problem ? 0 : ov.choosable && (ov.canUseMine || ov.canUseTheirs) ? 1 : 2;
			if (rank == pass) order.push_back(k);
		}
	}
	bool anyChosen = false;
	size_t shown = 0;
	for (const ModOverlap &ov : is->overlaps) anyChosen = anyChosen || ov.chosen > 0;
	for (const ModOverlapFile &f : is->files) anyChosen = anyChosen || f.fileChosen;
	for (size_t k : order) {
		const ModOverlap &ov = is->overlaps[k];
		const bool choose = !ov.problem && ov.choosable && (ov.canUseMine || ov.canUseTheirs);
		const std::string winner = ov.mine >= ov.theirs ? m.name : ov.name;
		std::string text;
		if (ov.problem) {
			text = Tr("Both change the same gameplay files. Online everyone must play with the same thing, so it can't be chosen: disable one, or "
					  "put them in different profiles.");
		}
		else if (choose) text = TrFmt("By order, the files of %s are used. Choose for the whole pack or file by file:", {winner});
		else if (ov.choosable) text = TrFmt("They are inside .pak files and can't be split: %s wins.", {winner});
		else if (ov.declared) text = TrFmt("Their authors set the order: %s wins.", {winner});
		else text = TrFmt("The load order decides: %s wins.", {winner});
		p.Font(3);
		const int textH = std::min(60, p.Measure(inner, text));
		std::vector<size_t> files;
		if (choose) {
			for (size_t fi = 0; fi < is->files.size(); fi++) {
				const ModOverlapFile &f = is->files[fi];
				if (HasFolderIn(f.folders, m.folder) && HasFolderIn(f.folders, ov.folder)) files.push_back(fi);
			}
		}
		const int fileH = 30;
		int extra = ov.problem ? 40 : choose ? 42 : 0;
		const int fixedH = pad + 26 + 4 + textH + extra + pad;
		int fitFiles = 0;
		if (choose) {
			const int room = limit - y - fixedH - 26;
			fitFiles = std::max(0, std::min(static_cast<int>(files.size()), room / fileH));
			if (fitFiles > 4) fitFiles = 4;
		}
		const bool moreFiles = static_cast<int>(files.size()) > fitFiles;
		const int boxH = fixedH + fitFiles * fileH + (choose && moreFiles ? 26 : 0);
		if (y + boxH > limit) break;
		const COLORREF frame = ov.problem ? RGB(0x6e, 0x20, 0x24) : pal::hairline;
		p.Fill(dx, y, dw, boxH, ov.problem ? RGB(0x21, 0x15, 0x16) : pal::raised);
		p.Frame(dx, y, dw, boxH, frame);
		int by = y + pad;
		if (ov.problem) PaintAlertIcon(p, dx + pad, by + 5, true, 16);
		else if (choose) PaintBolt(p, dx + pad + 2, by + 5, 16, pal::warn);
		else PaintPadlock(p, dx + pad + 1, by + 4, 17, pal::caption);
		std::string tag = ov.gameplay > 0 ? TrFmt("Gameplay · %s files", {std::to_string(ov.files)})
			: ov.choosable			   ? TrFmt("Cosmetic · %s files", {std::to_string(ov.files)})
									   : TrFmt("%s files", {std::to_string(ov.files)});
		p.Font(4);
		const int tagW = std::min(inner / 2, p.CapsWidth(tag, 1) + 16);
		const int tagX = dx + dw - pad - tagW;
		p.Frame(tagX, by + 2, tagW, 22, ov.problem ? frame : pal::edge);
		p.TextCaps(tagX, by + 2, tagW, 22, tag, ov.problem ? pal::bad : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.Font(1);
		p.Text(dx + pad + 26, by, tagX - 10 - (dx + pad + 26), 26, ov.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		by += 30;
		p.Font(3);
		p.TextWrapped(dx + pad, by, inner, textH + 2, text, ov.problem ? pal::body : pal::textDim);
		by += textH;
		if (ov.problem) {
			const int half = (inner - 8) / 2;
			p.Button(dx + pad, by + 8, half, 32, TrFmt("Disable %s", {ov.name}), kDisableOther, static_cast<int>(k), mx, my, !st.busy, false, 0);
			p.Button(dx + pad + half + 8, by + 8, inner - half - 8, 32, "Disable this one", kDisableOther, -1, mx, my, !st.busy, false, 0);
		}
		else if (choose) {
			const int half = inner / 2;
			const bool theirsOn = ov.theirs > 0 && ov.mine == 0, mineOn = ov.mine > 0 && ov.theirs == 0;
			PaintSeg(p, dx + pad, by + 8, half, 34, TrFmt("The files of %s", {ov.name}), theirsOn, ov.canUseTheirs && !st.busy, kUseFiles,
				static_cast<int>(k) * 2 + 1, mx, my);
			PaintSeg(p, dx + pad + half, by + 8, inner - half, 34, "The files of this pack", mineOn, ov.canUseMine && !st.busy, kUseFiles,
				static_cast<int>(k) * 2, mx, my);
			by += 42;
			const int segW = std::min(200, inner / 2), pathW = inner - segW - 10;
			for (int n = 0; n < fitFiles; n++) {
				const size_t fi = files[static_cast<size_t>(n)];
				const ModOverlapFile &f = is->files[fi];
				p.Fill(dx + pad, by, inner, 1, pal::hairline);
				p.Font(3);
				p.Text(dx + pad, by, pathW, fileH, f.path, f.fileChosen ? pal::warn : pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				if (f.fileChosen && p.Hover(dx + pad, by, pathW, fileH, mx, my)) SetTip("Your choice for this file", dx + pad, by + fileH);
				const int sx = dx + pad + inner - segW, sh = 24, sy = by + (fileH - sh) / 2;
				if (f.choosable) {
					const bool mineUsed = _stricmp(f.used.c_str(), m.folder.c_str()) == 0;
					PaintSeg(p, sx, sy, segW / 2, sh, ov.name, !mineUsed, !st.busy, kUseFile, UseFileArg(static_cast<int>(k), static_cast<int>(fi), false), mx, my);
					PaintSeg(p, sx + segW / 2, sy, segW - segW / 2, sh, "This one", mineUsed, !st.busy, kUseFile,
						UseFileArg(static_cast<int>(k), static_cast<int>(fi), true), mx, my);
				}
				else {
					p.Text(sx, by, segW, fileH, "inside a .pak", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
				}
				by += fileH;
			}
			if (moreFiles) {
				p.Font(3);
				p.Text(dx + pad, by, inner / 2, 26, TrFmt("%s more files", {std::to_string(files.size() - static_cast<size_t>(fitFiles))}), pal::caption);
				const std::string link = Tr("See them all");
				p.Font(4);
				const int lw = p.CapsWidth(link, 1) + 8;
				const int lx = dx + pad + inner - lw;
				const bool hov = p.Hover(lx, by, lw, 26, mx, my);
				p.TextCaps(lx, by, lw, 26, link, hov ? pal::text : pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
				p.AddHit(lx, by, lw, 26, kDropOpen, kDropClashFiles);
				if (st.drop == kDropClashFiles) g_drop.anchor = {dx, by, dx + dw, by + 26};
			}
		}
		y += boxH + 10;
		shown++;
	}
	if (shown < order.size()) {
		p.Font(3);
		p.Text(dx, y, dw, 22, TrFmt("And %s more packs: free up room by disabling some.", {std::to_string(order.size() - shown)}), pal::caption);
		y += 26;
	}
	if (!is->values.empty() && y + 70 < limit) {
		const std::string head = TrFmt("Shared values (%s)", {std::to_string(is->values.size())});
		const std::string note = is->notices.empty() ? std::string() : is->notices[0];
		p.Font(3);
		const int noteH = note.empty() ? 0 : std::min(40, p.Measure(inner, note));
		const int boxH = pad + 24 + noteH + pad;
		p.Fill(dx, y, dw, boxH, pal::raised);
		p.Frame(dx, y, dw, boxH, pal::hairline);
		PaintBolt(p, dx + pad + 2, y + pad + 4, 16, pal::warn);
		p.Font(1);
		p.Text(dx + pad + 26, y + pad, inner - 26, 24, head, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		if (!note.empty()) p.TextWrapped(dx + pad, y + pad + 24, inner, noteH + 2, note, pal::textDim);
	}
	const int fy = bottom - footH;
	p.Fill(dx, fy - 8, dw, 1, pal::hairline);
	const int resetW = 220;
	p.Font(3);
	p.TextWrapped(dx, fy, dw - resetW - 12, footH, "What you choose only changes what you see; it doesn't affect online games.", pal::caption);
	p.Button(dx + dw - resetW, fy + 2, resetW, 36, "Back to the order", kClearChoices, 0, mx, my, anyChosen && !st.busy, false, 0);
}

// The Files tab: the files the pack shares with others and whose copy is used.
void PaintCardFiles(Painter &p, const State &st, const ModInfo &m, const ModIssues *is, int dx, int y, int dw, int bottom)
{
	const int mx = st.mouseX, my = st.mouseY;
	const size_t shared = is ? is->files.size() : 0;
	p.Font(3);
	p.Text(dx, y, dw, 22, TrFmt("%s files · %s shared with other packs", {std::to_string(m.fileCount), std::to_string(shared)}), pal::textDim);
	y += 30;
	const int btnH = 36, rowH = 30, limit = bottom - btnH - 16;
	if (!shared) {
		p.Font(0);
		p.TextWrapped(dx, y, dw, 60, "None of its files are shared with the other active packs: all of them are used.", pal::textDim);
	}
	else {
		const int nameW = std::min(200, dw / 3), pathW = dw - nameW - 12;
		p.Font(4);
		p.TextCaps(dx, y, pathW, 22, "File", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
		p.TextCaps(dx + dw - nameW, y, nameW, 22, "Copy used", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
		y += 26;
		const int fit = std::max(0, (limit - y - 26) / rowH);
		size_t n = 0;
		for (; n < shared && static_cast<int>(n) < fit; n++) {
			const ModOverlapFile &f = is->files[n];
			const bool mine = _stricmp(f.used.c_str(), m.folder.c_str()) == 0;
			p.Fill(dx, y, dw, 1, pal::hairline);
			p.Font(3);
			p.Text(dx, y, pathW, rowH, f.path, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Text(dx + dw - nameW, y, nameW, rowH, mine ? Tr("this pack") : f.usedName, mine ? pal::ok : pal::warn,
				DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += rowH;
		}
		if (n < shared) {
			p.Font(3);
			p.Text(dx, y, dw / 2, 26, TrFmt("And %s more", {std::to_string(shared - n)}), pal::caption);
			const std::string link = Tr("See them all");
			p.Font(4);
			const int lw = p.CapsWidth(link, 1) + 8;
			const bool hov = p.Hover(dx + dw - lw, y, lw, 26, mx, my);
			p.TextCaps(dx + dw - lw, y, lw, 26, link, hov ? pal::text : pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
			p.AddHit(dx + dw - lw, y, lw, 26, kDropOpen, kDropClashFiles);
			if (st.drop == kDropClashFiles) g_drop.anchor = {dx, y, dx + dw, y + 26};
		}
	}
	p.Button(dx, bottom - btnH, dw, btnH, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
}
}
}

namespace modui_impl {
PanelLayout PanelLayoutFor(const State &st)
{
	PanelLayout l;
	static_cast<ScreenLayout &>(l) = ScreenLayoutFor(st.viewW, st.viewH, false);
	l.x = l.margin;
	l.inner = l.w - 2 * l.margin;
	l.toolsY = l.top;
	l.cardTop = l.top + kBarH + 16;
	l.footY = l.h - l.margin - kFootH;
	l.cardBottom = st.tab == kTabListExplore ? l.footY - 24 : l.h - l.margin;
	l.detailW = std::max(480, std::min(640, l.inner * 8 / 25));
	l.listW = l.inner - l.detailW - 32;
	l.detailX = l.x + l.listW + 32;
	return l;
}

void PaintBolt(Painter &p, int x, int y, int h, COLORREF c)
{
	p.Shape(x, y, std::max(1, h * 12 / 16), h, InBolt, c);
}

void PaintPadlock(Painter &p, int x, int y, int h, COLORREF c)
{
	const int w = std::max(8, h * 12 / 17), bodyY = y + h * 7 / 17;
	p.Frame(x + w / 5, y, w - 2 * (w / 5), bodyY - y + 2, c);
	p.Fill(x, bodyY, w, y + h - bodyY, c);
}

void SetTipText(const std::string &text, int x, int y)
{
	SetTip(text, x, y);
}

// A choice of two side by side: the one in use has the lighter fill and the red rule under it.
void PaintSeg(Painter &p, int x, int y, int w, int h, const std::string &label, bool on, bool enabled, int action, int arg, int mx, int my)
{
	const bool hov = enabled && p.Hover(x, y, w, h, mx, my);
	p.Fill(x, y, w, h, on ? pal::selected : hov ? pal::raised : pal::field);
	p.Frame(x, y, w, h, pal::edge);
	if (on) p.Fill(x + 1, y + h - 3, w - 2, 3, pal::red);
	p.Font(4);
	p.Text(x + 8, y, w - 16, h, label, !enabled ? pal::disabled : on ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (enabled && !on) p.AddHit(x, y, w, h, action, arg);
}

// What a pack shares, in the Conflicts column of the list and of the load order.
void PaintClashCell(Painter &p, const State &st, const Row &r, int x, int y, int w, int h)
{
	const int cy = y + h / 2;
	p.Font(3);
	if (!r.active) return;
	const ClashSummary c = ClashSummaryOf(st, r.folder);
	if (c.problem || RowHasProblems(st, r)) {
		PaintAlertIcon(p, x, cy - 8, true, 16);
		const std::string text = c.problem ? TrFmt("clashes with %s", {c.problemWith}) : Tr("it has problems");
		p.Text(x + 24, y, w - 24, h, text, pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (p.Hover(x, y, w, h, st.mouseX, st.mouseY)) SetTip("Select it to see what happens", x, y + h);
		return;
	}
	if (c.wins || c.loses || c.values) {
		const COLORREF col = c.loses || c.values ? pal::warn : pal::ok;
		PaintBolt(p, x + 2, cy - 8, 16, col);
		std::string text;
		if (c.wins && c.loses) text = TrFmt("wins %s · loses %s", {std::to_string(c.wins), std::to_string(c.loses)});
		else if (c.wins) text = TrFmt("wins %s", {std::to_string(c.wins)});
		else if (c.loses) text = TrFmt("loses %s", {std::to_string(c.loses)});
		else text = TrFmt("%s shared values", {std::to_string(c.values)});
		p.Text(x + 24, y, w - 24, h, text, col, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (p.Hover(x, y, w, h, st.mouseX, st.mouseY)) SetTip("Files it shares with other packs: see them in its Conflicts tab", x, y + h);
		return;
	}
	p.Text(x, y, w, h, "no conflicts", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintGroupBand(Painter &p, int x, int y, int w, int group, int count)
{
	p.Fill(x + 1, y, w - 2, kGroupH, pal::raised);
	p.Fill(x + 1, y + kGroupH - 1, w - 2, 1, pal::hairline);
	p.Font(4);
	const std::string name = Tr(OrderGroupName(group));
	const int nw = p.CapsWidth(name, 1);
	p.TextCaps(x + 18, y, nw + 4, kGroupH, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	std::string note = std::to_string(count);
	if (*GroupNote(group)) note += " · " + Tr(GroupNote(group));
	p.TextCaps(x + 18 + nw + 14, y, w - nw - 60, kGroupH, note, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
}


namespace {
// The tools over the list and the card: the profile, the search, the filters and the order of the list, or what the load order is;
// and over the card, installing a file and updating everything.
void PaintTools(Painter &p, const State &st, const PanelLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY, y = l.toolsY, gap = 12;
	const int x = PaintProfileBar(p, st, l.x, y, kBarH) + 16;
	const int listRight = l.x + l.listW;
	if (st.tab == kTabListInstalled) {
		const int filtersW = 150, sortW = 240;
		const int sortX = listRight - sortW, filtersX = sortX - gap - filtersW;
		const int searchW = std::max(120, filtersX - gap - x);
		PaintField(p, st, x, y, searchW, kBarH, st.searchText, "Search your content...", st.focus == kSearchField, false, kFocusSearch, mx, my);
		const int n = CatalogFilterSize(st.catFilter);
		p.Button(filtersX, y, filtersW, kBarH, n ? Tr("Filters") + " (" + std::to_string(n) + ")" : Tr("Filters"), kCatPanel, 0, mx, my, true, n > 0 || st.catOpen, 0);
		Combo(p, st, sortX, y, sortW, kBarH, Tr("Order: ") + Tr(kSortNames[st.sort]), kDropSort);
	}
	else {
		PaintPadlock(p, x, y + (kBarH - 17) / 2, 17, pal::caption);
		p.Font(3);
		p.Text(x + 26, y, listRight - x - 26, kBarH, "The order is worked out on its own and is the same on every PC, so online games match.", pal::body,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	const int updates = CountUpdates(st);
	const int half = (l.detailW - gap) / 2;
	p.Button(l.detailX, y, updates ? half : l.detailW, kBarH, "Install file...", kPickArchive, 0, mx, my);
	if (updates) {
		p.Button(l.detailX + half + gap, y, l.detailW - half - gap, kBarH, Tr("Update all (") + std::to_string(updates) + ")", kUpdateAll, 0, mx, my,
			!st.dl.active, true);
	}
}

// The card beside the list: the selected pack in three tabs, or the packs that are marked.
void PaintModCard(Painter &p, const State &st, const PanelLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int pad = 24, cardY = l.cardTop, cardH = l.cardBottom - l.cardTop;
	PaintGlassCard(p, l.detailX, cardY, l.detailW, cardH);
	const int dx = l.detailX + pad, dw = l.detailW - 2 * pad, top = cardY + pad - 4, bottom = cardY + cardH - pad;
	const int btnH = 36, btnY = bottom - btnH;
	if (st.marked.size() > 1) {
		int y = top;
		p.Font(2);
		p.Text(dx, y, dw, 40, TrFmt("%s content packs selected", {std::to_string(st.marked.size())}), pal::redText);
		y += 48;
		int problems = 0;
		p.Font(0);
		const int listEnd = bottom - 90;
		for (const std::string &folder : st.marked) {
			const ModInfo *mm = FindModIn(st.mods, folder);
			const auto it = std::find_if(st.rows.begin(), st.rows.end(), [&](const Row &r) { return r.folder == folder; });
			const bool bad = it != st.rows.end() && RowHasProblems(st, *it);
			if (bad) problems++;
			if (y > listEnd) continue;
			const bool on = it != st.rows.end() && it->active;
			p.Text(dx, y, dw, 28, std::string(on ? "\xE2\x97\x8F  " : "\xE2\x97\x8B  ") + (mm ? mm->name : folder), bad ? pal::bad : on ? pal::text : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 28;
		}
		if (y > listEnd) p.Text(dx, y, dw, 28, "...", pal::textDim);
		p.Font(3);
		p.TextWrapped(dx, bottom - 56, dw, 56,
			problems ? TrFmt("%s of them have problems. Use the bar at the foot of the list to enable, disable or uninstall them all.", {std::to_string(problems)})
					 : std::string("Use the bar at the foot of the list to enable, disable or uninstall them all."),
			problems ? pal::bad : pal::textDim);
		return;
	}
	const Row *sel = SelectedRow(st);
	const ModInfo *m = sel ? FindModIn(st.mods, sel->folder) : nullptr;
	if (!m && sel) {
		p.Font(0);
		p.TextWrapped(dx, top, dw, 100, "This content pack is in the profile, but its folder is no longer in the content folder (it was probably deleted by hand).", pal::bad);
		p.Button(dx, btnY, 260, btnH, "Remove from the profile", kForget, st.selected, mx, my, true, false, 0);
		p.Button(dx + 270, btnY, 200, btnH, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
		return;
	}
	if (!m) {
		p.Font(0);
		p.TextWrapped(dx, top, dw, 100, "Select a content pack to see what it is, or open the content folder to add one by hand.", pal::textDim);
		p.Button(dx, btnY, 200, btnH, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
		return;
	}
	const bool active = st.rows[st.selected].active;
	const auto isIt = st.issues->find(m->folder);
	const ModIssues *is = isIt != st.issues->end() ? &isIt->second : nullptr;
	const ClashSummary cs = ClashSummaryOf(st, m->folder);
	int y = top;
	p.Font(2);
	p.Text(dx, y, dw, 40, m->name, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	y = PaintCardTabs(p, st, dx, y + 42, dw, {{"Details", 0}, {"Conflicts", cs.pairs + (cs.values ? 1 : 0)}, {"Files", static_cast<int>(m->fileCount)}}) + 14;
	if (st.cardTab == kCardConflicts) {
		PaintCardConflicts(p, st, *m, active, is, dx, y, dw, bottom);
		return;
	}
	if (st.cardTab == kCardFiles) {
		PaintCardFiles(p, st, *m, is, dx, y, dw, bottom);
		return;
	}
	const std::shared_ptr<const GbMod> io = m->gbId ? GbInfo(m->gbId) : nullptr;
	std::string by = AuthorsOf(*m);
	if (by.empty() && io) by = io->author;
	p.Font(3);
	const int lineH = 22;
	if (m->gbId) {
		const int iw = 160, ih = 90;
		PaintIoImage(p, dx, y, iw, ih, io ? io->logoUrl : "");
		p.Frame(dx, y, iw, ih, pal::lineStrong);
		const int tx2 = dx + iw + 14, tw = dw - iw - 14;
		p.Text(tx2, y + 4, tw, lineH, m->version.empty() ? std::string("No version") : Tr("Version ") + m->version, pal::text);
		p.Text(tx2, y + 4 + lineH + 4, tw, lineH, by.empty() ? std::string() : (by.find(',') == std::string::npos ? Tr("Author: ") : Tr("Authors: ")) + by, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(tx2, y + 4 + 2 * (lineH + 4), tw, lineH, HumanBytes(m->totalBytes), pal::textDim);
		y += ih + 10;
	}
	else {
		p.Text(dx, y, dw, lineH, (m->version.empty() ? std::string("No version") : Tr("Version ") + m->version) +
			(by.empty() ? "" : std::string("  ·  ") + (by.find(',') == std::string::npos ? Tr("Author: ") : Tr("Authors: ")) + by) + "  ·  " + HumanBytes(m->totalBytes), pal::text,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += lineH;
	}
	p.Text(dx, y, dw, lineH, m->gameplay ? "Gameplay: everyone must have it online" : m->kind == kModContent ? "Maps and campaigns: only needed by whoever plays them" : "Cosmetic: does not affect games",
		m->gameplay ? pal::redText : m->kind == kModContent ? RGB(140, 196, 176) : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	y += lineH;
	{
		const auto it = st.usedBy.find(m->folder);
		std::string used;
		if (it != st.usedBy.end()) {
			for (const std::string &n : it->second) used += (used.empty() ? "" : ", ") + ModsProfileLabel(n);
		}
		p.Text(dx, y, dw, lineH, used.empty() ? "Not in any profile" : Tr("In profiles: ") + used, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += lineH;
	}
	if (m->newSinceStart) {
		p.Text(dx, y, dw, lineH, "Recently installed content", pal::ok, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += lineH;
	}
	const int mainY = btnY - btnH - 10;
	const int textLimit = mainY - 8 - 140;
	if (is) {
		for (size_t k = 0; k < is->problems.size() && k < 3; k++) {
			if (y > textLimit) break;
			PaintAlertIcon(p, dx, y + 3, true, 16);
			y += p.TextWrapped(dx + 24, y, dw - 24, 66, is->problems[k], pal::bad);
		}
	}
	if (active) {
		if (const ModOrderEntry *e = OrderEntryOf(st, m->folder)) {
			if (y + 64 < textLimit) y = PaintOrderBox(p, st, *e, dx, y + 10, dw);
		}
		if ((cs.pairs || cs.values) && y + 70 < textLimit) y = PaintClashBox(p, st, cs, dx, y + 10, dw);
	}
	const int textBottom = mainY - 12;
	if (textBottom - y > 50) {
		std::string notes, noteVersion;
		if (m->gbId) GbChangelog(m->gbId, m->version, notes, noteVersion);
		p.Fill(dx, y + 10, dw, 1, pal::hairline);
		const bool own = !m->description.empty() && (m->hasManifest || !io || io->descriptionHtml.empty());
		PaintRich(p, st, dx, y + 18, dw, textBottom - y - 18, own || !io ? "" : io->descriptionHtml,
			own ? m->description : io ? (!io->description.empty() ? io->description : io->summary) : "",
			Tr("What's new in version ") + noteVersion, notes, 3);
	}
	const bool pending = Pending(st, m->folder);
	CatalogEntry update;
	const bool hasUpdate = FindUpdateFor(st, *m, update);
	if (pending) {
		p.Font(0);
		p.Text(dx, mainY, dw, btnH, "It will be uninstalled when the game restarts", pal::bad);
	}
	else {
		const bool repair = m->noGameFiles && m->gbId && !hasUpdate;
		const int mainW = hasUpdate || repair ? 190 : dw;
		p.Button(dx, mainY, mainW, btnH, active ? "Disable" : "Enable", kSetActive, 0, mx, my, !st.busy, !active);
		if (hasUpdate) p.Button(dx + mainW + 8, mainY, dw - mainW - 8, btnH, Tr("Update to version ") + update.version, kUpdateMod, 0, mx, my, !st.dl.active, true);
		if (repair) p.Button(dx + mainW + 8, mainY, dw - mainW - 8, btnH, "Reinstall from GameBanana", kReinstall, 0, mx, my, !st.dl.active, true);
	}
	const int moreW = 48;
	const int pageW = m->gbId ? 170 : 0;
	p.Button(dx, btnY, dw - moreW - 8 - (pageW ? pageW + 8 : 0), btnH, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
	if (pageW) p.Button(dx + dw - moreW - 8 - pageW, btnY, pageW, btnH, "GameBanana", kOpenSource, 0, mx, my, io && !io->page.empty(), false, 0);
	if (!pending) {
		p.Button(dx + dw - moreW, btnY, moreW, btnH, "...", kDropOpen, kDropModMenu, mx, my, !st.busy, false, 0);
		if (st.drop == kDropModMenu || st.drop == kDropVersions) g_drop.anchor = {dx + dw - 200, btnY, dx + dw, btnY + btnH};
	}
}
}

void PaintPanel(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	UseScreenFonts(p);
	const PanelLayout l = PanelLayoutFor(st);
	const bool overlay = st.drop != kDropNone || st.confirm || (st.tab == kTabListExplore && (st.ioFilterOpen || st.ioShot >= 0)) ||
		(st.tab == kTabListInstalled && st.catOpen);
	p.noHover = overlay;
	int right = PaintHeaderActions(p, l, {{"Back", kClose, true, true}}, mx, my);
	if (st.restartNeeded) {
		p.Font(1);
		const std::string label = Tr("Apply and restart");
		const int w = p.CapsWidth(label, 1) + 48;
		right -= 12 + w;
		p.Button(right, l.margin, w, kHeaderButtonH, label, kApply, 0, mx, my, true, true);
		if (p.Hover(right, l.margin, w, kHeaderButtonH, mx, my)) SetTip("The content changes apply when the game restarts", right, l.margin + kHeaderButtonH);
	}
	PaintScreenTitle(p, l, "Content",
		{{Tr("Installed") + " (" + std::to_string(st.mods.size()) + ")", kTabInstalled, 0, st.tab == kTabListInstalled, true},
			{"Browse content", kTabExplore, 0, st.tab == kTabListExplore, true}},
		right - 16, mx, my);
	if (st.tab == kTabListExplore) {
		PaintExplore(p, st);
	}
	else {
		PaintTools(p, st, l);
		PaintInstalled(p, st);
	}
	PaintFooter(p, st);
	p.noHover = false;
	PaintFilterPanel(p, st);
	PaintInstalledFilterPanel(p, st);
	PaintShotViewer(p, st);
	if (st.drop == kDropNone && !st.confirm) PaintTip(p, st);
	g_tip.text.clear();
	// The menus and the questions over the screen keep the shared type their boxes are sized for.
	UseSharedFonts(p);
	PaintDropList(p, st);
	PaintConfirm(p, st);
}

void PaintInstalled(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const PanelLayout l = PanelLayoutFor(st);
	const int listX = l.x, listW = l.listW, cardY = l.cardTop, cardH = l.cardBottom - l.cardTop;
	PaintGlassCard(p, listX, cardY, listW, cardH);

	const int stripH = 48;
	{
		int counts[kStateCount];
		CountStates(st, st.catFilter, counts);
		const char *names[] = {"All", "Active", "Disabled", "With conflicts", "With problems"};
		int tx = listX + 10;
		for (int k = 0; k < kStateCount; k++) {
			const int count = counts[k];
			if ((k == kStateProblems || k == kStateConflicts) && count == 0 && st.stateFilter != k) continue;
			const bool on = st.stateFilter == k;
			const std::string label = Tr(names[k]) + " " + std::to_string(count);
			p.Font(4);
			const int icon = k == kStateProblems || k == kStateConflicts ? 24 : 0;
			const int w = p.CapsWidth(label, 1) + 28 + icon;
			const bool hov = p.Hover(tx, cardY, w, stripH, mx, my);
			const COLORREF c = k == kStateProblems ? pal::bad : on || hov ? pal::text : pal::caption;
			if (k == kStateProblems) PaintAlertIcon(p, tx + 14, cardY + (stripH - 16) / 2, true, 16);
			if (k == kStateConflicts) PaintBolt(p, tx + 16, cardY + (stripH - 16) / 2, 16, pal::warn);
			p.TextCaps(tx + 14 + icon, cardY, w - 14 - icon, stripH, label, c, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
			if (on) p.Fill(tx + 8, cardY + stripH - 3, w - 16, 3, pal::red);
			p.AddHit(tx, cardY, w, stripH, kStateFilter, k);
			tx += w;
		}
	}

	const int switchX = listX + listW - 28 - kBigToggleW, switchHitW = kBigToggleW + 12;
	const int posX = listX + 10, posW = 40, boxX = listX + 60, thumbX = listX + 92, thumbW = 56, thumbH = 32;
	const int clashW = 210, clashX = switchX - 24 - clashW, catW = 160, catX = clashX - 16 - catW;
	const int nameX = thumbX + thumbW + 14, nameW = catX - 16 - nameX;
	const int headY = cardY + stripH, headH = 36;
	p.Fill(listX + 1, headY, listW - 2, 1, pal::hairline);
	p.Fill(listX + 1, headY + headH - 1, listW - 2, 1, pal::lineStrong);
	p.Font(4);
	p.TextCaps(posX, headY, posW, headH, "#", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(thumbX, headY, nameW, headH, "Content", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(catX, headY, catW, headH, "Type", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(clashX, headY, clashW, headH, "Conflicts", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(switchX - 30, headY, kBigToggleW + 30, headH, "Active", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);

	const int footH = 48, footY = cardY + cardH - footH;
	const int listY = headY + headH, listH = footY - listY;
	const std::vector<int> view = VisibleRows(st);
	const bool grouped = st.sort == kSortPriority;
	int groupCount[4] = {0, 0, 0, 0};
	if (grouped) {
		for (int i : view) groupCount[GroupOf(st.rows[i], FindModIn(st.mods, st.rows[i].folder))]++;
	}
	int bands = 0;
	for (int g = 0; g < 4; g++) bands += groupCount[g] ? 1 : 0;
	// As many rows as fit with every group's band showing, so the last page never loses a row under them.
	const int rows = std::max(1, (listH - bands * kGroupH) / kModRowH);
	g_installedRows = rows;
	if (st.rows.empty()) {
		p.Font(0);
		p.TextWrapped(listX + 20, listY + 18, listW - 40, 140,
			"No content installed. Find it in the Browse content tab, install a .zip with \"Install file\", or "
			"copy each pack into its own folder inside \"mods\" (in the game folder): it appears here by itself.",
			pal::textDim);
	}
	else if (view.empty()) {
		p.Font(0);
		p.TextWrapped(listX + 20, listY + 18, listW - 40, 80, "No content matches the search or filter.", pal::textDim);
	}
	int y = listY, lastGroup = -1;
	for (int v = st.list.scroll; v < static_cast<int>(view.size()) && v < st.list.scroll + rows; v++) {
		const int i = view[v];
		const Row &r = st.rows[i];
		const ModInfo *m = FindModIn(st.mods, r.folder);
		if (grouped) {
			const int group = GroupOf(r, m);
			if (group != lastGroup) {
				PaintGroupBand(p, listX, y, listW, group, groupCount[group]);
				y += kGroupH;
				lastGroup = group;
			}
			else {
				p.Fill(listX + 1, y, listW - 2, 1, pal::hairline);
			}
		}
		else if (v > st.list.scroll) {
			p.Fill(listX + 1, y, listW - 2, 1, pal::hairline);
		}
		const bool pending = Pending(st, r.folder);
		const bool marked = IsMarked(st, r.folder);
		const bool sel = i == st.selected;
		if (marked) p.Fill(listX + 1, y + 1, listW - 2, kModRowH - 1, pal::hairline);
		else if (p.Hover(listX, y, listW, kModRowH, mx, my)) p.Fill(listX + 1, y + 1, listW - 2, kModRowH - 1, pal::selected);
		if (sel) {
			p.Fill(listX + 1, y + 1, listW - 2, kModRowH - 1, pal::selected);
			p.Fill(listX + 1, y + 1, 3, kModRowH - 1, pal::red);
		}
		p.AddHit(listX + 50, y, switchX - listX - 56, kModRowH, kRowClick, i);

		const ModOrderEntry *entry = r.active ? OrderEntryOf(st, r.folder) : nullptr;
		if (entry) {
			p.Font(2);
			p.Text(posX, y, posW, kModRowH, std::to_string(entry->position), sel ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}

		const int boxY = y + (kModRowH - 20) / 2;
		const bool boxHov = p.Hover(boxX - 10, y, 40, kModRowH, mx, my);
		p.Fill(boxX, boxY, 20, 20, pal::bg);
		p.Frame(boxX, boxY, 20, 20, boxHov ? pal::text : pal::edgeHot);
		if (marked) {
			p.Line(boxX + 4, boxY + 10, boxX + 8, boxY + 15, pal::redText, 2);
			p.Line(boxX + 8, boxY + 15, boxX + 16, boxY + 5, pal::redText, 2);
		}
		p.AddHit(boxX - 10, y, 40, kModRowH, kMark, i);

		const int thumbY = y + (kModRowH - thumbH) / 2;
		const std::shared_ptr<const GbMod> io = m && m->gbId ? GbInfo(m->gbId) : nullptr;
		const std::shared_ptr<const GbImage> img = io ? GbLogo(io->logoUrl) : nullptr;
		if (img) p.Image(thumbX, thumbY, thumbW, thumbH, img->bgra.data(), img->width, img->height);
		else p.Fill(thumbX, thumbY, thumbW, thumbH, m && m->gameplay ? pal::edge : m && m->kind == kModContent ? RGB(34, 60, 52) : pal::hairline);

		const std::string name = !m ? r.folder + Tr(" (not installed)")
			: m->version.empty() || IsDateVersion(m->version) || m->name.find(m->version) != std::string::npos ? m->name : m->name + " " + m->version;
		p.Font(r.active ? 1 : 0);
		const int nameTextW = std::min(nameW - 40, p.TextWidth(name));
		p.Text(nameX, y + 6, nameTextW, 24, name, pending || !m ? pal::bad : r.active ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		CatalogEntry upd;
		const bool hasUpd = m && FindUpdateFor(st, *m, upd);
		int markX = nameX + nameTextW + 10;
		if (hasUpd) {
			p.ArrowUp(markX, y + 12, 11, 9, pal::text);
			p.Fill(markX + 4, y + 21, 3, 6, pal::text);
			if (p.Hover(markX - 4, y + 6, 20, 24, mx, my)) SetTip(Tr("Update available: version ") + upd.version, markX, y + 30);
			markX += 20;
		}
		if (m && m->newSinceStart) {
			p.Diamond(markX + 6, y + 18, 6, pal::ok);
			if (p.Hover(markX - 2, y + 6, 18, 24, mx, my)) SetTip("Recently installed content", markX, y + 30);
		}
		std::string sub;
		if (m) {
			if (entry && entry->reason == kOrderNeeded) sub = OrderReasonText(st, *entry);
			else {
				const std::string by = AuthorsOf(*m);
				sub = (by.empty() ? Tr("unknown author") : by) + "  ·  " + HumanBytes(m->totalBytes);
				if (hasUpd) sub += "  ·  " + TrFmt("version %s available", {upd.version});
			}
		}
		p.Font(3);
		p.Text(nameX, y + 29, nameW, 20, sub, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (m) {
			const int cat = ModMainCategory(*m);
			const int group = CatalogCategoryGroup(cat);
			p.Text(catX, y, catW, kModRowH, Tr(CatalogCategoryLabel(cat)), group == 0 ? pal::redText : group == 1 ? RGB(140, 196, 176) : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (!pending) PaintClashCell(p, st, r, clashX, y, clashW, kModRowH);
		}
		if (pending) {
			p.Font(1);
			p.Text(switchX - 6, y, switchHitW, kModRowH, "\xE2\x9C\x95", pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(switchX - 6, y, switchHitW, kModRowH, mx, my)) SetTip("It will be uninstalled when the game restarts", switchX - 300, y + kModRowH);
		}
		else if (!m) {
			p.Font(1);
			p.Text(switchX - 6, y, switchHitW, kModRowH, "\xE2\x9C\x95", pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(switchX - 6, y, switchHitW, kModRowH, mx, my)) SetTip("Not installed: click to remove it from the profile", switchX - 360, y + kModRowH);
			p.AddHit(switchX - 6, y, switchHitW, kModRowH, kForget, i);
		}
		else {
			const bool hov = p.Hover(switchX - 6, y, switchHitW, kModRowH, mx, my);
			PaintBigToggle(p, switchX, y + (kModRowH - kBigToggleH) / 2, r.active, true, hov);
			if (hov) SetTip(r.active ? "Enabled: click to disable" : "Disabled: click to enable", switchX - 180, y + kModRowH);
			p.AddHit(switchX - 6, y, switchHitW, kModRowH, kToggle, i);
		}
		y += kModRowH;
	}
	{
		ListGeom geom;
		ScrollbarPaint(p, listX + listW - 9, listY, listH, static_cast<int>(view.size()), rows, st.list, geom, mx, my);
		g_listGeomOut = geom;
	}

	p.Fill(listX + 1, footY, listW - 2, 1, pal::hairline);
	if (!st.marked.empty()) {
		const std::string count = TrFmt(st.marked.size() == 1 ? "%s selected" : "%s selected:", {std::to_string(st.marked.size())});
		const char *labels[] = {"Enable", "Disable", "Uninstall...", "Clear selection"};
		const int actions[] = {kBulkOn, kBulkOff, kBulkUninstall, kClearMarks};
		const int bh = 36, by = footY + (footH - bh) / 2;
		int font = 0;
		for (; font <= 3; font += 3) {
			p.Font(font);
			int total = p.TextWidth(count) + 14 + 40;
			for (const char *label : labels) total += p.TextWidth(Tr(label)) + 28 + 8;
			if (total <= listW) break;
		}
		p.Font(font);
		const int cw = p.TextWidth(count) + 14;
		p.Text(listX + 20, by, cw, bh, count, pal::text);
		int bx = listX + 20 + cw;
		for (int k = 0; k < 4; k++) {
			p.Font(font);
			const int w = p.TextWidth(Tr(labels[k])) + 28;
			p.Button(bx, by, w, bh, labels[k], actions[k], 0, mx, my, !st.busy, false, font);
			bx += w + 8;
		}
	}
	else {
		const std::string link = Tr("Share profile");
		p.Font(4);
		const int lw = p.CapsWidth(link, 1) + 12;
		const int lx = listX + listW - 22 - lw;
		const bool hov = p.Hover(lx, footY, lw, footH, mx, my);
		p.TextCaps(lx, footY, lw, footH, link, hov ? pal::text : pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
		p.AddHit(lx, footY, lw, footH, kShareOpen, 0);
		p.Font(3);
		p.Text(listX + 20, footY, lx - 16 - listX - 20, footH,
			"Higher up wins: when two packs bring the same file, the one higher up is used. The order is automatic and the same on every PC.", pal::textDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	if (st.shareOpen) PaintShareCard(p, st, l);
	else PaintModCard(p, st, l);
}

void PaintField(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, const std::string &placeholder,
	bool focused, bool secret, int action, int mx, int my)
{
	(void)mx;
	(void)my;
	PaintEditField(p, x, y, w, h, text, placeholder, focused, secret, st.edit, action);
}

// Only Browse content keeps a foot, for its pages.
void PaintFooter(Painter &p, const State &st)
{
	if (st.tab != kTabListExplore || st.ioDetail >= 0 || st.io.mods.empty()) return;
	const int mx = st.mouseX, my = st.mouseY;
	const PanelLayout l = PanelLayoutFor(st);
	const int by = l.footY, bh = kFootH;
	const int pages = std::max(1, (st.io.total + kIoPerPage - 1) / kIoPerPage);
	const int first = st.ioPage * kIoPerPage + 1;
	const int last = std::min(st.io.total, first + kIoPerPage - 1);
	const int btnW = 150, textW = 320;
	const int total = 2 * btnW + textW + 16;
	const int x0 = l.x + (l.inner - total) / 2;
	p.Button(x0, by, btnW, bh, "Previous", kIoPrev, 0, mx, my, st.ioPage > 0 && !st.io.loading);
	p.Button(x0 + btnW + 8 + textW + 8, by, btnW, bh, "Next", kIoNext, 0, mx, my, st.ioPage + 1 < pages && !st.io.loading);
	p.Font(3);
	std::string text = TrFmt("%s-%s of %s", {std::to_string(first), std::to_string(last), std::to_string(st.io.total)}) + "   " +
		TrFmt("Page %s of %s", {std::to_string(st.ioPage + 1), std::to_string(pages)});
	if (p.TextWidth(text) > textW) text = TrFmt("Page %s of %s", {std::to_string(st.ioPage + 1), std::to_string(pages)});
	p.Text(x0 + btnW + 8, by, textW, bh, text, pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintInstalledFilterPanel(Painter &p, const State &st)
{
	if (!st.catOpen || st.tab != 0) return;
	PaintFacetPanel(p, PanelLayoutFor(st), st.catFilter, InstalledCounts(st), kCatChip, kCatClear, kCatPanel, st.mouseX, st.mouseY);
}
}

