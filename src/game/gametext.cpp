// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/gametext.h"

#include <windows.h>

#include <cstring>
#include <string>

#include "addr/engine.h"
#include "addr/state.h"

namespace {

const char *const kRaceEnglish[8] = {"Gaul", "Republican Rome", "Carthage", "Iberia", "Imperial Rome", "Britain", "Egypt", "Germany"};

typedef char *(__cdecl *LocFormat_t)(void *db, char *out, const char *key, ...);

// CLocalizationDB::Format (006897a0) sprintfs the text with no size limit: a buffer far larger than any text, for long translations.
const size_t kLocBuffer = 8192;

bool LocalizeKey(void *db, const char *key, char (&out)[kLocBuffer])
{
	out[0] = 0;
	__try {
		reinterpret_cast<LocFormat_t>(addr::kLocFormat)(db, out, key);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

}

std::string GameText(const std::string &text, const std::string &context)
{
	void *db = *reinterpret_cast<void **>(addr::kUiLocalization);
	if (!db || text.empty() || text.find('%') != std::string::npos) return text;
	char out[kLocBuffer];
	if (!context.empty()) {
		const std::string key = text + "@" + context;
		if (LocalizeKey(db, key.c_str(), out) && out[0] && key != out) return out;
	}
	if (LocalizeKey(db, text.c_str(), out) && out[0]) return out;
	return text;
}

std::string GameRaceText(int race)
{
	return race >= 0 && race < 8 ? GameText(kRaceEnglish[race], "race") : std::string();
}
