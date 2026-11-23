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

#include "core/log.h"
#include "game/gamestats.h"
#include "game/playerini.h"
#include "game/profiles.h"
#include "net/leagueclient.h"
#include "net/leagueclient_seasons.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leagueui.h"
#include "ui/modui.h"
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

std::mutex g_lock;
State g_s;
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

bool Handle(UINT msg, WPARAM wp, LPARAM lp, Command &cmd)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.dlg) return false;
	View &v = g_s.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	ScrollTo to;
	const bool onBar = ScrollbarMouse(v.bar, g_s.hits, msg, wp, at, to);
	if (to.id == kBarList) v.scroll = to.pos;
	else if (to.id == kBarHistory) v.historyScroll = to.pos;
	else v.panes.Apply(g_s.panes, to);
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (g_s.chipPress && !(wp & MK_LBUTTON)) g_s.chipPress = false;
		if (g_s.chipPress) {
			const int dx = at.x - g_s.chipStartX;
			if (!g_s.chipDragged && std::abs(dx) >= GetSystemMetrics(SM_CXDRAG)) g_s.chipDragged = true;
			const PaneArea *row = ChipRow(g_s);
			if (g_s.chipDragged && row) v.panes.Set(kPaneCivChips, ChipRowPos(*row, g_s.chipStartPos, dx), row->max);
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		if (onBar) return true;
		g_s.mouseDown = true;
		int arg = 0;
		const int action = HitActionAt(g_s.hits, at, arg, kActNone);
		if (action == kActCivScroll) {
			if (const PaneArea *row = ChipRow(g_s)) v.panes.Set(kPaneCivChips, v.panes.Of(kPaneCivChips) + arg * row->page * 2 / 3, row->max);
			return true;
		}
		if (const PaneArea *row = v.leagueOpen ? nullptr : ChipRow(g_s)) {
			if (PtInRect(&row->r, at)) {
				g_s.chipPress = true;
				g_s.chipDragged = false;
				g_s.chipStartX = at.x;
				g_s.chipStartPos = v.panes.Of(kPaneCivChips);
				g_s.chipAction = action;
				g_s.chipArg = arg;
				return true;
			}
		}
		const DWORD now = GetTickCount();
		const bool twice = action == kActPick && arg == g_s.lastClickRow && now - g_s.lastClick <= GetDoubleClickTime();
		g_s.lastClickRow = action == kActPick && !twice ? arg : -1;
		g_s.lastClick = now;
		if (twice && arg == v.selected) Act(v, kActUse, 0, cmd);
		else Act(v, action, arg, cmd);
		return true;
	}
	case WM_LBUTTONUP:
		g_s.mouseDown = false;
		if (g_s.chipPress && !g_s.chipDragged) Act(v, g_s.chipAction, g_s.chipArg, cmd);
		g_s.chipPress = false;
		return true;
	case WM_MOUSEWHEEL: {
		// The wheel's position comes in screen coordinates; the last move tells where the mouse is over the screen.
		if (v.panes.Wheel(g_s.panes, {v.mouseX, v.mouseY}, GET_WHEEL_DELTA_WPARAM(wp)) || v.leagueOpen) return true;
		const int steps = WheelSteps(g_s.wheelRest, GET_WHEEL_DELTA_WPARAM(wp));
		if (OverHistory(g_s, {v.mouseX, v.mouseY})) {
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

void ProfileUiGameTick()
{
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
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || ProfileDialogOpen() != g_s.dlg) {
			if (ProfileDialogOpen()) {
				PanelRender shell(g_canvas, buf, w, h);
				if (BeginFullScreen(shell, ScreenViewFor(w, h))) shell.End();
			}
			return;
		}
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
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
	if (reveal && g_s.view.civ == view.civ) {
		g_s.view.panes.Reveal(r.panes, kPaneCivChips, chipLeft - 24, chipW + 48);
		g_s.view.revealCiv = false;
	}
	// Kept where it was painted, so the arrows, the wheel and a drag start from what shows after a resize or a change of player.
	for (const PaneArea &pane : r.panes) g_s.view.panes.Set(pane.id, g_s.view.panes.Of(pane.id), pane.max);
	g_s.hits = std::move(r.hits);
	g_s.panes = std::move(r.panes);
	View &v = g_s.view;
	if (v.screenW != screen.w || v.screenH != screen.h) {
		v.screenW = screen.w;
		v.screenH = screen.h;
		KeepVisible(v);
		v.historyScroll = std::max(0, std::min(v.historyScroll, HistoryMaxScroll(v)));
	}
}

bool ProfileUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	void *dlg = ProfileDialogOpen();
	if (!dlg || ModUiIsOpen()) return false;
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
	const bool consumed = Handle(msg, wp, lp, cmd);
	if (cmd.action != kActNone) Execute(cmd);
	return consumed;
}
