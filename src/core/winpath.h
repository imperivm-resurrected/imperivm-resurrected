// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <cstring>
#include <ctime>
#include <string>

namespace winpath {

inline constexpr unsigned long long kUnixEpochAsFileTime = 116444736000000000ULL;

inline std::wstring WidenWith(const std::string &s, UINT codepage, DWORD flags)
{
	if (s.empty()) return std::wstring();
	const int n = MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) return std::wstring();
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

// Text that is not valid UTF-8 comes back empty.
inline std::wstring Widen(const std::string &s, UINT codepage)
{
	return WidenWith(s, codepage, codepage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0);
}

// Bytes that are not valid UTF-8 become U+FFFD instead.
inline std::wstring WidenLenient(const std::string &s, UINT codepage)
{
	return WidenWith(s, codepage, 0);
}

inline std::string Utf8(const std::wstring &w)
{
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	if (n <= 0) return std::string();
	std::string s(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}

inline std::wstring LongPath(const std::string &path)
{
	const std::wstring wide = Widen(path, CP_ACP);
	const DWORD n = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
	if (!n) return wide;
	std::wstring full(n, L'\0');
	full.resize(GetFullPathNameW(wide.c_str(), n, &full[0], nullptr));
	if (full.compare(0, 4, L"\\\\?\\") == 0) return full;
	if (full.compare(0, 2, L"\\\\") == 0) return L"\\\\?\\UNC\\" + full.substr(2);
	return L"\\\\?\\" + full;
}

inline void MakeDirs(const std::wstring &path)
{
	for (size_t at = path.find(L'\\', 3); at != std::wstring::npos; at = path.find(L'\\', at + 1)) CreateDirectoryW(path.substr(0, at).c_str(), nullptr);
	CreateDirectoryW(path.c_str(), nullptr);
}

inline FILETIME FileTimeOf(time_t t)
{
	const unsigned long long value = static_cast<unsigned long long>(t) * 10000000ULL + kUnixEpochAsFileTime;
	FILETIME ft;
	ft.dwLowDateTime = static_cast<DWORD>(value);
	ft.dwHighDateTime = static_cast<DWORD>(value >> 32);
	return ft;
}

// The folder of gbr.exe, without a backslash at the end.
inline std::string GameDir()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char *slash = strrchr(path, '\\')) *slash = 0;
	return path;
}

}
