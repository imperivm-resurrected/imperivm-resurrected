// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <cstdint>

#include "net/netdrop.h"
#include "net/leagueclient.h"
#include "net/spectator.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "addr/media.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "core/uiport.h"
#include "game/gamestats.h"
#include "net/steamnet.h"

namespace {
const unsigned kGameConsole = 0x44cc;

typedef void(__fastcall *Split_t)(void *cmd);
typedef void(__cdecl *CreateAI_t)(int player, char *profile, short difficulty);
typedef int(__cdecl *IsDemo_t)();
typedef void(__cdecl *VoidFn_t)();
typedef void(__fastcall *QueueCommand_t)(void *game, void *edx, int *cmd);
VoidFn_t g_origEndDlg = nullptr;
QueueCommand_t g_origQueue = nullptr;
bool g_windowClosing = false;

void __cdecl HookEndDlg()
{
	const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
	g_windowClosing = from >= addr::kWindowHandler && from < addr::kWindowHandler + 0x400;
	g_origEndDlg();
	g_windowClosing = false;
}

void __fastcall HookQueueCommand(void *game, void *edx, int *cmd)
{
	if (g_windowClosing && cmd && static_cast<uintptr_t>(cmd[0]) == addr::kWantsToQuitVtable) {
		LogI("match", "online game: the window is closing; no leave notice is sent (the others will see it as a drop)");
		return;
	}
	g_origQueue(game, edx, cmd);
}

Split_t g_origSplit = nullptr;
Split_t g_origWantsToQuit = nullptr;
Split_t g_origSurrender = nullptr;
typedef char *(__cdecl *LocFormat_t)(void *db, char *out, const char *key, ...);
typedef void(__fastcall *LocFormatV_t)(void *db, void *edx, const char *context, char *out, const char *key, va_list args);
typedef void(__cdecl *ConsolePrintf_t)(void *console, const char *fmt, ...);
LocFormat_t g_origLocFormat = nullptr;
int g_inQuit = 0;
CreateAI_t g_origCreateAI = nullptr;
int g_inSplit = 0;

std::mutex g_lock;
std::set<int> g_dropped;
// The players of a ranked 2v2 the AI plays for since they dropped: they may come back, and then it goes.
std::set<int> g_dropAi;

std::atomic<bool> g_dropRules{false};
bool g_hooksReady = false;

// The league's drop rules (a ranked or a cup game) hold for the game played or watched: its players read them once when its network starts
// (NetDropNetworkStarted), a spectator of the broadcast from the spectator room, which copies the game room's "drop". Whatever changes the
// game under them must be the same in all those copies.
bool TeamRules(bool demo)
{
	// Only while an online game's network runs: the value of the last one stays behind after it.
	if (!demo) return g_dropRules && *reinterpret_cast<void **>(addr::kTransport) != nullptr;
	return SpectatorViewing() && SteamCurrentRoom().info.dropAi;
}

std::string ReadString(const char *s)
{
	const unsigned size = *reinterpret_cast<const unsigned *>(s + 0x14);
	const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
	const char *text = cap < 0x10 ? s + 4 : *reinterpret_cast<const char *const *>(s + 4);
	return text && size < 256 ? std::string(text, size) : std::string();
}

std::string StripAiSuffix(char *s)
{
	const std::string name = ReadString(s);
	const size_t open = name.find_last_of('(');
	if (open == std::string::npos || name.back() != ')' || name.size() - open > 6) return name;
	std::string clean = name.substr(0, open);
	const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
	char *text = cap < 0x10 ? s + 4 : *reinterpret_cast<char *const *>(s + 4);
	text[clean.size()] = 0;
	*reinterpret_cast<unsigned *>(s + 0x14) = static_cast<unsigned>(clean.size());
	while (!clean.empty() && clean.back() == ' ') clean.pop_back();
	return clean;
}

// The players still in the lockstep (the gate's mask) and the tick: each computer decides a drop alone, so a notice on one computer only, or
// at another tick, is what to look for next to a desync.
int LockstepMask(int &tick)
{
	const char *t = *reinterpret_cast<const char *const *>(addr::kTransport);
	const char *gate = t ? *reinterpret_cast<const char *const *>(t + 0x50) : nullptr;
	tick = t ? *reinterpret_cast<const int *>(t + 0x54) : -1;
	return gate ? *reinterpret_cast<const uint8_t *>(gate + 0x108) : -1;
}

void __fastcall HookSplit(void *cmd)
{
	int tick = -1;
	const int before = LockstepMask(tick);
	g_inSplit++;
	g_origSplit(cmd);
	g_inSplit--;
	int tickAfter = -1;
	const int after = LockstepMask(tickAfter);
	LogI("match", "online game: drop notice at tick %d (players %02x; the lockstep had %02x, now %02x)", tick,
		*reinterpret_cast<unsigned *>(static_cast<char *>(cmd) + 0xc), static_cast<unsigned>(before) & 0xff, static_cast<unsigned>(after) & 0xff);
}

void __fastcall HookWantsToQuit(void *cmd)
{
	g_inQuit++;
	g_origWantsToQuit(cmd);
	g_inQuit--;
}

std::string PlayerName(int player)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || player < 0 || player >= 16) return std::string();
	return ReadString(game + 0x12cc + player * 800 + 0x90);
}

void ConsoleMessage(const std::string &text)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	void *console = game ? *reinterpret_cast<void **>(game + kGameConsole) : nullptr;
	if (console) reinterpret_cast<ConsolePrintf_t>(addr::kConsolePrintf)(console, "%s", text.c_str());
}

// The other player of that one's alliance, from the game's state; -1 when there is none.
int AllyOf(int index)
{
	std::vector<StatsLivePlayer> players;
	uint32_t clock = 0;
	if (!GameStatsLive(players, clock)) return -1;
	int team = -1;
	for (const StatsLivePlayer &p : players) {
		if (p.info.index == index) team = p.info.team;
	}
	for (const StatsLivePlayer &p : players) {
		if (p.playable && p.info.index != index && p.info.team == team) return p.info.index;
	}
	return -1;
}

// A ranked 2v2's player surrenders while the AI plays for their partner, who dropped or left: the team gives up, so the partner's army
// is defeated with them, as the surrender itself does (the game's slot 0x48, OnPlayerGameOver). Every computer, and a spectator of the
// broadcast, runs the surrender on the same tick and decides this by the game's state, so all of them do it.
void SurrenderTeam(int index, bool demo)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || index < 0 || index >= 16 || !TeamRules(demo)) return;
	const int partner = AllyOf(index);
	if (partner < 0 || partner >= 16) return;
	char *slot = game + partner * 800 + 0x12cc;
	if (!*reinterpret_cast<void **>(slot + 0x88) || *reinterpret_cast<int *>(slot + 0x290) != 1) return;
	int who = partner;
	void **vt = *reinterpret_cast<void ***>(game);
	reinterpret_cast<void(__fastcall *)(void *, void *, int *, int)>(vt[0x48 / 4])(game, nullptr, &who, 1);
	LeagueMatchSurrendered(partner);
	LogI("match", "ranked 2v2: player %d surrenders with the partner the AI plays for (%d)", index, partner);
}

void __fastcall HookSurrender(void *cmd)
{
	char *player = *reinterpret_cast<char **>(static_cast<char *>(cmd) + 4);
	const int index = player ? *reinterpret_cast<int *>(player + 8) : -1;
	const bool demo = reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0;
	if (!demo) {
		const std::string name = PlayerName(index);
		LogI("match", "online game: player %d (%s) surrenders", index, name.c_str());
		LeagueMatchSurrendered(index);
		ConsoleMessage((name.empty() ? std::string("A player") : name) + Tr(" has surrendered."));
	}
	g_origSurrender(cmd);
	SurrenderTeam(index, demo);
}

char *__cdecl HookLocFormat(void *db, char *out, const char *key, ...)
{
	va_list args;
	va_start(args, key);
	if ((g_inSplit > 0 || g_inQuit > 0) && key && out && strncmp(key, "Player %s1 dropped", 18) == 0 &&
		!reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) {
		const char *raw = va_arg(args, const char *);
		std::string name = raw ? raw : "";
		if (name.empty()) name = "A player";
		// Under the drop rules the AI plays for whoever drops while they come back; nobody takes the place of who leaves a 1v1, the AI keeps
		// the army of who leaves a ranked 2v2.
		const bool rules = TeamRules(false);
		std::string text;
		if (g_inQuit > 0) text = name + Tr(rules && !LeagueMatchRankedTeam() ? " has left the game." : " has left the game; their army is controlled by the AI.");
		else if (rules) text = TrFmt("%s has dropped: the AI controls them. They have %s to come back.", {name, LeagueClockText(kLeagueTeamDropWaitMs)});
		else text = name + Tr(" has disconnected. They can rejoin the game from Multiplayer.");
		strncpy_s(out, 1024, text.c_str(), _TRUNCATE);
		va_end(args);
		return out;
	}
	reinterpret_cast<LocFormatV_t>(addr::kLocFormatV)(db, nullptr, "", out, key, args);
	va_end(args);
	return out;
}

void __cdecl HookCreateAI(int player, char *profile, short difficulty)
{
	void *game = *reinterpret_cast<void **>(addr::kGame);
	if (game && player >= 0 && player < 16) {
		char *slot = static_cast<char *>(game) + player * 800 + 0x12cc;
		LogI("game", "ai: CreatePlayerAI(player %d, human=%d, previous AI %p) from %p%s", player, *reinterpret_cast<int *>(slot + 0x290),
			*reinterpret_cast<void **>(slot + 0x88), _ReturnAddress(), g_inSplit > 0 ? " [dropped]" : "");
	}
	// A live broadcast is the replay of a game played with this rule, so its drops must not bring the AI either, or
	// the spectator's copy drifts from the game. Saved replays keep the game's behaviour: they may come from without the mod.
	const bool demo = reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0;
	// Under the drop rules the AI plays for whoever drops, and for whoever quits only with a partner to play beside (a 2v2): a 1v1's quitter
	// has lost. The partner is read from the game's state, the same on every computer and in the broadcast.
	const bool rules = (g_inSplit > 0 || g_inQuit > 0) && TeamRules(demo);
	const bool team = rules && (g_inSplit > 0 || AllyOf(player) >= 0);
	if ((g_inSplit > 0 || (g_inQuit > 0 && rules)) && demo && SpectatorViewing() && game && player >= 0 && player < 16 && !team) {
		LogI("match", "broadcast: player %d %s the game; the AI does not replace them", player, g_inSplit > 0 ? "dropped in" : "left");
		return;
	}
	// Under the drop rules the AI plays for whoever drops, as the game itself does, and their return takes it away (NetDropAiRelease); a
	// 2v2's player who quits keeps it, as in a normal game. Its broadcast does the same.
	if (team && game && player >= 0 && player < 16) {
		char *slot = static_cast<char *>(game) + player * 800 + 0x12cc;
		const std::string name = ReadString(slot + 0x90);
		bool first;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			first = g_dropped.insert(player).second;
			if (g_inSplit > 0) g_dropAi.insert(player);
			else g_dropAi.erase(player);
		}
		if (g_inQuit > 0 && !demo) LeagueMatchPlayerQuit(player);
		if (first) {
			LogI("match", "league game: player %d (%s) %s; the AI plays for them%s", player, name.c_str(), g_inSplit > 0 ? "dropped" : "left",
				g_inSplit > 0 ? " until they come back" : "");
		}
		g_origCreateAI(player, profile, difficulty);
		return;
	}
	// Nobody takes the place of the one who leaves a league 1v1: the game is the other's.
	const bool rankedQuit = g_inQuit > 0 && rules;
	if ((g_inSplit > 0 || rankedQuit) && game && player >= 0 && player < 16 && !demo) {
		char *slot = static_cast<char *>(game) + player * 800 + 0x12cc;
		const std::string name = StripAiSuffix(slot + 0x90);
		bool first;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			first = g_dropped.insert(player).second;
		}
		if (rankedQuit) LeagueMatchPlayerQuit(player);
		if (first && rankedQuit) LogI("match", "league game: player %d (%s) left; the AI does not replace them", player, name.c_str());
		else if (first) {
			LogI("match", "online game: player %d (%s) dropped; the AI does not replace them", player, name.c_str());
			UiToast((name.empty() ? std::string("A player") : name) + Tr(" has disconnected. Their spot is now free: they can rejoin the game from Multiplayer."));
		}
		return;
	}
	g_origCreateAI(player, profile, difficulty);
}

}

void NetDropNetworkStarted()
{
	const OnlineRoom room = SteamCurrentRoom();
	const bool rules = room.id && room.info.dropAi;
	g_dropRules = rules;
	if (room.id) LogI("match", "online game: drop rules of the league %s (the room's data, read once)", rules ? "on" : "off");
}

bool NetDropHooksReady()
{
	return g_hooksReady;
}

void NetDropReset()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_dropped.clear();
	g_dropAi.clear();
}

bool NetDropAiReturnable(int player)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_dropAi.count(player) != 0;
}

void NetDropAiRelease(int player)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || player < 0 || player >= 16 || !TeamRules(reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0)) return;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_dropAi.erase(player);
		g_dropped.erase(player);
	}
	char *slot = game + player * 800 + 0x12cc;
	void **ai = reinterpret_cast<void **>(slot + 0x88);
	if (!*ai) return;
	// What the game's script native AIStop does, and CreatePlayerAI before it makes a new one: the AI's deleting
	// destructor (vtable slot 0, with 1), then no AI.
	void **vt = *reinterpret_cast<void ***>(*ai);
	reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vt[0])(*ai, nullptr, 1);
	*ai = nullptr;
	const std::string name = StripAiSuffix(slot + 0x90);
	LogI("match", "league game: player %d (%s) is coming back; the AI stops playing for them", player, name.c_str());
}

void InstallNetDrop(bool knownExe)
{
	if (!knownExe) return;
	void *split = reinterpret_cast<void *>(addr::kNetworkSplitExecute);
	void *create = reinterpret_cast<void *>(addr::kCreatePlayerAI);
	void *endDlg = reinterpret_cast<void *>(addr::kShowEndMultiplayerDlg);
	void *queue = reinterpret_cast<void *>(addr::kQueueCommand);
	if (HookCreate(endDlg, reinterpret_cast<void *>(&HookEndDlg), reinterpret_cast<void **>(&g_origEndDlg)) != MH_OK ||
		HookCreate(queue, reinterpret_cast<void *>(&HookQueueCommand), reinterpret_cast<void **>(&g_origQueue)) != MH_OK ||
		HookEnable(endDlg) != MH_OK || HookEnable(queue) != MH_OK) {
		LogE("match", "online game: could not hook the window close");
	}
	bool messages = HookCreate(reinterpret_cast<void *>(addr::kWantsToQuitExecute), reinterpret_cast<void *>(&HookWantsToQuit),
						reinterpret_cast<void **>(&g_origWantsToQuit)) == MH_OK &&
		HookCreate(reinterpret_cast<void *>(addr::kSurrenderExecute), reinterpret_cast<void *>(&HookSurrender),
			reinterpret_cast<void **>(&g_origSurrender)) == MH_OK &&
		HookCreate(reinterpret_cast<void *>(addr::kLocFormat), reinterpret_cast<void *>(&HookLocFormat),
			reinterpret_cast<void **>(&g_origLocFormat)) == MH_OK;
	messages = messages && HookEnable(reinterpret_cast<void *>(addr::kWantsToQuitExecute)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kSurrenderExecute)) == MH_OK && HookEnable(reinterpret_cast<void *>(addr::kLocFormat)) == MH_OK;
	LogI("match", "online game: drop, leave and surrender notices %s", messages ? "custom" : "from the game (could not be hooked)");
	if (HookCreate(split, reinterpret_cast<void *>(&HookSplit), reinterpret_cast<void **>(&g_origSplit)) == MH_OK &&
		HookCreate(create, reinterpret_cast<void *>(&HookCreateAI), reinterpret_cast<void **>(&g_origCreateAI)) == MH_OK &&
		HookEnable(split) == MH_OK && HookEnable(create) == MH_OK) {
		g_hooksReady = true;
		LogI("match", "online game: drops are no longer replaced by the AI (only whoever leaves on purpose)");
	}
	else {
		LogE("match", "online game: could not hook the drop notice");
	}
}
