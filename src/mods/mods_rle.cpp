// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
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

// The tail of a ruleset's folder is not made part of the file the game maps (that file is mapped once, when the game starts, and every sprite made from a descriptor
// keeps an address in it): the game maps its own rle.mmp, and CMappedFileSource::GetData is answered by the DLL for the offsets past the end of it, from a view of the
// tail file. A switch changes which view answers. The views are kept for the session (a sprite made from a tail may still be alive in a cache the reload keeps) and
// each costs address space (not memory) for the size of its tail only.
typedef void *(__fastcall *GetData_t)(void *self, void *edx, unsigned offset);
GetData_t g_origGetData = nullptr;

struct TailView {
	std::string path;
	uint64_t baseSize = 0, fileSize = 0;
	const unsigned char *bytes = nullptr; // the whole tail file, mapped
};
std::map<std::string, std::unique_ptr<TailView>> g_views;
std::atomic<const TailView *> g_activeTail{nullptr};
bool g_rootTail = false; // the file the game maps is the combined one of a package's root tail
bool g_getDataHooked = false;
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

void *__fastcall HookGetData(void *self, void *edx, unsigned offset)
{
	if (const TailView *tail = g_activeTail.load()) {
		if (offset >= tail->baseSize) {
			const int64_t at = RleTailFileOffset(tail->baseSize, tail->fileSize, offset);
			// Past the end of the tail: nothing (the sprite says it is damaged) rather than an address in nowhere.
			return at < 0 ? nullptr : const_cast<unsigned char *>(tail->bytes) + at;
		}
	}
	return g_origGetData(self, edx, offset);
}

// Maps a tail file (read-only, the whole file) once; the next time it is asked for the same view is returned.
const TailView *ViewOf(const std::string &path, uint64_t baseSize, uint64_t fileSize, uint64_t fileTime)
{
	const std::string key = path + "|" + std::to_string(fileSize) + "|" + std::to_string(fileTime) + "|" + std::to_string(baseSize);
	std::lock_guard<std::mutex> lock(g_rleLock);
	const auto it = g_views.find(key);
	if (it != g_views.end()) return it->second.get();
	HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE) return nullptr;
	HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	const void *view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
	// The view keeps the file alive: the handles are not needed any more.
	if (mapping) CloseHandle(mapping);
	CloseHandle(file);
	if (!view) return nullptr;
	auto tail = std::make_unique<TailView>();
	tail->path = path;
	tail->baseSize = baseSize;
	tail->fileSize = fileSize;
	tail->bytes = static_cast<const unsigned char *>(view);
	return g_views.emplace(key, std::move(tail)).first->second.get();
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

namespace {
// Whether a ruleset's tail file was made for the game's rle.mmp: its base size and sha256 are the game's. Hashing the 391 MB takes a few seconds, so the answer is kept next
// to the cache of the DLL (marker file with the key of both files) and asked again only when either file changes.
bool TailMadeForThisGame(const std::string &folder, const std::string &tailPath, uint64_t baseSize, const std::string &baseSha256)
{
	const std::string base = GameDir() + "\\rle.mmp";
	RleCacheSource src;
	src.tailPath = tailPath;
	if (!FileInfo(base, src.baseSize, src.baseTime) || !FileInfo(tailPath, src.tailSize, src.tailTime)) {
		LogW("content", "graphics: rle.mmp or %s cannot be read", tailPath.c_str());
		return false;
	}
	if (src.baseSize != baseSize) {
		LogW("content", "graphics: %s extends an rle.mmp of %u KB and the game's has %u KB", folder.c_str(), static_cast<unsigned>(baseSize >> 10), static_cast<unsigned>(src.baseSize >> 10));
		SetProblem(folder, "Made for another version of the game's graphics (rle.mmp): its new graphics cannot be shown");
		return false;
	}
	const std::string dir = GameDir() + "\\imperivm-resurrected\\_cache";
	CreateDirectoryA((GameDir() + "\\imperivm-resurrected").c_str(), nullptr);
	CreateDirectoryA(dir.c_str(), nullptr);
	std::string name;
	for (char c : tailPath) name += isalnum(static_cast<unsigned char>(c)) ? c : '_';
	const std::string marker = dir + "\\tail-" + name.substr(name.size() > 80 ? name.size() - 80 : 0) + ".ok", key = RleCacheKey(src);
	if (ReadSmall(marker) == key) return true;
	const DWORD start = GetTickCount();
	const std::string hash = HashFile(base);
	if (hash != baseSha256) {
		LogW("content", "graphics: %s extends another rle.mmp (sha256 %s, the game's is %s)", folder.c_str(), baseSha256.c_str(), hash.empty() ? "unreadable" : hash.c_str());
		SetProblem(folder, "Made for another version of the game's graphics (rle.mmp): its new graphics cannot be shown");
		return false;
	}
	WriteSmall(marker, key);
	LogI("content", "graphics: %s was made for this game's rle.mmp (checked in %.1f s)", folder.c_str(), (GetTickCount() - start) / 1000.0);
	return true;
}
}

bool RleActivateTail(const std::string &folder, const std::string &tailPath, uint64_t baseSize, const std::string &baseSha256)
{
	if (RleRootTailActive()) {
		LogW("content", "graphics: %s has a tail and the profile already has the combined one of a package's root: only one tail at a time", folder.c_str());
		return false;
	}
	if (!TailMadeForThisGame(folder, tailPath, baseSize, baseSha256)) return false;
	uint64_t size = 0, time = 0;
	if (!FileInfo(tailPath, size, time)) return false;
	const DWORD start = GetTickCount();
	const TailView *view = ViewOf(tailPath, baseSize, size, time);
	if (!view) {
		LogE("content", "graphics: could not map %s (error %lu)", tailPath.c_str(), static_cast<unsigned long>(GetLastError()));
		SetProblem(folder, "Could not read the graphics file (rle.mmp.tail)");
		return false;
	}
	g_activeTail.store(view);
	LogI("content", "graphics: the tail of %s (%u MB) answers the game's sprites past the end of rle.mmp (%lu ms)", folder.c_str(), static_cast<unsigned>((size - kRleTailHeaderSize) >> 20),
		static_cast<unsigned long>(GetTickCount() - start));
	return true;
}

void RleDeactivateTail()
{
	if (g_activeTail.exchange(nullptr)) LogI("content", "graphics: no tail answers past the end of rle.mmp any more");
}

bool RleTailsInPlace()
{
	return g_getDataHooked;
}

bool RleRootTailActive()
{
	std::lock_guard<std::mutex> lock(g_rleLock);
	return g_rootTail;
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
		g_rootTail = false;
	}
	g_activeTail.store(nullptr);
	// The first package with a tail provides it; the tail of a ruleset the profile mounts counts as its package's and goes first.
	struct Provider {
		std::string folder, tailDir;
		uint64_t rleBaseSize = 0;
		std::string rleBaseSha256;
		bool fromLayer = false;
	} found;
	bool have = false;
	for (const std::string &folder : p.mods) {
		for (const ModInfo &m : all) {
			if (_stricmp(m.folder.c_str(), folder.c_str()) != 0 || !m.forbidden.empty() || have) continue;
			const ModLayer *layer = ModsActiveLayer(p, m);
			if (layer && layer->rleTail && layer->rleTailError.empty()) {
				found = {m.folder, ModsDir() + "\\" + m.folder + "\\" + layer->path, layer->rleBaseSize, layer->rleBaseSha256, true};
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
	// The tail of a ruleset is not put in the file the game maps: the DLL answers for it (see HookGetData), so the ruleset can be changed without restarting.
	if (found.fromLayer && g_getDataHooked) {
		RleActivateTail(found.folder, found.tailDir + "\\" + kRleTailFile, found.rleBaseSize, found.rleBaseSha256);
		return;
	}
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
		g_rootTail = true;
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
	g_rootTail = true;
	LogI("content", "graphics: rle.mmp extended with %u MB from %s (%.1f s)",
		static_cast<unsigned>((src.tailSize - kRleTailHeaderSize) >> 20), provider->folder.c_str(), (GetTickCount() - start) / 1000.0);
}

void InstallRleHook()
{
	void *target = reinterpret_cast<void *>(addr::kOpenMappedFileSource);
	void *getData = reinterpret_cast<void *>(addr::kMappedFileSourceGetData);
	if (HookCreate(target, reinterpret_cast<void *>(&HookOpenMapped), reinterpret_cast<void **>(&g_origOpenMapped)) == MH_OK &&
		HookEnable(target) == MH_OK) {
		LogD("content", "graphics: ready to extend rle.mmp");
		// Without this hook a ruleset's tail cannot be served: the tail of a ruleset then needs the restart (ModsRuleTailsInPlace).
		g_getDataHooked = HookCreate(getData, reinterpret_cast<void *>(&HookGetData), reinterpret_cast<void **>(&g_origGetData)) == MH_OK && HookEnable(getData) == MH_OK;
		if (!g_getDataHooked) LogW("content", "graphics: could not hook the sprite data of rle.mmp; the tail of a ruleset is mounted as a package's");
	}
	else {
		LogE("content", "graphics: could not hook the opening of rle.mmp; content packs cannot add graphics to it");
	}
}
}
