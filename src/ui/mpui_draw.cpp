// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>
#include <atomic>

#include "ui/textedit.h"
#include "MinHook.h"
#include "core/log.h"
#include "ui/modui.h"
#include "mods/civsets.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "addr/state.h"

#include "game/mappreview.h"
#include "ui/mpui_internal.h"

using namespace mpui_impl;

int g_bgW = 0;
int g_bgH = 0;
DWORD g_bgTime = 0;

std::string BackgroundCachePath()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char *slash = strrchr(path, '\\')) *(slash + 1) = 0;
	return std::string(path) + "imperivm-resurrected\\_cache\\menu-background.bmp";
}

bool MostlyBlack(const uint32_t *buf, size_t n)
{
	unsigned long long sum = 0;
	size_t count = 0;
	for (size_t i = 0; i < n; i += 997, count++) sum += ((buf[i] >> 16) & 255) + ((buf[i] >> 8) & 255) + (buf[i] & 255);
	return count == 0 || sum / count < 30;
}

void PersistBackground(const uint32_t *buf, int w, int h)
{
	static bool done = false;
	if (done) return;
	done = true;
	const std::string path = BackgroundCachePath();
	CreateDirectoryA((path.substr(0, path.rfind('\\'))).c_str(), nullptr);
	FILE *fp = nullptr;
	if (fopen_s(&fp, path.c_str(), "wb") != 0 || !fp) return;
	const int stride = (w * 3 + 3) & ~3;
	unsigned char head[54] = {'B', 'M'};
	*reinterpret_cast<unsigned *>(head + 2) = 54 + stride * h;
	*reinterpret_cast<unsigned *>(head + 10) = 54;
	*reinterpret_cast<unsigned *>(head + 14) = 40;
	*reinterpret_cast<int *>(head + 18) = w;
	*reinterpret_cast<int *>(head + 22) = h;
	*reinterpret_cast<unsigned short *>(head + 26) = 1;
	*reinterpret_cast<unsigned short *>(head + 28) = 24;
	fwrite(head, 1, 54, fp);
	std::vector<unsigned char> line(stride, 0);
	for (int y = h - 1; y >= 0; y--) {
		const uint32_t *row = buf + static_cast<size_t>(y) * w;
		for (int x = 0; x < w; x++) {
			line[x * 3 + 0] = static_cast<unsigned char>(row[x] & 255);
			line[x * 3 + 1] = static_cast<unsigned char>((row[x] >> 8) & 255);
			line[x * 3 + 2] = static_cast<unsigned char>((row[x] >> 16) & 255);
		}
		fwrite(line.data(), 1, stride, fp);
	}
	fclose(fp);
}

bool LoadPersistedBackground(int w, int h)
{
	FILE *fp = nullptr;
	if (fopen_s(&fp, BackgroundCachePath().c_str(), "rb") != 0 || !fp) return false;
	unsigned char head[54] = {};
	bool ok = fread(head, 1, 54, fp) == 54 && head[0] == 'B' && head[1] == 'M' && *reinterpret_cast<unsigned short *>(head + 28) == 24;
	const int sw = ok ? *reinterpret_cast<int *>(head + 18) : 0, sh = ok ? *reinterpret_cast<int *>(head + 22) : 0;
	ok = ok && sw > 0 && sh > 0 && sw <= 8192 && sh <= 8192;
	if (!ok) {
		fclose(fp);
		return false;
	}
	const int stride = (sw * 3 + 3) & ~3;
	std::vector<unsigned char> raw(static_cast<size_t>(stride) * sh);
	ok = fread(raw.data(), 1, raw.size(), fp) == raw.size();
	fclose(fp);
	if (!ok) return false;
	g_bg.resize(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; y++) {
		const int sy = sh - 1 - std::min(sh - 1, y * sh / h);
		for (int x = 0; x < w; x++) {
			const unsigned char *px = &raw[static_cast<size_t>(sy) * stride + static_cast<size_t>(std::min(sw - 1, x * sw / w)) * 3];
			const uint32_t c = (static_cast<uint32_t>(px[2]) << 16) | (static_cast<uint32_t>(px[1]) << 8) | px[0];
			g_bg[static_cast<size_t>(y) * w + x] = 0xff000000u | ((c >> 1) & 0x7f7f7f);
		}
	}
	g_bgW = w;
	g_bgH = h;
	g_bgTime = GetTickCount();
	return true;
}

void SaveBackground(const uint32_t *buf, int w, int h, bool darkened)
{
	if (g_bgW == w && g_bgH == h && GetTickCount() - g_bgTime < 300) return;
	const size_t n = static_cast<size_t>(w) * h;
	g_bg.resize(n);
	for (size_t i = 0; i < n; i++) g_bg[i] = darkened ? buf[i] : 0xff000000u | ((buf[i] >> 1) & 0x7f7f7f);
	g_bgW = w;
	g_bgH = h;
	g_bgTime = GetTickCount();
}

std::atomic<bool> g_wakePosted{false};

void MpUiSetGameWindow(HWND hwnd)
{
	g_wakeWnd = hwnd;
}

void MpUiWakeHandled()
{
	g_wakePosted = false;
}

void WakeGameThread()
{
	const HWND hwnd = g_wakeWnd;
	if (hwnd && !g_wakePosted.exchange(true) && !PostMessageA(hwnd, kMsgMpUiWake, 0, 0)) g_wakePosted = false;
}

void MpUiWakeGameThread()
{
	WakeGameThread();
}

void MpUiObserveFrame(const uint32_t *buf, int w, int h)
{
	if (*reinterpret_cast<void **>(addr::kGame) == nullptr && !GameRoomActive() && !MostlyBlack(buf, static_cast<size_t>(w) * h)) {
		SaveBackground(buf, w, h, false);
		PersistBackground(buf, w, h);
	}
}

bool OverlayActive(const State &st)
{
	if (st.game.active) return st.dropOpen || (st.game.picker && !SaveMapPicking(st));
	return st.dropOpen || st.searching || st.connecting || st.passPrompt || st.syncConfirm || st.syncPhase || st.recoverAsk ||
		(st.recoverWaitHost && !st.room.id) || RejoinClientActive() || RejoinClientFailed() || (st.room.id && st.room.info.spectate) ||
		(st.room.id && !st.gameClosedAt);
}

void MpUiDraw(uint32_t *buf, int w, int h)
{
	State st;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.open) return;
		if (SpectatorViewing() && !SpectatorCovering()) {
			g_s.open = false;
			return;
		}
		g_s.ready = SteamOnlineReady();
		g_s.status = SteamOnlineStatus();
		g_s.loading = SteamLobbiesLoading();
		g_s.lobbies = SteamLobbies();
		g_s.room = SteamCurrentRoom();
		SyncTick();
		g_s.map = SteamMapStatus();
		if (g_s.map.state == 1) {
			static DWORD lastUnready = 0;
			const GameRoomSnapshot snap = GameRoomGet();
			if (snap.active && !snap.host && !snap.busy && GetTickCount() - lastUnready > 1500) {
				for (const GameRoomSlot &s : snap.slots) {
					if (s.used && s.local && s.ready) {
						lastUnready = GetTickCount();
						GameRoomToggleReady();
						LogI("room", "the host changed the scenario; you are not ready until you have it");
						break;
					}
				}
			}
		}
		g_s.save = SteamSaveStatus();
		bool roomOwner = false;
		for (const OnlineMember &m : g_s.room.members) {
			if (m.me && m.owner) roomOwner = true;
		}
		if (g_s.room.id) g_s.skirmishCivs = false;
		if (g_s.room.id && !roomOwner) CivRoomSetsSet(CivSetsSplit(g_s.room.info.civSets));
		else if (!g_s.skirmishCivs && !g_s.room.id && !g_s.connecting && !g_s.autoHost && !g_flow.hostPending && !g_flow.inJoin && !g_autoJoin.active && !RejoinBusy()) {
			CivRoomSetsSet({});
		}
		if (g_s.recoverWaitHost && !g_s.room.id) {
			bool joined = false;
			for (const OnlineLobby &l : g_s.lobbies) {
				if (l.owner == g_s.recoverWaitHost && !l.saveName.empty() && !l.playing) {
					g_s.recoverWaitHost = 0;
					LogI("lobby", "the game of %s is ready to continue; joining", l.host.c_str());
					TryJoin(l, "");
					joined = true;
					break;
				}
			}
			if (!joined && GetTickCount() - g_s.recoverRefresh > 4000) {
				g_s.recoverRefresh = GetTickCount();
				SteamRefreshLobbies();
			}
		}
		if (g_s.joinCheck.id) {
			const State::JoinCheck check = g_s.joinCheck;
			if (SteamLobbyDataSerial(check.id) != check.serial || GetTickCount() - check.since > 1200) {
				g_s.joinCheck = State::JoinCheck();
				const OnlineLobby *fresh = nullptr;
				for (const OnlineLobby &l : g_s.lobbies) {
					if (l.id == check.id) fresh = &l;
				}
				if (fresh) TryJoinNow(*fresh, check.password, check.watch);
				else ModUiToast("That room is no longer open.");
			}
		}
		if (const uint64_t resync = SteamTakeResync()) {
			for (const OnlineLobby &l : g_s.lobbies) {
				const bool watch = l.specLobby == resync && (l.id != resync || l.specOnly);
				if (l.id == resync || watch) TryJoinNow(l, g_s.joinPassInput, watch);
			}
		}
		if (g_s.ownKey.empty()) g_s.ownKey = SteamOwnGameplayKey();
		if (g_s.view == kViewList && g_s.ready && !g_s.room.id && !g_s.joinCheck.id && GetTickCount() - g_s.selectedPoll > 2500) {
			const std::vector<int> vis = Visible(g_s);
			if (g_s.selected >= 0 && g_s.selected < static_cast<int>(vis.size())) {
				g_s.selectedPoll = GetTickCount();
				SteamRequestLobbyData(g_s.lobbies[vis[g_s.selected]].id);
			}
		}
		if (g_s.discordJoin) {
			const OnlineLobby *found = nullptr;
			for (const OnlineLobby &l : g_s.lobbies) {
				if (l.id == g_s.discordJoin) found = &l;
			}
			if (found) {
				const OnlineLobby lobby = *found;
				g_s.discordJoin = 0;
				LogI("discord", "joining room %s", lobby.name.c_str());
				TryJoin(lobby, "");
			}
			else if (GetTickCount() - g_s.discordJoinAt > 6000) {
				const uint64_t id = g_s.discordJoin;
				g_s.discordJoin = 0;
				LogW("discord", "the room is not in the list; trying to join directly");
				SteamJoinLobby(id, "");
			}
			else if (GetTickCount() - g_s.discordJoinRefresh > 1500) {
				g_s.discordJoinRefresh = GetTickCount();
				SteamRefreshLobbies();
			}
		}
		if (g_s.autoHost && g_s.room.id && roomOwner) {
			g_s.autoHost = false;
			DoAction(kHostGame, 0);
		}
		if (g_s.room.id && !roomOwner && g_s.room.info.hosting && !RejoinBusy()) {
			if (!g_s.autoEntered && !g_s.connecting && !g_s.game.active && !g_s.syncPhase && !g_s.syncConfirm) {
				g_s.autoEntered = true;
				LogI("lobby", "the host created the game; joining alone");
				DoAction(kEnterGame, 0);
			}
		}
		else {
			g_s.autoEntered = false;
		}
		if (!g_s.hostSave.file.empty()) {
			if (g_s.room.id && roomOwner && !g_s.hostSavePublished) {
				SteamPublishSave(g_s.hostSave.file, g_s.hostSave.name);
				g_s.hostSavePublished = true;
			}
			else if (!g_s.room.id && g_s.hostSavePublished) {
				g_s.hostSave = SaveEntry();
				g_s.hostSavePublished = false;
			}
		}
		GameRoomSnapshot snap = GameRoomGet();
		const bool justClosed = !snap.active && g_s.game.active && (g_s.startingCover || (g_s.gameWasActive && !g_s.leftByUser));
		if (snap.active && !snap.online && !g_s.game.active && !g_s.room.id) {
			CivRoomSetsSet({}); // a new skirmish starts with every set allowed
			g_s.skirmishCivs = true;
		}
		if (!justClosed) g_s.game = std::move(snap);
		ApplyPredicted(g_s.game, g_s.predicted);
		ApplySeatDisplay(g_s.game);
		ApplySaveLook(g_s.game);
		RedirectReplacedRaces(g_s.game);
		if (!g_s.game.active && g_s.dropKind <= kDropPreset) g_s.dropOpen = false;
		if (g_s.dropOpen && g_s.dropKind == kDropSlot && (g_flow.arrivingIds[g_s.dropIndex & 7] || g_s.game.slots[g_s.dropIndex & 7].joining)) {
			g_s.dropOpen = false;
		}
		if (!g_s.game.picker) {
			g_s.pickSel = -1;
			g_s.pick.scroll = 0;
			g_s.pickOpened = false;
			g_s.pickSearch.clear();
			g_s.pickDelete = -1;
			g_s.pickGone.clear();
			g_s.pickNote.clear();
			if (g_s.focus == kPickSearchField) g_s.focus = kNoField;
		}
		else if (!g_s.pickOpened) {
			g_s.pickOpened = true;
			MapPreviewReset();
			MapPreviewWanted(g_s.game.pickerPaths, 0);
		}
		const std::string name = SteamOnlinePlayerName();
		g_s.player = name.empty() ? "Player" : name;
		if (g_s.ready && !g_s.room.id && GetTickCount() - g_s.lastRefresh > 15000) {
			g_s.lastRefresh = GetTickCount();
			SteamRefreshLobbies();
		}
		const std::string notice = SteamTakeNotice();
		if (!notice.empty()) ModUiToast(notice);
		if (g_s.searching && !g_s.found && GetTickCount() - g_s.searchStart > 7000) {
			g_s.found = true;
			g_s.foundAt = GetTickCount();
		}
		if (g_s.searching && g_s.found && GetTickCount() - g_s.foundAt > 10000) {
			g_s.found = false;
			g_s.searchStart = GetTickCount();
		}
		st = g_s;
	}
	const float s = std::max(0.6f, std::min(1.6f, std::min((w - 16) / static_cast<float>(kW), (h - 16) / static_cast<float>(kH))));
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.uiScale = s;
	}
	const bool cover = st.game.active || st.connecting || SpectatorCovering() || GetTickCount() - g_flow.lastLobbyUpdate < 1000;
	const size_t n = static_cast<size_t>(w) * h;
	static bool triedCache = false;
	if (cover && g_bg.size() != n && !triedCache) {
		triedCache = true;
		LoadPersistedBackground(w, h);
	}
	if (cover && g_bgW == w && g_bgH == h && g_bg.size() == n) {
		std::copy(g_bg.begin(), g_bg.end(), buf);
	}
	else {
		for (size_t i = 0; i < n; i++) {
			buf[i] = cover ? 0xff100c08u : 0xff000000u | ((buf[i] >> 1) & 0x7f7f7f);
		}
		if (!cover) SaveBackground(buf, w, h, true);
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kW, kH, 16, false, s)) return;
	r.Focus(st.tabAction, st.tabArg);
	if (st.mouseDown || GetTickCount() - st.pressTime < 150) {
		r.p.pressAction = st.pressAction;
		r.p.pressArg = st.pressArg;
	}
	PaintScreen(r.p, st);
	WakeGameThread();
	r.End(250);
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	g_s.syncGeom = g_syncGeomOut;
	g_s.createModsGeom = g_createModsGeomOut;
	g_s.createCivGeom = g_createCivGeomOut;
	const bool overlay = OverlayActive(st);
	g_s.pickGeom = g_pickGeomOut;
	g_s.optGeom = overlay ? ListGeom() : g_optGeomOut;
	g_s.detailGeom = overlay ? ListGeom() : g_detail.geomOut;
	g_s.chatGeom = overlay ? ListGeom() : g_chat.geomOut;
}
