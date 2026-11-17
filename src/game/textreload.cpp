// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstring>

#include "addr/state.h"
#include "addr/textcache.h"
#include "core/crashlog.h"
#include "game/textreload.h"

namespace {
typedef int(__fastcall *SetLanguage_t)(void *self, void *edx, const char *name);

// The number of texts of the table: a std::map whose size is at +0x18 of the object.
unsigned TextCount(const char *settings)
{
	const char *table = *reinterpret_cast<const char *const *>(settings);
	return table ? *reinterpret_cast<const unsigned *>(table + 0x18) : 0;
}
}

bool ReloadGameTexts(unsigned *before, unsigned *after)
{
	bool ok = false;
	*before = *after = 0;
	g_crashLogQuiet++;
	__try {
		char *settings = reinterpret_cast<char *>(addr::kLanguageSettings);
		*before = TextCount(settings);
		// The language name is a VS2003 std::string at +0x20 (characters inline below a capacity of 16, else behind a pointer, length at +0x14, capacity at +0x18
		// of the string). SetLanguage assigns it from the argument, so the argument is a copy.
		const unsigned length = *reinterpret_cast<const unsigned *>(settings + 0x20 + 0x14), capacity = *reinterpret_cast<const unsigned *>(settings + 0x20 + 0x18);
		const char *text = capacity < 16 ? settings + 0x20 + 4 : *reinterpret_cast<const char *const *>(settings + 0x20 + 4);
		char name[64];
		if (text && length > 0 && length < sizeof(name)) {
			memcpy(name, text, length);
			name[length] = 0;
			reinterpret_cast<SetLanguage_t>(addr::kLanguageSetLanguage)(settings, nullptr, name);
			*after = TextCount(settings);
			ok = *reinterpret_cast<char *const *>(settings) != nullptr;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	g_crashLogQuiet--;
	return ok;
}
