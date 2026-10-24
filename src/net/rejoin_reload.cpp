// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include "MinHook.h"
#include "core/log.h"
#include "core/sha256.h"
#include "game/autosave.h"
#include "i18n/tr.h"
#include "core/framefreeze.h"
#include "core/json.h"
#include "mods/modoptions.h"
#include "mods/civindices.h"
#include "net/mpsave.h"
#include "net/steamnet.h"
#include "addr/state.h"

#include "net/rejoin_internal.h"
#include "core/uiport.h"

using namespace rejoin_impl;

namespace rejoin_impl {
ReloadState g_reload;

namespace {
const char kReloadSave[] = "SaveOnline/reload.snr";
const char kReloadSettings[] = "SaveOnline/reload.usr";
const char kNotPlaying[] = "#out";
const size_t kChunk = 60 * 1024;
const DWORD kDataMs = 90000;
const DWORD kSlotMs = 30000;
const DWORD kAcksMs = 120000;
const DWORD kGoMs = 150000;
const DWORD kQuitMs = 30000;
const DWORD kLoadMs = 120000;
const DWORD kGoneMs = 3000;

void Arm(int phase, DWORD ms)
{
	g_reload.phase = phase;
	g_reload.deadline = GetTickCount() + ms;
}

bool Committed()
{
	return g_reload.phase >= kRQuit;
}

const char *TimeoutReason(int phase)
{
	switch (phase) {
	case kRWaitData: return "the host did not send the game";
	case kRWaitSlot: return "your spot in the game could not be found";
	case kRWaitGo: return "the host did not confirm the reload";
	case kRWaitAcks: return "a player did not receive the game in time";
	case kRQuit: return "the game did not close";
	case kRStart: return "the game did not start";
	default: return "the game did not load";
	}
}

bool InRoom(uint64_t peer)
{
	for (const OnlineMember &m : SteamCurrentRoom().members) {
		if (m.id == peer) return true;
	}
	return false;
}

bool FromRoomOwner(uint64_t peer)
{
	for (const OnlineMember &m : SteamCurrentRoom().members) {
		if (m.id == peer) return m.owner;
	}
	return false;
}

unsigned CurrentRules()
{
	const unsigned active = ModRulesActive();
	return active ? active : SteamCurrentRoom().info.rules;
}

int MyNumber()
{
	for (const OnlineMember &m : SteamCurrentRoom().members) {
		if (m.me) return m.number;
	}
	return 0;
}

int MySlot(const WireState &w)
{
	const int k = MyNumber();
	if (k <= 0) return -1;
	const uint32_t host = 0x7f4d0000u | static_cast<uint32_t>(k);
	const uint32_t net = (static_cast<uint32_t>(k) << 24) | 0x00004d7fu;
	for (int i = 0; i < 8; i++) {
		const uint32_t ip = w.multi[4 + i];
		if (ip == host || ip == net) return i;
	}
	return -1;
}

std::string CheckReceived()
{
	if (g_reload.wireRaw.size() < sizeof(WireState)) return "incomplete state";
	memcpy(&g_reload.wire, g_reload.wireRaw.data(), sizeof(WireState));
	uint64_t blobs = 0;
	for (uint32_t b : g_reload.wire.blob) blobs += b;
	if (g_reload.wire.magic != kWireMagic || sizeof(WireState) + blobs + g_reload.wire.settingsSize > g_reload.wireRaw.size()) {
		return "corrupted state";
	}
	if (g_reload.save.size() != g_reload.wire.saveSize) return "incomplete save";
	return "";
}

void Forget(std::vector<uint64_t> &list, uint64_t peer)
{
	list.erase(std::remove(list.begin(), list.end(), peer), list.end());
}

void Finish()
{
	FrameFreeze(false);
	g_reload = ReloadState();
}

void WaitAcksTick()
{
	if (InRoom(g_reload.returningId)) {
		g_reload.goneSince = 0;
	}
	else if (!g_reload.goneSince) {
		g_reload.goneSince = GetTickCount();
	}
	else if (GetTickCount() - g_reload.goneSince > kGoneMs) {
		return ReloadCancel("left the room (their game closed)", false);
	}
	for (uint64_t p : std::vector<uint64_t>(g_reload.waiting)) {
		if (p == g_reload.returningId || InRoom(p)) continue;
		LogI("rejoin", "reload: %llu left the room; not waiting for them", static_cast<unsigned long long>(p));
		Forget(g_reload.waiting, p);
		Forget(g_reload.peers, p);
	}
	if (!g_reload.waiting.empty()) return;
	for (uint64_t p : g_reload.peers) SteamRejoinSend(p, kRjReloadGo, "");
	Arm(kRQuit, kQuitMs);
	FrameFreeze(true);
	LogI("rejoin", "reload: the %u players have the game; everyone loads it, this computer too", static_cast<unsigned>(g_reload.peers.size()));
	UiToast(TrFmt("%s is rejoining the game: everyone reloads it, it takes a few seconds.", {g_reload.returning}));
}

void StartTick()
{
	if (Game() || !*reinterpret_cast<void **>(addr::kMenuFrame)) return;
	if (SteamCurrentRoom().info.gameplayKey != SteamOwnGameplayKey()) {
		return ReloadCancel(Tr("Rejoining requires the same civilizations and gameplay content as the room."), true);
	}
	const int slot = MySlot(g_reload.wire);
	if (slot < 0) return ReloadCancel("your spot in the game could not be found", false);
	uint64_t blobs = 0;
	for (uint32_t b : g_reload.wire.blob) blobs += b;
	const std::string settings = g_reload.wireRaw.substr(sizeof(WireState) + static_cast<size_t>(blobs), g_reload.wire.settingsSize);
	if (!WriteFileAll(DiskPath(kReloadSave), g_reload.save) || !WriteFileAll(DiskPath(kReloadSettings), settings)) {
		return ReloadCancel("could not write the game", false);
	}
	Json side = Json::MakeObject();
	side.Set("rules", Json::MakeNumber(static_cast<double>(g_reload.rules)));
	if (CivIndicesAutomatic()) {
		const std::string hash = HashFile(DiskPath(kReloadSave));
		if (hash.empty()) return ReloadCancel(Tr("Could not save the civilization index table."), true);
		side.Set("civIndices", CivIndicesToJson());
		side.Set("civSaveHash", Json::MakeString(hash));
	}
	if (!WriteJsonFile(DiskPath(kReloadSave) + ".mods.json", side)) return ReloadCancel(Tr("Could not save the civilization index table."), true);
	g_blobs.wire = g_reload.wire;
	if (!LoadSettings(kReloadSettings, slot)) return ReloadCancel("could not read the game settings", false);
	MpSaveSetPendingLoad(kReloadSave);
	MpSaveSetPendingCameras(g_reload.cams);
	UiRejoinStarting();
	*reinterpret_cast<int *>(*reinterpret_cast<char **>(addr::kMenuFrame) + 0x6c) = 1;
	Arm(kRLoading, kLoadMs);
	LogI("rejoin", "reload: starting the saved game in slot %d", slot);
}
}

void ReloadCancel(const std::string &why, bool tellHost)
{
	if (g_reload.phase == kRNone) return;
	const int phase = g_reload.phase;
	const uint64_t from = g_reload.from;
	const bool mine = g_reload.mine;
	const std::vector<uint64_t> peers = g_reload.peers;
	Finish();
	LogW("rejoin", "reload: cancelled (%s)", why.c_str());
	if (phase == kRWaitAcks) {
		for (uint64_t p : peers) SteamRejoinSend(p, kRjCancel, why);
		Unfreeze();
		UiToast(Tr("Couldn't bring the player back into the game: ") + Tr(why), true);
		return;
	}
	if (phase < kRQuit) {
		if (mine && g_session.role == kBack) return BackFail(why);
		if (tellHost && from) SteamRejoinSend(from, kRjCancel, why);
		if (g_session.role == kOther) {
			Unfreeze();
			ResetRole();
		}
		UiToast(Tr("Couldn't bring the player back into the game: ") + Tr(why), true);
		return;
	}
	MpSaveSetPendingLoad("");
	UiToast(Tr("Couldn't reload the game: ") + Tr(why), true);
	if (phase >= kRStart) SteamLeaveLobby();
}

void ReloadDrop()
{
	if (g_reload.phase != kRNone && !Committed()) Finish();
}

void ReloadBroadcast(const std::string &save, const std::string &cams, const std::string &returning, uint64_t returningId)
{
	const std::string state = BuildState(save);
	g_reload = ReloadState();
	g_reload.peers = OtherMembers();
	for (uint64_t m : g_reload.peers) {
		SteamRejoinSend(m, kRjReload, state);
		for (size_t off = 0; off < save.size(); off += kChunk) SteamRejoinSend(m, kRjReloadFile, save.substr(off, kChunk));
		SteamRejoinSend(m, kRjReloadEnd, cams);
	}
	g_reload.waiting = g_reload.peers;
	g_reload.returning = returning;
	g_reload.returningId = returningId;
	g_reload.wireRaw = state;
	memcpy(&g_reload.wire, state.data(), sizeof(WireState));
	g_reload.save = save;
	g_reload.cams = cams;
	g_reload.rules = CurrentRules();
	Arm(kRWaitAcks, kAcksMs);
	LogI("rejoin", "reload: the game (%u bytes) was sent to %u players; waiting for each one to confirm it", static_cast<unsigned>(save.size()),
		static_cast<unsigned>(g_reload.peers.size()));
}

void ReloadOnMessage(const Msg &m)
{
	if (m.type == kRjReloadAck) {
		if (g_reload.phase != kRWaitAcks || std::find(g_reload.waiting.begin(), g_reload.waiting.end(), m.peer) == g_reload.waiting.end()) return;
		Forget(g_reload.waiting, m.peer);
		if (m.data == kNotPlaying) {
			Forget(g_reload.peers, m.peer);
			LogI("rejoin", "reload: %llu is in the room but not in the game; it stays out", static_cast<unsigned long long>(m.peer));
		}
		else {
			LogI("rejoin", "reload: %llu has the game (%u still missing)", static_cast<unsigned long long>(m.peer),
				static_cast<unsigned>(g_reload.waiting.size()));
		}
		return;
	}
	if (m.type == kRjReload) {
		if (!(g_session.role == kBack && m.peer == g_session.peer) && !FromRoomOwner(m.peer)) {
			LogW("rejoin", "reload: game data from someone who does not hold the room; ignored");
			return;
		}
		if (Committed()) {
			LogW("rejoin", "reload: another reload arrived while this one is loading; ignored");
			return;
		}
		if (g_session.role != kBack && !Game()) {
			SteamRejoinSend(m.peer, kRjReloadAck, kNotPlaying);
			LogI("rejoin", "reload: this computer is not in the game; it stays out");
			return;
		}
		g_reload = ReloadState();
		g_reload.from = m.peer;
		g_reload.mine = g_session.role == kBack;
		g_reload.rules = CurrentRules();
		g_reload.wireRaw = m.data;
		if (g_session.role == kBack || g_session.role == kOther) g_session.deadline = 0;
		if (g_reload.mine) SetStatus("Receiving the game...");
		Arm(kRWaitData, kDataMs);
		LogI("rejoin", "reload: the host is sending the game");
		return;
	}
	if (g_reload.phase == kRNone || m.peer != g_reload.from) return;
	if (m.type == kRjReloadFile && g_reload.phase == kRWaitData) {
		g_reload.save += m.data;
		return;
	}
	if (m.type == kRjReloadEnd && g_reload.phase == kRWaitData) {
		g_reload.cams = m.data;
		const std::string bad = CheckReceived();
		if (!bad.empty()) return ReloadCancel(bad, true);
		Arm(kRWaitSlot, kSlotMs);
		return;
	}
	if (m.type == kRjReloadGo && g_reload.phase == kRWaitGo) {
		Arm(kRQuit, kQuitMs);
		if (!g_reload.mine) FrameFreeze(true);
		LogI("rejoin", "reload: everyone has the game; loading it again");
		if (g_reload.mine) SetStatus("Loading the game...");
		else UiToast("A player is rejoining the game: everyone reloads it, it takes a few seconds.");
	}
}

bool ReloadOnCancel(const Msg &m)
{
	if (g_reload.phase == kRWaitAcks) {
		if (std::find(g_reload.peers.begin(), g_reload.peers.end(), m.peer) == g_reload.peers.end()) return false;
		LogW("rejoin", "reload: %llu could not prepare it (%s)", static_cast<unsigned long long>(m.peer), m.data.c_str());
		const bool returningCanceled = m.peer == g_reload.returningId && m.data == "canceled it";
		ReloadCancel(returningCanceled ? "the player canceled" : "a player could not prepare the game", false);
		return true;
	}
	if (g_reload.phase == kRNone || Committed() || m.peer != g_reload.from) return false;
	ReloadCancel(m.data, false);
	return true;
}

void ReloadTick()
{
	if (g_reload.phase == kRNone) return;
	if (g_reload.deadline && static_cast<int>(GetTickCount() - g_reload.deadline) > 0) return ReloadCancel(TimeoutReason(g_reload.phase), true);
	switch (g_reload.phase) {
	case kRWaitAcks:
		return WaitAcksTick();
	case kRWaitSlot: {
		const int slot = MySlot(g_reload.wire);
		if (slot < 0) return;
		SteamRejoinSend(g_reload.from, kRjReloadAck, "");
		Arm(kRWaitGo, kGoMs);
		LogI("rejoin", "reload: game received (%u bytes), slot %d; waiting for the others", static_cast<unsigned>(g_reload.save.size()), slot);
		return;
	}
	case kRQuit:
		if (Game()) {
			if (!g_reload.quitAsked) {
				g_reload.quitAsked = true;
				LogI("rejoin", "reload: leaving the game to load it again");
				AutosaveQuitWithoutSaving();
			}
			return;
		}
		Unfreeze();
		if (g_session.role == kBack) SetStatus("Loading the game...");
		ResetRole();
		Arm(kRStart, kLoadMs);
		return;
	case kRStart:
		return StartTick();
	case kRLoading:
		if (!Game() || !Transport()) return;
		UiRejoinInGame();
		LogI("rejoin", "reload: game loaded; continuing");
		UiToast(g_reload.mine ? "You have rejoined the game." : "The player has rejoined the game.");
		Finish();
		return;
	}
}
}
