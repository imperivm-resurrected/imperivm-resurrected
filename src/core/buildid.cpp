// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/buildid.h"

#include <windows.h>

#include <algorithm>
#include <vector>

#include "core/log.h"
#include "core/sha256.h"

namespace {

// The DLL's own file, in UTF-16: a game folder may have characters the ANSI code page lacks.
std::wstring OwnFile()
{
	HMODULE self = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&OwnFile), &self)) {
		return L"";
	}
	std::vector<wchar_t> path(32768);
	const DWORD n = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
	return n && n < path.size() ? std::wstring(path.data(), n) : std::wstring();
}

// The DLL's file, opened once: renaming or replacing it later (the updater does both) leaves this handle on the file that was loaded.
HANDLE g_pinned = INVALID_HANDLE_VALUE;
DWORD g_pinError = 0;

HANDLE OpenOwnFile()
{
	const std::wstring path = OwnFile();
	if (path.empty()) {
		g_pinError = GetLastError();
		return INVALID_HANDLE_VALUE;
	}
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) g_pinError = GetLastError();
	return f;
}

std::string HashHandle(HANDLE f)
{
	LARGE_INTEGER zero = {};
	if (!SetFilePointerEx(f, zero, nullptr, FILE_BEGIN)) return "";
	Sha256 sha;
	std::vector<char> buf(1 << 20);
	DWORD n = 0;
	bool ok = true;
	while ((ok = ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) != FALSE) && n) sha.Add(buf.data(), n);
	return ok ? sha.Hex() : std::string();
}

}

void BuildPin()
{
	if (g_pinned == INVALID_HANDLE_VALUE) g_pinned = OpenOwnFile();
}

const std::string &BuildSha256()
{
	static const std::string sha = [] {
		HANDLE f = g_pinned != INVALID_HANDLE_VALUE ? g_pinned : OpenOwnFile();
		const std::string hex = f == INVALID_HANDLE_VALUE ? std::string() : HashHandle(f);
		if (f != INVALID_HANDLE_VALUE && f != g_pinned) CloseHandle(f);
		if (BuildShaValid(hex)) LogI("boot", "mod build %s (the SHA-256 of its winmm.dll)", hex.substr(0, 12).c_str());
		else LogE("boot", "the mod could not read its own DLL (error %lu): it announces no build, so rooms that check builds turn it away", g_pinError);
		return BuildShaValid(hex) ? hex : std::string();
	}();
	return sha;
}

std::string BuildShortId(const std::string &sha256)
{
	return sha256.substr(0, 12);
}

bool BuildShaValid(const std::string &sha256)
{
	if (sha256.size() != 64) return false;
	for (char c : sha256) {
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	}
	return true;
}

BuildVerdict JudgeMemberBuild(const std::string &mine, const std::string &theirs, unsigned waitedMs, unsigned graceMs)
{
	if (!BuildShaValid(mine)) return BuildVerdict::kAdmit;
	switch (CompareBuilds(mine, theirs)) {
	case BuildMatch::kSame: return BuildVerdict::kAdmit;
	case BuildMatch::kDifferent: return BuildVerdict::kRefuse;
	default: return waitedMs < graceMs ? BuildVerdict::kWait : BuildVerdict::kRefuse;
	}
}

unsigned ArrivalClock::Waited(unsigned long long id, unsigned now)
{
	const auto it = seen_.find(id);
	if (it == seen_.end()) {
		seen_[id] = now;
		return 0;
	}
	return now - it->second;
}

void ArrivalClock::KeepOnly(const std::vector<unsigned long long> &present)
{
	for (auto it = seen_.begin(); it != seen_.end();) {
		if (std::find(present.begin(), present.end(), it->first) == present.end()) it = seen_.erase(it);
		else ++it;
	}
}

bool IdListHas(const std::string &list, unsigned long long id)
{
	return id && list.find(";" + std::to_string(id) + ";") != std::string::npos;
}

std::string IdListWith(const std::string &list, unsigned long long id)
{
	if (!id || IdListHas(list, id)) return list;
	return (list.empty() ? std::string(";") : list) + std::to_string(id) + ";";
}

std::string IdListWithout(const std::string &list, unsigned long long id)
{
	const std::string item = ";" + std::to_string(id) + ";";
	const size_t at = list.find(item);
	if (!id || at == std::string::npos) return list;
	const std::string out = list.substr(0, at + 1) + list.substr(at + item.size());
	return out == ";" ? std::string() : out;
}

BuildMatch CompareBuilds(const std::string &mine, const std::string &theirs)
{
	if (!BuildShaValid(theirs)) return BuildMatch::kMissing;
	return theirs == mine ? BuildMatch::kSame : BuildMatch::kDifferent;
}
