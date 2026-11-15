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
	kSubShow = 1, kSubFilter, kSubOpen, kSubBack, kSubPageTab, kSubSignUp, kSubWithdraw, kSubCheckIn, kSubMatch, kSubGo, kSubWatch, kSubNoShow,
	kSubDispute, kSubDisputeSend, kSubDisputeCancel, kSubChampion, kSubField, kSubPan,
	// The community cups: the form, the code, and the organizer's page (kSubManage opens it on its tab).
	kSubCreate, kSubCupSet, kSubCupMenu, kSubCupSubmit, kSubCupOpenActive, kSubJoin, kSubJoinSend, kSubJoinCancel, kSubCopyCode, kSubCodeClose,
	kSubManage, kSubCancelCup, kSubCheckinNow, kSubStartNow, kSubRemove, kSubGameA, kSubGameB, kSubReplay, kSubWalkover, kSubWalkoverA,
	kSubWalkoverB, kSubWalkoverNobody, kSubWalkoverCancel,
	// The full form: a section of it brought into view, the map chooser of a round, changing a cup, and its maps tried again.
	kSubCupSection, kSubMapOpen, kSubChooserTab, kSubChooserRow, kSubChooserType, kSubChooserSize, kSubChooserOk, kSubChooserCancel, kSubEdit,
	kSubMapsRetry,
};

// What kSubCupSet changes in the form: the high bits of its value; the low 16 are the choice.
enum CupSetting {
	kCupPlaces = 1, kCupMinimum, kCupBestOf, kCupFinal, kCupSeeding, kCupCheckin, kCupPrivate, kCupPlay, kCupPreset, kCupHideCivs, kCupFog,
	kCupExplored, kCupDiplomacy, kCupSpectators, kCupDay, kCupMinute, kCupNoshow,
};

// The menus of the form (kDropTourney), by the index they open with.
enum CupMenu { kCupMenuHour = 0x100, kCupMenuGames, kCupMenuTier };

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
enum Bar { kBarList = 0x7100, kBarBracketY, kBarBracketX, kBarSide, kBarForm, kBarChooser };

// Where something scrolls, in the canvas' pixels, and how far it can go.
struct ScrollArea {
	RECT r = {};
	bool valid = false;
	int max = 0, maxX = 0;
};

// The way to the room of the player's match: claim it, then create the room or join the other's.
struct RoomFlow {
	enum Phase { kIdle, kClaiming, kWaiting, kCreating, kJoining, kInRoom };
	Phase phase = kIdle;
	int64_t match = 0;
	DWORD at = 0;
	uint64_t lobby = 0, registeredLobby = 0, registeredSpec = 0;
	uint64_t opponent = 0;
	// The room the server sent this player to and that could not be entered, once told to the server as gone.
	uint64_t stale = 0;
	std::string waitingFor;
	TourneyMap map;
	TourneySettings settings;
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
	bool mapWarned = false;
	// This room was the player's match: once the match is settled without a game (a no-show), the room has nothing left to do.
	bool wasMine = false;
};

struct Ui {
	int screen = kScreenList;
	int filter = 0;
	std::string slug;
	int pageTab = 0;
	int listScroll = 0, bracketX = 0, bracketY = 0, sideScroll = 0;
	ScrollArea listArea, bracketArea, sideArea;
	RoomFlow room;
	PendingJoin join;
	RoomLock lock;
	int64_t disputeMatch = 0;
	std::string disputeText;
	std::string championSlug;
	// A tournament of the player's that ended: its result is shown when the tab opens next.
	std::string pendingChampion;
	DWORD lastNoticeAt = 0;
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
	// chosen by its file, and its list's scroll.
	int chooserRound = -1, chooserTab = 0, chooserType = 0, chooserSize = 0, chooserScroll = 0;
	std::string chooserFile;
	ScrollArea chooserArea;
	// The dialog of the invitation code: open, what is typed and why it was not found.
	bool joinOpen = false;
	std::string joinCode, joinError;
	// The code of the private cup just made, shown once in large.
	std::string shownCode;
	// The match the organizer is giving without playing.
	int64_t walkoverMatch = 0;
};

// Under g_lock.
extern Ui g_tourney;

// What a frame painted that the input needs: the scrolling areas.
struct PaintOut {
	ScrollArea listArea, bracketArea, sideArea, formArea, chooserArea;
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
bool PaintPageHeader(Painter &p, const State &st, const Ui &u);
bool PaintMatchHeader(Painter &p, const State &st, const Ui &u);
bool PaintChampionHeader(Painter &p, const State &st, const Ui &u);
bool PaintCreateHeader(Painter &p, const State &st, const Ui &u);

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
// The organizer plays this match of their cup: the league team settles its disputes, not them.
bool OrganizerPlays(const TourneyDetail &d, const TourneyMatch &m);
// The earliest and latest start the form offers, in this computer's local time (see LocalMs).
void CupStartBounds(int64_t nowMs, int64_t &localMin, int64_t &localMax);
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
// The map chosen in it, or one not Known() while there is none.
TourneyMap ChooserMap(const Ui &u);
void PaintMapChooser(Painter &p, const State &st, const Ui &u, PaintOut &out);
// A round's map in the form and the Summary: "Random · Mediterranean · medium", "Island War · the game's", "My map · yours".
std::string CupMapLine(const TourneyMap &m);
// The cup's maps on this computer, for its page: whether they are all here, and a line of each.
void PaintCupMapsState(Painter &p, const State &st, const TourneyDetail &d, int x, int y, int w, int &h);
// The cup has scenarios this computer has to have.
bool CupHasScenarios(const TourneySummary &t);
// What is wrong with this computer's copy of a round's scenario, for the warning before the room; empty when nothing is.
std::string CupMapWarning(const TourneyMap &m);

// The room of the player's match (mpui_tourney_room.cpp), on the game thread under g_lock: the host puts the tournament's preset, switches
// and map in it, a scenario picked in the room's own selector, until they all hold; true once they do.
bool ApplyTourneyRoom(const TourneyNext &next, RoomLock &k);

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
void PaintName(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, COLORREF ink, int mx, int my);

// Texts.
// "Sun 15 Nov · 18:00", "Today, 18:00", "Tomorrow, 18:00", in this computer's time.
std::string WhenText(int64_t ms);
// "18:00".
std::string HourText(int64_t ms);
// "Sunday 15 November".
std::string DayText(int64_t ms);
// "41 min", "1 h 52 min".
std::string DurationText(int64_t ms);
std::string StageText(TourneyStage stage, int round);
// "Single elimination · 1 vs 1".
std::string FormatText(const TourneySummary &t);
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
