// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/adventureresult.h"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
#include "addr/world.h"
#include "core/log.h"
#include "core/winpath.h"
#include "game/conquest.h"
#include "game/gamehook.h"
#include "game/gamelayout.h"
#include "hooks/gameload.h"
#include "hooks/gamestate.h"
#include "mods/savelib.h"

namespace {

typedef void(__cdecl *DeclareTeamVictory_t)(int player, void *allies);
DeclareTeamVictory_t g_origDeclareTeamVictory = nullptr;

std::mutex g_lock;
std::string g_startedPath, g_startedTitle;
unsigned g_conquestSerial = 0;

// CVXGame: the kind of game (1 for an adventure, from map.xml), its map header and the difficulty (0 easy, 1 normal, 2 hard).
const size_t kGameType = 0xa0, kGameHeader = 0x98, kGameDifficulty = 0x254;
const int kGameTypeAdventure = 1;
// CMapHeader: the adventure's name, a std::string.
const size_t kHeaderName = 0x0c;
const int kStateWon = 3;

std::string ProfileFile(const char *name)
{
	const std::string profile = SaveLibActiveProfile();
	return profile.empty() ? std::string() : winpath::GameDir() + "\\Profiles\\" + profile + "\\" + name;
}

std::string LaurelsFile() { return ProfileFile("imperivm-laurels.txt"); }

std::string ConquestLaurelsFile() { return ProfileFile("imperivm-conquest-laurels.txt"); }

std::string ReadText(const std::string &file)
{
	std::string text;
	FILE *f = nullptr;
	if (file.empty() || fopen_s(&f, file.c_str(), "rb") != 0 || !f) return text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
	fclose(f);
	return text;
}

std::vector<BattleLaurel> ReadLaurels(const std::string &file) { return BattleLaurelsDecode(ReadText(file)); }

bool WriteText(const std::string &file, const std::string &text)
{
	const std::string tmp = file + ".tmp";
	FILE *f = nullptr;
	if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return false;
	const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	fclose(f);
	return ok && MoveFileExA(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool WriteLaurels(const std::string &file, const std::vector<BattleLaurel> &laurels) { return WriteText(file, BattleLaurelsEncode(laurels)); }

// A conquest's battles are played as adventures: their victory is the territory's laurel, not an adventure's.
bool NoteConquestBattle(const char *game, int difficulty)
{
	std::string key, territory;
	if (!ConquestBattleOf(game, key, territory)) return false;
	if (key.empty() || territory.empty()) return true;
	if (difficulty < 0 || difficulty > 2) {
		LogW("game", "a conquest battle was won at an unknown difficulty (%d): no laurel", difficulty);
		return true;
	}
	ConquestLaurelNote(key, territory, difficulty + 1);
	return true;
}

void __cdecl HookDeclareTeamVictory(int player, void *allies)
{
	g_origDeclareTeamVictory(player, allies);
	const char *game = *reinterpret_cast<const char **>(addr::kGame);
	const char *local = game ? layout::LocalPlayer(game) : nullptr;
	if (!local || Field<int>(local, layout::kPlayerState) != kStateWon || GameIsMultiplayer()) return;
	const int difficulty = Field<int>(game, kGameDifficulty);
	if (NoteConquestBattle(game, difficulty) || Field<int>(game, kGameType) != kGameTypeAdventure) return;
	if (difficulty < 0 || difficulty > 2) {
		LogW("game", "an adventure was won at an unknown difficulty (%d): no laurel", difficulty);
		return;
	}
	const std::string title = layout::StringValue(game + kGameHeader + kHeaderName, 260);
	std::string path, startedTitle;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		path = g_startedPath;
		startedTitle = g_startedTitle;
		// One victory per start: what comes next was not started from the screen until it says so again.
		g_startedPath.clear();
		g_startedTitle.clear();
	}
	// Another adventure than the one the screen started (one started another way): kept by its own title.
	if (!path.empty() && startedTitle != title) {
		LogI("game", "the adventure won is \"%s\", not the one the screen started (\"%s\"): kept by its title", title.c_str(), startedTitle.c_str());
		path.clear();
	}
	const std::string file = LaurelsFile();
	std::vector<BattleLaurel> laurels = ReadLaurels(file);
	if (!BattleLaurelsAdd(laurels, path, title, difficulty + 1)) {
		LogI("game", "adventure \"%s\" won at difficulty %d; its laurel was already that or better", title.c_str(), difficulty);
		return;
	}
	if (file.empty() || !WriteLaurels(file, laurels)) LogE("game", "the laurel of the adventure \"%s\" could not be saved", title.c_str());
	else LogI("game", "adventure \"%s\" (%s) won at difficulty %d: laurel saved", title.c_str(), path.empty() ? "loaded game" : path.c_str(), difficulty);
}

// A game loaded from a save is not the one the adventure screen started: its victory is kept by its title alone.
void AfterGameLoad(const char *path, int result)
{
	if (result != 0) return;
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_startedPath.empty()) LogD("game", "laurels: %s was loaded, so the adventure started from the screen is forgotten", path ? path : "a game");
	g_startedPath.clear();
	g_startedTitle.clear();
}

}

void InstallAdventureResult(bool knownExe)
{
	if (!knownExe) return;
	if (!HookFunction(addr::kDeclareTeamVictory, &HookDeclareTeamVictory, &g_origDeclareTeamVictory)) {
		LogE("game", "could not hook DeclareTeamVictory: adventures won get no laurel");
		return;
	}
	if (InstallGameLoadHook(knownExe)) OnGameLoadedFrom(&AfterGameLoad);
}

void AdventureResultNoteStart(const std::string &adventurePath, const std::string &title)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_startedPath = adventurePath;
	g_startedTitle = title;
}

std::vector<BattleLaurel> AdventureLaurels()
{
	return ReadLaurels(LaurelsFile());
}

std::vector<ConquestLaurel> ConquestLaurels()
{
	return ConquestLaurelsDecode(ReadText(ConquestLaurelsFile()));
}

unsigned ConquestLaurelsSerial()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_conquestSerial;
}

void ConquestLaurelNote(const std::string &key, const std::string &territory, int level)
{
	const std::string file = ConquestLaurelsFile();
	std::vector<ConquestLaurel> laurels = ConquestLaurels();
	const char *what = territory.empty() ? "the whole conquest" : territory.c_str();
	if (!ConquestLaurelsAdd(laurels, key, territory, level)) {
		LogI("game", "conquest \"%s\": %s won at level %d; its laurel was already that or better", key.c_str(), what, level);
		return;
	}
	if (file.empty() || !WriteText(file, ConquestLaurelsEncode(laurels))) {
		LogE("game", "the laurel of the conquest \"%s\" (%s) could not be saved", key.c_str(), what);
		return;
	}
	LogI("game", "conquest \"%s\": %s won at level %d, laurel saved", key.c_str(), what, level);
	std::lock_guard<std::mutex> lock(g_lock);
	g_conquestSerial++;
}
