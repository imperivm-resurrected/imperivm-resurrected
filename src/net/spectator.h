// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>

void InstallSpectator(bool knownExe);
void SpectatorGameThreadTick();

bool SpectatorWatching();
bool SpectatorViewing();
bool SpectatorCovering();
std::string SpectatorStatusText();
int SpectatorProgress();
bool SpectatorWantsWake();
int SpectatorLagMs();
bool SpectatorHostPaused();
int SpectatorDelaySec();

struct SpectatorState {
	bool paused = false, broadcast = false, atLive = false, busy = false;
	bool collapsed = false;
	bool ended = false;
	bool uiHidden = false;
	int speed = 1;
	unsigned cur = 0, total = 0;
	unsigned target = 0;
	// Following a player's view (a broadcast only): -1 nobody; paused once the spectator moves the camera.
	bool canFollow = false;
	int follow = -1;
	bool followPaused = false;
	std::string followName;
	uint32_t followRgb = 0;
};
bool SpectatorControls(SpectatorState &out);
// 0 pause, 1 the next speed, 2 and 3 ten seconds back and forward, 4 live, 5 the game's interface, 6 the bar, 7 the next player to follow,
// 8 to 11 the speed ×1, ×2, ×4 or ×8.
void SpectatorControl(int id);
void SpectatorBarPos(double &centerX, double &top, int mode);
void SpectatorSeekFraction(double fraction);
bool SpectatorHandleKey(unsigned msg, unsigned wp, long lp);

int SpectatorCount();

// A player's camera (the centre of their view) at a time of the match's clock, from the player's computer: the host keeps them while
// it broadcasts and sends them to the spectators, who can follow a player's view.
void SpectatorNoteCamera(int player, int x, int y, uint32_t clock);
