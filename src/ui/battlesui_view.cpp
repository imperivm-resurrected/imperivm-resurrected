// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/battlesui_view.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

#include "game/battlelist.h"
#include "ui/fullscreen.h"
#include "ui/patronui.h"
#include "ui/widgets.h"

using namespace ui;

namespace battlesui {
namespace {

constexpr int kGap = 16, kPad = 20, kHeaderH = 30, kHeaderGap = 14, kButtonH = 40, kSegmentH = 34, kWideCard = 820;

// The pictures are illustrations and keep their own gold frame.
const COLORREF kFrameGold = RGB(214, 170, 84), kFrameGoldDim = RGB(127, 98, 48);

struct Line {
	bool header = false;
	std::string text, path, title;
	int list = -1, index = -1, number = 0;
	int Height(bool first) const { return header ? kHeaderH + (first ? 0 : kHeaderGap) : kListRowH; }
};

int RawWrapped(Painter &p, int x, int y, int w, int h, const std::string &t, COLORREF c)
{
	p.raw = true;
	const int used = p.TextWrapped(x, y, w, h, t, c);
	p.raw = false;
	return used;
}

// League Gothic is only ever drawn in capitals.
std::string Caps(const std::string &text)
{
	std::wstring w = W(text);
	if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
}

std::string Upper(const std::string &text) { return Caps(Tr(text)); }

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

// The name of a campaign that is not installed yet.
const char *OfferTitle(int tab)
{
	return tab == kBattlesTabImperivm1 ? "The Gallic War" : "The Conquest of Hispania";
}

// Whether a tab offers the official download of its campaign: it has one, and it is missing, older or on its way.
bool Offers(const View &v, int tab)
{
	const CampaignPackView &o = v.offers[tab];
	return o.known && (o.downloading || o.queued || o.state != CampaignPackState::kInstalled);
}

// The download of a tab's campaign: its button, or the bar and the bytes while it downloads and unpacks.
void PaintOffer(Painter &p, int x, int y, int w, int h, int tab, const View &v, bool enabled, PaintOut &out)
{
	const CampaignPackView &o = v.offers[tab];
	if (o.downloading) {
		out.turning = true;
		const int barH = 6, barY = y + h - barH;
		p.Fill(x, barY, w, barH, pal::button);
		if (o.total) p.Fill(x, barY, static_cast<int>(static_cast<uint64_t>(w) * std::min(o.done, o.total) / o.total), barH, pal::red);
		p.Font(3);
		p.raw = true;
		const std::string text = o.unpacking ? Tr("Unpacking") + "  \xC2\xB7  " + std::to_string(o.total ? std::min<uint64_t>(100, o.done * 100 / o.total) : 0) + " %"
						     : Tr("Downloading") + "  \xC2\xB7  " + TransferText(o.done, o.total, 0);
		p.Text(x, y, w, h - barH - 2, text, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		return;
	}
	if (o.queued) {
		p.Font(3);
		p.Text(x, y, w, h, "Waiting", pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	p.Button(x, y, w, h, o.state == CampaignPackState::kOutdated ? "Update" : "Download", kActDownload, tab, v.mouseX, v.mouseY, enabled, true);
}

// A campaign with an official download that is not installed yet: its card and its tab do not open until it is.
bool Locked(const View &v, int tab)
{
	const CampaignPackView &o = v.offers[tab];
	return o.known && o.state == CampaignPackState::kMissing && !v.packs[tab].installed;
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
			r.path = entries[i].path;
			r.title = entries[i].title;
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

// The medal of an adventure won: a coin of bronze, silver or gold for the hardest difficulty won (1 to 3), with the laurel wreath
// struck on it. An adventure not won yet shows the wreath alone, faint.
constexpr int kMedal = 22;

void PaintMedal(Painter &p, int x, int y, int size, int level)
{
	static const COLORREF kRim[3] = {RGB(112, 70, 38), RGB(118, 122, 130), RGB(150, 110, 36)};
	static const COLORREF kFace[3] = {RGB(186, 124, 70), RGB(204, 208, 214), RGB(230, 188, 92)};
	static const COLORREF kStrike[3] = {RGB(84, 50, 24), RGB(72, 76, 84), RGB(104, 72, 14)};
	const int wreath = size * 78 / 100;
	if (level < 1 || level > 3) {
		PaintWreath(p, x + (size - wreath) / 2, y + (size - wreath) / 2, wreath, pal::hairline);
		return;
	}
	p.Ellipse(x, y, size, size, kRim[level - 1]);
	p.Ellipse(x + 2, y + 2, size - 4, size - 4, kFace[level - 1]);
	PaintWreath(p, x + (size - wreath) / 2, y + (size - wreath) / 2, wreath, kStrike[level - 1]);
}

// A two-digit number and the name; the chosen row has a quiet fill and a tiny red arrow, every row a hairline under it. A great battle
// shows its medal at the end.
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
		TextAsIs(p, x + 14, y, 30, kListRowH, num, selected ? pal::redText : pal::disabled);
		textX = x + 52;
	}
	const bool laurels = v.snap.tab == kBattlesTabGreat;
	const int laurelsX = x + w - 30 - kMedal;
	if (laurels) PaintMedal(p, laurelsX, y + (kListRowH - kMedal) / 2, kMedal, BattleLaurelOf(v.laurels, l.path, l.title));
	p.Font(selected ? 1 : 0);
	const int textRight = laurels ? laurelsX - 10 : x + w - (selected ? 30 : 12);
	p.Text(textX, y, textRight - textX, kListRowH, l.text, selected ? pal::text : pal::textDim,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (selected) {
		p.Font(1);
		TextAsIs(p, x + w - 26, y, 14, kListRowH, "\xE2\x80\xBA", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	if (hitBottom > hitTop) p.AddHit(x, hitTop, w, hitBottom - hitTop, kActRow, RowArg(l.list, l.index));
}

void PaintList(Painter &p, int x, int y, int w, int h, const View &v, PaintOut &out)
{
	PaintGlassCard(p, x, y, w, h);
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
	int won = 0;
	if (s.tab == kBattlesTabGreat && s.selList >= 0) won = BattleLaurelOf(v.laurels, s.lists[s.selList][s.selIndex].path, s.lists[s.selList][s.selIndex].title);
	for (int i = 0; i < n; i++) {
		const int bx = x + i * segW, bw = i == n - 1 ? w - i * segW : segW;
		const bool on = i == s.difficulty;
		const bool hot = enabled && !on && p.Hover(bx, y, bw, kSegmentH, v.mouseX, v.mouseY);
		p.Fill(bx, y, bw, kSegmentH, on ? pal::text : hot ? pal::button : pal::field);
		p.Frame(bx, y, bw, kSegmentH, on ? pal::text : hot ? pal::edgeHot : pal::edge);
		p.Font(on ? 1 : 0);
		p.Text(bx + 2, y, bw - 4, kSegmentH, s.difficulties[i], on ? pal::onLight : enabled ? pal::text : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		// The difficulties of a great battle are easy, normal and hard, in that order: the medal says which ones were won.
		if (i < won && i < 3) PaintMedal(p, bx + 8, y + (kSegmentH - 16) / 2, 16, i + 1);
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

// Where a picture is missing: a dark box with the brand's mark faintly in its middle.
void PaintPlaceholder(Painter &p, int x, int y, int w, int h)
{
	p.Fill(x, y, w, h, pal::bg);
	p.Frame(x, y, w, h, pal::hairline);
	PaintIsotype(p, x + w / 2, y + h / 2, h * 35 / 100, 46);
}

bool HasPixels(const BattleImage *img) { return img && img->w > 0 && img->h > 0; }

// The picture keeps its shape inside the box and sits at its top left.
RECT PaintPicture(Painter &p, int x, int y, int boxW, int boxH, const BattleImage *img)
{
	int w = boxW, h = boxH;
	if (!HasPixels(img)) {
		h = std::min(boxH, w * 360 / 440);
		PaintPlaceholder(p, x, y, w, h);
		return {x, y, x + w, y + h};
	}
	h = w * img->h / img->w;
	if (h > boxH) {
		h = boxH;
		w = h * img->w / img->h;
	}
	p.Fill(x - 4, y - 4, w + 8, h + 8, pal::bg);
	p.Image(x, y, w, h, img->bgra.data(), img->w, img->h);
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
	TextAsIs(p, x, y, w, 18, caption, pal::redText);
	SetTextCharacterExtra(p.dc, 0);
	y += 24;
	p.Font(2);
	const std::string name = Caps(t.name);
	p.raw = true;
	const int th = std::min(p.Measure(w, name), 90);
	p.raw = false;
	RawWrapped(p, x, y, w, th + 4, name, pal::text);
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
	// A title of the diary: it starts after a tiny diamond, and the lines it wraps to keep in line with it.
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

// The briefing and the diary of the newest game, as tiny tabs over the text: the same underline as the big tabs.
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
		TextAsIs(p, lx, y, lw, kPagesH - 4, labels[i], on || hot ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (on) p.Fill(lx, y + kPagesH - 3, lw - 8, 3, pal::red);
		if (enabled && !on) p.AddHit(lx - 4, y, lw + 8, kPagesH, kActPage, static_cast<int>(i));
		lx += lw + 22;
	}
	SetTextCharacterExtra(p.dc, 0);
	p.Fill(x, y + kPagesH - 1, w, 1, pal::hairline);
}

void PaintDetail(Painter &p, int x, int y, int w, int h, const View &v, PaintOut &out)
{
	PaintGlassCard(p, x, y, w, h);
	const BattlesSnapshot &s = v.snap;
	out.desc = ListGeom();
	if (s.selList < 0 || s.selIndex < 0 || s.selIndex >= static_cast<int>(s.lists[s.selList].size())) {
		bool empty = true;
		for (const Line &l : BuildLines(s)) empty = empty && l.header;
		if (!s.open || !empty) return;
		if (!v.packs[s.tab].installed) {
			p.Font(0);
			const bool offer = Offers(v, s.tab);
			p.Text(x, y + h / 2 - (offer ? 44 : 20), w, 40, EmptyText(s.tab), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (offer) {
				const int bw = std::min(w - 2 * kPad, 360);
				PaintOffer(p, x + (w - bw) / 2, y + h / 2 + 4, bw, kButtonH, s.tab, v, !s.starting && !v.loading, out);
			}
			return;
		}
		// The campaign is in the mods folder, but the content profile in use leaves it out.
		const int cy = y + h / 2;
		p.Font(2);
		TextAsIs(p, x + kPad, cy - 70, w - kPad * 2, 32, Upper("Not active"), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(0);
		p.Text(x + kPad, cy - 34, w - kPad * 2, 24, "Activate it in Additional content", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(1);
		const int bw = std::max(220, p.TextWidth("Additional content") + 48);
		p.Button(x + (w - bw) / 2, cy + 8, bw, kButtonH, "Additional content", kActContent, 0, v.mouseX, v.mouseY, !s.starting && !v.loading, true);
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
		TextAsIs(p, sx + 14, buttonsY - savedH, sw - 14, 20, TrFmt("Last saved: %s", {saved->name + "  \xC2\xB7  " + saved->when}), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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

// What fills a box of another shape around a picture: the picture itself, blurred, darkened and cropped to the box's shape, tiny
// enough to be stretched. Kept for the last pictures and boxes drawn.
struct Backdrop {
	const BattleImage *img = nullptr;
	int boxW = 0, boxH = 0, w = 0, h = 0;
	std::vector<uint32_t> px;
};

const Backdrop &BackdropOf(const BattleImage *img, int boxW, int boxH)
{
	static std::vector<Backdrop> cache;
	for (const Backdrop &b : cache) {
		if (b.img == img && b.boxW == boxW && b.boxH == boxH) return b;
	}
	if (cache.size() >= 24) cache.erase(cache.begin());
	Backdrop b;
	b.img = img;
	b.boxW = boxW;
	b.boxH = boxH;
	// The part of the picture that covers the box, averaged down to a few dozen pixels.
	int cw = img->w, ch = img->w * boxH / std::max(1, boxW);
	if (ch > img->h) {
		ch = img->h;
		cw = img->h * boxW / std::max(1, boxH);
	}
	const int cx = (img->w - cw) / 2, cy = (img->h - ch) / 2;
	const int sw = 40, sh = std::max(1, sw * boxH / std::max(1, boxW));
	std::vector<float> tiny(static_cast<size_t>(sw) * sh * 3, 0.0f);
	for (int y = 0; y < sh; y++) {
		for (int x = 0; x < sw; x++) {
			const int x0 = cx + x * cw / sw, x1 = std::max(x0 + 1, cx + (x + 1) * cw / sw);
			const int y0 = cy + y * ch / sh, y1 = std::max(y0 + 1, cy + (y + 1) * ch / sh);
			float r = 0, g = 0, bl = 0;
			int n = 0;
			for (int yy = y0; yy < y1; yy += 2) {
				for (int xx = x0; xx < x1; xx += 2) {
					const uint32_t c = img->bgra[static_cast<size_t>(yy) * img->w + xx];
					r += c >> 16 & 255;
					g += c >> 8 & 255;
					bl += c & 255;
					n++;
				}
			}
			float *d = &tiny[(static_cast<size_t>(y) * sw + x) * 3];
			d[0] = r / n;
			d[1] = g / n;
			d[2] = bl / n;
		}
	}
	// Two passes of a 3x3 box blur, then up four times with bilinear filtering, darkened.
	for (int pass = 0; pass < 2; pass++) {
		std::vector<float> out(tiny.size());
		for (int y = 0; y < sh; y++) {
			for (int x = 0; x < sw; x++) {
				for (int c = 0; c < 3; c++) {
					float s = 0;
					int n = 0;
					for (int dy = -1; dy <= 1; dy++) {
						for (int dx = -1; dx <= 1; dx++) {
							const int nx = x + dx, ny = y + dy;
							if (nx < 0 || ny < 0 || nx >= sw || ny >= sh) continue;
							s += tiny[(static_cast<size_t>(ny) * sw + nx) * 3 + c];
							n++;
						}
					}
					out[(static_cast<size_t>(y) * sw + x) * 3 + c] = s / n;
				}
			}
		}
		tiny.swap(out);
	}
	b.w = sw * 4;
	b.h = sh * 4;
	b.px.resize(static_cast<size_t>(b.w) * b.h);
	for (int y = 0; y < b.h; y++) {
		const float fy = std::max(0.0f, std::min(sh - 1.0f, (y + 0.5f) / 4 - 0.5f));
		const int y0 = static_cast<int>(fy), y1 = std::min(sh - 1, y0 + 1);
		const float ty = fy - y0;
		for (int x = 0; x < b.w; x++) {
			const float fx = std::max(0.0f, std::min(sw - 1.0f, (x + 0.5f) / 4 - 0.5f));
			const int x0 = static_cast<int>(fx), x1 = std::min(sw - 1, x0 + 1);
			const float tx = fx - x0;
			uint32_t px = 0xff000000u;
			for (int c = 0; c < 3; c++) {
				const auto at = [&](int xx, int yy) { return tiny[(static_cast<size_t>(yy) * sw + xx) * 3 + c]; };
				const float v = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
				px |= static_cast<uint32_t>(std::min(255.0f, v * 0.42f)) << (16 - c * 8);
			}
			b.px[static_cast<size_t>(y) * b.w + x] = px;
		}
	}
	cache.push_back(std::move(b));
	return cache.back();
}

// A picture kept in shape and centred in its box, over its own blurred backdrop where the shapes differ; or the placeholder.
void PaintFitted(Painter &p, int x, int y, int w, int h, const BattleImage *img)
{
	if (!HasPixels(img)) {
		PaintPlaceholder(p, x, y, w, h);
		return;
	}
	int iw = w, ih = w * img->h / img->w;
	if (ih > h) {
		ih = h;
		iw = h * img->w / img->h;
	}
	if (iw < w - 2 || ih < h - 2) {
		const Backdrop &b = BackdropOf(img, w, h);
		p.Image(x, y, w, h, b.px.data(), b.w, b.h);
	}
	else p.Fill(x, y, w, h, pal::bg);
	p.Image(x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, img->bgra.data(), img->w, img->h);
	p.Frame(x, y, w, h, pal::hairline);
}

constexpr int kBannerH = 160, kCampaignsGap = 16, kCampaignsH = 38, kHeaderTabH = 52;
const int kOverviewTabs[] = {kBattlesTabImperivm1, kBattlesTabImperivm2, kBattlesTabGreat, kBattlesTabCustom};

// Where the content goes under the header, in this screen's units.
struct Body {
	int x = 0, top = 0, w = 0, bottom = 0;
};

// The newest game of all, ready to go on; or, with none, where to begin.
void PaintContinue(Painter &p, int x, int y, int w, int h, const View &v)
{
	PaintGlassCard(p, x, y, w, h);
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
	// The picture in the shape of the covers (11:9), the texts beside it and the buttons at the right, clear of the texts.
	const int picH = h - 32, picW = picH * 11 / 9;
	PaintFitted(p, x + 16, y + 16, picW, picH, g.picture.get());
	if (HasPixels(g.picture.get())) p.Frame(x + 13, y + 13, picW + 6, picH + 6, kFrameGold);
	const int tx = x + 16 + picW + 28, tw = x + w - 24 - 2 * bw - 12 - 28 - tx;
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	p.Text(tx, y + 16, tw, 18, Upper("Last game"), pal::redText);
	SetTextCharacterExtra(p.dc, 0);
	p.Font(3);
	p.Text(tx, y + 34, tw, 18, TabName(g.tab), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(2);
	TextAsIs(p, tx, y + 52, tw, 30, Caps(SplitBattleTitle(BattleTitleWithoutGame(g.title)).name), pal::text);
	p.Font(0);
	TextAsIs(p, tx, y + 84, tw, 22, g.name, pal::text);
	std::string when = TrFmt("Saved %s", {g.when});
	if (!g.played.empty()) when += "   \xC2\xB7   " + TrFmt("Played %s", {g.played});
	p.Font(3);
	TextAsIs(p, tx, y + 106, tw, 20, when, pal::textDim);
	// Where the player was: the objective the game completed last.
	if (g.diary && !g.diary->empty()) {
		const ObjectiveNote &n = g.diary->front();
		TextAsIs(p, tx, y + 126, tw, 20, TrFmt("Last objective: %s", {n.title.empty() ? n.id : n.title}), pal::textDim);
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
	if (tab == kBattlesTabConquests) return n == 1 ? Tr("1 conquest") : TrFmt("%s conquests", {std::to_string(n)});
	return n == 1 ? Tr("1 campaign") : TrFmt("%s campaigns", {std::to_string(n)});
}

// The name a card shows: the campaign itself when its game has only one, the count when it has more.
std::string CardTitle(const View &v, int tab, int n)
{
	const BattlesSnapshot &s = v.snap;
	if (tab == kBattlesTabGreat) return Tr("Great Battles of Rome");
	if (tab == kBattlesTabCustom) return Tr("Custom adventures");
	if (n == 0 && !v.packs[tab].installed && v.offers[tab].known) return Tr(OfferTitle(tab));
	if (n == 0) return Tr(v.packs[tab].installed ? "Not active" : "Not installed");
	if (n > 1) return CountText(tab, n);
	for (const BattlesEntry &e : s.lists[kBattlesOthers]) {
		if (BattlesInTab(tab, kBattlesOthers, e.path)) return SplitBattleTitle(BattleTitleWithoutGame(e.title)).name;
	}
	return "";
}

// The cards with several pictures go through them: each stays kCoverHoldMs and the next fades in over kCoverFadeMs. The cards start
// apart so they never change together, and the one under the mouse stays on its picture.
constexpr int kCoverHoldMs = 6000, kCoverFadeMs = 600, kCoverStaggerMs = 1500;

struct CoverTurn {
	bool started = false;
	size_t index = 0;
	DWORD since = 0;
};
CoverTurn g_turns[kBattlesTabCount];

COLORREF Mix(COLORREF a, COLORREF b, int t)
{
	const auto one = [t](int x, int y) { return x + (y - x) * t / 255; };
	return RGB(one(GetRValue(a), GetRValue(b)), one(GetGValue(a), GetGValue(b)), one(GetBValue(a), GetBValue(b)));
}

std::string CoverCaption(const std::string &title)
{
	const BattleTitle t = SplitBattleTitle(BattleTitleWithoutGame(title));
	return t.year.empty() ? t.name : t.name + "  \xC2\xB7  " + t.year;
}

// The picture shown, and the next one fading in over it (alpha above 0) while a card changes.
struct CoverStep {
	size_t shown = 0, next = 0;
	int alpha = 0;
};

CoverStep TurnCover(int tab, int slot, size_t count, bool hot)
{
	CoverTurn &t = g_turns[tab];
	const DWORD now = GetTickCount();
	if (!t.started) {
		t.started = true;
		t.index = 0;
		t.since = now + static_cast<DWORD>(slot * kCoverStaggerMs);
	}
	if (t.index >= count) t.index = 0;
	CoverStep step;
	step.shown = step.next = t.index;
	if (count < 2) return step;
	const int elapsed = static_cast<int>(now - t.since);
	if (elapsed < kCoverHoldMs) {
		if (hot) t.since = now;
		return step;
	}
	if (elapsed >= kCoverHoldMs + kCoverFadeMs) {
		t.index = (t.index + 1) % count;
		t.since = now;
		step.shown = step.next = t.index;
		return step;
	}
	step.next = (t.index + 1) % count;
	step.alpha = (elapsed - kCoverHoldMs) * 255 / kCoverFadeMs;
	return step;
}

// A picture fitted in its box as PaintFitted does, over what is there at `alpha`.
void PaintFittedBlend(Painter &p, int x, int y, int w, int h, const BattleImage *img, int alpha)
{
	if (!HasPixels(img)) return;
	int iw = w, ih = w * img->h / img->w;
	if (ih > h) {
		ih = h;
		iw = h * img->w / img->h;
	}
	if (iw < w - 2 || ih < h - 2) {
		const Backdrop &b = BackdropOf(img, w, h);
		p.ImageBlend(x, y, w, h, b.px.data(), b.w, b.h, alpha);
	}
	p.ImageBlend(x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, img->bgra.data(), img->w, img->h, alpha);
}

void PaintCampaignCard(Painter &p, int x, int y, int w, int h, int tab, int slot, const View &v, PaintOut &out)
{
	const BattlesSnapshot &s = v.snap;
	const bool enabled = s.open && !s.starting && !s.switching && !v.loading;
	const bool locked = Locked(v, tab);
	const bool hot = enabled && !locked && p.Hover(x, y, w, h, v.mouseX, v.mouseY);
	const int n = AdventuresIn(s, tab);
	PaintGlassCard(p, x, y, w, h);
	if (hot) p.Frame(x, y, w, h, pal::edgeHot);
	// A shorter card keeps room for its texts by giving the picture less height.
	const int picH = std::max(90, std::min((w - 24) * 9 / 11, h - 148));
	const std::vector<BattlesSnapshot::Cover> &gallery = s.gallery[tab];
	const CoverStep step = TurnCover(tab, slot, gallery.size(), hot);
	if (gallery.size() > 1) out.turning = true;
	// A campaign whose pack is not active is not in the game's list: its picture comes from the pack.
	if (gallery.empty()) PaintFitted(p, x + 12, y + 12, w - 24, picH, s.covers[tab] ? s.covers[tab].get() : v.packCovers[tab].get());
	else {
		PaintFitted(p, x + 12, y + 12, w - 24, picH, gallery[step.shown].image.get());
		if (step.alpha > 0) {
			PaintFittedBlend(p, x + 12, y + 12, w - 24, picH, gallery[step.next].image.get(), step.alpha);
			p.Frame(x + 12, y + 12, w - 24, picH, pal::hairline);
		}
	}
	const int tx = x + 16, tw = w - 32, ty = y + 22 + picH;
	p.Font(4);
	SetTextCharacterExtra(p.dc, p.S(1));
	TextAsIs(p, tx, ty, tw, 18, Upper(TabName(tab)), pal::redText);
	SetTextCharacterExtra(p.dc, 0);
	p.Font(2);
	const int titleH = RawWrapped(p, tx, ty + 22, tw, 58, Caps(CardTitle(v, tab, n)), n == 0 ? pal::textDim : pal::text);
	// The adventure in the picture, under the campaign's name: it fades out and the next one in, with the picture.
	const int capY = ty + 22 + titleH + 2;
	if (gallery.size() > 1 && capY + 22 <= y + h - 46) {
		const bool second = step.alpha >= 128;
		const int fade = second ? (step.alpha - 128) * 2 : step.alpha * 2;
		const COLORREF ink = second ? Mix(pal::panel, pal::textDim, fade) : Mix(pal::textDim, pal::panel, fade);
		p.Font(3);
		p.raw = true;
		p.Text(tx, capY, tw, 22, CoverCaption(gallery[second ? step.next : step.shown].title), ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	p.Fill(tx, y + h - 42, tw, 1, pal::hairline);
	// How much of the campaign is done, as a gold stroke over the line.
	if (v.done[tab] > 0) p.Fill(tx, y + h - 43, tw * std::min(v.done[tab], 100) / 100, 3, kFrameGold);
	p.Font(3);
	const bool offer = Offers(v, tab);
	if (offer) PaintOffer(p, tx, y + h - 36, tw, 28, tab, v, enabled, out);
	else if (n > 0) {
		const std::string done = v.done[tab] >= 0 ? "  \xC2\xB7  " + TrFmt("%s% done", {std::to_string(v.done[tab])}) : std::string();
		TextAsIs(p, tx, y + h - 36, tw, 24, CountText(tab, n) + done, pal::textDim);
	}
	else if (v.packs[tab].installed) p.Text(tx, y + h - 36, tw, 24, "Activate it in Additional content", pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (v.savedInTab[tab] > 0 && !offer) {
		p.Font(4);
		const std::string badge = Tr("In progress");
		const int bw = std::min(tw / 2, p.TextWidth(badge) + 20);
		p.RoundRect(x + w - 16 - bw, y + h - 34, bw, 20, 10, RGB(52, 70, 34));
		TextAsIs(p, x + w - 16 - bw, y + h - 34, bw, 20, badge, RGB(196, 230, 160), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	if (enabled && !locked) p.AddHit(x, y, w, h, kActOpenTab, tab);
	// The download's button goes over the card's own click.
	if (offer && !v.offers[tab].downloading && !v.offers[tab].queued && enabled) p.AddHit(tx, y + h - 36, tw, 28, kActDownload, tab);
}

void PaintOverview(Painter &p, const Body &b, const View &v, PaintOut &out)
{
	PaintContinue(p, b.x, b.top, b.w, kBannerH, v);
	const int campaignsY = b.top + kBannerH + kCampaignsGap, cardsY = campaignsY + kCampaignsH;
	PaintGroupHeader(p, b.x + 4, campaignsY, b.w - 8, Upper("Campaigns"));
	const int count = static_cast<int>(sizeof(kOverviewTabs) / sizeof(kOverviewTabs[0]));
	const int cardW = (b.w - kGap * (count - 1)) / count;
	for (int i = 0; i < count; i++) PaintCampaignCard(p, b.x + i * (cardW + kGap), cardsY, cardW, b.bottom - cardsY, kOverviewTabs[i], i, v, out);
}

// The caption under the title: how many adventures there are and how many have a game going.
std::string CaptionOf(const View &v)
{
	const BattlesSnapshot &s = v.snap;
	if (!s.open) return "";
	if (s.tab == kBattlesTabConquests) return CountText(s.tab, AdventuresIn(s, s.tab));
	std::string text;
	int saved = 0;
	if (ShowsOverview(v)) {
		int n = 0;
		for (int tab : kOverviewTabs) {
			n += AdventuresIn(s, tab);
			saved += v.savedInTab[tab];
		}
		text = n == 1 ? Tr("1 adventure") : TrFmt("%s adventures", {std::to_string(n)});
	}
	else {
		const int n = AdventuresIn(s, s.tab);
		if (n == 0 && (s.tab == kBattlesTabImperivm1 || s.tab == kBattlesTabImperivm2)) return Tr(v.packs[s.tab].installed ? "Not active" : "Not installed");
		text = CountText(s.tab, n);
		saved = v.savedInTab[s.tab];
	}
	if (saved > 0) text += "  \xC2\xB7  " + TrFmt("%s in progress", {std::to_string(saved)});
	return text;
}

// The tabs on the title's baseline, as the lobby has them, while they fit before `right`: first all as wide as the widest, as there,
// then each as wide as its name with less and less room around it; in a row of their own under the caption when none of that fits.
// Returns where the content starts, in the units of `l`.
int PaintHeaderTabs(Painter &h, const ScreenLayout &l, const std::string &title, const std::vector<TabSpec> &tabs, int right, int mx, int my)
{
	struct Fit {
		bool even;
		int pad, gap;
	};
	static const Fit kFits[] = {{true, 44, 48}, {false, 44, 48}, {false, 32, 40}, {false, 24, 32}, {false, 16, 24}};
	SelectObject(h.dc, BrandFont(BrandFace::Display, l.titleSize, h.s));
	// As PaintScreenTitle measures the title when it has no tabs.
	const int titleW = std::min(std::max(160, right - 32 - l.margin), h.CapsWidth(title, 1) + 4);
	h.Font(2);
	std::vector<int> names;
	int widest = 0;
	for (const TabSpec &t : tabs) {
		names.push_back(h.CapsWidth(t.label, 2));
		widest = std::max(widest, names.back());
	}
	const auto rowW = [&](const Fit &f) {
		int w = (static_cast<int>(names.size()) - 1) * 6;
		for (int n : names) w += (f.even ? widest : n) + f.pad;
		return w;
	};
	const Fit *fit = nullptr;
	for (const Fit &f : kFits) {
		if (!fit && l.margin + titleW + f.gap + rowW(f) + 32 <= right) fit = &f;
	}
	int x = l.margin + titleW + (fit ? fit->gap : 0), y = l.titleY + l.titleH - kHeaderTabH - 4, top = l.top;
	if (!fit) {
		fit = &kFits[sizeof(kFits) / sizeof(kFits[0]) - 1];
		for (const Fit &f : kFits) {
			if (rowW(f) <= l.w - 2 * l.margin) {
				fit = &f;
				break;
			}
		}
		x = l.margin;
		y = l.infoY + l.infoH + (l.roomy ? 20 : 14);
		top = y + kHeaderTabH + (l.roomy ? 28 : 20);
	}
	for (size_t i = 0; i < tabs.size(); i++) {
		const int w = (fit->even ? widest : names[i]) + fit->pad;
		PaintTabs(h, x, y, {tabs[i]}, mx, my, w, kHeaderTabH, 2);
		x += w + 6;
	}
	return top;
}

// The header of the full screens, the lobby's, drawn in their units over the same canvas so that it comes out at the size it has
// there; returns where the content goes, in this screen's units.
Body PaintHeader(Painter &p, int screenW, const ScreenView &brand, const View &v)
{
	const float k = brand.s / p.s;
	Painter h = p;
	h.s = brand.s;
	UseScreenFonts(h);
	const ScreenLayout l = ScreenLayoutFor(static_cast<int>(screenW / k + 0.5f), static_cast<int>(kScreenH / k + 0.5f));
	const BattlesSnapshot &s = v.snap;
	// The Conquest button of the main menu opens the same screen with only the conquests: it has no tabs.
	const bool conquests = s.tab == kBattlesTabConquests, overview = ShowsOverview(v);
	// On the overview Back leaves the screen and in a campaign it goes back to the overview; the conquests have no overview.
	const int back = overview || conquests ? kActBack : kActOverview;
	const int right = PaintHeaderActions(h, l, {{"Back", back, true, !s.starting && !v.loading}}, v.mouseX, v.mouseY);
	const std::string title = conquests ? "Conquests" : "Great Battles";
	PaintScreenTitle(h, l, title, {}, right, v.mouseX, v.mouseY);
	int top = l.top;
	if (!overview && !conquests) {
		std::vector<TabSpec> tabs;
		for (int tab : kOverviewTabs) tabs.push_back({TabName(tab), kActTab, tab, s.tab == tab, !s.starting && !s.switching && !Locked(v, tab)});
		top = PaintHeaderTabs(h, l, title, tabs, right, v.mouseX, v.mouseY);
	}
	h.raw = true;
	PaintScreenInfo(h, l, CaptionOf(v));
	Body b;
	b.x = static_cast<int>(l.margin * k + 0.5f);
	b.top = static_cast<int>(top * k + 0.5f);
	b.w = screenW - 2 * b.x;
	b.bottom = kScreenH - b.x;
	return b;
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

void PaintBattlesScreen(Painter &p, int screenW, const ScreenView &brand, const View &v, PaintOut &out)
{
	const Body b = PaintHeader(p, screenW, brand, v);
	// The overview: the game to go on with and a card per campaign.
	if (ShowsOverview(v)) {
		out = PaintOut();
		PaintOverview(p, b, v, out);
		return;
	}
	const int listW = std::max(320, std::min(420, screenW * 30 / 100));
	PaintList(p, b.x, b.top, listW, b.bottom - b.top, v, out);
	const int detailX = b.x + listW + kGap;
	PaintDetail(p, detailX, b.top, b.x + b.w - detailX, b.bottom - b.top, v, out);
}

}
