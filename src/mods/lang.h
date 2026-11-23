// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <string>
#include <vector>

struct GameLanguage {
	std::string code;
	std::string name;
	std::string from;
};

void InstallLanguage(bool knownExe);

std::vector<GameLanguage> LangAvailable();
std::string LangDisplayName(const std::string &code);
std::string LangNext();
bool LangChoose(const std::string &code);

std::string LangFallbackFor(const std::string &code);
std::string LangFallback();

std::string LangLooseFile(const std::string &rel);
std::vector<std::string> LangPatchFiles();
bool LangMergeLoc(const std::string &base, const std::vector<std::string> &patches, std::string &out);

// The game's texts in the player's language (game.loc.xml with the mods' texts), so that a choice another player sees in their own
// language can travel as its key: "Default@gold" for "Por defecto". Without an entry the key is the text itself (an English game).
void LangRememberGameTexts(const std::string &loc);
std::string LangGameTextKey(const std::string &shown, const char *context);
std::string LangGameTextShown(const std::string &key);
