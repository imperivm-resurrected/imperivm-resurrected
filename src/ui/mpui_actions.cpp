// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>

#include "ui/textedit.h"
#include "MinHook.h"
#include "core/log.h"
#include "core/names.h"
#include "game/mappreview.h"
#include "ui/modui.h"
#include "mods/civsets.h"
#include "mods/modoptions.h"
#include "game/civs.h"
#include "net/rejoin.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
void DoAction(int action, int arg)
{
	if (g_s.game.active && RoomActionFrozen(g_s.game, action, arg)) {
		ModUiToast("You are ready: press \"Not ready\" to change this.");
		return;
	}
	if (action != kChatFocus && action != kFieldName && action != kFieldPass && action != kChatSend &&
		action != kFieldJoinPass && action != kJoin && action != kFieldSearch && action != kPassShow) {
		g_s.focus = kNoField;
	}
	switch (action) {
	case kQuick: {
		for (const OnlineLobby &g : g_s.lobbies) {
			if (g.players < g.maxPlayers && !g.playing && !g.password && !g.kickedMe && SteamRoomContent(g, g_s.ownKey) == RoomContent::kSame) {
				TryJoin(g, "");
				return;
			}
		}
		ModUiToast(g_s.ready ? "There are no free rooms with your content right now. Create one with \"Create game\"."
							 : Tr("Online is not available: ") + g_s.status);
		SteamRefreshLobbies();
		break;
	}
	case kRanked: g_s.view = kViewRanked; break;
	case kCreate:
		if (g_s.nameInput.empty()) g_s.nameInput = Tr("Game of ") + g_s.player;
		g_s.view = kViewCreate;
		break;
	case kListView: g_s.view = kViewList; break;
	case kClassic:
		g_s.open = false;
		g_s.classicPending = true;
		g_s.classicPendingAt = GetTickCount();
		LogI("lobby", "menu closed (classic menu)");
		break;
	case kBack:
		if (g_s.recoverAsk || g_s.recoverWaitHost) {
			DoAction(kRecoverNo, 0);
			break;
		}
		if (g_s.passPrompt) {
			DoAction(kPassCancel, 0);
			break;
		}
		if (g_s.syncConfirm) {
			g_s.syncConfirm = false;
			break;
		}
		if (g_s.game.active) LeaveGameRoom();
		else if (g_s.connecting || (g_s.room.id && !g_s.game.active)) {
			DoAction(kLeave, 0);
		}
		else if (g_s.view != kViewList) g_s.view = kViewList;
		else {
			g_s.open = false;
			LogI("lobby", "menu closed (Back)");
			if (g_s.room.id) SteamLeaveLobby();
			g_s.autoHost = false;
			g_flow.backToMain = true;
		}
		break;
	case kFilter:
		g_s.filter = arg;
		g_s.detail.scroll = 0;
		g_s.selected = 0;
		break;
	case kRefresh: SteamRefreshLobbies(); break;
	case kSort:
		if (g_s.sortCol == arg) g_s.sortDesc = !g_s.sortDesc;
		else {
			g_s.sortCol = arg;
			g_s.sortDesc = arg == 2;
		}
		g_s.selected = 0;
		break;
	case kColResize:
		g_s.resizeCol = arg;
		g_s.resizeStartX = g_s.mouseX;
		g_s.resizeStartW = g_s.colW[arg & 3];
		break;
	case kSelect:
		g_s.selected = arg;
		g_s.detail.scroll = 0;
		g_s.joinPassInput.clear();
		break;
	case kPassOk:
		if (g_s.joinPassInput.empty()) {
			ModUiToast("Enter the room password");
			FocusField(kJoinPassField);
			break;
		}
		g_s.passPrompt = false;
		TryJoin(g_s.passLobby, g_s.joinPassInput, g_s.passWatch);
		g_s.joinPassInput.clear();
		break;
	case kPassCancel:
		g_s.passPrompt = false;
		g_s.joinPassInput.clear();
		g_s.focus = kNoField;
		break;
	case kJoin: {
		const std::vector<int> vis = Visible(g_s);
		if (g_s.selected >= 0 && g_s.selected < static_cast<int>(vis.size())) {
			TryJoin(g_s.lobbies[vis[g_s.selected]], g_s.joinPassInput);
		}
		break;
	}
	case kSpectate: {
		const std::vector<int> vis = Visible(g_s);
		if (g_s.selected >= 0 && g_s.selected < static_cast<int>(vis.size())) {
			TrySpectate(g_s.lobbies[vis[g_s.selected]], g_s.joinPassInput);
		}
		break;
	}
	case kChatFocus: FocusField(kChatField); break;
	case kChatBottom: {
		std::lock_guard<std::mutex> lock(g_chat.mx);
		g_chat.scroll = 0;
		g_chat.unread = 0;
		break;
	}
	case kFieldName: FocusField(kNameField); break;
	case kFieldPass: FocusField(kPassField); break;
	case kPassShow: g_s.passShow = !g_s.passShow; break;
	case kFieldJoinPass: FocusField(kJoinPassField); break;
	case kFieldSearch: FocusField(kSearchField); break;
	case kChatSend:
		if (!g_s.chatInput.empty() && g_s.room.id) {
			SteamSendChat(g_s.chatInput);
			g_s.chatInput.clear();
		}
		break;
	case kInvite:
		if (RoomHasNoSeat(g_s)) {
			ModUiToast("No free slots left: open one to be able to invite.");
			break;
		}
		SteamInviteFriends();
		break;
	case kLeave:
		SteamLeaveLobby();
		AbortJoin();
		break;
	case kHostGame:
		g_s.connecting = true;
		g_s.connectStart = GetTickCount();
		g_s.classicPending = true;
		g_s.classicPendingAt = GetTickCount();
		g_flow.backToMain = false;
		g_flow.hostPending = true;
		g_flow.nameSwapPending = true;
		SteamSetHosting(true);
		break;
	case kEnterGame:
		g_s.connecting = true;
		g_s.connectStart = GetTickCount();
		g_s.classicPending = GetTickCount() - g_flow.lastLobbyUpdate >= 1000;
		g_s.classicPendingAt = GetTickCount();
		g_flow.backToMain = false;
		g_autoJoin.active = true;
		g_autoJoin.start = GetTickCount();
		g_autoJoin.refresh = 0;
		g_autoJoin.lan = false;
		g_autoJoin.selected = 0;
		g_autoJoin.clickedAt = 0;
		g_autoJoin.clicks = 0;
		g_autoJoin.items = nullptr;
		g_autoJoin.itemCount = 0;
		g_flow.nameSwapPending = true;
		break;
	case kGDropOpen:
		g_s.dropOpen = true;
		g_s.dropKind = arg & 0xf;
		g_s.dropIndex = arg >> 4;
		{
			int sel = -1;
			const int count = static_cast<int>(DropItems(g_s, sel).size());
			g_s.dropScroll = ClampDropScroll(sel - kDropRows / 2, count);
		}
		break;
	case kGDropClose: g_s.dropOpen = false; break;
	case kGDropPick: {
		g_s.dropOpen = false;
		const int idx = g_s.dropIndex;
		if (g_s.dropKind == kDropPickSort) {
			g_s.pickSort = std::max(0, std::min(2, arg));
			g_s.pick.scroll = 0;
		}
		else if (g_s.dropKind == kDropFilter) {
			g_s.filter = arg;
			g_s.selected = 0;
		}
		else if (g_s.dropKind == kDropSaveGame) {
			g_s.createSave = arg - 1;
		}
		else if (g_s.dropKind == kDropVisibility) {
			g_s.createVisibility = std::max(0, std::min(2, arg));
		}
		else if (g_s.dropKind == kDropPlayers) {
			g_s.createPlayers = std::max(2, std::min(8, arg + 2));
		}
		else if (g_s.dropKind == kDropSpectators) {
			g_s.createSpec = std::max(0, std::min(4, arg));
			if (g_s.room.id && g_s.room.owner) {
				static const int kDelays[] = {0, 30, 60, 120, 0};
				SteamSetSpectators(g_s.createSpec != 0, kDelays[g_s.createSpec]);
			}
		}
		else if (g_s.dropKind == kDropProfile) {
			if (arg >= 0 && arg < static_cast<int>(g_s.profiles.size())) {
				g_s.createProfile = g_s.profiles[arg];
				g_s.createProfileInfo = ProfileSummaryFor(g_s.createProfile), g_s.createModsList = ListState();
				g_s.createCivSets.clear();
				g_s.createCivList = ListState();
				const std::string chosen = g_s.createSave >= 0 && g_s.createSave < static_cast<int>(g_s.saves.size()) ? g_s.saves[g_s.createSave].vfsPath : "";
				g_s.saves = ListSaves();
				g_s.createSave = -1;
				for (size_t i = 0; i < g_s.saves.size() && !chosen.empty(); i++) {
					if (_stricmp(g_s.saves[i].vfsPath.c_str(), chosen.c_str()) == 0) g_s.createSave = static_cast<int>(i);
				}
			}
		}
		else if (g_s.dropKind == kDropColor) {
			g_s.game.slots[idx & 7].color = arg;
			Predict(kPredColor, idx & 7, arg);
			GameRoomSetColor(idx & 7, arg);
		}
		else if (g_s.dropKind == kDropBonus) {
			g_s.game.slots[idx & 7].bonus = arg;
			GameRoomSetBonus(idx & 7, arg);
		}
		else if (g_s.dropKind == kDropRace) {
			GameRoomSlot &s = g_s.game.slots[idx & 7];
			const int real = RaceRealIndex(s.raceNames, arg);
			s.race = real;
			Predict(kPredRace, idx & 7, real);
			GameRoomSetRace(idx & 7, real);
		}
		else if (g_s.dropKind == kDropAi) {
			GameRoomSlot &s = g_s.game.slots[idx & 7];
			s.nameSel = arg;
			Predict(kPredAi, idx & 7, arg);
			GameRoomSetSlotItem(idx & 7, arg);
		}
		else if (g_s.dropKind == kDropSites) {
			if (arg >= 0 && arg < static_cast<int>(sizeof(kSiteChoices) / sizeof(kSiteChoices[0]))) {
				g_s.game.siteFlags ^= kSiteChoices[arg].bit;
				GameRoomSetSiteFlags(g_s.game.siteFlags);
				g_s.dropOpen = true;
			}
		}
		else if (g_s.dropKind == kDropModOption) {
			const std::vector<ModOption> &mods = ModOptions();
			if (idx >= 0 && idx < static_cast<int>(mods.size()) && arg >= 0 && arg < static_cast<int>(mods[idx].values.size())) {
				g_s.game.rules = ModOptionSet(mods[idx], g_s.game.rules, arg);
				GameRoomSetRules(g_s.game.rules);
			}
		}
		else if (g_s.dropKind == kDropPreset) {
			if (arg >= 0 && arg < kPresetCount) {
				const Preset &pr = kPresets[arg];
				g_s.game.siteFlags = pr.sites;
				g_s.game.rules = pr.rules | (g_s.game.rules & ModOptionsMask());
				GameRoomSetSiteFlags(pr.sites);
				GameRoomSetRules(g_s.game.rules);
				if (pr.game) {
					auto setCombo = [&](int id, const char *text) {
						for (GameRoomOption &o : g_s.game.options) {
							if (o.id != id) continue;
							for (size_t k = 0; k < o.items.size(); k++) {
								if (o.items[k] != text || o.sel == static_cast<int>(k)) continue;
								o.sel = static_cast<int>(k);
								Predict(kPredOptSel, o.id, static_cast<int>(k));
								GameRoomSetOption(o.id, static_cast<int>(k));
							}
						}
					};
					auto setCheck = [&](int id, bool value) {
						for (GameRoomOption &o : g_s.game.options) {
							if (o.id != id || o.value == value) continue;
							o.value = value;
							Predict(kPredOptVal, o.id, o.value);
							GameRoomToggleOption(o.id);
						}
					};
					setCombo(0x1005, pr.gold);
					setCombo(0x1004, pr.pop);
					setCheck(0x1007, !pr.fog);
					setCheck(0x1008, pr.explored);
					setCheck(0x10a7, !pr.bonuses);
				}
				LogI("room", "preset %s", pr.name);
			}
		}
		else if (g_s.dropKind == kDropSlot) {
			GameRoomSlot &s = g_s.game.slots[idx & 7];
			int cur = 0;
			const std::vector<SlotChoice> choices = SlotChoices(g_s.game, s, cur);
			if (arg >= 0 && arg < static_cast<int>(choices.size()) && arg != cur) {
				const SlotChoice &c = choices[arg];
				if (c.type != s.type) {
					if (!SlotChangeBlocked(idx, c.type)) GameRoomSetSlotType(idx & 7, c.type);
				}
				else if (c.type == 1 && c.aiItem >= 0) {
					s.nameSel = c.aiItem;
					Predict(kPredAi, idx & 7, c.aiItem);
					GameRoomSetSlotItem(idx & 7, c.aiItem);
				}
			}
		}
		else if (idx >= 0 && idx < static_cast<int>(g_s.game.options.size())) {
			GameRoomOption &o = g_s.game.options[idx];
			if (arg >= 0 && arg < static_cast<int>(o.itemDisabled.size()) && o.itemDisabled[arg]) {
				ModUiToast("This map type does not exist in that size");
				break;
			}
			o.sel = arg;
			Predict(kPredOptSel, o.id, arg);
			GameRoomSetOption(o.id, arg);
		}
		break;
	}
	case kGTeam: {
		GameRoomSlot &s = g_s.game.slots[arg & 7];
		s.team = (s.team < 0 || s.team > 4 ? 0 : s.team + 1) % 5;
		Predict(kPredTeam, arg & 7, s.team);
		GameRoomCycleTeam(arg & 7);
		break;
	}
	case kGReady: {
		GameRoomSlot &me = g_s.game.slots[g_s.game.localSlot & 7];
		me.ready = !me.ready;
		Predict(kPredReady, g_s.game.localSlot & 7, me.ready);
		GameRoomToggleReady();
		break;
	}
	case kGStart: {
		const GameRoomSnapshot snap = GameRoomGet();
		{
			std::string without;
			int positions = 0;
			for (const GameRoomSlot &s : snap.slots) {
				if (s.exists) positions++;
				if (s.used && s.type == 3 && !s.exists) without += (without.empty() ? "" : ", ") + s.name;
			}
			if (!without.empty()) {
				ModUiToast(Tr("This map is for ") + std::to_string(positions) + Tr(" players: move or kick ") + without + Tr(" before starting."));
				break;
			}
		}
		if (const StartBlock block = StartBlocker(g_s, snap); !block.toast.empty()) {
			LogI("room", "not starting yet: %s", block.toast.c_str());
			ModUiToast(block.toast);
			break;
		}
		GameRoomStart();
		break;
	}
	case kGCancel: LeaveGameRoom(); break;
	case kGResync:
		LogI("lobby", "leaving the room to download the content of its map and come back");
		g_s.forcePackLobby = g_s.room.id;
		g_s.leftByUser = true;
		GameRoomCancel();
		SteamLeaveForContent(true);
		break;
	case kGRuleToggle:
		g_s.game.rules ^= static_cast<unsigned>(arg);
		GameRoomSetRules(g_s.game.rules);
		break;
	case kGSiteToggle:
		g_s.game.siteFlags ^= static_cast<unsigned>(arg);
		GameRoomSetSiteFlags(g_s.game.siteFlags);
		break;
	case kGOptToggle:
		for (GameRoomOption &o : g_s.game.options) {
			if (o.id != arg) continue;
			o.value = !o.value;
			Predict(kPredOptVal, o.id, o.value);
		}
		GameRoomToggleOption(arg);
		break;
	case kGColor: DoAction(kGDropOpen, kDropColor | ((arg & 7) << 4)); break;
	case kGKickJoining:
		if (arg >= 0 && arg < 8 && g_flow.arrivingIds[arg]) SteamKick(g_flow.arrivingIds[arg]);
		break;
	case kGMove:
		g_s.moveFrom = g_s.moveFrom == arg ? -1 : arg;
		break;
	case kGMoveTo: {
		const GameRoomSnapshot snap = GameRoomGet();
		const int from = g_s.moveFrom;
		g_s.moveFrom = -1;
		if (!snap.host || from < 0 || from >= 8 || arg < 0 || arg >= 8 || arg == from || !snap.slots[arg].exists) break;
		const GameRoomSlot &a = snap.slots[from], &b = snap.slots[arg];
		LogD("room", "the host swaps slot %d (%s) with %d (%s)", from + 1, a.name.c_str(), arg + 1, b.used ? b.name.c_str() : "empty");
		if (a.used && a.type == 3) {
			g_s.savePlaced.insert(LowerName(a.name));
			g_s.pendingSeat[LowerName(a.name)] = {arg, GetTickCount()};
		}
		if (b.used && b.type == 3) {
			g_s.savePlaced.insert(LowerName(b.name));
			g_s.pendingSeat[LowerName(b.name)] = {from, GetTickCount()};
		}
		GameRoomSwapSlots(from, arg);
		break;
	}
	case kGSwapAsk: {
		const GameRoomSnapshot snap = GameRoomGet();
		if (arg < 0 || arg >= 8 || !snap.slots[arg].used || snap.slots[arg].type != 3 || snap.slots[arg].local) break;
		const uint64_t id = MemberIdByName(g_s, snap.slots[arg].name);
		if (!id) break;
		g_s.swapPendingTo = id;
		g_s.swapPendingName = snap.slots[arg].name;
		g_s.swapPendingTime = GetTickCount();
		SteamSendSwap(id, "ask");
		{
			uint64_t hostId = 0;
			for (const OnlineMember &m : g_s.room.members) {
				if (m.owner) hostId = m.id;
			}
			if (hostId) SteamSendSwap(hostId, "intent " + std::to_string(id));
		}
		ModUiToast(Tr("You sent a swap request to ") + snap.slots[arg].name + Tr(". They must accept it."));
		break;
	}
	case kSwapAccept: {
		uint64_t owner = 0;
		for (const OnlineMember &m : g_s.room.members) {
			if (m.owner) owner = m.id;
		}
		if (owner && g_s.swapAskFrom) SteamSendSwap(owner, "ok " + std::to_string(g_s.swapAskFrom));
		g_s.swapAskFrom = 0;
		break;
	}
	case kDropYes: g_s.dropAnswer = 1; break;
	case kDropNo: g_s.dropAnswer = 0; break;
	case kSwapDecline:
		if (g_s.swapAskFrom) SteamSendSwap(g_s.swapAskFrom, "no");
		g_s.swapAskFrom = 0;
		break;
	case kGSeat: {
		const GameRoomSnapshot snap = GameRoomGet();
		if (arg < 0 || arg >= 8 || arg == snap.localSlot || !SeatTakeable(snap.slots[arg])) break;
		if (snap.host && (g_flow.arrivingIds[arg] || snap.slots[arg].joining)) {
			ModUiToast("That slot is being taken by a player who is joining.");
			break;
		}
		if (snap.host) {
			if (snap.localSlot >= 0 && snap.localSlot < 8) {
				g_s.savePlaced.insert(LowerName(snap.slots[snap.localSlot].name));
				g_s.pendingSeat[LowerName(snap.slots[snap.localSlot].name)] = {arg, GetTickCount()};
				GameRoomSwapSlots(snap.localSlot, arg);
			}
		}
		else {
			SteamRequestSeat(arg);
		}
		break;
	}
	case kGKick: {
		const std::string name = g_s.game.slots[arg & 7].name;
		const uint64_t victim = MemberIdByName(g_s, name, true);
		if (victim) SteamKick(victim);
		GameRoomKick(arg & 7);
		break;
	}
	case kGSlotType:
		if (!SlotChangeBlocked(arg, arg >> 4)) GameRoomSetSlotType(arg & 7, arg >> 4);
		break;
	case kRecoverYes: {
		const RecoverInfo r = g_s.recover;
		g_s.recoverAsk = false;
		ClearOnlineGame();
		if (r.host && r.saveIndex >= 0 && r.saveIndex < static_cast<int>(g_s.saves.size())) {
			int players = 1;
			if (!r.others.empty()) players += 1 + static_cast<int>(std::count(r.others.begin(), r.others.end(), ','));
			g_s.createSave = r.saveIndex;
			g_s.createPlayers = std::max(2, std::min(8, players));
			g_s.createVisibility = 0;
			g_s.passInput.clear();
			g_s.nameInput = Tr("Game of ") + g_s.player;
			g_s.createProfile = ModsMountedProfile().name;
			g_s.createCivSets.clear();
			DoAction(kCreateOk, 0);
		}
		else if (!r.host) {
			RecordOnlineGameRaw(r);
			std::vector<std::pair<uint64_t, std::string>> others;
			std::vector<std::string> names;
			for (size_t p = 0; !r.others.empty();) {
				const size_t q = r.others.find(", ", p);
				names.push_back(r.others.substr(p, q == std::string::npos ? std::string::npos : q - p));
				if (q == std::string::npos) break;
				p = q + 2;
			}
			size_t k = 0;
			for (size_t p = 0; !r.otherIds.empty(); k++) {
				const size_t q = r.otherIds.find(',', p);
				const uint64_t id = _strtoui64(r.otherIds.substr(p, q == std::string::npos ? std::string::npos : q - p).c_str(), nullptr, 10);
				if (id) others.push_back({id, k < names.size() ? names[k] : std::string("another player")});
				if (q == std::string::npos) break;
				p = q + 1;
			}
			RejoinRequest(r.hostId, r.hostName, others);
		}
		break;
	}
	case kReturnGame: {
		const std::vector<int> vis = Visible(g_s);
		if (g_s.selected >= 0 && g_s.selected < static_cast<int>(vis.size())) {
			const OnlineLobby &g = g_s.lobbies[vis[g_s.selected]];
			if (CanReturnToGame(g_s, g) && !RejoinBusy()) {
				RecoverInfo r;
				r.start = UnixNow();
				r.hostId = g.owner;
				r.hostName = g.host;
				g_s.recover = r;
				LogI("lobby", "returning to the game of %s from the list", g.host.c_str());
				DoAction(kRecoverYes, 0);
			}
		}
		break;
	}
	case kRejoinCancel:
		RejoinCancel();
		break;
	case kRejoinClose:
		RejoinClientDismiss();
		break;
	case kRecoverNo:
		g_s.recoverAsk = false;
		g_s.recoverWaitHost = 0;
		ClearOnlineGame();
		break;
	case kPickRow: {
		const DWORD now = GetTickCount();
		const bool twice = arg == g_s.pickLastIndex && now - g_s.pickLastClick < 450;
		g_s.pickSel = arg;
		GameRoomPickerSelect(arg);
		g_s.pickLastIndex = arg;
		g_s.pickLastClick = now;
		g_s.focus = kNoField;
		if (twice) GameRoomPickerAccept();
		break;
	}
	case kFieldPickSearch: FocusField(kPickSearchField); break;
	case kPickDelete:
		if (g_s.pickSel >= 0 && g_s.pickSel < static_cast<int>(g_s.game.pickerPaths.size()) && !PickInUse(g_s, g_s.pickSel)) g_s.pickDelete = g_s.pickSel;
		break;
	case kPickDeleteNo: g_s.pickDelete = -1; break;
	case kPickDeleteYes: {
		const int index = g_s.pickDelete;
		g_s.pickDelete = -1;
		if (index < 0 || index >= static_cast<int>(g_s.game.pickerPaths.size())) break;
		std::string message;
		const bool ok = ModsDeleteMap(g_s.game.pickerPaths[index], message);
		if (ok) {
			std::string key = g_s.game.pickerPaths[index];
			for (char &c : key) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (key.size() > 5 && key.compare(key.size() - 5, 5, ".bfhp") == 0) key.resize(key.size() - 5);
			g_s.pickGone.insert(key);
			g_s.pickSel = -1;
			MapPreviewReset();
		}
		g_s.pickNote = message;
		g_s.pickNoteUntil = GetTickCount() + 6000;
		break;
	}
	case kPickOk:
		if (g_s.pickSel >= 0) GameRoomPickerAccept();
		break;
	case kPickCancel: GameRoomPickerCancel(); break;
	case kSyncAccept:
	case kSyncUpdate:
		g_s.syncUpdate = action == kSyncUpdate;
		g_s.syncConfirm = false;
		g_s.syncPhase = 1;
		g_s.syncStage = 0;
		g_s.syncStart = GetTickCount();
		g_s.syncError.clear();
		SteamResetModDownload();
		SteamJoinLobby(g_s.syncLobby.id, g_s.syncPass, true);
		break;
	case kSyncCancel:
		if (g_s.syncStage == 1) RoomModsCancel();
		g_s.syncStage = 0;
		if (g_s.syncPhase && g_s.room.id) SteamLeaveLobby();
		g_s.syncConfirm = false;
		g_s.syncPhase = 0;
		break;
	case kCancelSearch: g_s.searching = false; break;
	case kAccept:
		g_s.searching = false;
		g_s.found = false;
		ModUiToast("Preview: ranked matches will arrive with the league server");
		break;
	case kDecline:
		g_s.found = false;
		g_s.searching = false;
		break;
	case kQueue1v1:
	case kQueue2v2:
		g_s.searching = true;
		g_s.searchMode = action == kQueue1v1 ? "ranked 1v1" : "ranked 2v2";
		g_s.searchStart = GetTickCount();
		g_s.found = false;
		break;
	case kCivSetToggle: {
		const std::vector<CivSet> sets = CivsSets();
		if (arg < 0 || arg >= static_cast<int>(sets.size())) break;
		// In a skirmish there is no "Create game" screen: the room's own rows edit the sets the game reads.
		const bool skirmish = g_s.game.active && !g_s.game.online;
		const std::vector<std::string> now = skirmish ? CivRoomSets() : g_s.createCivSets;
		std::vector<std::string> chosen;
		for (const CivSet &s : sets) {
			if (CivSetsAllow(now, s.id) != (s.id == sets[arg].id)) chosen.push_back(s.id);
		}
		if (chosen.empty()) break;
		if (chosen.size() == sets.size()) chosen.clear();
		if (skirmish) CivRoomSetsSet(chosen);
		else g_s.createCivSets = chosen;
		break;
	}
	case kPlayersPrev: g_s.createPlayers = std::max(2, g_s.createPlayers - 1); break;
	case kPlayersNext: g_s.createPlayers = std::min(8, g_s.createPlayers + 1); break;
	case kMapPrev: g_s.createMap = (g_s.createMap + 4) % 5; break;
	case kMapNext: g_s.createMap = (g_s.createMap + 1) % 5; break;
	case kCreateOk: {
		if (g_s.createSave >= 0 && g_s.createSave < static_cast<int>(g_s.saves.size())) {
			const SaveEntry &save = g_s.saves[g_s.createSave];
			const ModNeeds needs = ModsNeededFor(save.vfsPath, g_s.createProfile);
			if (!needs.Satisfied()) {
				if (!needs.AllInstalled()) {
					ModUiToast(Tr("The game \"") + save.name + Tr("\" needs content you don't have: ") + ShortList(needs.Names(true)) +
						". Download it in Options > Content > Browse content.");
					break;
				}
				const std::string profile = ModsActivateProfileFor(needs, save.name);
				if (profile.empty()) {
					ModUiToast(Tr("Could not prepare the content profile for the game \"") + save.name + "\".");
					break;
				}
				g_s.open = false;
				ModUiAskRestart("This game needs content that is not enabled. The game will restart to enable it; afterwards, create the room again.", -1,
					needs.Names(true), "Content that will be enabled:");
				break;
			}
		}
		if (g_s.createProfile != ModsMountedProfile().name) {
			ModsSetActiveProfileName(g_s.createProfile);
			g_s.open = false;
			std::vector<std::string> profileMods;
			for (const ModRow &row : g_s.createProfileInfo.rows) profileMods.push_back(row.name);
			ModUiAskRestart(profileMods.empty() ? "The game will restart without additional content. Afterwards, create the room again."
												: "The game will restart to load the room's content. Afterwards, create the room again.",
				-1, profileMods, "Content that will be loaded:");
			break;
		}
		if (g_s.createSave >= 0 && g_s.createSave < static_cast<int>(g_s.saves.size())) {
			const std::string problem = ModsCheckCivIndices(g_s.saves[g_s.createSave].vfsPath);
			if (!problem.empty()) {
				ModUiToast(problem);
				break;
			}
		}
		if (!g_s.ready) {
			ModUiToast(Tr("Online is not available: ") + g_s.status);
			break;
		}
		OnlineCreateSettings s;
		s.name = g_s.nameInput.empty() ? Tr("Game of ") + g_s.player : g_s.nameInput;
		s.map = "Chosen in the room";
		s.mode = "Elimination";
		s.civSets = CivSetsJoin(g_s.createCivSets);
		CivRoomSetsSet(g_s.createCivSets);
		if (g_s.createVisibility == 1 && g_s.passInput.empty()) {
			ModUiToast("Enter the room password.");
			FocusField(kPassField);
			break;
		}
		s.password = g_s.createVisibility == 1 ? g_s.passInput : "";
		s.maxPlayers = g_s.createPlayers;
		s.visibility = g_s.createVisibility == 2 ? 1 : 0;
		{
			static const int kSpecDelays[] = {0, 30, 60, 120, 0};
			s.spectators = g_s.createSpec != 0;
			s.specDelay = kSpecDelays[std::max(0, std::min(4, g_s.createSpec))];
		}
		g_s.hostSave = g_s.createSave >= 0 && g_s.createSave < static_cast<int>(g_s.saves.size()) ? g_s.saves[g_s.createSave] : SaveEntry();
		g_s.hostSavePublished = false;
		g_s.savePlayersRead = false;
		g_s.savePlayers.clear();
		g_s.savePlaced.clear();
		SteamCreateLobby(s);
		g_s.roomLimit = g_s.hostSave.name.empty() ? g_s.createPlayers : 0;
		g_s.autoHost = true;
		g_s.view = kViewList;
		g_s.connecting = true;
		g_s.connectStart = GetTickCount();
		g_s.view = kViewList;
		break;
	}
	}
}

std::string &FocusedText()
{
	switch (g_s.focus) {
	case kChatField: return g_s.chatInput;
	case kNameField: return g_s.nameInput;
	case kJoinPassField: return g_s.joinPassInput;
	case kSearchField: return g_s.searchInput;
	case kPickSearchField: return g_s.pickSearch;
	default: return g_s.passInput;
	}
}

int FieldOfAction(int action)
{
	return action == kChatFocus ? kChatField : action == kFieldName ? kNameField : action == kFieldPass ? kPassField
		: action == kFieldJoinPass ? kJoinPassField : action == kFieldSearch ? kSearchField
		: action == kFieldPickSearch ? kPickSearchField : kNoField;
}

int ActionOfField(int field)
{
	return field == kChatField ? kChatFocus : field == kNameField ? kFieldName : field == kPassField ? kFieldPass
		: field == kJoinPassField ? kFieldJoinPass : field == kSearchField ? kFieldSearch
		: field == kPickSearchField ? kFieldPickSearch : -1;
}

void FocusField(int field)
{
	g_s.focus = field;
	g_s.tabAction = -1;
	if (field != kNoField) EditToEnd(FocusedText(), g_s.edit);
}
}

namespace mpui_impl {
namespace {
bool TabSkip(int a)
{
	return a == kNone || a == kSelect || a == kPickRow || a == kColResize ||
		a == kGDropClose || a == kSort;
}
}
}

namespace mpui_impl {
void TabStep(bool back)
{
	FocusItem cur = {g_s.tabAction, g_s.tabArg};
	if (g_s.focus != kNoField) cur = {ActionOfField(g_s.focus), 0};
	const FocusItem next = FocusStep(FocusOrder(g_s.hits, TabSkip), cur, back);
	if (next.action < 0) return;
	g_s.focus = kNoField;
	g_s.tabAction = next.action;
	g_s.tabArg = next.arg;
	if (FieldOfAction(next.action) != kNoField) {
		DoAction(next.action, next.arg);
		g_s.tabAction = next.action;
		g_s.tabArg = 0;
	}
}

void FieldKey(UINT msg, WPARAM wp)
{
	std::string &text = FocusedText();
	if (msg == WM_CHAR) {
		if (EditChar(text, g_s.edit, wp, 200) && g_s.focus == kPickSearchField) g_s.pick.scroll = 0;
	}
	else if (g_s.focus == kPickSearchField && (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT)) {
		PickMove(wp);
	}
	else if (wp == VK_RETURN) {
		if (g_s.focus == kPickSearchField) DoAction(kPickOk, 0);
		else if (g_s.focus == kChatField) DoAction(kChatSend, 0);
		else if (g_s.focus == kJoinPassField) DoAction(kPassOk, 0);
		else g_s.focus = kNoField;
	}
	else if (wp == VK_ESCAPE) {
		if (g_s.focus == kPickSearchField && !g_s.pickSearch.empty()) {
			g_s.pickSearch.clear();
			g_s.edit = EditState();
		}
		else {
			g_s.focus = kNoField;
		}
	}
	else {
		if (EditKey(text, g_s.edit, wp, 200) == 2 && g_s.focus == kPickSearchField) g_s.pick.scroll = 0;
	}
}

void OpenMpMenu(bool checkRecover)
{
	g_s.open = true;
	g_s.skirmishCivs = false;
	g_s.view = kViewList;
	g_s.mods = ModsList();
	g_s.profiles = ModsProfileNames();
	g_s.createProfile = ModsMountedProfile().name;
	g_s.createCivSets.clear();
	g_s.createProfileInfo = ProfileSummaryFor(g_s.createProfile), g_s.createModsList = ListState();
	g_s.saves = ListSaves();
	g_s.createSave = -1;
	if (checkRecover) CheckRecover();
	g_s.ownKey = SteamOwnGameplayKey();
	g_s.lastRefresh = 0;
	LogI("lobby", "new menu open");
}
}
