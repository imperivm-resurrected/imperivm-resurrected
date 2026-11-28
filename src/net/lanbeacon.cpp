// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <map>
#include <mutex>
#include <string>

#include "core/log.h"
#include "net/lanbeacon.h"

namespace {
// A room not heard for this long has closed, started or gone.
const DWORD kForgetMs = 4000;

struct Heard {
	LanRoom room;
	DWORD at = 0;
};

std::mutex g_mx;
SOCKET g_listen = INVALID_SOCKET, g_send = INVALID_SOCKET;
bool g_wsa = false, g_listenFailed = false;
std::map<uint32_t, Heard> g_rooms;

bool Startup()
{
	if (g_wsa) return true;
	WSADATA data;
	g_wsa = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	return g_wsa;
}

void Listen()
{
	if (g_listen != INVALID_SOCKET || g_listenFailed || !Startup()) return;
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		g_listenFailed = true;
		return;
	}
	// Two games on one computer both hear the announcements.
	BOOL on = TRUE;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&on), sizeof(on));
	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(kLanRoomPort);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
		LogW("net", "local network rooms: port %u could not be opened (%d); rooms of the local network won't show", kLanRoomPort, WSAGetLastError());
		closesocket(s);
		g_listenFailed = true;
		return;
	}
	g_listen = s;
	LogI("net", "local network rooms: listening on port %u", kLanRoomPort);
}

void Drain()
{
	if (g_listen == INVALID_SOCKET) return;
	char buf[1500];
	for (int n = 0; n < 64; n++) {
		sockaddr_in from = {};
		int fromLen = sizeof(from);
		const int len = recvfrom(g_listen, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
		if (len <= 0) break;
		LanRoom room;
		if (!LanRoomParse(buf, len, room)) continue;
		const uint32_t ip = from.sin_addr.s_addr;
		const bool fresh = g_rooms.find(ip) == g_rooms.end();
		g_rooms[ip] = {room, GetTickCount()};
		if (fresh) LogI("net", "local network rooms: \"%s\" of %s heard", room.name.c_str(), room.host.c_str());
	}
	const DWORD now = GetTickCount();
	for (auto it = g_rooms.begin(); it != g_rooms.end();) {
		if (now - it->second.at > kForgetMs) it = g_rooms.erase(it);
		else ++it;
	}
}
}

void LanBeaconAnnounce(const LanRoom &room)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!Startup()) return;
	if (g_send == INVALID_SOCKET) {
		g_send = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (g_send == INVALID_SOCKET) return;
		BOOL on = TRUE;
		setsockopt(g_send, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&on), sizeof(on));
		LogI("net", "local network rooms: announcing the room \"%s\"", room.name.c_str());
	}
	const std::string packet = LanRoomPacket(room);
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(kLanRoomPort);
	a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	sendto(g_send, packet.data(), static_cast<int>(packet.size()), 0, reinterpret_cast<sockaddr *>(&a), sizeof(a));
}

std::vector<LanHeard> LanBeaconRooms()
{
	std::lock_guard<std::mutex> lock(g_mx);
	Listen();
	Drain();
	std::vector<LanHeard> out;
	for (const auto &kv : g_rooms) out.push_back({kv.second.room, kv.first});
	return out;
}

void LanBeaconStop()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_listen != INVALID_SOCKET) {
		closesocket(g_listen);
		g_listen = INVALID_SOCKET;
		LogD("net", "local network rooms: no longer listening");
	}
	g_listenFailed = false;
	g_rooms.clear();
}
