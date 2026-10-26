// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Players who support the mod, read from the honour roll of imperivmresurrected.com by SteamID.
// Dev is the team of the mod, shown with its own laurel whether or not they are on the roll.
enum class PatronTier { None, Gold, Silver, Bronze, Top10, Patron, Dev };

struct PatronInfo {
	int place = 0;
	PatronTier tier = PatronTier::None;
};

// Called from UpdateRoom while it holds the room lock: these never take it.
PatronInfo PatronOf(uint64_t id);
// Looks up the ids that are not known yet, or were looked up too long ago, in the background.
void PatronsWatch(const std::vector<uint64_t> &ids);
// For a player who has just joined and whose lookup has not finished: the notice is posted to the room when it does.
void PatronsAnnounceLater(uint64_t room, uint64_t id, const std::string &name);
std::string PatronJoinedNotice(const std::string &name, PatronTier tier);

struct PatronRollEntry {
	int place = 0;
	std::string name;
	double amount = 0;
	bool hideAmount = false;
	PatronTier tier = PatronTier::None;
};

// The honour roll as the website shows it, read in the background at most every five minutes.
void PatronsRollRefresh();
// state: 0 not read yet, 1 read, -1 could not be read.
std::vector<PatronRollEntry> PatronsRoll(int &state);
