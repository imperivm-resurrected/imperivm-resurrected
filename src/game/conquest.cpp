// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/conquest.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "addr/engine.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/log.h"

// CVXUIConquestGame (g_pConquestGameDlg) and CConquestMapData (g_pConquestMapData), read as the game's own functions read them:
// UpdateTerritoryInfo 006c94d0, UpdateTerritoryButtons 006c9220, GetSelectedTerritory 006c91c0 and RebuildTintedBitmap 00501160.
namespace {
const unsigned kMsgButtonClick = 0x12345604, kFlagVisible = 2, kFlagBusy = 0x100, kFlagDisabled = 0x200;
const int kIdMap = 0x1001, kIdDifficulty = 0x1003;
const size_t kTerritorySize = 0xe8, kMaxTerritories = 64, kMaxPath = 32;
// CConquestMapData
const size_t kDataTerritories = 0x04, kDataPathBegin = 0x4c, kDataPathEnd = 0x50, kDataChoosing = 0x70;
// CConquestTerritory: std::string objects (MSVC: buffer or pointer at +4, size at +0x14, capacity at +0x18).
// +0x1c: the colour of the territory in territories.bmp (FindTerritoryByIndex 00500fd0).
const size_t kTerId = 0x00, kTerPaletteIndex = 0x1c, kTerState = 0x20, kTerName = 0x28, kTerDescription = 0x60, kTerBonus = 0x98, kTerShield = 0xd0;
// CVXUIConquestMap: its rectangle (shorts, inclusive), the territory clicked last (a double click activates it), OnMouse in the vtable
// and the renderer, whose territories.bmp (8 bits) and tinted copy of global.bmp are what Paint 006ca830 draws.
const size_t kCtlRect = 0x10, kMapSelected = 0x58, kMapRenderer = 0x64, kSlotOnMouse = 0x58;
const size_t kRendererAreas = 0x1c, kRendererTinted = 0x20;
// CBitmap
const size_t kBmpHandle = 4, kBmpW = 8, kBmpH = 0xc, kBmpBits = 0x10, kBmpType = 0x1c;
const short kBmpType555 = 3, kBmpType565 = 6;
const int kMaxShieldSide = 512, kMaxMapSide = 4096;
// RebuildTintedBitmap rewrites the same bitmap in place: besides a change of territories, it is read again now and then.
const DWORD kMapRereadMs = 3000;
// A land of middle brightness, in the byte order TintColor reads (blue, green, red).
const uint8_t kLandSample[4] = {150, 170, 190, 0};

typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef int(__fastcall *HasFlags_t)(void *window, void *edx, unsigned flags);
typedef void(__fastcall *SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int extra);
typedef int(__fastcall *ComboGetSelected_t)(void *combo);
typedef void(__fastcall *ComboSetSelected_t)(void *combo, void *edx, int index, int notify);
typedef int(__fastcall *IsAttackable_t)(void *data, void *edx, int index);
typedef void(__fastcall *TintColor_t)(void *data, void *edx, uint32_t *out, const uint8_t *in, int state);
typedef void *(__fastcall *PixelPtr_t)(void *bitmap, void *edx, int x, int y);
typedef int(__cdecl *MemLock_t)(unsigned handle);
typedef void(__cdecl *MemUnlock_t)(unsigned handle);
typedef void(__fastcall *MapOnMouse_t)(void *map, void *edx, int event, short *at);

std::map<const void *, std::shared_ptr<const ConquestImage>> g_shields;
const void *g_shieldsOf = nullptr;

struct MapCache {
	const void *data = nullptr, *tinted = nullptr, *areas = nullptr;
	uint32_t tint = 0;
	DWORD readAt = 0;
	bool warned = false;
	std::shared_ptr<const ConquestImage> map;
	std::shared_ptr<const ConquestRegions> regions;
};
MapCache g_map;

bool Copy(void *to, const void *from, size_t n)
{
	__try {
		memcpy(to, from, n);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

template <typename T>
T At(const void *base, size_t off)
{
	T value{};
	if (base) Copy(&value, static_cast<const char *>(base) + off, sizeof(T));
	return value;
}

std::string ReadString(const char *s)
{
	unsigned char head[0x1c];
	if (!Copy(head, s, sizeof(head))) return std::string();
	unsigned size = 0, cap = 0;
	memcpy(&size, head + 0x14, 4);
	memcpy(&cap, head + 0x18, 4);
	if (size == 0 || size > 16384 || size > cap) return std::string();
	if (cap < 16) return std::string(reinterpret_cast<const char *>(head) + 4, size);
	const char *data = nullptr;
	memcpy(&data, head + 4, 4);
	std::string out(size, '\0');
	return data && Copy(&out[0], data, size) ? out : std::string();
}

bool HasFlags(void *window, unsigned flags)
{
	return reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(window, nullptr, flags) != 0;
}

void *Child(void *dlg, int id, uintptr_t classAddr)
{
	return reinterpret_cast<GetChild_t>((*reinterpret_cast<void ***>(dlg))[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(classAddr));
}

void *OpenDialog()
{
	void *dlg = *reinterpret_cast<void **>(addr::kConquestGameDlg);
	return dlg && *reinterpret_cast<void **>(addr::kConquestMapData) && HasFlags(dlg, kFlagVisible) ? dlg : nullptr;
}

ConquestButton Button(void *dlg, int id)
{
	ConquestButton b;
	void *button = Child(dlg, id, addr::kClassNameCUIButton);
	if (!button) return b;
	b.visible = HasFlags(button, kFlagVisible);
	b.enabled = b.visible && !HasFlags(button, kFlagDisabled) && !HasFlags(button, kFlagBusy);
	return b;
}

bool CopyRows(void *bmp, uint8_t *out, size_t rowBytes, int h)
{
	__try {
		const PixelPtr_t row = reinterpret_cast<PixelPtr_t>(addr::kBitmapGetPixelPtr);
		for (int y = 0; y < h; y++) {
			const void *from = row(bmp, nullptr, 0, y);
			if (!from) return false;
			memcpy(out + static_cast<size_t>(y) * rowBytes, from, rowBytes);
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

struct RawBitmap {
	int w = 0, h = 0;
	short bits = 0, type = 0;
	std::vector<uint8_t> px;
};

// The rows of one of the game's CBitmaps, without padding; false unless it has `bits` bits a pixel and fits in `maxSide`.
bool ReadBitmap(void *bmp, short bits, int maxSide, RawBitmap &out)
{
	if (!bmp) return false;
	void **vt = At<void **>(bmp, 0);
	out.w = At<int>(bmp, kBmpW);
	out.h = At<int>(bmp, kBmpH);
	out.bits = At<short>(bmp, kBmpBits);
	out.type = At<short>(bmp, kBmpType);
	const unsigned handle = At<unsigned>(bmp, kBmpHandle);
	if (!vt || At<uintptr_t>(vt, 0x10) != addr::kBitmapGetPixelPtr || out.bits != bits || !handle || out.w < 4 || out.h < 4 || out.w > maxSide ||
		out.h > maxSide)
		return false;
	const size_t rowBytes = static_cast<size_t>(out.w) * (bits / 8);
	out.px.resize(rowBytes * out.h);
	reinterpret_cast<MemLock_t>(addr::kMemLock)(handle);
	const bool ok = CopyRows(bmp, out.px.data(), rowBytes, out.h);
	reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(handle);
	return ok;
}

uint32_t Expand(uint16_t v, short type)
{
	const uint32_t r = type == kBmpType565 ? (v >> 11) & 31 : (v >> 10) & 31;
	const uint32_t g = type == kBmpType565 ? ((v >> 5) & 63) * 31 / 63 : (v >> 5) & 31, b = v & 31;
	return 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

// A 16-bit picture; with `keyed`, the colour of its pixel at (0, 0) turns transparent.
std::shared_ptr<const ConquestImage> ReadPicture(void *bmp, int maxSide, bool keyed, RawBitmap &raw)
{
	if (!ReadBitmap(bmp, 16, maxSide, raw) || (raw.type != kBmpType555 && raw.type != kBmpType565)) return nullptr;
	auto img = std::make_shared<ConquestImage>();
	img->w = raw.w;
	img->h = raw.h;
	img->argb.resize(static_cast<size_t>(raw.w) * raw.h);
	const uint16_t *px = reinterpret_cast<const uint16_t *>(raw.px.data());
	for (size_t i = 0; i < img->argb.size(); i++) img->argb[i] = keyed && px[i] == px[0] ? 0 : Expand(px[i], raw.type);
	return img;
}

// The shields are CBitmaps of 16 bits; their buttons take the pixel at (0, 0) as the transparent colour (CUIButton::SetImage).
std::shared_ptr<const ConquestImage> ReadShield(void *bmp)
{
	if (!bmp) return nullptr;
	const auto cached = g_shields.find(bmp);
	if (cached != g_shields.end()) return cached->second;
	RawBitmap raw;
	std::shared_ptr<const ConquestImage> result = ReadPicture(bmp, kMaxShieldSide, true, raw);
	if (!result) LogW("ui", "conquest: a shield could not be read (bitmap %dx%d, %d bits, type %d)", raw.w, raw.h, raw.bits, raw.type);
	g_shields[bmp] = result;
	return result;
}

// territories.bmp has one colour per territory: each pixel becomes the number of its territory.
std::shared_ptr<const ConquestRegions> ReadRegions(void *bmp, const char *territories, size_t count)
{
	RawBitmap raw;
	if (!ReadBitmap(bmp, 8, kMaxMapSide, raw)) {
		LogW("ui", "conquest: the territories of the map could not be read (bitmap %dx%d, %d bits)", raw.w, raw.h, raw.bits);
		return nullptr;
	}
	uint8_t owner[256];
	std::fill(std::begin(owner), std::end(owner), kConquestNoTerritory);
	for (size_t i = 0; i < count; i++) {
		const int index = At<int>(territories + i * kTerritorySize, kTerPaletteIndex);
		if (index >= 0 && index < 256 && owner[index] == kConquestNoTerritory) owner[index] = static_cast<uint8_t>(i);
	}
	auto regions = std::make_shared<ConquestRegions>();
	regions->w = raw.w;
	regions->h = raw.h;
	regions->territory.resize(raw.px.size());
	for (size_t i = 0; i < raw.px.size(); i++) regions->territory[i] = owner[raw.px[i]];
	return regions;
}

uint32_t TintKey(const ConquestSnapshot &s)
{
	uint32_t key = 2166136261u;
	for (const ConquestTerritory &t : s.territories) key = (key ^ static_cast<uint32_t>(t.standing * 2 + (t.disabled ? 1 : 0))) * 16777619u;
	return key;
}

void ReadMap(void *mapCtl, const void *data, const char *territories, size_t count, ConquestSnapshot &s)
{
	const char *renderer = static_cast<const char *>(mapCtl) + kMapRenderer;
	void *tinted = At<void *>(renderer, kRendererTinted), *areas = At<void *>(renderer, kRendererAreas);
	MapCache &c = g_map;
	if (data != c.data || areas != c.areas) {
		c = MapCache();
		c.data = data;
		c.areas = areas;
		c.regions = ReadRegions(areas, territories, count);
	}
	const uint32_t key = TintKey(s);
	if (tinted != c.tinted || key != c.tint || GetTickCount() - c.readAt >= kMapRereadMs) {
		RawBitmap raw;
		std::shared_ptr<const ConquestImage> fresh = ReadPicture(tinted, kMaxMapSide, false, raw);
		// The same pixels keep the same picture, so the screen does not build its copy again.
		if (!fresh || !c.map || fresh->w != c.map->w || fresh->h != c.map->h || fresh->argb != c.map->argb) c.map = fresh;
		if (!c.map && !c.warned) LogW("ui", "conquest: the map could not be read (bitmap %dx%d, %d bits, type %d)", raw.w, raw.h, raw.bits, raw.type);
		c.warned = !c.map;
		c.tinted = tinted;
		c.tint = key;
		c.readAt = GetTickCount();
	}
	s.map = c.map;
	if (c.map && c.regions && c.regions->w == c.map->w && c.regions->h == c.map->h) s.regions = c.regions;
}

void ReadColors(void *data, ConquestSnapshot &s)
{
	const TintColor_t tint = reinterpret_cast<TintColor_t>(addr::kConquestTintColor);
	// TintColor's states: 1 owned, 0 enemy within reach, 2 the rest.
	const int states[kStandingCount] = {1, 0, 2};
	for (int i = 0; i < kStandingCount; i++) {
		uint32_t out = 0;
		tint(data, nullptr, &out, kLandSample, states[i]);
		s.colors[i] = out & 0xffffff;
	}
}

void ReadDifficulties(void *dlg, ConquestSnapshot &s)
{
	void *combo = Child(dlg, kIdDifficulty, addr::kClassNameCUIComboBox);
	char *list = combo ? At<char *>(combo, 0x64) : nullptr;
	if (!list) return;
	const unsigned count = At<unsigned>(list, 0x58);
	const char *items = At<const char *>(list, 0x94);
	for (unsigned i = 0; items && i < count && i < 8; i++) {
		const char *text = At<const char *>(items, i * 0x14);
		char buf[128] = {};
		if (text) Copy(buf, text, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		s.difficulties.push_back(buf);
	}
	s.difficulty = reinterpret_cast<ComboGetSelected_t>(addr::kComboGetSelected)(combo);
}

void ReadConfirm(ConquestSnapshot &s)
{
	void *confirm = *reinterpret_cast<void **>(addr::kConfirmDlg);
	if (!confirm || !HasFlags(confirm, kFlagVisible)) return;
	s.confirm = true;
	for (int i = 0; i < 4; i++) s.confirmRect[i] = At<short>(confirm, 0x10 + i * 2);
}
}

ConquestSnapshot ConquestRead()
{
	ConquestSnapshot s;
	void *dlg = OpenDialog();
	if (!dlg) return s;
	char *data = *reinterpret_cast<char **>(addr::kConquestMapData);
	if (data != g_shieldsOf) {
		g_shields.clear();
		g_shieldsOf = data;
	}
	s.open = true;
	s.choosing = At<int>(data, kDataChoosing) != 0;
	const char *begin = At<const char *>(data, kDataTerritories), *end = At<const char *>(data, kDataTerritories + 4);
	const size_t count = begin && end > begin ? std::min<size_t>(kMaxTerritories, (end - begin) / kTerritorySize) : 0;
	const IsAttackable_t attackable = reinterpret_cast<IsAttackable_t>(addr::kConquestIsAttackable);
	for (size_t i = 0; i < count; i++) {
		const char *t = begin + i * kTerritorySize;
		ConquestTerritory ter;
		ter.name = ReadString(t + kTerName);
		if (ter.name.empty()) ter.name = ReadString(t + kTerId);
		ter.description = ReadString(t + kTerDescription);
		ter.bonus = ReadString(t + kTerBonus);
		const int state = At<int>(t, kTerState);
		ter.disabled = state == 2;
		ter.standing = state == 1 ? kStandingOwned : state == 0 && attackable(data, nullptr, static_cast<int>(i)) ? kStandingConquerable : kStandingOutOfReach;
		ter.shield = ReadShield(At<void *>(t, kTerShield));
		s.territories.push_back(std::move(ter));
	}
	const int *pathBegin = At<const int *>(data, kDataPathBegin), *pathEnd = At<const int *>(data, kDataPathEnd);
	const size_t steps = pathBegin && pathEnd > pathBegin ? std::min<size_t>(kMaxPath, pathEnd - pathBegin) : 0;
	for (size_t i = 0; i < steps; i++) {
		const int index = At<int>(pathBegin, i * 4);
		if (index >= 0 && static_cast<size_t>(index) < count) s.path.push_back(index);
	}
	if (void *map = Child(dlg, kIdMap, addr::kClassNameCVXUIConquestMap)) {
		const int selected = At<int>(map, kMapSelected);
		if (selected >= 0 && static_cast<size_t>(selected) < count) s.selected = selected;
		ReadMap(map, data, begin, count, s);
	}
	ReadDifficulties(dlg, s);
	ReadColors(data, s);
	s.start = Button(dlg, kConquestStart);
	s.select = Button(dlg, kConquestSelect);
	s.save = Button(dlg, kConquestSave);
	s.quit = Button(dlg, kConquestQuit);
	ReadConfirm(s);
	return s;
}

void ConquestPress(int id)
{
	void *dlg = OpenDialog();
	void *button = dlg ? Child(dlg, id, addr::kClassNameCUIButton) : nullptr;
	if (!button || !Button(dlg, id).enabled) return;
	LogI("ui", "conquest: pressing the game's button %#x", id);
	char *b = static_cast<char *>(button);
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(button))[0x6c / 4])(button, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgButtonClick,
		*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
}

// Notifying makes the combo send 0x1a000004, on which the dialog stores the level (OnMessage 006ca230).
void ConquestSetDifficulty(int index)
{
	void *dlg = OpenDialog();
	void *combo = dlg ? Child(dlg, kIdDifficulty, addr::kClassNameCUIComboBox) : nullptr;
	if (!combo) return;
	LogI("ui", "conquest: difficulty %d", index);
	reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(combo, nullptr, index, 1);
}

// OnMouse 006ca6b0 takes the point in the dialog's coordinates and hit-tests territories.bmp at it minus the control's corner; in mode 1
// (the only one the game sets) a press selects and notifies 0x1700ff23, a double click notifies 0x1700d2ed: the dialog does the rest.
void ConquestMapMouse(int event, int x, int y)
{
	void *dlg = OpenDialog();
	void *map = dlg ? Child(dlg, kIdMap, addr::kClassNameCVXUIConquestMap) : nullptr;
	if (!map) return;
	void **vt = At<void **>(map, 0);
	if (!vt || At<uintptr_t>(vt, kSlotOnMouse) != addr::kConquestMapOnMouse) {
		LogW("ui", "conquest: the map control is not the game's, its click is left out");
		return;
	}
	const short left = At<short>(map, kCtlRect), top = At<short>(map, kCtlRect + 2), right = At<short>(map, kCtlRect + 4), bottom = At<short>(map, kCtlRect + 6);
	if (x < 0 || y < 0 || x > right - left || y > bottom - top) return;
	short at[2] = {static_cast<short>(left + x), static_cast<short>(top + y)};
	if (event != kConquestMouseUp) LogD("ui", "conquest: map event %#x at %d,%d of the picture", event, x, y);
	reinterpret_cast<MapOnMouse_t>(addr::kConquestMapOnMouse)(map, nullptr, event, at);
}
