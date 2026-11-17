// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "mods/patchxml.h"

enum ClashKind { kClashCosmetic = 0, kClashContent = 1, kClashGameplay = 2 };

struct ClashMod {
	std::string folder;
	std::string id;
	std::vector<std::string> paths;
	std::vector<std::string> loose;
	std::vector<std::string> declares;
	std::vector<PatchedValue> values;
};

struct ClashChoice {
	std::string use;
	std::string over;
};

struct ClashFileChoice {
	std::string path;
	std::string use;
};

struct ClashFile {
	std::string path;
	int kind = kClashCosmetic;
	std::vector<std::string> folders;
	std::vector<bool> loose;
	std::string fallback;
	std::string used;
	bool fileChosen = false;
};

struct ClashPair {
	std::string a;
	std::string b;
	int files = 0;
	int gameplay = 0;
	int content = 0;
	bool declared = false;
	bool problem = false;
	bool choosable = false;
};

struct ValueClash {
	std::string used;
	std::string lost;
	std::vector<PatchedValue> values;
};

struct ClashReport {
	std::vector<ClashFile> files;
	std::vector<ClashPair> pairs;
	std::vector<ValueClash> values;
};

int ClashKindOf(const std::string &path);

ClashReport ClashAnalyze(const std::vector<ClashMod> &ordered, const std::vector<ClashChoice> &choices, const std::vector<ClashFileChoice> &fileChoices = {});
