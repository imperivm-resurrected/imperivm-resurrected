// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <cstdint>


#include "net/discord.h"
#include "core/json.h"
#include "core/guard.h"
#include "net/leaguepresence.h"
#include "net/steamnet.h"
#include "i18n/tr.h"
#include "addr/state.h"
#include "core/uiport.h"

const char kDiscordInviteUrl[] = "https://discord.gg/6kCatX3Zbc";

void OpenDiscordInvite()
{
	LogI("discord", "opening the invitation");
	SafeThread([] {
		const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		SHELLEXECUTEINFOA sei = {};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		sei.lpVerb = "open";
		sei.lpFile = kDiscordInviteUrl;
		sei.nShow = SW_SHOWNORMAL;
		const BOOL ok = ShellExecuteExA(&sei);
		LogAt(ok ? kLogInfo : kLogWarn, "discord", "invitation %s (%lu)", ok ? "open" : "could NOT open", ok ? 0ul : GetLastError());
		if (SUCCEEDED(co)) CoUninitialize();
	}).detach();
}

namespace {
const char kDefaultClientId[] = "";

HANDLE g_pipe = INVALID_HANDLE_VALUE;
unsigned g_nonce = 0;

std::string IniPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *(slash + 1) = 0;
	return std::string(path) + "imperivm-mod.ini";
}

bool WriteFrame(uint32_t op, const std::string &json)
{
	if (g_pipe == INVALID_HANDLE_VALUE) return false;
	std::string buf(8 + json.size(), '\0');
	const uint32_t len = static_cast<uint32_t>(json.size());
	memcpy(&buf[0], &op, 4);
	memcpy(&buf[4], &len, 4);
	memcpy(&buf[8], json.data(), json.size());
	DWORD done = 0;
	return WriteFile(g_pipe, buf.data(), static_cast<DWORD>(buf.size()), &done, nullptr) && done == buf.size();
}

bool ReadFrame(uint32_t &op, std::string &json, bool &connectionLost)
{
	DWORD avail = 0;
	if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
		connectionLost = true;
		return false;
	}
	if (avail < 8) return false;
	uint32_t head[2];
	DWORD got = 0;
	if (!ReadFile(g_pipe, head, 8, &got, nullptr) || got != 8) {
		connectionLost = true;
		return false;
	}
	op = head[0];
	json.assign(head[1] < (1u << 20) ? head[1] : 0, '\0');
	DWORD total = 0;
	while (total < json.size()) {
		DWORD n = 0;
		if (!ReadFile(g_pipe, &json[total], static_cast<DWORD>(json.size()) - total, &n, nullptr) || n == 0) {
			connectionLost = true;
			return false;
		}
		total += n;
	}
	return true;
}

void ClosePipe()
{
	if (g_pipe != INVALID_HANDLE_VALUE) CloseHandle(g_pipe);
	g_pipe = INVALID_HANDLE_VALUE;
}

bool OpenPipe()
{
	for (int i = 0; i < 10; i++) {
		const std::wstring name = L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(i);
		g_pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (g_pipe != INVALID_HANDLE_VALUE) return true;
	}
	return false;
}

std::string Nonce() { return std::to_string(++g_nonce); }

bool SendCommand(const std::string &cmd, const Json &args, const char *evt = nullptr)
{
	Json j = Json::MakeObject();
	j.Set("cmd", Json::MakeString(cmd));
	if (evt) j.Set("evt", Json::MakeString(evt));
	if (args.type == Json::Object) j.Set("args", args);
	j.Set("nonce", Json::MakeString(Nonce()));
	return WriteFrame(1, j.Dump());
}

bool Handshake(const std::string &clientId)
{
	Json hello = Json::MakeObject();
	hello.Set("v", Json::MakeNumber(1));
	hello.Set("client_id", Json::MakeString(clientId));
	if (!WriteFrame(0, hello.Dump())) return false;
	const DWORD start = GetTickCount();
	while (GetTickCount() - start < 5000) {
		uint32_t op;
		std::string text;
		bool lost = false;
		if (ReadFrame(op, text, lost)) {
			Json j;
			if (Json::Parse(text, j) && j.GetString("evt") == "READY") return true;
			if (Json::Parse(text, j) && j.GetString("evt") == "ERROR") {
				const Json *d = j.Get("data");
				LogW("discord", "refused the connection (%s)", d ? d->GetString("message").c_str() : "?");
				return false;
			}
		}
		else if (lost) {
			return false;
		}
		Sleep(50);
	}
	return false;
}

struct Presence {
	std::string details, state;
	std::string category;
	uint64_t lobby = 0;
	int cur = 0, max = 0;
};

Presence Compose()
{
	Presence p;
	const OnlineRoom room = SteamCurrentRoom();
	const bool inGame = *reinterpret_cast<void **>(addr::kGame) != nullptr;
	// A ranked search or game: the queue and the rank on the first line, the game or the menus under it.
	const std::string ranked = LeagueRankedPresence();
	if (!ranked.empty()) {
		const bool playing = room.id && room.info.playing;
		p.category = playing ? "ranked-" + std::to_string(room.id) : "ranked-search";
		p.details = ranked;
		p.state = playing ? Tr("Online game") + (room.info.map.empty() ? std::string() : ": " + room.info.map) : Tr("In the menus");
		return p;
	}
	if (room.id && room.info.playing) {
		p.category = "online-" + std::to_string(room.id);
		p.details = Tr("Online game") + (room.info.map.empty() ? std::string() : ": " + room.info.map);
		p.state = std::to_string(static_cast<int>(room.members.size())) + Tr(" players");
		p.cur = static_cast<int>(room.members.size());
		p.max = room.info.maxPlayers;
	}
	else if (room.id) {
		p.category = "room-" + std::to_string(room.id);
		p.details = Tr("In the room: ") + (room.info.name.empty() ? std::string("unnamed") : room.info.name);
		p.state = room.info.password ? Tr("Password protected") : Tr("Waiting for players");
		p.cur = static_cast<int>(room.members.size());
		p.max = room.info.maxPlayers;
		if (room.info.visibility == 0 && p.cur < p.max) p.lobby = room.id;
	}
	else if (inGame) {
		const bool editor = *reinterpret_cast<int *>(addr::kEditorMode) != 0;
		p.category = editor ? "editor" : "solo";
		p.details = editor ? Tr("In the editor") : Tr("Playing a game");
		p.state = editor ? Tr("Creating maps") : Tr("Solo");
	}
	else if (UiModManagerOpen()) {
		p.category = "mods";
		p.details = Tr("Managing content");
		p.state = Tr("In the menus");
	}
	else {
		p.category = "menu";
		p.details = Tr("In the main menu");
		p.state = "Imperivm Resurrected";
	}
	return p;
}

Json BuildActivity(const Presence &p, time_t start)
{
	Json a = Json::MakeObject();
	a.Set("details", Json::MakeString(p.details));
	a.Set("state", Json::MakeString(p.state));
	Json ts = Json::MakeObject();
	ts.Set("start", Json::MakeNumber(static_cast<double>(start)));
	a.Set("timestamps", ts);
	Json assets = Json::MakeObject();
	assets.Set("large_image", Json::MakeString("imperivm"));
	assets.Set("large_text", Json::MakeString("Imperivm Resurrected"));
	a.Set("assets", assets);
	if (p.lobby) {
		Json party = Json::MakeObject();
		party.Set("id", Json::MakeString(std::to_string(p.lobby)));
		Json size = Json::MakeArray();
		size.arr.push_back(Json::MakeNumber(p.cur));
		size.arr.push_back(Json::MakeNumber(p.max));
		party.Set("size", size);
		a.Set("party", party);
		Json secrets = Json::MakeObject();
		secrets.Set("join", Json::MakeString(std::to_string(p.lobby)));
		a.Set("secrets", secrets);
		a.Set("instance", Json::MakeBool(true));
	}
	else {
		if (p.max > 0) {
			Json party = Json::MakeObject();
			party.Set("id", Json::MakeString(p.category));
			Json size = Json::MakeArray();
			size.arr.push_back(Json::MakeNumber(p.cur));
			size.arr.push_back(Json::MakeNumber(p.max));
			party.Set("size", size);
			a.Set("party", party);
		}
		Json buttons = Json::MakeArray();
		Json b = Json::MakeObject();
		b.Set("label", Json::MakeString(Tr("Join our Discord")));
		b.Set("url", Json::MakeString(kDiscordInviteUrl));
		buttons.arr.push_back(b);
		a.Set("buttons", buttons);
	}
	return a;
}

void HandleEvent(const Json &j)
{
	const std::string evt = j.GetString("evt");
	const Json *data = j.Get("data");
	if (!data) return;
	if (evt == "ACTIVITY_JOIN") {
		const uint64_t lobby = _strtoui64(data->GetString("secret").c_str(), nullptr, 10);
		LogI("discord", "someone invited you to room %llu", static_cast<unsigned long long>(lobby));
		if (lobby) UiJoinFromDiscord(lobby);
	}
	else if (evt == "ACTIVITY_JOIN_REQUEST") {
		const Json *user = data->Get("user");
		const std::string id = user ? user->GetString("id") : "";
		const std::string name = user ? user->GetString("username") : Tr("someone");
		const Presence p = Compose();
		Json args = Json::MakeObject();
		args.Set("user_id", Json::MakeString(id));
		if (p.lobby && !id.empty()) {
			SendCommand("SEND_ACTIVITY_JOIN_INVITE", args);
			UiToast(name + Tr(" wants to join your room (Discord)"));
		}
		else {
			SendCommand("CLOSE_ACTIVITY_JOIN_REQUEST", args);
		}
	}
}

void Run()
{
	std::string clientId = kDefaultClientId;
	char buf[64] = {};
	GetPrivateProfileStringA("Discord", "ClientId", "", buf, sizeof(buf), IniPath().c_str());
	if (buf[0]) clientId = buf;
	if (clientId.empty()) {
		LogI("discord", "no ClientId in imperivm-mod.ini ([Discord]): presence disabled");
		return;
	}
	bool reportedMissing = false;
	const time_t processStart = time(nullptr);
	for (;;) {
		if (!OpenPipe()) {
			if (!reportedMissing) LogI("discord", "not open (checked again every 20 s)");
			reportedMissing = true;
			Sleep(20000);
			continue;
		}
		reportedMissing = false;
		if (!Handshake(clientId)) {
			ClosePipe();
			Sleep(20000);
			continue;
		}
		LogI("discord", "connected");
		SendCommand("SUBSCRIBE", Json(), "ACTIVITY_JOIN");
		SendCommand("SUBSCRIBE", Json(), "ACTIVITY_JOIN_REQUEST");
		std::string lastKey, lastJson;
		time_t start = processStart;
		DWORD lastSend = 0, lastCompose = 0;
		bool lost = false;
		while (!lost) {
			uint32_t op;
			std::string text;
			while (ReadFrame(op, text, lost)) {
				if (op == 3) WriteFrame(4, text);
				else if (op == 2) lost = true;
				else {
					Json j;
					if (Json::Parse(text, j) && j.GetString("cmd") == "DISPATCH") HandleEvent(j);
				}
			}
			const DWORD now = GetTickCount();
			if (!lost && now - lastCompose > 2000) {
				lastCompose = now;
				const Presence p = Compose();
				if (p.category != lastKey) {
					lastKey = p.category;
					start = p.category == "menu" ? processStart : time(nullptr);
				}
				Json args = Json::MakeObject();
				args.Set("pid", Json::MakeNumber(GetCurrentProcessId()));
				args.Set("activity", BuildActivity(p, start));
				const std::string json = args.Dump();
				if (json != lastJson || now - lastSend > 120000) {
					lastJson = json;
					lastSend = now;
					if (!SendCommand("SET_ACTIVITY", args)) lost = true;
				}
			}
			Sleep(250);
		}
		LogW("discord", "connection lost");
		ClosePipe();
		Sleep(10000);
	}
}

}

void StartDiscordPresence()
{
	if (GetPrivateProfileIntA("Discord", "Presence", 1, IniPath().c_str()) == 0) return;
	SafeThread(Run).detach();
}
