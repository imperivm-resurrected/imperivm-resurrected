// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>


#include <mutex>
#include <vector>
#include <algorithm>

#include "mods/downloads.h"
#include "core/json.h"
#include "core/guard.h"
#include "core/versioncmp.h"
#include "mods/mods.h"
#include "mods/updater.h"
#include "i18n/tr.h"
#include "core/uiport.h"
#include "core/version.h"

namespace {

const char kVersionUrl[] = "https://github.com/THIONG/imperivm-resurrected/releases/latest/download/version.json";

const unsigned char kPubKey[64] = {
	0xc5, 0x91, 0x7e, 0x06, 0x9b, 0xac, 0x32, 0x99, 0xe9, 0x7c, 0xe6, 0x9d, 0x31, 0xcc, 0xa7, 0x06,
	0x62, 0x4b, 0xa9, 0xcf, 0xde, 0x54, 0xaa, 0x24, 0x74, 0x1c, 0xa1, 0xec, 0x1a, 0xb0, 0xbf, 0xa6,
	0xfc, 0x90, 0xbd, 0x47, 0x44, 0xfa, 0x09, 0x31, 0xf0, 0xf1, 0xaa, 0xf6, 0x9b, 0x5f, 0xd9, 0x7a,
	0xc1, 0x55, 0x4d, 0x91, 0xa8, 0xc4, 0x29, 0x7c, 0x20, 0x60, 0x58, 0xa9, 0xac, 0x7f, 0x58, 0x2b};

const int kMaxBadStarts = 3;

enum State { kNone, kChecking, kAvailable, kDownloading, kReady, kFailed };

std::mutex g_mx;
State g_state = kNone;
std::string g_newVersion, g_notes, g_zipUrl, g_zipSha, g_error;
unsigned long long g_zipBytes = 0;
std::string g_zipPath;
bool g_menuSeen = false;
std::string g_rolledBack;
UpdaterInfo::Check g_check = UpdaterInfo::kNotChecked;

std::string GameDir()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string dir = path;
	return dir.substr(0, dir.find_last_of("\\/"));
}

std::string IniPath() { return GameDir() + "\\imperivm-mod.ini"; }

std::string IniGet(const char *key, const char *def = "")
{
	char buf[512] = {};
	GetPrivateProfileStringA("Updates", key, def, buf, sizeof(buf), IniPath().c_str());
	return buf;
}

void IniSet(const char *key, const std::string &value)
{
	WritePrivateProfileStringA("Updates", key, value.empty() ? nullptr : value.c_str(), IniPath().c_str());
}

bool IsFile(const std::string &p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool HexToBytes(const std::string &hex, std::vector<unsigned char> &out)
{
	if (hex.size() % 2) return false;
	out.clear();
	for (size_t i = 0; i < hex.size(); i += 2) {
		auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
		const int hi = nib(hex[i]), lo = nib(hex[i + 1]);
		if (hi < 0 || lo < 0) return false;
		out.push_back(static_cast<unsigned char>(hi * 16 + lo));
	}
	return true;
}

bool VerifySignature(const std::string &message, const std::string &signatureHex)
{
	std::vector<unsigned char> sig;
	if (!HexToBytes(signatureHex, sig) || sig.size() != 64) return false;
	unsigned char hash[32] = {};
	BCRYPT_ALG_HANDLE sha = nullptr;
	BCRYPT_HASH_HANDLE h = nullptr;
	bool hashed = false;
	if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
		if (BCryptCreateHash(sha, &h, nullptr, 0, nullptr, 0, 0) == 0) {
			hashed = BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char *>(message.data())), static_cast<ULONG>(message.size()), 0) == 0 &&
				BCryptFinishHash(h, hash, sizeof(hash), 0) == 0;
			BCryptDestroyHash(h);
		}
		BCryptCloseAlgorithmProvider(sha, 0);
	}
	if (!hashed) return false;
	bool ok = false;
	BCRYPT_ALG_HANDLE ecdsa = nullptr;
	if (BCryptOpenAlgorithmProvider(&ecdsa, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0) {
		struct {
			BCRYPT_ECCKEY_BLOB header;
			unsigned char xy[64];
		} blob = {};
		blob.header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
		blob.header.cbKey = 32;
		memcpy(blob.xy, kPubKey, sizeof(kPubKey));
		BCRYPT_KEY_HANDLE key = nullptr;
		if (BCryptImportKeyPair(ecdsa, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0) == 0) {
			ok = BCryptVerifySignature(key, nullptr, hash, sizeof(hash), sig.data(), static_cast<ULONG>(sig.size()), 0) == 0;
			BCryptDestroyKey(key);
		}
		BCryptCloseAlgorithmProvider(ecdsa, 0);
	}
	return ok;
}

void SetState(State s, const std::string &error = "")
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_state = s;
	g_error = error;
}

void Fail(const std::string &why)
{
	LogW("update", "%s", TrSource(why).c_str());
	SetState(kFailed, why);
}

void MakeDirs(const std::string &path)
{
	for (size_t i = 3; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '\\') CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
	}
}

void DeleteTree(const std::string &dir)
{
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (f != INVALID_HANDLE_VALUE) {
		do {
			const std::string n = fd.cFileName;
			if (n == "." || n == "..") continue;
			const std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
			else {
				SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileA(p.c_str());
			}
		} while (FindNextFileA(f, &fd));
		FindClose(f);
	}
	RemoveDirectoryA(dir.c_str());
}

bool CollectFiles(const std::string &base, const std::string &rel, std::vector<std::string> &out, std::string &bad)
{
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((base + (rel.empty() ? "" : "\\" + rel) + "\\*").c_str(), &fd);
	if (f == INVALID_HANDLE_VALUE) return true;
	do {
		const std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		const std::string r = rel.empty() ? n : rel + "\\" + n;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (!CollectFiles(base, r, out, bad)) {
				FindClose(f);
				return false;
			}
			continue;
		}
		if (_stricmp(r.c_str(), "winmm.dll") != 0 && _stricmp(r.c_str(), "steam_api.dll") != 0 && _strnicmp(r.c_str(), "imperivm-resurrected\\", 21) != 0) {
			bad = r;
			FindClose(f);
			return false;
		}
		out.push_back(r);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
	return true;
}

void DownloadAndInstall()
{
	std::string version, url, sha;
	unsigned long long bytes;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		version = g_newVersion;
		url = g_zipUrl;
		sha = g_zipSha;
		bytes = g_zipBytes;
	}
	SetState(kDownloading);
	const std::string work = GameDir() + "\\mods\\_downloads\\update-" + version;
	DeleteTree(work);
	MakeDirs(work);
	const std::string zip = work + ".zip";
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_zipPath = zip;
	}
	DeleteFileA(zip.c_str());
	LogI("update", "downloading %s (%.1f MB)", version.c_str(), bytes / 1048576.0);
	std::string err = DlHttpGet(url, nullptr, zip);
	if (!err.empty()) return Fail(TrFmt("could not download: %s", {Tr(err)}));
	if (DlFileSha256(zip) != sha) {
		DeleteFileA(zip.c_str());
		return Fail(Tr("the downloaded file is not the published one (different hash)"));
	}
	err = DlExtract(zip, work);
	DeleteFileA(zip.c_str());
	if (!err.empty()) return Fail(Tr(err));
	std::vector<std::string> files;
	std::string bad;
	if (!CollectFiles(work, "", files, bad)) {
		DeleteTree(work);
		return Fail(TrFmt("the update brings a file that does not belong to it (%s)", {bad}));
	}
	if (std::find_if(files.begin(), files.end(), [](const std::string &f) { return _stricmp(f.c_str(), "winmm.dll") == 0; }) == files.end()) {
		DeleteTree(work);
		return Fail(Tr("the update does not include winmm.dll"));
	}
	const std::string game = GameDir();
	const std::string dll = game + "\\winmm.dll", previous = dll + ".previous";
	auto sizeOf = [](const std::string &path) {
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return static_cast<unsigned long long>(-1);
		return (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	};
	int copied = 1;
	for (const std::string &f : files) {
		if (_stricmp(f.c_str(), "winmm.dll") == 0) continue;
		const std::string dest = game + "\\" + f;
		MakeDirs(dest.substr(0, dest.find_last_of('\\')));
		const bool loaded = _stricmp(f.c_str(), "steam_api.dll") == 0 && GetFileAttributesA(dest.c_str()) != INVALID_FILE_ATTRIBUTES;
		const std::string aside = dest + ".previous";
		if (loaded) {
			SetFileAttributesA(aside.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileA(aside.c_str());
			MoveFileExA(dest.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
		}
		if (!CopyFileA((work + "\\" + f).c_str(), dest.c_str(), FALSE)) {
			const DWORD e = GetLastError();
			if (loaded) MoveFileExA(aside.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING);
			DeleteTree(work);
			return Fail(TrFmt("could not copy %s (error %s)", {f, std::to_string(e)}));
		}
		copied++;
	}
	const std::string staged = dll + ".new", source = work + "\\winmm.dll";
	SetFileAttributesA(staged.c_str(), FILE_ATTRIBUTE_NORMAL);
	DeleteFileA(staged.c_str());
	if (!CopyFileA(source.c_str(), staged.c_str(), FALSE) || sizeOf(staged) != sizeOf(source)) {
		const DWORD e = GetLastError();
		DeleteFileA(staged.c_str());
		DeleteTree(work);
		return Fail(TrFmt("could not prepare the new winmm.dll (error %s)", {std::to_string(e)}));
	}
	SetFileAttributesA(previous.c_str(), FILE_ATTRIBUTE_NORMAL);
	DeleteFileA(previous.c_str());
	if (!MoveFileExA(dll.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		const DWORD e = GetLastError();
		DeleteFileA(staged.c_str());
		DeleteTree(work);
		return Fail(TrFmt("could not move winmm.dll aside (error %s)", {std::to_string(e)}));
	}
	if (!MoveFileExA(staged.c_str(), dll.c_str(), 0)) {
		const DWORD e = GetLastError();
		MoveFileExA(previous.c_str(), dll.c_str(), MOVEFILE_REPLACE_EXISTING);
		DeleteFileA(staged.c_str());
		DeleteTree(work);
		return Fail(TrFmt("could not install the new winmm.dll (error %s)", {std::to_string(e)}));
	}
	DeleteTree(work);
	IniSet("Testing", version);
	IniSet("Starts", "0");
	IniSet("Previous", ModVersionText());
	LogI("update", "%s installed (%d files); it will be used when the game restarts", version.c_str(), copied);
	SetState(kReady);
}

bool BeginCheck(State &before)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_state == kChecking || g_state == kDownloading || g_state == kReady) return false;
	before = g_state;
	g_state = kChecking;
	g_check = UpdaterInfo::kChecking;
	return true;
}

void EndCheck(State state, UpdaterInfo::Check result)
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_state = state;
	g_check = result;
}

void CheckNow(State before)
{
	Json j;
	const std::string err = DlHttpGetJson(kVersionUrl, j);
	if (!err.empty()) {
		LogW("update", "could not check (%s)", err.c_str());
		EndCheck(before, UpdaterInfo::kCheckFailed);
		return;
	}
	const std::string version = j.GetString("version");
	const Json *zip = j.Get("zip");
	const std::string url = zip ? zip->GetString("url") : "", sha = zip ? zip->GetString("sha256") : "";
	const unsigned long long bytes = zip ? static_cast<unsigned long long>(zip->GetNumber("bytes")) : 0;
	if (version.empty() || url.compare(0, 8, "https://") != 0 || sha.size() != 64 || !bytes) {
		LogW("update", "incomplete version.json");
		EndCheck(before, UpdaterInfo::kCheckFailed);
		return;
	}
	if (!VerifySignature("imperivm-resurrected\n" + version + "\n" + sha + "\n" + std::to_string(bytes), j.GetString("signature"))) {
		LogE("update", "the published version %s does NOT carry a valid signature: not offered", version.c_str());
		EndCheck(before, UpdaterInfo::kCheckFailed);
		return;
	}
	if (CompareReleaseVersions(version, ModVersionText()) <= 0) {
		LogI("update", "up to date (%s)", ModVersionText());
		EndCheck(kNone, UpdaterInfo::kUpToDate);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_newVersion = version;
		g_notes = j.GetString("notes");
		g_zipUrl = url;
		g_zipSha = sha;
		g_zipBytes = bytes;
		g_state = kAvailable;
		g_check = UpdaterInfo::kNewer;
	}
	LogI("update", "there is a new version, %s (you have %s)", version.c_str(), ModVersionText());
	if (_stricmp(IniGet("Mode", "notify").c_str(), "auto") == 0) DownloadAndInstall();
}

}

int UpdaterModeGet()
{
	const std::string mode = IniGet("Mode", "notify");
	return _stricmp(mode.c_str(), "auto") == 0 ? 1 : _stricmp(mode.c_str(), "off") == 0 ? 2 : 0;
}

void UpdaterModeSet(int mode)
{
	IniSet("Mode", mode == 1 ? "auto" : mode == 2 ? "off" : "notify");
}

void InstallUpdater()
{
	const std::string testing = IniGet("Testing");
	if (!testing.empty() && testing == ModVersionText()) {
		const int starts = atoi(IniGet("Starts", "0").c_str()) + 1;
		IniSet("Starts", std::to_string(starts));
		const std::string dll = GameDir() + "\\winmm.dll", previous = dll + ".previous", failed = dll + ".failed";
		if (starts >= kMaxBadStarts && IsFile(previous)) {
			DeleteFileA(failed.c_str());
			if (MoveFileExA(dll.c_str(), failed.c_str(), MOVEFILE_REPLACE_EXISTING) && MoveFileExA(previous.c_str(), dll.c_str(), MOVEFILE_REPLACE_EXISTING)) {
				g_rolledBack = testing;
				IniSet("Testing", "");
				IniSet("Starts", "");
				IniSet("Removed", testing);
				LogW("update", "%s did not reach the menu in %d starts: going back to the previous one (%s) on restart", testing.c_str(),
					starts, IniGet("Previous").c_str());
			}
		}
	}
	if (g_rolledBack.empty()) g_rolledBack = IniGet("Removed");
	if (_stricmp(IniGet("Mode", "notify").c_str(), "off") == 0) {
		LogI("update", "search disabled ([Updates] Mode=off)");
		return;
	}
	SafeThread([] {
		Sleep(8000);
		State before = kNone;
		if (BeginCheck(before)) CheckNow(before);
	}).detach();
}

void UpdaterMenuReached()
{
	if (g_menuSeen) return;
	g_menuSeen = true;
	if (!IniGet("Testing").empty() && IniGet("Testing") == ModVersionText()) {
		IniSet("Testing", "");
		IniSet("Starts", "");
		LogI("update", "%s started fine", ModVersionText());
	}
	if (!g_rolledBack.empty()) {
		UiToast(TrFmt("Version %s of Imperivm Resurrected failed to start: the previous one has been restored. Restart the game.", {g_rolledBack}), true);
		IniSet("Removed", "");
		g_rolledBack.clear();
	}
}

UpdaterInfo UpdaterGetInfo()
{
	UpdaterInfo i;
	std::string zip;
	unsigned long long bytes;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		i.state = g_state == kAvailable ? UpdaterInfo::kAvailable : g_state == kDownloading ? UpdaterInfo::kDownloading
				: g_state == kReady ? UpdaterInfo::kReady : g_state == kFailed ? UpdaterInfo::kFailed : UpdaterInfo::kNone;
		i.current = ModVersionText();
		i.newVersion = g_newVersion;
		i.notes = g_notes;
		i.error = g_error;
		i.check = g_check;
		zip = g_zipPath;
		bytes = g_zipBytes;
	}
	if (i.state == UpdaterInfo::kDownloading && bytes && !zip.empty()) {
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (GetFileAttributesExA(zip.c_str(), GetFileExInfoStandard, &fa)) {
			const unsigned long long got = (static_cast<unsigned long long>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
			i.percent = static_cast<int>(std::min<unsigned long long>(99, got * 100 / bytes));
		}
	}
	return i;
}

void UpdaterStartInstall()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_state != kAvailable && g_state != kFailed) return;
		g_state = kDownloading;
	}
	SafeThread(DownloadAndInstall).detach();
}

bool UpdaterCheckNow()
{
	State before = kNone;
	if (!BeginCheck(before)) return false;
	LogI("update", "checking for a new version (asked from the options)");
	SafeThread(CheckNow, before).detach();
	return true;
}

void UpdaterRestartNow()
{
	LogI("update", "restarting the game to use %s", g_newVersion.c_str());
	ModsRestartGame();
}
