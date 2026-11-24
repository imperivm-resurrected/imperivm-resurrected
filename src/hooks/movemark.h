// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
// What the movement patch adds to a room: the low word of the game's version hash (the content key has the high word) and a tag in
// the rules level. Each simulation of it has had its own mark, and a match of two of them would desync, so this one must differ from
// all of them: 1.0.3 and older turned walking units mid-stride from a hook on every SetGoalPoint and marked 0xc000 and "/mfs1"
// (Classic Modern's mark); the builds after it, without any turn, marked 0xa000 and "/mfs2". This one turns them only inside the
// lockstep move orders and folds each decision into the sync state.

struct MoveMark {
	unsigned hash;
	const char *rules;
};

inline constexpr unsigned kMoveHashMark = 0x6000;
inline constexpr const char *kMoveRulesTag = "/mfs3";

inline MoveMark MoveMarkFor(bool active) { return active ? MoveMark{kMoveHashMark, kMoveRulesTag} : MoveMark{0, ""}; }
