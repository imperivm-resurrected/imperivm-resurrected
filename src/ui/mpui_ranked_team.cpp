// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueseasons.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "game/gameroom.h"
#include "game/roomcountdown.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/leaguemapthumb.h"
#include "ui/leagueui.h"
#include "ui/modui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_ranked_kit.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

const int kInviteRowH = 52, kDuoHeadH = 48, kDuoRowH = 56, kDuoInviteH = 50, kDuoMoreH = 22;

// How many of the invitations got fit a panel `h` tall (two at most), leaving a line to say how many more there are.
int InvitesShown(int count, int h)
{
	const int room = h - kDuoHeadH - kDuoRowH - 14;
	int n = std::max(0, std::min({2, count, room / kDuoInviteH}));
	if (n > 0 && n < count && room - n * kDuoInviteH < kDuoMoreH) n--;
	return n;
}

// A caption in capitals that keeps to its box: the letters close up, then the type gets smaller, before anything is cut. The text is
// drawn as given, already translated, as CapsWidth measures it.
void FitCaps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT align = DT_LEFT)
{
	const bool raw = p.raw;
	p.raw = true;
	p.Font(4);
	int spacing = 2;
	while (spacing > 0 && p.CapsWidth(text, spacing) > w) spacing--;
	if (p.CapsWidth(text, spacing) > w) p.Font(3);
	p.TextCaps(x, y, w, h, text, c, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, spacing);
	p.raw = raw;
}

int CapsWidth(Painter &p, const std::string &text, int spacing = 1)
{
	const bool raw = p.raw;
	p.raw = true;
	p.Font(4);
	const int w = p.CapsWidth(text, spacing);
	p.raw = raw;
	return w;
}

// The ranked tab's outlined button and chip, their labels fitted to the box.
void FitGhost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my)
{
	const bool hot = enabled && p.Hover(x, y, w, h, mx, my);
	const bool down = enabled && p.pressAction == kRankedAct && p.pressArg == arg;
	if (down) p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, !enabled ? pal::hairline : hot || down ? pal::edgeHot : pal::edge);
	FitCaps(p, x + 8, y, w - 16, h, label, !enabled ? pal::disabled : hot || down ? pal::text : pal::textDim, DT_CENTER);
	if (enabled) p.AddHit(x, y, w, h, kRankedAct, arg);
}

void FitChip(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, COLORREF back)
{
	p.Fill(x, y, w, h, back);
	FitCaps(p, x + 4, y, w - 8, h, text, ink, DT_CENTER);
}

// A chip as wide as its text, already translated, and its margins.
int ChipWidth(Painter &p, const std::string &text, int minW = 0)
{
	return std::max(minW, CapsWidth(p, text, 1) + 24);
}

uint64_t IdOf(const std::string &steamId)
{
	return _strtoui64(steamId.c_str(), nullptr, 10);
}

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

// The friends the invitation dialog lists: the ones with the name looked for, in the order SteamFriendsInfo gives them.
std::vector<SteamFriendInfo> FriendsShown(const State &st)
{
	std::vector<SteamFriendInfo> out;
	const std::string want = Lower(st.ranked.inviteSearch);
	for (const SteamFriendInfo &f : SteamFriendsInfo()) {
		if (want.empty() || Lower(f.name).find(want) != std::string::npos) out.push_back(f);
	}
	return out;
}

struct PresenceLook {
	std::string label;
	COLORREF dot, ink;
};

PresenceLook PresenceLookOf(FriendPresence presence)
{
	switch (presence) {
	case FriendPresence::Playing: return {Tr("Playing Imperivm"), pal::ok, pal::ok};
	case FriendPresence::Online: return {Tr("Connected"), pal::ok, pal::textDim};
	case FriendPresence::Away: return {Tr("Away"), pal::warn, pal::caption};
	case FriendPresence::Busy: return {Tr("Busy"), pal::warn, pal::caption};
	default: return {Tr("Offline"), pal::disabled, pal::disabled};
	}
}

struct InviteLayout {
	Dialog d;
	int listY = 0, rows = 0;
};

InviteLayout InviteFor(int viewW, int viewH)
{
	InviteLayout l;
	l.d.w = std::min(760, viewW - 80);
	l.d.h = std::min(660, viewH - 80);
	l.d.x = (viewW - l.d.w) / 2;
	l.d.y = (viewH - l.d.h) / 2;
	l.listY = l.d.y + 168;
	l.rows = std::max(3, (l.d.FooterTop() - 16 - l.listY) / kInviteRowH);
	return l;
}

int InvitePages(size_t count, int rows)
{
	return std::max(1, (static_cast<int>(count) + rows - 1) / rows);
}

int InvitePageOf(int page, size_t count, int rows)
{
	return std::max(0, std::min(page, InvitePages(count, rows) - 1));
}

// "4:12" left of something the server ends at `until` (its clock).
std::string LeftText(int64_t until)
{
	return LeagueClockText(std::max<int64_t>(0, until - LeagueServerNowMs()) + 999);
}

// A player of a 2v2 match found: avatar, name and who they are to this player, their record, and their rank with the Glory, and whether
// they accepted (or `state`, as the room has them, once the match is played from the panel; `accepted` is then whether it is good).
void PaintTeamCard(Painter &p, int x, int y, int w, int h, const LeaguePlayerCard &c, bool you, bool partner, bool accepted, const char *state = nullptr)
{
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, you ? pal::edgeHot : pal::hairline);
	const std::string name = !c.name.empty() ? c.name : Tr("Player");
	const int av = 48;
	Avatar(p, x + 16, y + 14, av, IdOf(c.steamId), name);
	if (you) p.Frame(x + 16, y + 14, av, av, pal::red);
	// The marks after the name: who it is to this player, and a duo.
	std::vector<std::pair<std::string, COLORREF>> marks;
	if (you) marks.push_back({Tr("You"), pal::textDim});
	else if (partner) marks.push_back({Tr("Partner"), pal::textDim});
	if (c.duo) marks.push_back({Tr("Duo"), pal::redText});
	int marksW = 0;
	for (const auto &m : marks) marksW += ChipWidth(p, m.first, 44) + 6;
	const int nx = x + 16 + av + 14, nw = x + w - 16 - nx;
	Body(p, 20);
	p.raw = true;
	const int nameW = std::max(40, std::min(nw - marksW, p.TextWidth(name) + 4));
	p.Text(nx, y + 12, nameW, 28, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	int mx = nx + nameW + 6;
	for (const auto &m : marks) {
		const int cw = ChipWidth(p, m.first, 44);
		if (mx + cw > x + w - 16) break;
		FitChip(p, mx, y + 16, cw, 20, m.first, m.second, m.second == pal::redText ? RGB(0x3a, 0x12, 0x14) : pal::hairline);
		mx += cw + 6;
	}
	p.Font(3);
	const std::string record = c.standing ? RecordText(c) : Tr(c.tier == LeagueTier::None ? "Placement games" : "Ranked 2 vs 2");
	RawText(p, nx, y + 40, nw, 22, record, pal::caption);
	p.Fill(x + 16, y + 70, w - 32, 1, pal::hairline);
	// The rank and its Glory on the left, the answer on the right.
	const std::string answer = Tr(state ? state : accepted ? "Accepted" : "Pending");
	const int chipW = ChipWidth(p, answer, accepted ? 104 : 96), chipH = 26;
	const int emblem = 40, ry = y + 78;
	DrawLeagueEmblem(p, x + 14, ry + (h - 78 - emblem) / 2 - 2, emblem, c.tier);
	const int rx = x + 14 + emblem + 10, rw = x + w - 16 - chipW - 12 - rx;
	Display(p, 26);
	p.raw = true;
	p.TextCaps(rx, ry, rw, 28, RankOrPlacement(c.tier, c.division), Ink(c.tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	if (c.standing && c.tier != LeagueTier::None) {
		p.Font(3);
		RawText(p, rx, ry + 28, rw, 20, StandingText(c), pal::body);
	}
	FitChip(p, x + w - 16 - chipW, ry + (h - 78 - chipH) / 2, chipW, chipH, answer, accepted ? pal::ok : pal::textDim,
		accepted ? RGB(0x2a, 0x36, 0x24) : pal::hairline);
}

// A label over a value, as the room's summary shows its options.
void Fact(Painter &p, int x, int y, int w, const std::string &label, const std::string &value)
{
	FitCaps(p, x, y, w, 16, label, pal::caption);
	p.Font(0);
	RawText(p, x, y + 20, w, 26, value, pal::text);
}

}

std::string RankedQueueLabel(const std::string &queue)
{
	return Tr(LeagueQueueIsTeam(queue) ? "2 vs 2" : "1 vs 1");
}

int RankedDuoHeight(const LeagueDuoView &duo, int maxH)
{
	const int count = static_cast<int>(duo.invites.size()), shown = InvitesShown(count, maxH);
	return kDuoHeadH + kDuoRowH + shown * kDuoInviteH + (count > shown ? kDuoMoreH : 0) + 14;
}

void PaintRankedDuo(Painter &p, const State &st, const LeagueDuoView &duo, int x, int y, int w, int h, bool inCard)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const int ix = x + 24, iw = w - 48;
	// A card of its own, or a part of the rank card under a line.
	if (inCard) p.Fill(ix, y, iw, 1, pal::hairline);
	else PaintGlassCard(p, x, y, w, h);
	const int lineX = inCard ? ix : x + 1, lineW = inCard ? iw : w - 2;
	// The title, and how the duo stands on the right.
	const char *state = duo.role == LeagueDuoRole::Solo ? "Alone" : duo.role == LeagueDuoRole::Waiting ? "Invitation sent" : "Ready";
	const COLORREF stateInk = duo.role == LeagueDuoRole::Solo ? pal::caption : duo.role == LeagueDuoRole::Waiting ? pal::warn : pal::ok;
	const int stateW = std::min(iw / 2, CapsWidth(p, Tr(state), 2) + 4);
	FitCaps(p, ix, y + 14, iw - stateW - 12, 22, Tr("Your duo"), pal::redText);
	FitCaps(p, ix + iw - stateW, y + 14, stateW, 22, Tr(state), stateInk, DT_RIGHT);
	p.Fill(lineX, y + kDuoHeadH - 4, lineW, 1, pal::hairline);
	int ry = y + kDuoHeadH;
	const bool searching = v.phase != LeagueRankedPhase::Idle;
	if (duo.role == LeagueDuoRole::Solo) {
		// Alone: what a duo is, and the button to invite a friend.
		const std::string label = Tr("Invite a friend");
		const int bw = std::min(iw / 2, CapsWidth(p, label, 2) + 40);
		FitGhost(p, ix + iw - bw, ry + 8, bw, 40, label, kRkDuoInviteOpen, !duo.busy && !searching, mx, my);
		const std::string text = duo.missing ? Tr("The league server does not have duos yet: you search alone.") : Tr("Search alone, or invite a Steam friend to search together.");
		const int tw = iw - bw - 16;
		p.Font(3);
		p.raw = true;
		// Two lines of the caption's type, or of a smaller one where the button leaves less room (Italian, 1600 lines wide).
		if (p.Measure(tw, text) > 44) SelectObject(p.dc, BrandFont(BrandFace::Ui, 11, p.s));
		p.TextWrapped(ix, ry + 6, tw, 46, text, pal::caption);
		p.raw = false;
	}
	else {
		// The friend: invited and waiting for their answer, or the other member of the duo.
		const LeaguePartyMember &f = duo.partner;
		const std::string name = !f.name.empty() ? f.name : Tr("Your friend");
		Avatar(p, ix, ry + 6, 44, IdOf(f.steamId), name);
		const char *leave = duo.role == LeagueDuoRole::Waiting ? "Cancel the invitation" : "Leave the duo";
		const int bw = std::min(iw * 2 / 5, CapsWidth(p, Tr(leave), 2) + 32);
		// Leaving the duo stops its search: the partner who follows the leader's may, the leader cancels theirs first.
		FitGhost(p, ix + iw - bw, ry + 10, bw, 36, Tr(leave), kRkDuoLeave, !duo.busy && (v.partnerSearch || !searching), mx, my);
		const int tx = ix + 56, tw = iw - 56 - bw - 12;
		Body(p, 18);
		RawText(p, tx, ry + 4, tw, 26, name, pal::text);
		std::string line;
		if (duo.role == LeagueDuoRole::Waiting) line = duo.party.expiresAt > 0 ? TrFmt("Invited  \xC2\xB7  expires in %s", {LeftText(duo.party.expiresAt)}) : Tr("Invited");
		else if (duo.role == LeagueDuoRole::Leader) line = Tr("You search for both");
		else line = Tr("Leader: searches for both");
		p.Font(3);
		RawText(p, tx, ry + 30, tw, 20, line, duo.role == LeagueDuoRole::Waiting ? pal::warn : pal::caption);
	}
	ry += kDuoRowH;
	// The invitations got: who sends it and how long it lasts, Accept and Decline; as many as the panel holds, and how many more.
	const int shown = InvitesShown(static_cast<int>(duo.invites.size()), h);
	for (size_t i = 0; i < duo.invites.size() && static_cast<int>(i) < shown; i++) {
		const LeaguePartyInvite &inv = duo.invites[i];
		p.Fill(lineX, ry, lineW, 1, pal::hairline);
		const std::string from = !inv.fromName.empty() ? inv.fromName : Tr("A friend");
		Avatar(p, ix, ry + 9, 32, IdOf(inv.fromId), from);
		const std::string accept = Tr("Accept"), decline = Tr("Decline");
		const int aw = std::max(84, CapsWidth(p, accept, 1) + 28), dw = std::max(84, CapsWidth(p, decline, 1) + 28);
		// Accepting one undoes the invitation this player sent, if any; not a duo already made.
		const bool can = !duo.busy && (duo.role == LeagueDuoRole::Solo || duo.role == LeagueDuoRole::Waiting) && !searching;
		p.Button(ix + iw - aw - dw - 8, ry + 9, aw, 32, accept, kRankedAct, kRkDuoAccept + static_cast<int>(i), mx, my, can, true);
		FitGhost(p, ix + iw - dw, ry + 9, dw, 32, decline, kRkDuoDecline + static_cast<int>(i), !duo.busy, mx, my);
		const int tx = ix + 44, tw = iw - 44 - aw - dw - 20;
		p.Font(0);
		RawText(p, tx, ry + 4, tw, 22, from, pal::text);
		p.Font(3);
		RawText(p, tx, ry + 26, tw, 20, inv.expiresAt > 0 ? TrFmt("Invites you  \xC2\xB7  expires in %s", {LeftText(inv.expiresAt)}) : Tr("Invites you to their duo"),
			pal::caption);
		ry += kDuoInviteH;
	}
	const int more = static_cast<int>(duo.invites.size()) - shown;
	if (more > 0) {
		p.Font(3);
		RawText(p, ix, ry, iw, kDuoMoreH, more == 1 ? Tr("1 more invitation") : TrFmt("%s more invitations", {std::to_string(more)}), pal::caption);
	}
}

void PaintRankedInvite(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const InviteLayout l = InviteFor(st.viewW, st.viewH);
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, l.d.w, l.d.h, "Invite a friend to your duo", pal::redText);
	DialogText(p, d, 60, "Your friend needs the game and their league account linked. The invitation lasts 5 minutes.", pal::textDim, 3);
	PaintEditField(p, d.BodyX(), d.y + 112, d.BodyW(), 40, st.ranked.inviteSearch, Tr("Look for a friend by name..."), st.focus == kRankedField, false, st.edit,
		kFieldRanked);
	const std::vector<SteamFriendInfo> friends = FriendsShown(st);
	const int pages = InvitePages(friends.size(), l.rows);
	const int page = InvitePageOf(st.ranked.invitePage, friends.size(), l.rows);
	const int ix = d.BodyX(), iw = d.BodyW();
	if (friends.empty()) {
		p.Font(0);
		p.TextWrapped(ix, l.listY + 12, iw, 60,
			!SteamOnlineReady()				  ? "Steam is not available. Open Steam and try again."
			: st.ranked.inviteSearch.empty() ? "Reading your Steam friends..."
											   : "None of your Steam friends has that name.",
			pal::caption);
	}
	const std::string invite = Tr("Invite");
	const int bw = std::max(110, CapsWidth(p, invite, 1) + 36);
	const size_t first = static_cast<size_t>(page * l.rows);
	std::vector<uint64_t> visible;
	for (size_t k = first; k < friends.size() && k < first + static_cast<size_t>(l.rows); k++) visible.push_back(friends[k].id);
	PatronsWatch(visible);
	const std::string league = Tr("League");
	for (int i = 0; i < l.rows; i++) {
		const size_t k = first + static_cast<size_t>(i);
		if (k >= friends.size()) break;
		const SteamFriendInfo &f = friends[k];
		const int ry = l.listY + i * kInviteRowH;
		if (p.Hover(ix, ry, iw, kInviteRowH, mx, my)) p.Fill(ix, ry, iw, kInviteRowH, pal::raised);
		Avatar(p, ix + 8, ry + 8, 36, f.id, f.name);
		int chipW = 0;
		if (PlayerLinkedOf(f.id) || PlayerBadgeOf(f.id).kind != LeagueBadgeKind::None || PlayerFrameOf(f.id).Shown()) {
			chipW = ChipWidth(p, league, 60);
			FitChip(p, ix + iw - bw - 12 - chipW, ry + 15, chipW, 22, league, pal::redText, RGB(0x30, 0x1a, 0x1a));
		}
		const PresenceLook look = PresenceLookOf(f.presence);
		const int tw = iw - 56 - bw - 24 - chipW;
		p.Font(0);
		RawText(p, ix + 56, ry + 5, tw, 24, f.name, pal::text);
		p.Ellipse(ix + 57, ry + 34, 8, 8, look.dot);
		p.Font(3);
		RawText(p, ix + 71, ry + 28, tw - 15, 20, look.label, look.ink);
		p.Button(ix + iw - bw, ry + 8, bw, 36, invite, kRankedAct, kRkDuoFriend + i, mx, my, true, false);
		p.Fill(ix, ry + kInviteRowH - 1, iw, 1, pal::hairline);
	}
	// The pages, at the foot on the left; the button to close on the right.
	if (pages > 1) {
		const int fy = d.FooterTop();
		FitGhost(p, ix, fy, 48, kDialogBtnH, "\xE2\x80\xB9", kRkDuoFriendsPrev, page > 0, mx, my);
		FitGhost(p, ix + 56, fy, 48, kDialogBtnH, "\xE2\x80\xBA", kRkDuoFriendsNext, page + 1 < pages, mx, my);
		p.Font(3);
		RawText(p, ix + 120, fy, 200, kDialogBtnH, TrFmt("Page %s of %s", {std::to_string(page + 1), std::to_string(pages)}), pal::caption);
	}
	DialogButtons(p, d, {{"Close", kRankedAct, kRkDuoInviteClose}}, mx, my);
}

void PaintSearchTeam(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const bool accepted = v.phase == LeagueRankedPhase::Accepted;
	// Both teams accepted: the panel stays, with the room made and played under it.
	const bool live = v.phase == LeagueRankedPhase::Ready || v.phase == LeagueRankedPhase::Playing;
	const FoundBox box = PaintFoundBox(p, st, 1240, 700);
	const int dx = box.x, dy = box.y, dw = box.w;
	Display(p, 54);
	p.TextCaps(dx, dy + 18, dw, 56, "Match found", pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	if (live) Ghost(p, dx + 24, dy + 20, 120, 36, "Back", kRkHidePanel, true, mx, my);

	// Both sides, this player's on the left, from the ticket once it is read (it says who accepted).
	const std::string own = std::to_string(SteamOwnId());
	const std::vector<LeagueMatchTeam> teams = MatchTeams(v);
	int mySide = v.ticketLoaded && v.ticket.mySide ? v.ticket.mySide : v.found.mySide;
	if (!mySide) mySide = LeagueSideOf(teams, own);
	const LeagueMatchTeam *mine = nullptr, *theirs = nullptr;
	for (const LeagueMatchTeam &t : teams) {
		if (t.side == mySide) mine = &t;
		else theirs = &t;
	}
	const int colW = (dw - 64 - 264) / 2, cardH = 128, gap = 14, cy = dy + 132;
	FitCaps(p, dx + 32, cy - 30, colW, 22, Tr("Your team"), pal::redText);
	FitCaps(p, dx + dw - 32 - colW, cy - 30, colW, 22, Tr("The other team"), pal::caption, DT_RIGHT);
	int acceptedCount = 0, players = 0;
	auto column = [&](const LeagueMatchTeam *t, int x) {
		for (int i = 0; i < 2; i++) {
			const int y = cy + i * (cardH + gap);
			if (!t || i >= static_cast<int>(t->players.size())) {
				p.Fill(x, y, colW, cardH, pal::raised);
				p.Frame(x, y, colW, cardH, pal::hairline);
				continue;
			}
			LeaguePlayerCard c = t->players[i];
			const bool you = c.steamId == own;
			bool ok = you && accepted;
			if (v.ticketLoaded) {
				if (const LeaguePlayerCard *tc = LeagueTicketCard(v.ticket, c.steamId)) ok = ok || tc->accepted;
			}
			players++;
			acceptedCount += ok ? 1 : 0;
			if (live) {
				const int in = RankedRoomStateOf(st, IdOf(c.steamId));
				PaintTeamCard(p, x, y, colW, cardH, c, you, t == mine && !you, in > 0, in == 2 ? "Ready" : in == 1 ? "In the room" : "Not in the room yet");
			}
			else PaintTeamCard(p, x, y, colW, cardH, c, you, t == mine && !you, ok);
		}
	};
	column(mine, dx + 32);
	column(theirs, dx + dw - 32 - colW);

	// The countdown to answer, on the server's clock, and how many accepted.
	const int64_t now = LeagueServerNowMs();
	const int64_t acceptBy = v.found.acceptBy;
	const int mid = dx + dw / 2;
	Display(p, 34);
	p.TextCaps(mid - 132, cy - 4, 264, 34, "VS", pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	const int ringY = cy + 112;
	PaintArc(p, mid, ringY, 62, 8, pal::hairline, 1.0f);
	// While the room is played from the panel the ring is its start countdown, a spinner before.
	const int left = live ? GameRoomCountdownLeftMs() : 0;
	if (!live || left > 0) {
		PaintArc(p, mid, ringY, 62, 8, live || !accepted ? pal::red : pal::ok,
			live ? static_cast<float>(left) / kRoomCountdownMs : LeagueCountdownFraction(acceptBy, now));
		Display(p, 62);
		p.raw = true;
		p.Text(mid - 70, ringY - 38, 140, 62, std::to_string(live ? (left + 999) / 1000 : LeagueCountdownSeconds(acceptBy, now)), pal::text,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		FitCaps(p, mid - 70, ringY + 22, 140, 16, Tr("seconds"), pal::caption, DT_CENTER);
	}
	else p.Spinner(mid, ringY, 22, 6, GetTickCount());
	PaintEvenness(p, v, true, mid - 128, ringY + 82, 256);
	p.Font(3);
	// Once all accepted no line under the ring: its number and the cards say it all.
	if (!live) {
		RawText(p, mid - 128, ringY + 108, 256, 22, TrFmt("%s of %s accepted", {std::to_string(acceptedCount), std::to_string(std::max(4, players))}),
			acceptedCount >= 4 ? pal::ok : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	// The map and the options of the season.
	const int my2 = cy + 2 * cardH + gap + 20, mw = dw - 64, mh = 124;
	p.Fill(dx + 32, my2, mw, mh, pal::raised);
	p.Frame(dx + 32, my2, mw, mh, pal::hairline);
	const LeagueMapFacts &map = v.ticketLoaded && !v.ticket.map.name.empty() ? v.ticket.map : v.found.map;
	PaintLeagueMapThumb(p, dx + 48, my2 + 14, 96, map.name);
	FitCaps(p, dx + 164, my2 + 16, 300, 16, Tr("Map"), pal::caption);
	Display(p, 42);
	p.raw = true;
	p.TextCaps(dx + 164, my2 + 34, 330, 44, map.name.empty() ? Tr("Chosen by the league") : LeagueMapText(map.name), pal::text,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	std::string mapLine = Tr("4 players");
	if (!map.size.empty()) mapLine += "  \xC2\xB7  " + LeagueMapText(map.size);
	p.Font(0);
	RawText(p, dx + 164, my2 + 80, 330, 22, mapLine, pal::caption);
	p.Fill(dx + 532, my2 + 14, 1, 96, pal::hairline);
	const LeagueRoomSettings &s = v.ticketLoaded ? v.ticket.settings : v.season.settings;
	const int fx = dx + 562, fw = (dx + 32 + mw - 24 - fx) / 3;
	const std::array<MatchFact, 6> facts = MatchFacts(s);
	for (int i = 0; i < 6; i++) Fact(p, fx + i % 3 * fw, my2 + 14 + i / 3 * 52, fw - 16, Tr(facts[i].label), facts[i].value);

	const int by = my2 + mh + 22;
	if (live) {
		PaintRankedPanelFoot(p, st, dx, dw, by);
		return;
	}
	UseSharedFonts(p);
	if (accepted) {
		p.Spinner(mid - 170, by + 30, 11, 4, GetTickCount());
		p.Font(1);
		p.Text(mid - 148, by, 420, 60, "Accepted. Waiting for the other players...", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	else {
		p.fonts[5] = BrandFont(BrandFace::Display, 34, p.s);
		p.Button(mid - 328, by, 400, 60, "Accept", kRankedAct, kRkAccept, mx, my, !v.answering, true, 5);
		FitGhost(p, mid + 88, by, 240, 60, Tr("Decline"), kRkDecline, !v.answering, mx, my);
		UseSharedFonts(p);
	}
}

void RankedTeamAction(int arg)
{
	RankedScreen &r = g_s.ranked;
	if (arg >= kRkDuoFriend && arg < kRkTeamArgEnd) {
		const InviteLayout l = InviteFor(g_s.viewW, g_s.viewH);
		const std::vector<SteamFriendInfo> friends = FriendsShown(g_s);
		const size_t k = static_cast<size_t>(InvitePageOf(r.invitePage, friends.size(), l.rows) * l.rows + (arg - kRkDuoFriend));
		if (k >= friends.size()) return;
		LeagueDuoInvite(std::to_string(friends[k].id), friends[k].name);
		r.inviteOpen = false;
		if (g_s.focus == kRankedField) g_s.focus = kNoField;
		return;
	}
	const LeagueDuoView duo = LeagueDuoState();
	if (arg >= kRkDuoDecline && arg < kRkDuoFriend) {
		const size_t i = static_cast<size_t>(arg - kRkDuoDecline);
		if (i < duo.invites.size()) LeagueDuoAnswer(duo.invites[i].id, false);
		return;
	}
	if (arg >= kRkDuoAccept && arg < kRkDuoDecline) {
		const size_t i = static_cast<size_t>(arg - kRkDuoAccept);
		if (i < duo.invites.size()) LeagueDuoAnswer(duo.invites[i].id, true);
		return;
	}
	switch (arg) {
	case kRkQueueDuel:
	case kRkQueueTeam:
		LeagueRankedSelectQueue(arg == kRkQueueTeam ? kLeagueQueueTeam : kLeagueQueueDuel);
		if (arg == kRkQueueTeam) LeagueDuoWatch();
		// The page on screen is read again for the queue picked.
		if (r.sub == kRankedBoard) {
			RankedBoardAsk(0, false);
		}
		else if (r.sub == kRankedHistory) {
			r.historyBack.clear();
			LeagueHistoryRequest(0);
		}
		break;
	case kRkDuoInviteOpen:
		r.inviteOpen = true;
		r.invitePage = 0;
		r.inviteSearch.clear();
		FocusField(kRankedField);
		break;
	case kRkDuoInviteClose:
		r.inviteOpen = false;
		if (g_s.focus == kRankedField) g_s.focus = kNoField;
		break;
	case kRkDuoLeave: LeagueDuoLeave(); break;
	case kRkDuoFriendsPrev:
	case kRkDuoFriendsNext: {
		// From the page on screen: a search that shortens the list leaves invitePage past its end.
		const InviteLayout l = InviteFor(g_s.viewW, g_s.viewH);
		const int page = InvitePageOf(r.invitePage, FriendsShown(g_s).size(), l.rows);
		r.invitePage = arg == kRkDuoFriendsPrev ? std::max(0, page - 1) : page + 1;
		break;
	}
	default: break;
	}
}

void RankedTeamGameThreadTick()
{
	// What happened with the duo shows in whatever menu the player is: an invitation got, a friend who accepted or left.
	for (const LeagueDuoNotice &n : LeagueDuoTakeNotices()) ModUiToast(n.text, n.bad);
}

}
