// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <mutex>
#include <string>
#include <map>
#include <cstdint>

#include "mods/netmods.h"
#include "MinHook.h"
#include "hooks/movestart.h"
#include "hooks/hookcount.h"
#include "core/json.h"
#include "core/guard.h"
#include "mods/mods.h"
#include "i18n/tr.h"
#include "addr/net.h"
#include "addr/ui.h"
#include "core/uiport.h"

namespace {
const unsigned short kModsPort = 40449;
const char kQuery[] = "IMPMODS?";

typedef int(__cdecl *VersionHash_t)(int options);
typedef int(__fastcall *AddFoundGame_t)(void *self, void *edx, unsigned char *desc, void *addr, int flag);
typedef unsigned(__fastcall *GetIP_t)(void *addr);
typedef void *(__fastcall *CastTo_t)(void *self, void *edx, const char *cls);

VersionHash_t g_origHash = nullptr;
AddFoundGame_t g_origAddFoundGame = nullptr;

std::mutex g_lock;
bool g_keyReady = false;
unsigned g_key = 0;
std::string g_profileHash;
bool g_serverStarted = false;
std::map<std::string, DWORD> g_warned;

unsigned ProfileKey()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_keyReady) {
			return g_key;
		}
	}
	const ModProfile mounted = ModsMountedProfile();
	ModProfile original;
	original.name = "Original";
	const std::string base = ModsProfileGameplayHash(original);
	std::string h;
	for (int i = 0; i < 300 && (h = ModsProfileGameplayHash(mounted)).empty(); i++) {
		Sleep(100);
	}
	unsigned key = 0;
	if (!h.empty() && h != base) {
		key = strtoul(h.substr(0, 4).c_str(), nullptr, 16) & 0xffff;
		if (key == 0) key = 1;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_keyReady) {
		g_key = key;
		g_profileHash = h;
		g_keyReady = true;
		LogI("net", "online: profile \"%s\", gameplay fingerprint %s -> version key %04x%s", mounted.name.c_str(),
			h.empty() ? "(not available)" : h.substr(0, 16).c_str(), key, key ? "" : " (compatible without content)");
	}
	return g_key;
}

std::string BuildReply()
{
	const ModProfile p = ModsMountedProfile();
	Json j = Json::MakeObject();
	j.Set("v", Json::MakeNumber(1));
	j.Set("profile", Json::MakeString(p.name));
	j.Set("key", Json::MakeNumber(ProfileKey()));
	j.Set("fingerprint", Json::MakeString(g_profileHash));
	Json mods = Json::MakeArray();
	for (const std::string &folder : p.mods) {
		ModInfo m;
		Json e = Json::MakeObject();
		if (ModsFind(folder, m)) {
			e.Set("id", Json::MakeString(m.id));
			e.Set("name", Json::MakeString(m.name));
			e.Set("version", Json::MakeString(m.version));
			e.Set("gameplay", Json::MakeBool(m.gameplay));
			if (!m.sourceUrl.empty()) e.Set("url", Json::MakeString(m.sourceUrl));
		}
		else {
			e.Set("id", Json::MakeString(folder));
		}
		mods.arr.push_back(e);
		if (j.Dump().size() > 1000) break;
	}
	j.Set("mods", mods);
	return j.Dump();
}

void ServerThread()
{
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(kModsPort);
	a.sin_addr.s_addr = INADDR_ANY;
	if (s == INVALID_SOCKET || bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
		LogW("net", "online: could not open UDP port %u to report the mods (error %d)", kModsPort, WSAGetLastError());
		if (s != INVALID_SOCKET) closesocket(s);
		return;
	}
	LogI("net", "online: reporting the mods over UDP %u", kModsPort);
	char buf[64];
	for (;;) {
		sockaddr_in from = {};
		int fromLen = sizeof(from);
		int n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
		if (n == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAECONNRESET) continue;
			break;
		}
		if (n == static_cast<int>(sizeof(kQuery) - 1) && memcmp(buf, kQuery, n) == 0) {
			const std::string reply = BuildReply();
			sendto(s, reply.data(), static_cast<int>(reply.size()), 0, reinterpret_cast<sockaddr *>(&from), fromLen);
		}
	}
	closesocket(s);
}

void StartServer()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_serverStarted) return;
		g_serverStarted = true;
	}
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	SafeThread(ServerThread).detach();
}

int __cdecl HookVersionHash(int options)
{
	StartServer();
	const unsigned key = ProfileKey();
	// The movement patch marks the low word as Classic Modern does (0xc000), so a game without it refuses the room.
	return g_origHash(options) ^ static_cast<int>(key << 16) ^ (MoveStartActive() ? 0xc000 : 0);
}

typedef void(__fastcall *PublishGameOptions_t)(void *self, void *edx, int *options);
PublishGameOptions_t g_origPublish = nullptr;

typedef void(__fastcall *BroadcastGameDesc_t)(void *shell, void *edx, int to);
BroadcastGameDesc_t g_origBroadcast = nullptr;
thread_local bool t_publishing = false;

void __fastcall HookBroadcastGameDesc(void *shell, void *edx, int to)
{
	if (t_publishing) return;
	g_origBroadcast(shell, edx, to);
}

void __fastcall HookPublishGameOptions(void *self, void *edx, int *options)
{
	const unsigned key = ProfileKey();
	t_publishing = key != 0 && options != nullptr;
	g_origPublish(self, edx, options);
	t_publishing = false;
	if (!key || !options) return;
	options[0] = g_origHash(reinterpret_cast<int>(options)) ^ static_cast<int>(key << 16);
	char *dlg = static_cast<char *>(self);
	*reinterpret_cast<int *>(dlg + 0x84) = options[0];
	char *shell = *reinterpret_cast<char **>(dlg + 0x8228);
	if (shell) {
		char *desc = *reinterpret_cast<char **>(shell + 0x10);
		if (desc) *reinterpret_cast<int *>(desc + 0x4a) = options[0];
		g_origBroadcast(shell, nullptr, 0xffff);
	}
}

std::string IpText(unsigned ip)
{
	char buf[32];
	sprintf_s(buf, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
	return buf;
}

void ExplainMismatch(unsigned ip, unsigned hostKey, std::string gameName)
{
	std::string text;
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s != INVALID_SOCKET) {
		DWORD timeout = 1500;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
		sockaddr_in to = {};
		to.sin_family = AF_INET;
		to.sin_port = htons(kModsPort);
		to.sin_addr.s_addr = htonl(ip);
		char buf[1500];
		for (int attempt = 0; attempt < 2 && text.empty(); attempt++) {
			sendto(s, kQuery, sizeof(kQuery) - 1, 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
			int n = recv(s, buf, sizeof(buf) - 1, 0);
			if (n <= 0) continue;
			Json j;
			if (!Json::Parse(std::string(buf, n), j)) continue;
			std::string list;
			if (const Json *mods = j.Get("mods")) {
				for (const Json &m : mods->arr) {
					if (!m.GetBool("gameplay", true)) continue;
					std::string name = m.GetString("name", m.GetString("id"));
					std::string ver = m.GetString("version");
					list += (list.empty() ? "" : ", ") + name + (ver.empty() ? "" : " " + ver);
				}
			}
			text = Tr("The game \"") + gameName + Tr("\" uses the profile \"") + j.GetString("profile") + "\"" +
				(list.empty() ? "" : " (" + list + ")") + ". You use \"" + ModsMountedProfile().name +
				Tr("\": switch profile in Options > Content to join.");
		}
		closesocket(s);
	}
	if (text.empty()) {
		text = hostKey == 0 ? Tr("The game \"") + gameName + Tr("\" is played without additional content and you use the profile \"") +
								  ModsMountedProfile().name + Tr("\": choose the Original profile to join.")
							: Tr("The game \"") + gameName + Tr("\" uses other content that changes gameplay. Choose the same profile as the host to join.");
	}
	LogI("net", "online: %s (%s)", text.c_str(), IpText(ip).c_str());
	UiToast(text);
}

unsigned AddrIp(void *addr)
{
	__try {
		char *a = static_cast<char *>(addr);
		const int off = *reinterpret_cast<int *>(*reinterpret_cast<int *>(a + 4) + 4);
		void *iface = a + 4 + off;
		auto cast = reinterpret_cast<CastTo_t>((*reinterpret_cast<void ***>(iface))[1]);
		void *tcp = cast(iface, nullptr, *reinterpret_cast<const char **>(addr::kClassNameCTCPAddr));
		return tcp ? reinterpret_cast<GetIP_t>(addr::kCTCPAddrGetIP)(tcp) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

int __fastcall HookAddFoundGame(void *self, void *edx, unsigned char *desc, void *addr, int flag)
{
	if (desc && addr) {
		const int options = reinterpret_cast<int>(desc + 0x4a);
		const unsigned stored = *reinterpret_cast<unsigned *>(desc + 0x4a);
		const unsigned plain = static_cast<unsigned>(g_origHash(options));
		const unsigned diff = stored ^ plain;
		if ((diff & 0xffff) == 0 && (diff >> 16) != ProfileKey()) {
			const unsigned hostKey = diff >> 16;
			const unsigned ip = AddrIp(addr);
			char key[48];
			sprintf_s(key, "%08x/%04x", ip, hostKey);
			bool warn = false;
			{
				std::lock_guard<std::mutex> lock(g_lock);
				DWORD &last = g_warned[key];
				if (last == 0 || GetTickCount() - last > 60000) {
					last = GetTickCount();
					warn = true;
				}
			}
			if (warn && ip) {
				std::string name(reinterpret_cast<const char *>(desc), strnlen(reinterpret_cast<const char *>(desc), 0x40));
				SafeThread(ExplainMismatch, ip, hostKey, name).detach();
			}
		}
	}
	return g_origAddFoundGame(self, edx, desc, addr, flag);
}

bool HookAt(uintptr_t addr, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(addr), detour, orig) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr)) == MH_OK) {
		return true;
	}
	LogE("net", "online: could not hook %s", what);
	return false;
}

}

void InstallNetModsHooks(bool knownExe)
{
	if (!knownExe) {
		return;
	}
	const bool ok = HookAt(addr::kNetComputeGameVersionHash, reinterpret_cast<void *>(&HookVersionHash),
						reinterpret_cast<void **>(&g_origHash), "NetComputeGameVersionHash") &&
		HookAt(addr::kAddFoundGame, reinterpret_cast<void *>(&HookAddFoundGame), reinterpret_cast<void **>(&g_origAddFoundGame),
			"CNGameLocator::AddFoundGame") &&
		HookAt(addr::kBroadcastGameDesc, reinterpret_cast<void *>(&HookBroadcastGameDesc), reinterpret_cast<void **>(&g_origBroadcast),
			"CNGameShell::BroadcastGameDesc") &&
		HookAt(addr::kPublishGameOptions, reinterpret_cast<void *>(&HookPublishGameOptions), reinterpret_cast<void **>(&g_origPublish),
			"CVXUIMultiplayerGame::PublishGameOptions");
	LogAt(ok ? kLogInfo : kLogError, "net", "online: mod check in the rooms %s", ok ? "active" : "NOT active");
}
