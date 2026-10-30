// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"
#include "hooks/hookcount.h"

#include "addr/engine.h"
#include "addr/script.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/log.h"
#include "game/quickicons.h"

namespace {
typedef void *(__cdecl *OperatorNew_t)(size_t size);
typedef void *(__fastcall *IconCtor_t)(void *self, void *edx, int index, int flag);
typedef void(__fastcall *IconSelf_t)(void *self);
typedef void(__fastcall *IconRefresh_t)(void *self, void *edx, int mode);
typedef int(__fastcall *IconMouse_t)(void *self, void *edx, int message, short *pos);
typedef void(__fastcall *IconTooltip_t)(void *self, void *edx, short *pos);
typedef void(__fastcall *IconDraw_t)(void *self, void *edx, void *surface, int unused);
typedef void(__fastcall *FillRect_t)(void *surface, void *edx, int *rect, unsigned color);
typedef int(__cdecl *LevelFromExp_t)(int experience);
typedef int(__fastcall *SkillPoints_t)(void *hero);
typedef void(__cdecl *CreateBar_t)(int param);
typedef void(__cdecl *DestroyBar_t)();
typedef void(__cdecl *Release_t)(void *image);
typedef void(__fastcall *Invalidate_t)(void *manager, void *edx, int *rect);
typedef void(__fastcall *Placement_t)(void *manager, void *edx, int flag);

CreateBar_t g_origCreateBar = nullptr;
DestroyBar_t g_origDestroyBar = nullptr;
IconRefresh_t g_origRefresh = nullptr;
IconMouse_t g_origBarMouse = nullptr;
IconSelf_t g_origRefreshAll = nullptr;
IconDraw_t g_origIconDraw = nullptr;
IconSelf_t g_origRenderIcon = nullptr;

const int kExtraCount = 5;
const int kRealCount = 5;
const int kExtraIndex[kExtraCount] = {5, 6, 7, 8, -1};
const int kIconSize = 0xd4;
const int kColumnLeft = 0x44;
const int kBarCursorSlot = 10;

const unsigned kOffIndex = 0x60, kOffImage = 0xa8, kOffBitmap = 0xa4, kOffFrame = 0xac, kOffShown = 0xb0, kOffEmpty = 0xb4;
const unsigned kOffHealthA = 0xb8, kOffHealthB = 0xbc, kOffCount = 0xc0, kOffRace = 0xc4, kOffDirty = 0xc8, kOffInvalid = 0x18;
const unsigned kOffLeft = 0x84, kOffTop = 0x88, kOffRight = 0x8c, kOffBottom = 0x90;
const unsigned kOffCornerLeft = 0x94, kOffCornerTop = 0x98, kOffCornerRight = 0x9c, kOffCornerBottom = 0xa0;
const unsigned kOffPosX = 0x34, kOffPosY = 0x38;
const unsigned kOffPlayer = 0x12c8, kSelectionBase = 0xd8, kSelectionStride = 0x2c, kSelectionCount = 0x1c;

void *g_extra[kExtraCount] = {};
void *g_barFor = nullptr;
bool g_failed = false;
DWORD g_lastTick = 0;

template <typename T>
T &At(void *p, unsigned off) { return *reinterpret_cast<T *>(static_cast<char *>(p) + off); }

void *Bar() { return *reinterpret_cast<void **>(addr::kQuickIconBarGlobal); }

const unsigned kHeroFlag = 0x1000000, kOffFlags = 0x2c, kOffExperience = 0x180, kOffLevel = 0x184;
const DWORD kBlinkMs = 450;

const unsigned char kGlyphs[11][5] = {
	{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
	{7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
	{0, 2, 7, 2, 0},
};

char *HeroOf(void *icon)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *player = game ? At<char *>(game, kOffPlayer) : nullptr;
	if (!player) return nullptr;
	char *selection = player + kSelectionBase + At<int>(icon, kOffIndex) * kSelectionStride;
	if (At<int>(selection, kSelectionCount) != 1) return nullptr;
	char *head = At<char *>(selection, 0x18);
	char *node = head ? *reinterpret_cast<char **>(head) : nullptr;
	if (!node || node == head) return nullptr;
	char *object = reinterpret_cast<char **>(addr::kHandleTable)[*reinterpret_cast<unsigned short *>(node + 8)];
	if (!object || (At<unsigned>(object, kOffFlags) & kHeroFlag) == 0) return nullptr;
	return object;
}

bool LevelSpanOf(int experience, int &level, int &lower, int &upper)
{
	static int cachedLevel = 0, cachedLower = 0, cachedUpper = 0;
	if (experience < 0) return false;
	LevelFromExp_t levelOf = reinterpret_cast<LevelFromExp_t>(addr::kGetLevelFromExp);
	if (cachedLevel > 0 && experience >= cachedLower && experience < cachedUpper) {
		level = cachedLevel;
		lower = cachedLower;
		upper = cachedUpper;
		return true;
	}
	level = levelOf(experience);
	int lo = 0, hi = experience;
	if (level > 1) {
		while (lo < hi) {
			const int mid = lo + (hi - lo) / 2;
			if (levelOf(mid) >= level) hi = mid;
			else lo = mid + 1;
		}
	}
	else {
		lo = 0;
	}
	lower = lo;
	long long probe = static_cast<long long>(experience) + 1;
	for (int guard = 0; guard < 40 && levelOf(static_cast<int>(probe)) <= level && probe < 1000000000; guard++) probe = probe * 2 + 1;
	if (probe >= 2000000000LL || levelOf(static_cast<int>(probe)) <= level) return false;
	lo = experience + 1;
	hi = static_cast<int>(probe);
	while (lo < hi) {
		const int mid = lo + (hi - lo) / 2;
		if (levelOf(mid) > level) hi = mid;
		else lo = mid + 1;
	}
	upper = lo;
	cachedLevel = level;
	cachedLower = lower;
	cachedUpper = upper;
	return upper > lower;
}

void Block(void *surface, int left, int top, int right, int bottom, unsigned color)
{
	void **vtable = *reinterpret_cast<void ***>(surface);
	int rect[4] = {left, top, right, bottom};
	reinterpret_cast<FillRect_t>(vtable[0x3c / 4])(surface, nullptr, rect, color);
}

void DrawGlyphs(void *surface, int x, int y, const char *text, int scale, unsigned color)
{
	const int length = static_cast<int>(strlen(text));
	int cursor = x;
	for (int d = 0; d < length; d++) {
		const int index = text[d] == '+' ? 10 : text[d] - '0';
		if (index < 0 || index > 10) continue;
		const unsigned char *glyph = kGlyphs[index];
		for (int row = 0; row < 5; row++) {
			int col = 0;
			while (col < 3) {
				if (!(glyph[row] & (4 >> col))) {
					col++;
					continue;
				}
				int run = col;
				while (run < 3 && (glyph[row] & (4 >> run))) run++;
				Block(surface, cursor + col * scale, y + row * scale, cursor + run * scale - 1, y + (row + 1) * scale - 1, color);
				col = run;
			}
		}
		cursor += 4 * scale;
	}
}

void DrawOutlined(void *surface, int x, int y, const char *text, int scale, unsigned color)
{
	const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
	for (int i = 0; i < 4; i++) DrawGlyphs(surface, x + dx[i], y + dy[i], text, scale, 0x0000);
	DrawGlyphs(surface, x, y, text, scale, color);
}

void DecorateIcon(void *icon, void *surface)
{
	const int x = At<short>(icon, kOffPosX), y = At<short>(icon, kOffPosY);
	char text[16];
	const int count = At<int>(icon, kOffCount);
	if (count >= 0) {
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%d", std::min(count, 999));
		DrawOutlined(surface, x + 4, y + 0x2a, text, 2, 0x7fff);
	}
	char *hero = HeroOf(icon);
	if (!hero) return;
	const int experience = At<int>(hero, kOffExperience);
	int level = At<int>(hero, kOffLevel), lower = 0, upper = 0, spanLevel = 0;
	if (LevelSpanOf(experience, spanLevel, lower, upper)) {
		const int top = 0x12, bottom = 0x33, height = bottom - top + 1;
		const int filled = static_cast<int>(static_cast<long long>(experience - lower) * height / (upper - lower));
		Block(surface, x + 0x2e, y + top, x + 0x30, y + bottom, 0x0842);
		if (filled > 0) Block(surface, x + 0x2e, y + bottom - std::min(height, filled) + 1, x + 0x30, y + bottom, 0x7ec3);
	}
	const int points = reinterpret_cast<SkillPoints_t>(addr::kHeroAvailableSkillPoints)(hero);
	const bool blinkOn = points > 0 && ((GetTickCount() / kBlinkMs) & 1) == 0;
	const unsigned color = points > 0 ? (blinkOn ? 0x6284u : 0x7fffu) : 0x7fffu;
	_snprintf_s(text, sizeof(text), _TRUNCATE, points > 0 ? "%d+" : "%d", std::max(0, std::min(999, level)));
	DrawOutlined(surface, x + 4, y + 3, text, level >= 100 ? 1 : 2, color);
}

void __fastcall HookRenderIcon(void *self)
{
	const int saved = At<int>(self, kOffCount);
	At<int>(self, kOffCount) = -1;
	g_origRenderIcon(self);
	At<int>(self, kOffCount) = saved;
}

void __fastcall HookIconDraw(void *self, void *edx, void *surface, int unused)
{
	g_origIconDraw(self, edx, surface, unused);
	if (g_failed || !At<void *>(self, kOffBitmap) || At<int>(self, kOffShown) == 0) return;
	__try {
		DecorateIcon(self, surface);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed = true;
		LogE("game", "group icons: ERROR decorating the hero icon, disabled");
	}
}

bool ExtrasEnabled()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char *slash = strrchr(path, 0x5c)) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	return GetPrivateProfileIntA("Map", "ZoomExtras", 1, path) != 0;
}

bool IsExtraIndex(int index) { return index >= kRealCount || index == -1; }

void *PreviousIcon(int index)
{
	void *bar = Bar();
	if (index == 5) return bar ? At<void *>(bar, 0x14) : nullptr;
	if (index >= 6 && index <= 8) return g_extra[index - 6];
	if (index == -1) return g_extra[3];
	return nullptr;
}

void __fastcall HookRefresh(void *self, void *edx, int mode)
{
	const int index = At<int>(self, kOffIndex);
	if (!IsExtraIndex(index)) {
		g_origRefresh(self, edx, mode);
		return;
	}
	if (mode != 0 && At<int>(self, kOffDirty) == 0) return;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *player = game ? At<char *>(game, kOffPlayer) : nullptr;
	if (!player) return;
	char *selection = player + kSelectionBase + index * kSelectionStride;
	if (At<int>(selection, kSelectionCount) != 0) {
		g_origRefresh(self, edx, mode);
		return;
	}
	void *previous = PreviousIcon(index);
	const bool previousFilled = previous && At<int>(previous, kOffEmpty) == 0;
	const int wasShown = At<int>(self, kOffShown);
	const int shown = previousFilled ? 1 : 0;
	At<int>(self, kOffEmpty) = 1;
	At<int>(self, kOffShown) = shown;
	if (!wasShown && !shown) return;
	if (mode == 0) {
		reinterpret_cast<Invalidate_t>(addr::kVisibleMgrInvalidateRect)(*reinterpret_cast<void **>(addr::kVisibleMgr), nullptr, reinterpret_cast<int *>(static_cast<char *>(self) + kOffInvalid));
		At<int>(self, kOffDirty) = 1;
		return;
	}
	At<int>(self, kOffHealthA) = -1;
	At<int>(self, kOffHealthB) = -1;
	At<int>(self, kOffFrame) = 0;
	if (At<void *>(self, kOffImage) != nullptr || At<int>(self, kOffRace) != -1 || At<int>(self, kOffCount) != -1) {
		At<int>(self, kOffCount) = -1;
		At<int>(self, kOffRace) = -1;
		if (At<void *>(self, kOffImage)) reinterpret_cast<Release_t>(addr::kReleaseCachedFile)(At<void *>(self, kOffImage));
		At<void *>(self, kOffImage) = nullptr;
		reinterpret_cast<IconSelf_t>(addr::kQuickIconRenderIcon)(self);
	}
	At<int>(self, kOffDirty) = 0;
}

void PlaceExtra(void *icon, int row)
{
	const int left = kColumnLeft, right = left + 0x33, top = row * 0x44 + 8, bottom = top + 0x3b;
	At<int>(icon, kOffLeft) = left;
	At<int>(icon, kOffRight) = right;
	At<int>(icon, kOffTop) = top;
	At<int>(icon, kOffBottom) = bottom;
	At<int>(icon, kOffCornerLeft) = right - 0xf;
	At<int>(icon, kOffCornerTop) = top;
	At<int>(icon, kOffCornerRight) = right;
	At<int>(icon, kOffCornerBottom) = top + 0xf;
	reinterpret_cast<IconSelf_t>(addr::kQuickIconUpdateRect)(icon);
}

void RefreshExtras(int mode)
{
	for (void *icon : g_extra) {
		if (icon) reinterpret_cast<IconRefresh_t>(addr::kQuickIconRefresh)(icon, nullptr, mode);
	}
}

void CreateExtras()
{
	void *bar = Bar();
	if (!bar || bar == g_barFor || g_failed) return;
	g_barFor = bar;
	for (void *&icon : g_extra) icon = nullptr;
	for (int e = 0; e < kExtraCount; e++) {
		void *memory = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(kIconSize);
		if (!memory) break;
		void *icon = reinterpret_cast<IconCtor_t>(addr::kQuickIconCtor)(memory, nullptr, kExtraIndex[e], 1);
		g_extra[e] = icon;
		PlaceExtra(icon, e);
	}
	RefreshExtras(0);
	LogI("game", "group icons: %d extra icons created (groups 6 to 9 and 0)", kExtraCount);
}

void DestroyExtras()
{
	for (void *&icon : g_extra) {
		if (!icon) continue;
		At<int>(icon, kOffIndex) = kBarCursorSlot;
		void **vtable = *reinterpret_cast<void ***>(icon);
		reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vtable[0])(icon, nullptr, 1);
		icon = nullptr;
	}
	g_barFor = nullptr;
}

bool SafeCreateExtras()
{
	__try {
		CreateExtras();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed = true;
		return false;
	}
}

bool SafeDestroyExtras()
{
	__try {
		DestroyExtras();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed = true;
		for (void *&icon : g_extra) icon = nullptr;
		g_barFor = nullptr;
		return false;
	}
}

void __cdecl HookCreateBar(int param)
{
	g_origCreateBar(param);
	if (!SafeCreateExtras()) LogE("game", "group icons: ERROR creating the extra icons, disabled");
}

void __cdecl HookDestroyBar()
{
	if (!SafeDestroyExtras()) LogE("game", "group icons: ERROR removing the extra icons");
	g_origDestroyBar();
}

void __fastcall HookRefreshAll(void *bar)
{
	g_origRefreshAll(bar);
	if (!g_failed) RefreshExtras(0);
}

int __fastcall HookBarMouse(void *bar, void *edx, int message, short *pos)
{
	const int handled = g_origBarMouse(bar, edx, message, pos);
	if (handled == 0 || g_failed) return handled;
	for (void *icon : g_extra) {
		if (!icon) continue;
		if (reinterpret_cast<IconMouse_t>(addr::kQuickIconOnMouse)(icon, nullptr, message, pos) != 0) continue;
		At<int>(bar, 0x30) = *reinterpret_cast<int *>(pos);
		At<int>(bar, 0x2c) = 0;
		reinterpret_cast<Placement_t>(addr::kSetPlacementFlag)(*reinterpret_cast<void **>(addr::kSelectionManager), nullptr, 1);
		reinterpret_cast<IconTooltip_t>(addr::kQuickIconShowTooltip)(icon, nullptr, pos);
		return 0;
	}
	return handled;
}

void *IconAt(int slot)
{
	if (slot < kRealCount) {
		void *bar = Bar();
		return bar ? At<void *>(bar, 4 + slot * 4) : nullptr;
	}
	return g_extra[slot - kRealCount];
}

bool Inside(void *icon, int x, int y)
{
	return At<int>(icon, kOffShown) != 0 && x >= At<int>(icon, kOffLeft) && x <= At<int>(icon, kOffRight) && y >= At<int>(icon, kOffTop) && y <= At<int>(icon, kOffBottom);
}
}

void QuickIconsGameThreadTick()
{
	if (g_failed || !g_barFor) return;
	const DWORD now = GetTickCount();
	if (now - g_lastTick < 400) return;
	g_lastTick = now;
	__try {
		RefreshExtras(0);
		for (int slot = 0; slot < kRealCount + kExtraCount; slot++) {
			void *icon = IconAt(slot);
			if (icon && At<int>(icon, kOffShown) != 0 && HeroOf(icon)) {
				reinterpret_cast<Invalidate_t>(addr::kVisibleMgrInvalidateRect)(*reinterpret_cast<void **>(addr::kVisibleMgr), nullptr, reinterpret_cast<int *>(static_cast<char *>(icon) + kOffInvalid));
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed = true;
		LogE("game", "group icons: ERROR refreshing, disabled");
	}
}

void QuickIconsDraw(void *surface, int originX, int originY)
{
	if (g_failed || !Bar()) return;
	const int offsetX = At<int>(surface, 0x24), offsetY = At<int>(surface, 0x28);
	for (int slot = 0; slot < kRealCount + kExtraCount; slot++) {
		void *icon = IconAt(slot);
		if (!icon || At<int>(icon, kOffShown) == 0 || At<void *>(icon, kOffBitmap) == nullptr) continue;
		short &x = At<short>(icon, kOffPosX), &y = At<short>(icon, kOffPosY);
		const short savedX = x, savedY = y;
		x = static_cast<short>(originX + At<int>(icon, kOffLeft) - offsetX);
		y = static_cast<short>(originY + At<int>(icon, kOffTop) - offsetY);
		__try {
			reinterpret_cast<IconDraw_t>(addr::kQuickIconDraw)(icon, nullptr, surface, 0);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
		x = savedX;
		y = savedY;
	}
}

bool QuickIconsHit(int viewX, int viewY)
{
	if (g_failed) return false;
	for (int slot = 0; slot < kRealCount + kExtraCount; slot++) {
		void *icon = IconAt(slot);
		if (icon && Inside(icon, viewX, viewY)) return true;
	}
	return false;
}

bool QuickIconsMouse(int gameMessage, int viewX, int viewY)
{
	if (g_failed) return false;
	short pos[2] = {static_cast<short>(viewX), static_cast<short>(viewY)};
	for (int slot = 0; slot < kRealCount + kExtraCount; slot++) {
		void *icon = IconAt(slot);
		if (!icon || !Inside(icon, viewX, viewY)) continue;
		if (reinterpret_cast<IconMouse_t>(addr::kQuickIconOnMouse)(icon, nullptr, gameMessage, pos) == 0) return true;
	}
	return false;
}

void InstallQuickIcons(bool knownExe)
{
	if (!knownExe) return;
	if (!ExtrasEnabled()) {
		g_failed = true;
		LogI("game", "group icons: disabled in the .ini");
		return;
	}
	struct Item {
		uintptr_t address;
		void *detour;
		void **original;
		const char *name;
	};
	const Item items[] = {
		{addr::kCreateQuickIconBar, reinterpret_cast<void *>(&HookCreateBar), reinterpret_cast<void **>(&g_origCreateBar), "CreateQuickIconBar"},
		{addr::kDestroyQuickIconBar, reinterpret_cast<void *>(&HookDestroyBar), reinterpret_cast<void **>(&g_origDestroyBar), "DestroyQuickIconBar"},
		{addr::kQuickIconRefresh, reinterpret_cast<void *>(&HookRefresh), reinterpret_cast<void **>(&g_origRefresh), "CUIQuickIcon::Refresh"},
		{addr::kQuickIconBarOnMouse, reinterpret_cast<void *>(&HookBarMouse), reinterpret_cast<void **>(&g_origBarMouse), "CUIQuickIconBar::OnMouse"},
		{addr::kQuickIconRenderIcon, reinterpret_cast<void *>(&HookRenderIcon), reinterpret_cast<void **>(&g_origRenderIcon), "CUIQuickIcon::RenderIcon"},
		{addr::kQuickIconDraw, reinterpret_cast<void *>(&HookIconDraw), reinterpret_cast<void **>(&g_origIconDraw), "CUIQuickIcon::Draw"},
		{addr::kQuickIconBarRefreshAll, reinterpret_cast<void *>(&HookRefreshAll), reinterpret_cast<void **>(&g_origRefreshAll), "CUIQuickIconBar::RefreshAll"},
	};
	bool ok = true;
	for (const Item &item : items) {
		const bool created = HookCreate(reinterpret_cast<void *>(item.address), item.detour, item.original) == MH_OK;
		const bool enabled = created && HookEnable(reinterpret_cast<void *>(item.address)) == MH_OK;
		if (!enabled) LogE("game", "group icons: hook %s NOT available", item.name);
		ok = ok && enabled;
	}
	if (!ok) g_failed = true;
	LogAt(ok ? kLogInfo : kLogError, "game", "group icons: %s", ok ? "ready" : "NOT available");
}
