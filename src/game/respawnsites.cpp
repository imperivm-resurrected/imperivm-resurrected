// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/respawnsites.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "addr/engine.h"
#include "addr/script.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/gamelayout.h"
#include "game/gameroom.h"
#include "hooks/gamestate.h"
#include "i18n/tr.h"
#include "mods/modoptions.h"

namespace {

constexpr unsigned kObjectSlots = 0xffff;
constexpr DWORD kSampleMs = 500;
// Items that come back within this time are told in one line; water and mushrooms, which come back every few minutes at every site,
// at most once a minute each (the big map shows the rest).
constexpr DWORD kNoticeGatherMs = 4000, kNoticeEveryMs = 60000;

std::array<RespawnSiteState, kObjectSlots> g_sites;
std::vector<RespawnMark> g_marks;
bool g_installed = false;
// A read of the map that failed (a game torn down under it, as a rejoin's reload does) turns the timers off for that game only.
bool g_failed = false;
const void *g_failedGame = nullptr;
const void *g_game = nullptr, *g_map = nullptr, *g_player = nullptr;
DWORD g_sampledAt = 0;
unsigned g_clock = 0;
// Per kind (mushrooms, water, ruins): the sites seen with their item back and not told yet, and when each kind was last told.
unsigned g_backCount[3] = {};
DWORD g_backSince = 0, g_toldAt[3] = {};

int KindIndex(RespawnKind kind) { return kind == kRespawnMushrooms ? 0 : kind == kRespawnWater ? 1 : 2; }

template <class T> T Read(const void *base, std::size_t offset)
{
	return *reinterpret_cast<const T *>(static_cast<const char *>(base) + offset);
}

// The script classes of the sites (CVXScriptClass, the registry's own), looked up once per map; a map object points to its own at +0x3c.
struct SiteClass {
	const void *cls;
	RespawnKind kind;
};
std::array<SiteClass, 5> g_classes = {};
bool g_haveClasses = false;

void FindSiteClasses()
{
	typedef void *(__fastcall * FindClass_t)(void *registry, void *edx, const char *name);
	static const char *const kNames[] = {"StonePile", "OakStump", "Tombstone", "TombStone", "Ruins1"};
	g_classes = {};
	void *registry = *reinterpret_cast<void **>(addr::kRegistry);
	if (!registry) return;
	int found = 0;
	for (size_t i = 0; i < g_classes.size(); ++i) {
		g_classes[i].cls = reinterpret_cast<FindClass_t>(addr::kFindClassByName)(registry, nullptr, kNames[i]);
		g_classes[i].kind = RespawnKindOf(kNames[i]);
		found += g_classes[i].cls != nullptr;
	}
	g_haveClasses = true;
	LogD("game", "respawn timers: %d of %d site classes found", found, static_cast<int>(g_classes.size()));
}

// The kind of an object of the handle table. It also holds items and script objects without CVXMapObj's class field, so the
// value at +0x3c is only compared with the sites' classes, never followed.
RespawnKind KindOf(const char *object)
{
	const void *cls = Read<const void *>(object, 0x3c);
	if (!cls) return kRespawnNone;
	for (const SiteClass &c : g_classes) {
		if (c.cls == cls) return c.kind;
	}
	return kRespawnNone;
}

// CVXItemHolder registers event 0x2c with its scheduler for its next item, the randomized first one too: its game time, or 0.
unsigned RespawnDue(const void *site, unsigned clock)
{
	const auto events = Read<const char *const *>(site, 0xc);
	const auto count = Read<unsigned>(site, 0x10);
	const auto scheduler = Read<const unsigned *>(site, 0x18);
	if (!events || !scheduler || count > 32) return 0;
	for (unsigned i = 0; i < count; ++i) {
		const char *event = events[i];
		if (!event || Read<unsigned>(event, 0x10) != 0x2c || Read<const void *>(event, 0x14) != site) continue;
		const unsigned time = Read<unsigned>(event, 4), now = *scheduler;
		if (time > now && time - now <= 3600000) return clock + (time - now);
	}
	return 0;
}

// The handle table keeps, in a slot no object uses any more, the pointer of the one that was there: an object deleted, or the whole
// world before a load or a new match, whose memory may be gone. Read blindly, such a slot faulted and the timers went off for the rest of
// the game. A slot counts only when its memory is readable (each region asked once a pass) and the object at it names that
// very slot, as a living object does.
std::vector<std::pair<uintptr_t, uintptr_t>> g_readable;

bool Readable(const char *p, std::size_t size)
{
	const uintptr_t from = reinterpret_cast<uintptr_t>(p), to = from + size;
	for (const auto &r : g_readable) {
		if (from >= r.first && to <= r.second) return true;
	}
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
		!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
		return false;
	}
	const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress), end = base + mbi.RegionSize;
	if (to > end) return false;
	if (g_readable.size() < 4096) g_readable.push_back({base, end});
	return true;
}

RespawnKind LivingKindOf(const char *object, unsigned slot)
{
	if (!Readable(object, 0x40) || Read<unsigned short>(object, 8) != static_cast<unsigned short>(slot)) return kRespawnNone;
	return KindOf(object);
}

// Samples every site the handle table holds; counts the discovered sites seen with their item again.
void Observe(const void *player, unsigned clock)
{
	typedef int(__thiscall * IsVisible_t)(const void *, const void *);
	typedef const void *(__thiscall * GetItem_t)(const void *, int);
	unsigned back = 0;
	g_marks.clear();
	// Asked again on each pass: memory freed since the last one is not taken for readable.
	g_readable.clear();
	const auto table = reinterpret_cast<const char *const *>(addr::kHandleTable);
	for (unsigned i = 0; i < kObjectSlots; ++i) {
		const char *site = table[i];
		const RespawnKind kind = site ? LivingKindOf(site, i) : kRespawnNone;
		RespawnSiteState &state = g_sites[i];
		if (kind == kRespawnNone) {
			if (state.site) state = RespawnSiteState();
			continue;
		}
		const bool visible = reinterpret_cast<IsVisible_t>(addr::kMapObjectVisibleToPlayer)(site, player) != 0;
		bool available = false;
		unsigned due = 0;
		if (visible) {
			// Obj::GetItem: slot 0xf4, with the zero-based index of the inventory.
			const uintptr_t getter = Read<const uintptr_t *>(site, 0)[0xf4 / sizeof(void *)];
			if (getter < addr::kGameCodeBegin || getter >= addr::kGameCodeEnd) continue;
			available = reinterpret_cast<GetItem_t>(getter)(site, 0) != nullptr;
			if (!available && kind != kRespawnRuin) due = RespawnDue(site, clock);
		}
		if (state.Observe(site, visible, available, clock, due, kind)) {
			if (!g_backSince) g_backSince = GetTickCount() | 1;
			++g_backCount[KindIndex(kind)];
		}
		if (state.discovered) {
			RespawnMark m;
			m.x = Read<int>(site, 0x24);
			m.y = Read<int>(site, 0x28);
			m.kind = kind;
			m.stocked = state.stocked;
			m.due = state.due;
			m.approximate = state.approximate;
			g_marks.push_back(m);
		}
	}
}

void Forget()
{
	g_readable.clear();
	g_haveClasses = false;
	g_sites.fill(RespawnSiteState());
	g_marks.clear();
	g_sampledAt = 0;
	g_clock = 0;
	g_backSince = 0;
	for (unsigned &n : g_backCount) n = 0;
}

void Sample()
{
	__try {
		const char *game = *reinterpret_cast<const char **>(addr::kGame);
		const void *map = *reinterpret_cast<const void **>(addr::kMap);
		if (!game || !map || !*reinterpret_cast<void **>(addr::kMapView) || *reinterpret_cast<int *>(addr::kEditorMode) ||
			*reinterpret_cast<int *>(addr::kLoadingSavedGame) || *reinterpret_cast<int *>(addr::kLoadRequest) ||
			reinterpret_cast<int(__cdecl *)()>(addr::kIsDemoPlayback)()) {
			g_game = nullptr;
			return;
		}
		const char *player = layout::LocalPlayer(game);
		const unsigned clock = Field<unsigned>(game, layout::kGameClock);
		const uintptr_t offset = reinterpret_cast<uintptr_t>(player) - reinterpret_cast<uintptr_t>(layout::PlayerAt(game, 0));
		if (!player || offset >= 8 * layout::kPlayerSize || offset % layout::kPlayerSize || !clock) {
			g_game = nullptr;
			return;
		}
		// Another match, another map or a load: what was known is of another game.
		if (game != g_game || map != g_map || player != g_player || clock < g_clock) Forget();
		g_game = game;
		g_map = map;
		g_player = player;
		if (!RespawnTimersOn()) {
			g_marks.clear();
			g_sampledAt = 0;
			return;
		}
		const DWORD now = GetTickCount();
		if (g_sampledAt && (clock == g_clock || now - g_sampledAt < kSampleMs)) return;
		if (!g_haveClasses) FindSiteClasses();
		g_sampledAt = now;
		g_clock = clock;
		Observe(player, clock);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed = true;
		g_failedGame = *reinterpret_cast<const void **>(addr::kGame);
		Forget();
		LogE("game", "respawn timers: ERROR reading the map, off until this game ends");
		return;
	}
}

}

void InstallRespawnSites(bool knownExe)
{
	g_installed = knownExe;
}

bool RespawnTimersOn()
{
	if (!g_installed || g_failed || !GetConfig().respawnTimers) return false;
	return !(GameIsMultiplayer() && (ModRulesActive() & kRuleNoTimers));
}

void RespawnSitesGameTick()
{
	if (!g_installed) return;
	// The next game (or the same one loaded again by a rejoin) has them back.
	if (g_failed && *reinterpret_cast<const void **>(addr::kGame) != g_failedGame) {
		g_failed = false;
		LogI("game", "respawn timers: on again in the new game");
	}
	if (g_failed) return;
	Sample();
	const DWORD now = GetTickCount();
	if (!g_backSince || now - g_backSince < kNoticeGatherMs) return;
	g_backSince = 0;
	static const char *const kOne[] = {"Poison mushrooms are back.", "Healing water is back.", "There is loot in the ruins again."};
	static const char *const kMany[] = {"Poison mushrooms are back at %s sites.", "Healing water is back at %s sites.", "There is loot again in %s ruins."};
	std::string text;
	for (int k = 0; k < 3; ++k) {
		const unsigned n = g_backCount[k];
		g_backCount[k] = 0;
		// The ruins are rare and worth a trip: always told. Water and mushrooms: not again within the minute.
		if (!n || (k != 2 && g_toldAt[k] && now - g_toldAt[k] < kNoticeEveryMs)) continue;
		g_toldAt[k] = now | 1;
		text += (text.empty() ? "" : " ") + (n == 1 ? Tr(kOne[k]) : TrFmt(kMany[k], {std::to_string(n)}));
	}
	if (!text.empty()) UiToast(text);
}

const std::vector<RespawnMark> &RespawnMarks()
{
	static const std::vector<RespawnMark> none;
	// The map can be drawn before the sampler sees a load or a new match.
	const char *game = *reinterpret_cast<const char **>(addr::kGame);
	if (!RespawnTimersOn() || g_marks.empty() || !game || game != g_game || *reinterpret_cast<const void **>(addr::kMap) != g_map ||
		*reinterpret_cast<int *>(addr::kLoadingSavedGame) || *reinterpret_cast<int *>(addr::kLoadRequest) || layout::LocalPlayer(game) != g_player) {
		return none;
	}
	return g_marks;
}

unsigned RespawnClock()
{
	const char *game = *reinterpret_cast<const char **>(addr::kGame);
	return game ? Field<unsigned>(game, layout::kGameClock) : 0;
}
