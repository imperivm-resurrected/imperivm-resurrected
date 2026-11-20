// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/tournament.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/patrons.h"
#include "ui/fullscreen.h"
#include "ui/leagueui.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {
namespace {

// What the player can do with a tournament from the list: the one button of its row or of the featured card.
struct Act {
	std::string label;
	int arg = 0;
	bool strong = false, enabled = true;
	// A state instead of a button: "Signed up", "Checked in"; `waiting` when it is not done yet, as a team waiting for its partner.
	std::string done;
	bool waiting = false;
};

Act ActionFor(const TourneySummary &t, const TourneyMineView &mine)
{
	Act a;
	const TourneyMyEntry *e = MyEntry(mine, t.id);
	const bool in = e && TourneyEntryActive(e->status);
	const bool needsMe = in && t.status == TourneyStatus::Checkin && !e->checkedIn;
	if (IsOrganizer(t) && t.status != TourneyStatus::Finished && t.status != TourneyStatus::Cancelled && !needsMe) {
		a.label = "Manage";
		a.arg = ArgOf(kSubManage, t.id);
		return a;
	}
	switch (t.status) {
	case TourneyStatus::Signup:
	case TourneyStatus::Checkin:
		if (!LeagueLinked()) {
			a.label = "Link to sign up";
			a.arg = -1;
			a.strong = true;
		}
		else if (!in && TourneyInviteOf(mine.mine, t.id)) {
			// An invitation to a team: its answer is on the cup's page.
			a.label = "Answer";
			a.arg = ArgOf(kSubOpen, t.id);
			a.strong = true;
		}
		else if (!in && e && e->Pending()) {
			a.done = "Pending";
			a.waiting = true;
		}
		else if (!in) {
			const bool busy = LeagueTourneyBusy(TourneyOp::SignUp, t.id);
			a.label = busy ? "Signing up..." : "Sign up";
			a.arg = ArgOf(t.Teams() ? kSubTeamOpen : kSubSignUp, t.id);
			a.strong = true;
			// A cup whose requirements the player doesn't meet: its row says why.
			a.enabled = !busy && CupUnmet(t.requirements, mine) == TourneyUnmet::None;
		}
		else if (t.status == TourneyStatus::Checkin && !e->checkedIn) {
			const bool busy = LeagueTourneyBusy(TourneyOp::CheckIn, t.id);
			a.label = busy ? "Checking in..." : "Check in";
			a.arg = ArgOf(kSubCheckIn, t.id);
			a.strong = true;
			a.enabled = !busy;
		}
		else a.done = e->checkedIn ? "Checked in" : e->status == TourneyEntryStatus::Waitlist ? "Waiting list" : "Signed up";
		break;
	case TourneyStatus::Running:
		if (mine.mine.next.present && mine.mine.next.tournamentId == t.id) {
			a.label = "Your match";
			a.arg = ArgOf(kSubMatch, mine.mine.next.match);
			a.strong = true;
		}
		else {
			a.label = "Bracket";
			a.arg = ArgOf(kSubOpen, t.id);
		}
		break;
	default:
		a.label = "Result";
		a.arg = ArgOf(kSubChampion, t.id);
		break;
	}
	return a;
}

void PaintAct(Painter &p, const Act &a, int x, int y, int w, int h, int mx, int my)
{
	if (!a.done.empty()) {
		if (a.waiting) p.Shape(x + 5, y + h / 2 - 4, 8, 8, InUnitEllipse, pal::warn);
		else PaintCheck(p, x, y + (h - 18) / 2, 18, true);
		p.Font(1);
		p.Text(x + 26, y, w - 26, h, a.done, a.waiting ? pal::warn : pal::ok, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	if (a.label.empty()) return;
	if (a.arg == -1) p.Button(x, y, w, h, a.label, kLeagueLink, 0, mx, my, true, true);
	else p.Button(x, y, w, h, a.label, kTourney, a.arg, mx, my, a.enabled, a.strong);
}

std::string StatusCaption(const TourneySummary &t)
{
	switch (t.status) {
	case TourneyStatus::Signup: return Tr("Sign-up open");
	case TourneyStatus::Checkin: return Tr("Check-in open");
	case TourneyStatus::Running: return Tr("Live");
	case TourneyStatus::Finished: return Tr("Over");
	default: return Tr("Cancelled");
	}
}

std::string WindowText(const TourneySummary &t)
{
	switch (t.status) {
	case TourneyStatus::Signup: return TrFmt("Check-in %s, from %s to %s", {DayText(t.checkinOpensAt), HourText(t.checkinOpensAt), HourText(t.startsAt)});
	case TourneyStatus::Checkin: return TrFmt("Check-in open until %s", {HourText(t.startsAt)});
	case TourneyStatus::Running: return !RoundText(t).empty() ? RoundText(t) : TrFmt("Started at %s", {HourText(t.startsAt)});
	default: return TrFmt("Played on %s", {DayText(t.startsAt)});
	}
}

void PaintFeatured(Painter &p, const State &st, const TourneySummary *t, const TourneyMineView &mine, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	PaintGlassCard(p, x, y, w, h);
	p.Fill(x, y, 4, h, pal::red);
	const int logoW = 150, rightW = std::min(330, w / 3), pad = 32;
	PaintIsotype(p, x + pad + logoW / 2, y + h / 2 - 18, std::min(120, h - 90), 255);
	if (!t) {
		const int tx = x + pad + logoW + pad;
		PaintCaps(p, tx, y + 40, w - tx + x - pad, 20, "Official cups", pal::redText);
		UseDisplay(p, 46);
		p.TextCaps(tx, y + 66, w - tx + x - pad, 56, "No cup open right now", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.Font(0);
		p.TextWrapped(tx, y + 132, w - tx + x - pad, 60, "The official cups are announced on Discord and open here for sign-up a few days before.", pal::textDim);
		return;
	}
	const int tagW = TagWidth(p, t->official ? "Official" : "Community");
	PaintTag(p, x + pad + (logoW - tagW) / 2, y + h - 52, t->official ? "Official" : "Community", pal::red, pal::onRed);
	const int tx = x + pad + logoW + pad, rx = x + w - pad - rightW, tw = rx - pad - tx;
	PaintCaps(p, tx, y + 28, tw, 20, Tr(t->official ? "Official cup" : "Tournament") + " \xC2\xB7 " + StatusCaption(*t), pal::redText);
	UseDisplay(p, h >= 220 ? 58 : 48);
	p.raw = true;
	p.TextCaps(tx, y + 50, tw, h >= 220 ? 62 : 52, t->name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	int ly = y + (h >= 220 ? 120 : 106);
	p.Font(0);
	p.raw = true;
	const std::string pace = t->pace == TourneyPace::Rounds ? "   \xC2\xB7   " + PaceText(*t) : std::string();
	p.Text(tx, ly, tw, 24, WhenText(t->startsAt) + "   \xC2\xB7   " + FormatText(*t) + pace + "   \xC2\xB7   " + ContentText(*t) + ", " + PresetText(t->settings), pal::body,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	ly += 36;
	if (t->official) {
		DrawLeaguePhalera(p, tx, ly, 22, LeagueMetal::Gold);
		p.Font(0);
		p.Text(tx + 32, ly - 1, tw - 32, 24, "Gold, silver and bronze phalerae for the podium, next to the name for ever", pal::textDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	// The places, who signed up and what to do.
	int ry = y + 30;
	UseDisplay(p, 40);
	p.raw = true;
	const std::string count = std::to_string(t->entries);
	const int countW = p.TextWidth(count) + 4;
	p.Text(rx, ry, countW, 44, count, pal::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
	p.Font(0);
	p.Text(rx + countW + 6, ry, rightW - countW - 6, 40, TrFmt(t->Teams() ? "of %s teams signed up" : "of %s signed up", {std::to_string(t->maxEntries)}), pal::caption,
		DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
	const int left = std::max(0, t->maxEntries - t->entries);
	p.Font(3);
	if (t->status == TourneyStatus::Signup || t->status == TourneyStatus::Checkin) {
		p.Text(rx, ry, rightW, 40, left ? TrFmt("%s places", {std::to_string(left)}) : Tr("Waiting list"), pal::caption, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
	}
	p.raw = false;
	ry += 50;
	p.Fill(rx, ry, rightW, 6, pal::button);
	if (t->maxEntries > 0) p.Fill(rx, ry, rightW * std::min(t->entries, t->maxEntries) / t->maxEntries, 6, pal::red);
	ry += 18;
	const auto page = LeagueTourneyPage(t->slug, 30000);
	std::vector<const TourneyEntrant *> who;
	for (const TourneyEntrant &e : page->detail.entrants) {
		if (e.status == TourneyEntryStatus::Signed || e.status == TourneyEntryStatus::Playing || e.status == TourneyEntryStatus::Out) who.push_back(&e);
	}
	if (!who.empty() && h >= 220) {
		int ax = rx;
		for (size_t i = 0; i < who.size() && i < 6; i++) {
			PaintPlayerDisc(p, ax, ry, 30, who[i]->steamId, who[i]->name, pal::panel);
			ax += 24;
		}
		std::string names = who[0]->name;
		if (who.size() == 2) names = who[0]->name + ", " + who[1]->name;
		else if (who.size() > 2) names = TrFmt("%s, %s and %s more", {who[0]->name, who[1]->name, std::to_string(who.size() - 2)});
		p.Font(3);
		p.raw = true;
		p.Text(ax + 14, ry, rx + rightW - ax - 14, 30, names, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ry += 40;
	}
	p.Font(3);
	p.raw = true;
	p.Text(rx, ry, rightW, 22, WindowText(*t), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	const int by = y + h - 30 - 40, viewW = 96;
	const Act a = ActionFor(*t, mine);
	PaintAct(p, a, rx, by, rightW - viewW - 10, 40, mx, my);
	p.Button(rx + rightW - viewW, by, viewW, 40, "View", kTourney, ArgOf(kSubOpen, t->id), mx, my);
}

// "Create a cup" and "Join with a code" at the right of the list's tabs; returns where they start.
int PaintCupButtons(Painter &p, const TourneyMineView &mine, int right, int y, int h, int mx, int my)
{
	const TourneyOrganize &o = mine.mine.organize;
	std::string label = Tr("Create a cup");
	bool enabled = true;
	if (o.activeId) label = Tr("Manage my cup");
	else if (LeagueLinked() && mine.loaded && o.known && !o.allowed) {
		const int missing = std::max(1, o.GamesMissing());
		label = missing == 1 ? Tr("Create a cup \xC2\xB7 1 online game to go") : TrFmt("Create a cup \xC2\xB7 %s online games to go", {std::to_string(missing)});
		enabled = false;
	}
	p.Font(1);
	p.raw = true;
	const int cw = p.CapsWidth(label, 1) + 48;
	p.Button(right - cw, y, cw, h, label, kTourney, ArgOf(kSubCreate, 0), mx, my, enabled, true);
	p.raw = false;
	const std::string join = Tr("Join with a code");
	const int jw = SecondButtonWidth(p, join);
	PaintSecondButton(p, right - cw - 12 - jw, y, jw, h, join, ArgOf(kSubJoin, 0), true, mx, my);
	return right - cw - 12 - jw;
}

void PaintSubTabs(Painter &p, const Ui &u, const TourneyListsView &lists, int x, int y, int h, int mx, int my)
{
	struct Tab {
		const char *label;
		size_t count;
	} tabs[] = {{"Sign-up open", lists.open.size()}, {"Live", lists.live.size()}, {"Finished", 0}};
	for (int i = 0; i < 3; i++) {
		UseFace(p, BrandFace::UiStrong, 13);
		const int textW = p.CapsWidth(tabs[i].label, 2);
		const std::string n = tabs[i].count ? std::to_string(tabs[i].count) : std::string();
		const int countW = n.empty() ? 0 : 12 + static_cast<int>(n.size()) * 8;
		const int w = textW + (countW ? countW + 8 : 0);
		const bool on = u.filter == i, hot = p.Hover(x, y, w, h, mx, my);
		p.TextCaps(x, y, textW + 2, h - 10, tabs[i].label, on || hot ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
		if (countW) {
			p.Fill(x + textW + 8, y + (h - 10 - 20) / 2, countW, 20, pal::button);
			UseFace(p, BrandFace::Ui, 12);
			p.raw = true;
			p.Text(x + textW + 8, y + (h - 10 - 20) / 2, countW, 20, n, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
		}
		if (on) p.Fill(x, y + h - 2, w, 2, pal::red);
		else p.AddHit(x, y, w, h, kTourney, ArgOf(kSubFilter, i));
		x += w + 28;
	}
}

// Whether the list's filters let a cup through.
bool Shown(const TourneySummary &t, const Ui &u, const TourneyMineView &mine)
{
	if ((u.listKind == 1 && !t.official) || (u.listKind == 2 && t.official)) return false;
	const int format = TourneyIsSwiss(t) ? 3 : TourneyIsDouble(t) ? 2 : 1;
	if (u.listFormat && u.listFormat != format) return false;
	if (!u.listPlayable || u.filter != 0) return true;
	const bool open = t.status == TourneyStatus::Signup || t.status == TourneyStatus::Checkin;
	return open && !t.isPrivate && CupUnmet(t.requirements, mine) == TourneyUnmet::None;
}

// A filter of the list: its choice now with the arrow of its menu (which opens under it), or lit while it is on. Its width.
int PaintFilter(Painter &p, int x, int y, int h, const std::string &label, bool on, bool cycle, int sub, int mx, int my, bool open = false)
{
	UseFace(p, BrandFace::Ui, 13);
	p.raw = true;
	const int w = p.TextWidth(label) + (cycle ? 44 : 28);
	const bool hot = p.Hover(x, y, w, h, mx, my);
	if (on) p.Fill(x, y, w, h, pal::selected);
	p.Frame(x, y, w, h, on || hot ? pal::edgeHot : pal::edge);
	p.Text(x + 14, y, w - 28, h, label, on || hot ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	if (cycle) {
		const int cx = x + w - 20, cy = y + h / 2 - 2;
		p.Line(cx - 4, cy, cx, cy + 4, pal::caption, 2);
		p.Line(cx, cy + 4, cx + 4, cy, pal::caption, 2);
		// Its menu opens under it, at least wide enough for its longest choice.
		if (open) g_dropAnchor = {x, y, x + std::max(w, 230), y + h};
	}
	p.AddHit(x, y, w, h, kTourney, ArgOf(sub, 0));
	return w;
}

void PaintTable(Painter &p, const State &st, const Ui &u, const TourneyListsView &lists, const TourneyMineView &mine, int x, int y, int w, int h, PaintOut &out)
{
	const int mx = st.mouseX, my = st.mouseY;
	PaintGlassCard(p, x, y, w, h);
	const std::vector<TourneySummary> &all = u.filter == 1 ? lists.live : u.filter == 2 ? lists.finished : lists.open;
	std::vector<const TourneySummary *> rows;
	for (const TourneySummary &t : all) {
		if (Shown(t, u, mine)) rows.push_back(&t);
	}
	{
		static const char *const kKinds[] = {"Official and community", "Official only", "Community only"};
		static const char *const kFormats[] = {"All formats", "Single elimination", "Double elimination", "Swiss"};
		int fx = x + 16;
		const bool menu = st.dropOpen && st.dropKind == kDropTourney;
		fx += PaintFilter(p, fx, y + 12, 32, Tr(kKinds[u.listKind % 3]), u.listKind != 0, true, kSubListKind, mx, my, menu && st.dropIndex == kCupMenuListKind) + 10;
		fx += PaintFilter(p, fx, y + 12, 32, Tr(kFormats[u.listFormat % 4]), u.listFormat != 0, true, kSubListFormat, mx, my,
				  menu && st.dropIndex == kCupMenuListFormat) +
			10;
		if (u.filter == 0) PaintFilter(p, fx, y + 12, 32, Tr("Only the ones I can play"), u.listPlayable, false, kSubListPlayable, mx, my);
		y += 48;
		h -= 48;
	}
	// The mockup's grid: the name takes what the other columns leave.
	const int pad = 16, gap = 16;
	// "When" has room for a round's deadline, "until Thursday 20:00".
	int colW[6] = {0, 210, 130, 160, 190, 150};
	const float fit = std::min(1.0f, (w - 2 * pad - 5 * gap - 260) / 840.0f);
	for (int c = 1; c < 6; c++) colW[c] = static_cast<int>(colW[c] * std::max(0.6f, fit));
	colW[0] = w - 2 * pad - 5 * gap - colW[1] - colW[2] - colW[3] - colW[4] - colW[5];
	int cx[6];
	cx[0] = x + pad;
	for (int c = 1; c < 6; c++) cx[c] = cx[c - 1] + colW[c - 1] + gap;
	static const char *const kHeads[] = {"Tournament", "Format", "Places", "Content", "When"};
	for (int c = 0; c < 5; c++) PaintCaps(p, cx[c], y + 14, colW[c], 18, kHeads[c], pal::caption);
	const int top = y + 44, rowH = 68, viewH = y + h - top;
	const int total = static_cast<int>(rows.size()) * rowH;
	const int maxScroll = std::max(0, total - viewH), scroll = std::min(u.listScroll, maxScroll);
	SetScrollArea(p, out.listArea, x, top, w, viewH, maxScroll);
	if (rows.empty() && !lists.loaded && !lists.failed) {
		std::vector<std::pair<int, int>> bars;
		for (int c = 0; c < 5; c++) bars.push_back({cx[c], colW[c]});
		PaintSkeletonRows(p, x + 1, top, w - 2, rowH, std::max(1, std::min(6, viewH / rowH)), bars);
		return;
	}
	if (rows.empty()) {
		p.Font(0);
		const char *empty = !lists.loaded ? "Couldn't reach the league server. Check your connection and try again."
			: !all.empty()	? "No tournament matches these filters."
			: u.filter == 0 ? "No tournament is open for sign-up now. The official cups are announced on Discord."
			: u.filter == 1 ? "No tournament is being played now."
							: "No tournament has finished yet.";
		p.TextWrapped(x + 24, top + 20, w - 48, 80, empty, pal::textDim);
		return;
	}
	p.Clip(x + 1, top, w - 2, viewH);
	for (size_t i = 0; i < rows.size(); i++) {
		const TourneySummary &t = *rows[i];
		const int ry = top + static_cast<int>(i) * rowH - scroll;
		if (ry + rowH < top || ry > top + viewH) continue;
		p.Fill(x + 1, ry, w - 2, 1, pal::hairline);
		const int vy = std::max(ry, top), vh = std::min(ry + rowH, top + viewH) - vy;
		if (vh > 0 && p.Hover(x, vy, w, vh, mx, my)) p.Fill(x + 1, ry + 1, w - 2, rowH - 1, pal::raised);
		if (vh > 0) p.AddHit(x, vy, w, vh, kTourney, ArgOf(kSubOpen, t.id));
		const TourneyMyEntry *e = MyEntry(mine, t.id);
		// A cup of teams says so after its name, where the row has room for it.
		const int modeW = t.Teams() ? TagWidth(p, "2v2") : 0;
		p.Font(1);
		p.raw = true;
		const int titleW = t.Teams() ? std::min(colW[0] - modeW - 10, p.TextWidth(t.name) + 2) : colW[0];
		p.Text(cx[0], ry + 12, titleW, 24, t.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (t.Teams()) PaintTag(p, cx[0] + titleW + 10, ry + 13, "2v2", pal::edgeHot, pal::textDim, true);
		p.Font(3);
	const bool mineIn = e && (TourneyEntryActive(e->status) || e->placement);
		const std::string organizer = OrganizerText(t);
		std::string sub = t.official ? Tr("Official cup") : organizer.empty() ? Tr("Community") : organizer;
		const TourneyInvite *invite = !mineIn ? TourneyInviteOf(mine.mine, t.id) : nullptr;
		if (e && e->placement) sub = TrFmt("You finished in place %s", {std::to_string(e->placement)});
		else if (mineIn) sub = Tr(e->status == TourneyEntryStatus::Waitlist ? "You are on the waiting list" : t.Teams() ? "Your team is in" : "You are in");
		else if (invite) sub = TrFmt("%s invites you to their team", {invite->from.name.empty() ? Tr("A friend") : invite->from.name});
		else if (e && e->Pending()) sub = Tr("Waiting for your partner");
		if (!t.official && (mineIn || (e && e->placement)) && !organizer.empty()) sub = organizer + " \xC2\xB7 " + sub;
		COLORREF subInk = mineIn || IsOrganizer(t) || invite ? pal::redText : e && e->Pending() ? pal::warn : pal::caption;
		const bool open = t.status == TourneyStatus::Signup || t.status == TourneyStatus::Checkin;
		if (open && !mineIn && !invite && !(e && e->Pending()) && !IsOrganizer(t) && t.requirements.Any()) {
			const TourneyUnmet unmet = CupUnmet(t.requirements, mine);
			sub += " \xC2\xB7 " + (unmet == TourneyUnmet::None ? RequirementsText(t.requirements) : TrFmt("you can't sign up: %s", {RequirementsText(t.requirements)}));
			if (unmet != TourneyUnmet::None) subInk = pal::bad;
		}
		p.raw = true;
		p.Text(cx[0], ry + 36, colW[0], 20, sub, subInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		// The format over its pace; a cup by rounds being played, its round over its deadline.
		p.Font(0);
		p.raw = true;
		// The format's name alone: the column is narrow at 1280 x 720, and the mode is after the cup's name.
		p.Text(cx[1], ry + 12, colW[1], 24, Tr(TourneyIsSwiss(t) ? "Swiss" : TourneyIsDouble(t) ? "Double elimination" : "Single elimination"), pal::body,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(cx[2], ry + 14, colW[2], 22, TrFmt(t.Teams() ? "%s of %s teams" : "%s of %s players", {std::to_string(t.entries), std::to_string(t.maxEntries)}), pal::body,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		const int barW = std::min(110, colW[2]);
		p.Fill(cx[2], ry + 42, barW, 4, pal::button);
		if (t.maxEntries > 0) p.Fill(cx[2], ry + 42, barW * std::min(t.entries, t.maxEntries) / t.maxEntries, 4, pal::textDim);
		p.Text(cx[3], ry, colW[3], rowH, ContentText(t), pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		const int total = TourneyRoundCount(t);
		const bool round = !RoundText(t).empty();
		if (round) {
			p.Text(cx[4], ry + 12, colW[4], 24,
				total >= t.currentRound ? TrFmt("Round %s of %s", {std::to_string(t.currentRound), std::to_string(total)}) : TrFmt("Round %s", {std::to_string(t.currentRound)}),
				pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		else {
			p.Text(cx[4], ry, colW[4], rowH, t.status == TourneyStatus::Finished ? DayText(t.startsAt) : WhenText(t.startsAt), pal::body,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		p.Font(3);
		p.Text(cx[1], ry + 36, colW[1], 20, PaceText(t), t.pace == TourneyPace::Rounds ? pal::textDim : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (round && t.roundEndsAt > 0) {
			p.Text(cx[4], ry + 36, colW[4], 20, UntilText(t.roundEndsAt), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		p.raw = false;
		PaintAct(p, ActionFor(t, mine), cx[5], ry + (rowH - 38) / 2, colW[5], 38, mx, my);
	}
	p.Unclip();
	if (maxScroll > 0) PaintScrollbar(p, kBarList, x + w - 10, top, viewH, {scroll, maxScroll, viewH}, st.dropBar, mx, my);
}

// The match being played that the side card shows: a live one, else one ready, of the first tournament being played.
const TourneyMatch *LiveMatch(const TourneyDetail &d)
{
	const TourneyMatch *ready = nullptr;
	for (const TourneyMatch &m : d.matches) {
		if (m.status == TourneyMatchStatus::Live) return &m;
		if (m.status == TourneyMatchStatus::Ready && !ready) ready = &m;
	}
	return ready;
}

int PaintLiveCard(Painter &p, const State &st, const TourneyListsView &lists, int x, int y, int w, std::vector<uint64_t> &ids)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 24;
	// The first cup being played, unless its page already knows it is not any more (cancelled or over since the list was read).
	const TourneySummary *t = nullptr;
	for (const TourneySummary &s : lists.live) {
		const auto known = LeagueTourneyPage(s.slug, 15000);
		if (known->loaded && known->detail.status != TourneyStatus::Running) continue;
		t = &s;
		break;
	}
	const auto page = LeagueTourneyPage(t ? t->slug : std::string(), 15000);
	const TourneyMatch *m = page->loaded ? LiveMatch(page->detail) : nullptr;
	const int h = m ? 248 : t ? 150 : 104;
	PaintGlassCard(p, x, y, w, h);
	p.Shape(x + pad, y + 28, 8, 8, InUnitEllipse, t ? pal::red : pal::disabled);
	PaintCaps(p, x + pad + 18, y + 22, w - 2 * pad, 20, "Live", t ? pal::redText : pal::caption);
	if (!t) {
		p.Font(0);
		p.Text(x + pad, y + 54, w - 2 * pad, 26, "Nothing is being played now.", pal::textDim);
		return h;
	}
	int cy = y + 52;
	UseFace(p, BrandFace::UiStrong, 20);
	p.raw = true;
	p.Text(x + pad, cy, w - 2 * pad, 28, t->name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	cy += 30;
	p.Font(3);
	p.raw = true;
	p.Text(x + pad, cy, w - 2 * pad, 20, (m ? StageText(m->stage, m->round) + " \xC2\xB7 " : std::string()) + SideCountText(*t, t->entries), pal::caption,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	cy += 34;
	if (!m) {
		p.Button(x + pad, cy, w - 2 * pad, 38, "See the bracket", kTourney, ArgOf(kSubOpen, t->id), mx, my);
		return h;
	}
	const int half = (w - 2 * pad - 50) / 2;
	// A team shows its players' discs and its name; a player, their disc and their name with its badge.
	const std::vector<TourneyMember> as = TourneyPlayersOf(m->a), bs = TourneyPlayersOf(m->b);
	const int aw = PaintSideDiscs(p, x + pad, cy, 32, as), bw = SideDiscsWidth(32, bs.size());
	p.Font(1);
	if (t->Teams()) {
		p.raw = true;
		p.Text(x + pad + aw + 10, cy, half - aw - 10, 32, m->a.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	else PaintName(p, x + pad + 42, cy, half - 42, 32, m->a.name, m->a.steamId, pal::text, mx, my);
	UseDisplay(p, 26);
	p.TextCaps(x + pad + half, cy, 50, 32, "Vs", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	PaintSideDiscs(p, x + w - pad - bw, cy, 32, bs);
	p.Font(1);
	p.raw = true;
	p.Text(x + pad + half + 50, cy, half - bw - 10, 32, m->b.name, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	for (const std::vector<TourneyMember> *side : {&as, &bs}) {
		for (const TourneyMember &pl : *side) ids.push_back(pl.steamId);
	}
	cy += 46;
	p.Font(3);
	p.raw = true;
	const std::string played = m->status == TourneyMatchStatus::Live && m->startedAt ? TrFmt("%s of play", {DurationText(NowMs() - m->startedAt)}) : Tr("Waiting for the players");
	p.Text(x + pad, cy, w - 2 * pad, 20, played + " \xC2\xB7 " + (TourneySpecDelay(t->settings) ? TrFmt("spectators %s s behind", {std::to_string(TourneySpecDelay(t->settings))}) : Tr("no spectators")), pal::textDim,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	cy += 34;
	const bool watchable = m->status == TourneyMatchStatus::Live && m->lobby;
	p.Button(x + pad, cy, w - 2 * pad, 38, watchable ? "Watch the game" : "See the bracket", kTourney, watchable ? ArgOf(kSubWatch, m->id) : ArgOf(kSubOpen, t->id), mx, my);
	return h;
}

// A line of "My tournaments": an invitation to a team, with its answers, a cup the player organises, or one they are in.
struct MineRow {
	std::string name, line;
	int arg = 0;
	bool warn = false;
	// An invitation: its cup, which its two buttons answer.
	int64_t invite = 0;
	int Height() const { return invite ? 102 : 62; }
};

std::string OrganizedLine(const TourneyOrganized &c, bool &warn)
{
	warn = false;
	switch (c.status) {
	case TourneyStatus::Signup:
		return TrFmt("You organise it \xC2\xB7 %s of %s \xC2\xB7 starts %s", {std::to_string(c.entries), std::to_string(c.maxEntries), WhenText(c.startsAt)});
	case TourneyStatus::Checkin: return TrFmt("You organise it \xC2\xB7 %s of %s checked in", {std::to_string(c.checkedIn), std::to_string(c.entries)});
	case TourneyStatus::Running:
		warn = c.openDisputes > 0;
		if (c.openDisputes == 1) return Tr("You organise it \xC2\xB7 1 dispute to settle");
		if (c.openDisputes > 1) return TrFmt("You organise it \xC2\xB7 %s disputes to settle", {std::to_string(c.openDisputes)});
		return Tr("You organise it \xC2\xB7 being played");
	case TourneyStatus::Finished: return Tr("You organised it \xC2\xB7 over");
	default: return Tr("You organised it \xC2\xB7 cancelled");
	}
}

// The card is as tall as its rows, and no taller than `maxH`: the rows that don't fit are left for the cups' pages.
int PaintMineCard(Painter &p, const State &st, const TourneyMineView &mine, int x, int y, int w, int maxH)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 24;
	std::vector<MineRow> rows;
	for (const TourneyInvite &i : mine.mine.invites) {
		MineRow r;
		r.name = i.name;
		r.line = TrFmt("%s invites you to their team", {i.from.name.empty() ? Tr("A friend") : i.from.name});
		r.arg = ArgOf(kSubOpen, i.tournamentId);
		r.invite = i.tournamentId;
		rows.push_back(r);
	}
	for (const TourneyOrganized &c : mine.mine.organized) {
		MineRow r;
		r.name = c.name;
		r.line = OrganizedLine(c, r.warn);
		r.arg = ArgOf(kSubManage, c.id);
		rows.push_back(r);
	}
	for (const TourneyMyEntry &e : mine.mine.entries) {
		if (!(TourneyEntryActive(e.status) || e.Pending() || e.tournamentStatus == TourneyStatus::Finished) || TourneyOrganizedOf(mine.mine, e.tournamentId)) continue;
		MineRow r;
		r.name = e.name;
		r.arg = ArgOf(kSubOpen, e.tournamentId);
		std::string &line = r.line;
		if (e.tournamentStatus == TourneyStatus::Finished) line = e.placement ? TrFmt("Over \xC2\xB7 you finished in place %s", {std::to_string(e.placement)}) : Tr("Over");
		else if (e.Pending()) {
			line = PendingText(TourneyPartnerOf(e, SteamOwnId()).name);
			r.warn = true;
		}
		else if (e.tournamentStatus == TourneyStatus::Running) {
			const TourneyNext &n = mine.mine.next;
			line = n.present && n.tournamentId == e.tournamentId
				? TrFmt("Playing \xC2\xB7 %s, %s", {BracketStageText(n.bracket, n.stage, n.round),
					Tr(n.status == TourneyMatchStatus::Ready ? "ready" : n.status == TourneyMatchStatus::Live ? "live" : "waiting for your opponent")})
				: Tr("Playing");
		}
		else if (e.status == TourneyEntryStatus::Waitlist) line = Tr("Waiting list");
		else if (e.tournamentStatus == TourneyStatus::Checkin) line = Tr(e.checkedIn ? "Signed up \xC2\xB7 checked in" : "Signed up \xC2\xB7 check in now");
		else line = TrFmt("Signed up \xC2\xB7 starts %s", {WhenText(e.startsAt)});
		rows.push_back(r);
	}
	// Three rows at most, and as many as fit.
	int shown = 0, rowsH = 0;
	for (const MineRow &r : rows) {
		if (shown >= 3 || (shown && 56 + rowsH + r.Height() + 86 > maxH)) break;
		rowsH += r.Height();
		shown++;
	}
	const int h = 56 + std::max(62, rowsH) + 86;
	PaintGlassCard(p, x, y, w, h);
	PaintCaps(p, x + pad, y + 22, w - 2 * pad, 20, "My tournaments", pal::caption);
	int cy = y + 52;
	if (rows.empty() && !mine.loaded) {
		PaintSkeleton(p, x + pad, cy + 6, (w - 2 * pad) * 60 / 100, 14);
		PaintSkeleton(p, x + pad, cy + 32, (w - 2 * pad) * 35 / 100, 10);
		cy += 62;
	}
	else if (rows.empty()) {
		p.Font(0);
		p.Text(x + pad, cy, w - 2 * pad, 50, "You are not in any tournament.", pal::textDim);
		cy += 62;
	}
	for (int i = 0; i < shown; i++) {
		const MineRow &r = rows[static_cast<size_t>(i)];
		if (i) p.Fill(x + pad, cy - 6, w - 2 * pad, 1, pal::hairline);
		const bool hot = p.Hover(x + pad, cy, w - 2 * pad, 56, mx, my);
		UseFace(p, BrandFace::UiStrong, 17);
		p.raw = true;
		p.Text(x + pad, cy, w - 2 * pad, 26, r.name, hot ? pal::redText : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(x + pad, cy + 28, w - 2 * pad, 20, r.line, r.invite ? pal::redText : r.warn ? pal::warn : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		p.AddHit(x + pad, cy, w - 2 * pad, 52, kTourney, r.arg);
		if (r.invite) {
			const int bw = (w - 2 * pad - 10) / 2, by = cy + 56;
			const bool busy = InviteBusy(r.invite);
			p.Button(x + pad, by, bw, 34, "Accept", kTourney, ArgOf(kSubInviteAccept, r.invite), mx, my, !busy, true, 3);
			p.Button(x + pad + bw + 10, by, w - 2 * pad - bw - 10, 34, "Decline", kTourney, ArgOf(kSubInviteDecline, r.invite), mx, my, !busy, false, 3);
		}
		cy += r.Height();
	}
	const int noteY = y + h - 82;
	p.Fill(x + pad, noteY, w - 2 * pad, 66, pal::raised);
	p.Fill(x + pad, noteY, 3, 66, pal::edgeHot);
	p.Font(3);
	p.TextWrapped(x + pad + 14, noteY + 10, w - 2 * pad - 24, 50, "When the check-in opens or your match is ready, we tell you here and in any menu of the game.", pal::body);
	return h;
}

void PaintChampionsCard(Painter &p, const State &st, const TourneyListsView &lists, int x, int y, int w, int h, std::vector<uint64_t> &ids)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 24;
	PaintGlassCard(p, x, y, w, h);
	PaintCaps(p, x + pad, y + 22, w - 2 * pad, 20, "Latest champions", pal::caption);
	int cy = y + 56;
	int shown = 0;
	// The official cups only: theirs are the champions the league keeps (the phalerae). Each row opens the cup's final result.
	for (const TourneySummary &t : lists.finished) {
		if (!t.official) continue;
		if (shown >= 4 || cy + 44 > y + h - 12) break;
		const bool hot = p.Hover(x + pad - 8, cy - 6, w - 2 * pad + 16, 56, mx, my);
		if (hot) p.Fill(x + pad - 8, cy - 6, w - 2 * pad + 16, 56, pal::selected);
		const auto page = LeagueTourneyPage(t.slug, 300000);
		const TourneyEntrant *champ = page->loaded ? TourneyEntrantOf(page->detail, TourneyChampion(page->detail)) : nullptr;
		DrawLeaguePhalera(p, x + pad, cy + 4, 30, LeagueMetal::Gold);
		const int tx = x + pad + 44, tagW = 30;
		p.Font(1);
		if (champ && t.Teams()) {
			p.raw = true;
			p.Text(tx, cy, w - pad - tx + x - tagW, 22, champ->name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
			for (const TourneyMember &pl : TourneyPlayersOf(*champ)) ids.push_back(pl.steamId);
		}
		else if (champ) {
			PaintName(p, tx, cy, w - pad - tx + x - tagW, 22, champ->name, champ->steamId, pal::text, mx, my);
			ids.push_back(champ->steamId);
		}
		else if (page->loaded) p.Text(tx, cy, w - pad - tx + x - tagW, 22, "No champion", pal::textDim);
		else PaintSkeleton(p, tx, cy + 5, std::max(40, (w - pad - tx + x - tagW) * 55 / 100), 12);
		p.Font(3);
		p.raw = true;
		p.Text(tx, cy + 22, w - pad - tx + x - tagW, 20, t.name, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		// The arrow of a row that opens: its result, the podium and the bracket.
		p.ArrowRight(x + w - pad - 12, cy + 15, 8, 14, hot ? pal::text : pal::caption);
		p.AddHit(x + pad - 8, cy - 6, w - 2 * pad + 16, 56, kTourney, ArgOf(kSubChampion, t.id));
		cy += 58;
		shown++;
	}
	if (!shown) {
		p.Font(0);
		p.TextWrapped(x + pad, cy, w - 2 * pad, 52, "The champions of the official cups will show here.", pal::textDim);
	}
}

}

void PaintListScreen(Painter &p, const State &st, const Ui &u, PaintOut &out)
{
	const int mx = st.mouseX, my = st.mouseY;
	LeagueTourneyListsRefresh();
	const TourneyListsView lists = LeagueTourneyLists();
	const auto mineView = LeagueTourneyMine();
	const TourneyMineView &mine = *mineView;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH, false);
	const int m = l.margin, top = l.top, bottom = l.h - m, inner = l.w - 2 * m;
	const int sideW = std::max(360, std::min(480, inner * 27 / 100)), mainW = inner - sideW - kCardGap, sx = m + mainW + kCardGap;
	const int featH = l.roomy ? 232 : 200;
	PaintFeatured(p, st, Featured(lists), mine, m, top, mainW, featH);
	const int tabsY = top + featH + (l.roomy ? 24 : 16);
	PaintSubTabs(p, u, lists, m, tabsY, 40, mx, my);
	PaintCupButtons(p, mine, m + mainW, tabsY - 2, 40, mx, my);
	const int tableY = tabsY + 40 + 14;
	PaintTable(p, st, u, lists, mine, m, tableY, mainW, bottom - tableY, out);

	std::vector<uint64_t> ids;
	int y = top;
	if (!LeagueLinked()) {
		const LeagueLinkView link = LeagueLinkState();
		const int h = link.phase == LeagueLinkPhase::Waiting || link.phase == LeagueLinkPhase::Starting ? 330 : 270;
		PaintGlassCard(p, sx, y, sideW, h);
		p.Clip(sx, y, sideW, h);
		PaintLeagueAccount(p, sx + 24, y + 22, sideW - 48, {kLeagueLink, kLeagueOpen, kLeagueCancel, kLeagueUnlink}, mx, my);
		p.Unclip();
		y += h + 20;
	}
	y += PaintLiveCard(p, st, lists, sx, y, sideW, ids) + 20;
	if (LeagueLinked() && bottom - y > 300) y += PaintMineCard(p, st, mine, sx, y, sideW, bottom - y) + 20;
	if (bottom - y > 120) PaintChampionsCard(p, st, lists, sx, y, sideW, bottom - y, ids);
	PatronsWatch(ids);
}

}
