// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/profileui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <initializer_list>
#include <utility>
#include <vector>

#include "i18n/tr.h"
#include "ui/patronui.h"
#include "ui/widgets.h"

using namespace ui;

namespace profileui {
namespace {

constexpr int kMargin = 24, kTop = 96;
constexpr int kListX = kMargin, kListW = 290, kListTop = 122, kRowH = 58, kRowGap = 6;
constexpr int kDetailX = kListX + kListW + 24;
constexpr int kIdentityY = kTop, kIdentityH = 108, kAvatar = 80;
constexpr int kTilesY = kIdentityY + kIdentityH + 12, kTileH = 76, kTileGap = 10;
constexpr int kLowerY = kTilesY + kTileH + 12, kLowerBottom = kScreenH - 68;
constexpr int kCardPad = 14, kCardHead = 30, kGap = 12;
// At full width: the summary in two columns over the evolution, and the history beside them.
constexpr int kWideLeftW = 420, kWideSummaryH = 152;
// Beside the list: the summary in three columns over the evolution and the history.
constexpr int kStripH = 112, kNarrowChartW = 384;
constexpr int kFooterTop = kScreenH - 58, kFooterButtonsY = kScreenH - 48, kFooterButtonH = 36;
constexpr int kSegH = 20, kSegPad = 8, kDetailCellH = 32;

const COLORREF kWonColor = RGB(126, 170, 92), kLostColor = pal::redText, kAbandonedColor = pal::disabled, kMuted = pal::caption;
const COLORREF kSelectedTop = pal::selected, kRowBody = pal::panel, kRowHot = pal::hairline, kCardBody = pal::panel;
const COLORREF kHead = pal::edgeHot, kRule = pal::hairline;
const char kDash[] = "\xE2\x80\x94";

struct Box {
	int x = 0, y = 0, w = 0, h = 0;
};

struct Lower {
	Box summary, chart, history;
	int summaryColumns = 2;
};

// What a hovered element explains, drawn over everything at the end.
struct Tip {
	std::vector<std::pair<std::string, COLORREF>> lines;
	int x = 0, y = 0;
};

struct Area {
	int x = 0, w = 0;
	bool wide = false;
};

COLORREF Mix(COLORREF a, COLORREF b, float t)
{
	auto ch = [t](int x, int y) { return static_cast<BYTE>(x + (y - x) * t + 0.5f); };
	return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

// From dull bronze for the lowest rank through silver and gold to crimson and a regal purple for the highest, spread over
// however many ranks the game's constants define.
COLORREF RankColor(int rank, int count)
{
	static const COLORREF stops[] = {RGB(164, 108, 64), RGB(206, 140, 70), RGB(184, 198, 222), RGB(244, 196, 80), RGB(230, 90, 80), RGB(196, 128, 244)};
	constexpr int last = sizeof(stops) / sizeof(stops[0]) - 1;
	if (rank < 0 || count <= 0) return pal::redText;
	const float t = count > 1 ? std::min(1.0f, static_cast<float>(rank) / (count - 1)) * last : 0.0f;
	const int i = std::min(last - 1, static_cast<int>(t));
	return Mix(stops[i], stops[i + 1], t - i);
}

void PaintRankBar(Painter &p, int x, int y, int w, int h, float fraction, COLORREF c)
{
	p.Fill(x, y, w, h, pal::bg);
	const int filled = static_cast<int>(w * std::max(0.0f, std::min(1.0f, fraction)));
	if (filled > 0) p.Fill(x, y, filled, h, Mix(c, RGB(255, 255, 255), 0.2f));
}

Area DetailArea(const View &v)
{
	if (ShowsList(v)) return {kDetailX, kScreenW - kMargin - kDetailX, false};
	return {kMargin, kScreenW - 2 * kMargin, true};
}

// Names, numbers and texts already in the player's language skip the painter's translation.
void Raw(Painter &p, int x, int y, int w, int h, const std::string &t, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, t, c, fmt);
	p.raw = false;
}

int RawWidth(Painter &p, const std::string &t)
{
	p.raw = true;
	const int w = p.TextWidth(t);
	p.raw = false;
	return w;
}

std::string Grouped(uint64_t n)
{
	std::string s = std::to_string(n);
	// A no-break space: neither a comma nor a dot reads as a decimal point in any of the three languages.
	for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, "\xC2\xA0");
	return s;
}

std::string Percent(int value) { return std::to_string(value) + "\xC2\xA0%"; }

std::string Initials(const std::string &name)
{
	const std::wstring w = W(name);
	std::wstring out;
	bool start = true;
	for (wchar_t c : w) {
		if (c == L' ' || c == L'_' || c == L'-' || c == L'.') {
			start = true;
			continue;
		}
		if (start && out.size() < 2) out += c;
		start = false;
	}
	if (out.empty()) return "?";
	if (out.size() == 1 && w.size() > 1 && w[1] != L' ') out += w[1];
	CharUpperBuffW(&out[0], static_cast<DWORD>(out.size()));
	const int n = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), static_cast<int>(out.size()), nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, out.c_str(), static_cast<int>(out.size()), &s[0], n, nullptr, nullptr);
	return s;
}

std::string GamesText(int games)
{
	if (games == 0) return Tr("No games yet");
	return games == 1 ? Tr("1 game") : TrFmt("%s games", {Grouped(games)});
}

std::string Duration(uint32_t ms)
{
	const uint32_t minutes = ms / 60000;
	char buf[32];
	if (minutes == 0) return "<1 min";
	if (minutes < 60) sprintf_s(buf, "%u min", minutes);
	else sprintf_s(buf, "%u h %02u", minutes / 60, minutes % 60);
	return buf;
}

std::string GamesAndWins(int games, int wins)
{
	if (!games) return kDash;
	return TrFmt("%s \xC2\xB7 %s won", {Grouped(games), Percent(PercentOf(wins, games))});
}

void PaintMonogram(Painter &p, int x, int y, int size, const std::string &name, bool strong)
{
	p.Fill(x, y, size, size, strong ? pal::selected : pal::raised);
	p.Frame(x, y, size, size, strong ? pal::redText : pal::caption);
	p.Font(size >= 60 ? 5 : 1);
	Raw(p, x, y, size, size, Initials(name), strong ? pal::text : pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintListRow(Painter &p, const Card &c, int rankCount, int index, bool selected, int x, int y, int w, int mx, int my)
{
	const bool hot = !selected && p.Hover(x, y, w, kRowH, mx, my);
	if (selected) {
		p.Fill(x, y, w, kRowH, pal::selected);
		p.Fill(x, y, 3, kRowH, pal::red);
	}
	else {
		p.Fill(x, y, w, kRowH, hot ? kRowHot : kRowBody);
		p.Frame(x, y, w, kRowH, pal::hairline);
	}
	PaintMonogram(p, x + 12, y + 9, 40, c.name, selected);
	int nameW = w - 74;
	if (c.active) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 14;
		PaintBadge(p, x + w - bw - 10, y + 11, bw, 17, "In use", pal::hairline, pal::redText);
		nameW -= bw + 8;
	}
	p.Font(1);
	Raw(p, x + 62, y + 9, nameW, 22, c.name, selected ? pal::text : pal::text);
	p.Font(3);
	const std::string games = GamesText(c.summary.games);
	const COLORREF dim = selected ? pal::textDim : pal::textDim;
	if (c.rankName.empty() || !c.readable) {
		Raw(p, x + 62, y + 31, w - 74, 18, games, dim);
	}
	else {
		const int rankW = std::min(w - 74, RawWidth(p, c.rankName));
		Raw(p, x + 62, y + 31, rankW, 18, c.rankName, RankColor(c.summary.rank, rankCount));
		Raw(p, x + 62 + rankW, y + 31, w - 74 - rankW, 18, "  \xC2\xB7  " + games, dim);
	}
	p.AddHit(x, y, w, kRowH, kActPick, index);
}

void PaintList(Painter &p, const View &v)
{
	p.Font(4);
	p.Text(kListX, kTop, kListW, 20, "Players", pal::redText);
	p.Font(3);
	Raw(p, kListX, kTop, kListW, 20, std::to_string(v.cards.size()), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const int count = static_cast<int>(v.cards.size());
	const int maxScroll = std::max(0, count - kListRows);
	const int scroll = std::max(0, std::min(v.scroll, maxScroll));
	const int rowW = maxScroll > 0 ? kListW - 12 : kListW;
	for (int r = 0; r < kListRows && scroll + r < count; r++) {
		const int i = scroll + r;
		PaintListRow(p, v.cards[i], v.rankCount, i, i == v.selected, kListX, kListTop + r * (kRowH + kRowGap), rowW, v.mouseX, v.mouseY);
	}
	if (maxScroll > 0) {
		ScrollbarLook look;
		look.minThumb = 24;
		PaintScrollbar(p, kBarList, kListX + kListW - 6, kListTop, kListRows * (kRowH + kRowGap) - kRowGap, {scroll, maxScroll, kListRows}, v.bar,
			v.mouseX, v.mouseY, look);
	}
	p.Font(3);
	p.TextWrapped(kListX + 2, kLowerBottom - 34, kListW - 4, 34, "Double-click a player to use it.", kMuted);
}

void PaintIdentity(Painter &p, const View &v, const Card &c, const Area &a)
{
	const int x = a.x, y = kIdentityY, w = a.w;
	PaintCard(p, x, y, w, kIdentityH);
	const int ax = x + 14, ay = y + (kIdentityH - kAvatar) / 2;
	const bool steam = c.active && v.avatar && !v.avatar->bgra.empty();
	if (steam) {
		p.Image(ax, ay, kAvatar, kAvatar, v.avatar->bgra.data(), v.avatar->width, v.avatar->height);
		p.Frame(ax, ay, kAvatar, kAvatar, pal::redText);
	}
	else {
		PaintMonogram(p, ax, ay, kAvatar, c.name, true);
	}
	const int tx = ax + kAvatar + 18;
	int right = x + w - 14;
	if (c.active && ShowsList(v)) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 16;
		PaintBadge(p, right - bw, y + 14, bw, 18, "In use", pal::hairline, pal::redText);
		right -= bw + 12;
	}
	p.Font(2);
	const PatronTier tier = c.active ? v.patron.tier : PatronTier::None;
	if (tier != PatronTier::None) {
		const int laurel = PatronLaurelSize(tier);
		p.raw = true;
		DrawPatron(p, tx, y + 14 + (30 - laurel) / 2, laurel, tx + laurel + 8, y + 12, right - tx - laurel - 8, 34, c.name, tier,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		PatronHotspot(p, tx, y + 14 + (30 - laurel) / 2, laurel, v.patron, v.mouseX, v.mouseY, kActPatron);
	}
	else {
		Raw(p, tx, y + 12, right - tx, 34, c.name, pal::text);
	}
	if (!c.readable) {
		p.Font(3);
		p.Text(tx, y + 50, x + w - 14 - tx, 20, "Couldn't read this player's statistics.", pal::bad);
		return;
	}
	const ProfileSummary &s = c.summary;
	if (c.rankName.empty()) {
		p.Font(1);
		Raw(p, tx, y + 48, x + w - 14 - tx, 22, kDash, pal::redText);
		return;
	}
	const int barW = a.wide ? x + w - 14 - tx : std::min(380, x + w - 14 - tx);
	const std::string rating = Grouped(static_cast<uint64_t>(std::max<int64_t>(0, s.rating)));
	float fraction = 1.0f;
	std::string label;
	const std::string ahead = Tr(s.nextRank >= 0 ? "Next:" : "Highest rank");
	if (s.nextRank >= 0) {
		const int span = std::max(1, s.nextPoints - s.rankPoints);
		fraction = static_cast<float>(s.rating - s.rankPoints) / span;
		label = TrFmt("Military rating %s / %s", {rating, Grouped(static_cast<uint64_t>(std::max(0, s.nextPoints)))});
	}
	else {
		label = TrFmt("Military rating %s", {rating});
	}
	// The rank reached goes over the start of the bar and the one ahead over its end.
	const int barRight = tx + barW;
	p.Font(4);
	const int nextW = s.nextRank >= 0 ? std::min(barW / 2, RawWidth(p, c.nextRankName)) : 0;
	if (nextW) {
		Raw(p, barRight - nextW, y + 48, nextW, 22, c.nextRankName, RankColor(s.nextRank, v.rankCount), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	p.Font(3);
	const int aheadW = RawWidth(p, ahead);
	const int aheadX = barRight - nextW - (nextW ? 6 : 0) - aheadW;
	Raw(p, aheadX, y + 48, aheadW + 2, 22, ahead, pal::textDim);
	p.Font(1);
	Raw(p, tx, y + 48, std::max(0, aheadX - 16 - tx), 22, c.rankName, RankColor(s.rank, v.rankCount));
	PaintRankBar(p, tx, y + 74, barW, 7, fraction, RankColor(s.rank, v.rankCount));
	p.Frame(tx - 1, y + 73, barW + 2, 9, pal::hairline);
	p.Font(3);
	Raw(p, tx, y + 84, x + w - 14 - tx, 18, label, pal::textDim);
}

void PaintTiles(Painter &p, const Card &c, const Area &a)
{
	const ProfileSummary &s = c.summary;
	const int tileW = (a.w - 3 * kTileGap) / 4;
	const bool any = c.readable && s.games > 0;
	std::string hours = kDash;
	if (any) {
		const uint64_t h = s.playedMs / 3600000;
		hours = h == 0 ? "<1" : Grouped(h);
	}
	const std::string values[4] = {c.readable ? Grouped(s.games) : std::string(kDash), any ? Percent(PercentOf(s.singleWins + s.multiWins, s.games)) : std::string(kDash),
		hours, any ? Grouped(s.kills) : std::string(kDash)};
	const char *labels[4] = {"Games", "Wins", "Hours played", "Kills"};
	for (int i = 0; i < 4; i++) {
		const int x = a.x + i * (tileW + kTileGap), w = i == 3 ? a.x + a.w - x : tileW;
		PaintCard(p, x, kTilesY, w, kTileH);
		p.Font(5);
		Raw(p, x, kTilesY + 6, w, 42, values[i], pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x + 6, kTilesY + 50, w - 12, 18, labels[i], pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintCardTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	p.Font(4);
	p.Text(x + kCardPad, y + 7, w - kCardPad * 2, 18, title, pal::redText);
}

// Two columns over the evolution at full width, three in a strip beside the list.
void PaintSummary(Painter &p, const Card &c, int x, int y, int w, int h, int columns)
{
	PaintCard(p, x, y, w, h);
	PaintCardTitle(p, x, y, w, "Summary");
	const ProfileSummary &s = c.summary;
	const bool ok = c.readable;
	std::string civ = kDash;
	if (ok && s.favoriteRace >= 0) civ = (c.favoriteCiv.empty() ? Tr("Unknown") : c.favoriteCiv) + " \xC2\xB7 " + Percent(s.favoriteRacePercent);
	const std::string best = ok && !c.bestUnit.empty() ? TrFmt("%s \xC2\xB7 level %s", {c.bestUnit, std::to_string(s.bestLevel)}) : std::string(kDash);
	const std::string army = ok && s.largestArmy ? TrFmt("%s units", {Grouped(s.largestArmy)}) : std::string(kDash);
	const std::pair<const char *, std::string> rows[] = {
		{"Skirmish", ok ? GamesAndWins(s.singleGames, s.singleWins) : std::string(kDash)},
		{"Multiplayer", ok ? GamesAndWins(s.multiGames, s.multiWins) : std::string(kDash)},
		{"Favourite civilization", civ},
		{"Favourite unit", ok && !c.favoriteUnit.empty() ? c.favoriteUnit : std::string(kDash)},
		{"Best unit", best},
		{"Largest army", army},
	};
	constexpr int count = sizeof(rows) / sizeof(rows[0]), colGap = 20;
	const int rowH = columns > 1 ? 38 : std::min(46, (h - kCardHead - 10) / count), inner = w - kCardPad * 2, colW = (inner - (columns - 1) * colGap) / columns;
	for (int i = 0; i < count; i++) {
		const int cx = x + kCardPad + (i % columns) * (colW + colGap), ry = y + kCardHead + (i / columns) * rowH;
		if (i >= columns) p.Fill(cx, ry, colW, 1, pal::hairline);
		p.Font(3);
		p.Text(cx, ry + 3, colW, 17, rows[i].first, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(4);
		Raw(p, cx, ry + 19, colW, 18, rows[i].second, pal::text);
	}
}

Lower LowerLayout(const Area &a)
{
	Lower l;
	const int h = kLowerBottom - kLowerY;
	if (a.wide) {
		l.summary = {a.x, kLowerY, kWideLeftW, kWideSummaryH};
		l.chart = {a.x, kLowerY + kWideSummaryH + kGap, kWideLeftW, h - kWideSummaryH - kGap};
		l.history = {a.x + kWideLeftW + kGap, kLowerY, a.w - kWideLeftW - kGap, h};
		l.summaryColumns = 2;
		return l;
	}
	const int y = kLowerY + kStripH + kGap;
	l.summary = {a.x, kLowerY, a.w, kStripH};
	l.chart = {a.x, y, kNarrowChartW, kLowerBottom - y};
	l.history = {a.x + kNarrowChartW + kGap, y, a.w - kNarrowChartW - kGap, kLowerBottom - y};
	l.summaryColumns = 3;
	return l;
}

std::string DateText(const GameRow &g)
{
	char buf[32];
	sprintf_s(buf, "%02d/%02d/%04d %02d:%02d", g.day, g.month, g.year, g.hour, g.minute);
	return buf;
}

std::string AgeText(const View &v, const GameRow &g)
{
	if (!v.todayYear) return DateText(g).substr(0, 10);
	const MatchAge age = MatchAgeOf(g.year, g.month, g.day, v.todayYear, v.todayMonth, v.todayDay);
	const std::string n = std::to_string(age.count);
	switch (age.unit) {
	case kAgeToday: return Tr("Today");
	case kAgeYesterday: return Tr("Yesterday");
	case kAgeDays: return TrFmt("%s days ago", {n});
	case kAgeWeeks: return age.count == 1 ? Tr("1 week ago") : TrFmt("%s weeks ago", {n});
	case kAgeMonths: return age.count == 1 ? Tr("1 month ago") : TrFmt("%s months ago", {n});
	default: return age.count == 1 ? Tr("1 year ago") : TrFmt("%s years ago", {n});
	}
}

std::string OneDecimal(double value)
{
	const long long tenths = static_cast<long long>(std::max(0.0, value) * 10 + 0.5);
	// The decimal mark of the player's language.
	return TrFmt("%s.%s", {Grouped(static_cast<uint64_t>(tenths / 10)), std::to_string(tenths % 10)});
}

std::string MetricText(int metric, double value)
{
	switch (metric) {
	case kMetricKillsPerLoss: return OneDecimal(value);
	case kMetricDuration: return Duration(static_cast<uint32_t>(std::max(0.0, value) * 60000 + 0.5));
	default: return Grouped(static_cast<uint64_t>(std::max(0.0, value) + 0.5));
	}
}

const char *const kMetricShort[kMetricCount] = {"Kills", "Losses", "K/L ratio", "Army", "Gold", "Duration"};
const char *const kMetricLong[kMetricCount] = {"Kills", "Losses", "Kills per loss", "Largest army", "Gold", "Duration"};

COLORREF ResultColor(MatchResult r) { return r == kMatchWon ? kWonColor : r == kMatchLost ? kLostColor : kAbandonedColor; }
const char *ResultText(MatchResult r) { return r == kMatchWon ? "Victory" : r == kMatchLost ? "Defeat" : "Abandoned"; }

std::string CivText(const GameRow &g) { return g.civ.empty() ? Tr("Unknown") : g.civ; }
std::string ModeText(const GameRow &g) { return Tr(g.multi ? "Multiplayer" : "Skirmish"); }

std::vector<MatchFacts> FactsOfCard(const Card &c)
{
	std::vector<MatchFacts> out;
	out.reserve(c.games.size());
	for (const GameRow &g : c.games) out.push_back(FactsOf(g));
	return out;
}

int SegmentsWidth(Painter &p, const std::vector<std::string> &labels)
{
	p.Font(3);
	int w = 0;
	for (const std::string &l : labels) w += p.TextWidth(l) + 2 * kSegPad;
	return w;
}

// Translated labels side by side in a row as wide as `w`, the room left over shared between them.
void PaintSegments(Painter &p, int x, int y, int w, const std::vector<std::string> &labels, int selected, int action, int mx, int my)
{
	const int n = static_cast<int>(labels.size());
	if (!n) return;
	p.Font(3);
	std::vector<int> widths;
	int natural = 0;
	for (const std::string &l : labels) {
		widths.push_back(p.TextWidth(l) + 2 * kSegPad);
		natural += widths.back();
	}
	const int extra = std::max(0, w - natural);
	int bx = x;
	for (int i = 0; i < n; i++) {
		const int bw = i == n - 1 ? x + w - bx : widths[i] + extra / n;
		const bool on = i == selected;
		const bool hot = !on && p.Hover(bx, y, bw, kSegH, mx, my);
		if (on) p.Fill(bx, y, bw, kSegH, pal::text);
		else p.Fill(bx, y, bw, kSegH, hot ? pal::buttonHover : pal::field);
		p.Frame(bx, y, bw, kSegH, on ? pal::text : hot ? pal::edgeHot : pal::edge);
		p.Text(bx + 2, y, bw - 4, kSegH, labels[i], on ? pal::onLight : pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!on) p.AddHit(bx, y, bw, kSegH, action, i);
		bx += bw - 1;
	}
}

std::vector<std::string> Translated(std::initializer_list<const char *> keys)
{
	std::vector<std::string> out;
	for (const char *k : keys) out.push_back(Tr(k));
	return out;
}

void DashedH(Painter &p, int x, int y, int w, COLORREF c, int dash = 5, int gap = 4)
{
	for (int dx = 0; dx < w; dx += dash + gap) p.Fill(x + dx, y, std::min(dash, w - dx), 1, c);
}

void PaintTip(Painter &p, const Tip &tip)
{
	if (tip.lines.empty()) return;
	int w = 0;
	for (size_t i = 0; i < tip.lines.size(); i++) {
		p.Font(i == 0 ? 4 : 3);
		w = std::max(w, RawWidth(p, tip.lines[i].first));
	}
	w += 22;
	const int lineH = 18, h = static_cast<int>(tip.lines.size()) * lineH + 12;
	const int x = std::max(8, std::min(tip.x, kScreenW - 8 - w)), y = std::max(8, std::min(tip.y, kScreenH - 8 - h));
	p.Fill(x + 3, y + 3, w, h, pal::shadow);
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, pal::edge);
	for (size_t i = 0; i < tip.lines.size(); i++) {
		p.Font(i == 0 ? 4 : 3);
		Raw(p, x + 11, y + 6 + static_cast<int>(i) * lineH, w - 22, lineH, tip.lines[i].first, tip.lines[i].second);
	}
}

int MouseX(const Painter &p, const View &v) { return static_cast<int>((v.mouseX - p.ox) / p.s); }
int MouseY(const Painter &p, const View &v) { return static_cast<int>((v.mouseY - p.oy) / p.s); }

void PaintEvolution(Painter &p, const View &v, const Card &c, const Box &b, Tip &tip)
{
	PaintCard(p, b.x, b.y, b.w, b.h);
	PaintCardTitle(p, b.x, b.y, b.w, "Evolution");
	const int x0 = b.x + kCardPad, inner = b.w - kCardPad * 2, right = x0 + inner;
	if (c.games.empty()) {
		p.Font(3);
		p.Text(b.x, b.y + kCardHead, b.w, b.h - kCardHead - 8, c.readable ? "No games yet" : kDash, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	// The mode follows the title, the metrics take the row under it and the range goes under the plot.
	const std::vector<std::string> modes = Translated({"All games", "Skirmish", "Multiplayer"});
	const int modeW = SegmentsWidth(p, modes);
	PaintSegments(p, right - modeW, b.y + 6, modeW, modes, v.chartMode, kActChartMode, v.mouseX, v.mouseY);
	const int cy = b.y + kCardHead + 2;
	std::vector<std::string> metrics;
	for (const char *m : kMetricShort) metrics.push_back(Tr(m));
	PaintSegments(p, x0, cy, inner, metrics, v.metric, kActMetric, v.mouseX, v.mouseY);
	const std::vector<std::string> ranges = Translated({"Last 20", "Last 50", "All games"});
	const int rangeW = SegmentsWidth(p, ranges);
	const int footY = b.y + b.h - kSegH - 8;
	PaintSegments(p, x0, footY, rangeW, ranges, v.chartRange, kActChartRange, v.mouseX, v.mouseY);

	const std::vector<MatchFacts> facts = FactsOfCard(c);
	const size_t limit = v.chartRange == kRange20 ? 20 : v.chartRange == kRange50 ? 50 : 0;
	const std::vector<size_t> shown = MatchSelect(facts, static_cast<MatchMode>(v.chartMode), kOutcomeAll, limit);
	const int plotTop = cy + kSegH + 24, bottom = footY - 8, plotH = bottom - plotTop;
	if (shown.empty() || plotH < 20) {
		p.Font(3);
		p.Text(x0, plotTop, inner, std::max(20, plotH), "No games in this filter.", kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const MatchMetric metric = static_cast<MatchMetric>(v.metric);
	std::vector<double> values;
	for (size_t i : shown) values.push_back(MatchMetricValue(facts[i], metric));
	const double top = MatchChartTop(values, metric == kMetricKillsPerLoss ? 0.5 : 1);
	const double average = MatchAverage(values);
	const int n = static_cast<int>(shown.size());
	const double slot = static_cast<double>(inner) / n;
	const int barW = std::max(1, std::min(26, static_cast<int>(slot * 0.72)));
	const int mx = MouseX(p, v), my = MouseY(p, v);
	p.Fill(x0, bottom, inner, 1, pal::lineStrong);
	DashedH(p, x0, plotTop, inner, kRule);
	p.Font(3);
	Raw(p, x0, plotTop - 17, 70, 15, MetricText(metric, top), pal::edgeHot);
	int hovered = -1;
	for (int k = 0; k < n; k++) {
		const size_t game = shown[n - 1 - k];
		const double value = values[n - 1 - k];
		const int sx = x0 + static_cast<int>(k * slot), sw = std::max(1, static_cast<int>((k + 1) * slot) - static_cast<int>(k * slot));
		const bool hot = mx >= sx && mx < sx + sw && my >= plotTop - 4 && my <= bottom;
		const int bh = std::min(plotH, std::max(value > 0 ? 2 : 1, static_cast<int>(plotH * value / top + 0.5)));
		const int bx = x0 + static_cast<int>(k * slot + (slot - barW) / 2);
		const MatchResult r = MatchResultOf(facts[game]);
		const COLORREF color = ResultColor(r);
		const COLORREF lit = hot ? Mix(color, RGB(255, 255, 255), 0.3f) : color;
		p.Fill(bx, bottom - bh, barW, bh, lit);
		if (static_cast<int>(game) == v.expanded) p.Frame(bx - 1, bottom - bh - 1, barW + 2, bh + 2, pal::text);
		p.AddHit(sx, plotTop - 4, sw, bottom - plotTop + 4, kActBar, static_cast<int>(game));
		if (hot) hovered = k;
	}
	const int avgY = bottom - std::min(plotH, static_cast<int>(plotH * average / top + 0.5));
	DashedH(p, x0, avgY, inner, pal::text, 6, 4);
	const std::string avgText = TrFmt("Average %s", {MetricText(metric, average)});
	p.Font(3);
	const int avgW = RawWidth(p, avgText) + 10;
	const int avgLabelY = avgY - 17 < plotTop - 2 ? avgY + 3 : avgY - 17;
	p.Fill(right - avgW, avgLabelY, avgW, 15, kCardBody);
	Raw(p, right - avgW + 5, avgLabelY, avgW - 5, 15, avgText, pal::text);

	Raw(p, x0 + rangeW + 10, footY, inner - rangeW - 10, kSegH, n == 1 ? Tr("1 game") : TrFmt("%s games", {Grouped(n)}), kMuted,
		DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	int lx = right;
	for (MatchResult r : {kMatchAbandoned, kMatchLost, kMatchWon}) {
		const int tw = p.TextWidth(Tr(ResultText(r)));
		lx -= tw;
		p.Text(lx, plotTop - 18, tw + 2, 16, ResultText(r), pal::textDim);
		lx -= 14;
		p.Fill(lx, plotTop - 14, 9, 9, ResultColor(r));
		lx -= 12;
	}

	if (hovered >= 0) {
		const size_t game = shown[n - 1 - hovered];
		const GameRow &g = c.games[game];
		const MatchResult r = MatchResultOf(facts[game]);
		tip.lines.push_back({DateText(g) + "  \xC2\xB7  " + Tr(ResultText(r)), ResultColor(r) == kAbandonedColor ? pal::textDim : Mix(ResultColor(r), RGB(255, 255, 255), 0.35f)});
		tip.lines.push_back({CivText(g) + " \xC2\xB7 " + ModeText(g), pal::text});
		tip.lines.push_back({Tr(kMetricLong[metric]) + ": " + MetricText(metric, values[n - 1 - hovered]), pal::text});
		tip.lines.push_back({TrFmt("Kills %s \xC2\xB7 Losses %s", {Grouped(g.kills), Grouped(g.losses)}) + " \xC2\xB7 " + Duration(g.durationMs), pal::textDim});
		tip.x = x0 + static_cast<int>(hovered * slot + slot) + 10;
		tip.y = plotTop;
	}
}

struct HistoryGeom {
	int x0 = 0, inner = 0, listTop = 0, listBottom = 0, rowH = 28, columns = 4;
	bool wide = true;
};

HistoryGeom GeomOf(const Box &b)
{
	HistoryGeom g;
	g.x0 = b.x + kCardPad;
	g.inner = b.w - kCardPad * 2;
	g.wide = g.inner >= 460;
	g.rowH = g.wide ? 28 : 40;
	g.columns = g.wide ? 4 : 3;
	g.listTop = b.y + kCardHead + (g.wide ? 20 : kSegH + 8);
	g.listBottom = b.y + b.h - 8;
	return g;
}

struct DetailCell {
	std::string label, value;
	// 1 above the average, -1 below; good tells the colour.
	int trend = 0;
	bool good = false;
};

// The duration is in the row already. Narrow, the ratio takes its short name.
std::vector<DetailCell> DetailCells(const GameRow &g, const std::vector<MatchFacts> &facts, bool narrow)
{
	const MatchFacts f = FactsOf(g);
	auto trend = [&](MatchMetric m, bool higherIsBetter, DetailCell &cell) {
		const double avg = MatchMetricAverage(facts, m), value = MatchMetricValue(f, m);
		if (avg <= 0 || std::fabs(value - avg) < avg * 0.05) return;
		cell.trend = value > avg ? 1 : -1;
		cell.good = (value > avg) == higherIsBetter;
	};
	std::vector<DetailCell> cells;
	cells.push_back({Tr("Units produced"), Grouped(g.produced)});
	cells.push_back({Tr("Kills"), Grouped(g.kills)});
	trend(kMetricKills, true, cells.back());
	cells.push_back({Tr("Losses"), Grouped(g.losses)});
	trend(kMetricLosses, false, cells.back());
	cells.push_back({Tr(narrow ? "K/L ratio" : "Kills per loss"), OneDecimal(MatchMetricValue(f, kMetricKillsPerLoss))});
	trend(kMetricKillsPerLoss, true, cells.back());
	cells.push_back({Tr("Gold"), Grouped(g.gold)});
	trend(kMetricGold, true, cells.back());
	cells.push_back({Tr("Food"), Grouped(g.food)});
	cells.push_back({Tr("Largest army"), Grouped(g.armyMax)});
	if (g.damageDealt) cells.push_back({Tr("Damage dealt"), Grouped(g.damageDealt)});
	if (g.damageTaken) cells.push_back({Tr("Damage taken"), Grouped(g.damageTaken)});
	if (!g.bestUnit.empty()) cells.push_back({Tr("Best unit"), TrFmt("%s \xC2\xB7 level %s", {g.bestUnit, std::to_string(g.bestLevel)})});
	return cells;
}

int DetailHeight(size_t cells, int columns) { return static_cast<int>((cells + columns - 1) / columns) * kDetailCellH + 10; }

void PaintDetailCells(Painter &p, const std::vector<DetailCell> &cells, int x, int y, int w, int columns)
{
	const int colW = w / columns;
	for (size_t i = 0; i < cells.size(); i++) {
		const int cx = x + static_cast<int>(i % columns) * colW, cy = y + static_cast<int>(i / columns) * kDetailCellH;
		const DetailCell &cell = cells[i];
		// The last cell has the rest of its row.
		const int cw = i + 1 == cells.size() ? x + w - cx : colW;
		p.Font(3);
		Raw(p, cx, cy, cw - 8, 15, cell.label, pal::textDim);
		p.Font(4);
		const int vw = std::min(cw - (cell.trend ? 24 : 8), RawWidth(p, cell.value));
		Raw(p, cx, cy + 15, vw, 17, cell.value, pal::text);
		if (cell.trend) {
			const COLORREF c = cell.good ? pal::ok : pal::bad;
			if (cell.trend > 0) p.ArrowUp(cx + vw + 5, cy + 20, 9, 7, c);
			else p.ArrowDown(cx + vw + 5, cy + 20, 9, 7, c);
		}
	}
}

int ChipWidth(Painter &p)
{
	p.Font(3);
	int w = 0;
	for (MatchResult r : {kMatchWon, kMatchLost, kMatchAbandoned}) w = std::max(w, p.TextWidth(Tr(ResultText(r))));
	return w + 14;
}

void PaintChip(Painter &p, int x, int y, int w, int h, MatchResult r)
{
	const COLORREF bg = r == kMatchWon ? RGB(46, 70, 34) : r == kMatchLost ? pal::redPressed : pal::button;
	const COLORREF fg = r == kMatchWon ? RGB(170, 220, 140) : r == kMatchLost ? pal::onRed : pal::textDim;
	PaintBadge(p, x, y, w, h, ResultText(r), bg, fg);
}

void PaintHistory(Painter &p, const View &v, const Card &c, const Box &b, Tip &tip)
{
	PaintCard(p, b.x, b.y, b.w, b.h);
	p.AddHit(b.x, b.y, b.w, b.h, kActHistory, 0);
	PaintCardTitle(p, b.x, b.y, b.w, "Match history");
	const HistoryGeom gm = GeomOf(b);
	const int x0 = gm.x0, right = gm.x0 + gm.inner;
	if (c.games.empty()) {
		const int ty = b.y + b.h / 2 - 30;
		p.Font(0);
		p.Text(x0, ty, gm.inner, 22, c.readable ? "No games recorded yet." : kDash, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x0 + 20, ty + 30, gm.inner - 40, 60, "Finish a skirmish or a multiplayer game and it will show here.", pal::textDim, DT_CENTER | DT_TOP | DT_WORDBREAK);
		return;
	}
	const std::vector<std::string> outcomes = Translated({"All games", "Victories", "Defeats"});
	// Beside the title when there is room, under it otherwise.
	const int segW = gm.wide ? SegmentsWidth(p, outcomes) : gm.inner;
	PaintSegments(p, gm.wide ? right - segW : x0, gm.wide ? b.y + 6 : b.y + kCardHead, segW, outcomes, v.outcome, kActOutcome, v.mouseX, v.mouseY);
	const std::vector<size_t> rows = HistoryGames(v);
	p.Font(4);
	const int titleW = p.TextWidth(Tr("Match history"));
	p.Font(3);
	Raw(p, x0 + titleW + 10, b.y + 7, std::max(0, (gm.wide ? right - segW : right) - x0 - titleW - 20), 18, Grouped(rows.size()), pal::textDim);
	const std::vector<MatchFacts> facts = FactsOfCard(c);
	const int maxScroll = HistoryMaxScroll(v);
	const int scroll = std::max(0, std::min(v.historyScroll, maxScroll));
	const int listRight = maxScroll > 0 ? right - 12 : right;
	const int chipW = ChipWidth(p);
	// Columns of the wide history, from the right.
	const int klW = 84, timeW = 56, dateW = 96, modeW = 80;
	const int klX = listRight - klW, timeX = klX - timeW - 8, dateX = timeX - dateW - 8, modeX = dateX - modeW - 8, civX = x0 + chipW + 10;
	if (gm.wide) {
		p.Font(3);
		const int hy = b.y + kCardHead;
		p.Text(x0, hy, chipW, 16, "Result", kHead);
		p.Text(civX, hy, modeX - civX - 8, 16, "Civilization", kHead);
		p.Text(modeX, hy, modeW, 16, "Mode", kHead);
		p.Text(dateX, hy, dateW, 16, "Date", kHead);
		p.Text(timeX, hy, timeW, 16, "Duration", kHead, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Text(klX, hy, klW, 16, "Kills / losses", kHead, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	if (rows.empty()) {
		p.Font(3);
		p.Text(x0, gm.listTop + 20, gm.inner, 22, "No games in this filter.", kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const int mx = MouseX(p, v), my = MouseY(p, v);
	p.Clip(x0, gm.listTop, gm.inner, gm.listBottom - gm.listTop);
	int y = gm.listTop;
	for (size_t r = scroll; r < rows.size() && y < gm.listBottom; r++) {
		const int game = static_cast<int>(rows[r]);
		const GameRow &g = c.games[game];
		const MatchResult result = MatchResultOf(facts[game]);
		const bool open = game == v.expanded;
		const int rh = gm.rowH, w = listRight - x0;
		const bool hot = my >= gm.listTop && my < gm.listBottom && p.Hover(x0, y, w, rh, v.mouseX, v.mouseY);
		if (open) {
			p.Fill(x0, y, w, rh, kSelectedTop);
			p.Fill(x0, y, 3, rh, pal::red);
		}
		else if (hot) {
			p.Fill(x0, y, w, rh, kRowHot);
		}
		else {
			p.Fill(x0, y, w, 1, kRule);
		}
		const bool known = !g.civ.empty();
		const std::string kl = Grouped(g.kills), losses = " / " + Grouped(g.losses);
		p.Font(3);
		const int lossesW = RawWidth(p, losses);
		if (gm.wide) {
			PaintChip(p, x0 + 6, y + 5, chipW - 6, rh - 10, result);
			p.Font(known ? 4 : 3);
			Raw(p, civX, y, modeX - civX - 8, rh, CivText(g), known ? pal::text : kMuted);
			p.Font(3);
			Raw(p, modeX, y, modeW, rh, ModeText(g), pal::textDim);
			Raw(p, dateX, y, dateW, rh, AgeText(v, g), pal::textDim);
			if (mx >= dateX && mx < dateX + dateW && my >= y && my < y + rh && my < gm.listBottom) {
				tip.lines = {{DateText(g), pal::text}};
				tip.x = dateX;
				tip.y = y + rh + 2;
			}
			Raw(p, timeX, y, timeW, rh, Duration(g.durationMs), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			Raw(p, listRight - lossesW, y, lossesW, rh, losses, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(4);
			Raw(p, klX, y, klW - lossesW, rh, kl, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
		else {
			PaintChip(p, x0 + 6, y + 5, chipW - 6, 16, result);
			Raw(p, listRight - lossesW, y + 3, lossesW, 18, losses, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(4);
			const int killsW = RawWidth(p, kl);
			Raw(p, listRight - lossesW - killsW, y + 3, killsW, 18, kl, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(known ? 4 : 3);
			Raw(p, civX, y + 3, listRight - lossesW - killsW - civX - 8, 18, CivText(g), known ? pal::text : kMuted);
			p.Font(3);
			const std::string age = AgeText(v, g);
			const std::string line = ModeText(g) + "  \xC2\xB7  " + age + "  \xC2\xB7  " + Duration(g.durationMs);
			Raw(p, civX, y + 21, listRight - civX, 16, line, pal::textDim);
			if (mx >= civX && mx < listRight && my >= y + 20 && my < y + rh && my < gm.listBottom) {
				tip.lines = {{DateText(g), pal::text}};
				tip.x = civX;
				tip.y = y + rh + 2;
			}
		}
		if (y + rh > gm.listTop) p.AddHit(x0, std::max(y, gm.listTop), w, std::min(y + rh, gm.listBottom) - std::max(y, gm.listTop), kActGame, game);
		y += rh;
		if (open) {
			const std::vector<DetailCell> cells = DetailCells(g, facts, !gm.wide);
			const int dh = DetailHeight(cells.size(), gm.columns);
			p.Fill(x0, y, w, dh, pal::raised);
			p.Fill(x0, y, 3, dh, pal::red);
			PaintDetailCells(p, cells, x0 + 16, y + 6, w - 24, gm.columns);
			y += dh;
		}
	}
	p.Unclip();
	if (maxScroll > 0) {
		const int trackH = gm.listBottom - gm.listTop;
		ScrollbarLook look;
		look.minThumb = 24;
		PaintScrollbar(p, kBarHistory, right - 6, gm.listTop, trackH, {scroll, maxScroll, std::max(1, trackH / gm.rowH)}, v.bar, v.mouseX, v.mouseY,
			look);
	}
}

void PaintDetail(Painter &p, const View &v, const Card &c, Tip &tip)
{
	const Area a = DetailArea(v);
	PaintIdentity(p, v, c, a);
	PaintTiles(p, c, a);
	const Lower l = LowerLayout(a);
	PaintSummary(p, c, l.summary.x, l.summary.y, l.summary.w, l.summary.h, l.summaryColumns);
	PaintEvolution(p, v, c, l.chart, tip);
	PaintHistory(p, v, c, l.history, tip);
}

void PaintFooter(Painter &p, const View &v)
{
	p.Fill(18, kFooterTop, kScreenW - 36, 1, pal::lineStrong);
	const int count = static_cast<int>(v.cards.size());
	const bool other = v.selected >= 0 && v.selected < count && !v.cards[v.selected].active;
	int bx = kScreenW - kMargin;
	if (other) {
		bx -= 200;
		p.Button(bx, kFooterButtonsY, 200, kFooterButtonH, "Use this player", kActUse, 0, v.mouseX, v.mouseY, true, true);
		bx -= 10 + 120;
		p.Button(bx, kFooterButtonsY, 120, kFooterButtonH, "Back", kActBack, 0, v.mouseX, v.mouseY);
	}
	else {
		bx -= 150;
		p.Button(bx, kFooterButtonsY, 150, kFooterButtonH, "Back", kActBack, 0, v.mouseX, v.mouseY, true, true);
	}
	std::string hint;
	if (other) {
		for (const Card &c : v.cards) {
			if (c.active) hint = TrFmt("You are playing as %s.", {c.name});
		}
	}
	if (hint.empty()) return;
	p.Font(3);
	p.Diamond(kMargin + 4, kFooterButtonsY + kFooterButtonH / 2, 4, pal::redText);
	Raw(p, kMargin + 16, kFooterButtonsY, bx - kMargin - 28, kFooterButtonH, hint, pal::caption);
}

}

MatchFacts FactsOf(const GameRow &g)
{
	MatchFacts m;
	m.year = g.year;
	m.month = g.month;
	m.day = g.day;
	m.durationMs = g.durationMs;
	m.multi = g.multi;
	m.lost = !g.won;
	m.kills = g.kills;
	m.losses = g.losses;
	m.armyMax = g.armyMax;
	m.gold = g.gold;
	return m;
}

const Card *SelectedCard(const View &v)
{
	return v.selected >= 0 && v.selected < static_cast<int>(v.cards.size()) ? &v.cards[v.selected] : nullptr;
}

std::vector<size_t> HistoryGames(const View &v)
{
	const Card *c = SelectedCard(v);
	if (!c) return {};
	return MatchSelect(FactsOfCard(*c), kModeAll, static_cast<MatchOutcome>(v.outcome), 0);
}

int HistoryMaxScroll(const View &v)
{
	const Card *c = SelectedCard(v);
	if (!c) return 0;
	const HistoryGeom gm = GeomOf(LowerLayout(DetailArea(v)).history);
	const std::vector<size_t> rows = HistoryGames(v);
	int extra = 0;
	if (v.expanded >= 0 && std::find(rows.begin(), rows.end(), static_cast<size_t>(v.expanded)) != rows.end()) {
		const int dh = DetailHeight(DetailCells(c->games[v.expanded], FactsOfCard(*c), !gm.wide).size(), gm.columns);
		extra = (dh + gm.rowH - 1) / gm.rowH;
	}
	const int page = (gm.listBottom - gm.listTop) / gm.rowH;
	return std::max(0, static_cast<int>(rows.size()) + extra - page);
}

void HistoryReveal(View &v, int game)
{
	const Card *c = SelectedCard(v);
	if (!c || game < 0 || game >= static_cast<int>(c->games.size())) return;
	v.expanded = game;
	std::vector<size_t> rows = HistoryGames(v);
	auto it = std::find(rows.begin(), rows.end(), static_cast<size_t>(game));
	if (it == rows.end()) {
		v.outcome = kOutcomeAll;
		rows = HistoryGames(v);
		it = std::find(rows.begin(), rows.end(), static_cast<size_t>(game));
	}
	const int at = it == rows.end() ? 0 : static_cast<int>(it - rows.begin());
	// The game before it stays in sight when the detail leaves room for it.
	const HistoryGeom gm = GeomOf(LowerLayout(DetailArea(v)).history);
	const int needed = gm.rowH * 2 + DetailHeight(DetailCells(c->games[game], FactsOfCard(*c), !gm.wide).size(), gm.columns);
	const int first = needed <= gm.listBottom - gm.listTop ? at - 1 : at;
	v.historyScroll = std::max(0, std::min(first, HistoryMaxScroll(v)));
}

void PaintProfileScreen(Painter &p, const View &v)
{
	PaintWindowFrame(p, kScreenW, kScreenH);
	PaintWindowHeader(p, kScreenW, "STATISTICS");
	const bool list = ShowsList(v);
	if (list) {
		PaintList(p, v);
		p.Fill(kDetailX - 12, kTop, 1, kLowerBottom - kTop, pal::hairline);
	}
	Tip tip;
	if (const Card *c = SelectedCard(v)) {
		PaintDetail(p, v, *c, tip);
	}
	else if (list) {
		const Area a = DetailArea(v);
		p.Font(0);
		p.Text(a.x, kTop + 120, a.w, 24, "Choose a player.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	PaintFooter(p, v);
	PaintTip(p, tip);
	PaintPatronTip(p, kScreenW, kScreenH);
}

}
