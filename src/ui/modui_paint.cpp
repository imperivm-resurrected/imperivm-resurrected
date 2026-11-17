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
		SpinnerDone(url);
		p.Image(x, y, w, h, img->bgra.data(), img->width, img->height);
	}
	else {
		p.Fill(x, y, w, h, pal::panel);
		if (url.empty()) {
			p.Font(3);
			p.Text(x, y, w, h, "No image", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		else if (SpinnerDue(url)) {
			const int radius = std::max(7, std::min(22, std::min(w, h) / 6));
			p.Spinner(x + w / 2, y + h / 2, radius, std::max(4, radius / 3), GetTickCount());
		}
	}
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
	p.Button(refreshX, y0, refreshW, kBarH, "Refresh", kIoRefresh, 0, mx, my, !io.loading, false, 0);
	{
		const int trackX = l.x, trackW = l.inner, trackY = y0 + kBarH + 5;
		p.Fill(trackX, trackY, trackW, 2, pal::selected);
		if (io.loading) {
			const int segW = 360, cycle = 1400;
			const int pos = -segW + (trackW + segW) * static_cast<int>(GetTickCount() % cycle) / cycle;
			const int x0 = std::max(0, pos), x1 = std::min(trackW, pos + segW);
			if (x1 > x0) p.Fill(trackX + x0, trackY, x1 - x0, 2, pal::redText);
		}
	}

	const int gx = l.x, gy = y0 + kBarH + 16, gw = l.inner, gh = l.cardBottom - gy, cardGap = 16;
	if (io.mods.empty()) {
		PaintGlassCard(p, gx, gy, gw, gh);
		if (io.loading || !st.ioRequested) {
			p.Font(0);
			p.Text(gx, gy, gw, gh, "Loading...", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			return;
		}
		p.Font(0);
		std::string text = !io.error.empty() ? Tr("Could not connect to GameBanana: ") + Tr(io.error)
			: !st.ioSearch.empty() || nFilters ? "No content matches the search."
												: "There is no content published for Imperivm on GameBanana yet. Be the first to upload some!";
		p.TextWrapped(gx + 28, gy + 28, gw - 56, 80, text, io.error.empty() ? pal::textDim : pal::bad);
		if (!io.error.empty() && !io.loading) p.Button(gx + 28, gy + 110, 200, kBarH, "Retry", kIoRetry, 0, mx, my);
		return;
	}
	// A page of kIoPerPage in 4 columns and 2 rows; the pictures keep the shape of GameBanana's cards and the text takes the rest.
	const int cw = (gw - 3 * cardGap) / 4, ch = (gh - cardGap) / 2;
	const int imgH = std::max(60, ch - 96), imgW = std::min(cw - 2, imgH * 235 / 152);
	for (size_t i = 0; i < io.mods.size() && i < static_cast<size_t>(kIoPerPage); i++) {
		const GbMod &m = io.mods[i];
		const int x = gx + static_cast<int>(i % 4) * (cw + cardGap), y = gy + static_cast<int>(i / 4) * (ch + cardGap);
		const bool hov = p.Hover(x, y, cw, ch, mx, my);
		PaintGlassCard(p, x, y, cw, ch);
		if (hov) p.Fill(x + 1, y + 1, cw - 2, ch - 2, pal::field);
		p.Fill(x + 1, y + 1, cw - 2, imgH, pal::panel);
		PaintIoImage(p, x + (cw - imgW) / 2, y + 1, imgW, imgH, m.logoUrl);
		p.Frame(x, y, cw, ch, hov ? pal::edgeHot : pal::hairline);
		const std::string catText = m.facts.category >= 0 ? Tr(CatalogCategoryLabel(m.facts.category)) : m.category;
		if (!catText.empty()) {
			p.Font(4);
			const std::string cat = UpperText(catText);
			const int chipW = std::min(cw - 20, p.TextWidth(cat) + 20), chipH = 26, chipX = x + 10, chipY = y + 1 + imgH - chipH - 10;
			p.Fill(chipX, chipY, chipW, chipH, pal::panel);
			p.Text(chipX + 10, chipY, chipW - 14, chipH, cat, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		const int state = IoInstalledState(st, m);
		if (state == 1 || state == 2) {
			p.Font(4);
			const std::string badge = UpperText(Tr(state == 1 ? "Already installed" : "Update available"));
			const int badgeW = p.TextWidth(badge) + 20;
			p.Fill(x + cw - 10 - badgeW, y + 11, badgeW, 26, pal::panel);
			p.Text(x + cw - 10 - badgeW, y + 11, badgeW, 26, badge, state == 1 ? pal::ok : pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

const int kProfileComboW = 300, kProfileNameW = 300, kProfileButtonW = 130, kProfileGap = 8, kProfileBarH = 34;

int ProfileExtraWidth(Painter &p, const State &st)
{
	if (st.profile.name == kNoProfileName) {
		p.Font(1);
		return p.CapsWidth("Save as profile", 1) + 44;
	}
	return st.profile.name != kOriginalName ? 48 : 0;
}

int ProfileBarWidth(Painter &p, const State &st)
{
	p.Font(0);
	const int labelW = p.TextWidth("Profile") + 14;
	if (st.nameMode != kNameNone) return labelW + kProfileNameW + 2 * (kProfileGap + kProfileButtonW);
	const int extraW = ProfileExtraWidth(p, st);
	return labelW + kProfileComboW + (extraW ? kProfileGap + extraW : 0);
}

// The profile being edited (its name while it is named, and its menu), on the caption line and ending at `right`: the title's line has
// no room for it next to the tabs on a narrow screen.
void PaintProfileBar(Painter &p, const State &st, const PanelLayout &l, int right)
{
	const int mx = st.mouseX, my = st.mouseY, y = l.infoY - 3, h = kProfileBarH, gap = kProfileGap;
	int x = right - ProfileBarWidth(p, st);
	p.Font(0);
	const int labelW = p.TextWidth("Profile") + 14;
	p.Text(x, y, labelW, h, "Profile", pal::textDim);
	x += labelW;
	if (st.nameMode != kNameNone) {
		const char *what = st.nameMode == kNameRename ? "New name" : "Profile name";
		PaintField(p, st, x, y, kProfileNameW, h, st.nameText, what, st.focus == kNameField, false, kFocusName, mx, my);
		x += kProfileNameW + gap;
		p.Button(x, y, kProfileButtonW, h, st.nameMode == kNameRename ? "Rename" : "Save", kNameOk, 0, mx, my, true, true);
		x += kProfileButtonW + gap;
		p.Button(x, y, kProfileButtonW, h, "Cancel", kNameCancel, 0, mx, my);
		return;
	}
	Combo(p, st, x, y, kProfileComboW, h, ModsProfileLabel(st.profile.name), kDropProfile);
	x += kProfileComboW + gap;
	const int extraW = ProfileExtraWidth(p, st);
	if (st.profile.name == kNoProfileName) {
		p.Button(x, y, extraW, h, "Save as profile", kSaveAs, 0, mx, my, !st.profile.mods.empty());
	}
	else if (extraW) {
		p.Button(x, y, extraW, h, "...", kDropOpen, kDropMenu, mx, my);
		if (st.drop == kDropMenu) g_drop.anchor = {x, y, x + extraW, y + h};
	}
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
}
}

namespace modui_impl {
PanelLayout PanelLayoutFor(const State &st)
{
	PanelLayout l;
	static_cast<ScreenLayout &>(l) = ScreenLayoutFor(st.viewW, st.viewH);
	l.x = l.margin;
	l.inner = l.w - 2 * l.margin;
	l.footY = l.h - l.margin - kFootH;
	l.cardBottom = l.footY - 24;
	l.detailW = std::max(480, std::min(720, l.inner * 2 / 5));
	l.listW = l.inner - l.detailW - 32;
	l.detailX = l.x + l.listW + 32;
	l.selectionBarY = l.cardBottom - kBarH;
	return l;
}

void PaintPanel(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	UseScreenFonts(p);
	const PanelLayout l = PanelLayoutFor(st);
	const int profileRight = l.w - l.margin;
	int captionRight = profileRight - ProfileBarWidth(p, st) - 24;
	PaintProfileBar(p, st, l, profileRight);
	const bool overlay = st.drop != kDropNone || st.confirm || (st.tab == 2 && (st.ioFilterOpen || st.ioShot >= 0)) || (st.tab == 0 && st.catOpen);
	p.noHover = overlay;
	const int actionsLeft = PaintHeaderActions(p, l, {{"Back", kClose, true, true}}, mx, my);
	PaintScreenTitle(p, l, "Content",
		{{Tr("Installed") + " (" + std::to_string(st.mods.size()) + ")", kTabInstalled, 0, st.tab == 0, true},
			{"Browse content", kTabExplore, 0, st.tab == 2, true}},
		actionsLeft, mx, my);
	{
		const std::vector<std::string> &problems = st.problems;
		if (!problems.empty()) {
			const std::string label = std::to_string(problems.size()) + " " + Tr(problems.size() == 1 ? "problem in the profile" : "problems in the profile");
			p.Font(3);
			const int bw = p.TextWidth(label) + 54, bh = kProfileBarH, bx = captionRight - bw, by = l.infoY - 3;
			captionRight = bx - 16;
			const bool hov = p.Hover(bx, by, bw, bh, st.mouseX, st.mouseY);
			p.Fill(bx, by, bw, bh, hov ? pal::red : pal::selected);
			p.Frame(bx, by, bw, bh, hov ? pal::text : pal::bad);
			PaintAlertIcon(p, bx + 10, by + (bh - 18) / 2, true, 18);
			p.Font(3);
			p.Text(bx + 36, by, bw - 44, bh, label, pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			p.AddHit(bx, by, bw, bh, kShowProblems, 0);
		}
	}
	{
		// The caption ends before the profile and the problems that share its line.
		ScreenLayout caption = l;
		caption.w = captionRight + l.margin;
		if (st.restartNeeded) PaintScreenInfo(p, caption, "The game will restart to apply the content changes.", pal::redText);
		else if (st.tab == 2) PaintScreenInfo(p, caption, "Source: GameBanana");
		else PaintScreenInfo(p, caption, Tr("Content profile: ") + ModsProfileLabel(ModsMountedProfile().name));
	}
	if (st.tab == 2) {
		PaintExplore(p, st);
	}
	else {
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
	const int listX = l.x, listW = l.listW;
	{
		const int gap = 12, filtersW = 170, sortW = 240, searchW = listW - filtersW - sortW - 2 * gap;
		PaintField(p, st, listX, l.top, searchW, kBarH, st.searchText, "Search your content...", st.focus == kSearchField, false, kFocusSearch, mx, my);
		const int n = CatalogFilterSize(st.catFilter);
		const std::string label = n ? Tr("Filters") + " (" + std::to_string(n) + ")" : Tr("Filters");
		p.Button(listX + searchW + gap, l.top, filtersW, kBarH, label, kCatPanel, 0, mx, my, true, n > 0 || st.catOpen, 0);
		Combo(p, st, listX + listW - sortW, l.top, sortW, kBarH, Tr(kSortNames[st.sort]), kDropSort);
	}

	const int chipsY = l.top + kBarH + 12, chipsH = 30;
	int tx = listX;
	int counts[kStateCount];
	CountStates(st, st.catFilter, counts);
	for (int k = 0; k < kStateCount; k++) {
		const int count = counts[k];
		if (k == kStateProblems && count == 0 && st.stateFilter != kStateProblems) continue;
		const bool on = st.stateFilter == k;
		const char *names[] = {"All", "Active", "Disabled", "With problems"};
		const std::string label = Tr(names[k]) + " (" + std::to_string(count) + ")";
		p.Font(on ? 1 : 0);
		const int w = p.TextWidth(label) + 22;
		const int icon = k == kStateProblems ? 22 : 0;
		const bool hov = p.Hover(tx, chipsY, w + icon, chipsH, mx, my);
		const COLORREF c = k == kStateProblems ? pal::bad : on ? pal::redText : hov ? pal::text : pal::textDim;
		if (k == kStateProblems) PaintAlertIcon(p, tx, chipsY + (chipsH - 16) / 2, true, 16);
		const int textX = tx + icon;
		p.Text(textX, chipsY, w, chipsH - 2, label, c);
		if (on) p.Fill(textX, chipsY + chipsH - 2, w - 22, 2, c);
		p.AddHit(tx, chipsY, w + icon, chipsH, kStateFilter, k);
		tx += w + icon + 12;
	}

	const int listY = chipsY + chipsH + 10;
	// Down to the card at its right; the bar of the marked packs takes its foot only while some are marked.
	const int listBottom = st.marked.empty() ? l.cardBottom : l.selectionBarY - 10;
	const int listH = listBottom - listY;
	const int rows = std::max(1, listH / kRowH);
	g_installedRows = rows;
	PaintGlassCard(p, listX, listY, listW, listH);
	const std::vector<int> view = VisibleRows(st);
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
	const int switchX = listX + listW - 24 - kBigToggleW, switchHitW = kBigToggleW + 12;
	const int nameX = listX + 116, catW = 170, catX = switchX - 96 - catW, nameW = catX - 16 - nameX;
	for (int v = st.list.scroll; v < static_cast<int>(view.size()) && v < st.list.scroll + rows; v++) {
		const int i = view[v];
		const Row &r = st.rows[i];
		const int y = listY + (v - st.list.scroll) * kRowH;
		const ModInfo *m = FindModIn(st.mods, r.folder);
		const bool pending = Pending(st, r.folder);
		const bool marked = IsMarked(st, r.folder);
		const bool sel = i == st.selected;
		if (v > st.list.scroll) p.Fill(listX + 1, y, listW - 2, 1, pal::hairline);
		if (marked) p.Fill(listX + 1, y, listW - 2, kRowH, pal::hairline);
		else if (p.Hover(listX, y, listW, kRowH, mx, my)) p.Fill(listX + 1, y, listW - 2, kRowH, pal::selected);
		if (sel) {
			p.Frame(listX + 1, y, listW - 2, kRowH, pal::edge);
			p.Fill(listX + 1, y, 3, kRowH, pal::red);
		}
		p.AddHit(listX + 40, y, switchX - listX - 46, kRowH, kRowClick, i);

		const int boxX = listX + 14, boxY = y + (kRowH - 20) / 2;
		const bool boxHov = p.Hover(listX + 4, y, 36, kRowH, mx, my);
		p.Fill(boxX, boxY, 20, 20, pal::bg);
		p.Frame(boxX, boxY, 20, 20, boxHov ? pal::text : pal::edge);
		if (marked) {
			p.Line(boxX + 4, boxY + 10, boxX + 8, boxY + 15, pal::redText, 2);
			p.Line(boxX + 8, boxY + 15, boxX + 16, boxY + 5, pal::redText, 2);
		}
		p.AddHit(listX + 4, y, 36, kRowH, kMark, i);

		const int thumbX = listX + 46, thumbW = 56, thumbH = 32, thumbY = y + (kRowH - thumbH) / 2;
		const std::shared_ptr<const GbMod> io = m && m->gbId ? GbInfo(m->gbId) : nullptr;
		const std::shared_ptr<const GbImage> img = io ? GbLogo(io->logoUrl) : nullptr;
		if (img) p.Image(thumbX, thumbY, thumbW, thumbH, img->bgra.data(), img->width, img->height);
		else p.Fill(thumbX, thumbY, thumbW, thumbH, m && m->gameplay ? pal::edge : m && m->kind == kModContent ? RGB(34, 60, 52) : pal::hairline);

		std::string name = !m ? r.folder + Tr(" (not installed)")
			: m->version.empty() || IsDateVersion(m->version) || m->name.find(m->version) != std::string::npos ? m->name : m->name + " " + m->version;
		p.Font(r.active ? 1 : 0);
		p.Text(nameX, y, nameW, kRowH, name, pending || !m ? pal::bad : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		if (m) {
			const int cat = ModMainCategory(*m);
			const int group = CatalogCategoryGroup(cat);
			p.Text(catX, y, catW, kRowH, Tr(CatalogCategoryLabel(cat)), group == 0 ? pal::redText : group == 1 ? RGB(140, 196, 176) : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		int ix = switchX - 10;
		const auto icon = [&](const std::string &glyph, COLORREF c, const std::string &tip) {
			p.Font(1);
			const int w = p.TextWidth(glyph) + 10;
			ix -= w;
			p.Text(ix, y, w, kRowH, glyph, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(ix, y, w, kRowH, mx, my)) SetTip(tip, ix, y + kRowH);
		};
		if (pending) {
			p.Font(1);
			p.Text(switchX - 6, y, switchHitW, kRowH, "\xE2\x9C\x95", pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(switchX - 6, y, switchHitW, kRowH, mx, my)) SetTip("It will be uninstalled when the game restarts", switchX - 300, y + kRowH);
		}
		else if (m) {
			if (RowHasProblems(st, r)) icon("!", pal::bad, "It has problems: select it to see them");
			CatalogEntry upd;
			if (FindUpdateFor(st, *m, upd)) icon("\xE2\x86\x91", pal::text, Tr("Update available: version ") + upd.version);
			if (m->newSinceStart) icon("\xE2\x9C\xA6", pal::ok, "Recently installed content");
		}
		if (!pending && !m) {
			p.Font(1);
			p.Text(switchX - 6, y, switchHitW, kRowH, "\xE2\x9C\x95", pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(switchX - 6, y, switchHitW, kRowH, mx, my)) SetTip("Not installed: click to remove it from the profile", switchX - 360, y + kRowH);
			p.AddHit(switchX - 6, y, switchHitW, kRowH, kForget, i);
		}
		else if (!pending) {
			const bool hov = p.Hover(switchX - 6, y, switchHitW, kRowH, mx, my);
			PaintBigToggle(p, switchX, y + (kRowH - kBigToggleH) / 2, r.active, true, hov);
			if (hov) SetTip(r.active ? "Enabled: click to disable" : "Disabled: click to enable", switchX - 180, y + kRowH);
			p.AddHit(switchX - 6, y, switchHitW, kRowH, kToggle, i);
		}
	}
	{
		ListGeom geom;
		ScrollbarPaint(p, listX + listW - 8, listY, listH, static_cast<int>(view.size()), rows, st.list, geom, mx, my);
		g_listGeomOut = geom;
	}
	if (!st.marked.empty()) {
		const int by = l.selectionBarY;
		const std::string count = TrFmt(st.marked.size() == 1 ? "%s selected" : "%s selected:", {std::to_string(st.marked.size())});
		const char *labels[] = {"Enable", "Disable", "Uninstall...", "Clear selection"};
		const int actions[] = {kBulkOn, kBulkOff, kBulkUninstall, kClearMarks};
		int font = 0;
		int total = 0;
		for (; font <= 3; font += 3) {
			p.Font(font);
			total = p.TextWidth(count) + 14;
			for (const char *label : labels) total += p.TextWidth(Tr(label)) + 28 + 8;
			if (total <= listW) break;
		}
		p.Font(font);
		const int cw = p.TextWidth(count) + 14;
		p.Text(listX, by, cw, kBarH, count, pal::text);
		int bx = listX + cw;
		for (int k = 0; k < 4; k++) {
			p.Font(font);
			const int w = p.TextWidth(Tr(labels[k])) + 28;
			p.Button(bx, by, w, kBarH, labels[k], actions[k], 0, mx, my, !st.busy, false, font);
			bx += w + 8;
		}
	}

	const int pad = 24, cardY = l.top, cardH = l.cardBottom - l.top;
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
			const bool active = it != st.rows.end() && it->active;
			p.Text(dx, y, dw, 28, std::string(active ? "\xE2\x97\x8F  " : "\xE2\x97\x8B  ") + (mm ? mm->name : folder), bad ? pal::bad : active ? pal::text : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 28;
		}
		if (y > listEnd) p.Text(dx, y, dw, 28, "...", pal::textDim);
		p.Font(3);
		p.TextWrapped(dx, bottom - 56, dw, 56,
			problems ? TrFmt("%s of them have problems. Use the bar under the list to enable, disable or uninstall them all.", {std::to_string(problems)})
					 : std::string("Use the bar under the list to enable, disable or uninstall them all."),
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
	const std::shared_ptr<const GbMod> io = m->gbId ? GbInfo(m->gbId) : nullptr;
	int y = top;
	p.Font(2);
	p.Text(dx, y, dw, 40, m->name, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	y += 44;
	std::string by;
	for (size_t i = 0; i < m->authors.size(); i++) by += (i ? ", " : "") + m->authors[i];
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
	const auto isIt = st.issues->find(m->folder);
	if (isIt != st.issues->end()) {
		const ModIssues &is = isIt->second;
		for (size_t k = 0; k < is.problems.size() && k < 3; k++) {
			PaintAlertIcon(p, dx, y + 3, true, 16);
			y += p.TextWrapped(dx + 24, y, dw - 24, 66, is.problems[k], pal::bad);
		}
		for (const std::string &notice : is.notices) {
			if (y > textLimit) break;
			p.Font(3);
			y += 4 + p.TextWrapped(dx, y + 4, dw, 46, notice, pal::redText);
		}
		for (size_t k = 0; k < is.overlaps.size(); k++) {
			const ModOverlap &ov = is.overlaps[k];
			if (ov.problem) continue;
			if (y > textLimit) break;
			p.Font(3);
			y += 4 + p.TextWrapped(dx, y + 4, dw, 46, OverlapText(ov), pal::redText);
			if (!ov.choosable) continue;
			if (ov.canUseMine || ov.canUseTheirs) {
				const int bw = (dw - 8) / 2;
				const int arg = static_cast<int>(k) * 2;
				int bx = dx;
				if (ov.canUseMine) {
					p.Button(bx, y + 6, bw, 32, "Use this pack's files", kUseFiles, arg, mx, my, !st.busy, false, 0);
					bx += bw + 8;
				}
				if (ov.canUseTheirs) p.Button(bx, y + 6, bw, 32, TrFmt("Use the files of %s", {ov.name}), kUseFiles, arg + 1, mx, my, !st.busy, false, 0);
				y += 42;
			}
			else {
				y += 4 + p.TextWrapped(dx, y + 4, dw, 46, "They are inside .pak files, so you can't choose which ones are used", pal::textDim);
			}
		}
		if (!is.files.empty() || !is.values.empty()) {
			const std::string files = std::to_string(is.files.size()), values = std::to_string(is.values.size());
			std::string label = TrFmt("Shared files (%s) and values (%s)", {files, values});
			if (is.values.empty()) label = TrFmt("Shared files (%s)", {files});
			else if (is.files.empty()) label = TrFmt("Shared values (%s)", {values});
			Combo(p, st, dx, y + 6, dw, 32, label, kDropClashFiles);
			y += 42;
		}
	}
	const int textBottom = mainY - 12;
	if (textBottom - y > 50) {
		std::string notes, noteVersion;
		if (m->gbId) GbChangelog(m->gbId, m->version, notes, noteVersion);
		p.Fill(dx, y + 4, dw, 1, pal::hairline);
		const bool own = !m->description.empty() && (m->hasManifest || !io || io->descriptionHtml.empty());
		PaintRich(p, st, dx, y + 12, dw, textBottom - y - 12, own || !io ? "" : io->descriptionHtml,
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

void PaintField(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, const std::string &placeholder,
	bool focused, bool secret, int action, int mx, int my)
{
	(void)mx;
	(void)my;
	PaintEditField(p, x, y, w, h, text, placeholder, focused, secret, st.edit, action);
}

void PaintFooter(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const PanelLayout l = PanelLayoutFor(st);
	const int by = l.footY, bh = kFootH, gap = 12;
	const int installW = 220;
	p.Button(l.x, by, installW, bh, "Install file...", kPickArchive, 0, mx, my);
	int left = l.x + installW + gap;
	const int updates = CountUpdates(st);
	if (updates) {
		const int w = 260;
		p.Button(left, by, w, bh, Tr("Update all (") + std::to_string(updates) + ")", kUpdateAll, 0, mx, my, !st.dl.active, true);
		left += w + gap;
	}
	int right = l.x + l.inner;
	if (st.restartNeeded) {
		p.Font(1);
		const std::string label = Tr("Apply and restart");
		const int w = p.CapsWidth(label, 1) + 56;
		right -= w;
		p.Button(right, by, w, bh, label, kApply, 0, mx, my, true, true);
		right -= gap;
	}
	if (st.tab == 2 && st.ioDetail < 0 && !st.io.mods.empty()) {
		const int pages = std::max(1, (st.io.total + kIoPerPage - 1) / kIoPerPage);
		const int first = st.ioPage * kIoPerPage + 1;
		const int last = std::min(st.io.total, first + kIoPerPage - 1);
		const int btnW = 150;
		const int textW = std::max(0, std::min(320, right - left - 2 * btnW - 16));
		const int total = 2 * btnW + textW + 16;
		const int x0 = left + std::max(0, (right - left - total) / 2);
		p.Button(x0, by, btnW, bh, "Previous", kIoPrev, 0, mx, my, st.ioPage > 0 && !st.io.loading);
		p.Button(x0 + btnW + 8 + textW + 8, by, btnW, bh, "Next", kIoNext, 0, mx, my, st.ioPage + 1 < pages && !st.io.loading);
		if (textW > 60) {
			p.Font(3);
			std::string text = TrFmt("%s-%s of %s", {std::to_string(first), std::to_string(last), std::to_string(st.io.total)}) + "   " +
				TrFmt("Page %s of %s", {std::to_string(st.ioPage + 1), std::to_string(pages)});
			if (p.TextWidth(text) > textW) text = TrFmt("Page %s of %s", {std::to_string(st.ioPage + 1), std::to_string(pages)});
			p.Text(x0 + btnW + 8, by, textW, bh, text, pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}
}

void PaintInstalledFilterPanel(Painter &p, const State &st)
{
	if (!st.catOpen || st.tab != 0) return;
	PaintFacetPanel(p, PanelLayoutFor(st), st.catFilter, InstalledCounts(st), kCatChip, kCatClear, kCatPanel, st.mouseX, st.mouseY);
}
}

