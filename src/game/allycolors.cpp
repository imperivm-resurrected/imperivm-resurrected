// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "game/allycolors.h"
#include "game/controls.h"
#include "game/diplomacyrules.h"
#include "game/gamelayout.h"
#include "game/gamehook.h"

#include <cstdint>

#include "addr/state.h"
#include "addr/world.h"

namespace {

// The screen's rectangle in CVXVisibleMgr, which InvalidateRect takes to redraw it all.
const size_t kVisibleScreenRect = 0x618;
// CVXPersistArchive: its mode, in ints.
const int kArchiveMode = 4, kArchiveLoading = 1;

constexpr unsigned Rgb555(unsigned r, unsigned g, unsigned b)
{
	return (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
}
// Blue, yellow and red of the game's own palette: red and yellow tell apart for most colour-blind players, as blue does from both.
const unsigned kSelfColor = Rgb555(0, 0, 255), kAllyColor = Rgb555(255, 255, 0), kEnemyColor = Rgb555(255, 0, 0);

typedef void(__cdecl *BuildShades_t)(unsigned color, unsigned short *out);
typedef void(__fastcall *ClearCache_t)(void *cache);
typedef void(__cdecl *RefreshVisuals_t)();
typedef void(__fastcall *Invalidate_t)(void *mgr, void *edx, int *rect);
typedef void(__fastcall *Serialize_t)(void *player, void *edx, int *archive);
Serialize_t g_origSerialize = nullptr;

// What this PC shows of each player of the game being played: their real colour, and the one drawn now. Only the game thread uses it.
struct Shown {
	const char *game = nullptr;
	bool known[layout::kPlayerCount] = {};
	unsigned real[layout::kPlayerCount] = {}, drawn[layout::kPlayerCount] = {};
};
Shown g_shown;

int IndexOf(const char *game, const void *player)
{
	if (!game || !player) return -1;
	const ptrdiff_t at = static_cast<const char *>(player) - layout::PlayerAt(game, 0);
	if (at < 0 || at % layout::kPlayerSize || at / static_cast<ptrdiff_t>(layout::kPlayerSize) >= layout::kPlayerCount) return -1;
	return static_cast<int>(at / layout::kPlayerSize);
}

// The colour a player takes on this PC: theirs without the option, else by their diplomacy with the local player. Allies are those in
// mutual ceasefire (at peace or allied); anyone who can attack, or be attacked, is an enemy.
unsigned Wanted(const char *game, int index, const char *local, bool on)
{
	if (!on || !local) return g_shown.real[index];
	const char *player = layout::PlayerAt(game, index);
	if (player == local) return kSelfColor;
	const int ours = At<int>(local, layout::kPlayerId), theirs = At<int>(player, layout::kPlayerId);
	if (ours < 0 || ours >= layout::kPlayerCount || theirs < 0 || theirs >= layout::kPlayerCount) return g_shown.real[index];
	const bool peace = (At<unsigned>(local, layout::kPlayerDiplomacy + theirs * 4) & diplomacy::kCeasefire) != 0 &&
		(At<unsigned>(player, layout::kPlayerDiplomacy + ours * 4) & diplomacy::kCeasefire) != 0;
	return peace ? kAllyColor : kEnemyColor;
}

// Redraws the players with their new shades, as the editor does when it changes a colour.
void Redraw()
{
	reinterpret_cast<ClearCache_t>(addr::kPaletteShaderCacheClear)(*reinterpret_cast<void **>(addr::kPaletteShaderCache));
	reinterpret_cast<RefreshVisuals_t>(addr::kRefreshMapVisuals)();
	char *mgr = *reinterpret_cast<char **>(addr::kVisibleMgr);
	if (mgr) reinterpret_cast<Invalidate_t>(addr::kVisibleMgrInvalidateRect)(mgr, nullptr, reinterpret_cast<int *>(mgr + kVisibleScreenRect));
}

bool Step()
{
	__try {
		const char *game = *reinterpret_cast<const char *const *>(addr::kGame);
		if (game != g_shown.game) {
			g_shown = Shown();
			g_shown.game = game;
		}
		if (!game || !*reinterpret_cast<void **>(addr::kMap)) return false;
		const char *local = layout::LocalPlayer(game);
		// Only for one of the match's eight seats: never in the editor, whose colours are the map's own, nor for the observer (player 9)
		// a replay or a broadcast puts the watcher in, who is at peace with nobody and would see everyone red.
		const int seat = IndexOf(game, local);
		const bool on = GetConfig().allyColors && local && seat >= 0 && seat < layout::kSettingsEntryCount && !*reinterpret_cast<int *>(addr::kEditorMode);
		bool changed = false;
		for (int i = 0; i < layout::kPlayerCount; i++) {
			const char *player = layout::PlayerAt(game, i);
			const unsigned now = At<unsigned>(player, layout::kPlayerColor) & 0x7fff;
			if (!g_shown.known[i]) {
				g_shown.known[i] = true;
				g_shown.real[i] = g_shown.drawn[i] = now;
			}
			// Something else gave the player another colour (the editor, a game loaded): that one is theirs now.
			else if (now != g_shown.drawn[i]) g_shown.real[i] = now;
			const bool plays = At<int>(player, layout::kPlayerActive) != 0 || At<int>(player, layout::kPlayerState) != 0;
			const unsigned want = plays ? Wanted(game, i, local, on) : g_shown.real[i];
			g_shown.drawn[i] = want;
			if (want == now) continue;
			At<unsigned>(player, layout::kPlayerColor) = want;
			reinterpret_cast<BuildShades_t>(addr::kBuildColorShadeTable)(want, &At<unsigned short>(player, layout::kPlayerShades));
			changed = true;
		}
		if (changed) Redraw();
		return changed;
	}
	// The real colours stay known: forgetting them would take the shown ones for real on the next frame.
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// A save keeps the real colours: the player's goes back for its section and the shown one returns after it. Loading writes the colour
// read, which is the real one from then on.
void __fastcall HookPlayerSerialize(void *player, void *edx, int *archive)
{
	const char *game = *reinterpret_cast<const char *const *>(addr::kGame);
	const int i = game == g_shown.game ? IndexOf(game, player) : -1;
	unsigned &color = At<unsigned>(player, layout::kPlayerColor);
	// The archive's mode (+0x10) is 1 while loading: the game then rebuilds the shades from the colour read.
	if (i >= 0 && archive && archive[kArchiveMode] == kArchiveLoading) {
		g_origSerialize(player, edx, archive);
		g_shown.known[i] = true;
		g_shown.real[i] = g_shown.drawn[i] = color & 0x7fff;
		return;
	}
	if (i < 0 || !g_shown.known[i] || g_shown.drawn[i] == g_shown.real[i]) {
		g_origSerialize(player, edx, archive);
		return;
	}
	const unsigned drawn = color;
	color = g_shown.real[i];
	g_origSerialize(player, edx, archive);
	color = drawn;
}

}

void InstallAllyColors(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookFunction(addr::kPlayerSerialize, &HookPlayerSerialize, &g_origSerialize);
	if (ok) LogI("game", "ally and enemy colours: hooked");
	else LogE("game", "ally and enemy colours: could not hook CVXPlayer::Serialize; the option stays off");
}

void AllyColorsGameTick()
{
	if (!g_origSerialize) return;
	static bool wasOn = false;
	const bool on = GetConfig().allyColors;
	if (Step() && on != wasOn) LogI("game", "ally and enemy colours %s", on ? "shown" : "taken back");
	wasOn = on;
}

bool AllyColorsHandleKey(unsigned msg, unsigned wp, long lp)
{
	if ((msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) || !g_origSerialize || !*reinterpret_cast<void **>(addr::kGame)) return false;
	if (!ControlsModKeyPressed(kModKeyAllyColors, wp)) return false;
	// Held down, the key repeats (bit 30 set): it switches once per press.
	if (lp & (1L << 30)) return true;
	Config &c = MutableConfig();
	c.allyColors = !c.allyColors;
	SaveConfig();
	return true;
}

unsigned AllyColorsRealColor(int index, unsigned shown)
{
	if (index < 0 || index >= layout::kPlayerCount || !g_shown.known[index] || g_shown.game != *reinterpret_cast<const char *const *>(addr::kGame)) return shown;
	return g_shown.drawn[index] == (shown & 0x7fff) ? g_shown.real[index] : shown;
}
