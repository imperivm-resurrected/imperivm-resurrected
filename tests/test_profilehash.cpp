// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "mods/profilehash.h"

namespace {

ModInfo Mod(const std::string &folder, const std::string &id, bool gameplay, const std::string &hash)
{
	ModInfo m;
	m.folder = folder;
	m.id = id;
	m.gameplay = gameplay;
	m.gameplayHash = hash;
	return m;
}

const std::vector<ModInfo> kInstalled = {Mod("Rules", "rules", true, "aaaa"), Mod("Skin", "skin", false, ""), Mod("Other Skin", "other-skin", false, ""),
	Mod("More Rules", "more-rules", true, "bbbb")};

ModProfile Profile(std::vector<std::string> mods)
{
	ModProfile p;
	p.name = "test";
	p.mods = mods;
	return p;
}

void OnlyGameplayCounts()
{
	const std::string rules = ProfileGameplayHash(Profile({"Rules"}), kInstalled);
	CHECK(!rules.empty());
	CHECK_EQ(ProfileGameplayHash(Profile({"Rules", "Skin"}), kInstalled), rules);
	CHECK_EQ(ProfileGameplayHash(Profile({"Skin", "Rules", "Other Skin"}), kInstalled), rules);
	CHECK(ProfileGameplayHash(Profile({"Rules", "More Rules"}), kInstalled) != rules);
	CHECK_EQ(ProfileGameplayHash(Profile({"More Rules", "Rules"}), kInstalled), ProfileGameplayHash(Profile({"Rules", "More Rules"}), kInstalled));
	CHECK(ProfileGameplayHash(Profile({}), kInstalled) != rules);
}

void NotHashedYet()
{
	const std::vector<ModInfo> pending = {Mod("Rules", "rules", true, "")};
	CHECK_EQ(ProfileGameplayHash(Profile({"Rules"}), pending), std::string(""));
}

void ChoicesDoNotChangeIt()
{
	const ModProfile plain = Profile({"Rules", "Skin", "Other Skin"});
	ModProfile chosen = plain;
	chosen.name = "another name";
	chosen.choices.push_back({"other-skin", "skin"});
	chosen.choices.push_back({"skin", "rules"});
	CHECK_EQ(ProfileGameplayHash(chosen, kInstalled), ProfileGameplayHash(plain, kInstalled));
}

void FileChoicesDoNotChangeIt()
{
	const ModProfile plain = Profile({"Rules", "Skin", "More Rules"});
	ModProfile chosen = plain;
	chosen.files.push_back({"gameres/menu.tga", "skin"});
	chosen.files.push_back({"data/units.xml", "more-rules"});
	chosen.files.push_back({"units/legionary/walk.rle", "rules"});
	const std::string hash = ProfileGameplayHash(plain, kInstalled);
	CHECK(!hash.empty());
	CHECK_EQ(ProfileGameplayHash(chosen, kInstalled), hash);
	chosen.choices.push_back({"more-rules", "rules"});
	CHECK_EQ(ProfileGameplayHash(chosen, kInstalled), hash);
}

}

int main()
{
	OnlyGameplayCounts();
	NotHashedYet();
	ChoicesDoNotChangeIt();
	FileChoicesDoNotChangeIt();
	return test::Summary("profilehash");
}
