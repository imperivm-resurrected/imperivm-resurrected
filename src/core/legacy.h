// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <string>

void LegacyMigrate();
void LegacyMigrateControls(const std::string &path);
bool LegacyLooksLikeControls(const std::string &text);

const char *LegacyJsonKey(const std::string &key);
std::string LegacyDirective(const std::string &word);
unsigned short LegacyKeyVk(const std::string &name);
bool LegacyIsShiftName(const char *name);
int LegacyModKind(const std::string &lowerId);
bool LegacyIsYes(const std::string &lowerValue);
std::string LegacyProfileName(const std::string &name);
