// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/json.h"
#include "core/log.h"
#include "core/names.h"
#include "ui/modui.h"
#include "mods/civsets.h"
#include "mods/modoptions.h"
#include "net/netdrop.h"
#include "net/rejoin.h"
#include "addr/state.h"
#include "addr/ui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

void MpUiGameThreadTick()
{
	static bool inside = false;
	if (inside) return;
	inside = true;
	struct Leave {
		~Leave() { inside = false; }
	} leave;
	if (g_mainMenu && g_menuRefresh.exchange(false)) RunMenuRefresh(g_mainMenu);
	if (g_flow.nameSwapPending) {
		g_flow.nameSwapPending = false;
		GameRoomUseOnlineName(SteamOnlinePlayerName());
	}
	GameRoomTick();
	if (!g_hostAfter.savePath.empty()) {
		if (GetTickCount() - g_hostAfter.time > 300000) {
			g_hostAfter.savePath.clear();
		}
		else if (g_mainMenu && *reinterpret_cast<void **>(addr::kGame) == nullptr) {
			const std::string path = g_hostAfter.savePath;
			g_hostAfter.savePath.clear();
			LogI("lobby", "back in the main menu; room for the online game \"%s\"", path.c_str());
			MpUiHostSave(path);
		}
	}
	const bool active = GameRoomActive();
	if (GameRoomOnlineNameActive() && !active && !g_s.open && !g_s.connecting &&
		*reinterpret_cast<void **>(addr::kGame) == nullptr && GetTickCount() - g_flow.lastLobbyUpdate > 2000) {
		GameRoomRestoreProfileName();
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (active) {
			if (!g_s.gameWasActive) {
				LogI("lobby", "game room ready");
				g_s.createSave = -1;
			}
			g_s.open = true;
			g_s.connecting = false;
			g_s.gameWasActive = true;
			const GameRoomSnapshot snap = GameRoomGet();
			g_s.gameWasOnline = snap.online;
			static DWORD foodForcedAt = 0;
			if (snap.host && !snap.busy && g_s.hostSave.name.empty() && GetTickCount() - foodForcedAt > 1000) {
				for (const GameRoomOption &o : snap.options) {
					if (o.id == 0x1009 && o.checkbox && !o.value) {
						foodForcedAt = GetTickCount();
						GameRoomToggleOption(0x1009);
						LogD("room", "shared supplies enabled (always)");
					}
				}
			}
			bool owner = false;
			for (const OnlineMember &m : g_s.room.members) {
				if (m.me && m.owner) owner = true;
			}
			std::string pending;
			if (snap.online && owner && g_s.hostSavePublished) pending = g_s.hostSave.vfsPath;
			else if (snap.online && !owner && !g_s.room.info.saveName.empty() && g_s.save.ready) pending = "SaveOnline/room.snr";
			MpSaveSetPendingLoad(pending);
			if (!pending.empty()) {
				std::string cams = g_s.room.info.cams;
				if (owner) {
					cams.clear();
					FILE *f = nullptr;
					if (fopen_s(&f, (g_s.hostSave.file + ".cameras").c_str(), "rb") == 0 && f) {
						char buf[1024] = {};
						cams.assign(buf, fread(buf, 1, sizeof(buf) - 1, f));
						fclose(f);
					}
				}
				MpSaveSetPendingCameras(cams);
			}
			if (!snap.host && snap.online && !g_s.room.info.saveName.empty() && snap.localSlot >= 0 && snap.localSlot < 8) {
				const std::vector<int> &sc = g_s.room.info.saveColors;
				const int want = snap.localSlot < static_cast<int>(sc.size()) ? sc[snap.localSlot] : -1;
				if (g_s.guestColorSlot != snap.localSlot) {
					g_s.guestColorSlot = snap.localSlot;
					g_s.guestColorTries = 0;
				}
				if (want >= 0 && want < 12 && snap.slots[snap.localSlot].color != want && GetTickCount() - g_s.guestColorTick > 600 &&
					g_s.guestColorTries < 6) {
					g_s.guestColorTick = GetTickCount();
					g_s.guestColorTries++;
					GameRoomSetColor(snap.localSlot, want);
				}
			}
			if (g_s.roomLimit > 0 && snap.host && snap.online && !snap.busy && snap.localSlot >= 0 && g_s.hostSave.name.empty()) {
				int seats = 0;
				for (const GameRoomSlot &s : snap.slots) {
					if (s.exists && ((s.used && s.type == 3) || (!s.used && s.type == 0))) seats++;
				}
				for (int i = 7; i >= 0 && seats > g_s.roomLimit; i--) {
					const GameRoomSlot &s = snap.slots[i];
					if (!s.exists || s.used || s.local || s.type != 0) continue;
					GameRoomSetSlotType(i, 2);
					seats--;
				}
				LogI("room", "the room was created for %d players: the other open slots are closed", g_s.roomLimit);
				g_s.roomLimit = 0;
			}
			if (snap.online && !snap.host) {
				if (!LocalReady(snap)) {
					g_s.readySeen = false;
				}
				else if (!g_s.readySeen) {
					g_s.readySeen = true;
					g_s.readyConfig = RoomConfigKey(g_s.room.info);
				}
				else if (RoomConfigKey(g_s.room.info) != g_s.readyConfig) {
					g_s.readySeen = false;
					DoAction(kGReady, 0);
					ModUiToast("The host changed the game: press \"Ready\" again when you agree.");
					LogI("room", "the host changed the game options: ready taken back");
				}
			}
			if (snap.host && snap.online) {
				std::string opts = snap.optionsText;
				{
					uint64_t seatMember = 0;
					int seatSlot = -1;
					while (SteamTakeSeatRequest(seatMember, seatSlot)) {
						std::string who;
						for (const OnlineMember &m : g_s.room.members) {
							if (m.id == seatMember) who = m.name;
						}
						int from = -1;
						for (int i = 0; i < 8 && !who.empty(); i++) {
							if (snap.slots[i].used && snap.slots[i].type == 3 && PlayerNameMatches(snap.slots[i].name, who)) from = i;
						}
						if (from < 0 || seatSlot < 0 || seatSlot >= 8 || seatSlot == from || !SeatTakeable(snap.slots[seatSlot])) {
							LogW("room", "%s asks for slot %d, but it cannot be done", who.c_str(), seatSlot + 1);
							continue;
						}
						LogD("room", "%s moves to slot %d (chosen by the player)", who.c_str(), seatSlot + 1);
						g_s.savePlaced.insert(LowerName(who));
						g_s.pendingSeat[LowerName(who)] = {seatSlot, GetTickCount()};
						GameRoomSwapSlots(from, seatSlot);
						g_s.saveSwapTick = g_s.saveSeatStep = GetTickCount();
					}
				}
				if (g_s.hostSavePublished) {
					if (!g_s.savePlayersRead) {
						g_s.savePlayersRead = true;
						const MpSaveInfo info = MpSaveReadInfo(g_s.hostSave.vfsPath);
						g_s.savePlayers = info.players;
						g_s.saveMap = info.valid ? info.map : "";
						g_s.saveMapRandom = info.valid && info.random;
						g_s.saveMapStep = 0;
						g_s.saveMapPick = -1;
						g_s.saveMapTries = 0;
						g_s.saveMapTick = 0;
						g_s.saveMapError.clear();
						{
							Json side;
							const bool read = ReadJsonFile(g_s.hostSave.file + ".mods.json", side);
							if (read) CivRoomSetsSet(CivSetsSplit(side.GetString("civSets")));
							const unsigned saved = read ? static_cast<unsigned>(side.GetNumber("rules", 0)) & (kRuleFreeAttach | kRuleLockDiplomacy | kRuleCheats | ModOptionsMask()) : 0;
							if (saved) {
								GameRoomSetRules(saved);
								LogD("saves", "saved game: mod rules 0x%x", saved);
							}
						}
						g_s.saveConfiguredFor = "-";
						for (int &c : g_s.saveColors) c = -1;
						for (int &t : g_s.saveColorTries) t = 0;
					}
					StepSaveMap(snap);
					if (snap.scenario != g_s.saveConfiguredFor) {
						g_s.saveConfiguredFor = snap.scenario;
						for (size_t i = 0; i < g_s.savePlayers.size() && i < 8; i++) {
							const MpSavePlayer &sp = g_s.savePlayers[i];
							const int slot = static_cast<int>(i);
							LogD("saves", "saved game: slot %d, \"%s\", color %04x, control %d", slot + 1, sp.name.c_str(), sp.color555, sp.control);
							if (!sp.used()) GameRoomSetSlotType(slot, 2);
							else if (sp.ai()) GameRoomSetSlotType(slot, 1);
							else if (slot != snap.localSlot) GameRoomSetSlotType(slot, 0);
							if (sp.used()) {
								const int r = ((sp.color555 >> 10) & 31) * 255 / 31, gr = ((sp.color555 >> 5) & 31) * 255 / 31, b = (sp.color555 & 31) * 255 / 31;
								int best = 0, bestDist = 1 << 30;
								for (int c = 0; c < 12; c++) {
									const int cr = (snap.colors[c] >> 16) & 255, cg = (snap.colors[c] >> 8) & 255, cb = snap.colors[c] & 255;
									const int d = (cr - r) * (cr - r) + (cg - gr) * (cg - gr) + (cb - b) * (cb - b);
									if (d < bestDist) {
										bestDist = d;
										best = c;
									}
								}
								g_s.saveColors[slot] = best;
							}
						}
					}
					if (GetTickCount() - g_s.saveSeatStep > 500) {
						g_s.saveSeatStep = GetTickCount();
						bool moved = false;
						for (int i = 0; i < 8 && !moved; i++) {
							const GameRoomSlot &rs = snap.slots[i];
							if (!rs.used || rs.type != 3 || rs.name.empty()) continue;
							if (!g_s.savePlaced.insert(LowerName(rs.name)).second) continue;
							for (size_t j = 0; j < g_s.savePlayers.size() && j < 8; j++) {
								const MpSavePlayer &sp = g_s.savePlayers[j];
								if (!sp.used() || sp.ai() || _stricmp(sp.name.c_str(), rs.name.c_str()) != 0) continue;
								if (static_cast<int>(j) == i) break;
								const GameRoomSlot &target = snap.slots[j];
								if (target.used) break;
								if (target.type != 0) break;
								LogD("saves", "saved game: %s is in slot %d; moved to their own slot, %d", rs.name.c_str(), i + 1, static_cast<int>(j) + 1);
								g_s.pendingSeat[LowerName(rs.name)] = {static_cast<int>(j), GetTickCount()};
								GameRoomSwapSlots(i, static_cast<int>(j));
								g_s.saveSwapTick = GetTickCount();
								moved = true;
								break;
							}
						}
					}
					if (GetTickCount() - g_s.saveColorTick > 400) {
						g_s.saveColorTick = GetTickCount();
						for (int i = 0; i < 8; i++) {
							const GameRoomSlot &rs = snap.slots[i];
							if (rs.name != g_s.saveColorWho[i]) {
								g_s.saveColorWho[i] = rs.name;
								g_s.saveColorTries[i] = 0;
							}
							if (g_s.saveColors[i] >= 0 && rs.used && rs.color != g_s.saveColors[i] && (rs.type == 1 || rs.local) &&
								g_s.saveColorTries[i]++ < 6) {
								GameRoomSetColor(i, g_s.saveColors[i]);
								break;
							}
						}
					}
					opts += Tr("Players in the saved game:\n") + SavePlayersText(g_s.savePlayers);
					{
						std::string colors;
						for (int i = 0; i < 8; i++) {
							const bool used = i < static_cast<int>(g_s.savePlayers.size()) && g_s.savePlayers[i].used();
							colors += (i ? "," : "") + std::to_string(g_s.saveColors[i]) + "/" + std::to_string(used ? RaceIdToCombo(g_s.savePlayers[i].type) : -1);
						}
						SteamPublishSaveColors(colors);
					}
				}
				SteamPublishOptions(opts);
				SteamPublishRules(snap.randomMap ? snap.rules : snap.rules & ~kSeasonBits);
				SteamPublishCivSets(CivSetsJoin(CivRoomSets()));
				{
					std::string slotsText;
					for (int i = 0; i < 8; i++) {
						const GameRoomSlot &rs = snap.slots[i];
						if (!rs.exists && !(rs.used && rs.type == 3)) continue;
						const int type = !rs.exists ? 5 : rs.type;
						slotsText += std::to_string(type) + "|" + ((rs.used || rs.type == 1) ? SlotLabel(rs) : std::string()) + "\n";
					}
					SteamPublishSlots(slotsText);
					int seats = 0;
					for (const GameRoomSlot &rs : snap.slots) {
						if (rs.exists && ((rs.used && rs.type == 3) || (!rs.used && rs.type == 0))) seats++;
					}
					SteamSetMemberLimit((std::max)(seats, static_cast<int>(g_s.room.members.size())) + g_s.room.bannedPresent);
				}
				SteamPublishScenario(snap.scenario);
			}
		}
		else if (g_s.gameWasActive) {
			g_s.gameWasActive = false;
			g_s.closedRoomGen = g_s.lastRoomGen;
			g_s.gameClosedAt = GetTickCount();
			g_s.closedByUser = g_s.leftByUser;
			g_s.leftByUser = false;
			if (!g_s.gameWasOnline && g_s.closedByUser) g_s.open = false;
			else if (!g_s.closedByUser) g_s.startingCover = true;
		}
		if (g_s.gameClosedAt && (GetTickCount() - g_s.gameClosedAt > (g_s.startingCover ? 20000u : 1500u) ||
									*reinterpret_cast<void **>(addr::kGame) != nullptr)) {
			g_s.gameClosedAt = 0;
			const bool playing = *reinterpret_cast<void **>(addr::kGame) != nullptr;
			const bool wasCovering = g_s.startingCover;
			g_s.startingCover = false;
			if (playing) {
				g_s.open = false;
				if (g_s.gameWasOnline) {
					SteamSetPlaying(true);
					g_s.inOnlineGame = true;
					g_s.classicPending = false;
					RecordOnlineGame(g_s.room);
					NetDropReset();
				}
				if (g_s.hostSavePublished) {
					SteamPublishSave("", "");
					g_s.hostSave = SaveEntry();
					g_s.hostSavePublished = false;
				}
			}
			else {
				if (g_s.gameWasOnline) {
					if (g_s.closedRoomGen) SteamLeaveLobby(g_s.closedRoomGen);
					g_s.closedRoomGen = 0;
					if (!g_s.closedByUser) g_s.open = true;
				}
				if (!g_s.gameWasOnline && wasCovering) g_s.open = false;
				MpSaveSetPendingLoad("");
			}
			LogI("lobby", "game room closed (%s)", playing ? "the game starts" : g_s.gameWasOnline ? "out of the room, to the list" : "back to the menu");
		}
		{
			const uint64_t nowRoom = g_s.room.id;
			if (g_s.lastRoomId && !nowRoom && !active && !g_s.autoHost && !g_flow.hostPending && (g_s.connecting || g_autoJoin.active || g_flow.inJoin)) {
				LogW("lobby", "the Steam lobby disappeared while joining; cancelling the entry");
				AbortJoin();
				g_s.lastRoomId = 0;
			}
			if (nowRoom) {
				g_s.lastRoomId = nowRoom;
				g_s.lastRoomGen = SteamMembership();
			}
			if (g_s.connecting && !nowRoom && !g_s.autoHost && !g_autoJoin.active && !g_flow.inJoin && !g_flow.hostPending &&
				GetTickCount() - g_s.connectStart > 3000) {
				LogI("lobby", "no Steam lobby and no entry in progress; removing the connection notice");
				g_s.connecting = false;
				g_s.classicPending = false;
			}
		}
		if (SteamTakeJoinRefused()) {
			if (g_s.syncPhase == 1) {
				g_s.syncPhase = 4;
				g_s.syncError = Tr("You have been kicked from the room.");
			}
			if (g_s.connecting || g_autoJoin.active || g_flow.inJoin) AbortJoin();
		}
		if (SteamTakeCreateFailed()) {
			if (g_s.autoHost || g_s.connecting || g_flow.inJoin) AbortJoin();
		}
		if (SteamTakeKicked()) {
			if (active) {
				g_s.leftByUser = true;
				GameRoomCancel();
			}
			else if (g_s.connecting || g_autoJoin.active || g_flow.inJoin) {
				LogW("lobby", "the host kicked you while joining; cancelling the entry");
				AbortJoin();
			}
		}
		{
			static uint64_t roomId = 0, gameHost = 0;
			static std::string gameHostName;
			uint64_t owner = 0;
			std::string ownerName;
			for (const OnlineMember &m : g_s.room.members) {
				if (m.owner) {
					owner = m.id;
					ownerName = m.name;
				}
			}
			if (g_s.leftByUser) {
				roomId = gameHost = 0;
			}
			else if (active && g_s.game.online && !g_s.inOnlineGame && !g_s.room.id && roomId) {
				LogI("lobby", "the Steam lobby closed before starting; out of the game room");
				g_s.classicPending = false;
				roomId = gameHost = 0;
				LeaveGameRoom();
			}
			else if (active && g_s.game.online && g_s.room.id && !g_s.inOnlineGame && owner) {
				if (g_s.room.id != roomId || !gameHost) {
					roomId = g_s.room.id;
					gameHost = owner;
					gameHostName = ownerName;
				}
				else if (owner != gameHost) {
					LogI("lobby", "the host (%s) left the room before starting; out of the room", gameHostName.c_str());
					ModUiToast((gameHostName.empty() ? std::string("The host") : gameHostName) + Tr(" has closed the room."));
					roomId = gameHost = 0;
					LeaveGameRoom();
				}
			}
			else if (!active) {
				roomId = gameHost = 0;
			}
		}
		{
			uint64_t from = 0;
			std::string text;
			uint64_t roomOwner = 0;
			for (const OnlineMember &m : g_s.room.members) {
				if (m.owner) roomOwner = m.id;
			}
			for (auto it = g_s.swapIntents.begin(); it != g_s.swapIntents.end();) {
				if (GetTickCount() - it->second.second > 30000) it = g_s.swapIntents.erase(it);
				else ++it;
			}
			while (SteamTakeSwap(from, text)) {
				auto nameOf = [](uint64_t id) {
					for (const OnlineMember &m : g_s.room.members) {
						if (m.id == id) return m.name;
					}
					return std::string();
				};
				const std::string name = nameOf(from);
				if (text == "ask") {
					if (!active || g_s.swapAskFrom || name.empty()) {
						SteamSendSwap(from, "no");
						continue;
					}
					g_s.swapAskFrom = from;
					g_s.swapAskName = name;
					g_s.swapAskTime = GetTickCount();
					LogI("room", "%s asks to swap slots with me", name.c_str());
				}
				else if (text.compare(0, 7, "intent ") == 0) {
					if (from && GameRoomGet().host) g_s.swapIntents[from] = {_strtoui64(text.c_str() + 7, nullptr, 10), GetTickCount()};
				}
				else if (text.compare(0, 5, "drop ") == 0) {
					if (!roomOwner || from != roomOwner) continue;
					const int positions = atoi(text.c_str() + 5);
					LogW("room", "the host chose a map for %d players and we did not fit", positions);
					ModUiToast(Tr("The host chose a map for ") + std::to_string(positions) + Tr(" players and there was no room for you: you have left the room."));
					if (active) LeaveGameRoom();
				}
				else if (text == "no") {
					if (from == g_s.swapPendingTo) {
						ModUiToast(name + Tr(" did not accept the slot swap."));
						g_s.swapPendingTo = 0;
					}
				}
				else if (text.compare(0, 3, "ok ") == 0) {
					const GameRoomSnapshot snap = GameRoomGet();
					if (!snap.host) continue;
					const uint64_t otherId = _strtoui64(text.c_str() + 3, nullptr, 10);
					const auto intent = g_s.swapIntents.find(otherId);
					if (intent == g_s.swapIntents.end() || intent->second.first != from) {
						LogW("room", "%s accepted a slot swap that nobody had asked them for; ignored", name.c_str());
						continue;
					}
					g_s.swapIntents.erase(intent);
					const std::string other = nameOf(otherId);
					int a = -1, b = -1;
					for (int i = 0; i < 8; i++) {
						const GameRoomSlot &s = snap.slots[i];
						if (!s.used || s.type != 3) continue;
						if (PlayerNameMatches(s.name, name)) a = i;
						if (PlayerNameMatches(s.name, other)) b = i;
					}
					if (a < 0 || b < 0 || a == b) {
						LogW("room", "slot swap between %s and %s accepted, but it can no longer be done", name.c_str(), other.c_str());
						continue;
					}
					LogD("room", "%s (slot %d) and %s (slot %d) swap slots", name.c_str(), a + 1, other.c_str(), b + 1);
					g_s.savePlaced.insert(LowerName(name));
					g_s.savePlaced.insert(LowerName(other));
					g_s.pendingSeat[LowerName(name)] = {b, GetTickCount()};
					g_s.pendingSeat[LowerName(other)] = {a, GetTickCount()};
					GameRoomSwapSlots(a, b);
				}
			}
			if (g_s.swapAskFrom && GetTickCount() - g_s.swapAskTime > 20000) {
				SteamSendSwap(g_s.swapAskFrom, "no");
				g_s.swapAskFrom = 0;
			}
			if (g_s.swapPendingTo && GetTickCount() - g_s.swapPendingTime > 25000) g_s.swapPendingTo = 0;
			if (!active) {
				g_s.swapAskFrom = 0;
				g_s.swapPendingTo = 0;
				g_s.moveFrom = -1;
			}
		}
		if (!g_s.open && g_mainMenu && !active && *reinterpret_cast<void **>(addr::kGame) == nullptr && SteamCurrentRoom().id && !RejoinBusy()) {
			LogI("lobby", "in a room by invitation; opening the menu to join the game");
			OpenMpMenu(false);
		}
		if (g_s.inOnlineGame && *reinterpret_cast<void **>(addr::kGame) != nullptr && GetTickCount() - g_s.camTick > 3000) {
			g_s.camTick = GetTickCount();
			int player = 0, x = 0, y = 0;
			if (MpSaveLocalCamera(player, x, y)) {
				MpSaveSetPlayerCamera(player, x, y);
				SteamBroadcastCamera(player, x, y);
			}
		}
		if (g_s.inOnlineGame && *reinterpret_cast<void **>(addr::kGame) == nullptr && !RejoinReloading()) {
			g_s.inOnlineGame = false;
			g_s.classicPending = false;
			if (g_userQuit.tick && GetTickCount() - g_userQuit.tick < 120000) ClearOnlineGame();
			g_userQuit.id = 0;
			g_userQuit.tick = 0;
			SteamLeaveLobby();
		}
		if (g_s.connecting && GetTickCount() - g_s.connectStart > 65000) {
			g_s.connecting = false;
			g_s.classicPending = false;
			ModUiToast("Couldn't join the game. Try again in a moment.");
		}
	}
	void *dlg = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.classicPending && GetTickCount() - g_s.classicPendingAt > 20000) {
			g_s.classicPending = false;
			LogW("lobby", "the classic menu requested more than 20 s ago no longer opens");
		}
		if (!g_s.classicPending || !g_mainMenu) return;
		dlg = g_mainMenu;
	}
	LogI("lobby", "opening the classic menu");
	char *b = static_cast<char *>(reinterpret_cast<GetChild_t>((*reinterpret_cast<void ***>(dlg))[0x7c / 4])(dlg, nullptr,
		kIdMultiplayer, *reinterpret_cast<const char **>(addr::kClassNameCUIButton)));
	if (!b) {
		LogW("lobby", "the Multiplayer button of the main menu was not found");
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.classicPending = false;
		g_s.connecting = false;
		return;
	}
	typedef void(__fastcall * SendNotify_t)(void *, void *, int, unsigned, int, int, int);
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(b))[0x6c / 4])(b, nullptr, *reinterpret_cast<int *>(b + 0x34),
		kMsgButtonClick, *reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
}
