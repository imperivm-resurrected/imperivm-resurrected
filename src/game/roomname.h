// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <string>

// The creation dialog takes up to 200 bytes of UTF-8 for the name of a Steam room.
const size_t kRoomNameMaxBytes = 200;
// A local network game is announced with the char[0x40] name of the CNGameShell description.
const size_t kLanRoomNameMaxBytes = 0x3f;

// The name as the room shows it: valid UTF-8 without control characters (tabs and line breaks become spaces), without
// spaces at the ends and cut at a whole character to fit maxBytes. Empty when nothing is left.
std::string RoomNameClean(const std::string &name, size_t maxBytes);
