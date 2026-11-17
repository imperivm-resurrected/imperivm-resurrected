// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/gamefiles.h"

#include <windows.h>

#include <mutex>

#include "core/log.h"
#include "mods/mods_internal.h"
#include "mods/pak.h"

namespace mods_impl {

namespace {
// Every file below dir (not the folders), as game paths relative to the game folder.
void AddLooseFiles(const std::string &dir, const std::string &rel, GameFileSet &out)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::string name = fd.cFileName;
		if (name == "." || name == "..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) AddLooseFiles(dir + "\\" + name, rel + name + "/", out);
		else out.insert(RootGamePath(rel + name));
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

void AddPakNames(const std::string &dir, bool onlyCurrentLang, GameFileSet &out, unsigned &paks)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*.pak").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		std::vector<PakEntry> entries;
		if (!ReadPakIndex(dir + "\\" + fd.cFileName, entries)) continue;
		paks++;
		for (const PakEntry &e : entries) {
			const std::string name = RootGamePath(e.name);
			if (!onlyCurrentLang || name.compare(0, 12, "currentlang/") == 0) out.insert(name);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

GameFileSet Load()
{
	const DWORD start = GetTickCount();
	GameFileSet out;
	unsigned paks = 0;
	const std::string game = GameDir();
	AddPakNames(game + "\\Packs", false, out, paks);
	AddPakNames(game + "\\local", true, out, paks);
	AddLooseFiles(game + "\\Units", "units/", out);
	AddLooseFiles(game + "\\Currentlang", "currentlang/", out);
	LogI("content", "the game's own files: %u names from %u packs and the loose folders (%lu ms)", static_cast<unsigned>(out.size()), paks, static_cast<unsigned long>(GetTickCount() - start));
	return out;
}
}

const GameFileSet &GameFiles()
{
	static GameFileSet files;
	static std::once_flag once;
	std::call_once(once, [] { files = Load(); });
	return files;
}

}
