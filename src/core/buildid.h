// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <map>
#include <string>
#include <vector>

// The check stops honest mismatches (development or self-compiled builds, forks, stale installs), not a cheater: a modified DLL can
// announce any build it likes. The build of the mod this game runs: the SHA-256 of its own winmm.dll, in lower-case hex, read once from the file the game
// loaded. Players of one multiplayer game must run the same build, and the league accepts only the official ones. Empty when
// the file could not be read.
// BuildPin keeps the file the game loaded open from the start (DllMain), so that an update installed while the game runs, which
// moves it aside and puts the next release at its path, does not change the build this game announces.
void BuildPin();
const std::string &BuildSha256();
// Its first 12 characters, for the log and for a player to compare by eye.
std::string BuildShortId(const std::string &sha256);

enum class BuildMatch { kSame, kMissing, kDifferent };
// Whether another player's build, as it announced it, is this one's. kMissing when it announced none, as a build from before
// the check does; both lower-case hex of 64 characters, anything else counts as missing.
BuildMatch CompareBuilds(const std::string &mine, const std::string &theirs);
bool BuildShaValid(const std::string &sha256);

// What the host does with a member of its room by the build it announced: admits it, waits for its announcement (it arrives a
// moment after the member enters), or refuses it once it shows another build or has not announced one within the grace time.
// A host that could not read its own build judges nobody.
enum class BuildVerdict { kAdmit, kWait, kRefuse };
BuildVerdict JudgeMemberBuild(const std::string &mine, const std::string &theirs, unsigned waitedMs, unsigned graceMs);

// When the host first saw each member of its room, for the wait above. A member who left and came back waits afresh: its
// announcement goes with it when it leaves, and is sent again when it enters.
class ArrivalClock {
public:
	// How long ago `id` was first seen, counting from `now` (GetTickCount) when it is new.
	unsigned Waited(unsigned long long id, unsigned now);
	// Forgets everyone not in `present`.
	void KeepOnly(const std::vector<unsigned long long> &present);
	void Forget(unsigned long long id) { seen_.erase(id); }
	void Clear() { seen_.clear(); }

private:
	std::map<unsigned long long, unsigned> seen_;
};

// A list of Steam ids as the room keeps it in its data (";1;2;"): whether it has one, and the list with one added or taken out.
bool IdListHas(const std::string &list, unsigned long long id);
std::string IdListWith(const std::string &list, unsigned long long id);
std::string IdListWithout(const std::string &list, unsigned long long id);
