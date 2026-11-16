// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/compat.h"
#include "core/guard.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "ui/modui.h"
#include "core/winpath.h"

namespace {
const wchar_t kLayersKey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
const wchar_t kLayersKeyMachine[] = L"HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

bool Harmful(const std::wstring &token)
{
	static const wchar_t *const kPrefixes[] = {L"WIN95", L"WIN98", L"WINXP", L"WIN2000", L"WINSRV", L"WIN7", L"WIN8", L"VISTA", L"NT4"};
	for (const wchar_t *p : kPrefixes) {
		if (_wcsnicmp(token.c_str(), p, wcslen(p)) == 0) return true;
	}
	static const wchar_t *const kExact[] = {L"256COLOR", L"16BITCOLOR", L"640X480", L"RUNASADMIN"};
	for (const wchar_t *e : kExact) {
		if (_wcsicmp(token.c_str(), e) == 0) return true;
	}
	return false;
}

struct Layers {
	std::wstring valueName;
	std::wstring kept;
	std::vector<std::wstring> removed;
	bool found = false;
};

Layers Read(HKEY hive, const std::wstring &exe)
{
	Layers out;
	HKEY key = nullptr;
	if (RegOpenKeyExW(hive, kLayersKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return out;
	for (DWORD i = 0;; i++) {
		wchar_t name[1024];
		wchar_t data[1024];
		DWORD nameLen = 1024, dataLen = sizeof(data), type = 0;
		if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE *>(data), &dataLen) != ERROR_SUCCESS) break;
		if (type != REG_SZ || _wcsicmp(name, exe.c_str()) != 0) continue;
		data[std::min<size_t>(dataLen / sizeof(wchar_t), 1023)] = 0;
		out.valueName = name;
		std::wstring text = data, token, keptTokens;
		bool keepsSomething = false;
		for (size_t p = 0; p <= text.size(); p++) {
			if (p < text.size() && text[p] != L' ') {
				token += text[p];
				continue;
			}
			if (token.empty()) continue;
			if (Harmful(token)) {
				out.removed.push_back(token);
			}
			else {
				keptTokens += (keptTokens.empty() ? L"" : L" ") + token;
				if (token != L"~" && token != L"$") keepsSomething = true;
			}
			token.clear();
		}
		out.found = !out.removed.empty();
		out.kept = keepsSomething ? keptTokens : std::wstring();
		break;
	}
	RegCloseKey(key);
	return out;
}

std::string Joined(const std::vector<std::wstring> &tokens)
{
	std::string s;
	for (const std::wstring &t : tokens) s += (s.empty() ? "" : ", ") + winpath::Utf8(t);
	return s;
}

bool FixUser(const Layers &l)
{
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kLayersKey, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return false;
	LONG r;
	if (l.kept.empty()) {
		r = RegDeleteValueW(key, l.valueName.c_str());
	}
	else {
		r = RegSetValueExW(key, l.valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(l.kept.c_str()),
			static_cast<DWORD>((l.kept.size() + 1) * sizeof(wchar_t)));
	}
	RegCloseKey(key);
	return r == ERROR_SUCCESS;
}

void FixMachine(Layers l)
{
	std::wstring args = l.kept.empty()
		? L"delete \"" + std::wstring(kLayersKeyMachine) + L"\" /v \"" + l.valueName + L"\" /f /reg:64"
		: L"add \"" + std::wstring(kLayersKeyMachine) + L"\" /v \"" + l.valueName + L"\" /t REG_SZ /d \"" + l.kept + L"\" /f /reg:64";
	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"runas";
	info.lpFile = L"reg.exe";
	info.lpParameters = args.c_str();
	info.nShow = SW_HIDE;
	if (!ShellExecuteExW(&info) || !info.hProcess) {
		LogW("boot", "compatibility: removing the options for all users was cancelled or failed (code %lu)", GetLastError());
		ModUiToast(Tr("The compatibility options were not removed."), true);
		return;
	}
	WaitForSingleObject(info.hProcess, 30000);
	DWORD code = 1;
	GetExitCodeProcess(info.hProcess, &code);
	CloseHandle(info.hProcess);
	LogAt(code == 0 ? kLogInfo : kLogWarn, "boot", "compatibility: options for all users %s (reg.exe exit code %lu)", code == 0 ? "removed" : "NOT removed", code);
	ModUiToast(code == 0 ? Tr("Compatibility options removed: they stop applying the next time you start the game.") : Tr("The compatibility options were not removed."),
		code != 0);
}
}

void CheckCompatibilityLayers()
{
	wchar_t path[MAX_PATH];
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
	const std::wstring exe = path;
	const Layers user = Read(HKEY_CURRENT_USER, exe);
	if (user.found) {
		const bool ok = FixUser(user);
		LogAt(ok ? kLogInfo : kLogWarn, "boot", "compatibility: Windows options of this user for gbr.exe (%s) %s", Joined(user.removed).c_str(),
			ok ? "removed; they stop applying the next time the game starts" : "could NOT be removed");
		if (ok) ModUiToast(Tr("Windows compatibility mode removed from the game: it stops applying the next time you start it."));
	}
	const Layers machine = Read(HKEY_LOCAL_MACHINE, exe);
	if (!machine.found) return;
	LogW("boot", "compatibility: gbr.exe has Windows options for all users (%s); they can stop the mod from loading", Joined(machine.removed).c_str());
	ModUiAsk("Windows compatibility mode",
		TrFmt("The game has Windows compatibility options for all users (%s). They can stop the mod from loading or drawing well. Remove them? Windows will ask for permission.",
			{Joined(machine.removed)}),
		"Remove", [machine] { SafeThread(FixMachine, machine).detach(); });
}
