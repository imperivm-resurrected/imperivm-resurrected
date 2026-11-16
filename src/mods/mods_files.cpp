// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include "MinHook.h"
#include "core/log.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

namespace mods_impl {
std::map<std::string, std::vector<ScanFile>> g_scanFiles;

void Walk(const std::string &base, const std::string &rel, std::vector<ScanFile> &out, int depth)
{
	if (depth > 24) return;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((base + "\\" + rel + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		const std::string name = fd.cFileName;
		if (name == "." || name == "..") {
			continue;
		}
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
				LogW("content", "link %s%s ignored (links are not followed)", rel.c_str(), name.c_str());
				continue;
			}
			Walk(base, rel + name + "\\", out, depth + 1);
			continue;
		}
		ScanFile f;
		f.rel = NormPath(rel + name);
		f.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		f.mtime = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
		out.push_back(f);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}
}
