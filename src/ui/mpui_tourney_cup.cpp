// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leaguemaps.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "game/mappreview.h"
#include "i18n/tr.h"
#include "mods/mods.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leagueui.h"
#include "ui/mpui_tourney_internal.h"
#include "ui/scrollpane.h"
#include "ui/textedit.h"

namespace mpui_tourney_impl {
namespace {

const int64_t kMinuteMs = 60000, kHourMs = 60 * kMinuteMs, kDayMs = 24 * kHourMs;
// The earliest start the form offers: the league's 20 minutes, and five more to fill the form in.
const int64_t kLeadMs = 25 * kMinuteMs;
const int kGamesChoices[] = {0, 1, 3, 5, 10, 15, 20, 30, 50, 100};
const LeagueTier kTierChoices[] = {LeagueTier::None, LeagueTier::Recruit, LeagueTier::Legionary, LeagueTier::Signifer, LeagueTier::Centurion,
	LeagueTier::Tribune, LeagueTier::Legate};
const char *const kWeekdayNames[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
const char *const kWeekdayShort[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char *const kMonthNames[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
const char *const kMonthShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool Has(const std::vector<TourneyCupProblem> &list, TourneyCupProblem p)
{
	return std::find(list.begin(), list.end(), p) != list.end();
}

// The section of the form a problem is in: 0 Format, 1 Rules and maps, 2 Calendar and requirements.
int SectionOf(TourneyCupProblem p)
{
	switch (p) {
	case TourneyCupProblem::NameShort:
	case TourneyCupProblem::NameLong:
	case TourneyCupProblem::DescriptionLong:
	case TourneyCupProblem::Places:
	case TourneyCupProblem::Minimum:
	case TourneyCupProblem::Format: return 0;
	case TourneyCupProblem::Series:
	case TourneyCupProblem::Settings:
	case TourneyCupProblem::NoMaps:
	case TourneyCupProblem::TooManyMaps:
	case TourneyCupProblem::BadMap:
	case TourneyCupProblem::MapTooLarge:
	case TourneyCupProblem::MapSeats:
	case TourneyCupProblem::PoolCount:
	case TourneyCupProblem::PoolMap: return 1;
	default: return 2;
	}
}

// The time a format takes, beside its name: "≈ 2 h 10 min" live, "up to 6 days" by rounds.
std::string EstimateText(const TourneyEstimate &e)
{
	if (e.days > 0) return TrFmt("up to %s days", {std::to_string(e.days)});
	return "\xE2\x89\x88 " + DurationText(e.minutes * kMinuteMs);
}

// "Double elimination · Bo1, final Bo3", "Swiss, 4 rounds · Bo3".
std::string FormatLine(const TourneyCupForm &f)
{
	if (f.format == TourneyFormat::Swiss) {
		return TrFmt("Swiss, %s rounds", {std::to_string(TourneySwissRounds(f.swissRounds, f.maxEntries))}) + " \xC2\xB7 " + TrFmt("Bo%s", {std::to_string(f.bestOf)});
	}
	return Tr(f.format == TourneyFormat::Double ? "Double elimination" : "Single elimination") + " \xC2\xB7 " +
		TrFmt("Bo%s, final Bo%s", {std::to_string(f.bestOf), std::to_string(f.finalBestOf)});
}

// "2 vs 2 · Live, in one evening".
std::string PaceLine(const TourneyCupForm &f)
{
	const std::string mode = Tr(f.mode == TourneyMode::Teams ? "2 vs 2" : "1 vs 1") + " \xC2\xB7 ";
	if (f.pace == TourneyPace::Live) return mode + Tr("Live, in one evening");
	return mode + RoundDaysText(f.roundDays);
}

std::string Capitalized(std::string s)
{
	if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
	return s;
}

std::string SeriesText(int bestOf)
{
	return bestOf <= 1 ? Tr("1 game") : TrFmt("Best of %s", {std::to_string(bestOf)});
}

std::string GamesText(int games)
{
	if (games <= 0) return Tr("No minimum");
	return games == 1 ? Tr("1 online game") : TrFmt("%s online games", {std::to_string(games)});
}

std::string TierCapText(LeagueTier t)
{
	return TourneyMaxTierValid(t) ? TrFmt("Up to %s", {Tr(LeagueTierName(t))}) : Tr("No limit");
}

int LocalHour(int64_t localMs)
{
	return static_cast<int>((localMs - TourneyDayOf(localMs) * kDayMs) / kHourMs);
}

int LocalMinute(int64_t localMs)
{
	return static_cast<int>((localMs - TourneyDayOf(localMs) * kDayMs) / kMinuteMs % 60);
}

// The look of the form, tighter on a short screen.
struct Look {
	bool roomy;
	int labelH, fieldH, rowGap, pad, optionH, colGap;
};

Look LookFor(bool roomy)
{
	return roomy ? Look{true, 22, 44, 24, 30, 58, 40} : Look{false, 20, 40, 18, 24, 52, 32};
}

// One frame of the form. With `measure` it is only laid out, to size the sections' cards before their fields go on them.
struct Form {
	Painter &p;
	const State &st;
	const Ui &u;
	const TourneyCupForm &f;
	const std::vector<TourneyCupProblem> &problems;
	const TourneyMineView &mine;
	Look k;
	int mx = 0, my = 0;
	bool locked = false, measure = false, editing = false;
	int64_t now = 0;
	// The pane the form scrolls in: the text fields clip on their own and take it away.
	int vx = 0, vy = 0, vw = 0, vh = 0;
};

int Label(Form &c, int x, int y, int w, const std::string &text, const std::string &value = std::string(), int most = 0)
{
	if (!c.measure) {
		PaintCaps(c.p, x, y, w - (most ? 80 : 0), c.k.labelH, text, pal::caption);
		if (most) {
			const int n = TourneyTextLength(TourneyTrim(value));
			c.p.Font(3);
			c.p.Text(x + w - 80, y, 80, c.k.labelH, std::to_string(n) + "/" + std::to_string(most), n > most ? pal::bad : pal::caption,
				DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
	}
	return c.k.labelH + 8;
}

// What is wrong with a field, in red under it; returns how much room it took.
int Problem(Form &c, int x, int y, int w, std::initializer_list<TourneyCupProblem> which)
{
	for (TourneyCupProblem q : which) {
		if (!Has(c.problems, q)) continue;
		if (!c.measure) {
			c.p.Font(3);
			c.p.Text(x, y + 6, w, 20, CupProblemText(q), pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		return 28;
	}
	return 0;
}

// A line of help under a field, wrapped; returns its height.
int Hint(Form &c, int x, int y, int w, const std::string &text, COLORREF ink = pal::caption)
{
	c.p.Font(3);
	const int h = c.p.Measure(w, text);
	if (!c.measure) c.p.TextWrapped(x, y, w, h + 4, text, ink);
	return h;
}

struct Seg {
	std::string label;
	int arg = 0;
	bool on = false, enabled = true, soon = false;
	// Its share of the row against the others' (1 each by default).
	int weight = 1;
};

// A row of choices, the one on filled; the ones that are coming carry "Coming soon" under their name.
void Segments(Form &c, int x, int y, int w, int h, const std::vector<Seg> &segs)
{
	if (c.measure || segs.empty()) return;
	Painter &p = c.p;
	const int n = static_cast<int>(segs.size()), gap = 2;
	int weights = 0;
	for (const Seg &s : segs) weights += std::max(1, s.weight);
	int cx = x;
	for (int i = 0; i < n; i++) {
		const Seg &s = segs[static_cast<size_t>(i)];
		const int cw = i + 1 == n ? x + w - cx : (w - gap * (n - 1)) * std::max(1, s.weight) / weights;
		const bool enabled = s.enabled && !c.locked, hot = enabled && !s.on && p.Hover(cx, y, cw, h, c.mx, c.my);
		p.WellSegment(cx, y, cw, h, s.on, hot);
		const COLORREF ink = s.on ? (c.locked ? pal::textDim : pal::text) : !enabled ? pal::disabled : hot ? pal::text : pal::textDim;
		UseFace(p, BrandFace::UiStrong, 15);
		if (s.soon) {
			p.Text(cx + 4, y + 3, cw - 8, h / 2 + 2, s.label, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			UseFace(p, BrandFace::UiStrong, 10);
			p.TextCaps(cx + 4, y + h / 2 + 1, cw - 8, h / 2 - 5, Tr("Coming soon"), pal::warn, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		}
		else p.Text(cx + 4, y, cw - 8, h, s.label, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (enabled && !s.on) p.AddHit(cx, y, cw, h, kTourney, s.arg);
		cx += cw + gap;
	}
}

// A choice of a list with a radio, its name and a line under it, and at the right an estimate (in `asideInk` when it warns) or "Coming soon".
// The format's sign, 40 by 28: the players as solid slots joined by two-pixel lines, the winner's slot in the accent colour. Solid
// shapes on whole pixels stay sharp at every scale, where the mockup's thin strokes blurred and ran together.
void FormatIcon(Painter &p, int x, int y, TourneyFormat format, COLORREF ink, COLORREF back, COLORREF accent)
{
	const auto blend = [](COLORREF a, COLORREF b, int percent) {
		const auto channel = [&](int shift) { return static_cast<BYTE>((((a >> shift) & 0xff) * percent + ((b >> shift) & 0xff) * (100 - percent)) / 100); };
		return RGB(channel(0), channel(8), channel(16));
	};
	const COLORREF line = blend(ink, back, 55), faint = blend(ink, back, 35);
	const auto slot = [&](int sx, int sy, int w, COLORREF c) { p.Fill(x + sx, y + sy, w, 4, c); };
	// Two slots centred on rows a and b, ending at column `from`, joined into one that starts at `to`.
	const auto join = [&](int from, int a, int b, int to, COLORREF c) {
		const int bend = from + (to - from) / 2, mid = (a + b) / 2;
		p.Fill(x + from, y + a - 1, bend - from, 2, c);
		p.Fill(x + from, y + b - 1, bend - from, 2, c);
		p.Fill(x + bend, y + a - 1, 2, b - a + 2, c);
		p.Fill(x + bend, y + mid - 1, to - bend, 2, c);
	};
	static const int kSwissWins[] = {3, 2, 1, 0};
	switch (format) {
	case TourneyFormat::Double:
		// The winners' bracket above; the losers', fainter, below, and the line by which who loses above drops into it.
		for (int sy : {1, 7}) slot(0, sy, 10, ink);
		join(10, 3, 9, 15, line);
		slot(15, 4, 9, ink);
		for (int sy : {17, 23}) slot(0, sy, 10, faint);
		join(10, 19, 25, 15, faint);
		slot(15, 20, 9, faint);
		p.Fill(x + 19, y + 9, 2, 10, faint);
		join(24, 6, 22, 30, line);
		slot(30, 12, 10, accent);
		break;
	case TourneyFormat::Swiss:
		// A table of points: each player's slot and three rounds, won in full colour and lost faint; nobody leaves it.
		for (int row = 0; row < 4; ++row) {
			const int ry = 1 + row * 7;
			slot(0, ry, 16, row == 0 ? accent : ink);
			for (int round = 0; round < 3; ++round) p.Fill(x + 21 + round * 7, y + ry, 5, 4, round < kSwissWins[row] ? ink : faint);
		}
		break;
	default:
		// Four players, two, one.
		for (int sy : {1, 7, 17, 23}) slot(0, sy, 10, ink);
		join(10, 3, 9, 15, line);
		join(10, 19, 25, 15, line);
		slot(15, 4, 9, ink);
		slot(15, 20, 9, ink);
		join(24, 6, 22, 30, line);
		slot(30, 12, 10, accent);
		break;
	}
}

void Option(Form &c, int x, int y, int w, int h, bool on, bool enabled, const std::string &title, const std::string &text, const std::string &aside, bool soon,
	int arg, COLORREF asideInk = CLR_INVALID, int format = -1)
{
	if (c.measure) return;
	Painter &p = c.p;
	enabled = enabled && !c.locked;
	const bool hot = enabled && !on && p.Hover(x, y, w, h, c.mx, c.my);
	p.Fill(x, y, w, h, on ? pal::selected : pal::raised);
	p.Frame(x, y, w, h, on ? (c.locked ? pal::edgeHot : pal::red) : hot ? pal::edgeHot : pal::hairline);
	const int r = 18, rx = x + 16, ry = y + (h - r) / 2;
	const COLORREF ring = !enabled && !on ? pal::disabled : on ? pal::redText : pal::edgeHot;
	p.Shape(rx, ry, r, r, InUnitEllipse, ring);
	p.Shape(rx + 2, ry + 2, r - 4, r - 4, InUnitEllipse, on ? pal::selected : pal::raised);
	if (on) p.Shape(rx + 5, ry + 5, r - 10, r - 10, InUnitEllipse, pal::redText);
	int right = x + w - 14;
	if (soon) {
		UseFace(p, BrandFace::UiStrong, 11);
		const std::string tag = Tr("Coming soon");
		const int tw = p.CapsWidth(tag, 2) + 16;
		right -= tw;
		p.Frame(right, y + (h - 22) / 2, tw, 22, pal::warn);
		p.TextCaps(right, y + (h - 22) / 2, tw, 22, tag, pal::warn, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
		right -= 12;
	}
	else if (!aside.empty()) {
		p.Font(3);
		const int aw = p.TextWidth(aside) + 4;
		right -= aw;
		p.Text(right, y, aw, h, aside, asideInk != CLR_INVALID ? asideInk : on ? pal::text : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		right -= 12;
	}
	int tx = rx + r + 14;
	if (format >= 0) {
		const COLORREF ink = on ? pal::text : enabled ? pal::body : pal::textDim;
		FormatIcon(p, tx, y + (h - 28) / 2, static_cast<TourneyFormat>(format), ink, on ? pal::selected : pal::raised, on ? pal::redText : ink);
		tx += 40 + 14;
	}
	const int tw = right - tx;
	UseFace(p, BrandFace::UiStrong, 16);
	p.Text(tx, y + h / 2 - 22, tw, 22, title, enabled || on ? pal::text : pal::textDim, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(tx, y + h / 2 + 1, tw, 20, text, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (enabled && !on) p.AddHit(x, y, w, h, kTourney, arg);
}

// A switch with its name; returns its height.
int Switch(Form &c, int x, int y, int w, bool on, const std::string &label, int setting)
{
	const int h = 36;
	if (c.measure) return h;
	Painter &p = c.p;
	const bool enabled = !c.locked, hot = enabled && p.Hover(x, y, w, h, c.mx, c.my);
	PaintBigToggle(p, x, y + (h - kBigToggleH) / 2, on, enabled, hot);
	p.Font(0);
	p.Text(x + kBigToggleW + 14, y, w - kBigToggleW - 14, h, label, enabled ? (hot ? pal::text : pal::body) : pal::disabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (enabled) p.AddHit(x, y, w, h, kTourney, CupArg(setting, on ? 0 : 1));
	return h;
}

void Combo(Form &c, int x, int y, int w, const std::string &text, int menu)
{
	if (c.measure) return;
	const bool open = c.st.dropOpen && c.st.dropKind == kDropTourney && c.st.dropIndex == menu;
	PaintCombo(c.p, x, y, w, c.k.fieldH, text, open, !c.locked, c.mx, c.my, kTourney, ArgOf(kSubCupMenu, menu), &g_dropAnchor);
}

void Field(Form &c, int x, int y, int w, const std::string &text, const std::string &placeholder, int field, int action, bool enabled)
{
	if (c.measure) return;
	Painter &p = c.p;
	if (!enabled) {
		p.Fill(x, y, w, c.k.fieldH, pal::raised);
		p.Frame(x, y, w, c.k.fieldH, pal::hairline);
		p.Font(0);
		p.Text(x + 8, y, w - 16, c.k.fieldH, text, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	PaintEditField(p, x, y, w, c.k.fieldH, text, placeholder, c.st.focus == field, false, c.st.edit, action);
	p.Clip(c.vx, c.vy, c.vw, c.vh);
}

// "1 Format": the section's number in red and its name, over a hairline.
int Step(Form &c, int x, int y, int w, int number, const std::string &title)
{
	if (!c.measure) {
		Painter &p = c.p;
		UseDisplay(p, 30);
		p.Text(x, y, 26, 36, std::to_string(number), pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		UseDisplay(p, 28);
		p.TextCaps(x + 28, y, w - 28, 36, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.Fill(x, y + 46, w, 1, pal::hairline);
	}
	return 64;
}

// --- 1 Format -------------------------------------------------------------------------------------------------------------------

int FormatLeft(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	int at = y;
	at += Label(c, x, at, w, Tr("Name"), f.name, kTourneyCupNameMax);
	Field(c, x, at, w, f.name, TrFmt("%s's cup", {c.st.player}), kCupNameField, kFieldCupName, !c.locked);
	at += c.k.fieldH;
	at += Problem(c, x, at, w, {TourneyCupProblem::NameShort, TourneyCupProblem::NameLong}) + c.k.rowGap;
	at += Label(c, x, at, w, Tr("Description (optional)"), f.description, kTourneyCupDescriptionMax);
	Field(c, x, at, w, f.description, Tr("Rules, a date to remember, who it is for..."), kCupDescriptionField, kFieldCupDescription, true);
	at += c.k.fieldH;
	at += Problem(c, x, at, w, {TourneyCupProblem::DescriptionLong}) + c.k.rowGap;
	const bool teams = f.mode == TourneyMode::Teams;
	at += Label(c, x, at, w, Tr("Mode"));
	Segments(c, x, at, w, c.k.fieldH, {{Tr("1 vs 1"), CupArg(kCupMode, 0), !teams}, {Tr("2 vs 2"), CupArg(kCupMode, 1), teams}});
	at += c.k.fieldH;
	if (teams) {
		at += c.k.rowGap;
		at += Label(c, x, at, w, Tr("Teams"));
		Segments(c, x, at, w, c.k.fieldH, {{Tr("Chosen by the players"), CupArg(kCupTeamDraw, 0), !f.teamDraw}, {Tr("Drawn"), CupArg(kCupTeamDraw, 1), f.teamDraw}});
		at += c.k.fieldH + 8;
		at += Hint(c, x, at, w,
			Tr(f.teamDraw ? "Everyone signs up alone. When the check-in closes the teams are drawn, the best player with the weakest, so they come out even. The maps need room for four players."
						  : "Each team is a captain and the partner they invite, a Steam friend. The maps need room for four players."));
	}
	at += c.k.rowGap;
	at += Label(c, x, at, w, Tr(teams ? "Places (teams)" : "Places"));
	Segments(c, x, at, w, c.k.fieldH,
		{{"4", CupArg(kCupPlaces, 4), f.maxEntries == 4}, {"8", CupArg(kCupPlaces, 8), f.maxEntries == 8}, {"16", CupArg(kCupPlaces, 16), f.maxEntries == 16},
			{"32", CupArg(kCupPlaces, 32), f.maxEntries == 32}});
	at += c.k.fieldH + 8;
	at += Hint(c, x, at, w,
		TrFmt(teams && f.teamDraw ? "Twice as many players sign up. It starts with the teams drawn among those who have checked in, if there are at least %s; else it is cancelled on its own."
			: teams ? "It starts with the teams that have checked in, if there are at least %s; else it is cancelled on its own."
					: "It starts with the players who have checked in, if there are at least %s; else it is cancelled on its own.",
			{std::to_string(f.minEntries)}));
	at += Problem(c, x, at, w, {TourneyCupProblem::Places, TourneyCupProblem::Minimum});
	return at - y;
}

int FormatRight(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	int at = y;
	at += Label(c, x, at, w, TrFmt(f.mode == TourneyMode::Teams ? "Format \xC2\xB7 estimated time with %s teams" : "Format \xC2\xB7 estimated time with %s players",
		{std::to_string(f.maxEntries)}));
	struct Def {
		TourneyFormat format;
		const char *title, *text;
	};
	static const Def kDefs[] = {{TourneyFormat::Single, "Single elimination", "Whoever loses is out. The shortest and the easiest to follow."},
		{TourneyFormat::Double, "Double elimination", "You are out after losing twice."},
		{TourneyFormat::Swiss, "Swiss", "Nobody is out: rivals with your points."}};
	for (const Def &d : kDefs) {
		TourneyCupForm as = f;
		as.format = d.format;
		Option(c, x, at, w, c.k.optionH, f.format == d.format, true, Tr(d.title), Tr(d.text), EstimateText(TourneyEstimateOf(as)), false,
			CupArg(kCupFormat, static_cast<int>(d.format)), TourneyCupLongLive(as) ? pal::warn : CLR_INVALID, static_cast<int>(d.format));
		at += c.k.optionH + 8;
		if (d.format != TourneyFormat::Swiss || f.format != TourneyFormat::Swiss) continue;
		// The Swiss's rounds, under its choice.
		const int ix = x + 32, iw = w - 32, automatic = TourneySwissRounds(0, f.maxEntries);
		at += 6;
		at += Label(c, ix, at, iw, Tr("Rounds of the Swiss"));
		std::vector<Seg> segs = {{TrFmt("Automatic \xC2\xB7 %s", {std::to_string(automatic)}), CupArg(kCupSwissRounds, 0), f.swissRounds == 0, true, false, 2}};
		for (int r = kTourneySwissRoundsMin; r <= kTourneySwissRoundsMax; r++) segs.push_back({std::to_string(r), CupArg(kCupSwissRounds, r), f.swissRounds == r});
		Segments(c, ix, at, iw, c.k.fieldH, segs);
		at += c.k.fieldH + 8;
		at += Hint(c, ix, at, iw,
			TrFmt(f.mode == TourneyMode::Teams ? "Automatic: %s rounds with %s teams. More rounds make a fairer ranking and a longer cup."
											   : "Automatic: %s rounds with %s players. More rounds make a fairer ranking and a longer cup.",
				{std::to_string(automatic), std::to_string(f.maxEntries)}));
		at += Problem(c, ix, at, iw, {TourneyCupProblem::Format}) + 8;
	}
	if (TourneyCupLongLive(f)) {
		at += Hint(c, x, at, w,
			TrFmt("Live, this double elimination lasts about %s: more than one evening. Choose the by-rounds pace in section 3, or fewer places.",
				{DurationText(TourneyEstimateOf(f).minutes * kMinuteMs)}),
			pal::warn);
		at += 8;
	}
	return at - y;
}

// --- 2 Rules and maps -----------------------------------------------------------------------------------------------------------

int RulesLeft(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	Painter &p = c.p;
	int at = y;
	at += Label(c, x, at, w, Tr("Content"));
	std::string key = f.contentKey, name = f.contentName;
	if (!c.editing) OwnCupContent(key, name);
	const std::string title = key.empty() ? Tr("The mod's rules") : name.empty() ? Tr("Its own content") : name;
	const std::string text = c.editing ? Tr("The content the cup was made with: it stays the same when the cup changes.")
		: key.empty()				  ? Tr("No content pack changes the rules: anyone can play without downloading anything.")
									  : Tr("Your active content profile: only players with it active can check in.");
	p.Font(3);
	const int textH = p.Measure(w - 32, text), boxH = 14 + 24 + 4 + textH + 14;
	if (!c.measure) {
		p.Fill(x, at, w, boxH, pal::raised);
		p.Frame(x, at, w, boxH, pal::hairline);
		UseFace(p, BrandFace::UiStrong, 16);
		p.Text(x + 16, at + 14, w - 32, 24, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.TextWrapped(x + 16, at + 42, w - 32, textH + 4, text, pal::caption);
	}
	at += boxH + c.k.rowGap;
	const TourneyRoomRules r = TourneyRulesOf(f.settings);
	at += Label(c, x, at, w, Tr("Game settings"));
	Segments(c, x, at, w, c.k.fieldH,
		{{Tr("Competitive game"), CupArg(kCupPreset, 0), r.preset == TourneyPreset::Competitive}, {Tr("Classic game"), CupArg(kCupPreset, 1), r.preset == TourneyPreset::Classic},
			{Tr("Varied game"), CupArg(kCupPreset, 2), r.preset == TourneyPreset::Various}});
	at += c.k.fieldH + 8;
	at += Hint(c, x, at, w, PresetSummary(r.preset) + ". " + Tr("The switches below change it.")) + 14;
	at += Switch(c, x, at, w, r.hideCivs, Tr("Hide the civilizations until the game starts"), kCupHideCivs) + 4;
	at += Switch(c, x, at, w, !r.fog, Tr("No fog of war"), kCupFog) + 4;
	at += Switch(c, x, at, w, r.explored, Tr("Map explored from the start"), kCupExplored) + 4;
	at += Switch(c, x, at, w, r.lockedDiplomacy, Tr("Diplomacy locked"), kCupDiplomacy) + 4;
	at += Switch(c, x, at, w, r.timers, Tr("Respawn timers"), kCupTimers);
	at += Problem(c, x, at, w, {TourneyCupProblem::Settings});
	return at - y;
}

int RulesRight(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	int at = y;
	const int delay = TourneySpecDelay(f.settings);
	at += Label(c, x, at, w, Tr("Spectators"));
	Segments(c, x, at, w, c.k.fieldH,
		{{Tr("None"), CupArg(kCupSpectators, 0), delay == 0}, {"30 s", CupArg(kCupSpectators, 30), delay == 30}, {"60 s", CupArg(kCupSpectators, 60), delay == 60}, {"120 s", CupArg(kCupSpectators, 120), delay == 120},
			{"300 s", CupArg(kCupSpectators, 300), delay == 300}});
	at += c.k.fieldH + 8;
	at += Hint(c, x, at, w, delay ? TrFmt("Anyone can watch the games %s s behind, so nobody can tell the players what they see.", {std::to_string(delay)})
								 : Tr("Nobody can watch the games."));
	at += c.k.rowGap;
	at += Label(c, x, at, w, Tr("Rounds"));
	Segments(c, x, at, w, c.k.fieldH, {{SeriesText(1), CupArg(kCupBestOf, 1), f.bestOf == 1}, {SeriesText(3), CupArg(kCupBestOf, 3), f.bestOf == 3}});
	at += c.k.fieldH + c.k.rowGap;
	if (f.format == TourneyFormat::Swiss) {
		at += Label(c, x, at, w, Tr("Final"));
		at += Hint(c, x, at, w, Tr("A Swiss has no final: whoever has the most points after the last round is the champion."));
	}
	else {
		at += Label(c, x, at, w, Tr(f.format == TourneyFormat::Double ? "Grand final" : "Final"));
		Segments(c, x, at, w, c.k.fieldH,
			{{SeriesText(1), CupArg(kCupFinal, 1), f.finalBestOf == 1}, {SeriesText(3), CupArg(kCupFinal, 3), f.finalBestOf == 3},
				{SeriesText(5), CupArg(kCupFinal, 5), f.finalBestOf == 5}});
		at += c.k.fieldH;
		if (f.format == TourneyFormat::Double) {
			at += 8;
			at += Hint(c, x, at, w, Tr("If whoever comes from the losers' bracket wins it, a second final is played, with as many games."));
		}
	}
	at += Problem(c, x, at, w, {TourneyCupProblem::Series}) + c.k.rowGap;
	at += Label(c, x, at, w, Tr("Pairings"));
	at += Hint(c, x, at, w, Tr("Who meets whom in the first round.")) + 8;
	if (f.mode == TourneyMode::Teams) {
		Option(c, x, at, w, c.k.optionH, !f.randomSeeding, true, Tr("By 2v2 Glory"), Tr("The average of both players' 2v2 Glory, or 1v1 without it."), Tr("Recommended"),
			false, CupArg(kCupSeeding, 0));
	}
	else {
		Option(c, x, at, w, c.k.optionH, !f.randomSeeding, true, Tr("By Glory"), Tr("The best of the ranked leaderboard don't meet early."), Tr("Recommended"), false,
			CupArg(kCupSeeding, 0));
	}
	at += c.k.optionH + 8;
	Option(c, x, at, w, c.k.optionH, f.randomSeeding, true, Tr("At random"), Tr("The bracket is drawn by lot."), std::string(), false, CupArg(kCupSeeding, 1));
	at += c.k.optionH;
	return at - y;
}

// The maps of the series: a tile each, which opens the chooser, with a cross that takes it out, and a tile to add one while there are
// fewer than five.
int PoolBlock(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	Painter &p = c.p;
	int at = y;
	const int n = static_cast<int>(f.seriesPool.size());
	if (!c.measure) {
		PaintCaps(p, x, at, w - 80, c.k.labelH, Tr("Maps of the series (optional)"), pal::caption);
		p.Font(3);
		p.Text(x + w - 80, at, 80, c.k.labelH, std::to_string(n) + "/" + std::to_string(kTourneyPoolMax), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	at += c.k.labelH + 8;
	const int cols = w >= 900 ? 3 : 2, gap = 12, tileW = (w - gap * (cols - 1)) / cols, tileH = c.k.roomy ? 64 : 58;
	const int tiles = std::min(kTourneyPoolMax, n + (c.locked ? 0 : 1));
	for (int i = 0; i < tiles && !c.measure; i++) {
		const int tx = x + (i % cols) * (tileW + gap), ty = at + (i / cols) * (tileH + gap);
		if (i == n) {
			const bool hot = p.Hover(tx, ty, tileW, tileH, c.mx, c.my);
			if (hot) p.Fill(tx, ty, tileW, tileH, pal::raised);
			PaintDashedFrame(p, tx, ty, tileW, tileH, hot ? pal::text : pal::edgeHot);
			UseFace(p, BrandFace::UiStrong, 15);
			p.Text(tx + 8, ty, tileW - 16, tileH, "+ " + Tr("Add a map"), hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.AddHit(tx, ty, tileW, tileH, kTourney, ArgOf(kSubPoolOpen, i));
			continue;
		}
		const TourneyMap &m = f.seriesPool[static_cast<size_t>(i)];
		const bool enabled = !c.locked;
		const int crossW = enabled ? 40 : 0;
		const bool hot = enabled && p.Hover(tx, ty, tileW - crossW, tileH, c.mx, c.my), hotCross = enabled && p.Hover(tx + tileW - crossW, ty, crossW, tileH, c.mx, c.my);
		p.Fill(tx, ty, tileW, tileH, hot ? pal::selected : pal::field);
		p.Frame(tx, ty, tileW, tileH, hot || hotCross ? pal::edgeHot : pal::edge);
		const int thumb = tileH - 16;
		PaintMapPicture(p, m, tx + 8, ty + 8, thumb);
		const int textX = tx + 8 + thumb + 12, textW = tx + tileW - crossW - 8 - textX;
		std::string sub = Tr("Random map");
		if (m.kind == TourneyMapKind::Game) sub = Tr("Scenario of the game");
		else if (m.kind == TourneyMapKind::Upload) sub = TrFmt("Your scenario \xC2\xB7 %s", {HumanBytes(static_cast<uint64_t>(std::max<int64_t>(0, m.bytes)))});
		UseFace(p, BrandFace::UiStrong, 15);
		p.Text(textX, ty + tileH / 2 - 22, textW, 21, MapText(m), pal::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(textX, ty + tileH / 2 + 1, textW, 20, sub, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!enabled) continue;
		const int cx = tx + tileW - crossW / 2, cy = ty + tileH / 2;
		const COLORREF ink = hotCross ? pal::redText : pal::caption;
		p.Line(cx - 6, cy - 6, cx + 6, cy + 6, ink, 2);
		p.Line(cx + 6, cy - 6, cx - 6, cy + 6, ink, 2);
		p.AddHit(tx, ty, tileW - crossW, tileH, kTourney, ArgOf(kSubPoolOpen, i));
		p.AddHit(tx + tileW - crossW, ty, crossW, tileH, kTourney, ArgOf(kSubPoolRemove, i));
	}
	const int rows = (tiles + cols - 1) / cols;
	at += rows ? rows * (tileH + gap) - gap : 0;
	at += Problem(c, x, at, w, {TourneyCupProblem::PoolCount, TourneyCupProblem::PoolMap});
	at += 10;
	at += Hint(c, x, at, w,
		Tr("Without them, each game of a series is played on the round's map. With 3 to 5, whoever loses a game chooses the next one among those not "
		   "played yet in the series: they have 60 s, or it is drawn."));
	return at - y;
}

int MapsBlock(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	Painter &p = c.p;
	int at = y;
	at += Label(c, x, at, w, Tr("Maps: one for each round"));
	const int rowH = c.k.roomy ? 64 : 58, stageW = std::min(200, w / 4);
	uint64_t upload = 0;
	std::vector<std::string> counted;
	const auto count = [&upload, &counted](const TourneyMap &m) {
		if (m.kind != TourneyMapKind::Upload || std::find(counted.begin(), counted.end(), m.hash) != counted.end()) return;
		counted.push_back(m.hash);
		upload += static_cast<uint64_t>(std::max<int64_t>(0, m.bytes));
	};
	if (TourneyCupHasSeries(f)) {
		for (const TourneyMap &m : f.seriesPool) count(m);
	}
	for (int i = 0; i < static_cast<int>(f.maps.size()); i++) {
		const TourneyMap &m = f.maps[static_cast<size_t>(i)];
		count(m);
		if (!c.measure) {
			UseFace(p, BrandFace::UiStrong, 15);
			p.Text(x, at, stageW - 12, rowH, CupRoundName(f, i), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			const int bx = x + stageW, bw = w - stageW;
			const bool enabled = !c.locked;
			const bool hot = enabled && p.Hover(bx, at, bw, rowH, c.mx, c.my);
			p.Fill(bx, at, bw, rowH, hot ? pal::selected : pal::field);
			p.Frame(bx, at, bw, rowH, hot ? pal::edgeHot : pal::edge);
			const int thumb = rowH - 16;
			PaintMapPicture(p, m, bx + 8, at + 8, thumb);
			const std::string change = Tr("Change");
			UseFace(p, BrandFace::UiStrong, 12);
			const int cw = enabled ? p.CapsWidth(change, 2) + 20 : 0;
			if (enabled) p.TextCaps(bx + bw - cw - 8, at, cw, rowH, change, hot ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
			const int tx = bx + 8 + thumb + 14, tw = bx + bw - cw - 16 - tx;
			std::string title, sub;
			if (!m.Scenario()) {
				title = MapText(m);
				sub = Tr("Random map: a new one every game, nothing to download");
			}
			else if (m.kind == TourneyMapKind::Game) {
				title = m.title;
				sub = TrFmt("Scenario of the game \xC2\xB7 %s players \xC2\xB7 everyone has it", {std::to_string(m.players)});
			}
			else {
				title = m.title;
				sub = TrFmt("Your scenario \xC2\xB7 %s players \xC2\xB7 %s, uploaded when you publish", {std::to_string(m.players), HumanBytes(static_cast<uint64_t>(m.bytes))});
			}
			// The places past the match's players stay closed in the room, with no AI.
			const int closed = m.Scenario() ? m.players - 2 * TourneySidePlayers(f.mode) : 0;
			if (closed > 0) sub += " \xC2\xB7 " + TrFmt("%s places closed", {std::to_string(closed)});
			UseFace(p, BrandFace::UiStrong, 16);
			p.Text(tx, at + rowH / 2 - 23, tw, 22, title, pal::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(3);
			p.Text(tx, at + rowH / 2 + 2, tw, 20, sub, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (enabled) p.AddHit(bx, at, bw, rowH, kTourney, ArgOf(kSubMapOpen, i));
		}
		at += rowH + 8;
	}
	at -= 8;
	at += Problem(c, x, at, w, {TourneyCupProblem::NoMaps, TourneyCupProblem::TooManyMaps, TourneyCupProblem::BadMap, TourneyCupProblem::MapTooLarge});
	at += 10;
	at += Hint(c, x, at, w,
		upload ? TrFmt("Your scenarios (%s) go up to the league when you publish, and each player downloads them on their own when signing up.", {HumanBytes(upload)})
			   : Tr("A random map, one of the game's scenarios or one of yours: yours go up to the league and each player downloads them on their own."));
	if (TourneyCupHasSeries(f)) {
		at += c.k.rowGap;
		if (!c.measure) p.Fill(x, at, w, 1, pal::hairline);
		at += c.k.rowGap;
		at += PoolBlock(c, x, at, w);
	}
	return at - y;
}

// --- 3 Calendar and requirements ------------------------------------------------------------------------------------------------

int CalendarLeft(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	Painter &p = c.p;
	int at = y;
	int64_t lo = 0, hi = 0;
	CupStartBounds(c.now, lo, hi);
	const int64_t today = TourneyDayOf(LocalMs(c.now)), chosen = TourneyDayOf(LocalMs(f.startsAt)), last = TourneyDayOf(hi);
	const TourneyCalendar cal = TourneyCalendarOf(today, last);
	int y0 = 0, m0 = 0, d0 = 0, y1 = 0, m1 = 0, d1 = 0;
	TourneyCivilDate(today, y0, m0, d0);
	TourneyCivilDate(last, y1, m1, d1);
	const std::string months = m0 == m1 ? Tr(kMonthNames[m0 - 1]) + " " + std::to_string(y0)
		: y0 == y1				  ? Tr(kMonthNames[m0 - 1]) + " \xE2\x80\x93 " + Tr(kMonthNames[m1 - 1]) + " " + std::to_string(y1)
								  : Tr(kMonthNames[m0 - 1]) + " " + std::to_string(y0) + " \xE2\x80\x93 " + Tr(kMonthNames[m1 - 1]) + " " + std::to_string(y1);
	if (!c.measure) {
		PaintCaps(p, x, at, w / 2, c.k.labelH, Tr("Day"), pal::caption);
		p.Font(3);
		p.Text(x + w / 2, at, w / 2, c.k.labelH, Capitalized(months), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	at += c.k.labelH + 8;
	const int gap = 4, cellW = (w - 6 * gap) / 7, cellH = c.k.roomy ? 42 : 38, headH = 24;
	if (!c.measure) {
		for (int d = 0; d < 7; d++) {
			UseFace(p, BrandFace::UiStrong, 11);
			p.TextCaps(x + d * (cellW + gap), at, cellW, headH, Tr(kWeekdayShort[d]), d >= 5 ? pal::textDim : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		}
		for (int i = 0; i < cal.rows * 7; i++) {
			const int64_t day = cal.first + i;
			const int cx = x + (i % 7) * (cellW + gap), cy = at + headH + 4 + (i / 7) * (cellH + gap);
			const bool open = TourneyDayOpen(day, lo, hi), on = day == chosen, enabled = open && !c.locked && !on;
			const bool hot = enabled && p.Hover(cx, cy, cellW, cellH, c.mx, c.my);
			int yy = 0, mm = 0, dd = 0;
			TourneyCivilDate(day, yy, mm, dd);
			if (on) p.Fill(cx, cy, cellW, cellH, c.locked ? pal::button : pal::red);
			else if (open) {
				p.Fill(cx, cy, cellW, cellH, hot ? pal::button : pal::field);
				if (hot) p.Frame(cx, cy, cellW, cellH, pal::edgeHot);
			}
			const COLORREF ink = on ? (c.locked ? pal::text : pal::onRed) : open ? (hot ? pal::text : pal::body) : pal::disabled;
			UseFace(p, on ? BrandFace::UiStrong : BrandFace::Ui, 16);
			const std::string number = std::to_string(dd);
			if (dd == 1 && open) {
				p.Text(cx, cy + 2, cellW, cellH / 2 + 4, number, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				UseFace(p, BrandFace::UiStrong, 10);
				p.TextCaps(cx, cy + cellH / 2 + 3, cellW, cellH / 2 - 6, Tr(kMonthShort[mm - 1]), on ? ink : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
			}
			else p.Text(cx, cy, cellW, cellH, number, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (day == today) p.Fill(cx + cellW / 2 - 8, cy + cellH - 6, 16, 2, on ? ink : pal::redText);
			if (enabled) p.AddHit(cx, cy, cellW, cellH, kTourney, CupArg(kCupDay, i));
		}
	}
	at += headH + 4 + cal.rows * (cellH + gap) - gap + c.k.rowGap;
	at += Label(c, x, at, w, Tr("Time"));
	// The time is typed (21:30), or moved a quarter of an hour at a time.
	{
		const bool typing = c.st.focus == kCupTimeField;
		const int bw = c.k.fieldH + 4, fw = 110;
		if (!c.measure) p.Button(x, at, bw, c.k.fieldH, "-", kTourney, CupArg(kCupTimeStep, 0), c.mx, c.my, !c.locked);
		Field(c, x + bw + 4, at, fw, typing ? c.u.cupTime : CupStartClock(f.startsAt), "21:30", kCupTimeField, kFieldCupTime, !c.locked);
		if (!c.measure) {
			p.Button(x + bw + fw + 8, at, bw, c.k.fieldH, "+", kTourney, CupArg(kCupTimeStep, 1), c.mx, c.my, !c.locked);
			p.Font(3);
			const int hx = x + 2 * bw + fw + 22;
			p.Text(hx, at, x + w - hx, c.k.fieldH, Tr("Type it, or move it 15 minutes"), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	at += c.k.fieldH + 14;
	if (!c.measure) {
		UseFace(p, BrandFace::UiStrong, 18);
		p.Text(x, at, w, 26, LongDayText(f.startsAt) + ", " + HourText(f.startsAt), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	at += 30;
	std::string hint = Tr("In your computer's time: each player sees it in their own.");
	if (f.startsAt > c.now) hint += " " + TrFmt("It starts in %s.", {DurationText(f.startsAt - c.now)});
	at += Hint(c, x, at, w, hint);
	at += Problem(c, x, at, w, {TourneyCupProblem::StartSoon, TourneyCupProblem::StartLate});
	return at - y;
}

int CalendarRight(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	Painter &p = c.p;
	int at = y;
	const bool byRounds = f.pace == TourneyPace::Rounds;
	at += Label(c, x, at, w, Tr("Pace"));
	Segments(c, x, at, w, c.k.fieldH,
		{{Tr("Live, in one evening"), CupArg(kCupPace, 0), !byRounds}, {Tr("By rounds, with deadlines"), CupArg(kCupPace, 1), byRounds}});
	at += c.k.fieldH + 8;
	if (byRounds) {
		at += 6;
		at += Label(c, x, at, w, Tr("Days for each round"));
		Segments(c, x, at, w, c.k.fieldH,
			{{Tr("1 day"), CupArg(kCupRoundDays, 1), f.roundDays == 1}, {TrFmt("%s days", {"2"}), CupArg(kCupRoundDays, 2), f.roundDays == 2},
				{TrFmt("%s days", {"3"}), CupArg(kCupRoundDays, 3), f.roundDays == 3}, {Tr("1 week"), CupArg(kCupRoundDays, 7), f.roundDays == 7}});
		at += c.k.fieldH + 8;
		at += Hint(c, x, at, w,
			Tr("A round's matches are ready as soon as it opens, and each pair agrees on a time. The next round opens when they are all over, or when the "
			   "deadline ends."));
		at += Problem(c, x, at, w, {TourneyCupProblem::Pace});
	}
	else at += Hint(c, x, at, w, Tr("Each match is ready as soon as its players are known, from the start to the final."));
	at += c.k.rowGap;
	at += Label(c, x, at, w, Tr("Check-in"));
	Segments(c, x, at, w, c.k.fieldH,
		{{Tr("15 min before"), CupArg(kCupCheckin, 15), f.checkinMinutes == 15}, {Tr("30 min before"), CupArg(kCupCheckin, 30), f.checkinMinutes == 30}});
	at += c.k.fieldH + 8;
	at += Hint(c, x, at, w,
		TrFmt("From %s to %s. Whoever does not check in in time loses the place.", {HourText(f.startsAt - f.checkinMinutes * kMinuteMs), HourText(f.startsAt)}));
	at += Problem(c, x, at, w, {TourneyCupProblem::Checkin}) + c.k.rowGap;
	at += Label(c, x, at, w, Tr("No-show"));
	Segments(c, x, at, w, c.k.fieldH,
		{{TrFmt("After %s min", {"10"}), CupArg(kCupNoshow, 10), f.noshowMinutes == 10}, {TrFmt("After %s min", {"15"}), CupArg(kCupNoshow, 15), f.noshowMinutes == 15},
			{TrFmt("After %s min", {"20"}), CupArg(kCupNoshow, 20), f.noshowMinutes == 20}});
	at += c.k.fieldH + 8;
	at += Hint(c, x, at, w,
		byRounds ? Tr("Only in the last 24 hours of a round: whoever is not in the room by then loses the match. At the deadline, whoever waits alone in "
					  "the room wins; a match nobody played comes to you to settle, and if you don't within 24 hours, both are out.")
				 : Tr("Whoever is not in the room by then loses the match, and the other goes through."));
	at += Problem(c, x, at, w, {TourneyCupProblem::Noshow}) + c.k.rowGap;
	const int half = (w - 16) / 2;
	if (!c.measure) {
		PaintCaps(p, x, at, half, c.k.labelH, Tr("Minimum online games"), pal::caption);
		PaintCaps(p, x + half + 16, at, w - half - 16, c.k.labelH, Tr("Highest rank"), pal::caption);
	}
	at += c.k.labelH + 8;
	Combo(c, x, at, half, GamesText(f.requirements.minGames), kCupMenuGames);
	Combo(c, x + half + 16, at, w - half - 16, TierCapText(f.requirements.maxTier), kCupMenuTier);
	at += c.k.fieldH + 8;
	at += Hint(c, x, at, w,
		Tr(f.mode == TourneyMode::Teams ? "The games count as for organising a cup, and both players of a team have to meet them. Players without a rank this season can always sign up."
										: "The games count as for organising a cup. Players without a rank this season can always sign up."));
	at += Problem(c, x, at, w, {TourneyCupProblem::Requirements});
	if (f.play && f.mode != TourneyMode::Teams && f.requirements.Any() && CupUnmet(f.requirements, c.mine) != TourneyUnmet::None) {
		at += 6;
		at += Hint(c, x, at, w, Tr("You don't meet these requirements yourself: change them, or untick \"I play too\"."), pal::warn);
	}
	return at - y;
}

int AccessBlock(Form &c, int x, int y, int w)
{
	const TourneyCupForm &f = c.f;
	Painter &p = c.p;
	int at = y;
	at += Label(c, x, at, w, Tr("Access"));
	const int half = (w - 16) / 2;
	Option(c, x, at, half, c.k.optionH, !f.isPrivate, true, Tr("Open cup"), Tr("It shows in the list: anyone can sign up."), std::string(), false, CupArg(kCupPrivate, 0));
	Option(c, x + half + 16, at, w - half - 16, c.k.optionH, f.isPrivate, true, Tr("Private cup"), Tr("Only with its 6-character code, which you share."), std::string(),
		false, CupArg(kCupPrivate, 1));
	at += c.k.optionH + c.k.rowGap;
	if (f.mode == TourneyMode::Teams && !f.teamDraw) {
		// The organizer's team needs a partner: it signs up from the cup's page, as any other.
		at += Hint(c, x, at, w, Tr("To play your own cup, sign your team up from its page once it is published, with your partner."));
		return at - y;
	}
	if (!c.measure) {
		const bool enabled = !c.locked, hot = enabled && p.Hover(x, at, w, 30, c.mx, c.my);
		PaintBigToggle(p, x, at + 4, f.play, enabled, hot);
		UseFace(p, BrandFace::UiStrong, 16);
		const std::string play = Tr("I play too");
		const int playW = p.TextWidth(play) + 4, hintX = x + kBigToggleW + 14 + playW + 16;
		p.Text(x + kBigToggleW + 14, at, playW, 30, play, enabled ? pal::text : pal::disabled, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(hintX, at, x + w - hintX, 30, Tr("You take a place in your own cup."), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (enabled) p.AddHit(x, at, w, 30, kTourney, CupArg(kCupPlay, f.play ? 0 : 1));
	}
	at += 30;
	return at - y;
}

typedef int (*Block)(Form &, int, int, int);

// A numbered section: its two columns side by side (one under the other on a narrow screen), and a block across under them.
int Section(Form &c, int x, int y, int w, int number, const std::string &title, Block left, Block right, Block across)
{
	const int pad = c.k.pad, inner = w - 2 * pad, gap = c.k.colGap;
	const bool two = inner >= 920;
	const int colW = two ? (inner - gap) / 2 : inner;
	const bool measuring = c.measure;
	c.measure = true;
	const int hl = left(c, x + pad, 0, colW), hr = right(c, x + pad, 0, colW), ha = across ? across(c, x + pad, 0, inner) : 0;
	c.measure = measuring;
	const int body = two ? std::max(hl, hr) : hl + c.k.rowGap + hr;
	const int h = pad - 6 + 64 + body + (across ? 2 * c.k.rowGap + ha : 0) + pad;
	if (c.measure) return h;
	PaintGlassCard(c.p, x, y, w, h);
	int at = y + pad - 6;
	at += Step(c, x + pad, at, inner, number, title);
	left(c, x + pad, at, colW);
	if (two) right(c, x + pad + colW + gap, at, colW);
	else right(c, x + pad, at + hl + c.k.rowGap, colW);
	at += body;
	if (across) {
		at += c.k.rowGap;
		c.p.Fill(x + pad, at, inner, 1, pal::hairline);
		across(c, x + pad, at + c.k.rowGap, inner);
	}
	return h;
}

// The bracket's shape for the Summary: each round a column of boxes, joined towards the final.
void PaintMiniBracket(Painter &p, int x, int y, int w, int h, int places)
{
	const int rounds = TourneyRoundsFor(places);
	if (rounds <= 0) return;
	const int gap = 14, boxW = (w - gap * (rounds - 1) - 30) / rounds, first = std::max(1, places / 2);
	const int boxH = std::max(6, std::min(18, h / first - 4));
	std::vector<int> mids;
	for (int r = 1; r <= rounds; r++) {
		const int n = std::max(1, places >> r), bx = x + (r - 1) * (boxW + gap);
		std::vector<int> next;
		for (int i = 0; i < n; i++) {
			const int mid = r == 1 ? y + (2 * i + 1) * h / (2 * n) : (mids[static_cast<size_t>(2 * i)] + mids[static_cast<size_t>(2 * i + 1)]) / 2;
			p.Fill(bx, mid - boxH / 2, boxW, boxH, pal::selected);
			p.Frame(bx, mid - boxH / 2, boxW, boxH, pal::edge);
			if (r > 1) {
				const int jx = bx - gap / 2, a = mids[static_cast<size_t>(2 * i)], b = mids[static_cast<size_t>(2 * i + 1)];
				p.Fill(bx - gap, a, gap / 2, 1, pal::lineStrong);
				p.Fill(bx - gap, b, gap / 2, 1, pal::lineStrong);
				p.Fill(jx, std::min(a, b), 1, std::abs(b - a) + 1, pal::lineStrong);
				p.Fill(jx, mid, gap / 2, 1, pal::lineStrong);
			}
			next.push_back(mid);
		}
		mids.swap(next);
	}
	const int fx = x + rounds * (boxW + gap) - gap, mid = mids.empty() ? y + h / 2 : mids[0];
	p.Fill(fx, mid, 10, 1, pal::lineStrong);
	p.Shape(fx + 10, mid - 10, 20, 20, InUnitEllipse, RGB(0xc9, 0x9a, 0x2e));
	p.Shape(fx + 12, mid - 8, 16, 16, InUnitEllipse, RGB(0x2a, 0x21, 0x16));
	p.Shape(fx + 16, mid - 4, 8, 8, InUnitEllipse, RGB(0xe0, 0xb5, 0x4a));
}

// A row of the Summary: its name over its value, wrapped to `lines`; returns its height.
int SummaryRow(Painter &p, int x, int y, int w, const std::string &name, const std::string &value, int lines, bool paint)
{
	p.Font(0);
	const int one = p.Measure(w, "Ag");
	const int h = std::min(p.Measure(w, value), one * lines);
	if (paint) {
		PaintCaps(p, x, y, w, 18, name, pal::caption);
		p.Font(0);
		p.TextWrapped(x, y + 22, w, h + 2, value, pal::body);
	}
	return 22 + h + 12;
}

void PaintSummary(Painter &p, const State &st, const Ui &u, const std::vector<TourneyCupProblem> &problems, bool selfUnmet, int x, int y, int w, int h, int64_t now,
	PaintOut &out)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 26, iw = w - 2 * pad;
	const TourneyCupForm &f = u.cup;
	const bool editing = u.cupEditId != 0;
	PaintGlassCard(p, x, y, w, h);
	p.Fill(x, y, w, 3, pal::red);
	int at = y + 20;
	PaintCaps(p, x + pad, at, iw, 20, Tr("Summary"), pal::caption);
	at += 30;

	// The three sections, each a link to its place in the form: its number, or a cross when something in it is wrong, over its name.
	const char *const names[] = {"Format", "Rules and maps", "Calendar"};
	const int stepW = iw / 3, stepH = 50;
	for (int i = 0; i < 3; i++) {
		const int sx = x + pad + i * stepW, cw = stepW - 6;
		bool wrong = false;
		for (TourneyCupProblem q : problems) wrong = wrong || SectionOf(q) == i;
		const bool hot = p.Hover(sx, at, cw, stepH, mx, my);
		p.Fill(sx, at, cw, stepH, hot ? pal::selected : pal::raised);
		if (wrong) PaintCheck(p, sx + cw / 2 - 9, at + 5, 18, false);
		else {
			UseDisplay(p, 22);
			p.Text(sx, at + 2, cw, 24, std::to_string(i + 1), pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		UseFace(p, BrandFace::UiStrong, 10);
		p.TextCaps(sx + 4, at + 27, cw - 8, 18, Tr(names[i]), wrong ? pal::bad : hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.AddHit(sx, at, cw, stepH, kTourney, ArgOf(kSubCupSection, i));
	}
	at += stepH + 14;
	p.Fill(x + pad, at, iw, 1, pal::hairline);
	at += 16;

	const TourneyEstimate est = TourneyEstimateOf(f);
	const bool byRounds = f.pace == TourneyPace::Rounds;
	// League Gothic has no "≈" nor "≤": they go in Archivo before the figure. By rounds, the days at most and the last one.
	const char *mark = byRounds ? "\xE2\x89\xA4" : "\xE2\x89\x88";
	UseFace(p, BrandFace::Ui, 30);
	const int aw = p.TextWidth(mark) + 6;
	p.Text(x + pad, at, aw, 46, mark, pal::textDim, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
	const std::string duration = byRounds ? TrFmt("%s days", {std::to_string(est.days)}) : DurationText(est.minutes * kMinuteMs);
	UseDisplay(p, 46);
	const int dw = std::min(iw - aw, p.TextWidth(duration) + 6);
	p.Text(x + pad + aw, at, dw, 50, duration, pal::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
	p.Font(0);
	const std::string span = byRounds ? TrFmt("until %s at the latest", {DayText(f.startsAt + est.days * kDayMs)})
									  : TrFmt("from %s to %s", {HourText(f.startsAt), HourText(f.startsAt + est.minutes * kMinuteMs)});
	p.Text(x + pad + aw + dw + 12, at, iw - aw - dw - 12, 44, span, pal::textDim, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
	at += 56;
	const std::string players = std::to_string(f.maxEntries);
	const bool teams = f.mode == TourneyMode::Teams;
	const std::string figures = f.format == TourneyFormat::Double
		? TrFmt(teams ? "%s teams \xC2\xB7 two brackets \xC2\xB7 up to %s series and %s games" : "%s players \xC2\xB7 two brackets \xC2\xB7 up to %s series and %s games",
			  {players, std::to_string(est.series), std::to_string(est.maxGames)})
		: TrFmt(teams ? "%s teams \xC2\xB7 %s rounds \xC2\xB7 %s series \xC2\xB7 up to %s games" : "%s players \xC2\xB7 %s rounds \xC2\xB7 %s series \xC2\xB7 up to %s games",
			  {players, std::to_string(est.rounds), std::to_string(est.series), std::to_string(est.maxGames)});
	p.Font(3);
	p.Text(x + pad, at, iw, 22, figures, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	at += 30;
	if (TourneyCupLongLive(f)) {
		const std::string warning = Tr("More than 5 hours live: think of the by-rounds pace, or fewer places.");
		const int wh = std::min(p.Measure(iw - 14, warning), 44);
		p.Fill(x + pad, at - 4, 3, wh + 8, pal::warn);
		p.TextWrapped(x + pad + 14, at, iw - 14, wh + 2, warning, pal::warn);
		at += wh + 14;
	}

	// The foot first, from the bottom: the button, what is going on or wrong, and what happens without enough players.
	const bool linked = LeagueLinked(), busy = LeagueTourneyBusy(editing ? TourneyOp::Edit : TourneyOp::Create, editing ? u.cupEditId : 0);
	const int bh = 56, by = y + h - pad - bh;
	std::string label = editing ? Tr(busy ? "Saving..." : "Save changes") : Tr(busy ? "Publishing..." : "Publish the cup");
	p.Font(2);
	p.Button(x + pad, by, iw, bh, label, kTourney, ArgOf(kSubCupSubmit, 0), mx, my, linked && !busy && problems.empty() && !selfUnmet, true, 2);
	int foot = by - 12;
	std::string status;
	COLORREF ink = pal::textDim;
	const TourneyUploadView up = LeagueTourneyUploading();
	if (busy && up.active) {
		const int pct = up.total ? static_cast<int>(std::min<uint64_t>(100, up.done * 100 / up.total)) : 0;
		foot -= 6;
		PaintProgress(p, x + pad, foot, iw, 6, pct / 100.0f);
		foot -= 4;
		status = TrFmt("Uploading \"%s\" (%s of %s) \xC2\xB7 %s %%", {up.title, std::to_string(up.index), std::to_string(up.count), std::to_string(pct)});
	}
	else if (!linked) {
		status = Tr("Link your league account first.");
		ink = pal::warn;
	}
	else if (!u.cupError.empty()) {
		status = u.cupError;
		ink = pal::bad;
	}
	else if (!problems.empty()) {
		status = TrFmt("Section %s: %s", {std::to_string(SectionOf(problems[0]) + 1), CupProblemText(problems[0])});
		ink = pal::bad;
	}
	else if (selfUnmet) {
		status = Tr("You don't meet your own cup's requirements: change them, or untick \"I play too\".");
		ink = pal::warn;
	}
	else if (u.cupLocked) {
		status = Tr("Other players have signed up: only the description can change now.");
		ink = pal::warn;
	}
	if (!status.empty()) {
		p.Font(3);
		const int sh = std::min(p.Measure(iw, status), 60);
		foot -= sh;
		p.TextWrapped(x + pad, foot, iw, sh + 2, status, ink);
		foot -= 10;
	}
	if (!u.cupErrorSlug.empty()) {
		const std::string open = Tr("Open my cup");
		foot -= 40;
		PaintSecondButton(p, x + pad, foot, iw, 40, open, ArgOf(kSubCupOpenActive, 0), true, mx, my);
		foot -= 12;
	}
	const std::string note = TrFmt(teams ? "If fewer than %s teams have checked in at %s, the cup is cancelled on its own and its players are told."
										 : "If fewer than %s players have checked in at %s, the cup is cancelled on its own and its players are told.",
		{std::to_string(f.minEntries), HourText(f.startsAt)});
	p.Font(3);
	const int nh = p.Measure(iw - 28, note) + 20;
	foot -= nh;
	p.Fill(x + pad, foot, iw, nh, pal::raised);
	p.Fill(x + pad, foot, 3, nh, pal::edgeHot);
	p.TextWrapped(x + pad + 14, foot + 10, iw - 28, nh - 16, note, pal::body);
	foot -= 16;

	// The rows in between, as many as fit; the bracket's shape first when there is room for all. Each row has its full value, for a name over
	// it, and a short one, for a name beside it on a short screen.
	struct Row {
		std::string name, full, brief;
		int lines;
	};
	std::vector<Row> rows;
	const std::string starts = LongDayText(f.startsAt) + ", " + HourText(f.startsAt);
	rows.push_back({Tr("Starts"), starts + (f.startsAt > now ? " \xC2\xB7 " + TrFmt("in %s", {DurationText(f.startsAt - now)}) : std::string()),
		DayText(f.startsAt) + ", " + HourText(f.startsAt), 2});
	const std::string format = FormatLine(f) + "\n" + PaceLine(f);
	rows.push_back({Tr("Format"), format, format, 2});
	rows.push_back({Tr("Rules"), RulesText(f.settings), RulesText(f.settings), 3});
	std::string maps, mapsBrief;
	for (int i = 0; i < static_cast<int>(f.maps.size()); i++) {
		const TourneyMap &m = f.maps[static_cast<size_t>(i)];
		maps += (i ? "\n" : "") + CupRoundName(f, i) + ": " + CupMapLine(m);
		mapsBrief += (i ? "\n" : "") + CupRoundName(f, i) + ": " + MapText(m);
	}
	if (TourneyCupHasSeries(f)) {
		std::string pool;
		for (const TourneyMap &m : f.seriesPool) pool += (pool.empty() ? "" : ", ") + MapText(m);
		const std::string series = pool.empty() ? Tr("Series: the round's map in every game") : TrFmt("Series: %s", {pool});
		maps += "\n" + series;
		mapsBrief += "\n" + series;
	}
	rows.push_back({Tr("Maps"), maps, mapsBrief, 6});
	rows.push_back({Tr("Requirements"), RequirementsText(f.requirements), RequirementsText(f.requirements), 2});
	const std::string access = Tr(f.isPrivate ? "Private: only with its code" : "Open: it shows in the list") + " \xC2\xB7 " +
		Tr(teams ? "your team signs up from its page" : f.play ? "you play too" : "you only organise it");
	rows.push_back({Tr("Access"), access, access, 2});
	int need = 0;
	for (const Row &r : rows) need += SummaryRow(p, x + pad, 0, iw, r.name, r.full, r.lines, false);
	// Each name over its value when there is room; on a short screen each name beside a line of its value.
	const bool full = foot - at - 14 >= need;
	const int bracketH = 112;
	if (full && foot - at - 14 >= need + bracketH + 8 && est.rounds > 0 && f.format == TourneyFormat::Single) {
		PaintMiniBracket(p, x + pad, at + 4, iw, bracketH - 12, f.maxEntries);
		at += bracketH + 8;
	}
	p.Fill(x + pad, at, iw, 1, pal::hairline);
	at += 14;
	const int viewTop = at, viewH = std::max(0, foot - at);
	// The short form, a name beside each line of its value, scrolls when even it is taller than the room left.
	std::vector<std::vector<std::string>> briefLines;
	int briefNeed = 0;
	for (const Row &r : rows) {
		std::vector<std::string> lines;
		for (size_t a = 0;;) {
			const size_t b = r.brief.find('\n', a);
			lines.push_back(r.brief.substr(a, b == std::string::npos ? std::string::npos : b - a));
			if (b == std::string::npos) break;
			a = b + 1;
		}
		briefNeed += static_cast<int>(lines.size()) * 20 + 4;
		briefLines.push_back(std::move(lines));
	}
	const int max = full ? 0 : std::max(0, briefNeed - 4 - viewH), scroll = std::min(u.summaryScroll, max);
	ClipArea view(p, x + 1, viewTop, w - 2, viewH);
	if (full) {
		for (const Row &r : rows) {
			const int rh = SummaryRow(p, x + pad, at, iw, r.name, r.full, r.lines, false);
			if (at + rh - 12 > foot) break;
			SummaryRow(p, x + pad, at, iw, r.name, r.full, r.lines, true);
			at += rh;
		}
	}
	else {
		UseFace(p, BrandFace::UiStrong, 11);
		int labelW = 0;
		for (const Row &r : rows) labelW = std::max(labelW, p.CapsWidth(r.name, 1) + 12);
		labelW = std::min(labelW, iw / 3);
		at -= scroll;
		for (size_t ri = 0; ri < rows.size(); ri++) {
			const Row &r = rows[ri];
			const std::vector<std::string> &lines = briefLines[ri];
			UseFace(p, BrandFace::UiStrong, 11);
			p.TextCaps(x + pad, at, labelW - 8, 20, r.name, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
			p.Font(3);
			for (size_t l = 0; l < lines.size(); l++) {
				p.Text(x + pad + labelW, at + static_cast<int>(l) * 20, iw - labelW, 20, lines[l], pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
			at += static_cast<int>(lines.size()) * 20 + 4;
		}
	}
	view.End();
	if (max > 0) {
		PaintScrollbar(p, kBarSummary, x + w - 12, viewTop, viewH, {scroll, max, viewH}, st.dropBar, mx, my);
		SetScrollArea(p, out.summaryArea, x, viewTop, w, viewH, max);
	}
}

}

TourneyCupForm NewCupForm(const std::string &player, int64_t nowMs)
{
	TourneyCupForm f;
	f.name = TrFmt("%s's cup", {player});
	while (TourneyTextLength(f.name) > kTourneyCupNameMax) f.name.erase(static_cast<size_t>(PrevCp(f.name, static_cast<int>(f.name.size()))));
	f.minEntries = TourneyCupDefaultMinimum(f.maxEntries);
	TourneyFitMaps(f.maps, TourneyCupMapRounds(f));
	// The first full hour at least two hours away, so the players see it and sign up; at night or in the morning, the evening
	// instead (19:00), when people play.
	int64_t lo = 0, hi = 0;
	CupStartBounds(nowMs, lo, hi);
	const int64_t local = LocalMs(nowMs + 2 * kHourMs);
	int64_t start = (local + kHourMs - 1) / kHourMs * kHourMs;
	const int64_t day = 24 * kHourMs, hour = (start % day) / kHourMs;
	if (hour >= 23) start = start - start % day + day + 19 * kHourMs;
	else if (hour < 10) start = start - start % day + 19 * kHourMs;
	f.startsAt = UtcMs(TourneySnapStart(start, lo, hi));
	return f;
}

void OwnCupContent(std::string &key, std::string &name)
{
	key.clear();
	name.clear();
	const ModProfile profile = ModsMountedProfile();
	std::string rules;
	for (const std::string &folder : profile.mods) {
		ModInfo m;
		if (!ModsFind(folder, m) || !m.gameplay) continue;
		rules += (rules.empty() ? "" : " + ") + (m.name.empty() ? folder : m.name);
	}
	// Packs that change no rules (maps, looks) play as the base game: the cup asks for nothing.
	if (rules.empty()) return;
	key = SteamOwnGameplayKey();
	if (key.empty()) return;
	name = ModsIsReservedProfileName(profile.name) ? rules : profile.name;
	while (TourneyTextLength(name) > 60) name.erase(static_cast<size_t>(PrevCp(name, static_cast<int>(name.size()))));
}

std::string CupRoundName(const TourneyCupForm &f, int index)
{
	const int rounds = TourneyCupMapRounds(f), maps = static_cast<int>(f.maps.size());
	if (f.format == TourneyFormat::Single) return StageText(TourneyStageOf(index + 1, rounds), index + 1);
	if (index + 1 == maps && rounds > maps) return TrFmt("Rounds %s to %s", {std::to_string(index + 1), std::to_string(rounds)});
	return StageText(TourneyStage::Unknown, index + 1);
}

void CupStartBounds(int64_t nowMs, int64_t &localMin, int64_t &localMax)
{
	localMin = LocalMs(nowMs + kLeadMs);
	localMax = LocalMs(nowMs + kTourneyCupLeadMaxMs);
}

// "21:30" of a start, in the computer's time.
std::string CupStartClock(int64_t utcMs)
{
	const int64_t local = LocalMs(utcMs);
	char text[8];
	sprintf_s(text, "%02d:%02d", LocalHour(local), LocalMinute(local));
	return text;
}

void CupTimeFocus(Ui &u) { u.cupTime = CupStartClock(u.cup.startsAt); }

void CupTimeTyped(Ui &u, bool done)
{
	std::string &t = u.cupTime;
	std::string clean;
	for (char ch : t) {
		if ((ch >= '0' && ch <= '9') || ch == ':') clean += ch;
	}
	if (clean != t) t = clean;
	int hour = -1, minute = -1;
	const size_t colon = t.find(':');
	if (colon != std::string::npos) {
		const std::string h = t.substr(0, colon), m = t.substr(colon + 1);
		if (!h.empty() && h.size() <= 2 && m.size() == 2 && m.find(':') == std::string::npos) {
			hour = atoi(h.c_str());
			minute = atoi(m.c_str());
		}
	}
	else if (t.size() == 4) {
		hour = atoi(t.substr(0, 2).c_str());
		minute = atoi(t.substr(2).c_str());
	}
	if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && !u.cupLocked) {
		int64_t lo = 0, hi = 0;
		CupStartBounds(NowMs(), lo, hi);
		u.cup.startsAt = UtcMs(TourneyAtTime(LocalMs(u.cup.startsAt), hour, minute, lo, hi));
		u.cupError.clear();
	}
	if (done) t = CupStartClock(u.cup.startsAt);
}

void ApplyCupSetting(TourneyCupForm &f, int setting, int choice, int64_t nowMs)
{
	int64_t lo = 0, hi = 0;
	CupStartBounds(nowMs, lo, hi);
	TourneySettings &s = f.settings;
	switch (setting) {
	case kCupPlaces:
		if (choice != 4 && choice != 8 && choice != 16 && choice != 32) break;
		// The minimum follows the places: it is not asked.
		f.minEntries = TourneyCupDefaultMinimum(choice);
		f.maxEntries = choice;
		TourneyFitMaps(f.maps, TourneyCupMapRounds(f));
		break;
	case kCupMinimum: f.minEntries = std::max(2, std::min(f.maxEntries, f.minEntries + (choice ? 1 : -1))); break;
	case kCupFormat:
		if (choice < static_cast<int>(TourneyFormat::Single) || choice > static_cast<int>(TourneyFormat::Swiss)) break;
		f.format = static_cast<TourneyFormat>(choice);
		TourneyFitMaps(f.maps, TourneyCupMapRounds(f));
		break;
	case kCupSwissRounds:
		if (choice != 0 && (choice < kTourneySwissRoundsMin || choice > kTourneySwissRoundsMax)) break;
		f.swissRounds = choice;
		TourneyFitMaps(f.maps, TourneyCupMapRounds(f));
		break;
	case kCupPace: f.pace = choice ? TourneyPace::Rounds : TourneyPace::Live; break;
	case kCupMode:
		// The maps follow: a small map grows to the next size with room for four, and a scenario for two gives way to a random map.
		f.mode = choice ? TourneyMode::Teams : TourneyMode::Solo;
		TourneyFitCupToMode(f, LeagueTourneyMapTypes());
		CupDropUnsuitable(f);
		break;
	case kCupTeamDraw: f.teamDraw = choice != 0; break;
	case kCupRoundDays:
		if (TourneyRoundDaysValid(choice)) f.roundDays = choice;
		break;
	case kCupBestOf:
		if (choice == 1 || choice == 3) f.bestOf = choice;
		break;
	case kCupFinal:
		if (choice == 1 || choice == 3 || choice == 5) f.finalBestOf = choice;
		break;
	case kCupSeeding: f.randomSeeding = choice != 0; break;
	case kCupCheckin:
		if (choice == 15 || choice == 30) f.checkinMinutes = choice;
		break;
	case kCupNoshow:
		if (choice == 10 || choice == 15 || choice == 20) f.noshowMinutes = choice;
		break;
	case kCupPrivate: f.isPrivate = choice != 0; break;
	case kCupPlay: f.play = choice != 0; break;
	case kCupPreset: {
		// A preset brings its own fog, map and diplomacy back; the civilizations and the spectators stay as they were.
		const TourneyPreset preset = choice == 1 ? TourneyPreset::Classic : choice == 2 ? TourneyPreset::Various : TourneyPreset::Competitive;
		TourneySettings fresh;
		fresh.preset = TourneyPresetId(preset);
		fresh.hideCivs = s.hideCivs;
		fresh.timers = s.timers;
		fresh.specDelay = TourneySpecDelay(s);
		s = TourneyExplicitSettings(fresh);
		break;
	}
	case kCupHideCivs: s.hideCivs = choice != 0; break;
	case kCupFog: s.fog = choice ? 0 : 1; break;
	case kCupExplored: s.explored = choice ? 1 : 0; break;
	case kCupDiplomacy: s.diplomacy = choice ? "locked" : "free"; break;
	case kCupTimers: s.timers = choice ? 1 : 0; break;
	case kCupSpectators:
		if (TourneySpecDelayValid(choice)) s.specDelay = choice;
		break;
	case kCupDay: {
		const TourneyCalendar cal = TourneyCalendarOf(TourneyDayOf(LocalMs(nowMs)), TourneyDayOf(hi));
		const int64_t day = cal.first + choice;
		if (choice >= 0 && choice < cal.rows * 7 && TourneyDayOpen(day, lo, hi)) f.startsAt = UtcMs(TourneyOnDay(LocalMs(f.startsAt), day, lo, hi));
		break;
	}
	case kCupTimeStep: {
		const int64_t local = LocalMs(f.startsAt) + (choice ? kTourneyQuarterMs : -kTourneyQuarterMs);
		f.startsAt = UtcMs(TourneySnapStart(local, lo, hi));
		break;
	}
	case kCupMinute:
		if (choice == 0 || choice == 15 || choice == 30 || choice == 45) {
			const int64_t local = LocalMs(f.startsAt);
			f.startsAt = UtcMs(TourneyAtTime(local, LocalHour(local), choice, lo, hi));
		}
		break;
	default: break;
	}
}

std::vector<std::string> CupMenuItems(const TourneyCupForm &f, int menu, int &selected)
{
	std::vector<std::string> items;
	selected = -1;
	if (menu == kCupMenuHour) {
		selected = LocalHour(LocalMs(f.startsAt));
		for (int h = 0; h < 24; h++) {
			char text[8];
			sprintf_s(text, "%02d", h);
			items.push_back(text);
		}
	}
	else if (menu == kCupMenuGames) {
		bool listed = false;
		for (int g : kGamesChoices) {
			if (!listed && g > f.requirements.minGames) {
				selected = static_cast<int>(items.size());
				items.push_back(GamesText(f.requirements.minGames));
				listed = true;
			}
			if (g == f.requirements.minGames) {
				selected = static_cast<int>(items.size());
				listed = true;
			}
			items.push_back(GamesText(g));
		}
	}
	else if (menu == kCupMenuTier) {
		for (LeagueTier t : kTierChoices) {
			if (t == f.requirements.maxTier) selected = static_cast<int>(items.size());
			items.push_back(TierCapText(t));
		}
	}
	return items;
}

void CupMenuPick(TourneyCupForm &f, int menu, int item, int64_t nowMs)
{
	if (menu == kCupMenuHour && item >= 0 && item < 24) {
		int64_t lo = 0, hi = 0;
		CupStartBounds(nowMs, lo, hi);
		const int64_t local = LocalMs(f.startsAt);
		f.startsAt = UtcMs(TourneyAtTime(local, item, LocalMinute(local), lo, hi));
	}
	else if (menu == kCupMenuGames) {
		std::vector<int> values;
		bool listed = false;
		for (int g : kGamesChoices) {
			if (!listed && g > f.requirements.minGames) {
				values.push_back(f.requirements.minGames);
				listed = true;
			}
			if (g == f.requirements.minGames) listed = true;
			values.push_back(g);
		}
		if (item >= 0 && item < static_cast<int>(values.size())) f.requirements.minGames = values[static_cast<size_t>(item)];
	}
	else if (menu == kCupMenuTier && item >= 0 && item < static_cast<int>(sizeof(kTierChoices) / sizeof(kTierChoices[0]))) {
		f.requirements.maxTier = kTierChoices[item];
	}
}

std::string CupProblemText(TourneyCupProblem problem)
{
	switch (problem) {
	case TourneyCupProblem::NameShort: return Tr("The name needs at least 3 characters.");
	case TourneyCupProblem::NameLong: return Tr("The name can have up to 40 characters.");
	case TourneyCupProblem::DescriptionLong: return Tr("The description can have up to 200 characters.");
	case TourneyCupProblem::Places: return Tr("Choose 4, 8, 16 or 32 places; a double elimination, up to 16.");
	case TourneyCupProblem::Minimum: return Tr("The minimum goes from 2 to the number of places.");
	case TourneyCupProblem::Format: return Tr("A Swiss plays 3 to 6 rounds, or the automatic count.");
	case TourneyCupProblem::Series: return Tr("Choose the games of the rounds and of the final.");
	case TourneyCupProblem::Settings: return Tr("Choose the game settings and the spectators again.");
	case TourneyCupProblem::NoMaps: return Tr("Choose at least one map.");
	case TourneyCupProblem::TooManyMaps: return Tr("Up to 5 maps.");
	case TourneyCupProblem::BadMap: return Tr("One of the maps can't be used: choose it again.");
	case TourneyCupProblem::MapTooLarge: return Tr("One of your scenarios is larger than 16 MB: the league does not keep it.");
	case TourneyCupProblem::MapSeats: return Tr("In 2 vs 2 every map needs room for four players: choose it again.");
	case TourneyCupProblem::PoolCount: return Tr("The series need 3 to 5 maps, or none.");
	case TourneyCupProblem::PoolMap: return Tr("One of the series' maps can't be used or is repeated: choose it again.");
	case TourneyCupProblem::StartSoon: return Tr("It has to start at least 20 minutes from now.");
	case TourneyCupProblem::StartLate: return Tr("It has to start within the next 14 days.");
	case TourneyCupProblem::Pace: return Tr("Choose rounds of 1, 2, 3 or 7 days.");
	case TourneyCupProblem::Noshow: return Tr("Choose a no-show time of 10, 15 or 20 minutes.");
	case TourneyCupProblem::Requirements: return Tr("Choose up to 100 online games and a rank up to Legate.");
	default: return Tr("Choose a check-in of 15 or 30 minutes.");
	}
}

bool OrganizerPlays(const TourneyDetail &d, const TourneyMatch &m)
{
	const uint64_t organizer = d.organizer.steamId;
	if (!organizer) return false;
	if (m.a.steamId == organizer || m.b.steamId == organizer) return true;
	const TourneyEntrant *e = TourneyEntrantBySteamId(d, organizer);
	return e && m.Has(e->entryId);
}

TourneyUnmet CupUnmet(const TourneyRequirements &r, const TourneyMineView &mine)
{
	if (!r.Any()) return TourneyUnmet::None;
	// Without the games known yet the league decides; a player still in placement has no rank to cap.
	const int games = mine.loaded && mine.mine.organize.known ? mine.mine.organize.gamesPlayed : 1 << 20;
	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;
	const LeagueTier tier = me.loaded && me.me.ranked.present && me.me.ranked.placementLeft <= 0 ? me.me.ranked.tier : LeagueTier::None;
	return TourneyCheckRequirements(r, games, tier);
}

std::string RequirementsText(const TourneyRequirements &r)
{
	if (!r.Any()) return Tr("Anyone can sign up");
	std::string out;
	if (r.minGames > 0) out = GamesText(r.minGames);
	if (TourneyMaxTierValid(r.maxTier)) out += (out.empty() ? "" : " \xC2\xB7 ") + TrFmt("up to %s", {Tr(LeagueTierName(r.maxTier))});
	if (r.maxPlace > 0) out += (out.empty() ? "" : " \xC2\xB7 ") + TrFmt("top %s of the ranked ladder", {std::to_string(r.maxPlace)});
	return out;
}

std::string UnmetText(const TourneyRequirements &r, TourneyUnmet unmet, const TourneyMineView &mine)
{
	if (unmet == TourneyUnmet::Games) {
		return TrFmt("You need %s online games registered by the league; you have %s.", {std::to_string(r.minGames), std::to_string(mine.mine.organize.gamesPlayed)});
	}
	if (unmet == TourneyUnmet::Tier) {
		const auto meView = LeagueMeShared();
		const LeagueMeView &me = *meView;
		return TrFmt("This cup is for players up to %s, and you are %s.", {Tr(LeagueTierName(r.maxTier)), Tr(LeagueTierName(me.me.ranked.tier))});
	}
	return std::string();
}

std::string PresetSummary(TourneyPreset p)
{
	switch (p) {
	case TourneyPreset::Classic: return Tr("The game's own gold and population, with bonuses");
	case TourneyPreset::Various: return Tr("10000 gold, high population, with bonuses");
	default: return Tr("5000 gold, normal population, no bonuses");
	}
}

std::string RulesText(const TourneySettings &s)
{
	const TourneyRoomRules r = TourneyRulesOf(s);
	const char *preset = r.preset == TourneyPreset::Classic ? "Classic game" : r.preset == TourneyPreset::Various ? "Varied game" : "Competitive game";
	const int delay = TourneySpecDelay(s);
	return Tr(preset) + " \xC2\xB7 " + Tr(r.hideCivs ? "civilizations hidden" : "civilizations visible") + " \xC2\xB7 " + Tr(r.fog ? "fog" : "no fog") + " \xC2\xB7 " +
		Tr(r.explored ? "map explored" : "map unexplored") + " \xC2\xB7 " + Tr(r.lockedDiplomacy ? "diplomacy locked" : "free diplomacy") + " \xC2\xB7 " +
		Tr(r.timers ? "respawn timers" : "no respawn timers") + " \xC2\xB7 " +
		(delay ? TrFmt("spectators %s s behind", {std::to_string(delay)}) : Tr("no spectators"));
}

bool PaintCreateHeader(Painter &p, const State &st, const HeaderUi &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const bool editing = u.cupEditId != 0;
	const int right = PaintHeaderActions(p, l, {{Tr(editing ? "Back to the cup" : "Tournaments"), kBack, true, true}}, mx, my);
	p.raw = true;
	PaintScreenTitle(p, l, Tr(editing ? "Edit the cup" : "Create a cup"), {}, right, mx, my);
	PaintScreenInfo(p, l,
		Tr(editing ? (u.cupLocked ? "Other players have signed up: only the description can change now."
								  : "Everything can change until another player signs up; after that, only the description.")
				   : "It is published as soon as you create it, in Multiplayer, Tournaments. Until another player signs up you can change everything."),
		editing && u.cupLocked ? pal::warn : pal::caption);
	p.raw = false;
	return true;
}

void PaintCreateScreen(Painter &p, const State &st, const Ui &u, PaintOut &out)
{
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int64_t now = NowMs();
	const bool editing = u.cupEditId != 0;
	std::vector<TourneyCupProblem> problems = TourneyCheckCup(u.cup, now, LeagueTourneyMapTypes());
	if (editing && u.cupLocked) {
		problems.erase(std::remove_if(problems.begin(), problems.end(), [](TourneyCupProblem q) { return q != TourneyCupProblem::DescriptionLong; }), problems.end());
	}
	const auto mineView = LeagueTourneyMine();
	const TourneyMineView &mine = *mineView;
	const bool selfUnmet = !(editing && u.cupLocked) && u.cup.play && u.cup.mode != TourneyMode::Teams && CupUnmet(u.cup.requirements, mine) != TourneyUnmet::None;
	const int m = l.margin, inner = l.w - 2 * m, top = l.top, bottom = l.h - m;
	const int sideW = std::max(360, std::min(440, inner * 26 / 100)), gap = l.roomy ? kCardGap : 24, paneW = inner - sideW - gap, barW = 16;
	p.raw = true;
	Form c{p, st, u, u.cup, problems, mine, LookFor(l.roomy)};
	c.mx = st.mouseX;
	c.my = st.mouseY;
	c.locked = editing && u.cupLocked;
	c.editing = editing;
	c.now = now;
	c.vx = m;
	c.vy = top;
	c.vw = paneW;
	c.vh = bottom - top;
	const int sw = paneW - barW, sectionGap = l.roomy ? 24 : 18;
	struct Def {
		const char *title;
		Block left, right, across;
	} defs[] = {{"Format", FormatLeft, FormatRight, nullptr}, {"Rules and maps", RulesLeft, RulesRight, MapsBlock},
		{"Calendar and requirements", CalendarLeft, CalendarRight, AccessBlock}};
	int heights[3] = {}, total = 0;
	c.measure = true;
	for (int i = 0; i < 3; i++) {
		heights[i] = Section(c, m, 0, sw, i + 1, Tr(defs[i].title), defs[i].left, defs[i].right, defs[i].across);
		out.sectionTop[i] = total;
		total += heights[i] + (i < 2 ? sectionGap : 0);
	}
	c.measure = false;
	const int maxScroll = std::max(0, total - c.vh), scroll = std::max(0, std::min(u.formScroll, maxScroll));
	const size_t hitsBefore = p.hits->size();
	p.Clip(c.vx, c.vy, c.vw, c.vh);
	for (int i = 0; i < 3; i++) {
		const int sy = top - scroll + out.sectionTop[i];
		if (sy > bottom || sy + heights[i] < top) continue;
		Section(c, m, sy, sw, i + 1, Tr(defs[i].title), defs[i].left, defs[i].right, defs[i].across);
	}
	p.Unclip();
	// What scrolled out of the pane can't be clicked through the header or the Summary.
	RECT pane = p.R(c.vx, c.vy, c.vw, c.vh);
	OffsetRect(&pane, p.ox, p.oy);
	for (size_t i = hitsBefore; i < p.hits->size();) {
		RECT r;
		if (!IntersectRect(&r, &(*p.hits)[i].r, &pane)) {
			p.hits->erase(p.hits->begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		(*p.hits)[i].r = r;
		i++;
	}
	if (maxScroll > 0) PaintScrollbar(p, kBarForm, m + paneW - 8, top, c.vh, {scroll, maxScroll, c.vh}, st.dropBar, st.mouseX, st.mouseY);
	out.form = true;
	out.formArea.valid = true;
	out.formArea.r = pane;
	out.formArea.max = maxScroll;
	PaintSummary(p, st, u, problems, selfUnmet, m + paneW + gap, top, sideW, bottom - top, now, out);
	p.raw = false;
}

void PaintJoinDialog(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 560, 290, "Join with a code", pal::redText);
	DialogText(p, d, 66, "Type the code of 6 letters and digits the organiser of the cup gave you. It opens the cup's page, where you sign up.", pal::textDim, 3);
	const int fy = d.y + 128;
	PaintEditField(p, d.BodyX(), fy, 260, 44, u.joinCode, "ABC234", st.focus == kCupCodeField, false, st.edit, kFieldCupCode);
	if (!u.joinError.empty()) {
		p.Font(3);
		p.raw = true;
		p.TextWrapped(d.BodyX(), fy + 52, d.BodyW(), 40, u.joinError, pal::bad);
		p.raw = false;
	}
	const bool busy = LeagueTourneyBusy(TourneyOp::FindCode, 0);
	DialogButtons(p, d, {{busy ? "Looking for it..." : "Find the cup", kTourney, ArgOf(kSubJoinSend, 0), true, !busy}, {"Cancel", kTourney, ArgOf(kSubJoinCancel, 0)}}, mx, my);
}

void PaintCodeDialog(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 580, 330, "Your cup is ready", pal::redText);
	DialogText(p, d, 66, "It is private: only those with this code can find it and sign up. Share it with your players; it is also on the cup's page.", pal::textDim, 3);
	const int by = d.y + 130, bh = 92;
	p.Fill(d.BodyX(), by, d.BodyW(), bh, pal::raised);
	p.Frame(d.BodyX(), by, d.BodyW(), bh, pal::edgeHot);
	UseDisplay(p, 64);
	p.raw = true;
	p.TextCaps(d.BodyX(), by, d.BodyW(), bh, u.shownCode, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 10);
	p.raw = false;
	DialogButtons(p, d, {{"Copy the code", kTourney, ArgOf(kSubCopyCode, 0), true}, {"Close", kTourney, ArgOf(kSubCodeClose, 0)}}, mx, my);
}

}
