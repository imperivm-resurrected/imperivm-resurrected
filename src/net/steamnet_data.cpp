// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include <algorithm>
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/downloads.h"
#include "mods/mods.h"
#include "mods/civindices.h"
#include "net/modlist.h"
#include "net/scenvalue.h"

#include "net/steamnet_internal.h"

using namespace steamnet_impl;

namespace {
std::mutex g_seenNamesLock;
std::set<std::string> g_seenNames;
const size_t kSeenNamesMax = 256;
}

std::vector<std::string> SteamSeenNames()
{
	std::lock_guard<std::mutex> lock(g_seenNamesLock);
	return std::vector<std::string>(g_seenNames.begin(), g_seenNames.end());
}

namespace steamnet_impl {
void Notice(const std::string &text)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_notice = text;
	LogI("steam", "notice: %s", TrSource(text).c_str());
}

void Post(std::function<void()> fn)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_queue.push_back(std::move(fn));
}

std::string Name(uint64 id)
{
	if (id == g_steam.me) return g_myName;
	const char *n = S.SteamAPI_ISteamFriends_GetFriendPersonaName(g_steam.friends, id);
	if (!n || !*n) return std::to_string(id);
	std::lock_guard<std::mutex> lock(g_seenNamesLock);
	if (g_seenNames.size() < kSeenNamesMax) g_seenNames.insert(n);
	return n;
}

SteamNetworkingIdentity Identity(uint64 id)
{
	SteamNetworkingIdentity who;
	who.Clear();
	who.SetSteamID64(id);
	return who;
}

std::string Fnv(const std::string &s)
{
	uint64_t h = 1469598103934665603ull;
	for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
	char buf[24];
	sprintf_s(buf, "%016llx", h);
	return buf;
}

std::string Data(uint64 lobby, const char *key)
{
	const char *v = S.SteamAPI_ISteamMatchmaking_GetLobbyData(g_steam.mm, lobby, key);
	return v ? v : "";
}

}

namespace steamnet_impl {
std::string EncodeMods(const std::string &scenModId, int &optional)
{
	std::vector<ModInfo> mounted;
	for (const std::string &folder : ModsMountedProfile().mods) {
		ModInfo m;
		if (!ModsFind(folder, m)) continue;
		m.gbFile = DlInstalledGbFile(m);
		mounted.push_back(m);
	}
	return EncodeModList(mounted, scenModId, optional);
}

std::string OwnKey()
{
	const std::string h = ModsProfileGameplayHash(ModsMountedProfile());
	const Json indices = CivIndicesToJson();
	return indices.obj.empty() ? h.substr(0, 16) : Fnv(h + "\n" + indices.Dump());
}

bool IsBanned(uint64 lobby, uint64 id)
{
	return id && Data(lobby, "kick").find(";" + std::to_string(id) + ";") != std::string::npos;
}

bool StaleLobby(uint64 id)
{
	const std::string beat = Data(id, "alive");
	if (beat.empty()) return Data(id, "name").empty();
	const long long age = static_cast<long long>(time(nullptr)) - _atoi64(beat.c_str());
	return age > 180;
}

OnlineLobby ReadLobby(uint64 id)
{
	OnlineLobby l;
	l.id = id;
	l.kickedMe = IsBanned(id, g_steam.me);
	l.name = Data(id, "name");
	l.host = Data(id, "host");
	l.owner = _strtoui64(Data(id, "hostid").c_str(), nullptr, 10);
	l.friendHost = l.owner && l.owner != g_steam.me && S.SteamAPI_ISteamFriends_HasFriend(g_steam.friends, l.owner, k_EFriendFlagImmediate);
	l.map = Data(id, "map");
	l.mode = Data(id, "mode");
	l.gameplayKey = Data(id, "key");
	{
		ScenValue scen;
		if (ScenValueParse(Data(id, "scen"), scen) && !scen.modId.empty()) {
			l.scenModId = scen.modId;
			l.scenModVersion = scen.modVersion;
			l.scenModName = scen.modName;
			l.scenSimple = scen.simple;
			l.scenShareable = scen.shareable;
			l.scenGbId = scen.gbId;
			l.scenMapSize = scen.mapSize;
		}
	}
	l.password = !Data(id, "pw").empty();
	l.visibility = atoi(Data(id, "vis").c_str());
	l.hosting = Data(id, "hosting") == "1";
	l.playing = Data(id, "playing") == "1";
	l.cams = Data(id, "cams");
	l.optionsText = Data(id, "opts");
	l.rules = static_cast<unsigned>(strtoul(Data(id, "rules").c_str(), nullptr, 16));
	l.civSets = Data(id, "civsets");
	{
		const std::string colors = Data(id, "savecolors");
		for (size_t p = 0; !colors.empty() && p <= colors.size();) {
			const size_t q = colors.find(',', p);
			const std::string item = colors.substr(p, q == std::string::npos ? std::string::npos : q - p);
			const size_t slash = item.find('/');
			l.saveColors.push_back(atoi(item.c_str()));
			l.saveRaces.push_back(slash == std::string::npos ? -1 : atoi(item.c_str() + slash + 1));
			if (q == std::string::npos) break;
			p = q + 1;
		}
	}
	{
		const std::string list = Data(id, "plist");
		size_t from = 0;
		while (from < list.size()) {
			size_t to = list.find(10, from);
			if (to == std::string::npos) to = list.size();
			if (to > from) l.playerNames.push_back(list.substr(from, to - from));
			from = to + 1;
		}
	}
	{
		const std::string save = Data(id, "save");
		l.saveName = save.substr(0, save.find(9));
	}
	l.mods = DecodeModList(Data(id, "mods"));
	l.optionalMods = atoi(Data(id, "nopt").c_str());
	l.myNumber = atoi(Data(id, ("k:" + std::to_string(g_steam.me)).c_str()).c_str());
	l.modsRequiredOnly = Data(id, "modsv") == "2";
	l.version = Data(id, "irver");
	l.rulesLevel = Data(id, "irlvl");
	l.spectate = Data(id, "spec") == "1";
	l.specLobby = _strtoui64(Data(id, "sl").c_str(), nullptr, 10);
	l.specDelay = atoi(Data(id, l.spectate ? "delay" : "sd").c_str());
	l.started = _atoi64(Data(id, "t0").c_str());
	l.players = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, id);
	l.maxPlayers = S.SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(g_steam.mm, id);
	{
		const std::string text = Data(id, "slots");
		size_t from = 0;
		while (from < text.size()) {
			size_t to = text.find(10, from);
			if (to == std::string::npos) to = text.size();
			const std::string line = text.substr(from, to - from);
			from = to + 1;
			const size_t bar = line.find('|');
			if (bar == std::string::npos) continue;
			OnlineSlot s;
			s.type = atoi(line.substr(0, bar).c_str());
			s.name = line.substr(bar + 1);
			l.slots.push_back(s);
		}
	}
	if (l.spectate) l.spectators = (std::max)(0, l.players - 1);
	l.watching = (std::max)(0, atoi(Data(id, "watching").c_str()));
	if (!l.slots.empty()) {
		int taken = 0, open = 0;
		for (const OnlineSlot &s : l.slots) {
			if (s.type == 1 || s.type == 3 || s.type == 5) taken++;
			if (s.type != 2 && s.type != 4 && s.type != 5) open++;
		}
		l.players = taken;
		l.maxPlayers = open;
	}
	return l;
}
}
