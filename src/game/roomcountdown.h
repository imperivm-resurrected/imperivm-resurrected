// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

const int kRoomCountdownMs = 5000;
// How long the room must stay unchanged before a countdown starts again after a change cancelled it.
const int kRoomCountdownSettleMs = 1000;
// A guest stops showing 0 this long after its deadline when the game did not start.
const int kRoomCountdownGraceMs = 3000;

enum class CountdownEvent { None, Start, Cancel, Fire };
enum class CountdownReason { None, NotReady, Blocked, Changed, Host, Remote };

struct CountdownStep {
	CountdownEvent event = CountdownEvent::None;
	CountdownReason reason = CountdownReason::None;
	// With Changed: the index of the first part of the room that changed.
	int part = -1;
};

struct RoomCountdownInput {
	bool allReady = false;
	bool blocked = false;
	// What must not change while counting (slots, teams, civilizations, options...), one value per part.
	std::vector<uint32_t> parts;
};

// The start countdown of the game room. The host decides with HostStep and is the only authority: guests only show what
// the host sent (Remote*) and stop when they themselves stop being ready. Times are milliseconds of one clock
// (GetTickCount), compared with wrap-around.
class RoomCountdown {
public:
	CountdownStep HostStep(const RoomCountdownInput &in, uint32_t now, bool autoStart);
	// The host cancelled by hand: no new countdown until someone stops being ready.
	CountdownStep HostCancel();

	// From the host's message: start a countdown of `ms` (already corrected for the latency) or cancel it.
	bool RemoteStart(uint32_t seq, int ms, uint32_t now);
	bool RemoteCancel(uint32_t seq);
	// A guest's own tick: stops when the local player is no longer ready, or long after a deadline that did not start the game.
	CountdownStep GuestStep(bool localReady, uint32_t now);

	void Reset();
	bool Counting() const { return counting_; }
	uint32_t Seq() const { return seq_; }
	// Milliseconds left, 0 once the deadline passed while still counting, -1 without a countdown.
	int LeftMs(uint32_t now) const;

private:
	bool counting_ = false;
	uint32_t deadline_ = 0;
	uint32_t seq_ = 0;
	std::vector<uint32_t> parts_;
	bool haveParts_ = false;
	uint32_t changedAt_ = 0;
	bool changedSeen_ = false;
	bool held_ = false;
};
