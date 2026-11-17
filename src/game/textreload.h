// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The texts of the game are one table (CLocalizationDB, keyed by the English text and a context), read once from CurrentLang/game.loc.xml when the game starts.
// Everything that shows a text asks the table each time (Translate hands back a copy, not a pointer into it) or when its definition is read (commands, items,
// classes, skills: they are read again by the reload, after the table). So a ruleset's texts only need the table to be read again: the DLL serves the new
// game.loc.xml (the game's own merged with the loc\ files of the mounted folders, and the patches to it) and the game's own SetLanguage clears the table and
// loads the file. Windows that are open keep the texts they were built with until they are built again (the menus: the skin is the player's).
// Reads the table again with the mounted files. before and after are the number of texts. False if the game faulted or left no table.
bool ReloadGameTexts(unsigned *before, unsigned *after);
