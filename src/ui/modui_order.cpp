// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
std::atomic<int> g_orderRows{12};

namespace {
const COLORREF kTeal = RGB(140, 196, 176);

// The code of the profile on screen, made again only when the profile changes: making it reads every installed pack.
struct CodeCache {
	std::string key, code;
};
CodeCache g_code;

const std::string &ProfileCode(const ModProfile &p)
{
	std::string key = p.name;
	for (const std::string &m : p.mods) key += "\x01" + m;
	for (const ModChoice &c : p.choices) key += "\x02" + c.use + ">" + c.over;
	for (const ModFileChoice &f : p.files) key += "\x03" + f.path + ">" + f.use;
	if (key != g_code.key) {
		g_code.key = key;
		g_code.code = ModsProfileCode(p);
	}
	return g_code.code;
}

// What the pasted code holds, read again only when its text changes: reading it looks for each pack among the installed ones.
struct ImportPreview {
	std::string text;
	bool ok = false;
	std::string error;
	ModsSharedProfile profile;
};
ImportPreview g_preview;

const ImportPreview &PreviewOf(const std::string &text)
{
	if (text != g_preview.text) {
		g_preview = ImportPreview();
		g_preview.text = text;
		g_preview.ok = ModsParseProfileCode(text, g_preview.profile, g_preview.error);
	}
	return g_preview;
}

const char *ReasonTag(int reason)
{
	switch (reason) {
	case kOrderNeeds: return "Needs";
	case kOrderNeeded: return "Needed";
	case kOrderAuthor: return "Author";
	case kOrderByKind: return "Type";
	default: return "By name";
	}
}

COLORREF ReasonColor(int reason)
{
	return reason == kOrderNeeds || reason == kOrderNeeded ? kTeal : reason == kOrderAuthor ? pal::warn : pal::caption;
}

void PaintCheck(Painter &p, int x, int cy, COLORREF c)
{
	p.Line(x, cy, x + 4, cy + 4, c, 2);
	p.Line(x + 4, cy + 4, x + 12, cy - 4, c, 2);
}

void PaintCross(Painter &p, int x, int cy, COLORREF c)
{
	p.Line(x + 1, cy - 5, x + 11, cy + 5, c, 2);
	p.Line(x + 11, cy - 5, x + 1, cy + 5, c, 2);
}

void PaintDownArrow(Painter &p, int x, int cy, COLORREF c)
{
	p.Fill(x + 5, cy - 7, 2, 8, c);
	p.ArrowDown(x, cy, 12, 6, c);
}

std::string NamesOf(const std::vector<const ModsSharedMod *> &mods)
{
	std::string out;
	for (size_t i = 0; i < mods.size() && i < 4; i++) out += (i ? ", " : "") + (mods[i]->name.empty() ? mods[i]->id : mods[i]->name);
	if (mods.size() > 4) out += TrFmt(" and %s more", {std::to_string(mods.size() - 4)});
	return out;
}

// The active packs from the one that loads first, each with why it is there, and the rules under them.
void PaintOrderList(Painter &p, const State &st, const PanelLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int x = l.x, w = l.listW, y0 = l.cardTop, h = l.cardBottom - l.cardTop;
	PaintGlassCard(p, x, y0, w, h);
	const int stripH = 48;
	p.Font(4);
	const std::string title = TrFmt("%s active packs · from top to bottom", {std::to_string(st.order.size())});
	const int titleW = std::min(w - 44, p.CapsWidth(title, 1) + 4);
	p.TextCaps(x + 22, y0, titleW, stripH, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	{
		// The legend takes what the title leaves, shortened down to a readable part, and goes when not even that fits.
		p.Font(3);
		const std::string legend = Tr("Higher up wins when two packs bring the same file");
		const int room = w - 22 - (22 + titleW + 24) - 22;
		const int lw = std::min(p.TextWidth(legend), room);
		if (lw >= 160) {
			p.Text(x + w - 22 - lw, y0, lw, stripH, legend, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.ArrowUp(x + w - 22 - lw - 22, y0 + stripH / 2 - 7, 11, 7, pal::text);
			p.Fill(x + w - 22 - lw - 18, y0 + stripH / 2, 3, 7, pal::text);
		}
	}
	const int posX = x + 10, posW = 44, nameX = x + 66, nameW = std::min(320, w / 4);
	constexpr int kWhyRoomy = 200, kWhyLeast = 160;
	int typeW = 150, clashW = 200;
	auto whyRoom = [&] { return (x + w - 26 - clashW - 16) - (nameX + nameW + 12 + (typeW ? typeW + 12 : 0)); };
	if (whyRoom() < kWhyRoomy) {
		typeW = 110;
		clashW = 160;
	}
	if (whyRoom() < kWhyLeast) typeW = 0;
	const int typeX = nameX + nameW + 12, clashX = x + w - 26 - clashW, whyX = typeW ? typeX + typeW + 12 : typeX, whyW = clashX - 16 - whyX;
	const int headY = y0 + stripH, headH = 36;
	p.Fill(x + 1, headY, w - 2, 1, pal::hairline);
	p.Fill(x + 1, headY + headH - 1, w - 2, 1, pal::lineStrong);
	p.Font(4);
	p.TextCaps(posX, headY, posW, headH, "#", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(nameX, headY, nameW, headH, "Content", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	if (typeW) p.TextCaps(typeX, headY, typeW, headH, "Type", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(whyX, headY, whyW, headH, "Why it is here", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	p.TextCaps(clashX, headY, clashW, headH, "When they clash", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);

	const char *rules[] = {"Cosmetic first, then maps and campaigns, and gameplay at the end.", "What a pack needs stays below it.",
		"What its author asks for: to load before or after another one.", "When nothing else decides, by name."};
	constexpr int kRuleGapX = 20, kRuleNumW = 34, kRuleTop = 46, kRuleGapY = 10, kRuleColLeast = 240;
	const int ruleCols = (w - 44 - 3 * kRuleGapX) / 4 >= kRuleColLeast ? 4 : 2;
	const int colW = (w - 44 - (ruleCols - 1) * kRuleGapX) / ruleCols;
	int ruleRowH = 22;
	p.Font(3);
	for (const char *rule : rules) ruleRowH = std::max(ruleRowH, p.Measure(colW - kRuleNumW, rule));
	const int ruleRows = 4 / ruleCols;
	const int rulesH = kRuleTop + ruleRows * ruleRowH + (ruleRows - 1) * kRuleGapY + 14, rulesY = y0 + h - rulesH;
	const int listY = headY + headH, listH = rulesY - listY;
	int groupCount[3] = {0, 0, 0};
	for (const ModOrderEntry &e : st.order) groupCount[std::max(0, std::min(2, e.kind))]++;
	int bands = 0;
	for (int g = 0; g < 3; g++) bands += groupCount[g] ? 1 : 0;
	const int rows = std::max(1, (listH - bands * kGroupH) / kModRowH);
	g_orderRows = rows;
	if (st.order.empty()) {
		p.Font(0);
		p.TextWrapped(x + 22, listY + 18, w - 44, 80, "No active packs: the game loads as it came.", pal::textDim);
	}
	int y = listY, lastGroup = -1;
	const int count = static_cast<int>(st.order.size());
	for (int v = st.orderList.scroll; v < count && v < st.orderList.scroll + rows; v++) {
		const ModOrderEntry &e = st.order[static_cast<size_t>(v)];
		const int group = std::max(0, std::min(2, e.kind));
		if (group != lastGroup) {
			PaintGroupBand(p, x, y, w, group, groupCount[group]);
			y += kGroupH;
			lastGroup = group;
		}
		else {
			p.Fill(x + 1, y, w - 2, 1, pal::hairline);
		}
		const ModInfo *m = FindModIn(st.mods, e.folder);
		p.Font(2);
		p.Text(posX, y, posW, kModRowH, std::to_string(e.position), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(1);
		p.Text(nameX, y, nameW, kModRowH, m ? m->name : e.folder, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		if (m && typeW) {
			const int cat = ModMainCategory(*m);
			const int cg = CatalogCategoryGroup(cat);
			p.Text(typeX, y, typeW, kModRowH, Tr(CatalogCategoryLabel(cat)), cg == 0 ? pal::redText : cg == 1 ? kTeal : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		{
			const std::string tag = Tr(ReasonTag(e.reason));
			p.Font(4);
			const int tw = std::min(whyW, p.CapsWidth(tag, 1) + 16), th = 22, ty = y + (kModRowH - th) / 2;
			const COLORREF c = ReasonColor(e.reason);
			p.Frame(whyX, ty, tw, th, c == pal::caption ? pal::edge : c);
			p.TextCaps(whyX, ty, tw, th, tag, c == pal::caption ? pal::textDim : c, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
			p.Font(3);
			const std::string why = OrderReasonText(st, e);
			if (whyW - tw - 10 >= 40) p.Text(whyX + tw + 10, y, whyW - tw - 10, kModRowH, why, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (p.Hover(whyX, y, whyW, kModRowH, mx, my)) SetTipText(why, whyX, y + kModRowH);
		}
		Row r;
		r.folder = e.folder;
		r.active = true;
		PaintClashCell(p, st, r, clashX, y, clashW, kModRowH);
		y += kModRowH;
	}
	{
		ListGeom geom;
		ScrollbarPaint(p, x + w - 9, listY, listH, count, rows, st.orderList, geom, mx, my);
		g_orderGeomOut = geom;
	}

	p.Fill(x + 1, rulesY, w - 2, 1, pal::hairline);
	p.Font(4);
	p.TextCaps(x + 22, rulesY + 14, w - 44, 20, "How it is decided, in this order", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	const COLORREF colors[] = {pal::caption, kTeal, pal::warn, pal::caption};
	for (int k = 0; k < 4; k++) {
		const int cx = x + 22 + (k % ruleCols) * (colW + kRuleGapX), cy = rulesY + kRuleTop + (k / ruleCols) * (ruleRowH + kRuleGapY);
		p.Frame(cx, cy, 24, 22, colors[k] == pal::caption ? pal::edge : colors[k]);
		p.Font(4);
		p.Text(cx, cy, 24, 22, std::to_string(k + 1), colors[k] == pal::caption ? pal::textDim : colors[k], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.TextWrapped(cx + kRuleNumW, cy + 1, colW - kRuleNumW, ruleRowH + 2, rules[k], pal::textDim);
	}
}

// Sharing the profile on screen as a code, and making a new profile from someone else's.
void PaintShareCard(Painter &p, const State &st, const PanelLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 24;
	const int x = l.detailX, y0 = l.cardTop, w = l.detailW, h = l.cardBottom - l.cardTop;
	PaintGlassCard(p, x, y0, w, h);
	const int dx = x + pad, dw = w - 2 * pad, bottom = y0 + h - pad;
	int y = y0 + pad - 4;
	p.Font(2);
	p.Text(dx, y, dw, 40, "Share the profile", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	y += 42;
	p.Font(3);
	y += p.TextWrapped(dx, y, dw, 44, "The same packs, in the same order and with your file choices, in one code.", pal::textDim) + 18;

	PaintSectionLabel(p, dx, y, dw, 22, TrFmt("Your profile \"%s\"", {ModsProfileLabel(st.profile.name)}));
	y += 30;
	if (st.profile.mods.empty()) {
		p.Font(0);
		y += p.TextWrapped(dx, y, dw, 50, "This profile has no packs to share.", pal::textDim) + 16;
	}
	else {
		const std::string &code = ProfileCode(st.profile);
		p.Fill(dx, y, dw, 48, pal::bg);
		p.Frame(dx, y, dw, 48, pal::lineStrong);
		p.Font(3);
		const bool was = p.raw;
		p.raw = true;
		p.Text(dx + 14, y, dw - 28, 48, code, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = was;
		y += 56;
		p.Button(dx, y, dw, 40, "Copy code", kCopyCode, 0, mx, my, !code.empty());
		y += 48;
		size_t onGb = 0;
		for (const std::string &f : st.profile.mods) {
			const ModInfo *m = FindModIn(st.mods, f);
			if (m && m->gbId) onGb++;
		}
		p.Font(3);
		std::string note = TrFmt("%s packs · %s file choices", {std::to_string(st.profile.mods.size()), std::to_string(st.profile.files.size())});
		if (onGb < st.profile.mods.size()) note += "  ·  " + TrFmt("%s are not on GameBanana: whoever gets the code will have to ask you for them", {std::to_string(st.profile.mods.size() - onGb)});
		y += p.TextWrapped(dx, y, dw, 44, note, pal::caption) + 22;
	}

	PaintSectionLabel(p, dx, y, dw, 22, "Import a profile");
	y += 30;
	const int pasteW = 120;
	PaintField(p, st, dx, y, dw - pasteW - 10, kBarH, st.importText, "Paste a profile code here", st.focus == kImportField, false, kFocusImport, mx, my);
	p.Button(dx + dw - pasteW, y, pasteW, kBarH, "Paste", kPasteCode, 0, mx, my, true, false, 0);
	y += kBarH + 12;

	const int btnH = 44, btnY = bottom - btnH;
	if (!st.importProfile.empty()) {
		p.Font(0);
		p.TextWrapped(dx, y, dw, 80,
			TrFmt("Downloading the packs of \"%s\": %s left. They join the profile as they arrive.",
				{ModsProfileLabel(st.importProfile), std::to_string(st.importWant.size() + st.importLoading.size())}),
			pal::text);
		p.Button(dx, btnY, dw, btnH, "Stop waiting for them", kImportClear, 0, mx, my, true, false, 0);
		return;
	}
	if (st.importText.empty()) return;
	const ImportPreview &pv = PreviewOf(st.importText);
	if (!pv.ok) {
		p.Font(0);
		p.TextWrapped(dx, y, dw, 60, pv.error, pal::bad);
		p.Button(dx, btnY, dw, btnH, "Cancel", kImportClear, 0, mx, my, true, false, 0);
		return;
	}
	std::vector<const ModsSharedMod *> have, download, missing;
	for (const ModsSharedMod &m : pv.profile.mods) {
		if (m.installed) have.push_back(&m);
		else if (m.gbId) download.push_back(&m);
		else missing.push_back(&m);
	}
	const int boxPad = 14, iw = dw - 2 * boxPad;
	const int boxY = y, boxBottom = btnY - 12;
	p.Fill(dx, boxY, dw, boxBottom - boxY, pal::raised);
	p.Frame(dx, boxY, dw, boxBottom - boxY, pal::hairline);
	int by = boxY + boxPad;
	p.Font(1);
	p.Text(dx + boxPad, by, iw - 120, 24, pv.profile.name.empty() ? Tr("Shared profile") : pv.profile.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(dx + boxPad, by, iw, 24, TrFmt("%s packs", {std::to_string(pv.profile.mods.size())}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	by += 34;
	// The groups scroll between the name and the foot of the box when they are more than it holds.
	struct Group {
		int kind;
		std::string text;
		COLORREF color;
		const std::vector<const ModsSharedMod *> *mods;
	};
	std::vector<Group> groups;
	if (!have.empty()) groups.push_back({0, TrFmt("%s are already installed", {std::to_string(have.size())}), pal::ok, &have});
	if (!download.empty()) groups.push_back({1, TrFmt("%s download from GameBanana", {std::to_string(download.size())}), pal::text, &download});
	if (!missing.empty()) groups.push_back({2, TrFmt("%s are not on GameBanana", {std::to_string(missing.size())}), pal::bad, &missing});
	const int namesW = iw - 24 - 12;
	auto namesH = [&](const Group &g) {
		if (g.kind == 0 || g.mods->empty()) return 0;
		p.Font(3);
		return p.Measure(namesW, NamesOf(*g.mods)) + 6;
	};
	int contentH = missing.empty() ? 0 : 24;
	for (const Group &g : groups) contentH += 28 + namesH(g);
	PaneSpec spec;
	spec.id = State::kPaneImport;
	spec.x = dx + boxPad;
	spec.y = by;
	spec.w = dw - boxPad - 4;
	spec.h = std::max(0, boxBottom - 44 - by);
	spec.contentH = contentH;
	spec.drag = &st.dropBar;
	spec.mx = mx;
	spec.my = my;
	{
		ScrollPane pane(p, spec, st.scroll.Of(State::kPaneImport));
		by = pane.Top();
		for (const Group &g : groups) {
			const int cy = by + 13;
			if (g.kind == 0) PaintCheck(p, dx + boxPad, cy, g.color);
			else if (g.kind == 1) PaintDownArrow(p, dx + boxPad, cy, g.color);
			else PaintCross(p, dx + boxPad, cy, g.color);
			p.Font(0);
			p.Text(dx + boxPad + 24, by, namesW, 26, g.text, g.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			by += 28;
			const int nh = namesH(g);
			if (nh) {
				p.Font(3);
				p.TextWrapped(dx + boxPad + 24, by, namesW, nh, NamesOf(*g.mods), pal::textDim);
				by += nh;
			}
		}
		if (!missing.empty()) {
			p.Font(3);
			p.Text(dx + boxPad + 24, by, namesW, 20, "Ask whoever shared the code for them.", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	p.Fill(dx + boxPad, boxBottom - 40, iw, 1, pal::hairline);
	p.Font(3);
	p.Text(dx + boxPad, boxBottom - 38, iw, 36, "It is made as a new profile; yours does not change.", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	const int cancelW = 150;
	p.Button(dx, btnY, cancelW, btnH, "Cancel", kImportClear, 0, mx, my, true, false, 0);
	p.Button(dx + cancelW + 10, btnY, dw - cancelW - 10, btnH, download.empty() ? "Import" : "Import and download", kImportCode, 0, mx, my, !st.busy, true);
}
}

void PaintOrderTab(Painter &p, const State &st)
{
	const PanelLayout l = PanelLayoutFor(st);
	PaintOrderList(p, st, l);
	PaintShareCard(p, st, l);
}
}
