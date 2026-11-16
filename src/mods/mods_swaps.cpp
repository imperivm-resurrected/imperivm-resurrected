// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <atomic>
#include <fstream>
#include <algorithm>
#include "MinHook.h"
#include "i18n/tr.h"
#include "core/guard.h"
#include "core/log.h"
#include "mods/folderlist.h"
#include "mods/folderswap.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

namespace mods_impl {
namespace {
std::vector<std::string> PendingUninstalls()
{
	char buf[4096] = {};
	GetPrivateProfileStringA("Mods", "UninstallOnStart", "", buf, sizeof(buf), IniPath().c_str());
	return ParseFolderList(buf);
}

void SetPendingUninstalls(const std::vector<std::string> &list)
{
	const std::string s = JoinFolderList(list);
	WritePrivateProfileStringA("Mods", "UninstallOnStart", s.empty() ? nullptr : s.c_str(), IniPath().c_str());
}

bool RemoveFolder(const std::string &path)
{
	std::string from = path;
	from.push_back('\0');
	SHFILEOPSTRUCTA op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = from.c_str();
	op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	return SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted;
}

const char kRemovedPrefix[] = "removed-";
std::atomic<bool> g_purging{false};
std::atomic<bool> g_purgeAgain{false};

std::string RemovedRoot()
{
	return ModsDir() + "\\_downloads";
}

bool MoveToRemoved(const std::string &folder)
{
	CreateDirectoryA(RemovedRoot().c_str(), nullptr);
	const std::string from = ModsDir() + "\\" + folder;
	for (DWORD i = 0; i < 5; i++) {
		const std::string to = RemovedRoot() + "\\" + kRemovedPrefix + std::to_string(GetTickCount() + i) + "-" + folder;
		if (MoveFileExA(from.c_str(), to.c_str(), 0)) return true;
		if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
	}
	return false;
}

void PurgeRemovedNow()
{
	std::vector<std::string> dirs;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((RemovedRoot() + "\\" + kRemovedPrefix + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(fd.cFileName);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	for (const std::string &d : dirs) {
		const DWORD started = GetTickCount();
		const bool ok = RemoveFolder(RemovedRoot() + "\\" + d);
		LogAt(ok ? kLogInfo : kLogWarn, "content", "%s %s (%.1f s)", d.c_str(), ok ? "deleted from disk" : "could NOT be deleted; it is tried again on the next start",
			(GetTickCount() - started) / 1000.0);
	}
}

void PurgeRemoved()
{
	g_purgeAgain = true;
	if (g_purging.exchange(true)) return;
	SafeThread([] {
		while (g_purgeAgain.exchange(false)) PurgeRemovedNow();
		g_purging = false;
	}).detach();
}
}
}

int ModsCompareVersions(const std::string &a, const std::string &b)
{
	return CompareVersions(a, b);
}

namespace mods_impl {
namespace {
std::string SwapFile() { return ModsDir() + "\\_downloads\\swaps.txt"; }
}
}

void ModsQueueSwap(const std::string &newFolder, const std::string &oldFolder)
{
	CreateDirectoryA((ModsDir() + "\\_downloads").c_str(), nullptr);
	FILE *f = nullptr;
	if (fopen_s(&f, SwapFile().c_str(), "ab") == 0 && f) {
		fprintf(f, "%s\t%s\n", newFolder.c_str(), oldFolder.c_str());
		fclose(f);
	}
}

void ModsApplyPendingSwaps()
{
	FILE *f = nullptr;
	if (fopen_s(&f, SwapFile().c_str(), "rb") != 0 || !f) return;
	std::string text;
	char buf[512];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
	fclose(f);
	DeleteFileA(SwapFile().c_str());
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) end = text.size();
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		const size_t tab = line.find('\t');
		if (tab == std::string::npos) continue;
		const std::string nw = line.substr(0, tab), old = line.substr(tab + 1);
		if (nw.empty() || old.empty() || nw == old || nw[0] == '_' || old[0] == '_' || nw[0] == '.' || old[0] == '.' ||
			nw.find_first_of("\\/") != std::string::npos || old.find_first_of("\\/") != std::string::npos) {
			continue;
		}
		const std::string nwPath = ModsDir() + "\\" + nw, oldPath = ModsDir() + "\\" + old;
		if (GetFileAttributesA(nwPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
		std::string backup;
		if (GetFileAttributesA(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
			SYSTEMTIME t;
			GetLocalTime(&t);
			char stamp[32];
			sprintf_s(stamp, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
			CreateDirectoryA((ModsDir() + "\\_downloads").c_str(), nullptr);
			CreateDirectoryA((ModsDir() + "\\_downloads\\backup").c_str(), nullptr);
			backup = ModsDir() + "\\_downloads\\backup\\" + old + "-" + stamp;
		}
		unsigned long error = 0;
		const FolderSwap swap = SwapInFolder(nwPath, oldPath, backup, error);
		if (swap == FolderSwap::kTargetBusy) {
			LogW("content", "could not save the previous version of %s (error %lu); the room's copy is kept", old.c_str(), error);
			continue;
		}
		if (swap != FolderSwap::kMoved) {
			LogW("content", "could not put %s in place of %s (error %lu)", nw.c_str(), old.c_str(), error);
			if (swap == FolderSwap::kStranded) LogW("content", "could not put back the previous version of %s: it is in %s", old.c_str(), backup.c_str());
			continue;
		}
		for (const std::string &pn : ModsProfileNames()) {
			ModProfile pr = ModsLoadProfile(pn);
			bool changed = false;
			std::vector<std::string> out;
			for (const std::string &m : pr.mods) {
				const bool isNew = _stricmp(m.c_str(), nw.c_str()) == 0;
				if (isNew) changed = true;
				const std::string &name = isNew ? old : m;
				if (std::find_if(out.begin(), out.end(), [&](const std::string &o) { return _stricmp(o.c_str(), name.c_str()) == 0; }) == out.end()) {
					out.push_back(name);
				}
			}
			if (changed) {
				pr.mods = out;
				ModsSaveProfile(pr);
			}
		}
		LogI("content", "%s takes the place of %s (the previous version is in mods\\_downloads\\backup)", nw.c_str(), old.c_str());
	}
}

namespace mods_impl {
namespace {
void RemoveFromProfiles(const std::string &folder)
{
	for (const std::string &n : ModsProfilesUsing(folder)) {
		ModProfile p = ModsLoadProfile(n);
		p.mods.erase(std::remove_if(p.mods.begin(), p.mods.end(), [&](const std::string &f) { return _stricmp(f.c_str(), folder.c_str()) == 0; }),
			p.mods.end());
		ModsSaveProfile(p);
	}
}
}
}

namespace {
std::mutex g_prunedLock;
std::vector<std::string> g_pruned;
}

void ModsPruneMissing()
{
	if (!IsDir(ModsDir())) return;
	for (const std::string &name : ModsProfileNames()) {
		if (_stricmp(name.c_str(), kOriginal) == 0) continue;
		ModProfile p = ModsLoadProfile(name);
		std::vector<std::string> keep, gone;
		for (const std::string &folder : p.mods) {
			(GetFileAttributesA((ModsDir() + "\\" + folder).c_str()) == INVALID_FILE_ATTRIBUTES ? gone : keep).push_back(folder);
		}
		if (gone.empty()) continue;
		const auto isGone = [&](const std::string &f) {
			return std::find_if(gone.begin(), gone.end(), [&](const std::string &g) { return _stricmp(g.c_str(), f.c_str()) == 0; }) != gone.end();
		};
		p.mods = keep;
		p.choices.erase(std::remove_if(p.choices.begin(), p.choices.end(), [&](const ModChoice &c) { return isGone(c.use) || isGone(c.over); }), p.choices.end());
		if (!ModsSaveProfile(p)) continue;
		std::lock_guard<std::mutex> lock(g_prunedLock);
		for (const std::string &folder : gone) {
			LogW("content", "%s is no longer in the mods folder: removed from the profile \"%s\"", folder.c_str(), p.name.c_str());
			if (std::find(g_pruned.begin(), g_pruned.end(), folder) == g_pruned.end()) g_pruned.push_back(folder);
		}
	}
}

std::vector<std::string> ModsTakePruned()
{
	std::lock_guard<std::mutex> lock(g_prunedLock);
	std::vector<std::string> out;
	out.swap(g_pruned);
	return out;
}

void ModsApplyPendingUninstalls()
{
	std::vector<std::string> left;
	for (const std::string &folder : PendingUninstalls()) {
		const std::string path = ModsDir() + "\\" + folder;
		const bool exists = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		if (exists && !MoveToRemoved(folder)) {
			left.push_back(folder);
			continue;
		}
		if (exists) LogI("content", "%s uninstalled (its files are deleted in the background)", folder.c_str());
		RemoveFromProfiles(folder);
	}
	SetPendingUninstalls(left);
	PurgeRemoved();
}

namespace mods_impl {
namespace {
std::string ReceivedListPath() { return ModsDir() + "\\_received.txt"; }

std::vector<std::string> ReadReceived()
{
	std::vector<std::string> out;
	std::ifstream in(ReceivedListPath());
	std::string line;
	while (std::getline(in, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
		if (!line.empty()) out.push_back(line);
	}
	return out;
}

void WriteReceived(const std::vector<std::string> &list)
{
	std::ofstream out(ReceivedListPath(), std::ios::trunc);
	for (const std::string &l : list) out << l << "\n";
}

std::string FileNameOf(const std::string &path)
{
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

void CollectMapNames(const std::string &dir, std::vector<std::string> &names)
{
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (f == INVALID_HANDLE_VALUE) return;
	do {
		const std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CollectMapNames(dir + "\\" + n, names);
		else if (n.size() > 5 && _stricmp(n.c_str() + n.size() - 5, ".bfhp") == 0) names.push_back(n);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
}

void RemoveReceivedMaps(const std::vector<std::string> &names)
{
	if (names.empty()) return;
	std::vector<std::string> keep;
	for (const std::string &path : ReadReceived()) {
		const std::string file = FileNameOf(path);
		bool match = false;
		for (const std::string &n : names) match = match || _stricmp(n.c_str(), file.c_str()) == 0;
		if (!match) {
			keep.push_back(path);
			continue;
		}
		if (DeleteFileA(path.c_str()) || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			LogI("content", "also deleted the copy of the map a host passed (%s)", path.c_str());
		}
		else {
			keep.push_back(path);
		}
	}
	WriteReceived(keep);
}
}
}

void ModsNoteReceivedMap(const std::string &fullPath)
{
	std::vector<std::string> list = ReadReceived();
	for (const std::string &l : list) {
		if (_stricmp(l.c_str(), fullPath.c_str()) == 0) return;
	}
	list.push_back(fullPath);
	WriteReceived(list);
}

namespace mods_impl {
namespace {
std::string MapRel(const std::string &scenarioPath)
{
	std::string p = scenarioPath;
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (p.size() < 5 || p.compare(p.size() - 5, 5, ".bfhp") != 0) p += ".bfhp";
	if (p.find('/') == std::string::npos) p = "scenarios/" + p;
	return p;
}

std::string MapFile(const std::string &rel)
{
	std::string p = rel;
	for (char &c : p) {
		if (c == '/') c = '\\';
	}
	return GameDir() + "\\" + p;
}

bool ToRecycleBin(const std::string &file)
{
	std::string from = file;
	from.push_back('\0');
	SHFILEOPSTRUCTA op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = from.c_str();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	return SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted;
}
}
}

MapOrigin ModsMapOrigin(const std::string &scenarioPath)
{
	MapOrigin out;
	const std::string rel = MapRel(scenarioPath);
	const std::vector<std::string> mounted = ModsMountedProfile().mods;
	for (const ModInfo &m : ModsList()) {
		if (std::find(mounted.begin(), mounted.end(), m.folder) == mounted.end()) continue;
		if (std::binary_search(m.loosePaths.begin(), m.loosePaths.end(), rel) || std::binary_search(m.paths.begin(), m.paths.end(), rel)) {
			out.kind = kMapMod;
			out.modName = m.name;
			out.modFolder = m.folder;
			return out;
		}
	}
	const std::string file = MapFile(rel);
	if (GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES) return out;
	out.kind = kMapLoose;
	for (const std::string &l : ReadReceived()) {
		if (_stricmp(l.c_str(), file.c_str()) == 0) out.kind = kMapReceived;
	}
	return out;
}

bool ModsDeleteMap(const std::string &scenarioPath, std::string &message)
{
	const MapOrigin origin = ModsMapOrigin(scenarioPath);
	if (origin.kind == kMapMod) {
		message = TrFmt("This map comes from the content pack %s: uninstall that pack to remove it", {origin.modName});
		return false;
	}
	if (origin.kind == kMapBase) {
		message = Tr("This map is part of the game and cannot be deleted");
		return false;
	}
	const std::string file = MapFile(MapRel(scenarioPath));
	if (!ToRecycleBin(file)) {
		message = Tr("The map could not be deleted");
		return false;
	}
	if (origin.kind == kMapReceived) {
		std::vector<std::string> keep;
		for (const std::string &l : ReadReceived()) {
			if (_stricmp(l.c_str(), file.c_str()) != 0) keep.push_back(l);
		}
		WriteReceived(keep);
	}
	LogI("content", "map %s moved to the recycle bin", file.c_str());
	message = Tr("Map moved to the recycle bin");
	return true;
}

std::string ModsUninstall(const std::string &folder)
{
	if (folder.empty() || folder.find_first_of("\\/") != std::string::npos || folder[0] == '_' || folder[0] == '.') {
		return "That cannot be uninstalled";
	}
	ModInfo m;
	const std::string name = ModsFind(folder, m) ? m.name : folder;
	RemoveFromProfiles(folder);
	bool mounted = false;
	for (const std::string &f : ModsMountedProfile().mods) {
		if (_stricmp(f.c_str(), folder.c_str()) == 0) mounted = true;
	}
	const std::string path = ModsDir() + "\\" + folder;
	{
		std::vector<std::string> maps;
		CollectMapNames(path, maps);
		RemoveReceivedMaps(maps);
	}
	if (!mounted && MoveToRemoved(folder)) {
		LogI("content", "%s uninstalled (its files are deleted in the background)", folder.c_str());
		ModsScan();
		PurgeRemoved();
		return name + Tr(" uninstalled");
	}
	std::vector<std::string> pending = PendingUninstalls();
	if (std::find(pending.begin(), pending.end(), folder) == pending.end()) pending.push_back(folder);
	SetPendingUninstalls(pending);
	LogI("content", "%s will be uninstalled on restart%s", folder.c_str(), mounted ? " (it is loaded)" : " (could not delete now)");
	return name + Tr(" will be uninstalled when the game restarts");
}

bool ModsUninstallPending(const std::string &folder)
{
	const std::vector<std::string> pending = PendingUninstalls();
	return std::find(pending.begin(), pending.end(), folder) != pending.end();
}

void ModsCancelUninstall(const std::string &folder)
{
	std::vector<std::string> pending = PendingUninstalls();
	if (!RemoveFolderFrom(pending, folder)) return;
	SetPendingUninstalls(pending);
	LogI("content", "%s installed again: it is no longer uninstalled on restart", folder.c_str());
}
