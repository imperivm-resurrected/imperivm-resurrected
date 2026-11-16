// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "mods/folderlist.h"

namespace {

void ParsesAndJoins()
{
	CHECK_EQ(ParseFolderList(""), std::vector<std::string>{});
	CHECK_EQ(ParseFolderList("Caesar Mod"), std::vector<std::string>{"Caesar Mod"});
	CHECK_EQ(ParseFolderList("a||Caesar Mod|"), (std::vector<std::string>{"a", "Caesar Mod"}));
	CHECK_EQ(JoinFolderList({}), std::string());
	CHECK_EQ(JoinFolderList({"a", "Caesar Mod"}), std::string("a|Caesar Mod"));
	CHECK_EQ(ParseFolderList(JoinFolderList({"x", "y z"})), (std::vector<std::string>{"x", "y z"}));
}

void FindsInAnyCase()
{
	const std::vector<std::string> list{"Caesar Mod", "Other"};
	CHECK(HasFolder(list, "Caesar Mod"));
	CHECK(HasFolder(list, "caesar mod"));
	CHECK(HasFolder(list, "OTHER"));
	CHECK(!HasFolder(list, "Caesar"));
	CHECK(!HasFolder({}, "Other"));
}

void RemovesInAnyCase()
{
	std::vector<std::string> list{"Caesar Mod", "Other", "caesar mod"};
	CHECK(RemoveFolderFrom(list, "CAESAR MOD"));
	CHECK_EQ(list, std::vector<std::string>{"Other"});
	CHECK(!RemoveFolderFrom(list, "Missing"));
	CHECK_EQ(list, std::vector<std::string>{"Other"});
}

}

int main()
{
	ParsesAndJoins();
	FindsInAnyCase();
	RemovesInAnyCase();
	return test::Summary("folderlist");
}
