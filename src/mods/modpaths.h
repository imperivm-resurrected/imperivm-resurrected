// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace modpaths {

inline std::string GameDir()
{
	static const std::string dir = [] {
		char path[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		char *slash = strrchr(path, '\\');
		if (slash) *slash = 0;
		return std::string(path);
	}();
	return dir;
}

inline bool IsDir(const std::string &p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool IsFile(const std::string &p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline void MakeDirs(const std::string &path)
{
	for (size_t i = 3; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '\\') CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
	}
}

// The local time as yyyymmdd-hhmmss, which leftovers.cpp recognizes in the names of interrupted downloads.
inline std::string Timestamp()
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	char buf[32];
	sprintf_s(buf, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	return buf;
}

}
