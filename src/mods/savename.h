// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

struct SaveNameParts {
	std::string name, campaign;
	// The title of titles that the campaign is, or -1 when none of them ends the name.
	int title = -1;
};

// The game names the games of an adventure or a conquest "<name>-<translated title>" (gbr.exe BuildQuickSavePath), and titles have dashes of
// their own ("Imperivm I - La guerra de las Galias"). The longest of titles that ends the name after a dash wins; without one, the
// campaign starts after the last dash that is not one of the spaced dashes of a title. name + "-" + campaign is always the whole name.
SaveNameParts SaveNameSplit(const std::string &baseName, const std::vector<std::string> &titles = {});
