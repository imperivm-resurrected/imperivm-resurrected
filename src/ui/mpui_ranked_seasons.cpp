// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_seasons.h"
#include "net/steamnet.h"
#include "ui/leagueui.h"
#include "ui/modui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_ranked_kit.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

// The search waits for the player to stop typing.
const DWORD kSearchRestMs = 450;

struct BoardWatch {
	// The text as last seen, to tell when it was typed; and how many friends the last page of friends was asked with.
	std::string typed;
	size_t friendsAsked = 0;
};
BoardWatch g_watch;

std::string Trimmed(const std::string &s)
{
	const size_t a = s.find_first_not_of(' ');
	if (a == std::string::npos) return std::string();
	return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

// Letters, not bytes: an accented one is two.
size_t Letters(const std::string &s)
{
	size_t n = 0;
	for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
	return n;
}

// The name the leaderboard is asked for: none until it has two letters, and at most the server's 64.
std::string SearchName(const std::string &typed)
{
	std::string t = Trimmed(typed);
	if (Letters(t) < 2) return std::string();
	size_t end = 0;
	for (size_t letters = 0; end < t.size(); end++) {
		if ((static_cast<unsigned char>(t[end]) & 0xC0) != 0x80 && letters++ == 64) break;
	}
	return Trimmed(t.substr(0, end));
}

// The friends' leaderboard is asked for this player and their Steam friends, this many at most.
const size_t kMaxFriendIds = 200;

std::vector<std::string> FriendIds()
{
	std::vector<std::string> ids;
	if (const uint64_t me = SteamOwnId()) ids.push_back(std::to_string(me));
	for (uint64_t f : SteamFriendIds()) {
		if (ids.size() >= kMaxFriendIds) break;
		ids.push_back(std::to_string(f));
	}
	return ids;
}

// How many FriendIds gives, without writing them out: the frame checks it while the friends' leaderboard is on.
size_t FriendIdCount()
{
	return std::min(kMaxFriendIds, (SteamOwnId() ? 1 : 0) + SteamFriendIds().size());
}

// The seasons the menu offers, newest first: the server's list, or the season being played while it has not come.
std::vector<LeagueSeasonInfo> SeasonChoices(const State &st)
{
	std::vector<LeagueSeasonInfo> list = LeagueSeasonsShared()->seasons;
	if (list.empty() && st.ranked.view.seasonLoaded) {
		LeagueSeasonInfo s;
		s.id = st.ranked.view.season.number;
		s.name = LeagueSeasonName(st.ranked.view.season, st.ranked.view.lang);
		list.push_back(s);
	}
	return list;
}

// The season of the leaderboard as the server numbers it: the one picked, or the one being played.
int BoardSeasonOf(const State &st)
{
	if (st.ranked.boardSeason >= 0) return st.ranked.boardSeason;
	return st.ranked.view.seasonLoaded ? st.ranked.view.season.number : -1;
}

void CloseCeremony(bool toBoard)
{
	RankedScreen &r = g_s.ranked;
	r.ceremonyOpen = false;
	g_s.view = toBoard ? kViewRanked : r.ceremonyBack;
	if (!toBoard) return;
	LeagueRankedSelectQueue(r.ceremonyQueue);
	RankedAction(kRkBoard);
	r.boardSeason = r.ceremonySeason;
	RankedBoardAsk(0, false);
}

// A click on a player's badge: their row in the leaderboard of the season being played, found from their card's place.
void OpenPlayerRow(uint64_t id)
{
	if (!id) return;
	RankedScreen &r = g_s.ranked;
	const std::string sid = std::to_string(id);
	g_s.view = kViewRanked;
	const bool onBoard = r.sub == kRankedBoard && r.boardSeason < 0 && !r.boardFriends && SearchName(r.boardSearch).empty();
	if (onBoard && LeagueBoardFind(r.view.page, sid)) {
		r.boardPick = sid;
		return;
	}
	RankedAction(kRkBoard);
	r.boardPick = sid;
	r.boardSeek = sid;
	LeaguePlayerRequest(sid, -1, true, r.view.queue);
	LogI("league", "a badge was clicked: looking for the player's row");
}

}

int RankedBoardRowsFor(int viewW, int viewH)
{
	// Under the tool bar: the heading, the rows, the Imperator line and the player's own row pinned at the bottom.
	const ScreenLayout l = ScreenLayoutFor(viewW, viewH, false);
	const int h = l.h - l.margin - (l.top + 72 + RankedBoardFrameFor(viewW, viewH).secondRow);
	return std::max(5, std::min(kRankedBoardRows, (h - 44 - 28 - 53) / 46));
}

int RankedBoardRows()
{
	return RankedBoardRowsFor(g_s.viewW, g_s.viewH);
}

void RankedBoardAsk(int offset, bool aroundMe)
{
	RankedScreen &r = g_s.ranked;
	LeagueBoardQuery q;
	q.offset = std::max(0, offset);
	q.limit = RankedBoardRows();
	q.aroundMe = aroundMe;
	q.season = r.boardSeason;
	q.name = SearchName(r.boardSearch);
	q.friends = r.boardFriends;
	if (q.friends) {
		q.ids = FriendIds();
		g_watch.friendsAsked = q.ids.size();
	}
	r.boardAsked = q.name;
	LeagueBoardRequest(q);
}

std::vector<std::string> RankedSeasonItems(const State &st, int &sel)
{
	std::vector<std::string> items;
	const std::vector<LeagueSeasonInfo> list = SeasonChoices(st);
	const int season = BoardSeasonOf(st);
	sel = -1;
	for (size_t i = 0; i < list.size(); i++) {
		const LeagueSeasonInfo &s = list[i];
		std::string label = TrFmt("Season %s", {LeagueRoman(s.id)});
		if (!s.name.empty()) label += "  \xC2\xB7  " + s.name;
		label += "  \xC2\xB7  " + Tr(s.closed ? "closed" : "in progress");
		items.push_back(label);
		if (s.id == season) sel = static_cast<int>(i);
	}
	return items;
}

void RankedSeasonPick(int index)
{
	const std::vector<LeagueSeasonInfo> list = SeasonChoices(g_s);
	if (index < 0 || index >= static_cast<int>(list.size())) return;
	RankedScreen &r = g_s.ranked;
	const LeagueSeasonInfo &s = list[index];
	// The season being played is asked without its number, as the hub asks it.
	const bool current = !s.closed && r.view.seasonLoaded && s.id == r.view.season.number;
	r.boardSeason = current ? -1 : s.id;
	r.boardPick.clear();
	RankedBoardAsk(0, false);
}

void RankedSeasonAction(int arg)
{
	RankedScreen &r = g_s.ranked;
	if (arg >= kRkBadge && arg < kRkBadgeEnd) {
		const uint64_t id = LeagueBadgeClickedId(arg);
		// From the ceremony's hall of fame: the ceremony was seen.
		if (id && r.ceremonyOpen) {
			LeagueCeremonySeen(r.ceremonySeason, r.ceremonyQueue);
			r.ceremonyOpen = false;
		}
		OpenPlayerRow(id);
		return;
	}
	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;
	switch (arg) {
	case kRkCeremonyEquip: {
		const LeagueCeremony &c = me.me.ceremony;
		if (!r.ceremonyOpen) break;
		// The season's vexillum is one of the player's badges; the server numbers it by its season.
		std::string badge = std::to_string(r.ceremonySeason);
		for (const LeagueBadge &b : me.me.badges) {
			if (b.kind == LeagueBadgeKind::Season && (b.season == r.ceremonySeason || b.id == badge)) badge = b.id;
		}
		const bool frame = c.present && c.reward.season == r.ceremonySeason && c.reward.frame;
		LeagueCeremonyEquip(r.ceremonySeason, frame, badge, r.ceremonyQueue);
		ModUiToast(frame ? "Your vexillum, your frame and your title are on." : "Your vexillum is on, next to your name.");
		CloseCeremony(false);
		break;
	}
	case kRkCeremonyLater:
		if (!r.ceremonyOpen) break;
		LeagueCeremonySeen(r.ceremonySeason, r.ceremonyQueue);
		CloseCeremony(false);
		break;
	case kRkCeremonyBoard:
		if (!r.ceremonyOpen) break;
		LeagueCeremonySeen(r.ceremonySeason, r.ceremonyQueue);
		CloseCeremony(true);
		break;
	case kRkBoardAll:
	case kRkBoardFriends:
		if (r.boardFriends == (arg == kRkBoardFriends)) break;
		r.boardFriends = arg == kRkBoardFriends;
		r.boardPick.clear();
		RankedBoardAsk(0, false);
		break;
	case kRkBoardClear:
		r.boardSearch.clear();
		g_watch.typed.clear();
		if (g_s.focus == kRankedField) EditToEnd(r.boardSearch, g_s.edit);
		RankedBoardAsk(0, false);
		break;
	default: break;
	}
}

void RankedSeasonsFrame()
{
	RankedScreen &r = g_s.ranked;
	const DWORD now = GetTickCount();
	if (r.sub == kRankedBoard && g_s.view == kViewRanked) {
		LeagueSeasonsRefresh();
		// The name looked for is asked once the player stops typing.
		if (r.boardSearch != g_watch.typed) {
			g_watch.typed = r.boardSearch;
			r.boardTypedAt = now;
		}
		const std::string wanted = SearchName(r.boardSearch);
		if (wanted != r.boardAsked && now - r.boardTypedAt >= kSearchRestMs) RankedBoardAsk(0, false);
		// Steam gives the friends a moment after they are first asked for.
		if (r.boardFriends && FriendIdCount() != g_watch.friendsAsked) RankedBoardAsk(0, false);
		if (!r.boardSeek.empty()) {
			const LeaguePlayerView card = LeaguePlayerState(r.boardSeek, -1, r.view.queue);
			if (card.state == LeagueCardState::Ready) {
				if (card.card.place > 0) RankedBoardAsk(LeagueBoardPageOf(card.card.place, RankedBoardRows()), false);
				r.boardSeek.clear();
			}
			else if (card.state == LeagueCardState::Missing || card.state == LeagueCardState::Failed) {
				ModUiToast(card.state == LeagueCardState::Missing ? "This player has no rank in the season being played."
																  : "Couldn't reach the league server. Check your connection and try again.");
				r.boardSeek.clear();
			}
		}
	}
	// The ceremony of a closed season shows once, over Multiplayer, when nothing else is on.
	if (!g_s.open || !LeagueLinked()) return;
	LeagueMeRefresh();
	if (r.ceremonyOpen) {
		g_s.view = kViewRanked;
		// The hall of the queue whose season closed.
		LeagueHallRequest(r.ceremonySeason, r.ceremonyQueue);
		LeagueSeasonsRefresh();
		return;
	}
	if (g_s.game.active || g_s.room.id || g_s.connecting || r.resultOpen || g_s.searching || r.disputeMatch) return;
	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;
	if (!me.loaded || !me.me.ceremony.present) return;
	const int season = me.me.ceremony.reward.season;
	const std::string queue = LeagueQueueId(me.me.ceremony.reward.queue), shown = std::to_string(season) + ":" + queue;
	if (std::find(r.ceremonyShown.begin(), r.ceremonyShown.end(), shown) != r.ceremonyShown.end()) return;
	r.ceremonyShown.push_back(shown);
	r.ceremonyQueue = queue;
	r.ceremonyOpen = true;
	r.ceremonySeason = season;
	r.ceremonyBack = g_s.view;
	g_s.view = kViewRanked;
	g_s.dropOpen = false;
	if (g_s.focus == kRankedField) g_s.focus = kNoField;
	LeagueHallRequest(season, queue);
	LeagueSeasonsRefresh(true);
	LogI("league", "showing the end of season %d", season);
}

}
