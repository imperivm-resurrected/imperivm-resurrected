// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/steamnet.h"
#include "ui/leagueui.h"
#include "ui/modui.h"

#include "ui/mpui_internal.h"

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

void Display(Painter &p, int size)
{
	SelectObject(p.dc, BrandFont(BrandFace::Display, size, p.s));
}

void Body(Painter &p, int size, bool bold = false)
{
	SelectObject(p.dc, BrandFont(bold ? BrandFace::BodyBold : BrandFace::Body, size, p.s));
}

void RawText(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, text, c, fmt);
	p.raw = false;
}

// A caption in capitals that keeps to its box: the letters close up, then the type gets smaller, before anything is cut.
void FitCaps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT align = DT_LEFT)
{
	p.Font(4);
	int spacing = 2;
	while (spacing > 0 && p.CapsWidth(text, spacing) > w) spacing--;
	if (p.CapsWidth(text, spacing) > w) p.Font(3);
	p.TextCaps(x, y, w, h, text, c, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, spacing);
}

int CapsWidth(Painter &p, const std::string &text, int spacing = 1)
{
	p.Font(4);
	return p.CapsWidth(text, spacing);
}

void Ghost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my)
{
	const bool hot = enabled && p.Hover(x, y, w, h, mx, my);
	const bool down = enabled && p.pressAction == kRankedAct && p.pressArg == arg;
	if (down) p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, !enabled ? pal::hairline : hot || down ? pal::edgeHot : pal::edge);
	FitCaps(p, x + 8, y, w - 16, h, label, !enabled ? pal::disabled : hot || down ? pal::text : pal::textDim, DT_CENTER);
	if (enabled) p.AddHit(x, y, w, h, kRankedAct, arg);
}

void Chip(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, COLORREF back)
{
	p.Fill(x, y, w, h, back);
	FitCaps(p, x + 4, y, w - 8, h, text, ink, DT_CENTER);
}

// A chip as wide as its text and its margins.
int ChipWidth(Painter &p, const std::string &text, int minW = 0)
{
	return std::max(minW, CapsWidth(p, Tr(text), 1) + 24);
}

// The avatar, or the first letters of the name on a plain square.
void Avatar(Painter &p, int x, int y, int size, uint64_t id, const std::string &name)
{
	if (PaintAvatar(p, x, y, size, id)) return;
	p.Fill(x, y, size, size, pal::hairline);
	p.Font(size >= 48 ? 2 : 1);
	RawText(p, x, y, size, size, name.substr(0, std::min<size_t>(2, name.size())), pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

// The friends the invitation dialog lists: the ones with the name looked for, playing the game now first.
std::vector<OnlineFriend> FriendsShown(const State &st)
{
	std::vector<OnlineFriend> out;
	const std::string want = Lower(st.ranked.inviteSearch);
	for (const OnlineFriend &f : SteamFriendList()) {
		if (want.empty() || Lower(f.name).find(want) != std::string::npos) out.push_back(f);
	}
	return out;
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

// "4:12" left of something the server ends at `until` (its clock).
std::string LeftText(int64_t until)
{
	return LeagueClockText(std::max<int64_t>(0, until - LeagueServerNowMs()) + 999);
}

}

std::string RankedQueueLabel(const std::string &queue)
{
	return Tr(LeagueQueueIsTeam(queue) ? "2 vs 2" : "1 vs 1");
}

int RankedDuoHeight(int maxH)
{
	const LeagueDuoView duo = LeagueDuoState();
	const int count = static_cast<int>(duo.invites.size()), shown = InvitesShown(count, maxH);
	return kDuoHeadH + kDuoRowH + shown * kDuoInviteH + (count > shown ? kDuoMoreH : 0) + 14;
}

void PaintRankedDuo(Painter &p, const State &st, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueDuoView duo = LeagueDuoState();
	const LeagueRankedView &v = st.ranked.view;
	const int ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, h);
	// The title, and how the duo stands on the right.
	const char *state = duo.role == LeagueDuoRole::Solo ? "Alone" : duo.role == LeagueDuoRole::Waiting ? "Invitation sent" : "Ready";
	const COLORREF stateInk = duo.role == LeagueDuoRole::Solo ? pal::caption : duo.role == LeagueDuoRole::Waiting ? pal::warn : pal::ok;
	const int stateW = std::min(iw / 2, CapsWidth(p, Tr(state), 2) + 4);
	FitCaps(p, ix, y + 14, iw - stateW - 12, 22, Tr("Your duo"), pal::redText);
	FitCaps(p, ix + iw - stateW, y + 14, stateW, 22, Tr(state), stateInk, DT_RIGHT);
	p.Fill(x + 1, y + kDuoHeadH - 4, w - 2, 1, pal::hairline);
	int ry = y + kDuoHeadH;
	const bool searching = v.phase != LeagueRankedPhase::Idle;
	if (duo.role == LeagueDuoRole::Solo) {
		// Alone: what a duo is, and the button to invite a friend.
		const std::string label = Tr("Invite a friend");
		const int bw = std::min(iw / 2, CapsWidth(p, label, 2) + 40);
		Ghost(p, ix + iw - bw, ry + 8, bw, 40, label, kRkDuoInviteOpen, !duo.busy && !searching, mx, my);
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
		Ghost(p, ix + iw - bw, ry + 10, bw, 36, Tr(leave), kRkDuoLeave, !duo.busy && (v.partnerSearch || !searching), mx, my);
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
		p.Fill(x + 1, ry, w - 2, 1, pal::hairline);
		const std::string from = !inv.fromName.empty() ? inv.fromName : Tr("A friend");
		Avatar(p, ix, ry + 9, 32, IdOf(inv.fromId), from);
		const std::string accept = Tr("Accept"), decline = Tr("Decline");
		const int aw = std::max(84, CapsWidth(p, accept, 1) + 28), dw = std::max(84, CapsWidth(p, decline, 1) + 28);
		// Accepting one undoes the invitation this player sent, if any; not a duo already made.
		const bool can = !duo.busy && (duo.role == LeagueDuoRole::Solo || duo.role == LeagueDuoRole::Waiting) && !searching;
		p.Button(ix + iw - aw - dw - 8, ry + 9, aw, 32, accept, kRankedAct, kRkDuoAccept + static_cast<int>(i), mx, my, can, true);
		Ghost(p, ix + iw - dw, ry + 9, dw, 32, decline, kRkDuoDecline + static_cast<int>(i), !duo.busy, mx, my);
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
	const std::vector<OnlineFriend> friends = FriendsShown(st);
	const int pages = std::max(1, (static_cast<int>(friends.size()) + l.rows - 1) / l.rows);
	const int page = std::min(st.ranked.invitePage, pages - 1);
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
	for (int i = 0; i < l.rows; i++) {
		const size_t k = static_cast<size_t>(page * l.rows + i);
		if (k >= friends.size()) break;
		const OnlineFriend &f = friends[k];
		const int ry = l.listY + i * kInviteRowH;
		if (p.Hover(ix, ry, iw, kInviteRowH, mx, my)) p.Fill(ix, ry, iw, kInviteRowH, pal::raised);
		Avatar(p, ix + 8, ry + 8, 36, f.id, f.name);
		int chipW = 0;
		if (f.playing) {
			chipW = ChipWidth(p, "Playing now", 90);
			Chip(p, ix + iw - bw - 12 - chipW, ry + 15, chipW, 22, "Playing now", pal::ok, RGB(0x2a, 0x36, 0x24));
		}
		p.Font(0);
		RawText(p, ix + 56, ry, iw - 56 - bw - 24 - chipW, kInviteRowH, f.name, pal::text);
		p.Button(ix + iw - bw, ry + 8, bw, 36, invite, kRankedAct, kRkDuoFriend + i, mx, my, true, false);
		p.Fill(ix, ry + kInviteRowH - 1, iw, 1, pal::hairline);
	}
	// The pages, at the foot on the left; the button to close on the right.
	if (pages > 1) {
		const int fy = d.FooterTop();
		Ghost(p, ix, fy, 48, kDialogBtnH, "\xE2\x80\xB9", kRkDuoFriendsPrev, page > 0, mx, my);
		Ghost(p, ix + 56, fy, 48, kDialogBtnH, "\xE2\x80\xBA", kRkDuoFriendsNext, page + 1 < pages, mx, my);
		p.Font(3);
		RawText(p, ix + 120, fy, 200, kDialogBtnH, TrFmt("Page %s of %s", {std::to_string(page + 1), std::to_string(pages)}), pal::caption);
	}
	DialogButtons(p, d, {{"Close", kRankedAct, kRkDuoInviteClose}}, mx, my);
}

void RankedTeamAction(int arg)
{
	RankedScreen &r = g_s.ranked;
	if (arg >= kRkDuoFriend && arg < kRkTeamArgEnd) {
		const InviteLayout l = InviteFor(g_s.viewW, g_s.viewH);
		const std::vector<OnlineFriend> friends = FriendsShown(g_s);
		const size_t k = static_cast<size_t>(r.invitePage * l.rows + (arg - kRkDuoFriend));
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
			r.boardSel = -1;
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
	case kRkDuoFriendsPrev: r.invitePage = std::max(0, r.invitePage - 1); break;
	case kRkDuoFriendsNext: r.invitePage++; break;
	default: break;
	}
}

void RankedTeamGameThreadTick()
{
	// What happened with the duo shows in whatever menu the player is: an invitation got, a friend who accepted or left.
	for (const LeagueDuoNotice &n : LeagueDuoTakeNotices()) ModUiToast(n.text, n.bad);
}

}
