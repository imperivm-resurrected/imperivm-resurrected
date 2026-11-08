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

void PaintTip(Painter &p)
{
	if (g_tip.text.empty()) return;
	p.Font(3);
	const int w = std::min(360, p.TextWidth(g_tip.text) + 16);
	const int x = std::max(8, std::min(kDesignW - w - 8, g_tip.x)), y = std::min(kDesignH - 30, g_tip.y + 2);
	p.Fill(x, y, w, 24, pal::dialog);
	p.Frame(x, y, w, 24, pal::edge);
	p.Text(x + 8, y, w - 12, 24, g_tip.text, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
		if ((st.drop == kDropModMenu || st.drop == kDropRowMenu) && i + 1 == n) items[i].color = kBad;
		if (st.drop == kDropMenu && i == 2) items[i].color = kBad;
		if (st.drop == kDropProfile && (i == 1 || i + 1 == n)) items[i].color = kGold;
	}
	if (st.drop == kDropClashFiles) {
		if (const ModIssues *is = SelectedIssues(st)) {
			const int files = static_cast<int>(is->files.size());
			for (int i = 0; i < n && i < files + static_cast<int>(is->values.size()); i++) {
				const int kind = i < files ? is->files[i].kind : is->values[i - files].kind;
				items[i].color = kind == kModRules ? kGold : kind == kModContent ? RGB(140, 196, 176) : kText;
			}
		}
	}
	const ui::DropLayout d = PaintDropMenu(p, anchor, items, DropCurrent(st), st.dropScroll, kDesignW, kDesignH, kDropPick, kDropClose,
		st.mouseX, st.mouseY, st.drop == kDropClashFiles ? kDesignW - kDetailX - 16 : st.drop == kDropVersions ? 360 : 180);
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
		p.Fill(x, y, w, h, RGB(22, 17, 12));
		if (url.empty()) {
			p.Font(3);
			p.Text(x, y, w, h, "No image", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
	}
	g_rich.view = h;
	g_rich.rect = p.R(x, y, w, h);
	OffsetRect(&g_rich.rect, p.ox, p.oy);
	g_rich.rectValid = true;
	g_rich.rectTick = GetTickCount();
	if (g_rich.blocks.empty()) {
		p.Font(baseFont);
		p.Text(x, y, w, 20, "No description.", kDim);
		g_rich.geom = ListGeom();
		return;
	}
	const int barW = 6, tw = w - barW - 8;
	struct Pos {
		int y, h, indent;
	};
	std::vector<Pos> pos;
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
		pos.push_back({total + before, bh, indent});
		total += before + bh + after;
	}
	const int maxScroll = std::max(0, total - h);
	g_rich.list.scroll = std::max(0, std::min(g_rich.list.scroll, maxScroll));
	const int scroll = g_rich.list.scroll;
	p.Clip(x, y, w, h);
	for (size_t i = 0; i < g_rich.blocks.size(); i++) {
		const RichBlock &b = g_rich.blocks[i];
		const Pos &ps = pos[i];
		if (ps.y + ps.h < scroll || ps.y > scroll + h) continue;
		const int by = y + ps.y - scroll;
		if (b.kind == 4) {
			p.Fill(x, by + 4, tw, 1, pal::line);
			continue;
		}
		if (b.kind == 5) continue;
		const int font = b.kind == 1 ? 2 : b.kind == 2 || b.bold ? 1 : baseFont;
		const int top = std::max(by, y), bottom = std::min(by + ps.h, y + h);
		const bool hov = !b.link.empty() && bottom > top && p.Hover(x + ps.indent, top, tw - ps.indent, bottom - top, st.mouseX, st.mouseY);
		const COLORREF col = !b.link.empty() ? (hov ? RGB(180, 214, 255) : kLink) : b.kind == 1 || b.kind == 2 ? kGold : kText;
		p.Font(font);
		if (b.kind == 3) p.Text(x + ps.indent - 14, by, 14, ps.h, b.marker, kGold, DT_LEFT | DT_TOP | DT_SINGLELINE);
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
	const int x = 16, y = kContentY, w = kDesignW - 32, h = kFootY - 8 - kContentY;
	p.Fill(x, y, w, h, pal::wellRaised);
	p.Frame(x, y, w, h, pal::edge);
	const int iw = 176, ih = 99;
	PaintIoImage(p, x + 10, y + 10, iw, ih, m.logoUrl);
	p.Frame(x + 10, y + 10, iw, ih, pal::line);
	const int dx = x + iw + 24, dw = w - iw - 34;
	int ty = y + 8;
	p.Font(2);
	p.Text(dx, ty, dw, 30, m.name, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	ty += 32;
	p.Font(0);
	p.Text(dx, ty, dw, 20, Tr("Author: ") + (m.author.empty() ? Tr("unknown") : m.author), kText);
	ty += 22;
	p.Font(3);
	p.Text(dx, ty, dw, 18, (m.entry.version.empty() ? Tr("No version") : Tr("Version ") + m.entry.version) +
		(m.entry.bytes ? "  ·  " + Tr("download") + " " + HumanBytes(m.entry.bytes) : ""), kDim);
	ty += 18;
	p.Text(dx, ty, dw, 18, Thousands(m.downloads) + " " + Tr("downloads") + "  ·  " + Thousands(static_cast<uint64_t>(m.likes)) +
		" " + Tr("likes") + "  ·  " + Thousands(m.views) + " " + Tr("views"), kDim);
	ty += 18;
	const std::string tags = IoTagsText(m);
	if (!tags.empty()) {
		ty += 2 + p.TextWrapped(dx, ty + 2, dw, 36, tags, kGold);
	}
	if (m.dependencies) {
		ty += 4;
		p.Text(dx, ty, dw, 18, "Needs other content: it is installed with it", kGold);
		ty += 18;
	}
	int descY = std::max(y + 10 + ih, ty) + 8;
	p.Fill(x + 10, descY - 4, w - 20, 1, pal::lineDim);
	if (m.shots.size() > 1) {
		const int tw = 124, th = 70, tgap = 8;
		const int perRow = std::max(1, (w - 20 + tgap) / (tw + tgap));
		for (size_t i = 0; i < m.shots.size() && static_cast<int>(i) < perRow; i++) {
			const int sx = x + 10 + static_cast<int>(i) * (tw + tgap);
			const bool hov = p.Hover(sx, descY, tw, th, mx, my);
			PaintIoImage(p, sx, descY, tw, th, m.shots[i].thumb);
			p.Frame(sx, descY, tw, th, hov ? pal::edgeHot : pal::line);
			p.AddHit(sx, descY, tw, th, kIoShotOpen, static_cast<int>(i));
		}
		descY += th + 10;
		p.Fill(x + 10, descY - 4, w - 20, 1, pal::lineDim);
	}
	std::string notes, noteVersion;
	if (m.id) GbChangelog(m.id, "", notes, noteVersion);
	PaintRich(p, st, x + 10, descY, w - 20, y + h - 46 - descY, m.descriptionHtml, !m.description.empty() ? m.description : m.summary,
		Tr("What's new in version ") + noteVersion, notes, 0);
	const int state = IoInstalledState(st, m);
	const std::string label = !m.hasFile ? "No file" : state == 1 ? "Already installed" : state == 2 ? Tr("Update to ") + m.entry.version : "Install";
	p.Button(x + w - 520, y + h - 40, 180, 30, label, kIoInstall, index, mx, my, m.hasFile && state != 1 && !st.dl.active, true);
	Combo(p, st, x + w - 520 - 176, y + h - 40, 170, 30, "Other versions", kDropVersions);
	p.Button(x + w - 334, y + h - 40, 170, 30, "View on GameBanana", kIoPage, index, mx, my, !m.page.empty());
	p.Button(x + w - 158, y + h - 40, 148, 30, "Back", kIoBack, 0, mx, my);
}

void PaintShotViewer(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	if (st.tab != 2 || st.ioDetail < 0 || st.ioDetail >= static_cast<int>(st.io.mods.size())) return;
	const GbMod &m = st.io.mods[st.ioDetail];
	if (st.ioShot >= 0 && st.ioShot < static_cast<int>(m.shots.size())) {
		const int n = static_cast<int>(m.shots.size());
		p.hits->clear();
		p.AddHit(0, 0, kDesignW, kDesignH, kIoShotClose, 0);
		p.Fill(0, 0, kDesignW, kDesignH, RGB(14, 11, 8));
		p.Frame(0, 0, kDesignW, kDesignH, kGold);
		p.Frame(3, 3, kDesignW - 6, kDesignH - 6, pal::line);
		p.Font(2);
		p.Text(24, 10, kDesignW - 240, 34, m.name, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Button(kDesignW - 16 - 100, 12, 100, 30, "Close", kIoShotClose, 0, mx, my);
		if (n > 1) {
			const int cw = 78, cx = kDesignW - 16 - 100 - 8 - cw;
			p.Fill(cx, 12, cw, 30, RGB(30, 24, 16));
			p.Frame(cx, 12, cw, 30, pal::lineDim);
			p.Font(3);
			p.Text(cx, 12, cw, 30, std::to_string(st.ioShot + 1) + " / " + std::to_string(n), kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		p.Fill(16, 52, kDesignW - 32, 1, pal::line);
		const int ix = 16, iy = 62, iw = kDesignW - 32, ih = 462;
		const GbShot &shot = m.shots[st.ioShot];
		RECT r = PaintIoImageFit(p, ix, iy, iw, ih, shot.big, true);
		const bool loaded = r.right > r.left;
		if (!loaded) {
			p.Fill(ix, iy, iw, ih, RGB(10, 8, 6));
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
			const int box = 64, bx = ix + (iw - box) / 2, by = iy + (ih - box) / 2;
			p.Fill(bx, by, box, box, RGB(14, 11, 8));
			p.Frame(bx, by, box, box, pal::lineDim);
			p.Spinner(bx + box / 2, by + box / 2, 18, 7, GetTickCount());
		}
		if (n > 1) {
			const int bw = 40, bh = 88, by = iy + ih / 2 - bh / 2;
			const COLORREF arrow = RGB(240, 226, 180);
			p.Button(ix + 10, by, bw, bh, "", kIoShotStep, -1, mx, my);
			p.ArrowLeft(ix + 10 + bw / 2 - 6, by + bh / 2 - 10, 12, 20, arrow);
			p.Button(ix + iw - 10 - bw, by, bw, bh, "", kIoShotStep, 1, mx, my);
			p.ArrowRight(ix + iw - 10 - bw + bw / 2 - 6, by + bh / 2 - 10, 12, 20, arrow);
		}
		const int tw = 100, th = 56, tgap = 8;
		const int shown = std::min(n, std::max(1, (kDesignW - 60 + tgap) / (tw + tgap)));
		const int stripW = shown * tw + (shown - 1) * tgap;
		const int tx0 = (kDesignW - stripW) / 2, ty = iy + ih + 14;
		for (int i = 0; i < shown; i++) {
			const int sx = tx0 + i * (tw + tgap);
			const bool cur = i == st.ioShot, hov = p.Hover(sx, ty, tw, th, mx, my);
			PaintIoImage(p, sx, ty, tw, th, m.shots[i].thumb);
			p.Frame(sx, ty, tw, th, cur ? RGB(255, 226, 150) : hov ? pal::edgeHot : pal::divider);
			if (cur) p.Frame(sx + 1, ty + 1, tw - 2, th - 2, RGB(255, 226, 150));
			p.AddHit(sx, ty, tw, th, kIoShotOpen, i);
		}
	}
}
}
}

namespace modui_impl {
void PaintFacetPanel(Painter &p, const CatalogFilter &f, const CatalogCounts &counts, int toggleAction, int clearAction, int closeAction, int mx, int my)
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
	const int x = 16, y = kContentY + 32, w = kDesignW - 32, labelW = 200, chipH = 28, rowStep = 34;
	std::vector<Chip> chips;
	std::vector<Line> lines;
	int cy = y + 16;
	p.Font(0);
	const auto addLine = [&](const char *label, int group, uint32_t mask, const std::vector<std::pair<int, std::string>> &items, const char *empty,
							 uint32_t disabled = 0) {
		lines.push_back({label, cy, items.empty() ? empty : nullptr});
		int cx = x + labelW;
		for (const auto &it : items) {
			const int cw = std::max(34, p.TextWidth(it.second) + 28);
			if (cx + cw > x + w - 16) {
				cx = x + labelW;
				cy += rowStep;
			}
			chips.push_back({group * kIoFilterGroupStride + it.first, cx, cy, cw, it.second, ((mask >> it.first) & 1) != 0, !((disabled >> it.first) & 1)});
			cx += cw + 8;
		}
		cy += rowStep + 8;
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
	const int h = cy - y + 46;
	p.hits->clear();
	p.AddHit(0, 0, kDesignW, kDesignH, closeAction, 0);
	p.Fill(x + 4, y + 4, w, h, pal::shadow);
	p.Fill(x, y, w, h, pal::dialog);
	p.Frame(x, y, w, h, pal::edgeStrong);
	p.AddHit(x, y, w, h, kNone, 0);
	for (const Line &l : lines) {
		p.Font(1);
		p.Text(x + 16, l.y, labelW - 24, chipH, l.label, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (l.empty) {
			p.Font(3);
			p.Text(x + labelW, l.y, w - labelW - 16, chipH, l.empty, kDim);
		}
	}
	for (const Chip &c : chips) {
		const bool hov = c.enabled && p.Hover(c.x, c.y, c.w, chipH, mx, my);
		p.Fill(c.x, c.y, c.w, chipH, c.on ? (hov ? RGB(150, 104, 40) : RGB(122, 82, 30)) : (hov ? pal::fieldHover : pal::field));
		p.Frame(c.x, c.y, c.w, chipH, c.on ? pal::focus : hov ? pal::edgeHot : c.enabled ? pal::edge : pal::divider);
		p.Font(c.on ? 1 : 0);
		p.Text(c.x, c.y, c.w, chipH, c.text, c.on ? RGB(255, 244, 210) : c.enabled ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (c.enabled) p.AddHit(c.x, c.y, c.w, chipH, toggleAction, c.arg);
	}
	const int n = CatalogFilterSize(f);
	p.Button(x + w - 296, y + h - 40, 140, 30, "Clear filters", clearAction, 0, mx, my, n > 0);
	p.Button(x + w - 148, y + h - 40, 132, 30, "Close", closeAction, 0, mx, my, true, true);
}

void PaintFilterPanel(Painter &p, const State &st)
{
	if (!st.ioFilterOpen || st.tab != 2) return;
	PaintFacetPanel(p, st.ioFilter, st.io.counts, kIoFilterToggle, kIoFilterClear, kIoFilterClose, st.mouseX, st.mouseY);
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
	const int nFilters = CatalogFilterSize(st.ioFilter);
	PaintField(p, st, 16, kContentY, 504, 26, st.ioSearch, "Search by name, author or category", st.focus == kIoSearchField, false, kIoFocusSearch, mx, my);
	p.Button(528, kContentY, 160, 26, nFilters ? Tr("Filters (") + std::to_string(nFilters) + ")" : "Filters", kIoFilterOpen, 0, mx, my, true, nFilters > 0, 0);
	Combo(p, st, 696, kContentY, 170, 26, kIoSortNames[st.ioSort], kDropIoSort);
	p.Button(874, kContentY, 110, 26, "Refresh", kIoRefresh, 0, mx, my, !io.loading, false, 0);
	{
		const int trackX = 16, trackW = kDesignW - 32, trackY = kContentY + 29;
		p.Fill(trackX, trackY, trackW, 2, RGB(43, 34, 20));
		if (io.loading) {
			const int segW = 240, cycle = 1400;
			const int pos = -segW + (trackW + segW) * static_cast<int>(GetTickCount() % cycle) / cycle;
			const int x0 = std::max(0, pos), x1 = std::min(trackW, pos + segW);
			if (x1 > x0) p.Fill(trackX + x0, trackY, x1 - x0, 2, kGold);
		}
	}

	const int gx = 16, gy = kContentY + 34, cw = 237, ch = 222, gap = 4;
	if (io.mods.empty()) {
		p.Fill(gx, gy, kDesignW - 32, kFootY - 8 - gy, pal::well);
		p.Frame(gx, gy, kDesignW - 32, kFootY - 8 - gy, pal::line);
		if (io.loading || !st.ioRequested) {
			p.Font(0);
			p.Text(gx, gy, kDesignW - 32, kFootY - 8 - gy, "Loading...", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			return;
		}
		p.Font(0);
		std::string text = !io.error.empty() ? Tr("Could not connect to GameBanana: ") + Tr(io.error)
			: !st.ioSearch.empty() || nFilters ? "No content matches the search."
												: "There is no content published for Imperivm on GameBanana yet. Be the first to upload some!";
		p.TextWrapped(gx + 20, gy + 20, kDesignW - 72, 60, text, io.error.empty() ? kDim : kBad);
		if (!io.error.empty() && !io.loading) p.Button(gx + 20, gy + 80, 160, 28, "Retry", kIoRetry, 0, mx, my);
		return;
	}
	for (size_t i = 0; i < io.mods.size() && i < static_cast<size_t>(kIoPerPage); i++) {
		const GbMod &m = io.mods[i];
		const int x = gx + static_cast<int>(i % 4) * (cw + gap), y = gy + static_cast<int>(i / 4) * (ch + gap);
		const bool hov = p.Hover(x, y, cw, ch, mx, my);
		p.Fill(x, y, cw, ch, hov ? pal::fieldOpen : RGB(26, 21, 15));
		const int imgH = 152;
		PaintIoImage(p, x + 1, y + 1, cw - 2, imgH, m.logoUrl);
		p.Frame(x, y, cw, ch, hov ? pal::edgeHot : pal::line);
		const std::string catText = m.facts.category >= 0 ? Tr(CatalogCategoryLabel(m.facts.category)) : m.category;
		if (!catText.empty()) {
			p.Font(4);
			const std::string cat = UpperText(catText);
			const int chipW = std::min(cw - 16, p.TextWidth(cat) + 16), chipH = 22, chipX = x + 8, chipY = y + 1 + imgH - chipH - 8;
			p.Fill(chipX, chipY, chipW, chipH, RGB(22, 17, 12));
			p.Text(chipX + 8, chipY, chipW - 12, chipH, cat, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		const int state = IoInstalledState(st, m);
		if (state == 1 || state == 2) {
			p.Font(4);
			const std::string badge = UpperText(Tr(state == 1 ? "Already installed" : "Update available"));
			const int badgeW = p.TextWidth(badge) + 16;
			p.Fill(x + cw - 8 - badgeW, y + 9, badgeW, 20, RGB(22, 17, 12));
			p.Text(x + cw - 8 - badgeW, y + 9, badgeW, 20, badge, state == 1 ? kOk : kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		p.Font(1);
		p.Text(x + 10, y + imgH + 8, cw - 20, 22, UpperText(m.name), kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(x + 10, y + imgH + 31, cw - 20, 16, Tr("By") + " " + m.author, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(x + 10, y + imgH + 50, cw - 20, 16, Thousands(m.downloads) + " " + Tr(m.downloads == 1 ? "download" : "downloads"), kDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.AddHit(x, y, cw, ch, kIoOpen, static_cast<int>(i));
	}
}
}
}

namespace modui_impl {
void PaintPanel(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	PaintWindowFrame(p, kDesignW, kDesignH);
	PaintTitle(p, 20, 12, 130, "Content");
	p.Font(0);
	p.Text(152, 16, 50, 28, "Profile", kDim);
	if (st.nameMode != kNameNone) {
		const char *what = st.nameMode == kNameRename ? "New name" : "Profile name";
		PaintField(p, st, 200, 16, 266, 28, st.nameText, what, st.focus == kNameField, false, kFocusName, mx, my);
		p.Button(472, 16, 90, 28, st.nameMode == kNameRename ? "Rename" : "Save", kNameOk, 0, mx, my, true, true);
		p.Button(566, 16, 86, 28, "Cancel", kNameCancel, 0, mx, my);
	}
	else {
		Combo(p, st, 200, 16, 236, 28, ModsProfileLabel(st.profile.name), kDropProfile);
		if (st.profile.name == kNoProfileName) {
			p.Button(442, 16, 170, 28, "Save as profile", kSaveAs, 0, mx, my, !st.profile.mods.empty());
		}
		else if (st.profile.name != kOriginalName) {
			p.Button(442, 16, 36, 28, "...", kDropOpen, kDropMenu, mx, my);
			if (st.drop == kDropMenu) g_drop.anchor = {442, 16, 478, 44};
		}

	}
	const bool overlay = st.drop != kDropNone || st.confirm || (st.tab == 2 && (st.ioFilterOpen || st.ioShot >= 0)) || (st.tab == 0 && st.catOpen);
	p.noHover = overlay;
	PaintRule(p, 54, kDesignW);
	PaintTabBar(p, kDesignW,
		{{Tr("Installed") + " (" + std::to_string(st.mods.size()) + ")", kTabInstalled, 0, st.tab == 0, true},
			{"Browse content", kTabExplore, 0, st.tab == 2, true}},
		mx, my);
	{
		const std::vector<std::string> &problems = st.problems;
		if (!problems.empty()) {
			const std::string label = std::to_string(problems.size()) + " " + Tr(problems.size() == 1 ? "problem in the profile" : "problems in the profile");
			p.Font(3);
			const int bw = p.TextWidth(label) + 46;
			const bool hov = p.Hover(kDesignW - 16 - bw, 60, bw, 26, st.mouseX, st.mouseY);
			p.Fill(kDesignW - 16 - bw, 60, bw, 26, hov ? RGB(86, 36, 26) : RGB(60, 26, 20));
			p.Frame(kDesignW - 16 - bw, 60, bw, 26, hov ? RGB(255, 150, 130) : kBad);
			PaintAlertIcon(p, kDesignW - 16 - bw + 8, 65, true);
			p.Font(3);
			p.Text(kDesignW - 16 - bw + 30, 60, bw - 38, 26, label, kBad, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			p.AddHit(kDesignW - 16 - bw, 60, bw, 26, kShowProblems, 0);
		}
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
	if (st.drop == kDropNone && !st.confirm) PaintTip(p);
	g_tip.text.clear();
	PaintDropList(p, st);
	PaintConfirm(p, st);
}

void PaintInstalled(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	PaintField(p, st, kListX, kContentY, 230, 24, st.searchText, "Search your content...", st.focus == kSearchField, false, kFocusSearch, mx, my);
	{
		const int n = CatalogFilterSize(st.catFilter);
		const std::string label = n ? Tr("Filters") + " (" + std::to_string(n) + ")" : Tr("Filters");
		p.Button(kListX + 236, kContentY, 120, 24, label, kCatPanel, 0, mx, my, true, n > 0 || st.catOpen, 0);
	}
	Combo(p, st, kListX + 362, kContentY, kListW - 362, 24, Tr(kSortNames[st.sort]), kDropSort);

	int tx = kListX;
	for (int k = 0; k < kStateCount; k++) {
		const int count = static_cast<int>(VisibleRows(st, k, st.catFilter).size());
		if (k == kStateProblems && count == 0 && st.stateFilter != kStateProblems) continue;
		const bool on = st.stateFilter == k;
		const char *names[] = {"All", "Active", "Disabled", "With problems"};
		const std::string label = Tr(names[k]) + " (" + std::to_string(count) + ")";
		p.Font(on ? 1 : 0);
		const int w = p.TextWidth(label) + 18;
		const bool hov = p.Hover(tx, 128, w, 24, mx, my);
		const COLORREF c = k == kStateProblems ? kBad : on ? kGold : hov ? kText : kDim;
		if (k == kStateProblems) PaintAlertIcon(p, tx, 132, true, 14);
		const int textX = k == kStateProblems ? tx + 18 : tx;
		p.Text(textX, 128, w, 24, label, c);
		if (on) p.Fill(textX, 151, w - 18, 2, c);
		p.AddHit(tx, 128, w + (k == kStateProblems ? 18 : 0), 24, kStateFilter, k);
		tx += w + (k == kStateProblems ? 18 : 0) + 8;
	}

	const int listY = 158;
	const int rows = std::max(1, (kSelectionBarY - 6 - listY) / kRowH);
	const int listH = rows * kRowH;
	g_installedRows = rows;
	p.Fill(kListX, listY, kListW, listH, pal::well);
	p.Frame(kListX, listY, kListW, listH, pal::line);
	const std::vector<int> view = VisibleRows(st);
	if (st.rows.empty()) {
		p.Font(0);
		p.TextWrapped(kListX + 12, listY + 12, kListW - 24, 120,
			"No content installed. Find it in the Browse content tab, install a .zip with \"Install file\", or "
			"copy each pack into its own folder inside \"mods\" (in the game folder): it appears here by itself.",
			kDim);
	}
	else if (view.empty()) {
		p.Font(0);
		p.TextWrapped(kListX + 12, listY + 12, kListW - 24, 60, "No content matches the search or filter.", kDim);
	}
	const int switchX = kListX + kListW - 16 - Painter::kToggleW;
	for (int v = st.list.scroll; v < static_cast<int>(view.size()) && v < st.list.scroll + rows; v++) {
		const int i = view[v];
		const Row &r = st.rows[i];
		const int y = listY + (v - st.list.scroll) * kRowH;
		const ModInfo *m = FindModIn(st.mods, r.folder);
		const bool pending = Pending(st, r.folder);
		const bool marked = IsMarked(st, r.folder);
		const bool sel = i == st.selected;
		if (marked) p.Fill(kListX + 1, y, kListW - 2, kRowH, pal::lineDim);
		else if (p.Hover(kListX, y, kListW, kRowH, mx, my)) p.Fill(kListX + 1, y, kListW - 2, kRowH, RGB(44, 34, 24));
		if (sel) p.Frame(kListX + 1, y, kListW - 2, kRowH, pal::edge);
		p.AddHit(kListX + 30, y, switchX - kListX - 36, kRowH, kRowClick, i);

		const bool boxHov = p.Hover(kListX + 4, y, 26, kRowH, mx, my);
		p.Fill(kListX + 9, y + 6, 16, 16, RGB(10, 8, 6));
		p.Frame(kListX + 9, y + 6, 16, 16, boxHov ? pal::focus : pal::edge);
		if (marked) {
			p.Line(kListX + 12, y + 14, kListX + 16, y + 18, kGold, 2);
			p.Line(kListX + 16, y + 18, kListX + 22, y + 10, kGold, 2);
		}
		p.AddHit(kListX + 4, y, 26, kRowH, kMark, i);

		const int thumbX = kListX + 32;
		const std::shared_ptr<const GbMod> io = m && m->gbId ? GbInfo(m->gbId) : nullptr;
		const std::shared_ptr<const GbImage> img = io ? GbLogo(io->logoUrl) : nullptr;
		if (img) p.Image(thumbX, y + 3, 40, 22, img->bgra.data(), img->width, img->height);
		else p.Fill(thumbX, y + 3, 40, 22, m && m->gameplay ? RGB(70, 54, 26) : m && m->kind == kModContent ? RGB(34, 60, 52) : RGB(44, 40, 36));

		std::string name = !m ? r.folder + Tr(" (not installed)")
			: m->version.empty() || IsDateVersion(m->version) || m->name.find(m->version) != std::string::npos ? m->name : m->name + " " + m->version;
		p.Font(r.active ? 1 : 0);
		p.Text(kListX + 80, y, 250, kRowH, name, pending || !m ? kBad : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		if (m) {
			const int cat = ModMainCategory(*m);
			const int group = CatalogCategoryGroup(cat);
			p.Text(kListX + 334, y, 110, kRowH, Tr(CatalogCategoryLabel(cat)), group == 0 ? kGold : group == 1 ? RGB(140, 196, 176) : kDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		int ix = switchX - 8;
		const auto icon = [&](const std::string &glyph, COLORREF c, const std::string &tip) {
			p.Font(1);
			const int w = p.TextWidth(glyph) + 6;
			ix -= w;
			p.Text(ix, y, w, kRowH, glyph, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(ix, y, w, kRowH, mx, my)) SetTip(tip, ix, y + kRowH);
		};
		if (pending) {
			p.Font(1);
			p.Text(switchX - 6, y, Painter::kToggleW + 12, kRowH, "\xE2\x9C\x95", kBad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(switchX - 6, y, Painter::kToggleW + 12, kRowH, mx, my)) SetTip("It will be uninstalled when the game restarts", switchX - 200, y + kRowH);
		}
		else if (m) {
			if (RowHasProblems(st, r)) icon("!", kBad, "It has problems: select it to see them");
			CatalogEntry upd;
			if (FindUpdateFor(st, *m, upd)) icon("\xE2\x86\x91", RGB(255, 226, 140), Tr("Update available: version ") + upd.version);
			if (m->newSinceStart) icon("\xE2\x9C\xA6", kOk, "Recently installed content");
		}
		if (!pending && !m) {
			p.Font(1);
			p.Text(switchX - 6, y, Painter::kToggleW + 12, kRowH, "\xE2\x9C\x95", kBad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (p.Hover(switchX - 6, y, Painter::kToggleW + 12, kRowH, mx, my)) SetTip("Not installed: click to remove it from the profile", switchX - 240, y + kRowH);
			p.AddHit(switchX - 6, y, Painter::kToggleW + 12, kRowH, kForget, i);
		}
		else if (!pending) {
			const bool hov = p.Hover(switchX - 6, y, Painter::kToggleW + 12, kRowH, mx, my);
			p.Toggle(switchX, y + (kRowH - Painter::kToggleH) / 2, r.active, true, hov);
			if (hov) SetTip(r.active ? "Enabled: click to disable" : "Disabled: click to enable", switchX - 120, y + kRowH);
			p.AddHit(switchX - 6, y, Painter::kToggleW + 12, kRowH, kToggle, i);
		}
	}
	{
		ListGeom geom;
		ScrollbarPaint(p, kListX + kListW - 6, listY, listH, static_cast<int>(view.size()), rows, st.list, geom, mx, my);
		g_listGeomOut = geom;
	}
	if (!st.marked.empty()) {
		const int by = kSelectionBarY;
		const std::string count = TrFmt(st.marked.size() == 1 ? "%s selected" : "%s selected:", {std::to_string(st.marked.size())});
		const char *labels[] = {"Enable", "Disable", "Uninstall...", "Clear selection"};
		const int actions[] = {kBulkOn, kBulkOff, kBulkUninstall, kClearMarks};
		int font = 0;
		int total = 0;
		for (; font <= 3; font += 3) {
			p.Font(font);
			total = p.TextWidth(count) + 10;
			for (const char *l : labels) total += p.TextWidth(Tr(l)) + 20 + 6;
			if (total <= kListW) break;
		}
		p.Font(font);
		const int cw = p.TextWidth(count) + 10;
		p.Text(kListX, by, cw, 28, count, kText);
		int bx = kListX + cw;
		for (int k = 0; k < 4; k++) {
			p.Font(font);
			const int w = p.TextWidth(Tr(labels[k])) + 20;
			p.Button(bx, by, w, 28, labels[k], actions[k], 0, mx, my, !st.busy, false, font);
			bx += w + 6;
		}
	}

	const int dx = kDetailX, dw = kDesignW - dx - 16;
	if (st.marked.size() > 1) {
		int y = kContentY;
		p.Font(2);
		p.Text(dx, y, dw, 28, TrFmt("%s content packs selected", {std::to_string(st.marked.size())}), kGold);
		y += 36;
		int problems = 0;
		p.Font(0);
		for (const std::string &folder : st.marked) {
			const ModInfo *mm = FindModIn(st.mods, folder);
			const auto it = std::find_if(st.rows.begin(), st.rows.end(), [&](const Row &r) { return r.folder == folder; });
			const bool bad = it != st.rows.end() && RowHasProblems(st, *it);
			if (bad) problems++;
			if (y > kSelectionBarY - 60) continue;
			const bool active = it != st.rows.end() && it->active;
			p.Text(dx, y, dw, 22, std::string(active ? "\xE2\x97\x8F  " : "\xE2\x97\x8B  ") + (mm ? mm->name : folder), bad ? kBad : active ? kText : kDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 22;
		}
		if (y > kSelectionBarY - 60) p.Text(dx, y, dw, 22, "...", kDim);
		p.Font(3);
		p.TextWrapped(dx, kSelectionBarY - 44, dw, 40,
			problems ? TrFmt("%s of them have problems. Use the bar under the list to enable, disable or uninstall them all.", {std::to_string(problems)})
					 : std::string("Use the bar under the list to enable, disable or uninstall them all."),
			problems ? kBad : kDim);
		return;
	}
	const ModInfo *m = nullptr;
	if (st.selected >= 0 && st.selected < static_cast<int>(st.rows.size())) {
		m = FindModIn(st.mods, st.rows[st.selected].folder);
	}
	const int btnY = kSelectionBarY + 4;
	if (!m && st.selected >= 0 && st.selected < static_cast<int>(st.rows.size())) {
		p.Font(0);
		p.TextWrapped(dx, kContentY, dw, 80, "This content pack is in the profile, but its folder is no longer in the content folder (it was probably deleted by hand).", kBad);
		p.Button(dx, btnY, 200, 28, "Remove from the profile", kForget, st.selected, mx, my, true, false, 0);
		p.Button(dx + 210, btnY, 170, 28, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
		return;
	}
	if (!m) {
		p.Font(0);
		p.TextWrapped(dx, kContentY, dw, 80, "Select a content pack to see what it is, or open the content folder to add one by hand.", kDim);
		p.Button(dx, btnY, 170, 28, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
		return;
	}
	const bool active = st.rows[st.selected].active;
	const std::shared_ptr<const GbMod> io = m->gbId ? GbInfo(m->gbId) : nullptr;
	int y = kContentY;
	p.Font(2);
	p.Text(dx, y, dw, 28, m->name, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	y += 30;
	std::string by;
	for (size_t i = 0; i < m->authors.size(); i++) by += (i ? ", " : "") + m->authors[i];
	if (by.empty() && io) by = io->author;
	p.Font(3);
	if (m->gbId) {
		const int iw = 112, ih = 63;
		PaintIoImage(p, dx, y, iw, ih, io ? io->logoUrl : "");
		p.Frame(dx, y, iw, ih, pal::line);
		const int tx2 = dx + iw + 10, tw = dw - iw - 10;
		p.Text(tx2, y + 2, tw, 18, m->version.empty() ? std::string("No version") : Tr("Version ") + m->version, kText);
		p.Text(tx2, y + 22, tw, 18, by.empty() ? std::string() : (by.find(',') == std::string::npos ? Tr("Author: ") : Tr("Authors: ")) + by, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(tx2, y + 42, tw, 18, HumanBytes(m->totalBytes), kDim);
		y += ih + 6;
	}
	else {
		p.Text(dx, y, dw, 18, (m->version.empty() ? std::string("No version") : Tr("Version ") + m->version) +
			(by.empty() ? "" : std::string("  ·  ") + (by.find(',') == std::string::npos ? Tr("Author: ") : Tr("Authors: ")) + by) + "  ·  " + HumanBytes(m->totalBytes), kText,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += 18;
	}
	p.Text(dx, y, dw, 18, m->gameplay ? "Gameplay: everyone must have it online" : m->kind == kModContent ? "Maps and campaigns: only needed by whoever plays them" : "Cosmetic: does not affect games",
		m->gameplay ? kGold : m->kind == kModContent ? RGB(140, 196, 176) : kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	y += 18;
	{
		const auto it = st.usedBy.find(m->folder);
		std::string used;
		if (it != st.usedBy.end()) {
			for (const std::string &n : it->second) used += (used.empty() ? "" : ", ") + ModsProfileLabel(n);
		}
		p.Text(dx, y, dw, 18, used.empty() ? "Not in any profile" : Tr("In profiles: ") + used, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += 18;
	}
	if (m->newSinceStart) {
		p.Text(dx, y, dw, 18, "Recently installed content", kOk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += 18;
	}
	const int textLimit = btnY - 38 - 110;
	const auto isIt = st.issues->find(m->folder);
	if (isIt != st.issues->end()) {
		const ModIssues &is = isIt->second;
		for (size_t k = 0; k < is.problems.size() && k < 3; k++) {
			PaintAlertIcon(p, dx, y + 2, true, 14);
			y += p.TextWrapped(dx + 20, y, dw - 20, 54, is.problems[k], kBad);
		}
		for (const std::string &notice : is.notices) {
			if (y > textLimit) break;
			p.Font(3);
			y += 2 + p.TextWrapped(dx, y + 2, dw, 36, notice, kGold);
		}
		// Information only: rulesets of different packages cannot be mounted together, so sharing files with one is not a problem.
		for (size_t k = 0; k < is.rulesets.size(); k++) {
			if (y > textLimit) break;
			p.Font(3);
			y += 2 + p.TextWrapped(dx, y + 2, dw, 36, is.rulesets[k], k == 0 ? kText : kDim);
		}
		for (size_t k = 0; k < is.overlaps.size(); k++) {
			const ModOverlap &ov = is.overlaps[k];
			if (ov.problem) continue;
			if (y > textLimit) break;
			p.Font(3);
			y += 2 + p.TextWrapped(dx, y + 2, dw, 36, OverlapText(ov), kGold);
			if (!ov.choosable) continue;
			if (ov.canUseMine || ov.canUseTheirs) {
				const int bw = (dw - 6) / 2;
				const int arg = static_cast<int>(k) * 2;
				int bx = dx;
				if (ov.canUseMine) {
					p.Button(bx, y + 4, bw, 24, "Use this pack's files", kUseFiles, arg, mx, my, !st.busy, false, 0);
					bx += bw + 6;
				}
				if (ov.canUseTheirs) p.Button(bx, y + 4, bw, 24, TrFmt("Use the files of %s", {ov.name}), kUseFiles, arg + 1, mx, my, !st.busy, false, 0);
				y += 30;
			}
			else {
				y += 2 + p.TextWrapped(dx, y + 2, dw, 36, "They are inside .pak files, so you can't choose which ones are used", kDim);
			}
		}
		if (!is.files.empty() || !is.values.empty()) {
			const std::string files = std::to_string(is.files.size()), values = std::to_string(is.values.size());
			std::string label = TrFmt("Shared files (%s) and values (%s)", {files, values});
			if (is.values.empty()) label = TrFmt("Shared files (%s)", {files});
			else if (is.files.empty()) label = TrFmt("Shared values (%s)", {values});
			Combo(p, st, dx, y + 4, dw, 24, label, kDropClashFiles);
			y += 30;
		}
	}
	const int textBottom = btnY - 38;
	if (textBottom - y > 40) {
		std::string notes, noteVersion;
		if (m->gbId) GbChangelog(m->gbId, m->version, notes, noteVersion);
		p.Fill(dx, y + 2, dw, 1, pal::lineDim);
		const bool own = !m->description.empty() && (m->hasManifest || !io || io->descriptionHtml.empty());
		PaintRich(p, st, dx, y + 8, dw, textBottom - y - 8, own || !io ? "" : io->descriptionHtml,
			own ? m->description : io ? (!io->description.empty() ? io->description : io->summary) : "",
			Tr("What's new in version ") + noteVersion, notes, 3);
	}
	const bool pending = Pending(st, m->folder);
	CatalogEntry update;
	const bool hasUpdate = FindUpdateFor(st, *m, update);
	if (pending) {
		p.Font(0);
		p.Text(dx, btnY - 32, dw, 26, "It will be uninstalled when the game restarts", kBad);
	}
	else {
		const bool repair = m->noGameFiles && m->gbId && !hasUpdate;
		const int mainW = hasUpdate || repair ? 150 : dw;
		p.Button(dx, btnY - 32, mainW, 26, active ? "Disable" : "Enable", kSetActive, 0, mx, my, !st.busy, !active);
		if (hasUpdate) p.Button(dx + mainW + 6, btnY - 32, dw - mainW - 6, 26, Tr("Update to version ") + update.version, kUpdateMod, 0, mx, my, !st.dl.active, true);
		if (repair) p.Button(dx + mainW + 6, btnY - 32, dw - mainW - 6, 26, "Reinstall from GameBanana", kReinstall, 0, mx, my, !st.dl.active, true);
	}
	const int moreW = 40;
	const int pageW = m->gbId ? 130 : 0;
	p.Button(dx, btnY, dw - moreW - 6 - (pageW ? pageW + 6 : 0), 26, "Open folder", kOpenMod, 0, mx, my, true, false, 0);
	if (pageW) p.Button(dx + dw - moreW - 6 - pageW, btnY, pageW, 26, "GameBanana", kOpenSource, 0, mx, my, io && !io->page.empty(), false, 0);
	if (!pending) {
		p.Button(dx + dw - moreW, btnY, moreW, 26, "...", kDropOpen, kDropModMenu, mx, my, !st.busy, false, 0);
		if (st.drop == kDropModMenu || st.drop == kDropVersions) g_drop.anchor = {dx + dw - 200, btnY, dx + dw, btnY + 26};
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
	p.Fill(16, kFootY, kDesignW - 32, 1, pal::line);
	const int by = kFootY + 10;
	p.Button(16, by, 170, 30, "Install file...", kPickArchive, 0, mx, my);
	int updates = 0;
	for (const ModInfo &m : st.mods) {
		CatalogEntry e;
		if (FindUpdateFor(st, m, e)) updates++;
	}
	if (updates) {
		p.Button(192, by, 210, 30, Tr("Update all (") + std::to_string(updates) + ")", kUpdateAll, 0, mx, my, !st.dl.active, true);
	}
	int closeX = kDesignW - 16 - 120;
	if (st.restartNeeded) {
		p.Font(1);
		const std::string label = Tr("Apply and restart");
		const int w = p.TextWidth(label) + 36;
		closeX -= w + 8;
		p.Button(closeX + 0, by, w, 30, label, kApply, 0, mx, my, true, true);
	}
	if (st.tab == 2 && st.ioDetail < 0 && !st.io.mods.empty()) {
		const int pages = std::max(1, (st.io.total + kIoPerPage - 1) / kIoPerPage);
		const int first = st.ioPage * kIoPerPage + 1;
		const int last = std::min(st.io.total, first + kIoPerPage - 1);
		const int spanL = updates ? 412 : 196, spanR = closeX - 8;
		const int btnW = 112;
		const int textW = std::max(0, std::min(230, spanR - spanL - 2 * btnW - 12));
		const int total = 2 * btnW + textW + 12;
		const int x0 = spanL + std::max(0, (spanR - spanL - total) / 2);
		p.Button(x0, by, btnW, 30, "Previous", kIoPrev, 0, mx, my, st.ioPage > 0 && !st.io.loading);
		p.Button(x0 + btnW + 6 + textW + 6, by, btnW, 30, "Next", kIoNext, 0, mx, my, st.ioPage + 1 < pages && !st.io.loading);
		if (textW > 60) {
			p.Font(3);
			std::string text = TrFmt("%s-%s of %s", {std::to_string(first), std::to_string(last), std::to_string(st.io.total)}) + "   " +
				TrFmt("Page %s of %s", {std::to_string(st.ioPage + 1), std::to_string(pages)});
			if (p.TextWidth(text) > textW) text = TrFmt("Page %s of %s", {std::to_string(st.ioPage + 1), std::to_string(pages)});
			p.Text(x0 + btnW + 6, by, textW, 30, text, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}
	p.Button(kDesignW - 16 - 120, by, 120, 30, "Close", kClose, 0, mx, my);
}

void PaintInstalledFilterPanel(Painter &p, const State &st)
{
	if (!st.catOpen || st.tab != 0) return;
	PaintFacetPanel(p, st.catFilter, InstalledCounts(st), kCatChip, kCatClear, kCatPanel, st.mouseX, st.mouseY);
}
}

