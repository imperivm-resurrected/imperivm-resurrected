// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <memory>

#include "ui/mpui_tourney.h"
#include "addr/state.h"
#include "core/leaguemaps.h"
#include "core/log.h"
#include "core/tournament.h"
#include "game/gameroom.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/leagueui.h"
#include "ui/modui.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {

Ui g_tourney;

namespace {

const DWORD kClaimRetryMs = 5000, kFlowTimeoutMs = 45000, kPresenceEveryMs = 30000, kJoinWaitMs = 6000;
// Not in the room the server gave after this long: it is taken as gone (Steam enters a room in a few seconds).
const DWORD kStaleAfterMs = 20000;

// Under g_lock: the tournament of an id, from the lists and the player's own.
std::string SlugOf(int64_t tournament)
{
	const TourneyListsView lists = LeagueTourneyLists();
	for (const std::vector<TourneySummary> *list : {&lists.open, &lists.live, &lists.finished}) {
		for (const TourneySummary &t : *list) {
			if (t.id == tournament) return t.slug;
		}
	}
	const TourneyMineView mine = LeagueTourneyMine();
	if (mine.mine.next.tournamentId == tournament) return mine.mine.next.slug;
	if (mine.mine.checkin.tournamentId == tournament) return mine.mine.checkin.slug;
	for (const TourneyMyEntry &e : mine.mine.entries) {
		if (e.tournamentId == tournament) return e.slug;
	}
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

// The room and the spectators' room of a match, from the pages read for the tab.
bool FindMatchRooms(int64_t match, uint64_t &lobby, uint64_t &spec)
{
	std::vector<std::string> slugs;
	if (!g_tourney.slug.empty()) slugs.push_back(g_tourney.slug);
	for (const TourneySummary &t : LeagueTourneyLists().live) slugs.push_back(t.slug);
	for (const std::string &slug : slugs) {
		const TourneyPageView page = LeagueTourneyPage(slug, 60000);
		for (const TourneyMatch &m : page.detail.matches) {
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

void Flash()
{
	const HWND hwnd = g_wakeWnd;
	if (!hwnd || GetForegroundWindow() == hwnd) return;
	FLASHWINFO fi = {sizeof(fi), hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 5, 0};
	FlashWindowEx(&fi);
}

std::string StageOf(const TourneyNotice &n)
{
	return StageText(n.stage, n.round);
}

// Under g_lock: the room for this match is made here, with the tournament's options.
void CreateRoom(const TourneyClaim &claim)
{
	RoomFlow &f = g_tourney.room;
	const TourneyMineView mine = LeagueTourneyMine();
	const TourneyNext &n = mine.mine.next;
	if (!claim.contentKey.empty() && claim.contentKey != SteamOwnGameplayKey()) {
		Toast(TrFmt("This tournament is played with %s: switch to that content profile first.", {n.contentName.empty() ? Tr("other content") : n.contentName}), true);
		f = RoomFlow();
		return;
	}
	OnlineCreateSettings s;
	const std::string stage = n.match == f.match ? StageText(n.stage, n.round) : std::string();
	s.name = n.match == f.match && !n.name.empty() ? n.name + " \xC2\xB7 " + stage + " \xC2\xB7 " + TrFmt("%s vs %s", {g_s.player, n.opponent.name}) : Tr("Tournament match");
	s.map = claim.map.Known() ? MapText(claim.map) : std::string("Chosen in the room");
	s.mode = "Tournament";
	s.maxPlayers = 2;
	s.visibility = 0;
	s.spectators = true;
	s.specDelay = TourneySpecDelay(claim.settings);
	s.leagueTag = TourneyRoomTag(f.match);
	g_s.hostSave = SaveEntry();
	g_s.hostSavePublished = false;
	g_s.savePlayersRead = false;
	g_s.savePlayers.clear();
	g_s.savePlaced.clear();
	SteamCreateLobby(s);
	g_s.roomLimit = 2;
	g_s.autoHost = true;
	f.phase = RoomFlow::kCreating;
	f.at = GetTickCount();
	f.map = claim.map;
	f.settings = claim.settings;
	LogI("league", "creating the room of tournament match %lld", static_cast<long long>(f.match));
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
				f.waitingFor = r.claim.by && r.claim.by == f.opponent ? LeagueTourneyMine().mine.next.opponent.name : std::string();
			}
			break;
		case TourneyOp::Register:
			if (!r.ok) Toast(ResultText(r), true);
			break;
		case TourneyOp::Presence: break;
		default: Toast(ResultText(r), !r.ok); break;
		}
	}
}

// Under g_lock.
void TakeNotices()
{
	TourneyNotice n;
	while (LeagueTourneyTakeNotice(n)) {
		switch (n.kind) {
		case TourneyNotice::MatchReady:
			Toast(TrFmt("%s, %s: your match against %s is ready. Enter the room before %s.", {n.name, StageOf(n), n.opponent.empty() ? Tr("your opponent") : n.opponent,
				HourText(n.deadlineAt)}));
			Flash();
			break;
		case TourneyNotice::CheckinOpen:
			Toast(TrFmt("The check-in of %s is open until %s.", {n.name, HourText(n.deadlineAt)}));
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
	}
	const DWORD now = GetTickCount();
	const bool mine = next.present && next.match == match;
	if (mine && (!k.presenceAt || now - k.presenceAt >= kPresenceEveryMs)) {
		k.presenceAt = now;
		LeagueTourneyPresence(match, false);
	}
	if (mine && g_s.inOnlineGame && !k.startedSent) {
		k.startedSent = true;
		k.presenceAt = now;
		LeagueTourneyPresence(match, true);
	}
	if (!g_s.inOnlineGame) k.startedSent = false;
	// Only the two players: anyone else who came in before the room filled up is sent out by the host.
	if (mine && g_s.room.owner && next.opponent.steamId) {
		for (const OnlineMember &m : g_s.room.members) {
			if (m.me || m.id == next.opponent.steamId || m.id == SteamOwnId() || !k.kicked.insert(m.id).second) continue;
			LogI("league", "%s is not a player of the tournament match; out of the room", m.name.c_str());
			SteamKick(m.id);
		}
	}
	if (!mine || !g_s.game.active || !g_s.game.host || g_s.game.busy || k.applied || k.tries >= 12 || now - k.at < 800) return;
	k.at = now;
	k.tries++;
	bool done = true;
	k.applying = true;
	for (int i = 0; i < kPresetCount; i++) {
		if (_stricmp(kPresets[i].name, next.settings.preset.c_str()) != 0) continue;
		// The hidden civilizations are the tournament's on top of the preset, which does not have them.
		if (MatchPreset(g_s.game.siteFlags, g_s.game.rules & ~kRuleBlindPick, HostOptionGetter(g_s.game)) != i) {
			g_s.dropKind = kDropPreset;
			g_s.dropIndex = 0;
			DoAction(kGDropPick, i);
			done = false;
		}
	}
	if (next.settings.hideCivs && !(g_s.game.rules & kRuleBlindPick)) {
		g_s.game.rules |= kRuleBlindPick;
		GameRoomSetRules(g_s.game.rules);
		done = false;
	}
	auto pick = [&](int id, const std::string &want) {
		if (want.empty()) return;
		for (GameRoomOption &o : g_s.game.options) {
			if (o.id != id) continue;
			bool byText = false;
			const int i = LeagueMapItemFor(o.keys, o.items, o.itemDisabled, want, byText);
			if (i < 0) {
				// The size waits for the type set just now, which decides the sizes the game offers. Otherwise the game has no such
				// map here: the host picks it by hand.
				if (done || id != 0x01000008) {
					k.mapAllowed = true;
					LogW("league", "the tournament's map \"%s\" is not in the room's list (0x%x)", want.c_str(), id);
				}
				return;
			}
			if (byText) LogW("league", "no map id behind the room's items; \"%s\" picked by its name (\"%s\")", want.c_str(), o.items[i].c_str());
			if (o.sel != i) {
				o.sel = i;
				Predict(kPredOptSel, o.id, i);
				GameRoomSetOption(o.id, i);
				done = false;
			}
			return;
		}
	};
	pick(0x1000, next.map.name);
	pick(0x01000008, next.map.size);
	k.applying = false;
	if (done) {
		k.applied = true;
		LogI("league", "the tournament's options are set in the room%s", k.mapAllowed ? " (the map is the host's to pick)" : "");
	}
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
const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

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

std::string DurationText(int64_t ms)
{
	const int64_t min = std::max<int64_t>(0, ms / 60000);
	if (min < 60) return TrFmt("%s min", {std::to_string(min)});
	return TrFmt("%s h %s min", {std::to_string(min / 60), std::to_string(min % 60)});
}

std::string StageText(TourneyStage stage, int round)
{
	const char *name = TourneyStageName(stage);
	return *name ? Tr(name) : TrFmt("Round %s", {std::to_string(round)});
}

std::string FormatText(const TourneySummary &t)
{
	return Tr(t.format == "single" || t.format.empty() ? "Single elimination" : t.format) + " \xC2\xB7 " + Tr("1 vs 1");
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
	std::string out = LeagueMapText(m.name);
	if (!m.size.empty()) out += " \xC2\xB7 " + LeagueMapText(m.size);
	return out;
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
		default: return std::string();
		}
	}
	const std::string &e = r.error;
	if (e == "not_linked") return Tr("Link your league account first.");
	if (e == "closed") return Tr("Sign-up for this tournament is closed.");
	if (e == "suspended") return TrFmt("Two no-shows in 30 days: you can sign up for official tournaments again from %s.", {WhenText(r.until)});
	if (e == "already_signed") return Tr("You are already signed up.");
	if (e == "checkin_closed") return Tr("The check-in is not open.");
	if (e == "wrong_content") {
		return TrFmt("This tournament is played with %s: switch to that content profile first.", {r.contentName.empty() ? Tr("other content") : r.contentName});
	}
	if (e == "not_ready") return Tr("The match is not waiting for players.");
	if (e == "too_early") return Tr("The waiting time is not over yet.");
	if (e == "not_in_room") return Tr("You have to be in the room to claim the win.");
	if (e == "opponent_present") return Tr("Your opponent is in the room.");
	if (e == "not_claimed") return Tr("Someone else is opening the room.");
	if (e == "not_found") return Tr("The league could not find it. It may have changed: try again.");
	if (!r.status) return Tr("Couldn't reach the league server. Check your connection and try again.");
	return Tr("The league server refused the request. Try again later.");
}

const TourneyMyEntry *MyEntry(const TourneyMineView &mine, int64_t tournament)
{
	return mine.loaded ? TourneyMyEntryOf(mine.mine, tournament) : nullptr;
}

const TourneySummary *Featured(const TourneyListsView &lists)
{
	for (const TourneySummary &t : lists.open) {
		if (t.official) return &t;
	}
	for (const TourneySummary &t : lists.live) {
		if (t.official) return &t;
	}
	if (!lists.open.empty()) return &lists.open.front();
	if (!lists.live.empty()) return &lists.live.front();
	for (const TourneySummary &t : lists.finished) {
		if (t.official) return &t;
	}
	return nullptr;
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

void PaintName(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, COLORREF ink, int mx, int my)
{
	const LeagueBadge badge = id ? PlayerBadgeOf(id) : LeagueBadge();
	const int nameW = std::max(20, w - LeagueBadgeRoom(badge));
	p.raw = true;
	p.Text(x, y, nameW, h, name, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DrawBadgeAfterName(p, x, y, nameW, h, name, badge, mx, my);
	p.raw = false;
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
	switch (u.screen) {
	case kScreenPage: PaintPageScreen(p, st, u, out); break;
	case kScreenMatch: PaintMatchScreen(p, st, u); break;
	case kScreenChampion: PaintChampionScreen(p, st, u); break;
	default: PaintListScreen(p, st, u, out); break;
	}
	if (u.disputeMatch) {
		UseSharedFonts(p);
		PaintDisputeDialog(p, st, u);
		UseScreenFonts(p);
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_tourney.listArea = out.listArea;
	g_tourney.bracketArea = out.bracketArea;
	g_tourney.sideArea = out.sideArea;
}

bool PaintTourneyHeader(Painter &p, const State &st)
{
	Ui u;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		u = g_tourney;
	}
	switch (u.screen) {
	case kScreenPage: return PaintPageHeader(p, st, u);
	case kScreenMatch: return PaintMatchHeader(p, st, u);
	case kScreenChampion: return PaintChampionHeader(p, st, u);
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
	case kSubOpen: {
		const std::string slug = SlugOf(value);
		if (slug.empty()) break;
		u.slug = slug;
		u.screen = kScreenPage;
		u.pageTab = 0;
		u.bracketX = u.bracketY = u.sideScroll = 0;
		break;
	}
	case kSubBack: TourneyBack(); break;
	case kSubPageTab:
		u.pageTab = static_cast<int>(value);
		u.sideScroll = 0;
		break;
	case kSubSignUp:
		if (!LeagueLinked()) Toast("Link your league account first.");
		else LeagueTourneySignUp(value);
		break;
	case kSubWithdraw: {
		const int64_t tournament = value;
		ModUiAsk("Withdraw from the tournament", "Once your match is ready, withdrawing counts as a no-show. Withdraw anyway?", "Withdraw",
			[tournament] { LeagueTourneyWithdraw(tournament); });
		break;
	}
	case kSubCheckIn: {
		const TourneyListsView lists = LeagueTourneyLists();
		const TourneySummary *t = SummaryOf(lists, value);
		if (t && !t->contentKey.empty() && t->contentKey != SteamOwnGameplayKey()) {
			Toast(TrFmt("This tournament is played with %s: switch to that content profile first.", {t->contentName.empty() ? Tr("other content") : t->contentName}), true);
			break;
		}
		LeagueTourneyCheckIn(value);
		break;
	}
	case kSubMatch:
		u.screen = kScreenMatch;
		LeagueTourneyMineRefresh();
		break;
	case kSubGo: {
		const TourneyNext next = LeagueTourneyMine().mine.next;
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
			Toast(TrFmt("This tournament is played with %s: switch to that content profile first.", {next.contentName.empty() ? Tr("other content") : next.contentName}), true);
			break;
		}
		if (u.room.phase != RoomFlow::kIdle && u.room.match == value) break;
		u.room = RoomFlow();
		u.room.phase = RoomFlow::kClaiming;
		u.room.match = value;
		u.room.at = GetTickCount();
		u.room.opponent = next.opponent.steamId;
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
		u.join.watch = true;
		u.join.at = GetTickCount();
		SteamRefreshLobbies();
		break;
	}
	case kSubNoShow: LeagueTourneyClaimNoShow(value); break;
	case kSubDispute:
		u.disputeMatch = value;
		u.disputeText.clear();
		FocusField(kTourneyField);
		break;
	case kSubDisputeSend: TourneyFieldEnter(); break;
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
	case kSubField: FocusField(kTourneyField); break;
	case kSubPan: {
		const int page = std::max(200, static_cast<int>((u.bracketArea.r.right - u.bracketArea.r.left) / std::max(0.1f, g_s.uiScale)) * 3 / 4);
		u.bracketX = std::max(0, std::min(u.bracketArea.maxX, u.bracketX + (value ? page : -page)));
		break;
	}
	default: break;
	}
}

bool TourneyBack()
{
	Ui &u = g_tourney;
	if (u.disputeMatch) {
		u.disputeMatch = 0;
		g_s.focus = kNoField;
		return true;
	}
	if (u.screen == kScreenList) return false;
	u.screen = u.screen == kScreenPage || u.slug.empty() ? kScreenList : kScreenPage;
	return true;
}

bool TourneyWheel(int delta, bool shift)
{
	Ui &u = g_tourney;
	const POINT at = {g_s.mouseX, g_s.mouseY};
	const int step = -delta / WHEEL_DELTA * 60;
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
	return false;
}

bool TourneyScrollTo(int bar, int pos)
{
	Ui &u = g_tourney;
	switch (bar) {
	case kBarList: u.listScroll = std::max(0, std::min(u.listArea.max, pos)); return true;
	case kBarBracketY: u.bracketY = std::max(0, std::min(u.bracketArea.max, pos)); return true;
	case kBarBracketX: u.bracketX = std::max(0, std::min(u.bracketArea.maxX, pos)); return true;
	case kBarSide: u.sideScroll = std::max(0, std::min(u.sideArea.max, pos)); return true;
	default: return false;
	}
}

std::string &TourneyFieldText()
{
	return g_tourney.disputeText;
}

void TourneyFieldEnter()
{
	Ui &u = g_tourney;
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

bool TourneyBlocksAction(int action, int arg)
{
	if (!g_s.game.active || !g_s.room.id || !TourneyMatchOfTag(g_s.room.info.leagueTag) || g_tourney.lock.applying) return false;
	bool blocked = false;
	switch (action) {
	case kGRuleToggle:
	case kGSiteToggle:
	case kGOptToggle: blocked = true; break;
	case kGDropOpen: {
		const int kind = arg & kDropKindMask, index = arg >> kDropKindBits;
		if (kind == kDropOption) {
			const int id = index >= 0 && index < static_cast<int>(g_s.game.options.size()) ? g_s.game.options[index].id : 0;
			blocked = !(g_tourney.lock.mapAllowed && (id == 0x1000 || id == 0x01000008));
		}
		else blocked = kind == kDropPreset || kind == kDropSites || kind == kDropModOption || kind == kDropSpectators || kind == kDropSlot;
		break;
	}
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
	const TourneyMineView mine = LeagueTourneyMine();
	return !(mine.mine.next.present && mine.mine.next.match == match);
}

void TourneyGameThreadTick()
{
	const TourneyNext next = LeagueTourneyMine().mine.next;
	std::lock_guard<std::mutex> lock(g_lock);
	TakeNotices();
	TakeResults();
	if (!g_s.ready) return;
	StepJoin();
	StepRoom();
	StepInRoom(next);
}
