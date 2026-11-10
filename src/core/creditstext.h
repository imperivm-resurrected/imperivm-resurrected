// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

// The game's credits file (CurrentLang/credits.txt) read into what the credits screen draws. The file is plain text: sections
// separated by lines of "=", and inside them titles and lists of names separated by blank lines.
enum CreditsKind { kCreditsHeading, kCreditsSubtitle, kCreditsRole, kCreditsNote, kCreditsNames, kCreditsText, kCreditsRule };

struct CreditsItem {
	CreditsKind kind = kCreditsText;
	// One line for each kind but the names, which keep one per name. A title written over several lines is joined into one.
	std::vector<std::string> lines;
};

// A line of the index: where a section, or a long list that deserves its own line, starts.
struct CreditsEntry {
	std::string label;
	size_t item = 0;
};

struct CreditsDoc {
	std::vector<CreditsItem> items;
	std::vector<CreditsEntry> index;
};

// Lists with more names than this are drawn in columns.
constexpr size_t kCreditsColumnsFrom = 12;

// Takes UTF-8 with or without its BOM, with CRLF or LF.
CreditsDoc ParseCredits(const std::string &text);
