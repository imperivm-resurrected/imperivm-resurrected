// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "hooks/gamefiles.h"
#include "hooks/gameload.h"
#include "core/log.h"
#include "game/cmdcheck.h"
#include "game/gamelayout.h"
#include "game/i1engine.h"
#include "addr/state.h"
#include "addr/script.h"
#include "addr/world.h"

// What differs from Great Battles of Rome is in imperivm-re/analisis/i1-vs-gbr (combate.md, propiedades.md).
namespace {

const unsigned kReadMode = 1;

// CVXScriptClass: id (std::string) +0x04, behaviour count +0x1ec, methods resolved +0x218.
const int kClassId = 0x04, kClassBehaviorCount = 0x1ec, kClassResolved = 0x218;
// CVXMapObj/CVXUnit: class +0x3c, flags +0x2c, life +0xc0, max life +0xc8, armours +0xe4/+0xe8, experience +0x180, level +0x184,
// curse bits +0x194, parry +0x1a0, last attack (game ms) +0x1a4, hits in a row +0x1ac.
const int kObjClass = 0x3c, kObjFlags = 0x2c, kLife = 0xc0, kMaxLife = 0xc8, kArmorSlash = 0xe4, kArmorPierce = 0xe8;
const int kExperience = 0x180, kLevel = 0x184, kCurse = 0x194, kParry = 0x1a0, kLastAttack = 0x1a4, kHitsInRow = 0x1ac;
// Flags: hidden 0x200000, unit 0x400000, training target 0x10000000. Curse bits: cursed 0x8000000, under a cover of mercy 0x10000000.
const unsigned kFlagHidden = 0x200000, kFlagUnit = 0x400000, kFlagTraining = 0x10000000, kFlagCursed = 0x8000000, kFlagMercy = 0x10000000;

// Imperivm I's specialities (Imperivm.exe table 007376a4): "speciality" is split on commas, without trimming, and each
// part must match exactly.
enum Special : unsigned {
	kDefensiveStand = 1 << 0,
	kBerserk = 1 << 1,
	kSpread = 1 << 2,
	kAttackSkill = 1 << 3,
	kDefenseSkill = 1 << 4,
	kDeathBlow = 1 << 5,
	kCharge = 1 << 6,
	kTrample = 1 << 7,
	kSpike = 1 << 8,
	kVampire = 1 << 10,
	kSacrificing = 1 << 11,
};
const std::pair<const char *, unsigned> kSpecialNames[] = {{"defensive stand", kDefensiveStand}, {"berserk", kBerserk}, {"spread damage", kSpread},
	{"attack skill", kAttackSkill}, {"defense skill", kDefenseSkill}, {"death blow", kDeathBlow}, {"charge", kCharge}, {"trample damage", kTrample},
	{"spike damage", kSpike}, {"vampire blow", kVampire}, {"sacrificing blow", kSacrificing}};
// Imperivm II's (Imperivm II.exe table 0076dca0): the same bits, some with a new speciality.
enum Special2 : unsigned {
	kRage = 1 << 1,
	kCombatSkill = 1 << 3,
	kFullArmor = 1 << 4,
	kPenetrating = 1 << 11,
	kBleeding = 1 << 13,
};
const std::pair<const char *, unsigned> kSpecialNames2[] = {{"defensive stand", kDefensiveStand}, {"rage", kRage}, {"spread damage", kSpread},
	{"combat skill", kCombatSkill}, {"full armor", kFullArmor}, {"death blow", kDeathBlow}, {"charge", kCharge}, {"trample damage", kTrample},
	{"spike damage", kSpike}, {"vampire blow", kVampire}, {"penetrating", kPenetrating}, {"bleeding", kBleeding}};

struct Stats {
	int minAttack = 0, maxAttack = 0, defenseSlash = 0, defensePierce = 0;
	// 0 slash, 1 pierce, 2 anything else (siege): Imperivm I's damage_type.
	int damageType = 0;
	unsigned specials = 0;
	int bonusBuilding = 100;
};

struct Rules {
	int expA = 100, expB = 180, expT = 10;
	int deathBlow = 40, berserk = 100, spread = 20, chargeTime = 10000, chargeBonus = 600, spike = 100, trample = 50, vampire = 50, sacrificing = 20;
	int healthPerLevel = 5, heroHealthPerLevel = 20, ghostMinHealth = 10, expFromArmyDivider = 10, levelExpDivider = 8;
	// Imperivm II only.
	int rage = 50, bleeding = 10, fullArmor = 8, minDamage = 2, coverOfMercy = 50;
	std::vector<int> expTable;
};

// A few bytes of the game's code that read otherwise while a call of an Imperivm I object runs: checked against what the
// game has, then swapped around that call only. The game's logic runs on one thread.
struct CodeSwap {
	unsigned char *at = nullptr;
	unsigned char game[4] = {}, ours[4] = {};
	size_t size = 0;
	bool ready = false;
	void Prepare(uintptr_t where, const unsigned char *expected, const unsigned char *alt, size_t n)
	{
		at = reinterpret_cast<unsigned char *>(where);
		size = n;
		DWORD old = 0;
		if (n > sizeof(game) || memcmp(at, expected, n) != 0 || !VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return;
		memcpy(game, expected, n);
		memcpy(ours, alt, n);
		ready = true;
	}
	void Use(bool imperivm1) const
	{
		if (!ready) return;
		memcpy(at, imperivm1 ? ours : game, size);
		FlushInstructionCache(GetCurrentProcess(), at, size);
	}
};

// Attacking does not show a hidden unit in Imperivm I: gbr.exe's PerformAttack does (005dc8be jumps over SetVisible only
// when the unit is already visible; EB jumps always).
CodeSwap g_revealOnAttack;
// How long a unit drained by a ghost stays bound to it (thread 0x173): 1600 ms in gbr.exe (push 0x640 at 005d2980), 500 ms
// in Imperivm I (Imperivm.exe 0050df40), so that several ghosts can take turns on one victim.
CodeSwap g_drainLock;

typedef void *(__fastcall *ResolveMethods_t)(char *scriptClass);
typedef int(__fastcall *HasProjectileClass_t)(const void *obj);
typedef void(__fastcall *RecalcStats_t)(char *unit);
typedef void(__fastcall *SetHealth_t)(char *obj, void *edx, int life);

ResolveMethods_t g_origResolveMethods = nullptr;
RecalcStats_t g_origRecalcStats = nullptr;
int g_trimmedClasses = 0;

// The older games whose classes a campaign brings (tools/imperivm1, tools/imperivm2): [1] Imperivm I (I1*), [2] Imperivm II
// (I2*), each with the combat values and rules its converter writes.
struct Game {
	const char *name, *statsFile;
	std::map<std::string, Stats> stats;
	Rules rules;
};
Game g_games[] = {{"", ""}, {"Imperivm I", "data/mod/imperivm1/stats.ini"}, {"Imperivm II", "data/mod/imperivm2/stats.ini"}};
const int kGames = static_cast<int>(sizeof(g_games) / sizeof(g_games[0])), kImperivm1 = 1, kImperivm2 = 2;
bool g_loaded = false;
std::unordered_map<const void *, const Stats *> g_byClass;
// CVXUnit: the handle of its hero (+0x170).
const int kLeaderHandle = 0x170;
const unsigned kFlagHero = 0x1000000;

// The game a class of a campaign comes from: "I1" or "I2" and a capital letter; 0 for the game's own classes.
int GameOfId(const std::string &id)
{
	if (id.size() < 3 || id[0] != 'I' || id[2] < 'A' || id[2] > 'Z') return 0;
	const int g = id[1] - '0';
	return g > 0 && g < kGames ? g : 0;
}

void *__fastcall HookResolveMethods(char *scriptClass)
{
	const bool fresh = scriptClass && At<int>(scriptClass, kClassResolved) == 0;
	const unsigned own = fresh ? At<unsigned>(scriptClass, kClassBehaviorCount) : 0;
	void *result = g_origResolveMethods(scriptClass);
	// A class appends the behaviours of its parent to its own (gbr.exe 005a5fd0), with no way to leave one out. An Imperivm I
	// or Imperivm II class hangs from one of the game's (BaseTownhall, Hero...) and would run its sentries, hero skills or autotrain.
	if (fresh && At<unsigned>(scriptClass, kClassBehaviorCount) > own && GameOfId(GameStringAt(scriptClass + kClassId))) {
		At<unsigned>(scriptClass, kClassBehaviorCount) = own;
		if (++g_trimmedClasses <= 3 || g_trimmedClasses % 50 == 0)
			LogD("game", "class %s keeps its own %u behaviours only (%d classes so far)", GameStringAt(scriptClass + kClassId).c_str(), own, g_trimmedClasses);
	}
	return result;
}

int ToInt(const std::string &v, int fallback)
{
	char *end = nullptr;
	const long n = strtol(v.c_str(), &end, 10);
	return end && end != v.c_str() ? static_cast<int>(n) : fallback;
}

void BuildExpTable(Rules &r)
{
	// Exp[n] = Exp[n-1] + max(n - 1 - T, 0) * A + B, in hundredths (Imperivm.exe 0050b690; gbr.exe 005d26d0 with its own A, B, T).
	r.expTable.assign(1002, 0);
	long long acc = 0;
	for (int n = 2; n <= 1001; n++) {
		acc += static_cast<long long>((std::max)(n - 1 - r.expT, 0)) * r.expA + r.expB;
		r.expTable[n] = static_cast<int>((std::min<long long>)(acc / 100, 0x7fffffff));
	}
}
void Parse(const std::string &text, Game &game, bool imperivm2)
{
	Rules &r = game.rules;
	std::string section;
	size_t from = 0;
	while (from < text.size()) {
		size_t nl = text.find('\n', from);
		if (nl == std::string::npos) nl = text.size();
		std::string line = text.substr(from, nl - from);
		from = nl + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == ';') continue;
		if (line[0] == '[') {
			section = line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1);
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
		if (section == "GamePlay") {
			int *const fields[] = {&r.expA, &r.expB, &r.expT, &r.deathBlow, &r.berserk, &r.spread, &r.chargeTime, &r.chargeBonus, &r.spike, &r.trample, &r.vampire,
				&r.sacrificing, &r.healthPerLevel, &r.heroHealthPerLevel, &r.ghostMinHealth, &r.expFromArmyDivider, &r.levelExpDivider, &r.rage, &r.bleeding, &r.fullArmor,
				&r.minDamage, &r.coverOfMercy};
			const char *const names[] = {"ExpA", "ExpB", "ExpT", "DeathBlowPerc", "BerserkPerc", "SpreadPerc", "ChargeTime", "ChargeBonusPerc", "SpikePerc",
				"TramplePerc", "VampireBlowPerc", "SacraficeBlowPerc", "HealthPerLevel", "HeroHealthPerLevel", "GhostMinHealthPercent", "ExpFromArmyDivider", "LevelExpDivider",
				"RagePerc", "BleedingPerc", "FullArmor", "MinDamage", "CoverOfMercy"};
			for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
				if (key == names[i]) *fields[i] = ToInt(value, *fields[i]);
			continue;
		}
		Stats &s = game.stats[section];
		if (key == "minattack") s.minAttack = ToInt(value, 0);
		else if (key == "maxattack") s.maxAttack = ToInt(value, 0);
		else if (key == "defense_slash") s.defenseSlash = ToInt(value, 0);
		else if (key == "defense_pierce") s.defensePierce = ToInt(value, 0);
		else if (key == "damage_type") s.damageType = value == "slash" ? 0 : value == "pierce" ? 1 : 2;
		else if (key == "bonus_building") s.bonusBuilding = ToInt(value, 100);
		else if (key == "speciality") {
			size_t at = 0;
			while (at <= value.size()) {
				size_t comma = value.find(',', at);
				if (comma == std::string::npos) comma = value.size();
				const std::string part = value.substr(at, comma - at);
				for (const auto &n : kSpecialNames)
					if (!imperivm2 && part == n.first) s.specials |= n.second;
				for (const auto &n : kSpecialNames2)
					if (imperivm2 && part == n.first) s.specials |= n.second;
				at = comma + 1;
			}
		}
	}
}

// The stats of the campaigns' classes, read once: the data mods that write them need a restart of the game anyway. Only
// objects of their classes use them.
void LoadStats()
{
	if (g_loaded) return;
	g_loaded = true;
	g_byClass.clear();
	for (int g = 1; g < kGames; g++) {
		Game &game = g_games[g];
		game.stats.clear();
		game.rules = Rules();
		std::vector<char> bytes;
		if (ReadGameFile(game.statsFile, kReadMode, bytes) && !bytes.empty()) Parse(std::string(bytes.begin(), bytes.end()), game, g == kImperivm2);
		BuildExpTable(game.rules);
		if (!game.stats.empty()) LogI("game", "%s's combat for %d classes of its campaigns", game.name, static_cast<int>(game.stats.size()));
	}
}

std::string ClassIdOf(const void *obj)
{
	const void *cls = obj ? At<void *>(obj, kObjClass) : nullptr;
	return cls ? GameStringAt(static_cast<const char *>(cls) + kClassId) : std::string();
}

const Stats *StatsOf(const void *obj)
{
	if (!obj) return nullptr;
	const void *cls = At<void *>(obj, kObjClass);
	if (!cls) return nullptr;
	const auto hit = g_byClass.find(cls);
	if (hit != g_byClass.end()) return hit->second;
	const std::string id = ClassIdOf(obj);
	const int g = GameOfId(id);
	const auto it = g ? g_games[g].stats.find(id) : g_games[0].stats.end();
	const Stats *s = g && it != g_games[g].stats.end() ? &it->second : nullptr;
	g_byClass[cls] = s;
	return s;
}

std::unordered_map<const void *, int> g_classGame;

// The game an object's class comes from (GameOfId), 0 for the game's own.
int GameOf(const void *obj)
{
	if (!obj) return 0;
	const void *cls = At<void *>(obj, kObjClass);
	if (!cls) return 0;
	const auto hit = g_classGame.find(cls);
	if (hit != g_classGame.end()) return hit->second;
	return g_classGame[cls] = GameOfId(ClassIdOf(obj));
}

// An object of one of the campaigns' classes (with or without combat values).
bool IsI1Object(const void *obj)
{
	return GameOf(obj) != 0;
}

// The rules of the game an object's class comes from (call LoadStats first).
const Rules &RulesOf(const void *obj)
{
	return g_games[GameOf(obj)].rules;
}

// gbr.exe's experience table (g_expThresholds: [k] the experience of level k + 1), read by GetLevelFromExp and
// GetExpForLevel, so by every level of the game (SetLevel, ModLevel, a hero's bonus...). While a game of the campaign runs
// it holds Imperivm I's; the game's comes back when another game starts or one is loaded.
const int kExpLevels = 1001;
int g_gameExp[kExpLevels];
bool g_gameExpSaved = false;
int g_tableGame = 0;
int g_lastGameTime = 0;

// The experience table of a game (1, 2), or the game's own (0).
void UseTable(int g)
{
	if (g == g_tableGame) return;
	int *table = reinterpret_cast<int *>(addr::kExpThresholds);
	if (!g_gameExpSaved) memcpy(g_gameExp, table, sizeof(g_gameExp));
	g_gameExpSaved = true;
	if (g) {
		for (int k = 0; k < kExpLevels; k++) table[k] = g_games[g].rules.expTable[k + 1];
	}
	else {
		memcpy(table, g_gameExp, sizeof(g_gameExp));
	}
	g_tableGame = g;
	LogD("game", "levels: %s experience table", g ? g_games[g].name : "the game's");
}

// A unit of a campaign's classes means a game of that campaign; the clock going back means a new game.
void WatchGame(const void *unit)
{
	const char *game = *reinterpret_cast<char *const *>(addr::kGame);
	const int now = game ? Field<int>(game, layout::kGameClock) : 0;
	if (now + 1000 < g_lastGameTime) UseTable(0);
	g_lastGameTime = now;
	const int g = GameOf(unit);
	if (g && g != g_tableGame) UseTable(g);
}

void OnLoading()
{
	UseTable(0);
	g_lastGameTime = 0;
}

typedef int(__fastcall *PerformAttack_t)(char *unit, void *edx, void *target);
typedef int(__fastcall *GhostDamage_t)(char *functor, void *edx, char *unit);
PerformAttack_t g_origPerformAttack = nullptr;
GhostDamage_t g_origGhostDamage = nullptr;

int __fastcall HookPerformAttack(char *unit, void *edx, void *target)
{
	const bool i1 = IsI1Object(unit);
	if (i1) g_revealOnAttack.Use(true);
	const int result = g_origPerformAttack(unit, edx, target);
	if (i1) g_revealOnAttack.Use(false);
	return result;
}

// An Imperivm I ghost leaves its victims at GhostMinHealthPercent 10 (gbr.exe 20, read by the engine: 0052b1f0).
int __fastcall HookGhostDamage(char *functor, void *edx, char *unit)
{
	const void *ghost = functor ? *reinterpret_cast<void **>(functor + 4) : nullptr;
	if (!IsI1Object(ghost)) return g_origGhostDamage(functor, edx, unit);
	LoadStats();
	int &minHealth = *reinterpret_cast<int *>(addr::kGhostMinHealthPercent);
	const int game = minHealth;
	minHealth = RulesOf(ghost).ghostMinHealth;
	g_drainLock.Use(true);
	const int result = g_origGhostDamage(functor, edx, unit);
	g_drainLock.Use(false);
	minHealth = game;
	return result;
}

// A kill an Imperivm I unit is about to make (I1AfterAttack), for its experience once the game has given its own.
struct PendingKill {
	char *attacker = nullptr, *defender = nullptr, *leader = nullptr;
	int expBefore = 0, armyExpBefore = 0, gain = 0;
};
PendingKill g_kill;
// CVXUnit: the experience its hero gathers from its army (+0x2c4); SetExperience is vtable +0x118 (experience, 1).
const int kArmyExp = 0x2c4, kVtSetExperience = 0x118;
typedef void(__fastcall *SetExperience_t)(char *unit, void *edx, int exp, int notify);
typedef void(__cdecl *ResolveAttack_t)(char *attacker, char *defender);
typedef int(__fastcall *GetBonus_t)(void *table, void *edx, const void *attackerClass, const void *defenderClass);
ResolveAttack_t g_origResolveAttack = nullptr;
GetBonus_t g_origGetBonus = nullptr;

// What a kill gives in Imperivm I: the victim's level / LevelExpDivider (8) + 1, to the killer and to its hero's army
// experience (Imperivm.exe 00510691); gbr.exe gives 1 + (victim's level - killer's level) / 6, from 0 to 8.
void __cdecl HookResolveAttack(char *attacker, char *defender)
{
	g_kill = PendingKill();
	g_origResolveAttack(attacker, defender);
	const PendingKill k = g_kill;
	g_kill = PendingKill();
	if (!k.attacker || k.attacker != attacker || k.defender != defender || At<int>(defender, kLife) > 0) return;
	const int exp = k.expBefore + k.gain;
	if (At<int>(attacker, kExperience) != exp)
		(*reinterpret_cast<SetExperience_t *>(*reinterpret_cast<char **>(attacker) + kVtSetExperience))(attacker, nullptr, exp, 1);
	if (k.leader && At<int>(k.leader, kArmyExp) != k.armyExpBefore) At<int>(k.leader, kArmyExp) = k.armyExpBefore + k.gain;
}

// Imperivm I's bonus.xml for the attacks it applies to through the game's table (the catapult against buildings: 600
// against gbr.exe's 300).
int __fastcall HookGetBonus(void *table, void *edx, const void *attackerClass, const void *defenderClass)
{
	const int game = g_origGetBonus(table, edx, attackerClass, defenderClass);
	if (game == 100 || !attackerClass) return game;
	const std::string id = GameStringAt(static_cast<const char *>(attackerClass) + kClassId);
	const int g = GameOfId(id);
	if (!g) return game;
	LoadStats();
	const auto it = g_games[g].stats.find(id);
	return it != g_games[g].stats.end() && it->second.bonusBuilding != 100 ? it->second.bonusBuilding : game;
}

// A game global read by a call of an Imperivm I object: Imperivm I's value during that call. One not loaded yet (0, read
// by that very call the first time) is left alone.
struct GlobalSwap {
	int *at;
	int game;
	bool active;
	GlobalSwap(uintptr_t where, int value, bool when) : at(reinterpret_cast<int *>(where)), game(*at), active(when && *at != 0)
	{
		if (active) *at = value;
	}
	~GlobalSwap()
	{
		if (active) *at = game;
	}
};

typedef void(__fastcall *RegenTick_t)(char *unit, void *edx, int fed);
typedef int(__fastcall *CalcFireRate_t)(char *settlement);
typedef int(__fastcall *BuildingRepair_t)(char *building, void *edx, int amount);
typedef void(__fastcall *UpdateProduction_t)(char *settlement, void *edx, int arg);
RegenTick_t g_origRegenTick = nullptr;
CalcFireRate_t g_origCalcFireRate = nullptr;
BuildingRepair_t g_origBuildingRepair = nullptr;
UpdateProduction_t g_origUpdateProduction = nullptr;
const int kSettlementHolder = 0x5e;

// The game a settlement's central building comes from (GameOf).
int SettlementGame(const char *settlement)
{
	const unsigned short building = settlement ? At<unsigned short>(settlement, 0x8c) : 0xffff;
	return building != 0xffff ? GameOf(reinterpret_cast<char **>(addr::kHandleTable)[building]) : 0;
}

// In a building an Imperivm I unit heals twice the step without food of its own, once with it (Imperivm.exe 005a1960);
// gbr.exe and Imperivm II four times (HealthInHolderFactor).
void __fastcall HookRegenTick(char *unit, void *edx, int fed)
{
	GlobalSwap factor(addr::kHealthInHolderFactor, fed ? 1 : 2, GameOf(unit) == kImperivm1);
	g_origRegenTick(unit, edx, fed);
}

// An Imperivm I settlement's towers fire at sqrt(every unit in its garrison) x BaseFireRate / 100 (Imperivm.exe
// 005044c0); gbr.exe and Imperivm II count at most 10 that are not soldiers.
int __fastcall HookCalcFireRate(char *settlement)
{
	if (SettlementGame(settlement) != kImperivm1) return g_origCalcFireRate(settlement);
	const char *holder = reinterpret_cast<char **>(addr::kHandleTable)[At<unsigned short>(settlement, kSettlementHolder)];
	if (!holder) return 0;
	// The garrison's handles: a deque of shorts (map +0x34, map size +0x38, first +0x3c, count +0x40), as gbr.exe walks it.
	const unsigned short *const *map = At<unsigned short **>(holder, 0x34);
	const unsigned mapSize = At<unsigned>(holder, 0x38), first = At<unsigned>(holder, 0x3c), count = At<unsigned>(holder, 0x40);
	int units = 0;
	for (unsigned i = first; i < first + count; i++) {
		unsigned block = i >> 3;
		if (block >= mapSize) block -= mapSize;
		if (map && map[block] && reinterpret_cast<char **>(addr::kHandleTable)[map[block][i & 7]]) units++;
	}
	int root = 0;
	while ((root + 1) * (root + 1) <= units) root++;
	return static_cast<int>(static_cast<long long>(root) * *reinterpret_cast<const int *>(addr::kBaseFireRate) / 100);
}

// An Imperivm I or II village or house is repaired however few live in it (Imperivm.exe 004d6390; Imperivm II.exe reads
// MinPopulation for its production only, 005075a0); gbr.exe waits for MinPopulation.
int __fastcall HookBuildingRepair(char *building, void *edx, int amount)
{
	GlobalSwap minPopulation(addr::kMinPopulation, -0x7fffffff, IsI1Object(building));
	return g_origBuildingRepair(building, edx, amount);
}

// An Imperivm I or II settlement produces ProductionFoodBase 20 (gbr.exe 24).
void __fastcall HookUpdateProduction(char *settlement, void *edx, int arg)
{
	GlobalSwap food(addr::kProductionFoodBase, 20, SettlementGame(settlement) != 0);
	g_origUpdateProduction(settlement, edx, arg);
}

typedef int(__cdecl *ScriptNative_t)(int *sp);
typedef const unsigned short *(__fastcall *GetSacrifice_t)(char *unit, void *edx, void *out);
ScriptNative_t g_origFindUnitToHeal = nullptr;
// CVXUnit: the ritual (Sacrifice) it feeds, by vtable +0x12c (gbr.exe Unit::GetSacrifice 005d7bc0); a unit with flag
// 0x4000000 is never a heal target (CVXMap::FindHealTargetInCircle 00512430).
const int kVtSacrifice = 0x12c;
const unsigned kFlagNotHealTarget = 0x4000000;

bool FeedsRitual(char *unit)
{
	unsigned char out[8] = {};
	const unsigned short *h = (*reinterpret_cast<GetSacrifice_t *>(*reinterpret_cast<char **>(unit) + kVtSacrifice))(unit, nullptr, out);
	return h && *h != 0xffff;
}

// Druid::FindUnitToHeal leaves out, for an Imperivm I druid, the druids that feed a ritual, as Imperivm I's
// FindUnit2HTL(1) does (Imperivm.exe 004ddbf0): they lose life all the time, so gbr.exe picks them first, the ported
// druid_heal_ground.vs refuses them and the healer heals nobody while a ritual is near.
int __cdecl HookFindUnitToHeal(int *sp)
{
	const int top = *sp;
	const unsigned args[2] = {reinterpret_cast<unsigned *>(top - 8)[0], reinterpret_cast<unsigned *>(top - 8)[1]};
	const char *druid = reinterpret_cast<char **>(addr::kHandleTable)[args[0] & 0xffff];
	if (!IsI1Object(druid)) return g_origFindUnitToHeal(sp);
	std::vector<char *> hidden;
	int result = 0;
	for (int tries = 0; tries < 8; tries++) {
		*sp = top;
		reinterpret_cast<unsigned *>(top - 8)[0] = args[0];
		reinterpret_cast<unsigned *>(top - 8)[1] = args[1];
		result = g_origFindUnitToHeal(sp);
		const unsigned short found = static_cast<unsigned short>(*reinterpret_cast<unsigned *>(*sp - 8));
		char *unit = found != 0xffff ? reinterpret_cast<char **>(addr::kHandleTable)[found] : nullptr;
		if (!unit || !FeedsRitual(unit) || (At<unsigned>(unit, kObjFlags) & kFlagNotHealTarget)) break;
		At<unsigned>(unit, kObjFlags) |= kFlagNotHealTarget;
		hidden.push_back(unit);
		if (tries == 7) *reinterpret_cast<unsigned *>(*sp - 8) = 0xffff;
	}
	for (char *unit : hidden) At<unsigned>(unit, kObjFlags) &= ~kFlagNotHealTarget;
	return result;
}

typedef void(__fastcall *DamageGarrison_t)(char *settlement, void *edx, int damage);
DamageGarrison_t g_origDamageGarrison = nullptr;
// CVXSettlement: its central building's handle (+0x8c).
const int kSettlementBuilding = 0x8c;

// A blow to an Imperivm I settlement's central building passes to its garrison as it is (Imperivm.exe 005031c0); gbr.exe
// and Imperivm II double it (DamageAmplify 200, read only here).
void __fastcall HookDamageGarrison(char *settlement, void *edx, int damage)
{
	const unsigned short building = settlement ? At<unsigned short>(settlement, kSettlementBuilding) : 0xffff;
	if (building == 0xffff || GameOf(reinterpret_cast<char **>(addr::kHandleTable)[building]) != kImperivm1) {
		g_origDamageGarrison(settlement, edx, damage);
		return;
	}
	int &amplify = *reinterpret_cast<int *>(addr::kDamageAmplify);
	const int game = amplify;
	amplify = 100;
	g_origDamageGarrison(settlement, edx, damage);
	amplify = game;
}

typedef void(__fastcall *UpdateArmyExperience_t)(char *hero);
typedef int(__fastcall *CalcArmyExpBonus_t)(char *hero, void *edx, int expForLevel);
UpdateArmyExperience_t g_origUpdateArmyExperience = nullptr;
CalcArmyExpBonus_t g_origCalcArmyExpBonus = nullptr;
// CVXScriptClass: heroarmyexpgain (+0x660), which gbr.exe reads and never uses.
const int kClassHeroArmyExpGain = 0x660;

// An Imperivm I hero takes a tenth of what its army kills (ExpFromArmyDivider 10; gbr.exe 3), every second.
void __fastcall HookUpdateArmyExperience(char *hero)
{
	if (!IsI1Object(hero)) {
		g_origUpdateArmyExperience(hero);
		return;
	}
	LoadStats();
	int &divider = *reinterpret_cast<int *>(addr::kExpFromArmyDivider);
	const int game = divider;
	divider = (std::max)(1, RulesOf(hero).expFromArmyDivider);
	g_origUpdateArmyExperience(hero);
	divider = game;
}

// What an Imperivm I hero adds to its army's experience: heroarmyexpgain (25, Roman heroes 37) percent of its level's
// experience, plus 2 (Imperivm.exe 004e3d20); gbr.exe gives 40 percent plus Leadership, plus 1.
int __fastcall HookCalcArmyExpBonus(char *hero, void *edx, int expForLevel)
{
	if (!IsI1Object(hero)) return g_origCalcArmyExpBonus(hero, edx, expForLevel);
	const char *cls = At<char *>(hero, kObjClass);
	return At<int>(cls, kClassHeroArmyExpGain) * expForLevel / 100 + 2;
}

bool IsUnit(const void *obj)
{
	return obj && (At<unsigned>(obj, kObjFlags) & kFlagUnit) != 0;
}

// A unit's level; the game computes it with its campaign's experience table while the campaign runs (UseTable).
int LevelOf(const void *unit)
{
	return At<int>(unit, kLevel);
}

// Imperivm I's levels (Imperivm.exe 0050d000): the game's level, with Imperivm I's experience table already in place
// (WatchGame runs before it), and HealthPerLevel (5) more life a level, HeroHealthPerLevel (20) for a hero, which Great
// Battles of Rome does not give.
void __fastcall HookRecalcStats(char *unit)
{
	LoadStats();
	WatchGame(unit);
	g_origRecalcStats(unit);
	if (!unit || !StatsOf(unit)) return;
	const int level = At<int>(unit, kLevel);
	const Rules &rules = RulesOf(unit);
	const int perLevel = (At<unsigned>(unit, kObjFlags) & kFlagHero) ? rules.heroHealthPerLevel : rules.healthPerLevel;
	const int oldMax = At<int>(unit, kMaxLife);
	const int newMax = oldMax + (level - 1) * perLevel;
	if (newMax != oldMax && oldMax > 0) {
		At<int>(unit, kMaxLife) = newMax;
		reinterpret_cast<SetHealth_t>(addr::kSetHealth)(unit, nullptr, static_cast<int>(static_cast<long long>(At<int>(unit, kLife)) * newMax / oldMax));
	}
}

// The defence of `defender` against attacks of `damageType`: Imperivm I's own, or the game's armour of a class of the game.
int DefenseOf(const void *defender, int damageType)
{
	if (const Stats *d = StatsOf(defender)) return damageType == 0 ? d->defenseSlash : damageType == 1 ? d->defensePierce : 0;
	return damageType == 0 ? At<int>(defender, kArmorSlash) : damageType == 1 ? At<int>(defender, kArmorPierce) : 0;
}

// Imperivm I's hit (Imperivm.exe 004fcba0): no dice; each point of (level + rating) over the defender's (level + defence)
// moves the hit 1/20 of the way from minattack to maxattack.
int Hit(const Stats &a, const void *attacker, const void *defender, bool withSkills)
{
	const bool unitA = IsUnit(attacker), unitD = IsUnit(defender);
	int ratA = unitA ? LevelOf(attacker) : 1;
	int ratD = unitD ? LevelOf(defender) + DefenseOf(defender, a.damageType) : 1;
	if (withSkills && unitA && unitD) {
		const int inRow = At<int>(attacker, kHitsInRow);
		const Stats *d = StatsOf(defender);
		if (d && (d->specials & kDefenseSkill)) ratD += inRow;
		if (a.specials & kAttackSkill) ratA += inRow;
	}
	const int bonus = !unitD ? a.bonusBuilding : 100;
	const int t = (std::max)(0, (std::min)(20, 20 + ratA - ratD));
	return (a.minAttack + (a.maxAttack - a.minAttack) * t / 20) * bonus / 100;
}

int GameNow()
{
	const char *game = *reinterpret_cast<char *const *>(addr::kGame);
	return game ? Field<int>(game, layout::kGameClock) : 0;
}

long long Percent(long long value, int percent)
{
	return value * percent / 100;
}

// A cover of mercy (Imperivm II's Sacrifice ritual; gbr.exe keeps it in the same bit) leaves CoverOfMercy percent of a
// change of life, at least 2.
void Mercy(const void *unit, int &delta, const Rules &r)
{
	if (!(At<unsigned>(unit, kCurse) & kFlagMercy) || (delta >= -2 && delta <= 2)) return;
	const int kept = (std::max)(static_cast<int>(Percent(delta < 0 ? -delta : delta, r.coverOfMercy)), 2);
	delta = delta < 0 ? -kept : kept;
}

// Imperivm II's hit (Imperivm II.exe 004fff80, imperivm-re/analisis/i2-vs-i1.md 2.2): the life each side gains. Imperivm I's
// interpolation, a cursed attacker hits its minattack, then the specialities in Imperivm II's order, with MinDamage before
// the death blow and the caps of life at the end. Trample is this same hit given back without specialities. Shallow water
// and Bloodlust, which hit the maxattack, are left to the game.
void Hit2(const Stats &a, const Rules &r, const void *attacker, const void *defender, bool noSpecials, bool cursed, int &da, int &dd)
{
	const bool unitA = IsUnit(attacker), unitD = IsUnit(defender);
	const Stats *d = StatsOf(defender);
	da = dd = 0;
	const int def = unitA && (a.specials & kPenetrating) ? 0 : DefenseOf(defender, a.damageType);
	int ratA = unitA ? LevelOf(attacker) : 1;
	int ratD = unitD ? LevelOf(defender) + def : 1;
	if (unitD && d && (d->specials & kCombatSkill)) ratD += At<int>(defender, kHitsInRow);
	if (unitA && (a.specials & kCombatSkill)) ratA += At<int>(attacker, kHitsInRow);
	const int bonus = unitD ? 100 : a.bonusBuilding;
	const int t = (std::max)(0, (std::min)(20, 20 + ratA - ratD));
	dd = -(cursed ? a.minAttack : a.minAttack + (a.maxAttack - a.minAttack) * t / 20) * bonus / 100;
	if (!unitA || !unitD) return;
	const int lifeA = At<int>(attacker, kLife), maxLifeA = (std::max)(1, At<int>(attacker, kMaxLife));
	const int lifeD = At<int>(defender, kLife), maxLifeD = (std::max)(1, At<int>(defender, kMaxLife));
	if ((At<unsigned>(attacker, kObjFlags) | At<unsigned>(defender, kObjFlags)) & kFlagTraining) {
		if (dd + lifeD < maxLifeD / 10) dd = (std::min)(maxLifeD / 10 - lifeD, 0);
		if (da + lifeA < maxLifeA / 10) da = maxLifeA / 10 - lifeA;
		return;
	}
	if (a.specials & kSpread) dd -= static_cast<int>(static_cast<long long>(maxLifeD - lifeD) * (100 - r.spread) * dd / (static_cast<long long>(maxLifeD) * 100));
	if (a.specials & kRage) {
		const int keep = static_cast<int>(Percent(maxLifeA, r.rage));
		if (lifeA > keep) {
			const int x = (std::min)(lifeA - keep, lifeD);
			da -= x;
			dd -= x;
		}
	}
	if (a.specials & kCharge) {
		const int last = At<int>(attacker, kLastAttack);
		if (last == 0 || last + r.chargeTime < GameNow()) dd += static_cast<int>(Percent(dd, r.chargeBonus));
	}
	if (d && (d->specials & kFullArmor)) dd += r.fullArmor;
	if (dd > -r.minDamage) dd = -r.minDamage;
	if (!noSpecials) {
		const bool melee = !reinterpret_cast<HasProjectileClass_t>(addr::kHasProjectileClass)(attacker);
		if ((a.specials & kDeathBlow) && lifeD <= Percent(maxLifeD, r.deathBlow) - dd) dd = -lifeD;
		if (d && (d->specials & kSpike) && melee) da += static_cast<int>(Percent(dd, r.spike));
		if ((a.specials & kBleeding) && !(At<unsigned>(defender, kObjFlags) & kFlagHero)) dd = -static_cast<int>(Percent(maxLifeD, r.bleeding));
		if (d && (d->specials & kDefensiveStand) && At<int>(attacker, kHitsInRow) == 0) {
			da = dd = 0;
			return;
		}
		if (a.specials & kVampire) da += static_cast<int>(Percent(-dd, r.vampire));
		// Imperivm II also asks that the attacker be in the defender's reach and not hidden; a melee attacker is in it.
		if (d && (d->specials & kTrample) && melee && d->maxAttack > 0 && !(At<unsigned>(attacker, kObjFlags) & kFlagHidden)) {
			int backA = 0, backD = 0;
			Hit2(*d, r, defender, attacker, true, (At<unsigned>(defender, kCurse) & kFlagCursed) != 0, backA, backD);
			da += static_cast<int>(Percent(backD, r.trample));
		}
		if (-dd > lifeD) dd = -lifeD;
		if (-da > lifeA) da = -lifeA;
	}
	Mercy(defender, dd, r);
	Mercy(attacker, da, r);
}

// The kill an attack of a campaign's class is about to make: its experience once the game has given its own
// (HookResolveAttack).
void NoteKill(void *attacker, void *defender, int dd, const Rules &rules)
{
	if (-dd < At<int>(defender, kLife) || !IsUnit(attacker)) return;
	const unsigned short leaderHandle = At<unsigned short>(attacker, kLeaderHandle);
	g_kill.attacker = static_cast<char *>(attacker);
	g_kill.defender = static_cast<char *>(defender);
	g_kill.leader = leaderHandle != 0xffff ? reinterpret_cast<char **>(addr::kHandleTable)[leaderHandle] : nullptr;
	g_kill.expBefore = At<int>(attacker, kExperience);
	g_kill.armyExpBefore = g_kill.leader ? At<int>(g_kill.leader, kArmyExp) : 0;
	g_kill.gain = LevelOf(defender) / (std::max)(1, rules.levelExpDivider) + 1;
}

}

void InstallI1Engine(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kResolveMethods);
	if (HookCreate(target, reinterpret_cast<void *>(&HookResolveMethods), reinterpret_cast<void **>(&g_origResolveMethods)) != MH_OK || HookEnable(target) != MH_OK) {
		LogE("game", "could not hook the classes' behaviours: the classes of an Imperivm I campaign also run their Great Battles of Rome parent's");
		return;
	}
	LogD("game", "the classes of an Imperivm I campaign run only their own behaviours");
	target = reinterpret_cast<void *>(addr::kRecalcStats);
	if (HookCreate(target, reinterpret_cast<void *>(&HookRecalcStats), reinterpret_cast<void **>(&g_origRecalcStats)) != MH_OK || HookEnable(target) != MH_OK)
		LogE("game", "could not hook the units' stats: the classes of an Imperivm I campaign keep Great Battles of Rome's levels");

	const unsigned char jumpIfVisible[] = {0x75, 0x09}, jumpAlways[] = {0xeb, 0x09};
	g_revealOnAttack.Prepare(addr::kRevealOnAttackJump, jumpIfVisible, jumpAlways, sizeof(jumpIfVisible));
	target = reinterpret_cast<void *>(addr::kPerformAttack);
	if (!g_revealOnAttack.ready || HookCreate(target, reinterpret_cast<void *>(&HookPerformAttack), reinterpret_cast<void **>(&g_origPerformAttack)) != MH_OK ||
		HookEnable(target) != MH_OK)
		LogE("game", "could not hook the attacks: a hidden unit of an Imperivm I campaign shows itself when it attacks");

	const unsigned char lock1600[] = {0x40, 0x06, 0x00, 0x00}, lock500[] = {0xf4, 0x01, 0x00, 0x00};
	g_drainLock.Prepare(addr::kDrainLockTime, lock1600, lock500, sizeof(lock1600));
	if (!g_drainLock.ready) LogW("game", "the ghosts of an Imperivm I campaign keep their victims as long as Great Battles of Rome's");
	target = reinterpret_cast<void *>(addr::kGhostDamageCallback);
	if (HookCreate(target, reinterpret_cast<void *>(&HookGhostDamage), reinterpret_cast<void **>(&g_origGhostDamage)) != MH_OK || HookEnable(target) != MH_OK)
		LogE("game", "could not hook the ghosts: the ghosts of an Imperivm I campaign leave their victims at Great Battles of Rome's health");
	target = reinterpret_cast<void *>(addr::kDamageRandomGarrisonUnit);
	if (HookCreate(target, reinterpret_cast<void *>(&HookDamageGarrison), reinterpret_cast<void **>(&g_origDamageGarrison)) != MH_OK || HookEnable(target) != MH_OK)
		LogE("game", "could not hook the garrisons: an Imperivm I settlement's garrison takes double damage");
	target = reinterpret_cast<void *>(addr::kUpdateArmyExperience);
	void *bonus = reinterpret_cast<void *>(addr::kCalcArmyExpBonus);
	if (HookCreate(target, reinterpret_cast<void *>(&HookUpdateArmyExperience), reinterpret_cast<void **>(&g_origUpdateArmyExperience)) != MH_OK ||
		HookCreate(bonus, reinterpret_cast<void *>(&HookCalcArmyExpBonus), reinterpret_cast<void **>(&g_origCalcArmyExpBonus)) != MH_OK || HookEnable(target) != MH_OK ||
		HookEnable(bonus) != MH_OK)
		LogE("game", "could not hook the heroes' experience: Imperivm I heroes level up as Great Battles of Rome's");
	target = reinterpret_cast<void *>(addr::kResolveAttack);
	if (HookCreate(target, reinterpret_cast<void *>(&HookResolveAttack), reinterpret_cast<void **>(&g_origResolveAttack)) != MH_OK || HookEnable(target) != MH_OK)
		LogE("game", "could not hook the kills: Imperivm I units gain Great Battles of Rome's experience");
	target = reinterpret_cast<void *>(addr::kGetBonus);
	if (HookCreate(target, reinterpret_cast<void *>(&HookGetBonus), reinterpret_cast<void **>(&g_origGetBonus)) != MH_OK || HookEnable(target) != MH_OK)
		LogE("game", "could not hook the attack bonuses: the Imperivm I catapult does Great Battles of Rome's damage to buildings");
	if (InstallGameLoadHook(true)) OnGameLoading(&OnLoading);

	const struct {
		uintptr_t at;
		void *hook;
		void **orig;
		const char *lost;
	} settlementHooks[] = {
		{addr::kRegenTick, reinterpret_cast<void *>(&HookRegenTick), reinterpret_cast<void **>(&g_origRegenTick), "units in a building heal as fast as Great Battles of Rome's"},
		{addr::kCalcFireRate, reinterpret_cast<void *>(&HookCalcFireRate), reinterpret_cast<void **>(&g_origCalcFireRate), "towers count at most 10 peasants of the garrison"},
		{addr::kBuildingRepair, reinterpret_cast<void *>(&HookBuildingRepair), reinterpret_cast<void **>(&g_origBuildingRepair), "villages and houses with few people are not repaired"},
		{addr::kUpdateProduction, reinterpret_cast<void *>(&HookUpdateProduction), reinterpret_cast<void **>(&g_origUpdateProduction), "settlements produce Great Battles of Rome's food"},
		{addr::kScriptDruidFindUnitToHeal, reinterpret_cast<void *>(&HookFindUnitToHeal), reinterpret_cast<void **>(&g_origFindUnitToHeal), "druids stop healing near a ritual"},
	};
	for (const auto &h : settlementHooks) {
		target = reinterpret_cast<void *>(h.at);
		if (HookCreate(target, h.hook, h.orig) != MH_OK || HookEnable(target) != MH_OK) LogE("game", "could not hook the settlements: in an Imperivm I campaign %s", h.lost);
	}
}

I1AttackState I1BeforeAttack(const void *attacker)
{
	I1AttackState s;
	if (IsUnit(attacker)) {
		s.cursed = (At<unsigned>(attacker, kCurse) & kFlagCursed) != 0;
		s.parrying = At<int>(attacker, kParry) != 0;
	}
	return s;
}

void I1AfterAttack(const I1AttackState &before, void *attacker, void *defender, int *dLifeAtt, int *dLifeDef)
{
	if (!attacker || !defender || !dLifeAtt || !dLifeDef || before.parrying) return;
	LoadStats();
	const Stats *a = StatsOf(attacker);
	if (!a || a->maxAttack <= 0) return;
	const Rules &rules = RulesOf(attacker);
	const Stats *d = StatsOf(defender);
	const bool unitD = IsUnit(defender);
	// gbr.exe's curse makes the blow miss and goes; Imperivm I had none, Imperivm II's hits the minattack. A curse still on
	// the attacker is a blow dodged before it.
	const bool curseSpent = before.cursed && !(At<unsigned>(attacker, kCurse) & kFlagCursed);
	if (before.cursed && (GameOf(attacker) != kImperivm2 || !curseSpent)) return;
	// A class of the game that dodged or stopped the blow with its own skills keeps it so.
	if (unitD && !d && *dLifeDef == 0 && !curseSpent) return;
	if (GameOf(attacker) == kImperivm2) {
		int da = 0, dd = 0;
		Hit2(*a, rules, attacker, defender, false, curseSpent, da, dd);
		if (!unitD) dd = -(std::min)(-dd, At<int>(defender, kLife));
		*dLifeDef = dd;
		*dLifeAtt = da;
		if (unitD) NoteKill(attacker, defender, dd, rules);
		return;
	}
	const int lifeD = At<int>(defender, kLife), maxLifeD = (std::max)(1, At<int>(defender, kMaxLife));
	if (!unitD) {
		*dLifeDef = -(std::min)(Hit(*a, attacker, defender, false), lifeD);
		*dLifeAtt = 0;
		return;
	}
	const int inRow = At<int>(attacker, kHitsInRow);
	if (d && (d->specials & kDefensiveStand) && inRow == 0) {
		*dLifeDef = 0;
		*dLifeAtt = 0;
		return;
	}
	const int hit = Hit(*a, attacker, defender, true);
	int dd = -hit, da = 0;
	const int lifeA = At<int>(attacker, kLife), maxLifeA = (std::max)(1, At<int>(attacker, kMaxLife));
	if ((a->specials & kDeathBlow) && lifeD <= maxLifeD * rules.deathBlow / 100 + hit) dd = -lifeD;
	if (a->specials & kBerserk) dd += static_cast<int>(static_cast<long long>(maxLifeA - lifeA) * dd * rules.berserk / (static_cast<long long>(maxLifeA) * 100));
	if (a->specials & kSpread) dd -= static_cast<int>(static_cast<long long>(maxLifeD - lifeD) * (100 - rules.spread) * dd / (static_cast<long long>(maxLifeD) * 100));
	if (a->specials & kCharge) {
		if (At<int>(attacker, kLastAttack) + rules.chargeTime < GameNow()) dd += dd * rules.chargeBonus / 100;
	}
	if (-dd > lifeD) dd = -lifeD;
	const bool melee = !reinterpret_cast<HasProjectileClass_t>(addr::kHasProjectileClass)(attacker);
	if (d && (d->specials & kSpike) && melee) da += dd * rules.spike / 100;
	// Trample is the defender striking back at half its hit; Imperivm I also asks that the attacker be in its reach, which a
	// melee attacker is.
	if (d && (d->specials & kTrample) && melee && d->maxAttack > 0) da -= Hit(*d, defender, attacker, false) * rules.trample / 100;
	if (a->specials & kVampire) da -= dd * rules.vampire / 100;
	if (a->specials & kSacrificing) da += dd * rules.sacrificing / 100;
	if (dd >= 0) dd = -1;
	*dLifeDef = dd;
	*dLifeAtt = da;
	NoteKill(attacker, defender, dd, rules);
}
