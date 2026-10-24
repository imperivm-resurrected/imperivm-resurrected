// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <string>
#include <cstdio>
#include <mutex>
#include <vector>
#include <cstdint>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "hooks/gamestate.h"
#include "net/mpsave.h"
#include "net/rejoin.h"
#include "mods/savelib.h"
#include "mods/mods.h"
#include "core/uiport.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/ui.h"
#include "addr/world.h"

namespace {
const int kIdSave = 0x1002;
const unsigned kFlagDisabled = 0x200;

typedef int(__fastcall *OnShow_t)(void *dlg);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *ModifyFlags_t)(void *wnd, void *edx, unsigned set, unsigned clear, int which);

OnShow_t g_origOnShow = nullptr;

bool InMultiplayer()
{
	return GameIsMultiplayer();
}

}

bool MpSaveInMultiplayer()
{
	return InMultiplayer();
}

namespace {
int __fastcall HookGameMenuOnShow(void *dlg)
{
	const int r = g_origOnShow(dlg);
	if (InMultiplayer()) {
		void **vt = *reinterpret_cast<void ***>(dlg);
		void *save = reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, kIdSave, *reinterpret_cast<const char **>(addr::kClassNameCWindow));
		if (save) reinterpret_cast<ModifyFlags_t>(addr::kModifyFlags)(save, nullptr, 0, kFlagDisabled, 1);
	}
	return r;
}

const unsigned kSaveMagic = 0x76787367;

typedef int(__fastcall *StartNetwork_t)(void *game, void *edx, int arg);
typedef void *(__cdecl *OperatorNew_t)(unsigned size);
typedef void *(__fastcall *Ctor1_t)(void *obj, void *edx, int arg);
typedef int(__fastcall *StreamOpen_t)(void *stream, void *edx, const char *path);
typedef int(__fastcall *ReadInt_t)(void *stream, void *edx, void *value, const char *name);
typedef void(__fastcall *ThisArg_t)(void *obj, void *edx, void *arg);
typedef void(__fastcall *ThisInt_t)(void *obj, void *edx, int arg);
typedef void(__fastcall *CenterOn_t)(void *mgr, void *edx, int *pos, int x);
typedef void(__fastcall *ThisOnly_t)(void *obj);

StartNetwork_t g_origStartNetwork = nullptr;
std::string g_pendingSave;

void **Vt(void *obj) { return *reinterpret_cast<void ***>(obj); }

void *OpenSaveStream(const char *path, int size[2])
{
	void *stream = nullptr, *settings = nullptr;
	__try {
		stream = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x40);
		reinterpret_cast<Ctor1_t>(addr::kStreamLoadCtor)(stream, nullptr, 1);
		reinterpret_cast<StreamOpen_t>(Vt(stream)[0x8 / 4])(stream, nullptr, path);
		unsigned magic = 0, build = 0, type = 0;
		reinterpret_cast<ReadInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, &magic, "magic");
		if (magic != kSaveMagic) return nullptr;
		reinterpret_cast<ReadInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, &build, "build");
		reinterpret_cast<ReadInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, reinterpret_cast<void *>(addr::kPersistVersion), "version");
		reinterpret_cast<ReadInt_t>(Vt(stream)[0x4c / 4])(stream, nullptr, &type, "gametype");
		if (type != 3) return nullptr;
		settings = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0xdd0);
		reinterpret_cast<Ctor1_t>(addr::kSingleSettingsCtor)(settings, nullptr, 1);
		reinterpret_cast<ThisArg_t>(Vt(settings)[0x4 / 4])(settings, nullptr, stream);
		int season = 0;
		reinterpret_cast<ReadInt_t>(Vt(stream)[0x48 / 4])(stream, nullptr, &season, nullptr);
		reinterpret_cast<ThisArg_t>(addr::kLanguagePersist)(reinterpret_cast<void *>(addr::kLanguageSettings), nullptr, stream);
		size[0] = *reinterpret_cast<int *>(static_cast<char *>(settings) + 0xdb0);
		size[1] = *reinterpret_cast<int *>(static_cast<char *>(settings) + 0xdb4);
		reinterpret_cast<ThisInt_t>(Vt(settings)[0])(settings, nullptr, 1);
		return stream;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

bool PersistInto(void *game, void *stream)
{
	__try {
		reinterpret_cast<ThisArg_t>(Vt(game)[0x90 / 4])(game, nullptr, stream);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void CloseSaveStream(void *stream)
{
	if (!stream) return;
	__try {
		reinterpret_cast<ThisOnly_t>(Vt(stream)[0xc / 4])(stream);
		reinterpret_cast<ThisInt_t>(Vt(stream)[0])(stream, nullptr, 1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

struct RawSavePlayer {
	int color;
	int control;
	int type;
	char name[64];
};

struct RawSaveMap {
	char path[260];
	int random;
	int sizeX, sizeY;
};

std::string ReadString(const char *s)
{
	const unsigned size = *reinterpret_cast<const unsigned *>(s + 0x14);
	const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
	const char *text = cap < 0x10 ? s + 4 : *reinterpret_cast<const char *const *>(s + 4);
	return text && size < 256 ? std::string(text, size) : std::string();
}

int ReadSavePlayersRaw(const char *path, RawSavePlayer *out, int max, RawSaveMap *map = nullptr)
{
	void *stream = nullptr, *settings = nullptr;
	int count = -1;
	const unsigned oldVersion = *reinterpret_cast<unsigned *>(addr::kPersistVersion);
	__try {
		stream = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x40);
		reinterpret_cast<Ctor1_t>(addr::kStreamLoadCtor)(stream, nullptr, 1);
		reinterpret_cast<StreamOpen_t>(Vt(stream)[0x8 / 4])(stream, nullptr, path);
		unsigned magic = 0, build = 0, version = 0, type = 0;
		reinterpret_cast<ReadInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, &magic, "magic");
		if (magic == kSaveMagic) {
			reinterpret_cast<ReadInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, &build, "build");
			reinterpret_cast<ReadInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, reinterpret_cast<void *>(addr::kPersistVersion), "version");
			reinterpret_cast<ReadInt_t>(Vt(stream)[0x4c / 4])(stream, nullptr, &type, "gametype");
			if (type == 3) {
				settings = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0xdd0);
				reinterpret_cast<Ctor1_t>(addr::kSingleSettingsCtor)(settings, nullptr, 1);
				reinterpret_cast<ThisArg_t>(Vt(settings)[0x4 / 4])(settings, nullptr, stream);
				count = 0;
				if (map) {
					const char *s = static_cast<const char *>(settings) + 8;
					const unsigned len = *reinterpret_cast<const unsigned *>(s + 0x14);
					const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
					const char *text = cap < 16 ? s + 4 : *reinterpret_cast<char *const *>(s + 4);
					const unsigned n = len < sizeof(map->path) - 1 ? len : sizeof(map->path) - 1;
					for (unsigned k = 0; k < n; k++) map->path[k] = text ? text[k] : 0;
					map->path[n] = 0;
					map->random = *reinterpret_cast<const int *>(static_cast<const char *>(settings) + 0x28);
					map->sizeX = *reinterpret_cast<const int *>(static_cast<const char *>(settings) + 0xdb0);
					map->sizeY = *reinterpret_cast<const int *>(static_cast<const char *>(settings) + 0xdb4);
				}
				for (int i = 0; i < max && i < 16; i++) {
					const char *p = static_cast<const char *>(settings) + 0x9c + i * 0xc0;
					out[i].type = *reinterpret_cast<const int *>(p + 8);
					out[i].color = *reinterpret_cast<const int *>(p + 0x60);
					out[i].control = *reinterpret_cast<const int *>(p + 0x10);
					const char *s = p + 0x44;
					const unsigned len = *reinterpret_cast<const unsigned *>(s + 0x14);
					const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
					const char *text = cap < 16 ? s + 4 : *reinterpret_cast<char *const *>(s + 4);
					const unsigned n = len < 63 ? len : 63;
					for (unsigned k = 0; k < n; k++) out[i].name[k] = text ? text[k] : 0;
					out[i].name[n] = 0;
					count++;
				}
				reinterpret_cast<ThisInt_t>(Vt(settings)[0])(settings, nullptr, 1);
			}
		}
		(void)version;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		count = -1;
	}
	*reinterpret_cast<unsigned *>(addr::kPersistVersion) = oldVersion;
	CloseSaveStream(stream);
	return count;
}

typedef int(__cdecl *SaveGameToFile_t)(const char *path);
SaveGameToFile_t g_origSaveGameToFile = nullptr;

std::mutex g_camLock;
int g_camX[16], g_camY[16];
bool g_camValid[16] = {};
std::string g_pendingCams;

bool LocalCamera(int &player, int &x, int &y)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *view = *reinterpret_cast<char **>(addr::kVisibleMgr);
	if (!game || !view) return false;
	char *local = *reinterpret_cast<char **>(game + 0x12c8);
	if (!local) return false;
	player = static_cast<int>((local - (game + 0x12cc)) / 800);
	if (player < 0 || player >= 16) return false;
	x = (*reinterpret_cast<int *>(view + 0x618) + *reinterpret_cast<int *>(view + 0x620)) / 2;
	y = (*reinterpret_cast<int *>(view + 0x61c) + *reinterpret_cast<int *>(view + 0x624)) / 2;
	return true;
}

std::string GameFolder()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\'));
}

int __cdecl HookSaveGameToFile(const char *path)
{
	const int r = g_origSaveGameToFile(path);
	// The binary is closed now: a required index table must describe these completed bytes.
	if (r == 0 && path && !ModsOnSaveWritten(path, InMultiplayer())) {
		LogE("saves", "could not write required save metadata for %s", path);
		static const std::string error = Tr("Could not write the save metadata. Automatic civilizations require it: fix the file permissions or free disk space, then save again.");
		UiToast(error, true);
		return static_cast<int>(reinterpret_cast<uintptr_t>(error.c_str()));
	}
	if (InMultiplayer() && path) {
		int me = 0, x = 0, y = 0;
		if (LocalCamera(me, x, y)) MpSaveSetPlayerCamera(me, x, y);
		std::string text;
		{
			std::lock_guard<std::mutex> lock(g_camLock);
			for (int i = 0; i < 16; i++) {
				if (g_camValid[i]) text += std::to_string(i) + " " + std::to_string(g_camX[i]) + " " + std::to_string(g_camY[i]) + "\n";
			}
		}
		std::string file = GameFolder() + "\\" + path;
		for (char &c : file) {
			if (c == '/') c = '\\';
		}
		FILE *f = nullptr;
		if (!text.empty() && fopen_s(&f, (file + ".cameras").c_str(), "wb") == 0 && f) {
			fwrite(text.data(), 1, text.size(), f);
			fclose(f);
		}
	}
	return r;
}

bool CenterOnSavedCamera(int local)
{
	std::string text = g_pendingCams;
	g_pendingCams.clear();
	int player = 0, x = 0, y = 0;
	for (size_t p = 0; p < text.size();) {
		const size_t e = text.find('\n', p);
		const std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
		p = e == std::string::npos ? text.size() : e + 1;
		if (sscanf_s(line.c_str(), "%d %d %d", &player, &x, &y) == 3 && player == local) {
			void *view = *reinterpret_cast<void **>(addr::kVisibleMgr);
			if (!view) return false;
			int pos[2] = {x, y};
			reinterpret_cast<CenterOn_t>(addr::kCenterOn)(view, nullptr, pos, 0);
			return true;
		}
	}
	return false;
}
int __fastcall HookStartNetwork(void *game, void *edx, int arg)
{
	const std::string path = g_pendingSave;
	g_pendingSave.clear();
	if (path.empty()) return g_origStartNetwork(game, edx, arg);
	char *gs = *reinterpret_cast<char **>(addr::kGameSettings);
	char *g = static_cast<char *>(game);
	const unsigned oldVersion = *reinterpret_cast<unsigned *>(addr::kPersistVersion);
	int size[2] = {-1, -1};
	void *stream = gs ? OpenSaveStream(path.c_str(), size) : nullptr;
	if (!stream) {
		*reinterpret_cast<unsigned *>(addr::kPersistVersion) = oldVersion;
		LogW("match", "online game: could not open the saved game \"%s\"; no new game was started", path.c_str());
		if (RejoinPendingLoad()) RejoinAfterLoad(game, false);
		// The native StartGame ABI returns a nullable error string, not a success boolean.
		static const std::string error = Tr("Could not open the saved game. No new game was started.");
		return static_cast<int>(reinterpret_cast<uintptr_t>(error.c_str()));
	}
	const int local = *reinterpret_cast<int *>(gs + 0xd74);
	*reinterpret_cast<int *>(gs + 0xdac) = 1;
	*reinterpret_cast<int *>(gs + 0xdb0) = size[0];
	*reinterpret_cast<int *>(gs + 0xdb4) = size[1];
	*reinterpret_cast<int *>(addr::kLoadingSavedGame) = 1;
	*reinterpret_cast<void **>(addr::kLoadStream) = stream;
	*reinterpret_cast<char **>(g + 0x12c8) = g + 0x12cc + local * 800;
	const int r = g_origStartNetwork(game, edx, arg);
	const bool ok = PersistInto(game, stream);
	*reinterpret_cast<int *>(gs + 0xdac) = 0;
	*reinterpret_cast<int *>(addr::kLoadingSavedGame) = 0;
	*reinterpret_cast<void **>(addr::kLoadStream) = nullptr;
	CloseSaveStream(stream);
	*reinterpret_cast<unsigned *>(addr::kPersistVersion) = oldVersion;
	if (!ok) {
		LogW("match", "online game: error restoring \"%s\" (another scenario or a damaged file?)", path.c_str());
		if (RejoinPendingLoad()) RejoinAfterLoad(game, false);
		return r;
	}
	reinterpret_cast<ThisInt_t>(addr::kSetCurrentPlayer)(game, nullptr, local);
	if (!RejoinPendingLoad()) {
		typedef void *(__fastcall * AssignCstr_t)(void *str, void *edx, const char *text);
		typedef void(__cdecl * CreatePlayerAI_t)(int player, char *profile, short difficulty);
		const AssignCstr_t assign = reinterpret_cast<AssignCstr_t>(addr::kStringAssignCstr);
		for (int i = 0; i < 8; i++) {
			char *player = g + 0x12cc + i * 800;
			const std::string current = ReadString(player + 0x90);
			if (*reinterpret_cast<uint32_t *>(gs + 0xdc8 + i * 4)) {
				const std::string name = ReadString(gs + 0x9c + i * 0xc0 + 0x44);
				if (!name.empty() && name != current) {
					LogD("match", "online game: player %d (\"%s\" in the saved game) is now played by %s", i, current.c_str(), name.c_str());
					assign(player + 0x90, nullptr, name.c_str());
				}
			}
			else if (*reinterpret_cast<int *>(player + 0x290) == 1 && !current.empty()) {
				if (!*reinterpret_cast<void **>(player + 0x88)) {
					LogI("match", "online game: nobody plays player %d (\"%s\"); an AI plays it", i, current.c_str());
					reinterpret_cast<CreatePlayerAI_t>(addr::kCreatePlayerAI)(i, nullptr, 0);
					assign(player + 0x90, nullptr, (current + "(IA)").c_str());
				}
				else {
					LogD("match", "online game: nobody plays player %d (\"%s\"); it keeps its AI from the saved game", i, current.c_str());
				}
				*reinterpret_cast<int *>(gs + 0x9c + i * 0xc0 + 0x14) = 1;
			}
		}
	}
	if (!RejoinPendingLoad()) {
		for (int i = 0; i < 8; i++) {
			if (!*reinterpret_cast<uint32_t *>(gs + 0xdc8 + i * 4)) continue;
			void **ai = reinterpret_cast<void **>(g + 0x12cc + i * 800 + 0x88);
			if (!*ai) continue;
			LogD("match", "online game: player %d is played by a human and had an AI from the saved game; it is removed", i);
			typedef void(__fastcall * Delete_t)(void *self, void *edx, int flags);
			reinterpret_cast<Delete_t>((*reinterpret_cast<void ***>(*ai))[0])(*ai, nullptr, 1);
			*ai = nullptr;
			char *name = g + 0x12cc + i * 800 + 0x90;
			const unsigned size = *reinterpret_cast<unsigned *>(name + 0x14);
			const unsigned cap = *reinterpret_cast<unsigned *>(name + 0x18);
			char *text = cap < 0x10 ? name + 4 : *reinterpret_cast<char **>(name + 4);
			if (text && size > 4 && size < 256 && text[size - 1] == ')') {
				std::string s(text, size);
				const size_t open = s.find_last_of('(');
				if (open != std::string::npos && size - open <= 6) {
					size_t end = open;
					while (end > 0 && s[end - 1] == ' ') end--;
					text[end] = 0;
					*reinterpret_cast<unsigned *>(name + 0x14) = static_cast<unsigned>(end);
					LogI("match", "online game: player %d is named \"%s\" again", i, s.substr(0, end).c_str());
				}
			}
		}
	}
	char *outQueue = g + 0x1054 + 0xf4;
	reinterpret_cast<ThisOnly_t>(addr::kCmdQueueSetDtor)(outQueue);
	reinterpret_cast<ThisOnly_t>(addr::kCmdQueueSetCtor)(outQueue);
	if (void *sel = *reinterpret_cast<void **>(addr::kSelectionManager)) reinterpret_cast<ThisInt_t>(addr::kClearSelection)(sel, nullptr, 0);
	if (CenterOnSavedCamera(local)) {
	}
	else if (void *view = *reinterpret_cast<void **>(addr::kVisibleMgr)) {
		char *player = *reinterpret_cast<char **>(g + 0x12c8);
		int pos[2] = {*reinterpret_cast<int *>(player + 0x68), *reinterpret_cast<int *>(player + 0x6c)};
		reinterpret_cast<CenterOn_t>(addr::kCenterOn)(view, nullptr, pos, 0);
	}
	LogI("match", "online game: loaded \"%s\" (local player %d)", path.c_str(), local);
	if (RejoinPendingLoad()) RejoinAfterLoad(game, true);
	return r;
}
typedef void(__fastcall *DrawScores_t)(void *self, void *edx, void *a, void *b);
DrawScores_t g_origDrawScores = nullptr;

void __fastcall HookDrawScores(void *self, void *edx, void *a, void *b)
{
	char *gs = *reinterpret_cast<char **>(addr::kGameSettings);
	if (gs && self) {
		int count = 0;
		for (int i = 0; i < 8; i++) {
			if (*reinterpret_cast<int *>(gs + 0xb0 + i * 0xc0)) count++;
		}
		int &rows = *reinterpret_cast<int *>(static_cast<char *>(self) + 0xe8);
		if (rows != count) {
			rows = count;
			typedef int(__fastcall * Update_t)(void *self);
			reinterpret_cast<Update_t>(addr::kScoreVisibleUpdate)(self);
		}
	}
	g_origDrawScores(self, edx, a, b);
}
}

void InstallMpSave(bool knownExe)
{
	if (!knownExe) return;
	if (HookCreate(reinterpret_cast<void *>(addr::kScoreVisibleDraw), reinterpret_cast<void *>(&HookDrawScores),
			reinterpret_cast<void **>(&g_origDrawScores)) == MH_OK) {
		HookEnable(reinterpret_cast<void *>(addr::kScoreVisibleDraw));
	}
	unsigned char *at = reinterpret_cast<unsigned char *>(addr::kPersistMpCheck);
	bool patched = false;
	if (at[0] == 0x74 && at[1] == 0x18) {
		DWORD old = 0;
		if (VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &old)) {
			at[0] = 0xEB;
			VirtualProtect(at, 1, old, &old);
			FlushInstructionCache(GetCurrentProcess(), at, 1);
			patched = true;
		}
	}
	void *target = reinterpret_cast<void *>(addr::kGameMenuOnShow);
	const bool hooked = HookCreate(target, reinterpret_cast<void *>(&HookGameMenuOnShow), reinterpret_cast<void **>(&g_origOnShow)) ==
			MH_OK &&
		HookEnable(target) == MH_OK;
	void *net = reinterpret_cast<void *>(addr::kStartNetwork);
	const bool load = HookCreate(net, reinterpret_cast<void *>(&HookStartNetwork), reinterpret_cast<void **>(&g_origStartNetwork)) == MH_OK &&
		HookEnable(net) == MH_OK;
	void *save = reinterpret_cast<void *>(addr::kSaveGameToFile);
	if (HookCreate(save, reinterpret_cast<void *>(&HookSaveGameToFile), reinterpret_cast<void **>(&g_origSaveGameToFile)) == MH_OK) {
		HookEnable(save);
	}
	LogAt(patched && hooked && load ? kLogInfo : kLogWarn, "saves", "online games: save %s, load %s", patched && hooked ? "enabled" : "NOT enabled (different exe)", load ? "ready" : "NOT available");
}

void MpSaveSetPendingLoad(const std::string &path)
{
	g_pendingSave = path;
}

std::string MpSavePendingLoad()
{
	return g_pendingSave;
}

MpSaveInfo MpSaveReadInfo(const std::string &path)
{
	RawSavePlayer raw[16] = {};
	RawSaveMap map = {};
	map.sizeX = map.sizeY = -1;
	SaveSessionIgnoreLoads(true);
	const int n = ReadSavePlayersRaw(path.c_str(), raw, 16, &map);
	SaveSessionIgnoreLoads(false);
	MpSaveInfo info;
	info.valid = n >= 0;
	for (int i = 0; i < n; i++) {
		MpSavePlayer p;
		p.type = raw[i].type;
		p.color555 = raw[i].color & 0xffff;
		p.control = raw[i].control;
		p.name = raw[i].name;
		info.players.push_back(p);
	}
	if (n >= 0) {
		info.map = map.path;
		info.random = map.random != 0;
		info.sizeX = map.sizeX;
		info.sizeY = map.sizeY;
		LogD("saves", "Saved game \"%s\": map \"%s\", random %d, size %dx%d, %d slots", path.c_str(), map.path, map.random, map.sizeX, map.sizeY, n);
		for (int i = 0; i < n && i < 8; i++) {
			LogD("saves", "slot %d: \"%s\", field+8=%d, control=%d, color %04x", i + 1, raw[i].name, raw[i].type, raw[i].control, raw[i].color & 0xffff);
		}
	}
	return info;
}

std::vector<MpSavePlayer> MpSaveReadPlayers(const std::string &path)
{
	return MpSaveReadInfo(path).players;
}

void MpSaveSetPlayerCamera(int player, int x, int y)
{
	if (player < 0 || player >= 16) return;
	std::lock_guard<std::mutex> lock(g_camLock);
	g_camX[player] = x;
	g_camY[player] = y;
	g_camValid[player] = true;
}

bool MpSaveLocalCamera(int &player, int &x, int &y)
{
	return LocalCamera(player, x, y);
}

void MpSaveSetPendingCameras(const std::string &text)
{
	g_pendingCams = text;
}
