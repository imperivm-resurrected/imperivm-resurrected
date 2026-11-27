// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/profileui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
#include "ui/scrollpane.h"
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
	f.head = ScreenLayoutFor(v.screenW, v.screenH, false);
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
	TextAsIs(p, x, y, size, size, Initials(name), strong ? pal::text : pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
	TextAsIs(p, tx, y + 9, nameW, 25, c.name, pal::text);
	p.Font(3);
	const std::string games = GamesText(c.summary.games);
	const COLORREF dim = pal::textDim;
	if (c.rankName.empty() || !c.readable) {
		TextAsIs(p, tx, y + 35, w - 80, 21, games, dim);
	}
	else {
		const int rankW = std::min(w - 80, RawWidth(p, c.rankName));
		TextAsIs(p, tx, y + 35, rankW, 21, c.rankName, RankColor(c.summary.rank, rankCount));
		TextAsIs(p, tx + rankW, y + 35, w - 80 - rankW, 21, "  \xC2\xB7  " + games, dim);
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
	TextAsIs(p, x, f.top + 18, w, 22, std::to_string(v.cards.size()), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
	PaintSectionLabel(p, x, y + 14, w / 2, 22, "Showcase");
	if (!v.pub.on) PaintFrameLink(p, v, x + w, y + 14, w / 2);
	if (!v.meLoaded) {
		// The row of vexilla and phalerae the showcase will hold.
		for (int k = 0; k < 6 && k * 66 + 56 <= w; k++) PaintSkeleton(p, x + k * 66, y + 44, 56, 56);
		return;
	}
	// Another player's showcase is only looked at.
	const int shown = PaintLeagueShowcase(p, x, y + 44, w, 56, v.me, v.pub.on ? -1 : kActLeagueBadge, v.mouseX, v.mouseY);
	p.Font(3);
	if (!shown && !v.pub.on) {
		const char *text = "What you win in each season stays here. Play 10 ranked games in a season and win 5 to earn its vexillum.";
		p.TextWrapped(x, y + 46, w, 58, text, kMuted);
		return;
	}
	// The titles of calendar cups held now (the Crown of the month, Champion of a season) take the hint's line, in gold.
	if (!v.me.cupTitles.empty()) {
		std::string titles;
		for (const LeagueCupTitle &t : v.me.cupTitles) titles += (titles.empty() ? "" : "  ·  ") + t.name;
		p.raw = true;
		p.Text(x, y + 104, w, 18, titles, RGB(0xd8, 0xae, 0x46), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		return;
	}
	if (!v.pub.on) p.Text(x, y + 104, w, 18, "Click one to wear it next to your name.", kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

bool PublicShowcase(const View &v)
{
	return std::any_of(v.me.badges.begin(), v.me.badges.end(), [](const LeagueBadge &b) { return b.kind == LeagueBadgeKind::Trophy; }) ||
		!v.me.cupTitles.empty();
}

// Under another player's name, where the player's own military rank goes: their rank in each queue of the season, and what the figures
// come from.
void PaintPublicStanding(Painter &p, const View &v, int x, int rankY, int w, int labelY)
{
	int cx = x;
	p.Font(1);
	for (const LeagueRanked *rk : {&v.me.ranked, &v.me.ranked2v2}) {
		if (!rk->present || rk->tier == LeagueTier::None || rk->placementLeft > 0) continue;
		std::string text = LeagueRankLabel(rk->tier, rk->division);
		const std::string queue = Tr(rk->queue == "2v2" ? "2 vs 2" : "1 vs 1");
		if (cx > x) {
			const std::string dot = "  \xC2\xB7  ";
			TextAsIs(p, cx, rankY, std::max(0, x + w - cx), 26, dot, pal::textDim);
			cx += RawWidth(p, dot);
		}
		const int tw = std::min(std::max(0, x + w - cx), RawWidth(p, text));
		TextAsIs(p, cx, rankY, tw, 26, text, LeagueTierInk(rk->tier));
		cx += tw;
		p.Font(3);
		const std::string tail = " (" + queue + ")";
		TextAsIs(p, cx, rankY, std::max(0, x + w - cx), 26, tail, pal::textDim);
		cx += RawWidth(p, tail);
		p.Font(1);
	}
	p.Font(3);
	p.Text(x, labelY, w, 22, "League and cup games", kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintIdentity(Painter &p, const View &v, const Card &c, const Area &a, const Frame &f)
{
	const int x = a.x, y = f.top, w = a.w;
	PaintGlassCard(p, x, y, w, kIdentityH);
	const int ax = x + 16, ay = y + 14;
	// The Steam user's picture, laurel and badge: on the player in use, or on the player the public profile is of.
	const bool pub = v.pub.on, own = c.active || pub;
	const bool steam = own && v.avatar && !v.avatar->bgra.empty();
	// The season's frame and title go with the league account, which is the Steam user's: only on the player in use.
	const bool league = c.active && v.linked && v.meLoaded;
	const LeagueFrame frame = pub ? v.pub.frame : league ? LeagueOwnCosmetic(v.me, v.me.equippedFrame) : LeagueFrame();
	const LeagueFrame title = pub ? v.pub.title : league ? LeagueOwnCosmetic(v.me, v.me.equippedTitle) : LeagueFrame();
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
	const int showcaseW = a.wide && ((c.active && v.linked) || (pub && PublicShowcase(v))) ? std::min(600, w * 36 / 100) : 0;
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
	const PatronTier tier = own ? v.patron.tier : PatronTier::None;
	const LeagueBadge badge = own ? v.badge : LeagueBadge();
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
		TextAsIs(p, tx, nameY, nameW, nameH, c.name, pal::text);
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
	if (pub) {
		PaintPublicStanding(p, v, tx, rankY, right - tx, labelY);
		return;
	}
	if (!c.readable) {
		p.Font(3);
		p.Text(tx, rankY + 2, right - tx, 24, "Couldn't read this player's statistics.", pal::bad);
		return;
	}
	const ProfileSummary &s = c.summary;
	if (c.rankName.empty()) {
		p.Font(1);
		TextAsIs(p, tx, rankY, right - tx, 26, kDash, pal::redText);
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
		TextAsIs(p, barRight - nextW, rankY, nextW, 26, c.nextRankName, RankColor(s.nextRank, v.rankCount), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	p.Font(3);
	const int aheadW = RawWidth(p, ahead);
	const int aheadX = barRight - nextW - (nextW ? 6 : 0) - aheadW;
	TextAsIs(p, aheadX, rankY, aheadW + 2, 26, ahead, pal::textDim);
	p.Font(1);
	TextAsIs(p, tx, rankY, std::max(0, aheadX - 16 - tx), 26, c.rankName, RankColor(s.rank, v.rankCount));
	PaintRankBar(p, tx, barY, barW, 8, fraction, RankColor(s.rank, v.rankCount));
	p.Frame(tx - 1, barY - 1, barW + 2, 10, pal::hairline);
	p.Font(3);
	TextAsIs(p, tx, labelY, right - tx, 22, label, pal::textDim);
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
		TextAsIs(p, x, f.tilesY + 6, w, f.tileH - 38, values[i], pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

// How a stat label fits its cell: 0 in the caption type, 1 in the small type, 2 in two lines of it.
int StatLabelFit(Painter &p, int w, const char *label)
{
	p.Font(3);
	if (p.TextWidth(label) <= w - 8) return 0;
	p.Font(4);
	return p.TextWidth(label) <= w - 8 ? 1 : 2;
}

constexpr int kStatLabelH = 22, kStatLabel2H = 36;

void PaintStatCell(Painter &p, int x, int y, int w, int h, const std::string &value, const char *label, COLORREF ink)
{
	const int fit = StatLabelFit(p, w, label), labelH = fit == 2 ? kStatLabel2H : kStatLabelH;
	p.Font(1);
	TextAsIs(p, x + 4, y, w - 8, h - labelH, value, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(fit ? 4 : 3);
	const UINT fmt = fit == 2 ? DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS : DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	p.Text(x + 4, y + h - labelH, w - 8, labelH - 2, label, pal::caption, fmt);
}

const char *const kStatLabels[4] = {"Record", "Win rate", "Streak", "Season peak"};

// A caption under the rank: in one line, or two when it is longer and the card has room for them.
void PaintRankCaption(Painter &p, int x, int y, int w, int room, const char *text)
{
	p.Font(3);
	if (p.TextWidth(text) > w && room >= 40) p.TextWrapped(x, y, w, std::min(room, 42), text, pal::caption);
	else p.Text(x, y, w, 22, text, pal::caption, kLine);
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
		TextAsIs(p, ix + iw / 3, y + 16, iw - iw / 3, 22, SeasonText(rk.season), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	const bool strip = loaded && played;
	// The league keeps no streak of another player's: their place in the leaderboard takes its cell.
	const bool pub = v.pub.on;
	const char *const labels4[4] = {kStatLabels[0], kStatLabels[1], pub ? "Position" : kStatLabels[2], kStatLabels[3]};
	bool twoLines = false;
	for (int i = 0; i < 4; i++) twoLines = twoLines || StatLabelFit(p, iw / 4, labels4[i]) == 2;
	const int stripH = twoLines ? 62 + kStatLabel2H - kStatLabelH : 62, stripY = y + h - stripH - 12;
	const int zoneTop = y + 52, zoneBottom = strip ? stripY - 12 : y + h - 20;
	const int emblem = std::max(48, std::min({128, zoneBottom - zoneTop, iw * 30 / 100}));
	const int ey = zoneTop + (zoneBottom - zoneTop - emblem) / 2;
	const bool placed = loaded && played && rk.present && rk.placementLeft <= 0 && rk.tier != LeagueTier::None;
	const bool placing = loaded && played && !placed;
	if (!loaded) {
		// The emblem, the rank and the bar the card will show.
		const int tx = ix + emblem + 28, tw = ix + iw - tx;
		const int blockH = std::min(zoneBottom - zoneTop, 128), by = zoneTop + (zoneBottom - zoneTop - blockH) / 2;
		PaintSkeleton(p, ix, ey, emblem, emblem);
		PaintSkeleton(p, tx, by + 10, tw * 55 / 100, 24);
		PaintSkeleton(p, tx, by + 52, tw * 35 / 100, 12);
		if (blockH >= 96) PaintSkeleton(p, tx, by + 82, tw, 10);
		return;
	}
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
	const int captionRoom = zoneBottom - (by + 102);
	std::string headline;
	COLORREF ink = pal::text;
	if (!played) headline = Tr("Not played this season");
	else if (placed) {
		headline = LeagueRankLabel(rk.tier, rk.division);
		ink = LeagueTierInk(rk.tier);
	}
	else headline = Tr(rk.present ? "Placement" : "Not played");
	// The rank in the display type, a little smaller when the card is too narrow for it.
	int headSize = 30;
	SelectObject(p.dc, BrandFont(BrandFace::Display, headSize, p.s));
	while (headSize > 22 && RawWidth(p, headline) > tw) SelectObject(p.dc, BrandFont(BrandFace::Display, headSize -= 2, p.s));
	TextAsIs(p, tx, by, tw, 44, headline, played ? ink : pal::textDim);
	if (!played) {
		p.Font(3);
		p.TextWrapped(tx, by + 50, tw, 44, pub ? "No ranked games in this queue this season." : "This queue is not part of the season being played.", pal::caption);
		return;
	}
	p.Font(0);
	if (placing) {
		const int left = rk.present ? std::max(1, rk.placementLeft) : 5, played5 = std::max(0, 5 - left);
		TextAsIs(p, tx, by + 48, tw, 24, left == 1 ? Tr("1 placement game left") : TrFmt("%s placement games left", {std::to_string(left)}), pal::textDim);
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
		if (labels && !pub) PaintRankCaption(p, tx, by + 102, tw, captionRoom, "Your first five games place you, at most in Centurion I.");
	}
	else {
		std::string line = TrFmt("%s Glory", {Grouped(static_cast<uint64_t>(std::max(0, rk.glory)))});
		if (rk.position > 0) line += "  \xC2\xB7  #" + std::to_string(rk.position);
		TextAsIs(p, tx, by + 48, tw, 24, line, pal::textDim);
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
				if (!pub) PaintRankCaption(p, tx, by + 102, tw, captionRoom,
					elite ? "Elite rank: it goes by your place in the leaderboard." : "Top division: climb the leaderboard to the elite.");
			}
			else {
				const std::string into = std::to_string(gp.into) + " / 100 " + Tr("Glory");
				const int intoW = std::min(tw / 2, RawWidth(p, into) + 4);
				TextAsIs(p, tx, by + 102, intoW, 22, into, pal::caption);
				LeagueTier nt = LeagueTier::None;
				int nd = 0;
				LeagueNextDivision(gp.tier, gp.division, nt, nd);
				if (nt != LeagueTier::None) {
					const int wins = LeagueWinsToNext(rk.glory);
					TextAsIs(p, tx + intoW + 12, by + 102, tw - intoW - 12, 22,
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
	PaintStatCell(p, ix, stripY + 8, cellW, stripH - 8, games ? std::to_string(rk.wins) + "\xE2\x80\x93" + std::to_string(rk.losses) : std::string(kDash), labels4[0],
		pal::text);
	PaintStatCell(p, ix + cellW, stripY + 8, cellW, stripH - 8, games ? Percent(LeagueWinRate(rk.wins, rk.losses)) : std::string(kDash), labels4[1], pal::text);
	if (pub) PaintStatCell(p, ix + 2 * cellW, stripY + 8, cellW, stripH - 8, rk.position > 0 ? "#" + std::to_string(rk.position) : std::string(kDash), labels4[2], pal::text);
	else {
		PaintStatCell(p, ix + 2 * cellW, stripY + 8, cellW, stripH - 8, games ? LeagueStreakText(rk.streak) : std::string(kDash), labels4[2],
			rk.streak > 0 ? pal::ok : rk.streak < 0 ? pal::bad : pal::text);
	}
	PaintStatCell(p, ix + 3 * cellW, stripY + 8, iw - 3 * cellW, stripH - 8, peak, labels4[3],
		rk.peak.present && rk.peak.tier != LeagueTier::None ? LeagueTierInk(rk.peak.tier) : pal::text);
	for (int i = 1; i < 4; i++) p.Fill(ix + i * cellW, stripY + 14, 1, stripH - 22, pal::hairline);
}

// The rows of a league list card, from listY to the card's bottom: they scroll when there are more than fit.
PaneSpec LeagueListPane(const View &v, int id, int x, int listY, int w, int bottom, int count)
{
	PaneSpec spec;
	spec.id = id;
	spec.x = x + kLeaguePad;
	spec.y = listY;
	spec.w = w - kLeaguePad - 8;
	spec.h = std::max(0, bottom - listY);
	spec.contentH = count * kLeagueRowH;
	spec.barX = x + w - 14;
	spec.drag = &v.bar;
	spec.mx = v.mouseX;
	spec.my = v.mouseY;
	return spec;
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

// The tournaments: how many phalerae of each metal, and every cup played, official or of the community, with the place reached. A server
// before the list of cups gives only the phalerae, which are listed instead.
void PaintLeagueTrophies(Painter &p, const View &v, int x, int y, int w, int h)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kLeaguePad, iw = w - 2 * kLeaguePad;
	PaintSectionLabel(p, ix, y + 16, iw / 2, 22, "Tournaments");
	if (!v.meLoaded) {
		const int listY = y + 52;
		for (int i = 0; listY + (i + 1) * kLeagueRowH <= y + h - 12 && i < 4; i++) {
			const int ry = listY + i * kLeagueRowH;
			if (i) p.Fill(ix, ry, iw, 1, pal::hairline);
			PaintSkeleton(p, ix, ry + 10, 32, 32);
			PaintSkeleton(p, ix + 48, ry + 14, (iw - 48) * (60 - i * 8) / 100, 12);
			PaintSkeleton(p, ix + 48, ry + 32, (iw - 48) * 30 / 100, 10);
		}
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
		TextAsIs(p, right - nw, y + 14, nw, 26, n, counts[i] ? pal::text : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		DrawLeaguePhalera(p, right - nw - 30, y + 14, 26, order[i]);
		right -= nw + 30 + 18;
	}
	const int listY = y + 52;
	const int count = v.me.cupsKnown ? static_cast<int>(v.me.cups.size()) : static_cast<int>(trophies.size());
	if (!count && v.pub.on) {
		p.Font(3);
		p.TextWrapped(ix, listY + 6, iw, 64, "No podium in an official cup yet.", kMuted);
		return;
	}
	if (!count) {
		p.Font(3);
		p.TextWrapped(ix, listY + 6, iw, 64,
			"You have not played any cup yet. The podium of the official cups wins gold, silver and bronze phalerae; anyone can make a community cup in "
			"Multiplayer, Tournaments.",
			kMuted);
		return;
	}
	ScrollPane pane(p, LeagueListPane(v, kPaneTrophies, x, listY, w, y + h - 12, count), v.panes.Of(kPaneTrophies));
	for (int i = 0; i < count; i++) {
		const int ry = pane.Top() + i * kLeagueRowH;
		if (!pane.Shows(ry, kLeagueRowH)) continue;
		if (i) p.Fill(ix, ry, iw, 1, pal::hairline);
		if (!v.me.cupsKnown) {
			const LeagueBadge &b = *trophies[i];
			DrawLeaguePhalera(p, ix, ry + 10, 32, b.metal == LeagueMetal::None ? LeagueMetal::Gold : b.metal);
			p.Font(4);
			TextAsIs(p, ix + 46, ry + 5, iw - 46, 22, b.title.empty() ? Tr("Tournament") : b.title, pal::text);
			p.Font(3);
			TextAsIs(p, ix + 46, ry + 27, iw - 46, 20, Tr(MetalText(b.metal)) + "  \xC2\xB7  " + SeasonText(b.season), pal::caption);
			continue;
		}
		const LeagueCup &cup = v.me.cups[i];
		// The podium's metal: the phalera itself in an official cup, its colour on the place in any cup.
		const LeagueMetal metal = cup.placement == 1 ? LeagueMetal::Gold : cup.placement == 2 ? LeagueMetal::Silver : cup.placement == 3 ? LeagueMetal::Bronze
																																	 : LeagueMetal::None;
		if (cup.official && metal != LeagueMetal::None) DrawLeaguePhalera(p, ix, ry + 10, 32, metal);
		else {
			p.Frame(ix, ry + 10, 32, 32, pal::edge);
			p.Font(4);
			TextAsIs(p, ix, ry + 10, 32, 32, cup.placement > 0 ? std::to_string(cup.placement) : std::string(kDash), metal != LeagueMetal::None ? LeagueMetalInk(metal) : pal::textDim,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		const std::string place = cup.placement > 0 ? TrFmt("place %s of %s", {std::to_string(cup.placement), std::to_string(std::max(cup.placement, cup.entries))})
													: std::string(kDash);
		p.Font(4);
		const int placeW = std::min(iw / 3, RawWidth(p, place) + 4);
		TextAsIs(p, ix + iw - placeW, ry + 5, placeW, 22, place, metal != LeagueMetal::None ? LeagueMetalInk(metal) : pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		TextAsIs(p, ix + 46, ry + 5, iw - 46 - placeW - 12, 22, cup.name, pal::text);
		std::string line = Tr(cup.official ? "Official cup" : "Community cup") + "  \xC2\xB7  " + QueueText(cup.mode);
		if (cup.season >= 0) line += "  \xC2\xB7  " + SeasonText(cup.season);
		p.Font(3);
		TextAsIs(p, ix + 46, ry + 27, iw - 46, 20, line, pal::caption);
	}
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
		for (int i = 0; listY + (i + 1) * kLeagueRowH <= y + h - 12 && i < 4; i++) {
			const int ry = listY + i * kLeagueRowH;
			if (i) p.Fill(ix, ry, iw, 1, pal::hairline);
			PaintSkeleton(p, ix, ry + 10, 32, 32);
			PaintSkeleton(p, ix + 48, ry + 14, (iw - 48) * (55 - i * 8) / 100, 12);
			PaintSkeleton(p, ix + 48, ry + 32, (iw - 48) * 25 / 100, 10);
		}
		return;
	}
	if (v.me.rewards.empty()) {
		p.Font(3);
		p.TextWrapped(ix, listY + 6, iw, 64, "What you win in each season stays here. Play 10 ranked games in a season to win its vexillum.", kMuted);
		return;
	}
	const int count = static_cast<int>(v.me.rewards.size());
	ScrollPane pane(p, LeagueListPane(v, kPaneSeasons, x, listY, w, y + h - 12, count), v.panes.Of(kPaneSeasons));
	for (int i = 0; i < count; i++) {
		const LeagueReward &r = v.me.rewards[i];
		const int ry = pane.Top() + i * kLeagueRowH;
		if (!pane.Shows(ry, kLeagueRowH)) continue;
		if (i) p.Fill(ix, ry, iw, 1, pal::hairline);
		DrawLeagueBadge(p, ix, ry + 10, 32, LeagueVexillumBadge(r.tier, r.founder, r.season));
		const std::string rank = r.founder ? Tr("Founder") : Tr(LeagueTierName(r.tier));
		p.Font(4);
		const int rankW = std::min(iw / 3, RawWidth(p, rank) + 4);
		TextAsIs(p, ix + iw - rankW, ry + 5, rankW, 22, rank, r.founder ? pal::redText : LeagueTierInk(r.tier), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		TextAsIs(p, ix + 46, ry + 5, iw - 46 - rankW - 12, 22, (r.seasonName.empty() ? SeasonText(r.season) : r.seasonName) + "  \xC2\xB7  " + QueueText(r.queue), pal::text);
		std::string line = GamesText(r.games);
		if (r.place > 0) line += "  \xC2\xB7  #" + std::to_string(r.place);
		if (r.frame) line += "  \xC2\xB7  " + Tr("Frame and title");
		p.Font(3);
		TextAsIs(p, ix + 46, ry + 27, iw - 46, 20, line, pal::caption);
	}
}

void PaintGloryChart(Painter &p, const View &v, int x, int y, int w, int h, Tip &tip);

void PaintLeagueTab(Painter &p, const View &v, const Area &a, const Frame &f, Tip &tip)
{
	const int top = f.tilesY, bottom = f.bottom;
	if (!v.linked && !v.pub.on) {
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
	// Another player's queue they have not played this season is said so, whatever the season plays.
	const bool plays1 = v.pub.on ? v.me.ranked.present : q.empty() || std::find(q.begin(), q.end(), "1v1") != q.end() || v.me.ranked.present;
	const bool plays2 = v.pub.on ? v.me.ranked2v2.present : std::find(q.begin(), q.end(), "2v2") != q.end() || v.me.ranked2v2.present;
	const int half = (a.w - kGap) / 2, rightX = a.x + half + kGap, rightW = a.w - half - kGap;
	const int rankH = std::max(200, std::min(300, (bottom - top - kGap) * 48 / 100));
	PaintLeagueRank(p, v, "Ranked 1 vs 1", v.me.ranked, plays1, a.x, top, half, rankH);
	PaintLeagueRank(p, v, "Ranked 2 vs 2", v.me.ranked2v2, plays2, rightX, top, rightW, rankH);
	// Under the ranks, the season's Glory beside the tournaments and the seasons.
	const int lowY = top + rankH + kGap, lowH = bottom - lowY;
	// The league keeps another player's seasons for themselves: their tournaments take that room.
	if (v.pub.on) {
		const int chartW = (a.w - kGap) * 55 / 100, trophiesX = a.x + chartW + kGap;
		PaintGloryChart(p, v, a.x, lowY, chartW, lowH, tip);
		PaintLeagueTrophies(p, v, trophiesX, lowY, a.x + a.w - trophiesX, lowH);
		return;
	}
	const int chartW = (a.w - 2 * kGap) * 40 / 100, listW = (a.w - 2 * kGap - chartW) / 2;
	const int trophiesX = a.x + chartW + kGap, seasonsX = trophiesX + listW + kGap;
	PaintGloryChart(p, v, a.x, lowY, chartW, lowH, tip);
	PaintLeagueTrophies(p, v, trophiesX, lowY, listW, lowH);
	PaintLeagueSeasons(p, v, seasonsX, lowY, a.x + a.w - seasonsX, lowH);
}

void PaintCardTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	p.Font(4);
	p.Text(x + kCardPad, y + 11, w - kCardPad * 2, 22, title, pal::redText);
}

using SummaryRows = std::vector<std::pair<const char *, std::string>>;
SummaryRows PublicSummaryRows(const View &v);

// Two columns over the evolution at full width, three in a strip beside the list.
void PaintSummary(Painter &p, const View &v, const Card &c, int x, int y, int w, int h, int columns)
{
	PaintGlassCard(p, x, y, w, h);
	PaintCardTitle(p, x, y, w, "Summary");
	const ProfileSummary &s = c.summary;
	const bool ok = c.readable;
	std::string civ = kDash;
	if (ok && s.favoriteRace >= 0) civ = (c.favoriteCiv.empty() ? Tr("Unknown") : c.favoriteCiv) + " \xC2\xB7 " + Percent(s.favoriteRacePercent);
	const std::string best = ok && !c.bestUnit.empty() ? TrFmt("%s \xC2\xB7 level %s", {c.bestUnit, std::to_string(s.bestLevel)}) : std::string(kDash);
	const std::string army = ok && s.largestArmy ? TrFmt("%s units", {Grouped(s.largestArmy)}) : std::string(kDash);
	const SummaryRows rows = v.pub.on ? PublicSummaryRows(v)
									  : SummaryRows{
											{"Skirmish", ok ? GamesAndWins(s.singleGames, s.singleWins) : std::string(kDash)},
											{"Multiplayer", ok ? GamesAndWins(s.multiGames, s.multiWins) : std::string(kDash)},
											{"Favourite civilization", civ},
											{"Favourite unit", ok && !c.favoriteUnit.empty() ? c.favoriteUnit : std::string(kDash)},
											{"Best unit", best},
											{"Largest army", army},
										};
	const int count = static_cast<int>(rows.size()), colGap = 20;
	if (!count) {
		p.Font(3);
		p.Text(x, y + kCardHead, w, h - kCardHead - 8, "No games yet", kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const int rowH = columns > 1 ? 46 : std::min(54, (h - kCardHead - 10) / std::max(1, count)), inner = w - kCardPad * 2,
			  colW = (inner - (columns - 1) * colGap) / columns;
	for (int i = 0; i < count; i++) {
		const int cx = x + kCardPad + (i % columns) * (colW + colGap), ry = y + kCardHead + (i / columns) * rowH;
		if (i >= columns) p.Fill(cx, ry, colW, 1, pal::hairline);
		p.Font(3);
		p.Text(cx, ry + 3, colW, 21, rows[i].first, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(4);
		TextAsIs(p, cx, ry + 24, colW, 20, rows[i].second, pal::text);
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
std::string ModeText(const GameRow &g)
{
	if (g.league == kMatchLeague) return Tr("Ranked");
	if (g.league == kMatchCup) return Tr("Tournament");
	return Tr(g.multi ? "Multiplayer" : "Skirmish");
}

std::vector<MatchFacts> FactsOfCard(const Card &c)
{
	std::vector<MatchFacts> out;
	out.reserve(c.games.size());
	for (const GameRow &g : c.games) out.push_back(FactsOf(g));
	return out;
}

// The civilization whose games the chart and the history show: the one chosen in the civilizations tab, every one elsewhere.
int ShownCiv(const View &v)
{
	if (ShownTab(v) != kTabCivs || v.civ == kCivAll) return kCivAll;
	return CivKnown(v, v.civ) ? v.civ : kCivAll;
}

int SegmentsWidth(Painter &p, const std::vector<std::string> &labels)
{
	p.Font(3);
	int w = 0;
	for (const std::string &l : labels) w += p.TextWidth(l) + 2 * kSegPad;
	return w;
}

// Translated labels side by side in a row as wide as `w`, the room left over shared between them. A click gives the label's position, or
// its value in `args`.
void PaintSegments(Painter &p, int x, int y, int w, const std::vector<std::string> &labels, int selected, int action, int mx, int my,
	const std::vector<int> *args = nullptr)
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
		p.WellSegment(bx, y, bw, kSegH, on, hot);
		p.Text(bx + 2, y, bw - 4, kSegH, labels[i], on || hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!on) p.AddHit(bx, y, bw, kSegH, action, args && i < static_cast<int>(args->size()) ? (*args)[i] : i);
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
		TextAsIs(p, x + 11, y + 6 + static_cast<int>(i) * lineH, w - 22, lineH, tip.lines[i].first, tip.lines[i].second);
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
	static const char *const kModeLabels[] = {"All games", "Skirmish", "Multiplayer", "Ranked", "Tournament"};
	const std::vector<int> modeArgs = ChartModes(v), metricArgs = ChartMetrics(v);
	std::vector<std::string> modes, metrics;
	for (int m : modeArgs) modes.push_back(Tr(kModeLabels[m]));
	for (int m : metricArgs) metrics.push_back(Tr(kMetricShort[m]));
	const auto at = [](const std::vector<int> &list, int value) {
		const auto it = std::find(list.begin(), list.end(), value);
		return it == list.end() ? 0 : static_cast<int>(it - list.begin());
	};
	const int modeAt = at(modeArgs, v.chartMode), metricAt = at(metricArgs, v.metric);
	const int modeW = SegmentsWidth(p, modes);
	PaintSegments(p, right - modeW, b.y + 8, modeW, modes, modeAt, kActChartMode, v.mouseX, v.mouseY, &modeArgs);
	const int cy = b.y + kCardHead + 2;
	PaintSegments(p, x0, cy, inner, metrics, metricAt, kActMetric, v.mouseX, v.mouseY, &metricArgs);
	const std::vector<std::string> ranges = Translated({"Last 20", "Last 50", "All games"});
	const int rangeW = SegmentsWidth(p, ranges);
	const int footY = b.y + b.h - kSegH - 10;
	PaintSegments(p, x0, footY, rangeW, ranges, v.chartRange, kActChartRange, v.mouseX, v.mouseY);

	const std::vector<MatchFacts> facts = FactsOfCard(c);
	const size_t limit = v.chartRange == kRange20 ? 20 : v.chartRange == kRange50 ? 50 : 0;
	const std::vector<size_t> shown = MatchSelect(facts, static_cast<MatchMode>(modeArgs[modeAt]), kOutcomeAll, limit, ShownCiv(v));
	const int plotTop = cy + kSegH + 30, bottom = footY - 10, plotH = bottom - plotTop;
	if (shown.empty() || plotH < 20) {
		p.Font(3);
		p.Text(x0, plotTop, inner, std::max(20, plotH), "No games in this filter.", kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const MatchMetric metric = static_cast<MatchMetric>(metricArgs[metricAt]);
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
	TextAsIs(p, x0, plotTop - 22, 90, 20, MetricText(metric, top), pal::edgeHot);
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
	TextAsIs(p, right - avgW + 5, avgLabelY, avgW - 5, 20, avgText, pal::text);

	TextAsIs(p, x0 + rangeW + 10, footY, inner - rangeW - 10, kSegH, n == 1 ? Tr("1 game") : TrFmt("%s games", {Grouped(n)}), kMuted,
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
	// The wide row's fixed columns (result, mode, date, duration, kills) take about 580 units with the bar: below this the civilization
	// would be a few letters, and the two-line rows read better.
	g.wide = g.inner >= 740;
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
	if (g.remote) {
		// What the league keeps of a game.
		if (!g.opponents.empty()) cells.push_back({Tr(g.opponents.find(", ") == std::string::npos ? "Opponent" : "Opponents"), g.opponents});
		if (!g.cupName.empty()) cells.push_back({Tr("Tournament"), g.cupName});
		else if (!g.queue.empty()) cells.push_back({Tr("Queue"), Tr(g.queue == "2v2" ? "2 vs 2" : "1 vs 1")});
		if (!g.map.empty()) cells.push_back({Tr("Map"), LeagueMapText(g.map)});
		cells.push_back({Tr("Kills"), Grouped(g.kills)});
		trend(kMetricKills, true, cells.back());
		cells.push_back({Tr("Losses"), Grouped(g.losses)});
		trend(kMetricLosses, false, cells.back());
		cells.push_back({Tr(narrow ? "K/L ratio" : "Kills per loss"), OneDecimal(MatchMetricValue(f, kMetricKillsPerLoss))});
		trend(kMetricKillsPerLoss, true, cells.back());
		return cells;
	}
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
	if (g.goldSpent >= 0) cells.push_back({Tr("Gold spent"), Grouped(static_cast<uint64_t>(g.goldSpent))});
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
		TextAsIs(p, cx, cy, cw - 8, 20, cell.label, pal::textDim);
		p.Font(4);
		const int vw = std::min(cw - (cell.trend ? 24 : 8), RawWidth(p, cell.value));
		TextAsIs(p, cx, cy + 20, vw, 20, cell.value, pal::text);
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
		if (v.pub.on) return;
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
	TextAsIs(p, x0 + titleW + 10, b.y + 11, std::max(0, (gm.wide ? right - segW : right) - x0 - titleW - 20), 22, Grouped(rows.size()), pal::textDim);
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
			TextAsIs(p, civX, y, modeX - civX - 8, rh, CivText(g), known ? pal::text : kMuted);
			p.Font(3);
			TextAsIs(p, modeX, y, modeW, rh, ModeText(g), pal::textDim);
			TextAsIs(p, dateX, y, dateW, rh, AgeText(v, g), pal::textDim);
			if (mx >= dateX && mx < dateX + dateW && my >= y && my < y + rh && my < gm.listBottom) {
				tip.lines = {{DateText(g), pal::text}};
				tip.x = dateX;
				tip.y = y + rh + 2;
			}
			TextAsIs(p, timeX, y, timeW, rh, Duration(g.durationMs), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			TextAsIs(p, listRight - lossesW, y, lossesW, rh, losses, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(4);
			TextAsIs(p, klX, y, klW - lossesW, rh, kl, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
		else {
			PaintChip(p, x0 + 6, y + 6, chipW - 6, 20, result);
			TextAsIs(p, listRight - lossesW, y + 4, lossesW, 22, losses, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(4);
			const int killsW = RawWidth(p, kl);
			TextAsIs(p, listRight - lossesW - killsW, y + 4, killsW, 22, kl, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Font(known ? 4 : 3);
			TextAsIs(p, civX, y + 4, listRight - lossesW - killsW - civX - 8, 22, CivText(g), known ? pal::text : kMuted);
			p.Font(3);
			const std::string age = AgeText(v, g);
			const std::string line = ModeText(g) + "  \xC2\xB7  " + age + "  \xC2\xB7  " + Duration(g.durationMs);
			TextAsIs(p, civX, y + 27, listRight - civX, 20, line, pal::textDim);
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

// The civilizations tab: the player's games with one civilization, or with all of them, so that a hard one does not hide how the
// others go. The chips choose it and the table compares them all; the chart and the history are those of the general tab, of its
// games alone.
constexpr int kCivRowH = 36, kCivTableHead = 28, kChipGap = 6, kChipArrowW = 30;
// Under this the tiles leave their room to the chart and the history: the table's row of the civilization has their figures.
constexpr int kCivsMinLowH = 480;
// The chart's controls take about 150 units: under this its plot would be too short to read.
constexpr int kCivMinChartH = 230;
// The figures sit in rows of label and value, two to a row; the last one alone has a row of its own.
constexpr int kCivFigureRowH = 26, kCivFigureMinRowH = 22, kCivFigurePad = 14;

std::string CivNameOf(const View &v, const Card &c, int race)
{
	if (race == kCivAll) return Tr("All civilizations");
	for (const PublicCiv &pc : v.pub.civs) {
		if (pc.race == race && !pc.name.empty()) return pc.name;
	}
	for (const GameRow &g : c.games) {
		if (g.race == race && !g.civ.empty()) return g.civ;
	}
	return Tr("Unknown");
}

// The civilizations of the tab: those of the player's own games, or those the league has figures of.
std::vector<int> CivsOf(const View &v, const std::vector<MatchFacts> &facts)
{
	if (!v.pub.on) return MatchCivsPlayed(facts);
	std::vector<int> out;
	for (const PublicCiv &pc : v.pub.civs) out.push_back(pc.race);
	return out;
}

MatchCivRecord RecordOf(const View &v, const std::vector<MatchFacts> &facts, int race)
{
	if (!v.pub.on) return MatchCivRecordOf(facts, race);
	if (race == kCivAll) return v.pub.all;
	for (const PublicCiv &pc : v.pub.civs) {
		if (pc.race == race) return pc.record;
	}
	return MatchCivRecord();
}

std::string Rounded(double value) { return Grouped(static_cast<uint64_t>(std::max(0.0, value) + 0.5)); }

SummaryRows PublicSummaryRows(const View &v)
{
	const MatchCivRecord &all = v.pub.all;
	SummaryRows rows;
	if (!v.pub.civs.empty() && v.pub.civs.front().race >= 0 && all.games > 0) {
		const PublicCiv &fav = v.pub.civs.front();
		rows.push_back({"Favourite civilization", fav.name + " \xC2\xB7 " + Percent(PercentOf(fav.record.games, all.games))});
	}
	if (all.games > 0) rows.push_back({"Average duration", Duration(static_cast<uint32_t>(all.minutes * 60000 + 0.5))});
	if (all.ran > 0) {
		rows.push_back({"K/D", OneDecimal(all.killRatio)});
		rows.push_back({"Kills per game", Rounded(all.kills1)});
		rows.push_back({"Average damage", Rounded(all.damageDealt)});
		rows.push_back({"Gold per game", Rounded(all.gold)});
	}
	return rows;
}

// On the left the figures over the chart; on the right the table, as tall as its rows up to a few, over the history.
struct CivsLayout {
	int chipsY = 0, tilesY = 0;
	bool tiles = true;
	Box left, table, history;
};

CivsLayout CivsLayoutOf(const Area &a, const Frame &f, int civCount)
{
	CivsLayout l;
	l.chipsY = f.tilesY;
	l.tilesY = f.tilesY + kSegH + 12;
	int lowY = l.tilesY + f.tileH + kGap;
	l.tiles = f.bottom - lowY >= kCivsMinLowH;
	if (!l.tiles) lowY = l.tilesY;
	const int leftW = a.wide ? kWideLeftW : kNarrowChartW, rightX = a.x + leftW + kGap, rightW = a.w - leftW - kGap;
	const int rows = civCount ? std::min(civCount, f.head.roomy ? 4 : 3) : 2;
	const int tableH = kCardHead + kCivTableHead + rows * kCivRowH + 12;
	l.left = {a.x, lowY, leftW, f.bottom - lowY};
	l.table = {rightX, lowY, rightW, tableH};
	l.history = {rightX, lowY + tableH + kGap, rightW, f.bottom - lowY - tableH - kGap};
	return l;
}

// "All" first and then every civilization played, each chip as wide as its name. When they don't all fit, the row scrolls sideways
// between two arrows.
struct ChipStrip {
	std::vector<int> races;
	std::vector<std::string> labels;
	// Along the row, from its start.
	std::vector<int> lefts, widths;
	int contentW = 0;
	int x = 0, y = 0, w = 0, viewX = 0, viewW = 0;
	bool scrolls = false;
};

ChipStrip ChipStripOf(Painter &p, const View &v, const Card &c, const std::vector<int> &civs, int x, int y, int w)
{
	ChipStrip s;
	s.races = {kCivAll};
	s.races.insert(s.races.end(), civs.begin(), civs.end());
	p.Font(3);
	int at = 0;
	for (int race : s.races) {
		s.labels.push_back(CivNameOf(v, c, race));
		s.lefts.push_back(at);
		s.widths.push_back(RawWidth(p, s.labels.back()) + 2 * kSegPad);
		at += s.widths.back() + kChipGap;
	}
	s.contentW = at - kChipGap;
	s.x = x;
	s.y = y;
	s.w = w;
	s.scrolls = s.contentW > w;
	s.viewX = s.scrolls ? x + kChipArrowW + kChipGap : x;
	s.viewW = s.scrolls ? w - 2 * (kChipArrowW + kChipGap) : w;
	return s;
}

void PaintChipArrow(Painter &p, const View &v, int x, int y, int dir, bool enabled)
{
	const bool hot = enabled && p.Hover(x, y, kChipArrowW, kSegH, v.mouseX, v.mouseY);
	p.Well(x, y, kChipArrowW, kSegH, hot);
	const COLORREF ink = enabled ? pal::text : pal::disabled;
	const int ax = x + (kChipArrowW - 7) / 2, ay = y + (kSegH - 11) / 2;
	if (dir < 0) p.ArrowLeft(ax, ay, 7, 11, ink);
	else p.ArrowRight(ax, ay, 7, 11, ink);
	if (enabled) p.AddHit(x, y, kChipArrowW, kSegH, kActCivScroll, dir);
}

void PaintCivChips(Painter &p, const View &v, const ChipStrip &s, int shown)
{
	const int max = std::max(0, s.contentW - s.viewW), pos = ScrollClamp(v.panes.Of(kPaneCivChips), max);
	if (s.scrolls) {
		PaintChipArrow(p, v, s.x, s.y, -1, pos > 0);
		PaintChipArrow(p, v, s.x + s.w - kChipArrowW, s.y, 1, pos < max);
	}
	ClipArea clip(p, s.viewX, s.y, s.viewW, kSegH);
	// Published as a pane, the row takes the wheel; the controller drags it.
	if (p.panes && max > 0) p.panes->push_back({clip.OnScreen(), kPaneCivChips, max, s.viewW});
	p.Font(3);
	for (size_t i = 0; i < s.races.size(); i++) {
		const int bx = s.viewX - pos + s.lefts[i], bw = s.widths[i];
		if (bx + bw <= s.viewX || bx >= s.viewX + s.viewW) continue;
		const bool on = s.races[i] == shown;
		const bool hot = !on && p.Hover(bx, s.y, bw, kSegH, v.mouseX, v.mouseY);
		p.WellSegment(bx, s.y, bw, kSegH, on, hot);
		TextAsIs(p, bx + 4, s.y, bw - 8, kSegH, s.labels[i], on || hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (!on) p.AddHit(bx, s.y, bw, kSegH, kActCiv, s.races[i]);
	}
}

// The league counts the kills only of the games with figures (when figured).
void PaintCivTiles(Painter &p, const MatchCivRecord &r, const Area &a, int y, int h, bool figured)
{
	const int tileW = (a.w - 3 * kTileGap) / 4;
	const std::string values[4] = {r.games ? Grouped(r.games) : std::string(kDash), r.games ? Percent(r.winPercent) : std::string(kDash),
		r.games && (!figured || r.ran) ? OneDecimal(r.killRatio) : std::string(kDash), r.ran ? Rounded(r.damageDealt) : std::string(kDash)};
	const char *labels[4] = {"Games", "Wins", "K/D", "Average damage"};
	for (int i = 0; i < 4; i++) {
		const int x = a.x + i * (tileW + kTileGap), w = i == 3 ? a.x + a.w - x : tileW;
		PaintGlassCard(p, x, y, w, h);
		p.Font(5);
		TextAsIs(p, x, y + 6, w, h - 38, values[i], pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x + 6, y + h - 32, w - 12, 22, labels[i], pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

std::string CivFiguresNote(const View &v, const MatchCivRecord &r)
{
	// The league's figures come from the games whose report had them.
	if (v.pub.on) {
		if (r.ran >= r.games) return std::string();
		return r.ran == 1 ? TrFmt("The averages come from 1 of the %s games.", {Grouped(r.games)})
						  : TrFmt("The averages come from %s of the %s games.", {Grouped(r.ran), Grouped(r.games)});
	}
	std::string note = Tr("The averages leave out the abandoned games.");
	if (r.scriptGames) {
		note += " " + (r.scriptGames == 1 ? Tr("In 1 game the map's script counted the kills and deaths.")
										  : TrFmt("In %s games the map's script counted the kills and deaths.", {Grouped(r.scriptGames)}));
	}
	return note;
}

int CivFiguresNoteHeight(Painter &p, const View &v, const MatchCivRecord &r, int w)
{
	const std::string note = CivFiguresNote(v, r);
	if (note.empty()) return 0;
	p.Font(3);
	p.raw = true;
	const int h = p.Measure(w - kCardPad * 2, note);
	p.raw = false;
	return h;
}

// The figures of a game with the civilization. The league keeps neither the food nor the gold spent nor the best unit, and another
// player's figures that it has none of are left out.
SummaryRows CivFigureRows(const View &v, const Card &c, const MatchCivRecord &r)
{
	auto average = [&](double value) { return r.ran ? Rounded(value) : std::string(kDash); };
	if (v.pub.on) {
		SummaryRows rows;
		if (r.ran) {
			rows = {{"Kills", average(r.kills1)}, {"Losses", average(r.deaths1)}, {"Damage dealt", average(r.damageDealt)},
				{"Damage taken", average(r.damageTaken)}, {"Gold gathered", average(r.gold)}, {"Units produced", average(r.produced)}};
			if (r.army > 0) rows.push_back({"Largest army", average(r.army)});
		}
		if (r.games) rows.push_back({"Duration", Duration(static_cast<uint32_t>(r.minutes * 60000 + 0.5))});
		return rows;
	}
	std::string spent = kDash;
	if (r.goldSpentGames) {
		spent = Rounded(r.goldSpent);
		if (r.goldSpentGames < r.ran) {
			spent += " \xC2\xB7 " + (r.goldSpentGames == 1 ? Tr("in 1 game") : TrFmt("in %s games", {Grouped(r.goldSpentGames)}));
		}
	}
	std::string best = kDash;
	if (r.bestGame >= 0 && !c.games[r.bestGame].bestUnit.empty()) {
		best = TrFmt("%s \xC2\xB7 level %s", {c.games[r.bestGame].bestUnit, std::to_string(r.bestLevel)});
	}
	return {
		{"Kills", average(r.kills1)},
		{"Losses", average(r.deaths1)},
		{"Damage dealt", average(r.damageDealt)},
		{"Damage taken", average(r.damageTaken)},
		{"Gold gathered", average(r.gold)},
		{"Gold spent", spent},
		{"Food", average(r.food)},
		{"Units produced", average(r.produced)},
		{"Largest army", average(r.army)},
		{"Duration", r.ran ? Duration(static_cast<uint32_t>(r.minutes * 60000 + 0.5)) : std::string(kDash)},
		{"Best unit", best},
	};
}

int FigureRowCount(const SummaryRows &rows) { return std::max(1, static_cast<int>(rows.size() + 1) / 2); }

// What a game with the civilization averages, as tall as its rows, and under them what the averages leave out when there is room.
void PaintCivFigures(Painter &p, const View &v, const SummaryRows &rows, const MatchCivRecord &r, const Box &b, int rowH, int noteH)
{
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	PaintCardTitle(p, b.x, b.y, b.w, "Per game");
	constexpr int columns = 2, colGap = 24;
	const int count = static_cast<int>(rows.size());
	const int inner = b.w - kCardPad * 2, colW = (inner - colGap) / columns;
	if (!count) {
		p.Font(3);
		p.Text(b.x, b.y + kCardHead, b.w, b.h - kCardHead - 8, kDash, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	for (int i = 0; i < count; i++) {
		const int cx = b.x + kCardPad + (i % columns) * (colW + colGap), ry = b.y + kCardHead + (i / columns) * rowH;
		// The last one alone in its row has the whole width.
		const int cw = i == count - 1 && i % columns == 0 ? inner : colW;
		if (i >= columns) p.Fill(cx, ry, cw, 1, pal::hairline);
		p.Font(4);
		const int vw = std::min(cw * 2 / 3, RawWidth(p, rows[i].second));
		TextAsIs(p, cx + cw - vw, ry, vw, rowH, rows[i].second, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(cx, ry, cw - vw - 10, rowH, rows[i].first, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	if (noteH <= 0) return;
	const int noteY = b.y + kCardHead + FigureRowCount(rows) * rowH + 4;
	p.Font(3);
	p.raw = true;
	p.TextWrapped(b.x + kCardPad, noteY, inner, std::max(0, b.y + b.h - 6 - noteY), CivFiguresNote(v, r), kMuted);
	p.raw = false;
}

// Every civilization played side by side; a row chooses it.
void PaintCivTable(Painter &p, const View &v, const Card &c, const std::vector<MatchFacts> &facts, const std::vector<int> &civs, int shown, const Box &b)
{
	PaintGlassCard(p, b.x, b.y, b.w, b.h);
	PaintCardTitle(p, b.x, b.y, b.w, "By civilization");
	const int x0 = b.x + kCardPad, inner = b.w - kCardPad * 2, right = x0 + inner;
	if (civs.empty()) {
		p.Font(3);
		p.Text(x0, b.y + kCardHead, inner, b.h - kCardHead - 12, c.readable ? "No games recorded yet." : kDash, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const int count = static_cast<int>(civs.size());
	const int listTop = b.y + kCardHead + kCivTableHead, listBottom = b.y + b.h - 12;
	const bool scrolls = count * kCivRowH > listBottom - listTop;
	const int listRight = scrolls ? right - 12 : right;
	const int colW = std::max(70, std::min(140, (listRight - x0 - 180) / 5));
	const char *heads[5] = {"Games", "Wins", "K/D", "Average damage", "Average duration"};
	p.Font(3);
	p.Text(x0, b.y + kCardHead, listRight - 5 * colW - x0 - 8, 22, "Civilization", kHead);
	for (int k = 0; k < 5; k++) {
		p.Text(listRight - (5 - k) * colW, b.y + kCardHead, colW, 22, heads[k], kHead, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	PaneSpec spec;
	spec.id = kPaneCivs;
	spec.x = x0;
	spec.y = listTop;
	spec.w = inner + 8;
	spec.h = std::max(0, listBottom - listTop);
	spec.contentH = count * kCivRowH;
	spec.barX = right - 6;
	spec.drag = &v.bar;
	spec.mx = v.mouseX;
	spec.my = v.mouseY;
	ScrollPane pane(p, spec, v.panes.Of(kPaneCivs));
	for (int i = 0; i < count; i++) {
		const int ry = pane.Top() + i * kCivRowH, rw = listRight - x0;
		if (!pane.Shows(ry, kCivRowH)) continue;
		const int race = civs[i];
		const MatchCivRecord r = RecordOf(v, facts, race);
		const bool on = race == shown;
		const bool hot = !on && p.Hover(x0, ry, rw, kCivRowH, v.mouseX, v.mouseY);
		if (on) {
			p.Fill(x0, ry, rw, kCivRowH, kSelectedTop);
			p.Fill(x0, ry, 3, kCivRowH, pal::red);
		}
		else if (hot) p.Fill(x0, ry, rw, kCivRowH, kRowHot);
		else if (i) p.Fill(x0, ry, rw, 1, kRule);
		p.Font(4);
		TextAsIs(p, x0 + 12, ry, listRight - 5 * colW - x0 - 20, kCivRowH, CivNameOf(v, c, race), race >= 0 ? pal::text : kMuted);
		// The league has the duration of every game, and the kills of those with figures.
		const bool timed = v.pub.on ? r.games > 0 : r.ran > 0;
		const std::string cells[5] = {Grouped(r.games), Percent(r.winPercent), !v.pub.on || r.ran ? OneDecimal(r.killRatio) : std::string(kDash),
			r.ran ? Rounded(r.damageDealt) : std::string(kDash), timed ? Duration(static_cast<uint32_t>(r.minutes * 60000 + 0.5)) : std::string(kDash)};
		for (int k = 0; k < 5; k++) {
			p.Font(k == 2 ? 4 : 3);
			TextAsIs(p, listRight - (5 - k) * colW, ry, colW, kCivRowH, cells[k], k == 2 ? pal::text : pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		p.AddHit(x0, ry, rw, kCivRowH, kActCiv, race);
	}
}

void PaintCivsTab(Painter &p, const View &v, const Card &c, const Area &a, const Frame &f, Tip &tip)
{
	const std::vector<MatchFacts> facts = FactsOfCard(c);
	const std::vector<int> civs = CivsOf(v, facts);
	const int shown = ShownCiv(v);
	const MatchCivRecord r = RecordOf(v, facts, shown);
	const CivsLayout l = CivsLayoutOf(a, f, static_cast<int>(civs.size()));
	PaintCivChips(p, v, ChipStripOf(p, v, c, civs, a.x, l.chipsY, a.w), shown);
	if (l.tiles) PaintCivTiles(p, r, a, l.tilesY, f.tileH, v.pub.on);
	// The figures line up with the table when they come out close to it, and never leave the chart too short.
	const SummaryRows figures = CivFigureRows(v, c, r);
	const int figRows = FigureRowCount(figures);
	int noteH = CivFiguresNoteHeight(p, v, r, l.left.w);
	int figH = kCardHead + figRows * kCivFigureRowH + noteH + kCivFigurePad;
	if (std::abs(figH - l.table.h) <= 30 && l.table.h - kCardHead - kCivFigurePad - noteH >= figRows * (kCivFigureRowH - 2)) figH = l.table.h;
	figH = std::max(kCardHead + figRows * kCivFigureMinRowH + kCivFigurePad, std::min(figH, l.left.h - kGap - kCivMinChartH));
	if (figH - kCardHead - kCivFigurePad - noteH < figRows * kCivFigureMinRowH) noteH = 0;
	const int rowH = std::min(30, (figH - kCardHead - kCivFigurePad - noteH) / figRows);
	PaintCivFigures(p, v, figures, r, {l.left.x, l.left.y, l.left.w, figH}, rowH, noteH);
	PaintEvolution(p, v, c, {l.left.x, l.left.y + figH + kGap, l.left.w, l.left.h - figH - kGap}, tip);
	PaintCivTable(p, v, c, facts, civs, shown, l.table);
	PaintHistory(p, v, c, l.history, tip);
}

std::string DayText(int64_t atMs)
{
	const time_t t = static_cast<time_t>(atMs / 1000);
	tm local = {};
	if (atMs <= 0 || localtime_s(&local, &t) != 0) return std::string();
	char buf[32];
	sprintf_s(buf, "%02d/%02d/%04d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
	return buf;
}

std::string SignedText(int value) { return (value > 0 ? "+" : value < 0 ? "\xE2\x88\x92" : "") + Grouped(static_cast<uint64_t>(value < 0 ? -value : value)); }

// The Glory after each game of the season once placed, as a line over round steps of Glory; the queue is chosen when both were played.
// The duel in the orange of the match statistics' own line, the pairs in the league's red.
const COLORREF kGloryInk[2] = {RGB(0xe8, 0xa4, 0x5c), RGB(0xd9, 0x4a, 0x4a)};

struct GloryDot {
	float x = 0;
	int y = 0, glory = 0, game = 0;
	int64_t at = 0;
	// The Glory the player came into the window with, from a game before it: the line starts there, the dot is not a game.
	bool carried = false;
};

void PaintGloryChart(Painter &p, const View &v, int x, int y, int w, int h, Tip &tip)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kLeaguePad, iw = w - 2 * kLeaguePad;
	PaintSectionLabel(p, ix, y + 16, iw * 3 / 5, 22, "Glory over time");
	if (!v.meLoaded) {
		PaintSkeleton(p, ix, y + 64, iw, std::max(0, h - 110));
		return;
	}
	const LeagueRanked *ranks[2] = {&v.me.ranked, &v.me.ranked2v2};
	static const char *const kQueueNames[2] = {"1 vs 1", "2 vs 2"};
	const bool has[2] = {!ranks[0]->gloryHistory.empty(), !ranks[1]->gloryHistory.empty()};
	if (!has[0] && !has[1]) {
		const LeagueRanked &rk = *ranks[0];
		// A server before the history sends nothing to tell.
		if (!rk.gloryKnown) {
			p.Font(1);
			p.Text(ix, y + 52, iw, std::max(0, h - 76), kDash, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			return;
		}
		const bool placing = !rk.present || rk.placementLeft > 0;
		p.Font(3);
		p.TextWrapped(ix, y + 60, iw, std::max(0, h - 76),
			placing && !v.pub.on ? "Once your placement games are over, your Glory after each ranked game of the season shows here." : "No ranked games this season yet.", kMuted);
		return;
	}
	// A queue stays hidden only while the other one shows.
	bool shown[2] = {has[0] && !(v.gloryHidden & 1), has[1] && !(v.gloryHidden & 2)};
	if (!shown[0] && !shown[1]) shown[has[0] ? 0 : 1] = true;

	// The games are placed by their day, so that both queues share the line of time; a server that sent no days spreads them evenly.
	bool dated = true;
	int64_t first = INT64_MAX, last = 0;
	for (int q = 0; q < 2; q++) {
		if (!shown[q]) continue;
		for (const LeagueGloryPoint &pt : ranks[q]->gloryHistory) dated = dated && pt.at > 0;
		first = std::min(first, ranks[q]->gloryHistory.front().at);
		last = std::max(last, ranks[q]->gloryHistory.back().at);
	}
	constexpr int64_t kDayMs = 86400000;
	int64_t from = first, to = last;
	if (dated && v.gloryRange < 2) {
		to = std::max(last, static_cast<int64_t>(std::time(nullptr)) * 1000);
		from = std::max(first, to - (v.gloryRange == 0 ? 7 : 30) * kDayMs);
	}
	if (dated) {
		const std::vector<std::string> ranges = Translated({"7 days", "30 days", "Whole season"});
		const int segW = SegmentsWidth(p, ranges);
		PaintSegments(p, ix + iw - segW, y + 12, segW, ranges, v.gloryRange, kActGloryRange, v.mouseX, v.mouseY);
	}

	std::vector<GloryDot> series[2];
	std::vector<LeagueGloryPoint> inWindow;
	for (int q = 0; q < 2; q++) {
		if (!shown[q]) continue;
		const std::vector<LeagueGloryPoint> &all = ranks[q]->gloryHistory;
		for (int i = 0; i < static_cast<int>(all.size()); i++) {
			if (dated && all[i].at < from) {
				if (i + 1 < static_cast<int>(all.size()) && all[i + 1].at >= from) {
					GloryDot d;
					d.glory = all[i].glory;
					d.at = from;
					d.game = i;
					d.carried = true;
					series[q].push_back(d);
					inWindow.push_back({from, all[i].glory});
				}
				continue;
			}
			GloryDot d;
			d.glory = all[i].glory;
			d.at = all[i].at;
			d.game = i;
			series[q].push_back(d);
			inWindow.push_back(all[i]);
		}
	}

	// The legend says which line is which queue and what each won or lost in the window; a click hides or shows one.
	p.Font(3);
	int lx = ix;
	const int legendY = y + 42;
	const int mx = MouseX(p, v), my = MouseY(p, v);
	for (int q = 0; q < 2; q++) {
		if (!has[q]) continue;
		const std::string name = Tr(kQueueNames[q]);
		std::string change;
		int delta = 0;
		if (series[q].size() > 1) {
			delta = series[q].back().glory - series[q].front().glory;
			change = SignedText(delta);
		}
		const int nameW = p.TextWidth(name), changeW = change.empty() ? 0 : RawWidth(p, change) + 8;
		const int itemW = 22 + nameW + changeW;
		const bool toggles = has[0] && has[1];
		const bool hot = toggles && p.Hover(lx - 4, legendY - 2, itemW + 8, 22, v.mouseX, v.mouseY);
		if (shown[q]) p.Fill(lx, legendY + 8, 14, 3, kGloryInk[q]);
		else p.Frame(lx, legendY + 7, 14, 5, kGloryInk[q]);
		p.Text(lx + 22, legendY - 2, nameW + 2, 22, kQueueNames[q], shown[q] || hot ? pal::text : kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		if (!change.empty() && shown[q]) {
			TextAsIs(p, lx + 22 + nameW + 8, legendY - 2, changeW, 22, change, delta > 0 ? pal::ok : delta < 0 ? pal::bad : kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
		if (toggles) p.AddHit(lx - 4, legendY - 2, itemW + 8, 22, kActGloryQueue, q);
		lx += itemW + 24;
	}

	if (inWindow.empty()) {
		p.Font(1);
		p.Text(ix, y + 70, iw, std::max(0, h - 110), kDash, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const LeagueGloryAxis axis = LeagueGloryAxisOf(inWindow);
	p.Font(3);
	const int labelW = RawWidth(p, Grouped(static_cast<uint64_t>(axis.high))) + 12;
	const int px = ix + labelW, pw = ix + iw - px - 6, py = y + 76, ph = y + h - 40 - py;
	if (ph < 40 || pw < 40) return;
	const int span = std::max(1, axis.high - axis.low);
	for (int g = axis.low; g <= axis.high; g += std::max(1, axis.step)) {
		const int gy = py + ph - static_cast<int>(static_cast<int64_t>(ph) * (g - axis.low) / span);
		p.Fill(px, gy, pw, 1, g == axis.low ? pal::lineStrong : kRule);
		TextAsIs(p, ix, gy - 10, labelW - 10, 20, Grouped(static_cast<uint64_t>(g)), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	for (int q = 0; q < 2; q++) {
		const int n = static_cast<int>(series[q].size());
		for (int i = 0; i < n; i++) {
			GloryDot &d = series[q][i];
			if (!dated) d.x = n == 1 ? px + pw / 2.0f : px + static_cast<float>(pw) * i / (n - 1);
			else d.x = to > from ? px + static_cast<float>(pw) * static_cast<float>(d.at - from) / static_cast<float>(to - from) : px + pw / 2.0f;
			const int value = std::max(axis.low, std::min(axis.high, d.glory));
			d.y = py + ph - static_cast<int>(static_cast<int64_t>(ph) * (value - axis.low) / span);
		}
	}

	int hoveredQ = -1, hovered = -1;
	if (!p.noHover && mx >= px - 10 && mx <= px + pw + 10 && my >= py - 10 && my <= py + ph + 10) {
		float best = 0;
		for (int q = 0; q < 2; q++) {
			for (int i = 0; i < static_cast<int>(series[q].size()); i++) {
				const GloryDot &d = series[q][i];
				if (d.carried) continue;
				const float dx = d.x - mx, dy = static_cast<float>(d.y - my) / 3;
				const float dist = dx * dx + dy * dy;
				if (hovered < 0 || dist < best) {
					best = dist;
					hoveredQ = q;
					hovered = i;
				}
			}
		}
	}
	if (hovered >= 0) p.Fill(static_cast<int>(series[hoveredQ][hovered].x), py, 1, ph, pal::hairline);

	// Drawn as the match statistics draw theirs: one smooth line per queue, and a dot only where the player stands now.
	Surface surface;
	if (surface.Open(p.dc)) {
		const float width = 2.6f * p.s;
		for (int q = 0; q < 2; q++) {
			const std::vector<GloryDot> &s = series[q];
			for (size_t i = 1; i < s.size(); i++) surface.Segment(s[i - 1].x * p.s, s[i - 1].y * p.s, s[i].x * p.s, s[i].y * p.s, width, kGloryInk[q]);
			if (!s.empty() && !s.back().carried) surface.Segment(s.back().x * p.s, s.back().y * p.s, s.back().x * p.s, s.back().y * p.s, width * 2.2f, kGloryInk[q]);
		}
	}
	if (hovered >= 0) {
		const GloryDot &d = series[hoveredQ][hovered];
		const int cx = static_cast<int>(d.x + 0.5f);
		p.Ellipse(cx - 7, d.y - 7, 15, 15, pal::text);
		p.Ellipse(cx - 5, d.y - 5, 11, 11, kGloryInk[hoveredQ]);
	}

	p.Font(3);
	const int footY = y + h - 34;
	if (dated) {
		TextAsIs(p, px, footY, pw / 2, 22, DayText(from), kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		if (to > from) TextAsIs(p, px + pw / 2, footY, pw - pw / 2, 22, DayText(to), kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}

	if (hovered >= 0) {
		const GloryDot &d = series[hoveredQ][hovered];
		const std::vector<LeagueGloryPoint> &all = ranks[hoveredQ]->gloryHistory;
		std::string head = TrFmt("Game %s", {std::to_string(d.game + 1)});
		if (shown[0] && shown[1]) head = Tr(kQueueNames[hoveredQ]) + "  \xC2\xB7  " + head;
		const std::string day = DayText(d.at);
		if (!day.empty()) head += "  \xC2\xB7  " + day;
		tip.lines.push_back({head, pal::text});
		std::string line = TrFmt("%s Glory", {Grouped(static_cast<uint64_t>(d.glory))});
		COLORREF lineInk = pal::text;
		if (d.game > 0) {
			const int delta = d.glory - all[d.game - 1].glory;
			line += "  (" + SignedText(delta) + ")";
			lineInk = delta > 0 ? pal::ok : delta < 0 ? pal::bad : pal::text;
		}
		tip.lines.push_back({line, lineInk});
		tip.x = static_cast<int>(d.x) + 14;
		tip.y = d.y - 30;
	}
}

void PaintDetail(Painter &p, const View &v, const Card &c, const Frame &f, Tip &tip)
{
	const Area a = DetailArea(v);
	PaintIdentity(p, v, c, a, f);
	if (ShownTab(v) == kTabLeague) {
		PaintLeagueTab(p, v, a, f, tip);
		return;
	}
	if (ShownTab(v) == kTabCivs) {
		PaintCivsTab(p, v, c, a, f, tip);
		return;
	}
	PaintTiles(p, c, a, f);
	const Lower l = LowerLayout(a, f);
	PaintSummary(p, v, c, l.summary.x, l.summary.y, l.summary.w, l.summary.h, l.summaryColumns);
	PaintEvolution(p, v, c, l.chart, tip);
	PaintHistory(p, v, c, l.history, tip);
}

// Another player's profile while the league answers, or when it has nothing of them.
void PaintPublicPending(Painter &p, const View &v, const Card &c, const Frame &f)
{
	const Area a = DetailArea(v);
	PaintIdentity(p, v, c, a, f);
	const LeagueProfileState state = v.pub.state;
	if (state == LeagueProfileState::Loading || state == LeagueProfileState::None) {
		const int tileW = (a.w - 3 * kTileGap) / 4;
		for (int i = 0; i < 4; i++) {
			const int x = a.x + i * (tileW + kTileGap);
			PaintSkeleton(p, x, f.tilesY, i == 3 ? a.x + a.w - x : tileW, f.tileH);
		}
		const Lower l = LowerLayout(a, f);
		PaintSkeleton(p, l.summary.x, l.summary.y, l.summary.w, l.summary.h);
		PaintSkeleton(p, l.chart.x, l.chart.y, l.chart.w, l.chart.h);
		PaintSkeleton(p, l.history.x, l.history.y, l.history.w, l.history.h);
		return;
	}
	const int h = std::min(f.bottom - f.tilesY, 160);
	PaintGlassCard(p, a.x, f.tilesY, a.w, h);
	p.Font(0);
	p.TextWrapped(a.x + kLeaguePad, f.tilesY + 40, a.w - 2 * kLeaguePad, h - 60,
		state == LeagueProfileState::Missing ? "This player has no league games yet." : "Couldn't reach the league server. Check your connection and try again.",
		pal::textDim);
}

// The title with the way back at the top right and, for another player than the one in use, the button that changes to it. Another
// player's profile has only the way back, and says whose it is.
void PaintHeader(Painter &p, const View &v, const Frame &f)
{
	if (v.pub.on) {
		const int right = PaintHeaderActions(p, f.head, {{"Back", kActBack, true, true}}, v.mouseX, v.mouseY);
		const Card *c = SelectedCard(v);
		const bool ready = v.pub.state == LeagueProfileState::Ready;
		const int tab = ShownTab(v);
		p.raw = true;
		PaintScreenTitle(p, f.head, TrFmt("Profile of %s", {c && !c->name.empty() ? c->name : Tr("Player")}),
			{{Tr("General"), kActTab, kTabGeneral, tab == kTabGeneral, ready}, {Tr("Civilizations"), kActTab, kTabCivs, tab == kTabCivs, ready},
				{Tr("League"), kActTab, kTabLeague, tab == kTabLeague, ready}},
			right, v.mouseX, v.mouseY);
		p.raw = false;
		return;
	}
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
	PaintScreenTitle(p, f.head, "STATISTICS",
		{{"General", kActTab, kTabGeneral, tab == kTabGeneral, true}, {"Civilizations", kActTab, kTabCivs, tab == kTabCivs, true},
			{"League", kActTab, kTabLeague, tab == kTabLeague, SelectedIsActive(v)}},
		right, v.mouseX, v.mouseY);
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
	m.league = g.league;
	m.lost = !g.won;
	m.kills = g.kills;
	m.losses = g.losses;
	m.armyMax = g.armyMax;
	m.gold = g.gold;
	m.race = g.race;
	m.food = g.food;
	m.produced = g.produced;
	m.damageDealt = g.damageDealt;
	m.damageTaken = g.damageTaken;
	m.bestLevel = g.bestLevel;
	m.goldSpent = g.goldSpent;
	m.scriptKills = g.scriptKills;
	m.scriptDeaths = g.scriptDeaths;
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
	return MatchSelect(FactsOfCard(*c), kModeAll, static_cast<MatchOutcome>(v.outcome), 0, ShownCiv(v));
}

namespace {

Box HistoryBox(const View &v)
{
	const Area a = DetailArea(v);
	const Frame f = FrameOf(v);
	if (ShownTab(v) != kTabCivs) return LowerLayout(a, f).history;
	const Card *c = SelectedCard(v);
	return CivsLayoutOf(a, f, c ? static_cast<int>(CivsOf(v, FactsOfCard(*c)).size()) : 0).history;
}

}

int HistoryMaxScroll(const View &v)
{
	const Card *c = SelectedCard(v);
	if (!c) return 0;
	const HistoryGeom gm = GeomOf(HistoryBox(v));
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
	const HistoryGeom gm = GeomOf(HistoryBox(v));
	const int needed = gm.rowH * 2 + DetailHeight(DetailCells(c->games[game], FactsOfCard(*c), !gm.wide).size(), gm.columns);
	const int first = needed <= gm.listBottom - gm.listTop ? at - 1 : at;
	v.historyScroll = std::max(0, std::min(first, HistoryMaxScroll(v)));
}

bool CivChipSpan(Painter &p, const View &v, int &left, int &width)
{
	const Card *c = SelectedCard(v);
	if (!c || ShownTab(v) != kTabCivs) return false;
	const Area a = DetailArea(v);
	const Frame f = FrameOf(v);
	const std::vector<int> civs = CivsOf(v, FactsOfCard(*c));
	const ChipStrip s = ChipStripOf(p, v, *c, civs, a.x, CivsLayoutOf(a, f, static_cast<int>(civs.size())).chipsY, a.w);
	const auto it = std::find(s.races.begin(), s.races.end(), ShownCiv(v));
	const size_t i = it == s.races.end() ? 0 : static_cast<size_t>(it - s.races.begin());
	left = s.lefts[i];
	width = s.widths[i];
	return true;
}

int ListRows(const View &v)
{
	return FrameOf(v).rows;
}

std::vector<int> ChartModes(const View &v)
{
	if (v.pub.on) return {kModeAll, kModeLeague, kModeCup};
	return {kModeAll, kModeSkirmish, kModeMultiplayer, kModeLeague, kModeCup};
}

std::vector<int> ChartMetrics(const View &v)
{
	if (v.pub.on) return {kMetricKills, kMetricLosses, kMetricKillsPerLoss, kMetricDuration};
	return {kMetricKills, kMetricLosses, kMetricKillsPerLoss, kMetricArmy, kMetricGold, kMetricDuration};
}

bool CivKnown(const View &v, int race)
{
	if (v.pub.on) return std::any_of(v.pub.civs.begin(), v.pub.civs.end(), [&](const PublicCiv &pc) { return pc.race == race; });
	const Card *c = SelectedCard(v);
	return c && std::any_of(c->games.begin(), c->games.end(), [&](const GameRow &g) { return g.race == race; });
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
	const Card *c = SelectedCard(v);
	if (c && v.pub.on && v.pub.state != LeagueProfileState::Ready) {
		PaintPublicPending(p, v, *c, f);
	}
	else if (c) {
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
	const int colGap = 40, leftW = linked ? (d.BodyW() - colGap) / 2 : d.BodyW(), rx = d.BodyX() + leftW + colGap, rw = d.BodyW() - leftW - colGap;
	const int bodyTop = d.BodyTop() + 6, bodyH = d.FooterTop() - 12 - bodyTop;
	const LeagueLinkActions actions{kActLeagueLink, kActLeagueOpen, kActLeagueCancel, kActLeagueUnlink, kActLeagueBadge};
	auto left = [&](int y) { return PaintLeagueAccount(p, d.BodyX(), y, leftW, actions, v.mouseX, v.mouseY); };
	auto right = [&](int y) {
		if (v.meLoaded) return PaintLeagueCosmeticChoices(p, rx, y, rw, v.me, kActLeagueFrame, kActLeagueTitle, v.mouseX, v.mouseY);
		p.Font(3);
		p.Text(rx, y, rw, 24, "Reading your season rewards...", pal::caption);
		return 24;
	};
	if (linked) p.Fill(rx - colGap / 2, bodyTop, 1, bodyH - 4, pal::hairline);
	PaneSpec spec;
	spec.id = kPaneLeagueDialog;
	spec.x = d.BodyX();
	spec.y = bodyTop;
	spec.w = d.BodyW() + d.pad / 2;
	spec.h = bodyH;
	spec.contentH = std::max(MeasuredHeight(p, [&] { return left(bodyTop); }), linked ? MeasuredHeight(p, [&] { return right(bodyTop); }) : 0);
	spec.barX = d.x + d.w - d.pad / 2 - 3;
	spec.drag = &v.bar;
	spec.mx = v.mouseX;
	spec.my = v.mouseY;
	{
		ScrollPane pane(p, spec, v.panes.Of(kPaneLeagueDialog));
		left(pane.Top());
		if (linked) right(pane.Top());
	}
	DialogButtons(p, d, {{"Close", kActLeagueClose}}, v.mouseX, v.mouseY);
	PaintLeagueBadgeTip(p, v.screenW, v.screenH);
}

}
