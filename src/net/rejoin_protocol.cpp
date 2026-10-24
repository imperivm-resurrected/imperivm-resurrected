// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <set>
#include "MinHook.h"
#include "game/autosave.h"
#include "core/config.h"
#include "core/log.h"
#include "core/sha256.h"
#include "mods/modvars.h"
#include "mods/civindices.h"
#include "net/mpsave.h"
#include "net/steamnet.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"

#include "net/rejoin_internal.h"
#include "core/uiport.h"

using namespace rejoin_impl;

namespace rejoin_impl {
void LogDumpDiff(const std::string &host, const std::string &mine)
{
	auto lines = [](const std::string &t) {
		std::vector<std::string> out;
		size_t p = 0;
		bool history = false;
		while (p < t.size()) {
			size_t e = t.find('\n', p);
			if (e == std::string::npos) e = t.size();
			const std::string line = t.substr(p, e - p);
			if (!line.empty() && line[0] == '[') history = line == "[HASHHISTORY]";
			if (!history) out.push_back(line);
			p = e + 1;
		}
		return out;
	};
	const std::vector<std::string> a = lines(host), b = lines(mine);
	size_t diffs = 0;
	std::string section;
	for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
		if (!a[i].empty() && a[i][0] == '[') section = a[i];
		if (a[i].compare(0, 7, "handle=") == 0) section += " " + a[i];
		if (a[i] == b[i]) continue;
		diffs++;
		if (diffs <= 60) LogD("rejoin", "DIFFERENCE %s | host: %s | me: %s", section.c_str(), a[i].c_str(), b[i].c_str());
	}
	LogW("rejoin", "dumps compared: %u host lines, %u mine, %u different", static_cast<unsigned>(a.size()),
		static_cast<unsigned>(b.size()), static_cast<unsigned>(diffs));
}
}

namespace rejoin_impl {
namespace {
void HostDeny(uint64_t peer, const std::string &why)
{
	LogI("rejoin", "rejecting %llu (%s)", static_cast<unsigned long long>(peer), why.c_str());
	SteamRejoinSend(peer, kRjDeny, why);
}
}
}

namespace rejoin_impl {
void HostCancel(const std::string &why)
{
	LogW("rejoin", "cancelled (%s)", why.c_str());
	if (g_session.phase == kHWaitFreeze && !g_session.frozen) g_session.ignoreFreezeUntil = GetTickCount() + 120000;
	for (uint64_t m : OtherMembers()) SteamRejoinSend(m, kRjCancel, why);
	if (g_session.peer) SteamRejoinSend(g_session.peer, kRjCancel, why);
	UiToast(Tr("Couldn't bring the player back into the game: ") + why, true);
	Unfreeze();
	ResetRole();
}

void HostOnRequest(uint64_t peer, int number)
{
	if (g_session.role == kHost && peer == g_session.peer) {
		if (g_session.phase == kHWaitJoin) return;
		LogI("rejoin", "%llu asks to join again (their game closed halfway); starting over",
			static_cast<unsigned long long>(peer));
		Unfreeze();
		ResetRole();
	}
	if (g_session.role == kBack) return HostDeny(peer, std::string(kDenyOut) + "is not in the game either");
	if (g_session.role != kNoRole || g_reload.phase != kRNone) return HostDeny(peer, std::string(kDenyBusy) + "another player is already rejoining");
	if (!InOnlineGame()) return HostDeny(peer, std::string(kDenyOut) + "the host is no longer in that game");
	const OnlineRoom room = SteamCurrentRoom();
	if (!room.owner) {
		for (const OnlineMember &m : room.members) {
			if (!m.owner || m.me || m.id == peer) continue;
			LogI("rejoin", "%llu asks to return, but the room is held by %s: sending them there", static_cast<unsigned long long>(peer),
				m.name.c_str());
			SteamRejoinSend(peer, kRjDeny, std::string(kOwnerRedirect) + std::to_string(m.id) + "|" + m.name);
			return;
		}
		return HostDeny(peer, std::string(kDenyOut) + "the host is no longer in that game");
	}
	void *game = Game();
	if (reinterpret_cast<IntThis_t>(Vt(game)[kSlotIsPaused])(game)) return HostDeny(peer, "the game is paused; ask them to resume it and try again");
	char *t = Transport();
	const int slot = SlotOfNumber(number);
	const int hostIndex = *reinterpret_cast<int *>(t + 0x4c);
	char *gate = *reinterpret_cast<char **>(t + 0x50);
	if (slot < 0 || slot == hostIndex || !gate) return HostDeny(peer, "your spot in the game could not be found");
	if (slot < 16 && *reinterpret_cast<void **>(static_cast<char *>(game) + 0x12cc + slot * 800 + 0x88)) {
		return HostDeny(peer, "you left that game and your army is controlled by the AI");
	}
	if (*reinterpret_cast<uint8_t *>(gate + 0x108) & (1u << slot)) {
		SteamRejoinSend(peer, kRjWait, "");
		static DWORD lastLog = 0;
		if (GetTickCount() - lastLog > 10000) {
			lastLog = GetTickCount();
			LogI("rejoin", "%llu asks to return, but the game still has them as connected: they should wait", static_cast<unsigned long long>(peer));
		}
		return;
	}
	{
		std::set<int> present;
		for (const OnlineMember &m : room.members) {
			if (m.number) present.insert(m.number);
		}
		char *gs = *reinterpret_cast<char **>(addr::kGameSettings);
		for (int i = 0; gs && i < 8; i++) {
			if (i == slot || i == hostIndex || !(*reinterpret_cast<uint8_t *>(gate + 0x108) & (1u << i))) continue;
			const uint32_t ip = *reinterpret_cast<uint32_t *>(gs + 0xdc8 + i * 4);
			int k = 0;
			if ((ip & 0xffffff00u) == 0x7f4d0000u) k = static_cast<int>(ip & 0xff);
			else if ((ip & 0x00ffffffu) == 0x00004d7fu) k = static_cast<int>(ip >> 24);
			if (!k || present.count(k)) continue;
			SteamRejoinSend(peer, kRjWait, "");
			static DWORD lastOtherLog = 0;
			if (GetTickCount() - lastOtherLog > 10000) {
				lastOtherLog = GetTickCount();
				LogI("rejoin", "%llu asks to return, but the game is still waiting for slot %d (it left the room): they should wait",
					static_cast<unsigned long long>(peer), i);
			}
			return;
		}
	}
	char *player = static_cast<char *>(game) + 0x12cc + slot * 800;
	LogD("rejoin", "slot %d, AI of player %p", slot, *reinterpret_cast<void **>(player + 0x88));
	g_session.role = kHost;
	g_session.phase = kHWaitJoin;
	g_session.peer = peer;
	g_session.slot = slot;
	g_session.deadline = GetTickCount() + 60000;
	SteamRejoinAllow(peer);
	SteamRejoinSend(peer, kRjAccept, std::to_string(SteamRejoinLobby()));
	LogI("rejoin", "%llu returns to slot %d (127.77.0.%d)", static_cast<unsigned long long>(peer), slot, number);
}
}

namespace rejoin_impl {
namespace {
bool QueueFreezeCommand(int slot)
{
	char *game = static_cast<char *>(Game());
	void *player = game ? *reinterpret_cast<void **>(game + 0x12c8) : nullptr;
	if (!player) return false;
	void *cmd = reinterpret_cast<Create_t>(addr::kCmdMessageCreate)();
	if (!cmd) return false;
	const std::string text = std::string(kMagicPrefix) + std::to_string(slot);
	reinterpret_cast<Assign_t>(addr::kStringAssign)(static_cast<char *>(cmd) + 0xc, nullptr, text.c_str(), static_cast<unsigned>(text.size()));
	reinterpret_cast<ThisPtr_t>(Vt(cmd)[3])(cmd, nullptr, player);
	reinterpret_cast<ThisPtr_t>(addr::kQueueCommand)(game, nullptr, cmd);
	return true;
}
}

std::string BuildState(const std::string &saveData)
{
	char *t = Transport();
	char *game = static_cast<char *>(Game());
	WireState ws = {};
	ws.magic = kWireMagic;
	ws.tick = *reinterpret_cast<int *>(t + 0x54);
	ws.hash58 = *reinterpret_cast<uint32_t *>(t + 0x58);
	ws.tick70 = *reinterpret_cast<int *>(t + 0x70);
	ws.pause90 = *reinterpret_cast<int *>(t + 0x90) != 0;
	for (int i = 0; i < 8; i++) {
		if (*reinterpret_cast<uint8_t *>(Record(t, i, ws.tick - 1) + 0x18) & 1) ws.pause90 = 1;
	}
	ws.timer2c = *reinterpret_cast<int *>(game + 0x1264);
	ws.rng[0] = *reinterpret_cast<uint32_t *>(game + 0x12a0 + 4);
	ws.rng[1] = *reinterpret_cast<uint32_t *>(game + 0x12b4 + 4);
	ws.gameEnd = *reinterpret_cast<int *>(addr::kGameEndTick);
	for (int i = 0; i < 8; i++) ws.sync[i] = *reinterpret_cast<uint32_t *>(addr::kSyncState + 8 + i * 4);
	ws.hostIndex = *reinterpret_cast<int *>(t + 0x4c);
	char *gate = *reinterpret_cast<char **>(t + 0x50);
	ws.mask = (*reinterpret_cast<uint8_t *>(gate + 0x108) | (1u << ws.hostIndex)) & ~(1u << g_session.slot);
	ws.slot = g_session.slot;
	std::string blobs;
	for (int i = 0; i < 8; i++) {
		char *rec = Record(t, i, ws.tick);
		const uint32_t size = *reinterpret_cast<uint32_t *>(rec + 0x10);
		const char *buf = *reinterpret_cast<char **>(rec + 0x14);
		if (buf || size) LogD("rejoin", "tick %d data of slot %d: %p, %u bytes", ws.tick, i, buf, size);
		if (!buf || !size || size > 64 * 1024) continue;
		ws.blob[i] = size;
		blobs.append(buf, size);
	}
	if (char *gs = *reinterpret_cast<char **>(addr::kGameSettings)) memcpy(ws.multi, gs + 0xdb8, sizeof(ws.multi));
	for (const OnlineMember &m : SteamCurrentRoom().members) {
		if (m.me && m.number && ws.hostIndex >= 0 && ws.hostIndex < 8) ws.multi[4 + ws.hostIndex] = 0x7f4d0000u | static_cast<uint32_t>(m.number);
	}
	std::string settings;
	ReadFileAll(DiskPath("profiles/lastsettings.usr"), settings);
	ws.settingsSize = static_cast<uint32_t>(settings.size());
	ws.saveSize = static_cast<uint32_t>(saveData.size());
	LogD("rejoin", "state at tick %d (fingerprint %08x, tick %d ms, mask %02x, %u bytes of data, settings %u, save %u)",
		ws.tick, ws.hash58, ws.tick70, ws.mask, static_cast<unsigned>(blobs.size()), ws.settingsSize, ws.saveSize);
	return std::string(reinterpret_cast<const char *>(&ws), sizeof(ws)) + blobs + settings;
}
}

namespace rejoin_impl {
void HostTick()
{
	if (g_session.role != kHost) return;
	if (g_session.deadline && static_cast<int>(GetTickCount() - g_session.deadline) > 0) return HostCancel("took too long");
	if (!InOnlineGame()) {
		ResetRole();
		Unfreeze();
		return;
	}
	if (g_session.phase != kHWaitJoin) {
		static DWORD goneSince = 0;
		bool inRoom = false;
		for (const OnlineMember &m : SteamCurrentRoom().members) inRoom = inRoom || m.id == g_session.peer;
		if (inRoom) goneSince = 0;
		else if (!goneSince) goneSince = GetTickCount();
		else if (GetTickCount() - goneSince > 3000) {
			goneSince = 0;
			return HostCancel("left the room (their game closed)");
		}
	}
	if (g_session.phase == kHWaitJoin) {
		for (const OnlineMember &m : SteamCurrentRoom().members) {
			if (m.id != g_session.peer || !m.number) continue;
			g_session.ignoreFreezeUntil = 0;
			g_session.peerName = m.name;
			if (!QueueFreezeCommand(g_session.slot)) return HostCancel("could not pause the game");
			g_session.phase = kHWaitFreeze;
			g_session.deadline = GetTickCount() + 30000;
			LogI("rejoin", "%s is in the room; pausing the game", m.name.c_str());
			UiToast(m.name + Tr(" is rejoining the game: it pauses for a few seconds while they join."));
			return;
		}
	}
	else if (g_session.phase == kHWaitFreeze && g_session.frozen) {
		if (AutosavePending()) return;
		g_session.savePath = AutosaveRequestNamed(kSaveName);
		if (g_session.savePath.empty()) return HostCancel("could not save the game");
		g_session.saveAskedAt = GetTickCount();
		g_session.phase = kHWaitSave;
		g_session.deadline = GetTickCount() + 60000;
	}
	else if (g_session.phase == kHWaitSave) {
		if (AutosavePending() || GetTickCount() - g_session.saveAskedAt < 300) return;
		std::string save;
		if (!ReadFileAll(DiskPath(g_session.savePath), save) || save.size() < 1024) return HostCancel("the save was not found");
		if (GetConfig().rejoinReload) {
			std::string cams;
			ReadFileAll(DiskPath(g_session.savePath) + ".cameras", cams);
			ReloadBroadcast(save, cams, g_session.peerName, g_session.peer);
			ResetRole();
			return;
		}
		const std::string state = BuildState(save);
		SteamRejoinSend(g_session.peer, kRjState, state);
		const std::string threads = CollectThreads();
		SteamRejoinSend(g_session.peer, kRjThreads, threads);
		if (ModVarsCount() > 0) {
			const std::string vars = ModVarsToJson().Dump();
			for (size_t off = 0; off < vars.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjModVars, vars.substr(off, 60 * 1024));
		}
		std::string dump;
		if (DumpGameState(kDumpHost) && ReadFileAll(DiskPath(kDumpHost), dump)) {
			for (size_t off = 0; off < dump.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjDump, dump.substr(off, 60 * 1024));
			DumpObjectsBinary(dump, kObjectsHost);
			const std::string objects = CollectObjects(dump);
			for (size_t off = 0; off < objects.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjObjects, objects.substr(off, 60 * 1024));
			const std::string handles = CollectHandles();
			for (size_t off = 0; off < handles.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjHandles, handles.substr(off, 60 * 1024));
			const std::string ai = CollectAi();
			for (size_t off = 0; off < ai.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjAi, ai.substr(off, 60 * 1024));
			const std::string squads = CollectSquads();
			for (size_t off = 0; off < squads.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjSquads, squads.substr(off, 60 * 1024));
			const std::string queries = CollectQueries();
			for (size_t off = 0; off < queries.size(); off += 60 * 1024) SteamRejoinSend(g_session.peer, kRjQueries, queries.substr(off, 60 * 1024));
		}
		LogD("rejoin", "sent %u threads and the state dump (%u bytes)", static_cast<unsigned>(threads.size() / sizeof(ThreadRec)),
			static_cast<unsigned>(dump.size()));
		const size_t chunk = 60 * 1024;
		for (size_t off = 0; off < save.size(); off += chunk) SteamRejoinSend(g_session.peer, kRjFile, save.substr(off, chunk));
		SteamRejoinSend(g_session.peer, kRjFileEnd, "");
		g_session.phase = kHWaitReady;
		g_session.deadline = GetTickCount() + 120000;
		LogD("rejoin", "save sent (%u bytes)", static_cast<unsigned>(save.size()));
	}
}

void ApplyGo(int slot)
{
	char *t = Transport();
	if (t && slot >= 0 && slot < 8) {
		char *gate = *reinterpret_cast<char **>(t + 0x50);
		if (gate) *reinterpret_cast<uint8_t *>(gate + 0x108) |= static_cast<uint8_t>(1u << slot);
	}
	LogI("rejoin", "slot %d plays again; continuing", slot);
	if (t) TraceStart(*reinterpret_cast<int *>(t + 0x54));
	Unfreeze();
}

void BackFail(const std::string &why)
{
	LogW("rejoin", "failed (%s)", why.c_str());
	SteamRejoinJoinEnd();
	if (g_session.peer && g_session.phase >= kBWaitData && g_session.phase != kBFailed) SteamRejoinSend(g_session.peer, kRjCancel, why);
	g_session.pendingLoad = false;
	g_session.filterHost = false;
	Unfreeze();
	if (g_session.phase >= kBWaitData && g_session.phase != kBFailed) SteamLeaveLobby();
	g_session.phase = kBFailed;
	SetStatus(Tr("Couldn't rejoin the game: ") + Tr(why), true);
}
}

namespace rejoin_impl {
bool LoadSettings(const std::string &vfsPath, int slot)
{
	void *settings = nullptr;
	__try {
		void *stream = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x40);
		reinterpret_cast<ThisInt_t>(addr::kStreamLoadCtor)(stream, nullptr, 1);
		reinterpret_cast<StreamOpen_t>(Vt(stream)[0x8 / 4])(stream, nullptr, vfsPath.c_str());
		char *obj = static_cast<char *>(reinterpret_cast<LoadObject_t>(Vt(stream)[0x54 / 4])(stream, nullptr, nullptr, 0, 0));
		if (obj) {
			char *base = obj + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(obj + 4) + 4);
			settings = reinterpret_cast<IsKindOf_t>(Vt(base)[1])(base, nullptr, *reinterpret_cast<const char **>(addr::kClassNameBasicSettings));
		}
		reinterpret_cast<ThisOnly_t>(Vt(stream)[0xc / 4])(stream);
		reinterpret_cast<ThisInt_t>(Vt(stream)[0])(stream, nullptr, 1);
		if (!settings) return false;
		char *s = static_cast<char *>(settings);
		char *sbase = s + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(s + 4) + 4);
		if (!reinterpret_cast<IsKindOf_t>(Vt(sbase)[1])(sbase, nullptr, *reinterpret_cast<const char **>(addr::kClassNameMultiSettings))) {
			LogD("rejoin", "the received settings are not from an online game (vtable %08x)", *reinterpret_cast<uint32_t *>(s));
			return false;
		}
		memcpy(s + 0xdb8, g_blobs.wire.multi, sizeof(g_blobs.wire.multi));
		*reinterpret_cast<int *>(s + 0xd74) = slot;
		*reinterpret_cast<uint32_t *>(s + 0xde8) = *reinterpret_cast<uint32_t *>(s + 0xdc8 + slot * 4);
		LogD("rejoin", "IP of each slot %08x %08x %08x %08x %08x %08x %08x %08x; mine %08x",
			g_blobs.wire.multi[4], g_blobs.wire.multi[5], g_blobs.wire.multi[6], g_blobs.wire.multi[7], g_blobs.wire.multi[8], g_blobs.wire.multi[9], g_blobs.wire.multi[10], g_blobs.wire.multi[11],
			*reinterpret_cast<uint32_t *>(s + 0xde8));
		*reinterpret_cast<void **>(addr::kGameSettings) = settings;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
}

namespace rejoin_impl {

bool AskNext(const std::string &why)
{
	while (!g_session.candidates.empty()) {
		const auto next = g_session.candidates.front();
		g_session.candidates.erase(g_session.candidates.begin());
		if (!next.first || next.first == g_session.peer) continue;
		LogD("rejoin", "%s %s; asking %s", g_session.peerName.c_str(), why.c_str(), next.second.c_str());
		g_session.peer = next.first;
		g_session.peerName = next.second;
		g_session.deadline = GetTickCount() + (g_session.candidates.empty() ? 30000 : 15000);
		g_session.lastRequest = GetTickCount();
		SetStatus(Tr("Asking ") + g_session.peerName + "...");
		SteamRejoinSend(g_session.peer, kRjReq, "");
		return true;
	}
	return false;
}

void BackTick()
{
	if (g_session.role != kBack) return;
	if (g_session.phase == kBWaitAccept && GetTickCount() - g_session.lastRequest > 4000) {
		g_session.lastRequest = GetTickCount();
		SteamRejoinSend(g_session.peer, kRjReq, "");
	}
	if (g_session.phase != kBFailed && g_session.deadline && static_cast<int>(GetTickCount() - g_session.deadline) > 0) {
		if (g_session.phase == kBWaitAccept && AskNext("not responding")) return;
		return BackFail("the host is not responding");
	}
	if (g_session.phase == kBWaitData) {
		if (const int code = SteamRejoinJoinFailed()) {
			if (++g_session.joinTries >= 10) return BackFail(Tr("Steam won't let you into the room (code ") + std::to_string(code) + ")");
			g_session.joinRetryAt = GetTickCount() + 2000;
		}
		if (g_session.joinRetryAt && static_cast<int>(GetTickCount() - g_session.joinRetryAt) >= 0) {
			g_session.joinRetryAt = 0;
			LogI("rejoin", "trying again to enter the room (attempt %d)", g_session.joinTries + 1);
			SetStatus(Tr("Joining the room of ") + g_session.peerName + "... (attempt " + std::to_string(g_session.joinTries + 1) + ")");
			SteamRejoinJoin(g_session.lobby);
		}
	}
	if (g_session.phase == kBWaitData && g_session.saveDone) {
		const OnlineRoom room = SteamCurrentRoom();
		int myK = 0;
		for (const OnlineMember &m : room.members) {
			if (m.me) myK = m.number;
		}
		if (room.id != g_session.lobby || !myK) return;
		if (Game() || !*reinterpret_cast<void **>(addr::kMenuFrame)) return;
		if (g_blobs.wireRaw.size() < sizeof(WireState)) return BackFail("incomplete state");
		memcpy(&g_blobs.wire, g_blobs.wireRaw.data(), sizeof(WireState));
		uint64_t blobs = 0;
		for (uint32_t b : g_blobs.wire.blob) blobs += b;
		if (g_blobs.wire.magic != kWireMagic || g_blobs.wire.slot < 0 || g_blobs.wire.slot > 7 || sizeof(WireState) + blobs + g_blobs.wire.settingsSize > g_blobs.wireRaw.size()) {
			return BackFail("corrupted state");
		}
		if (room.info.gameplayKey != SteamOwnGameplayKey()) return BackFail(Tr("Rejoining requires the same civilizations and gameplay content as the room."));
		if (g_blobs.save.size() != g_blobs.wire.saveSize) return BackFail("incomplete save");
		const std::string settings = g_blobs.wireRaw.substr(sizeof(WireState) + blobs, g_blobs.wire.settingsSize);
		if (!WriteFileAll(DiskPath(kBackSave), g_blobs.save) || !WriteFileAll(DiskPath(kBackSettings), settings)) {
			return BackFail("could not write the game");
		}
		Json side = Json::MakeObject();
		side.Set("rules", Json::MakeNumber(room.info.rules));
		if (CivIndicesAutomatic()) {
			const std::string hash = HashFile(DiskPath(kBackSave));
			if (hash.empty()) return BackFail(Tr("Could not save the civilization index table."));
			side.Set("civIndices", CivIndicesToJson());
			side.Set("civSaveHash", Json::MakeString(hash));
		}
		if (!WriteJsonFile(DiskPath(kBackSave) + ".mods.json", side)) return BackFail(Tr("Could not save the civilization index table."));
		if (!LoadSettings(kBackSettings, g_blobs.wire.slot)) return BackFail("could not read the game settings");
		MpSaveSetPendingLoad(kBackSave);
		g_session.pendingLoad = true;
		g_session.phase = kBWaitLoad;
		g_session.deadline = GetTickCount() + 90000;
		SetStatus("Loading the game...");
		UiRejoinStarting();
		*reinterpret_cast<int *>(*reinterpret_cast<char **>(addr::kMenuFrame) + 0x6c) = 1;
		LogI("rejoin", "starting in slot %d (127.77.0.%d), tick %d", g_blobs.wire.slot, myK, g_blobs.wire.tick);
		g_session.startCrumb = true;
	}
}
}
