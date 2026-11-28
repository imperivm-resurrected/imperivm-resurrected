// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <memory>

#include "ui/mpui_tourney.h"
#include "addr/state.h"
#include "core/guard.h"
#include "core/league.h"
#include "core/leaguemaps.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "core/tournament.h"
#include "game/civs.h"
#include "game/cupmaps.h"
#include "game/gameroom.h"
#include "hooks/alerts.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "core/version.h"
#include "mods/mods.h"
#include "ui/leaguemodsprep.h"
#include "ui/leagueui.h"
#include "ui/modui.h"
#include "ui/mpui_league_room.h"
#include "ui/mpui_tourney_internal.h"
#include "ui/patronui.h"
#include "ui/textedit.h"

namespace mpui_tourney_impl {

Ui g_tourney;

namespace {

const DWORD kClaimRetryMs = 5000, kFlowTimeoutMs = 45000, kPresenceEveryMs = 30000, kJoinWaitMs = 6000;
// The tries to put this player's slot in their team, 0.8 s apart.
const int kTeamTries = 24;
// Not in the room the server gave after this long: it is taken as gone (Steam enters a room in a few seconds).
const DWORD kStaleAfterMs = 20000;
// The setting with the match of a game against a test bot being played (0 for none).
const char *const kBotGameSetting = "TourneyBotGame";

// Under g_lock: the tournament of an id, from the lists and the player's own.
std::string SlugOf(int64_t tournament)
{
	const TourneyListsView lists = LeagueTourneyLists();
	for (const std::vector<TourneySummary> *list : {&lists.open, &lists.live, &lists.finished}) {
		for (const TourneySummary &t : *list) {
			if (t.id == tournament) return t.slug;
		}
	}
	const auto view = LeagueTourneyMine();
	const TourneyMine &mine = view->mine;
	if (mine.next.tournamentId == tournament) return mine.next.slug;
	if (mine.checkin.tournamentId == tournament) return mine.checkin.slug;
	for (const TourneyMyEntry &e : mine.entries) {
		if (e.tournamentId == tournament) return e.slug;
	}
	if (mine.organize.activeId == tournament) return mine.organize.activeSlug;
	if (const TourneyOrganized *c = TourneyOrganizedOf(mine, tournament)) return c->slug;
	const auto page = LeagueTourneyPage(g_tourney.slug, 60000);
	if (page->loaded && page->detail.id == tournament) return page->detail.slug;
	return std::string();
}

const TourneySummary *SummaryOf(const TourneyListsView &lists, int64_t tournament)
{
	for (const std::vector<TourneySummary> *list : {&lists.open, &lists.live, &lists.finished}) {
		for (const TourneySummary &t : *list) {
			if (t.id == tournament) return &t;
		}
	}
	return nullptr;
}

// Under g_lock: the gameplay mods of a cup, by what the page on screen, the lists or the player's next match say of it.
LeagueModsRule CupMods(int64_t tournament)
{
	const auto page = LeagueTourneyPage(g_tourney.slug, 60000);
	if (page->loaded && page->detail.id == tournament) return page->detail.mods;
	const TourneyListsView lists = LeagueTourneyLists();
	if (const TourneySummary *t = SummaryOf(lists, tournament)) return t->mods;
	const auto mine = LeagueTourneyMine();
	const TourneyNext &next = mine->mine.next;
	if (next.present && next.tournamentId == tournament) return next.mods;
	return LeagueModsRule();
}

// Under g_lock: a cup of teams, by what the lists, the page on screen or the player's next match say of it.
// A cup of teams whose players sign up with their partner (not one whose teams are drawn).
bool TeamsCup(int64_t tournament)
{
	const TourneyListsView lists = LeagueTourneyLists();
	if (const TourneySummary *t = SummaryOf(lists, tournament)) return t->Teams() && !t->teamDraw;
	const auto page = LeagueTourneyPage(g_tourney.slug, 60000);
	if (page->loaded && page->detail.id == tournament) return page->detail.Teams() && !page->detail.teamDraw;
	const auto mine = LeagueTourneyMine();
	const TourneyNext &next = mine->mine.next;
	return next.present && next.tournamentId == tournament && next.Teams();
}

// The room and the spectators' room of a match, from the pages read for the tab.
bool FindMatchRooms(int64_t match, uint64_t &lobby, uint64_t &spec)
{
	std::vector<std::string> slugs;
	if (!g_tourney.slug.empty()) slugs.push_back(g_tourney.slug);
	for (const TourneySummary &t : LeagueTourneyLists().live) slugs.push_back(t.slug);
	for (const std::string &slug : slugs) {
		const auto page = LeagueTourneyPage(slug, 60000);
		for (const TourneyMatch &m : page->detail.matches) {
			if (m.id != match) continue;
			lobby = m.lobby;
			spec = m.specLobby;
			return lobby != 0;
		}
	}
	return false;
}

void Toast(const std::string &text, bool error = false)
{
	ModUiToast(text, error);
}

// A tournament played with other gameplay content than this computer's.
std::string OtherContentText(const std::string &contentName)
{
	return TrFmt("This tournament is played with %s: switch to that content profile first.", {contentName.empty() ? Tr("other content") : contentName});
}

// The tournament asks for other gameplay mods than this computer's: told, and true. The page offers to prepare them.
bool ModsBlocked(const LeagueModsRule &rule)
{
	const LeagueModsCheck fit = LeagueModsFitNow(rule);
	if (fit.fit == LeagueModsFit::Ok) return false;
	Toast(LeagueModsProblemText(rule, fit), true);
	return true;
}

std::string WrongModsText(const LeagueModsRule &rule)
{
	if (!rule.set) return Tr("This tournament is played with other gameplay mods: prepare your mods first.");
	if (rule.BaseGame()) return Tr("This tournament is played without gameplay mods: prepare your mods first.");
	return TrFmt("This tournament is played with %s: prepare your mods first.", {LeagueModsNames(rule.mods)});
}

void Flash()
{
	const HWND hwnd = g_wakeWnd;
	if (!hwnd || GetForegroundWindow() == hwnd) return;
	// Called under the screen's lock: flashing sends the window messages at once, which come back into the screen's handlers on this
	// thread, so it goes from another thread.
	SafeThread([](HWND w) {
		FLASHWINFO fi = {sizeof(fi), w, FLASHW_ALL | FLASHW_TIMERNOFG, 5, 0};
		FlashWindowEx(&fi);
	}, hwnd).detach();
}

// Under g_lock: the page of a tournament, on one of its tabs.
void OpenPage(const std::string &slug, int tab)
{
	Ui &u = g_tourney;
	if (slug.empty()) return;
	u.slug = slug;
	u.screen = kScreenPage;
	u.pageTab = tab;
	u.bracketX = u.bracketY = u.sideScroll = u.pageSideScroll = 0;
}

// The scenarios of a cup, looked for on this computer and downloaded when missing.
void FetchCupMaps(int64_t tournament)
{
	std::vector<TourneyMap> maps;
	const TourneyListsView lists = LeagueTourneyLists();
	if (const TourneySummary *t = SummaryOf(lists, tournament)) maps = CupAllMaps(*t);
	const auto page = LeagueTourneyPage(g_tourney.slug, 60000);
	if (maps.empty() && page->loaded && page->detail.id == tournament) maps = CupAllMaps(page->detail);
	for (const TourneyMap &m : maps) {
		if (m.Scenario()) LeagueCupMap(m);
	}
}

// Under g_lock: the cup being made, once the server answered.
void CupMade(const TourneyOpResult &r)
{
	Ui &u = g_tourney;
	if (!r.ok) {
		u.cupError = ResultText(r);
		u.cupErrorSlug = r.error == "already_organizing" ? r.slug : std::string();
		return;
	}
	u.cupError.clear();
	u.cupErrorSlug.clear();
	if (u.screen == kScreenCreate && !u.cupEditId) OpenPage(r.slug, 0);
	if (!r.inviteCode.empty()) u.shownCode = r.inviteCode;
	else Toast("Your cup is published: it is in the list of tournaments.");
}

// Under g_lock: the organizer's cup changed, once the server answered.
void CupEdited(const TourneyOpResult &r)
{
	Ui &u = g_tourney;
	if (!r.ok) {
		u.cupError = ResultText(r);
		// Someone signed up meanwhile: from now on the form only changes the description.
		if (r.error == "locked") u.cupLocked = true;
		return;
	}
	u.cupError.clear();
	if (u.screen == kScreenCreate && u.cupEditId == r.id) {
		u.cupEditId = 0;
		OpenPage(r.slug.empty() ? u.cupEditSlug : r.slug, 0);
	}
	Toast("Changes saved: the cup's page and the list show them now.");
}

// Under g_lock: the cup of a code, once the server answered.
void CodeFound(const TourneyOpResult &r)
{
	Ui &u = g_tourney;
	if (!u.joinOpen) return;
	if (!r.ok) {
		u.joinError = r.status == 404 || r.error == "not_found" ? Tr("No cup has that code. Check it with the organiser.") : ResultText(r);
		FocusField(kCupCodeField);
		return;
	}
	u.joinOpen = false;
	u.joinCode.clear();
	u.joinError.clear();
	if (g_s.focus == kCupCodeField) g_s.focus = kNoField;
	OpenPage(r.slug, 0);
}

// Under g_lock: asks first, then runs `act` (which only talks to the league: the dialog answers outside this lock).
void Confirm(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> act)
{
	ModUiAsk(title, text, yes, std::move(act));
}

std::string StageOf(const TourneyNotice &n)
{
	return BracketStageText(n.bracket, n.stage, n.round);
}

// Under g_lock: the room for this match is made here, with the tournament's options.
void CreateRoom(const TourneyClaim &claim)
{
	RoomFlow &f = g_tourney.room;
	const auto mine = LeagueTourneyMine();
	const TourneyNext &n = mine->mine.next;
	if (!claim.contentKey.empty() && claim.contentKey != SteamOwnGameplayKey()) {
		Toast(OtherContentText(n.contentName), true);
		f = RoomFlow();
		return;
	}
	if (ModsBlocked(claim.mods.set ? claim.mods : n.mods)) {
		f = RoomFlow();
		return;
	}
	OnlineCreateSettings s;
	const std::string stage = n.match == f.match ? BracketStageText(n.bracket, n.stage, n.round) : std::string();
	// A match of teams is named after the two teams, and the room holds its four players. Every claim has the sides, 1 vs 1 too.
	const bool teams = n.match == f.match && n.Teams();
	const std::string own = teams && !n.team.name.empty() ? n.team.name : g_s.player;
	s.name = n.match == f.match && !n.name.empty() ? n.name + " \xC2\xB7 " + stage + " \xC2\xB7 " + TrFmt("%s vs %s", {own, n.opponent.name}) : Tr("Tournament match");
	s.map = claim.map.Known() ? MapText(claim.map) : std::string("Chosen in the room");
	// A match of teams says so in the room's data, the same on every computer: its team plays as a ranked 2v2's.
	s.mode = teams ? kLeagueTeamCupRoomMode : "Tournament";
	s.maxPlayers = teams ? 4 : 2;
	s.visibility = 0;
	s.specDelay = TourneySpecDelay(claim.settings);
	s.spectators = s.specDelay != 0;
	if (!s.spectators) s.specDelay = 120;
	s.leagueTag = TourneyRoomTag(f.match);
	g_s.hostSave = SaveEntry();
	g_s.hostSavePublished = false;
	g_s.savePlayersRead = false;
	g_s.savePlayers.clear();
	g_s.savePlaced.clear();
	SteamCreateLobby(s);
	g_s.roomLimit = s.maxPlayers;
	g_s.autoHost = true;
	f.phase = RoomFlow::kCreating;
	f.at = GetTickCount();
	LogI("league", "creating the room of tournament match %lld for %d players", static_cast<long long>(f.match), s.maxPlayers);
}

// Under g_lock: who a claim waits for, by name: the opponent, a player of either team, or nobody known.
std::string WaiterName(const TourneyNext &n, uint64_t by)
{
	if (!by) return std::string();
	if (by == n.opponent.steamId && !n.Teams()) return n.opponent.name;
	for (const std::vector<TourneyMember> *side : {&n.team.members, &n.opponent.members}) {
		if (const TourneyMember *m = TourneyMemberOf(*side, by)) return m->name;
	}
	return by == n.opponent.steamId ? n.opponent.name : std::string();
}

// Under g_lock.
void TakeResults()
{
	TourneyOpResult r;
	while (LeagueTourneyTakeResult(r)) {
		RoomFlow &f = g_tourney.room;
		switch (r.op) {
		case TourneyOp::Claim:
			if (r.id != f.match || f.phase != RoomFlow::kClaiming) break;
			if (!r.ok) {
				Toast(ResultText(r), true);
				f = RoomFlow();
				break;
			}
			if (!r.claim.teams.empty()) {
				g_tourney.claimTeams = r.claim.teams;
				g_tourney.claimTeamsMatch = r.id;
			}
			if (r.claim.kind == TourneyClaim::Create) CreateRoom(r.claim);
			else if (r.claim.kind == TourneyClaim::Join) {
				LogI("league", "joining the room of tournament match %lld", static_cast<long long>(f.match));
				f.phase = RoomFlow::kJoining;
				f.lobby = r.claim.lobby;
				f.at = GetTickCount();
				g_tourney.join = PendingJoin();
				g_tourney.join.lobby = r.claim.lobby;
				g_tourney.join.at = GetTickCount();
				SteamRefreshLobbies();
			}
			else {
				f.phase = RoomFlow::kWaiting;
				f.at = GetTickCount();
				f.waitingFor = WaiterName(LeagueTourneyMine()->mine.next, r.claim.by);
			}
			break;
		case TourneyOp::Register:
			if (!r.ok) Toast(ResultText(r), true);
			break;
		case TourneyOp::Presence: break;
		case TourneyOp::Concede:
			if (r.ok) Toast("Your cup game against the bot closed with the game: it counts as lost.");
			break;
		case TourneyOp::Create: CupMade(r); break;
		case TourneyOp::Edit: CupEdited(r); break;
		case TourneyOp::FindCode: CodeFound(r); break;
		case TourneyOp::SignUp:
			// A team's sign-up answers in its dialog while it is open: what is wrong stays in it, and it closes once the invitation is sent.
			if (g_tourney.teamCup == r.id && !r.ok) {
				g_tourney.teamError = ResultText(r);
				break;
			}
			if (r.pendingPartner) {
				Toast(TrFmt("Invitation sent to %s: your team takes its place in the cup when they accept it.",
					{g_tourney.teamCup == r.id && !g_tourney.teamPartnerName.empty() ? g_tourney.teamPartnerName : Tr("your partner")}));
			}
			else Toast(ResultText(r), !r.ok);
			if (g_tourney.teamCup == r.id) {
				g_tourney.teamCup = 0;
				if (g_s.focus == kTeamNameField || g_s.focus == kTeamSearchField) g_s.focus = kNoField;
			}
			// The cup's scenarios start coming down as soon as the player is in, page open or not.
			if (r.ok) FetchCupMaps(r.id);
			break;
		case TourneyOp::Accept:
			Toast(ResultText(r), !r.ok);
			if (r.ok) FetchCupMaps(r.id);
			break;
		case TourneyOp::RemoveEntry:
			if (r.ok && LeagueTourneyPage(g_tourney.slug, 60000)->detail.Teams()) Toast("The team is out of the cup.");
			else Toast(ResultText(r), !r.ok);
			break;
		// The swap shows in the first round: only a refusal is told.
		case TourneyOp::Seeds:
			if (!r.ok) Toast(ResultText(r), true);
			break;
		default: Toast(ResultText(r), !r.ok); break;
		}
	}
}

// Under g_lock.
void TakeNotices()
{
	TourneyNotice n;
	// One invitation is asked at a time: the dialog of a second would take the first one's place.
	bool asked = false;
	while (LeagueTourneyTakeNotice(n)) {
		switch (n.kind) {
		case TourneyNotice::MatchReady:
			Toast(TrFmt("%s, %s: your match against %s is ready. Enter the room before %s.", {n.name, StageOf(n), n.opponent.empty() ? Tr("your opponent") : n.opponent,
				HourText(n.deadlineAt)}));
			AlertPlayer(PlayerAlert::CupMatchReady);
			break;
		case TourneyNotice::CheckinOpen:
			Toast(TrFmt("The check-in of %s is open until %s.", {n.name, HourText(n.deadlineAt)}));
			Flash();
			break;
		case TourneyNotice::PickOpen:
			Toast(TrFmt("You choose the map of game %s against %s: you have a minute, or it is drawn.", {std::to_string(n.game), n.opponent.empty() ? Tr("your opponent") : n.opponent}));
			Flash();
			// In the tab, the choice is on the match's screen; in the room, over its table.
			if (g_s.open && g_s.view == kViewTournaments && !g_s.room.id) g_tourney.screen = kScreenMatch;
			break;
		case TourneyNotice::Invite:
			AskInvite(n, !asked);
			asked = true;
			Flash();
			break;
		default:
			Toast(TrFmt("%s is over: see the result in Multiplayer, Tournaments.", {n.name}));
			g_tourney.pendingChampion = n.slug;
			if (g_s.open && g_s.view == kViewTournaments && !g_s.room.id && (g_tourney.screen == kScreenList || g_tourney.slug == n.slug)) {
				g_tourney.slug = n.slug;
				g_tourney.championSlug = n.slug;
				g_tourney.screen = kScreenChampion;
				g_tourney.pendingChampion.clear();
			}
			break;
		}
	}
}

// Under g_lock: joins or watches a room once the lobby list has it.
void StepJoin()
{
	PendingJoin &j = g_tourney.join;
	if (!j.lobby) return;
	for (const OnlineLobby &l : g_s.lobbies) {
		if (l.id != j.lobby) continue;
		const OnlineLobby lobby = l;
		const bool watch = j.watch;
		j = PendingJoin();
		if (watch) TrySpectate(lobby, "");
		else TryJoin(lobby, "");
		return;
	}
	// Watching: the game's own room may be out of the list while it is played; its spectators' room (copied from it) is enough.
	if (j.watch && j.spec) {
		for (const OnlineLobby &l : g_s.lobbies) {
			if (l.id != j.spec) continue;
			OnlineLobby lobby = l;
			lobby.id = j.lobby;
			lobby.specLobby = j.spec;
			lobby.spectate = false;
			lobby.playing = true;
			j = PendingJoin();
			LogI("league", "watching a tournament game through its spectators' room");
			TrySpectate(lobby, "");
			return;
		}
	}
	const DWORD now = GetTickCount();
	if (now - j.at > kJoinWaitMs) {
		const uint64_t lobby = j.lobby;
		const bool watch = j.watch;
		j = PendingJoin();
		if (watch) Toast("That game is no longer in the list of rooms.");
		else {
			LogW("league", "the tournament room is not in the list; joining it directly");
			SteamJoinLobby(lobby, "");
		}
		return;
	}
	if (now - j.refresh > 1500) {
		j.refresh = now;
		SteamRefreshLobbies();
	}
}

// Under g_lock: the claim waiting for the other's room, the room being created and registered, and the joining.
void StepRoom()
{
	RoomFlow &f = g_tourney.room;
	if (f.phase == RoomFlow::kIdle) return;
	const DWORD now = GetTickCount();
	const int64_t inRoom = g_s.room.id ? TourneyMatchOfTag(g_s.room.info.leagueTag) : 0;
	switch (f.phase) {
	case RoomFlow::kWaiting:
		if (now - f.at >= kClaimRetryMs) {
			f.phase = RoomFlow::kClaiming;
			f.at = now;
			LeagueTourneyClaimRoom(f.match);
		}
		break;
	case RoomFlow::kCreating:
		if (inRoom == f.match && g_s.room.owner) {
			f.lobby = g_s.room.id;
			f.registeredLobby = f.lobby;
			f.registeredSpec = g_s.room.info.specLobby;
			LeagueTourneyRegisterRoom(f.match, f.lobby, f.registeredSpec);
			f.phase = RoomFlow::kInRoom;
			f.at = now;
		}
		else if (now - f.at > kFlowTimeoutMs) {
			Toast("Steam could not create the tournament room. Try again.", true);
			f = RoomFlow();
		}
		break;
	case RoomFlow::kJoining:
		if (inRoom == f.match) {
			f.phase = RoomFlow::kInRoom;
			f.at = now;
		}
		else if (now - f.at > (f.stale ? kFlowTimeoutMs : kStaleAfterMs) && !g_s.syncConfirm && !g_s.syncPhase) {
			if (f.lobby && !f.stale) {
				// The room the server sent this player to is gone (it closed, or Steam lost it): the server forgets it and answers
				// with another room, or lets this player open one.
				LogI("league", "could not enter the room of tournament match %lld; telling the league it is gone", static_cast<long long>(f.match));
				f.stale = f.lobby;
				f.lobby = 0;
				f.phase = RoomFlow::kClaiming;
				f.at = now;
				g_tourney.join = PendingJoin();
				LeagueTourneyClaimRoom(f.match, f.stale);
				break;
			}
			Toast("Could not enter the tournament room. Try again.", true);
			f = RoomFlow();
		}
		break;
	case RoomFlow::kInRoom:
		if (inRoom != f.match) {
			if (now - f.at > 5000) f = RoomFlow();
			break;
		}
		f.at = now;
		// The spectators' room comes a moment after the room: it is registered when it appears.
		if (f.registeredLobby == g_s.room.id && g_s.room.info.specLobby && g_s.room.info.specLobby != f.registeredSpec) {
			f.registeredSpec = g_s.room.info.specLobby;
			LeagueTourneyRegisterRoom(f.match, f.registeredLobby, f.registeredSpec);
		}
		break;
	default:
		if (now - f.at > kFlowTimeoutMs) f = RoomFlow();
		break;
	}
}

// Under g_lock: in the room of a tournament match, the presence for the no-shows, the start, the strangers out and the options locked.
void StepInRoom(const TourneyNext &next)
{
	RoomLock &k = g_tourney.lock;
	const int64_t match = g_s.room.id ? TourneyMatchOfTag(g_s.room.info.leagueTag) : 0;
	if (!match || g_s.room.info.spectate) {
		if (k.room) k = RoomLock();
		return;
	}
	if (k.room != g_s.room.id) {
		k = RoomLock();
		k.room = g_s.room.id;
		LogI("league", "in the room of tournament match %lld", static_cast<long long>(match));
		// What the host's game is next to this one: a different build or content makes the game refuse the join ("different version of
		// the protocol"), which says nothing more.
		const OnlineLobby &info = g_s.room.info;
		const std::string ownKey = SteamOwnGameplayKey();
		LogI("league", "tournament room: the host's build %s, rules %s, content key %s; this one %s, %s, %s", info.version.c_str(), info.rulesLevel.c_str(),
			info.gameplayKey.c_str(), ModVersionText(), ModsRulesLevel().c_str(), ownKey.c_str());
		if (!g_s.room.owner) {
			const std::string mismatch = SteamVersionMismatch(info.version, info.rulesLevel);
			if (!mismatch.empty()) Toast(mismatch, true);
			else if (!info.gameplayKey.empty() && info.gameplayKey != ownKey) Toast("The host plays with other content (Options > Content): both of you need the same to play this match.", true);
		}
	}
	const DWORD now = GetTickCount();
	const bool mine = next.present && next.match == match;
	if (g_s.game.active) GameRoomKeepPlayerPrefs();
	if (mine) k.wasMine = true;
	else if (k.wasMine && !g_s.inOnlineGame && !g_s.game.busy && LeagueTourneyMine()->loaded) {
		// Settled while the player waited here (a no-show claimed or given): out of the room and back to the tournaments.
		LogI("league", "tournament match %lld is settled; out of its room", static_cast<long long>(match));
		k = RoomLock();
		LeaveGameRoom();
		g_s.view = kViewTournaments;
		return;
	}
	if (mine && (!k.presenceAt || now - k.presenceAt >= kPresenceEveryMs)) {
		k.presenceAt = now;
		LeagueTourneyPresence(match, false);
	}
	// The cup to come back to when its game ends, known from the room on (the room is not there while the game is played).
	if (mine && !next.slug.empty()) g_tourney.afterSlug = next.slug;
	if (mine && !k.shownAt && g_s.game.active) {
		k.shownAt = now;
		// The room stays under the match page: the player chose there, and gets ready and sees the start there.
		g_s.view = kViewTournaments;
		g_tourney.screen = kScreenMatch;
	}
	// What the match page chose goes into the player's slot and stays there: sent again until the room has it, and again when the page
	// changes it. A ready player's row does not change: the player is made not ready first, and ready again after.
	const int slot = g_s.game.localSlot;
	const bool slotOk = mine && g_s.game.active && !g_s.game.busy && !g_s.inOnlineGame && slot >= 0 && slot < 8 && g_s.game.slots[static_cast<size_t>(slot)].used;
	bool slotDone = false;
	if (slotOk && k.shownAt && now - k.shownAt >= 800) {
		const LeagueSlotStep step = LeagueSlotKeepStep(k.slot, MatchCiv(g_tourney), MatchColor(g_tourney), g_tourney.colorRgb, now, "tournament", match);
		if (step == LeagueSlotStep::Unreadied) k.readySent = false;
		else if (step == LeagueSlotStep::Done) {
			slotDone = true;
			k.civSet = true;
		}
	}
	// Against staging test bots the host puts the game's AI in an open seat for each of them (two against a team), in the other side's team:
	// the bots play through it, and the game is reported.
	if (slotOk && g_s.game.host && TourneyIsTestBot(next.opponent.steamId)) {
		const int want = next.Teams() ? 2 : 1;
		for (int i = 0; i < 8 && static_cast<int>(k.aiSlots.size()) < want; i++) {
			const GameRoomSlot &o = g_s.game.slots[static_cast<size_t>(i)];
			if (i == slot || o.type != 0 || std::find(k.aiSlots.begin(), k.aiSlots.end(), i) != k.aiSlots.end()) continue;
			k.aiSlots.push_back(i);
			GameRoomSetSlotType(i, 1);
			LogI("league", "tournament match %lld: the game's AI takes a test bot's seat (%d)", static_cast<long long>(match), i + 1);
		}
		// In a match of teams, the bots' seats go to the other side's team.
		const int foe = k.team == 1 ? 2 : k.team == 2 ? 1 : 0;
		if (foe && !g_s.game.busy) {
			for (const int i : k.aiSlots) {
				const GameRoomSlot &o = g_s.game.slots[static_cast<size_t>(i)];
				if (o.type == 1 && o.team != foe && foe < g_s.game.teamChoices) GameRoomSetTeam(i, foe);
			}
		}
	}
	// Ready once the slot has what the page chose, the team is set (2v2) and the host has put the tournament's options in.
	const bool teamReady = !next.Teams() || k.teamSet;
	const bool optionsReady = !g_s.game.host || k.applied;
	if (slotOk && slotDone && teamReady && optionsReady && !k.readySent && now - k.shownAt >= 1600) {
		k.readySent = true;
		if (!LocalReady(g_s.game)) {
			Predict(kPredReady, slot, 1);
			GameRoomToggleReady("cup auto-ready");
			LogI("league", "tournament match %lld: the player is made ready", static_cast<long long>(match));
		}
	}
	if (mine && g_s.inOnlineGame && !k.startedSent) {
		k.startedSent = true;
		k.presenceAt = now;
		LeagueTourneyPresence(match, true);
		// A game against a test bot lives on this computer only: if it closes with the game, the next start gives it up.
		if (TourneyIsTestBot(next.opponent.steamId)) {
			GameRoomSaveSetting(kBotGameSetting, static_cast<int>(match));
			g_tourney.botGameMarked = true;
		}
	}
	if (!g_s.inOnlineGame) k.startedSent = false;
	// A match of teams: the sides of this player's claim set the room's teams, side A team 1 and side B team 2.
	if (mine && k.teams.empty() && g_tourney.claimTeamsMatch == match) k.teams = g_tourney.claimTeams;
	const bool teams = mine && next.Teams();
	if (teams && !k.team) {
		k.team = TourneyTeamOf(k.teams, SteamOwnId());
		if (!k.team && (next.team.side == "a" || next.team.side == "b")) k.team = next.team.side == "a" ? 1 : 2;
		if (k.team) LogI("league", "tournament match %lld: this player is in team %d", static_cast<long long>(match), k.team);
	}
	// Only the match's players: anyone else who came in before the room filled up is sent out by the host. With teams, the four must be
	// known first, or a partner the league did not list would be sent out.
	std::vector<uint64_t> players = mine ? TourneyMatchPlayers(next, SteamOwnId()) : std::vector<uint64_t>();
	for (const std::vector<uint64_t> &side : k.teams) {
		for (uint64_t id : side) {
			if (std::find(players.begin(), players.end(), id) == players.end()) players.push_back(id);
		}
	}
	const size_t needed = teams ? 4 : 2;
	if (mine && g_s.room.owner && next.opponent.steamId && players.size() >= needed) {
		for (const OnlineMember &m : g_s.room.members) {
			if (m.me || m.id == SteamOwnId() || std::find(players.begin(), players.end(), m.id) != players.end() || !k.kicked.insert(m.id).second) continue;
			LogI("league", "%s is not a player of the tournament match; out of the room", m.name.c_str());
			SteamKick(m.id);
		}
	}
	// Each player puts their own slot in their team, which nobody can change after; the game lets only a slot's player do it.
	if (teams && k.team && !k.teamSet && k.teamTries < kTeamTries && !g_s.inOnlineGame && g_s.game.active && !g_s.game.busy && now - k.teamAt >= 800) {
		k.teamAt = now;
		k.teamTries++;
		k.applying = true;
		k.teamSet = ApplyTourneyTeam(k);
		k.applying = false;
		if (k.teamSet) LogI("league", "the room's slot of this player is in team %d", k.team);
		else if (k.teamTries >= kTeamTries) LogW("league", "the room's slot of this player could not be put in team %d; the room is left as it is", k.team);
	}
	// Something put the slot in another team (a seat moved): it is set again.
	if (k.teamSet && g_s.game.active && !g_s.game.busy) {
		const int slot = g_s.game.localSlot;
		if (slot >= 0 && slot < 8 && g_s.game.slots[static_cast<size_t>(slot)].team != k.team) {
			k.teamSet = false;
			k.teamTries = 0;
		}
	}
	// The next game of a series may be on another map: once it is chosen, the room is set again for it.
	const TourneyMap gameMap = TourneyGameMap(next);
	if (mine && k.applied && !g_s.inOnlineGame && (next.pick.present || !TourneySameMap(k.appliedMap, gameMap))) {
		LogI("league", "game %d of tournament match %lld is on another map; the room is set again", next.game, static_cast<long long>(match));
		k.applied = k.mapAllowed = k.mapWarned = false;
		k.tries = 0;
		k.pick = -1;
	}
	// This computer's copy of a scenario that is not the cup's is told once, before the game starts.
	if (mine && !k.mapWarned && gameMap.Scenario()) {
		const CupMapStatus map = LeagueCupMap(gameMap).status;
		if (map != CupMapStatus::Checking) {
			k.mapWarned = true;
			const std::string warning = map == CupMapStatus::Downloading ? std::string() : CupMapWarning(gameMap);
			if (!warning.empty()) Toast(warning, true);
		}
	}
	// Whatever changed the room after (the host's own choices) is put back before the game starts.
	if (mine && k.applied && !g_s.inOnlineGame && g_s.game.active && g_s.game.host && !g_s.game.busy && !LeagueRoomHeld(g_s.game, g_s.room.id)) {
		LogI("league", "the room no longer has the tournament's options; they are set again");
		k.applied = false;
		k.tries = 0;
	}
	if (!mine || next.pick.present || !g_s.game.active || !g_s.game.host || g_s.game.busy || k.applied || k.tries >= 24 || now - k.at < 800) return;
	k.at = now;
	k.tries++;
	k.applying = true;
	const bool done = ApplyTourneyRoom(next, k);
	k.applying = false;
	if (done) {
		k.applied = true;
		k.appliedMap = gameMap;
		LogI("league", "the tournament's options are set in the room%s", k.mapAllowed ? " (the map is the host's to pick)" : "");
	}
	else if (k.tries >= 24) LogW("league", "the tournament's options could not all be set in the room");
}

std::string Initials(const std::string &name)
{
	std::string out;
	int letters = 0;
	bool start = true;
	for (size_t i = 0; i < name.size() && letters < 2;) {
		const unsigned char lead = static_cast<unsigned char>(name[i]);
		const size_t n = lead < 0x80 ? 1 : lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		const bool word = lead >= 0x80 || isalnum(lead);
		if (word && start) {
			out += name.substr(i, std::min(n, name.size() - i));
			letters++;
		}
		start = !word;
		i += n;
	}
	return out.empty() ? "?" : out;
}

bool LocalTime(int64_t ms, SYSTEMTIME &out)
{
	if (ms <= 0) return false;
	ULARGE_INTEGER u;
	u.QuadPart = static_cast<unsigned long long>(ms) * 10000ULL + 116444736000000000ULL;
	FILETIME ft = {u.LowPart, u.HighPart};
	SYSTEMTIME utc;
	return FileTimeToSystemTime(&ft, &utc) && SystemTimeToTzSpecificLocalTime(nullptr, &utc, &out);
}

int DayNumber(const SYSTEMTIME &t)
{
	SYSTEMTIME d = t;
	d.wHour = d.wMinute = d.wSecond = d.wMilliseconds = 0;
	FILETIME ft;
	if (!SystemTimeToFileTime(&d, &ft)) return 0;
	const unsigned long long v = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int>(v / 864000000000ULL);
}

const char *const kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const kWeekdaysLong[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// A moment some days away in the words of `templates`: today, tomorrow, a weekday within the week, or a weekday and its day of the month.
std::string DayWords(int64_t ms, const char *const (&templates)[4])
{
	SYSTEMTIME t, now;
	if (!LocalTime(ms, t) || !LocalTime(NowMs(), now)) return std::string();
	const int days = DayNumber(t) - DayNumber(now);
	if (days == 0) return TrFmt(templates[0], {HourText(ms)});
	if (days == 1) return TrFmt(templates[1], {HourText(ms)});
	const std::string weekday = Tr(kWeekdaysLong[t.wDayOfWeek % 7]);
	if (days > 1 && days < 7) return TrFmt(templates[2], {weekday, HourText(ms)});
	return TrFmt(templates[3], {weekday, std::to_string(t.wDay), HourText(ms)});
}

}

int64_t NowMs()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	const unsigned long long t = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int64_t>((t - 116444736000000000ULL) / 10000ULL);
}

std::string HourText(int64_t ms)
{
	SYSTEMTIME t;
	if (!LocalTime(ms, t)) return "--:--";
	char buf[16];
	sprintf_s(buf, "%02u:%02u", t.wHour, t.wMinute);
	return buf;
}

std::string DayText(int64_t ms)
{
	SYSTEMTIME t;
	if (!LocalTime(ms, t)) return std::string();
	return Tr(kWeekdays[t.wDayOfWeek % 7]) + " " + std::to_string(t.wDay) + " " + Tr(kMonths[(t.wMonth + 11) % 12]);
}

std::string WhenText(int64_t ms)
{
	SYSTEMTIME t, now;
	if (!LocalTime(ms, t) || !LocalTime(NowMs(), now)) return std::string();
	const int days = DayNumber(t) - DayNumber(now);
	if (days == 0) return TrFmt("Today, %s", {HourText(ms)});
	if (days == 1) return TrFmt("Tomorrow, %s", {HourText(ms)});
	if (days == -1) return TrFmt("Yesterday, %s", {HourText(ms)});
	return DayText(ms) + " \xC2\xB7 " + HourText(ms);
}

int64_t LocalMs(int64_t utcMs)
{
	SYSTEMTIME t;
	FILETIME ft;
	if (!LocalTime(utcMs, t) || !SystemTimeToFileTime(&t, &ft)) return utcMs;
	const unsigned long long v = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int64_t>((v - 116444736000000000ULL) / 10000ULL);
}

int64_t UtcMs(int64_t localMs)
{
	ULARGE_INTEGER u;
	u.QuadPart = static_cast<unsigned long long>(std::max<int64_t>(0, localMs)) * 10000ULL + 116444736000000000ULL;
	FILETIME ft = {u.LowPart, u.HighPart}, out;
	SYSTEMTIME local, utc;
	if (!FileTimeToSystemTime(&ft, &local) || !TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &out)) return localMs;
	const unsigned long long v = (static_cast<unsigned long long>(out.dwHighDateTime) << 32) | out.dwLowDateTime;
	return static_cast<int64_t>((v - 116444736000000000ULL) / 10000ULL);
}

std::string LongDayText(int64_t ms)
{
	static const char *const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
	static const char *const kMonthsLong[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
	SYSTEMTIME t;
	if (!LocalTime(ms, t)) return std::string();
	std::string out = TrFmt("%s, %s %s", {Tr(kDays[t.wDayOfWeek % 7]), std::to_string(t.wDay), Tr(kMonthsLong[(t.wMonth + 11) % 12])});
	if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
	return out;
}

std::string DurationText(int64_t ms)
{
	const int64_t min = std::max<int64_t>(0, ms / 60000);
	if (min < 60) return TrFmt("%s min", {std::to_string(min)});
	return TrFmt("%s h %s min", {std::to_string(min / 60), std::to_string(min % 60)});
}

std::string LongWaitText(int64_t ms)
{
	const int64_t hours = std::max<int64_t>(0, ms / 3600000);
	if (hours < 48) return DurationText(ms);
	return TrFmt("%s d %s h", {std::to_string(hours / 24), std::to_string(hours % 24)});
}

std::string UntilText(int64_t ms)
{
	static const char *const kTemplates[4] = {"until today, %s", "until tomorrow, %s", "until %s %s", "until %s %s, %s"};
	return DayWords(ms, kTemplates);
}

std::string FromText(int64_t ms)
{
	static const char *const kTemplates[4] = {"from today, %s", "from tomorrow, %s", "from %s %s", "from %s %s, %s"};
	return DayWords(ms, kTemplates);
}

std::string ShortWhenText(int64_t ms)
{
	SYSTEMTIME t, now;
	if (!LocalTime(ms, t) || !LocalTime(NowMs(), now)) return std::string();
	const int days = DayNumber(t) - DayNumber(now);
	const std::string day = days == 0 ? Tr("Today") : days == 1 ? Tr("Tomorrow") : Tr(kWeekdays[t.wDayOfWeek % 7]);
	return day + " " + HourText(ms);
}

std::string StageText(TourneyStage stage, int round)
{
	const char *name = TourneyStageName(stage);
	return *name ? Tr(name) : TrFmt("Round %s", {std::to_string(round)});
}

std::string FormatText(const TourneySummary &t)
{
	return Tr(TourneyIsSwiss(t) ? "Swiss" : TourneyIsDouble(t) ? "Double elimination" : "Single elimination") + " \xC2\xB7 " + Tr(t.Teams() ? "2 vs 2" : "1 vs 1") +
		(t.teamDraw ? " \xC2\xB7 " + Tr("drawn teams") : std::string());
}

std::string ContentText(const TourneySummary &t)
{
	if (!t.contentName.empty()) return t.contentName;
	return Tr(t.contentKey.empty() ? "The mod's rules" : "Its own content");
}

std::string PresetText(const TourneySettings &s)
{
	if (_stricmp(s.preset.c_str(), "competitive") == 0) return Tr("Competitive settings");
	if (_stricmp(s.preset.c_str(), "classic") == 0) return Tr("Classic settings");
	if (_stricmp(s.preset.c_str(), "various") == 0) return Tr("Various settings");
	return s.preset.empty() ? Tr("The room's settings") : s.preset;
}

std::string MapText(const TourneyMap &m)
{
	if (!m.Known()) return Tr("Chosen in the room");
	if (m.Scenario()) return m.title.empty() ? TourneyScenarioStem(m.file) : m.title;
	std::string out = LeagueMapText(m.name);
	if (!m.size.empty()) out += " \xC2\xB7 " + LeagueMapText(m.size);
	return out;
}

std::string PaceText(const TourneySummary &t)
{
	// Not "Live": next to a finished cup it read as its state.
	if (t.pace == TourneyPace::Live) return Tr("In one evening");
	return TourneyRoundDaysValid(t.roundDays) ? RoundDaysText(t.roundDays) : Tr("By rounds, with deadlines");
}

std::string RoundText(const TourneySummary &t)
{
	if (t.pace != TourneyPace::Rounds || t.status != TourneyStatus::Running || t.currentRound <= 0) return std::string();
	const int total = TourneyRoundCount(t);
	std::string out = total >= t.currentRound ? TrFmt("Round %s of %s", {std::to_string(t.currentRound), std::to_string(total)})
											  : TrFmt("Round %s", {std::to_string(t.currentRound)});
	if (t.roundEndsAt > 0) out += " \xC2\xB7 " + UntilText(t.roundEndsAt);
	return out;
}

std::string RoundDaysText(int days)
{
	if (days == 1) return Tr("By rounds of 1 day");
	if (days == 7) return Tr("By rounds of a week");
	return TrFmt("By rounds of %s days", {std::to_string(days)});
}

std::string BracketStageText(TourneyBracket bracket, TourneyStage stage, int round)
{
	switch (bracket) {
	case TourneyBracket::Losers: return TrFmt("Losers' round %s", {std::to_string(round)});
	case TourneyBracket::Final: return Tr("Grand final");
	case TourneyBracket::Reset: return Tr("Reset match");
	case TourneyBracket::Swiss: return TrFmt("Round %s (Swiss)", {std::to_string(round)});
	default: return StageText(stage, round);
	}
}

std::string ResultText(const TourneyOpResult &r)
{
	if (r.ok) {
		switch (r.op) {
		case TourneyOp::SignUp: return Tr(r.waitlist ? "The places are taken: you are on the waiting list." : "You are signed up. We'll tell you when the check-in opens.");
		case TourneyOp::Withdraw: return Tr("You have withdrawn from the tournament.");
		case TourneyOp::CheckIn: return Tr("Check-in done. See you at the start!");
		case TourneyOp::NoShow: return Tr("Your opponent did not come: the win is yours.");
		case TourneyOp::Dispute: return Tr("The organisers will review the match.");
		case TourneyOp::Cancel: return Tr("The cup is cancelled. Its players will see it on its page.");
		case TourneyOp::CheckinNow: return Tr("The check-in is open: the cup starts when it closes.");
		case TourneyOp::StartNow: return Tr("The cup has started: the bracket is drawn.");
		case TourneyOp::RemoveEntry: return Tr("The player is out of the cup.");
		case TourneyOp::Resolve: return Tr("Done: the match is settled.");
		case TourneyOp::Edit: return Tr("Changes saved: the cup's page and the list show them now.");
		case TourneyOp::Pick: return r.map.Known() ? TrFmt("Map of the next game: %s.", {MapText(r.map)}) : Tr("Map chosen.");
		case TourneyOp::Accept: return Tr("You are in the team. We'll tell you when the check-in opens: either of you can do it.");
		case TourneyOp::Decline: return Tr("Invitation declined.");
		case TourneyOp::CancelTeam: return Tr("Invitation cancelled: your team is not signed up.");
		case TourneyOp::Bots:
			if (r.added == 1) return Tr("A test bot took the free place.");
			return r.added > 0 ? TrFmt("%s test bots took the free places.", {std::to_string(r.added)}) : Tr("No place was free: the cup is full.");
		default: return std::string();
		}
	}
	const std::string &e = r.error;
	if (e == "partner_not_linked") return Tr("Your partner has not linked the game to the league yet: they link it from Multiplayer, in the game, and then you invite them again.");
	if (e == "partner_taken") return Tr("Your partner is already in another team of this cup.");
	if ((r.op == TourneyOp::Accept || r.op == TourneyOp::Decline) && (r.status == 404 || r.status == 410 || e == "expired" || e == "not_found")) {
		return Tr("That invitation is no longer waiting for an answer: it expired or was cancelled.");
	}
	if (e == "already_signed" && (r.op == TourneyOp::Accept || r.op == TourneyOp::SignUp) && r.status == 409) {
		return Tr(r.op == TourneyOp::Accept ? "You are already in a team of this cup." : "You are already signed up.");
	}
	if (e == "not_linked") return Tr("Link your league account first.");
	if (e == "not_your_pick") return Tr("It is not your turn to choose the map.");
	if (e == "not_in_bracket") return Tr("That player is no longer in the first round.");
	if (e == "mode_locked") return Tr("Players have signed up: the mode can no longer change.");
	if (e == "too_many_signed") return Tr("More players have signed up than that: take some off first.");
	if (e == "panel_only") return Tr("An official tournament is cancelled from the league's web panel.");
	if (e == "changed") return Tr("The cup changed meanwhile: try again.");
	if (e == "pick_closed") return Tr("The time to choose is over: the map was drawn.");
	if (e == "closed") return Tr("Sign-up for this tournament is closed.");
	if (e == "suspended" && r.who == "partner") return TrFmt("Two no-shows in 30 days: your partner can sign up for official tournaments again from %s.", {WhenText(r.until)});
	if (e == "suspended") return TrFmt("Two no-shows in 30 days: you can sign up for official tournaments again from %s.", {WhenText(r.until)});
	if (e == "banned" && r.who == "partner") return TrFmt("The league team has banned your partner until %s: they can't play tournaments until then.", {WhenText(r.until)});
	if (e == "banned") return TrFmt("The league team has banned you until %s: you can't play or organize tournaments until then.", {WhenText(r.until)});
	if (e == "dispute_closed") return Tr("This match can no longer be disputed: a finished match can be disputed for one day.");
	if (e == "already_signed") return Tr("You are already signed up.");
	if (e == "checkin_closed") return Tr("The check-in is not open.");
	if (e == "wrong_content") return OtherContentText(r.contentName);
	if (e == "wrong_mods") return WrongModsText(r.mods);
	if (e == "not_ready") return Tr("The match is not waiting for players.");
	if (e == "too_early") return r.noshowFrom > 0 ? TrFmt("It is too early to claim the win: the no-show counts %s.", {FromText(r.noshowFrom)}) : Tr("The waiting time is not over yet.");
	if (e == "not_in_room") return Tr("You have to be in the room to claim the win.");
	if (e == "opponent_present") return Tr("Your opponent is in the room.");
	if (e == "not_claimed") return Tr("Someone else is opening the room.");
	if (e == "not_eligible") {
		return TrFmt("To organise a cup you need %s online games registered by the league; you have %s.", {std::to_string(r.gamesNeeded), std::to_string(r.gamesPlayed)});
	}
	if (e == "already_organizing") return Tr("You already organise a cup that is not over. Finish it or cancel it first.");
	if (e == "invite_required") return Tr("This cup is private: you need its code to sign up.");
	if (e == "not_enough") return TrFmt("%s players have checked in and the cup needs %s to start.", {std::to_string(r.checkedIn), std::to_string(r.needed)});
	if (e == "own_match") return Tr("You play this match: the league's team settles it.");
	if (e == "not_organizer") return Tr("Only the organiser of the cup can do that.");
	if (e == "started" && r.op == TourneyOp::Bots) return Tr("The cup has moved on: that cannot be done at this stage any more.");
	if (e == "wrong_status") return Tr("The cup has moved on: that cannot be done at this stage any more.");
	if (e == "removed") return Tr("The organiser took you off this cup: you cannot sign up again.");
	if (e == "requirement") {
		std::string why;
		// The other player of a team: the partner a captain invites, or the captain whose invitation is accepted.
		const bool partner = r.who == "partner", captain = r.who == "captain";
		if (r.requirement == "place") {
			why = r.have.empty() ? TrFmt("This cup is for the top %s of the season's ranked ladder, and you are not placed on it yet.", {r.need})
								 : TrFmt("This cup is for the top %s of the season's ranked ladder, and you are number %s.", {r.need, r.have});
		}
		else if (r.requirement == "tier") {
			const LeagueTier need = LeagueTierFromId(r.need), have = LeagueTierFromId(r.have);
			why = TrFmt(partner ? "This cup is for players up to %s, and your partner is %s."
						: captain ? "This cup is for players up to %s, and the team's captain is %s."
								  : "This cup is for players up to %s, and you are %s.",
				{Tr(LeagueTierName(need)), Tr(LeagueTierName(have))});
		}
		else {
			why = TrFmt(partner ? "This cup asks for %s online games registered by the league, and your partner has %s."
						: captain ? "This cup asks for %s online games registered by the league, and the team's captain has %s."
								  : "This cup asks for %s online games registered by the league, and you have %s.",
				{r.need.empty() ? "?" : r.need, r.have.empty() ? "0" : r.have});
		}
		if (partner || captain) return why + " " + Tr("Both players of a team have to meet the requirements.");
		if (r.op == TourneyOp::Create || r.op == TourneyOp::Edit) return why + " " + Tr("To play your own cup you must meet its requirements: change them, or untick \"I play too\".");
		return why;
	}
	if (e == "locked") return Tr("Another player has just signed up: now only the description can change.");
	if (e == "map_missing") return Tr("The league is missing one of the cup's scenarios. Publish again and it goes up.");
	if (e == "map_changed") return TrFmt("The scenario \"%s\" has changed since you chose it. Choose it again.", {r.mapTitle});
	if (e == "map_unreadable") return TrFmt("The scenario \"%s\" could not be read. Choose it again.", {r.mapTitle});
	if (e == "too_large") return TrFmt("The scenario \"%s\" is larger than 16 MB: the league does not keep files that large.", {r.mapTitle});
	if (e == "hash_mismatch") return TrFmt("The scenario \"%s\" did not arrive whole. Try again.", {r.mapTitle});
	if (e == "upload_failed" || e == "upload_refused") return TrFmt("The scenario \"%s\" could not go up to the league. Check your connection and try again.", {r.mapTitle});
	if (e == "rate_limited" && !r.mapTitle.empty()) {
		return r.retryAfter > 0 ? TrFmt("You have uploaded too many scenarios today. Try again in %s.", {WaitText(r.retryAfter)}) : Tr("You have uploaded too many scenarios today. Try again tomorrow.");
	}
	if (e == "rate_limited") {
		if (r.op == TourneyOp::FindCode) return TrFmt("Too many wrong codes. Try again in %s.", {WaitText(r.retryAfter > 0 ? r.retryAfter : 600)});
		if (r.op == TourneyOp::Create) return r.retryAfter > 0 ? TrFmt("You can make up to 5 cups a day. Try again in %s.", {WaitText(r.retryAfter)}) : Tr("You can make up to 5 cups a day. Try again later.");
		return r.retryAfter > 0 ? TrFmt("Too many requests in a short time. Try again in %s.", {WaitText(r.retryAfter)}) : Tr("Too many requests in a short time. Try again in a moment.");
	}
	if (e == "not_ready" && r.op == TourneyOp::Resolve) return Tr("That match is not being played any more.");
	if (e == "not_found") return Tr("The league could not find it. It may have changed: try again.");
	if (e == "unofficial_build") return Tr("This copy of Imperivm Resurrected is not an official version. Install the official one to play the league and tournaments.");
	if (!r.status) return Tr("Couldn't reach the league server. Check your connection and try again.");
	if ((r.op == TourneyOp::Create || r.op == TourneyOp::Edit) && r.status == 400) return Tr("The league did not accept the cup's details. Check them and try again.");
	return Tr("The league server refused the request. Try again later.");
}

const TourneyMyEntry *MyEntry(const TourneyMineView &mine, int64_t tournament)
{
	return mine.loaded ? TourneyMyEntryOf(mine.mine, tournament) : nullptr;
}

const TourneyMatch *MatchById(const TourneyDetail &d, int64_t id)
{
	for (const TourneyMatch &m : d.matches) {
		if (m.id == id) return &m;
	}
	return nullptr;
}

bool InviteBusy(int64_t tournament)
{
	return LeagueTourneyBusy(TourneyOp::Accept, tournament) || LeagueTourneyBusy(TourneyOp::Decline, tournament);
}

const TourneySummary *Featured(const TourneyListsView &lists)
{
	for (const std::vector<TourneySummary> *list : {&lists.open, &lists.live, &lists.finished}) {
		for (const TourneySummary &t : *list) {
			if (t.official) return &t;
		}
	}
	return nullptr;
}

bool IsOrganizer(const TourneySummary &t)
{
	return !t.official && t.organizer.steamId && t.organizer.steamId == SteamOwnId();
}

bool CanManage(const TourneySummary &t)
{
	if (!t.official) return IsOrganizer(t);
	return LeagueTourneyMine()->mine.organize.staff;
}

std::string OrganizerText(const TourneySummary &t)
{
	if (t.official || !t.organizer.Present()) return std::string();
	if (IsOrganizer(t)) return Tr("You organise it");
	return TrFmt("By %s", {t.organizer.name.empty() ? Tr("a player") : t.organizer.name});
}

std::string WaitText(int seconds)
{
	if (seconds < 60) return TrFmt("%s s", {std::to_string(std::max(1, seconds))});
	if (seconds < 3600) return TrFmt("%s min", {std::to_string((seconds + 59) / 60)});
	return TrFmt("%s h", {std::to_string((seconds + 3599) / 3600)});
}

int SecondButtonWidth(Painter &p, const std::string &label)
{
	return GhostButtonWidth(p, label, false);
}

void DrawCupCrown(Painter &p, int x, int y, int size, bool official)
{
	if (official) {
		DrawLeaguePhalera(p, x, y, size, LeagueMetal::Gold);
		return;
	}
	const COLORREF gold = RGB(0xe0, 0xb5, 0x4a);
	PaintWreath(p, x, y, size, gold);
	UseDisplay(p, size * 45 / 100);
	const bool raw = p.raw;
	p.raw = true;
	p.Text(x, y, size, size, "1", gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = raw;
}

const char *FinishedEntrantText(const TourneyDetail &d, const TourneyEntrant &e)
{
	if (d.status != TourneyStatus::Finished || (e.status != TourneyEntryStatus::Playing && e.status != TourneyEntryStatus::Out)) return "";
	const int place = EntrantPlace(d, e);
	if (place == 1) return "Champion";
	// Nobody is knocked out of a Swiss tournament: everyone plays every round.
	if (TourneyIsSwiss(d)) return place == 2 ? "2nd place" : place == 3 ? "3rd place" : "Finished the cup";
	if (place == 2) return "Finalist";
	if (place == 3) return TourneyIsDouble(d) ? "3rd place" : "Semifinals";
	return "Knocked out";
}

void PaintSecondButton(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my)
{
	PaintGhostButton(p, x, y, w, h, label, kTourney, false, enabled, mx, my, arg);
}

void PaintSegments(Painter &p, int x, int y, int w, int h, const std::vector<std::string> &labels, int selected, const std::vector<int> &args, int mx, int my)
{
	const int n = static_cast<int>(labels.size());
	if (!n) return;
	const int gap = 2;
	int cx = x;
	for (int i = 0; i < n; i++) {
		const int cw = i + 1 == n ? x + w - cx : (w - gap * (n - 1)) / n;
		const bool on = i == selected, hot = !on && p.Hover(cx, y, cw, h, mx, my);
		p.WellSegment(cx, y, cw, h, on, hot);
		UseFace(p, BrandFace::UiStrong, 15);
		p.raw = true;
		p.Text(cx + 4, y, cw - 8, h, labels[static_cast<size_t>(i)], on || hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (!on && i < static_cast<int>(args.size())) p.AddHit(cx, y, cw, h, kTourney, args[static_cast<size_t>(i)]);
		cx += cw + gap;
	}
}

int TagWidth(Painter &p, const std::string &text)
{
	UseFace(p, BrandFace::UiStrong, 11);
	return p.CapsWidth(text, 2) + 16;
}

int PaintTag(Painter &p, int x, int y, const std::string &text, COLORREF back, COLORREF ink, bool outline)
{
	const int w = TagWidth(p, text);
	if (outline) p.Frame(x, y, w, 22, back);
	else p.Fill(x, y, w, 22, back);
	p.TextCaps(x, y, w, 22, text, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	return w;
}

void PaintCaps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, UINT fmt)
{
	UseFace(p, BrandFace::UiStrong, 13);
	p.TextCaps(x, y, w, h, text, ink, fmt, 2);
}

void UseDisplay(Painter &p, int size)
{
	UseFace(p, BrandFace::Display, size);
}

void UseFace(Painter &p, BrandFace face, int size)
{
	SelectObject(p.dc, BrandFont(face, size, p.s));
}

void PaintPlayerDisc(Painter &p, int x, int y, int size, uint64_t id, const std::string &name, COLORREF ring)
{
	if (ring != CLR_INVALID) {
		const int gap = size >= 80 ? 3 : 2;
		p.Shape(x - gap - 1, y - gap - 1, size + 2 * gap + 2, size + 2 * gap + 2, InUnitEllipse, ring);
		p.Shape(x - gap, y - gap, size + 2 * gap, size + 2 * gap, InUnitEllipse, pal::bg);
	}
	const std::shared_ptr<const OnlineAvatar> a = id ? SteamAvatar(id) : nullptr;
	const RECT r = p.R(x, y, size, size);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	if (a && a->width > 0 && a->height > 0 && a->bgra.size() >= static_cast<size_t>(a->width) * a->height && pw > 0 && ph > 0) {
		// GDI has no smooth clip: the disc's coverage goes into the alpha of a copy scaled by hand.
		const std::vector<uint8_t> mask = CachedShapeMask(InUnitEllipse, pw, ph);
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = pw;
		bi.bmiHeader.biHeight = -ph;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		uint32_t *bits = nullptr;
		HBITMAP bmp = mask.size() == static_cast<size_t>(pw) * ph ? CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, reinterpret_cast<void **>(&bits), nullptr, 0) : nullptr;
		if (bmp && bits) {
			for (int py = 0; py < ph; py++) {
				const int sy = std::min(a->height - 1, py * a->height / ph);
				for (int px = 0; px < pw; px++) {
					const uint32_t c = a->bgra[static_cast<size_t>(sy) * a->width + std::min(a->width - 1, px * a->width / pw)];
					const uint32_t al = mask[static_cast<size_t>(py) * pw + px];
					bits[static_cast<size_t>(py) * pw + px] =
						(al << 24) | ((((c >> 16) & 255) * al / 255) << 16) | ((((c >> 8) & 255) * al / 255) << 8) | ((c & 255) * al / 255);
				}
			}
			GdiFlush();
			HDC mem = CreateCompatibleDC(p.dc);
			HGDIOBJ old = SelectObject(mem, bmp);
			BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
			GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
			SelectObject(mem, old);
			DeleteDC(mem);
			DeleteObject(bmp);
			return;
		}
		if (bmp) DeleteObject(bmp);
	}
	p.Shape(x, y, size, size, InUnitEllipse, RGB(0x2e, 0x29, 0x26));
	UseFace(p, BrandFace::UiStrong, std::max(9, size * 3 / 8));
	p.raw = true;
	p.TextCaps(x, y, size, size, Initials(name), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
	p.raw = false;
}

void PaintCheck(Painter &p, int x, int y, int size, bool ok)
{
	p.Shape(x, y, size, size, InUnitEllipse, ok ? pal::ok : pal::bad);
	const COLORREF ink = pal::bg;
	if (ok) {
		p.Line(x + size * 27 / 100, y + size * 52 / 100, x + size * 44 / 100, y + size * 69 / 100, ink, 2);
		p.Line(x + size * 44 / 100, y + size * 69 / 100, x + size * 75 / 100, y + size * 34 / 100, ink, 2);
	}
	else {
		p.Line(x + size * 32 / 100, y + size * 32 / 100, x + size * 68 / 100, y + size * 68 / 100, ink, 2);
		p.Line(x + size * 68 / 100, y + size * 32 / 100, x + size * 32 / 100, y + size * 68 / 100, ink, 2);
	}
}

void PaintRing(Painter &p, int cx, int cy, int r, COLORREF c)
{
	HPEN pen = CreatePen(PS_SOLID, 1, c);
	HGDIOBJ oldPen = SelectObject(p.dc, pen), oldBrush = SelectObject(p.dc, GetStockObject(NULL_BRUSH));
	const RECT box = p.R(cx - r, cy - r, 2 * r, 2 * r);
	::Ellipse(p.dc, box.left, box.top, box.right, box.bottom);
	SelectObject(p.dc, oldBrush);
	SelectObject(p.dc, oldPen);
	DeleteObject(pen);
}

void PaintDashedFrame(Painter &p, int x, int y, int w, int h, COLORREF c)
{
	for (int i = 0; i < w; i += 8) {
		p.Fill(x + i, y, std::min(4, w - i), 1, c);
		p.Fill(x + i, y + h - 1, std::min(4, w - i), 1, c);
	}
	for (int i = 0; i < h; i += 8) {
		p.Fill(x, y + i, 1, std::min(4, h - i), c);
		p.Fill(x + w - 1, y + i, 1, std::min(4, h - i), c);
	}
}

int PaintName(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, COLORREF ink, int mx, int my)
{
	const LeagueBadge badge = id ? PlayerBadgeOf(id) : LeagueBadge();
	const PatronInfo patron = id ? PatronOf(id) : PatronInfo();
	const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	p.raw = true;
	if (patron.tier != PatronTier::None) {
		const int size = std::max(12, std::min(20, h - 2)), ly = y + (h - size) / 2, nx = x + size + 4;
		const int nameW = std::max(20, w - (nx - x) - LeagueBadgeRoom(badge));
		DrawPatron(p, x, ly, size, nx, y, nameW, h, name, patron.tier, fmt);
		PatronHotspot(p, x, ly, size, patron, mx, my, kPatronLink);
		SteamProfileHotspot(p, nx, y, nameW, h, name, id, mx, my);
		DrawBadgeAfterName(p, nx, y, nameW, h, name, badge, mx, my, id);
		p.raw = false;
		return nx;
	}
	const int nameW = std::max(20, w - LeagueBadgeRoom(badge));
	p.Text(x, y, nameW, h, name, ink, fmt);
	SteamProfileHotspot(p, x, y, nameW, h, name, id, mx, my);
	DrawBadgeAfterName(p, x, y, nameW, h, name, badge, mx, my, id);
	p.raw = false;
	return x;
}

// The bracket's zoom steps, in percent.
const int kZoomSteps[] = {40, 50, 60, 70, 80, 90, 100, 115, 130};

int ZoomStep(int zoom, int dir)
{
	const int n = static_cast<int>(sizeof(kZoomSteps) / sizeof(kZoomSteps[0]));
	if (dir > 0) {
		for (int i = 0; i < n; i++) {
			if (kZoomSteps[i] > zoom) return kZoomSteps[i];
		}
		return kZoomSteps[n - 1];
	}
	for (int i = n - 1; i >= 0; i--) {
		if (kZoomSteps[i] < zoom) return kZoomSteps[i];
	}
	return kZoomSteps[0];
}

// A new zoom, keeping the point (fx, fy) of the view (UI units) on the same place of the bracket.
void SetZoom(Ui &u, int zoom, int fx, int fy)
{
	zoom = std::max(kZoomMin, std::min(kZoomMax, zoom));
	if (zoom == u.bracketZoom) return;
	const float k = static_cast<float>(zoom) / static_cast<float>(u.bracketZoom);
	u.bracketX = std::max(0, static_cast<int>((u.bracketX + fx) * k) - fx);
	u.bracketY = std::max(0, static_cast<int>((u.bracketY + fy) * k) - fy);
	u.bracketZoom = zoom;
}

void SetScrollArea(Painter &p, ScrollArea &a, int x, int y, int w, int h, int max)
{
	a.valid = true;
	a.r = p.R(x, y, w, h);
	OffsetRect(&a.r, p.ox, p.oy);
	a.max = max;
}

}

void PaintTournaments(Painter &p, const State &st)
{
	Ui u;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		u = g_tourney;
	}
	LeagueTourneyWatching();
	PaintOut out;
	const bool chooser = u.screen == kScreenCreate && u.chooserRound >= 0;
	const bool dialog = u.disputeMatch || u.joinOpen || !u.shownCode.empty() || u.walkoverMatch || chooser || u.teamCup;
	if (dialog) p.noHover = true;
	switch (u.screen) {
	case kScreenPage: PaintPageScreen(p, st, u, out); break;
	case kScreenMatch:
		PaintMatchScreen(p, st, u, out);
		if (u.colorOpen) PaintMatchColorMenu(p, st, u);
		break;
	case kScreenChampion: PaintChampionScreen(p, st, u); break;
	case kScreenCreate: PaintCreateScreen(p, st, u, out); break;
	default: PaintListScreen(p, st, u, out); break;
	}
	if (dialog) {
		UseSharedFonts(p);
		if (u.disputeMatch) PaintDisputeDialog(p, st, u);
		else if (u.teamCup) PaintTeamDialog(p, st, u, out);
		else if (u.walkoverMatch) PaintWalkoverDialog(p, st, u);
		else if (!u.shownCode.empty()) PaintCodeDialog(p, st, u);
		else if (chooser) PaintMapChooser(p, st, u, out);
		else PaintJoinDialog(p, st, u);
		UseScreenFonts(p);
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_tourney.listArea = out.listArea;
	g_tourney.bracketArea = out.bracketArea;
	g_tourney.pan = out.pan;
	g_tourney.sideArea = out.sideArea;
	g_tourney.formArea = out.formArea;
	g_tourney.chooserArea = out.chooserArea;
	g_tourney.teamArea = out.teamArea;
	g_tourney.pageSideArea = out.pageSideArea;
	g_tourney.summaryArea = out.summaryArea;
	g_tourney.matchRulesArea = out.matchRulesArea;
	if (out.pageSideH >= 0) g_tourney.pageSideH = out.pageSideH;
	if (out.summaryH >= 0) g_tourney.summaryH = out.summaryH;
	if (out.matchRulesH >= 0) g_tourney.matchRulesH = out.matchRulesH;
	if (u.teamCup) g_tourney.teamRows = out.teamRows;
	if (out.form) std::copy(std::begin(out.sectionTop), std::end(out.sectionTop), std::begin(g_tourney.sectionTop));
}

bool PaintTourneyHeader(Painter &p, const State &st)
{
	HeaderUi u;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		u.screen = g_tourney.screen;
		u.pageTab = g_tourney.pageTab;
		u.slug = g_tourney.slug;
		u.championSlug = g_tourney.championSlug;
		u.cupEditId = g_tourney.cupEditId;
		u.cupLocked = g_tourney.cupLocked;
	}
	switch (u.screen) {
	case kScreenPage: return PaintPageHeader(p, st, u);
	case kScreenMatch: return PaintMatchHeader(p, st, u);
	case kScreenChampion: return PaintChampionHeader(p, st, u);
	case kScreenCreate: return PaintCreateHeader(p, st, u);
	default: return false;
	}
}

void TourneyAction(int arg)
{
	Ui &u = g_tourney;
	const int sub = (arg >> 24) & 0x7F;
	const int64_t value = arg & 0xFFFFFF;
	switch (sub) {
	case kSubShow:
		g_s.view = kViewTournaments;
		if (!u.pendingChampion.empty()) {
			u.slug = u.championSlug = u.pendingChampion;
			u.screen = kScreenChampion;
			u.pendingChampion.clear();
		}
		LeagueTourneyListsRefresh(true);
		LeagueTourneyMineRefresh();
		break;
	case kSubFilter:
		u.filter = static_cast<int>(value);
		u.listScroll = 0;
		break;
	case kSubListKind:
	case kSubListFormat: {
		// The list's filters are menus: their choices, the one in use marked.
		const bool kind = sub == kSubListKind;
		if (g_s.dropOpen && g_s.dropKind == kDropTourney && g_s.dropIndex == (kind ? kCupMenuListKind : kCupMenuListFormat)) {
			g_s.dropOpen = false;
			break;
		}
		g_s.tourneyDropItems = kind ? std::vector<std::string>{Tr("Official and community"), Tr("Official only"), Tr("Community only")}
									: std::vector<std::string>{Tr("All formats"), Tr("Single elimination"), Tr("Double elimination"), Tr("Swiss")};
		g_s.tourneyDropSel = kind ? u.listKind : u.listFormat;
		g_s.dropOpen = true;
		g_s.dropKind = kDropTourney;
		g_s.dropIndex = kind ? kCupMenuListKind : kCupMenuListFormat;
		g_s.dropScroll = 0;
		break;
	}
	case kSubListPlayable:
		u.listPlayable = !u.listPlayable;
		u.listScroll = 0;
		break;
	case kSubOpen: OpenPage(SlugOf(value), 0); break;
	case kSubManage: OpenPage(SlugOf(value), 0); break;
	case kSubPageTab:
		u.pageTab = static_cast<int>(value);
		u.bracketX = u.bracketY = u.sideScroll = 0;
		break;
	case kSubSignUp:
	case kSubTeamOpen:
		if (!LeagueLinked()) Toast("Link your league account first.");
		else if (ModsBlocked(CupMods(value))) break;
		else if (sub == kSubTeamOpen || TeamsCup(value)) OpenTeamDialog(u, value);
		else LeagueTourneySignUp(value);
		break;
	case kSubTeamPick: {
		const std::vector<SteamFriendInfo> &friends = u.teamRows;
		if (value < 0 || value >= static_cast<int64_t>(friends.size())) break;
		u.teamPartner = friends[static_cast<size_t>(value)].id;
		u.teamPartnerName = friends[static_cast<size_t>(value)].name;
		u.teamError.clear();
		break;
	}
	case kSubTeamSend: TourneyFieldEnter(kTeamNameField); break;
	case kSubTeamCancel:
		u.teamCup = 0;
		if (g_s.focus == kTeamNameField || g_s.focus == kTeamSearchField) g_s.focus = kNoField;
		break;
	case kSubInviteAccept:
	case kSubInviteDecline: {
		const auto mine = LeagueTourneyMine();
		const TourneyInvite *invite = TourneyInviteOf(mine->mine, value);
		if (!invite) {
			Toast("That invitation is no longer waiting for an answer.");
			break;
		}
		if (InviteBusy(value)) break;
		LogI("league", "%s the invitation to a team of the cup %lld", sub == kSubInviteAccept ? "accepting" : "declining", static_cast<long long>(value));
		LeagueTourneyAnswerInvite(value, invite->entryId, sub == kSubInviteAccept);
		break;
	}
	case kSubTeamWithdraw: {
		const int64_t tournament = value;
		Confirm(Tr("Cancel the invitation"), Tr("Your team is taken off the cup before your partner answers. You can sign it up again, with them or with someone else."),
			Tr("Cancel the invitation"), [tournament] { LeagueTourneyCancelTeam(tournament); });
		break;
	}
	case kSubWithdraw: {
		const int64_t tournament = value;
		Confirm(Tr("Withdraw from the tournament"),
			Tr(TeamsCup(tournament) ? "Your whole team withdraws, your partner too. Once your match is ready, withdrawing counts as a no-show. Withdraw anyway?"
									: "Once your match is ready, withdrawing counts as a no-show. Withdraw anyway?"),
			Tr("Withdraw"), [tournament] { LeagueTourneyWithdraw(tournament); });
		break;
	}
	case kSubCheckIn: {
		const TourneyListsView lists = LeagueTourneyLists();
		const TourneySummary *t = SummaryOf(lists, value);
		if (t && !t->contentKey.empty() && t->contentKey != SteamOwnGameplayKey()) {
			Toast(OtherContentText(t->contentName), true);
			break;
		}
		if (ModsBlocked(CupMods(value))) break;
		LeagueTourneyCheckIn(value);
		break;
	}
	case kSubFillBots:
		if (!LeagueTourneyBusy(TourneyOp::Bots, value)) LeagueTourneyFillBots(value);
		break;
	case kSubPrepareMods: {
		const LeagueModsRule rule = CupMods(value);
		if (rule.set) LeagueModsPrepStart(rule, "tourney:" + std::to_string(value));
		break;
	}
	case kSubMatch:
		u.screen = kScreenMatch;
		LeagueTourneyMineRefresh();
		break;
	case kSubGo: {
		const auto mine = LeagueTourneyMine();
		const TourneyNext &next = mine->mine.next;
		if (!next.present || next.match != value) {
			Toast("This match is no longer waiting for you.");
			break;
		}
		if (g_s.room.id && TourneyMatchOfTag(g_s.room.info.leagueTag) == value) break;
		if (g_s.room.id || g_s.connecting) {
			Toast("Leave the room you are in first.");
			break;
		}
		if (!g_s.ready) {
			Toast(Tr("Online is not available: ") + g_s.status);
			break;
		}
		if (!next.contentKey.empty() && next.contentKey != SteamOwnGameplayKey()) {
			Toast(OtherContentText(next.contentName), true);
			break;
		}
		if (ModsBlocked(next.mods)) break;
		if (u.room.phase != RoomFlow::kIdle && u.room.match == value) break;
		u.room = RoomFlow();
		u.room.phase = RoomFlow::kClaiming;
		u.room.match = value;
		u.room.at = GetTickCount();
		LogI("league", "going to the room of tournament match %lld", static_cast<long long>(value));
		LeagueTourneyClaimRoom(value);
		break;
	}
	case kSubWatch: {
		uint64_t lobby = 0, spec = 0;
		if (!FindMatchRooms(value, lobby, spec)) {
			Toast("That game has no room to watch yet.");
			break;
		}
		if (g_s.room.id || g_s.connecting) {
			Toast("Leave the room you are in first.");
			break;
		}
		u.join = PendingJoin();
		u.join.lobby = lobby;
		u.join.spec = spec;
		u.join.watch = true;
		u.join.at = GetTickCount();
		SteamRefreshLobbies();
		break;
	}
	case kSubNoShow: LeagueTourneyClaimNoShow(value); break;
	case kSubPick: {
		const auto mine = LeagueTourneyMine();
		const TourneyNext &next = mine->mine.next;
		if (!next.present || !next.MyPick() || value >= static_cast<int64_t>(next.pick.options.size())) {
			Toast("It is not your turn to choose the map.");
			break;
		}
		if (LeagueTourneyBusy(TourneyOp::Pick, next.match)) break;
		LogI("league", "choosing option %d for game %d of tournament match %lld", static_cast<int>(value) + 1, next.game, static_cast<long long>(next.match));
		LeagueTourneyPick(next.match, static_cast<int>(value));
		break;
	}
	case kSubSteamChat: {
		const auto mine = LeagueTourneyMine();
		const TourneyNext &next = mine->mine.next;
		if (next.present && next.opponent.steamId) SteamOpenChat(next.opponent.steamId);
		break;
	}
	case kSubDispute:
		u.disputeMatch = value;
		u.disputeText.clear();
		FocusField(kTourneyField);
		break;
	case kSubDisputeSend: TourneyFieldEnter(kTourneyField); break;
	case kSubDisputeCancel:
		u.disputeMatch = 0;
		u.disputeText.clear();
		g_s.focus = kNoField;
		break;
	case kSubChampion: {
		const std::string slug = SlugOf(value);
		if (slug.empty()) break;
		u.slug = u.championSlug = slug;
		u.screen = kScreenChampion;
		break;
	}
	case kSubPan: {
		const int page = std::max(200, static_cast<int>((u.bracketArea.r.right - u.bracketArea.r.left) / std::max(0.1f, g_s.uiScale)) * 3 / 4);
		u.bracketX = std::max(0, std::min(u.bracketArea.maxX, u.bracketX + (value ? page : -page)));
		break;
	}
	case kSubZoom: {
		// 0 fits the bracket in its view; 1 and 2 zoom in and out a step.
		int zoom = u.bracketZoom;
		if (value == 0 && u.pan.contentW > 0 && u.pan.contentH > 0) {
			zoom = std::min(100, std::min(u.pan.viewW * 100 / u.pan.contentW, u.pan.viewH * 100 / u.pan.contentH));
			zoom = std::max(kZoomMin, zoom);
		}
		else zoom = ZoomStep(zoom, value == 1 ? 1 : -1);
		SetZoom(u, zoom, u.pan.viewW / 2, u.pan.viewH / 2);
		break;
	}
	case kSubRefresh:
		// The list, the player's own cups and the pages open, read again now.
		LeagueTourneyListsRefresh(true);
		LeagueTourneyMineRefresh();
		break;
	case kSubCreate: {
		if (!LeagueLinked()) {
			Toast("Link your league account first.");
			break;
		}
		const auto mine = LeagueTourneyMine();
		const TourneyOrganize &o = mine->mine.organize;
		if (o.activeId) {
			OpenPage(o.activeSlug.empty() ? SlugOf(o.activeId) : o.activeSlug, 0);
			break;
		}
		if (o.known && !o.allowed) {
			Toast(TrFmt("To organise a cup you need %s online games registered by the league; you have %s.", {std::to_string(o.gamesNeeded), std::to_string(o.gamesPlayed)}));
			break;
		}
		if (u.screen != kScreenCreate || u.cupEditId) {
			u.cup = NewCupForm(g_s.player, NowMs());
			u.cupError.clear();
			u.cupErrorSlug.clear();
			u.cupEditId = 0;
			u.cupEditSlug.clear();
			u.cupLocked = false;
			u.formScroll = 0;
			u.chooserRound = -1;
			CupScenariosRescan();
		}
		u.screen = kScreenCreate;
		break;
	}
	case kSubEdit: {
		const auto page = LeagueTourneyPage(u.slug, 60000);
		const TourneyDetail &d = page->detail;
		if (!page->loaded || d.id != value || !CanManage(d)) break;
		if (d.status != TourneyStatus::Signup && d.status != TourneyStatus::Checkin) {
			Toast("The cup is being played or is over: it can't change any more.");
			break;
		}
		u.cup = TourneyCupFormOf(d);
		u.cupEditId = d.id;
		u.cupEditSlug = d.slug;
		u.cupLocked = !d.official && TourneyCupLocked(d);
		u.cupEditOfficial = d.official;
		u.cupError.clear();
		u.cupErrorSlug.clear();
		u.formScroll = 0;
		u.chooserRound = -1;
		u.screen = kScreenCreate;
		CupScenariosRescan();
		break;
	}
	case kSubCupSet:
		if (u.cupLocked) break;
		ApplyCupSetting(u.cup, static_cast<int>(value >> 16), static_cast<int>(value & 0xFFFF), NowMs());
		u.cupError.clear();
		break;
	case kSubCupMenu: {
		if (u.cupLocked) break;
		const int menu = static_cast<int>(value);
		g_s.tourneyDropItems = CupMenuItems(u.cup, menu, g_s.tourneyDropSel);
		if (g_s.tourneyDropItems.empty()) break;
		g_s.dropOpen = true;
		g_s.dropKind = kDropTourney;
		g_s.dropIndex = menu;
		g_s.dropScroll = ClampDropScroll(g_s.tourneyDropSel - kDropRows / 2, static_cast<int>(g_s.tourneyDropItems.size()));
		break;
	}
	case kSubCivMenu: {
		const std::vector<std::pair<int, std::string>> choices = MatchCivChoices();
		const int race = MatchCiv(u);
		// The game's civilizations, Random first: a random one is the room's own choice.
		g_s.tourneyDropItems.clear();
		u.civChoices.clear();
		g_s.tourneyDropSel = 0;
		for (const auto &c : choices) {
			if (c.first == race) g_s.tourneyDropSel = static_cast<int>(u.civChoices.size());
			g_s.tourneyDropItems.push_back(c.second);
			u.civChoices.push_back(c.first);
		}
		g_s.dropOpen = true;
		g_s.dropKind = kDropTourney;
		g_s.dropIndex = kCupMenuCiv;
		g_s.dropScroll = ClampDropScroll(g_s.tourneyDropSel - kDropRows / 2, static_cast<int>(g_s.tourneyDropItems.size()));
		break;
	}
	case kSubColorPick: {
		const int pick = static_cast<int>(value);
		if (pick < 0 || pick >= 12) break;
		u.colorPick = pick;
		u.colorOpen = u.colorWheel = false;
		GameRoomSaveSetting("TourneyColor", u.colorPick);
		break;
	}
	case kSubColorOpen: {
		u.colorOpen = true;
		u.colorWheel = value == 1;
		if (u.colorWheel) {
			const COLORREF now = MatchColorRef(u);
			g_s.colorTry = now == CLR_INVALID ? 0xc0392b : static_cast<uint32_t>(GetRValue(now)) << 16 | static_cast<uint32_t>(GetGValue(now)) << 8 | GetBValue(now);
		}
		break;
	}
	case kSubColorUse:
		u.colorPick = kOwnColor;
		u.colorRgb = g_s.colorTry;
		u.colorOpen = u.colorWheel = false;
		GameRoomSaveSetting("TourneyColor", u.colorPick);
		GameRoomSaveSetting("TourneyColorRgb", static_cast<int>(u.colorRgb));
		break;
	case kSubColorClose: u.colorOpen = u.colorWheel = false; break;
	case kSubReady: {
		if (!g_s.game.active || !g_s.room.id || TourneyMatchOfTag(g_s.room.info.leagueTag) != value) break;
		const DWORD now = GetTickCount();
		if (u.readyAt && now - u.readyAt < 1500) break;
		const auto mine = LeagueTourneyMine();
		if (!LocalReady(g_s.game) && mine->mine.next.Teams() && !u.lock.teamSet) {
			Toast("Your team is being set in the room: get ready in a moment.");
			break;
		}
		u.readyAt = now;
		// Ready and not ready: after this, the player's own choice; the mod does not make them ready again.
		u.lock.readySent = true;
		Predict(kPredReady, g_s.game.localSlot, LocalReady(g_s.game) ? 0 : 1);
		GameRoomToggleReady("cup match page");
		break;
	}
	case kSubCupSection:
		if (value >= 0 && value < 3) u.formScroll = std::max(0, std::min(u.formArea.max, u.sectionTop[value]));
		break;
	case kSubMapOpen:
		if (u.cupLocked) break;
		// The keys go to the chooser, not to the field behind it.
		if (g_s.focus == kCupNameField || g_s.focus == kCupDescriptionField || g_s.focus == kCupTimeField) g_s.focus = kNoField;
		OpenMapChooser(u, static_cast<int>(value));
		break;
	case kSubChooserTab:
		u.chooserTab = static_cast<int>(value);
		u.chooserScroll = 0;
		break;
	case kSubChooserRow:
		if (u.chooserTab == 0) {
			const std::vector<std::string> types = LeagueMapTypes();
			if (value < 0 || value >= static_cast<int64_t>(types.size())) break;
			// The size goes with the new type when it has it; medium otherwise, which every type has.
			const TourneyMap before = ChooserMap(u);
			const std::vector<std::string> sizes = CupSizesOf(u.cup, types[static_cast<size_t>(value)]);
			u.chooserType = static_cast<int>(value);
			u.chooserSize = 0;
			for (size_t i = 0; i < sizes.size(); i++) {
				if (sizes[i] == "medium") u.chooserSize = static_cast<int>(i);
			}
			for (size_t i = 0; i < sizes.size(); i++) {
				if (sizes[i] == before.size) u.chooserSize = static_cast<int>(i);
			}
		}
		else {
			const CupScenariosView v = CupScenarios();
			if (value < 0 || value >= static_cast<int64_t>(v.list.size())) break;
			std::string file = v.list[static_cast<size_t>(value)].file;
			for (char &ch : file) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
			u.chooserFile = file;
		}
		break;
	case kSubChooserSize: u.chooserSize = static_cast<int>(value); break;
	case kSubChooserOk: {
		const TourneyMap map = ChooserMap(u);
		if (!map.Known() || u.chooserRound < 0) break;
		if (u.chooserPool) {
			std::vector<TourneyMap> &pool = u.cup.seriesPool;
			const int i = u.chooserRound;
			if (i > static_cast<int>(pool.size()) || i >= kTourneyPoolMax) break;
			bool repeated = false;
			for (size_t k = 0; k < pool.size(); k++) repeated = repeated || (static_cast<int>(k) != i && TourneySameMap(pool[k], map));
			if (repeated) {
				Toast("That map is already among the series' maps.", true);
				break;
			}
			if (i == static_cast<int>(pool.size())) pool.push_back(map);
			else pool[static_cast<size_t>(i)] = map;
		}
		else if (u.chooserRound < static_cast<int>(u.cup.maps.size())) u.cup.maps[static_cast<size_t>(u.chooserRound)] = map;
		else break;
		u.chooserRound = -1;
		u.chooserPool = false;
		u.cupError.clear();
		break;
	}
	case kSubChooserCancel:
		u.chooserRound = -1;
		u.chooserPool = false;
		break;
	case kSubPoolOpen:
		if (u.cupLocked) break;
		if (g_s.focus == kCupNameField || g_s.focus == kCupDescriptionField || g_s.focus == kCupTimeField) g_s.focus = kNoField;
		OpenPoolChooser(u, static_cast<int>(value));
		break;
	case kSubPoolRemove:
		if (u.cupLocked || value >= static_cast<int64_t>(u.cup.seriesPool.size())) break;
		u.cup.seriesPool.erase(u.cup.seriesPool.begin() + static_cast<std::ptrdiff_t>(value));
		u.cupError.clear();
		break;
	case kSubMapsRetry: {
		const auto page = LeagueTourneyPage(u.slug, 60000);
		if (page->loaded && page->detail.id == value) LeagueCupMapsRetry(CupAllMaps(page->detail));
		break;
	}
	case kSubCupSubmit: {
		// Only from the form: a late Enter after it closed would publish the cup again, or an edited one as a new cup.
		if (u.screen != kScreenCreate) break;
		const bool editing = u.cupEditId != 0;
		if (LeagueTourneyBusy(editing ? TourneyOp::Edit : TourneyOp::Create, editing ? u.cupEditId : 0)) break;
		if (!editing) OwnCupContent(u.cup.contentKey, u.cup.contentName);
		std::vector<TourneyCupProblem> problems = TourneyCheckCup(u.cup, NowMs(), LeagueTourneyMapTypes());
		if (u.cupLocked) {
			problems.erase(std::remove_if(problems.begin(), problems.end(), [](TourneyCupProblem q) { return q != TourneyCupProblem::DescriptionLong; }), problems.end());
		}
		if (!problems.empty()) {
			Toast("Some of the cup's details need fixing: they are marked in red.", true);
			break;
		}
		u.cupError.clear();
		u.cupErrorSlug.clear();
		int uploads = 0;
		for (const TourneyMap &m : u.cup.maps) uploads += m.kind == TourneyMapKind::Upload ? 1 : 0;
		if (editing) {
			LogI("league", "changing the cup %lld%s", static_cast<long long>(u.cupEditId), u.cupLocked ? " (only its description)" : "");
			LeagueTourneyEdit(u.cupEditId, u.cup, u.cupLocked);
		}
		else {
			LogI("league", "making a %s cup of %d places, %s, with %d scenarios to upload", TourneyModeId(u.cup.mode), u.cup.maxEntries, u.cup.isPrivate ? "private" : "open", uploads);
			LeagueTourneyCreate(u.cup);
		}
		break;
	}
	case kSubCupOpenActive: OpenPage(u.cupErrorSlug, 0); break;
	case kSubJoin:
		u.joinOpen = true;
		u.joinCode.clear();
		u.joinError.clear();
		FocusField(kCupCodeField);
		break;
	case kSubJoinSend: TourneyFieldEnter(kCupCodeField); break;
	case kSubJoinCancel:
		u.joinOpen = false;
		u.joinError.clear();
		g_s.focus = kNoField;
		break;
	case kSubCopyCode: {
		const std::string code = !u.shownCode.empty() ? u.shownCode : LeagueTourneyCodeOf(u.slug);
		if (code.empty()) break;
		ClipboardSet(code);
		Toast(TrFmt("Code %s copied: paste it to your players.", {code}));
		break;
	}
	case kSubCodeClose: u.shownCode.clear(); break;
	case kSubCancelCup: {
		const int64_t tournament = value;
		Confirm(Tr("Cancel the cup"), Tr("The cup ends here for everyone and leaves the list. This cannot be undone. Cancel it?"), Tr("Cancel the cup"),
			[tournament] { LeagueTourneyCancel(tournament); });
		break;
	}
	case kSubCheckinNow: {
		const int64_t tournament = value;
		Confirm(Tr("Open the check-in now"), Tr("The start moves forward to the end of the check-in. Whoever does not check in in time loses the place. Open it?"),
			Tr("Open the check-in"), [tournament] { LeagueTourneyCheckinNow(tournament); });
		break;
	}
	case kSubStartNow: {
		const int64_t tournament = value;
		Confirm(Tr("Start now"), Tr("The bracket is drawn with the players checked in; whoever has not checked in is out. Start the cup?"), Tr("Start"),
			[tournament] { LeagueTourneyStartNow(tournament); });
		break;
	}
	case kSubRemove: {
		const auto page = LeagueTourneyPage(u.slug, 60000);
		const TourneyEntrant *e = page->loaded ? TourneyEntrantOf(page->detail, value) : nullptr;
		if (!e) break;
		const int64_t tournament = page->detail.id, entry = value;
		Confirm(Tr(page->detail.Teams() ? "Take a team off" : "Take a player off"),
			TrFmt(page->detail.Teams() ?"The team %s will be out of your cup, both its players. Take it off?" : "%s will be out of your cup. Take them off?", {e->name}), Tr("Take off"),
			[tournament, entry] { LeagueTourneyRemoveEntry(tournament, entry); });
		break;
	}
	case kSubGameA:
	case kSubGameB:
	case kSubReplay: {
		const auto page = LeagueTourneyPage(u.slug, 60000);
		const TourneyMatch *m = MatchById(page->detail, value);
		if (!m) break;
		const int64_t match = value;
		if (sub == kSubReplay) {
			Confirm(Tr("Play the game again"), TrFmt("The game of %s against %s does not count and is played again. Go ahead?", {m->a.name, m->b.name}), Tr("Play it again"),
				[match] { LeagueTourneyResolve(match, TourneyVerdict::Replay); });
			break;
		}
		const std::string winner = sub == kSubGameA ? m->a.name : m->b.name;
		const TourneyVerdict verdict = sub == kSubGameA ? TourneyVerdict::GameToA : TourneyVerdict::GameToB;
		Confirm(Tr("Settle the dispute"), TrFmt("The disputed game goes to %s. Go ahead?", {winner}), TrFmt("To %s", {winner}),
			[match, verdict] { LeagueTourneyResolve(match, verdict); });
		break;
	}
	case kSubWalkover: u.walkoverMatch = value; break;
	case kSubWalkoverA:
	case kSubWalkoverB:
	case kSubWalkoverNobody:
		if (u.walkoverMatch) {
			LeagueTourneyResolve(u.walkoverMatch, sub == kSubWalkoverA ? TourneyVerdict::WalkoverToA : sub == kSubWalkoverB ? TourneyVerdict::WalkoverToB : TourneyVerdict::WalkoverNobody);
		}
		u.walkoverMatch = 0;
		break;
	case kSubWalkoverCancel: u.walkoverMatch = 0; break;
	default: break;
	}
}

bool TourneyBack()
{
	Ui &u = g_tourney;
	if (u.colorOpen) {
		u.colorOpen = u.colorWheel = false;
		return true;
	}
	if (u.disputeMatch) {
		u.disputeMatch = 0;
		g_s.focus = kNoField;
		return true;
	}
	if (u.walkoverMatch) {
		u.walkoverMatch = 0;
		return true;
	}
	if (u.teamCup) {
		u.teamCup = 0;
		g_s.focus = kNoField;
		return true;
	}
	if (u.joinOpen) {
		u.joinOpen = false;
		g_s.focus = kNoField;
		return true;
	}
	if (!u.shownCode.empty()) {
		u.shownCode.clear();
		return true;
	}
	if (u.screen == kScreenCreate && u.chooserRound >= 0) {
		u.chooserRound = -1;
		u.chooserPool = false;
		return true;
	}
	if (u.screen == kScreenCreate && u.cupEditId) {
		u.cupEditId = 0;
		OpenPage(u.cupEditSlug, 0);
		return true;
	}
	if (u.screen == kScreenList) return false;
	u.screen = u.screen == kScreenPage || u.screen == kScreenCreate || u.slug.empty() ? kScreenList : kScreenPage;
	return true;
}

bool OverlayActive(const mpui_impl::State &st);

namespace {
// The screen's overlays (a search, a dropdown, the link offer) lie over the tab: its areas are those of the last frame under them.
bool CoveredByScreen()
{
	return OverlayActive(g_s) || LinkOfferShown(g_s) || g_s.ranked.resultOpen || g_s.ranked.ceremonyOpen;
}

// A dialog of the tab over the screen: nothing under it scrolls or pans.
bool TourneyDialogOver(const Ui &u)
{
	return u.disputeMatch || u.joinOpen || !u.shownCode.empty() || u.walkoverMatch || u.teamCup || u.colorOpen ||
		(u.screen == kScreenCreate && u.chooserRound >= 0);
}
}

bool TourneyWheel(int delta, bool shift, bool ctrl)
{
	Ui &u = g_tourney;
	const POINT at = {g_s.mouseX, g_s.mouseY};
	if (CoveredByScreen()) return false;
	// A touchpad sends small deltas: each one still moves a little.
	int step = -delta * 60 / WHEEL_DELTA;
	if (!step && delta) step = delta > 0 ? -1 : 1;
	if (u.teamCup) {
		if (u.teamArea.valid && PtInRect(&u.teamArea.r, at)) u.teamScroll = std::max(0, std::min(u.teamArea.max, u.teamScroll + (delta > 0 ? -1 : 1)));
		return true;
	}
	// A dialog over the screen takes the wheel, so nothing scrolls under it.
	if (u.disputeMatch || u.joinOpen || !u.shownCode.empty() || u.walkoverMatch || u.colorOpen) return true;
	// Ctrl and the wheel over the bracket zoom it about the pointer.
	if (ctrl && u.screen == kScreenPage && u.bracketArea.valid && PtInRect(&u.bracketArea.r, at) && u.pan.contentW > 0) {
		const float scale = std::max(0.1f, g_s.uiScale);
		SetZoom(u, ZoomStep(u.bracketZoom, delta > 0 ? 1 : -1), static_cast<int>((at.x - u.bracketArea.r.left) / scale),
			static_cast<int>((at.y - u.bracketArea.r.top) / scale));
		return true;
	}
	if (u.screen == kScreenCreate) {
		if (u.chooserRound >= 0) {
			if (u.chooserArea.valid && PtInRect(&u.chooserArea.r, at)) u.chooserScroll = std::max(0, std::min(u.chooserArea.max, u.chooserScroll + (delta > 0 ? -1 : 1)));
			return true;
		}
		if (u.summaryArea.valid && PtInRect(&u.summaryArea.r, at)) {
			u.summaryScroll = std::max(0, std::min(u.summaryArea.max, u.summaryScroll + step));
			return true;
		}
		if (u.formArea.valid && PtInRect(&u.formArea.r, at)) {
			u.formScroll = std::max(0, std::min(u.formArea.max, u.formScroll + step * 2));
			return true;
		}
		return false;
	}
	if (u.screen == kScreenMatch) {
		if (u.matchRulesArea.valid && PtInRect(&u.matchRulesArea.r, at)) {
			u.matchRulesScroll = std::max(0, std::min(u.matchRulesArea.max, u.matchRulesScroll + step));
			return true;
		}
		return false;
	}
	if (u.bracketArea.valid && PtInRect(&u.bracketArea.r, at)) {
		if (shift || u.bracketArea.max <= 0) u.bracketX = std::max(0, std::min(u.bracketArea.maxX, u.bracketX + step));
		else u.bracketY = std::max(0, std::min(u.bracketArea.max, u.bracketY + step));
		return true;
	}
	if (u.listArea.valid && PtInRect(&u.listArea.r, at)) {
		u.listScroll = std::max(0, std::min(u.listArea.max, u.listScroll + step));
		return true;
	}
	if (u.sideArea.valid && PtInRect(&u.sideArea.r, at)) {
		u.sideScroll = std::max(0, std::min(u.sideArea.max, u.sideScroll + step));
		return true;
	}
	if (u.pageSideArea.valid && PtInRect(&u.pageSideArea.r, at)) {
		u.pageSideScroll = std::max(0, std::min(u.pageSideArea.max, u.pageSideScroll + step));
		return true;
	}
	return false;
}

bool TourneyPanDown(POINT at, const std::vector<ui::Hit> &hits)
{
	Ui &u = g_tourney;
	if (u.screen != kScreenPage || TourneyDialogOver(u) || CoveredByScreen()) return false;
	// A player of a first round paired by hand: taken, to be dropped onto another.
	if (const ui::Hit *hit = ui::TopHitAt(hits, at); hit && hit->action == kTourney && (hit->arg >> 24) == kSubSeed) {
		u.seedDrag = hit->arg & 0xFFFFFF;
		u.seedFrom = at;
		u.seedMoved = false;
		return true;
	}
	int mode = -1;
	if (u.pan.valid && PtInRect(&u.pan.thumb, at) && !ui::TopHitAt(hits, at)) mode = 1;
	else if (u.bracketArea.valid && PtInRect(&u.bracketArea.r, at) && (u.bracketArea.max > 0 || u.bracketArea.maxX > 0) && !ui::TopHitAt(hits, at)) mode = 0;
	if (mode < 0) return false;
	u.panning = true;
	u.panMode = mode;
	u.panFrom = at;
	u.panStartX = u.bracketX;
	u.panStartY = u.bracketY;
	return true;
}

void TourneyPanMove(POINT at, WPARAM buttons)
{
	Ui &u = g_tourney;
	if (u.seedDrag) {
		if (!(buttons & MK_LBUTTON)) u.seedDrag = 0;
		else if (std::abs(at.x - u.seedFrom.x) > 4 || std::abs(at.y - u.seedFrom.y) > 4) u.seedMoved = true;
		return;
	}
	if (!u.panning) return;
	// The release can happen where the window doesn't hear it.
	if (!(buttons & MK_LBUTTON)) {
		u.panning = false;
		return;
	}
	const float scale = std::max(0.1f, g_s.uiScale);
	const int dx = static_cast<int>((at.x - u.panFrom.x) / scale), dy = static_cast<int>((at.y - u.panFrom.y) / scale);
	if (u.panMode == 1) {
		const int room = std::max(1, u.pan.track - u.pan.thumbLen);
		u.bracketX = std::max(0, std::min(u.bracketArea.maxX, u.panStartX + dx * u.bracketArea.maxX / room));
		return;
	}
	u.bracketX = std::max(0, std::min(u.bracketArea.maxX, u.panStartX - dx));
	u.bracketY = std::max(0, std::min(u.bracketArea.max, u.panStartY - dy));
}

void TourneyPanUp(POINT at, const std::vector<ui::Hit> &hits)
{
	Ui &u = g_tourney;
	u.panning = false;
	if (!u.seedDrag) return;
	const int64_t from = u.seedDrag;
	const bool moved = u.seedMoved;
	u.seedDrag = 0;
	u.seedMoved = false;
	const ui::Hit *hit = ui::TopHitAt(hits, at);
	const int64_t to = hit && hit->action == kTourney && (hit->arg >> 24) == kSubSeed ? (hit->arg & 0xFFFFFF) : 0;
	if (!moved || !to || to == from) return;
	const auto page = LeagueTourneyPage(u.slug, 60000);
	if (page->loaded && TourneyCanArrange(page->detail)) LeagueTourneySwapSeeds(page->detail.id, from, to);
}

bool TourneyScrollTo(int bar, int pos)
{
	Ui &u = g_tourney;
	switch (bar) {
	case kBarList: u.listScroll = std::max(0, std::min(u.listArea.max, pos)); return true;
	case kBarBracketY: u.bracketY = std::max(0, std::min(u.bracketArea.max, pos)); return true;
	case kBarSide: u.sideScroll = std::max(0, std::min(u.sideArea.max, pos)); return true;
	case kBarForm: u.formScroll = std::max(0, std::min(u.formArea.max, pos)); return true;
	case kBarChooser: u.chooserScroll = std::max(0, std::min(u.chooserArea.max, pos)); return true;
	case kBarFriends: u.teamScroll = std::max(0, std::min(u.teamArea.max, pos)); return true;
	case kBarPageSide: u.pageSideScroll = std::max(0, std::min(u.pageSideArea.max, pos)); return true;
	case kBarSummary: u.summaryScroll = std::max(0, std::min(u.summaryArea.max, pos)); return true;
	case kBarMatchRules: u.matchRulesScroll = std::max(0, std::min(u.matchRulesArea.max, pos)); return true;
	default: return false;
	}
}

void TourneyCupTimeFocus() { CupTimeFocus(g_tourney); }

bool TourneyOwnsField(int field)
{
	return field == kTourneyField || field == kCupNameField || field == kCupDescriptionField || field == kCupCodeField || field == kTeamNameField ||
		field == kTeamSearchField || field == kCupTimeField;
}

bool TourneyChooserOpen()
{
	return g_s.view == kViewTournaments && g_tourney.screen == kScreenCreate && g_tourney.chooserRound >= 0;
}

std::string &TourneyFieldText(int field)
{
	Ui &u = g_tourney;
	switch (field) {
	case kCupNameField: return u.cup.name;
	case kCupDescriptionField: return u.cup.description;
	case kCupCodeField: return u.joinCode;
	case kTeamNameField: return u.teamName;
	case kTeamSearchField: return u.teamSearch;
	case kCupTimeField: return u.cupTime;
	default: return u.disputeText;
	}
}

size_t TourneyFieldBytes(int field)
{
	switch (field) {
	case kCupNameField: return 4 * kTourneyCupNameMax;
	case kCupDescriptionField: return 4 * kTourneyCupDescriptionMax;
	case kCupCodeField: return 24;
	case kTeamNameField: return 4 * kTourneyTeamNameMax;
	case kTeamSearchField: return 128;
	case kCupTimeField: return 5;
	default: return 200;
	}
}

void TourneyFieldEdited(int field)
{
	std::string &text = TourneyFieldText(field);
	if (field == kCupCodeField) {
		const std::string tidy = TourneyTidyCode(text).substr(0, kTourneyCodeLength);
		if (tidy != text) {
			text = tidy;
			EditToEnd(text, g_s.edit);
		}
		g_tourney.joinError.clear();
		return;
	}
	if (field == kTeamSearchField) {
		g_tourney.teamScroll = 0;
		return;
	}
	if (field == kCupTimeField) {
		CupTimeTyped(g_tourney, false);
		EditFix(text, g_s.edit);
		return;
	}
	const int most = field == kCupNameField ? kTourneyCupNameMax : field == kCupDescriptionField ? kTourneyCupDescriptionMax : field == kTeamNameField ? kTourneyTeamNameMax : 0;
	if (!most) return;
	while (TourneyTextLength(text) > most) text.erase(static_cast<size_t>(PrevCp(text, static_cast<int>(text.size()))));
	EditFix(text, g_s.edit);
	if (field == kTeamNameField) g_tourney.teamError.clear();
	else g_tourney.cupError.clear();
}

void TourneyFieldEnter(int field)
{
	Ui &u = g_tourney;
	if (field == kCupNameField) {
		FocusField(kCupDescriptionField);
		return;
	}
	if (field == kCupDescriptionField) {
		g_s.focus = kNoField;
		return;
	}
	if (field == kCupTimeField) {
		CupTimeTyped(u, true);
		g_s.focus = kNoField;
		return;
	}
	if (field == kTeamSearchField) {
		// Return picks the only friend left, or moves on to the team's name.
		const std::vector<SteamFriendInfo> friends = TeamFriends(u.teamSearch);
		if (friends.size() == 1) {
			u.teamPartner = friends[0].id;
			u.teamPartnerName = friends[0].name;
		}
		FocusField(kTeamNameField);
		return;
	}
	if (field == kTeamNameField) {
		if (!u.teamCup || LeagueTourneyBusy(TourneyOp::SignUp, u.teamCup)) return;
		if (!u.teamPartner) {
			u.teamError = Tr("Choose your partner among your friends first.");
			return;
		}
		if (u.teamPartner == SteamOwnId()) {
			u.teamError = Tr("Your partner has to be another player.");
			return;
		}
		const int named = TourneyTextLength(TourneyTrim(u.teamName));
		if (named < kTourneyTeamNameMin || named > kTourneyTeamNameMax) {
			u.teamError = TrFmt("Give your team a name of %s to %s characters.", {std::to_string(kTourneyTeamNameMin), std::to_string(kTourneyTeamNameMax)});
			FocusField(kTeamNameField);
			return;
		}
		u.teamError.clear();
		LogI("league", "signing a team up for the cup %lld", static_cast<long long>(u.teamCup));
		LeagueTourneySignUpTeam(u.teamCup, u.teamPartner, u.teamName);
		return;
	}
	if (field == kCupCodeField) {
		const std::string code = TourneyTidyCode(u.joinCode);
		if (!TourneyCodeValid(code)) {
			u.joinError = Tr("A code has 6 letters and digits, without 0, O, 1 or I.");
			FocusField(kCupCodeField);
			return;
		}
		u.joinError.clear();
		LeagueTourneyFindByCode(code);
		return;
	}
	std::string text = u.disputeText;
	while (!text.empty() && text.back() == ' ') text.pop_back();
	if (text.size() < 4) {
		Toast("Say in a few words what went wrong.");
		FocusField(kTourneyField);
		return;
	}
	LeagueTourneyDispute(u.disputeMatch, text);
	u.disputeMatch = 0;
	u.disputeText.clear();
	g_s.focus = kNoField;
}

void TourneyDropPick(int index, int item)
{
	if (index == kCupMenuListKind || index == kCupMenuListFormat) {
		Ui &u = g_tourney;
		if (item < 0 || item >= (index == kCupMenuListKind ? 3 : 4)) return;
		(index == kCupMenuListKind ? u.listKind : u.listFormat) = item;
		u.listScroll = 0;
		return;
	}
	if (index == kCupMenuCiv) {
		Ui &u = g_tourney;
		if (item < 0 || item >= static_cast<int>(u.civChoices.size())) return;
		u.civRace = u.civChoices[static_cast<size_t>(item)];
		GameRoomSaveSetting("TourneyCiv", u.civRace);
		LogI("league", "the civilization for the cup matches is race %d", u.civRace);
		return;
	}
	if (g_tourney.cupLocked) return;
	CupMenuPick(g_tourney.cup, index, item, NowMs());
	g_tourney.cupError.clear();
}

namespace {

// Under g_lock.
bool TeamsLocked(const State &st)
{
	return st.game.active && st.room.id && TourneyMatchOfTag(st.room.info.leagueTag) && g_tourney.lock.room == st.room.id && g_tourney.lock.team > 0;
}

}

bool TourneyRoomApplying(const mpui_impl::State &st)
{
	if (!st.game.host || !st.room.id || !TourneyMatchOfTag(st.room.info.leagueTag)) return false;
	return g_tourney.lock.room == st.room.id && !g_tourney.lock.applied;
}

int TourneyRoomPlayers(const mpui_impl::State &st)
{
	if (!st.room.id || st.room.info.spectate) return 0;
	const int64_t match = TourneyMatchOfTag(st.room.info.leagueTag);
	if (!match) return 0;
	// By the room alone: once the league has settled the match, its room may still be counting down.
	const auto mine = LeagueTourneyMine();
	const TourneyNext &n = mine->mine.next;
	// Against test bots their seats are the game's AI: the room waits for the people only, the player (and their partner).
	if (n.present && n.match == match && TourneyIsTestBot(n.opponent.steamId)) return n.Teams() ? 2 : 1;
	if (n.present && n.match == match) return n.Teams() ? 4 : 2;
	return st.room.info.maxPlayers >= 4 ? 4 : 2;
}

bool TourneyRoomHiddenHeld(const mpui_impl::State &st)
{
	if (!st.game.active || !st.room.id || st.room.info.spectate) return false;
	const int64_t match = TourneyMatchOfTag(st.room.info.leagueTag);
	if (!match) return false;
	const auto mine = LeagueTourneyMine();
	return st.view == kViewTournaments && mine->mine.next.present && mine->mine.next.match == match;
}

bool TourneyRoomHidden(const mpui_impl::State &st)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return TourneyRoomHiddenHeld(st);
}

bool TourneyTeamsLocked(const mpui_impl::State &st)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return TeamsLocked(st);
}

bool TourneyBlocksAction(int action, int arg)
{
	if (!g_s.game.active || !g_s.room.id || !TourneyMatchOfTag(g_s.room.info.leagueTag) || g_tourney.lock.applying) return false;
	// A match of teams: the teams are the league's, and a player gets ready once theirs is set.
	if (TeamsLocked(g_s)) {
		const int kind = arg & kDropKindMask;
		if (action == kGTeam || (action == kGDropOpen && kind == kDropTeam)) {
			Toast("The tournament sets the teams of this room.");
			return true;
		}
		if (action == kGReady && !g_tourney.lock.teamSet && g_tourney.lock.teamTries < kTeamTries && !LocalReady(g_s.game)) {
			Toast("Your team is being set in the room: get ready in a moment.");
			return true;
		}
	}
	bool blocked = false;
	switch (action) {
	case kGRuleToggle:
	case kGSiteToggle:
	case kGOptToggle: blocked = true; break;
	// The room's selector, while the tournament picks its scenario in it.
	case kPickRow:
	case kPickOk:
	case kPickCancel: blocked = !g_tourney.lock.mapAllowed && TourneyGameMap(LeagueTourneyMine()->mine.next).Scenario(); break;
	case kGDropOpen: {
		const int kind = arg & kDropKindMask, index = arg >> kDropKindBits;
		if (kind == kDropOption) {
			const int id = index >= 0 && index < static_cast<int>(g_s.game.options.size()) ? g_s.game.options[index].id : 0;
			blocked = !(g_tourney.lock.mapAllowed && (id == 0x1000 || id == 0x01000008));
		}
		else blocked = kind == kDropPreset || kind == kDropSites || kind == kDropModOption || kind == kDropSpectators || kind == kDropSlot || kind == kDropTeam ||
			kind == kDropSpeed;
		break;
	}
	// A 1v1 has no teams to choose: two in one would be allies.
	case kGTeam: blocked = true; break;
	default: break;
	}
	if (blocked) Toast("The tournament sets the map and the rules of this room.");
	return blocked;
}

bool TourneyLobby(const OnlineLobby &g)
{
	return TourneyMatchOfTag(g.leagueTag) != 0;
}

bool TourneyLobbyWatchOnly(const OnlineLobby &g)
{
	const int64_t match = TourneyMatchOfTag(g.leagueTag);
	if (!match) return false;
	const auto mine = LeagueTourneyMine();
	return !(mine->mine.next.present && mine->mine.next.match == match);
}

// From the game's end to the cup page: the tick that ended the game is set, the page is not open yet. Read by the render thread.
std::atomic<DWORD> g_cupReturnAt{0};

// The game of a cup match ends in the game's main menu: the multiplayer menu opens on the cup's page, where the score, the next match
// (with its own button) or the final result are.
void StepAfterGame(const TourneyNext &next)
{
	Ui &u = g_tourney;
	(void)next;
	// The game ended here: a game against a test bot needs nothing at the next start.
	if (!g_s.inOnlineGame && u.botGameMarked) {
		u.botGameMarked = false;
		GameRoomSaveSetting(kBotGameSetting, 0);
	}
	if (g_s.inOnlineGame) {
		if (!u.afterSlug.empty() && !u.afterGame) {
			u.afterGame = true;
			LogI("league", "a cup game is played: its page (%s) opens when it ends", u.afterSlug.c_str());
		}
		return;
	}
	if (u.afterGame && !g_cupReturnAt.load()) g_cupReturnAt = GetTickCount() | 1;
	if (!u.afterGame) {
		// Out of the room without a game (not while the game is starting): nothing to come back to.
		if (!u.afterSlug.empty() && !g_s.room.id && !g_s.connecting && !g_s.gameClosedAt && !g_s.startingCover) u.afterSlug.clear();
		return;
	}
	// With the menu closed neither its room nor the game's room is followed (MpUiDraw does): the ones left when the game started would
	// still look current.
	if (!g_s.open) g_s.room = SteamCurrentRoom();
	const bool roomActive = g_s.open ? g_s.game.active : GameRoomActive();
	if (roomActive || g_s.room.id || g_s.connecting) return;
	if (!g_s.open && !MainMenuOnTop()) return;
	if (!g_s.open) OpenMpMenu(false);
	g_s.view = kViewTournaments;
	LeagueTourneyMineRefresh();
	OpenPage(u.afterSlug, 0);
	LogI("league", "back from a cup game: its page (%s)", u.afterSlug.c_str());
	u.afterSlug.clear();
	u.afterGame = false;
	g_cupReturnAt = 0;
}

bool CupReturnPending()
{
	const DWORD at = g_cupReturnAt.load();
	if (!at) return false;
	if (GetTickCount() - at < 3000) return true;
	g_cupReturnAt = 0;
	return false;
}

void TourneyGameThreadTick()
{
	const auto mine = LeagueTourneyMine();
	// Once a start: a game against a test bot that closed with the game is given up; the league would only give it to the bot later.
	static bool botGameChecked = false;
	if (!botGameChecked && mine->loaded && LeagueLinked()) {
		botGameChecked = true;
		const int match = GameRoomLoadSetting(kBotGameSetting, 0);
		if (match > 0) {
			GameRoomSaveSetting(kBotGameSetting, 0);
			LogI("league", "tournament match %d: its game against a test bot closed with the game; it is given up", match);
			LeagueTourneyConcede(match);
		}
	}
	std::lock_guard<std::mutex> lock(g_lock);
	TakeNotices();
	TakeResults();
	MatchCiv(g_tourney);
	MatchColor(g_tourney);
	if (!g_s.ready) return;
	StepJoin();
	StepRoom();
	StepInRoom(mine->mine.next);
	StepAfterGame(mine->mine.next);
}
