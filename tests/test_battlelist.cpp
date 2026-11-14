// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>
#include <vector>

#include "game/battlelist.h"

namespace {

void Put32(std::vector<unsigned char> &b, size_t at, uint32_t v)
{
	for (int i = 0; i < 4; i++) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
}

std::vector<unsigned char> Bmp(int w, int h, int bpp)
{
	const size_t stride = (static_cast<size_t>(w) * bpp / 8 + 3) & ~static_cast<size_t>(3);
	const size_t paletteSize = bpp == 8 ? 256 * 4 : 0, offset = 54 + paletteSize;
	std::vector<unsigned char> b(offset + stride * (h < 0 ? -h : h), 0);
	b[0] = 'B';
	b[1] = 'M';
	Put32(b, 2, static_cast<uint32_t>(b.size()));
	Put32(b, 10, static_cast<uint32_t>(offset));
	Put32(b, 14, 40);
	Put32(b, 18, static_cast<uint32_t>(w));
	Put32(b, 22, static_cast<uint32_t>(h));
	b[26] = 1;
	b[28] = static_cast<unsigned char>(bpp);
	return b;
}

void BottomUpRowsComeOutTopDown()
{
	std::vector<unsigned char> b = Bmp(2, 2, 24);
	// The first row of the file is the bottom of the picture.
	b[54] = 0x10;
	b[55] = 0x20;
	b[56] = 0x30;
	b[54 + 8 + 3] = 0xff;
	BattleImage img;
	CHECK(DecodeBattleBmp(b.data(), b.size(), img));
	CHECK_EQ(img.w, 2);
	CHECK_EQ(img.h, 2);
	CHECK_EQ(img.bgra[2], 0xff302010u);
	CHECK_EQ(img.bgra[1], 0xff0000ffu);
}

void PalettesAndTopDownFilesWork()
{
	std::vector<unsigned char> b = Bmp(1, -1, 8);
	Put32(b, 54 + 5 * 4, 0x00abcdef);
	b[54 + 1024] = 5;
	BattleImage img;
	CHECK(DecodeBattleBmp(b.data(), b.size(), img));
	CHECK_EQ(img.bgra[0], 0xffabcdefu);
}

void SixteenBitPicturesAreFiveFiveFive()
{
	std::vector<unsigned char> b = Bmp(1, 1, 16);
	b[54] = 0x1f;
	b[55] = 0x7c;
	BattleImage img;
	CHECK(DecodeBattleBmp(b.data(), b.size(), img));
	CHECK_EQ(img.bgra[0], 0xffff00ffu);
}

void BrokenFilesAreRejected()
{
	BattleImage img;
	std::vector<unsigned char> b = Bmp(4, 4, 24);
	CHECK(!DecodeBattleBmp(b.data(), b.size() - 1, img));
	b[0] = 'X';
	CHECK(!DecodeBattleBmp(b.data(), b.size(), img));
	std::vector<unsigned char> rle = Bmp(4, 4, 8);
	Put32(rle, 30, 1);
	CHECK(!DecodeBattleBmp(rle.data(), rle.size(), img));
	CHECK(!DecodeBattleBmp(nullptr, 0, img));
}

void TitlesLoseTheirYear()
{
	BattleTitle t = SplitBattleTitle("El sitio de Alesia - 52 a.C.");
	CHECK_EQ(t.name, std::string("El sitio de Alesia"));
	CHECK_EQ(t.year, std::string("52 a.C."));
	t = SplitBattleTitle("Desembarco en \xc3\x81" "frica \xe2\x80\x93 204 a.C.");
	CHECK_EQ(t.name, std::string("Desembarco en \xc3\x81" "frica"));
	CHECK_EQ(t.year, std::string("204 a.C."));
	t = SplitBattleTitle("Landing in Africa - 204BC");
	CHECK_EQ(t.year, std::string("204BC"));
	t = SplitBattleTitle("The Siege of Alesia");
	CHECK_EQ(t.name, std::string("The Siege of Alesia"));
	CHECK(t.year.empty());
	t = SplitBattleTitle("Rome - the eternal city");
	CHECK(t.year.empty());
	t = SplitBattleTitle(" - 52 BC");
	CHECK(t.year.empty());
}

void IntrosFollowTheBattleFolder()
{
	CHECK_EQ(BattleIntroPattern("adventures/GreatBattles/1_Great_Battles_Zama"), std::string("*_Intro_Great_B_Zama.avi"));
	CHECK_EQ(BattleIntroPattern("adventures\\GreatChallenges\\3_Great_Losses_Egypt.bfhp"), std::string("*_Intro_Great_L_Egypt.avi"));
	CHECK(BattleIntroPattern("adventures/Tutorial").empty());
	CHECK(BattleIntroPattern("conquests/mediterranean").empty());
	CHECK(BattleIntroPattern("Zama").empty());
}

void AdventuresAreSortedByGame()
{
	CHECK_EQ(BattleGameOf("adventures/Imperivm I - La guerra de las Galias"), kBattleGameImperivm1);
	CHECK_EQ(BattleGameOf("adventures\\Imperivm II - Hispania.bfhp"), kBattleGameImperivm2);
	CHECK_EQ(BattleGameOf("Imperivm II - Hispania"), kBattleGameImperivm2);
	CHECK_EQ(BattleGameOf("adventures/imperivm i - galia"), kBattleGameImperivm1);
	CHECK_EQ(BattleGameOf("adventures/Imperivm III - Roma"), kBattleGameOther);
	CHECK_EQ(BattleGameOf("adventures/Imperivm I-Galia"), kBattleGameOther);
	CHECK_EQ(BattleGameOf("adventures/Imperivm IIGalia"), kBattleGameOther);
	CHECK_EQ(BattleGameOf("adventures/Tutorial"), kBattleGameOther);
	CHECK_EQ(BattleGameOf("adventures/Imperivm I - Galia/Tutorial"), kBattleGameOther);
	CHECK_EQ(BattleGameOf(""), kBattleGameOther);
	CHECK_EQ(BattleTitleWithoutGame("Imperivm I - La guerra de las Galias"), std::string("La guerra de las Galias"));
	CHECK_EQ(BattleTitleWithoutGame("Imperivm II - Hispania"), std::string("Hispania"));
	CHECK_EQ(BattleTitleWithoutGame("Imperivm III - Roma"), std::string("Imperivm III - Roma"));
	CHECK_EQ(BattleTitleWithoutGame("Imperivm I - "), std::string("Imperivm I -"));
	CHECK_EQ(BattleTitleWithoutGame("Tutorial"), std::string("Tutorial"));
}

void DescriptionsGetRealLineBreaks()
{
	CHECK_EQ(BattleDescriptionText("Year 204BC\\n\\nEach victory\r\n"), std::string("Year 204BC\n\nEach victory"));
	CHECK_EQ(BattleDescriptionText("a\\"), std::string("a\\"));
}

void SavesBelongToTheLongestTitleAfterADash()
{
	const std::vector<std::string> titles = {"Galias", "Imperivm I - La guerra de las Galias", "Desembarco en Africa - 204 a.C.", ""};
	std::string name;
	// The title has dashes of its own, and so may the name.
	CHECK_EQ(BattleSaveAdventure("guardar rapidamente-Imperivm I - La guerra de las Galias", titles, &name), 1);
	CHECK_EQ(name, std::string("guardar rapidamente"));
	CHECK_EQ(BattleSaveAdventure("antes-del-campamento-Imperivm I - La guerra de las Galias", titles, &name), 1);
	CHECK_EQ(name, std::string("antes-del-campamento"));
	CHECK_EQ(BattleSaveAdventure("Autoguardado 17h27-desembarco en africa - 204 a.C.", titles, &name), 2);
	CHECK_EQ(name, std::string("Autoguardado 17h27"));
	// A shorter title is only taken when no longer one fits.
	CHECK_EQ(BattleSaveAdventure("mi partida-Galias", titles, &name), 0);
	// The title must follow a dash and leave a name before it; an empty title never counts.
	CHECK_EQ(BattleSaveAdventure("La guerra de las Galias", titles, &name), -1);
	CHECK(name.empty());
	CHECK_EQ(BattleSaveAdventure("-Galias", titles, &name), -1);
	CHECK_EQ(BattleSaveAdventure("partidaGalias", titles, &name), -1);
	CHECK_EQ(BattleSaveAdventure("partida-Landing in Africa", titles, &name), -1);
	CHECK_EQ(BattleSaveAdventure("partida-Galias", {}, &name), -1);
	CHECK_EQ(BattleSaveAdventure("partida-Galias", titles), 0);
}

void CampaignPacksPreferTheActiveOne()
{
	const BattlePackFiles data = {"imperivm1-campaign-data", true, {"data/i1/stats.ini"}};
	const BattlePackFiles galia = {"imperivm1-campaign", false,
		{"adventures/readme.txt", "adventures/imperivm i - la guerra de las galias.bfhp", "adventures/imperivm i - la guerra de las galias.bmp"}};
	const BattlePackFiles hispania = {"imperivm2-hispania", false, {"adventures/imperivm ii - hispania.bfhp"}};
	const BattlePackFiles numancia = {"imperivm2-numancia", true, {"adventures\\Imperivm II - Numancia.bfhp"}};
	const BattlePackFiles mine = {"my-maps", true, {"adventures/mi aventura.bfhp", "maps/imperivm i - galia.bfhp", "adventures/imperivm i - galia.txt"}};

	BattleCampaignPack p = BattleCampaignPackOf({data, mine}, kBattleGameImperivm1);
	CHECK(!p.installed);
	CHECK(!p.active);
	CHECK(p.folder.empty());
	p = BattleCampaignPackOf({data, galia, mine}, kBattleGameImperivm1);
	CHECK(p.installed);
	CHECK(!p.active);
	CHECK_EQ(p.folder, std::string("imperivm1-campaign"));
	CHECK_EQ(p.adventure, std::string("adventures/imperivm i - la guerra de las galias.bfhp"));
	CHECK(!BattleCampaignPackOf({galia}, kBattleGameImperivm2).installed);
	CHECK(!BattleCampaignPackOf({galia}, kBattleGameOther).installed);
	// An inactive pack listed first gives way to an active one.
	p = BattleCampaignPackOf({hispania, numancia}, kBattleGameImperivm2);
	CHECK(p.installed);
	CHECK(p.active);
	CHECK_EQ(p.folder, std::string("imperivm2-numancia"));
	p = BattleCampaignPackOf({hispania, mine}, kBattleGameImperivm2);
	CHECK(!p.active);
	CHECK_EQ(p.folder, std::string("imperivm2-hispania"));
	CHECK_EQ(BattlePicturePath(p.adventure), std::string("adventures/imperivm ii - hispania.bmp"));
	CHECK_EQ(BattlePicturePath("adventures/Galia.BFHP"), std::string("adventures/Galia.bmp"));
	CHECK(BattlePicturePath("adventures/galia.bmp").empty());
}

}

int main()
{
	BottomUpRowsComeOutTopDown();
	PalettesAndTopDownFilesWork();
	SixteenBitPicturesAreFiveFiveFive();
	BrokenFilesAreRejected();
	TitlesLoseTheirYear();
	IntrosFollowTheBattleFolder();
	AdventuresAreSortedByGame();
	DescriptionsGetRealLineBreaks();
	SavesBelongToTheLongestTitleAfterADash();
	CampaignPacksPreferTheActiveOne();
	return test::Summary("battlelist");
}
