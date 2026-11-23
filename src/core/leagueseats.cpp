// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueseats.h"

#include <algorithm>

#include "core/names.h"

int RoomNumberOfAddress(uint32_t ip)
{
	if ((ip & 0xffffff00u) == 0x7f4d0000u) return static_cast<int>(ip & 0xff);
	if ((ip & 0x00ffffffu) == 0x00004d7fu) return static_cast<int>(ip >> 24);
	return 0;
}

uint64_t PickSeatSteamId(int number, const std::string &name, const std::vector<SeatMember> &members, uint64_t ownId, const std::vector<uint64_t> &taken,
	const std::vector<uint64_t> &allowed)
{
	auto usable = [&](uint64_t id) {
		if (!id || id == ownId || std::find(taken.begin(), taken.end(), id) != taken.end()) return false;
		return allowed.empty() || std::find(allowed.begin(), allowed.end(), id) != allowed.end();
	};
	if (number > 0) {
		for (const SeatMember &m : members) {
			if (m.number == number && usable(m.id)) return m.id;
		}
	}
	if (name.empty()) return 0;
	for (bool whole : {true, false}) {
		uint64_t found = 0;
		int count = 0;
		for (const SeatMember &m : members) {
			if (!usable(m.id) || m.id == found) continue;
			if (whole ? SamePlayerName(m.name, name) : PlayerNameMatches(m.name, name)) {
				found = m.id;
				count++;
			}
		}
		if (count == 1) return found;
		if (count > 1) return 0;
	}
	return 0;
}

bool LeagueReportRetry(int status, bool failed)
{
	if (status >= 200 && status < 300) return failed;
	return status == 0 || status == 401 || status == 408 || status == 429 || status >= 500;
}
