// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "net/steamnet.h"
#include "ui/uikit.h"

// The Tournaments tab of the multiplayer screen (TorneoLista, TorneoCuadro, TorneoPartida, TorneoCampeon, TorneoCrear) and the rooms of the
// tournament matches. Every action of the tab goes through the screen's one action kTourney, with what to do in its argument. The
// functions marked "locked" run with the screen's lock held; the others take it.

namespace mpui_impl {
struct State;
}

// The argument of kTourney that opens the tab.
constexpr int kTourneyShowArg = 1 << 24;

void PaintTournaments(ui::Painter &p, const mpui_impl::State &st);
// The header of the tab's own screens (a tournament's page, the next match, the champion): false on the list, which keeps the screen's.
bool PaintTourneyHeader(ui::Painter &p, const mpui_impl::State &st);
// Locked.
void TourneyAction(int arg);
// Locked. Esc and Back go from the tab's inner screens to the list; false on the list.
bool TourneyBack();
// Locked. The wheel over a list or the bracket; false when it is over nothing of the tab.
bool TourneyWheel(int delta, bool shift, bool ctrl);
// Locked. The bracket dragged with the mouse, or its bottom bar's thumb: true when a press starts it (on nothing else of the screen).
bool TourneyPanDown(POINT at, const std::vector<ui::Hit> &hits);
void TourneyPanMove(POINT at, WPARAM buttons);
void TourneyPanUp();
// Locked. A scroll bar of the tab dragged or paged.
bool TourneyScrollTo(int bar, int pos);
// The text fields of the tab: the dispute's, the cup's name and description, the invitation code.
bool TourneyOwnsField(int field);
// Locked. The cup's map chooser is open: the scenario search field is its, not the room selector's.
bool TourneyChooserOpen();
// Locked. The text of one of the tab's fields, while it has the focus.
std::string &TourneyFieldText(int field);
// The bytes a field takes as it is typed; TourneyFieldEdited cuts it to its characters.
size_t TourneyFieldBytes(int field);
// Locked. After a key in one of the tab's fields: its length kept, the code in capitals.
void TourneyFieldEdited(int field);
// Locked. Return in one of the tab's fields.
void TourneyFieldEnter(int field);
// The cup's start time field takes the time chosen now, to be typed over.
void TourneyCupTimeFocus();
// Locked. An item of a menu the tab opened (kDropTourney) picked.
void TourneyDropPick(int index, int item);
// Locked. What the tournament fixes in its room (map, options, open seats, the teams of a match of teams) cannot be changed: true, with a
// notice, for such an action.
bool TourneyBlocksAction(int action, int arg);
// The room is a tournament match of teams whose teams the league set: nobody changes them.
bool TourneyTeamsLocked(const mpui_impl::State &st);
// The room of the player's own cup match stays under its match page, which chooses, gets ready and shows the countdown.
bool TourneyRoomHidden(const mpui_impl::State &st);
// The same, for a caller that holds the menu's lock already (the input, the actions).
bool TourneyRoomHiddenHeld(const mpui_impl::State &st);

// The players a cup match's room needs before it may start (2, or 4 in 2v2); 0 for any other room. Takes no lock.
int TourneyRoomPlayers(const mpui_impl::State &st);
// The host of a cup match's room has not put the tournament's options in yet. For a caller that holds the menu's lock.
bool TourneyRoomApplying(const mpui_impl::State &st);
// The room of this player's tournament match: `note` gets the line under its title (how long the rival has to come) and, once the
// rival let the time run out, a button that claims the win is painted left of `right` at `y`. Returns where the header's
// buttons start now.
int PaintTourneyRoomStatus(ui::Painter &p, const mpui_impl::State &st, int right, int y, std::string &note);
// In the room of this player's series, while the map of its next game is being chosen: the choice as a band `w` wide at `y`. Returns the
// height it took with the gap under it; 0 when there is nothing to choose.
int PaintTourneyRoomPick(ui::Painter &p, const mpui_impl::State &st, int x, int y, int w);
// A room of a tournament match, which the lobby lists first with its tag.
bool TourneyLobby(const OnlineLobby &g);
// A room of a tournament match that is not this player's: they can only watch it.
bool TourneyLobbyWatchOnly(const OnlineLobby &g);
// The civilization and colour the player takes into a league match, chosen on a cup's match page or on the panel of a ranked match (one
// choice for both, kept with the game's settings): two menus `w` wide at `y`, kLeagueSidePickerH high. Their menus are the tab's (kTourney).
constexpr int kLeagueSidePickerH = 76;
void PaintLeagueSidePicker(ui::Painter &p, const mpui_impl::State &st, int x, int y, int w);
// The colour's menu of that picker, over everything, while it is open.
void PaintLeagueSideMenu(ui::Painter &p, const mpui_impl::State &st);
// Locked. Closes the colour's menu of the picker: true when it was open.
bool LeagueSideMenuClose();
// Locked. The civilization chosen (a race of the game, the random one included) and the colour: one of the game's twelve, or
// kLeagueOwnColor (core/leagueslot.h) with `rgb`.
void LeagueSideChoice(int &race, int &color, uint32_t &rgb);
// Every frame on the game thread: the notices in any menu, the answers of the server and the room of the player's match.
void TourneyGameThreadTick();
bool CupReturnPending();
