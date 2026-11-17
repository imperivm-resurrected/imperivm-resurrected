// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <cstring>

#include "core/log.h"
#include "mods/gamefiles.h"
#include "mods/mods_internal.h"
#include "mods/patchxml.h"

namespace mods_impl {

namespace {
bool ReadText(const std::string &path, std::string &out)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	const DWORD size = GetFileSize(h, nullptr);
	out.assign(size == INVALID_FILE_SIZE ? 0 : size, '\0');
	DWORD got = 0;
	const BOOL ok = out.empty() || ReadFile(h, &out[0], size, &got, nullptr);
	CloseHandle(h);
	out.resize(got);
	return ok != FALSE;
}

bool Ends(const std::string &s, const char *suffix)
{
	const size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
}

void ScanRootPatches(ModInfo &m, const std::vector<ScanFile> &files)
{
	m.globalPatchFiles.clear();
	m.globalPatchSample.clear();
	m.globalPatchOps = 0;
	// Only the root of a rules package, and only the files the patcher reads (.xml, .ini, .vs) that are not patches of the interface.
	if (m.kind != kModRules) return;
	const DWORD start = GetTickCount();
	unsigned examined = 0;
	for (const ScanFile &f : files) {
		if (!f.layer.empty() || f.rel.compare(0, 8, "patches/") != 0) continue;
		const std::string target = RootGamePath(f.rel.substr(8));
		if (target.compare(0, 15, "data/interface/") == 0) continue;
		if (!Ends(target, ".xml") && !Ends(target, ".ini") && !Ends(target, ".vs")) continue;
		std::string base, patch;
		// A file the game does not have has nothing to change. The patch is read as the patcher will read it.
		if (!GameFileText(target, base) || !ReadText(ModsDir() + "\\" + m.folder + "\\" + f.rel, patch)) continue;
		examined++;
		const std::vector<std::string> skipped = PatchSkippedOps(target, base, patch);
		if (skipped.empty()) continue;
		m.globalPatchOps += static_cast<unsigned>(skipped.size());
		m.globalPatchFiles.push_back("patches/" + target);
		if (m.globalPatchSample.size() < 3) m.globalPatchSample.push_back(target + (skipped[0].empty() ? "" : ": " + skipped[0]));
	}
	std::sort(m.globalPatchFiles.begin(), m.globalPatchFiles.end());
	if (examined) {
		LogI("content", "%s: %u root patch files compared with the game's own: %u operations change what the game has (%lu ms)", m.folder.c_str(), examined, m.globalPatchOps,
			static_cast<unsigned long>(GetTickCount() - start));
	}
}

}
