// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "ui/widgets.h"
#include "core/transferrate.h"
#include "mods/downloads.h"
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "MinHook.h"
#include "mods/mods.h"
#include "ui/mpui.h"
#include "game/gameroom.h"
#include "net/mpsave.h"
#include "net/steamnet.h"

namespace mpui_impl {
using namespace ui;

const int kW = 1000;

const int kH = 640;

const int kIdMultiplayer = 0x1003;

const int kIdLoadGame = 0x1004;

const int kIdMainMenuMods = 0x1041;

const unsigned kMsgButtonClick = 0x12345604;

typedef unsigned(__fastcall *OnMessage_t)(void *dlg, void *edx, int *msg);

typedef int(__fastcall *GetId_t)(void *ctrl);

extern OnMessage_t g_origMainMenu;
enum Action {
	kNone, kQuick, kRanked, kCreate, kClassic, kBack, kFilter, kSelect, kJoin, kChatFocus, kChatSend, kCancelSearch,
	kAccept, kDecline, kQueue1v1, kQueue2v2, kFieldName, kFieldPass, kPlayersPrev, kPlayersNext, kMapPrev, kMapNext,
	kCreateOk, kListView, kRefresh, kFieldJoinPass, kInvite, kChatBottom, kLeave, kHostGame, kEnterGame,
	kGDropOpen, kGDropPick, kGDropClose, kGTeam, kGReady, kGStart, kGCancel,
	kGOptToggle, kGColor, kGSlotType, kSort, kColResize, kFieldSearch, kSyncAccept, kSyncUpdate, kSyncCancel,
	kPickRow, kPickOk, kPickCancel,
	kRecoverYes, kRecoverNo, kRejoinCancel, kRejoinClose,
	kGKick, kGKickJoining, kDiscord, kPassOk, kPassCancel, kGSeat, kGMove, kGMoveTo, kGSwapAsk, kSwapAccept, kSwapDecline, kDropYes, kDropNo, kPassShow, kGSiteToggle, kGRuleToggle, kSpectate, kReturnGame,
	kFieldPickSearch, kPickDelete, kPickDeleteYes, kPickDeleteNo, kGResync, kCivSetToggle,
};

typedef int(__cdecl *HostGame_t)(int, unsigned, int);

typedef void(__fastcall *ThisOnly_t)(void *self);

typedef void(__fastcall *SetSelectedItem_t)(void *list, void *edx, unsigned index, int notify, int redraw);

typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);

extern ThisOnly_t g_origLobbyUpdate;
struct AutoJoinState {
	bool active = false;
	DWORD start = 0;
	DWORD refresh = 0;
	bool lan = false;
	DWORD selected = 0;
	DWORD clickedAt = 0;
	int clicks = 0;
	unsigned countAtClick = 0;
	char *items = nullptr;
	unsigned itemCount = 0;
};
extern AutoJoinState g_autoJoin;
struct JoinFlowState {
	bool hostPending = false;
	uint64_t arrivingIds[8] = {};
	std::atomic<unsigned> joinCount{0};
	DWORD lastLobbyUpdate = 0;
	bool backToMain = false;
	std::atomic<bool> inJoin{false};
	std::atomic<bool> joinAbort{false};
	bool nameSwapPending = false;
};
extern JoinFlowState g_flow;
enum View { kViewList, kViewRanked, kViewCreate };

enum Field { kNoField, kChatField, kNameField, kPassField, kJoinPassField, kSearchField, kPickSearchField };

enum { kPredTeam, kPredRace, kPredAi, kPredReady, kPredOptSel, kPredOptVal, kPredColor };

enum { kDropRace, kDropAi, kDropOption, kDropColor, kDropSlot, kDropSites, kDropModOption, kDropPreset, kDropFilter, kDropPlayers, kDropProfile, kDropVisibility, kDropSaveGame, kDropSpectators, kDropPickSort, kDropBonus };

struct SaveEntry {
	std::string name, vfsPath, file;
	uint64_t time = 0;
	bool online = false;
};

std::string SaveLabel(const SaveEntry &e);
struct RecoverInfo {
	uint64_t start = 0;
	bool host = false;
	uint64_t hostId = 0;
	std::string hostName, others;
	std::string otherIds;
	int saveIndex = -1;
	uint64_t saveAge = 0;
};

struct ModRow {
	std::string name;
	int kind = 2;
	std::string status;
	COLORREF color = kText;
};

struct ProfileSummary {
	std::vector<ModRow> rows;
};

extern ListGeom g_createModsGeomOut;
extern ListGeom g_createCivGeomOut;
std::vector<ListLine> BuildModLines(const std::vector<ModRow> &rows, const std::string &emptyText);
int PaintLinesInline(Painter &p, const std::vector<ListLine> &lines, int x, int y, int w, int rowH);
struct State {
	bool open = false;
	int view = kViewList;
	int filter = 0;
	int selected = 0;
	int focus = kNoField;
	std::string chatInput, nameInput, passInput, joinPassInput, searchInput;
	bool passShow = false;
	ListState opt;
	ListGeom optGeom;
	int createPlayers = 4, createMap = 0;
	std::vector<OnlineLobby> lobbies;
	ListState detail;
	ListGeom detailGeom;
	bool passPrompt = false;
	OnlineLobby passLobby;
	bool passWatch = false;
	OnlineRoom room;
	bool loading = false;
	bool ready = false;
	std::string status;
	std::vector<ModInfo> mods;
	std::string ownKey;
	bool searching = false;
	std::string searchMode;
	DWORD searchStart = 0;
	bool found = false;
	DWORD foundAt = 0;
	int mouseX = -1, mouseY = -1;
	std::vector<Hit> hits;
	std::string player;
	bool classicPending = false;
	DWORD classicPendingAt = 0;
	DWORD lastRefresh = 0;
	struct JoinCheck {
		uint64_t id = 0;
		std::string password;
		DWORD since = 0;
		unsigned serial = 0;
		bool watch = false;
	} joinCheck;
	DWORD selectedPoll = 0;
	bool connecting = false;
	DWORD connectStart = 0;
	GameRoomSnapshot game;
	bool gameWasActive = false;
	int roomLimit = 0;
	bool readySeen = false;
	std::string readyConfig;
	bool gameWasOnline = false;
	DWORD gameClosedAt = 0;
	struct Predicted {
		int kind;
		int index;
		int value;
		DWORD time;
	};
	std::vector<Predicted> predicted;
	int pressAction = -1, pressArg = 0;
	DWORD pressTime = 0;
	bool mouseDown = false;
	bool dropOpen = false;
	std::string createProfile;
	std::vector<std::string> profiles;
	ProfileSummary createProfileInfo;
	std::vector<std::string> createCivSets;
	bool skirmishCivs = false; // the skirmish room owns CivRoomSets until the multiplayer menu opens
	int createVisibility = 0;
	int createSpec = 1;
	bool syncConfirm = false;
	OnlineLobby syncLobby;
	uint64_t forcePackLobby = 0;
	bool syncWatch = false;
	bool syncLive = false;
	std::string syncPass;
	bool syncUpdate = false;
	std::string syncUsing;
	ListState syncList;
	ListGeom syncGeom;
	ListState createModsList;
	ListGeom createModsGeom;
	ListState createCivList;
	ListGeom createCivGeom;
	int syncPhase = 0;
	DWORD syncStart = 0;
	std::string syncError;
	OnlineModDownload dl;
	uint64_t hashDone = 0, hashTotal = 0;
	int syncStage = 0;
	std::vector<OnlineMod> syncHostList;
	std::map<std::string, std::string> syncInstalled;
	std::set<std::string> syncFromIo;
	bool syncRetryHost = false;
	RoomModStatus io;
	TransferRate rate;
	int pickSel = -1;
	std::string pickSearch;
	int pickSort = 0;
	int pickDelete = -1;
	std::set<std::string> pickGone;
	std::string pickNote;
	DWORD pickNoteUntil = 0;
	bool pickOpened = false;
	DWORD pickLastClick = 0;
	int pickLastIndex = -1;
	ListState pick;
	ListGeom pickGeom;
	ListState chat;
	ListGeom chatGeom;
	EditState edit;
	uint64_t discordJoin = 0;
	DWORD discordJoinAt = 0, discordJoinRefresh = 0;
	int tabAction = -1, tabArg = 0;
	OnlineMapStatus map;
	OnlineMapStatus save;
	std::vector<SaveEntry> saves;
	int createSave = -1;
	SaveEntry hostSave;
	bool hostSavePublished = false;
	bool autoEntered = false;
	uint64_t lastRoomId = 0;
	uint32_t lastRoomGen = 0, closedRoomGen = 0;
	bool autoHost = false;
	bool inOnlineGame = false;
	bool leftByUser = false;
	DWORD camTick = 0;
	bool closedByUser = false;
	bool startingCover = false;
	RecoverInfo recover;
	bool recoverAsk = false;
	uint64_t recoverWaitHost = 0;
	DWORD recoverRefresh = 0;
	std::vector<MpSavePlayer> savePlayers;
	bool savePlayersRead = false;
	std::string saveMap;
	bool saveMapRandom = false;
	int saveMapStep = 0;
	int saveMapPick = -1;
	int saveMapTries = 0;
	DWORD saveMapTick = 0;
	std::string saveMapError;
	std::string saveConfiguredFor = "-";
	int saveColors[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int saveColorTries[8] = {};
	std::string saveColorWho[8];
	int guestColorSlot = -1;
	int guestColorTries = 0;
	DWORD guestColorTick = 0;
	DWORD saveColorTick = 0;
	DWORD saveSwapTick = 0;
	DWORD saveSeatStep = 0;
	std::set<std::string> savePlaced;
	std::map<std::string, std::pair<int, DWORD>> pendingSeat;
	int moveFrom = -1;
	uint64_t swapAskFrom = 0;
	std::map<uint64_t, std::pair<uint64_t, DWORD>> swapIntents;
	std::string swapAskName;
	DWORD swapAskTime = 0;
	bool dropAsk = false;
	std::vector<std::string> dropNames;
	int dropPositions = 0;
	int dropAnswer = -1;
	uint64_t swapPendingTo = 0;
	std::string swapPendingName;
	DWORD swapPendingTime = 0;
	int sortCol = 2, sortDesc = 1;
	int colW[5] = {170, 110, 80, 110, 0};
	int resizeCol = -1, resizeStartX = 0, resizeStartW = 0;
	float uiScale = 1.0f;
	int dropKind = 0, dropIndex = 0, dropScroll = 0;
};

extern std::mutex g_lock;
extern State g_s;
extern std::atomic<int> g_gameModal;
struct ChatPaneState {
	std::mutex mx;
	int scroll = 0;
	int lastTotal = 0;
	int unread = 0;
	uint32_t serial = 0;
	uint64_t room = 0;
	RECT rect = {};
	bool rectValid = false;
	ListGeom geomOut;
};
extern ChatPaneState g_chat;
struct DetailPaneState {
	RECT rect = {};
	bool rectValid = false;
	ListGeom geomOut;
};
extern DetailPaneState g_detail;
extern void *g_mainMenu;
extern Canvas g_canvas;
std::string SpectatorText(bool enabled, int delay);
bool RoomHasNoSeat(const State &st);
std::vector<int> Visible(const State &st);
const ModInfo *OlderInstalled(const State &st, const OnlineMod &m);
std::vector<ListLine> OnlineLobbyLines(const State &st, const OnlineLobby &g, bool withStatus);
std::vector<OnlineMod> MissingMods(const State &st, const OnlineLobby &g);
extern std::string g_syncResult;
void SyncTick();
uint64_t UnixNow();
void RecordOnlineGame(const OnlineRoom &room);
void RecordOnlineGameRaw(const RecoverInfo &r);
void ClearOnlineGame();
void CheckRecover();
bool CanReturnToGame(const State &st, const OnlineLobby &g);
void TryJoinNow(const OnlineLobby &g, const std::string &password, bool watch = false);
void TryJoin(const OnlineLobby &g, const std::string &password, bool watch = false);
void TrySpectate(const OnlineLobby &g, const std::string &password);
extern ListGeom g_pickGeomOut;
const int kPickRows = 13;

bool ChatBarDown(POINT pt);
void ChatBarMove(POINT pt);
extern RECT g_optRect;
extern ListGeom g_optGeomOut;
const int kDropRows = 12;

const COLORREF kSpecial = RGB(255, 196, 84);

struct SiteChoice {
	unsigned bit;
	const char *label;
};

const SiteChoice kSiteChoices[] = {{kSiteRuins, "Ruins"}, {kSiteCaves, "Caves"}, {kSiteItems, "Item holders"},
	{kSiteStrongholds, "Strongholds (large maps)"}};

struct Preset {
	const char *name;
	unsigned sites, rules;
	bool game;
	const char *gold, *pop;
	bool fog, explored, bonuses;
};

const Preset kPresets[] = {
	{"Classic", 0, 0, false, "", "", false, false, false},
	{"Competitive", 0, kRuleLockDiplomacy, true, "5000", "Normal", true, false, false},
	{"Various", 0, 0, true, "10000", "Alta", false, true, true},
};

const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

typedef std::function<std::string(int id, const char *published)> OptionGetter;

int MatchPreset(unsigned sites, unsigned rules, const OptionGetter &get);
OptionGetter HostOptionGetter(const GameRoomSnapshot &g);
struct StartBlock {
	std::string label, toast;
};

StartBlock StartBlocker(const State &st, const GameRoomSnapshot &g);
bool SaveMapPicking(const State &st);
struct SlotChoice {
	std::string label;
	int type;
	int aiItem;
};

std::string SlotLabel(const GameRoomSlot &s);
std::vector<SlotChoice> SlotChoices(const GameRoomSnapshot &g, const GameRoomSlot &s, int &sel);
std::string SavePlayersText(const std::vector<MpSavePlayer> &players);
std::vector<SaveEntry> ListSaves();
ProfileSummary ProfileSummaryFor(const std::string &profile);
void Predict(int kind, int index, int value);
bool SeatTakeable(const GameRoomSlot &s);
bool BonusesDisabled(const GameRoomSnapshot &g);
void ApplySeatDisplay(GameRoomSnapshot &g);
int RaceIdToCombo(int id);
void ApplySaveLook(GameRoomSnapshot &g);
bool RaceEditable(const GameRoomSnapshot &g, const GameRoomSlot &s, bool load);
bool LocalReady(const GameRoomSnapshot &g);
std::string RoomConfigKey(const OnlineLobby &l);
bool SlotFrozen(const GameRoomSnapshot &g, const GameRoomSlot &s);
bool RoomActionFrozen(const GameRoomSnapshot &g, int action, int arg);
void RedirectReplacedRaces(GameRoomSnapshot &g);
void ApplyPredicted(GameRoomSnapshot &g, std::vector<State::Predicted> &list);
typedef unsigned(__fastcall *GameMenuMsg_t)(void *dlg, void *edx, int *msg);

extern GameMenuMsg_t g_origGameMenuMsg;
struct UserQuitState {
	DWORD tick = 0;
	int id = 0;
};
extern UserQuitState g_userQuit;
unsigned __fastcall HookGameMenuMsg(void *dlg, void *edx, int *msg);
void LeaveGameRoom();
bool SlotChangeBlocked(int slot, int newType);
void AbortJoin();
void DoAction(int action, int arg);
void PaintField(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, const std::string &placeholder,
	bool focused, int action, bool secret = false);
bool PaintAvatar(Painter &p, int x, int y, int size, uint64_t id);
std::string SaveSlotNameFromOptions(const std::string &opts, int i);
uint64_t MemberIdByName(const State &st, const std::string &name, bool skipMe = false);
void PaintHeader(Painter &p, const State &st);
void PaintPassPrompt(Painter &p, const State &st);
void PaintList(Painter &p, const State &st);
void PaintRanked(Painter &p, const State &st);
void PaintCreate(Painter &p, const State &st);
void PaintChat(Painter &p, const State &st);
extern ListGeom g_syncGeomOut;
void PaintSyncConfirm(Painter &p, const State &st);
void PaintSyncProgress(Painter &p, const State &st);
void PaintScenarioPicker(Painter &p, const State &st);
void PaintRejoin(Painter &p, const State &st);
void PaintRecover(Painter &p, const State &st);
void PaintSearch(Painter &p, const State &st);
COLORREF PlayerColor(const GameRoomSnapshot &g, int index);
bool RaceSeparator(const std::vector<std::string> &names);
int RaceRealIndex(const std::vector<std::string> &names, int shown);
std::vector<std::string> DropItems(const State &st, int &sel);
int ClampDropScroll(int scroll, int count);
extern RECT g_dropAnchor;
void Combo(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, int kind, int index, bool enabled);
void PaintDropList(Painter &p, const State &st);
void PaintChatLog(Painter &p, const State &st, int x, int y, int w, int h);
void PaintScreen(Painter &p, const State &st);
std::string &FocusedText();
int FieldOfAction(int action);
int ActionOfField(int field);
void FocusField(int field);
void TabStep(bool back);
void FieldKey(UINT msg, WPARAM wp);
void PaintPickCombo(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text);
std::vector<int> PickView(const State &st);
bool PickInUse(const State &st, int index);
void PickMove(WPARAM key);
int PickRowsShown();
void OpenMpMenu(bool checkRecover);
extern std::atomic<bool> g_menuRefresh;
extern std::atomic<HWND> g_wakeWnd;
void RunMenuRefresh(void *dlg);

}

using namespace mpui_impl;

void __fastcall HookLobbyUpdate(void *self);
struct HostAfterGameState {
	std::string savePath;
	DWORD time = 0;
};
extern HostAfterGameState g_hostAfter;
void StepSaveMap(const GameRoomSnapshot &snap);
extern std::vector<uint32_t> g_bg;
void WakeGameThread();
