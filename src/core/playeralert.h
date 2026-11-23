// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

// Calling the player to the game, as the big online games do: a horn, the taskbar flashing and the window brought to the front. What is
// decided here is pure; hooks/alerts.h does it.

enum class PlayerAlert {
	// A ranked match was found and waits for an answer.
	MatchFound,
	// The player's cup match is ready: their room waits for them.
	CupMatchReady,
	// The start countdown of a multiplayer room began.
	Countdown,
	// The game of a multiplayer room started.
	GameStarts,
};

// The player's options: the online sounds (Sound, OnlineAlerts), the sound of a match found (League, MatchFoundSound) and the window brought
// to the front (League, AlertFront).
struct PlayerAlertOptions {
	bool sound = true, matchSound = true, front = true;
};

struct PlayerAlertPlan {
	bool sound = false, flash = false, front = false;
};

// What a call does with the game's window in the foreground or not. Only the sound plays in the foreground; the start of a game calls only a
// player who is somewhere else.
PlayerAlertPlan PlayerAlertPlanFor(PlayerAlert kind, bool foreground, const PlayerAlertOptions &o);

// A room's countdown calls the player once, and again only after `quietMs`: a countdown cancelled and started again (a player changed their
// slot) does not sound each time. Another room calls at once.
class PlayerAlertGate {
public:
	bool Allow(uint64_t room, uint32_t now, uint32_t quietMs);

private:
	bool any_ = false;
	uint64_t room_ = 0;
	uint32_t at_ = 0;
};

// The call of a Roman horn (cornu) on its natural notes, mono at `rate` samples a second: G3 and D4 short, then G4 held, about 1.2 s. Brass
// harmonics that brighten as each note speaks, a slow attack with a small scoop up to the pitch, and a slight vibrato on the long note.
std::vector<float> PlayerAlertCornu(unsigned rate);
