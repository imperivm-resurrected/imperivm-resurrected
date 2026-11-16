// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <string>
#include <vector>

struct ModSpecial {
	std::string id;
	int bit = 36;
	std::vector<std::pair<std::string, std::string>> names, descriptions;
	std::string icon;
	std::string modFolder;
};

void SpecialsRebuild();
std::vector<ModSpecial> SpecialsList();
std::string SpecialDisplayName(const ModSpecial &s);

std::vector<std::string> SpecialsErrorsFor(const std::string &folder);

void SpecialsRegisterConstants(void *engine);

void SpecialsOnClassElement(void *cls, const unsigned *element);

void *SpecialsUnitFromObj(unsigned handle, int offset);
bool SpecialsHas(void *unit, int bit);
void SpecialsSet(void *unit, int bit, bool on);

void InstallSpecials(bool knownExe);
