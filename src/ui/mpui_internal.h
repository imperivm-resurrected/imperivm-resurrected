// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "ui/fullscreen.h"
#include "ui/scrollpane.h"
#include "ui/widgets.h"
#include "core/transferrate.h"
#include "mods/downloads.h"
#include <functional>
#include <set>
#include <array>
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
#include "game/cupmaps.h"
#include "game/mapinfo.h"
#include "game/mappreview.h"
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
	kNone, kRanked, kCreate, kBack, kSelect, kJoin, kChatFocus, kChatSend, kFieldName, kFieldPass,
	kCreateOk, kListView, kRefresh, kFieldJoinPass, kInvite, kChatBottom, kLeave, kHostGame, kEnterGame,
	kGDropOpen, kGDropPick, kGDropClose, kGTeam, kGReady, kGStart, kGCancel,
	kGOptToggle, kGColor, kGSlotType, kSort, kColResize, kFieldSearch, kSyncAccept, kSyncUpdate, kSyncCancel,
	kPickRow, kPickOk, kPickCancel,
	kRecoverYes, kRecoverNo, kRejoinCancel, kRejoinClose,
	kGKick, kGKickJoining, kPassOk, kPassCancel, kGSeat, kGMove, kGMoveTo, kGSwapAsk, kSwapAccept, kSwapDecline, kDropYes, kDropNo, kPassShow, kGSiteToggle, kGRuleToggle, kSpectate, kReturnGame,
	kFieldPickSearch, kPickDelete, kPickDeleteYes, kPickDeleteNo, kGResync, kPatronLink, kProfile, kSteamProfile, kGTab, kFieldRoomName,
	kLeagueLink, kLeagueOpen, kLeagueCancel, kLeagueUnlink,
	kRankedAct,
	kTourney, kFieldTourney, kFieldCupName, kFieldCupDescription, kFieldCupCode,
	kFieldTeamName, kFieldTeamSearch, kFieldCupTime,
	kLinkOfferYes, kLinkOfferNo,
	kFieldRanked,
	// "Team colours" of the room, turned on or off by the host.
	kGTeamColors,
	// The colour menu's own colour: open the wheel, drag on it or on its brightness, take it, go back to the twelve.
	kGColorCustom, kGColorWheel, kGColorValue, kGColorUse, kGColorBack,
	// The list's filter tabs: the filter is the argument.
	kRoomFilter,
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
	kTeamNameField, kTeamSearchField, kCupTimeField };

enum { kPredTeam, kPredRace, kPredAi, kPredReady, kPredOptSel, kPredOptVal, kPredColor };

enum { kDropRace, kDropAi, kDropOption, kDropColor, kDropSlot, kDropSites, kDropModOption, kDropPreset, kDropPlayers, kDropProfile, kDropVisibility, kDropSaveGame, kDropSpectators, kDropPickSort, kDropBonus, kDropTeam, kDropTourney, kDropRankedSeason, kDropSpeed };

// The room's game speeds: the percent shown, and the options' slider value the game takes (-1 the normal speed). The game turns the slider
// into a pace with value * 0x8fd / 100 + 700, where 1000 is the normal speed.
struct SpeedChoice {
	int percent, value;
};
const SpeedChoice kSpeedChoices[] = {{100, -1}, {120, 22}, {140, 30}, {160, 39}, {180, 48}, {200, 57}, {250, 78}, {300, 100}};
const int kSpeedChoiceCount = static_cast<int>(sizeof(kSpeedChoices) / sizeof(kSpeedChoices[0]));
// The percent of a game speed as the room has it (-1 the normal speed).
inline int SpeedPercent(int value)
{
	if (value < 1) return 100;
	return ((value * 0x8fd / 100 + 700) / 10 + 2) / 5 * 5;
}

// The argument of kGDropOpen: the kind in the low bits and the slot or option index above them.
const int kDropKindBits = 5, kDropKindMask = (1 << kDropKindBits) - 1;

inline int DropArg(int kind, int index)
{
	return kind | (index << kDropKindBits);
}

// The ranked tab: what the player asked of its screens, as the argument of kRankedAct (rows of the leaderboard from kRkRow, and "Report a
// problem" of the history's games from kRkDisputeHistory).
enum RankedArg {
	kRkSearch = 1, kRkCancel, kRkAccept, kRkDecline, kRkBoard, kRkHub, kRkBoardPrev, kRkBoardNext, kRkBoardMe, kRkRoom,
	kRkResultClose, kRkAgain, kRkResultBoard, kRkSkip, kRkSteam, kRkAbandon, kRkHistory, kRkHistoryPrev, kRkHistoryNext,
	kRkHistoryNewest, kRkDisputeResult, kRkDisputeSend, kRkDisputeCancel, kRkGuide, kRkGuideRanks, kRkGuideRules, kRkHubRecent, kRkHubMastery,
	kRkPrepareMods, kRkContentSwitch, kRkContentClose,
	// The panel of the match being played: ready or not in the room under it, and that room's own screen shown instead.
	kRkReady, kRkShowRoom,
	// The staging server's practice bot against the search.
	kRkPracticeBot,
	// Back to the hub from the panel of a match being played.
	kRkHidePanel,
	// Giving up a 1v1 whose game closed on this player.
	kRkSurrender,
	// The league server asked again now, after it could not be reached.
	kRkReconnect,
	kRkRow = 0x100, kRkDisputeHistory = 0x300,
	kRkArgEnd = 0x400,
};
// The full leaderboard's filters and the player's card, and the end of season ceremony (mpui_ranked_seasons.cpp), after kRkArgEnd. The
// clicks on the players' badges of the multiplayer screens come from kRkBadge (LeagueBadgeClicksBegin).
enum RankedSeasonArg {
	kRkCeremonyEquip = kRkArgEnd, kRkCeremonyLater, kRkCeremonyBoard, kRkBoardAll, kRkBoardFriends, kRkBoardClear,
	kRkBadge = kRkArgEnd + 0x100, kRkBadgeEnd = kRkBadge + 0x100,
};
// The 2v2 (mpui_ranked_team.cpp), after the badges: the queue picked, the duo, the invitations got (kRkDuoAccept and kRkDuoDecline plus
// the invitation's place) and the friends to invite (kRkDuoFriend plus the row of the page).
enum RankedTeamArg {
	kRkQueueDuel = kRkBadgeEnd, kRkQueueTeam, kRkDuoInviteOpen, kRkDuoInviteClose, kRkDuoLeave, kRkDuoFriendsPrev, kRkDuoFriendsNext,
	kRkDuoAccept = kRkBadgeEnd + 0x40, kRkDuoDecline = kRkBadgeEnd + 0x80, kRkDuoFriend = kRkBadgeEnd + 0x100, kRkTeamArgEnd = kRkBadgeEnd + 0x200,
};
// The page of one game of the history: a row opens it (kRkDetailOpen plus the row), back to the history, the number the chart shows.
enum RankedDetailArg { kRkDetailOpen = kRkTeamArgEnd, kRkDetailClose = kRkDetailOpen + 0x40, kRkDetailMetric, kRkDetailArgEnd = kRkDetailMetric + 0x10 };
// A player's public profile: of the player whose card is open, of the player's own row of the summary's leaderboard, of a row of its
// first ten (kRkProfileTop plus the row), and of a badge clicked where badges open the profile (kRkBadgeProfile, LeagueBadgeClicksBegin).
enum RankedProfileArg {
	kRkProfile = kRkDetailArgEnd, kRkProfileMe, kRkProfileTop = kRkDetailArgEnd + 0x10, kRkBadgeProfile = kRkProfileTop + 0x20,
	kRkProfileArgEnd = kRkBadgeProfile + 0x100,
};
enum RankedSub { kRankedHub, kRankedBoard, kRankedHistory, kRankedGuide };
// The rows of a page of the full leaderboard at most; RankedBoardRows says how many fit the screen.
const int kRankedBoardRows = 13;

struct RankedScreen {
	int sub = kRankedHub;
	// "How it works": its tab, the ranks (0) or the rules (1).
	int guideTab = 0;
	// The summary's middle card shows the mastery of the civilizations instead of the recent games.
	bool hubMastery = false;
	// The result screen is open while resultOpen; resultAt is when it opened, for the animation.
	bool resultOpen = false;
	int64_t resultMatch = 0;
	DWORD resultAt = 0;
	// The result's sounds already played (kLeagueCue*); skipping the animation silences the rest.
	int resultCues = 0;
	// The history's pages already passed, newest first: the `before` of each, to go back.
	std::vector<int64_t> historyBack;
	// The page of one game of the history (0: the list), and the number its chart shows (kDetailMetrics).
	int64_t detailMatch = 0;
	int detailMetric = 0;
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
	// The match whose panel the player put away (its room's own screen, or the room left by hand): the panel shows again with Go to the room.
	int64_t panelOff = 0;
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
	// The room selected in the list: `selected` is its row, which follows it when rooms fill, start or go away.
	uint64_t selectedId = 0;
	int focus = kNoField;
	std::string chatInput, nameInput, passInput, joinPassInput, searchInput;
	bool passShow = false;
	ListState opt;
	ListGeom optGeom;
	int createPlayers = 4;
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
	bool found = false;
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
	// The room being made or joined is one of the game's own local network, without Steam: the game's old screen does it behind the
	// mod's, as for a Steam room. The name the host gave it, set once the room is open; when its announcement was last sent.
	bool lan = false;
	std::string lanRoomName;
	DWORD lanAnnouncedAt = 0;
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
	int camX = 0, camY = 0;
	DWORD leagueTick = 0;
	bool closedByUser = false;
	bool startingCover = false;
	RecoverInfo recover;
	bool recoverAsk = false;
	std::vector<MpSavePlayer> savePlayers;
	bool savePlayersRead = false;
	std::string saveMap;
	bool saveMapRandom = false;
	int saveMapStep = 0;
	int saveMapPick = -1;
	int saveMapTries = 0;
	// The saved game's scenario, once offered to the game's selector.
	std::string saveMapOffered;
	DWORD saveMapTick = 0;
	std::string saveMapError;
	std::string saveConfiguredFor = "-";
	int saveColors[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int saveColorTries[8] = {};
	std::string saveColorWho[8];
	// "Team colours": the host's choice, and the colour each slot is being given with the tries made (TeamColorsStep).
	bool teamColors = false;
	int teamColorWant[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	// The colours picked outside the game's twelve, per slot (0xRRGGBB, 0 for none): the host's and an offline room's own table.
	uint32_t customColors[8] = {};
	// The colour menu shows the wheel; the colour on it, and the part being dragged with its hit.
	bool colorWheel = false;
	uint32_t colorTry = 0;
	int colorDrag = 0;
	RECT colorRect = {};
	int teamColorTries[8] = {};
	DWORD teamColorTick = 0;
	// The slots' colours, ready checks and players when the tries were last counted from zero.
	std::string teamColorSeen;
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
	// The parts that scroll when they are taller than their space (ui::ScrollPane), and where they were in the last frame.
	ui::PaneScrolls scrolls;
	std::vector<PaneArea> paneAreas;
	// The arrows moved the selected room: the list scrolls to show it on the next frame.
	bool lobbyReveal = false;
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
// Whether the list shows a room with `filter` (State::filter: 0 all, 1 friends', 6 with open slots, 7 being played) and the search
// typed.
bool RoomShown(const State &st, const OnlineLobby &g, int filter);
// The two halves of RoomShown: the filter, and the search typed.
bool RoomInFilter(const OnlineLobby &g, int filter);
bool RoomMatchesSearch(const State &st, const OnlineLobby &g);
std::vector<int> Visible(const State &st);
// Selects the room on `row` of Visible (kept in the list), and puts its card back at the top when it is another room.
void SelectRoom(int row);
// Finds the selected room's row again after the list changed, or selects another one when it is gone.
void FollowSelectedRoom();
// The list's group of a room, in the order they show: one that can be joined (0), a game being played that can be watched (1), and one
// nobody can come into now (2), full, playing or that kicked this player.
int RoomGroup(const OnlineLobby &g);
const ModInfo *OlderInstalled(const State &st, const OnlineMod &m);
std::string MapTypeLabel(const std::string &item);
// The options the host published, "label: value" by label, with the choices of its lists in this player's language.
std::map<std::string, std::string> ParsePublishedOptions(const std::string &optionsText, std::vector<std::string> *order = nullptr);
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
// A room of the local network: joined through the game's old screen, behind the mod's; and the rooms heard, added to the list.
void LanJoin(const OnlineLobby &g);
void AddLanRooms(std::vector<OnlineLobby> &lobbies);
void TrySpectate(const OnlineLobby &g, const std::string &password);
extern ListGeom g_pickGeomOut;

bool ChatBarDown(POINT pt);
void ChatBarMove(POINT pt);
extern RECT g_optRect;
extern ListGeom g_optGeomOut;
const int kDropRows = 12;
// Every pane of the multiplayer screens, one id each: their scrolls share State::scrolls. Clear of the tournament bars (kBarList and up).
constexpr int kPaneLobbyRooms = ui::kPaneIdBase + 0x20, kPaneRoomTable = ui::kPaneIdBase + 0x21, kPaneLeagueCard = ui::kPaneIdBase + 0x22,
			  kPaneTourneyAccount = ui::kPaneIdBase + 0x30;
constexpr int kPaneGuideKeeps = ui::kPaneIdBase + 0x40, kPaneGuideWay = ui::kPaneIdBase + 0x41, kPaneGuideGlory = ui::kPaneIdBase + 0x42,
			  kPaneGuideFair = ui::kPaneIdBase + 0x43, kPaneGuideSeasons = ui::kPaneIdBase + 0x44;
// Where the lobby's list of rooms was scrolled to in the frame just painted: the arrows' selection moves it.
extern int g_lobbyScrollOut;

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
	// The game's keys of the choices (game.loc.xml), shown in the player's language by PresetChoice.
	const char *gold, *pop;
	bool fog, explored, bonuses;
	// Set when the preset is picked but not compared by MatchPreset: the game disables it in a room without teams, where it stays off.
	bool sharedControl;
};

const Preset kPresets[] = {
	{"Classic", 0, 0, false, "", "", false, false, false, false},
	{"Competitive", 0, kRuleLockDiplomacy, true, "5000@gold", "Normal@population", true, false, false, true},
	{"Various", 0, 0, true, "10000@gold", "High@population", false, true, true, true},
};

// A choice of a preset as the room's list shows it in this player's language.
std::string PresetChoice(const char *key);

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
// A player's name drawn at (x, y) in a box w wide: a click on it opens their Steam profile, and it is underlined under the pointer.
// Only a person's account (not a staging test player's).
void SteamProfileHotspot(ui::Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, int mx, int my);
// The lobby's table of rooms is this wide in a frame of viewW x viewH; the widths of its columns are kept in units of a table
// kColumnBaseW wide, so the ones the player dragged follow the screen.
int LobbyTableWidth(int viewW, int viewH);
const int kColumnBaseW = 600;
// Straight strokes, each {x0, y0, x1, y1} across or down, in a box of w by h at (x, y), smoothed, with square corners.
void PaintSquareStrokes(Painter &p, int x, int y, int w, int h, const std::vector<std::array<float, 4>> &segments, float width, COLORREF c);
// The ranked search's ring: a faint track and an arc turning round it, centred on (cx, cy).
void PaintSearchRing(Painter &p, float cx, float cy, float radius, float width, COLORREF c, DWORD tick);
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
// The scenarios of the room's selector: the mod's own list, read in the background (the game's selector reads them only when one is
// chosen). PickView's indexes and pickSel are positions in it.
std::vector<CupScenario> PickScenarios();

// What the room's selector and the cup's map chooser share (mpui_scenarios.cpp). A seat's colour on the map: the room's player in it, or
// the map's own when it is empty.
typedef std::function<COLORREF(const MapPlayer &)> ScenarioSeat;
constexpr int kScenarioRowH = 56;
std::string ScenarioOriginLine(const CupScenario &s);
std::string ScenarioSizeLine(const std::string &path);
// The picture of a scenario, with the human starts when asked; an empty path draws a random map's dots.
// A map's picture still being read: a shimmering square with the red loading circle in its middle.
void PaintLoadingThumb(Painter &p, int x, int y, int size);
void PaintScenarioThumb(Painter &p, const std::string &path, int x, int y, int size, bool starts, const ScenarioSeat &seat = ScenarioSeat());
// rows (indexes into list) that match the search, in the order chosen: 0 name, 1 players, 2 size.
std::vector<int> ScenarioOrder(const std::vector<CupScenario> &list, const std::vector<int> &rows, const std::string &search, int sort);
// The search field and the order menu over the list; both screens keep them in pickSearch and pickSort.
void PaintScenarioToolbar(Painter &p, const State &st, int x, int y, int w, int h);
void PaintScenarioRow(Painter &p, const CupScenario &s, int x, int y, int w, int h, bool on, bool hot, const std::string &tag, COLORREF tagInk, bool warn,
	COLORREF tagBack = RGB(40, 80, 44));
// A seat's player name after its civilization, empty for none.
typedef std::function<std::string(const MapPlayer &)> ScenarioSeatName;
// Each seat of a scenario with its colour and civilization, in columns, down to bottom; the y under the last line.
int PaintScenarioPlayers(Painter &p, const ::MapPreviewData &d, int x, int y, int w, int bottom, int columns, const ScenarioSeat &seat,
	const ScenarioSeatName &who = ScenarioSeatName());
// The chosen scenario: its picture with the starts, its lines (and the extra ones), each seat's civilization, a note and its own text.
void PaintScenarioCard(Painter &p, const CupScenario &s, int x, int y, int w, int h, const std::vector<std::string> &extra, const std::string &note, COLORREF noteInk,
	const ScenarioSeat &seat);
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
// A ranked game just ended and its result has not opened yet: the main menu stays hidden meanwhile.
bool RankedReturnPending();
// The player left the room of their ranked match by hand: it is not made or joined again by itself.
void RankedRoomLeftByUser();
// The room is the Steam room of a ranked match: its options, slots and seats are the server's.
bool RankedRoomLocked(const State &st);
// Whether the room gives its players the colours of their teams: the host's choice, which the guests read from the room; never in the
// room of a saved game, whose colours are the save's.
bool TeamColorsOn(const State &st, const GameRoomSnapshot &g);
// A slot's own colour (0xRRGGBB), 0 when it has the game's: the host's or an offline room's table, the room's for a guest.
uint32_t CustomColorOf(const State &st, const GameRoomSnapshot &g, int slot);
// What a slot looks like: its own colour, else the game's.
COLORREF SlotColor(const State &st, const GameRoomSnapshot &g, int slot);
// The host takes the guests' colours and publishes them; every PC hands the room's colours to the match that starts next.
void CustomColorsStep(State &st, const GameRoomSnapshot &g);
// Takes the wheel's colour for the slot of the colour menu, when no other player's is too close; false otherwise.
bool UseCustomColor(State &st, int slot, uint32_t rgb);
// A colour of the twelve for a slot: its own colour goes.
void ClearCustomColor(State &st, int slot);
// While it is on, sets this PC's slots to the colours of their teams, one try every half second.
void TeamColorsStep(State &st, const GameRoomSnapshot &g);
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
// "How it works" (mpui_paint_guide.cpp), and its ladder of the ranks, `mine` lit, which the tab also shows before the account is linked.
void PaintRankedGuide(Painter &p, const State &st);
void PaintRankLadder(Painter &p, int x, int y, int w, int h, LeagueTier mine);
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
int RankedDuoHeight(const LeagueDuoView &duo, int maxH);
void PaintRankedDuo(Painter &p, const State &st, const LeagueDuoView &duo, int x, int y, int w, int h, bool inCard = false);
void PaintRankedInvite(Painter &p, const State &st);
void RankedTeamAction(int arg);
void RankedTeamGameThreadTick();
// The match found of a 2v2: both teams with their four players, who accepted, and how even it is.
void PaintSearchTeam(Painter &p, const State &st);
// A ranked room starts only with all the players of the match, and a 2v2's with each in the side of the ticket.
StartBlock RankedStartBlocker(const State &st, const GameRoomSnapshot &g);
// After Accept, the panel of the match found stays up while its room is made or entered and waits under it (as a cup's match page): the
// player chooses their civilization and colour there, is made ready, and sees the start. Off once the game starts, outside the Ranked tab,
// and for a match whose panel the player put away (RankedScreen::panelOff).
bool RankedPanelOpen(const State &st);
// The room of the ranked match is under its panel: its own screen is not shown.
bool RankedRoomHidden(const State &st);
// A ranked room whose match is over or called off: it is only being left, and nothing of it is shown meanwhile.
bool RankedRoomLeaving(const State &st);
// The player pressed Ready (or Not ready) on the room's own screen: the ranked panel keeps it as theirs.
void RankedReadyPressed();
// The bottom of the panel while the match's room is made and played from it: the civilization and colour, Ready, Cancel match and the room's
// own screen; `by` is its top, about 120 high.
void PaintRankedPanelFoot(Painter &p, const State &st, int dx, int dw, int by);
// A player of the match as the room has them: 2 ready, 1 in the room, 0 not in it yet.
int RankedRoomStateOf(const State &st, uint64_t id);
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
