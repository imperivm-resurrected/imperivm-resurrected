// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

struct ModCreditsGroup {
	std::string title;
	std::vector<std::string> names;
};

// The mod's own credits, with its texts in the player's language.
struct ModCredits {
	std::string name, version, tagline, author;
	// What the mod does, in the two lines the game's credits text shows.
	std::vector<std::string> summary;
	std::vector<ModCreditsGroup> groups;
};

ModCredits ResurrectedCredits();
// The block put before the game's own credits, for its scrolling text: sections closed by lines of "=", CRLF.
std::string ResurrectedCreditsText(const ModCredits &c);
