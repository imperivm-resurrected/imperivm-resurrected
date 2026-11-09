// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "mods/patch.h"
#include "hooks/builtinfixes.h"
#include "core/log.h"
#include "core/sha256.h"
#include "mods/mods.h"
#include "mods/patchxml.h"
#include "mods/vfs.h"
#include "i18n/tr.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include "core/uiport.h"

namespace {

struct Source {
	std::string folder;
	std::string path;
	std::string text;
	std::string gameFileSha256;
};

std::mutex g_lock;
bool g_ready = false;
std::map<std::string, std::vector<Source>> g_patches;
std::map<std::string, std::vector<std::string>> g_errors;
std::map<std::string, std::string> g_bases;
std::set<std::string> g_noBase;
std::map<std::string, std::vector<std::vector<PatchedValue>>> g_resolved;
std::set<std::string> g_toasted;

std::string HiddenFileUsedBy(const std::string &text)
{
	for (const char *attr : {"vs=\"", "verify=\""}) {
		const size_t len = strlen(attr);
		for (size_t at = text.find(attr); at != std::string::npos; at = text.find(attr, at)) {
			at += len;
			const size_t end = text.find('"', at);
			if (end == std::string::npos) break;
			std::string path = text.substr(at, end - at);
			for (char &c : path) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (VfsIsHidden(path)) return path;
			at = end;
		}
	}
	return "";
}

std::vector<BuiltinPatch> UsableBuiltinPatches(bool log)
{
	std::vector<BuiltinPatch> out;
	for (const BuiltinPatch &b : BuiltinPatches()) {
		const std::string hidden = HiddenFileUsedBy(b.text);
		if (hidden.empty()) out.push_back(b);
		else if (log) LogI("patch", "built-in fix of %s not applied: an active content pack hides %s", b.path.c_str(), hidden.c_str());
	}
	return out;
}

std::string GameDirPath()
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

void Walk(const std::string &base, const std::string &sub, const std::string &folder, std::map<std::string, std::vector<Source>> &out)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((base + sub + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::string name = fd.cFileName;
		if (name == "." || name == "..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			Walk(base, sub + name + "\\", folder, out);
			continue;
		}
		std::string rel = Lower(sub + name);
		std::replace(rel.begin(), rel.end(), '\\', '/');
		out[rel].push_back({folder, base + sub + name});
	} while (FindNextFileA(h, &fd));
	FindClose(h);
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

void AddError(const std::string &folder, const std::string &rel, const std::string &message)
{
	const std::string who = folder.empty() ? "imperivm-resurrected\\patches" : folder;
	const std::string line = rel + ": " + message;
	bool first = false, toast = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		std::vector<std::string> &list = g_errors[who];
		first = std::find(list.begin(), list.end(), line) == list.end();
		if (first && list.size() < 50) list.push_back(line);
		toast = first && g_toasted.insert(who).second;
	}
	if (first) LogW("patch", "(%s) %s", who.c_str(), line.c_str());
	if (toast) UiToast(TrFmt("Patch with errors in %s: %s. There are more details in the content manager and the log.", {who, line}), true);
}

}

void PatchRebuild()
{
	std::map<std::string, std::vector<Source>> found;
	std::vector<std::string> dirs = ModsMountedDirs();
	for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
		Walk(*it + "\\patches\\", "", ModsPackFolder(*it), found);
	}
	Walk(GameDirPath() + "\\imperivm-resurrected\\patches\\", "", "", found);
	for (const BuiltinPatch &b : UsableBuiltinPatches(true)) found[b.path].insert(found[b.path].begin(), {"Imperivm Resurrected", "", b.text, b.gameFileSha256});
	unsigned files = 0;
	for (const auto &kv : found) files += static_cast<unsigned>(kv.second.size());
	const size_t targets = found.size();
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_patches.swap(found);
		g_ready = true;
	}
	if (files) LogI("patch", "%u patch files for %u game files", files, static_cast<unsigned>(targets));
}

bool PatchHas(const std::string &rel)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_ready && !g_patches.empty() && g_patches.count(rel) != 0;
}

bool PatchApply(const std::string &rel, std::string &data)
{
	std::vector<Source> sources;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		auto it = g_patches.find(rel);
		if (it == g_patches.end()) return false;
		sources = it->second;
	}
	const bool isIni = rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".ini") == 0;
	const bool isXml = rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".xml") == 0;
	const bool isVs = rel.size() > 3 && rel.compare(rel.size() - 3, 3, ".vs") == 0;
	std::string loadedSha256;
	for (const Source &s : sources) {
		if (s.gameFileSha256.empty()) continue;
		loadedSha256 = HashString(data);
		break;
	}
	bool changed = false;
	std::set<std::string> removed;
	std::unique_ptr<XmlPatchRun> xml;
	if (isXml) {
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_bases[rel] = data;
		}
		xml = std::make_unique<XmlPatchRun>(data);
	}
	for (const Source &s : sources) {
		if (!s.gameFileSha256.empty() && s.gameFileSha256 != loadedSha256) {
			LogI("patch", "built-in fix of %s not applied: the loaded file is not the game's own (a content pack brings its own version)", rel.c_str());
			continue;
		}
		if (!isIni && !isXml && !isVs) {
			AddError(s.folder, rel, Tr("only .xml, .ini and .vs files can be patched"));
			continue;
		}
		std::string text = s.text;
		if (text.empty() && !ReadFileText(s.path, text)) {
			AddError(s.folder, rel, Tr("the patch file cannot be read"));
			continue;
		}
		std::vector<std::string> errors;
		bool ok = false;
		if (xml) {
			ok = xml->Apply(text, errors);
		}
		else {
			std::string work = data;
			ok = isIni ? PatchIniText(work, text, errors, &removed) : PatchScriptText(work, text, errors);
			if (ok) data.swap(work);
		}
		for (const std::string &e : errors) AddError(s.folder, rel, e);
		if (ok) {
			changed = true;
			LogI("patch", "(%s) %s applied%s", s.folder.empty() ? "imperivm-resurrected\\patches" : s.folder.c_str(), rel.c_str(),
				errors.empty() ? "" : (" (" + std::to_string(errors.size()) + " operations with errors)").c_str());
		}
	}
	if (xml && changed) data = xml->Text();
	return changed;
}

void PatchReadBases(const std::vector<std::string> &rels)
{
	unsigned read = 0;
	for (const std::string &rel : rels) {
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (g_bases.count(rel) || g_noBase.count(rel)) continue;
		}
		std::string data;
		const bool ok = VfsReadUnpatched(rel, data);
		std::lock_guard<std::mutex> lock(g_lock);
		if (ok) {
			g_bases.emplace(rel, std::move(data));
			read++;
		}
		else {
			g_noBase.insert(rel);
		}
	}
	if (read) LogI("patch", "%u game files read without patches to compare the patches of several packs", read);
}

std::vector<std::vector<PatchedValue>> PatchResolve(const std::string &rel, const std::vector<std::string> &patches)
{
	std::string key = rel, base;
	for (const std::string &p : patches) key += "|" + std::to_string(p.size()) + ":" + std::to_string(std::hash<std::string>()(p));
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const auto done = g_resolved.find(key);
		if (done != g_resolved.end()) return done->second;
		const auto it = g_bases.find(rel);
		if (it == g_bases.end()) return {};
		base = it->second;
	}
	std::vector<std::string> before;
	for (const BuiltinPatch &b : UsableBuiltinPatches(false)) {
		if (b.path == rel && (b.gameFileSha256.empty() || b.gameFileSha256 == HashString(base))) before.push_back(b.text);
	}
	const std::vector<std::vector<PatchedValue>> out = PatchXmlResolve(rel, base, before, patches);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_resolved.size() >= 64) g_resolved.clear();
	g_resolved[key] = out;
	return out;
}

void PatchMissingTarget(const std::string &rel)
{
	std::vector<Source> sources;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		auto it = g_patches.find(rel);
		if (it == g_patches.end()) return;
		sources = it->second;
	}
	for (const Source &s : sources) AddError(s.folder, rel, Tr("the game has no such file (check the patch path)"));
}

std::vector<std::string> PatchErrorsFor(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_lock);
	auto it = g_errors.find(folder);
	return it == g_errors.end() ? std::vector<std::string>() : it->second;
}
