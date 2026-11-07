// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/profileui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/playerini.h"
#include "game/profiles.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/modui.h"
#include "ui/patronui.h"
#include "ui/profileui_view.h"
#include "ui/textedit.h"
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
	bool mouseDown = false;
	DWORD lastRead = 0, closedAt = 0;
	// The game window may not send double clicks, so they are told apart here.
	DWORD lastClick = 0;
	int lastClickRow = -1;
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
	for (const PlayedGame &g : RecentGames(ini, kChartGames)) {
		RecentRow r;
		r.day = g.day;
		r.month = g.month;
		r.year = g.year;
		r.won = !g.lost;
		r.multi = g.multi;
		r.civ = ProfileCivName(g.race);
		r.durationMs = g.durationMs;
		r.kills = g.unitsKilled;
		c.recent.push_back(r);
	}
	c.saves = ProfileSaveCount(slot.dir);
	return c;
}

void KeepVisible(View &v)
{
	const int count = static_cast<int>(v.cards.size());
	if (v.selected >= 0) {
		if (v.selected < v.scroll) v.scroll = v.selected;
		if (v.selected >= v.scroll + kListRows) v.scroll = v.selected - kListRows + 1;
	}
	v.scroll = std::max(0, std::min(v.scroll, std::max(0, count - kListRows)));
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
	const std::string steamName = me ? SteamOnlinePlayerName() : std::string();

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
	v.cards = std::move(cards);
	v.selected = native.selected;
	v.avatar = avatar;
	v.patron = patron;
	v.steamName = steamName;
	if (moved) KeepVisible(v);
	g_s.dirs = std::move(dirs);
	g_s.cache = std::move(kept);
	g_s.lastRead = GetTickCount();
}

struct Command {
	int action = kActNone;
	int arg = 0;
	int modal = kModalNone;
	std::string text;
};

int HitAt(const State &st, POINT at, int &arg)
{
	for (auto it = st.hits.rbegin(); it != st.hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		return it->action;
	}
	return kActNone;
}

void OpenModal(View &v, int modal)
{
	v.modal = modal;
	v.text = modal == kModalRename && v.selected >= 0 && v.selected < static_cast<int>(v.cards.size()) ? v.cards[v.selected].name : std::string();
	EditSelectAll(v.text, v.edit);
}

bool CanAct(const View &v, int action)
{
	const int count = static_cast<int>(v.cards.size());
	const bool one = v.selected >= 0 && v.selected < count;
	switch (action) {
	case kActRename:
	case kActPlay: return one;
	case kActDelete: return one && count > 1;
	default: return true;
	}
}

// Changes of the screen alone happen here, under the lock; whatever touches the game is left in `cmd` for Execute.
void Act(View &v, int action, int arg, Command &cmd)
{
	switch (action) {
	case kActNew: OpenModal(v, kModalNew); break;
	case kActRename:
		if (CanAct(v, action)) OpenModal(v, kModalRename);
		break;
	case kActDelete:
		if (CanAct(v, action)) OpenModal(v, kModalDelete);
		break;
	case kActModalCancel:
		v.modal = kModalNone;
		break;
	case kActModalOk:
		if (v.modal != kModalDelete && NameDraft(v.text).empty()) break;
		cmd.action = kActModalOk;
		cmd.modal = v.modal;
		cmd.arg = v.selected;
		cmd.text = NameDraft(v.text);
		v.modal = kModalNone;
		break;
	case kActPick:
		if (arg != v.selected) {
			cmd.action = kActPick;
			cmd.arg = arg;
		}
		break;
	case kActPlay:
		if (CanAct(v, action)) {
			cmd.action = kActPlay;
			cmd.arg = v.selected;
		}
		break;
	case kActBack:
	case kActPatron:
		cmd.action = action;
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
	case kActModalOk:
		if (cmd.modal == kModalNew) ProfileDialogCreate(cmd.text);
		else if (cmd.modal == kModalRename) ProfileDialogRename(cmd.arg, cmd.text);
		else if (cmd.modal == kModalDelete) ProfileDialogDelete(cmd.arg);
		Refresh();
		break;
	case kActPlay:
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
	default: break;
	}
}

bool HandleModalKey(View &v, UINT msg, WPARAM wp, Command &cmd)
{
	if (v.modal == kModalNone) return false;
	if (msg == WM_CHAR) {
		if (v.modal != kModalDelete) EditChar(v.text, v.edit, wp, kProfileNameMax);
		return true;
	}
	if (msg != WM_KEYDOWN) return true;
	if (wp == VK_ESCAPE) Act(v, kActModalCancel, 0, cmd);
	else if (wp == VK_RETURN) Act(v, kActModalOk, 0, cmd);
	else if (v.modal != kModalDelete) EditKey(v.text, v.edit, wp, kProfileNameMax);
	return true;
}

bool Handle(UINT msg, WPARAM wp, LPARAM lp, Command &cmd)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.dlg) return false;
	View &v = g_s.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (g_s.mouseDown && v.modal != kModalNone) {
			FieldGeom geom;
			if (FieldGet(kActField, geom)) v.edit.caret = EditIndexAt(geom, v.text, at.x);
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseDown = true;
		int arg = 0;
		const int action = HitAt(g_s, at, arg);
		if (action == kActField) {
			FieldGeom geom;
			if (FieldGet(kActField, geom)) v.edit.caret = v.edit.anchor = EditIndexAt(geom, v.text, at.x);
			if (msg == WM_LBUTTONDBLCLK) EditSelectWord(v.text, v.edit);
			return true;
		}
		const DWORD now = GetTickCount();
		const bool twice = action == kActPick && arg == g_s.lastClickRow && now - g_s.lastClick <= GetDoubleClickTime();
		g_s.lastClickRow = action == kActPick && !twice ? arg : -1;
		g_s.lastClick = now;
		if (twice && arg == v.selected) Act(v, kActPlay, 0, cmd);
		else Act(v, action, arg, cmd);
		return true;
	}
	case WM_LBUTTONUP:
		g_s.mouseDown = false;
		return true;
	case WM_MOUSEWHEEL:
		if (v.modal == kModalNone) {
			v.scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
			v.scroll = std::max(0, std::min(v.scroll, std::max(0, static_cast<int>(v.cards.size()) - kListRows)));
		}
		return true;
	case WM_KEYDOWN:
	case WM_CHAR:
		if (HandleModalKey(v, msg, wp, cmd)) return true;
		if (msg == WM_CHAR) return true;
		switch (wp) {
		case VK_ESCAPE: Act(v, kActBack, 0, cmd); break;
		case VK_RETURN: Act(v, kActPlay, 0, cmd); break;
		case VK_F2: Act(v, kActRename, 0, cmd); break;
		case VK_DELETE: Act(v, kActDelete, 0, cmd); break;
		case VK_INSERT: Act(v, kActNew, 0, cmd); break;
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
	if (!dlg || g_s.dlg != dlg) return false;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void ProfileUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || ProfileDialogOpen() != g_s.dlg) return;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kScreenW, kScreenH, 16)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintProfileScreen(r.p, view);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
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
