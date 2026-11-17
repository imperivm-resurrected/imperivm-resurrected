// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The table of texts of the game (see game/textreload.h).
namespace addr {

// (The CLanguageSettings object, kLanguageSettings, is in addr/state.h: its first word is the table of texts, a CLocalizationDB with the loaded flag at +8 and the
// number of texts 24 bytes in.)
// CLanguageSettings::SetLanguage(this, name): keeps the language name (a std::string 32 bytes into the object) and reads CurrentLang/game.loc.xml into the table the object points to
// (clearing it first when it exists).
inline constexpr uintptr_t kLanguageSetLanguage = 0x0067b9b0;

}
