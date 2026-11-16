// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <windows.h>

#include <string>

#include "mods/saveside.h"

namespace {

void MovesTheSideFiles(const std::string &root)
{
	const std::string from = root + "\\a.snr", to = root + "\\b.snr";
	test::WriteFile(from + ".bmp", "picture");
	test::WriteFile(from + ".mods.json", "{}");
	test::WriteFile(to + ".stats", "stale");
	CHECK(MoveSaveSideFiles(from, to));
	CHECK_EQ(test::ReadFile(to + ".bmp"), std::string("picture"));
	CHECK_EQ(test::ReadFile(to + ".mods.json"), std::string("{}"));
	CHECK(!test::Exists(from + ".bmp"));
	CHECK(!test::Exists(from + ".mods.json"));
	CHECK(!test::Exists(to + ".stats"));
	CHECK(!test::Exists(to + ".cameras"));
}

void CopiesAFileHeldOpen(const std::string &root)
{
	const std::string from = root + "\\c.snr", to = root + "\\d.snr";
	test::WriteFile(from + ".bmp", "picture");
	test::WriteFile(from + ".mods.json", "{\"session\": \"x\"}");
	HANDLE viewer = CreateFileA((from + ".mods.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	CHECK(viewer != INVALID_HANDLE_VALUE);
	CHECK(MoveSaveSideFiles(from, to));
	CHECK_EQ(test::ReadFile(to + ".mods.json"), std::string("{\"session\": \"x\"}"));
	CHECK_EQ(test::ReadFile(to + ".bmp"), std::string("picture"));
	CloseHandle(viewer);
}

void ReportsAFileThatCannotFollow(const std::string &root)
{
	const std::string from = root + "\\e.snr", to = root + "\\f.snr";
	test::WriteFile(from + ".stats", "kills");
	HANDLE locked = CreateFileA((from + ".stats").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	CHECK(locked != INVALID_HANDLE_VALUE);
	CHECK(!MoveSaveSideFiles(from, to));
	CloseHandle(locked);
	CHECK_EQ(test::ReadFile(from + ".stats"), std::string("kills"));
}

}

int main()
{
	const std::string root = test::TempDir("saveside");
	MovesTheSideFiles(root);
	CopiesAFileHeldOpen(root);
	ReportsAFileThatCannotFollow(root);
	test::RemoveTree(root);
	return test::Summary("saveside");
}
