// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/profileui_view.h"

#include <algorithm>
#include <cstdio>
#include <string>
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
constexpr int kLowerY = kTilesY + kTileH + 12, kLowerBottom = 612;
constexpr int kCardPad = 14, kCardHead = 30;
// Beside the list: the summary in one column and five games over the chart.
constexpr int kSummaryW = 262, kNarrowRecentRows = 5;
// At full width: the summary in two columns over the chart, and more games beside them.
constexpr int kWideLeftW = 430, kWideSummaryH = 152, kWideRecentRows = 9;
constexpr int kFooterTop = 622, kFooterButtonsY = 632, kFooterButtonH = 36;

const COLORREF kWonColor = RGB(126, 170, 92), kLostColor = RGB(184, 86, 66), kMuted = RGB(140, 126, 98);
const COLORREF kSelectedTop = RGB(96, 34, 24), kRowBody = RGB(25, 19, 13), kRowHot = RGB(50, 38, 22);
const char kDash[] = "\xE2\x80\x94";

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
	if (rank < 0 || count <= 0) return kGold;
	const float t = count > 1 ? std::min(1.0f, static_cast<float>(rank) / (count - 1)) * last : 0.0f;
	const int i = std::min(last - 1, static_cast<int>(t));
	return Mix(stops[i], stops[i + 1], t - i);
}

void PaintRankBar(Painter &p, int x, int y, int w, int h, float fraction, COLORREF c)
{
	p.Fill(x, y, w, h, pal::well);
	const int filled = static_cast<int>(w * std::max(0.0f, std::min(1.0f, fraction)));
	if (filled > 0) p.FillV(x, y, filled, h, Mix(c, RGB(255, 255, 255), 0.2f), Mix(c, RGB(0, 0, 0), 0.4f));
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
	if (minutes < 60) sprintf_s(buf, "%u min", minutes);
	else sprintf_s(buf, "%u h %02u", minutes / 60, minutes % 60);
	return buf;
}

std::string Date(const RecentRow &r)
{
	char buf[32];
	sprintf_s(buf, "%02d/%02d/%04d", r.day, r.month, r.year);
	return buf;
}

std::string GamesAndWins(int games, int wins)
{
	if (!games) return kDash;
	return TrFmt("%s \xC2\xB7 %s won", {Grouped(games), Percent(PercentOf(wins, games))});
}

void PaintMonogram(Painter &p, int x, int y, int size, const std::string &name, bool strong)
{
	p.FillV(x, y, size, size, strong ? RGB(70, 50, 24) : RGB(44, 33, 20), strong ? RGB(40, 28, 14) : RGB(28, 21, 13));
	p.Frame(x, y, size, size, strong ? kGold : pal::bronze);
	p.Font(size >= 60 ? 5 : 1);
	Raw(p, x, y, size, size, Initials(name), strong ? pal::goldLight : kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintListRow(Painter &p, const Card &c, int rankCount, int index, bool selected, int x, int y, int w, int mx, int my)
{
	const bool hot = !selected && p.Hover(x, y, w, kRowH, mx, my);
	if (selected) {
		p.FillH(x, y, w, kRowH, kSelectedTop, pal::panel);
		p.Frame(x, y, w, kRowH, pal::bronze);
		p.FillV(x, y, 4, kRowH, pal::goldLight, pal::bronze);
	}
	else {
		p.Fill(x, y, w, kRowH, hot ? kRowHot : kRowBody);
		p.Frame(x, y, w, kRowH, pal::lineDim);
	}
	PaintMonogram(p, x + 12, y + 9, 40, c.name, selected);
	int nameW = w - 74;
	if (c.active) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 14;
		PaintBadge(p, x + w - bw - 10, y + 11, bw, 17, "In use", RGB(70, 46, 18), RGB(236, 186, 110));
		nameW -= bw + 8;
	}
	p.Font(1);
	Raw(p, x + 62, y + 9, nameW, 22, c.name, selected ? pal::goldLight : kText);
	p.Font(3);
	const std::string games = GamesText(c.summary.games);
	const COLORREF dim = selected ? RGB(200, 180, 136) : kDim;
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
	p.Text(kListX, kTop, kListW, 20, "Players", kGold);
	p.Font(3);
	Raw(p, kListX, kTop, kListW, 20, std::to_string(v.cards.size()), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const int count = static_cast<int>(v.cards.size());
	const int maxScroll = std::max(0, count - kListRows);
	const int scroll = std::max(0, std::min(v.scroll, maxScroll));
	const int rowW = maxScroll > 0 ? kListW - 12 : kListW;
	for (int r = 0; r < kListRows && scroll + r < count; r++) {
		const int i = scroll + r;
		PaintListRow(p, v.cards[i], v.rankCount, i, i == v.selected, kListX, kListTop + r * (kRowH + kRowGap), rowW, v.mouseX, v.mouseY);
	}
	if (maxScroll > 0) {
		const int trackY = kListTop, trackH = kListRows * (kRowH + kRowGap) - kRowGap;
		const int thumbH = std::max(24, trackH * kListRows / count);
		p.Fill(kListX + kListW - 6, trackY, 6, trackH, pal::track);
		p.Fill(kListX + kListW - 6, trackY + (trackH - thumbH) * scroll / maxScroll, 6, thumbH, pal::thumb);
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
		p.Frame(ax, ay, kAvatar, kAvatar, kGold);
	}
	else {
		PaintMonogram(p, ax, ay, kAvatar, c.name, true);
	}
	const int tx = ax + kAvatar + 18;
	int right = x + w - 14;
	if (c.active && ShowsList(v)) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 16;
		PaintBadge(p, right - bw, y + 14, bw, 18, "In use", RGB(70, 46, 18), RGB(236, 186, 110));
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
		Raw(p, tx, y + 12, right - tx, 34, c.name, pal::goldLight);
	}
	if (!c.readable) {
		p.Font(3);
		p.Text(tx, y + 50, x + w - 14 - tx, 20, "Couldn't read this player's statistics.", kBad);
		return;
	}
	const ProfileSummary &s = c.summary;
	if (c.rankName.empty()) {
		p.Font(1);
		Raw(p, tx, y + 48, x + w - 14 - tx, 22, kDash, kGold);
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
	Raw(p, aheadX, y + 48, aheadW + 2, 22, ahead, kDim);
	p.Font(1);
	Raw(p, tx, y + 48, std::max(0, aheadX - 16 - tx), 22, c.rankName, RankColor(s.rank, v.rankCount));
	PaintRankBar(p, tx, y + 74, barW, 7, fraction, RankColor(s.rank, v.rankCount));
	p.Frame(tx - 1, y + 73, barW + 2, 9, pal::lineDim);
	p.Font(3);
	Raw(p, tx, y + 84, x + w - 14 - tx, 18, label, RGB(196, 178, 136));
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
		Raw(p, x, kTilesY + 6, w, 42, values[i], pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x + 6, kTilesY + 50, w - 12, 18, labels[i], kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintCardTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	p.Font(4);
	p.Text(x + kCardPad, y + 7, w - kCardPad * 2, 18, title, kGold);
}

// One column fills the height beside the list; two columns leave room under them for the chart.
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
		if (i >= columns) p.Fill(cx, ry, colW, 1, RGB(48, 37, 23));
		p.Font(3);
		p.Text(cx, ry + 3, colW, 17, rows[i].first, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(4);
		Raw(p, cx, ry + 19, colW, 18, rows[i].second, kText);
	}
}

void PaintLegend(Painter &p, int right, int y)
{
	p.Font(3);
	int lx = right;
	for (const auto &item : {std::make_pair("Defeat", kLostColor), std::make_pair("Victory", kWonColor)}) {
		const int tw = p.TextWidth(item.first);
		lx -= tw;
		p.Text(lx, y, tw + 2, 16, item.first, kDim);
		lx -= 14;
		p.Fill(lx, y + 6, 9, 9, item.second);
		lx -= 12;
	}
}

void PaintBars(Painter &p, const Card &c, int x, int top, int w, int bottom)
{
	const int n = std::min(static_cast<int>(c.recent.size()), kChartGames);
	p.Fill(x, bottom, w, 1, pal::line);
	if (!n) return;
	uint32_t most = 1;
	for (int i = 0; i < n; i++) most = std::max(most, c.recent[i].kills);
	const int slot = w / kChartGames, barW = std::max(4, slot - 6);
	for (int k = 0; k < n; k++) {
		const RecentRow &r = c.recent[n - 1 - k];
		const int bh = std::max(2, static_cast<int>((bottom - top - 12) * static_cast<uint64_t>(r.kills) / most));
		const int bx = x + k * slot + (slot - barW) / 2;
		p.FillV(bx, bottom - bh, barW, bh, r.won ? kWonColor : kLostColor, r.won ? RGB(70, 104, 50) : RGB(110, 48, 36));
	}
	p.Font(3);
	Raw(p, x, top - 4, 80, 14, Grouped(most), RGB(120, 108, 84));
}

// Under the recent games, beside the list.
void PaintChart(Painter &p, const Card &c, int x, int y, int w, int h)
{
	p.Font(3);
	p.Text(x, y, w, 16, "Kills per game", kDim);
	PaintLegend(p, x + w, y);
	PaintBars(p, c, x, y + 22, w, y + h);
}

// A card of its own when the detail has the whole width.
void PaintChartCard(Painter &p, const Card &c, int x, int y, int w, int h)
{
	PaintCard(p, x, y, w, h);
	PaintCardTitle(p, x, y, w, "Kills per game");
	if (c.recent.empty()) {
		p.Font(3);
		p.Text(x, y + kCardHead, w, h - kCardHead - 8, c.readable ? "No games yet" : kDash, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	PaintLegend(p, x + w - kCardPad, y + 8);
	PaintBars(p, c, x + kCardPad, y + kCardHead + 12, w - kCardPad * 2, y + h - 14);
}

// With the chart under it beside the list; alone, with more games, at full width.
void PaintRecent(Painter &p, const Card &c, int x, int y, int w, int h, int rows, bool chart)
{
	PaintCard(p, x, y, w, h);
	PaintCardTitle(p, x, y, w, "Recent games");
	const int x0 = x + kCardPad, inner = w - kCardPad * 2;
	if (c.recent.empty()) {
		const int ty = chart ? y + 70 : y + h / 2 - 30;
		p.Font(0);
		p.Text(x0, ty, inner, 22, c.readable ? "No games recorded yet." : kDash, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x0 + 20, ty + 30, inner - 40, 60, "Finish a skirmish or a multiplayer game and it will show here.", kDim, DT_CENTER | DT_TOP | DT_WORDBREAK);
		return;
	}
	// With room enough the mode gets its own column; otherwise it follows the civilization.
	const bool modeColumn = inner >= 520;
	const int dateW = 68, badgeX = x0 + dateW + 4, civX = badgeX + 30, killsW = modeColumn ? 52 : 40, timeW = modeColumn ? 64 : 52;
	const int killsX = x0 + inner - killsW, timeX = killsX - timeW - 6, modeW = modeColumn ? 120 : 0, modeX = timeX - 10 - modeW;
	const int civW = (modeColumn ? modeX - 10 : timeX - 6) - civX;
	int ry = y + kCardHead;
	p.Font(3);
	const COLORREF head = RGB(130, 116, 90);
	p.Text(x0, ry, dateW, 16, "Date", head);
	p.Text(civX, ry, civW, 16, "Civilization", head);
	if (modeColumn) p.Text(modeX, ry, modeW, 16, "Mode", head);
	p.Text(timeX, ry, timeW, 16, "Duration", head, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.Text(killsX, ry, killsW, 16, "Kills", head, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	ry += 20;
	const int rowH = 28;
	for (int i = 0; i < rows && i < static_cast<int>(c.recent.size()); i++, ry += rowH) {
		const RecentRow &r = c.recent[i];
		p.Fill(x0, ry, inner, 1, RGB(48, 37, 23));
		p.Font(3);
		Raw(p, x0, ry, dateW, rowH, Date(r), kDim);
		p.Fill(badgeX, ry + 6, 22, rowH - 12, r.won ? RGB(46, 70, 34) : RGB(84, 34, 26));
		p.Font(4);
		p.Text(badgeX, ry + 6, 22, rowH - 12, r.won ? "W" : "L", r.won ? RGB(170, 220, 140) : RGB(240, 150, 128), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		// A civilization of a pack that is not installed has no name.
		const bool known = !r.civ.empty();
		const std::string civ = known ? r.civ : Tr("Unknown");
		const std::string mode = Tr(r.multi ? "Multiplayer" : "Skirmish");
		if (modeColumn) {
			p.Font(known ? 4 : 3);
			Raw(p, civX, ry, civW, rowH, civ, known ? kText : kMuted);
			p.Font(3);
			Raw(p, modeX, ry, modeW, rowH, mode, kDim);
		}
		else {
			// The mode always shows; a long civilization gives way to it.
			const std::string tail = " \xC2\xB7 " + mode;
			p.Font(3);
			const int tailW = std::min(civW / 2, RawWidth(p, tail));
			p.Font(known ? 4 : 3);
			const int civTextW = std::min(civW - tailW, RawWidth(p, civ));
			Raw(p, civX, ry, civTextW, rowH, civ, known ? kText : kMuted);
			p.Font(3);
			Raw(p, civX + civTextW, ry, civW - civTextW, rowH, tail, kDim);
		}
		p.Font(3);
		Raw(p, timeX, ry, timeW, rowH, Duration(r.durationMs), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Font(4);
		Raw(p, killsX, ry, killsW, rowH, Grouped(r.kills), kText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	if (!chart) return;
	const int chartY = std::max(ry + 10, y + h - 108);
	PaintChart(p, c, x0, chartY, inner, y + h - 14 - chartY);
}

void PaintDetail(Painter &p, const View &v, const Card &c)
{
	const Area a = DetailArea(v);
	PaintIdentity(p, v, c, a);
	PaintTiles(p, c, a);
	const int h = kLowerBottom - kLowerY;
	if (!a.wide) {
		PaintSummary(p, c, a.x, kLowerY, kSummaryW, h, 1);
		PaintRecent(p, c, a.x + kSummaryW + 12, kLowerY, a.w - kSummaryW - 12, h, kNarrowRecentRows, true);
		return;
	}
	PaintSummary(p, c, a.x, kLowerY, kWideLeftW, kWideSummaryH, 2);
	PaintChartCard(p, c, a.x, kLowerY + kWideSummaryH + 12, kWideLeftW, h - kWideSummaryH - 12);
	PaintRecent(p, c, a.x + kWideLeftW + 12, kLowerY, a.w - kWideLeftW - 12, h, kWideRecentRows, false);
}

void PaintFooter(Painter &p, const View &v)
{
	p.Fill(18, kFooterTop, kScreenW - 36, 1, pal::line);
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
	p.Diamond(kMargin + 4, kFooterButtonsY + kFooterButtonH / 2, 4, kGold);
	Raw(p, kMargin + 16, kFooterButtonsY, bx - kMargin - 28, kFooterButtonH, hint, RGB(186, 170, 134));
}

}

void PaintProfileScreen(Painter &p, const View &v)
{
	PaintWindowFrame(p, kScreenW, kScreenH);
	PaintWindowHeader(p, kScreenW, "STATISTICS");
	const bool list = ShowsList(v);
	if (list) {
		PaintList(p, v);
		p.Fill(kDetailX - 12, kTop, 1, kLowerBottom - kTop, pal::lineDim);
	}
	if (v.selected >= 0 && v.selected < static_cast<int>(v.cards.size())) {
		PaintDetail(p, v, v.cards[v.selected]);
	}
	else if (list) {
		const Area a = DetailArea(v);
		p.Font(0);
		p.Text(a.x, kTop + 120, a.w, 24, "Choose a player.", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	PaintFooter(p, v);
	PaintPatronTip(p, kScreenW, kScreenH);
}

}
