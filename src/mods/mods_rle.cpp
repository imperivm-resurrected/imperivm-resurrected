// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "addr/engine.h"
#include "core/log.h"
#include "core/sha256.h"
#include "mods/rletail.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

namespace mods_impl {
namespace {
typedef void *(__cdecl *OpenMapped_t)(const char *path);
OpenMapped_t g_origOpenMapped = nullptr;
std::mutex g_rleLock;
std::string g_rlePath;
std::map<std::string, std::string> g_rleProblems;

bool FileInfo(const std::string &path, uint64_t &size, uint64_t &time)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
	size = static_cast<uint64_t>(a.nFileSizeHigh) << 32 | a.nFileSizeLow;
	time = static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32 | a.ftLastWriteTime.dwLowDateTime;
	return true;
}

std::string ReadSmall(const std::string &path)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return "";
	std::string text(4096, '\0');
	DWORD got = 0;
	const BOOL ok = ReadFile(h, &text[0], static_cast<DWORD>(text.size()), &got, nullptr);
	CloseHandle(h);
	text.resize(ok ? got : 0);
	return text;
}

bool WriteSmall(const std::string &path, const std::string &text)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD put = 0;
	const BOOL ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr);
	CloseHandle(h);
	return ok && put == text.size();
}

bool CopyRange(HANDLE from, uint64_t offset, HANDLE to, std::vector<char> &buffer, Sha256 *hash)
{
	LARGE_INTEGER at;
	at.QuadPart = static_cast<LONGLONG>(offset);
	if (!SetFilePointerEx(from, at, nullptr, FILE_BEGIN)) return false;
	for (;;) {
		DWORD got = 0;
		if (!ReadFile(from, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) return false;
		if (!got) return true;
		if (hash) hash->Add(buffer.data(), got);
		if (to != INVALID_HANDLE_VALUE) {
			DWORD put = 0;
			if (!WriteFile(to, buffer.data(), got, &put, nullptr) || put != got) return false;
		}
	}
}

std::string HashFile(const std::string &path)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (h == INVALID_HANDLE_VALUE) return "";
	std::vector<char> buffer(4 << 20);
	Sha256 sha;
	const bool ok = CopyRange(h, 0, INVALID_HANDLE_VALUE, buffer, &sha);
	CloseHandle(h);
	return ok ? sha.Hex() : "";
}

bool BuildCombined(const std::string &base, const std::string &tail, const std::string &out)
{
	const std::string tmp = out + ".tmp";
	HANDLE b = CreateFileA(base.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	HANDLE t = CreateFileA(tail.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	HANDLE o = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	std::vector<char> buffer(4 << 20);
	const bool ok = b != INVALID_HANDLE_VALUE && t != INVALID_HANDLE_VALUE && o != INVALID_HANDLE_VALUE && CopyRange(b, 0, o, buffer, nullptr) &&
		CopyRange(t, kRleTailHeaderSize, o, buffer, nullptr);
	if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
	if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
	if (o != INVALID_HANDLE_VALUE) CloseHandle(o);
	if (!ok || !MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileA(tmp.c_str());
		return false;
	}
	return true;
}

void SetProblem(const std::string &folder, const std::string &text)
{
	std::lock_guard<std::mutex> lock(g_rleLock);
	g_rleProblems[folder] = text;
}

void *__cdecl HookOpenMapped(const char *path)
{
	if (path && _stricmp(path, "rle.mmp") == 0) {
		std::string redirect;
		{
			std::lock_guard<std::mutex> lock(g_rleLock);
			redirect = g_rlePath;
		}
		if (!redirect.empty()) {
			if (void *r = g_origOpenMapped(redirect.c_str())) {
				LogI("content", "graphics: rle.mmp opened from %s", redirect.c_str());
				return r;
			}
			LogW("content", "graphics: could not open %s; the game's rle.mmp is used", redirect.c_str());
		}
	}
	return g_origOpenMapped(path);
}
}

uint64_t GameRleSize()
{
	static uint64_t size = 0;
	static bool known = false;
	if (!known) {
		uint64_t time = 0;
		known = FileInfo(GameDir() + "\\rle.mmp", size, time);
	}
	return size;
}

std::string RleProblemOf(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_rleLock);
	const auto it = g_rleProblems.find(folder);
	return it == g_rleProblems.end() ? std::string() : it->second;
}

void PrepareRleMmp(const ModProfile &p, const std::vector<ModInfo> &all)
{
	{
		std::lock_guard<std::mutex> lock(g_rleLock);
		g_rlePath.clear();
		g_rleProblems.clear();
	}
	// The first package with a tail provides it; the tail of a ruleset the profile mounts counts as its package's and goes first.
	struct Provider {
		std::string folder, tailDir;
		uint64_t rleBaseSize = 0;
		std::string rleBaseSha256;
	} found;
	bool have = false;
	for (const std::string &folder : p.mods) {
		for (const ModInfo &m : all) {
			if (_stricmp(m.folder.c_str(), folder.c_str()) != 0 || !m.forbidden.empty() || have) continue;
			const ModLayer *layer = ModsActiveLayer(p, m);
			if (layer && layer->rleTail && layer->rleTailError.empty()) {
				found = {m.folder, ModsDir() + "\\" + m.folder + "\\" + layer->path, layer->rleBaseSize, layer->rleBaseSha256};
				std::replace(found.tailDir.begin(), found.tailDir.end(), '/', '\\');
				have = true;
			}
			else if (m.rleTail && m.rleTailError.empty()) {
				found = {m.folder, ModsDir() + "\\" + m.folder, m.rleBaseSize, m.rleBaseSha256};
				have = true;
			}
		}
	}
	if (!have) return;
	const Provider *provider = &found;
	const std::string base = GameDir() + "\\rle.mmp";
	const std::string tail = provider->tailDir + "\\" + kRleTailFile;
	RleCacheSource src;
	src.tailPath = tail;
	if (!FileInfo(base, src.baseSize, src.baseTime) || !FileInfo(tail, src.tailSize, src.tailTime)) {
		LogW("content", "graphics: rle.mmp or %s\\%s cannot be read", provider->folder.c_str(), kRleTailFile);
		return;
	}
	if (src.baseSize != provider->rleBaseSize) {
		LogW("content", "graphics: %s extends an rle.mmp of %u KB and the game's has %u KB", provider->folder.c_str(),
			static_cast<unsigned>(provider->rleBaseSize >> 10), static_cast<unsigned>(src.baseSize >> 10));
		SetProblem(provider->folder, "Made for another version of the game's graphics (rle.mmp): its new graphics cannot be shown");
		return;
	}
	const std::string dir = GameDir() + "\\imperivm-resurrected\\_cache";
	CreateDirectoryA((GameDir() + "\\imperivm-resurrected").c_str(), nullptr);
	CreateDirectoryA(dir.c_str(), nullptr);
	const std::string cache = dir + "\\rle.mmp", keyFile = cache + ".key";
	const std::string key = RleCacheKey(src);
	uint64_t cacheSize = 0, cacheTime = 0;
	if (ReadSmall(keyFile) == key && FileInfo(cache, cacheSize, cacheTime) && cacheSize == RleCombinedSize(src)) {
		std::lock_guard<std::mutex> lock(g_rleLock);
		g_rlePath = cache;
		LogI("content", "graphics: rle.mmp extended by %s (already prepared)", provider->folder.c_str());
		return;
	}
	const DWORD start = GetTickCount();
	const std::string hash = HashFile(base);
	if (hash != provider->rleBaseSha256) {
		LogW("content", "graphics: %s extends another rle.mmp (sha256 %s, the game's is %s)", provider->folder.c_str(), provider->rleBaseSha256.c_str(),
			hash.empty() ? "unreadable" : hash.c_str());
		SetProblem(provider->folder, "Made for another version of the game's graphics (rle.mmp): its new graphics cannot be shown");
		return;
	}
	DeleteFileA(keyFile.c_str());
	if (!BuildCombined(base, tail, cache)) {
		LogE("content", "graphics: could not write %s (error %lu)", cache.c_str(), GetLastError());
		SetProblem(provider->folder, "Could not prepare the graphics file (rle.mmp): check the free disk space");
		return;
	}
	WriteSmall(keyFile, key);
	std::lock_guard<std::mutex> lock(g_rleLock);
	g_rlePath = cache;
	LogI("content", "graphics: rle.mmp extended with %u MB from %s (%.1f s)",
		static_cast<unsigned>((src.tailSize - kRleTailHeaderSize) >> 20), provider->folder.c_str(), (GetTickCount() - start) / 1000.0);
}

void InstallRleHook()
{
	void *target = reinterpret_cast<void *>(addr::kOpenMappedFileSource);
	if (HookCreate(target, reinterpret_cast<void *>(&HookOpenMapped), reinterpret_cast<void **>(&g_origOpenMapped)) == MH_OK &&
		HookEnable(target) == MH_OK) {
		LogD("content", "graphics: ready to extend rle.mmp");
	}
	else {
		LogE("content", "graphics: could not hook the opening of rle.mmp; content packs cannot add graphics to it");
	}
}
}
