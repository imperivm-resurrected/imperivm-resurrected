// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/cmdcheck.h"

#include <cstdint>

#include "addr/engine.h"
#include "addr/script.h"
#include "addr/state.h"

namespace {

typedef char *(__fastcall *FindDesc_t)(void *registry, void *edx, const char *name);
typedef int(__fastcall *IsCommandEnabled_t)(void *obj, void *edx, int index);
typedef bool(__fastcall *RunPlayerVerifier_t)(void *desc, void *edx, int player, void *out);
typedef bool(__fastcall *RunGroupVerifier_t)(void *desc, void *edx, void *list, void *out);
typedef void *(__cdecl *OperatorNew_t)(size_t size);
typedef void(__cdecl *GameFree_t)(void *p);
typedef void *(__fastcall *ObjListCtor_t)(void *self, void *edx, int a, int b);
typedef void(__fastcall *DequePush_t)(void *deque, void *edx, const uint16_t *value);
typedef void(__fastcall *StringAssign_t)(void *str, void *edx, const char *text, size_t n);

const int kDescName = 0x8, kDescMethod = 0x24, kDescParam = 0x40, kDescText2 = 0x80, kDescText3 = 0x9c;
const int kDescGroupVerifier = 0x164, kDescPlayerVerifier = 0x16c, kDescIndex = 0x1e4;
const int kObjClass = 0x3c, kObjPlayer = 0x70, kClassCommands = 0x1f8, kPlayerIndex = 8;

void AssignString(uintptr_t at, const std::string &text)
{
	reinterpret_cast<StringAssign_t>(addr::kStringAssign)(reinterpret_cast<void *>(at), nullptr, text.c_str(), text.size());
}

struct GameString {
	char raw[0x1c] = {};
	GameString() { *reinterpret_cast<unsigned *>(raw + 0x18) = 0xf; }
	~GameString()
	{
		if (*reinterpret_cast<unsigned *>(raw + 0x18) >= 0x10) reinterpret_cast<GameFree_t>(addr::kGameFree)(*reinterpret_cast<void **>(raw + 4));
	}
	GameString(const GameString &) = delete;
	GameString &operator=(const GameString &) = delete;
};

void *Registry()
{
	return *reinterpret_cast<void **>(addr::kCmdRegistry);
}

char *FindDesc(const char *name)
{
	void *registry = Registry();
	if (!registry || !name) return nullptr;
	void **vt = *reinterpret_cast<void ***>(registry);
	return reinterpret_cast<FindDesc_t>(vt[0x14 / 4])(registry, nullptr, name);
}

bool Offered(char *obj, const char *desc)
{
	char *cls = *reinterpret_cast<char **>(obj + kObjClass);
	char *head = cls ? *reinterpret_cast<char **>(cls + kClassCommands) : nullptr;
	if (!head) return false;
	const int index = *reinterpret_cast<const int *>(desc + kDescIndex);
	for (char *node = *reinterpret_cast<char **>(head); node && node != head; node = *reinterpret_cast<char **>(node)) {
		if (*reinterpret_cast<int *>(node + 8) == index) return true;
	}
	return false;
}

bool Enabled(char *obj, const char *desc)
{
	void **vt = *reinterpret_cast<void ***>(obj);
	return reinterpret_cast<IsCommandEnabled_t>(vt[0xd8 / 4])(obj, nullptr, *reinterpret_cast<const int *>(desc + kDescIndex)) != 0;
}

bool RunGroupVerifier(uint16_t handle, char *desc)
{
	void *list = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x2c);
	if (!list) return false;
	list = reinterpret_cast<ObjListCtor_t>(addr::kObjListCtor)(list, nullptr, 0, 1);
	reinterpret_cast<DequePush_t>(addr::kDequeUShortPushBack)(static_cast<char *>(list) + 8, nullptr, &handle);
	const uintptr_t texts[3] = {addr::kCmdContextParam, addr::kCmdContextText2, addr::kCmdContextText3};
	const int fields[3] = {kDescParam, kDescText2, kDescText3};
	std::string saved[3];
	for (int i = 0; i < 3; i++) {
		saved[i] = GameStringAt(reinterpret_cast<const char *>(texts[i]));
		AssignString(texts[i], GameStringAt(desc + fields[i]));
	}
	int *ints = reinterpret_cast<int *>(addr::kCmdContextInts);
	const int savedInts[4] = {ints[0], ints[1], ints[2], ints[3]};
	ints[0] = *reinterpret_cast<int *>(desc + 0x1e8);
	ints[1] = *reinterpret_cast<int *>(desc + 0x1ec);
	ints[2] = *reinterpret_cast<int *>(desc + 0x1f4);
	ints[3] = *reinterpret_cast<int *>(desc + 0x1f0);
	GameString label;
	const bool ok = reinterpret_cast<RunGroupVerifier_t>(addr::kRunGroupVerifier)(desc, nullptr, list, label.raw);
	for (int i = 0; i < 3; i++) AssignString(texts[i], saved[i]);
	for (int i = 0; i < 4; i++) ints[i] = savedInts[i];
	return ok;
}

bool Verified(char *obj, uint16_t handle, char *desc)
{
	if (*reinterpret_cast<int *>(desc + kDescPlayerVerifier)) {
		char *player = *reinterpret_cast<char **>(obj + kObjPlayer);
		if (!player) return false;
		GameString label;
		return reinterpret_cast<RunPlayerVerifier_t>(addr::kRunPlayerVerifier)(desc, nullptr, *reinterpret_cast<int *>(player + kPlayerIndex), label.raw);
	}
	if (*reinterpret_cast<int *>(desc + kDescGroupVerifier)) return RunGroupVerifier(handle, desc);
	return true;
}

}

std::string GameStringAt(const void *at)
{
	const char *s = static_cast<const char *>(at);
	if (!s) return std::string();
	const unsigned size = *reinterpret_cast<const unsigned *>(s + 0x14);
	const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
	const char *text = cap < 0x10 ? s + 4 : *reinterpret_cast<const char *const *>(s + 4);
	return text && size < 0x10000 ? std::string(text, size) : std::string();
}

std::vector<GameCommand> GameCommands()
{
	std::vector<GameCommand> out;
	char *registry = static_cast<char *>(Registry());
	if (!registry) return out;
	char **begin = *reinterpret_cast<char ***>(registry - 0x10);
	char **end = *reinterpret_cast<char ***>(registry - 0xc);
	if (!begin || end < begin || end - begin > 20000) return out;
	for (char **it = begin; it != end; ++it) {
		if (!*it) continue;
		out.push_back({GameStringAt(*it + kDescName), GameStringAt(*it + kDescMethod), GameStringAt(*it + kDescParam)});
	}
	return out;
}

int GameCommandCount()
{
	char *registry = static_cast<char *>(Registry());
	if (!registry) return 0;
	char **begin = *reinterpret_cast<char ***>(registry - 0x10);
	char **end = *reinterpret_cast<char ***>(registry - 0xc);
	return begin && end >= begin && end - begin <= 20000 ? static_cast<int>(end - begin) : 0;
}

bool GameCommandCost(const char *name, int *gold, int *food)
{
	const char *desc = FindDesc(name);
	if (!desc) return false;
	*gold = *reinterpret_cast<const int *>(desc + 0x1e8);
	*food = *reinterpret_cast<const int *>(desc + 0x1ec);
	return true;
}

bool OffersCommand(unsigned handle, const char *command)
{
	const uint16_t index = static_cast<uint16_t>(handle & 0xffff);
	char *obj = reinterpret_cast<char **>(addr::kHandleTable)[index];
	char *desc = obj ? FindDesc(command) : nullptr;
	return desc && Offered(obj, desc);
}

bool CanExecCommand(unsigned handle, const char *command)
{
	const uint16_t index = static_cast<uint16_t>(handle & 0xffff);
	char *obj = reinterpret_cast<char **>(addr::kHandleTable)[index];
	char *desc = obj ? FindDesc(command) : nullptr;
	return desc && Offered(obj, desc) && Enabled(obj, desc) && Verified(obj, index, desc);
}
