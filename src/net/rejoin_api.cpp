// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include "MinHook.h"
#include "core/config.h"
#include "core/log.h"
#include "mods/modvars.h"
#include "net/steamnet.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/net.h"
#include "addr/world.h"

#include "net/rejoin_internal.h"
#include "core/uiport.h"

using namespace rejoin_impl;

void InstallRejoin(bool knownExe)
{
	if (!knownExe) return;
	bool ok = HookAt(addr::kAdvanceTick, reinterpret_cast<void *>(&HookAdvanceTick), reinterpret_cast<void **>(&g_orig.advance));
	ok = HookAt(addr::kAddCommand, reinterpret_cast<void *>(&HookAddCommand), reinterpret_cast<void **>(&g_orig.addCommand)) && ok;
	ok = HookAt(addr::kStartThread, reinterpret_cast<void *>(&HookStartThread), reinterpret_cast<void **>(&g_orig.startThread)) && ok;
	ok = HookAt(addr::kRescheduleThread, reinterpret_cast<void *>(&HookRescheduleThread), reinterpret_cast<void **>(&g_orig.reschedule)) && ok;
	ok = HookAt(addr::kStopThread, reinterpret_cast<void *>(&HookStopThread), reinterpret_cast<void **>(&g_orig.stopThread)) && ok;
	ok = HookAt(addr::kDeleteThread, reinterpret_cast<void *>(&HookDeleteThread), reinterpret_cast<void **>(&g_orig.deleteThread)) && ok;
	ok = HookAt(addr::kSetThreadTime, reinterpret_cast<void *>(&HookSetThreadTime), reinterpret_cast<void **>(&g_orig.setThreadTime)) && ok;
	ok = HookAt(addr::kRandomRange, reinterpret_cast<void *>(&HookRandomRange), reinterpret_cast<void **>(&g_orig.randomRange)) && ok;
	ok = HookAt(addr::kAiUpdate, reinterpret_cast<void *>(&HookAiUpdate), reinterpret_cast<void **>(&g_orig.aiUpdate)) && ok;
	ok = HookAt(addr::kAiAddOrder, reinterpret_cast<void *>(&HookAiAddOrder), reinterpret_cast<void **>(&g_orig.aiAddOrder)) && ok;
	ok = HookAt(addr::kNetReportDesync, reinterpret_cast<void *>(&HookDesync), reinterpret_cast<void **>(&g_orig.desync)) && ok;
	ok = HookAt(addr::kSetPosition, reinterpret_cast<void *>(&HookSetPosition), reinterpret_cast<void **>(&g_orig.setPosition)) && ok;
	ok = HookAt(addr::kRemoveFromMap, reinterpret_cast<void *>(&HookRemoveFromMap), reinterpret_cast<void **>(&g_orig.removeFromMap)) && ok;
	ok = HookAt(addr::kTraceTileLine, reinterpret_cast<void *>(&HookTraceTileLine), reinterpret_cast<void **>(&g_orig.traceTileLine)) && ok;
	ok = HookAt(addr::kSmoothPath, reinterpret_cast<void *>(&HookSmoothPath), reinterpret_cast<void **>(&g_orig.smoothPath)) && ok;
	ok = HookAt(addr::kTileLineVisit, reinterpret_cast<void *>(&HookTileLineVisit), reinterpret_cast<void **>(&g_orig.tileLineVisit)) && ok;
	void **slot = reinterpret_cast<void **>(addr::kCmdMessageExecSlot);
	DWORD old = 0;
	if (VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
		g_orig.msgExec = reinterpret_cast<Exec_t>(*slot);
		*slot = reinterpret_cast<void *>(&HookMsgExecute);
		VirtualProtect(slot, sizeof(void *), old, &old);
	}
	else {
		ok = false;
	}
	LogAt(ok ? kLogInfo : kLogError, "rejoin", "hooks: %s", ok ? "ready" : "NOT available");
}

void RejoinOnSteamMessage(uint64_t peer, uint8_t type, const std::string &data)
{
	if (type == kRjReport) return;
	std::string extra;
	if (type == kRjReq) extra = std::to_string(SteamLobbyNumberOf(peer));
	std::lock_guard<std::mutex> lock(g_mx);
	g_queue.push_back({peer, type, type == kRjReq ? extra : data});
}

void RejoinGameThreadTickInner();

void RejoinGameThreadTick()
{
	RejoinGameThreadTickInner();
	if (g_session.startCrumb) {
		g_session.startCrumb = false;
		LogI("rejoin", "startup step finished");
	}
}

void RejoinGameThreadTickInner()
{
	std::deque<Msg> msgs;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		msgs.swap(g_queue);
	}
	for (Msg &m : msgs) {
		switch (m.type) {
		case kRjReload:
		case kRjReloadFile:
		case kRjReloadEnd:
		case kRjReloadAck:
		case kRjReloadGo:
			ReloadOnMessage(m);
			break;
		case kRjReq:
			HostOnRequest(m.peer, atoi(m.data.c_str()));
			break;
		case kRjAccept:
			if (g_session.role == kBack && g_session.phase == kBWaitAccept && m.peer == g_session.peer) {
				g_session.lobby = _strtoui64(m.data.c_str(), nullptr, 10);
				g_session.phase = kBWaitData;
				g_session.deadline = GetTickCount() + 120000;
				SetStatus(Tr("Joining the room of ") + g_session.peerName + "...");
				g_session.joinTries = 0;
				g_session.joinRetryAt = 0;
				SteamRejoinJoin(g_session.lobby);
			}
			break;
		case kRjWait:
			if (g_session.role == kBack && g_session.phase == kBWaitAccept && m.peer == g_session.peer) {
				g_session.deadline = GetTickCount() + 60000;
				SetStatus(g_session.peerName + Tr(" still has you as connected. Their game will mark you as disconnected in a few seconds; once that "
					"happens, you will join."));
			}
			break;
		case kRjDeny:
		case kRjCancel:
			if (m.type == kRjCancel && ReloadOnCancel(m)) break;
			if (m.type == kRjDeny && g_session.role == kBack && g_session.phase == kBWaitAccept && m.peer == g_session.peer &&
				m.data.compare(0, strlen(kOwnerRedirect), kOwnerRedirect) == 0) {
				const std::string rest = m.data.substr(strlen(kOwnerRedirect));
				const size_t bar = rest.find('|');
				const uint64_t owner = _strtoui64(rest.substr(0, bar).c_str(), nullptr, 10);
				if (owner) {
					g_session.peer = owner;
					g_session.peerName = bar == std::string::npos ? "the host" : rest.substr(bar + 1);
					g_session.deadline = GetTickCount() + 30000;
					g_session.lastRequest = GetTickCount();
					SetStatus(Tr("Asking ") + g_session.peerName + "...");
					SteamRejoinSend(g_session.peer, kRjReq, "");
					LogI("rejoin", "the room is now held by %s; asking them", g_session.peerName.c_str());
					break;
				}
			}
			if (m.type == kRjDeny && g_session.role == kBack && g_session.phase == kBWaitAccept && m.peer == g_session.peer &&
				m.data.compare(0, strlen(kDenyBusy), kDenyBusy) == 0) {
				g_session.deadline = GetTickCount() + 180000;
				SetStatus(g_session.peerName + Tr(" is bringing another player into the game; as soon as they finish, you will join."));
				break;
			}
			if (m.type == kRjDeny && g_session.role == kBack && g_session.phase == kBWaitAccept && m.peer == g_session.peer &&
				m.data.compare(0, strlen(kDenyOut), kDenyOut) == 0) {
				if (AskNext("is not in the game")) break;
				BackFail("nobody from that game is still in it");
				break;
			}
			if (g_session.role == kBack && m.peer == g_session.peer) {
				if (g_session.phase != kBFailed) {
					g_session.phase = kBWaitAccept;
					std::string why = m.data;
					if (why.compare(0, strlen(kDenyBusy), kDenyBusy) == 0) why.erase(0, strlen(kDenyBusy));
					if (why.compare(0, strlen(kDenyOut), kDenyOut) == 0) why.erase(0, strlen(kDenyOut));
					BackFail(why);
				}
			}
			else if (g_session.role == kHost && m.peer == g_session.peer) {
				HostCancel(Tr("the player canceled (") + m.data + ")");
			}
			else if (g_session.role == kOther) {
				Unfreeze();
				ResetRole();
			}
			else if (g_session.role == kNoRole && m.type == kRjCancel) {
				g_session.ignoreFreezeUntil = GetTickCount() + 120000;
			}
			break;
		case kRjState:
			if (g_session.role == kBack && m.peer == g_session.peer) {
				g_blobs.wireRaw = m.data;
				g_blobs.save.clear();
				g_blobs.threads.clear();
				g_blobs.hostDump.clear();
				g_blobs.objects.clear();
				g_blobs.handles.clear();
				g_blobs.queries.clear();
				g_blobs.modVars.clear();
				g_blobs.ai.clear();
				g_blobs.squads.clear();
				SetStatus("Receiving the game...");
			}
			break;
		case kRjFile:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.save += m.data;
			break;
		case kRjThreads:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.threads = m.data;
			break;
		case kRjDump:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.hostDump += m.data;
			break;
		case kRjObjects:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.objects += m.data;
			break;
		case kRjAi:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.ai += m.data;
			break;
		case kRjSquads:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.squads += m.data;
			break;
		case kRjModVars:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.modVars += m.data;
			break;
		case kRjHandles:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.handles += m.data;
			break;
		case kRjQueries:
			if (g_session.role == kBack && m.peer == g_session.peer) g_blobs.queries += m.data;
			break;
		case kRjFileEnd:
			if (g_session.role == kBack && m.peer == g_session.peer) {
				g_session.saveDone = true;
				LogI("rejoin", "game received (%u bytes)", static_cast<unsigned>(g_blobs.save.size()));
			}
			break;
		case kRjReady:
			if (g_session.role == kHost && g_session.phase == kHWaitReady && m.peer == g_session.peer) {
				for (uint64_t id : OtherMembers()) SteamRejoinSend(id, kRjGo, std::to_string(g_session.slot));
				ApplyGo(g_session.slot);
				UiToast("The player has rejoined the game.");
				ResetRole();
			}
			break;
		case kRjGo:
			if (g_session.role == kBack && g_session.phase == kBWaitGo && m.peer == g_session.peer) {
				char *t = Transport();
				if (t) SendOwnTickData(t);
				if (!g_trace.active && t) TraceStart(*reinterpret_cast<int *>(t + 0x54));
				if (g_trace.active) TracePush({t ? *reinterpret_cast<int *>(t + 0x54) : -1, 'G', 0, 0, 0, 0, 0, 0, ThreadsHash()});
				Unfreeze();
				g_session.role = kNoRole;
				g_session.phase = 0;
				SetStatus("");
				UiToast("You have rejoined the game.");
				LogI("rejoin", "inside; continuing");
			}
			else if (g_session.role == kOther) {
				ApplyGo(atoi(m.data.c_str()));
				ResetRole();
			}
			break;
		}
	}
	HostTick();
	BackTick();
	ReloadTick();
	if (g_session.role == kOther && g_session.deadline && static_cast<int>(GetTickCount() - g_session.deadline) > 0) {
		LogI("rejoin", "the host said nothing; continuing");
		Unfreeze();
		ResetRole();
	}
}

bool RejoinWantsWake()
{
	return (g_session.role == kBack || g_reload.phase != kRNone) && !Game();
}

void RejoinMenuTick()
{
	if ((g_session.role == kBack || g_reload.phase != kRNone) && !Game()) RejoinGameThreadTick();
}

void RejoinRequest(uint64_t hostId, const std::string &hostName, const std::vector<std::pair<uint64_t, std::string>> &others)
{
	if (!hostId) return;
	ResetRole();
	g_session.candidates.clear();
	for (const auto &o : others) {
		if (o.first && o.first != hostId) g_session.candidates.push_back(o);
	}
	g_session.role = kBack;
	g_session.phase = kBWaitAccept;
	g_session.peer = hostId;
	g_session.peerName = hostName;
	g_session.deadline = GetTickCount() + (g_session.candidates.empty() ? 30000 : 15000);
	SetStatus(Tr("Asking ") + hostName + "...");
	SteamRejoinSend(hostId, kRjReq, "");
	g_session.lastRequest = GetTickCount();
	LogI("rejoin", "requested from %s", hostName.c_str());
}

void RejoinCancel()
{
	if (g_session.role == kBack && g_session.phase != kBFailed && g_session.phase < kBWaitLoad) {
		if (g_session.peer) SteamRejoinSend(g_session.peer, kRjCancel, "canceled it");
	}
	if (g_session.role == kBack && g_session.phase < kBWaitLoad) {
		ReloadDrop();
		ResetRole();
		SetStatus("");
	}
}

bool RejoinClientActive()
{
	return g_session.role == kBack && g_session.phase != kBFailed && g_session.phase < kBWaitLoad;
}

std::string RejoinClientStatus()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_session.status;
}

bool RejoinClientFailed()
{
	return g_session.role == kBack && g_session.phase == kBFailed;
}

void RejoinClientDismiss()
{
	if (g_session.role == kBack && g_session.phase == kBFailed) {
		ResetRole();
		SetStatus("");
	}
}

bool RejoinBusy()
{
	return (g_session.role == kBack && g_session.phase != kBFailed) || g_reload.phase != kRNone;
}

bool RejoinReloading()
{
	return g_reload.phase != kRNone;
}

bool RejoinPendingLoad()
{
	return g_session.pendingLoad;
}

void RejoinAfterLoad(void *gameIn, bool loaded)
{
	g_session.pendingLoad = false;
	if (!loaded) return BackFail("could not load the game");
	char *game = static_cast<char *>(gameIn);
	char *t = Transport();
	if (!t) return BackFail("the game network did not start");
	if (*reinterpret_cast<int *>(game + 0x1240)) reinterpret_cast<ThisInt_t>(Vt(game)[kSlotSetPaused])(game, nullptr, 0);
	*reinterpret_cast<int *>(game + 0x1264) = g_blobs.wire.timer2c;
	for (int i = 0; i < 2; i++) {
		uint32_t &rng = *reinterpret_cast<uint32_t *>(game + (i ? 0x12b4 : 0x12a0) + 4);
		if (rng != g_blobs.wire.rng[i]) LogW("rejoin", "random generator %d differs after loading (%08x, the host's %08x); equalized", i, rng, g_blobs.wire.rng[i]);
		rng = g_blobs.wire.rng[i];
	}
	*reinterpret_cast<int *>(addr::kGameEndTick) = g_blobs.wire.gameEnd;
	uint32_t *sync = reinterpret_cast<uint32_t *>(addr::kSyncState + 8);
	std::string diff;
	static const char *const names[8] = {"slots", "threads", "pathfinder", "exploration", "scriptstate", "netcmds", "extra", "ai"};
	for (int i = 0; i < 8; i++) {
		if (sync[i] != g_blobs.wire.sync[i]) {
			char b[96];
			sprintf_s(b, " %s %08x/%08x", names[i], sync[i], g_blobs.wire.sync[i]);
			diff += b;
		}
		sync[i] = g_blobs.wire.sync[i];
	}
	LogD("rejoin", "fingerprints after loading %s", diff.empty() ? "equal to the host's" : ("different (mine/host):" + diff).c_str());
	DumpGameState(kDumpBackBefore);
	if (!g_blobs.hostDump.empty()) DumpObjectsBinary(g_blobs.hostDump, kObjectsBack);
	if (!g_blobs.threads.empty()) FixThreads();
	if (!g_blobs.hostDump.empty()) FixCommandIds();
	if (!g_blobs.objects.empty()) FixObjects();
	if (!g_blobs.handles.empty()) ApplyHandles();
	{
		Json vars;
		if (!g_blobs.modVars.empty() && Json::Parse(g_blobs.modVars, vars)) ModVarsFromJson(vars);
		else ModVarsClear();
		LogD("rejoin", "%d mod variables", ModVarsCount());
	}
	if (!g_blobs.queries.empty()) ApplyQueries();
	if (!g_blobs.ai.empty()) ApplyAi();
	if (!g_blobs.squads.empty()) ApplySquads();
	for (int i = 0; i < 8; i++) sync[i] = g_blobs.wire.sync[i];
	TraceStart(g_blobs.wire.tick);
	if (!g_blobs.hostDump.empty()) {
		std::string mine;
		if (DumpGameState(kDumpBackAfter) && ReadFileAll(DiskPath(kDumpBackAfter), mine)) LogDumpDiff(g_blobs.hostDump, mine);
		WriteFileAll(DiskPath(kDumpHost), g_blobs.hostDump);
	}
	const int me = *reinterpret_cast<int *>(t + 0x4c);
	if (me != g_blobs.wire.slot) LogW("rejoin", "WARNING the transport puts me in slot %d and not %d", me, g_blobs.wire.slot);
	const unsigned now = Now();
	*reinterpret_cast<int *>(t + 0x54) = g_blobs.wire.tick;
	*reinterpret_cast<int *>(t + 0x7c) = g_blobs.wire.tick - 1;
	*reinterpret_cast<uint32_t *>(t + 0x58) = g_blobs.wire.hash58;
	*reinterpret_cast<int *>(t + 0x5c) = 0;
	*reinterpret_cast<int *>(t + 0x70) = g_blobs.wire.tick70;
	*reinterpret_cast<int *>(t + 0x68) = 0;
	*reinterpret_cast<int *>(t + 0x6c) = 0;
	*reinterpret_cast<int *>(t + 0x90) = g_blobs.wire.pause90;
	*reinterpret_cast<int *>(t + 0x94) = 0;
	*reinterpret_cast<int *>(t + 0x3f8) = 0;
	*reinterpret_cast<int *>(t + 0x3fc) = 0;
	*reinterpret_cast<unsigned *>(t + 0x60) = now;
	*reinterpret_cast<unsigned *>(t + 0x64) = now;
	*reinterpret_cast<unsigned *>(t + 0x78) = now;
	*reinterpret_cast<unsigned *>(t + 0x88) = now;
	*reinterpret_cast<unsigned *>(t + 0x8c) = now;
	*reinterpret_cast<int *>(t + 0x74) = 0;
	*reinterpret_cast<int *>(t + 0x98) = g_blobs.wire.tick70;
	*reinterpret_cast<int *>(t + 0x9c) = 0;
	*reinterpret_cast<int *>(t + 0x20) = 0;
	*reinterpret_cast<int *>(t + 0x3a4) = 0;
	*reinterpret_cast<unsigned *>(t + 0x3a8) = now;
	char *gate = *reinterpret_cast<char **>(t + 0x50);
	uint8_t mask = static_cast<uint8_t>(g_blobs.wire.mask & ~(1u << me));
	g_session.restoreBits = 0;
	for (int i = 0; i < 8; i++) {
		if ((mask & (1u << i)) && !g_blobs.wire.blob[i]) g_session.restoreBits |= static_cast<uint8_t>(1u << i);
	}
	mask &= static_cast<uint8_t>(~g_session.restoreBits);
	if (gate) *reinterpret_cast<uint8_t *>(gate + 0x108) = mask;
	if (g_session.restoreBits) LogD("rejoin", "no tick %d data from %02x; not waiting for them in the first step", g_blobs.wire.tick, g_session.restoreBits);
	FillRing(t, me);
	reinterpret_cast<ThisOnly_t>(addr::kBuildLocalTick)(t);
	g_session.freezeTick = g_blobs.wire.tick;
	g_session.frozen = false;
	g_session.firstAdvance = true;
	RaiseQuitTimeouts();
	g_session.phase = kBWaitGo;
	g_session.deadline = GetTickCount() + 60000;
	UiRejoinInGame();
	SteamRejoinSend(g_session.peer, kRjReady, "");
	LogI("rejoin", "loaded and placed at tick %d (mask %02x); waiting for the host", g_blobs.wire.tick,
		gate ? *reinterpret_cast<uint8_t *>(gate + 0x108) : 0);
}
