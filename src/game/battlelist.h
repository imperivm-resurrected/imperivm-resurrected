// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct BattleImage {
	int w = 0, h = 0;
	std::vector<uint32_t> bgra;
};

// Uncompressed 8, 16, 24 and 32-bit bitmaps, the kinds the game ships next to its adventures.
bool DecodeBattleBmp(const unsigned char *data, size_t size, BattleImage &out);
// The picture made at most `maxWidth` wide, each pixel the average of the ones it covers; a picture already that narrow is kept.
BattleImage ShrinkBattleImage(BattleImage image, int maxWidth);

struct BattleTitle {
	std::string name, year;
};

// "El sitio de Alesia - 52 a.C." gives the name and the year; a title without a year at its end stays whole.
BattleTitle SplitBattleTitle(const std::string &title);

// The intro of a great battle, as a pattern for the files of the Movies folder: "adventures/GreatBattles/1_Great_Battles_Zama" gives
// "*_Intro_Great_B_Zama.avi". Empty for the other adventures, which have no intro of their own.
std::string BattleIntroPattern(const std::string &adventurePath);

enum BattleGame { kBattleGameOther, kBattleGameImperivm1, kBattleGameImperivm2 };

// Which game an adventure comes from, by its file name: the converted campaigns of the first two games are installed as
// "Imperivm I - <name>" and "Imperivm II - <name>". Anything else is the player's own.
BattleGame BattleGameOf(const std::string &adventurePath);

// "Imperivm I - La guerra de las Galias" without the game's name, which its tab already shows; other titles stay whole.
std::string BattleTitleWithoutGame(const std::string &title);

// The adventures an installed content pack brings (paths inside the pack, as "adventures/imperivm i - galia.bfhp") and whether the
// game mounted it.
struct BattlePackFiles {
	std::string folder;
	bool active = false;
	std::vector<std::string> files;
};

// The pack that brings a game's campaign, an active one before an inactive one: `adventure` is its .bfhp inside the pack, which has
// the picture next to it. Not installed when no pack has an adventure of that game.
struct BattleCampaignPack {
	bool installed = false, active = false;
	std::string folder, adventure;
};
BattleCampaignPack BattleCampaignPackOf(const std::vector<BattlePackFiles> &packs, BattleGame game);
// "adventures/imperivm i - galia.bfhp" gives "adventures/imperivm i - galia.bmp"; empty when it is not an adventure.
std::string BattlePicturePath(const std::string &adventureFile);

// The mod's own covers of the campaigns of Imperivm I and II (files of imperivm-resurrected\campaigns), in the order the packs
// install them, with the adventure each one belongs to and its English title.
struct BattleCampaignCover {
	const char *file, *adventure, *title;
};
std::vector<BattleCampaignCover> BattleCampaignCovers(BattleGame game);
// The cover of an adventure, by its file name with or without its folder and extension; empty for one that has none.
std::string BattleCampaignCoverOf(const std::string &adventurePath);
// The mod's remastered cover of one of the game's twelve great battles and challenges (a file of imperivm-resurrected\adventures,
// named after the adventure's own file), by its file name with or without its folder and extension; empty for any other adventure.
std::string BattleRemasteredCoverOf(const std::string &adventurePath);

// An adventure the player has won, with the hardest difficulty it was won at: 1 easy, 2 normal, 3 hard. `title` is the adventure's
// name as the game gives it when it is won; `path` its row in the game's list, when it was started from the adventure screen.
struct BattleLaurel {
	std::string path, title;
	int level = 0;
};
// One laurel a line: the level, the path and the title, separated by tabs. Lines that do not read are left out.
std::vector<BattleLaurel> BattleLaurelsDecode(const std::string &text);
std::string BattleLaurelsEncode(const std::vector<BattleLaurel> &laurels);
// A victory at `level`: a known adventure keeps its hardest level and learns the path or title it lacked. False when nothing changed.
bool BattleLaurelsAdd(std::vector<BattleLaurel> &laurels, const std::string &path, const std::string &title, int level);
// The level won of an adventure of the list, by its path or else its title; 0 when it was never won.
int BattleLaurelOf(const std::vector<BattleLaurel> &laurels, const std::string &path, const std::string &title);

// A conquest's laurels: the hardest difficulty (1 to 3) each territory was won at, and the conquest itself once it is won whole
// (`territory` empty). `conquest` is ConquestKeyOf its file, so a conquest of a mod gets its own.
struct ConquestLaurel {
	std::string conquest, territory;
	int level = 0;
};
// The name of a conquest's file without its folder or extension, in lowercase: the game's list and its settings write the path apart.
std::string ConquestKeyOf(const std::string &path);
// One laurel a line: the level, the conquest and the territory, separated by tabs. Lines that do not read are left out.
std::vector<ConquestLaurel> ConquestLaurelsDecode(const std::string &text);
std::string ConquestLaurelsEncode(const std::vector<ConquestLaurel> &laurels);
// A victory at `level` keeps the hardest one. False when nothing changed.
bool ConquestLaurelsAdd(std::vector<ConquestLaurel> &laurels, const std::string &conquest, const std::string &territory, int level);
// 0 when it was never won.
int ConquestLaurelOf(const std::vector<ConquestLaurel> &laurels, const std::string &conquest, const std::string &territory);

// The adventure's text keeps the "\n" of its file as two characters.
std::string BattleDescriptionText(const std::string &raw);

// Which adventure a saved game belongs to. The game names it "<name>-<the adventure's title, translated>" (BuildQuickSavePath), and
// both the name and the title may have dashes of their own: the save is the title, among `titles`, that ends its name after a dash
// and leaves a name before it; the longest one when several do ("…-La guerra de las Galias" and "…-Galias"). -1 when none does,
// as for a save made in another language. `name` gets the part before the dash.
int BattleSaveAdventure(const std::string &saveBaseName, const std::vector<std::string> &titles, std::string *name = nullptr);
