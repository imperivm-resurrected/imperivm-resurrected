// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/gbcache.h"

#include <windows.h>

#include <algorithm>
#include <ctime>

#include "mods/modpaths.h"

namespace {

const char kMagic[4] = {'I', 'G', 'B', 'C'};
const uint32_t kVersion = 2;
const size_t kHeader = 32;
const size_t kMaxFile = 64u << 20;
const int64_t kFileTimeOffset = 11644473600LL;

int64_t UnixOf(const FILETIME &ft)
{
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return static_cast<int64_t>(u.QuadPart / 10000000ULL) - kFileTimeOffset;
}

FILETIME FileTimeOf(int64_t unix)
{
	ULARGE_INTEGER u;
	u.QuadPart = static_cast<ULONGLONG>(unix + kFileTimeOffset) * 10000000ULL;
	FILETIME ft;
	ft.dwLowDateTime = u.LowPart;
	ft.dwHighDateTime = u.HighPart;
	return ft;
}

void Touch(const std::string &path, int64_t now)
{
	HANDLE h = CreateFileA(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;
	const FILETIME ft = FileTimeOf(now);
	SetFileTime(h, nullptr, nullptr, &ft);
	CloseHandle(h);
}

bool ReadAll(const std::string &path, std::string &out)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size;
	bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<uint64_t>(size.QuadPart) <= kMaxFile + kHeader + 4096;
	if (ok) {
		out.resize(static_cast<size_t>(size.QuadPart));
		size_t done = 0;
		while (ok && done < out.size()) {
			DWORD got = 0;
			ok = ReadFile(h, &out[done], static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 20)), &got, nullptr) && got > 0;
			done += got;
		}
	}
	CloseHandle(h);
	return ok;
}

void MakeDirs(std::string path)
{
	std::replace(path.begin(), path.end(), '/', '\\');
	modpaths::MakeDirs(path);
}

void Collect(const std::string &dir, std::vector<GbCacheEntry> &out, std::vector<std::string> &stray, int64_t now)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::string name = fd.cFileName;
		if (name == "." || name == "..") continue;
		const std::string path = dir + "\\" + name;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			Collect(path, out, stray, now);
			continue;
		}
		GbCacheEntry e;
		e.path = path;
		e.bytes = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		e.lastUse = UnixOf(fd.ftLastWriteTime);
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) {
			if (now - e.lastUse > 86400) stray.push_back(path);
			continue;
		}
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) out.push_back(e);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

}

int64_t GbCacheNow()
{
	return static_cast<int64_t>(time(nullptr));
}

std::string GbCacheKey(const std::string &url)
{
	uint64_t h = 1469598103934665603ULL;
	for (unsigned char c : url) {
		h ^= c;
		h *= 1099511628211ULL;
	}
	char buf[24];
	sprintf_s(buf, "%016llx", static_cast<unsigned long long>(h));
	return buf;
}

std::string GbCachePath(const std::string &dir, const std::string &url)
{
	const std::string key = GbCacheKey(url);
	return dir + "\\" + key.substr(0, 2) + "\\" + key + ".bin";
}

bool GbCacheIsFresh(int64_t fetched, int64_t now, int64_t ttlSeconds)
{
	if (ttlSeconds <= 0) return true;
	return now - fetched >= 0 && now - fetched < ttlSeconds;
}

bool GbCacheRead(const std::string &dir, const std::string &url, int64_t ttlSeconds, std::string &body, bool &stale, int64_t now)
{
	const std::string path = GbCachePath(dir, url);
	std::string raw;
	if (!ReadAll(path, raw) || raw.size() < kHeader || memcmp(raw.data(), kMagic, 4) != 0) return false;
	uint32_t version, urlLen;
	int64_t fetched;
	uint64_t bodyLen;
	memcpy(&version, raw.data() + 4, 4);
	memcpy(&fetched, raw.data() + 8, 8);
	memcpy(&bodyLen, raw.data() + 16, 8);
	memcpy(&urlLen, raw.data() + 24, 4);
	if (version != kVersion || urlLen != url.size() || bodyLen > kMaxFile) return false;
	if (raw.size() != kHeader + static_cast<size_t>(urlLen) + static_cast<size_t>(bodyLen)) return false;
	if (raw.compare(kHeader, urlLen, url) != 0) return false;
	body.assign(raw, kHeader + urlLen, std::string::npos);
	stale = !GbCacheIsFresh(fetched, now, ttlSeconds);
	Touch(path, now);
	return true;
}

bool GbCacheWrite(const std::string &dir, const std::string &url, const std::string &body, int64_t now)
{
	if (body.size() > kMaxFile || url.size() > 4096) return false;
	const std::string path = GbCachePath(dir, url);
	MakeDirs(path.substr(0, path.find_last_of('\\')));
	const std::string temp = path + "." + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetCurrentThreadId()) + ".tmp";
	HANDLE h = CreateFileA(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	char header[kHeader] = {};
	const uint64_t bodyLen = body.size();
	const uint32_t urlLen = static_cast<uint32_t>(url.size());
	memcpy(header, kMagic, 4);
	memcpy(header + 4, &kVersion, 4);
	memcpy(header + 8, &now, 8);
	memcpy(header + 16, &bodyLen, 8);
	memcpy(header + 24, &urlLen, 4);
	DWORD wrote = 0;
	bool ok = WriteFile(h, header, static_cast<DWORD>(kHeader), &wrote, nullptr) && wrote == kHeader;
	ok = ok && (url.empty() || (WriteFile(h, url.data(), urlLen, &wrote, nullptr) && wrote == urlLen));
	size_t done = 0;
	while (ok && done < body.size()) {
		const DWORD chunk = static_cast<DWORD>(std::min<size_t>(body.size() - done, 1u << 20));
		ok = WriteFile(h, body.data() + done, chunk, &wrote, nullptr) && wrote == chunk;
		done += chunk;
	}
	ok = ok && FlushFileBuffers(h);
	CloseHandle(h);
	if (!ok || !MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileA(temp.c_str());
		return false;
	}
	return true;
}

void GbCacheRemove(const std::string &dir, const std::string &url)
{
	DeleteFileA(GbCachePath(dir, url).c_str());
}

std::vector<std::string> GbCachePlanPrune(std::vector<GbCacheEntry> entries, uint64_t maxBytes, int64_t maxIdleSeconds, int64_t now)
{
	std::sort(entries.begin(), entries.end(), [](const GbCacheEntry &a, const GbCacheEntry &b) { return a.lastUse < b.lastUse; });
	uint64_t total = 0;
	for (const GbCacheEntry &e : entries) total += e.bytes;
	std::vector<std::string> remove;
	for (const GbCacheEntry &e : entries) {
		const bool idle = maxIdleSeconds > 0 && now - e.lastUse > maxIdleSeconds;
		if (!idle && total <= maxBytes) break;
		remove.push_back(e.path);
		total -= (std::min)(total, e.bytes);
	}
	return remove;
}

uint64_t GbCachePrune(const std::string &dir, uint64_t maxBytes, int64_t maxIdleSeconds, int64_t now)
{
	std::vector<GbCacheEntry> entries;
	std::vector<std::string> stray;
	Collect(dir, entries, stray, now);
	for (const std::string &p : stray) DeleteFileA(p.c_str());
	uint64_t freed = 0;
	const std::vector<std::string> remove = GbCachePlanPrune(entries, maxBytes, maxIdleSeconds, now);
	for (const std::string &p : remove) {
		for (const GbCacheEntry &e : entries) {
			if (e.path == p) {
				freed += e.bytes;
				break;
			}
		}
		DeleteFileA(p.c_str());
	}
	return freed;
}

void GbCacheClear(const std::string &dir)
{
	std::vector<GbCacheEntry> entries;
	std::vector<std::string> stray;
	Collect(dir, entries, stray, 0);
	for (const GbCacheEntry &e : entries) DeleteFileA(e.path.c_str());
	for (const std::string &p : stray) DeleteFileA(p.c_str());
}
