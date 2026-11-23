// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/profileui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/leagueprofile.h"
#include "core/log.h"
#include "game/gamestats.h"
#include "game/playerini.h"
#include "game/profiles.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_profile.h"
#include "net/leagueclient_seasons.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leaguemastery.h"
#include "ui/leagueui.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/patronui.h"
#include "ui/profileui_view.h"
#include "ui/widgets.h"

using namespace ui;
using namespace profileui;

namespace {

const DWORD kReadEveryMs = 250, kCloseTimeoutMs = 2000;

struct CachedCard {
	FILETIME stamp = {};
	uint64_t size = 0;
	bool found = false;
	Card card;
};

struct State {
	void *dlg = nullptr;
	View view;
	std::vector<std::string> dirs;
	std::string openedWith;
	std::vector<RankStep> ranks;
	std::map<std::string, CachedCard> cache;
	std::vector<Hit> hits;
	std::vector<PaneArea> panes;
	bool mouseDown = false;
	DWORD lastRead = 0, closedAt = 0;
	// The game window may not send double clicks, so they are told apart here.
	DWORD lastClick = 0;
	int lastClickRow = -1;
	int wheelRest = 0;
	// A press on the row of chips when it scrolls: a drag moves the row, a click without one chooses the chip on release.
	bool chipPress = false, chipDragged = false;
	int chipStartX = 0, chipStartPos = 0, chipAction = kActNone, chipArg = 0;
};

// Another player's profile, over the multiplayer screen: whose, the matches found when it opened (one found closes it, so the call to
// accept it shows), and the answer of the league it was built from.
struct Public {
	bool open = false;
	uint64_t id = 0;
	std::string steamId, name;
	bool serialKnown = false;
	unsigned foundSerial = 0;
	DWORD checkedAt = 0;
	LeagueProfileState builtState = LeagueProfileState::None;
	std::shared_ptr<const LeagueProfile> built;
	State screen;
};

std::mutex g_lock;
State g_s;
Public g_pub;
Canvas g_canvas;

std::string LowerDir(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

bool ReadText(const std::string &path, std::string &out)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
	char buf[8192];
	size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < 16u * 1024 * 1024) out.append(buf, n);
	fclose(f);
	return true;
}

Card BuildCard(const ProfileSlot &slot, const std::vector<RankStep> &ranks)
{
	Card c;
	c.name = slot.name;
	std::string text;
	if (!ReadText(ProfileIniPath(slot.dir), text)) {
		c.readable = false;
		return c;
	}
	const PlayerIni ini = ParsePlayerIni(text);
	c.summary = SummarizeProfile(ini, ranks);
	if (c.summary.rank >= 0) c.rankName = ranks[c.summary.rank].name;
	if (c.summary.nextRank >= 0) c.nextRankName = ranks[c.summary.nextRank].name;
	c.favoriteCiv = ProfileCivName(c.summary.favoriteRace);
	if (!ini.favoriteUnit.empty()) c.favoriteUnit = ProfileUnitName(ini.favoriteUnit);
	if (!c.summary.bestUnit.empty()) c.bestUnit = ProfileUnitName(c.summary.bestUnit);
	const std::vector<MatchTag> tags = LeagueGameTags();
	const std::vector<MatchNote> notes = GameStatsNotes();
	for (const PlayedGame &g : RecentGames(ini, ini.played.size())) {
		GameRow r;
		r.day = g.day;
		r.month = g.month;
		r.year = g.year;
		r.hour = g.hour;
		r.minute = g.minute;
		r.won = !g.lost;
		r.multi = g.multi;
		if (!tags.empty() || !notes.empty()) {
			// The game records its local time; the notes are in seconds since 1970.
			tm local = {};
			local.tm_year = g.year - 1900;
			local.tm_mon = g.month - 1;
			local.tm_mday = g.day;
			local.tm_hour = g.hour;
			local.tm_min = g.minute;
			local.tm_isdst = -1;
			const time_t at = mktime(&local);
			if (at != static_cast<time_t>(-1)) {
				if (g.multi) r.league = MatchLeagueOf(tags, static_cast<int64_t>(at), g.durationMs);
				if (const MatchNote *n = MatchNoteOf(notes, static_cast<int64_t>(at))) {
					r.goldSpent = n->goldSpent;
					r.scriptKills = n->scriptKills;
					r.scriptDeaths = n->scriptDeaths;
				}
			}
		}
		r.civ = ProfileCivName(g.race);
		r.race = MatchCivKey(g.race, !r.civ.empty());
		if (!g.levelMaxUnit.empty()) r.bestUnit = ProfileUnitName(g.levelMaxUnit);
		r.bestLevel = g.levelMax;
		r.durationMs = g.durationMs;
		r.kills = g.unitsKilled;
		r.losses = g.unitsLost;
		r.produced = g.unitsProduced;
		r.armyMax = g.unitsMax;
		r.gold = g.gold;
		r.food = g.food;
		r.damageDealt = g.damageInflicted;
		r.damageTaken = g.damageTaken;
		c.games.push_back(r);
	}
	return c;
}

void KeepVisible(View &v)
{
	const int count = static_cast<int>(v.cards.size());
	if (v.selected >= 0) {
		if (v.selected < v.scroll) v.scroll = v.selected;
		if (v.selected >= v.scroll + ListRows(v)) v.scroll = v.selected - ListRows(v) + 1;
	}
	v.scroll = std::max(0, std::min(v.scroll, std::max(0, count - ListRows(v))));
}

// On the game thread: reads the game's dialog and the files of the profiles that changed.
void Refresh()
{
	void *dlg = ProfileDialogOpen();
	ProfileDialogState native;
	if (!dlg || !ProfileDialogRead(native)) return;
	bool fresh = false;
	std::map<std::string, CachedCard> cache;
	std::vector<RankStep> ranks;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		fresh = g_s.dlg != dlg;
		if (!fresh) {
			cache = g_s.cache;
			ranks = g_s.ranks;
		}
	}
	if (fresh) ranks = ProfileGameRanks();
	std::vector<Card> cards;
	std::vector<std::string> dirs;
	std::map<std::string, CachedCard> kept;
	for (size_t i = 0; i < native.slots.size(); i++) {
		const ProfileSlot &slot = native.slots[i];
		const std::string key = LowerDir(slot.dir);
		WIN32_FILE_ATTRIBUTE_DATA fa = {};
		const bool found = GetFileAttributesExA(ProfileIniPath(slot.dir).c_str(), GetFileExInfoStandard, &fa) != 0;
		const uint64_t size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
		auto it = cache.find(key);
		const bool same = it != cache.end() && it->second.found == found && it->second.size == size &&
			CompareFileTime(&it->second.stamp, &fa.ftLastWriteTime) == 0 && it->second.card.name == slot.name;
		CachedCard entry;
		if (same) entry = it->second;
		else {
			entry.stamp = fa.ftLastWriteTime;
			entry.size = size;
			entry.found = found;
			entry.card = BuildCard(slot, ranks);
		}
		entry.card.active = static_cast<int>(i) == native.active;
		cards.push_back(entry.card);
		dirs.push_back(slot.dir);
		kept[key] = entry;
	}
	const uint64_t me = SteamOwnId();
	std::shared_ptr<const OnlineAvatar> avatar = me ? SteamAvatar(me) : nullptr;
	const PatronInfo patron = me ? PatronOf(me) : PatronInfo();
	if (me) PatronsWatch({me});
	const LeagueBadge badge = me ? PlayerBadgeOf(me) : LeagueBadge();
	const bool linked = me && LeagueLinked();
	if (linked) LeagueMeRefresh();
	const LeagueMeView league = linked ? LeagueMeState() : LeagueMeView();

	std::lock_guard<std::mutex> lock(g_lock);
	if (fresh) {
		g_s = State();
		g_s.dlg = dlg;
		g_s.ranks = ranks;
		g_s.openedWith = native.active >= 0 ? native.slots[native.active].dir : std::string();
		LogI("profile", "%d players, %d ranks", static_cast<int>(cards.size()), static_cast<int>(ranks.size()));
	}
	View &v = g_s.view;
	const bool moved = v.selected != native.selected || v.cards.size() != cards.size();
	if (v.selected != native.selected) {
		v.expanded = -1;
		v.historyScroll = 0;
		v.civ = kCivAll;
	}
	v.cards = std::move(cards);
	v.selected = native.selected;
	v.avatar = avatar;
	v.patron = patron;
	v.badge = badge;
	v.linked = linked;
	v.meLoaded = league.loaded;
	v.me = league.me;
	v.rankCount = static_cast<int>(g_s.ranks.size());
	SYSTEMTIME now;
	GetLocalTime(&now);
	v.todayYear = now.wYear;
	v.todayMonth = now.wMonth;
	v.todayDay = now.wDay;
	if (v.expanded >= 0 && (!SelectedCard(v) || v.expanded >= static_cast<int>(SelectedCard(v)->games.size()))) v.expanded = -1;
	if (v.civ != kCivAll) {
		const Card *c = SelectedCard(v);
		const bool played = c && std::any_of(c->games.begin(), c->games.end(), [&](const GameRow &g) { return g.race == v.civ; });
		if (!played) v.civ = kCivAll;
	}
	v.historyScroll = std::max(0, std::min(v.historyScroll, HistoryMaxScroll(v)));
	if (moved) KeepVisible(v);
	g_s.dirs = std::move(dirs);
	g_s.cache = std::move(kept);
	g_s.lastRead = GetTickCount();
}

// What the screen needs of a profile from the league: its card, its standing and its civilizations. The civilizations take their place
// in the league's list as their key; a game's civilization the list doesn't have gets one after them.
struct PublicBuilt {
	Card card;
	LeagueMe me;
	PublicProfile pub;
};

PublicBuilt BuildPublic(const LeagueProfile &pr, const std::string &fallbackName)
{
	PublicBuilt b;
	std::map<std::string, int> keys;
	uint64_t armySum = 0;
	int armyGames = 0;
	for (size_t i = 0; i < pr.civs.size(); i++) {
		const LeagueProfileCiv &civ = pr.civs[i];
		PublicCiv pc;
		pc.race = civ.civ.empty() ? kCivUnknown : static_cast<int>(i);
		pc.name = LeagueCivName(civ.civ);
		pc.record = LeagueProfileRecord(civ.sums, pc.race);
		if (!civ.civ.empty()) keys[civ.civ] = pc.race;
		armySum += civ.sums.army;
		armyGames += civ.sums.statsGames;
		b.pub.civs.push_back(pc);
	}
	b.pub.all = LeagueProfileRecord(pr.totals, kCivAll);
	// The totals have no army: the civilizations' do.
	b.pub.all.army = armyGames > 0 ? static_cast<double>(armySum) / armyGames : 0;
	b.pub.frame = pr.frame;
	b.pub.title = pr.title;

	Card &c = b.card;
	c.name = pr.name.empty() ? fallbackName : pr.name;
	c.readable = true;
	c.summary.games = pr.totals.games;
	c.summary.multiGames = pr.totals.games;
	c.summary.multiWins = pr.totals.wins;
	c.summary.playedMs = static_cast<uint64_t>(pr.totals.minutes * 60000);
	c.summary.kills = pr.totals.kills;
	for (const LeagueProfileGame &g : pr.recent) {
		GameRow r;
		const time_t t = static_cast<time_t>(g.at / 1000);
		tm local = {};
		if (g.at > 0 && localtime_s(&local, &t) == 0) {
			r.day = local.tm_mday;
			r.month = local.tm_mon + 1;
			r.year = local.tm_year + 1900;
			r.hour = local.tm_hour;
			r.minute = local.tm_min;
		}
		r.won = g.won;
		r.multi = true;
		r.league = g.cup ? kMatchCup : kMatchLeague;
		r.remote = true;
		r.civ = LeagueCivName(g.civ);
		if (g.civ.empty()) r.race = kCivUnknown;
		else {
			auto it = keys.find(g.civ);
			if (it == keys.end()) it = keys.insert({g.civ, static_cast<int>(pr.civs.size() + keys.size())}).first;
			r.race = it->second;
		}
		r.durationMs = g.durationMs;
		r.kills = g.kills;
		r.losses = g.deaths;
		r.queue = g.cup ? std::string() : g.queue;
		r.cupName = g.cup ? (g.cupName.empty() ? Tr("Tournament") : g.cupName) : std::string();
		r.map = g.map;
		for (const auto &o : g.opponents) {
			if (o.second.empty()) continue;
			r.opponents += (r.opponents.empty() ? "" : ", ") + o.second;
		}
		c.games.push_back(r);
	}

	LeagueMe &me = b.me;
	me.steamId = pr.steamId;
	me.name = c.name;
	me.ranked = pr.ranked;
	me.ranked2v2 = pr.ranked2v2;
	// Nothing of the showcase is marked as worn.
	me.badgeKind = "none";
	for (const LeagueProfileTrophy &t : pr.trophies) {
		LeagueBadge badge;
		badge.kind = LeagueBadgeKind::Trophy;
		badge.metal = t.metal;
		badge.title = t.title;
		badge.season = t.season;
		me.badges.push_back(badge);
	}
	me.cupTitles = pr.cupTitles;
	return b;
}

void ClampPublic(View &v)
{
	const std::vector<int> modes = ChartModes(v), metrics = ChartMetrics(v);
	if (std::find(modes.begin(), modes.end(), v.chartMode) == modes.end()) v.chartMode = kModeAll;
	if (std::find(metrics.begin(), metrics.end(), v.metric) == metrics.end()) v.metric = kMetricKills;
	if (v.civ != kCivAll && !CivKnown(v, v.civ)) v.civ = kCivAll;
	if (v.expanded >= 0 && (!SelectedCard(v) || v.expanded >= static_cast<int>(SelectedCard(v)->games.size()))) v.expanded = -1;
	v.historyScroll = std::max(0, std::min(v.historyScroll, HistoryMaxScroll(v)));
}

// On the game thread: asks the league for the profile, and closes it when the multiplayer screen goes or a match is found.
void PublicTick()
{
	std::string steamId, name;
	uint64_t id = 0;
	bool check = false, serialKnown = false;
	unsigned serial = 0;
	LeagueProfileState builtState = LeagueProfileState::None;
	std::shared_ptr<const LeagueProfile> built;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_pub.open) return;
		steamId = g_pub.steamId;
		name = g_pub.name;
		id = g_pub.id;
		check = GetTickCount() - g_pub.checkedAt >= kReadEveryMs;
		if (check) g_pub.checkedAt = GetTickCount();
		serialKnown = g_pub.serialKnown;
		serial = g_pub.foundSerial;
		builtState = g_pub.builtState;
		built = g_pub.built;
	}
	if (!check) return;
	const unsigned found = LeagueRankedState().foundSerial;
	if (!MpUiIsOpen() || (serialKnown && found != serial)) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_pub = Public();
		LogI("profile", "the public profile closed with the multiplayer screen or a match found");
		return;
	}
	LeagueProfileRequest(steamId);
	const LeagueProfileView pv = LeagueProfileOf(steamId);
	PatronsWatch({id});
	const std::shared_ptr<const OnlineAvatar> avatar = SteamAvatar(id);
	const PatronInfo patron = PatronOf(id);
	const bool rebuild = pv.state != builtState || pv.profile != built;
	PublicBuilt b;
	if (rebuild && pv.profile) b = BuildPublic(*pv.profile, name);
	const LeagueBadge badge = pv.profile && pv.profile->badge.Shown() ? pv.profile->badge : PlayerBadgeOf(id);

	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_pub.open || g_pub.steamId != steamId) return;
	if (!g_pub.serialKnown) {
		g_pub.serialKnown = true;
		g_pub.foundSerial = found;
	}
	View &v = g_pub.screen.view;
	if (rebuild) {
		g_pub.builtState = pv.state;
		g_pub.built = pv.profile;
		if (pv.profile) {
			v.cards = {b.card};
			v.me = b.me;
			const bool on = v.pub.on;
			const uint64_t pid = v.pub.id;
			v.pub = b.pub;
			v.pub.on = on;
			v.pub.id = pid;
		}
	}
	v.pub.state = pv.state == LeagueProfileState::None ? LeagueProfileState::Loading : pv.state;
	if (v.pub.state != LeagueProfileState::Ready) v.cards[0].name = pv.profile && !pv.profile->name.empty() ? pv.profile->name : name;
	v.meLoaded = v.pub.state == LeagueProfileState::Ready;
	v.avatar = avatar;
	v.patron = patron;
	v.badge = badge;
	SYSTEMTIME now;
	GetLocalTime(&now);
	v.todayYear = now.wYear;
	v.todayMonth = now.wMonth;
	v.todayDay = now.wDay;
	ClampPublic(v);
}

struct Command {
	int action = kActNone;
	int arg = 0;
};

const PaneArea *ChipRow(const State &st)
{
	for (const PaneArea &pane : st.panes) {
		if (pane.id == kPaneCivChips && pane.max > 0) return &pane;
	}
	return nullptr;
}

// Design units of the row per pixel of the screen.
int ChipRowPos(const PaneArea &row, int startPos, int dx)
{
	const int px = std::max(1, static_cast<int>(row.r.right - row.r.left));
	return startPos - static_cast<int>(static_cast<long long>(dx) * row.page / px);
}

bool OverHistory(const State &st, POINT at)
{
	for (const Hit &h : st.hits) {
		if (h.action == kActHistory && PtInRect(&h.r, at)) return true;
	}
	return false;
}

// Changes of the screen alone happen here, under the lock; whatever touches the game is left in `cmd` for Execute.
void Act(View &v, int action, int arg, Command &cmd)
{
	// Another player's profile only shows: what changes the player in use or their league account is not for it.
	if (v.pub.on) {
		switch (action) {
		case kActBack:
		case kActPatron:
		case kActTab:
		case kActCiv:
		case kActGloryQueue:
		case kActMetric:
		case kActChartMode:
		case kActChartRange:
		case kActOutcome:
		case kActBar:
		case kActGame: break;
		default: return;
		}
	}
	switch (action) {
	case kActPick:
		if (arg != v.selected) {
			cmd.action = kActPick;
			cmd.arg = arg;
		}
		break;
	case kActUse:
		// The player in use has nothing to change to: the screen just closes.
		if (SelectedIsActive(v)) cmd.action = kActBack;
		else if (v.selected >= 0 && v.selected < static_cast<int>(v.cards.size())) {
			cmd.action = kActUse;
			cmd.arg = v.selected;
		}
		break;
	case kActBack:
	case kActPatron:
	case kActLeagueLink:
	case kActLeagueOpen:
	case kActLeagueCancel:
	case kActLeagueUnlink:
		cmd.action = action;
		break;
	case kActLeagueBadge:
	case kActLeagueFrame:
	case kActLeagueTitle:
		cmd.action = action;
		cmd.arg = arg;
		break;
	case kActLeague:
		v.leagueOpen = true;
		v.panes.Reset();
		cmd.action = action;
		break;
	case kActLeagueClose: v.leagueOpen = false; break;
	case kActTab:
		v.tab = arg == kTabLeague || arg == kTabCivs ? arg : kTabGeneral;
		v.panes.Reset();
		v.historyScroll = 0;
		v.revealCiv = true;
		break;
	case kActCiv:
		v.civ = arg == v.civ ? kCivAll : arg;
		v.historyScroll = 0;
		v.revealCiv = true;
		break;
	case kActGloryQueue: v.gloryQueue = arg ? 1 : 0; break;
	case kActMetric: v.metric = std::max(0, std::min(arg, kMetricCount - 1)); break;
	case kActChartMode: v.chartMode = arg; break;
	case kActChartRange: v.chartRange = arg; break;
	case kActOutcome:
		v.outcome = arg;
		v.historyScroll = 0;
		break;
	case kActBar: HistoryReveal(v, arg); break;
	case kActGame:
		v.expanded = v.expanded == arg ? -1 : arg;
		v.historyScroll = std::max(0, std::min(v.historyScroll, HistoryMaxScroll(v)));
		break;
	default: break;
	}
}

void Execute(const Command &cmd)
{
	switch (cmd.action) {
	case kActPick:
		ProfileDialogSelect(cmd.arg);
		Refresh();
		break;
	case kActUse:
	case kActBack: {
		int index = cmd.arg;
		if (cmd.action == kActBack) {
			std::lock_guard<std::mutex> lock(g_lock);
			index = g_s.view.selected;
			for (size_t i = 0; i < g_s.dirs.size(); i++) {
				if (!g_s.openedWith.empty() && _stricmp(g_s.dirs[i].c_str(), g_s.openedWith.c_str()) == 0) index = static_cast<int>(i);
			}
		}
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.closedAt = GetTickCount();
		}
		if (!ProfileDialogPlay(index)) {
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.closedAt = 0;
		}
		break;
	}
	case kActPatron: OpenPatronPage(); break;
	case kActLeague: LeagueMeRefresh(true); break;
	case kActLeagueLink: LeagueLinkStart(); break;
	case kActLeagueOpen: LeagueOpenLinkPage(); break;
	case kActLeagueCancel: LeagueLinkCancel(); break;
	case kActLeagueUnlink: LeagueUnlink(); break;
	case kActLeagueBadge: LeagueChooseBadgeAt(cmd.arg); break;
	case kActLeagueFrame: LeagueCosmeticsSet(true, cmd.arg, false, -1); break;
	case kActLeagueTitle: LeagueCosmeticsSet(false, -1, true, cmd.arg); break;
	default: break;
	}
}

// Back from another player's profile is the multiplayer screen as it was left.
void ExecutePublic(const Command &cmd)
{
	switch (cmd.action) {
	case kActBack: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_pub = Public();
		break;
	}
	case kActPatron: OpenPatronPage(); break;
	default: break;
	}
}

// Under the lock, for the screen of `st`.
bool Handle(State &st, UINT msg, WPARAM wp, LPARAM lp, Command &cmd)
{
	View &v = st.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	ScrollTo to;
	const bool onBar = ScrollbarMouse(v.bar, st.hits, msg, wp, at, to);
	if (to.id == kBarList) v.scroll = to.pos;
	else if (to.id == kBarHistory) v.historyScroll = to.pos;
	else v.panes.Apply(st.panes, to);
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (st.chipPress && !(wp & MK_LBUTTON)) st.chipPress = false;
		if (st.chipPress) {
			const int dx = at.x - st.chipStartX;
			if (!st.chipDragged && std::abs(dx) >= GetSystemMetrics(SM_CXDRAG)) st.chipDragged = true;
			const PaneArea *row = ChipRow(st);
			if (st.chipDragged && row) v.panes.Set(kPaneCivChips, ChipRowPos(*row, st.chipStartPos, dx), row->max);
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		if (onBar) return true;
		st.mouseDown = true;
		int arg = 0;
		const int action = HitActionAt(st.hits, at, arg, kActNone);
		if (action == kActCivScroll) {
			if (const PaneArea *row = ChipRow(st)) v.panes.Set(kPaneCivChips, v.panes.Of(kPaneCivChips) + arg * row->page * 2 / 3, row->max);
			return true;
		}
		if (const PaneArea *row = v.leagueOpen ? nullptr : ChipRow(st)) {
			if (PtInRect(&row->r, at)) {
				st.chipPress = true;
				st.chipDragged = false;
				st.chipStartX = at.x;
				st.chipStartPos = v.panes.Of(kPaneCivChips);
				st.chipAction = action;
				st.chipArg = arg;
				return true;
			}
		}
		const DWORD now = GetTickCount();
		const bool twice = action == kActPick && arg == st.lastClickRow && now - st.lastClick <= GetDoubleClickTime();
		st.lastClickRow = action == kActPick && !twice ? arg : -1;
		st.lastClick = now;
		if (twice && arg == v.selected) Act(v, kActUse, 0, cmd);
		else Act(v, action, arg, cmd);
		return true;
	}
	case WM_LBUTTONUP: {
		// Over the multiplayer screen, the release of a press made there is its own.
		const bool ours = st.mouseDown;
		st.mouseDown = false;
		if (st.chipPress && !st.chipDragged) Act(v, st.chipAction, st.chipArg, cmd);
		st.chipPress = false;
		return ours || !v.pub.on;
	}
	case WM_MOUSEWHEEL: {
		// The wheel's position comes in screen coordinates; the last move tells where the mouse is over the screen.
		if (v.panes.Wheel(st.panes, {v.mouseX, v.mouseY}, GET_WHEEL_DELTA_WPARAM(wp)) || v.leagueOpen) return true;
		const int steps = WheelSteps(st.wheelRest, GET_WHEEL_DELTA_WPARAM(wp));
		if (OverHistory(st, {v.mouseX, v.mouseY})) {
			v.historyScroll = std::max(0, std::min(v.historyScroll - steps * 3, HistoryMaxScroll(v)));
		}
		else {
			v.scroll -= steps;
			v.scroll = std::max(0, std::min(v.scroll, std::max(0, static_cast<int>(v.cards.size()) - ListRows(v))));
		}
		return true;
	}
	case WM_KEYDOWN:
	case WM_CHAR:
		if (msg == WM_CHAR) return true;
		if (v.leagueOpen) {
			if (wp == VK_ESCAPE || wp == VK_RETURN) v.leagueOpen = false;
			return true;
		}
		switch (wp) {
		case VK_ESCAPE: Act(v, kActBack, 0, cmd); break;
		case VK_RETURN: Act(v, kActUse, 0, cmd); break;
		case VK_UP:
		case VK_DOWN: {
			const int count = static_cast<int>(v.cards.size());
			const int next = std::max(0, std::min(count - 1, v.selected + (wp == VK_UP ? -1 : 1)));
			if (count) Act(v, kActPick, next, cmd);
			break;
		}
		default: break;
		}
		return true;
	case WM_KEYUP:
		return true;
	default:
		return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	}
}

}

namespace {

// Paints the screen of `st` (the player's own, or another player's over the multiplayer screen) and keeps what it painted, unless the
// screen closed meanwhile (`still`, asked under the lock).
template <class Still> void DrawScreen(State &st, Still still, uint32_t *buf, int w, int h, View view, std::vector<Hit> previous, bool mouseDown)
{
	const ScreenView screen = ScreenViewFor(w, h);
	view.screenW = screen.w;
	view.screenH = screen.h;
	PanelRender r(g_canvas, buf, w, h);
	if (!BeginFullScreen(r, screen)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintProfileScreen(r.p, view);
	int chipLeft = 0, chipW = 0;
	const bool reveal = view.revealCiv && CivChipSpan(r.p, view, chipLeft, chipW);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	if (!still()) return;
	if (reveal && st.view.civ == view.civ) {
		st.view.panes.Reveal(r.panes, kPaneCivChips, chipLeft - 24, chipW + 48);
		st.view.revealCiv = false;
	}
	// Kept where it was painted, so the arrows, the wheel and a drag start from what shows after a resize or a change of player.
	for (const PaneArea &pane : r.panes) st.view.panes.Set(pane.id, st.view.panes.Of(pane.id), pane.max);
	st.hits = std::move(r.hits);
	st.panes = std::move(r.panes);
	View &v = st.view;
	if (v.screenW != screen.w || v.screenH != screen.h) {
		v.screenW = screen.w;
		v.screenH = screen.h;
		KeepVisible(v);
		v.historyScroll = std::max(0, std::min(v.historyScroll, HistoryMaxScroll(v)));
	}
}

bool PublicOpen()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_pub.open;
}

}

void ProfileUiOpenPublic(uint64_t steamId, const std::string &name)
{
	if (!steamId) return;
	std::lock_guard<std::mutex> lock(g_lock);
	const std::string sid = std::to_string(steamId);
	if (g_pub.open && g_pub.steamId == sid) return;
	g_pub = Public();
	g_pub.open = true;
	g_pub.id = steamId;
	g_pub.steamId = sid;
	g_pub.name = name;
	View &v = g_pub.screen.view;
	v.pub.on = true;
	v.pub.id = steamId;
	v.pub.state = LeagueProfileState::Loading;
	Card c;
	c.name = name.empty() ? Tr("Player") : name;
	v.cards = {c};
	v.selected = 0;
	LogI("profile", "public profile of a player opened");
}

void ProfileUiPublicTick()
{
	PublicTick();
}

void ProfileUiGameTick()
{
	PublicTick();
	void *dlg = ProfileDialogOpen();
	bool due = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!dlg) {
			if (g_s.dlg) g_s = State();
			return;
		}
		due = g_s.dlg != dlg || (!g_s.closedAt && GetTickCount() - g_s.lastRead >= kReadEveryMs);
	}
	if (due) Refresh();
}

bool ProfileUiNeedsDraw()
{
	// It steps aside with the multiplayer screen while a dialog of the game is over it.
	if (PublicOpen()) return MpUiNeedsDraw();
	void *dlg = ProfileDialogOpen();
	std::lock_guard<std::mutex> lock(g_lock);
	if (!dlg) return false;
	// The game's dialog is open but not read yet: the screen's backdrop already covers it, so it never shows for a frame.
	if (g_s.dlg != dlg) return true;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void ProfileUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false, pub = false;
	std::string sid;
	void *dlg = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		pub = g_pub.open;
		State &st = pub ? g_pub.screen : g_s;
		if (pub) sid = g_pub.steamId;
		else if (!g_s.dlg || ProfileDialogOpen() != g_s.dlg) {
			if (ProfileDialogOpen()) {
				PanelRender shell(g_canvas, buf, w, h);
				if (BeginFullScreen(shell, ScreenViewFor(w, h))) shell.End();
			}
			return;
		}
		dlg = g_s.dlg;
		view = st.view;
		previous = st.hits;
		mouseDown = st.mouseDown;
	}
	if (pub) DrawScreen(g_pub.screen, [sid] { return g_pub.open && g_pub.steamId == sid; }, buf, w, h, std::move(view), std::move(previous), mouseDown);
	else DrawScreen(g_s, [dlg] { return g_s.dlg == dlg; }, buf, w, h, std::move(view), std::move(previous), mouseDown);
}

bool ProfileUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	// A press that closed the public profile (its Back) keeps its release: the multiplayer screen under it acts on releases, and
	// its own Back sits in the same corner.
	static bool swallowRelease = false;
	if (msg == WM_LBUTTONUP && swallowRelease) {
		swallowRelease = false;
		return true;
	}
	if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) swallowRelease = false;
	if (ModUiIsOpen()) return false;
	if (PublicOpen()) {
		if (!MpUiNeedsDraw()) return false;
		Command cmd;
		bool consumed = false, pub = false;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			pub = g_pub.open;
			if (pub) consumed = Handle(g_pub.screen, msg, wp, lp, cmd);
		}
		if (pub) {
			if (cmd.action != kActNone) ExecutePublic(cmd);
			if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) && !PublicOpen()) swallowRelease = true;
			return consumed;
		}
	}
	void *dlg = ProfileDialogOpen();
	if (!dlg) return false;
	{
		bool fresh = false;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			fresh = g_s.dlg != dlg;
			if (!fresh && g_s.closedAt) return msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP;
		}
		if (fresh) Refresh();
	}
	Command cmd;
	bool consumed = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg) return false;
		consumed = Handle(g_s, msg, wp, lp, cmd);
	}
	if (cmd.action != kActNone) Execute(cmd);
	return consumed;
}
