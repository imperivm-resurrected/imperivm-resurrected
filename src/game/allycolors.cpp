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
#include "game/roomcolors.h"

#include <cstdint>
#include <string>

#include "addr/state.h"
#include "addr/world.h"

namespace {

// The screen's rectangle in CVXVisibleMgr, which InvalidateRect takes to redraw it all.
const size_t kVisibleScreenRect = 0x618;
// CVXPersistArchive: its mode, in ints.
const int kArchiveMode = 4, kArchiveLoading = 1;
// The settlements and troops of nobody: players 14 and 15 are the independents, the neutral populations of the map, and a player
// whose CVXPlayer+0x80 (layout::kPlayerControl) is 3 is the map's neutral one.
const int kControlNeutral = 3, kFirstIndependent = 14;

constexpr unsigned Rgb555(unsigned r, unsigned g, unsigned b)
{
	return (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
}
// You, allies and enemies: the colours chosen in the options (Config::allyRgb), a preset's or the player's own.
enum { kSelf, kAlly, kEnemy };

unsigned PaletteColor(int who)
{
	const uint32_t rgb = GetConfig().allyRgb[who];
	return Rgb555((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

typedef void(__cdecl *BuildShades_t)(unsigned color, unsigned short *out);
typedef void(__fastcall *ClearCache_t)(void *cache);
typedef void(__cdecl *RefreshVisuals_t)();
typedef void(__fastcall *Invalidate_t)(void *mgr, void *edx, int *rect);
typedef void(__fastcall *Serialize_t)(void *player, void *edx, int *archive);
Serialize_t g_origSerialize = nullptr;

// What this PC shows of each player of the game being played: their real colour, and the one drawn now. Only the game thread uses it.
struct Shown {
	const char *game = nullptr;
	// The room's own colours (game/roomcolors.h) wait for the match's players.
	bool roomColorsDue = false;
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
// mutual ceasefire (at peace or allied); anyone who can attack, or be attacked, is an enemy. The map's neutral player keeps its own
// colour: red would take what belongs to nobody for an army.
unsigned Wanted(const char *game, int index, const char *local, bool on)
{
	if (!on || !local) return g_shown.real[index];
	const char *player = layout::PlayerAt(game, index);
	if (player == local) return PaletteColor(kSelf);
	if (index >= kFirstIndependent || At<int>(player, layout::kPlayerControl) == kControlNeutral) return g_shown.real[index];
	const int ours = At<int>(local, layout::kPlayerId), theirs = At<int>(player, layout::kPlayerId);
	if (ours < 0 || ours >= layout::kPlayerCount || theirs < 0 || theirs >= layout::kPlayerCount) return g_shown.real[index];
	const bool peace = (At<unsigned>(local, layout::kPlayerDiplomacy + theirs * 4) & diplomacy::kCeasefire) != 0 &&
		(At<unsigned>(player, layout::kPlayerDiplomacy + ours * 4) & diplomacy::kCeasefire) != 0;
	return PaletteColor(peace ? kAlly : kEnemy);
}

// Redraws the players with their new shades, as the editor does when it changes a colour.
void Redraw()
{
	reinterpret_cast<ClearCache_t>(addr::kPaletteShaderCacheClear)(*reinterpret_cast<void **>(addr::kPaletteShaderCache));
	reinterpret_cast<RefreshVisuals_t>(addr::kRefreshMapVisuals)();
	char *mgr = *reinterpret_cast<char **>(addr::kVisibleMgr);
	if (mgr) reinterpret_cast<Invalidate_t>(addr::kVisibleMgrInvalidateRect)(mgr, nullptr, reinterpret_cast<int *>(mgr + kVisibleScreenRect));
}

// The colours the room's players picked outside the game's twelve: room slot s plays the s-th entry of the settings that holds its
// own index (as CivsBuildEnd finds a slot's player). Before the players are first read, so they are their real colours.
void ApplyRoomColors(const char *game)
{
	uint32_t rgb[kRoomColorSlots];
	if (!RoomColorsTake(rgb) || *reinterpret_cast<int *>(addr::kEditorMode)) return;
	const char *gs = *reinterpret_cast<const char *const *>(addr::kGameSettings);
	if (!gs) return;
	int entries[layout::kSettingsEntryCount];
	int count = 0;
	for (int i = 0; i < layout::kSettingsEntryCount; i++) {
		if (At<int>(layout::SettingsEntry(gs, i), layout::kEntrySlot) == i) entries[count++] = i;
	}
	for (int slot = 0; slot < kRoomColorSlots && slot < count; slot++) {
		if (!rgb[slot]) continue;
		char *player = const_cast<char *>(layout::PlayerAt(game, entries[slot]));
		const unsigned color = Rgb555((rgb[slot] >> 16) & 0xff, (rgb[slot] >> 8) & 0xff, rgb[slot] & 0xff);
		At<unsigned>(player, layout::kPlayerColor) = color;
		reinterpret_cast<BuildShades_t>(addr::kBuildColorShadeTable)(color, &At<unsigned short>(player, layout::kPlayerShades));
		LogI("game", "room colours: player %d (room slot %d) is #%06x", entries[slot], slot, rgb[slot]);
	}
	Redraw();
}

// Who is who in the match, once: the players that play, with their control (0 human, 3 the neutral one), for the colours of nobody.
void LogPlayers(const char *game)
{
	std::string text;
	for (int i = 0; i < layout::kPlayerCount; i++) {
		const char *player = layout::PlayerAt(game, i);
		if (At<int>(player, layout::kPlayerActive) == 0 && At<int>(player, layout::kPlayerState) == 0) continue;
		text += " " + std::to_string(i) + ":" + std::to_string(At<int>(player, layout::kPlayerControl));
	}
	LogI("game", "ally colours: players (index:control)%s", text.c_str());
}

bool Step()
{
	__try {
		const char *game = *reinterpret_cast<const char *const *>(addr::kGame);
		if (game != g_shown.game) {
			g_shown = Shown();
			g_shown.game = game;
			g_shown.roomColorsDue = game != nullptr;
		}
		if (!game || !*reinterpret_cast<void **>(addr::kMap)) return false;
		if (g_shown.roomColorsDue) {
			g_shown.roomColorsDue = false;
			ApplyRoomColors(game);
			LogPlayers(game);
		}
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
