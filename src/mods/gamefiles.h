// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "mods/rootrules.h"

namespace mods_impl {

// The files the game has, read once from the game folder: every name in the indexes of Packs\*.pak and of local\*.pak (the ones under
// currentlang/), and the loose files of the game's Units\ and Currentlang\ folders. The packs of the game do not change, so the answer does not
// depend on the profile, the language or the order the mods are mounted in, and it is the same for the players of one version of the game.
// sdk/tools/lib/assetclass.js (gameFiles) reads the same files, so the validator and the game agree.
const GameFileSet &GameFiles();

}
