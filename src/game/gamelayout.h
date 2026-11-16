// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>

// Reads and writes of the game's memory. They only compute addresses and copy plain values, so they can be used inside __try.
template <class T> T &At(const void *base, ptrdiff_t offset)
{
	return *reinterpret_cast<T *>(static_cast<char *>(const_cast<void *>(base)) + offset);
}

template <class T> T Field(const void *base, ptrdiff_t offset)
{
	return *reinterpret_cast<const T *>(static_cast<const char *>(base) + offset);
}

// The layout of the game's objects that several features read.
namespace layout {

// The game's std::string (MSVC 2008): allocator, 16-byte buffer or pointer to the text, size, capacity.
constexpr size_t kStringBuffer = 4, kStringSize = 0x14, kStringCapacity = 0x18;

// CVXGame: its clock in ms, the local player and the 16 players.
constexpr size_t kGameClock = 0x1258, kGameLocalPlayer = 0x12c8, kGamePlayers = 0x12cc, kPlayerSize = 800;
constexpr int kPlayerCount = 16;

// CVXPlayer: its id (its index among the 16), the mask of its enemies, the permissions it grants each id (4 bytes each,
// game/diplomacyrules.h), whether it plays, its race, who controls it, its AI (null for a human), its name (a std::string), its state,
// its 64 shades and its colour (RGB555).
constexpr size_t kPlayerId = 8, kPlayerEnemies = 0x10, kPlayerDiplomacy = 0x24, kPlayerActive = 0x64, kPlayerRace = 0x7c, kPlayerControl = 0x80,
				 kPlayerAi = 0x88, kPlayerName = 0x90, kPlayerState = 0x290, kPlayerShades = 0x294, kPlayerColor = 0x314;
static_assert(kPlayerName + kStringBuffer == 0x94 && kPlayerName + kStringSize == 0xa4 && kPlayerName + kStringCapacity == 0xa8,
	"the player's name is the std::string at +0x90");
// The state is 1 while the player is in the match; a defeat or a surrender changes it.
constexpr int kStatePlaying = 1;

inline const char *PlayerAt(const char *game, int index)
{
	return game + kGamePlayers + static_cast<size_t>(index) * kPlayerSize;
}

inline char *PlayerAt(char *game, int index)
{
	return game + kGamePlayers + static_cast<size_t>(index) * kPlayerSize;
}

inline char *LocalPlayer(const char *game)
{
	return Field<char *>(game, kGameLocalPlayer);
}

// The text of the game's std::string at `s`: in its buffer while the capacity is below 16, else behind its pointer.
inline const char *StringChars(const char *s)
{
	return Field<unsigned>(s, kStringCapacity) < 0x10 ? s + kStringBuffer : Field<const char *>(s, kStringBuffer);
}

inline unsigned StringSize(const char *s)
{
	return Field<unsigned>(s, kStringSize);
}

inline const char *PlayerName(const char *player)
{
	return StringChars(player + kPlayerName);
}

}

// A colour of the game (RGB555) as 0xRRGGBB.
inline uint32_t Rgb555ToRgb(unsigned c)
{
	return ((c >> 10 & 0x1f) * 255 / 31) << 16 | ((c >> 5 & 0x1f) * 255 / 31) << 8 | ((c & 0x1f) * 255 / 31);
}
