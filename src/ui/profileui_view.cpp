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
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "net/leagueclient.h"
#include "ui/fullscreen.h"
#include "ui/leaguerewards.h"
#include "ui/leagueui.h"
#include "ui/patronui.h"
#include "ui/widgets.h"

using namespace ui;

namespace profileui {
namespace {

constexpr int kListW = 340, kListHead = 56, kListHint = 48, kRowH = 64, kRowGap = 8;
constexpr int kIdentityH = 128, kAvatar = 96, kTileGap = 12;
constexpr int kCardPad = 20, kCardHead = 40, kGap = 16;
// At full width: the summary in two columns over the evolution, and the history beside them.
constexpr int kWideLeftW = 520, kWideSummaryH = 190;
// Beside the list: the summary in three columns over the evolution and the history.
constexpr int kStripH = 140, kNarrowChartW = 470;
constexpr int kSegH = 28, kSegPad = 10, kDetailCellH = 44;

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

// Where the parts of the screen go, in the units of ScreenView: the content runs from under the header to the bottom margin.
struct Frame {
	ScreenLayout head;
	int top = 0, bottom = 0, rows = 1, tileH = 88, tilesY = 0, lowerY = 0;
};

Frame FrameOf(const View &v)
{
	Frame f;
	f.head = ScreenLayoutFor(v.screenW, v.screenH);
	f.top = f.head.top;
	f.bottom = v.screenH - f.head.margin;
	f.rows = std::max(1, (f.bottom - kListHint - f.top - kListHead + kRowGap) / (kRowH + kRowGap));
	f.tileH = f.head.roomy ? 104 : 88;
	f.tilesY = f.top + kIdentityH + kGap;
	f.lowerY = f.tilesY + f.tileH + kGap;
	return f;
}

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
	const int m = FrameOf(v).head.margin;
	if (ShowsList(v)) return {m + kListW + 24, v.screenW - 2 * m - kListW - 24, false};
	return {m, v.screenW - 2 * m, true};
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
	PaintMonogram(p, x + 12, y + 10, 44, c.name, selected);
	const int tx = x + 68;
	int nameW = w - 80;
	if (c.active) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 14;
		PaintBadge(p, x + w - bw - 10, y + 11, bw, 21, "In use", pal::hairline, pal::redText);
		nameW -= bw + 8;
	}
	p.Font(1);
	Raw(p, tx, y + 9, nameW, 25, c.name, selected ? pal::text : pal::text);
	p.Font(3);
	const std::string games = GamesText(c.summary.games);
	const COLORREF dim = selected ? pal::textDim : pal::textDim;
	if (c.rankName.empty() || !c.readable) {
		Raw(p, tx, y + 35, w - 80, 21, games, dim);
	}
	else {
		const int rankW = std::min(w - 80, RawWidth(p, c.rankName));
		Raw(p, tx, y + 35, rankW, 21, c.rankName, RankColor(c.summary.rank, rankCount));
		Raw(p, tx + rankW, y + 35, w - 80 - rankW, 21, "  \xC2\xB7  " + games, dim);
	}
	p.AddHit(x, y, w, kRowH, kActPick, index);
}

void PaintList(Painter &p, const View &v, const Frame &f)
{
	const int cardX = f.head.margin, x = cardX + 14, w = kListW - 28, rowsTop = f.top + kListHead, rows = f.rows;
	PaintGlassCard(p, cardX, f.top, kListW, f.bottom - f.top);
	p.Font(4);
	p.Text(x, f.top + 18, w, 22, "Players", pal::redText);
	p.Font(3);
	Raw(p, x, f.top + 18, w, 22, std::to_string(v.cards.size()), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const int count = static_cast<int>(v.cards.size());
	const int maxScroll = std::max(0, count - rows);
	const int scroll = std::max(0, std::min(v.scroll, maxScroll));
	const int rowW = maxScroll > 0 ? w - 12 : w;
	for (int r = 0; r < rows && scroll + r < count; r++) {
		const int i = scroll + r;
		PaintListRow(p, v.cards[i], v.rankCount, i, i == v.selected, x, rowsTop + r * (kRowH + kRowGap), rowW, v.mouseX, v.mouseY);
	}
	if (maxScroll > 0) {
		ScrollbarLook look;
		look.minThumb = 24;
		PaintScrollbar(p, kBarList, x + w - 6, rowsTop, rows * (kRowH + kRowGap) - kRowGap, {scroll, maxScroll, rows}, v.bar, v.mouseX, v.mouseY, look);
	}
	p.Font(3);
	p.TextWrapped(x + 2, f.bottom - kListHint + 4, w - 4, kListHint - 12, "Double-click a player to use it.", kMuted);
}

// "Frame and title" with its arrow, right-aligned at `right`: it opens the league account, where both are chosen.
void PaintFrameLink(Painter &p, const View &v, int right, int y, int maxW)
{
	p.Font(4);
	const int lw = std::min(maxW, p.CapsWidth("Frame and title", 1) + 22), lx = right - lw;
	const bool hot = p.Hover(lx, y - 6, lw, 32, v.mouseX, v.mouseY);
	p.TextCaps(lx, y, lw - 18, 22, "Frame and title", hot ? pal::text : pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.ArrowRight(lx + lw - 10, y + 5, 7, 12, hot ? pal::text : pal::redText);
	p.AddHit(lx, y - 6, lw, 32, kActLeague, 0);
}

// The league's showcase at the right of the identity card: the vexilla and phalerae won, one click to wear one next to the name.
void PaintShowcase(Painter &p, const View &v, int x, int y, int w)
{
	p.Fill(x - 20, y + 16, 1, kIdentityH - 32, pal::hairline);
	PaintSectionLabel(p, x, y + 14, w / 2, 22, "Season showcase");
	PaintFrameLink(p, v, x + w, y + 14, w / 2);
	if (!v.meLoaded) {
		p.Font(3);
		p.Text(x, y + 48, w, 22, "Reading your season rewards...", kMuted);
		return;
	}
	const int shown = PaintLeagueShowcase(p, x, y + 44, w, 56, v.me, kActLeagueBadge, v.mouseX, v.mouseY);
	p.Font(3);
	if (!shown) {
		const char *text = "What you win in each season stays here. Play 10 ranked games in a season to win its vexillum.";
		p.TextWrapped(x, y + 46, w, 58, text, kMuted);
		return;
	}
	p.Text(x, y + 104, w, 18, "Click one to wear it next to your name.", kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintIdentity(Painter &p, const View &v, const Card &c, const Area &a, const Frame &f)
{
	const int x = a.x, y = f.top, w = a.w;
	PaintGlassCard(p, x, y, w, kIdentityH);
	const int ax = x + 16, ay = y + 14;
	const bool steam = c.active && v.avatar && !v.avatar->bgra.empty();
	// The season's frame and title go with the league account, which is the Steam user's: only on the player in use.
	const bool league = c.active && v.linked && v.meLoaded;
	const LeagueFrame frame = league ? LeagueOwnCosmetic(v.me, v.me.equippedFrame) : LeagueFrame();
	const LeagueFrame title = league ? LeagueOwnCosmetic(v.me, v.me.equippedTitle) : LeagueFrame();
	if (steam) {
		const int in = frame.Shown() ? kLeagueFrameInset : 0;
		p.Image(ax + in, ay + in, kAvatar - 2 * in, kAvatar - 2 * in, v.avatar->bgra.data(), v.avatar->width, v.avatar->height);
		if (frame.Shown()) PaintLeagueFrame(p, ax, ay, kAvatar, frame);
		else p.Frame(ax, ay, kAvatar, kAvatar, pal::redText);
	}
	else {
		PaintMonogram(p, ax, ay, kAvatar, c.name, true);
		if (frame.Shown()) PaintLeagueFrame(p, ax, ay, kAvatar, frame);
	}
	const int tx = ax + kAvatar + 22;
	int right = x + w - 18;
	// The showcase takes the right of the card where the screen is wide enough for it.
	const int showcaseW = a.wide && c.active && v.linked ? std::min(600, w * 36 / 100) : 0;
	if (showcaseW >= 300) {
		PaintShowcase(p, v, x + w - 18 - showcaseW, y, showcaseW);
		right -= showcaseW + 40;
	}
	// With a title the name moves up and the title goes under it; the military rank follows a little lower.
	const bool titled = !LeagueTitleLabel(title).empty();
	const int nameY = titled ? y + 4 : y + 12, nameH = titled ? 40 : 42;
	const int rankY = titled ? y + 64 : y + 58, barY = titled ? y + 92 : y + 88, labelY = titled ? y + 103 : y + 100;
	if (c.active && ShowsList(v)) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 16;
		PaintBadge(p, right - bw, y + 18, bw, 22, "In use", pal::hairline, pal::redText);
		right -= bw + 12;
	}
	p.Font(2);
	const PatronTier tier = c.active ? v.patron.tier : PatronTier::None;
	const LeagueBadge badge = c.active ? v.badge : LeagueBadge();
	const int laurelW = tier != PatronTier::None ? PatronLaurelSize(tier) + 8 : 0;
	const int nameW = right - tx - laurelW - LeagueBadgeRoom(badge);
	if (tier != PatronTier::None) {
		const int laurel = PatronLaurelSize(tier);
		p.raw = true;
		DrawPatron(p, tx, nameY + 2 + (40 - laurel) / 2, laurel, tx + laurelW, nameY, nameW, nameH, c.name, tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		PatronHotspot(p, tx, nameY + 2 + (40 - laurel) / 2, laurel, v.patron, v.mouseX, v.mouseY, kActPatron);
	}
	else {
		Raw(p, tx, nameY, nameW, nameH, c.name, pal::text);
	}
	p.raw = true;
	DrawBadgeAfterName(p, tx + laurelW, nameY, nameW, nameH, c.name, badge, v.mouseX, v.mouseY);
	p.raw = false;
	if (titled) {
		p.Font(4);
		p.raw = true;
		p.TextCaps(tx, y + 44, right - tx, 20, LeagueTitleLabel(title), LeagueFrameInk(title), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
		p.raw = false;
	}
	if (!c.readable) {
		p.Font(3);
		p.Text(tx, rankY + 2, right - tx, 24, "Couldn't read this player's statistics.", pal::bad);
		return;
	}
	const ProfileSummary &s = c.summary;
	if (c.rankName.empty()) {
		p.Font(1);
		Raw(p, tx, rankY, right - tx, 26, kDash, pal::redText);
		return;
	}
	const int barW = a.wide ? right - tx : std::min(520, right - tx);
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
		Raw(p, barRight - nextW, rankY, nextW, 26, c.nextRankName, RankColor(s.nextRank, v.rankCount), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	p.Font(3);
	const int aheadW = RawWidth(p, ahead);
	const int aheadX = barRight - nextW - (nextW ? 6 : 0) - aheadW;
	Raw(p, aheadX, rankY, aheadW + 2, 26, ahead, pal::textDim);
	p.Font(1);
	Raw(p, tx, rankY, std::max(0, aheadX - 16 - tx), 26, c.rankName, RankColor(s.rank, v.rankCount));
	PaintRankBar(p, tx, barY, barW, 8, fraction, RankColor(s.rank, v.rankCount));
	p.Frame(tx - 1, barY - 1, barW + 2, 10, pal::hairline);
	p.Font(3);
	Raw(p, tx, labelY, right - tx, 22, label, pal::textDim);
}

void PaintTiles(Painter &p, const Card &c, const Area &a, const Frame &f)
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
		PaintGlassCard(p, x, f.tilesY, w, f.tileH);
		p.Font(5);
		Raw(p, x, f.tilesY + 6, w, f.tileH - 38, values[i], pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x + 6, f.tilesY + f.tileH - 32, w - 12, 22, labels[i], pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

// The league tab: the ranks of the season's two queues over the tournaments' phalerae and the seasons' rewards. It is laid out the same
// from the first frame, each card saying it reads until the account's data arrives, so nothing moves when it does.
constexpr int kLeaguePad = 24, kLeagueRowH = 52;
const UINT kLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

std::string SeasonText(int season)
{
	return TrFmt("Season %s", {LeagueRoman(season)});
}

std::string QueueText(const std::string &queue)
{
	return Tr(queue == "2v2" ? "2 vs 2" : "1 vs 1");
}

void PaintStatCell(Painter &p, int x, int y, int w, int h, const std::string &value, const char *label, COLORREF ink)
{
	p.Font(1);
	Raw(p, x + 4, y, w - 8, h - 22, value, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(x + 4, y + h - 22, w - 8, 20, label, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// One queue's standing: the emblem and the rank, the Glory and how far the next division is (or the placement games), and under them the
// record, the win rate, the streak and the season's peak.
void PaintLeagueRank(Painter &p, const View &v, const char *title, const LeagueRanked &rk, bool played, int x, int y, int w, int h)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kLeaguePad, iw = w - 2 * kLeaguePad;
	PaintSectionLabel(p, ix, y + 16, iw * 2 / 3, 22, title);
	const bool loaded = v.meLoaded;
	if (loaded && played && rk.present) {
		p.Font(3);
		Raw(p, ix + iw / 3, y + 16, iw - iw / 3, 22, SeasonText(rk.season), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	const bool strip = loaded && played;
	const int stripH = 62, stripY = y + h - stripH - 12;
	const int zoneTop = y + 52, zoneBottom = strip ? stripY - 12 : y + h - 20;
	const int emblem = std::max(48, std::min(128, zoneBottom - zoneTop));
	const int ey = zoneTop + (zoneBottom - zoneTop - emblem) / 2;
	const bool placed = loaded && played && rk.present && rk.placementLeft <= 0 && rk.tier != LeagueTier::None;
	const bool placing = loaded && played && !placed;
	if (placed) DrawLeagueEmblem(p, ix, ey, emblem, rk.tier);
	else {
		p.Frame(ix, ey, emblem, emblem, pal::edge);
		p.Font(1);
		p.Text(ix, ey, emblem, emblem, "?", loaded && played ? pal::textDim : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	const int tx = ix + emblem + 28, tw = ix + iw - tx;
	// The text block: the rank, a line under it and the bar with its labels, as much of it as the card's height lets in.
	const int blockH = std::min(zoneBottom - zoneTop, 128), by = zoneTop + (zoneBottom - zoneTop - blockH) / 2;
	const bool bar = blockH >= 96, labels = blockH >= 120;
	p.Font(2);
	std::string headline;
	COLORREF ink = pal::text;
	if (!loaded) headline = Tr("Reading your rank...");
	else if (!played) headline = Tr("Not played this season");
	else if (placed) {
		headline = LeagueRankLabel(rk.tier, rk.division);
		ink = LeagueTierInk(rk.tier);
	}
	else headline = Tr(rk.present ? "Placement" : "Not played");
	if (!loaded) {
		p.Font(0);
		Raw(p, tx, by, tw, 44, headline, pal::caption);
	}
	else Raw(p, tx, by, tw, 44, headline, loaded && played ? ink : pal::textDim);
	if (!loaded) return;
	if (!played) {
		p.Font(3);
		p.TextWrapped(tx, by + 50, tw, 44, "This queue is not part of the season being played.", pal::caption);
		return;
	}
	p.Font(0);
	if (placing) {
		const int left = rk.present ? std::max(1, rk.placementLeft) : 5, played5 = std::max(0, 5 - left);
		Raw(p, tx, by + 48, tw, 24, left == 1 ? Tr("1 placement game left") : TrFmt("%s placement games left", {std::to_string(left)}), pal::textDim);
		if (bar) {
			// One box a game, green for a win and red for a loss, in the order they were played.
			const int gap = 10, box = std::min(56, (tw - 4 * gap) / 5);
			for (int i = 0; i < 5; i++) {
				COLORREF fill = i < played5 ? pal::textDim : pal::bg;
				if (i < static_cast<int>(rk.placement.size())) {
					const LeaguePlacementMark mark = rk.placement[i];
					fill = mark == LeaguePlacementMark::Win ? pal::ok : mark == LeaguePlacementMark::Loss ? pal::bad : pal::disabled;
				}
				p.Fill(tx + i * (box + gap), by + 84, box, 10, fill);
				p.Frame(tx + i * (box + gap), by + 84, box, 10, pal::hairline);
			}
		}
		if (labels) {
			p.Font(3);
			p.Text(tx, by + 102, tw, 22, "Your first five games place you, at most in Centurion I.", pal::caption, kLine);
		}
	}
	else {
		std::string line = TrFmt("%s Glory", {Grouped(static_cast<uint64_t>(std::max(0, rk.glory)))});
		if (rk.position > 0) line += "  \xC2\xB7  #" + std::to_string(rk.position);
		Raw(p, tx, by + 48, tw, 24, line, pal::textDim);
		const LeagueGloryPlace gp = LeagueGloryPlaceOf(rk.glory);
		const bool elite = !LeagueTierHasDivisions(rk.tier);
		if (bar) {
			const float frac = gp.capped && !elite ? gp.into / 100.0f : 1.0f;
			PaintRankBar(p, tx, by + 86, tw, 8, frac, LeagueTierInk(rk.tier));
			p.Frame(tx - 1, by + 85, tw + 2, 10, pal::hairline);
		}
		if (labels) {
			p.Font(3);
			if (elite || !gp.capped) {
				p.Text(tx, by + 102, tw, 22, elite ? "Elite rank: it goes by your place in the leaderboard." : "Top division: climb the leaderboard to the elite.", pal::caption, kLine);
			}
			else {
				const std::string into = std::to_string(gp.into) + " / 100 " + Tr("Glory");
				const int intoW = std::min(tw / 2, RawWidth(p, into) + 4);
				Raw(p, tx, by + 102, intoW, 22, into, pal::caption);
				LeagueTier nt = LeagueTier::None;
				int nd = 0;
				LeagueNextDivision(gp.tier, gp.division, nt, nd);
				if (nt != LeagueTier::None) {
					const int wins = LeagueWinsToNext(rk.glory);
					Raw(p, tx + intoW + 12, by + 102, tw - intoW - 12, 22,
						wins == 1 ? TrFmt("~1 win to %s", {LeagueRankLabel(nt, nd)}) : TrFmt("~%s wins to %s", {std::to_string(wins), LeagueRankLabel(nt, nd)}),
						LeagueTierInk(nt), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				}
			}
		}
	}
	// Record, win rate, streak and the season's peak.
	p.Fill(ix, stripY, iw, 1, pal::hairline);
	const int cellW = iw / 4;
	const bool games = rk.present && rk.wins + rk.losses > 0;
	std::string peak = kDash;
	if (rk.peak.present && rk.peak.tier != LeagueTier::None) peak = LeagueRankLabel(rk.peak.tier, rk.peak.division);
	else if (rk.peak.present && rk.peak.place > 0) peak = "#" + std::to_string(rk.peak.place);
	PaintStatCell(p, ix, stripY + 8, cellW, stripH - 8, games ? std::to_string(rk.wins) + "\xE2\x80\x93" + std::to_string(rk.losses) : std::string(kDash), "Record",
		pal::text);
	PaintStatCell(p, ix + cellW, stripY + 8, cellW, stripH - 8, games ? Percent(LeagueWinRate(rk.wins, rk.losses)) : std::string(kDash), "Win rate", pal::text);
	PaintStatCell(p, ix + 2 * cellW, stripY + 8, cellW, stripH - 8, games ? LeagueStreakText(rk.streak) : std::string(kDash), "Streak",
		rk.streak > 0 ? pal::ok : rk.streak < 0 ? pal::bad : pal::text);
	PaintStatCell(p, ix + 3 * cellW, stripY + 8, iw - 3 * cellW, stripH - 8, peak, "Season peak",
		rk.peak.present && rk.peak.tier != LeagueTier::None ? LeagueTierInk(rk.peak.tier) : pal::text);
	for (int i = 1; i < 4; i++) p.Fill(ix + i * cellW, stripY + 14, 1, stripH - 22, pal::hairline);
}

// The rows of a list card from `y` to `bottom`: how many fit, the last one giving way to "+N more" when they do not all fit.
int ListFit(int y, int bottom, int count, bool &more)
{
	const int fit = std::max(0, (bottom - y) / kLeagueRowH);
	more = count > fit && fit > 0;
	return more ? fit - 1 : std::min(count, fit);
}

void PaintMoreRow(Painter &p, int x, int y, int w, int hidden)
{
	p.Fill(x, y, w, 1, pal::hairline);
	p.Font(3);
	Raw(p, x, y, w, kLeagueRowH, TrFmt("+%s more", {std::to_string(hidden)}), pal::caption);
}

const char *MetalText(LeagueMetal m)
{
	switch (m) {
	case LeagueMetal::Silver: return "Silver phalera";
	case LeagueMetal::Bronze: return "Bronze phalera";
	case LeagueMetal::Season: return "Season phalera";
	default: return "Gold phalera";
	}
}

// The tournaments: how many phalerae of each metal, and every one with its cup and season.
void PaintLeagueTrophies(Painter &p, const View &v, int x, int y, int w, int h)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kLeaguePad, iw = w - 2 * kLeaguePad;
	PaintSectionLabel(p, ix, y + 16, iw / 2, 22, "Tournaments");
	if (!v.meLoaded) {
		p.Font(3);
		p.Text(ix, y + 56, iw, 22, "Reading your tournaments...", kMuted);
		return;
	}
	std::vector<const LeagueBadge *> trophies;
	int counts[3] = {};
	for (const LeagueBadge &b : v.me.badges) {
		if (b.kind != LeagueBadgeKind::Trophy) continue;
		trophies.push_back(&b);
		const LeagueMetal m = b.metal == LeagueMetal::None ? LeagueMetal::Gold : b.metal;
		if (m == LeagueMetal::Gold || m == LeagueMetal::Season) counts[0]++;
		else if (m == LeagueMetal::Silver) counts[1]++;
		else if (m == LeagueMetal::Bronze) counts[2]++;
	}
	// The three metals at the right of the title, gold first.
	const LeagueMetal order[3] = {LeagueMetal::Gold, LeagueMetal::Silver, LeagueMetal::Bronze};
	int right = ix + iw;
	p.Font(4);
	for (int i = 2; i >= 0; i--) {
		const std::string n = std::to_string(counts[i]);
		const int nw = RawWidth(p, n) + 2;
		Raw(p, right - nw, y + 14, nw, 26, n, counts[i] ? pal::text : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		DrawLeaguePhalera(p, right - nw - 30, y + 14, 26, order[i]);
		right -= nw + 30 + 18;
	}
	const int listY = y + 52;
	if (trophies.empty()) {
		p.Font(3);
		p.TextWrapped(ix, listY + 6, iw, 64, "No phalerae yet. The podium of every official cup wins gold, silver and bronze phalerae, which you can wear next to your name.", kMuted);
		return;
	}
	bool more = false;
	const int rows = ListFit(listY, y + h - 12, static_cast<int>(trophies.size()), more);
	for (int i = 0; i < rows; i++) {
		const LeagueBadge &b = *trophies[i];
		const int ry = listY + i * kLeagueRowH;
		if (i) p.Fill(ix, ry, iw, 1, pal::hairline);
		DrawLeaguePhalera(p, ix, ry + 10, 32, b.metal == LeagueMetal::None ? LeagueMetal::Gold : b.metal);
		p.Font(4);
		Raw(p, ix + 46, ry + 5, iw - 46, 22, b.title.empty() ? Tr("Tournament") : b.title, pal::text);
		p.Font(3);
		Raw(p, ix + 46, ry + 27, iw - 46, 20, Tr(MetalText(b.metal)) + "  \xC2\xB7  " + SeasonText(b.season), pal::caption);
	}
	if (more) PaintMoreRow(p, ix, listY + rows * kLeagueRowH, iw, static_cast<int>(trophies.size()) - rows);
}

// What each closed season left: its vexillum, the rank reached in which queue, and whether it gave the frame and the title.
void PaintLeagueSeasons(Painter &p, const View &v, int x, int y, int w, int h)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kLeaguePad, iw = w - 2 * kLeaguePad;
	PaintSectionLabel(p, ix, y + 16, iw / 2, 22, "Seasons");
	PaintFrameLink(p, v, ix + iw, y + 16, iw / 2);
	const int listY = y + 52;
	if (!v.meLoaded) {
		p.Font(3);
		p.Text(ix, listY + 4, iw, 22, "Reading your season rewards...", kMuted);
		return;
	}
	if (v.me.rewards.empty()) {
		p.Font(3);
		p.TextWrapped(ix, listY + 6, iw, 64, "What you win in each season stays here. Play 10 ranked games in a season to win its vexillum.", kMuted);
		return;
	}
	bool more = false;
	const int count = static_cast<int>(v.me.rewards.size()), rows = ListFit(listY, y + h - 12, count, more);
	for (int i = 0; i < rows; i++) {
		const LeagueReward &r = v.me.rewards[i];
		const int ry = listY + i * kLeagueRowH;
		if (i) p.Fill(ix, ry, iw, 1, pal::hairline);
		DrawLeagueBadge(p, ix, ry + 10, 32, LeagueVexillumBadge(r.tier, r.founder, r.season));
		const std::string rank = r.founder ? Tr("Founder") : Tr(LeagueTierName(r.tier));
		p.Font(4);
		const int rankW = std::min(iw / 3, RawWidth(p, rank) + 4);
		Raw(p, ix + iw - rankW, ry + 5, rankW, 22, rank, r.founder ? pal::redText : LeagueTierInk(r.tier), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		Raw(p, ix + 46, ry + 5, iw - 46 - rankW - 12, 22, (r.seasonName.empty() ? SeasonText(r.season) : r.seasonName) + "  \xC2\xB7  " + QueueText(r.queue), pal::text);
		std::string line = GamesText(r.games);
		if (r.place > 0) line += "  \xC2\xB7  #" + std::to_string(r.place);
		if (r.frame) line += "  \xC2\xB7  " + Tr("Frame and title");
		p.Font(3);
		Raw(p, ix + 46, ry + 27, iw - 46, 20, line, pal::caption);
	}
	if (more) PaintMoreRow(p, ix, listY + rows * kLeagueRowH, iw, count - rows);
}

void PaintLeagueTab(Painter &p, const View &v, const Area &a, const Frame &f)
{
	const int top = f.tilesY, bottom = f.bottom;
	if (!v.linked) {
		const int h = std::min(bottom - top, 240);
		PaintGlassCard(p, a.x, top, a.w, h);
		const int ix = a.x + kLeaguePad, iw = std::min(a.w - 2 * kLeaguePad, 900);
		PaintSectionLabel(p, ix, top + 16, iw, 22, "League");
		p.Font(0);
		p.TextWrapped(ix, top + 54, iw, 80,
			"Link your Steam account to the Imperivm Resurrected league. From then on every online game you play is recorded, and ranked play will use it.",
			pal::textDim);
		PaintSteamButton(p, ix, top + h - 72, 300, 48, "Link with Steam", kActLeague, 0, v.mouseX, v.mouseY, true, true);
		return;
	}
	const std::vector<std::string> &q = v.me.queues;
	const bool plays1 = q.empty() || std::find(q.begin(), q.end(), "1v1") != q.end() || v.me.ranked.present;
	const bool plays2 = std::find(q.begin(), q.end(), "2v2") != q.end() || v.me.ranked2v2.present;
	const int half = (a.w - kGap) / 2, rightX = a.x + half + kGap, rightW = a.w - half - kGap;
	const int rankH = std::max(200, std::min(300, (bottom - top - kGap) * 48 / 100));
	PaintLeagueRank(p, v, "Ranked 1 vs 1", v.me.ranked, plays1, a.x, top, half, rankH);
	PaintLeagueRank(p, v, "Ranked 2 vs 2", v.me.ranked2v2, plays2, rightX, top, rightW, rankH);
	const int lowY = top + rankH + kGap;
	PaintLeagueTrophies(p, v, a.x, lowY, half, bottom - lowY);
	PaintLeagueSeasons(p, v, rightX, lowY, rightW, bottom - lowY);
}

void PaintCardTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	p.Font(4);
	p.Text(x + kCardPad, y + 11, w - kCardPad * 2, 22, title, pal::redText);
}

// Two columns over the evolution at full width, three in a strip beside the list.
void PaintSummary(Painter &p, const Card &c, int x, int y, int w, int h, int columns)
{
	PaintGlassCard(p, x, y, w, h);
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
	const int rowH = columns > 1 ? 46 : std::min(54, (h - kCardHead - 10) / count), inner = w - kCardPad * 2, colW = (inner - (columns - 1) * colGap) / columns;
	for (int i = 0; i < count; i++) {
		const int cx = x + kCardPad + (i % columns) * (colW + colGap), ry = y + kCardHead + (i / columns) * rowH;
		if (i >= columns) p.Fill(cx, ry, colW, 1, pal::hairline);
		p.Font(3);
		p.Text(cx, ry + 3, colW, 21, rows[i].first, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(4);
		Raw(p, cx, ry + 24, colW, 20, rows[i].second, pal::text);
	}
}

Lower LowerLayout(const Area &a, const Frame &f)
{
	Lower l;
	const int top = f.lowerY, bottom = f.bottom, h = bottom - top;
	if (a.wide) {
		l.summary = {a.x, top, kWideLeftW, kWideSummaryH};
		l.chart = {a.x, top + kWideSummaryH + kGap, kWideLeftW, h - kWideSummaryH - kGap};
		l.history = {a.x + kWideLeftW + kGap, top, a.w - kWideLeftW - kGap, h};
		l.summaryColumns = 2;
		return l;
	}
	const int y = top + kStripH + kGap;
	l.summary = {a.x, top, a.w, kStripH};
	l.chart = {a.x, y, kNarrowChartW, bottom - y};
	l.history = {a.x + kNarrowChartW + kGap, y, a.w - kNarrowChartW - kGap, bottom - y};
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

void PaintTip(Painter &p, const View &v, const Tip &tip)
{
	if (tip.lines.empty()) return;
	int w = 0;
	for (size_t i = 0; i < tip.lines.size(); i++) {
		p.Font(i == 0 ? 4 : 3);
		w = std::max(w, RawWidth(p, tip.lines[i].first));
	}
	w += 22;
	const int lineH = 22, h = static_cast<int>(tip.lines.size()) * lineH + 12;
	const int x = std::max(8, std::min(tip.x, v.screenW - 8 - w)), y = std::max(8, std::min(tip.y, v.screenH - 8 - h));
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
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
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
	PaintSegments(p, right - modeW, b.y + 8, modeW, modes, v.chartMode, kActChartMode, v.mouseX, v.mouseY);
	const int cy = b.y + kCardHead + 2;
	std::vector<std::string> metrics;
	for (const char *m : kMetricShort) metrics.push_back(Tr(m));
	PaintSegments(p, x0, cy, inner, metrics, v.metric, kActMetric, v.mouseX, v.mouseY);
	const std::vector<std::string> ranges = Translated({"Last 20", "Last 50", "All games"});
	const int rangeW = SegmentsWidth(p, ranges);
	const int footY = b.y + b.h - kSegH - 10;
	PaintSegments(p, x0, footY, rangeW, ranges, v.chartRange, kActChartRange, v.mouseX, v.mouseY);

	const std::vector<MatchFacts> facts = FactsOfCard(c);
	const size_t limit = v.chartRange == kRange20 ? 20 : v.chartRange == kRange50 ? 50 : 0;
	const std::vector<size_t> shown = MatchSelect(facts, static_cast<MatchMode>(v.chartMode), kOutcomeAll, limit);
	const int plotTop = cy + kSegH + 30, bottom = footY - 10, plotH = bottom - plotTop;
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
	const int barW = std::max(1, std::min(32, static_cast<int>(slot * 0.72)));
	const int mx = MouseX(p, v), my = MouseY(p, v);
	p.Fill(x0, bottom, inner, 1, pal::lineStrong);
	DashedH(p, x0, plotTop, inner, kRule);
	p.Font(3);
	Raw(p, x0, plotTop - 22, 90, 20, MetricText(metric, top), pal::edgeHot);
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
	const int avgLabelY = avgY - 21 < plotTop - 2 ? avgY + 3 : avgY - 21;
	p.Fill(right - avgW, avgLabelY, avgW, 20, kCardBody);
	Raw(p, right - avgW + 5, avgLabelY, avgW - 5, 20, avgText, pal::text);

	Raw(p, x0 + rangeW + 10, footY, inner - rangeW - 10, kSegH, n == 1 ? Tr("1 game") : TrFmt("%s games", {Grouped(n)}), kMuted,
		DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	int lx = right;
	for (MatchResult r : {kMatchAbandoned, kMatchLost, kMatchWon}) {
		const int tw = p.TextWidth(Tr(ResultText(r)));
		lx -= tw;
		p.Text(lx, plotTop - 23, tw + 2, 20, ResultText(r), pal::textDim);
		lx -= 15;
		p.Fill(lx, plotTop - 18, 10, 10, ResultColor(r));
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
	g.wide = g.inner >= 560;
	g.rowH = g.wide ? 36 : 50;
	g.columns = g.wide ? 4 : 3;
	g.listTop = b.y + kCardHead + (g.wide ? 26 : kSegH + 10);
	g.listBottom = b.y + b.h - 10;
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
		Raw(p, cx, cy, cw - 8, 20, cell.label, pal::textDim);
		p.Font(4);
		const int vw = std::min(cw - (cell.trend ? 24 : 8), RawWidth(p, cell.value));
		Raw(p, cx, cy + 20, vw, 20, cell.value, pal::text);
		if (cell.trend) {
			const COLORREF c = cell.good ? pal::ok : pal::bad;
			if (cell.trend > 0) p.ArrowUp(cx + vw + 5, cy + 26, 9, 7, c);
			else p.ArrowDown(cx + vw + 5, cy + 26, 9, 7, c);
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
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	p.AddHit(b.x, b.y, b.w, b.h, kActHistory, 0);
	PaintCardTitle(p, b.x, b.y, b.w, "Match history");
	const HistoryGeom gm = GeomOf(b);
	const int x0 = gm.x0, right = gm.x0 + gm.inner;
	if (c.games.empty()) {
		const int ty = b.y + b.h / 2 - 34;
		p.Font(0);
		p.Text(x0, ty, gm.inner, 26, c.readable ? "No games recorded yet." : kDash, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x0 + 20, ty + 34, gm.inner - 40, 66, "Finish a skirmish or a multiplayer game and it will show here.", pal::textDim, DT_CENTER | DT_TOP | DT_WORDBREAK);
		return;
	}
	const std::vector<std::string> outcomes = Translated({"All games", "Victories", "Defeats"});
	// Beside the title when there is room, under it otherwise.
	const int segW = gm.wide ? SegmentsWidth(p, outcomes) : gm.inner;
	PaintSegments(p, gm.wide ? right - segW : x0, gm.wide ? b.y + 8 : b.y + kCardHead, segW, outcomes, v.outcome, kActOutcome, v.mouseX, v.mouseY);
	const std::vector<size_t> rows = HistoryGames(v);
	p.Font(4);
	const int titleW = p.TextWidth(Tr("Match history"));
	p.Font(3);
	Raw(p, x0 + titleW + 10, b.y + 11, std::max(0, (gm.wide ? right - segW : right) - x0 - titleW - 20), 22, Grouped(rows.size()), pal::textDim);
	const std::vector<MatchFacts> facts = FactsOfCard(c);
	const int maxScroll = HistoryMaxScroll(v);
	const int scroll = std::max(0, std::min(v.historyScroll, maxScroll));
	const int listRight = maxScroll > 0 ? right - 12 : right;
	const int chipW = ChipWidth(p);
	// Columns of the wide history, from the right.
	const int klW = 110, timeW = 76, dateW = 120, modeW = 110;
	const int klX = listRight - klW, timeX = klX - timeW - 8, dateX = timeX - dateW - 8, modeX = dateX - modeW - 8, civX = x0 + chipW + 10;
	if (gm.wide) {
		p.Font(3);
		const int hy = b.y + kCardHead;
		p.Text(x0, hy, chipW, 22, "Result", kHead);
		p.Text(civX, hy, modeX - civX - 8, 22, "Civilization", kHead);
		p.Text(modeX, hy, modeW, 22, "Mode", kHead);
		p.Text(dateX, hy, dateW, 22, "Date", kHead);
		p.Text(timeX, hy, timeW, 22, "Duration", kHead, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Text(klX, hy, klW, 22, "Kills / losses", kHead, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	if (rows.empty()) {
		p.Font(3);
		p.Text(x0, gm.listTop + 20, gm.inner, 26, "No games in this filter.", kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
			PaintChip(p, x0 + 6, y + 6, chipW - 6, rh - 12, result);
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
			PaintChip(p, x0 + 6, y + 6, chipW - 6, 20, result);
			Raw(p, listRight - lossesW, y + 4, lossesW, 22, losses, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(4);
			const int killsW = RawWidth(p, kl);
			Raw(p, listRight - lossesW - killsW, y + 4, killsW, 22, kl, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(known ? 4 : 3);
			Raw(p, civX, y + 4, listRight - lossesW - killsW - civX - 8, 22, CivText(g), known ? pal::text : kMuted);
			p.Font(3);
			const std::string age = AgeText(v, g);
			const std::string line = ModeText(g) + "  \xC2\xB7  " + age + "  \xC2\xB7  " + Duration(g.durationMs);
			Raw(p, civX, y + 27, listRight - civX, 20, line, pal::textDim);
			if (mx >= civX && mx < listRight && my >= y + 26 && my < y + rh && my < gm.listBottom) {
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

void PaintDetail(Painter &p, const View &v, const Card &c, const Frame &f, Tip &tip)
{
	const Area a = DetailArea(v);
	PaintIdentity(p, v, c, a, f);
	if (ShownTab(v) == kTabLeague) {
		PaintLeagueTab(p, v, a, f);
		return;
	}
	PaintTiles(p, c, a, f);
	const Lower l = LowerLayout(a, f);
	PaintSummary(p, c, l.summary.x, l.summary.y, l.summary.w, l.summary.h, l.summaryColumns);
	PaintEvolution(p, v, c, l.chart, tip);
	PaintHistory(p, v, c, l.history, tip);
}

// The title with the way back at the top right and, for another player than the one in use, the button that changes to it.
void PaintHeader(Painter &p, const View &v, const Frame &f)
{
	const int count = static_cast<int>(v.cards.size());
	const bool other = v.selected >= 0 && v.selected < count && !v.cards[v.selected].active;
	int right = PaintHeaderActions(p, f.head, {{"League account", kActLeague, false, true}, {"Back", kActBack, true, true}}, v.mouseX, v.mouseY);
	if (other) {
		p.Font(1);
		const int bw = p.CapsWidth("Use this player", 1) + 56;
		right -= 12 + bw;
		p.Button(right, f.head.margin, bw, kHeaderButtonH, "Use this player", kActUse, 0, v.mouseX, v.mouseY, true, true);
	}
	const int tab = ShownTab(v);
	PaintScreenTitle(p, f.head, "STATISTICS", {{"General", kActTab, kTabGeneral, tab == kTabGeneral, true}, {"League", kActTab, kTabLeague, tab == kTabLeague, SelectedIsActive(v)}},
		right, v.mouseX, v.mouseY);
	for (const Card &c : v.cards) {
		if (!c.active) continue;
		p.raw = true;
		PaintScreenInfo(p, f.head, TrFmt("You are playing as %s.", {c.name}));
		p.raw = false;
	}
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
	const HistoryGeom gm = GeomOf(LowerLayout(DetailArea(v), FrameOf(v)).history);
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
	const HistoryGeom gm = GeomOf(LowerLayout(DetailArea(v), FrameOf(v)).history);
	const int needed = gm.rowH * 2 + DetailHeight(DetailCells(c->games[game], FactsOfCard(*c), !gm.wide).size(), gm.columns);
	const int first = needed <= gm.listBottom - gm.listTop ? at - 1 : at;
	v.historyScroll = std::max(0, std::min(first, HistoryMaxScroll(v)));
}

int ListRows(const View &v)
{
	return FrameOf(v).rows;
}

void PaintProfileScreen(Painter &p, const View &v)
{
	const Frame f = FrameOf(v);
	UseScreenFonts(p);
	p.noHover = v.leagueOpen;
	// The figures of the tiles and the big monogram: the screen's largest display size would not fit the tiles.
	p.fonts[5] = BrandFont(BrandFace::Display, f.head.roomy ? 46 : 40, p.s);
	PaintHeader(p, v, f);
	const bool list = ShowsList(v);
	if (list) PaintList(p, v, f);
	Tip tip;
	if (const Card *c = SelectedCard(v)) {
		PaintDetail(p, v, *c, f, tip);
	}
	else if (list) {
		const Area a = DetailArea(v);
		p.Font(0);
		p.Text(a.x, f.top + 120, a.w, 26, "Choose a player.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	PaintTip(p, v, tip);
	PaintPatronTip(p, v.screenW, v.screenH);
	PaintLeagueBadgeTip(p, v.screenW, v.screenH);
	if (!v.leagueOpen) return;
	// Linked, the account and the badge on the left and the season's frame and title on the right.
	const bool linked = LeagueLinked();
	const Dialog d = DialogBegin(p, v.screenW, v.screenH, linked ? std::min(v.screenW - 40, 1080) : 640, std::min(v.screenH - 40, linked ? 840 : 520), "League");
	const int colGap = 40, leftW = linked ? (d.BodyW() - colGap) / 2 : d.BodyW();
	PaintLeagueAccount(p, d.BodyX(), d.BodyTop() + 6, leftW, {kActLeagueLink, kActLeagueOpen, kActLeagueCancel, kActLeagueUnlink, kActLeagueBadge},
		v.mouseX, v.mouseY);
	if (linked) {
		const int rx = d.BodyX() + leftW + colGap;
		p.Fill(rx - colGap / 2, d.BodyTop() + 6, 1, d.FooterTop() - d.BodyTop() - 22, pal::hairline);
		if (v.meLoaded) PaintLeagueCosmeticChoices(p, rx, d.BodyTop() + 6, d.BodyW() - leftW - colGap, v.me, kActLeagueFrame, kActLeagueTitle, v.mouseX, v.mouseY);
		else {
			p.Font(3);
			p.Text(rx, d.BodyTop() + 6, d.BodyW() - leftW - colGap, 24, "Reading your season rewards...", pal::caption);
		}
	}
	DialogButtons(p, d, {{"Close", kActLeagueClose}}, v.mouseX, v.mouseY);
	PaintLeagueBadgeTip(p, v.screenW, v.screenH);
}

}
