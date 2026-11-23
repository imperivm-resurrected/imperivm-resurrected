// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The room number k of a slot's address 127.77.0.k, in either byte order the game keeps it; 0 for any other address.
int RoomNumberOfAddress(uint32_t ip);

struct SeatMember {
	uint64_t id = 0;
	std::string name;
	// Their number in the Steam room (the k of 127.77.0.k); 0 when not known.
	int number = 0;
};

// The SteamID of a seat that is not this computer's: the member with the seat's room number, or else the one member with its name (whole,
// then a prefix of 8 or more letters). Never this computer's id nor one already taken, and only one of `allowed` when it is not empty (the
// ranked ticket's players). 0 when none fits.
uint64_t PickSeatSteamId(int number, const std::string &name, const std::vector<SeatMember> &members, uint64_t ownId, const std::vector<uint64_t> &taken,
	const std::vector<uint64_t> &allowed);

// A game report that did not go through: true to try again later (no answer, an answer cut halfway, a timeout, the server busy or failing,
// the token refused), false to give it up.
bool LeagueReportRetry(int status, bool failed);
