// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "core/tournament.h"
#include "net/leagueclient_tourney.h"
#include "ui/mpui_internal.h"
#include "ui/mpui_tourney.h"

namespace mpui_tourney_impl {
using namespace mpui_impl;

enum Screen { kScreenList, kScreenPage, kScreenMatch, kScreenChampion, kScreenCreate };

// What an action of the tab does: the high byte of kTourney's argument; the rest is a tournament, a match, an entry or an index.
enum Sub {
	kSubShow = 1, kSubFilter, kSubOpen, kSubPageTab, kSubSignUp, kSubWithdraw, kSubCheckIn, kSubMatch, kSubGo, kSubWatch, kSubNoShow,
	kSubDispute, kSubDisputeSend, kSubDisputeCancel, kSubChampion, kSubPan, kSubZoom,
	// The community cups: the form, the code, and the organizer's page (kSubManage opens it on its tab).
	kSubCreate, kSubCupSet, kSubCupMenu, kSubCupSubmit, kSubCupOpenActive, kSubJoin, kSubJoinSend, kSubJoinCancel, kSubCopyCode, kSubCodeClose,
	kSubManage, kSubCancelCup, kSubCheckinNow, kSubStartNow, kSubRemove, kSubGameA, kSubGameB, kSubReplay, kSubWalkover, kSubWalkoverA,
	kSubWalkoverB, kSubWalkoverNobody, kSubWalkoverCancel,
	// The full form: a section of it brought into view, the map chooser of a round, changing a cup, and its maps tried again.
	kSubCupSection, kSubMapOpen, kSubChooserTab, kSubChooserRow, kSubChooserSize, kSubChooserOk, kSubChooserCancel, kSubEdit,
	kSubMapsRetry,
	// The form's maps of the series: one chosen in the map chooser (its place, or the count to add one) and one taken out.
	kSubPoolOpen, kSubPoolRemove,
	// The map of the next game of the player's series, by its place among the options; the Steam chat with the rival.
	kSubPick, kSubSteamChat,
	// A cup of teams: the dialog of the team's sign-up (its cup, a friend picked by their place in its list, sent, closed), the invitation to
	// a team answered and the captain's team taken back while it waits for the partner (both by their cup).
	kSubTeamOpen, kSubTeamPick, kSubTeamSend, kSubTeamCancel, kSubInviteAccept, kSubInviteDecline, kSubTeamWithdraw,
	// The list's filters, each click the next choice: official or community cups, the format, and only those the player can sign up for.
	kSubListKind, kSubListFormat, kSubListPlayable,
	// The civilization the player takes into the match's room: its menu opened; the colour picked (again: the room's own); ready in the
	// hidden room; the room shown after all.
	kSubCivMenu, kSubColorPick, kSubReady,
	// The colour's menu on the match page: opened (1: the wheel), the wheel's colour taken, closed.
	kSubColorOpen, kSubColorUse, kSubColorClose,
};

// What kSubCupSet changes in the form: the high bits of its value; the low 16 are the choice.
enum CupSetting {
	kCupPlaces = 1, kCupMinimum, kCupBestOf, kCupFinal, kCupSeeding, kCupCheckin, kCupPrivate, kCupPlay, kCupPreset, kCupHideCivs, kCupFog,
	kCupExplored, kCupDiplomacy, kCupSpectators, kCupDay, kCupMinute, kCupNoshow, kCupFormat, kCupSwissRounds, kCupPace, kCupRoundDays, kCupMode, kCupTimeStep,
};

// The menus of the form (kDropTourney), by the index they open with.
enum CupMenu { kCupMenuHour = 0x100, kCupMenuGames, kCupMenuTier, kCupMenuCiv, kCupMenuListKind, kCupMenuListFormat };

inline int CupArg(int setting, int choice)
{
	return (kSubCupSet << 24) | (setting << 16) | (choice & 0xFFFF);
}

// The page's tab of the organizer, after the bracket, the entrants, the rules and the log.
const int kOrganizerTab = 4;

inline int ArgOf(int sub, int64_t value)
{
	return (sub << 24) | static_cast<int>(value & 0xFFFFFF);
}

// The tab's scroll bars, as their hits name them.
enum Bar { kBarList = 0x7100, kBarBracketY, kBarSide, kBarForm, kBarChooser, kBarFriends };

// Where something scrolls, in the canvas' pixels, and how far it can go.
struct ScrollArea {
	RECT r = {};
	bool valid = false;
	int max = 0, maxX = 0;
};

// The bracket as it was drawn: its bottom bar's thumb (screen pixels), the bar's track and thumb, and the bracket's size at 100 % and its
// view's (UI units), for dragging it and zooming it.
const int kZoomMin = 40, kZoomMax = 130;
struct BracketPanGeom {
	RECT thumb = {};
	bool valid = false;
	int track = 0, thumbLen = 0, contentW = 0, contentH = 0, viewW = 0, viewH = 0;
};

// The way to the room of the player's match: claim it, then create the room or join the other's.
struct RoomFlow {
	enum Phase { kIdle, kClaiming, kWaiting, kCreating, kJoining, kInRoom };
	Phase phase = kIdle;
	int64_t match = 0;
	DWORD at = 0;
	uint64_t lobby = 0, registeredLobby = 0, registeredSpec = 0;
	// The room the server sent this player to and that could not be entered, once told to the server as gone.
	uint64_t stale = 0;
	std::string waitingFor;
};

// A room to join or to watch once the lobby list has it.
struct PendingJoin {
	uint64_t lobby = 0;
	bool watch = false;
	DWORD at = 0, refresh = 0;
};

// What the room of a tournament match has had applied: the tournament's preset, rules and map, once per room.
struct RoomLock {
	uint64_t room = 0;
	bool applied = false, applying = false, mapAllowed = false;
	int tries = 0;
	DWORD at = 0;
	std::set<uint64_t> kicked;
	int64_t presenceMatch = 0;
	DWORD presenceAt = 0;
	bool startedSent = false;
	// A scenario being chosen in the room's selector: the item picked and when, and the warning about this computer's copy, once.
	int pick = -1;
	DWORD pickAt = 0;
	// The scenario offered to the game's selector, which holds none until then.
	std::string offered;
	// The civilization of the match page put in the player's slot, and the player made ready, once each; since when the room is shown.
	bool civSet = false, readySent = false;
	DWORD shownAt = 0;
	// What the page chose, put in the slot: when it was last sent and how many times, the own colour last tried, and the player made not
	// ready to change the slot (the game keeps a ready row as it is).
	DWORD setAt = 0;
	int setTries = 0;
	uint32_t ownTried = 0;
	bool unreadied = false;
	// The game's AI put in a test bot's seat.
	bool aiSet = false;
	bool mapWarned = false;
	// This room was the player's match: once the match is settled without a game (a no-show), the room has nothing left to do.
	bool wasMine = false;
	// The map the room was set for: the next game of a series may be on another one, and the room is set again.
	TourneyMap appliedMap;
	// A match of teams: the room's team of this player (1 for side A, 2 for side B; 0 not known yet), once their slot has it, and when it
	// was last asked for.
	int team = 0;
	bool teamSet = false;
	DWORD teamAt = 0;
	// The tries made; after kTeamTries the room is left as it is: a slot whose team the game won't change must not keep its player
	// from getting ready.
	int teamTries = 0;
	// The player was made not ready once to be put in the team.
	bool teamUnready = false;
	// The sides of the claim this player made, which the room's teams follow.
	std::vector<std::vector<uint64_t>> teams;
};

struct Ui {
	int screen = kScreenList;
	// The cup whose game is being played, to come back to its page in the menu when the game ends.
	std::string afterSlug;
	// A game of that cup was played: its end brings the page back.
	bool afterGame = false;
	// The civilization chosen on the match page (a race of the game, -1 for the room's own), kept between matches; the races of its open menu.
	int civRace = -2;
	std::vector<int> civChoices;
	// The colour chosen on the match page, -1 for the room's own.
	int colorPick = -2;
	// When the page's Ready was last pressed: a second press (a double click) before the room has the first is ignored.
	DWORD readyAt = 0;
	// The colour's menu, its wheel, and the player's own colour (0xRRGGBB) when colorPick is kOwnColor.
	bool colorOpen = false, colorWheel = false;
	uint32_t colorRgb = 0;
	int filter = 0;
	// The list's filters: every cup (0), the official (1) or the community's (2); every format (0), single (1) or double elimination (2) or
	// Swiss (3); and only those the player can sign up for now.
	int listKind = 0, listFormat = 0;
	bool listPlayable = false;
	std::string slug;
	int pageTab = 0;
	int listScroll = 0, bracketX = 0, bracketY = 0, sideScroll = 0;
	ScrollArea listArea, bracketArea, sideArea;
	// The bracket's zoom in percent, and its drag: by the bracket itself (0) or by its bottom bar's thumb (1).
	int bracketZoom = 100;
	bool panning = false;
	int panMode = 0, panStartX = 0, panStartY = 0;
	POINT panFrom = {};
	BracketPanGeom pan;
	RoomFlow room;
	PendingJoin join;
	RoomLock lock;
	int64_t disputeMatch = 0;
	std::string disputeText;
	std::string championSlug;
	// A tournament of the player's that ended: its result is shown when the tab opens next.
	std::string pendingChampion;
	// The cup being made, what the server answered against it, and the cup the player already organises when that was the answer.
	TourneyCupForm cup;
	std::string cupError, cupErrorSlug;
	// The cup being changed instead (0 when making one), its slug, and whether only its description can change by now.
	int64_t cupEditId = 0;
	std::string cupEditSlug;
	bool cupLocked = false;
	// The form's scroll, and where its three sections start in it.
	int formScroll = 0;
	int sectionTop[3] = {};
	ScrollArea formArea;
	// The map chooser of a round (-1 closed): its tab (random, the game's, the player's), the random type and size chosen, the scenario
	// chosen by its file, and its list's scroll. With `chooserPool`, chooserRound is a place among the series' maps instead.
	int chooserRound = -1, chooserTab = 0, chooserType = 0, chooserSize = 0, chooserScroll = 0;
	bool chooserPool = false;
	std::string chooserFile;
	ScrollArea chooserArea;
	// The dialog of the invitation code: open, what is typed and why it was not found.
	bool joinOpen = false;
	std::string joinCode, joinError;
	// The start time as it is being typed, "21:30".
	std::string cupTime;
	// The code of the private cup just made, shown once in large.
	std::string shownCode;
	// The match the organizer is giving without playing.
	int64_t walkoverMatch = 0;
	// The dialog of a team's sign-up (0 closed): its cup, the partner chosen among the Steam friends, the team's name, the filter of the
	// friends, their list's scroll and what the league answered against it.
	int64_t teamCup = 0;
	uint64_t teamPartner = 0;
	std::string teamPartnerName, teamName, teamSearch, teamError;
	int teamScroll = 0;
	ScrollArea teamArea;
	// The friends of the list as it was painted, so a click picks the one it was on while the list is read again.
	std::vector<SteamFriendInfo> teamRows;
	// The sides the last claim gave, and its match: the room keeps its teams by them.
	std::vector<std::vector<uint64_t>> claimTeams;
	int64_t claimTeamsMatch = 0;
};

// Under g_lock.
extern Ui g_tourney;

// What a frame painted that the input needs: the scrolling areas.
struct PaintOut {
	ScrollArea listArea, bracketArea, sideArea, formArea, chooserArea, teamArea;
	BracketPanGeom pan;
	std::vector<SteamFriendInfo> teamRows;
	int sectionTop[3] = {};
	bool form = false;
};

// The screens; `u` is a copy of g_tourney taken for the frame.
void PaintListScreen(Painter &p, const State &st, const Ui &u, PaintOut &out);
void PaintPageScreen(Painter &p, const State &st, const Ui &u, PaintOut &out);
void PaintMatchScreen(Painter &p, const State &st, const Ui &u);
void PaintChampionScreen(Painter &p, const State &st, const Ui &u);
void PaintDisputeDialog(Painter &p, const State &st, const Ui &u);
void PaintCreateScreen(Painter &p, const State &st, const Ui &u, PaintOut &out);
void PaintJoinDialog(Painter &p, const State &st, const Ui &u);
void PaintCodeDialog(Painter &p, const State &st, const Ui &u);
void PaintWalkoverDialog(Painter &p, const State &st, const Ui &u);
// What the screens' headers show of the tab: copied for them instead of the whole Ui.
struct HeaderUi {
	int screen = kScreenList, pageTab = 0;
	std::string slug, championSlug;
	int64_t cupEditId = 0;
	bool cupLocked = false;
};
bool PaintPageHeader(Painter &p, const State &st, const HeaderUi &u);
bool PaintMatchHeader(Painter &p, const State &st, const HeaderUi &u);
bool PaintChampionHeader(Painter &p, const State &st, const HeaderUi &u);
bool PaintCreateHeader(Painter &p, const State &st, const HeaderUi &u);

// The double elimination and Swiss cups (mpui_tourney_formats.cpp).
// A match's stage with its part of the tournament: "Quarterfinal", "Winners' final", "Losers' round 3", "Grand final", a Swiss "Round 2".
std::string MatchStageText(const TourneyDetail &d, const TourneyMatch &m);
// "1 point", "3 points".
std::string PointsText(int points);
// Where a player finished: their placement, or in a Swiss tournament that is over their place in the standings; 0 while it is not known.
int EntrantPlace(const TourneyDetail &d, const TourneyEntrant &e);
// A place of a double elimination or of a Swiss tournament, as the player's result: "Finalist", "Losers' round 2 · places 5 to 6",
// "Place 4 of 12".
std::string PlacementText(const TourneyDetail &d, int placement);
// The champion's line under their name: "Final: 2–1 against Escipión", "Grand final: 1–2, and 2–0 in the reset, against Varo",
// "4 points · 4–0 · Buchholz 9".
std::string ChampionLine(const TourneyDetail &d, const TourneyEntrant &champ);
// How long a cup lasted: "3 h 31 min", or "6 d 4 h" over days.
std::string SpanText(int64_t ms);

// The community cups (mpui_tourney_cup.cpp).
// A new form: the player's name for the cup, a map for each round, and the start at the first full hour two hours from now.
TourneyCupForm NewCupForm(const std::string &player, int64_t nowMs);
// The organizer's content profile as the form sends it: none for the base game.
void OwnCupContent(std::string &key, std::string &name);
// A choice of kSubCupSet applied to the form.
void ApplyCupSetting(TourneyCupForm &f, int setting, int choice, int64_t nowMs);
// What the player is told about a problem of the form.
std::string CupProblemText(TourneyCupProblem problem);
// The round a map of the form is for: "Semifinal" in single elimination, "Round 2" otherwise, "Rounds 5 to 6" for the last of a Swiss
// with more rounds than maps.
std::string CupRoundName(const TourneyCupForm &f, int index);
// The organizer plays this match of their cup: the league team settles its disputes, not them.
bool OrganizerPlays(const TourneyDetail &d, const TourneyMatch &m);
// The earliest and latest start the form offers, in this computer's local time (see LocalMs).
void CupStartBounds(int64_t nowMs, int64_t &localMin, int64_t &localMax);
// The civilizations a player can take into a match: the random one, the game's (unless a content pack replaces one) and the packs'. Each is a
// race and its name.
std::vector<std::pair<int, std::string>> MatchCivChoices();
// The civilization chosen for the matches, read once from the room's settings.
int MatchCiv(Ui &u);
int MatchColor(Ui &u);
// colorPick for the player's own colour, from the wheel.
constexpr int kOwnColor = 100;
// The colour chosen as it looks, or CLR_INVALID for the room's own.
COLORREF MatchColorRef(const Ui &u);
// The colour's menu over the match page, under its button.
void PaintMatchColorMenu(Painter &p, const State &st, const Ui &u);
// The start time field: filled with the chosen time when it takes the keys; what is typed moves the start once it reads as a time, and
// on Return (done) the field shows the time the start took.
// "21:30": a start in the computer's time.
std::string CupStartClock(int64_t utcMs);
void CupTimeFocus(Ui &u);
void CupTimeTyped(Ui &u, bool done);
// The menus of the form: their items and the one chosen now, and what picking an item does.
std::vector<std::string> CupMenuItems(const TourneyCupForm &f, int menu, int &selected);
void CupMenuPick(TourneyCupForm &f, int menu, int item, int64_t nowMs);
// What keeps this player out of a cup: the online games they have (-1 when not known yet) and their rank this season.
TourneyUnmet CupUnmet(const TourneyRequirements &r, const TourneyMineView &mine);
// "10 online games · up to Centurion", or "Anyone can sign up".
std::string RequirementsText(const TourneyRequirements &r);
// Why the player cannot sign up, for the button's line: "You need 10 online games; you have 3."
std::string UnmetText(const TourneyRequirements &r, TourneyUnmet unmet, const TourneyMineView &mine);
// "Competitive · civilizations hidden · fog · unexplored map · diplomacy locked · spectators 120 s behind".
std::string RulesText(const TourneySettings &s);
// What the preset puts in the room: "5000 gold · normal population · no bonuses".
std::string PresetSummary(TourneyPreset p);

// The map chooser of a round (mpui_tourney_chooser.cpp).
// Opens it on the round's map.
void OpenMapChooser(Ui &u, int round);
// Opens it on a map of the series (`index`), or on a new one when `index` is past the last.
void OpenPoolChooser(Ui &u, int index);
// A map's picture in a square: the scenario's preview with its starts once this computer has it, or the drawing of a random map's kind of land.
void PaintMapPicture(Painter &p, const TourneyMap &m, int x, int y, int size);
// The map chosen in it, or one not Known() while there is none.
TourneyMap ChooserMap(const Ui &u);
// The sizes a random map type is offered in for the form's mode: those with room for four players in a cup of teams.
std::vector<std::string> CupSizesOf(const TourneyCupForm &f, const std::string &type);
void PaintMapChooser(Painter &p, const State &st, const Ui &u, PaintOut &out);
// A round's map in the form and the Summary: "Random · Mediterranean · medium", "Island War · the game's", "My map · yours".
std::string CupMapLine(const TourneyMap &m);
// The cup's maps on this computer, for its page: whether they are all here, and a line of each.
void PaintCupMapsState(Painter &p, const State &st, const TourneyDetail &d, int x, int y, int w, int &h);
// The cup has scenarios this computer has to have.
bool CupHasScenarios(const TourneySummary &t);
// The rounds' maps and the series'.
std::vector<TourneyMap> CupAllMaps(const TourneySummary &t);
// What is wrong with this computer's copy of a round's scenario, for the warning before the room; empty when nothing is.
std::string CupMapWarning(const TourneyMap &m);

// The room of the player's match (mpui_tourney_room.cpp), on the game thread under g_lock: the host puts the tournament's preset, switches
// and map (the game's, TourneyGameMap) in it, a scenario picked in the room's own selector, until they all hold; true once they do.
bool ApplyTourneyRoom(const TourneyNext &next, RoomLock &k);
// A match of teams: this player's slot to their team (k.team), as the team button would; true once the slot has it. Only a player's own slot
// can change, so each of the four sets theirs.
bool ApplyTourneyTeam(RoomLock &k);

// The cups of teams (mpui_tourney_teams.cpp).
// "Varo and Tulia": a side's players by name.
std::string MembersText(const std::vector<TourneyMember> &members);
// The line under a team's name: its players, or nothing when the name is already theirs.
std::string TeamPlayersLine(const std::string &name, const std::vector<TourneyMember> &members);
// The players' discs side by side, each overlapping the one before by a third: one disc for one player. Returns their width.
int PaintSideDiscs(Painter &p, int x, int y, int size, const std::vector<TourneyMember> &members, COLORREF ring = CLR_INVALID);
int SideDiscsWidth(int size, size_t count);
// "1 player", "8 players"; "1 team", "8 teams" in a cup of teams.
std::string SideCountText(const TourneySummary &t, int n);
// "3 of 8 signed up", with teams in a cup of teams: "3 of 8 teams signed up".
std::string SignedUpText(const TourneySummary &t, int entries, int places);
// A player of the match is in its room: this computer's room when the player is in it, otherwise the league's word for them, otherwise their
// side's.
bool MemberInRoom(const State &st, int64_t match, const TourneyMember &m, bool sideIn);
// The Steam friends the team's dialog lists, by name with those playing the game first; only the ones whose name has `filter`.
std::vector<SteamFriendInfo> TeamFriends(const std::string &filter);
// Opens the dialog of a team's sign-up for a cup.
void OpenTeamDialog(Ui &u, int64_t tournament);
void PaintTeamDialog(Painter &p, const State &st, const Ui &u, PaintOut &out);
// The invitation to a team, over any menu outside a room, with Accept, Decline and Later; a notice instead in a room, where a dialog would be
// in the way, or without `ask`.
void AskInvite(const TourneyNotice &n, bool ask);
// "Varo invites you to their team for Copa X": the invitation's line, with the team's name when it has one.
std::string InviteText(const TourneyInvite &i);
// A team waiting for its partner: "Waiting for your partner", with their name when it is known.
std::string PendingText(const std::string &partner);

// The choice of the next game's map in the player's series (mpui_tourney_match.cpp): the options with their pictures and the time left,
// which the player clicks when it is theirs. Below 170 high the title and the clock go beside the options.
void PaintPickPanel(Painter &p, const State &st, const TourneyNext &n, int x, int y, int w, int h);
// The other side did not come to the room in time: the line that offers the win to the player, or to their team.
std::string NoShowClaimText(const TourneyNext &n);

// Shared drawing.
const int kCardGap = 32;
// A small capitals label on a coloured box (OFFICIAL, LIVE...); returns its width.
int PaintTag(Painter &p, int x, int y, const std::string &text, COLORREF back, COLORREF ink, bool outline = false);
int TagWidth(Painter &p, const std::string &text);
// Small spaced capitals, as the mockups' captions.
void PaintCaps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
// The display face at a size of the mockups, in the painter.
void UseDisplay(Painter &p, int size);
void UseFace(Painter &p, BrandFace face, int size);
// The player's Steam picture in a disc, or their initials; `ring` around it when it is not CLR_INVALID.
void PaintPlayerDisc(Painter &p, int x, int y, int size, uint64_t id, const std::string &name, COLORREF ring = CLR_INVALID);
// A green disc with a tick, or a red one with a cross.
void PaintCheck(Painter &p, int x, int y, int size, bool ok);
// A thin circle, for the rings behind the big screens.
void PaintRing(Painter &p, int cx, int cy, int r, COLORREF c);
void PaintDashedFrame(Painter &p, int x, int y, int w, int h, COLORREF c);
// A name and the badge after it, drawn as given.
// A player's name: a patron's laurel before it (and the name in their tier's colour, as in the rooms), the league badge after it.
// Returns where the name starts.
int PaintName(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, COLORREF ink, int mx, int my);
// A scrolling area as the input finds it: the box painted at (x, y, w, h) and how far it scrolls.
void SetScrollArea(Painter &p, ScrollArea &a, int x, int y, int w, int h, int max);

// Texts.
// "Sun 15 Nov · 18:00", "Today, 18:00", "Tomorrow, 18:00", in this computer's time.
std::string WhenText(int64_t ms);
// "18:00".
std::string HourText(int64_t ms);
// "Sunday 15 November".
std::string DayText(int64_t ms);
// "41 min", "1 h 52 min".
std::string DurationText(int64_t ms);
// "2 d 4 h" from two days on, otherwise as DurationText.
std::string LongWaitText(int64_t ms);
// A moment some days away, after "until" or "from": "until today, 20:00", "until Thursday 20:00", "until Thursday 26, 20:00".
std::string UntilText(int64_t ms);
std::string FromText(int64_t ms);
// "Thu 20:00", "Today 20:00".
std::string ShortWhenText(int64_t ms);
std::string StageText(TourneyStage stage, int round);
// The player's match by its part of the tournament: "Losers' round 2", "Grand final", "Reset match", "Round 3 (Swiss)", or its stage.
std::string BracketStageText(TourneyBracket bracket, TourneyStage stage, int round);
// "Single elimination · 1 vs 1", "Swiss · 2 vs 2".
std::string FormatText(const TourneySummary &t);
// "Live", "By rounds of 2 days".
std::string PaceText(const TourneySummary &t);
// While a cup by rounds is played: "Round 2 of 4 · until Thursday 20:00"; empty otherwise.
std::string RoundText(const TourneySummary &t);
// "By rounds of 2 days", "By rounds of a week".
std::string RoundDaysText(int days);
// "Caesar Mod" or "The mod's rules".
std::string ContentText(const TourneySummary &t);
// "Competitive settings".
std::string PresetText(const TourneySettings &s);
// A round's map, translated: "Mediterranean · small".
std::string MapText(const TourneyMap &m);
// What the server answered, for the player.
std::string ResultText(const TourneyOpResult &r);
// The player's own entry in a tournament, from their tournaments; null when they are not in it.
const TourneyMyEntry *MyEntry(const TourneyMineView &mine, int64_t tournament);
// A match of the tournament by its id; null when it has none.
const TourneyMatch *MatchById(const TourneyDetail &d, int64_t id);
// The player's answer to an invitation to a team of this cup is on its way.
bool InviteBusy(int64_t tournament);
// The official tournament the tab features: the soonest one open, else one being played, else the latest one over.
const TourneySummary *Featured(const TourneyListsView &lists);
int64_t NowMs();
// A time in this computer's local time, as milliseconds counted from 1 January 1970 there (core's calendar), and back.
int64_t LocalMs(int64_t utcMs);
int64_t UtcMs(int64_t localMs);
// "Saturday 21 November".
std::string LongDayText(int64_t ms);
// The player organises this cup.
bool IsOrganizer(const TourneySummary &t);
// "By Furio", "You organise it"; empty for an official tournament.
std::string OrganizerText(const TourneySummary &t);
// "12 min", "3 h", "45 s": how long the server asks to wait.
std::string WaitText(int seconds);
// The secondary button beside a main one: only its outline, as the header's.
void PaintSecondButton(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my);
// The champion's mark in a square of `size`: the gold phalera of an official cup, or a laurel around a 1 for a community cup,
// which awards no prize.
void DrawCupCrown(Painter &p, int x, int y, int size, bool official);
// What became of a player once the cup is over: champion, finalist, semifinals or knocked out; empty while it is played.
const char *FinishedEntrantText(const TourneyDetail &d, const TourneyEntrant &e);
int SecondButtonWidth(Painter &p, const std::string &label);
// A row of choices, the one on filled: the form's segmented controls, with their labels already translated. `args[i]` is each choice's kTourney
// argument.
void PaintSegments(Painter &p, int x, int y, int w, int h, const std::vector<std::string> &labels, int selected, const std::vector<int> &args, int mx, int my);

}

using namespace mpui_tourney_impl;
