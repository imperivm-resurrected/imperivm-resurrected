// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
#include "core/config.h"
#include "core/guard.h"
#include "core/league.h"
#include "core/leagueseasons.h"
#include "core/leaguemaps.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"
#include "core/leagueroomoptions.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "hooks/dmshim.h"
#include "i18n/tr.h"
#include "mods/modoptions.h"
#include "mods/mods.h"
#include "net/leagueclient.h"
#include "net/steamnet.h"
#include "ui/leaguemapthumb.h"
#include "ui/leaguemodsprep.h"
#include "ui/leagueui.h"
#include "ui/modui.h"
#include "ui/profileui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_league_room.h"
#include "ui/mpui_ranked_kit.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

const int kOptMapType = 0x1000, kOptMapSize = 0x01000008;

// What the game thread does for a ranked match: the room it made or tried to join, and when.
struct RoomAuto {
	bool init = false;
	DWORD lastRead = 0;
	unsigned foundSerial = 0;
	int64_t match = 0;
	DWORD createdAt = 0, joinAt = 0, kickAt = 0, settingsAt = 0, teamAt = 0;
	int createTries = 0, joinTries = 0;
	uint64_t joinLobby = 0, invitedLobby = 0;
	bool warned = false, settingsLogged = false, mapWarned = false, textWarned = false;
	// The room's retries gave up and the match was called off; the called-off match whose room this computer left.
	bool abandoned = false;
	int64_t leftCalledOff = 0;
	// The match whose room the player left by hand: no room is made or joined for it again until they ask (Go to the room).
	int64_t userLeft = 0;
	std::map<int, DWORD> setAt;
};
RoomAuto g_auto;

bool InGame()
{
	return *reinterpret_cast<void **>(addr::kGame) != nullptr;
}

// Lower-case letters and digits only, the accents of Spanish and Italian taken off and everything else a single space.
std::string Plain(const std::string &s)
{
	static const std::pair<const char *, char> kAccents[] = {{"\xC3\xA1", 'a'}, {"\xC3\xA9", 'e'}, {"\xC3\xAD", 'i'}, {"\xC3\xB3", 'o'}, {"\xC3\xBA", 'u'},
		{"\xC3\xB1", 'n'}, {"\xC3\xA0", 'a'}, {"\xC3\xA8", 'e'}, {"\xC3\xAC", 'i'}, {"\xC3\xB2", 'o'}, {"\xC3\xB9", 'u'}};
	std::string out;
	for (size_t i = 0; i < s.size();) {
		bool done = false;
		for (const auto &a : kAccents) {
			if (s.compare(i, 2, a.first) == 0) {
				out += a.second;
				i += 2;
				done = true;
				break;
			}
		}
		if (done) continue;
		const unsigned char c = static_cast<unsigned char>(s[i++]);
		if (isalnum(c)) out += static_cast<char>(tolower(c));
		else if (!out.empty() && out.back() != ' ') out += ' ';
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

// Under g_lock, on the game thread: one change of the room at most every 3 s per option, so the host never fights the game.
bool Due(int id)
{
	const DWORD now = GetTickCount();
	auto it = g_auto.setAt.find(id);
	if (it != g_auto.setAt.end() && now - it->second < 3000) return false;
	g_auto.setAt[id] = now;
	return true;
}

// The ticket's map type or size, by the game's id behind each item (core/leaguemaps.h); by the item's text only when the ids can't
// be read, which is logged. A size the chosen type does not offer waits until the type is right.
void SetMapItem(const GameRoomSnapshot &g, int id, const std::string &want)
{
	for (const GameRoomOption &o : g.options) {
		if (o.id != id || o.checkbox) continue;
		bool byText = false;
		const int k = LeagueMapItemFor(o.keys, o.items, o.itemDisabled, want, byText);
		if (k < 0) {
			if (id == kOptMapType && !g_auto.mapWarned) {
				g_auto.mapWarned = true;
				std::string items;
				for (size_t i = 0; i < o.items.size(); i++) items += (i ? ", " : "") + o.items[i] + (i < o.keys.size() && !o.keys[i].empty() ? "=" + o.keys[i] : "");
				LogW("league", "ranked: the season's map \"%s\" is not among the room's map types (%s)", want.c_str(), items.c_str());
			}
			return;
		}
		if (byText && !g_auto.textWarned) {
			g_auto.textWarned = true;
			LogW("league", "ranked: no map id behind the room's items; \"%s\" picked by its name (\"%s\")", want.c_str(), o.items[k].c_str());
		}
		if (o.sel != k && Due(id)) {
			GameRoomSetOption(id, k);
			Predict(kPredOptSel, id, k);
		}
		return;
	}
}

// The host keeps the room as the server's ticket says: map and size, and every other option from the season's settings and the
// league's defaults (core/leagueroomoptions.h), whatever the host's last room had. Put back every second, until the game starts.
void ApplySettings(const LeagueMatchTicket &t)
{
	GameRoomSnapshot g = GameRoomGet();
	if (!g.active || !g.host || !g.online || g.busy) return;
	LeagueRoomApply(LeagueRoomOptionsFor(LeagueRoomRequestOf(t.settings)), g, g_s.room.id, "ranked");
	// The type first: the sizes the game offers depend on it.
	if (!t.map.name.empty()) SetMapItem(g, kOptMapType, t.map.name);
	if (!t.map.size.empty()) SetMapItem(g, kOptMapSize, t.map.size);
	if (!g_auto.settingsLogged) {
		g_auto.settingsLogged = true;
		LogI("league", "ranked: the room takes the league's options (map %s %s, preset %s)", t.map.name.c_str(), t.map.size.c_str(),
			t.settings.preset.empty() ? "competitive" : t.settings.preset.c_str());
	}
}

// The ticket is of a 2v2: its queue, or its two sides.
bool TeamTicket(const LeagueMatchTicket &t)
{
	return LeagueQueueIsTeam(t.queue) || !t.teams.empty();
}

// This player's side in the ticket of a 2v2 (1 or 2); 0 when it does not say.
int MySide(const LeagueRankedView &v)
{
	if (v.ticket.mySide) return v.ticket.mySide;
	return LeagueSideOf(LeagueTicketTeams(v.ticket), std::to_string(SteamOwnId()));
}

// The host's Steam room for a ranked match: private (entered by invitation or by its id), two players (four in a 2v2, whose "mode" tells
// every computer what the game is), the league's tag, and spectators with the season's delay in a room anyone can find.
void CreateRoom(const LeagueRankedView &v)
{
	OnlineCreateSettings s;
	const std::string me = !v.me.name.empty() ? v.me.name : SteamOnlinePlayerName();
	const bool team = TeamTicket(v.ticket);
	if (team) {
		const LeaguePlayerCard *partner = LeagueTeamPartner(LeagueTicketTeams(v.ticket), std::to_string(SteamOwnId()));
		s.name = TrFmt("Ranked 2 vs 2: %s and %s", {me, partner && !partner->name.empty() ? partner->name : Tr("your partner")});
	}
	else {
		const std::string other = !v.opponent.name.empty() ? v.opponent.name : Tr("your opponent");
		s.name = TrFmt("Ranked: %s vs %s", {me, other});
	}
	s.map = !v.ticket.map.name.empty() ? v.ticket.map.name : std::string("Chosen in the room");
	s.mode = team ? kLeagueTeamRoomMode : "Ranked 1v1";
	s.maxPlayers = team ? 4 : 2;
	s.visibility = 2;
	const int delay = v.ticket.settings.specDelay;
	s.spectators = delay != 0;
	s.specDelay = delay > 0 ? delay : 120;
	s.specVisibility = 0;
	s.leagueTag = LeagueRankedRoomTag(v.matchId);
	g_s.hostSave = SaveEntry();
	g_s.hostSavePublished = false;
	g_s.savePlayersRead = false;
	g_s.savePlayers.clear();
	g_s.savePlaced.clear();
	g_s.createSave = -1;
	g_s.createSpec = !s.spectators ? 0 : s.specDelay <= 30 ? 1 : s.specDelay <= 60 ? 2 : 3;
	SteamCreateLobby(s);
	g_s.roomLimit = s.maxPlayers;
	g_s.autoHost = true;
	g_s.connecting = true;
	g_s.connectStart = GetTickCount();
	LogI("league", "ranked: creating the room of match %lld", static_cast<long long>(v.matchId));
}

void SetOwnTeam(const LeagueRankedView &v)
{
	const int side = MySide(v);
	const GameRoomSnapshot g = GameRoomGet();
	if (!g.active || !g.online || g.busy || side <= 0 || side >= g.teamChoices || g.localSlot < 0 || g.localSlot > 7) return;
	const GameRoomSlot &slot = g.slots[g.localSlot];
	if (slot.team == side) return;
	if (LocalReady(g)) {
		if (Due(-3)) GameRoomToggleReady();
		return;
	}
	if (!Due(-4)) return;
	GameRoomSetTeam(g.localSlot, side);
	Predict(kPredTeam, g.localSlot, side);
	LogI("league", "ranked 2v2: this player's slot goes to team %d", side);
}

bool InTicket(const LeagueMatchTicket &t, uint64_t id)
{
	const std::string text = std::to_string(id);
	for (const LeaguePlayerCard &p : t.players) {
		if (p.steamId == text) return true;
	}
	return false;
}

void ResetRoom()
{
	const unsigned serial = g_auto.foundSerial;
	const bool init = g_auto.init;
	const int64_t left = g_auto.leftCalledOff;
	g_auto = RoomAuto();
	g_auto.foundSerial = serial;
	g_auto.init = init;
	g_auto.leftCalledOff = left;
}

bool OwnsRoom()
{
	for (const OnlineMember &m : g_s.room.members) {
		if (m.me && m.owner) return true;
	}
	return false;
}

void OpenRankedTab()
{
	if (!g_s.open) OpenMpMenu(false);
	g_s.view = kViewRanked;
	g_s.ranked.sub = kRankedHub;
}

const int kCreateTries = 3, kJoinTries = 6;
const DWORD kCreateWaitMs = 30000, kJoinWaitMs = 15000;

// The room's own retries gave up: the match is called off, once (the server decides who, if anyone, is at fault).
bool GiveUpRoom(const LeagueRankedView &v)
{
	if (g_auto.abandoned || v.abandoning || !LeagueTicketAbandonable(v.ticket)) return false;
	g_auto.abandoned = true;
	LeagueMatchAbandon(true);
	return true;
}

// Both accepted: the host makes the room, the other one joins it as soon as the server knows it. When that fails every time,
// the match is called off.
void RoomStep(const LeagueRankedView &v, DWORD now)
{
	const bool live = (v.phase == LeagueRankedPhase::Ready || v.phase == LeagueRankedPhase::Playing) && v.ticketLoaded && v.matchId;
	if (!live) {
		if (g_auto.match) ResetRoom();
		// A match called off while this computer waited in its room: the room is of no use any more.
		if (v.calledOff && v.calledOff != g_auto.leftCalledOff && g_s.open && !InGame() && g_s.room.id && g_s.room.info.leagueTag == LeagueRankedRoomTag(v.calledOff)) {
			g_auto.leftCalledOff = v.calledOff;
			LogI("league", "ranked: match %lld was called off; leaving its room", static_cast<long long>(v.calledOff));
			ModUiToast("The ranked match was called off, so you left its room.");
			DoAction(kLeave, 0);
		}
		return;
	}
	if (g_auto.match != v.matchId) {
		ResetRoom();
		g_auto.match = v.matchId;
	}
	if (InGame()) return;
	const std::string tag = LeagueRankedRoomTag(v.matchId);
	// The guest may be in the room before its data says what it is for.
	if (g_s.room.id && (g_s.room.info.leagueTag == tag || g_s.room.id == LeagueTicketLobby(v.ticket))) {
		LeagueRankedInRoom();
		if (TeamTicket(v.ticket) && now - g_auto.teamAt > 1000) {
			g_auto.teamAt = now;
			SetOwnTeam(v);
		}
		if (GameRoomActive()) GameRoomKeepPlayerPrefs();
		if (!v.host || !OwnsRoom()) return;
		LeagueRankedRoomOpened(g_s.room.id);
		if (g_auto.invitedLobby != g_s.room.id) {
			g_auto.invitedLobby = g_s.room.id;
			// Every other player of the ticket: in a 2v2 the partner and both opponents.
			const std::string own = std::to_string(SteamOwnId());
			for (const LeaguePlayerCard &pl : v.ticket.players) {
				if (pl.steamId != own) SteamInviteToRoom(_strtoui64(pl.steamId.c_str(), nullptr, 10));
			}
		}
		if (now - g_auto.kickAt > 1000) {
			g_auto.kickAt = now;
			for (const OnlineMember &m : g_s.room.members) {
				if (m.me || InTicket(v.ticket, m.id)) continue;
				LogW("league", "ranked: %s is not a player of the match; out of the room", m.name.c_str());
				SteamKick(m.id);
			}
		}
		if (now - g_auto.settingsAt > 1000) {
			g_auto.settingsAt = now;
			ApplySettings(v.ticket);
		}
		return;
	}
	if (g_auto.userLeft == v.matchId) return;
	if (g_s.room.id || GameRoomActive()) {
		if (!g_auto.warned) {
			g_auto.warned = true;
			ModUiToast("Your ranked match is ready: leave this room to play it.");
		}
		return;
	}
	if (g_s.connecting || !SteamOnlineReady()) return;
	if (!g_s.open) {
		if (!MainMenuOnTop()) return;
		OpenRankedTab();
	}
	if (v.host) {
		if (g_auto.createdAt && now - g_auto.createdAt < kCreateWaitMs) return;
		if (g_auto.createTries >= kCreateTries) {
			if (GiveUpRoom(v)) {
				LogW("league", "ranked: Steam could not create the room after %d tries; calling the match off", g_auto.createTries);
				ModUiToast("The room of your ranked match could not be made: the match is called off.", true);
			}
			return;
		}
		g_auto.createdAt = now;
		g_auto.createTries++;
		CreateRoom(v);
		return;
	}
	const uint64_t lobby = LeagueTicketLobby(v.ticket);
	if (lobby && g_auto.joinTries >= kJoinTries && now - g_auto.joinAt >= kJoinWaitMs) {
		if (GiveUpRoom(v)) {
			LogW("league", "ranked: could not enter the host's room after %d tries; calling the match off", g_auto.joinTries);
			ModUiToast("Could not enter the host's room: the ranked match is called off.", true);
		}
		return;
	}
	if (!lobby || g_auto.joinTries >= kJoinTries || (g_auto.joinLobby == lobby && now - g_auto.joinAt < kJoinWaitMs)) return;
	g_auto.joinLobby = lobby;
	g_auto.joinAt = now;
	g_auto.joinTries++;
	// As a join from Discord: the screen enters the host's game by itself once the room is joined.
	LogI("room", "ranked: joining the host's room %llu", lobby);
	SteamJoinLobby(lobby, "");
}

// A match was found: the taskbar flashes and a sound plays even when the game is in the background, and the menu shows the answer.
void CallPlayer()
{
	// This runs under the screen's lock, and flashing the window sends it messages at once (WM_NCACTIVATE) that come back into
	// the screen's handlers on this thread: the flash goes from another thread, so they wait for the lock.
	if (const HWND wnd = g_wakeWnd.load()) {
		SafeThread([](HWND w) {
			FLASHWINFO fi = {sizeof(fi), w, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
			FlashWindowEx(&fi);
		}, wnd).detach();
	}
	if (GetConfig().matchFoundSound) UiSoundPlay(1);
	if (!g_s.open && !InGame() && MainMenuOnTop()) OpenRankedTab();
	else if (!g_s.open) ModUiToast("Ranked match found! Open Multiplayer to accept it.");
}

// The record the server sent of the same player elsewhere (the ticket's opponent, the found match), when this card has none.
void TakeStanding(LeaguePlayerCard &into, const LeaguePlayerCard &from)
{
	if (into.standing || !from.standing || (!into.steamId.empty() && !from.steamId.empty() && into.steamId != from.steamId)) return;
	into.standing = true;
	into.wins = from.wins;
	into.losses = from.losses;
	into.glory = from.glory;
	into.place = from.place;
	if (!into.streakKnown && from.streakKnown) {
		into.streakKnown = true;
		into.streak = from.streak;
	}
}

// One side of the match found: avatar, name, record, emblem, rank, Glory and place, and whether they accepted.
void PaintSide(Painter &p, int x, int y, int w, int h, uint64_t id, const std::string &name, const LeaguePlayerCard &card, bool you, bool accepted)
{
	const LeagueTier tier = card.tier;
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::hairline);
	const int av = 72;
	Avatar(p, x + 24, y + 24, av, id, name);
	p.Frame(x + 24, y + 24, av, av, you ? pal::red : pal::edge);
	SelectObject(p.dc, BrandFont(BrandFace::Body, 22, p.s));
	p.raw = true;
	const int nameW = std::min(w - 136 - (you ? 56 : 0), p.TextWidth(name) + 4);
	p.Text(x + 112, y + 30, nameW, 30, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	if (you) Chip(p, x + 112 + nameW + 10, y + 35, 40, 20, "You", pal::textDim, pal::hairline);
	p.Font(3);
	if (card.standing) {
		p.raw = true;
		p.Text(x + 112, y + 64, w - 136, 22, RecordText(card), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	else p.Text(x + 112, y + 64, w - 136, 22, tier == LeagueTier::None ? "Placement games" : "Ranked 1 vs 1", pal::caption);
	DrawLeagueEmblem(p, x + 20, y + 108, 80, tier);
	SelectObject(p.dc, BrandFont(BrandFace::Display, 36, p.s));
	p.raw = true;
	p.TextCaps(x + 112, y + 114, w - 136, 38, RankOrPlacement(tier, card.division), Ink(tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	// Glory and place, once the player has a rank: a player in placement has neither yet.
	if (card.standing && tier != LeagueTier::None) {
		p.Font(0);
		RawText(p, x + 112, y + 154, w - 136, 24, StandingText(card), pal::body);
	}
	p.Fill(x + 24, y + h - 80, w - 48, 1, pal::hairline);
	const int cw = accepted ? 150 : 110;
	Chip(p, x + w - 24 - cw, y + h - 58, cw, 28, accepted ? "Accepted" : "Pending", accepted ? pal::ok : pal::textDim,
		accepted ? RGB(0x2a, 0x36, 0x24) : pal::hairline);
}

// A label over a value, as the room's summary shows its options.
void Fact(Painter &p, int x, int y, int w, const std::string &label, const std::string &value)
{
	p.Font(4);
	p.TextCaps(x, y, w, 16, label, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	p.Font(0);
	p.Text(x, y + 20, w, 26, value, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

}

std::string RankedFoundCaption(const LeagueRankedView &v, const char *queue)
{
	std::string caption = Tr(queue);
	if (!v.seasonLoaded) return caption;
	caption += "  \xC2\xB7  " + TrFmt("Season %s", {LeagueRoman(v.season.number)}) + "  \xC2\xB7  " + LeagueSeasonName(v.season, v.lang);
	// A season with its own content was searched with it mounted: the queue checks the key.
	if (!v.season.contentKey.empty()) caption += "  \xC2\xB7  " + ModsProfileLabel(ModsMountedProfile().name);
	return caption;
}

void RankedRoomLeftByUser()
{
	if (!g_auto.match) return;
	g_auto.userLeft = g_auto.match;
	LogI("league", "ranked: the player left the room of match %lld; it is not made again until they ask", static_cast<long long>(g_auto.match));
	ModUiToast("You left your ranked match's room. Go back to it, or cancel the match, from Ranked.");
}

bool RankedRoomLocked(const State &st)
{
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t id = 0;
	return st.room.id && LeagueParseRoomTag(st.room.info.leagueTag, kind, id) && kind == LeagueMatchKind::Ranked;
}

bool RankedRoomAction(int action, int arg)
{
	if (!RankedRoomLocked(g_s)) return false;
	switch (action) {
	case kGOptToggle:
	case kGSiteToggle:
	case kGRuleToggle:
	case kGTeam:
	case kGKick:
	case kGKickJoining:
	case kGSeat:
	case kGMove:
	case kGMoveTo:
	case kGSwapAsk:
	case kGSlotType:
	case kInvite:
	case kFieldRoomName: return true;
	case kGDropOpen: {
		const int kind = arg & kDropKindMask;
		return kind == kDropOption || kind == kDropPreset || kind == kDropSites || kind == kDropModOption || kind == kDropSpectators ||
			kind == kDropSlot || kind == kDropTeam || kind == kDropAi;
	}
	default: return false;
	}
}

namespace {

void OpenDispute(int64_t matchId)
{
	RankedScreen &r = g_s.ranked;
	if (matchId <= 0 || r.view.disputeBusy) return;
	r.disputeMatch = matchId;
	r.disputeText.clear();
	FocusField(kRankedField);
}

}

std::string &RankedFieldText()
{
	// The field is the report's or the friend's to invite while their dialog is open, and the leaderboard's search otherwise.
	if (g_s.ranked.inviteOpen) return g_s.ranked.inviteSearch;
	return g_s.ranked.disputeMatch ? g_s.ranked.disputeText : g_s.ranked.boardSearch;
}

void RankedFieldEnter()
{
	RankedScreen &r = g_s.ranked;
	if (r.inviteOpen) {
		r.invitePage = 0;
		g_s.focus = kNoField;
		return;
	}
	if (!r.disputeMatch) {
		RankedBoardAsk(0, false);
		g_s.focus = kNoField;
		return;
	}
	std::string text = r.disputeText;
	while (!text.empty() && text.back() == ' ') text.pop_back();
	while (!text.empty() && text.front() == ' ') text.erase(text.begin());
	if (text.size() < 4) {
		ModUiToast("Say in a few words what went wrong.");
		FocusField(kRankedField);
		return;
	}
	LeagueMatchDispute(r.disputeMatch, text);
	r.disputeMatch = 0;
	r.disputeText.clear();
	g_s.focus = kNoField;
}

void PaintRankedDispute(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 620, 290, "Report a problem", pal::redText);
	DialogText(p, d, 64, "Tell the league in a few words what went wrong with this game. The result stays as it is; the team reviews the game.",
		pal::textDim, 3);
	PaintEditField(p, d.BodyX(), d.y + 136, d.BodyW(), 40, st.ranked.disputeText, Tr("What happened?"), st.focus == kRankedField, false, st.edit,
		kFieldRanked);
	DialogButtons(p, d, {{"Send", kRankedAct, kRkDisputeSend, true, !st.ranked.view.disputeBusy}, {"Cancel", kRankedAct, kRkDisputeCancel}}, mx, my);
}

void PaintRankedContent(Painter &p, const State &st)
{
	const RankedScreen &r = st.ranked;
	const std::string label = r.contentProfile.empty() ? std::string() : ModsProfileLabel(r.contentProfile);
	const std::string name = !r.contentName.empty() ? r.contentName : label;
	std::string text = Tr("This season is played with other gameplay content. Change your content profile to search.");
	if (!name.empty() && r.contentSwitchable) {
		text = TrFmt("This season is played with \"%s\". Your content profile \"%s\" has it: switch to it and the search starts.", {name, label});
	}
	else if (!name.empty() && !label.empty()) {
		text = TrFmt("This season is played with \"%s\". Activate the content profile \"%s\" in Content and restart the game to search.", {name, label});
	}
	else if (!name.empty()) text = TrFmt("This season is played with \"%s\". Install it, activate it in Content and restart the game to search.", {name});
	p.Font(0);
	p.raw = true;
	const int textH = p.Measure(620 - 48, text);
	p.raw = false;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 620, std::max(220, 60 + 16 + textH + 24 + kDialogBtnH + 16), "The season's content", pal::redText);
	p.raw = true;
	DialogText(p, d, 76, text, pal::text, 0);
	p.raw = false;
	if (r.contentSwitchable) {
		DialogButtons(p, d, {{"Switch to the season's content", kRankedAct, kRkContentSwitch, true}, {"Close", kRankedAct, kRkContentClose}}, st.mouseX, st.mouseY);
	}
	else DialogButtons(p, d, {{"Close", kRankedAct, kRkContentClose, true}}, st.mouseX, st.mouseY);
}

namespace {

// The public profile of a player of the leaderboard, with the name the leaderboard has while the league answers.
void OpenProfile(const RankedScreen &r, const std::string &steamId)
{
	const uint64_t id = _strtoui64(steamId.c_str(), nullptr, 10);
	if (!id) return;
	const LeagueBoardRow *row = LeagueBoardFind(r.view.page, steamId);
	if (!row) row = LeagueBoardFind(r.view.around, steamId);
	if (!row) row = LeagueBoardFind(r.view.top, steamId);
	ProfileUiOpenPublic(id, row ? row->name : std::string());
}

void RankedProfileAction(int arg)
{
	const RankedScreen &r = g_s.ranked;
	const std::string own = std::to_string(SteamOwnId());
	if (arg == kRkProfile) OpenProfile(r, r.boardPick.empty() ? own : r.boardPick);
	else if (arg == kRkProfileMe) OpenProfile(r, own);
	else if (arg >= kRkBadgeProfile) ProfileUiOpenPublic(LeagueBadgeClickedId(arg), std::string());
	else if (arg >= kRkProfileTop) {
		const size_t i = static_cast<size_t>(arg - kRkProfileTop);
		if (i < r.view.top.rows.size()) OpenProfile(r, r.view.top.rows[i].steamId);
	}
}

}

void RankedAction(int arg)
{
	RankedScreen &r = g_s.ranked;
	if (arg >= kRkProfile && arg < kRkProfileArgEnd) {
		RankedProfileAction(arg);
		return;
	}
	if (arg >= kRkDetailOpen && arg < kRkDetailArgEnd) {
		if (arg == kRkDetailClose) r.detailMatch = 0;
		else if (arg >= kRkDetailMetric) r.detailMetric = arg - kRkDetailMetric;
		else {
			const size_t i = static_cast<size_t>(arg - kRkDetailOpen);
			if (i < r.view.history.games.size()) {
				r.detailMatch = r.view.history.games[i].id;
				r.detailMetric = 0;
				LeagueMatchDetailRequest(r.detailMatch);
			}
		}
		return;
	}
	if (arg >= kRkBadgeEnd && arg < kRkTeamArgEnd) {
		RankedTeamAction(arg);
		return;
	}
	if (arg >= kRkArgEnd) {
		RankedSeasonAction(arg);
		return;
	}
	if (arg >= kRkDisputeHistory && arg < kRkArgEnd) {
		const size_t i = static_cast<size_t>(arg - kRkDisputeHistory);
		if (i < r.view.history.games.size()) OpenDispute(r.view.history.games[i].id);
		return;
	}
	if (arg >= kRkRow && arg < kRkDisputeHistory) {
		const size_t row = static_cast<size_t>(arg - kRkRow);
		if (row < r.view.page.rows.size()) r.boardPick = r.view.page.rows[row].steamId;
		return;
	}
	switch (arg) {
	case kRkHistory:
		r.sub = kRankedHistory;
		r.detailMatch = 0;
		r.historyBack.clear();
		LeagueHistoryRequest(0);
		break;
	case kRkHistoryNewest:
		r.historyBack.clear();
		LeagueHistoryRequest(0);
		break;
	case kRkHistoryNext:
		if (r.view.historyLoaded && r.view.history.more) {
			r.historyBack.push_back(r.view.historyBefore);
			LeagueHistoryRequest(LeagueHistoryNextBefore(r.view.history));
		}
		break;
	case kRkHistoryPrev:
		if (!r.historyBack.empty()) {
			const int64_t before = r.historyBack.back();
			r.historyBack.pop_back();
			LeagueHistoryRequest(before);
		}
		break;
	case kRkDisputeResult: OpenDispute(r.view.result.game.matchId); break;
	case kRkDisputeSend: RankedFieldEnter(); break;
	case kRkDisputeCancel:
		r.disputeMatch = 0;
		r.disputeText.clear();
		g_s.focus = kNoField;
		break;
	case kRkSearch:
		LeagueQueueJoin();
		break;
	case kRkCancel: LeagueQueueLeave(); break;
	case kRkAccept: LeagueMatchAnswer(true); break;
	case kRkDecline: LeagueMatchAnswer(false); break;
	case kRkBoard:
		r.sub = kRankedBoard;
		r.boardPick.clear();
		r.boardSeek.clear();
		r.boardSeason = -1;
		r.boardFriends = false;
		r.boardSearch.clear();
		RankedBoardAsk(0, false);
		break;
	case kRkHub: r.sub = kRankedHub; break;
	case kRkGuide: r.sub = kRankedGuide; break;
	case kRkGuideRanks: r.guideTab = 0; break;
	case kRkGuideRules: r.guideTab = 1; break;
	case kRkHubRecent: r.hubMastery = false; break;
	case kRkHubMastery: r.hubMastery = true; break;
	case kRkBoardPrev:
	case kRkBoardNext: {
		const int total = r.view.page.total;
		const int rows = RankedBoardRows();
		const int offset = r.view.page.offset + (arg == kRkBoardNext ? rows : -rows);
		RankedBoardAsk(std::max(0, std::min(offset, std::max(0, total - 1) / rows * rows)), false);
		break;
	}
	case kRkBoardMe: {
		const int me = r.view.around.me;
		if (me > 0) {
			r.boardSeason = -1;
			r.boardFriends = false;
			r.boardSearch.clear();
			r.boardPick = std::to_string(SteamOwnId());
			RankedBoardAsk(LeagueBoardPageOf(me, RankedBoardRows()), false);
		}
		break;
	}
	case kRkRoom:
		ResetRoom();
		LeagueRankedRetryRoom();
		break;
	case kRkAbandon: LeagueMatchAbandon(false); break;
	case kRkResultClose:
		r.resultOpen = false;
		r.sub = kRankedHub;
		LeagueRankedResultSeen();
		break;
	case kRkAgain:
		r.resultOpen = false;
		r.sub = kRankedHub;
		LeagueRankedResultSeen();
		LeagueQueueJoin();
		break;
	case kRkResultBoard:
		r.resultOpen = false;
		LeagueRankedResultSeen();
		RankedAction(kRkBoard);
		RankedAction(kRkBoardMe);
		break;
	case kRkSkip:
		r.resultAt = GetTickCount() - 100000;
		r.resultCues = kLeagueCueOutcome | kLeagueCueRank;
		break;
	case kRkSteam:
		// In the Steam overlay's browser, as the account's link opens; in the browser when the overlay is off.
		if (_strtoui64(r.boardPick.c_str(), nullptr, 10)) SteamOpenWebPage("https://steamcommunity.com/profiles/" + r.boardPick);
		break;
	case kRkContentClose: r.contentOpen = false; break;
	case kRkPrepareMods: {
		const LeagueModsRule &rule = r.view.season.mods;
		if (r.view.phase != LeagueRankedPhase::Idle || !rule.set) break;
		LeagueRankedClearNotice();
		LeagueModsPrepStart(rule, "ranked");
		break;
	}
	case kRkContentSwitch: {
		r.contentOpen = false;
		if (!r.contentSwitchable || r.contentProfile.empty()) break;
		const std::string profile = r.contentProfile;
		ModsSetActiveProfileName(profile);
		ModsApplyWithoutRestart();
		if (ModsRestartNeeded()) {
			LogW("league", "ranked: the content profile \"%s\" needs a restart after all", profile.c_str());
			ModUiToast("The content could not be switched without restarting the game.", true);
			break;
		}
		LogI("league", "ranked: switched to the content profile \"%s\" of the season; searching", profile.c_str());
		LeagueRankedClearNotice();
		ModUiToast(TrFmt("Content profile \"%s\" active: searching.", {ModsProfileLabel(profile)}));
		LeagueQueueJoin();
		break;
	}
	default: break;
	}
}

void RankedFrame()
{
	RankedScreen &r = g_s.ranked;
	const LeagueResultState before = r.view.result.state;
	r.view = LeagueRankedState();
	const LeagueRankedView &v = r.view;
	if (v.disputeSerial != r.disputeSeen) {
		r.disputeSeen = v.disputeSerial;
		if (!v.disputeNotice.empty()) ModUiToast(v.disputeNotice, v.disputeBad);
	}
	// The queue answered that the season needs other content: the dialog offers the profile that has it.
	if (v.wrongContentSerial != r.contentSeen) {
		r.contentSeen = v.wrongContentSerial;
		if (!v.seasonContentKey.empty()) {
			r.contentName = v.seasonContentName;
			r.contentProfile = ModsProfileWithGameplayKey(v.seasonContentKey);
			r.contentSwitchable = !r.contentProfile.empty() && ModsSwitchesWithoutRestart(r.contentProfile);
			r.contentOpen = true;
			LogI("league", "ranked: the season needs other content (%s); profile with it: %s, %s", v.seasonContentName.empty() ? "unnamed" : v.seasonContentName.c_str(),
				r.contentProfile.empty() ? "none" : r.contentProfile.c_str(), r.contentSwitchable ? "switches without a restart" : "needs a restart");
		}
	}
	if (r.contentOpen && g_s.view != kViewRanked) r.contentOpen = false;
	if (r.inviteOpen && (g_s.view != kViewRanked || LeagueDuoOwnRole() != LeagueDuoRole::Solo)) {
		r.inviteOpen = false;
		if (g_s.focus == kRankedField) g_s.focus = kNoField;
	}
	if (r.disputeMatch && g_s.view != kViewRanked) {
		r.disputeMatch = 0;
		if (g_s.focus == kRankedField) g_s.focus = kNoField;
	}
	// The match found is answered in a dialog over any tab; the room's own screen hides it.
	g_s.searching = !g_s.game.active && (v.phase == LeagueRankedPhase::Found || v.phase == LeagueRankedPhase::Accepted);
	g_s.found = g_s.searching;
	if (r.resultOpen && v.result.state == LeagueResultState::None) r.resultOpen = false;
	// The animation starts when the server's verdict arrives, not when the screen opened on "sending".
	if (r.resultOpen && v.result.state == LeagueResultState::Ready && before != LeagueResultState::Ready) {
		r.resultAt = GetTickCount();
		r.resultCues = 0;
	}
	// The sounds of a rated result, each at its moment of the animation.
	const LeagueRankedResult &rated = v.result.answer.ranked;
	const bool disputed = v.result.answer.status == "disputed" || v.result.answer.outcome == "disputed";
	if (r.resultOpen && v.result.state == LeagueResultState::Ready && rated.present && !disputed) {
		const int due = LeagueResultCues(rated, GetTickCount() - r.resultAt) & ~r.resultCues;
		if (due & kLeagueCueOutcome) UiSoundPlay(rated.delta > 0 ? 4 : 5);
		if (due & kLeagueCueRank) UiSoundPlay(LeagueResultKindOf(rated) == LeagueResultKind::Demotion ? 7 : 6);
		r.resultCues |= due;
	}
	if (r.resultOpen) g_s.view = kViewRanked;
	RankedSeasonsFrame();
}

// The season's notices in whatever menu the player is, never in a game: it started, a week is left, the last day. What was told is
// kept with the game's settings, so a restart does not tell it again.
void SeasonNotices(const LeagueRankedView &v)
{
	if (!v.linked) return;
	LeagueSeasonRefresh();
	if (!v.seasonLoaded || InGame()) return;
	static bool loaded = false;
	static LeagueSeasonTold told;
	if (!loaded) {
		loaded = true;
		told.started = GameRoomLoadSetting("LeagueSeasonStartedTold", -1);
		told.weekLeft = GameRoomLoadSetting("LeagueSeasonWeekLeftTold", -1);
		told.lastDay = GameRoomLoadSetting("LeagueSeasonLastDayTold", -1);
	}
	const int64_t now = LeagueServerNowMs();
	const LeagueSeasonNotice n = LeagueSeasonNoticeFor(v.season, now, told);
	if (n == LeagueSeasonNotice::None) return;
	GameRoomSaveSetting("LeagueSeasonStartedTold", told.started);
	GameRoomSaveSetting("LeagueSeasonWeekLeftTold", told.weekLeft);
	GameRoomSaveSetting("LeagueSeasonLastDayTold", told.lastDay);
	const std::string name = LeagueSeasonName(v.season, v.lang);
	if (n == LeagueSeasonNotice::Started) {
		std::string fresh;
		for (const std::string &m : v.season.newMaps) fresh += (fresh.empty() ? "" : ", ") + m;
		if (fresh.empty()) ModUiToast(TrFmt("The ranked season %s has started. Good luck in the queue!", {name}));
		else ModUiToast(TrFmt("The ranked season %s has started with new maps: %s. Good luck in the queue!", {name, fresh}));
	}
	else if (n == LeagueSeasonNotice::WeekLeft) {
		ModUiToast(TrFmt("The ranked season %s ends in %s days: play your last games to earn its rewards.", {name, std::to_string(LeagueDaysLeft(v.season.endsAt, now))}));
	}
	else ModUiToast(TrFmt("Last day of the ranked season %s: it ends in less than 24 hours.", {name}));
}

// The recap of the last season for a player it gave no reward (the ceremony tells the others), once a season: the record, and what the
// vexillum still asked for.
void RecapNotice(const LeagueRankedView &v)
{
	if (!v.linked || InGame()) return;
	LeagueMeRefresh();
	const auto me = LeagueMeShared();
	if (!me || !me->loaded || !me->me.recap.present || me->me.recap.rewarded) return;
	const LeagueRecap &r = me->me.recap;
	static int seen = -2;
	if (seen == -2) seen = GameRoomLoadSetting("LeagueRecapSeen", -1);
	if (seen == r.season) return;
	seen = r.season;
	GameRoomSaveSetting("LeagueRecapSeen", r.season);
	const std::string name = r.name.empty() ? TrFmt("Season %s", {LeagueRoman(r.season)}) : r.name;
	std::string missing;
	if (r.vexGames > 0 && r.vexWins > 0) missing = TrFmt("%s more games and %s more wins", {std::to_string(r.vexGames), std::to_string(r.vexWins)});
	else if (r.vexGames > 0) missing = TrFmt("%s more games", {std::to_string(r.vexGames)});
	else if (r.vexWins > 0) missing = TrFmt("%s more wins", {std::to_string(r.vexWins)});
	const std::string record = TrFmt("The ranked season %s is over: you played %s games and won %s.", {name, std::to_string(r.games), std::to_string(r.wins)});
	ModUiToast(missing.empty() ? record : record + " " + TrFmt("The vexillum needed %s.", {missing}));
}

void RankedGameThreadTick()
{
	const DWORD now = GetTickCount();
	if (now - g_auto.lastRead < 200) return;
	g_auto.lastRead = now;
	const LeagueRankedView v = LeagueRankedState();
	// The screen keeps the room up to date only while it is open.
	const bool matchLive = v.phase == LeagueRankedPhase::Ready || v.phase == LeagueRankedPhase::Playing;
	if (!g_s.open && (matchLive || v.result.state != LeagueResultState::None)) g_s.room = SteamCurrentRoom();
	if (!g_auto.init) {
		g_auto.init = true;
		g_auto.foundSerial = v.foundSerial;
	}
	if (v.foundSerial != g_auto.foundSerial) {
		g_auto.foundSerial = v.foundSerial;
		if (v.phase == LeagueRankedPhase::Found) CallPlayer();
	}
	RankedTeamGameThreadTick();
	SeasonNotices(v);
	RecapNotice(v);
	// A game or a room ends the search: nobody can answer a match from inside one. The partner of a duo cannot end the leader's.
	if ((InGame() || GameRoomActive()) && !v.partnerSearch && (v.phase == LeagueRankedPhase::Searching || v.phase == LeagueRankedPhase::Joining)) {
		LogI("league", "ranked: a game or a room started; the search stops");
		LeagueQueueLeave();
		ModUiToast("The ranked search stopped because you entered a game or a room.");
	}
	RoomStep(v, now);
	// Back in the menu after a ranked game, the result opens by itself.
	RankedScreen &r = g_s.ranked;
	if (v.result.state != LeagueResultState::None && v.result.game.matchId != r.resultMatch && !r.resultOpen && !InGame() &&
		!g_s.game.active && !g_s.room.id) {
		if (g_s.open || MainMenuOnTop()) {
			OpenRankedTab();
			r.resultOpen = true;
			r.resultMatch = v.result.game.matchId;
			r.resultAt = now;
			r.resultCues = 0;
			r.view = v;
			LogI("league", "ranked: showing the result of match %lld", static_cast<long long>(v.result.game.matchId));
		}
	}
}

void PaintRankedPill(Painter &p, const State &st, int right, int left)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const bool searching = v.phase == LeagueRankedPhase::Joining || v.phase == LeagueRankedPhase::Searching;
	const bool ready = (v.phase == LeagueRankedPhase::Ready || v.phase == LeagueRankedPhase::Playing) && !st.room.id && !st.connecting;
	if (!searching && !ready) return;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int h = kHeaderButtonH, y = l.margin, room = right - 12 - left;
	p.Font(4);
	if (searching) {
		const std::string clock = LeagueClockText(LeagueServerNowMs() - v.since);
		SelectObject(p.dc, BrandFont(BrandFace::Display, 26, p.s));
		p.raw = true;
		int clockW = std::max(p.TextWidth("00:00"), p.TextWidth(clock)) + 8;
		p.raw = false;
		// The spinner, the label, the time and the cross: the label shortens and goes, then the time, then the cross, before the pill
		// would cover the tabs beside it. The partner of a duo has no cross: only the leader stops the search.
		p.Font(4);
		const bool team = LeagueQueueIsTeam(v.searchQueue);
		bool cross = !v.partnerSearch;
		if (46 + clockW + (cross ? 44 : 16) > room) clockW = 0;
		if (46 + clockW + (cross ? 44 : 16) > room) cross = false;
		const int fixed = 46 + clockW + (cross ? 44 : clockW ? 16 : 0);
		const char *label = team ? "Searching 2 vs 2" : "Searching 1 vs 1";
		int labelW = p.CapsWidth(label, 2) + 14;
		if (fixed + labelW > room) {
			label = team ? "2 vs 2" : "1 vs 1";
			labelW = p.CapsWidth(label, 2) + 14;
		}
		if (fixed + labelW > room) labelW = 0;
		const int w = fixed + labelW, x = right - 12 - w;
		p.Fill(x, y, w, h, pal::selected);
		p.Fill(x, y, 3, h, pal::red);
		p.Spinner(x + 26, y + h / 2, 9, 4, GetTickCount());
		if (labelW) p.TextCaps(x + 46, y, labelW, h, label, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
		if (clockW) {
			SelectObject(p.dc, BrandFont(BrandFace::Display, 26, p.s));
			p.raw = true;
			p.Text(x + 46 + labelW, y, clockW, h, clock, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
		}
		if (!cross) {
			p.AddHit(x, y, w, h, kRanked, 0);
			return;
		}
		const bool hot = p.Hover(x + w - 44, y, 44, h, mx, my);
		p.Line(x + w - 28, y + 15, x + w - 16, y + 27, hot ? pal::text : pal::textDim, 2);
		p.Line(x + w - 28, y + 27, x + w - 16, y + 15, hot ? pal::text : pal::textDim, 2);
		p.AddHit(x + w - 44, y, 44, h, kRankedAct, kRkCancel);
		p.AddHit(x, y, w - 44, h, kRanked, 0);
		return;
	}
	const char *label = "Ranked match ready  \xC2\xB7  go to the room";
	if (p.CapsWidth(label, 2) + 40 > room) label = "Go to the room";
	// Without room for even the short label, the pill keeps only its arrow rather than cover the tabs.
	const bool arrowOnly = p.CapsWidth(label, 2) + 40 > room;
	const int w = arrowOnly ? std::max(44, room) : p.CapsWidth(label, 2) + 40, x = right - 12 - w;
	p.Fill(x, y, w, h, pal::selected);
	p.Fill(x, y, 3, h, pal::red);
	if (arrowOnly) p.ArrowRight(x + (w - 8) / 2 + 1, y + h / 2 - 6, 8, 12, pal::text);
	else p.TextCaps(x + 20, y, w - 30, h, label, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	p.AddHit(x, y, w, h, kRankedAct, kRkRoom);
}

void PaintSearch(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	if (LeagueQueueIsTeam(v.searchQueue) || !v.found.teams.empty() || (v.ticketLoaded && TeamTicket(v.ticket))) {
		PaintSearchTeam(p, st);
		return;
	}
	const bool accepted = v.phase == LeagueRankedPhase::Accepted;
	const FoundBox box = PaintFoundBox(p, st, 1240, 720);
	const int dx = box.x, dy = box.y, dw = box.w;
	SelectObject(p.dc, BrandFont(BrandFace::Display, 58, p.s));
	p.TextCaps(dx, dy + 24, dw, 60, "Match found", pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	const std::string caption = RankedFoundCaption(v, "Ranked 1 vs 1");
	p.Font(4);
	p.raw = true;
	p.TextCaps(dx, dy + 88, dw, 20, caption, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	p.raw = false;

	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;
	const int cardW = (dw - 64 - 296) / 2, cardH = 284, cy = dy + 132;
	const bool otherAccepted = v.ticketLoaded && v.opponent.accepted;
	// This player's record is their own standing; the opponent's comes with the match found and the ticket.
	LeaguePlayerCard mine = v.ticketLoaded ? v.me : LeaguePlayerCard();
	if (!v.ticketLoaded) {
		mine.tier = me.me.ranked.tier;
		mine.division = me.me.ranked.division;
	}
	if (me.loaded && me.me.ranked.present) {
		LeaguePlayerCard own;
		own.standing = true;
		own.wins = me.me.ranked.wins;
		own.losses = me.me.ranked.losses;
		own.glory = me.me.ranked.glory;
		own.place = me.me.ranked.position;
		TakeStanding(mine, own);
		if (!mine.streakKnown) {
			mine.streakKnown = true;
			mine.streak = me.me.ranked.streak;
		}
	}
	PaintSide(p, dx + 32, cy, cardW, cardH, SteamOwnId(), st.player, mine, true, accepted);
	LeaguePlayerCard o = v.ticketLoaded ? v.opponent : v.found.opponent;
	if (v.ticketLoaded) TakeStanding(o, v.ticket.opponent);
	TakeStanding(o, v.found.opponent);
	const std::string otherName = !o.name.empty() ? o.name : Tr("Opponent");
	PaintSide(p, dx + dw - 32 - cardW, cy, cardW, cardH, _strtoui64(o.steamId.c_str(), nullptr, 10), otherName, o, false, otherAccepted);

	// The countdown to answer, in the middle, on the server's clock.
	const int64_t now = LeagueServerNowMs();
	const int64_t acceptBy = v.found.acceptBy;
	const int mid = dx + dw / 2;
	SelectObject(p.dc, BrandFont(BrandFace::Display, 34, p.s));
	p.TextCaps(mid - 148, cy + 8, 296, 34, "VS", pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	const int ringY = cy + 54 + 74;
	PaintArc(p, mid, ringY, 66, 8, pal::hairline, 1.0f);
	PaintArc(p, mid, ringY, 66, 8, accepted ? pal::ok : pal::red, LeagueCountdownFraction(acceptBy, now));
	SelectObject(p.dc, BrandFont(BrandFace::Display, 66, p.s));
	p.raw = true;
	p.Text(mid - 74, ringY - 40, 148, 66, std::to_string(LeagueCountdownSeconds(acceptBy, now)), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	p.Font(4);
	p.TextCaps(mid - 74, ringY + 22, 148, 16, "seconds", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	PaintEvenness(p, v, false, mid - 140, cy + 200, 280);
	p.Font(3);
	p.TextWrapped(mid - 140, cy + 230, 280, 44, accepted ? TrFmt("Waiting for %s to accept", {otherName}) : Tr("Accept before the countdown ends"), pal::caption);

	// The map and the options of the season.
	const int my2 = cy + cardH + 20, mw = dw - 64, mh = 128;
	p.Fill(dx + 32, my2, mw, mh, pal::raised);
	p.Frame(dx + 32, my2, mw, mh, pal::hairline);
	const LeagueMapFacts &map = v.ticketLoaded && !v.ticket.map.name.empty() ? v.ticket.map : v.found.map;
	PaintLeagueMapThumb(p, dx + 48, my2 + 16, 96, map.name);
	p.Font(4);
	p.TextCaps(dx + 164, my2 + 18, 300, 16, "Map", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	SelectObject(p.dc, BrandFont(BrandFace::Display, 44, p.s));
	p.raw = true;
	p.TextCaps(dx + 164, my2 + 36, 330, 46, map.name.empty() ? Tr("Chosen by the league") : LeagueMapText(map.name), pal::text,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	std::string mapLine = Tr("2 players");
	if (!map.size.empty()) mapLine += "  \xC2\xB7  " + LeagueMapText(map.size);
	if (!map.type.empty()) mapLine += "  \xC2\xB7  " + Tr(Plain(map.type) == "random" ? "Random map" : "Scenario");
	p.Text(dx + 164, my2 + 84, 330, 22, mapLine, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Fill(dx + 532, my2 + 16, 1, 96, pal::hairline);
	const LeagueRoomSettings &s = v.ticketLoaded ? v.ticket.settings : v.season.settings;
	const int fx = dx + 562, fw = (dx + 32 + mw - 24 - fx) / 3;
	const std::array<MatchFact, 6> facts = MatchFacts(s);
	for (int i = 0; i < 6; i++) Fact(p, fx + i % 3 * fw, my2 + 16 + i / 3 * 52, fw - 16, facts[i].label, facts[i].value);

	const int by = my2 + mh + 28;
	UseSharedFonts(p);
	if (accepted) {
		p.Spinner(mid - 150, by + 32, 11, 4, GetTickCount());
		p.Font(1);
		p.Text(mid - 128, by, 380, 64, TrFmt("Accepted. Waiting for %s...", {otherName}), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	else {
		SelectObject(p.dc, BrandFont(BrandFace::Display, 36, p.s));
		p.fonts[5] = BrandFont(BrandFace::Display, 36, p.s);
		p.Button(mid - 328, by, 400, 64, "Accept", kRankedAct, kRkAccept, mx, my, !v.answering, true, 5);
		Ghost(p, mid + 88, by, 240, 64, "Decline", kRkDecline, !v.answering, mx, my);
		UseSharedFonts(p);
	}
	p.Font(3);
	p.Text(dx, by + 74, dw, 22, "If you decline or don't answer you can't search for 5 minutes; the wait doubles if you do it again.", pal::caption,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

int PaintRankedMark(Painter &p, const State &st, int x, int y, int h)
{
	const char *label = st.room.info.mode == kLeagueTeamRoomMode ? "Ranked 2 vs 2" : "Ranked 1 vs 1";
	p.Font(4);
	const int w = p.CapsWidth(label, 2) + 24;
	p.Fill(x, y, w, h, RGB(0x3a, 0x12, 0x14));
	p.Fill(x, y, 3, h, pal::red);
	p.TextCaps(x + 3, y, w - 3, h, label, pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	return w;
}

StartBlock RankedStartBlocker(const State &st, const GameRoomSnapshot &g)
{
	if (!RankedRoomLocked(st)) return {};
	if (!LeagueRoomHeld(g, st.room.id)) return {Tr("Preparing the room..."), Tr("The league's options are being set in the room.")};
	if (st.room.info.mode != kLeagueTeamRoomMode) return {};
	const LeagueRankedView &v = st.ranked.view;
	int humans = 0;
	for (const GameRoomSlot &s : g.slots) humans += s.used && s.type == 3 ? 1 : 0;
	if (humans < 4) return {"Waiting for the four players...", Tr("The game starts when the four players of the match are in the room.")};
	if (!v.ticketLoaded) return {};
	// Each player's slot in the side the league gave them.
	const std::vector<LeagueMatchTeam> teams = LeagueTicketTeams(v.ticket);
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &s = g.slots[i];
		if (!s.used || s.type != 3) continue;
		const uint64_t id = s.local ? SteamOwnId() : MemberIdByName(st, s.name);
		const int side = id ? LeagueSideOf(teams, std::to_string(id)) : 0;
		if (side && s.team != side) return {"Setting the teams...", Tr("Wait a moment: each player is being put in their team.")};
	}
	return {};
}

}
