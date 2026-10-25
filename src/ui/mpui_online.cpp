// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "ui/modui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
std::string IniFile()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\') + 1) + "imperivm-mod.ini";
}
}
}

namespace mpui_impl {
uint64_t UnixNow()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return ((static_cast<uint64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) - 116444736000000000ULL) / 10000000ULL;
}
}

namespace mpui_impl {
namespace {
uint64_t FileTimeToUnix(uint64_t ft)
{
	return ft > 116444736000000000ULL ? (ft - 116444736000000000ULL) / 10000000ULL : 0;
}
}
}

namespace mpui_impl {
void RecordOnlineGame(const OnlineRoom &room)
{
	bool host = false;
	uint64_t hostId = 0;
	std::string hostName, others, otherIds;
	for (const OnlineMember &m : room.members) {
		if (m.owner) {
			hostId = m.id;
			hostName = m.name;
			if (m.me) host = true;
		}
		if (!m.me) {
			others += (others.empty() ? "" : ", ") + m.name;
			otherIds += (otherIds.empty() ? "" : ",") + std::to_string(m.id);
		}
	}
	const std::string value = std::to_string(UnixNow()) + "|" + (host ? "1" : "0") + "|" + std::to_string(hostId) + "|" + hostName + "|" + others +
		"|" + otherIds;
	WritePrivateProfileStringA("Online", "GameInProgress", value.c_str(), IniFile().c_str());
}

void RecordOnlineGameRaw(const RecoverInfo &r)
{
	const std::string value = std::to_string(r.start) + "|" + (r.host ? "1" : "0") + "|" + std::to_string(r.hostId) + "|" + r.hostName + "|" + r.others +
		"|" + r.otherIds;
	WritePrivateProfileStringA("Online", "GameInProgress", value.c_str(), IniFile().c_str());
}

void ClearOnlineGame()
{
	WritePrivateProfileStringA("Online", "GameInProgress", nullptr, IniFile().c_str());
}
}

namespace mpui_impl {
namespace {
bool ReadOnlineGame(RecoverInfo &out)
{
	char buf[512] = {};
	GetPrivateProfileStringA("Online", "GameInProgress", "", buf, sizeof(buf), IniFile().c_str());
	std::vector<std::string> f;
	std::string s = buf;
	for (size_t p = 0;;) {
		const size_t q = s.find('|', p);
		f.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
		if (q == std::string::npos) break;
		p = q + 1;
	}
	if (f.size() < 5) return false;
	out.start = _strtoui64(f[0].c_str(), nullptr, 10);
	out.host = f[1] == "1";
	out.hostId = _strtoui64(f[2].c_str(), nullptr, 10);
	out.hostName = f[3];
	out.others = f[4];
	out.otherIds = f.size() > 5 ? f[5] : "";
	return out.start && UnixNow() - out.start < 24 * 3600;
}
}
}

namespace mpui_impl {
void CheckRecover()
{
	RecoverInfo r;
	if (!ReadOnlineGame(r)) return;
	if (!r.host) {
		g_s.recover = r;
		g_s.recoverAsk = true;
		return;
	}
	if (r.host) {
		if (r.otherIds.empty()) return;
		const size_t comma = r.otherIds.find(',');
		r.host = false;
		r.hostId = _strtoui64(r.otherIds.substr(0, comma).c_str(), nullptr, 10);
		const size_t sep = r.others.find(", ");
		r.hostName = sep == std::string::npos ? r.others : r.others.substr(0, sep);
		if (!r.hostId) return;
		g_s.recover = r;
		g_s.recoverAsk = true;
		return;
	}
	if (r.host) {
		for (size_t i = 0; i < g_s.saves.size(); i++) {
			if (FileTimeToUnix(g_s.saves[i].time) >= r.start) {
				r.saveIndex = static_cast<int>(i);
				r.saveAge = UnixNow() - FileTimeToUnix(g_s.saves[i].time);
				break;
			}
		}
		if (r.saveIndex < 0) {
			ClearOnlineGame();
			return;
		}
	}
	g_s.recover = r;
	g_s.recoverAsk = true;
}

bool CanReturnToGame(const State &st, const OnlineLobby &g)
{
	if (!g.playing || g.specOnly || g.kickedMe || g.myNumber <= 0 || !g.owner) return false;
	if (g.slots.empty()) return true;
	for (const OnlineSlot &s : g.slots) {
		if (s.type == 3 && PlayerNameMatches(s.name, st.player)) return true;
	}
	return false;
}

namespace {
OnlineLobby WithMapPack(const OnlineLobby &lobby)
{
	OnlineLobby out = lobby;
	if (lobby.scenModId.empty()) return out;
	for (const OnlineMod &m : lobby.mods) {
		if (m.id == lobby.scenModId) return out;
	}
	OnlineMod pack;
	pack.id = lobby.scenModId;
	pack.name = lobby.scenModName;
	pack.version = lobby.scenModVersion;
	pack.gameplay = false;
	pack.kind = 1;
	pack.bytes = lobby.scenMapSize;
	pack.redistributable = lobby.scenShareable;
	pack.gbId = lobby.scenGbId;
	out.mods.push_back(pack);
	return out;
}
}

void TryJoinNow(const OnlineLobby &lobby, const std::string &password, bool watch)
{
	const bool strictMap = g_s.forcePackLobby && g_s.forcePackLobby == lobby.id;
	if (strictMap) g_s.forcePackLobby = 0;
	const OnlineLobby withPack = strictMap ? WithMapPack(lobby) : OnlineLobby();
	const OnlineLobby &g = strictMap ? withPack : lobby;
	if (watch && !g.specLobby) {
		ModUiToast("This game does not allow spectators.");
		return;
	}
	if (g.kickedMe) {
		ModUiToast("You have been kicked from the room.");
		return;
	}
	if (g.playing && !watch) {
		ModUiToast("That game has already started: you can't join midway.");
		return;
	}
	const std::string mismatch = SteamVersionMismatch(g.version, g.rulesLevel);
	if (!mismatch.empty()) {
		ModUiToast(mismatch, true);
		return;
	}
	if (g.password && password.empty()) {
		g_s.passLobby = g;
		g_s.passWatch = watch;
		g_s.passPrompt = true;
		g_s.joinPassInput.clear();
		FocusField(kJoinPassField);
		return;
	}
	OnlineLobby target = g;
	if (watch) target.id = g.specLobby;
	if (g_s.ownKey.empty()) g_s.ownKey = SteamOwnGameplayKey();
	const RoomContent content = SteamRoomContent(g, g_s.ownKey, strictMap, watch);
	if (content == RoomContent::kChecking) {
		ModUiToast("Your content is still being checked: try again in a few seconds.");
		return;
	}
	if (RoomContentDiffers(content)) {
		std::vector<std::string> blockedNames, hostOnlyNames;
		for (const OnlineMod &m : MissingMods(g_s, g)) {
			if (!m.redistributable && !m.gbId) blockedNames.push_back(m.name + " " + m.version);
			else if (!m.gbId) hostOnlyNames.push_back(m.name + " " + m.version);
		}
		if (!blockedNames.empty()) {
			ModUiToast(Tr("This room uses content whose author doesn't allow sharing: ") + ShortList(blockedNames) +
				Tr(". Download it from its page (Options > Content > Browse content)."));
			return;
		}
		const bool live = watch && g.playing;
		if (live && !hostOnlyNames.empty()) {
			ModUiToast(TrFmt("This game uses content that is not on GameBanana (%s). The host can't send it during a game: you can watch "
				"once you have it.", {ShortList(hostOnlyNames)}), true);
			return;
		}
		g_s.syncWatch = watch;
		g_s.syncLive = live;
		g_s.syncUsing.clear();
		for (const OnlineMod &m : MissingMods(g_s, g)) {
			const ModInfo *old = OlderInstalled(g_s, m);
			if (!old) continue;
			for (const std::string &pn : ModsProfilesUsing(old->folder)) g_s.syncUsing += (g_s.syncUsing.empty() ? "" : ", ") + pn;
		}
		g_s.syncUpdate = false;
		g_s.syncConfirm = true, g_s.syncList = ListState();
		g_s.syncLobby = target;
		g_s.syncPass = password;
		return;
	}
	SteamJoinLobby(target.id, password);
}

void TryJoin(const OnlineLobby &g, const std::string &password, bool watch)
{
	if (g_s.joinCheck.id) return;
	g_s.joinCheck.id = g.id;
	g_s.joinCheck.password = password;
	g_s.joinCheck.watch = watch;
	g_s.joinCheck.since = GetTickCount();
	g_s.joinCheck.serial = SteamLobbyDataSerial(g.id);
	SteamRequestLobbyData(g.id);
}

void TrySpectate(const OnlineLobby &g, const std::string &password)
{
	if (!g.specLobby) {
		ModUiToast("This game does not allow spectators.");
		return;
	}
	TryJoin(g, password, true);
}
}
