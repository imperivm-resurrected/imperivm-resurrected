// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include "game/cupmaps.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/basescenarios.h"
#include "core/guard.h"
#include "core/log.h"
#include "core/sha256.h"
#include "core/tournament.h"
#include "game/mapinfo.h"
#include "game/mappreview.h"
#include "hooks/gamefiles.h"
#include "core/strutil.h"
#include "core/winpath.h"

namespace {

// ReadGameFile's mode for a file read whole, as the map previews read them.
const unsigned kReadMode = 1;
// No scenario of the game comes near this; a larger file is not read at all.
const uint64_t kReadMostBytes = 64ull * 1024 * 1024;
const int kTitleMost = 80;

std::mutex g_mx;
CupScenariosView g_view;
bool g_started = false;
unsigned g_serial = 0;

using strutil::Lower;

std::string ScenariosFolder()
{
	return winpath::GameDir() + "\\Scenarios";
}

std::string ShortTitle(std::string title)
{
	const size_t a = title.find_first_not_of(" \t");
	title = a == std::string::npos ? std::string() : title.substr(a, title.find_last_not_of(" \t") - a + 1);
	while (TourneyTextLength(title) > kTitleMost) {
		while (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0) == 0x80) title.pop_back();
		if (!title.empty()) title.pop_back();
	}
	return title;
}

bool ReadDisk(const std::string &full, std::string &out)
{
	HANDLE h = CreateFileA(full.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size = {};
	bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && static_cast<uint64_t>(size.QuadPart) <= kReadMostBytes;
	if (ok) {
		out.resize(static_cast<size_t>(size.QuadPart));
		DWORD got = 0;
		ok = ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
	}
	CloseHandle(h);
	return ok;
}

std::string Sha256Of(const std::string &bytes)
{
	Sha256 h;
	h.Add(bytes);
	return h.Hex();
}

void Scan(unsigned serial)
{
	std::vector<std::string> files;
	std::set<std::string> seen;
	const auto add = [&](const std::string &file) {
		if (TourneyScenarioFileValid(file) && seen.insert(Lower(file)).second) files.push_back(file);
	};
	WIN32_FIND_DATAA fd;
	HANDLE find = FindFirstFileA((ScenariosFolder() + "\\*.bfhp").c_str(), &fd);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) add(fd.cFileName);
		} while (FindNextFileA(find, &fd));
		FindClose(find);
	}
	const std::vector<std::string> mounted = ModsMountedProfile().mods;
	for (const ModInfo &m : ModsList()) {
		if (std::find(mounted.begin(), mounted.end(), m.folder) == mounted.end()) continue;
		for (const std::vector<std::string> *list : {&m.paths, &m.loosePaths}) {
			for (const std::string &rel : *list) {
				static const char kFolder[] = "scenarios/";
				if (rel.compare(0, sizeof(kFolder) - 1, kFolder) != 0 || rel.find('/', sizeof(kFolder) - 1) != std::string::npos) continue;
				add(rel.substr(sizeof(kFolder) - 1));
			}
		}
	}
	CupScenariosView view;
	view.loaded = true;
	for (const std::string &file : files) {
		CupScenario s;
		s.file = file;
		s.path = CupScenarioPath(file);
		s.origin = ModsMapOrigin(s.path);
		if (s.origin.kind == kMapMod) {
			ModInfo mod;
			if (ModsFind(s.origin.modFolder, mod) && mod.gameplay) {
				view.fromRules++;
				continue;
			}
		}
		std::string bytes;
		if (!CupScenarioRead(file, bytes)) continue;
		MapInfo info;
		if (!MapInfoParse(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), info)) continue;
		// Adventures and conquests kept in Scenarios are not in the room's selector.
		s.humans = MapHumanSlots(info);
		if (info.gameType != 0 || s.humans < 2) continue;
		s.bytes = bytes.size();
		s.sha256 = Sha256Of(bytes);
		s.title = ShortTitle(info.name.empty() ? TourneyScenarioStem(file) : info.name);
		if (s.title.empty()) s.title = ShortTitle(TourneyScenarioStem(file));
		s.game = s.origin.kind != kMapMod && BaseScenarioIs(file, s.sha256);
		view.list.push_back(std::move(s));
	}
	std::sort(view.list.begin(), view.list.end(), [](const CupScenario &a, const CupScenario &b) { return _stricmp(a.title.c_str(), b.title.c_str()) < 0; });
	int game = 0;
	for (const CupScenario &s : view.list) game += s.game ? 1 : 0;
	LogI("league", "cup scenarios: %d of the game's, %d of the player's, %d left out for coming with rules", game, static_cast<int>(view.list.size()) - game,
		view.fromRules);
	std::lock_guard<std::mutex> lock(g_mx);
	if (serial != g_serial) return;
	g_view = std::move(view);
}

void StartScan()
{
	g_started = true;
	g_view.loading = true;
	SafeThread(Scan, ++g_serial).detach();
}

}

CupScenariosView CupScenarios()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_started) StartScan();
	CupScenariosView v = g_view;
	return v;
}

void CupScenariosRescan()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_view.loading && g_started) return;
	StartScan();
}

bool CupScenarioRead(const std::string &file, std::string &bytes)
{
	if (!TourneyScenarioFileValid(file)) return false;
	std::vector<char> data;
	if (ReadGameFile(("Scenarios\\" + file).c_str(), kReadMode, data) && !data.empty() && data.size() <= kReadMostBytes) {
		bytes.assign(data.begin(), data.end());
		return true;
	}
	return ReadDisk(ScenariosFolder() + "\\" + file, bytes);
}

bool CupScenarioLocal(const std::string &file, std::string &sha256, uint64_t &bytes)
{
	std::string data;
	if (!CupScenarioRead(file, data)) return false;
	sha256 = Sha256Of(data);
	bytes = data.size();
	return true;
}

bool CupScenarioStore(const std::string &file, const std::string &bytes, std::string &error)
{
	if (!TourneyScenarioFileValid(file)) {
		error = "the file name is not one of a scenario";
		return false;
	}
	const std::string folder = ScenariosFolder(), full = folder + "\\" + file, part = full + ".partial";
	CreateDirectoryA(folder.c_str(), nullptr);
	HANDLE h = CreateFileA(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		error = "could not create the file (" + std::to_string(GetLastError()) + ")";
		return false;
	}
	DWORD wrote = 0;
	const bool written = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) && wrote == bytes.size();
	CloseHandle(h);
	if (!written) {
		DeleteFileA(part.c_str());
		error = "could not write the file";
		return false;
	}
	// As a map a host passes in a room: the player's copy of that name is kept beside it.
	if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES && !MoveFileExA(full.c_str(), (full + ".previous").c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileA(part.c_str());
		error = "could not set the old copy aside (" + std::to_string(GetLastError()) + ")";
		return false;
	}
	if (!MoveFileExA(part.c_str(), full.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileA(part.c_str());
		error = "could not save the file (" + std::to_string(GetLastError()) + ")";
		return false;
	}
	ModsNoteReceivedMap(full);
	MapPreviewReset();
	LogI("league", "cup scenario saved: %s (%u bytes)", full.c_str(), static_cast<unsigned>(bytes.size()));
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_started && !g_view.loading) StartScan();
	}
	return true;
}

std::string CupScenarioPath(const std::string &file)
{
	return "Scenarios\\" + TourneyScenarioStem(file);
}
