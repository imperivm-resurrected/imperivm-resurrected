// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/objectivelog.h"

// Keeps the notes the scripts take away (RemoveNote, ClearNotes) and the ones read on the screen, for the match: a new game
// empties them, a change of map in an adventure keeps them, and a save carries them in <save>.objectives.
void InstallObjectives(bool knownExe);
void ObjectivesWriteFile(const std::string &saveFile);

// What the game holds now, in the order it gave it. On the game thread.
std::vector<ObjectiveNote> ObjectivesActive();
ObjectiveLog ObjectivesLog();
// Counts every change of the log, to know when to read it again.
uint32_t ObjectivesLogVersion();
void ObjectivesMarkSeen(const std::string &id);

// The game's notes dialog (F8, CVXUINotes), which stays open under the mod's screen. Null when it is not open. On the game thread.
void *NotesDialogOpen();
bool NotesDialogClose(void *dlg);
// Selects the note in the dialog's list and presses its "View location", which centres the view on it as the game does.
bool NotesDialogShowOnMap(void *dlg, const std::string &id);
