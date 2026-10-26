// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <memory>
#include <vector>

#include "net/roomcontent.h"

struct OnlineMod {
	std::string id, name, version;
	bool gameplay = true;
	int kind = 2;
	uint64_t bytes = 0;
	bool redistributable = false;
	uint64_t gbId = 0;
	uint64_t gbFile = 0;
	std::string copyKey;
};

struct OnlineSlot {
	int type = 0;
	std::string name;
};

struct OnlineLobby {
	uint64_t id = 0;
	bool kickedMe = false;
	std::string name, host, map, mode;
	std::string gameplayKey;
	std::string scenModId, scenModVersion, scenModName;
	bool scenSimple = false;
	bool scenShareable = false;
	uint64_t scenGbId = 0;
	uint64_t scenMapSize = 0;
	int players = 0, maxPlayers = 0;
	bool password = false;
	int visibility = 0;
	bool hosting = false;
	bool playing = false;
	std::string cams;
	uint64_t owner = 0;
	bool friendHost = false;
	std::string optionsText;
	unsigned rules = 0;
	std::string saveName;
	std::vector<std::string> playerNames;
	// SteamIDs in the order of playerNames; empty in rooms made with versions before 1.0.3.
	std::vector<uint64_t> playerIds;
	std::vector<OnlineSlot> slots;
	std::vector<int> saveColors;
	std::vector<int> saveRaces;
	std::vector<OnlineMod> mods;
	int optionalMods = 0;
	int myNumber = 0;
	bool modsRequiredOnly = false;
	std::string version, rulesLevel;
	bool spectate = false;
	uint64_t specLobby = 0;
	bool specOnly = false;
	int spectators = 0;
	int watching = 0;
	int specDelay = 0;
	long long started = 0;
};

struct OnlineMember {
	uint64_t id = 0;
	std::string name;
	int number = 0;
	std::string route;
	bool owner = false;
	bool me = false;
	int receiving = -1;
	uint64_t receivingBytes = 0;
};

struct OnlineChatLine {
	uint64_t id = 0;
	std::string who, text;
	int minute = 0;
	bool system = false;
	// A system line saying that a patron joined; id and who are the patron's.
	bool patron = false;
};

struct OnlineRoom {
	uint64_t id = 0;
	int bannedPresent = 0;
	OnlineLobby info;
	std::vector<OnlineMember> members;
	std::vector<OnlineMember> restarting;
	std::vector<OnlineChatLine> chat;
	uint32_t chatSerial = 0;
	bool owner = false;
	bool contentSync = false;
};

struct OnlineCreateSettings {
	std::string name, map, mode, password;
	int maxPlayers = 4;
	int visibility = 0;
	bool spectators = true;
	int specDelay = 30;
};

void StartSteamOnline();
bool SteamOnlineReady();
std::string SteamOnlineStatus();
std::string SteamOnlinePlayerName();
std::vector<std::string> SteamSeenNames();
uint64_t SteamOwnId();

struct OnlineAvatar {
	int width = 0, height = 0;
	std::vector<uint32_t> bgra;
};
std::shared_ptr<const OnlineAvatar> SteamAvatar(uint64_t id);

void SteamRefreshLobbies();
bool SteamLobbiesLoading();
void SteamRequestLobbyData(uint64_t id);
unsigned SteamLobbyDataSerial(uint64_t id);
uint64_t SteamTakeResync();
std::vector<OnlineLobby> SteamLobbies();

struct OnlineModDownload {
	bool active = false, done = false, failed = false;
	std::string error;
	uint64_t got = 0, total = 0;
	std::map<std::string, std::string> installed;
};
void SteamRequestMods(const std::vector<OnlineMod> &mods);
OnlineModDownload SteamModDownload();
void SteamSetRejoin(uint64_t lobby);
void SteamResetModDownload();

struct OnlineMapStatus {
	int state = 0;
	bool ready = false;
	bool needsPack = false;
	std::string name, error, packName;
	uint64_t got = 0, total = 0;
};
void SteamPublishScenario(const std::string &path);
OnlineMapStatus SteamMapStatus();
void SteamPublishSave(const std::string &file, const std::string &name);
OnlineMapStatus SteamSaveStatus();

void SteamCreateLobby(const OnlineCreateSettings &s);
void SteamJoinLobby(uint64_t id, const std::string &password, bool forContentSync = false);
void SteamLeaveLobby(uint32_t onlyIfMembership = 0);
uint32_t SteamMembership();
OnlineRoom SteamCurrentRoom();
// The chat line for a patron who joined, once their lookup has finished. Dropped if the room has changed since.
void SteamAddPatronJoined(uint64_t room, uint64_t id, const std::string &name);
std::string SteamVersionMismatch(const std::string &roomVersion, const std::string &roomRulesLevel = std::string());
std::vector<std::string> SteamMissingScenario();
std::string SteamScenarioModName();
void SteamReportDownloadProgress(uint64_t got, uint64_t total);
void SteamSendChat(const std::string &text);
void SteamInviteFriends();
void SteamSetHosting(bool hosting);
void SteamSetPlaying(bool playing);
void SteamPublishWatchers(int count);
int SteamRoomWatchers();
void SteamKick(uint64_t member);
struct NetPeerStat {
	uint64_t id = 0;
	std::string name;
	int number = 0;
	bool connected = false;
	bool relay = false;
	int ping = -1;
	float quality = -1;
	float inKBs = 0, outKBs = 0, inPkts = 0, outPkts = 0;
};
std::vector<NetPeerStat> SteamNetStats();
void SteamRequestSeat(int slot);
bool SteamTakeSeatRequest(uint64_t &member, int &slot);
void SteamSendSwap(uint64_t to, const std::string &text);
void SteamSetMemberLimit(int n);
bool SteamTakeSwap(uint64_t &from, std::string &text);
bool SteamTakeKicked();
bool SteamTakeJoinRefused();
bool SteamTakeCreateFailed();
void SteamBroadcastCamera(int player, int x, int y);
void SteamPublishOptions(const std::string &text);
void SteamPublishRules(unsigned rules);
void SteamPublishSlots(const std::string &text);
void SteamPublishSaveColors(const std::string &text);
std::string SteamTakeNotice();
std::string SteamOwnGameplayKey();
RoomContent SteamRoomContent(const OnlineLobby &lobby, const std::string &ownKey, bool strictMap = false, bool watching = false);
void SteamLeaveForContent(bool strictMap = false);

void SteamRejoinSend(uint64_t peer, uint8_t type, const std::string &data);
void SteamRejoinAllow(uint64_t peer);
void SteamRejoinJoin(uint64_t lobby);
int SteamRejoinJoinFailed();
void SteamRejoinJoinEnd();
uint64_t SteamRejoinLobby();
int SteamLobbyNumberOf(uint64_t id);

void SteamSetSpectators(bool allow, int delaySeconds);
bool SteamSpectatorsOpen(int &delaySeconds);
long long SteamGameStartedAt();
std::vector<uint64_t> SteamSpectatorIds();
void SteamSpectatorSend(uint64_t peer, uint8_t type, const std::string &data);
void SteamResetStaleSession(uint64_t peer);
void SpectatorOnSteamMessage(uint64_t peer, uint8_t type, const std::string &data);
