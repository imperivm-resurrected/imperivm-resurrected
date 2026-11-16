// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include "MinHook.h"
#include "net/steamnet.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"

#include "net/rejoin_internal.h"

using namespace rejoin_impl;

namespace rejoin_impl {
namespace {
typedef unsigned(__fastcall *Time_t)(void *os);

typedef void *(__cdecl *VfsOpen_t)(const char *path, int mode, int *error);
}
}

namespace rejoin_impl {
HookOriginals g_orig;

std::mutex g_mx;
std::deque<Msg> g_queue;

RejoinSession g_session;
RejoinBlobs g_blobs;
}

namespace rejoin_impl {
namespace {
unsigned g_savedQuit[2] = {0, 0};
bool g_quitRaised = false;
}
}

namespace rejoin_impl {
namespace {
bool g_failed = false;
}
}

namespace rejoin_impl {
void *Game() { return *reinterpret_cast<void **>(addr::kGame); }
char *Transport() { return *reinterpret_cast<char **>(addr::kTransport); }
void **Vt(void *obj) { return *reinterpret_cast<void ***>(obj); }

unsigned Now()
{
	void *os = *reinterpret_cast<void **>(addr::kWinOs);
	return os ? reinterpret_cast<Time_t>(Vt(os)[0x5c / 4])(os) : GetTickCount();
}

char *Record(char *t, int peer, int tick)
{
	return t + 0xa4 + (peer + (tick % 3) * 8) * 0x20;
}
}

namespace rejoin_impl {
namespace {
std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\'));
}
}
}

namespace rejoin_impl {
std::string DiskPath(const std::string &vfs)
{
	std::string d = GameDir() + "\\" + vfs;
	for (char &c : d) {
		if (c == '/') c = '\\';
	}
	return d;
}

bool ReadFileAll(const std::string &path, std::string &out)
{
	FILE *f = _fsopen(path.c_str(), "rb", _SH_DENYNO);
	if (!f) return false;
	out.clear();
	char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

bool WriteFileAll(const std::string &path, const std::string &data)
{
	CreateDirectoryA(path.substr(0, path.find_last_of('\\')).c_str(), nullptr);
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
	const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	fclose(f);
	return ok;
}
}

namespace rejoin_impl {
void SetStatus(const std::string &text, bool failed)
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_session.status = text;
	g_failed = failed;
}

void RaiseQuitTimeouts()
{
	if (g_quitRaised) return;
	unsigned *a = reinterpret_cast<unsigned *>(addr::kQuitInitial), *b = reinterpret_cast<unsigned *>(addr::kQuitSubsequent);
	g_savedQuit[0] = *a;
	g_savedQuit[1] = *b;
	*a = *b = 10 * 60 * 1000;
	g_quitRaised = true;
}
}

namespace rejoin_impl {
namespace {
void RestoreQuitTimeouts()
{
	if (!g_quitRaised) return;
	*reinterpret_cast<unsigned *>(addr::kQuitInitial) = g_savedQuit[0];
	*reinterpret_cast<unsigned *>(addr::kQuitSubsequent) = g_savedQuit[1];
	g_quitRaised = false;
}
}
}

namespace rejoin_impl {
void Unfreeze()
{
	g_session.freezeTick = -1;
	g_session.frozen = false;
	RestoreQuitTimeouts();
}

void ResetRole()
{
	g_session.role = kNoRole;
	g_session.phase = 0;
	g_session.peer = 0;
	g_session.slot = -1;
	g_session.deadline = 0;
	g_session.savePath.clear();
	g_blobs.wireRaw.clear();
	g_blobs.save.clear();
	g_session.saveDone = false;
	SteamRejoinJoinEnd();
}

std::vector<uint64_t> OtherMembers()
{
	std::vector<uint64_t> out;
	for (const OnlineMember &m : SteamCurrentRoom().members) {
		if (!m.me) out.push_back(m.id);
	}
	return out;
}

bool InOnlineGame()
{
	return Game() && Transport() && SteamOnlineReady();
}

int SlotOfNumber(int k)
{
	char *gs = *reinterpret_cast<char **>(addr::kGameSettings);
	if (!gs || k <= 0) return -1;
	const uint32_t host = 0x7f4d0000u | static_cast<uint32_t>(k);
	const uint32_t net = (static_cast<uint32_t>(k) << 24) | 0x00004d7fu;
	for (int i = 0; i < 8; i++) {
		const uint32_t ip = *reinterpret_cast<uint32_t *>(gs + 0xdc8 + i * 4);
		if (ip == host || ip == net) return i;
	}
	return -1;
}

void EnsureLogDir()
{
	CreateDirectoryA(DiskPath("logs").c_str(), nullptr);
	CreateDirectoryA(DiskPath("logs/imperivm").c_str(), nullptr);
	CreateDirectoryA(DiskPath(kLogDir).c_str(), nullptr);
}

bool DumpGameState(const char *vfsPath)
{
	EnsureLogDir();
	void *game = Game();
	if (!game) return false;
	__try {
		int err = 0;
		void *f = (*reinterpret_cast<VfsOpen_t *>(addr::kVfsOpenFilePtr))(vfsPath, 0x1e, &err);
		if (!f) return false;
		reinterpret_cast<ThisPtr_t>(Vt(game)[0x94 / 4])(game, nullptr, f);
		reinterpret_cast<ThisInt_t>(Vt(f)[0x8 / 4])(f, nullptr, 1);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

NativeState g_natives;
}
