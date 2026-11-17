// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The sounds of the game come from two places. The files (voices, effects, music) are opened through the game's file system each time one plays: DirectMusic
// is given a stream, not a name, so nothing of them is cached, and a ruleset's files are heard as soon as they are mounted. What a name means is not: the
// sound manager, made once when the game starts, reads data/sound entities/*.xml (the voice of every unit, the selection sounds, the ambient layers) into a
// table of entities that PlaySound searches by name. A ruleset that changes one of those files (or patches it) needs the table made again.
// Frees the entities of the table (as the manager's destructor does) and reads data/sound entities again, with the sound code locked. before and after are the
// number of entities; active is false when the game has no sound (Sound=0 or no sound device): there is no table and nothing to do. False if the game faulted.
bool ReloadSoundEntities(unsigned *before, unsigned *after, bool *active);
