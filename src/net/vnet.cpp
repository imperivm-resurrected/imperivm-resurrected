// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#include <windows.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "net/vnet.h"

namespace {

typedef hostent *(WSAAPI *Gethostbyname_t)(const char *);
typedef int(WSAAPI *Sendto_t)(SOCKET, const char *, int, int, const sockaddr *, int);
typedef int(WSAAPI *Bind_t)(SOCKET, const sockaddr *, int);
typedef int(WSAAPI *WSASendTo_t)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr *, int, LPWSAOVERLAPPED,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE);

Gethostbyname_t g_origGethostbyname = nullptr;
Sendto_t g_origSendto = nullptr;
WSASendTo_t g_origWSASendTo = nullptr;
Bind_t g_origBind = nullptr;

std::mutex g_lock;
int g_myK = 0;
std::map<uint64_t, int> g_peers;
std::atomic<unsigned> g_peersVersion{0};
VnetSendFn g_send = nullptr;
}
void VnetCountOut(uint64_t peer, int bytes);
void VnetCountIn(uint64_t peer, int bytes);
namespace {

struct Endpoint {
	uint64_t peer;
	int k;
	uint16_t port;
	SOCKET s;
};
std::vector<Endpoint> g_endpoints;
bool g_wsaReady = false;

const uint16_t kGamePorts[] = {40444, 40445, 40447};

// Windows lets the game's wildcard socket and our per-peer sockets share a game port; Wine (Linux) only does if both set SO_REUSEADDR.
bool RunningUnderWine()
{
	return GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != nullptr;
}

void AllowSharedPort(SOCKET s)
{
	BOOL on = TRUE;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&on), sizeof(on));
}

uint32_t VirtualIp(int k)
{
	return (127u << 24) | (77u << 16) | static_cast<uint32_t>(k & 0xffff);
}

SOCKET OpenEndpoint(int k, uint16_t port)
{
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) return s;
	if (RunningUnderWine()) AllowSharedPort(s);
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = htonl(VirtualIp(k));
	if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
		static DWORD lastReport = 0;
		if (GetTickCount() - lastReport > 10000) {
			lastReport = GetTickCount();
			LogW("net", "virtual network: could not open 127.77.%d.%d:%u (error %d)", (k >> 8) & 255, k & 255, port, WSAGetLastError());
		}
		closesocket(s);
		return INVALID_SOCKET;
	}
	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);
	BOOL off = FALSE;
	DWORD ret = 0;
	WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
	return s;
}

Endpoint *FindEndpoint(uint64_t peer, uint16_t port)
{
	for (Endpoint &e : g_endpoints) {
		if (e.peer == peer && e.port == port) return &e;
	}
	return nullptr;
}

Endpoint *GetEndpoint(uint64_t peer, int k, uint16_t port)
{
	if (Endpoint *e = FindEndpoint(peer, port)) return e;
	SOCKET s = OpenEndpoint(k, port);
	if (s == INVALID_SOCKET) return nullptr;
	g_endpoints.push_back({peer, k, port, s});
	return &g_endpoints.back();
}

void SyncEndpoints()
{
	// It runs on every pump: the peers are copied only when they changed, or while a socket that could not be bound is still missing.
	static unsigned synced = 0;
	static bool complete = false;
	if (complete && synced == g_peersVersion) return;
	std::map<uint64_t, int> peers;
	int myK;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		synced = g_peersVersion;
		peers = g_peers;
		myK = g_myK;
	}
	for (size_t i = 0; i < g_endpoints.size();) {
		auto it = peers.find(g_endpoints[i].peer);
		if (myK == 0 || it == peers.end() || it->second != g_endpoints[i].k) {
			closesocket(g_endpoints[i].s);
			g_endpoints.erase(g_endpoints.begin() + i);
		}
		else {
			i++;
		}
	}
	complete = true;
	if (myK == 0) return;
	for (auto &p : peers) {
		for (uint16_t port : kGamePorts) {
			if (!GetEndpoint(p.first, p.second, port)) complete = false;
		}
	}
}

bool IsLocalHostName(const char *name)
{
	if (!name || !*name) return true;
	char host[256] = {};
	if (gethostname(host, sizeof(host)) != 0) return false;
	return _stricmp(host, name) == 0;
}

hostent *WSAAPI HookGethostbyname(const char *name)
{
	hostent *r = g_origGethostbyname(name);
	int myK;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		myK = g_myK;
	}
	if (!myK || !r || r->h_addrtype != AF_INET || !IsLocalHostName(name)) return r;
	thread_local in_addr addrs[16];
	thread_local char *list[17];
	thread_local hostent h;
	int n = 0;
	addrs[n].s_addr = htonl(VirtualIp(myK));
	list[n] = reinterpret_cast<char *>(&addrs[n]);
	n++;
	for (int i = 0; r->h_addr_list[i] && n < 16; i++) {
		memcpy(&addrs[n], r->h_addr_list[i], sizeof(in_addr));
		list[n] = reinterpret_cast<char *>(&addrs[n]);
		n++;
	}
	list[n] = nullptr;
	h = *r;
	h.h_addr_list = list;
	return &h;
}

bool IsBroadcast(const sockaddr *to, int tolen)
{
	if (!to || tolen < static_cast<int>(sizeof(sockaddr_in)) || to->sa_family != AF_INET) return false;
	return reinterpret_cast<const sockaddr_in *>(to)->sin_addr.s_addr == INADDR_BROADCAST;
}

std::vector<int> PeerNumbers()
{
	std::lock_guard<std::mutex> lock(g_lock);
	std::vector<int> out;
	if (!g_myK) return out;
	for (auto &p : g_peers) out.push_back(p.second);
	return out;
}

int WSAAPI HookSendto(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int tolen)
{
	if (IsBroadcast(to, tolen)) {
		const std::vector<int> peers = PeerNumbers();
		if (!peers.empty()) {
			sockaddr_in a = *reinterpret_cast<const sockaddr_in *>(to);
			for (int k : peers) {
				a.sin_addr.s_addr = htonl(VirtualIp(k));
				g_origSendto(s, buf, len, flags, reinterpret_cast<sockaddr *>(&a), sizeof(a));
			}
			return len;
		}
	}
	return g_origSendto(s, buf, len, flags, to, tolen);
}

int WSAAPI HookWSASendTo(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags, const sockaddr *to, int tolen,
	LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE done)
{
	if (IsBroadcast(to, tolen)) {
		const std::vector<int> peers = PeerNumbers();
		if (!peers.empty()) {
			std::string data;
			for (DWORD i = 0; i < count; i++) data.append(bufs[i].buf, bufs[i].len);
			sockaddr_in a = *reinterpret_cast<const sockaddr_in *>(to);
			for (size_t i = 1; i < peers.size(); i++) {
				a.sin_addr.s_addr = htonl(VirtualIp(peers[i]));
				g_origSendto(s, data.data(), static_cast<int>(data.size()), static_cast<int>(flags),
					reinterpret_cast<sockaddr *>(&a), sizeof(a));
			}
			a.sin_addr.s_addr = htonl(VirtualIp(peers[0]));
			return g_origWSASendTo(s, bufs, count, sent, flags, reinterpret_cast<sockaddr *>(&a), sizeof(a), ov, done);
		}
	}
	return g_origWSASendTo(s, bufs, count, sent, flags, to, tolen, ov, done);
}

int WSAAPI HookBind(SOCKET s, const sockaddr *a, int len)
{
	if (a && len >= static_cast<int>(sizeof(sockaddr_in)) && a->sa_family == AF_INET) {
		const uint16_t port = ntohs(reinterpret_cast<const sockaddr_in *>(a)->sin_port);
		int type = 0, typeLen = sizeof(type);
		if (getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &typeLen) == 0 && type == SOCK_DGRAM) {
			for (uint16_t gamePort : kGamePorts) {
				if (gamePort == port) AllowSharedPort(s);
			}
		}
	}
	return g_origBind(s, a, len);
}

bool HookExport(HMODULE m, const char *name, void *detour, void **orig)
{
	void *target = reinterpret_cast<void *>(GetProcAddress(m, name));
	return target && HookCreate(target, detour, orig) == MH_OK && HookEnable(target) == MH_OK;
}

}

void VnetInstallHooks(bool knownExe)
{
	if (!knownExe) return;
	HMODULE ws = LoadLibraryA("ws2_32.dll");
	const bool ok = ws &&
		HookExport(ws, "gethostbyname", reinterpret_cast<void *>(&HookGethostbyname), reinterpret_cast<void **>(&g_origGethostbyname)) &&
		HookExport(ws, "sendto", reinterpret_cast<void *>(&HookSendto), reinterpret_cast<void **>(&g_origSendto)) &&
		HookExport(ws, "WSASendTo", reinterpret_cast<void *>(&HookWSASendTo), reinterpret_cast<void **>(&g_origWSASendTo));
	if (ok && RunningUnderWine()) {
		if (!HookExport(ws, "bind", reinterpret_cast<void *>(&HookBind), reinterpret_cast<void **>(&g_origBind))) {
			LogW("net", "virtual network: bind could NOT be hooked; the game may fail to open its game ports under Wine");
		}
	}
	LogAt(ok ? kLogInfo : kLogError, "net", "virtual network: %s", ok ? "ready" : "Network functions could NOT be hooked");
}

void VnetSetSender(VnetSendFn fn)
{
	g_send = fn;
}

void VnetSetPeers(int myK, const std::vector<std::pair<uint64_t, int>> &peers)
{
	std::lock_guard<std::mutex> lock(g_lock);
	std::map<uint64_t, int> next(peers.begin(), peers.end());
	if (myK != g_myK || next != g_peers) {
		LogI("net", "virtual network: %s 127.77.%d.%d, %u peers", myK ? "active as" : "off", (myK >> 8) & 255, myK & 255,
			static_cast<unsigned>(next.size()));
		for (auto &p : next) {
			LogD("net", "virtual network:   %llu -> 127.77.%d.%d", p.first, (p.second >> 8) & 255, p.second & 255);
		}
		g_peersVersion++;
	}
	g_myK = myK;
	g_peers = next;
}

bool VnetHasPeer(uint64_t peer)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_peers.count(peer) != 0;
}

bool VnetActive()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_myK != 0;
}

int VnetMyNumber()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_myK;
}

void VnetPump(int timeoutMs)
{
	if (!g_wsaReady) {
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		g_wsaReady = true;
	}
	SyncEndpoints();
	if (g_endpoints.empty()) {
		Sleep(timeoutMs);
		return;
	}
	fd_set rd;
	FD_ZERO(&rd);
	for (const Endpoint &e : g_endpoints) {
		if (rd.fd_count < FD_SETSIZE) FD_SET(e.s, &rd);
	}
	timeval tv = {0, timeoutMs * 1000};
	if (select(0, &rd, nullptr, nullptr, &tv) <= 0) return;
	char buf[2048];
	for (const Endpoint &e : g_endpoints) {
		if (!FD_ISSET(e.s, &rd)) continue;
		for (;;) {
			sockaddr_in from = {};
			int fromLen = sizeof(from);
			int n = recvfrom(e.s, buf + 4, sizeof(buf) - 4, 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
			if (n <= 0) break;
			const uint16_t sp = ntohs(from.sin_port);
			memcpy(buf, &sp, 2);
			memcpy(buf + 2, &e.port, 2);
			if (g_send) g_send(e.peer, buf, n + 4);
			VnetCountOut(e.peer, n);
		}
	}
}

void VnetDeliver(uint64_t from, const void *data, int len)
{
	if (len < 4) return;
	int k = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_myK) return;
		auto it = g_peers.find(from);
		if (it == g_peers.end()) return;
		k = it->second;
	}
	uint16_t sp, dp;
	memcpy(&sp, data, 2);
	memcpy(&dp, static_cast<const char *>(data) + 2, 2);
	Endpoint *e = GetEndpoint(from, k, sp);
	if (!e) return;
	sockaddr_in to = {};
	to.sin_family = AF_INET;
	to.sin_port = htons(dp);
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sendto(e->s, static_cast<const char *>(data) + 4, len - 4, 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
	VnetCountIn(from, len - 4);
}

namespace {
struct Traffic {
	unsigned packetsOut = 0, packetsIn = 0;
	uint64_t bytesOut = 0, bytesIn = 0;
};
std::map<uint64_t, Traffic> g_traffic;
std::mutex g_inMx;
std::map<uint64_t, DWORD> g_lastIn;
}

void VnetCountOut(uint64_t peer, int bytes)
{
	g_traffic[peer].packetsOut++;
	g_traffic[peer].bytesOut += bytes;
}

void VnetCountIn(uint64_t peer, int bytes)
{
	g_traffic[peer].packetsIn++;
	g_traffic[peer].bytesIn += bytes;
	std::lock_guard<std::mutex> lock(g_inMx);
	g_lastIn[peer] = GetTickCount();
}

int VnetSilenceMs(uint64_t peer)
{
	std::lock_guard<std::mutex> lock(g_inMx);
	const auto it = g_lastIn.find(peer);
	return it == g_lastIn.end() ? -1 : static_cast<int>(GetTickCount() - it->second);
}

void VnetLogStats()
{
	for (auto &t : g_traffic) {
		if (!t.second.packetsOut && !t.second.packetsIn) continue;
		LogD("net", "virtual network: with %llu in the last minute: sent %u packets (%llu KB), received %u (%llu KB)", t.first,
			t.second.packetsOut, t.second.bytesOut / 1024, t.second.packetsIn, t.second.bytesIn / 1024);
		t.second = Traffic();
	}
}
