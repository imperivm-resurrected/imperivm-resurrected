// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "mods/campaignmods.h"

namespace {

const std::vector<std::string> kPacks = {"imperivm1-campaign", "imperivm1-campaign-data"};

CampaignModEntry Mod(const std::string &folder, const std::string &id, const std::string &name, bool gameplay, const std::string &version = "1.0")
{
	CampaignModEntry m;
	m.folder = folder;
	m.id = id;
	m.name = name;
	m.version = version;
	m.gameplay = gameplay;
	m.cosmetic = !gameplay;
	return m;
}

std::vector<CampaignModEntry> Installed()
{
	return {
		Mod("Imperivm1Campaign", "imperivm1-campaign", "The Gallic War", false),
		Mod("Imperivm1Data", "imperivm1-campaign-data", "The Gallic War data", true),
		Mod("GreekMod", "greek-mod", "Greek Civilization Mod", true),
		Mod("NoName", "", "", true),
		Mod("HdPortraits", "hd-portraits", "HD portraits", false),
	};
}

void OnlyOtherGameplayModsAreForeign()
{
	const std::vector<std::string> mounted = {"Imperivm1Campaign", "Imperivm1Data", "HdPortraits"};
	CHECK(CampaignForeignMods(kPacks, mounted, Installed()).empty());
	const std::vector<std::string> mixed = {"greekmod", "Imperivm1Data", "HdPortraits", "NoName", "Gone"};
	CHECK_EQ(CampaignForeignMods(kPacks, mixed, Installed()), (std::vector<std::string>{"Greek Civilization Mod", "NoName"}));
}

void PackIdsMatchWithoutCase()
{
	const std::vector<CampaignModEntry> installed = {Mod("Data", "Imperivm1-Campaign-DATA", "Data", true)};
	CHECK(CampaignForeignMods(kPacks, {"Data"}, installed).empty());
}

void TheProfileKeepsThePacksAndTheCosmetics()
{
	const std::vector<std::string> mounted = {"GreekMod", "Imperivm1Data", "HdPortraits", "Imperivm1Campaign"};
	std::vector<std::string> folders;
	CHECK(CampaignProfileFolders(kPacks, mounted, Installed(), folders));
	CHECK_EQ(folders, (std::vector<std::string>{"Imperivm1Campaign", "Imperivm1Data", "HdPortraits"}));
}

void AModWithForbiddenFilesStaysOut()
{
	std::vector<CampaignModEntry> installed = Installed();
	installed[4].cosmetic = false;
	std::vector<std::string> folders;
	CHECK(CampaignProfileFolders(kPacks, {"GreekMod", "HdPortraits"}, installed, folders));
	CHECK_EQ(folders, (std::vector<std::string>{"Imperivm1Campaign", "Imperivm1Data"}));
}

void TheMountedCopyOfAPackWins()
{
	std::vector<CampaignModEntry> installed = Installed();
	installed.push_back(Mod("Imperivm1Data_new", "imperivm1-campaign-data", "The Gallic War data", true, "1.1"));
	std::vector<std::string> folders;
	CHECK(CampaignProfileFolders(kPacks, {"GreekMod", "Imperivm1Data"}, installed, folders));
	CHECK_EQ(folders, (std::vector<std::string>{"Imperivm1Campaign", "Imperivm1Data"}));
	CHECK(CampaignProfileFolders(kPacks, {"GreekMod"}, installed, folders));
	CHECK_EQ(folders, (std::vector<std::string>{"Imperivm1Campaign", "Imperivm1Data_new"}));
}

void AMissingPackGivesNoProfile()
{
	std::vector<CampaignModEntry> installed = Installed();
	installed.erase(installed.begin());
	std::vector<std::string> folders = {"stale"};
	CHECK(!CampaignProfileFolders(kPacks, {"GreekMod", "Imperivm1Data"}, installed, folders));
	CHECK(folders.empty());
}

}

int main()
{
	OnlyOtherGameplayModsAreForeign();
	PackIdsMatchWithoutCase();
	TheProfileKeepsThePacksAndTheCosmetics();
	AModWithForbiddenFilesStaysOut();
	TheMountedCopyOfAPackWins();
	AMissingPackGivesNoProfile();
	return test::Summary("campaignmods");
}
