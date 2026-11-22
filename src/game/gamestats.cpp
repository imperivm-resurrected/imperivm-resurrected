// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/gamestats.h"

#include <windows.h>

#include <cstddef>
#include <cstdio>

#include "addr/state.h"
#include "addr/world.h"
#include "core/log.h"
#include "game/allycolors.h"
#include "game/gamelayout.h"
#include "hooks/gameload.h"
#include "core/winpath.h"

namespace {
const size_t kGameStats = 0x258, kStatsRecordSize = 0xd0;
const char kFileSuffix[] = ".stats";

// The totals function adds up into this: mask at 0, gold 1, food 2, units on the map 3, units in buildings 4, city points 5, mask again at 6.
struct Totals {
	unsigned mask;
	int gold, food, unitsOutside, unitsInside, cityPoints;
	unsigned maskAgain;
};

typedef void(__fastcall *ComputeTotals_t)(Totals *totals);

StatsHistory g_history;
StatsPlayerInfo g_info[kStatMaxPlayers];
bool g_seen[kStatMaxPlayers] = {};
char *g_game = nullptr;
uint32_t g_lastClock = 0, g_lastSample = 0;
bool g_sampled = false;
std::shared_ptr<const StatsSnapshot> g_snapshot;
bool g_dirty = true;
bool g_loadPending = false;
std::string g_loadData;

bool ReadClock(const char *game, uint32_t &clock)
{
	__try {
		clock = Field<uint32_t>(game, layout::kGameClock);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadTotals(int player, Totals &totals)
{
	totals = {};
	totals.mask = totals.maskAgain = 1u << player;
	__try {
		reinterpret_cast<ComputeTotals_t>(addr::kComputePlayerTotals)(&totals);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

struct RawPlayer {
	bool active = false, playing = false, human = false, local = false, ally = false;
	int state = 0, control = 0;
	char name[64] = {};
	uint32_t rgb = 0;
};

bool ReadPlayer(const char *game, int index, RawPlayer &out)
{
	__try {
		const char *player = layout::PlayerAt(game, index);
		const int state = Field<int>(player, layout::kPlayerState);
		out.active = Field<int>(player, layout::kPlayerActive) != 0 || state != 0;
		if (!out.active) return true;
		out.state = state;
		out.control = Field<int>(player, layout::kPlayerControl);
		out.playing = state == layout::kStatePlaying;
		// A player that lost has no AI object any more, so the game's settings say it better.
		const char *settings = *reinterpret_cast<const char *const *>(addr::kGameSettings);
		if (settings && index < layout::kSettingsEntryCount) out.human = Field<int>(layout::SettingsEntry(settings, index), layout::kEntryType) != layout::kEntryTypeAi;
		else out.human = Field<void *>(player, layout::kPlayerAi) == nullptr;
		const char *local = layout::LocalPlayer(game);
		out.local = local == player;
		if (local && !out.local) {
			const int ours = Field<int>(local, layout::kPlayerId), theirs = Field<int>(player, layout::kPlayerId);
			// An id outside the 16 would read past the diplomacy table: that player counts as no ally.
			const bool known = ours >= 0 && ours < layout::kPlayerCount && theirs >= 0 && theirs < layout::kPlayerCount;
			const bool weAlly = known && (Field<unsigned char>(local, layout::kPlayerDiplomacy + theirs * 4) & 1) != 0;
			const bool theyAlly = known && (Field<unsigned char>(player, layout::kPlayerDiplomacy + ours * 4) & 1) != 0;
			out.ally = weAlly && theyAlly;
		}
		const char *name = layout::PlayerName(player);
		for (size_t i = 0; name && i + 1 < sizeof(out.name) && name[i]; i++) out.name[i] = name[i];
		// The player's own colour, not the ally or enemy one this PC may draw them in.
		out.rgb = Rgb555ToRgb(AllyColorsRealColor(index, Field<unsigned>(player, layout::kPlayerColor)));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadAllied(const char *game, int a, int b)
{
	__try {
		const char *first = layout::PlayerAt(game, a), *second = layout::PlayerAt(game, b);
		const int firstId = Field<int>(first, layout::kPlayerId), secondId = Field<int>(second, layout::kPlayerId);
		if (firstId < 0 || firstId >= layout::kPlayerCount || secondId < 0 || secondId >= layout::kPlayerCount) return false;
		return (Field<unsigned char>(first, layout::kPlayerDiplomacy + secondId * 4) & 1) != 0 &&
			(Field<unsigned char>(second, layout::kPlayerDiplomacy + firstId * 4) & 1) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadRecord(const char *game, int index, StatSample &s, int &killRatio, int &storedStrength)
{
	__try {
		const char *record = game + kGameStats + (index + 1) * kStatsRecordSize;
		const int goldSpent = Field<int>(record, 0x38), built = Field<int>(record, 0x40), lost = Field<int>(record, 0x48);
		s.values[kStatGoldSpent] = goldSpent;
		s.values[kStatFoodSpent] = Field<int>(record, 0x3c);
		s.values[kStatUnitsBuilt] = built;
		s.values[kStatKilled] = Field<int>(record, 0x44);
		s.values[kStatLost] = lost;
		s.values[kStatUnitsMax] = Field<int>(record, 0x4c);
		s.values[kStatLevelMax] = Field<int>(record, 0x50);
		s.values[kStatUnitsAlive] = built > lost ? built - lost : 0;
		s.values[kStatDamageTaken] = Field<int>(record, 0xa0);
		s.values[kStatDamageDealt] = Field<int>(record, 0xa4);
		s.values[kStatGoldCaptured] = Field<int>(record, 0xac);
		s.values[kStatGoldTaxes] = Field<int>(record, 0xb0);
		s.values[kStatGoldOther] = Field<int>(record, 0xb4);
		s.values[kStatGoldTotal] = s.values[kStatGoldCaptured] + s.values[kStatGoldTaxes] + s.values[kStatGoldOther];
		s.values[kStatDevelopment] = static_cast<int>(static_cast<unsigned>(goldSpent) / 500);
		const unsigned killed = Field<unsigned>(record, 0xbc) >> 1, died = Field<unsigned>(record, 0xc0) >> 1;
		// As CPlayerStats::GetKillRatioPercent.
		killRatio = static_cast<int>((killed + 1000 + Field<int>(record, 0xa4)) * 100 / (died + 10000 + Field<int>(record, 0xa0)));
		s.values[kStatMilitary] = killRatio;
		storedStrength = Field<int>(record, 0xb8);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// CPlayerStats +0x54: the game's std::string with the name of the unit that reached the highest level (+0x50).
const size_t kRecordBestUnit = 0x54;

bool ReadFacts(const char *game, int index, GameRecordFacts &out, char *unit, size_t unitSize)
{
	__try {
		const char *record = game + kGameStats + (index + 1) * kStatsRecordSize;
		out.killed = Field<uint32_t>(record, 0x44);
		out.lost = Field<uint32_t>(record, 0x48);
		out.armyMax = Field<uint32_t>(record, 0x4c);
		out.bestLevel = Field<uint32_t>(record, 0x50);
		out.produced = Field<uint32_t>(record, 0x40);
		out.gold = Field<uint32_t>(record, 0xac) + Field<uint32_t>(record, 0xb0) + Field<uint32_t>(record, 0xb4);
		const char *s = record + kRecordBestUnit;
		const unsigned size = layout::StringSize(s);
		const char *text = layout::StringChars(s);
		size_t n = 0;
		for (; text && n + 1 < unitSize && n < size && text[n]; n++) unit[n] = text[n];
		unit[n] = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// One player's values now; false for a seat nobody plays, or a record that cannot be read.
bool ReadSample(const char *game, int index, uint32_t clock, RawPlayer &raw, StatSample &s)
{
	if (!ReadPlayer(game, index, raw) || !raw.active) return false;
	s = StatSample();
	s.ms = clock;
	s.values[kStatInGame] = raw.playing ? 1 : 0;
	int killRatio = 100, strength = 0;
	if (!ReadRecord(game, index, s, killRatio, strength)) return false;
	// A player out of the game keeps the strength the game stored when they lost; the totals would find nothing.
	Totals totals;
	if (raw.playing && ReadTotals(index, totals)) {
		s.values[kStatGoldHeld] = totals.gold;
		s.values[kStatFoodHeld] = totals.food;
		strength = totals.cityPoints + static_cast<int>((static_cast<unsigned>(totals.food) >> 1) + totals.unitsInside + totals.unitsOutside + totals.gold) / 100;
	}
	s.values[kStatArmyStrength] = strength;
	// As CPlayerStats::CalcPowerScore.
	s.values[kStatScore] = strength * killRatio / 100 + s.values[kStatDevelopment];
	return true;
}

void Sample(const char *game, uint32_t clock)
{
	bool active[kStatMaxPlayers] = {};
	for (int index = 0; index < kStatMaxPlayers; index++) {
		RawPlayer raw;
		StatSample s;
		if (!ReadPlayer(game, index, raw) || !raw.active) continue;
		active[index] = true;
		if (!ReadSample(game, index, clock, raw, s)) continue;
		g_history.Add(index, s);
		StatsPlayerInfo &info = g_info[index];
		info.index = index;
		info.name = raw.name;
		info.rgb = raw.rgb;
		info.human = raw.human;
		info.local = raw.local;
		info.ally = raw.ally;
		g_seen[index] = true;
	}
	bool allied[kStatMaxPlayers][kStatMaxPlayers] = {};
	for (int a = 0; a < kStatMaxPlayers; a++) {
		for (int b = a + 1; active[a] && b < kStatMaxPlayers; b++) {
			if (active[b]) allied[a][b] = ReadAllied(game, a, b);
		}
	}
	int teams[kStatMaxPlayers];
	StatsGroupTeams(active, allied, teams);
	for (int index = 0; index < kStatMaxPlayers; index++) {
		if (active[index]) g_info[index].team = teams[index];
	}
	g_lastSample = clock;
	g_sampled = true;
	g_dirty = true;
}

std::string ReadWholeFile(const std::string &path)
{
	std::string data;
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return data;
	char buffer[16384];
	for (size_t got = fread(buffer, 1, sizeof(buffer), f); got; got = fread(buffer, 1, sizeof(buffer), f)) data.append(buffer, got);
	fclose(f);
	return data;
}

std::string DiskPath(const std::string &gamePath)
{
	std::string file = winpath::GameDir() + "\\" + gamePath;
	for (char &c : file) {
		if (c == '/') c = '\\';
	}
	return file;
}

void AfterGameLoad(const char *path, int result)
{
	if (result != 0 || !path) return;
	g_loadPending = true;
	g_loadData = ReadWholeFile(DiskPath(path) + kFileSuffix);
}

void Reset(char *game)
{
	g_history.Clear();
	for (bool &seen : g_seen) seen = false;
	g_game = game;
	g_lastClock = 0;
	g_lastSample = 0;
	g_sampled = false;
	g_dirty = true;
}
}

void GameStatsTick()
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) {
		if (g_game) Reset(nullptr);
		return;
	}
	uint32_t clock = 0;
	if (!ReadClock(game, clock)) return;
	if (g_loadPending) {
		g_loadPending = false;
		Reset(game);
		std::vector<StatsPlayerInfo> players;
		if (!g_loadData.empty() && StatsDecode(g_loadData, g_history, players)) {
			g_history.TrimAfter(clock);
			for (const StatsPlayerInfo &info : players) {
				g_info[info.index] = info;
				g_seen[info.index] = true;
			}
			g_sampled = !g_history.Empty();
			g_lastSample = g_history.LastMs();
			LogD("saves", "history restored up to %u ms of the game", g_lastSample);
		}
		else if (!g_loadData.empty()) LogW("saves", "the history of the loaded game could not be read");
		g_loadData.clear();
		g_lastClock = clock;
		return;
	}
	if (game != g_game || clock < g_lastClock) Reset(game);
	// Only a game that advances is sure to be fully there: loading one also shows frames.
	const bool advanced = clock != g_lastClock;
	const bool firstTick = g_lastClock == 0;
	g_lastClock = clock;
	if (!advanced || firstTick) return;
	if (g_sampled && clock - g_lastSample < g_history.IntervalMs()) return;
	Sample(game, clock);
}

int GameStatsReadAll(StatSample out[kStatMaxPlayers], bool active[kStatMaxPlayers])
{
	int n = 0;
	for (int index = 0; index < kStatMaxPlayers; index++) active[index] = false;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) return 0;
	uint32_t clock = 0;
	if (!ReadClock(game, clock)) return 0;
	for (int index = 0; index < kStatMaxPlayers; index++) {
		RawPlayer raw;
		if (!ReadSample(game, index, clock, raw, out[index])) continue;
		active[index] = true;
		n++;
	}
	return n;
}

void GameStatsSampleNow()
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) return;
	uint32_t clock = 0;
	if (!ReadClock(game, clock)) return;
	if (game != g_game || clock < g_lastClock) Reset(game);
	g_lastClock = clock;
	Sample(game, clock);
}

void InstallGameStats(bool knownExe)
{
	if (!InstallGameLoadHook(knownExe)) return;
	OnGameLoadedFrom(&AfterGameLoad);
}

void GameStatsWriteFile(const std::string &saveFile)
{
	const std::string file = saveFile + kFileSuffix;
	GameStatsSampleNow();
	if (g_history.Empty()) {
		DeleteFileA(file.c_str());
		return;
	}
	const std::shared_ptr<const StatsSnapshot> data = GameStatsSnapshot();
	const std::string bytes = StatsEncode(data->history, data->players);
	FILE *f = nullptr;
	if (fopen_s(&f, file.c_str(), "wb") != 0 || !f) {
		LogW("saves", "could not write the history of the save");
		return;
	}
	fwrite(bytes.data(), 1, bytes.size(), f);
	fclose(f);
}

std::shared_ptr<const StatsSnapshot> GameStatsSnapshot()
{
	if (g_dirty || !g_snapshot) {
		auto fresh = std::make_shared<StatsSnapshot>();
		fresh->history = g_history;
		for (int index = 0; index < kStatMaxPlayers; index++) {
			if (g_seen[index]) fresh->players.push_back(g_info[index]);
		}
		g_snapshot = fresh;
		g_dirty = false;
	}
	return g_snapshot;
}

bool GameStatsLive(std::vector<StatsLivePlayer> &players, uint32_t &clockMs)
{
	players.clear();
	const char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap) || !ReadClock(game, clockMs)) return false;
	bool active[kStatMaxPlayers] = {};
	for (int index = 0; index < kStatMaxPlayers; index++) {
		RawPlayer raw;
		if (!ReadPlayer(game, index, raw) || !raw.active) continue;
		StatsLivePlayer p;
		p.info.index = index;
		p.info.name = raw.name;
		p.info.rgb = raw.rgb;
		p.info.human = raw.human;
		p.info.local = raw.local;
		p.info.ally = raw.ally;
		p.state = raw.state;
		// 3 is the map's neutral player and 4 an unused slot: neither plays, nor joins two teams into one through its alliances.
		p.playable = raw.control != 3 && raw.control != 4;
		active[index] = p.playable;
		players.push_back(p);
	}
	bool allied[kStatMaxPlayers][kStatMaxPlayers] = {};
	for (int a = 0; a < kStatMaxPlayers; a++) {
		for (int b = a + 1; active[a] && b < kStatMaxPlayers; b++) {
			if (active[b]) allied[a][b] = ReadAllied(game, a, b);
		}
	}
	int teams[kStatMaxPlayers];
	StatsGroupTeams(active, allied, teams);
	for (StatsLivePlayer &p : players) p.info.team = teams[p.info.index];
	return true;
}

bool GameStatsRecordOf(int player, GameRecordFacts &out)
{
	out = GameRecordFacts();
	const char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap) || player < 0 || player >= kStatMaxPlayers) return false;
	char unit[128] = {};
	if (!ReadFacts(game, player, out, unit, sizeof(unit))) return false;
	out.bestUnit = unit;
	return true;
}
