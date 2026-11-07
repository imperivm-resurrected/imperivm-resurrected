// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/battlesui_view.h"

#include <algorithm>
#include <string>
#include <vector>

#include "game/battlelist.h"
#include "ui/widgets.h"

using namespace ui;

namespace battlesui {
namespace {

constexpr int kMargin = 24, kGap = 16, kTabsY = 90, kTabsH = 38, kBodyTop = 142, kBodyBottom = 702, kFooterY = 714, kFooterH = 38;
constexpr int kPad = 20, kHeaderH = 30, kHeaderGap = 14, kButtonH = 40, kSegmentH = 34, kWideCard = 820;

struct Line {
	bool header = false;
	std::string text;
	int list = -1, index = -1, number = 0;
	int Height(bool first) const { return header ? kHeaderH + (first ? 0 : kHeaderGap) : kListRowH; }
};

std::string Upper(const std::string &text)
{
	std::wstring w = W(Tr(text));
	if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
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
			Line r;
			r.text = SplitBattleTitle(entries[i].title).name;
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
		group(s.tab == kBattlesTabConquests ? "Conquests" : "Custom adventures", kBattlesOthers, false);
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
	p.Text(x, y, tw, kHeaderH, text, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	SetTextCharacterExtra(p.dc, 0);
	if (x + tw + 10 < x + w) p.FillH(x + tw + 10, y + kHeaderH / 2, w - tw - 10, 1, pal::line, pal::panel);
}

void PaintRow(Painter &p, const Line &l, bool selected, int x, int y, int w, int clipTop, int clipBottom, const View &v)
{
	const int hitTop = std::max(y, clipTop), hitBottom = std::min(y + kListRowH, clipBottom);
	const bool hot = !selected && hitBottom > hitTop && p.Hover(x, hitTop, w, hitBottom - hitTop, v.mouseX, v.mouseY);
	if (selected) {
		p.FillH(x, y + 2, w, kListRowH - 4, RGB(96, 34, 24), pal::panel);
		p.FillV(x, y + 2, 4, kListRowH - 4, pal::goldLight, pal::bronze);
	}
	else if (hot) {
		p.FillH(x, y + 2, w, kListRowH - 4, RGB(54, 41, 24), pal::panel);
	}
	int textX = x + 18;
	if (l.number) {
		const int cx = x + 26, cy = y + kListRowH / 2;
		p.Diamond(cx, cy, 14, selected ? pal::goldLight : hot ? kGold : pal::bronze);
		p.Diamond(cx, cy, 12, selected ? RGB(150, 104, 40) : RGB(40, 30, 18));
		p.Font(4);
		p.Text(cx - 12, cy - 9, 24, 18, std::to_string(l.number), selected ? RGB(255, 244, 210) : pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		textX = x + 52;
	}
	else {
		p.Diamond(x + 18, y + kListRowH / 2, 3, selected ? pal::goldLight : pal::bronze);
		textX = x + 30;
	}
	p.Font(selected ? 1 : 0);
	p.Text(textX, y, x + w - 8 - textX, kListRowH, l.text, selected ? pal::goldLight : hot ? RGB(246, 232, 190) : kText,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
		p.Fill(x + 16, y + h - 40, w - 32, 1, pal::lineDim);
		p.Font(3);
		p.Text(x + 16, y + h - 36, w - 32, 28, v.progress, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
		if (on) p.FillV(bx, y, bw, kSegmentH, pal::goldLight, kGold);
		else p.Fill(bx, y, bw, kSegmentH, hot ? pal::fieldHover : pal::well);
		p.Frame(bx, y, bw, kSegmentH, on ? pal::goldLight : pal::edge);
		p.Font(on ? 1 : 0);
		p.Text(bx + 2, y, bw - 4, kSegmentH, s.difficulties[i], on ? RGB(34, 22, 8) : enabled ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (enabled && !on) p.AddHit(bx, y, bw, kSegmentH, kActDifficulty, i);
	}
}

void PaintDifficulty(Painter &p, int x, int y, int w, const View &v)
{
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	p.Text(x, y, w, 18, Upper("Difficulty"), kGold);
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
	p.Fill(x - 4, y - 4, w + 8, h + 8, pal::well);
	if (img) p.Image(x, y, w, h, img->bgra.data(), img->w, img->h);
	else {
		const int lw = std::min(66, w / 4), lh = lw * 42 / 66;
		PaintOrnament(p, kOrnamentLaurel, x + w / 2 - lw - 4, y + (h - lh) / 2, lw, lh, pal::lineDim);
		PaintOrnament(p, kOrnamentLaurel, x + w / 2 + 4, y + (h - lh) / 2, lw, lh, pal::lineDim, true);
	}
	p.Frame(x - 4, y - 4, w + 8, h + 8, kGold);
	p.Frame(x - 1, y - 1, w + 2, h + 2, pal::lineDim);
	PaintFrameCorners(p, x - 4, y - 4, w + 8, h + 8, 14);
	return {x, y, x + w, y + h};
}

int PaintTitle(Painter &p, int x, int y, int w, const BattleTitle &t, const View &v)
{
	p.Font(2);
	const int th = std::min(p.Measure(w, t.name), 90);
	p.TextWrapped(x, y, w, th + 4, t.name, pal::goldLight);
	y += th + 4;
	if (!t.year.empty()) {
		p.Font(0);
		p.Text(x, y, w, 22, t.year, RGB(196, 176, 132));
		y += 24;
	}
	if (!v.badge.empty()) {
		p.Font(4);
		const int bw = std::min(w, p.TextWidth(v.badge) + 24);
		p.RoundRect(x, y + 4, bw, 24, 12, RGB(52, 70, 34));
		p.Text(x, y + 4, bw, 24, v.badge, RGB(196, 230, 160), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += 34;
	}
	return y;
}

void PaintDescription(Painter &p, int x, int y, int w, int h, const View &v, PaintOut &out)
{
	out.desc = ListGeom();
	if (h < kDescLineH) return;
	p.Font(0);
	std::vector<std::wstring> lines;
	const std::string &text = v.snap.description;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		const std::string para = text.substr(start, end - start);
		if (para.empty()) lines.push_back(L"");
		else {
			for (std::wstring &l : p.WrapLines(para, 0, w - 16)) lines.push_back(std::move(l));
		}
		start = end + 1;
	}
	while (!lines.empty() && lines.back().empty()) lines.pop_back();
	const int visible = std::max(1, h / kDescLineH);
	const int total = static_cast<int>(lines.size());
	const int scroll = std::max(0, std::min(v.descScroll, ListMaxScroll(total, visible)));
	for (int i = 0; i < visible && scroll + i < total; i++) p.LineW(x, y + i * kDescLineH, w - 16, kDescLineH, lines[scroll + i], RGB(222, 208, 170));
	ListState st;
	st.scroll = scroll;
	RECT a = p.R(x, y, w, h);
	OffsetRect(&a, p.ox, p.oy);
	ScrollbarPaint(p, x + w - 6, y, visible * kDescLineH, total, visible, st, out.desc, v.mouseX, v.mouseY);
	out.desc.area = a;
}

void PaintButtons(Painter &p, int right, int y, const View &v)
{
	const BattlesSnapshot &s = v.snap;
	const bool ready = !s.starting && s.selList >= 0;
	p.Font(1);
	const int startW = std::max(150, p.TextWidth(s.starting ? "Loading..." : "Start") + 48);
	int x = right - startW;
	p.Button(x, y, startW, kButtonH, s.starting ? "Loading..." : "Start", kActStart, 0, v.mouseX, v.mouseY, ready, true);
	if (s.intro) {
		const int introW = std::max(150, p.TextWidth("Watch intro") + 48);
		x -= 12 + introW;
		p.Button(x, y, introW, kButtonH, "Watch intro", kActIntro, 0, v.mouseX, v.mouseY, ready);
	}
}

void PaintDetail(Painter &p, int x, int y, int w, int h, const View &v, PaintOut &out)
{
	PaintCard(p, x, y, w, h);
	const BattlesSnapshot &s = v.snap;
	out.desc = ListGeom();
	if (s.selList < 0 || s.selIndex < 0 || s.selIndex >= static_cast<int>(s.lists[s.selList].size())) {
		bool empty = true;
		for (const std::vector<BattlesEntry> &l : s.lists) empty = empty && l.empty();
		if (s.open && empty) {
			p.Font(0);
			p.Text(x, y + h / 2 - 20, w, 40, s.tab == kBattlesTabConquests ? "There are no conquests here." : "There are no adventures here.", kDim,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		return;
	}
	const BattleTitle title = SplitBattleTitle(s.lists[s.selList][s.selIndex].title);
	const int innerX = x + kPad, innerW = w - kPad * 2, innerTop = y + kPad, innerBottom = y + h - kPad;
	const int buttonsY = innerBottom - kButtonH;
	if (w >= kWideCard) {
		// The picture at its own size on the left; the text and the buttons in a column beside it.
		const int imgW = std::min(540, innerW * 56 / 100), imgH = std::min(imgW * 360 / 440, buttonsY - innerTop - kDifficultyH - 24);
		const RECT pic = PaintPicture(p, innerX + 4, innerTop + 4, imgW, imgH, s.image.get());
		if (s.difficultyShown) {
			const int dy = pic.bottom + std::max(20, (innerBottom - static_cast<int>(pic.bottom) - kDifficultyH) / 2);
			PaintDifficulty(p, innerX + 4, std::min(dy, innerBottom - kDifficultyH), static_cast<int>(pic.right - pic.left), v);
		}
		const int colX = pic.right + 28, colW = innerX + innerW - colX;
		int ty = PaintTitle(p, colX, innerTop, colW, title, v);
		PaintOrnament(p, kOrnamentRule, colX, ty + 6, colW, 10, kGold);
		ty += 24;
		PaintDescription(p, colX, ty, colW, buttonsY - 16 - ty, v, out);
		PaintButtons(p, innerX + innerW, buttonsY, v);
		return;
	}
	// Narrow: the title and the difficulty beside the picture, the description under it across the card.
	const int imgW = std::min(440, innerW * 56 / 100), imgH = imgW * 360 / 440;
	const RECT pic = PaintPicture(p, innerX + 4, innerTop + 4, imgW, imgH, s.image.get());
	const int colX = pic.right + 24, colW = innerX + innerW - colX;
	PaintTitle(p, colX, innerTop, colW, title, v);
	if (s.difficultyShown) PaintDifficulty(p, colX, pic.bottom - kDifficultyH, colW, v);
	const int descY = pic.bottom + 22;
	PaintOrnament(p, kOrnamentRule, innerX, descY - 8, innerW, 8, kGold);
	PaintDescription(p, innerX, descY + 6, innerW, buttonsY - 14 - descY - 6, v, out);
	PaintButtons(p, innerX + innerW, buttonsY, v);
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
	PaintWindowHeader(p, W, Upper("Great Battles"));

	const char *labels[kBattlesTabCount] = {"Great Battles", "Conquests", "Custom adventures"};
	p.Font(1);
	int tabW = 190;
	for (const char *l : labels) tabW = std::max(tabW, p.TextWidth(l) + 48);
	std::vector<TabSpec> tabs;
	for (int i = 0; i < kBattlesTabCount; i++) tabs.push_back({labels[i], kActTab, i, v.snap.tab == i, !v.snap.starting && !v.snap.switching});
	const int tabsW = tabW * kBattlesTabCount + 6 * (kBattlesTabCount - 1);
	PaintTabs(p, (W - tabsW) / 2, kTabsY, tabs, v.mouseX, v.mouseY, tabW, kTabsH);
	p.Fill(kMargin, kTabsY + kTabsH, W - kMargin * 2, 1, pal::lineDim);

	const int listW = std::max(320, std::min(420, W * 30 / 100));
	PaintList(p, kMargin, kBodyTop, listW, kBodyBottom - kBodyTop, v, out);
	const int detailX = kMargin + listW + kGap;
	PaintDetail(p, detailX, kBodyTop, W - kMargin - detailX, kBodyBottom - kBodyTop, v, out);

	p.Button(kMargin, kFooterY, 150, kFooterH, "Back", kActBack, 0, v.mouseX, v.mouseY, !v.snap.starting);
}

}
