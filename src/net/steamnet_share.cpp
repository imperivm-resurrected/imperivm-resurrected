// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include "core/log.h"
#include "core/pathsafe.h"
#include "mods/mods.h"
#include "i18n/tr.h"

#include "net/steamnet_internal.h"

using namespace steamnet_impl;

namespace steamnet_impl {
ShareState g_share;

SeatState g_seats;

void PumpRejoinOut()
{
	static DWORD lastFullLog = 0;
	while (!g_share.rejoinOut.empty()) {
		const RejoinOut &o = g_share.rejoinOut.front();
		const EResult r = ShareSend(o.peer, o.type, o.data.data(), o.data.size());
		if (r == k_EResultLimitExceeded) {
			if (GetTickCount() - lastFullLog > 2000) {
				lastFullLog = GetTickCount();
				LogD("rejoin", "the Steam send queue is full; %u messages left to send", static_cast<unsigned>(g_share.rejoinOut.size()));
			}
			return;
		}
		if (r != k_EResultOK) LogW("rejoin", "could not send message %d (code %d)", o.type, static_cast<int>(r));
		g_share.rejoinOut.pop_front();
	}
}

EResult ShareSend(uint64 peer, uint8_t type, const void *data, size_t len)
{
	std::vector<uint8_t> buf(1 + len);
	buf[0] = type;
	if (len) memcpy(&buf[1], data, len);
	SteamNetworkingIdentity who = Identity(peer);
	return S.SteamAPI_ISteamNetworkingMessages_SendMessageToUser(g_steam.msgs, who, buf.data(), static_cast<uint32>(buf.size()),
		k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession, kShareChannel);
}

std::vector<std::string> SplitTabs(const std::string &s)
{
	std::vector<std::string> f;
	size_t p = 0;
	while (true) {
		const size_t t = s.find('\t', p);
		f.push_back(s.substr(p, t == std::string::npos ? std::string::npos : t - p));
		if (t == std::string::npos) break;
		p = t + 1;
	}
	return f;
}
}

namespace steamnet_impl {
namespace {
void ListModFiles(const std::string &root, const std::string &rel, std::vector<std::pair<std::string, uint64_t>> &out)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((root + (rel.empty() ? "" : "\\" + rel) + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::string name = fd.cFileName;
		if (name == "." || name == ".." || name[0] == '.') continue;
		const std::string r = rel.empty() ? name : rel + "\\" + name;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			ListModFiles(root, r, out);
		}
		else if (_stricmp(name.c_str(), "mod.json") == 0 || ModsAllowedFile(r)) {
			out.push_back({r, (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow});
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}
}
}

namespace steamnet_impl {
namespace {
uint64_t TransferBytes(const OutTransfer &t)
{
	uint64_t total = 0;
	for (const ShareStep &s : t.steps) {
		if (s.type != kShFile && s.type != kShMapFile) continue;
		const size_t tab = s.text.find_last_of('\t');
		total += _strtoui64(s.text.c_str() + (tab == std::string::npos ? 0 : tab + 1), nullptr, 10);
	}
	return total;
}
}
}

namespace steamnet_impl {
void HostOnRequest(uint64 peer, const std::string &text)
{
	for (size_t i = 0; i < g_share.out.size();) {
		if (g_share.out[i].peer == peer && g_share.out[i].mods) {
			if (g_share.out[i].file != INVALID_HANDLE_VALUE) CloseHandle(g_share.out[i].file);
			g_share.out.erase(g_share.out.begin() + i);
		}
		else {
			i++;
		}
	}
	OutTransfer t;
	t.peer = peer;
	std::set<std::string> asked;
	size_t start = 0;
	while (start < text.size() && asked.size() < 64) {
		size_t end = text.find('\n', start);
		const std::vector<std::string> f = SplitTabs(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
		start = end == std::string::npos ? text.size() : end + 1;
		if (f.size() < 2) continue;
		const std::string &id = f[0], &version = f[1];
		if (!asked.insert(id + "\t" + version).second) continue;
		ModInfo mod;
		bool found = false;
		for (const std::string &folder : ModsMountedProfile().mods) {
			ModInfo m;
			if (ModsFind(folder, m) && _stricmp(m.id.c_str(), id.c_str()) == 0 && m.version == version) {
				mod = m;
				found = true;
			}
		}
		if (!found || !mod.redistributable || !mod.forbidden.empty()) {
			const std::string why = !found ? "the host doesn't have it" : !mod.redistributable ? "its author doesn't allow sharing it"
																							   : "contains disallowed files";
			t.steps.push_back({kShDeny, id + "\t" + why, ""});
			continue;
		}
		std::vector<std::pair<std::string, uint64_t>> files;
		const std::string root = ModsFolderPath() + "\\" + mod.folder;
		ListModFiles(root, "", files);
		for (const auto &file : files) {
			t.steps.push_back({kShFile, id + "\t" + mod.folder + "\t" + file.first + "\t" + std::to_string(file.second),
				root + "\\" + file.first});
		}
		t.steps.push_back({kShModEnd, id + "\t" + mod.folder, ""});
		LogI("content-share", "mods over steam: sending %s %s (%u files) to %s", mod.name.c_str(), mod.version.c_str(),
			static_cast<unsigned>(files.size()), Name(peer).c_str());
	}
	t.steps.push_back({kShDone, "", ""});
	t.mods = true;
	g_share.out.push_back(std::move(t));
}

void PumpOut()
{
	for (size_t i = 0; i < g_share.out.size();) {
		OutTransfer &t = g_share.out[i];
		bool earlier = false;
		for (size_t j = 0; j < i; j++) {
			if (g_share.out[j].peer == t.peer) earlier = true;
		}
		if (earlier) {
			i++;
			continue;
		}
		bool failed = false;
		for (int budget = 0; budget < 64 && t.step < t.steps.size(); budget++) {
			ShareStep &s = t.steps[t.step];
			if (!t.headerSent) {
				const EResult r = ShareSend(t.peer, s.type, s.text.data(), s.text.size());
				if (r == k_EResultLimitExceeded) break;
				if (r != k_EResultOK) {
					failed = true;
					break;
				}
				t.headerSent = true;
				if (s.type != kShFile && s.type != kShMapFile) {
					t.step++;
					t.headerSent = false;
					continue;
				}
				t.file = CreateFileA(s.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
				if (t.file == INVALID_HANDLE_VALUE) {
					failed = true;
					break;
				}
			}
			if (t.pending.empty()) {
				t.pending.resize(kShareChunk);
				DWORD got = 0;
				ReadFile(t.file, t.pending.data(), kShareChunk, &got, nullptr);
				t.pending.resize(got);
				if (!got) {
					CloseHandle(t.file);
					t.file = INVALID_HANDLE_VALUE;
					t.step++;
					t.headerSent = false;
					continue;
				}
			}
			const EResult r = ShareSend(t.peer, kShData, t.pending.data(), t.pending.size());
			if (r == k_EResultLimitExceeded) break;
			if (r != k_EResultOK) {
				failed = true;
				break;
			}
			t.sent += t.pending.size();
			t.pending.clear();
		}
		if (failed || t.step >= t.steps.size()) {
			if (t.file != INVALID_HANDLE_VALUE) CloseHandle(t.file);
			LogI("content-share", "mods over steam: sending to %s %s", Name(t.peer).c_str(), failed ? "interrupted" : "finished");
			if (!failed && t.mods) g_sentDoneAt[t.peer] = GetTickCount();
			g_share.out.erase(g_share.out.begin() + i);
			continue;
		}
		i++;
	}
	std::map<uint64_t, std::pair<uint64_t, uint64_t>> progress;
	for (OutTransfer &t : g_share.out) {
		if (!t.total) t.total = TransferBytes(t);
		if (!t.total) continue;
		progress[t.peer].first += t.sent;
		progress[t.peer].second += t.total;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.sendProgress.swap(progress);
}

void DropTransfers(uint64 peer)
{
	for (size_t i = 0; i < g_share.out.size();) {
		if (g_share.out[i].peer != peer) {
			i++;
			continue;
		}
		if (g_share.out[i].file != INVALID_HANDLE_VALUE) CloseHandle(g_share.out[i].file);
		LogI("content-share", "mods over steam: sending to %s cancelled (left the room or was kicked)", Name(peer).c_str());
		g_share.out.erase(g_share.out.begin() + i);
	}
	g_sentDoneAt.erase(peer);
	g_restarting.erase(peer);
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.sendProgress.erase(peer);
	g_share.mapHave.erase(peer);
}


namespace {
void ResetDownload()
{
	if (g_share.in.file != INVALID_HANDLE_VALUE) CloseHandle(g_share.in.file);
	const std::string root = g_share.in.tempRoot;
	g_share.in = InTransfer();
	if (!root.empty()) DeleteTree(root);
}
}

void FailDownload(const std::string &why)
{
	ResetDownload();
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.download.active = false;
	g_share.download.failed = true;
	g_share.download.error = why;
	LogW("content-share", "mods over steam: download failed (%s)", why.c_str());
}

void AbortDownload()
{
	if (!g_share.in.host && g_share.in.file == INVALID_HANDLE_VALUE) return;
	ResetDownload();
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.download.active = false;
}

bool DownloadActive()
{
	return g_share.in.host != 0;
}

void MakeDirs(const std::string &path)
{
	for (size_t p = path.find('\\', 3); p != std::string::npos; p = path.find('\\', p + 1)) {
		CreateDirectoryA(path.substr(0, p).c_str(), nullptr);
	}
	CreateDirectoryA(path.c_str(), nullptr);
}

void DeleteTree(const std::string &dir)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const std::string name = fd.cFileName;
			if (name == "." || name == "..") continue;
			const std::string p = dir + "\\" + name;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryA(p.c_str());
				else DeleteFileA(p.c_str());
			}
			else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
			else DeleteFileA(p.c_str());
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryA(dir.c_str());
}
}

namespace steamnet_impl {
namespace {
bool SafeRel(const std::string &rel)
{
	return pathsafe::SafeRelative(rel) && (_stricmp(rel.c_str(), "mod.json") == 0 || ModsAllowedFile(rel));
}
}
}

namespace steamnet_impl {
void ClientOnMessage(uint64 peer, const uint8_t *data, size_t len)
{
	if (!len || peer != g_share.in.host || g_share.in.tempRoot.empty()) return;
	const uint8_t type = data[0];
	const std::string text(reinterpret_cast<const char *>(data + 1), len - 1);
	if (type == kShData) {
		DWORD wrote = 0;
		if (len - 1 > g_share.in.fileLeft) {
			FailDownload("the host sent more data than announced");
			return;
		}
		if (g_share.in.file == INVALID_HANDLE_VALUE || !WriteFile(g_share.in.file, data + 1, static_cast<DWORD>(len - 1), &wrote, nullptr) || wrote != len - 1) {
			FailDownload("could not write a file");
			return;
		}
		g_share.in.fileLeft -= len - 1;
		std::lock_guard<std::mutex> lock(g_lock);
		g_share.download.got += len - 1;
		return;
	}
	const std::vector<std::string> f = SplitTabs(text);
	if (type == kShFile && f.size() >= 4) {
		if (g_share.in.file != INVALID_HANDLE_VALUE) CloseHandle(g_share.in.file);
		g_share.in.file = INVALID_HANDLE_VALUE;
		const std::string &folder = f[1], &rel = f[2];
		if (!pathsafe::SafeFolderName(folder) || !SafeRel(rel)) {
			FailDownload(Tr("the host sent a disallowed file (") + rel + ")");
			return;
		}
		const uint64_t declared = _strtoui64(f[3].c_str(), nullptr, 10);
		uint64_t advertised = 0;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			advertised = g_share.download.total;
		}
		const uint64_t limit = advertised ? advertised + advertised / 4 + (16ull << 20) : (4ull << 30);
		if (declared > limit || g_share.in.budget + declared > limit) {
			FailDownload("the host sent more content than announced");
			return;
		}
		g_share.in.budget += declared;
		g_share.in.fileLeft = declared;
		const std::string path = g_share.in.tempRoot + "\\" + folder + "\\" + rel;
		MakeDirs(path.substr(0, path.rfind('\\')));
		g_share.in.file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (g_share.in.file == INVALID_HANDLE_VALUE) FailDownload(Tr("could not create ") + rel);
	}
	else if (type == kShModEnd && f.size() >= 2) {
		if (g_share.in.file != INVALID_HANDLE_VALUE) CloseHandle(g_share.in.file);
		g_share.in.file = INVALID_HANDLE_VALUE;
		if (!pathsafe::SafeFolderName(f[1])) {
			FailDownload("the host sent a folder that is not allowed");
			return;
		}
		const std::string from = g_share.in.tempRoot + "\\" + f[1];
		std::string folder = f[1];
		for (int n = 2; GetFileAttributesA((ModsFolderPath() + "\\" + folder).c_str()) != INVALID_FILE_ATTRIBUTES; n++) {
			folder = f[1] + " (room " + std::to_string(n) + ")";
		}
		if (!MoveFileExA(from.c_str(), (ModsFolderPath() + "\\" + folder).c_str(), MOVEFILE_COPY_ALLOWED)) {
			FailDownload(Tr("could not install ") + f[1]);
			return;
		}
		LogI("content-share", "mods over steam: installed %s in mods\\%s", f[0].c_str(), folder.c_str());
		std::lock_guard<std::mutex> lock(g_lock);
		g_share.download.installed[f[0]] = folder;
	}
	else if (type == kShDeny && f.size() >= 2) {
		FailDownload(f[0] + ": " + f[1]);
	}
	else if (type == kShDone) {
		DeleteTree(g_share.in.tempRoot);
		g_share.in = InTransfer();
		std::lock_guard<std::mutex> lock(g_lock);
		g_share.download.active = false;
		g_share.download.done = true;
		LogI("content-share", "mods over steam: download finished");
	}
}
}
