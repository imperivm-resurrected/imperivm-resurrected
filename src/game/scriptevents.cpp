// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "mods/mods.h"
#include "mods/modvars.h"
#include "game/scriptevents.h"

#include <atomic>
#include <string>
#include <vector>
#include "addr/state.h"
#include "addr/script.h"
#include "addr/world.h"

namespace {
typedef void(__fastcall *StartVictory_t)(void *map);
typedef const char *(__fastcall *LoadScriptFile_t)(void *lib, void *edx, const char *name, const char *path, const char *sig, int reload);
typedef int(__cdecl *StartScriptByName_t)(void *host, int slot, const char *name, int delay, int param, ...);
typedef const char *(__cdecl *RunScriptByName_t)(void *lib, const char *name, ...);
typedef void(__cdecl *ProcessDeath_t)(void *obj);
typedef void(__fastcall *OnKilled_t)(void *killer, void *edx, void *victim);

StartVictory_t g_origStartVictory = nullptr;
ProcessDeath_t g_origProcessDeath = nullptr;
OnKilled_t g_origOnKilled = nullptr;
typedef void(__fastcall *SettlementSetPlayer_t)(void *self, void *edx, int player);
typedef void(__cdecl *DeclareDefeated_t)(void *players);
SettlementSetPlayer_t g_origSettlementSetPlayer = nullptr;
DeclareDefeated_t g_origDeclareDefeated = nullptr;

struct Event {
	const char *name;
	const char *sig;
	int slot;
};
const Event kOnGameStart = {"OnGameStart", "void", 20};

bool FileExists(const std::string &p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

void RunEvent(void *host, const Event &e)
{
	std::vector<std::pair<std::string, std::string>> scripts;
	for (const std::string &dir : ModsMountedDirs()) {
		if (FileExists(dir + "\\events\\" + e.name + ".vs")) scripts.push_back({"mods/" + ModsDirVfsName(dir) + "/events/" + e.name + ".vs", ModsPackFolder(dir)});
	}
	if (FileExists(GameDir() + "\\imperivm-resurrected\\events\\" + e.name + ".vs")) scripts.push_back({std::string("imperivm-resurrected/events/") + e.name + ".vs", "imperivm-resurrected\\events"});
	void *lib = *reinterpret_cast<void **>(addr::kScriptLibrary);
	for (const auto &s : scripts) {
		const char *err = lib ? reinterpret_cast<LoadScriptFile_t>(addr::kLoadScriptFile)(lib, nullptr, s.first.c_str(), s.first.c_str(), e.sig, 0) : "no script library";
		if (err) {
			LogE("event", "%s (%s): the script does not compile or cannot be read: %s", e.name, s.second.c_str(), err);
			continue;
		}
		const int r = reinterpret_cast<StartScriptByName_t>(addr::kStartScriptByName)(host, e.slot, s.first.c_str(), 0, 100);
		if (r) LogE("event", "%s (%s): could not start the script (code %d)", e.name, s.second.c_str(), r);
		else LogI("event", "%s (%s): script started", e.name, s.second.c_str());
	}
}

struct ObjScript {
	std::string path, who;
	bool ok = false;
	int errors = 0;
};

struct ObjEvent {
	const char *name;
	const char *sig;
	std::vector<ObjScript> scripts;
	bool ready = false;
	int depth = 0;
};
ObjEvent g_onDie = {"OnObjectDie", "void, Obj victim"};
ObjEvent g_onKill = {"OnObjectKill", "void, Obj killer, Obj victim"};
ObjEvent g_onCapture = {"OnSettlementCaptured", "void, Obj building, int oldPlayer, int newPlayer"};
ObjEvent g_onDefeat = {"OnPlayerDefeated", "void, int player"};
ObjEvent g_onAttack = {"OnAttackResolved", "void, Obj attacker, Obj victim, int damage"};

struct AttackState {
	bool active = false;
	int damage = 0;
	bool changed = false;
} g_attack;

void PrepareObjEvent(ObjEvent &e)
{
	e.ready = true;
	const std::string file = std::string("events\\") + e.name + ".vs";
	for (const std::string &dir : ModsMountedDirs()) {
		if (FileExists(dir + "\\" + file)) e.scripts.push_back({"mods/" + ModsDirVfsName(dir) + "/events/" + e.name + ".vs", ModsPackFolder(dir)});
	}
	if (FileExists(GameDir() + "\\imperivm-resurrected\\" + file)) e.scripts.push_back({std::string("imperivm-resurrected/events/") + e.name + ".vs", "imperivm-resurrected\\events"});
	void *lib = *reinterpret_cast<void **>(addr::kScriptLibrary);
	for (ObjScript &d : e.scripts) {
		const char *err = lib ? reinterpret_cast<LoadScriptFile_t>(addr::kLoadScriptFile)(lib, nullptr, d.path.c_str(), d.path.c_str(), e.sig, 0) : "no script library";
		if (err) LogE("event", "%s (%s): the script does not compile or cannot be read: %s", e.name, d.who.c_str(), err);
		else LogD("event", "%s (%s): script prepared", e.name, d.who.c_str());
		d.ok = err == nullptr;
	}
}

unsigned HandleOf(void *obj)
{
	return *reinterpret_cast<unsigned short *>(static_cast<char *>(obj) + 8);
}

void FireObjEvent(ObjEvent &e, void *a, void *b)
{
	if (!e.ready) PrepareObjEvent(e);
	if (e.scripts.empty() || e.depth >= 4) return;
	void *lib = *reinterpret_cast<void **>(addr::kScriptLibrary);
	const unsigned ha = HandleOf(a), hb = b ? HandleOf(b) : 0;
	e.depth++;
	for (ObjScript &d : e.scripts) {
		if (!d.ok || !lib) continue;
		const char *err = b ? reinterpret_cast<RunScriptByName_t>(addr::kRunScriptByName)(lib, d.path.c_str(), ha, 0u, hb, 0u)
							: reinterpret_cast<RunScriptByName_t>(addr::kRunScriptByName)(lib, d.path.c_str(), ha, 0u);
		if (err && d.errors++ < 5) LogE("event", "%s (%s): the script ended with an error: %s", e.name, d.who.c_str(), err);
	}
	e.depth--;
}

void FireWords(ObjEvent &e, const unsigned *w, int n)
{
	if (!e.ready) PrepareObjEvent(e);
	if (e.scripts.empty() || e.depth >= 4) return;
	void *lib = *reinterpret_cast<void **>(addr::kScriptLibrary);
	auto run = reinterpret_cast<RunScriptByName_t>(addr::kRunScriptByName);
	e.depth++;
	for (ObjScript &d : e.scripts) {
		if (!d.ok || !lib) continue;
		const char *err = n == 1 ? run(lib, d.path.c_str(), w[0])
			: n == 4 ? run(lib, d.path.c_str(), w[0], w[1], w[2], w[3])
					 : run(lib, d.path.c_str(), w[0], w[1], w[2], w[3], w[4]);
		if (err && d.errors++ < 5) LogE("event", "%s (%s): the script ended with an error: %s", e.name, d.who.c_str(), err);
	}
	e.depth--;
}

typedef void(__cdecl *CalcAttackOutcome_t)(void *attacker, void *defender, int *dLifeAtt, int *dLifeDef, int *dEnergyAtt, int *dEnergyDef,
	int *learn, void *xp, int *feedbackAtt, int *feedbackDef);
CalcAttackOutcome_t g_origCalcAttack = nullptr;

void __cdecl HookCalcAttackOutcome(void *attacker, void *defender, int *dLifeAtt, int *dLifeDef, int *dEnergyAtt, int *dEnergyDef, int *learn,
	void *xp, int *feedbackAtt, int *feedbackDef)
{
	g_origCalcAttack(attacker, defender, dLifeAtt, dLifeDef, dEnergyAtt, dEnergyDef, learn, xp, feedbackAtt, feedbackDef);
	if ((g_onAttack.ready && g_onAttack.scripts.empty()) || !attacker || !defender || !dLifeDef || g_attack.active) return;
	const int original = *dLifeDef;
	if (original >= 0) return;
	g_attack.active = true;
	g_attack.damage = -original;
	g_attack.changed = false;
	const unsigned w[4] = {HandleOf(attacker), 0u, HandleOf(defender), 0u};
	const unsigned args[6] = {w[0], w[1], w[2], w[3], static_cast<unsigned>(g_attack.damage), 0u};
	FireWords(g_onAttack, args, 5);
	if (g_attack.changed) *dLifeDef = -g_attack.damage;
	g_attack.active = false;
}

bool GameRunning()
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	return game && *reinterpret_cast<int *>(game + 0x1258) > 0;
}

void __fastcall HookSettlementSetPlayer(void *self, void *edx, int newPlayer)
{
	char *s = static_cast<char *>(self);
	char *stats = *reinterpret_cast<char **>(s + 0x90);
	const int oldPlayer = stats ? *reinterpret_cast<int *>(stats + 8) : -1;
	const unsigned short building = *reinterpret_cast<unsigned short *>(s + 0x8c);
	g_origSettlementSetPlayer(self, edx, newPlayer);
	if (oldPlayer == newPlayer || building == 0xffff || !GameRunning()) return;
	const unsigned w[4] = {building, 0u, static_cast<unsigned>(oldPlayer >= 0 && oldPlayer < 16 ? oldPlayer + 1 : 0),
		static_cast<unsigned>(newPlayer >= 0 && newPlayer < 16 ? newPlayer + 1 : 0)};
	FireWords(g_onCapture, w, 4);
}

void __cdecl HookDeclareDefeated(void *vec)
{
	std::vector<void *> alive;
	char *begin = vec ? *reinterpret_cast<char **>(static_cast<char *>(vec) + 4) : nullptr;
	char *end = vec ? *reinterpret_cast<char **>(static_cast<char *>(vec) + 8) : nullptr;
	for (char *p = begin; p && end && p < end && alive.size() < 16; p += 4) {
		char *player = *reinterpret_cast<char **>(p);
		if (player && *reinterpret_cast<int *>(player + 0x290) == 1 && *reinterpret_cast<int *>(player + 8) < 8) alive.push_back(player);
	}
	g_origDeclareDefeated(vec);
	for (void *player : alive) {
		char *p = static_cast<char *>(player);
		if (*reinterpret_cast<int *>(p + 0x290) != 2) continue;
		const unsigned w[1] = {static_cast<unsigned>(*reinterpret_cast<int *>(p + 8) + 1)};
		FireWords(g_onDefeat, w, 1);
	}
}

void __cdecl HookProcessDeath(void *obj)
{
	if (obj && *reinterpret_cast<int *>(static_cast<char *>(obj) + 0xbc) == 0 && *reinterpret_cast<int *>(addr::kMutatingUnit) == 0) FireObjEvent(g_onDie, obj, nullptr);
	g_origProcessDeath(obj);
}

void __fastcall HookOnKilled(void *killer, void *edx, void *victim)
{
	if (killer && victim) FireObjEvent(g_onKill, killer, victim);
	g_origOnKilled(killer, edx, victim);
}

void __fastcall HookStartVictory(void *map)
{
	ModVarsClear();
	g_origStartVictory(map);
	RunEvent(map, kOnGameStart);
}

}

void InstallScriptEvents(bool knownExe)
{
	if (!knownExe) return;
	if (HookCreate(reinterpret_cast<void *>(addr::kStartVictoryScripts), reinterpret_cast<void *>(&HookStartVictory),
			reinterpret_cast<void **>(&g_origStartVictory)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kStartVictoryScripts)) != MH_OK) {
		LogE("script", "events: could not hook StartVictoryScripts");
		return;
	}
	if (HookCreate(reinterpret_cast<void *>(addr::kProcessDeath), reinterpret_cast<void *>(&HookProcessDeath), reinterpret_cast<void **>(&g_origProcessDeath)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kProcessDeath)) != MH_OK) {
		LogE("script", "events: could not hook ProcessDeath (OnObjectDie will not be available)");
	}
	if (HookCreate(reinterpret_cast<void *>(addr::kOnKilled), reinterpret_cast<void *>(&HookOnKilled), reinterpret_cast<void **>(&g_origOnKilled)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kOnKilled)) != MH_OK) {
		LogE("script", "events: could not hook OnKilled (OnObjectKill will not be available)");
	}
	if (HookCreate(reinterpret_cast<void *>(addr::kSettlementSetPlayer), reinterpret_cast<void *>(&HookSettlementSetPlayer), reinterpret_cast<void **>(&g_origSettlementSetPlayer)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kSettlementSetPlayer)) != MH_OK) {
		LogE("script", "events: could not hook Settlement::SetPlayer (OnSettlementCaptured will not be available)");
	}
	if (HookCreate(reinterpret_cast<void *>(addr::kDeclareDefeated), reinterpret_cast<void *>(&HookDeclareDefeated), reinterpret_cast<void **>(&g_origDeclareDefeated)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kDeclareDefeated)) != MH_OK) {
		LogE("script", "events: could not hook DeclarePlayersDefeated (OnPlayerDefeated will not be available)");
	}
	if (HookCreate(reinterpret_cast<void *>(addr::kCalcAttackOutcome), reinterpret_cast<void *>(&HookCalcAttackOutcome), reinterpret_cast<void **>(&g_origCalcAttack)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kCalcAttackOutcome)) != MH_OK) {
		LogE("script", "events: could not hook CalcAttackOutcome (OnAttackResolved will not be available)");
	}
	LogI("script", "events: hooked");
}

int ModAttackDamage()
{
	return g_attack.active ? g_attack.damage : 0;
}

void ModSetAttackDamage(int damage)
{
	if (!g_attack.active) return;
	g_attack.damage = damage < 0 ? 0 : damage;
	g_attack.changed = true;
}
