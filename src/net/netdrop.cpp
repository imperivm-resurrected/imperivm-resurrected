// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <mutex>
#include <set>
#include <string>
#include <atomic>
#include <cstdint>

#include "net/netdrop.h"
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
#include "core/uiport.h"

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

void __fastcall HookSplit(void *cmd)
{
	LogI("match", "online game: drop notice (players %02x)", *reinterpret_cast<unsigned *>(static_cast<char *>(cmd) + 0xc));
	g_inSplit++;
	g_origSplit(cmd);
	g_inSplit--;
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

void __fastcall HookSurrender(void *cmd)
{
	char *player = *reinterpret_cast<char **>(static_cast<char *>(cmd) + 4);
	const int index = player ? *reinterpret_cast<int *>(player + 8) : -1;
	if (!reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) {
		const std::string name = PlayerName(index);
		LogI("match", "online game: player %d (%s) surrenders", index, name.c_str());
		ConsoleMessage((name.empty() ? std::string("A player") : name) + Tr(" has surrendered."));
	}
	g_origSurrender(cmd);
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
		const std::string text = g_inQuit > 0 ? name + Tr(" has left the game; their army is controlled by the AI.")
											   : name + Tr(" has disconnected. They can rejoin the game from Multiplayer.");
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
	if (g_inSplit > 0 && demo && SpectatorViewing() && game && player >= 0 && player < 16) {
		LogI("match", "broadcast: player %d dropped in the game; the AI does not replace them", player);
		return;
	}
	if (g_inSplit > 0 && game && player >= 0 && player < 16 && !demo) {
		char *slot = static_cast<char *>(game) + player * 800 + 0x12cc;
		const std::string name = StripAiSuffix(slot + 0x90);
		bool first;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			first = g_dropped.insert(player).second;
		}
		if (first) {
			LogI("match", "online game: player %d (%s) dropped; the AI does not replace them", player, name.c_str());
			UiToast((name.empty() ? std::string("A player") : name) + Tr(" has disconnected. Their spot is now free: they can rejoin the game from Multiplayer."));
		}
		return;
	}
	g_origCreateAI(player, profile, difficulty);
}

const unsigned kFastQuitMs = 2000;
std::atomic<DWORD> g_fastUntil{0};
unsigned g_savedQuit[2] = {0, 0};
std::atomic<bool> g_fastActive{false};

}

void NetDropPeerLeft()
{
	unsigned *a = reinterpret_cast<unsigned *>(addr::kQuitInitial), *b = reinterpret_cast<unsigned *>(addr::kQuitSubsequent);
	if (*b > 120000) return;
	if (!g_fastActive.exchange(true)) {
		g_savedQuit[0] = *a;
		g_savedQuit[1] = *b;
	}
	*a = *b = kFastQuitMs;
	g_fastUntil = GetTickCount() + 15000;
	LogI("match", "online game: a player left the room (their game closed): treated as dropped without waiting");
}

void NetDropTick()
{
	if (!g_fastActive || static_cast<int>(GetTickCount() - g_fastUntil) < 0) return;
	*reinterpret_cast<unsigned *>(addr::kQuitInitial) = g_savedQuit[0];
	*reinterpret_cast<unsigned *>(addr::kQuitSubsequent) = g_savedQuit[1];
	g_fastActive = false;
}

void NetDropReset()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_dropped.clear();
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
		LogI("match", "online game: drops are no longer replaced by the AI (only whoever leaves on purpose)");
	}
	else {
		LogE("match", "online game: could not hook the drop notice");
	}
}
