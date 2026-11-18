// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

struct ModOption {
	std::string id;
	std::string name;
	std::string description;
	std::vector<std::string> values;
	int def = 0;
	int pos = 0;
	unsigned width = 1;
	unsigned bit = 0;
	std::string mod;
};

const unsigned kModOptionFirstBit = 0x80000;
const int kModOptionBits = 8;
const char kSeasonOptionId[] = "season";
const unsigned kSeasonBits = 3u << 28;

const std::vector<ModOption> &ModOptions();
unsigned ModOptionsMask();
unsigned ModOptionsDefaults();
unsigned ModOptionWidth(size_t values);
bool ModOptionPlace(ModOption &o, int &used);
int ModOptionGet(const ModOption &o, unsigned rules);
unsigned ModOptionSet(const ModOption &o, unsigned rules, int value);
int ModOptionValue(const std::string &id);
unsigned ModRulesActive();
void ModRulesSetActive(unsigned rules);
