// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
#include "core/config.h"
#include "core/league.h"
#include "core/leaguemaps.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"
#include "core/log.h"
#include "hooks/dmshim.h"
#include "i18n/tr.h"
#include "mods/modoptions.h"
#include "mods/mods.h"
#include "net/leagueclient.h"
#include "net/steamnet.h"
#include "ui/leaguemapthumb.h"
#include "ui/leagueui.h"
#include "ui/modui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

const int kOptMapType = 0x1000, kOptMapSize = 0x01000008, kOptPopulation = 0x1004, kOptGold = 0x1005, kOptNoFog = 0x1007, kOptExplored = 0x1008,
		  kOptNoBonuses = 0x10a7;
const int kCompetitive = 1;

// What the game thread does for a ranked match: the room it made or tried to join, and when.
struct RoomAuto {
	bool init = false;
	DWORD lastRead = 0;
	unsigned foundSerial = 0;
	int64_t match = 0;
	DWORD createdAt = 0, joinAt = 0, kickAt = 0, settingsAt = 0;
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

bool Contains(const std::string &haystack, const std::string &needle)
{
	return !needle.empty() && haystack.find(needle) != std::string::npos;
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

int FindItem(const GameRoomOption &o, const std::vector<std::string> &words, bool exact)
{
	for (size_t k = 0; k < o.items.size(); k++) {
		if (k < o.itemDisabled.size() && o.itemDisabled[k]) continue;
		const std::string item = Plain(o.items[k]);
		for (const std::string &w : words) {
			if (exact ? item == w : Contains(item, w)) return static_cast<int>(k);
		}
	}
	return -1;
}

void SetCombo(const GameRoomSnapshot &g, int id, const std::vector<std::string> &words, bool exact)
{
	for (const GameRoomOption &o : g.options) {
		if (o.id != id || o.checkbox) continue;
		const int want = FindItem(o, words, exact);
		if (want >= 0 && o.sel != want && Due(id)) {
			GameRoomSetOption(id, want);
			Predict(kPredOptSel, id, want);
		}
		return;
	}
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

void SetCheck(const GameRoomSnapshot &g, int id, bool value)
{
	for (const GameRoomOption &o : g.options) {
		if (o.id != id || !o.checkbox) continue;
		if (o.value != value && Due(id)) {
			GameRoomToggleOption(id);
			Predict(kPredOptVal, id, value);
		}
		return;
	}
}

// The host keeps the room as the server's ticket says: map, size, preset and the options of the season. The other player cannot change
// anything (RankedRoomAction), so this only puts the room right once and keeps it so.
void ApplySettings(const LeagueMatchTicket &t)
{
	const GameRoomSnapshot g = GameRoomGet();
	if (!g.active || !g.host || !g.online || g.busy) return;
	const LeagueRoomSettings &s = t.settings;
	const bool competitive = s.preset.empty() || s.preset == "competitive";
	const Preset &preset = kPresets[kCompetitive];
	// The type first: the sizes the game offers depend on it.
	if (!t.map.name.empty()) SetMapItem(g, kOptMapType, t.map.name);
	if (!t.map.size.empty()) SetMapItem(g, kOptMapSize, t.map.size);
	unsigned base = competitive ? preset.rules : 0;
	if (s.diplomacy == "locked") base |= kRuleLockDiplomacy;
	else if (s.diplomacy == "free" || s.diplomacy == "open") base &= ~kRuleLockDiplomacy;
	if (s.hideCivs == 1) base |= kRuleBlindPick;
	if (s.cheats == 1) base |= kRuleCheats;
	const unsigned rules = (g.rules & ~kRuleBaseMask) | base;
	if (rules != g.rules && Due(-1)) GameRoomSetRules(rules);
	if (competitive && (g.siteFlags & kSiteMask) != preset.sites && Due(-2)) GameRoomSetSiteFlags(preset.sites);
	if (competitive) {
		SetCombo(g, kOptGold, {Plain(preset.gold)}, true);
		SetCombo(g, kOptPopulation, {Plain(preset.pop)}, true);
		SetCheck(g, kOptNoBonuses, !preset.bonuses);
	}
	const bool fog = s.fog >= 0 ? s.fog == 1 : competitive ? preset.fog : true;
	const bool explored = s.explored >= 0 ? s.explored == 1 : competitive ? preset.explored : false;
	SetCheck(g, kOptNoFog, !fog);
	SetCheck(g, kOptExplored, explored);
	if (!g_auto.settingsLogged) {
		g_auto.settingsLogged = true;
		LogI("league", "ranked: the room takes the league's options (map %s %s, preset %s)", t.map.name.c_str(), t.map.size.c_str(),
			competitive ? "competitive" : s.preset.c_str());
	}
}

// The host's Steam room for a ranked match: private (entered by invitation or by its id), two players, the league's tag, and spectators
// with the season's delay in a room anyone can find.
void CreateRoom(const LeagueRankedView &v)
{
	OnlineCreateSettings s;
	const std::string me = !v.me.name.empty() ? v.me.name : SteamOnlinePlayerName();
	const std::string other = !v.opponent.name.empty() ? v.opponent.name : Tr("your opponent");
	s.name = TrFmt("Ranked: %s vs %s", {me, other});
	s.map = !v.ticket.map.name.empty() ? v.ticket.map.name : std::string("Chosen in the room");
	s.mode = "Ranked 1v1";
	s.maxPlayers = 2;
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
	g_s.roomLimit = 2;
	g_s.autoHost = true;
	g_s.connecting = true;
	g_s.connectStart = GetTickCount();
	LogI("league", "ranked: creating the room of match %lld", static_cast<long long>(v.matchId));
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
		if (!v.host || !OwnsRoom()) return;
		LeagueRankedRoomOpened(g_s.room.id);
		if (g_auto.invitedLobby != g_s.room.id) {
			g_auto.invitedLobby = g_s.room.id;
			SteamInviteToRoom(_strtoui64(v.opponent.steamId.c_str(), nullptr, 10));
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
	if (const HWND wnd = g_wakeWnd.load()) {
		FLASHWINFO fi = {sizeof(fi), wnd, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
		FlashWindowEx(&fi);
	}
	if (GetConfig().matchFoundSound) UiSoundPlay(1);
	if (!g_s.open && !InGame() && MainMenuOnTop()) OpenRankedTab();
	else if (!g_s.open) ModUiToast("Ranked match found! Open Multiplayer to accept it.");
}

COLORREF Ink(LeagueTier t)
{
	return t == LeagueTier::None ? pal::caption : LeagueTierInk(t);
}

std::string RankLabel(LeagueTier t, int division)
{
	return t == LeagueTier::None ? Tr("In placement") : LeagueRankLabel(t, division);
}

// An outlined button whose action is a ranked one, with its argument.
void Ghost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my, int font = 4)
{
	const bool hot = enabled && p.Hover(x, y, w, h, mx, my);
	const bool down = enabled && p.pressAction == kRankedAct && p.pressArg == arg;
	if (down) p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, !enabled ? pal::hairline : hot || down ? pal::edgeHot : pal::edge);
	p.Font(font);
	p.TextCaps(x, y, w, h, label, !enabled ? pal::disabled : hot || down ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	if (enabled) p.AddHit(x, y, w, h, kRankedAct, arg);
}

void Chip(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, COLORREF back)
{
	p.Fill(x, y, w, h, back);
	p.Font(4);
	p.TextCaps(x, y, w, h, text, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
}

// The record of a player as the found match gives it: wins and losses, then the share won.
std::string RecordText(const LeaguePlayerCard &c)
{
	return std::to_string(c.wins) + "\xE2\x80\x93" + std::to_string(c.losses) + "  \xC2\xB7  " + std::to_string(LeagueWinRate(c.wins, c.losses)) + "\xC2\xA0%";
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
}

// One side of the match found: avatar, name, record, emblem, rank, Glory and place, and whether they accepted.
void PaintSide(Painter &p, int x, int y, int w, int h, uint64_t id, const std::string &name, const LeaguePlayerCard &card, bool you, bool accepted)
{
	const LeagueTier tier = card.tier;
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::hairline);
	const int av = 72;
	if (!PaintAvatar(p, x + 24, y + 24, av, id)) {
		p.Fill(x + 24, y + 24, av, av, pal::hairline);
		p.Font(2);
		p.raw = true;
		p.Text(x + 24, y + 24, av, av, name.substr(0, std::min<size_t>(2, name.size())), pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}
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
	p.TextCaps(x + 112, y + 114, w - 136, 38, RankLabel(tier, card.division), Ink(tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	// Glory and place, once the player has a rank: a player in placement has neither yet.
	if (card.standing && tier != LeagueTier::None) {
		std::string standing = TrFmt("%s Glory", {std::to_string(card.glory)});
		if (card.place > 0) standing += "  \xC2\xB7  " + TrFmt("place %s", {std::to_string(card.place)});
		p.Font(0);
		p.raw = true;
		p.Text(x + 112, y + 154, w - 136, 24, standing, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
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

std::string YesNo(int flag, const char *unknown = "—")
{
	return flag < 0 ? std::string(unknown) : Tr(flag ? "Yes" : "No");
}

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
	return g_s.ranked.disputeText;
}

void RankedFieldEnter()
{
	RankedScreen &r = g_s.ranked;
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

void RankedAction(int arg)
{
	RankedScreen &r = g_s.ranked;
	if (arg >= kRkDisputeHistory && arg < kRkArgEnd) {
		const size_t i = static_cast<size_t>(arg - kRkDisputeHistory);
		if (i < r.view.history.games.size()) OpenDispute(r.view.history.games[i].id);
		return;
	}
	if (arg >= kRkDisputeRecent && arg < kRkDisputeHistory) {
		const size_t i = static_cast<size_t>(arg - kRkDisputeRecent);
		if (i < r.view.recent.size()) OpenDispute(r.view.recent[i].id);
		return;
	}
	if (arg >= kRkRow && arg < kRkDisputeRecent) {
		r.boardSel = arg - kRkRow;
		return;
	}
	switch (arg) {
	case kRkHistory:
		r.sub = kRankedHistory;
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
		r.boardSel = -1;
		r.boardOffset = 0;
		LeagueBoardRequest(0, kRankedBoardRows, false);
		break;
	case kRkHub: r.sub = kRankedHub; break;
	case kRkBoardPrev:
	case kRkBoardNext:
	case kRkBoardTop: {
		const int total = r.view.page.total;
		int offset = arg == kRkBoardTop ? 0 : r.view.page.offset + (arg == kRkBoardNext ? kRankedBoardRows : -kRankedBoardRows);
		offset = std::max(0, std::min(offset, std::max(0, total - 1) / kRankedBoardRows * kRankedBoardRows));
		r.boardOffset = offset;
		r.boardSel = -1;
		LeagueBoardRequest(offset, kRankedBoardRows, false);
		break;
	}
	case kRkBoardMe: {
		const int me = r.view.around.me;
		r.boardSel = -1;
		if (me > 0) {
			r.boardOffset = LeagueBoardPageOf(me, kRankedBoardRows);
			LeagueBoardRequest(r.boardOffset, kRankedBoardRows, false);
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
	case kRkSkip: r.resultAt = GetTickCount() - 100000; break;
	case kRkSteam: {
		const std::vector<LeagueBoardRow> &rows = r.view.page.rows;
		if (r.boardSel >= 0 && r.boardSel < static_cast<int>(rows.size())) SteamOpenProfile(_strtoui64(rows[r.boardSel].steamId.c_str(), nullptr, 10));
		break;
	}
	case kRkNotice: LeagueRankedClearNotice(); break;
	case kRkContentClose: r.contentOpen = false; break;
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
	if (r.disputeMatch && g_s.view != kViewRanked) {
		r.disputeMatch = 0;
		if (g_s.focus == kRankedField) g_s.focus = kNoField;
	}
	// The match found is answered in a dialog over any tab; the room's own screen hides it.
	g_s.searching = !g_s.game.active && (v.phase == LeagueRankedPhase::Found || v.phase == LeagueRankedPhase::Accepted);
	g_s.found = g_s.searching;
	if (r.resultOpen && v.result.state == LeagueResultState::None) r.resultOpen = false;
	// The animation starts when the server's verdict arrives, not when the screen opened on "sending".
	if (r.resultOpen && v.result.state == LeagueResultState::Ready && before != LeagueResultState::Ready) r.resultAt = GetTickCount();
	if (r.resultOpen) g_s.view = kViewRanked;
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
	// A game or a room ends the search: nobody can answer a match from inside one.
	if ((InGame() || GameRoomActive()) && (v.phase == LeagueRankedPhase::Searching || v.phase == LeagueRankedPhase::Joining)) {
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
		const int clockW = std::max(p.TextWidth("00:00"), p.TextWidth(clock)) + 8;
		p.raw = false;
		// The spinner, the label, the time and the cross: the label shortens, and goes, before anything overlaps.
		p.Font(4);
		const int fixed = 46 + clockW + 44;
		const char *label = "Searching 1 vs 1";
		int labelW = p.CapsWidth(label, 2) + 14;
		if (fixed + labelW > room) {
			label = "1 vs 1";
			labelW = p.CapsWidth(label, 2) + 14;
		}
		if (fixed + labelW > room) labelW = 0;
		const int w = fixed + labelW, x = right - 12 - w;
		p.Fill(x, y, w, h, pal::selected);
		p.Fill(x, y, 3, h, pal::red);
		p.Spinner(x + 26, y + h / 2, 9, 4, GetTickCount());
		if (labelW) p.TextCaps(x + 46, y, labelW, h, label, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
		SelectObject(p.dc, BrandFont(BrandFace::Display, 26, p.s));
		p.raw = true;
		p.Text(x + 46 + labelW, y, clockW, h, clock, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		const bool hot = p.Hover(x + w - 44, y, 44, h, mx, my);
		p.Line(x + w - 28, y + 15, x + w - 16, y + 27, hot ? pal::text : pal::textDim, 2);
		p.Line(x + w - 28, y + 27, x + w - 16, y + 15, hot ? pal::text : pal::textDim, 2);
		p.AddHit(x + w - 44, y, 44, h, kRankedAct, kRkCancel);
		p.AddHit(x, y, w - 44, h, kRanked, 0);
		return;
	}
	const char *label = "Ranked match ready  \xC2\xB7  go to the room";
	if (p.CapsWidth(label, 2) + 40 > room) label = "Go to the room";
	const int w = std::min(std::max(160, room), p.CapsWidth(label, 2) + 40), x = right - 12 - w;
	p.Fill(x, y, w, h, pal::selected);
	p.Fill(x, y, 3, h, pal::red);
	p.TextCaps(x + 20, y, w - 30, h, label, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	p.AddHit(x, y, w, h, kRankedAct, kRkRoom);
}

void PaintSearch(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const bool accepted = v.phase == LeagueRankedPhase::Accepted;
	const int dw = std::min(1240, st.viewW - 40), dh = std::min(720, st.viewH - 40);
	const int dx = (st.viewW - dw) / 2, dy = (st.viewH - dh) / 2;
	// A dialog: nothing under it takes the mouse.
	p.hits->clear();
	p.noHover = false;
	PaintTint(p, 0, 0, st.viewW, st.viewH, pal::bg, 219);
	p.Fill(dx + 6, dy + 6, dw, dh, RGB(8, 8, 7));
	p.Fill(dx, dy, dw, dh, pal::panel);
	p.Frame(dx, dy, dw, dh, pal::hairline);
	UseScreenFonts(p);
	SelectObject(p.dc, BrandFont(BrandFace::Display, 58, p.s));
	p.TextCaps(dx, dy + 24, dw, 60, "Match found", pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	std::string caption = Tr("Ranked 1 vs 1");
	if (v.seasonLoaded) caption += "  \xC2\xB7  " + TrFmt("Season %s", {std::to_string(v.season.number)}) + "  \xC2\xB7  " + LeagueSeasonName(v.season, v.lang);
	p.Font(4);
	p.raw = true;
	p.TextCaps(dx, dy + 88, dw, 20, caption, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	p.raw = false;

	const LeagueMeView me = LeagueMeState();
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
	// How even the match is, from the chance the league gives this player to win it.
	const double expected = v.ticketLoaded && v.ticket.expected >= 0 ? v.ticket.expected : v.found.expected;
	if (const char *even = LeagueEvennessText(expected)) {
		const COLORREF ink = std::string(even) == "Even match" ? pal::ok : expected > 0.5 ? pal::text : pal::warn;
		p.Font(1);
		if (p.TextWidth(even) > 280) p.Font(3);
		p.Text(mid - 140, cy + 200, 280, 24, even, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
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
	Fact(p, fx, my2 + 16, fw - 16, "Preset", s.preset.empty() || s.preset == "competitive" ? Tr("Competitive") : s.preset);
	Fact(p, fx + fw, my2 + 16, fw - 16, "Fog of war", YesNo(s.fog));
	Fact(p, fx + 2 * fw, my2 + 16, fw - 16, "Explored map", YesNo(s.explored));
	Fact(p, fx, my2 + 68, fw - 16, "Civilizations", s.hideCivs == 1 ? Tr("Hidden until the game") : Tr("Visible"));
	Fact(p, fx + fw, my2 + 68, fw - 16, "Diplomacy", s.diplomacy == "locked" ? Tr("Locked") : s.diplomacy.empty() ? std::string("—") : Tr("Free"));
	Fact(p, fx + 2 * fw, my2 + 68, fw - 16, "Spectators",
		s.specDelay == 0 ? Tr("No spectators") : TrFmt("With a %s s delay", {std::to_string(s.specDelay > 0 ? s.specDelay : 120)}));

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
		Ghost(p, mid + 88, by, 240, 64, "Decline", kRkDecline, !v.answering, mx, my, 4);
		UseSharedFonts(p);
	}
	p.Font(3);
	p.Text(dx, by + 74, dw, 22, "If you decline or don't answer you can't search for 5 minutes; the wait doubles if you do it again.", pal::caption,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

int PaintRankedMark(Painter &p, int x, int y, int h)
{
	p.Font(4);
	const int w = p.CapsWidth("Ranked 1 vs 1", 2) + 24;
	p.Fill(x, y, w, h, RGB(0x3a, 0x12, 0x14));
	p.Fill(x, y, 3, h, pal::red);
	p.TextCaps(x + 3, y, w - 3, h, "Ranked 1 vs 1", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	return w;
}

}
