// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/guard.h"
#include "i18n/tr.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
bool CopiesStillHashing(const std::vector<ModInfo> &mods, const std::vector<OnlineMod> &room)
{
	for (const OnlineMod &m : room) {
		for (const ModInfo &i : mods) {
			if (_stricmp(i.id.c_str(), m.id.c_str()) == 0 && i.version == m.version && ModsCompareRoomCopy(i, m.copyKey) == RoomCopy::kNotHashed) return true;
		}
	}
	return false;
}

std::string FolderOfCopy(const std::vector<ModInfo> &mods, const OnlineMod &m)
{
	std::string same, any;
	for (const ModInfo &i : mods) {
		if (_stricmp(i.id.c_str(), m.id.c_str()) != 0 || i.version != m.version) continue;
		if (same.empty() && ModsCompareRoomCopy(i, m.copyKey) == RoomCopy::kSame) same = i.folder;
		any = i.folder;
	}
	return same.empty() ? any : same;
}

void StartSyncCheck(const OnlineLobby lobby, const std::map<std::string, std::string> installed, bool update)
{
	g_s.rate.Reset();
	g_s.hashDone = g_s.hashTotal = 0;
	SafeThread([lobby, installed, update] {
		ModsScan();
		const std::vector<ModInfo> mods = ModsList();
		ModProfile p;
		std::string error;
		for (const OnlineMod &m : lobby.mods) {
			auto it = installed.find(m.id);
			if (it != installed.end()) {
				p.mods.push_back(it->second);
				continue;
			}
			const std::string folder = FolderOfCopy(mods, m);
			if (folder.empty()) error = Tr("missing ") + m.name + " " + m.version;
			p.mods.push_back(folder);
		}
		std::string hash;
		for (int i = 0; error.empty() && i < 600 && hash.empty(); i++) {
			hash = ModsProfileGameplayHash(p);
			if (hash.empty()) Sleep(200);
		}
		if (error.empty() && hash.empty()) error = "the content could not be checked";
		if (error.empty() && !lobby.gameplayKey.empty() && hash.substr(0, 16) != lobby.gameplayKey) {
			error = "the received content does not match the host's";
		}
		if (error.empty()) {
			const ModProfile loose = ModsLooseProfileWith(p.mods);
			p.name = loose.name;
			if (!ModsSaveProfile(loose)) error = "the profile could not be saved";
		}
		if (error.empty() && update) {
			for (const OnlineMod &m : lobby.mods) {
				auto it = installed.find(m.id);
				if (it == installed.end()) continue;
				const ModInfo *old = nullptr;
				for (const ModInfo &i : mods) {
					if (_stricmp(i.id.c_str(), m.id.c_str()) != 0 || i.folder == it->second || ModsCompareVersions(i.version, m.version) >= 0) continue;
					if (!old || ModsCompareVersions(i.version, old->version) > 0) old = &i;
				}
				if (old) ModsQueueSwap(it->second, old->folder);
			}
		}
		LogAt(error.empty() ? kLogInfo : kLogWarn, "content-share", "profile for the room: %s", error.empty() ? ("profile \"" + p.name + "\" ready").c_str() : TrSource(error).c_str());
		std::lock_guard<std::mutex> lock(g_lock);
		g_syncResult = error.empty() ? "ok:" + p.name : error;
	}).detach();
}

void UpdateSpeed(uint64_t bytes)
{
	g_s.rate.Sample(bytes, GetTickCount());
}

bool RequestFromHost(const std::vector<OnlineMod> &list)
{
	if (list.empty()) return true;
	if (g_s.syncWatch && (g_s.syncLive || g_s.room.info.playing || g_s.room.info.started)) {
		std::vector<std::string> names;
		for (const OnlineMod &m : list) names.push_back(m.name + " " + m.version);
		g_s.syncPhase = 4;
		g_s.syncError = TrFmt("%s could not be downloaded from GameBanana, and the host can't send content during a game.", {ShortList(names)});
		LogI("content-share", "room mods: %u mods would have to come from the host, which does not send content during a game", static_cast<unsigned>(list.size()));
		return false;
	}
	for (const OnlineMod &m : list) {
		if (m.redistributable) continue;
		g_s.syncPhase = 4;
		g_s.syncError = m.name + " " + m.version + Tr(" could not be downloaded from GameBanana and its author does not allow the host to share it.");
		return false;
	}
	LogI("content-share", "room mods: %u mods are requested from the host through Steam", static_cast<unsigned>(list.size()));
	SteamResetModDownload();
	SteamRequestMods(list);
	g_s.syncStage = 2;
	g_s.rate.Reset();
	return true;
}

void StartSyncReceive(const std::vector<OnlineMod> &missing)
{
	std::vector<RoomModRequest> io;
	g_s.syncHostList.clear();
	g_s.syncInstalled.clear();
	g_s.syncFromIo.clear();
	g_s.syncRetryHost = false;
	g_s.rate.Reset();
	for (const OnlineMod &m : missing) {
		if (m.gbId) io.push_back({m.id, m.gbId, m.version, m.gbFile, m.copyKey});
		else g_s.syncHostList.push_back(m);
	}
	g_s.syncPhase = 2;
	if (!io.empty()) {
		LogI("content-share", "room mods: %u mods are downloaded from GameBanana, %u are requested from the host", static_cast<unsigned>(io.size()),
			static_cast<unsigned>(g_s.syncHostList.size()));
		g_s.io = RoomModStatus();
		g_s.io.active = true;
		g_s.syncStage = 1;
		RoomModsFromGb(io);
		return;
	}
	if (!RequestFromHost(g_s.syncHostList)) return;
}
}
}

namespace mpui_impl {
void SyncTick()
{
	switch (g_s.syncPhase) {
	case 1:
		if (g_s.room.id == g_s.syncLobby.id) {
			g_s.mods = ModsList();
			if (CopiesStillHashing(g_s.mods, g_s.syncLobby.mods) && GetTickCount() - g_s.syncStart < 120000) break;
			const std::vector<OnlineMod> missing = MissingMods(g_s, g_s.syncLobby);
			if (missing.empty()) {
				g_s.syncPhase = 3;
				g_syncResult.clear();
				StartSyncCheck(g_s.syncLobby, {}, false);
			}
			else {
				StartSyncReceive(missing);
			}
		}
		else if (GetTickCount() - g_s.syncStart > 20000) {
			g_s.syncPhase = 4;
			g_s.syncError = "Could not join the room.";
		}
		break;
	case 2:
		if (g_s.syncStage == 1) {
			g_s.io = RoomModsStatus();
			UpdateSpeed(g_s.io.got);
			SteamReportDownloadProgress(g_s.io.got, g_s.io.total);
			if (!g_s.io.done) break;
			for (const auto &kv : g_s.io.installed) {
				g_s.syncInstalled[kv.first] = kv.second;
				g_s.syncFromIo.insert(kv.first);
			}
			std::vector<OnlineMod> rest = g_s.syncHostList;
			for (const std::string &id : g_s.io.failed) {
				for (const OnlineMod &m : g_s.syncLobby.mods) {
					if (m.id == id) rest.push_back(m);
				}
			}
			if (!RequestFromHost(rest)) break;
			if (g_s.syncStage == 2 || g_s.syncPhase != 2) break;
			g_s.syncPhase = 3;
			g_syncResult.clear();
			StartSyncCheck(g_s.syncLobby, g_s.syncInstalled, g_s.syncUpdate);
			break;
		}
		g_s.dl = SteamModDownload();
		UpdateSpeed(g_s.dl.got);
		if (g_s.dl.failed) {
			g_s.syncPhase = 4;
			g_s.syncError = g_s.dl.error == "you were kicked from the room" ? "You have been kicked from the room: the content download has stopped."
				: Tr("Could not receive the content: ") + g_s.dl.error + ".";
		}
		else if (g_s.dl.done) {
			for (const auto &kv : g_s.dl.installed) g_s.syncInstalled[kv.first] = kv.second;
			g_s.syncPhase = 3;
			g_syncResult.clear();
			StartSyncCheck(g_s.syncLobby, g_s.syncInstalled, g_s.syncUpdate);
		}
		break;
	case 3:
		ModsHashProgress(g_s.hashDone, g_s.hashTotal);
		if (g_s.hashTotal) UpdateSpeed(g_s.hashDone);
		if (g_syncResult.empty()) break;
		if (g_s.room.id != g_s.syncLobby.id) {
			g_s.syncPhase = 4;
			g_s.syncError = "You are no longer in the room (you were kicked or it was closed).";
			break;
		}
		if (g_syncResult.compare(0, 3, "ok:") == 0) {
			const std::string profile = g_syncResult.substr(3);
			ModsSetActiveProfileName(profile);
			SteamSetRejoin(g_s.syncLobby.id);
			LogI("content-share", "room mods: restarting with \"%s\"", profile.c_str());
			ModsRestartGame();
		}
		else if (!g_s.syncFromIo.empty() && !g_s.syncRetryHost) {
			LogI("content-share", "room mods: the GameBanana ones do not match (%s); requesting from the host", g_syncResult.c_str());
			g_s.syncRetryHost = true;
			std::vector<OnlineMod> again;
			for (const OnlineMod &m : g_s.syncLobby.mods) {
				if (g_s.syncFromIo.count(m.id)) {
					again.push_back(m);
					g_s.syncInstalled.erase(m.id);
				}
			}
			g_s.syncFromIo.clear();
			g_s.syncPhase = 2;
			SteamResetModDownload();
			if (!RequestFromHost(again)) break;
		}
		else {
			g_s.syncPhase = 4;
			g_s.syncError = Tr("Could not prepare: ") + g_syncResult + ".";
		}
		break;
	default:
		break;
	}
}
}
