// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "game/scripterrors.h"
#include "game/gamehook.h"
#include "core/log.h"
#include "mods/mods.h"
#include "mods/features.h"
#include "i18n/tr.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <cstdint>
#include "addr/script.h"
#include "core/winpath.h"
#include "core/strutil.h"

namespace {
const unsigned kMessageOffset = 0x290;

void *g_origReportError = nullptr;
typedef const char *(__fastcall *LoadScriptFile_t)(void *lib, void *edx, const char *name, const char *path, const char *sig, int reload);
LoadScriptFile_t g_origLoadScript = nullptr;
const char *g_compiling = nullptr;

DWORD g_reportCompiler = 0;
DWORD g_reportReturn = 0;

std::mutex g_lock;
std::vector<std::string> g_errors;

using winpath::GameDir;

using strutil::Lower;

bool ParseMessage(const std::string &line, std::string &path, std::string &rest)
{
	const size_t paren = line.find('(');
	if (paren == std::string::npos || paren == 0) return false;
	size_t i = paren + 1;
	auto digits = [&]() {
		const size_t s = i;
		while (i < line.size() && isdigit(static_cast<unsigned char>(line[i]))) i++;
		return i > s;
	};
	if (!digits() || line.compare(i, 2, ", ") != 0) return false;
	i += 2;
	if (!digits() || line.compare(i, 2, "):") != 0) return false;
	path = line.substr(0, paren);
	rest = line.substr(paren);
	return true;
}

bool CopyMessage(const char *compiler, char *out)
{
	__try {
		memcpy(out, compiler + kMessageOffset, 0x100);
		out[0x100] = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void __stdcall OnReportError(const char *compiler)
{
	if (reinterpret_cast<uintptr_t>(compiler) < 0x10000) return;
	char text[0x101];
	if (!CopyMessage(compiler, text)) return;
	std::string msg = text;
	if (!msg.empty() && msg[0] == '(' && g_compiling && g_compiling[0]) msg = std::string(g_compiling) + msg;
	while (!msg.empty() && isspace(static_cast<unsigned char>(msg.back()))) msg.pop_back();
	if (msg.empty()) return;
	bool added = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (std::find(g_errors.begin(), g_errors.end(), msg) == g_errors.end()) {
			g_errors.push_back(msg);
			if (g_errors.size() > 300) g_errors.erase(g_errors.begin());
			added = true;
		}
	}
	if (added) LogE("script", "compilation error: %s", msg.c_str());
}

const char *__fastcall HookLoadScriptFile(void *lib, void *edx, const char *name, const char *path, const char *sig, int reload)
{
	const char *previous = g_compiling;
	g_compiling = name;
	const char *r = g_origLoadScript(lib, edx, name, path, sig, reload);
	g_compiling = previous;
	return r;
}

__declspec(naked) void HookReportError()
{
	__asm {
		pop eax
		mov g_reportReturn, eax
		mov eax, [esp]
		mov g_reportCompiler, eax
		call dword ptr [g_origReportError]
		push eax
		push g_reportCompiler
		call OnReportError
		pop eax
		push g_reportReturn
		ret
	}
}

}

void ScriptErrorsInit(bool knownExe)
{
	CreateDirectoryA((GameDir() + "\\Logs").c_str(), nullptr);
	if (!knownExe) return;
	if (!HookFunction(addr::kReportError, &HookReportError, &g_origReportError)) {
		LogE("script", "errors: could not hook CScriptCompiler::ReportError");
		return;
	}
	if (!HookFunction(addr::kLoadScriptFile, &HookLoadScriptFile, &g_origLoadScript)) {
		LogE("script", "errors: could not hook LoadScriptFile (errors will appear without the script name)");
	}
	LogI("script", "errors: hooked");
}

std::vector<std::string> ScriptErrorsFor(const std::string &folder)
{
	std::vector<std::string> out;
	ModInfo mod;
	if (!ModsFind(folder, mod)) return out;
	const std::set<std::string> paths(mod.paths.begin(), mod.paths.end());
	const std::string prefix = "mods/" + Lower(folder) + "/";
	bool mounted = false;
	for (const std::string &name : ModsMountedProfile().mods) mounted = mounted || Lower(name) == Lower(folder);
	std::vector<std::string> all;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		all = g_errors;
	}
	for (const std::string &line : all) {
		std::string path, rest;
		if (!ParseMessage(line, path, rest)) continue;
		std::string norm = Lower(path);
		std::replace(norm.begin(), norm.end(), '\\', '/');
		while (!norm.empty() && norm[0] == '/') norm.erase(0, 1);
		const bool inMod = norm.compare(0, prefix.size(), prefix) == 0;
		if (!inMod && !(mounted && paths.count(norm))) continue;
		const std::string shown = (inMod ? path.substr(prefix.size()) : path) + " " + rest;
		out.push_back(shown.size() > 220 ? shown.substr(0, 217) + "..." : shown);
	}
	if (out.size() > 6) out.erase(out.begin(), out.end() - 6);
	return out;
}

namespace {

std::vector<std::string> ScriptProblemsFor(const std::string &folder)
{
	std::vector<std::string> out;
	for (const std::string &e : ScriptErrorsFor(folder)) out.push_back(Tr("Script ") + e);
	return out;
}

const bool g_featureRegistered = (ModsRegisterFeature({10, nullptr, &ScriptProblemsFor}), true);

}
