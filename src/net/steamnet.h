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
	std::string passwordHash;
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
	// The host gives every player the colour of their team ("tcol").
	bool teamColors = false;
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
	// When the host started the game, in milliseconds since 1970 ("t0ms"): the same for every player, for the league's report.
	long long startedMs = 0;
	// "irm": what a ranked or tournament room is for the league server, "ranked:<id>" or "tournament:<id>".
	std::string leagueTag;
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
	// A system line saying that a patron joined. Join lines also carry the id and name of who joined.
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
	// A ranked or tournament room: its "irm" data for the league ("ranked:<id>" or "tournament:<id>"), and who may watch it: -1 the
	// room's own visibility, 0 everyone, so the game of a private room still shows in the list for spectators.
	std::string leagueTag;
	int specVisibility = -1;
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
bool SteamPasswordMatches(const OnlineLobby &lobby, const std::string &password);
void SteamLeaveLobby(uint32_t onlyIfMembership = 0);
uint32_t SteamMembership();
OnlineRoom SteamCurrentRoom();
// The name of the current Steam room, without copying the whole room; empty outside one.
std::string SteamCurrentRoomName();
// The owner renames its Steam room (lobby data "name", mirrored to the spectator room). False when it owns none.
bool SteamRenameRoom(const std::string &name);
// The chat line for a patron who joined, once their lookup has finished. Dropped if the room has changed since.
void SteamAddPatronJoined(uint64_t room, uint64_t id, const std::string &name);
std::string SteamVersionMismatch(const std::string &roomVersion, const std::string &roomRulesLevel = std::string());
std::vector<std::string> SteamMissingScenario();
std::string SteamScenarioModName();
void SteamReportDownloadProgress(uint64_t got, uint64_t total);
void SteamSendChat(const std::string &text);
void SteamInviteFriends();
// The owner of the room invites one player to it, as the Steam overlay would: a private room is entered by invitation.
void SteamInviteToRoom(uint64_t user);
// In the Steam overlay, or in the Steam client when the overlay is off.
void SteamOpenProfile(uint64_t id);
// The Steam chat with a player, in the overlay or in the Steam client when the overlay is off.
void SteamOpenChat(uint64_t id);
// The player's Steam friends (at most 500), as last read: the first call, and any a minute later, read them again in the background.
std::vector<uint64_t> SteamFriendIds();
// A Steam friend with their name, and whether they are playing this game now.
struct SteamFriendInfo {
	uint64_t id = 0;
	std::string name;
	bool inGame = false;
};
// The same friends with their names, as last read: a call 10 s after the last read reads them again in the background, so a list on screen
// follows who comes into the game.
std::vector<SteamFriendInfo> SteamFriendsInfo();
// A web page in the Steam overlay's browser, on its own over the game until the player closes it; in the default browser when
// the overlay is off or Steam is not running. Only http and https addresses.
void SteamOpenWebPage(const std::string &url);
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
bool SteamTakeWrongPassword();
bool SteamTakeCreateFailed();
void SteamBroadcastCamera(int player, int x, int y);
void SteamPublishOptions(const std::string &text);
void SteamPublishRules(unsigned rules);
void SteamPublishSlots(const std::string &text);
void SteamPublishSaveColors(const std::string &text);
void SteamPublishTeamColors(bool on);
// The host sends the start countdown (ms >= 0) or its cancellation (ms < 0) to the other members of its Steam room. False when
// this PC is not the owner of a Steam room.
bool SteamSendRoomCountdown(unsigned seq, int ms);
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
// The host's start of the game being played, in milliseconds since 1970: this computer's own when it hosts, the room's otherwise.
// 0 until the room says it.
long long SteamGameStartedAtMs();
std::vector<uint64_t> SteamSpectatorIds();
void SteamSpectatorSend(uint64_t peer, uint8_t type, const std::string &data);
void SteamResetStaleSession(uint64_t peer);
void SpectatorOnSteamMessage(uint64_t peer, uint8_t type, const std::string &data);
