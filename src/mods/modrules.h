// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

namespace mods_impl {

const char kGameId[] = "gbr-hd-1.0";

const int kSimLevel = 3;

const int kRulesSchema = 2;

std::string Lower(std::string s);

std::string NormPath(std::string s);

bool StartsWith(const std::string &s, const char *prefix);

bool EndsWith(const std::string &s, const char *suffix);

std::string SafeFileName(const std::string &name);

std::string MakeId(const std::string &folder);

bool AllowedExtension(const std::string &rel);
bool IsDocumentation(const std::string &rel);

bool IsGameplayPath(const std::string &p);

bool IsContentPath(const std::string &p);

struct RulesPart {
	std::string id;
	std::string gameplayHash;
};

std::string RulesFingerprint(std::vector<RulesPart> parts);

}
