// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/gamefiles.h"

#include <windows.h>

#include <map>
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

struct PakRef {
	std::string pak;
	uint32_t offset = 0, size = 0;
};
std::map<std::string, PakRef> g_dataRefs;

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
			if (!onlyCurrentLang && name.compare(0, 5, "data/") == 0) g_dataRefs[name] = {dir + "\\" + fd.cFileName, e.offset, e.size};
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

bool GameFileText(const std::string &realPath, std::string &out)
{
	GameFiles();
	const auto it = g_dataRefs.find(realPath);
	if (it == g_dataRefs.end()) return false;
	HANDLE h = CreateFileA(it->second.pak.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	out.assign(it->second.size, '\0');
	DWORD got = 0;
	const bool ok = SetFilePointer(h, static_cast<LONG>(it->second.offset), nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		(out.empty() || ReadFile(h, &out[0], it->second.size, &got, nullptr)) && got == it->second.size;
	CloseHandle(h);
	if (!ok) out.clear();
	return ok;
}

}
