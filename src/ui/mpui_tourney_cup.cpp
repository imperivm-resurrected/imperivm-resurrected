// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/leaguemaps.h"
#include "core/tournament.h"
#include "i18n/tr.h"
#include "mods/mods.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/mpui_tourney_internal.h"
#include "ui/textedit.h"

namespace mpui_tourney_impl {
namespace {

const int64_t kMinuteMs = 60000, kHourMs = 60 * kMinuteMs, kDayMs = 24 * kHourMs;
// The start's quick choices and the steps that move it, in the order of their buttons.
const int64_t kQuickStarts[] = {30 * kMinuteMs, kHourMs, 2 * kHourMs, kDayMs};
const char *const kQuickLabels[] = {"In 30 min", "In 1 hour", "In 2 hours", "Tomorrow"};
const int64_t kShifts[] = {-kDayMs, -kHourMs, -15 * kMinuteMs, 15 * kMinuteMs, kHourMs, kDayMs};
const char *const kShiftLabels[] = {"-1 d", "-1 h", "-15 min", "+15 min", "+1 h", "+1 d"};
const int kQuickCount = 4, kShiftCount = 6;

int64_t RoundUp(int64_t ms, int64_t step)
{
	return (ms + step - 1) / step * step;
}

bool Has(const std::vector<TourneyCupProblem> &list, TourneyCupProblem p)
{
	return std::find(list.begin(), list.end(), p) != list.end();
}

// The sizes of the form, tighter on a short screen.
struct Metrics {
	bool roomy;
	int labelH, fieldH, gap, optionH, pad;
};

Metrics MetricsFor(bool roomy)
{
	return roomy ? Metrics{true, 22, 44, 14, 56, 24} : Metrics{false, 20, 36, 8, 46, 20};
}

// "1 Format": the step's number in red and its name, over a hairline. Returns where its fields start.
int PaintStep(Painter &p, int x, int y, int w, int number, const std::string &title)
{
	UseDisplay(p, 30);
	p.raw = true;
	p.Text(x, y, 24, 34, std::to_string(number), pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	UseDisplay(p, 28);
	p.TextCaps(x + 26, y, w - 26, 34, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.Fill(x, y + 42, w, 1, pal::hairline);
	return y + 56;
}

// A field's caption, with a count of its characters at the right when `most` is given.
void PaintLabel(Painter &p, int x, int y, int w, int h, const std::string &text, const std::string &value = std::string(), int most = 0)
{
	PaintCaps(p, x, y, w - (most ? 80 : 0), h, text, pal::caption);
	if (!most) return;
	const int n = TourneyTextLength(TourneyTrim(value));
	p.Font(3);
	p.raw = true;
	p.Text(x + w - 80, y, 80, h, std::to_string(n) + "/" + std::to_string(most), n > most ? pal::bad : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
}

// What is wrong with a field, in red under it; returns how much room it took.
int PaintProblem(Painter &p, int x, int y, int w, const std::vector<TourneyCupProblem> &problems, std::initializer_list<TourneyCupProblem> which)
{
	for (TourneyCupProblem q : which) {
		if (!Has(problems, q)) continue;
		p.Font(3);
		p.raw = true;
		p.Text(x, y + 2, w, 20, CupProblemText(q), pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		return 24;
	}
	return 0;
}

// A choice of a list with a radio and a line under it, as the mockup's; the texts come translated.
void PaintOption(Painter &p, int x, int y, int w, int h, bool on, const std::string &title, const std::string &text, int arg, int mx, int my)
{
	const bool hot = !on && p.Hover(x, y, w, h, mx, my);
	p.Fill(x, y, w, h, on ? pal::selected : pal::raised);
	p.Frame(x, y, w, h, on ? pal::red : hot ? pal::edgeHot : pal::hairline);
	const int r = 18, rx = x + 14, ry = y + (h - r) / 2;
	p.Shape(rx, ry, r, r, InUnitEllipse, on ? pal::redText : pal::edgeHot);
	p.Shape(rx + 2, ry + 2, r - 4, r - 4, InUnitEllipse, on ? pal::selected : pal::raised);
	if (on) p.Shape(rx + 5, ry + 5, r - 10, r - 10, InUnitEllipse, pal::redText);
	const int tx = rx + r + 14, tw = x + w - 12 - tx;
	const bool two = h >= 52;
	UseFace(p, BrandFace::UiStrong, 16);
	p.raw = true;
	p.Text(tx, two ? y + 7 : y, tw, two ? 22 : h / 2 + 4, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(tx, two ? y + 29 : y + h / 2 + 2, tw, 20, text, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	if (!on) p.AddHit(x, y, w, h, kTourney, arg);
}

// The round a map of the form is for: its stage, "Semifinal onwards" for the last one, or nothing past the final.
std::string MapRoundText(int index, int maps, int rounds)
{
	const int round = index + 1;
	if (round > rounds) return Tr("Not used");
	const std::string stage = StageText(TourneyStageOf(round, rounds), round);
	if (index + 1 == maps && round < rounds) return TrFmt("%s onwards", {stage});
	return stage;
}

std::string SeriesText(int bestOf)
{
	return bestOf <= 1 ? Tr("1 game") : TrFmt("Best of %s", {std::to_string(bestOf)});
}

void PaintFormatColumn(Painter &p, const State &st, const Ui &u, const std::vector<TourneyCupProblem> &problems, const Metrics &mt, int x, int y, int w)
{
	const int mx = st.mouseX, my = st.mouseY;
	const TourneyCupForm &f = u.cup;
	y = PaintStep(p, x, y, w, 1, "Format");
	PaintLabel(p, x, y, w, mt.labelH, "Name", f.name, kTourneyCupNameMax);
	y += mt.labelH + 4;
	PaintEditField(p, x, y, w, mt.fieldH, f.name, TrFmt("%s's cup", {st.player}), st.focus == kCupNameField, false, st.edit, kFieldCupName);
	y += mt.fieldH;
	y += PaintProblem(p, x, y, w, problems, {TourneyCupProblem::NameShort, TourneyCupProblem::NameLong}) + mt.gap;
	PaintLabel(p, x, y, w, mt.labelH, "Description (optional)", f.description, kTourneyCupDescriptionMax);
	y += mt.labelH + 4;
	PaintEditField(p, x, y, w, mt.fieldH, f.description, Tr("Rules, a date to remember, who it is for..."), st.focus == kCupDescriptionField, false, st.edit, kFieldCupDescription);
	y += mt.fieldH;
	y += PaintProblem(p, x, y, w, problems, {TourneyCupProblem::DescriptionLong}) + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Places");
	y += mt.labelH + 4;
	PaintSegments(p, x, y, w, mt.fieldH, {"4", "8", "16"}, f.maxEntries == 4 ? 0 : f.maxEntries == 8 ? 1 : f.maxEntries == 16 ? 2 : -1,
		{CupArg(kCupPlaces, 4), CupArg(kCupPlaces, 8), CupArg(kCupPlaces, 16)}, mx, my);
	y += mt.fieldH + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Minimum to start");
	y += mt.labelH + 4;
	const int bw = mt.fieldH + 4, nw = 64;
	p.Button(x, y, bw, mt.fieldH, "-", kTourney, CupArg(kCupMinimum, 0), mx, my, f.minEntries > 2);
	p.Fill(x + bw + 2, y, nw, mt.fieldH, pal::field);
	UseFace(p, BrandFace::UiStrong, 18);
	p.raw = true;
	p.Text(x + bw + 2, y, nw, mt.fieldH, std::to_string(f.minEntries), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	p.Button(x + bw + nw + 4, y, bw, mt.fieldH, "+", kTourney, CupArg(kCupMinimum, 1), mx, my, f.minEntries < f.maxEntries);
	p.Font(3);
	const int hx = x + 2 * bw + nw + 18;
	p.Text(hx, y, x + w - hx, mt.fieldH, "Fewer checked in at the start, and the cup is cancelled.", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	y += mt.fieldH;
	y += PaintProblem(p, x, y, w, problems, {TourneyCupProblem::Places, TourneyCupProblem::Minimum}) + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Rounds");
	y += mt.labelH + 4;
	PaintSegments(p, x, y, w, mt.fieldH, {SeriesText(1), SeriesText(3)}, f.bestOf == 3 ? 1 : f.bestOf == 1 ? 0 : -1, {CupArg(kCupBestOf, 1), CupArg(kCupBestOf, 3)}, mx, my);
	y += mt.fieldH + mt.gap;
	PaintLabel(p, x, y, w, mt.labelH, "Final");
	y += mt.labelH + 4;
	PaintSegments(p, x, y, w, mt.fieldH, {SeriesText(1), SeriesText(3), SeriesText(5)}, f.finalBestOf == 1 ? 0 : f.finalBestOf == 3 ? 1 : f.finalBestOf == 5 ? 2 : -1,
		{CupArg(kCupFinal, 1), CupArg(kCupFinal, 3), CupArg(kCupFinal, 5)}, mx, my);
	y += mt.fieldH;
	PaintProblem(p, x, y, w, problems, {TourneyCupProblem::Series});
}

void PaintMapsColumn(Painter &p, const State &st, const Ui &u, const std::vector<TourneyCupProblem> &problems, const Metrics &mt, int x, int y, int w)
{
	const int mx = st.mouseX, my = st.mouseY;
	const TourneyCupForm &f = u.cup;
	y = PaintStep(p, x, y, w, 2, "Maps and rules");
	PaintLabel(p, x, y, w, mt.labelH, "Maps: one per round, the last one repeats");
	y += mt.labelH + 4;
	const int rounds = TourneyRoundsFor(f.maxEntries), n = static_cast<int>(f.maps.size());
	const int roundW = std::min(170, w / 3), removeW = mt.fieldH, rowGap = mt.roomy ? 8 : 6;
	for (int i = 0; i < n; i++) {
		const std::string round = MapRoundText(i, n, rounds);
		p.Font(3);
		p.raw = true;
		p.Text(x, y, roundW - 8, mt.fieldH, round, i + 1 > rounds ? pal::disabled : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		const int cw = w - roundW - (n > 1 ? removeW + 6 : 0);
		const bool open = st.dropOpen && st.dropKind == kDropTourney && st.dropIndex == i;
		p.raw = true;
		PaintCombo(p, x + roundW, y, cw, mt.fieldH, MapText(f.maps[static_cast<size_t>(i)]), open, true, mx, my, kTourney, ArgOf(kSubCupMapMenu, i), &g_dropAnchor);
		p.raw = false;
		if (n > 1) p.Button(x + w - removeW, y, removeW, mt.fieldH, "\xC3\x97", kTourney, CupArg(kCupMapRemove, i), mx, my);
		y += mt.fieldH + rowGap;
	}
	if (n < kTourneyCupMapsMax) {
		const std::string add = Tr("Add a map");
		PaintSecondButton(p, x + roundW, y, std::min(w - roundW, SecondButtonWidth(p, add)), mt.fieldH, add, CupArg(kCupMapAdd, 0), true, mx, my);
		y += mt.fieldH;
	}
	y += PaintProblem(p, x, y, w, problems, {TourneyCupProblem::NoMaps, TourneyCupProblem::TooManyMaps, TourneyCupProblem::BadMap}) + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Seeding");
	y += mt.labelH + 4;
	PaintOption(p, x, y, w, mt.optionH, !f.randomSeeding, Tr("By Glory"), Tr("The best of the ranked leaderboard don't meet early."), CupArg(kCupSeeding, 0), mx, my);
	y += mt.optionH + 6;
	PaintOption(p, x, y, w, mt.optionH, f.randomSeeding, Tr("At random"), Tr("The bracket is drawn by lot."), CupArg(kCupSeeding, 1), mx, my);
	y += mt.optionH + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Content and rules");
	y += mt.labelH + 4;
	std::string key, name;
	OwnCupContent(key, name);
	const int boxH = mt.roomy ? 92 : 80;
	p.Fill(x, y, w, boxH, pal::raised);
	p.Frame(x, y, w, boxH, pal::hairline);
	UseFace(p, BrandFace::UiStrong, 16);
	p.raw = true;
	p.Text(x + 14, y + 8, w - 28, 24, key.empty() ? Tr("The mod's rules") : name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	p.Font(3);
	p.TextWrapped(x + 14, y + 34, w - 28, boxH - 40,
		key.empty() ? "Your content profile adds no rules: anyone can play. The rooms use the settings of the official cups."
					: "Your active content profile: only players with it active can check in. The rooms use the settings of the official cups.",
		pal::caption);
}

void PaintTimeColumn(Painter &p, const State &st, const Ui &u, const std::vector<TourneyCupProblem> &problems, const Metrics &mt, int x, int y, int w, int64_t now)
{
	const int mx = st.mouseX, my = st.mouseY;
	const TourneyCupForm &f = u.cup;
	y = PaintStep(p, x, y, w, 3, "Time and access");
	PaintLabel(p, x, y, w, mt.labelH, "Start time");
	y += mt.labelH + 4;
	const int qGap = 6, qw = (w - qGap * (kQuickCount - 1)) / kQuickCount;
	for (int i = 0; i < kQuickCount; i++) {
		p.Button(x + i * (qw + qGap), y, i + 1 == kQuickCount ? w - i * (qw + qGap) : qw, mt.fieldH, kQuickLabels[i], kTourney, CupArg(kCupQuickStart, i), mx, my, true, false,
			mt.roomy ? 1 : 3);
	}
	y += mt.fieldH + 10;
	const int whenH = mt.roomy ? 50 : 42;
	UseDisplay(p, mt.roomy ? 40 : 34);
	p.raw = true;
	p.TextCaps(x, y, w * 2 / 3, whenH, WhenText(f.startsAt), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.Font(3);
	p.Text(x + w * 2 / 3, y, w / 3, whenH, f.startsAt > now ? TrFmt("in %s", {DurationText(f.startsAt - now)}) : std::string(), pal::caption,
		DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	y += whenH + 6;
	const int sGap = 4, sw = (w - sGap * (kShiftCount - 1)) / kShiftCount, shiftH = mt.fieldH - 4;
	for (int i = 0; i < kShiftCount; i++) {
		p.Button(x + i * (sw + sGap), y, i + 1 == kShiftCount ? w - i * (sw + sGap) : sw, shiftH, kShiftLabels[i], kTourney, CupArg(kCupShift, i), mx, my, true, false, 3);
	}
	y += shiftH + 6;
	p.Font(3);
	p.raw = true;
	p.Text(x, y, w, 20, TrFmt("Check-in from %s to %s, in your computer's time", {HourText(f.startsAt - f.checkinMinutes * kMinuteMs), HourText(f.startsAt)}), pal::caption,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	y += 20;
	y += PaintProblem(p, x, y, w, problems, {TourneyCupProblem::StartSoon, TourneyCupProblem::StartLate}) + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Check-in");
	y += mt.labelH + 4;
	PaintSegments(p, x, y, w, mt.fieldH, {Tr("15 min before"), Tr("30 min before")}, f.checkinMinutes == 15 ? 0 : f.checkinMinutes == 30 ? 1 : -1,
		{CupArg(kCupCheckin, 15), CupArg(kCupCheckin, 30)}, mx, my);
	y += mt.fieldH;
	y += PaintProblem(p, x, y, w, problems, {TourneyCupProblem::Checkin}) + mt.gap;

	PaintLabel(p, x, y, w, mt.labelH, "Access");
	y += mt.labelH + 4;
	PaintOption(p, x, y, w, mt.optionH, !f.isPrivate, Tr("Open cup"), Tr("It shows in the list: anyone can sign up."), CupArg(kCupPrivate, 0), mx, my);
	y += mt.optionH + 6;
	PaintOption(p, x, y, w, mt.optionH, f.isPrivate, Tr("Private cup"), Tr("Only with its 6-character code, which you share."), CupArg(kCupPrivate, 1), mx, my);
	y += mt.optionH + mt.gap + 4;
	const bool hot = p.Hover(x, y, w, 26, mx, my);
	PaintBigToggle(p, x, y + 2, f.play, true, hot);
	UseFace(p, BrandFace::UiStrong, 16);
	const int playW = p.TextWidth("I play too") + 4, hintX = x + kBigToggleW + 12 + playW + 14;
	p.Text(x + kBigToggleW + 12, y, playW, 26, "I play too", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.Font(3);
	p.Text(hintX, y, x + w - hintX, 26, "You take a place in your own cup.", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.AddHit(x, y, w, 26, kTourney, CupArg(kCupPlay, f.play ? 0 : 1));
}

void PaintFooter(Painter &p, const State &st, const Ui &u, const std::vector<TourneyCupProblem> &problems, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 28;
	const TourneyCupForm &f = u.cup;
	PaintGlassCard(p, x, y, w, h);
	p.Fill(x, y, 4, h, pal::red);
	const bool busy = LeagueTourneyBusy(TourneyOp::Create, 0), linked = LeagueLinked();
	const std::string label = busy ? Tr("Creating...") : Tr("Create the cup");
	p.Font(1);
	const int bw = std::max(260, p.CapsWidth(label, 1) + 64), bh = std::min(52, h - 24), bx = x + w - pad - bw, by = y + (h - bh) / 2;
	p.Button(bx, by, bw, bh, label, kTourney, ArgOf(kSubCupSubmit, 0), mx, my, linked && !busy && problems.empty(), true);
	int right = bx - 20;
	if (!u.cupErrorSlug.empty()) {
		const std::string open = Tr("Open my cup");
		const int ow = SecondButtonWidth(p, open);
		PaintSecondButton(p, right - ow, by, ow, bh, open, ArgOf(kSubCupOpenActive, 0), true, mx, my);
		right -= ow + 16;
	}
	const int tx = x + pad, tw = right - tx;
	const std::string line = TrFmt("%s places", {std::to_string(f.maxEntries)}) + " \xC2\xB7 " + Tr("Rounds") + ": " + SeriesText(f.bestOf) + " \xC2\xB7 " + Tr("Final") + ": " +
		SeriesText(f.finalBestOf) + " \xC2\xB7 " + Tr(f.isPrivate ? "Private cup" : "Open cup");
	UseFace(p, BrandFace::UiStrong, 17);
	p.raw = true;
	p.Text(tx, y + h / 2 - 28, tw, 26, line, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	std::string note;
	COLORREF ink = pal::textDim;
	if (!linked) {
		note = Tr("Link your league account first.");
		ink = pal::warn;
	}
	else if (!u.cupError.empty()) {
		note = u.cupError;
		ink = pal::bad;
	}
	else if (!problems.empty()) {
		note = Tr("Fix what is marked in red to create the cup.");
		ink = pal::bad;
	}
	else {
		note = TrFmt("Starts %s. If fewer than %s players check in, the cup is cancelled. Community cups give no phalerae.", {WhenText(f.startsAt), std::to_string(f.minEntries)});
	}
	p.Text(tx, y + h / 2 + 2, tw, 24, note, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
}

}

TourneyCupForm NewCupForm(const std::string &player, int64_t nowMs)
{
	TourneyCupForm f;
	f.name = TrFmt("%s's cup", {player});
	while (TourneyTextLength(f.name) > kTourneyCupNameMax) f.name.erase(static_cast<size_t>(PrevCp(f.name, static_cast<int>(f.name.size()))));
	f.maps = {{"mediterranean", "random", "medium"}};
	f.minEntries = TourneyCupDefaultMinimum(f.maxEntries);
	f.startsAt = RoundUp(nowMs + kHourMs, 15 * kMinuteMs);
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

std::vector<TourneyMap> CupMapChoices()
{
	std::vector<TourneyMap> out;
	for (const std::string &type : LeagueMapTypes()) {
		for (const std::string &size : LeagueMapSizesOf(type)) out.push_back({type, "random", size});
	}
	return out;
}

void ApplyCupSetting(TourneyCupForm &f, int setting, int choice, int64_t nowMs)
{
	switch (setting) {
	case kCupPlaces:
		if (choice != 4 && choice != 8 && choice != 16) break;
		if (f.minEntries == TourneyCupDefaultMinimum(f.maxEntries) || f.minEntries > choice) f.minEntries = TourneyCupDefaultMinimum(choice);
		f.maxEntries = choice;
		break;
	case kCupMinimum: f.minEntries = std::max(2, std::min(f.maxEntries, f.minEntries + (choice ? 1 : -1))); break;
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
	case kCupPrivate: f.isPrivate = choice != 0; break;
	case kCupPlay: f.play = choice != 0; break;
	case kCupQuickStart:
		if (choice >= 0 && choice < kQuickCount) f.startsAt = RoundUp(nowMs + kQuickStarts[choice], 5 * kMinuteMs);
		break;
	case kCupShift:
		if (choice >= 0 && choice < kShiftCount) f.startsAt = std::max(RoundUp(nowMs, 5 * kMinuteMs), f.startsAt + kShifts[choice]);
		break;
	case kCupMapAdd:
		if (static_cast<int>(f.maps.size()) < kTourneyCupMapsMax) f.maps.push_back(f.maps.empty() ? TourneyMap{"mediterranean", "random", "medium"} : f.maps.back());
		break;
	case kCupMapRemove:
		if (f.maps.size() > 1 && choice >= 0 && choice < static_cast<int>(f.maps.size())) f.maps.erase(f.maps.begin() + choice);
		break;
	default: break;
	}
}

std::string CupProblemText(TourneyCupProblem problem)
{
	switch (problem) {
	case TourneyCupProblem::NameShort: return Tr("The name needs at least 3 characters.");
	case TourneyCupProblem::NameLong: return Tr("The name can have up to 40 characters.");
	case TourneyCupProblem::DescriptionLong: return Tr("The description can have up to 200 characters.");
	case TourneyCupProblem::Places: return Tr("Choose 4, 8 or 16 places.");
	case TourneyCupProblem::Minimum: return Tr("The minimum goes from 2 to the number of places.");
	case TourneyCupProblem::Series: return Tr("Choose the games of the rounds and of the final.");
	case TourneyCupProblem::NoMaps: return Tr("Choose at least one map.");
	case TourneyCupProblem::TooManyMaps: return Tr("Up to 5 maps.");
	case TourneyCupProblem::BadMap: return Tr("One of the maps is not in the league's list.");
	case TourneyCupProblem::StartSoon: return Tr("It has to start at least 20 minutes from now.");
	case TourneyCupProblem::StartLate: return Tr("It has to start within the next 14 days.");
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

bool PaintCreateHeader(Painter &p, const State &st, const Ui &)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int right = PaintHeaderActions(p, l, {{Tr("Tournaments"), kBack, true, true}}, mx, my);
	PaintScreenTitle(p, l, "Create a cup", {}, right, mx, my);
	PaintScreenInfo(p, l, "It is published as soon as you create it, and you can cancel it until it ends. A private cup only opens with its code.");
	return true;
}

void PaintCreateScreen(Painter &p, const State &st, const Ui &u)
{
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const Metrics mt = MetricsFor(l.roomy);
	const int64_t now = NowMs();
	const std::vector<TourneyCupProblem> problems = TourneyCheckCup(u.cup, now);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	const int footH = l.roomy ? 96 : 80, footGap = l.roomy ? 20 : 14;
	const int top = l.top, colH = bottom - footH - footGap - top, colW = (inner - 2 * kCardGap) / 3;
	for (int c = 0; c < 3; c++) {
		const int cx = m + c * (colW + kCardGap);
		PaintGlassCard(p, cx, top, colW, colH);
		p.Clip(cx, top, colW, colH);
		const int x = cx + mt.pad, y = top + (l.roomy ? 20 : 14), w = colW - 2 * mt.pad;
		if (c == 0) PaintFormatColumn(p, st, u, problems, mt, x, y, w);
		else if (c == 1) PaintMapsColumn(p, st, u, problems, mt, x, y, w);
		else PaintTimeColumn(p, st, u, problems, mt, x, y, w, now);
		p.Unclip();
	}
	PaintFooter(p, st, u, problems, m, bottom - footH, inner, footH);
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
