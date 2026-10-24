// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include <algorithm>
#include <atomic>
#include "core/guard.h"
#include "core/log.h"
#include "mods/downloads.h"
#include "mods/mods.h"
#include "mods/civindices.h"
#include "net/mpsave.h"
#include "net/rejoin.h"
#include "net/scenvalue.h"
#include "net/sharemsg.h"
#include "i18n/tr.h"

#include "net/steamnet_internal.h"

using namespace steamnet_impl;

namespace steamnet_impl {
namespace {
std::string GameFolder()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

std::string g_savePublishedFile;

const char kSaveTransferPath[] = "#save";

std::string ScenarioFile(const std::string &path)
{
	if (path == kSaveTransferPath) return GameFolder() + "\\SaveOnline\\room.snr";
	std::string p = path;
	for (char &c : p) {
		if (c == '/') c = '\\';
	}
	if (_strnicmp(p.c_str(), "Scenarios\\", 10) != 0 || p.find("..") != std::string::npos || p.find(':') != std::string::npos) return "";
	return GameFolder() + "\\" + p + ".bfhp";
}

bool FileFingerprint(const std::string &file, uint64_t &size, std::string &hash)
{
	HANDLE h = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	uint64_t fnv = 1469598103934665603ULL;
	size = 0;
	std::vector<uint8_t> buf(64 * 1024);
	DWORD got = 0;
	while (ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got) {
		for (DWORD i = 0; i < got; i++) fnv = (fnv ^ buf[i]) * 1099511628211ULL;
		size += got;
	}
	CloseHandle(h);
	char hex[17];
	sprintf_s(hex, "%016llx", static_cast<unsigned long long>(fnv));
	hash = hex;
	return true;
}

const uint64_t kMaxMapBytes = 64ull * 1024 * 1024;

std::string g_scenPublished;
std::string g_scenPackFile;
bool g_scenPackShare = false;
std::atomic<int> g_fetchToken{0};
}
}

namespace steamnet_impl {
void PublishScenario(const std::string &path)
{
	if (!g_lobby || path == g_scenPublished) return;
	g_scenPublished = path;
	g_scenPackFile.clear();
	g_scenPackShare = false;
	ScenValue scen;
	if (!path.empty()) {
		scen.path = path;
		uint64_t size = 0;
		std::string hash;
		const std::string file = ScenarioFile(path);
		const bool loose = !file.empty() && FileFingerprint(file, size, hash);
		if (loose) {
			scen.size = size;
			scen.hash = hash;
		}
		else if (ModsScenarioProvider(path, scen.modId, scen.modVersion, scen.modName)) {
			ModsMapPack pack;
			uint64_t packSize = 0;
			std::string packHash;
			if (ModsScenarioPack(path, pack) && pack.simple && FileFingerprint(pack.file, packSize, packHash) && packSize > 0 && packSize <= kMaxMapBytes) {
				scen.simple = true;
				scen.shareable = pack.shareable;
				scen.mapSize = packSize;
				scen.mapHash = packHash;
				scen.gbId = pack.gbId;
				g_scenPackFile = pack.file;
				g_scenPackShare = pack.shareable;
			}
		}
	}
	const std::string value = path.empty() ? std::string() : ScenValueEncode(scen);
	std::string modId = scen.modId;
	std::replace(modId.begin(), modId.end(), '\t', ' ');
	PublishMods(scen.simple ? std::string() : modId);
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "scen", value.c_str());
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_share.scenarioValue = value;
	}
	LogI("room", "scenario of the room: %s", path.empty() ? "(random map)" : value.c_str());
}
}

namespace steamnet_impl {
namespace {
void HostOnMapRequest(uint64 peer, const std::string &path)
{
	const bool save = path == kSaveTransferPath;
	std::string file = save ? g_savePublishedFile : ScenarioFile(path);
	if (!save && path == g_scenPublished && g_scenPackShare && !g_scenPackFile.empty() && !file.empty() &&
		GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
		file = g_scenPackFile;
	}
	uint64_t size = 0;
	std::string hash;
	if (file.empty() || (!save && path != g_scenPublished) || !FileFingerprint(file, size, hash) || size > kMaxMapBytes) {
		LogW("room", "scenarios: \"%s\" is not sent to %s", path.c_str(), Name(peer).c_str());
		return;
	}
	for (size_t i = 0; i < g_share.out.size();) {
		OutTransfer &o = g_share.out[i];
		const bool sameMap = o.peer == peer && !o.mods && !o.steps.empty() && o.steps.back().type == kShMapEnd && o.steps.back().text == path;
		if (!sameMap) {
			i++;
			continue;
		}
		if (o.file != INVALID_HANDLE_VALUE) CloseHandle(o.file);
		LogW("room", "scenarios: previous sending of \"%s\" to %s discarded (they ask again)", path.c_str(), Name(peer).c_str());
		g_share.out.erase(g_share.out.begin() + i);
	}
	OutTransfer t;
	t.peer = peer;
	t.steps.push_back({kShMapFile, path + "\t" + std::to_string(size), file});
	t.steps.push_back({kShMapEnd, path, ""});
	g_share.out.push_back(std::move(t));
	LogI("room", "scenarios: sending \"%s\" (%llu bytes) to %s", path.c_str(), size, Name(peer).c_str());
}

struct MapIn {
	uint64 host = 0;
	std::string handled;
	std::string handledSave;
	bool isSave = false;
	std::string path, part;
	Json saveMetadata;
	HANDLE file = INVALID_HANDLE_VALUE;
};
MapIn g_mapIn;
}
}

namespace steamnet_impl {
}

namespace steamnet_impl {
namespace {
OnlineMapStatus &InStatus()
{
	return g_mapIn.isSave ? g_share.saveStatus : g_share.mapStatus;
}

void MapFailed(const std::string &why)
{
	if (g_mapIn.file != INVALID_HANDLE_VALUE) CloseHandle(g_mapIn.file);
	g_mapIn.file = INVALID_HANDLE_VALUE;
	if (!g_mapIn.part.empty()) DeleteFileA(g_mapIn.part.c_str());
	std::lock_guard<std::mutex> lock(g_lock);
	InStatus().state = 2;
	InStatus().error = why;
	LogW("room", "scenarios: could not receive (%s)", why.c_str());
}
}
}

namespace steamnet_impl {
namespace {
void FetchPackMapThread(int token, ScenValue sv, std::string value, uint64 host, std::string file, std::string localFile)
{
	const std::string leaf = file.substr(file.find_last_of('\\') + 1);
	const std::string part = file + ".partial";
	auto stale = [token] { return token != g_fetchToken.load(); };
	uint64_t size = 0;
	std::string hash;
	bool local = false;
	if (!localFile.empty() && FileFingerprint(localFile, size, hash) && size == sv.mapSize && hash == sv.mapHash) {
		local = CopyFileA(localFile.c_str(), part.c_str(), FALSE) != 0;
	}
	std::string err;
	if (!local) {
		err = DlFetchRoomMap(
			sv.gbId, sv.modVersion, sv.modId, sv.modName, leaf, part,
			[&](uint64_t got, uint64_t total) {
				if (stale()) return;
				std::lock_guard<std::mutex> lock(g_lock);
				g_share.mapStatus.got = got;
				g_share.mapStatus.total = total ? total : sv.mapSize;
			},
			stale);
	}
	if (err.empty() && !(FileFingerprint(part, size, hash) && size == sv.mapSize && hash == sv.mapHash)) {
		err = "the map on GameBanana is not the one of the host";
	}
	if (stale()) {
		DeleteFileA(part.c_str());
		return;
	}
	if (err.empty() && GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES) err = "a map with that name already exists";
	if (err.empty() && !MoveFileExA(part.c_str(), file.c_str(), 0)) err = "could not save the map";
	if (err.empty()) {
		ModsNoteReceivedMap(file);
		LogI("room", "scenarios: \"%s\" %s (%llu bytes)", sv.path.c_str(), local ? "copied from the package you already have" : "downloaded from GameBanana",
			static_cast<unsigned long long>(sv.mapSize));
		Post([=] {
			if (stale()) return;
			ShareSend(host, kShMapHave, value.data(), value.size());
			std::lock_guard<std::mutex> lock(g_lock);
			g_share.mapStatus.state = 0;
			g_share.mapStatus.ready = true;
		});
		return;
	}
	DeleteFileA(part.c_str());
	LogW("room", "scenarios: could not download \"%s\" from GameBanana (%s)", sv.path.c_str(), err.c_str());
	Post([=] {
		if (stale()) return;
		if (sv.shareable) {
			g_mapIn.host = host;
			ShareSend(host, kShMapReq, sv.path.data(), sv.path.size());
			LogI("room", "scenarios: requesting \"%s\" from the host", sv.path.c_str());
			std::lock_guard<std::mutex> lock(g_lock);
			g_share.mapStatus.got = 0;
			g_share.mapStatus.total = sv.mapSize;
			return;
		}
		std::lock_guard<std::mutex> lock(g_lock);
		g_share.mapStatus.state = 2;
		g_share.mapStatus.error = err;
		g_share.mapStatus.needsPack = true;
		g_share.mapStatus.packName = sv.modName;
	});
}

void NeedsPack(OnlineMapStatus &st, const ScenValue &sv, const std::string &why)
{
	LogW("room", "scenarios: the map \"%s\" comes from \"%s\" %s, which is not loaded (%s)", sv.path.c_str(), sv.modName.c_str(), sv.modVersion.c_str(),
		why.empty() ? "it brings more than the map" : why.c_str());
	st.state = 2;
	st.error = why;
	st.needsPack = true;
	st.packName = sv.modName;
	if (g_lobby != g_pendingJoin.syncLobby) {
		Notice(TrFmt("The room map comes from \"%s\", which you do not have loaded: use the button to download it and come back", {sv.modName}));
	}
}

void BeginPackMapFetch(const ScenValue &sv, const std::string &value, uint64 host, const std::string &file, OnlineMapStatus &st)
{
	uint64_t mySize = 0;
	std::string myHash;
	if (GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
		if (FileFingerprint(file, mySize, myHash) && mySize == sv.mapSize && myHash == sv.mapHash) {
			ShareSend(host, kShMapHave, value.data(), value.size());
			return;
		}
		NeedsPack(st, sv, "you already have another map with that name");
		return;
	}
	st.state = 1;
	st.total = sv.mapSize;
	SafeThread(FetchPackMapThread, g_fetchToken.load(), sv, value, host, file, ModsInstalledPackMap(sv.modId, sv.modVersion, sv.path)).detach();
}
}
}

namespace steamnet_impl {
void CheckRoomScenario()
{
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) return;
	const std::string value = Data(g_lobby, "scen");
	if (value == g_mapIn.handled) return;
	g_mapIn.handled = value;
	g_fetchToken++;
	ScenValue sv;
	OnlineMapStatus st;
	if (ScenValueParse(value, sv)) {
		st.name = sv.path.substr(sv.path.find_last_of("/\\") + 1);
		const std::string file = ScenarioFile(sv.path);
		const bool fromMod = !sv.modId.empty();
		const uint64 host = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
		uint64_t mySize = 0;
		std::string myHash;
		if (fromMod && sv.size == 0) {
			if (ModsMountedHasExact(sv.modId, sv.modVersion)) {
				ShareSend(host, kShMapHave, value.data(), value.size());
			}
			else if (sv.simple && !file.empty()) {
				BeginPackMapFetch(sv, value, host, file, st);
			}
			else {
				NeedsPack(st, sv, "");
			}
		}
		else if (sv.size && !file.empty() && !(FileFingerprint(file, mySize, myHash) && mySize == sv.size && myHash == sv.hash)) {
			g_mapIn.host = host;
			st.state = 1;
			st.total = sv.size;
			ShareSend(g_mapIn.host, kShMapReq, sv.path.data(), sv.path.size());
			LogI("room", "scenarios: requesting \"%s\" from the host", sv.path.c_str());
		}
		else if (sv.size) {
			ShareSend(host, kShMapHave, value.data(), value.size());
		}
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.mapStatus = st;
}

void CheckRoomSave()
{
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) return;
	const std::string value = Data(g_lobby, "save");
	if (value == g_mapIn.handledSave) return;
	g_mapIn.handledSave = value;
	const std::vector<std::string> f = SplitTabs(value);
	OnlineMapStatus st;
	if (f.size() >= 3 && !f[0].empty()) {
		st.name = f[0];
		Json metadata = Json::MakeObject();
		if ((f.size() > 3 && (!Json::Parse(f[3], metadata) || metadata.type != Json::Object)) ||
			CivIndicesCheck(metadata.Get("civIndices")) != CivIndicesStatus::Compatible ||
			(metadata.Get("civIndices") && metadata.GetString("civSaveHash").size() != 64)) {
			st.state = 2;
			st.error = Tr("The room's saved game has a missing or incompatible civilization index table.");
			std::lock_guard<std::mutex> lock(g_lock);
			g_share.saveStatus = st;
			return;
		}
		if (const Json *rules = metadata.Get("rules")) {
			if (rules->type != Json::Number || !(rules->num >= 0 && rules->num <= 4294967295.0) || rules->num != static_cast<unsigned>(rules->num)) {
				st.state = 2;
				st.error = Tr("The room's saved game metadata is invalid.");
				std::lock_guard<std::mutex> lock(g_lock);
				g_share.saveStatus = st;
				return;
			}
		}
		else metadata.Set("rules", Json::MakeNumber(ReadLobby(g_lobby).rules));
		g_mapIn.saveMetadata = metadata;
		const uint64_t size = _strtoui64(f[1].c_str(), nullptr, 10);
		uint64_t mySize = 0;
		std::string myHash;
		if (!(FileFingerprint(ScenarioFile(kSaveTransferPath), mySize, myHash) && mySize == size && myHash == f[2])) {
			g_mapIn.host = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
			st.state = 1;
			st.total = size;
			ShareSend(g_mapIn.host, kShMapReq, kSaveTransferPath, strlen(kSaveTransferPath));
			LogD("match", "online game: requesting the saved game \"%s\" from the host", f[0].c_str());
		}
		else if (WriteJsonFile(ScenarioFile(kSaveTransferPath) + ".mods.json", metadata) && ModsCheckCivIndices(ScenarioFile(kSaveTransferPath)).empty()) {
			st.ready = true;
		}
		else {
			st.state = 2;
			st.error = Tr("Could not save the civilization index table.");
		}
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.saveStatus = st;
}

void PublishSave(const std::string &file, const std::string &name)
{
	if (!g_lobby) return;
	g_savePublishedFile = file;
	uint64_t size = 0;
	std::string hash, value;
	if (!file.empty() && FileFingerprint(file, size, hash)) {
		Json side;
		ReadJsonFile(file + ".mods.json", side);
		if (!ModsCheckCivIndices(file).empty()) {
			Notice(Tr("This saved game has a missing or incompatible civilization index table. Activate its original content profile and restore its .mods.json file."));
			g_savePublishedFile.clear();
		}
		else {
			Json metadata = Json::MakeObject();
			if (const Json *table = side.Get("civIndices")) {
				metadata.Set("civIndices", *table);
				metadata.Set("civSaveHash", Json::MakeString(side.GetString("civSaveHash")));
			}
			if (const Json *rules = side.Get("rules")) metadata.Set("rules", *rules);
			value = name + "\t" + std::to_string(size) + "\t" + hash + "\t" + metadata.Dump();
		}
	}
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "save", value.c_str());
	std::string cams;
	FILE *f = nullptr;
	if (!value.empty() && fopen_s(&f, (file + ".cameras").c_str(), "rb") == 0 && f) {
		char buf[1024] = {};
		const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
		cams.assign(buf, n);
	}
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "cams", cams.c_str());
	LogI("match", "online game: the room loads %s", value.empty() ? "(nothing)" : ("\"" + name + "\"").c_str());
}
}

namespace steamnet_impl {
namespace {
bool ClientOnMapMessage(uint64 peer, const uint8_t *data, size_t len)
{
	if (!len || peer != g_mapIn.host) return false;
	const uint8_t type = data[0];
	const std::string text(reinterpret_cast<const char *>(data + 1), len - 1);
	if (type == kShMapFile) {
		const std::vector<std::string> f = SplitTabs(text);
		const std::string file = f.empty() ? "" : ScenarioFile(f[0]);
		if (file.empty()) {
			MapFailed("the host sent a disallowed path");
			return true;
		}
		if (g_mapIn.file != INVALID_HANDLE_VALUE) {
			CloseHandle(g_mapIn.file);
			g_mapIn.file = INVALID_HANDLE_VALUE;
			LogI("room", "scenarios: the previous download was left half done; starting over");
		}
		g_mapIn.isSave = f[0] == kSaveTransferPath;
		g_mapIn.path = file;
		g_mapIn.part = file + ".partial";
		MakeDirs(file.substr(0, file.rfind('\\')));
		g_mapIn.file = CreateFileA(g_mapIn.part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (g_mapIn.file == INVALID_HANDLE_VALUE) MapFailed("could not create the file");
		return true;
	}
	if (type == kShData && g_mapIn.file != INVALID_HANDLE_VALUE) {
		DWORD wrote = 0;
		if (!WriteFile(g_mapIn.file, data + 1, static_cast<DWORD>(len - 1), &wrote, nullptr)) {
			MapFailed("could not write");
			return true;
		}
		std::lock_guard<std::mutex> lock(g_lock);
		InStatus().got += len - 1;
		return true;
	}
	if (type == kShMapEnd && g_mapIn.file != INVALID_HANDLE_VALUE) {
		CloseHandle(g_mapIn.file);
		g_mapIn.file = INVALID_HANDLE_VALUE;
		if (GetFileAttributesA(g_mapIn.path.c_str()) != INVALID_FILE_ATTRIBUTES) {
			MoveFileExA(g_mapIn.path.c_str(), (g_mapIn.path + ".previous").c_str(), MOVEFILE_REPLACE_EXISTING);
		}
		if (!MoveFileExA(g_mapIn.part.c_str(), g_mapIn.path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			MapFailed("could not save the scenario");
			return true;
		}
		if (g_mapIn.isSave && !WriteJsonFile(g_mapIn.path + ".mods.json", g_mapIn.saveMetadata)) {
			MapFailed(Tr("Could not save the civilization index table."));
			return true;
		}
		if (g_mapIn.isSave) {
			const std::string problem = ModsCheckCivIndices(g_mapIn.path);
			if (!problem.empty()) {
				MapFailed(problem);
				return true;
			}
		}
		LogI("room", "scenarios: received %s", g_mapIn.path.c_str());
		if (!g_mapIn.isSave && !g_mapIn.handled.empty()) {
			ShareSend(g_mapIn.host, kShMapHave, g_mapIn.handled.data(), g_mapIn.handled.size());
		}
		ModsNoteReceivedMap(g_mapIn.path);
		std::lock_guard<std::mutex> lock(g_lock);
		InStatus().state = 0;
		InStatus().ready = true;
		return true;
	}
	return false;
}
}
}

namespace steamnet_impl {
void ResetRoomScenario()
{
	g_fetchToken++;
	g_scenPackFile.clear();
	g_scenPackShare = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_share.scenarioValue.clear();
		g_share.mapHave.clear();
	}
	if (g_scenPublished.empty() && g_mapIn.handled.empty() && g_mapIn.handledSave.empty() && g_savePublishedFile.empty() &&
		g_mapIn.file == INVALID_HANDLE_VALUE) {
		return;
	}
	if (g_mapIn.file != INVALID_HANDLE_VALUE) {
		CloseHandle(g_mapIn.file);
		DeleteFileA(g_mapIn.part.c_str());
	}
	g_mapIn = MapIn();
	g_scenPublished.clear();
	g_savePublishedFile.clear();
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.mapStatus = OnlineMapStatus();
	g_share.saveStatus = OnlineMapStatus();
}

void ReceiveShare()
{
	SteamNetworkingMessage_t *in[32];
	const int n = S.SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(g_steam.msgs, kShareChannel, in, 32);
	for (int i = 0; i < n; i++) {
		const uint64 peer = in[i]->m_identityPeer.GetSteamID64();
		const uint8_t *data = static_cast<const uint8_t *>(in[i]->m_pData);
		const size_t len = static_cast<size_t>(in[i]->m_cbSize);
		if (len && data[0] == kShReq) {
			if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
				HostOnRequest(peer, std::string(reinterpret_cast<const char *>(data + 1), len - 1));
			}
		}
		else if (len && data[0] == kShSeat) {
			if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
				const int slot = atoi(std::string(reinterpret_cast<const char *>(data + 1), len - 1).c_str());
				std::lock_guard<std::mutex> lock(g_seats.mx);
				if (g_seats.requests.size() < 32) g_seats.requests.push_back({peer, slot});
			}
		}
		else if (len && data[0] == kShSwap) {
			bool member = false;
			const int n = g_lobby ? S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_lobby) : 0;
			for (int k = 0; k < n; k++) {
				if (S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_lobby, k) == peer) member = true;
			}
			if (member && len < 128) {
				std::lock_guard<std::mutex> lock(g_seats.mx);
				if (g_seats.swaps.size() < 64) g_seats.swaps.push_back({peer, std::string(reinterpret_cast<const char *>(data + 1), len - 1)});
			}
		}
		else if (len && data[0] == kShMapHave) {
			if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me && len < 1024) {
				std::lock_guard<std::mutex> lock(g_lock);
				g_share.mapHave[peer] = std::string(reinterpret_cast<const char *>(data + 1), len - 1);
			}
		}
		else if (len && data[0] == kShProgress) {
			if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me && len < 64) {
				unsigned long long got = 0, total = 0;
				const std::string t(reinterpret_cast<const char *>(data + 1), len - 1);
				if (sscanf_s(t.c_str(), "%llu %llu", &got, &total) == 2 && total) {
					std::lock_guard<std::mutex> lock(g_lock);
					g_share.extProgress[peer] = {std::min<uint64_t>(got, total), total, GetTickCount()};
				}
			}
		}
		else if (len && data[0] == kShCam) {
			int player = 0, x = 0, y = 0;
			const std::string t(reinterpret_cast<const char *>(data + 1), len - 1);
			if (sscanf_s(t.c_str(), "%d %d %d", &player, &x, &y) == 3) MpSaveSetPlayerCamera(player, x, y);
		}
		else if (len && kSpectatorMessages.Has(data[0])) {
			const bool fromViewer = g_spectate.lobby && InLobby(g_spectate.lobby, peer);
			const bool fromHost = g_lobby && Data(g_lobby, "spec") == "1" && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == peer;
			if (fromViewer || fromHost) SpectatorOnSteamMessage(peer, data[0], std::string(reinterpret_cast<const char *>(data + 1), len - 1));
		}
		else if (len && (kRejoinMessages.Has(data[0]) || kReloadMessages.Has(data[0]))) {
			RejoinOnSteamMessage(peer, data[0], std::string(reinterpret_cast<const char *>(data + 1), len - 1));
		}
		else if (len && data[0] == kShMapReq) {
			if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
				HostOnMapRequest(peer, std::string(reinterpret_cast<const char *>(data + 1), len - 1));
			}
		}
		else if (!ClientOnMapMessage(peer, data, len)) {
			ClientOnMessage(peer, data, len);
		}
		in[i]->Release();
	}
	PumpOut();
}
}
