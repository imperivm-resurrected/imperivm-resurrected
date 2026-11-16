// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "mods/saveside.h"

namespace {

void MovesTheSideFiles(const std::string &root)
{
	const std::string from = root + "\\a.snr", to = root + "\\b.snr";
	test::WriteFile(from + ".bmp", "picture");
	test::WriteFile(from + ".mods.json", "{}");
	test::WriteFile(to + ".stats", "stale");
	MoveSaveSideFiles(from, to);
	CHECK_EQ(test::ReadFile(to + ".bmp"), std::string("picture"));
	CHECK_EQ(test::ReadFile(to + ".mods.json"), std::string("{}"));
	CHECK(!test::Exists(from + ".bmp"));
	CHECK(!test::Exists(from + ".mods.json"));
	CHECK(!test::Exists(to + ".stats"));
	CHECK(!test::Exists(to + ".cameras"));
}

}

int main()
{
	const std::string root = test::TempDir("saveside");
	MovesTheSideFiles(root);
	test::RemoveTree(root);
	return test::Summary("saveside");
}
