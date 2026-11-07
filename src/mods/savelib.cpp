// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <mutex>

#include "core/json.h"
#include "core/legacy.h"
#include "core/log.h"
#include "mods/mods.h"
#include "mods/savelib.h"
#include "i18n/tr.h"
#include "addr/state.h"

namespace {

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

bool EndsWith(const std::string &s, const char *suffix)
{
	const size_t n = strlen(suffix);
	return s.size() >= n && _stricmp(s.c_str() + s.size() - n, suffix) == 0;
}

bool FileExists(const std::string &path)
{
	const DWORD a = GetFileAttributesA(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ProfileDir(const std::string &profile) { return GameDir() + "\\Profiles\\" + profile + "\\"; }
std::string OnlineDir(const std::string &profile) { return ProfileDir(profile) + kSaveOnlineFolder + "\\"; }

const char *const kAuxSuffixes[] = {".bmp", ".cameras", ".mods.json", ".stats"};

}

std::string SaveLibActiveProfile()
{
	FILE *f = nullptr;
	if (fopen_s(&f, (GameDir() + "\\Profiles\\profiles.ini").c_str(), "rb") != 0 || !f) return "";
	char line[512];
	std::string out;
	while (fgets(line, sizeof(line), f)) {
		std::string l = line;
		while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ')) l.pop_back();
		if (_strnicmp(l.c_str(), "default=", 8) != 0) continue;
		out = l.substr(8);
		const size_t slash = out.find_last_of("/\\");
		if (slash != std::string::npos) out = out.substr(slash + 1);
		break;
	}
	fclose(f);
	return out;
}

std::string SaveLibGroupKey(const SaveRec &rec)
{
	return rec.session.empty() ? "l:" + Lower(rec.vfsPath) : rec.session;
}

bool SaveLibIsAutosaveName(const std::string &baseName)
{
	for (const std::string &variant : TrAllLanguages("Autosave %02dh%02d")) {
		const std::string prefix = variant.substr(0, variant.find('%'));
		if (!prefix.empty() && _strnicmp(baseName.c_str(), prefix.c_str(), prefix.size()) == 0) return true;
	}
	return false;
}

bool SaveLibEnsureOnlineDir(const std::string &profile)
{
	if (profile.empty()) return false;
	const std::string dir = OnlineDir(profile);
	return CreateDirectoryA(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

namespace {

bool MoveToOnlineFolder(SaveRec &r)
{
	if (!SaveLibEnsureOnlineDir(r.profile)) return false;
	const std::string dir = OnlineDir(r.profile);
	const std::string ext = r.diskPath.substr(r.diskPath.size() - 4);
	std::string base = r.baseName;
	for (int n = 2; FileExists(dir + base + ext) && n < 100; n++) base = r.baseName + " (" + std::to_string(n) + ")";
	const std::string target = dir + base + ext;
	if (FileExists(target) || !MoveFileA(r.diskPath.c_str(), target.c_str())) {
		LogW("saves", "could not move \"%s\" to the folder %s (error %lu)", r.vfsPath.c_str(), kSaveOnlineFolder, GetLastError());
		return false;
	}
	for (const char *suffix : kAuxSuffixes) {
		if (FileExists(r.diskPath + suffix)) MoveFileExA((r.diskPath + suffix).c_str(), (target + suffix).c_str(), MOVEFILE_REPLACE_EXISTING);
		else DeleteFileA((target + suffix).c_str());
	}
	const std::string oldVfs = r.vfsPath;
	r.vfsPath = "Profiles/" + r.profile + "/" + kSaveOnlineFolder + "/" + base + ext;
	r.diskPath = target;
	if (!r.autosave || !SaveLibIsAutosaveName(r.baseName)) r.name = base;
	r.baseName = base;
	if (r.session.empty()) {
		std::map<std::string, std::string> names = SaveLibLoadGameNames(r.profile);
		const auto it = names.find("l:" + Lower(oldVfs));
		if (it != names.end()) {
			SaveLibSetGameName(r.profile, "l:" + Lower(r.vfsPath), it->second);
			SaveLibSetGameName(r.profile, "l:" + Lower(oldVfs), "");
		}
	}
	LogD("saves", "\"%s\" is online: becomes \"%s\"", oldVfs.c_str(), r.vfsPath.c_str());
	return true;
}

void ScanDir(const std::string &profile, bool onlineFolder, std::vector<SaveRec> &out)
{
	const std::string dir = onlineFolder ? OnlineDir(profile) : ProfileDir(profile);
	const std::string vfsDir = "Profiles/" + profile + "/" + (onlineFolder ? std::string(kSaveOnlineFolder) + "/" : "");
	std::vector<size_t> toMove;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const std::string file = fd.cFileName;
		int kind;
		if (EndsWith(file, ".snr")) kind = kSaveStrategy;
		else if (EndsWith(file, ".adv")) kind = kSaveAdventure;
		else if (EndsWith(file, ".con")) kind = kSaveConquest;
		else if (EndsWith(file, ".dmo")) kind = kSaveReplay;
		else continue;
		SaveRec r;
		r.kind = kind;
		r.profile = profile;
		r.baseName = file.substr(0, file.size() - 4);
		r.name = r.baseName;
		if (kind == kSaveAdventure || kind == kSaveConquest) {
			const size_t dash = r.baseName.find_last_of('-');
			if (dash != std::string::npos && dash > 0) {
				r.name = r.baseName.substr(0, dash);
				r.campaign = r.baseName.substr(dash + 1);
			}
		}
		r.vfsPath = vfsDir + file;
		r.diskPath = dir + file;
		r.time = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
		r.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		r.hasThumb = FileExists(r.diskPath + ".bmp");
		Json j;
		if (ReadJsonFile(r.diskPath + ".mods.json", j)) {
			r.online = j.GetBool("online", false);
			r.autosave = j.GetBool("autosave", false);
			r.session = j.GetString("session");
			r.modProfile = LegacyProfileName(j.GetString("profile"));
			r.modHash = j.GetString("gameplayFingerprint");
			r.modSchema = static_cast<int>(j.GetNumber("schema", 1));
			r.playSeconds = static_cast<int>(j.GetNumber("seconds", -1));
			if (const Json *states = j.Get("states")) {
				for (const Json &e : states->arr) r.playerStates.push_back(static_cast<int>(e.num));
			}
			if (const Json *mods = j.Get("mods")) {
				for (const Json &e : mods->arr) {
					SaveMod m;
					m.folder = e.GetString("folder");
					m.id = e.GetString("id");
					m.name = e.GetString("name");
					m.version = e.GetString("version");
					m.hash = e.GetString("fingerprint");
					m.kind = ModsKindFromId(e.GetString("kind"));
					m.oldGameplay = e.GetBool("gameplay", false);
					if (!m.folder.empty() || !m.id.empty()) r.mods.push_back(std::move(m));
				}
			}
		}
		if (SaveLibIsAutosaveName(r.baseName)) r.autosave = true;
		if (r.autosave && SaveLibIsAutosaveName(r.baseName)) r.name = "Autosave";
		if (onlineFolder && kind == kSaveStrategy) r.online = true;
		else if (!onlineFolder && r.online && kind == kSaveStrategy) toMove.push_back(out.size());
		out.push_back(std::move(r));
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	for (size_t i : toMove) MoveToOnlineFolder(out[i]);
}

}

std::vector<SaveRec> SaveLibScan(const std::string &profileArg)
{
	std::vector<SaveRec> out;
	const std::string profile = profileArg.empty() ? SaveLibActiveProfile() : profileArg;
	if (profile.empty()) return out;
	ScanDir(profile, false, out);
	ScanDir(profile, true, out);
	std::sort(out.begin(), out.end(), [](const SaveRec &a, const SaveRec &b) { return a.time > b.time; });
	return out;
}

bool SaveLibLoadThumb(const SaveRec &rec, std::vector<uint32_t> &bgra, int &w, int &h)
{
	FILE *f = nullptr;
	if (fopen_s(&f, (rec.diskPath + ".bmp").c_str(), "rb") != 0 || !f) return false;
	std::vector<unsigned char> data;
	unsigned char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && data.size() < 8u * 1024 * 1024) data.insert(data.end(), buf, buf + n);
	fclose(f);
	if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') return false;
	auto u32 = [&](size_t o) { return static_cast<uint32_t>(data[o] | data[o + 1] << 8 | data[o + 2] << 16 | static_cast<uint32_t>(data[o + 3]) << 24); };
	const uint32_t offset = u32(10);
	const int width = static_cast<int>(u32(18));
	int height = static_cast<int>(u32(22));
	const int bpp = data[28] | data[29] << 8;
	const uint32_t compression = u32(30);
	const bool topDown = height < 0;
	if (topDown) height = -height;
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || (bpp != 16 && bpp != 24 && bpp != 32)) return false;
	const size_t stride = (static_cast<size_t>(width) * bpp + 31) / 32 * 4;
	if (static_cast<uint64_t>(offset) + static_cast<uint64_t>(stride) * static_cast<uint64_t>(height) > data.size()) return false;
	bool is565 = false;
	if (bpp == 16 && compression == 3 && data.size() >= 66) is565 = u32(58) == 0x07E0;
	bgra.assign(static_cast<size_t>(width) * height, 0xff000000u);
	for (int y = 0; y < height; y++) {
		const unsigned char *row = &data[offset + stride * (topDown ? y : height - 1 - y)];
		for (int x = 0; x < width; x++) {
			unsigned r, g, b;
			if (bpp == 16) {
				const unsigned v = row[x * 2] | row[x * 2 + 1] << 8;
				if (is565) {
					r = (v >> 11) & 31;
					g = (v >> 5) & 63;
					b = v & 31;
					r = r << 3 | r >> 2;
					g = g << 2 | g >> 4;
					b = b << 3 | b >> 2;
				}
				else {
					r = (v >> 10) & 31;
					g = (v >> 5) & 31;
					b = v & 31;
					r = r << 3 | r >> 2;
					g = g << 3 | g >> 2;
					b = b << 3 | b >> 2;
				}
			}
			else {
				const unsigned char *px = row + x * (bpp / 8);
				b = px[0];
				g = px[1];
				r = px[2];
			}
			bgra[static_cast<size_t>(y) * width + x] = 0xff000000u | r << 16 | g << 8 | b;
		}
	}
	w = width;
	h = height;
	return true;
}

bool SaveLibDelete(const SaveRec &rec)
{
	std::string list = rec.diskPath;
	list.push_back('\0');
	for (const char *suffix : kAuxSuffixes) {
		if (FileExists(rec.diskPath + suffix)) {
			list += rec.diskPath + suffix;
			list.push_back('\0');
		}
	}
	list.push_back('\0');
	SHFILEOPSTRUCTA op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = list.c_str();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	const int rc = SHFileOperationA(&op);
	LogI("saves", "deleted \"%s\" (%s)", rec.vfsPath.c_str(), rc == 0 && !op.fAnyOperationsAborted ? "to the recycle bin" : "ERROR");
	return rc == 0 && !op.fAnyOperationsAborted && !FileExists(rec.diskPath);
}

namespace {

std::string NewBase(const SaveRec &rec, const std::string &newName)
{
	return rec.campaign.empty() ? newName : newName + "-" + rec.campaign;
}

bool ValidName(const std::string &n)
{
	if (n.empty() || n.size() > 80 || n.back() == '.' || n.back() == ' ' || n.front() == ' ') return false;
	for (char c : n) {
		if (static_cast<unsigned char>(c) < 32 || strchr("\\/:*?\"<>|", c)) return false;
	}
	return true;
}

}

bool SaveLibValidName(const std::string &name) { return ValidName(name); }

bool SaveLibExists(const SaveRec &rec, const std::string &newName)
{
	const std::string dir = rec.diskPath.substr(0, rec.diskPath.find_last_of('\\') + 1);
	const std::string ext = rec.diskPath.substr(rec.diskPath.size() - 4);
	const std::string target = dir + NewBase(rec, newName) + ext;
	return _stricmp(target.c_str(), rec.diskPath.c_str()) != 0 && FileExists(target);
}

std::string SaveLibRename(const SaveRec &rec, const std::string &newName)
{
	if (!ValidName(newName)) return "That name is not valid (no \\ / : * ? \" < > |)";
	const std::string dir = rec.diskPath.substr(0, rec.diskPath.find_last_of('\\') + 1);
	const std::string ext = rec.diskPath.substr(rec.diskPath.size() - 4);
	const std::string target = dir + NewBase(rec, newName) + ext;
	if (target == rec.diskPath) return "";
	if (SaveLibIsAutosaveName(newName)) return "That name is reserved for autosaves";
	if (SaveLibExists(rec, newName)) return "A game with that name already exists";
	if (!MoveFileA(rec.diskPath.c_str(), target.c_str())) return "Couldn't change the name";
	for (const char *suffix : kAuxSuffixes) {
		if (FileExists(rec.diskPath + suffix)) MoveFileExA((rec.diskPath + suffix).c_str(), (target + suffix).c_str(), MOVEFILE_REPLACE_EXISTING);
		else DeleteFileA((target + suffix).c_str());
	}
	if (rec.autosave) {
		Json j;
		if (ReadJsonFile(target + ".mods.json", j)) {
			j.Set("autosave", Json::MakeBool(false));
			WriteJsonFile(target + ".mods.json", j);
		}
	}
	LogI("saves", "\"%s\" renamed to \"%s\"", rec.baseName.c_str(), NewBase(rec, newName).c_str());
	return "";
}

namespace {

void CleanOrphansIn(const std::string &dirA)
{
	const int n = MultiByteToWideChar(CP_ACP, 0, dirA.c_str(), -1, nullptr, 0);
	if (n <= 1) return;
	std::wstring dir(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_ACP, 0, dirA.c_str(), -1, &dir[0], n);
	auto endsWith = [](const std::wstring &s, const wchar_t *tail) {
		const size_t len = wcslen(tail);
		return s.size() >= len && _wcsicmp(s.c_str() + s.size() - len, tail) == 0;
	};
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	std::vector<std::wstring> orphans;
	do {
		const std::wstring name = fd.cFileName;
		const wchar_t *suffix = endsWith(name, L".mods.json") ? L".mods.json" : endsWith(name, L".cameras") ? L".cameras" : endsWith(name, L".bmp") ? L".bmp" : endsWith(name, L".stats") ? L".stats" : nullptr;
		if (!suffix) continue;
		const std::wstring main = name.substr(0, name.size() - wcslen(suffix));
		if (!(endsWith(main, L".snr") || endsWith(main, L".adv") || endsWith(main, L".con") || endsWith(main, L".dmo"))) continue;
		if (GetFileAttributesW((dir + main).c_str()) == INVALID_FILE_ATTRIBUTES) orphans.push_back(name);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	for (const std::wstring &o : orphans) DeleteFileW((dir + o).c_str());
	if (!orphans.empty()) LogD("saves", "%u auxiliary files of saves that no longer exist, removed", static_cast<unsigned>(orphans.size()));
}

}

void SaveLibCleanOrphans(const std::string &profile)
{
	if (profile.empty()) return;
	CleanOrphansIn(ProfileDir(profile));
	CleanOrphansIn(OnlineDir(profile));
}

void SaveLibPruneAutosaves(const std::string &profile, const std::string &session, int keep)
{
	if (session.empty() || keep < 0) return;
	std::vector<SaveRec> mine;
	for (SaveRec &r : SaveLibScan(profile)) {
		if (r.autosave && r.session == session) mine.push_back(std::move(r));
	}
	for (size_t i = static_cast<size_t>(keep); i < mine.size(); i++) SaveLibDelete(mine[i]);
}

std::map<std::string, std::string> SaveLibLoadGameNames(const std::string &profile)
{
	std::map<std::string, std::string> out;
	Json j;
	if (!profile.empty() && ReadJsonFile(ProfileDir(profile) + "games.json", j) && j.type == Json::Object) {
		for (const auto &kv : j.obj) {
			if (kv.second.type == Json::String && !kv.second.str.empty()) out[kv.first] = kv.second.str;
		}
	}
	return out;
}

bool SaveLibSetGameName(const std::string &profile, const std::string &key, const std::string &name)
{
	if (profile.empty() || key.empty()) return false;
	std::map<std::string, std::string> all = SaveLibLoadGameNames(profile);
	if (name.empty()) all.erase(key);
	else all[key] = name;
	Json j = Json::MakeObject();
	for (const auto &kv : all) j.Set(kv.first, Json::MakeString(kv.second));
	return WriteJsonFile(ProfileDir(profile) + "games.json", j);
}

namespace {

std::mutex g_sessionMx;
std::string g_session;
int g_ignoreLoads = 0;
DWORD g_adoptTick = 0;

std::string NewSessionId()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	uint64_t x = (static_cast<uint64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) ^ (static_cast<uint64_t>(qpc.QuadPart) * 0x9E3779B97F4A7C15ull) ^
		GetCurrentProcessId();
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	char buf[24];
	sprintf_s(buf, "%012llx", x & 0xffffffffffffull);
	return buf;
}

}

void SaveSessionBegin()
{
	std::lock_guard<std::mutex> lock(g_sessionMx);
	if (g_adoptTick && !g_session.empty() && GetTickCount() - g_adoptTick < 20000) {
		LogD("saves", "the game continues session %s", g_session.c_str());
	}
	else {
		g_session = NewSessionId();
		LogD("saves", "new session %s", g_session.c_str());
	}
	g_adoptTick = 0;
}

void SaveSessionIgnoreLoads(bool ignore)
{
	std::lock_guard<std::mutex> lock(g_sessionMx);
	g_ignoreLoads += ignore ? 1 : -1;
}

int SaveLibGameSeconds()
{
	__try {
		const char *game = *reinterpret_cast<char *const *>(addr::kGame);
		if (!game) return -1;
		const unsigned ms = *reinterpret_cast<const unsigned *>(game + 0x1258);
		return ms < 1000u * 3600 * 1000 ? static_cast<int>(ms / 1000) : -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

bool SaveLibPlayerStates(int out[8])
{
	__try {
		const char *game = *reinterpret_cast<char *const *>(addr::kGame);
		if (!game) return false;
		for (int i = 0; i < 8; i++) {
			const int state = *reinterpret_cast<const int *>(game + 0x12cc + i * 800 + 0x290);
			out[i] = state >= 1 && state <= 3 ? state : -1;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SaveSessionLoadsIgnored()
{
	std::lock_guard<std::mutex> lock(g_sessionMx);
	return g_ignoreLoads > 0;
}

void SaveSessionOnLoad(const std::string &relIn)
{
	{
		std::lock_guard<std::mutex> lock(g_sessionMx);
		if (g_ignoreLoads > 0) return;
	}
	std::string rel = relIn;
	for (char &c : rel) {
		if (c == '/') c = '\\';
	}
	Json j;
	std::string session;
	if (ReadJsonFile(GameDir() + "\\" + rel + ".mods.json", j)) session = j.GetString("session");
	if (session.empty()) {
		for (char &c : rel) {
			if (c == '\\') c = '/';
		}
		session = "l:" + Lower(rel);
	}
	std::lock_guard<std::mutex> lock(g_sessionMx);
	g_session = session;
	g_adoptTick = GetTickCount();
	LogD("saves", "reading \"%s\" (session %s)", relIn.c_str(), session.c_str());
}

std::string SaveSessionCurrent()
{
	std::lock_guard<std::mutex> lock(g_sessionMx);
	if (g_session.empty()) g_session = NewSessionId();
	return g_session;
}
