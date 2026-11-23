// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
// What the movement patch adds to a room: the low word of the game's version hash (the content key has the high word) and a tag in
// the rules level. 1.0.3 and older also turned walking units mid-stride and marked 0xc000 and "/mfs1" (Classic Modern's mark): their
// simulation is not this one, so this mark must differ from theirs or a match of the two would desync.

struct MoveMark {
	unsigned hash;
	const char *rules;
};

inline constexpr unsigned kMoveHashMark = 0xa000;
inline constexpr const char *kMoveRulesTag = "/mfs2";

inline MoveMark MoveMarkFor(bool active) { return active ? MoveMark{kMoveHashMark, kMoveRulesTag} : MoveMark{0, ""}; }
