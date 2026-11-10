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
const size_t kTerId = 0x00, kTerState = 0x20, kTerName = 0x28, kTerDescription = 0x60, kTerBonus = 0x98, kTerShield = 0xd0;
// CVXUIConquestMap: the territory clicked last (a double click activates it).
const size_t kMapSelected = 0x58;
// CBitmap
const size_t kBmpHandle = 4, kBmpW = 8, kBmpH = 0xc, kBmpBits = 0x10, kBmpType = 0x1c;
const short kBmpType555 = 3, kBmpType565 = 6;
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

std::map<const void *, std::shared_ptr<const ConquestImage>> g_shields;
const void *g_shieldsOf = nullptr;

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

bool CopyRows(void *bmp, uint16_t *out, int w, int h)
{
	__try {
		const PixelPtr_t row = reinterpret_cast<PixelPtr_t>(addr::kBitmapGetPixelPtr);
		for (int y = 0; y < h; y++) {
			const void *from = row(bmp, nullptr, 0, y);
			if (!from) return false;
			memcpy(out + static_cast<size_t>(y) * w, from, static_cast<size_t>(w) * 2);
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

uint32_t Expand(uint16_t v, short type)
{
	const uint32_t r = type == kBmpType565 ? (v >> 11) & 31 : (v >> 10) & 31;
	const uint32_t g = type == kBmpType565 ? ((v >> 5) & 63) * 31 / 63 : (v >> 5) & 31, b = v & 31;
	return 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

// The shields are CBitmaps of 16 bits; their buttons take the pixel at (0, 0) as the transparent colour (CUIButton::SetImage).
std::shared_ptr<const ConquestImage> ReadShield(void *bmp)
{
	if (!bmp) return nullptr;
	const auto cached = g_shields.find(bmp);
	if (cached != g_shields.end()) return cached->second;
	std::shared_ptr<const ConquestImage> result;
	void **vt = At<void **>(bmp, 0);
	const int w = At<int>(bmp, kBmpW), h = At<int>(bmp, kBmpH);
	const short bits = At<short>(bmp, kBmpBits), type = At<short>(bmp, kBmpType);
	const unsigned handle = At<unsigned>(bmp, kBmpHandle);
	if (vt && At<uintptr_t>(vt, 0x10) == addr::kBitmapGetPixelPtr && bits == 16 && (type == kBmpType555 || type == kBmpType565) && handle && w >= 4 && h >= 4 &&
		w <= 512 && h <= 512) {
		std::vector<uint16_t> raw(static_cast<size_t>(w) * h);
		reinterpret_cast<MemLock_t>(addr::kMemLock)(handle);
		const bool ok = CopyRows(bmp, raw.data(), w, h);
		reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(handle);
		if (ok) {
			auto img = std::make_shared<ConquestImage>();
			img->w = w;
			img->h = h;
			img->argb.resize(raw.size());
			for (size_t i = 0; i < raw.size(); i++) img->argb[i] = raw[i] == raw[0] ? 0 : Expand(raw[i], type);
			result = img;
		}
	}
	if (!result) LogW("ui", "conquest: a shield could not be read (bitmap %dx%d, %d bits, type %d)", w, h, bits, type);
	g_shields[bmp] = result;
	return result;
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
