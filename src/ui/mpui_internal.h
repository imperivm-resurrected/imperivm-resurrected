// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "ui/fullscreen.h"
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
#include "net/leagueclient.h"
#include "net/mpsave.h"
#include "net/steamnet.h"

namespace mpui_impl {
using namespace ui;

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
	kFieldPickSearch, kPickDelete, kPickDeleteYes, kPickDeleteNo, kGResync, kPatronLink, kProfile, kGTab, kFieldRoomName,
	kLeagueLink, kLeagueOpen, kLeagueCancel, kLeagueUnlink,
	kRankedAct,
	kTourney, kFieldTourney, kFieldCupName, kFieldCupDescription, kFieldCupCode,
	kFieldTeamName, kFieldTeamSearch,
	kLinkOfferYes, kLinkOfferNo,
	kFieldRanked,
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
enum View { kViewList, kViewRanked, kViewCreate, kViewTournaments };

enum Field { kNoField, kChatField, kNameField, kPassField, kJoinPassField, kSearchField, kPickSearchField, kRoomNameField, kTourneyField, kCupNameField, kCupDescriptionField, kCupCodeField, kRankedField,
	kTeamNameField, kTeamSearchField };

enum { kPredTeam, kPredRace, kPredAi, kPredReady, kPredOptSel, kPredOptVal, kPredColor };

enum { kDropRace, kDropAi, kDropOption, kDropColor, kDropSlot, kDropSites, kDropModOption, kDropPreset, kDropFilter, kDropPlayers, kDropProfile, kDropVisibility, kDropSaveGame, kDropSpectators, kDropPickSort, kDropBonus, kDropTeam, kDropTourney, kDropRankedSeason };

// The argument of kGDropOpen: the kind in the low bits and the slot or option index above them.
const int kDropKindBits = 5, kDropKindMask = (1 << kDropKindBits) - 1;

inline int DropArg(int kind, int index)
{
	return kind | (index << kDropKindBits);
}

// The ranked tab: what the player asked of its screens, as the argument of kRankedAct (rows of the leaderboard from kRkRow, "Report a
// problem" of the hub's games from kRkDisputeRecent and of the history's from kRkDisputeHistory).
enum RankedArg {
	kRkSearch = 1, kRkCancel, kRkAccept, kRkDecline, kRkBoard, kRkHub, kRkBoardPrev, kRkBoardNext, kRkBoardTop, kRkBoardMe, kRkRoom,
	kRkResultClose, kRkAgain, kRkResultBoard, kRkSkip, kRkSteam, kRkNotice, kRkAbandon, kRkHistory, kRkHistoryPrev, kRkHistoryNext,
	kRkHistoryNewest, kRkDisputeResult, kRkDisputeSend, kRkDisputeCancel, kRkRow = 0x100, kRkDisputeRecent = 0x200, kRkDisputeHistory = 0x300,
	kRkArgEnd = 0x400, kRkContentSwitch, kRkContentClose,
};
// The full leaderboard's filters and the player's card, and the end of season ceremony (mpui_ranked_seasons.cpp), after kRkArgEnd. The
// clicks on the players' badges of the multiplayer screens come from kRkBadge (LeagueBadgeClicksBegin).
enum RankedSeasonArg {
	kRkCeremonyEquip = kRkArgEnd, kRkCeremonyLater, kRkCeremonyBoard, kRkBoardAll, kRkBoardFriends, kRkBoardSearch, kRkBoardClear,
	kRkBadge = kRkArgEnd + 0x100, kRkBadgeEnd = kRkBadge + 0x100,
};
// The 2v2 (mpui_ranked_team.cpp), after the badges: the queue picked, the duo, the invitations got (kRkDuoAccept and kRkDuoDecline plus
// the invitation's place) and the friends to invite (kRkDuoFriend plus the row of the page).
enum RankedTeamArg {
	kRkQueueDuel = kRkBadgeEnd, kRkQueueTeam, kRkDuoInviteOpen, kRkDuoInviteClose, kRkDuoLeave, kRkDuoFriendsPrev, kRkDuoFriendsNext,
	kRkDuoAccept = kRkBadgeEnd + 0x40, kRkDuoDecline = kRkBadgeEnd + 0x80, kRkDuoFriend = kRkBadgeEnd + 0x100, kRkTeamArgEnd = kRkBadgeEnd + 0x200,
};
enum RankedSub { kRankedHub, kRankedBoard, kRankedHistory };
// The rows of a page of the full leaderboard at most; RankedBoardRows says how many fit the screen.
const int kRankedBoardRows = 13;

struct RankedScreen {
	int sub = kRankedHub;
	int boardSel = -1;
	// The result screen is open while resultOpen; resultAt is when it opened, for the animation.
	bool resultOpen = false;
	int64_t resultMatch = 0;
	DWORD resultAt = 0;
	// The result's sounds already played (kLeagueCue*); skipping the animation silences the rest.
	int resultCues = 0;
	// The history's pages already passed, newest first: the `before` of each, to go back.
	std::vector<int64_t> historyBack;
	// "Report a problem": the match it is about (0: the dialog is closed), what the player wrote, and the last answer already shown.
	int64_t disputeMatch = 0;
	std::string disputeText;
	unsigned disputeSeen = 0;
	// The season needs other gameplay content: its dialog, the content's name, the profile that has it (empty when none does) and
	// whether switching to it needs no restart.
	bool contentOpen = false, contentSwitchable = false;
	std::string contentName, contentProfile;
	unsigned contentSeen = 0;
	// The full leaderboard's filters: the season (-1: the one being played), everyone or the Steam friends, and the name looked for, typed
	// in the ranked field while no report is being written (asked once it rests); the player whose card is open, and one whose row a
	// click on their badge asked for, until their card says where it is.
	int boardSeason = -1;
	bool boardFriends = false;
	std::string boardSearch, boardAsked, boardPick, boardSeek;
	DWORD boardTypedAt = 0;
	// The end of season ceremony: open, its season and queue (each queue has its own), the tab it opened over, and the ceremonies already
	// shown in this session (season and queue).
	bool ceremonyOpen = false;
	int ceremonySeason = -1, ceremonyBack = 0;
	std::string ceremonyQueue = "1v1";
	std::vector<std::string> ceremonyShown;
	// The 2v2's dialog to invite a Steam friend to the duo: open, its page, and the name looked for (typed in the ranked field while it is
	// open).
	bool inviteOpen = false;
	int invitePage = 0;
	std::string inviteSearch;
	LeagueRankedView view;
};

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
	COLORREF color = pal::text;
};

struct ProfileSummary {
	std::vector<ModRow> rows;
};

extern ListGeom g_createModsGeomOut;
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
	DWORD leagueTick = 0;
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
	// The size of the frame being painted, in the units of ScreenView: dialogs and menus over it are placed in it.
	int viewW = 1920, viewH = 1080;
	int dropKind = 0, dropIndex = 0, dropScroll = 0;
	ScrollDrag dropBar;
	int roomTab = 0;
	std::string roomNameInput;
	RankedScreen ranked;
	// The offer to link the account over Ranked or Tournaments: open now, and already made in this session.
	bool linkOffer = false, linkOffered = false;
	// The items of a menu the Tournaments tab opened (kDropTourney) and the one chosen now.
	std::vector<std::string> tourneyDropItems;
	int tourneyDropSel = -1;
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
std::string MapTypeLabel(const std::string &item);
std::string LobbyMapLabel(const OnlineLobby &g);
uint64_t LobbyPlayerId(const OnlineLobby &g, const std::string &name);
std::vector<ListLine> OnlineLobbyLines(const State &st, const OnlineLobby &g, bool withStatus);
std::vector<OnlineMod> MissingMods(const State &st, const OnlineLobby &g);
extern std::string g_syncResult;
void SyncTick();
uint64_t UnixNow();
void RecordOnlineGame(const OnlineRoom &room);
// What the league's report needs of the room of the online game being played.
LeagueMatchInfo LeagueInfoOfRoom(const OnlineRoom &room);
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

const COLORREF kSpecial = pal::text;

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
// The argument of kProfile says where the player was clicked.
const int kProfileLobbySlot = 0, kProfileLobbyName = 0x1000, kProfileRoomSlot = 0x2000;
void ProfileHotspot(ui::Painter &p, const State &st, int x, int y, int w, int h, const std::string &name, int arg);
// The lobby's table of rooms is this wide in a frame of viewW x viewH; the widths of its columns are kept in units of a table
// kColumnBaseW wide, so the ones the player dragged follow the screen.
int LobbyTableWidth(int viewW, int viewH);
const int kColumnBaseW = 600;
void PaintHeader(Painter &p, const State &st);
void PaintPassPrompt(Painter &p, const State &st);
void PaintList(Painter &p, const State &st);
void PaintRanked(Painter &p, const State &st);
void PaintCreate(Painter &p, const State &st);
// The first visit to Ranked or Tournaments of a player who is not linked offers to link the account (see LeagueOfferLinkNow).
void OfferLink();
// The offer is on screen: over Ranked or Tournaments, outside a room, while the account is still not linked.
bool LinkOfferShown(const State &st);
void PaintLinkOffer(Painter &p, const State &st);
std::map<std::string, std::string> PublishedOptions(const State &st);
std::string RoomOptionText(const State &st, int id, const char *published);
std::string RoomMapLabel(const State &st);
void PaintGameTab(Painter &p, const State &st, int x, int y, int w, int h);
void PaintMapThumb(Painter &p, const State &st, int x, int y, int size, bool markers);
std::string RoomNotice(const State &st, COLORREF &color);

const int kRoomTeams = 4;

// What the room screen asks of the room that the game does not do yet; mpui_room_link.cpp keeps them together.
void SetTeamTo(int slot, int team);
int CountdownLeftMs();
void RenameRoom(const std::string &name);
void StartRoomCountdown();
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
// Locked. After OpenMpMenu from the main menu: the section its Multiplayer submenu asked for (MpUiOpenSection), or the rooms.
void ApplyOpenSection();

// The ranked tab (mpui_ranked.cpp, mpui_paint_ranked.cpp, mpui_paint_result.cpp).
void RankedAction(int arg);
// Under g_lock at the start of every frame of the screen: reads the queue and opens the result when one comes.
void RankedFrame();
// Every frame on the game thread, under g_lock, open screen or not: calls the player when a match is found, makes or joins the room of a
// ranked match, fixes its options and keeps strangers out.
void RankedGameThreadTick();
// The room is the Steam room of a ranked match: its options, slots and seats are the server's.
// The player left the room of their ranked match by hand: it is not made or joined again by itself.
void RankedRoomLeftByUser();
bool RankedRoomLocked(const State &st);
bool RankedRoomAction(int action, int arg);
// The hub (linked account) and the full leaderboard; false when the account is not linked and the tab shows the link card.
bool PaintRankedTab(Painter &p, const State &st);
// The pill of the search in the header, between `left` and `right`: shorter where the room is short.
void PaintRankedPill(Painter &p, const State &st, int right, int left);
// "Report a problem" with a ranked game: the dialog over the tab, its text field, and sending it.
void PaintRankedDispute(Painter &p, const State &st);
// The season is played with other content: switching to the profile that has it, or which one to activate.
void PaintRankedContent(Painter &p, const State &st);
std::string &RankedFieldText();
void RankedFieldEnter();
// The result screen over the whole lobby, with its own header.
void PaintRankedResult(Painter &p, const State &st);
// The full leaderboard's filters, the card of the player picked and the season's ceremony (mpui_ranked_seasons.cpp): the arguments from
// kRkArgEnd, the page with the filters, the season's menu, and the ceremony, which opens once over Multiplayer after a season closes.
void RankedSeasonAction(int arg);
void RankedBoardAsk(int offset, bool aroundMe);
int RankedBoardRowsFor(int viewW, int viewH);
int RankedBoardRows();
std::vector<std::string> RankedSeasonItems(const State &st, int &sel);
void RankedSeasonPick(int index);
void RankedSeasonsFrame();
void PaintCeremony(Painter &p, const State &st);
// A small mark for the header of a ranked room; returns how wide it came out.
int PaintRankedMark(Painter &p, const State &st, int x, int y, int h);
// The 2v2 (mpui_ranked_team.cpp): the queue's name ("1 vs 1", "2 vs 2", translated), the duo's panel of the hub (as tall as
// RankedDuoHeight says, no more than `maxH`), its dialog to invite a friend, what their buttons do, and the duo's notices in any menu.
std::string RankedQueueLabel(const std::string &queue);
int RankedDuoHeight(int maxH);
void PaintRankedDuo(Painter &p, const State &st, int x, int y, int w, int h);
void PaintRankedInvite(Painter &p, const State &st);
void RankedTeamAction(int arg);
void RankedTeamGameThreadTick();
// The match found of a 2v2: both teams with their four players, who accepted, and how even it is.
void PaintSearchTeam(Painter &p, const State &st);
// The line under "Match found": the queue, the season and the content it is played with, translated.
std::string RankedFoundCaption(const LeagueRankedView &v, const char *queue);
// A ranked 2v2's room starts only with its four players, each in the side of the ticket.
StartBlock RankedStartBlocker(const State &st, const GameRoomSnapshot &g);
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
bool MainMenuOnTop();
void WakeGameThread();
