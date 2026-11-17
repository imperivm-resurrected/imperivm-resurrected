// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "mods/lang.h"
#include "i18n/tr.h"
#include "core/log.h"
#include "mods/mods.h"
#include "mods/features.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <cstdint>
#include "addr/engine.h"

namespace {
typedef unsigned(__fastcall *HasLocalNotes_t)(void *self, void *edx, void *outPath);
typedef int(__cdecl *VfsFileExists_t)(const char *);
typedef void(__fastcall *StringAssign_t)(void *str, void *edx, const char *text, size_t n);

HasLocalNotes_t g_origHasLocalNotes = nullptr;

std::mutex g_lock;
std::string g_fallback;
bool g_looseReady = false;
std::vector<std::string> g_looseBases;
std::vector<std::string> g_looseFolders;
std::map<std::string, std::string> g_looseCache;

const struct {
	const char *code, *name;
} kNames[] = {
	{"english", "English"}, {"spanish", "Spanish"}, {"italian", "Italian"}, {"french", "Français"},
	{"german", "Deutsch"}, {"portuguese", "Português"}, {"polish", "Polski"}, {"russian", "Русский"},
	{"czech", "Čeština"}, {"hungarian", "Magyar"}, {"romanian", "Română"}, {"turkish", "Türkçe"},
	{"greek", "Ελληνικά"}, {"dutch", "Nederlands"}, {"catalan", "Català"}, {"ukrainian", "Українська"},
};

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string GameDirPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

std::string SettingsPath()
{
	return GameDirPath() + "\\Settings.ini";
}

std::string ModIniPath()
{
	return GameDirPath() + "\\imperivm-mod.ini";
}

bool IsDir(const std::string &p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsFile(const std::string &p)
{
	const DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string SettingsName(const std::string &code)
{
	std::string s = code;
	if (!s.empty()) s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
	return s;
}

std::string ReadSettingsLanguage()
{
	char buf[64] = {};
	GetPrivateProfileStringA("Language", "Default", "English", buf, sizeof(buf), SettingsPath().c_str());
	return Lower(buf);
}

std::string ReadPending()
{
	char buf[64] = {};
	GetPrivateProfileStringA("Language", "Next", "", buf, sizeof(buf), ModIniPath().c_str());
	return Lower(buf);
}

void WritePending(const std::string &code)
{
	WritePrivateProfileStringA("Language", "Next", code.empty() ? nullptr : code.c_str(), ModIniPath().c_str());
}

std::vector<std::string> PakNames(const std::string &dir)
{
	std::vector<std::string> out;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*.pak").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(Lower(fd.cFileName));
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
}

std::string PakLanguage(const std::string &file)
{
	std::string code = file.substr(0, file.size() - 4);
	return code.substr(0, code.find('_'));
}

bool GameHasLanguage(const std::string &code)
{
	for (const std::string &f : PakNames(GameDirPath() + "\\local")) {
		if (PakLanguage(f) == code) return true;
	}
	return false;
}

bool DeclaredLanguage(const std::string &code, ModLanguage &out, std::string *modName = nullptr)
{
	const ModProfile p = ModsLoadProfile(ModsActiveProfileName());
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (!ModsFind(folder, m)) continue;
		for (const ModLanguage &l : m.languages) {
			if (l.code != code) continue;
			out = l;
			if (modName) *modName = m.name;
			return true;
		}
	}
	return false;
}

unsigned __fastcall HookHasLocalNotes(void *self, void *edx, void *outPath)
{
	const unsigned r = g_origHasLocalNotes(self, edx, outPath);
	std::string base;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		base = g_fallback;
	}
	if (r || base.empty() || !outPath) return r;
	const std::string dir = "CurrentGame/local/" + base + "/";
	auto exists = *reinterpret_cast<VfsFileExists_t *>(addr::kPfnVfsFileExists);
	if (!exists || !exists((dir + "notes.xml").c_str())) return r;
	reinterpret_cast<StringAssign_t>(addr::kStringAssign)(outPath, nullptr, dir.c_str(), dir.size());
	LogW("language", "the map has no texts in %s; using those of %s", ModsLanguage().c_str(), base.c_str());
	return 1;
}

struct LocEntry {
	std::string text, justtext, result;
};

bool ValidUtf8(const std::string &s)
{
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		size_t n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 99;
		if (n == 99 || i + n >= s.size() + (n ? 0 : 1)) return false;
		for (size_t k = 1; k <= n; k++) {
			if ((static_cast<unsigned char>(s[i + k]) >> 6) != 2) return false;
		}
		i += n + 1;
	}
	return true;
}

std::string AnsiToUtf8(const std::string &s)
{
	const int wn = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(wn, L'\0');
	MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), &w[0], wn);
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
	std::string out(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, &out[0], n, nullptr, nullptr);
	return out;
}

bool XmlUnescape(const std::string &in, std::string &out)
{
	out.clear();
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] != '&') {
			out += in[i];
			continue;
		}
		const size_t semi = in.find(';', i);
		if (semi == std::string::npos || semi - i > 10) return false;
		const std::string ent = in.substr(i + 1, semi - i - 1);
		if (ent == "amp") out += '&';
		else if (ent == "lt") out += '<';
		else if (ent == "gt") out += '>';
		else if (ent == "quot") out += '"';
		else if (ent == "apos") out += '\'';
		else if (ent.size() > 1 && ent[0] == '#') {
			const unsigned long cp = ent[1] == 'x' || ent[1] == 'X' ? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
			if (cp == 0 || cp > 0x10ffff) return false;
			if (cp < 0x80) out += static_cast<char>(cp);
			else if (cp < 0x800) {
				out += static_cast<char>(0xc0 | (cp >> 6));
				out += static_cast<char>(0x80 | (cp & 0x3f));
			}
			else if (cp < 0x10000) {
				out += static_cast<char>(0xe0 | (cp >> 12));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
				out += static_cast<char>(0x80 | (cp & 0x3f));
			}
			else {
				out += static_cast<char>(0xf0 | (cp >> 18));
				out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
				out += static_cast<char>(0x80 | (cp & 0x3f));
			}
		}
		else return false;
		i = semi;
	}
	return true;
}

std::string XmlEscape(const std::string &in)
{
	std::string out;
	for (char c : in) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		case '\n': out += "&#10;"; break;
		case '\r': out += "&#13;"; break;
		case '\t': out += "&#9;"; break;
		default:
			if (static_cast<unsigned char>(c) >= 0x20) out += c;
		}
	}
	return out;
}

int LineAt(const std::string &s, size_t pos)
{
	return 1 + static_cast<int>(std::count(s.begin(), s.begin() + (std::min)(pos, s.size()), '\n'));
}

std::vector<LocEntry> ParseLocPatch(const std::string &data, std::vector<std::string> &errors)
{
	std::vector<LocEntry> out;
	const std::string lower = Lower(data);
	const std::string tag = "<translationtableentry";
	size_t pos = 0;
	while ((pos = lower.find(tag, pos)) != std::string::npos) {
		const size_t start = pos;
		pos += tag.size();
		std::map<std::string, std::string> attrs;
		std::string error;
		bool closed = false;
		while (pos < data.size()) {
			while (pos < data.size() && isspace(static_cast<unsigned char>(data[pos]))) pos++;
			if (data.compare(pos, 2, "/>") == 0) {
				pos += 2;
				closed = true;
				break;
			}
			if (pos < data.size() && data[pos] == '>') {
				const size_t end = lower.find("</translationtableentry", pos);
				if (end == std::string::npos) {
					error = "missing </translationtableentry>";
					break;
				}
				pos = lower.find('>', end);
				pos = pos == std::string::npos ? data.size() : pos + 1;
				closed = true;
				break;
			}
			const size_t nameStart = pos;
			while (pos < data.size() && (isalnum(static_cast<unsigned char>(data[pos])) || data[pos] == '_' || data[pos] == '-' || data[pos] == ':')) pos++;
			const std::string name = Lower(data.substr(nameStart, pos - nameStart));
			while (pos < data.size() && isspace(static_cast<unsigned char>(data[pos]))) pos++;
			if (name.empty() || pos >= data.size() || data[pos] != '=') {
				error = "malformed attribute";
				break;
			}
			pos++;
			while (pos < data.size() && isspace(static_cast<unsigned char>(data[pos]))) pos++;
			const char quote = pos < data.size() ? data[pos] : 0;
			if (quote != '"' && quote != '\'') {
				error = "the value of " + name + " is not in quotes";
				break;
			}
			const size_t valueEnd = data.find(quote, pos + 1);
			if (valueEnd == std::string::npos) {
				error = "missing closing quotes for " + name;
				break;
			}
			const std::string raw = data.substr(pos + 1, valueEnd - pos - 1);
			pos = valueEnd + 1;
			std::string value;
			if (raw.find('<') != std::string::npos || !XmlUnescape(raw, value)) {
				error = "the value of " + name + " has a < or & not written as &lt; or &amp;";
				break;
			}
			attrs[name] = value;
		}
		if (error.empty() && !closed) error = "the entry is not closed";
		if (error.empty() && attrs["text"].empty()) error = "missing text";
		if (error.empty() && !attrs.count("result")) error = "missing result";
		if (!error.empty()) {
			errors.push_back("line " + std::to_string(LineAt(data, start)) + ": " + error);
			if (!closed) {
				const size_t next = data.find('>', pos);
				pos = next == std::string::npos ? data.size() : next + 1;
			}
			continue;
		}
		LocEntry e;
		e.text = attrs["text"];
		if (e.text.find('@') == std::string::npos && !attrs["context"].empty()) e.text += "@" + attrs["context"];
		e.justtext = attrs["justtext"];
		e.result = attrs["result"];
		out.push_back(e);
	}
	return out;
}

bool ReadFileText(const std::string &path, std::string &out)
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

void AddPatchFiles(const std::string &dir, const std::string &lang, std::vector<std::string> &out)
{
	const std::string single = dir + "\\" + lang + ".loc.xml";
	if (IsFile(single)) out.push_back(single);
	std::vector<std::string> files;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\" + lang + "\\*.loc.xml").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(dir + "\\" + lang + "\\" + fd.cFileName);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::sort(files.begin(), files.end());
	out.insert(out.end(), files.begin(), files.end());
}

bool HookAt(uintptr_t addr, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(addr), detour, orig) == MH_OK && HookEnable(reinterpret_cast<void *>(addr)) == MH_OK) {
		return true;
	}
	LogE("language", "could not hook %s", what);
	return false;
}

}

void InstallLanguage(bool knownExe)
{
	const std::string pending = ReadPending();
	if (!pending.empty()) {
		const std::string before = ReadSettingsLanguage();
		WritePrivateProfileStringA("Language", "Default", SettingsName(pending).c_str(), SettingsPath().c_str());
		WritePending("");
		LogD("language", "%s -> %s (chosen in Options)", before.c_str(), pending.c_str());
	}
	if (!knownExe) return;
	HookAt(addr::kHasLocalNotes, reinterpret_cast<void *>(&HookHasLocalNotes), reinterpret_cast<void **>(&g_origHasLocalNotes),
		"CLanguageSettings::HasLocalNotes");
}

std::string LangDisplayName(const std::string &code)
{
	ModLanguage l;
	if (DeclaredLanguage(code, l) && !l.name.empty()) return l.name;
	for (const auto &n : kNames) {
		if (code == n.code) return n.name;
	}
	return SettingsName(code);
}

std::vector<GameLanguage> LangAvailable()
{
	std::vector<GameLanguage> out;
	auto add = [&](const std::string &code, const std::string &from) {
		if (code.empty()) return;
		for (const GameLanguage &g : out) {
			if (g.code == code) return;
		}
		out.push_back({code, LangDisplayName(code), from});
	};
	for (const std::string &f : PakNames(GameDirPath() + "\\local")) add(PakLanguage(f), "");
	const ModProfile p = ModsLoadProfile(ModsActiveProfileName());
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (!ModsFind(folder, m)) continue;
		for (const ModLanguage &l : m.languages) add(l.code, m.name);
	}
	add(ModsLanguage(), "");
	add(LangNext(), "");
	return out;
}

std::string LangNext()
{
	const std::string pending = ReadPending();
	return pending.empty() ? ReadSettingsLanguage() : pending;
}

bool LangChoose(const std::string &code)
{
	if (code.empty()) return false;
	const std::string running = ModsLanguage();
	WritePrivateProfileStringA("Language", "Default", SettingsName(code).c_str(), SettingsPath().c_str());
	WritePending(code == running ? "" : code);
	LogI("language", "%s chosen for the next start (now %s)", code.c_str(), running.c_str());
	return code != running;
}

std::string LangFallbackFor(const std::string &code)
{
	if (code.empty() || GameHasLanguage(code)) return "";
	ModLanguage declared;
	std::string base = DeclaredLanguage(code, declared) && !declared.base.empty() ? declared.base : "english";
	if (base == code || !GameHasLanguage(base)) base = "english";
	if (base == code) return "";
	std::lock_guard<std::mutex> lock(g_lock);
	g_fallback = base;
	return base;
}

std::string LangFallback()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_fallback;
}

std::string LangLooseFile(const std::string &rel)
{
	const std::string prefix = "currentlang/";
	if (rel.compare(0, prefix.size(), prefix) != 0) return "";
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_looseReady) {
		g_looseReady = true;
		const std::string lang = ModsLanguage();
		for (const std::string &dir : ModsMountedDirs()) {
			if (!IsDir(dir + "\\Local\\" + lang)) continue;
			const std::string folder = ModsDirVfsName(dir);
			g_looseBases.push_back("mods/" + folder + "/Local/" + lang + "/");
			g_looseFolders.push_back(ModsPackFolder(dir));
			LogD("language", "%s brings loose files from %s", folder.c_str(), lang.c_str());
		}
	}
	if (g_looseBases.empty()) return "";
	auto cached = g_looseCache.find(rel);
	if (cached != g_looseCache.end()) return cached->second;
	std::string found;
	const std::string game = GameDirPath() + "\\";
	for (size_t i = 0; i < g_looseBases.size(); i++) {
		const std::string &base = g_looseBases[i];
		// A language file of a root that replaces the game's is ignored unless its own ruleset is in use (mods/rootrules.h).
		if (ModsRootFileIgnored(g_looseFolders[i], rel)) continue;
		std::string disk = game + base + rel.substr(prefix.size());
		std::replace(disk.begin(), disk.end(), '/', '\\');
		if (IsFile(disk)) {
			found = base + rel.substr(prefix.size());
			LogD("language", "%s -> %s", rel.c_str(), found.c_str());
			break;
		}
	}
	g_looseCache.emplace(rel, found);
	return found;
}

std::vector<std::string> LangPatchFiles()
{
	const std::string lang = ModsLanguage();
	std::vector<std::string> out;
	std::vector<std::string> dirs = ModsMountedDirs();
	for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) AddPatchFiles(*it + "\\loc", lang, out);
	AddPatchFiles(GameDirPath() + "\\imperivm-resurrected\\loc", lang, out);
	return out;
}

namespace {
std::string RawAttr(const std::string &tag, const char *name)
{
	const std::string key = std::string(" ") + name + "=\"";
	const size_t at = tag.find(key);
	if (at == std::string::npos) return "";
	const size_t from = at + key.size(), to = tag.find('"', from);
	return to == std::string::npos ? "" : tag.substr(from, to - from);
}

std::multimap<std::string, std::string> FullKeysByText(const std::string &base)
{
	std::multimap<std::string, std::string> out;
	size_t pos = 0;
	while ((pos = base.find("<translationtableentry", pos)) != std::string::npos) {
		const size_t end = base.find('>', pos);
		if (end == std::string::npos) break;
		const std::string tag = base.substr(pos, end - pos);
		pos = end;
		const std::string text = RawAttr(tag, "text"), just = RawAttr(tag, "justtext");
		if (!just.empty() && text != just) out.emplace(just, text);
	}
	return out;
}

}

const char *const kMenuKeys[] = {"Apply", "Reset", "Controls", "Play in window", "Vertical sync",
	"Trap the mouse in the game", "Show FPS and memory", "Network statistics", "Mute in the background",
	"Chat and invitation alerts", "Cutscenes", "Use up to 4 GB of memory", "Brightness", "Autosave",
	"Save when leaving the game", "Language", "Updates", "Monitor", "Multiplayer", "Imperivm League", "Additional content"};

bool LangMergeLoc(const std::string &base, const std::vector<std::string> &patches, std::string &out)
{
	std::multimap<std::string, std::string> fullKeys;
	bool fullKeysReady = false;
	unsigned variants = 0;
	std::string head = base, tail;
	const std::string lower = Lower(base);
	const size_t close = lower.rfind("</translationtable");
	if (close != std::string::npos) {
		head = base.substr(0, close);
		tail = base.substr(close);
	}
	else if (lower.find("<translationtable") == std::string::npos) {
		head = "<translationtable>\r\n";
		tail = "</translationtable>\r\n";
	}
	else {
		LogW("language", "game.loc.xml does not end with </translationtable>; the mods' texts are not added to it");
		return false;
	}
	const std::string game = GameDirPath() + "\\";
	std::string added;
	unsigned total = 0;
	if (ModsLanguage() != "english") {
		for (const char *key : kMenuKeys) {
			const std::string translated = Tr(key);
			if (translated == key) continue;
			added += "<translationtableentry text=\"" + XmlEscape(key) + "\" result=\"" + XmlEscape(translated) + "\"/>\r\n";
			total++;
		}
	}
	for (const std::string &path : patches) {
		const std::string shortName = path.compare(0, game.size(), game) == 0 ? path.substr(game.size()) : path;
		std::string data;
		if (!ReadFileText(path, data)) {
			LogW("language", "could not read %s", shortName.c_str());
			continue;
		}
		if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
		if (!ValidUtf8(data)) {
			data = AnsiToUtf8(data);
			LogI("language", "%s was not in UTF-8; read as ANSI", shortName.c_str());
		}
		std::vector<std::string> errors;
		const std::vector<LocEntry> entries = ParseLocPatch(data, errors);
		for (const LocEntry &e : entries) {
			added += "<translationtableentry text=\"" + XmlEscape(e.text) + "\"";
			if (!e.justtext.empty()) added += " justtext=\"" + XmlEscape(e.justtext) + "\"";
			added += " result=\"" + XmlEscape(e.result) + "\"/>\r\n";
			if (e.text.find('@') != std::string::npos) continue;
			if (!fullKeysReady) {
				fullKeys = FullKeysByText(base);
				fullKeysReady = true;
			}
			const auto range = fullKeys.equal_range(XmlEscape(e.text));
			for (auto it = range.first; it != range.second; ++it) {
				added += "<translationtableentry text=\"" + it->second + "\" result=\"" + XmlEscape(e.result) + "\"/>\r\n";
				variants++;
			}
		}
		total += static_cast<unsigned>(entries.size());
		LogD("language", "%s: %u texts%s", shortName.c_str(), static_cast<unsigned>(entries.size()),
			errors.empty() ? "" : (", " + std::to_string(errors.size()) + " with errors (skipped)").c_str());
		for (size_t i = 0; i < errors.size() && i < 10; i++) LogW("language", "%s", errors[i].c_str());
		if (entries.empty() && errors.empty()) LogW("language", "has no <translationtableentry>");
	}
	if (!total) return false;
	out = head + "\r\n" + added + tail;
	LogI("language", "%u mod texts added to game.loc.xml (%u variants with context)", total, variants);
	return true;
}

namespace {

void ForgetLooseFiles()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_looseReady = false;
	g_looseBases.clear();
	g_looseCache.clear();
}

const bool g_featureRegistered = (ModsRegisterFeature({5, &ForgetLooseFiles, nullptr}), true);

}
