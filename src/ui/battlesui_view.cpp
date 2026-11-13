// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/battlesui_view.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "game/battlelist.h"
#include "ui/widgets.h"

using namespace ui;

namespace battlesui {
namespace {

constexpr int kMargin = 24, kGap = 16, kTabsY = 90, kTabsH = 38, kBodyTop = 142, kBodyBottom = 702, kFooterY = 714, kFooterH = 38;
constexpr int kPad = 20, kHeaderH = 30, kHeaderGap = 14, kButtonH = 40, kSegmentH = 34, kWideCard = 820;

// The pictures are illustrations and keep their own gold frame.
const COLORREF kFrameGold = RGB(214, 170, 84), kFrameGoldDim = RGB(127, 98, 48);

struct Line {
	bool header = false;
	std::string text;
	int list = -1, index = -1, number = 0;
	int Height(bool first) const { return header ? kHeaderH + (first ? 0 : kHeaderGap) : kListRowH; }
};

// The game's own texts (titles, the names of saved games) are shown as they are: Tr() would look each one up as a key.
void Raw(Painter &p, int x, int y, int w, int h, const std::string &t, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, t, c, fmt);
	p.raw = false;
}

int RawWrapped(Painter &p, int x, int y, int w, int h, const std::string &t, COLORREF c)
{
	p.raw = true;
	const int used = p.TextWrapped(x, y, w, h, t, c);
	p.raw = false;
	return used;
}

std::string Upper(const std::string &text)
{
	std::wstring w = W(Tr(text));
	if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
}

// The names of the games stay as they are in every language.
const char *TabName(int tab)
{
	switch (tab) {
	case kBattlesTabImperivm1: return "Imperivm I";
	case kBattlesTabImperivm2: return "Imperivm II";
	case kBattlesTabGreat: return "Imperivm III";
	case kBattlesTabConquests: return "Conquests";
	default: return "Custom adventures";
	}
}

const char *EmptyText(int tab)
{
	switch (tab) {
	case kBattlesTabImperivm1: return "No Imperivm I campaign is installed yet.";
	case kBattlesTabImperivm2: return "No Imperivm II campaign is installed yet.";
	case kBattlesTabConquests: return "There are no conquests here.";
	default: return "There are no adventures here.";
	}
}

std::vector<Line> BuildLines(const BattlesSnapshot &s)
{
	std::vector<Line> lines;
	const auto group = [&](const char *title, int list, bool numbered) {
		Line h;
		h.header = true;
		h.text = Upper(title);
		lines.push_back(h);
		const std::vector<BattlesEntry> &entries = s.lists[list];
		for (size_t i = 0; i < entries.size(); i++) {
			if (!BattlesInTab(s.tab, list, entries[i].path)) continue;
			Line r;
			r.text = SplitBattleTitle(BattleTitleWithoutGame(entries[i].title)).name;
			r.list = list;
			r.index = static_cast<int>(i);
			r.number = numbered ? static_cast<int>(i) + 1 : 0;
			lines.push_back(r);
		}
	};
	if (s.tab == kBattlesTabGreat) {
		group("The great victories of Rome", kBattlesVictories, true);
		group("Rome's enemies fight for freedom", kBattlesDefeats, true);
	}
	else {
		group(TabName(s.tab), kBattlesOthers, false);
	}
	return lines;
}

void PaintGroupHeader(Painter &p, int x, int y, int w, const std::string &text)
{
	p.Font(4);
	// The spaced capitals give way when a long heading would not fit.
	SetTextCharacterExtra(p.dc, p.S(1));
	if (p.TextWidth(text) + 4 > w - 8) SetTextCharacterExtra(p.dc, 0);
	const int tw = std::min(w - 8, p.TextWidth(text) + 4);
	p.Text(x, y, tw, kHeaderH, text, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	SetTextCharacterExtra(p.dc, 0);
	if (x + tw + 10 < x + w) p.Fill(x + tw + 10, y + kHeaderH / 2, w - tw - 10, 1, pal::hairline);
}

// A two-digit number and the name; the chosen row has a quiet fill and a small red arrow, every row a hairline under it.
void PaintRow(Painter &p, const Line &l, bool selected, int x, int y, int w, int clipTop, int clipBottom, const View &v)
{
	const int hitTop = std::max(y, clipTop), hitBottom = std::min(y + kListRowH, clipBottom);
	const bool hot = !selected && hitBottom > hitTop && p.Hover(x, hitTop, w, hitBottom - hitTop, v.mouseX, v.mouseY);
	if (selected) p.Fill(x, y, w, kListRowH, pal::selected);
	else if (hot) p.Fill(x, y, w, kListRowH, pal::raised);
	p.Fill(x, y + kListRowH - 1, w, 1, pal::hairline);
	int textX = x + 14;
	if (l.number) {
		char num[16];
		sprintf_s(num, "%02d", l.number);
		p.Font(4);
		Raw(p, x + 14, y, 30, kListRowH, num, selected ? pal::redText : pal::disabled);
		textX = x + 52;
	}
	p.Font(selected ? 1 : 0);
	const int textRight = x + w - (selected ? 30 : 12);
	p.Text(textX, y, textRight - textX, kListRowH, l.text, selected ? pal::text : pal::textDim,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (selected) {
		p.Font(1);
		Raw(p, x + w - 26, y, 14, kListRowH, "\xE2\x80\xBA", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	if (hitBottom > hitTop) p.AddHit(x, hitTop, w, hitBottom - hitTop, kActRow, RowArg(l.list, l.index));
}

void PaintList(Painter &p, int x, int y, int w, int h, const View &v, PaintOut &out)
{
	PaintCard(p, x, y, w, h);
	const BattlesSnapshot &s = v.snap;
	const std::vector<Line> lines = BuildLines(s);
	const bool counter = !v.progress.empty();
	const int areaX = x + 12, areaY = y + 10, areaW = w - 24, areaH = h - 20 - (counter ? 34 : 0);
	int total = 0;
	for (size_t i = 0; i < lines.size(); i++) total += lines[i].Height(i == 0);
	const int maxScroll = std::max(0, total - areaH);
	const int scroll = std::max(0, std::min(v.listScroll, maxScroll));
	const int rowW = areaW - (maxScroll > 0 ? 14 : 0);
	p.Clip(areaX, areaY, areaW, areaH);
	int ly = areaY - scroll;
	for (size_t i = 0; i < lines.size(); i++) {
		const Line &l = lines[i];
		const int lh = l.Height(i == 0);
		if (ly + lh > areaY && ly < areaY + areaH) {
			if (l.header) PaintGroupHeader(p, areaX + 4, ly + lh - kHeaderH, rowW - 4, l.text);
			else PaintRow(p, l, l.list == s.selList && l.index == s.selIndex, areaX, ly, rowW, areaY, areaY + areaH, v);
		}
		ly += lh;
	}
	p.Unclip();
	ListState st;
	st.scroll = scroll;
	out.list = ListGeom();
	RECT a = p.R(areaX, areaY, areaW, areaH);
	OffsetRect(&a, p.ox, p.oy);
	out.list.area = a;
	ScrollbarPaint(p, areaX + areaW - 7, areaY + 2, areaH - 4, total, areaH, st, out.list, v.mouseX, v.mouseY);
	if (counter) {
		p.Fill(x + 16, y + h - 40, w - 32, 1, pal::hairline);
		p.Font(3);
		p.Text(x + 16, y + h - 36, w - 32, 28, v.progress, pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintSegment(Painter &p, int x, int y, int w, const BattlesSnapshot &s, bool enabled, const View &v)
{
	const int n = static_cast<int>(s.difficulties.size());
	if (n <= 0) return;
	const int segW = w / n;
	for (int i = 0; i < n; i++) {
		const int bx = x + i * segW, bw = i == n - 1 ? w - i * segW : segW;
		const bool on = i == s.difficulty;
		const bool hot = enabled && !on && p.Hover(bx, y, bw, kSegmentH, v.mouseX, v.mouseY);
		p.Fill(bx, y, bw, kSegmentH, on ? pal::text : hot ? pal::button : pal::field);
		p.Frame(bx, y, bw, kSegmentH, on ? pal::text : hot ? pal::edgeHot : pal::edge);
		p.Font(on ? 1 : 0);
		p.Text(bx + 2, y, bw - 4, kSegmentH, s.difficulties[i], on ? pal::onLight : enabled ? pal::text : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (enabled && !on) p.AddHit(bx, y, bw, kSegmentH, kActDifficulty, i);
	}
}

void PaintDifficulty(Painter &p, int x, int y, int w, const View &v)
{
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	p.Text(x, y, w, 18, Upper("Difficulty"), pal::caption);
	SetTextCharacterExtra(p.dc, 0);
	PaintSegment(p, x, y + 22, w, v.snap, !v.snap.starting, v);
}

constexpr int kDifficultyH = 22 + kSegmentH;

// The picture keeps its shape inside the box and sits at its top left.
RECT PaintPicture(Painter &p, int x, int y, int boxW, int boxH, const BattleImage *img)
{
	int w = boxW, h = boxH;
	if (img && img->w > 0 && img->h > 0) {
		h = w * img->h / img->w;
		if (h > boxH) {
			h = boxH;
			w = h * img->w / img->h;
		}
	}
	else {
		h = std::min(boxH, w * 360 / 440);
	}
	p.Fill(x - 4, y - 4, w + 8, h + 8, pal::bg);
	if (img) p.Image(x, y, w, h, img->bgra.data(), img->w, img->h);
	else {
		p.Diamond(x + w / 2, y + h / 2, 8, pal::lineStrong);
	}
	p.Frame(x - 4, y - 4, w + 8, h + 8, kFrameGold);
	p.Frame(x - 1, y - 1, w + 2, h + 2, kFrameGoldDim);
	return {x, y, x + w, y + h};
}

// The red caption, the title and the badge; returns where the next thing starts.
int PaintTitle(Painter &p, int x, int y, int w, const BattleTitle &t, int tab, const View &v)
{
	std::string caption = Upper(TabName(tab));
	if (!t.year.empty()) caption += "  \xC2\xB7  " + t.year;
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	Raw(p, x, y, w, 18, caption, pal::redText);
	SetTextCharacterExtra(p.dc, 0);
	y += 24;
	p.Font(2);
	const int th = std::min(p.Measure(w, t.name), 90);
	p.TextWrapped(x, y, w, th + 4, t.name, pal::text);
	y += th + 8;
	if (!v.badge.empty()) {
		p.Font(4);
		const int bw = std::min(w, p.TextWidth(v.badge) + 24);
		p.RoundRect(x, y + 4, bw, 24, 12, RGB(52, 70, 34));
		p.Text(x, y + 4, bw, 24, v.badge, RGB(196, 230, 160), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += 34;
	}
	return y;
}

const COLORREF kBodyText = pal::body;
constexpr int kDiaryIndent = 18, kPagesH = 26;

struct TextLine {
	std::wstring text;
	COLORREF color = kBodyText;
	// A title of the diary: it starts after a small diamond, and the lines it wraps to keep in line with it.
	int indent = 0;
	bool mark = false;
};

// Each paragraph wrapped to the width; an empty one is an empty line.
void AddParagraphs(Painter &p, std::vector<TextLine> &lines, const std::string &text, int width, COLORREF color)
{
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		std::string para = text.substr(start, end - start);
		if (!para.empty() && para.back() == '\r') para.pop_back();
		if (para.empty()) lines.push_back({L"", color});
		else {
			for (std::wstring &l : p.WrapLines(para, 0, width)) lines.push_back({std::move(l), color});
		}
		start = end + 1;
	}
}

// The objectives the newest game had completed, newest first: each one's title, then its text.
std::vector<TextLine> DiaryLines(Painter &p, const std::vector<ObjectiveNote> &notes, int width)
{
	std::vector<TextLine> lines;
	for (const ObjectiveNote &n : notes) {
		if (!lines.empty()) lines.push_back({L"", kBodyText});
		const size_t first = lines.size();
		p.Font(1);
		AddParagraphs(p, lines, n.title.empty() ? n.id : n.title, width - kDiaryIndent, pal::text);
		p.Font(0);
		while (lines.size() > first && lines.back().text.empty()) lines.pop_back();
		for (size_t i = first; i < lines.size(); i++) {
			lines[i].indent = kDiaryIndent;
			lines[i].mark = i == first;
		}
		AddParagraphs(p, lines, n.text, width, kBodyText);
		while (lines.size() > first && lines.back().text.empty()) lines.pop_back();
		// A long diary stops at a size the screen can still scroll through quickly.
		if (lines.size() > 2000) break;
	}
	return lines;
}

void PaintDescription(Painter &p, int x, int y, int w, int h, const View &v, const SaveInfo *saved, PaintOut &out)
{
	out.desc = ListGeom();
	if (h < kDescLineH) return;
	p.Font(0);
	std::vector<TextLine> lines;
	if (v.diary && saved && saved->diary) lines = DiaryLines(p, *saved->diary, w - 16);
	else AddParagraphs(p, lines, v.snap.description, w - 16, kBodyText);
	while (!lines.empty() && lines.back().text.empty()) lines.pop_back();
	const int visible = std::max(1, h / kDescLineH);
	const int total = static_cast<int>(lines.size());
	const int scroll = std::max(0, std::min(v.descScroll, ListMaxScroll(total, visible)));
	for (int i = 0; i < visible && scroll + i < total; i++) {
		const TextLine &l = lines[scroll + i];
		const int ly = y + i * kDescLineH;
		if (l.mark) p.Fill(x + 3, ly + kDescLineH / 2 - 2, 4, 4, pal::red);
		p.Font(l.indent ? 1 : 0);
		p.LineW(x + l.indent, ly, w - 16 - l.indent, kDescLineH, l.text, l.color);
	}
	p.Font(0);
	ListState st;
	st.scroll = scroll;
	RECT a = p.R(x, y, w, h);
	OffsetRect(&a, p.ox, p.oy);
	ScrollbarPaint(p, x + w - 6, y, visible * kDescLineH, total, visible, st, out.desc, v.mouseX, v.mouseY);
	out.desc.area = a;
}

// The newest game of the chosen adventure, or null.
const SaveInfo *ChosenSave(const View &v)
{
	if (v.snap.selList < 0 || v.snap.selIndex < 0) return nullptr;
	const auto it = v.lastByRow.find(RowArg(v.snap.selList, v.snap.selIndex));
	return it == v.lastByRow.end() ? nullptr : &it->second;
}

// With a game of the chosen adventure, Continue is the main button and Start begins a new one beside it.
void PaintButtons(Painter &p, int right, int y, const View &v)
{
	const BattlesSnapshot &s = v.snap;
	const bool ready = !s.starting && !v.loading && s.selList >= 0;
	const bool saved = ChosenSave(v) != nullptr;
	p.Font(1);
	int x = right;
	if (saved) {
		const char *label = v.loading ? "Loading..." : "Continue";
		const int w = std::max(150, p.TextWidth(label) + 48);
		x -= w;
		p.Button(x, y, w, kButtonH, label, kActContinue, 1, v.mouseX, v.mouseY, ready, true);
		x -= 12;
	}
	const int startW = std::max(150, p.TextWidth(s.starting ? "Loading..." : "Start") + 48);
	x -= startW;
	p.Button(x, y, startW, kButtonH, s.starting ? "Loading..." : "Start", kActStart, 0, v.mouseX, v.mouseY, ready, !saved);
	if (s.intro) {
		const int introW = std::max(150, p.TextWidth("Watch intro") + 48);
		x -= 12 + introW;
		p.Button(x, y, introW, kButtonH, "Watch intro", kActIntro, 0, v.mouseX, v.mouseY, ready);
	}
}

// The briefing and the diary of the newest game, as small tabs over the text: the same underline as the big tabs.
void PaintPages(Painter &p, int x, int y, int w, const View &v, const SaveInfo *saved)
{
	const bool enabled = !v.snap.starting && !v.loading;
	std::vector<std::string> labels = {Upper("Briefing")};
	if (saved) labels.push_back(Upper("Diary") + " (" + std::to_string(saved->diary->size()) + ")");
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	int lx = x;
	for (size_t i = 0; i < labels.size(); i++) {
		const bool on = v.diary == (i == 1);
		const int lw = std::min(x + w - lx, p.TextWidth(labels[i]) + 8);
		if (lw <= 0) break;
		const bool hot = enabled && !on && p.Hover(lx - 4, y, lw + 8, kPagesH, v.mouseX, v.mouseY);
		Raw(p, lx, y, lw, kPagesH - 4, labels[i], on || hot ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (on) p.Fill(lx, y + kPagesH - 3, lw - 8, 3, pal::red);
		if (enabled && !on) p.AddHit(lx - 4, y, lw + 8, kPagesH, kActPage, static_cast<int>(i));
		lx += lw + 22;
	}
	SetTextCharacterExtra(p.dc, 0);
	p.Fill(x, y + kPagesH - 1, w, 1, pal::hairline);
}

void PaintDetail(Painter &p, int x, int y, int w, int h, const View &v, PaintOut &out)
{
	PaintCard(p, x, y, w, h);
	const BattlesSnapshot &s = v.snap;
	out.desc = ListGeom();
	if (s.selList < 0 || s.selIndex < 0 || s.selIndex >= static_cast<int>(s.lists[s.selList].size())) {
		bool empty = true;
		for (const Line &l : BuildLines(s)) empty = empty && l.header;
		if (s.open && empty) {
			p.Font(0);
			p.Text(x, y + h / 2 - 20, w, 40, EmptyText(s.tab), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		return;
	}
	const BattleTitle title = SplitBattleTitle(BattleTitleWithoutGame(s.lists[s.selList][s.selIndex].title));
	const int innerX = x + kPad, innerW = w - kPad * 2, innerTop = y + kPad, innerBottom = y + h - kPad;
	const int buttonsY = innerBottom - kButtonH;
	// The newest game of this adventure, above the buttons; the description ends above it.
	const SaveInfo *saved = ChosenSave(v);
	const int savedH = saved ? 28 : 0;
	const bool hasDiary = saved && saved->diary && !saved->diary->empty();
	const auto paintSaved = [&](int sx, int sw) {
		if (!saved) return;
		p.Font(3);
		p.Ellipse(sx, buttonsY - savedH + 7, 6, 6, pal::red);
		Raw(p, sx + 14, buttonsY - savedH, sw - 14, 20, TrFmt("Last saved: %s", {saved->name + "  \xC2\xB7  " + saved->when}), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	};
	if (w >= kWideCard) {
		// The picture at its own size on the left; the text and the buttons in a column beside it.
		const int imgW = std::min(540, innerW * 56 / 100), imgH = std::min(imgW * 360 / 440, buttonsY - innerTop - 24);
		const RECT pic = PaintPicture(p, innerX + 4, innerTop + 4, imgW, imgH, s.image.get());
		const int colX = pic.right + 28, colW = innerX + innerW - colX;
		int ty = PaintTitle(p, colX, innerTop, colW, title, s.tab, v);
		PaintPages(p, colX, ty + 4, colW, v, hasDiary ? saved : nullptr);
		ty += kPagesH + 14;
		int descBottom = buttonsY - 16 - savedH;
		if (s.difficultyShown) {
			descBottom -= kDifficultyH + 12;
			PaintDifficulty(p, colX, descBottom + 12, colW, v);
		}
		PaintDescription(p, colX, ty, colW, descBottom - ty, v, saved, out);
		paintSaved(colX, colW);
		PaintButtons(p, innerX + innerW, buttonsY, v);
		return;
	}
	// Narrow: the title and the difficulty beside the picture, the description under it across the card.
	const int imgW = std::min(440, innerW * 56 / 100), imgH = imgW * 360 / 440;
	const RECT pic = PaintPicture(p, innerX + 4, innerTop + 4, imgW, imgH, s.image.get());
	const int colX = pic.right + 24, colW = innerX + innerW - colX;
	PaintTitle(p, colX, innerTop, colW, title, s.tab, v);
	if (s.difficultyShown) PaintDifficulty(p, colX, pic.bottom - kDifficultyH, colW, v);
	int descY = pic.bottom + 22;
	PaintPages(p, innerX, descY - 12, innerW, v, hasDiary ? saved : nullptr);
	descY += kPagesH - 4;
	PaintDescription(p, innerX, descY + 6, innerW, buttonsY - 14 - savedH - descY - 6, v, saved, out);
	paintSaved(innerX, innerW);
	PaintButtons(p, innerX + innerW, buttonsY, v);
}

// A picture kept in shape and centred in its box, or a lozenge when there is none.
void PaintFitted(Painter &p, int x, int y, int w, int h, const BattleImage *img)
{
	p.Fill(x, y, w, h, pal::bg);
	if (img && img->w > 0 && img->h > 0) {
		int iw = w, ih = w * img->h / img->w;
		if (ih > h) {
			ih = h;
			iw = h * img->w / img->h;
		}
		p.Image(x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, img->bgra.data(), img->w, img->h);
	}
	else {
		p.Diamond(x + w / 2, y + h / 2, 7, pal::lineStrong);
	}
	p.Frame(x, y, w, h, pal::hairline);
}

constexpr int kBannerY = 96, kBannerH = 226, kCampaignsY = 338, kCardsY = 376;

// The newest game of all, ready to go on; or, with none, where to begin.
void PaintContinue(Painter &p, int x, int y, int w, int h, const View &v)
{
	PaintCard(p, x, y, w, h);
	const BattlesSnapshot &s = v.snap;
	const bool ready = s.open && !s.starting && !s.switching && !v.loading;
	const int bw = 210, by = y + h - 20 - kButtonH;
	if (!v.hasLatest) {
		p.Font(0);
		p.Text(x + 24, y + 24, w - 48, h - 48 - kButtonH - 12, "You have no adventure in progress. Choose a campaign to start one.", pal::textDim,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Button(x + (w - bw) / 2, by, bw, kButtonH, "All saved games", kActAllSaves, 0, v.mouseX, v.mouseY, ready);
		return;
	}
	const SaveInfo &g = v.latest;
	const int picW = 290, picH = h - 40;
	PaintFitted(p, x + 20, y + 20, picW, picH, g.picture.get());
	p.Frame(x + 17, y + 17, picW + 6, picH + 6, kFrameGold);
	const int tx = x + 20 + picW + 30, tw = x + w - 24 - tx;
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	p.Text(tx, y + 22, tw, 18, Upper("Last game"), pal::redText);
	SetTextCharacterExtra(p.dc, 0);
	p.Font(3);
	p.Text(tx, y + 44, tw, 18, TabName(g.tab), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(2);
	Raw(p, tx, y + 62, tw, 32, SplitBattleTitle(BattleTitleWithoutGame(g.title)).name, pal::text);
	p.Font(0);
	Raw(p, tx, y + 100, tw, 22, g.name, pal::text);
	std::string when = TrFmt("Saved %s", {g.when});
	if (!g.played.empty()) when += "   \xC2\xB7   " + TrFmt("Played %s", {g.played});
	p.Font(3);
	Raw(p, tx, y + 124, tw, 20, when, pal::textDim);
	// Where the player was: the objective the game completed last.
	if (g.diary && !g.diary->empty()) {
		const ObjectiveNote &n = g.diary->front();
		Raw(p, tx, y + 144, tw, 20, TrFmt("Last objective: %s", {n.title.empty() ? n.id : n.title}), pal::textDim);
	}
	p.Font(1);
	const char *label = v.loading ? "Loading..." : "Continue";
	p.Button(x + w - 24 - bw, by, bw, kButtonH, label, kActContinue, 0, v.mouseX, v.mouseY, ready, true);
	p.Button(x + w - 24 - bw - 12 - bw, by, bw, kButtonH, "All saved games", kActAllSaves, 0, v.mouseX, v.mouseY, ready);
}

int AdventuresIn(const BattlesSnapshot &s, int tab)
{
	int n = 0;
	for (int list = 0; list < kBattlesListCount; list++) {
		for (const BattlesEntry &e : s.lists[list]) n += BattlesInTab(tab, list, e.path) ? 1 : 0;
	}
	return n;
}

std::string CountText(int tab, int n)
{
	if (tab == kBattlesTabGreat) return n == 1 ? Tr("1 battle") : TrFmt("%s battles", {std::to_string(n)});
	if (tab == kBattlesTabCustom) return n == 1 ? Tr("1 adventure") : TrFmt("%s adventures", {std::to_string(n)});
	return n == 1 ? Tr("1 campaign") : TrFmt("%s campaigns", {std::to_string(n)});
}

// The name a card shows: the campaign itself when its game has only one, the count when it has more.
std::string CardTitle(const BattlesSnapshot &s, int tab, int n)
{
	if (tab == kBattlesTabGreat) return Tr("Great Battles of Rome");
	if (tab == kBattlesTabCustom) return Tr("Custom adventures");
	if (n == 0) return Tr("Not installed");
	if (n > 1) return CountText(tab, n);
	for (const BattlesEntry &e : s.lists[kBattlesOthers]) {
		if (BattlesInTab(tab, kBattlesOthers, e.path)) return SplitBattleTitle(BattleTitleWithoutGame(e.title)).name;
	}
	return "";
}

void PaintCampaignCard(Painter &p, int x, int y, int w, int h, int tab, const View &v)
{
	const BattlesSnapshot &s = v.snap;
	const bool enabled = s.open && !s.starting && !s.switching && !v.loading;
	const bool hot = enabled && p.Hover(x, y, w, h, v.mouseX, v.mouseY);
	const int n = AdventuresIn(s, tab);
	PaintCard(p, x, y, w, h);
	if (hot) p.Frame(x, y, w, h, pal::edgeHot);
	PaintFitted(p, x + 12, y + 12, w - 24, 150, s.covers[tab].get());
	const int tx = x + 16, tw = w - 32;
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	Raw(p, tx, y + 172, tw, 18, Upper(TabName(tab)), pal::redText);
	SetTextCharacterExtra(p.dc, 0);
	p.Font(2);
	RawWrapped(p, tx, y + 194, tw, 58, CardTitle(s, tab, n), n == 0 ? pal::textDim : pal::text);
	p.Fill(tx, y + h - 42, tw, 1, pal::hairline);
	p.Font(3);
	if (n > 0) Raw(p, tx, y + h - 36, tw, 24, CountText(tab, n), pal::textDim);
	if (v.savedInTab[tab] > 0) {
		p.Font(4);
		const std::string badge = Tr("In progress");
		const int bw = std::min(tw / 2, p.TextWidth(badge) + 20);
		p.RoundRect(x + w - 16 - bw, y + h - 34, bw, 20, 10, RGB(52, 70, 34));
		Raw(p, x + w - 16 - bw, y + h - 34, bw, 20, badge, RGB(196, 230, 160), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	if (enabled) p.AddHit(x, y, w, h, kActOpenTab, tab);
}

void PaintOverview(Painter &p, int W, const View &v)
{
	PaintContinue(p, kMargin, kBannerY, W - kMargin * 2, kBannerH, v);
	PaintGroupHeader(p, kMargin + 4, kCampaignsY, W - kMargin * 2 - 8, Upper("Campaigns"));
	const int tabs[] = {kBattlesTabImperivm1, kBattlesTabImperivm2, kBattlesTabGreat, kBattlesTabCustom};
	const int count = static_cast<int>(sizeof(tabs) / sizeof(tabs[0]));
	const int cardW = (W - kMargin * 2 - kGap * (count - 1)) / count;
	for (int i = 0; i < count; i++) PaintCampaignCard(p, kMargin + i * (cardW + kGap), kCardsY, cardW, kBodyBottom - kCardsY, tabs[i], v);
}

}

int RowTop(const BattlesSnapshot &s, int list, int index)
{
	const std::vector<Line> lines = BuildLines(s);
	int y = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		if (!lines[i].header && lines[i].list == list && lines[i].index == index) return y;
		y += lines[i].Height(i == 0);
	}
	return -1;
}

float ScreenScale(int frameW, int frameH)
{
	(void)frameW;
	return std::max(0.6f, std::min(1.6f, frameH / static_cast<float>(kScreenH)));
}

int ScreenWidth(int frameW, int frameH)
{
	return std::max(1000, static_cast<int>(frameW / ScreenScale(frameW, frameH) + 0.5f));
}

void PaintBattlesScreen(Painter &p, int screenW, const View &v, PaintOut &out)
{
	const int W = screenW;
	PaintWindowFrame(p, W, kScreenH);
	// The Conquest button of the main menu opens the same screen with only the conquests: it has no tabs.
	const bool conquests = v.snap.tab == kBattlesTabConquests;
	PaintWindowHeader(p, W, Upper(conquests ? "Conquests" : "Great Battles"));

	// The overview: the game to go on with and a card per campaign; Back leaves the screen.
	if (ShowsOverview(v)) {
		out = PaintOut();
		PaintOverview(p, W, v);
		p.Button(kMargin, kFooterY, 150, kFooterH, "Back", kActBack, 0, v.mouseX, v.mouseY, !v.snap.starting && !v.loading);
		return;
	}

	if (!conquests) {
		const std::pair<int, const char *> kTabs[] = {{kBattlesTabImperivm1, TabName(kBattlesTabImperivm1)}, {kBattlesTabImperivm2, TabName(kBattlesTabImperivm2)},
			{kBattlesTabGreat, TabName(kBattlesTabGreat)}, {kBattlesTabCustom, TabName(kBattlesTabCustom)}};
		p.Font(1);
		int tabW = 160;
		for (const auto &t : kTabs) tabW = std::max(tabW, p.TextWidth(t.second) + 40);
		tabW = std::min(tabW, (W - kMargin * 2 - 18) / 4);
		std::vector<TabSpec> tabs;
		for (const auto &t : kTabs) tabs.push_back({t.second, kActTab, t.first, v.snap.tab == t.first, !v.snap.starting && !v.snap.switching});
		PaintTabs(p, kMargin, kTabsY, tabs, v.mouseX, v.mouseY, tabW, kTabsH);
	}
	p.Fill(kMargin, kTabsY + kTabsH - 1, W - kMargin * 2, 1, pal::hairline);

	const int listW = std::max(320, std::min(420, W * 30 / 100));
	PaintList(p, kMargin, kBodyTop, listW, kBodyBottom - kBodyTop, v, out);
	const int detailX = kMargin + listW + kGap;
	PaintDetail(p, detailX, kBodyTop, W - kMargin - detailX, kBodyBottom - kBodyTop, v, out);

	// In a campaign Back goes to the overview; the conquests have none and leave.
	p.Button(kMargin, kFooterY, 150, kFooterH, "Back", conquests ? kActBack : kActOverview, 0, v.mouseX, v.mouseY, !v.snap.starting && !v.loading);
}

}
