// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "net/leagueclient.h"
#include "core/config.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/league.h"
#include "core/log.h"
#include "core/version.h"
#include "core/winpath.h"
#include "net/leagueclient_internal.h"
#include "net/patrons.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kResolveMs = 5000, kConnectMs = 5000, kSendMs = 10000, kReceiveMs = 15000;
const size_t kMaxBody = 512 * 1024;
const DWORD kPollEveryMs = 3000, kPollRetryMs = 6000, kMeKeepMs = 60 * 1000;

struct Account {
	bool loaded = false;
	std::string token, steamId, name;
};

// What the screens see of the link: the phases in between, the code and the last failure.
struct Link {
	LeagueLinkPhase phase = LeagueLinkPhase::Unlinked;
	std::string code, url, pollToken, error;
	int64_t expiresAt = 0;
	bool polling = false;
	DWORD nextPoll = 0;
	unsigned serial = 0;
};

struct Me {
	bool loaded = false, loading = false;
	DWORD at = 0;
	LeagueMe me;
};

std::mutex g_mx;
Account g_account;
Link g_link;
Me g_me;

// Under g_mx.
void LoadAccount()
{
	if (g_account.loaded) return;
	g_account.loaded = true;
	const Config &c = GetConfig();
	g_account.token = c.leagueToken;
	g_account.steamId = c.leagueSteamId;
	g_account.name = c.leagueName;
}

// Under g_mx.
void StoreAccount(const std::string &token, const std::string &steamId, const std::string &name)
{
	g_account.loaded = true;
	g_account.token = token;
	g_account.steamId = steamId;
	g_account.name = name;
	Config &c = MutableConfig();
	c.leagueToken = token;
	c.leagueSteamId = steamId;
	c.leagueName = name;
	SaveLeagueAccount();
}

// Under g_mx.
bool AccountValid()
{
	LoadAccount();
	const uint64_t me = SteamOwnId();
	return me && !g_account.token.empty() && g_account.steamId == std::to_string(me);
}

// It goes into a header: printable ASCII only.
bool UsableToken(const std::string &token)
{
	if (token.empty() || token.size() > 1024) return false;
	for (unsigned char c : token) {
		if (c <= ' ' || c > '~') return false;
	}
	return true;
}

std::wstring UserAgent()
{
	return winpath::Widen(std::string("ImperivmResurrected/") + ModVersionText() + " (league)", CP_UTF8);
}

void Forget(const char *why)
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_account.token.empty()) return;
		StoreAccount("", "", "");
		g_me = Me();
		g_link.error = "The league no longer recognises this computer. Link your account again.";
	}
	LogW("league", "account forgotten: %s", why);
}

void Open(const std::string &url)
{
	ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void StartLink(unsigned serial)
{
	Json body = Json::MakeObject();
	body.Set("steamId", Json::MakeString(std::to_string(SteamOwnId())));
	body.Set("name", Json::MakeString(SteamOnlinePlayerName()));
	const HttpReply r = Http("POST", "/v1/link/start", body.Dump(), false);
	Json j;
	LeagueLinkCode start;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParseLinkStart(j, start);
	if (ok) LogI("league", "link: code issued, valid until %lld", static_cast<long long>(start.expiresAt));
	else LogW("league", "link: could not get a code (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_link.serial != serial || g_link.phase != LeagueLinkPhase::Starting) return;
	if (!ok) {
		g_link.phase = LeagueLinkPhase::Unlinked;
		if (r.status == 429) g_link.error = "Too many link codes were asked for this account. Try again in a few minutes.";
		else if (r.status) g_link.error = "The league server refused the request. Try again later.";
		else g_link.error = "Couldn't reach the league server. Check your connection and try again.";
		return;
	}
	g_link.phase = LeagueLinkPhase::Waiting;
	g_link.code = start.code;
	g_link.url = start.url;
	g_link.pollToken = start.pollToken;
	g_link.expiresAt = start.expiresAt;
	g_link.nextPoll = GetTickCount() + kPollEveryMs;
}

void PollLink(unsigned serial, std::string pollToken)
{
	const HttpReply r = Http("GET", "/v1/link/poll?token=" + UrlEncode(pollToken), "", false);
	Json j;
	const bool parsed = !r.body.empty() && Json::Parse(r.body, j);
	const LeagueLinkPoll poll = r.status == 410 || (r.Ok() && parsed) ? LeagueParseLinkPoll(r.status, j) : LeagueLinkPoll();
	const std::string own = std::to_string(SteamOwnId());
	bool linked = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_link.polling = false;
		if (g_link.serial != serial || g_link.phase != LeagueLinkPhase::Waiting) return;
		switch (poll.status) {
		case LeaguePollStatus::Pending: g_link.nextPoll = GetTickCount() + kPollEveryMs; return;
		case LeaguePollStatus::Expired:
			g_link = Link();
			g_link.serial = serial + 1;
			g_link.error = "The code expired before it was confirmed. Ask for a new one.";
			break;
		case LeaguePollStatus::Linked:
			if (!UsableToken(poll.gameToken)) {
				g_link = Link();
				g_link.serial = serial + 1;
				g_link.error = "The league server refused the request. Try again later.";
				break;
			}
			if (!poll.steamId.empty() && poll.steamId != own) {
				g_link = Link();
				g_link.serial = serial + 1;
				g_link.error = "That code was confirmed with another Steam account. Sign in on the website with this one.";
				break;
			}
			StoreAccount(poll.gameToken, own, poll.name.empty() ? SteamOnlinePlayerName() : poll.name);
			g_link = Link();
			g_link.serial = serial + 1;
			g_me = Me();
			linked = true;
			break;
		default: g_link.nextPoll = GetTickCount() + kPollRetryMs; break;
		}
	}
	if (linked) {
		LogI("league", "link: account linked");
		LeagueMeRefresh(true);
		PatronsForget(SteamOwnId());
	}
	else if (poll.status == LeaguePollStatus::Unknown) LogD("league", "link: no answer to the poll (%s); trying again", r.Describe().c_str());
	else if (poll.status != LeaguePollStatus::Pending) LogI("league", "link: ended without linking (%s)", poll.status == LeaguePollStatus::Expired ? "expired" : "another Steam account");
}

void Unlink(unsigned serial)
{
	const HttpReply r = Http("DELETE", "/v1/link/token", "", true);
	if (r.Ok() || r.status == 401) LogI("league", "unlinked: the server revoked the token");
	else LogW("league", "unlinked here, but the server did not revoke the token (%s)", r.Describe().c_str());
	{
		std::lock_guard<std::mutex> lock(g_mx);
		StoreAccount("", "", "");
		g_me = Me();
		if (g_link.serial == serial) g_link = Link();
		g_link.serial = serial + 1;
	}
	PatronsForget(SteamOwnId());
}

void FetchMe()
{
	const HttpReply r = Http("GET", "/v1/league/me", "", true);
	Json j;
	LeagueMe me;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParseMe(j, me);
	if (!ok && r.status != 401) LogW("league", "could not read the player's standing (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_me.loading = false;
	g_me.at = GetTickCount();
	if (!ok) return;
	g_me.loaded = true;
	g_me.me = me;
}

}

namespace leagueclient_impl {

std::string HttpReply::Describe() const
{
	if (!failure.empty()) return failure;
	std::string out = std::to_string(status);
	Json j;
	if (!body.empty() && Json::Parse(body, j)) {
		const std::string code = j.GetString("error"), message = j.GetString("message");
		if (!code.empty()) out += " " + code;
		if (!message.empty()) out += ": " + message;
	}
	return out;
}

int64_t NowMs()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	const unsigned long long t = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int64_t>((t - winpath::kUnixEpochAsFileTime) / 10000ULL);
}

std::string UrlEncode(const std::string &s)
{
	static const char kHex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
		else {
			out += '%';
			out += kHex[c >> 4];
			out += kHex[c & 15];
		}
	}
	return out;
}

bool AccountFor(std::string &token, std::string &steamId)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!AccountValid()) return false;
	token = g_account.token;
	steamId = g_account.steamId;
	return true;
}

std::string AccountSteamId()
{
	std::lock_guard<std::mutex> lock(g_mx);
	LoadAccount();
	return g_account.steamId;
}

HttpReply Http(const char *method, const std::string &path, const std::string &body, bool auth)
{
	HttpReply out;
	std::string token, steamId;
	if (auth && !AccountFor(token, steamId)) {
		out.failure = "not linked";
		return out;
	}
	const std::wstring url = winpath::Widen(LeagueApiBase() + path, CP_UTF8);
	URL_COMPONENTS uc = {sizeof(uc)};
	wchar_t host[256] = {};
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.dwUrlPathLength = static_cast<DWORD>(-1);
	uc.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (url.empty() || !WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
		out.failure = "invalid address";
		return out;
	}
	const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
	const std::wstring object = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) + (uc.lpszExtraInfo ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");
	const std::wstring agent = UserAgent();
	HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s) s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (s) WinHttpSetTimeouts(s, kResolveMs, kConnectMs, kSendMs, kReceiveMs);
	HINTERNET c = s ? WinHttpConnect(s, std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(), uc.nPort, 0) : nullptr;
	HINTERNET r = c ? WinHttpOpenRequest(c, winpath::Widen(method, CP_UTF8).c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						  secure ? WINHTTP_FLAG_SECURE : 0)
					: nullptr;
	auto close = [&] {
		if (r) WinHttpCloseHandle(r);
		if (c) WinHttpCloseHandle(c);
		if (s) WinHttpCloseHandle(s);
	};
	if (!r) {
		out.failure = "could not connect";
		close();
		return out;
	}
	std::wstring headers = L"Accept: application/json\r\n";
	if (!body.empty()) headers += L"Content-Type: application/json; charset=utf-8\r\n";
	if (auth) headers += L"Authorization: Bearer " + winpath::Widen(token, CP_UTF8) + L"\r\n";
	const DWORD size = static_cast<DWORD>(body.size());
	if (!WinHttpSendRequest(r, headers.c_str(), static_cast<DWORD>(-1), size ? const_cast<char *>(body.data()) : WINHTTP_NO_REQUEST_DATA, size, size, 0) ||
		!WinHttpReceiveResponse(r, nullptr)) {
		out.failure = "network error " + std::to_string(GetLastError());
		close();
		return out;
	}
	DWORD code = 0, codeSize = sizeof(code);
	WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
	out.status = static_cast<int>(code);
	char buf[8192];
	for (;;) {
		DWORD n = 0;
		if (!WinHttpReadData(r, buf, sizeof(buf), &n)) {
			out.failure = "connection interrupted";
			break;
		}
		if (!n) break;
		if (out.body.size() + n > kMaxBody) {
			out.failure = "answer too large";
			break;
		}
		out.body.append(buf, n);
	}
	close();
	if (auth && out.status == 401) Forget("the server answered 401 to the token");
	return out;
}

}

std::string LeagueApiBase()
{
	static std::once_flag logged;
	const std::string base = LeagueBaseUrl(GetConfig().leagueUrl);
	std::call_once(logged, [&base] {
		std::string wanted = GetConfig().leagueUrl;
		while (!wanted.empty() && (wanted.back() == '/' || wanted.back() == ' ')) wanted.pop_back();
		if (!wanted.empty() && LeagueUrlIsDefault(base) && !LeagueUrlIsDefault(wanted)) {
			LogW("league", "the configured server \"%s\" is not allowed (https, or http only to this computer); using the public one", wanted.c_str());
		}
		else if (!LeagueUrlIsDefault(base)) LogI("league", "server: %s", base.c_str());
	});
	return base;
}

std::string LeagueGetJson(const std::string &path, Json &out)
{
	const HttpReply r = Http("GET", path, "", false);
	if (!r.Ok()) return r.Describe();
	std::string error;
	if (!Json::Parse(r.body, out, &error)) return "invalid answer (" + error + ")";
	return std::string();
}

LeagueLinkView LeagueLinkState()
{
	LeagueLinkView v;
	v.steamReady = SteamOwnId() != 0;
	std::lock_guard<std::mutex> lock(g_mx);
	const bool valid = AccountValid();
	v.phase = g_link.phase != LeagueLinkPhase::Unlinked ? g_link.phase : valid ? LeagueLinkPhase::Linked : LeagueLinkPhase::Unlinked;
	v.code = g_link.code;
	v.url = g_link.url;
	v.expiresAt = g_link.expiresAt;
	v.error = g_link.error;
	v.name = valid ? g_account.name : std::string();
	v.otherAccount = v.steamReady && !valid && !g_account.token.empty();
	return v;
}

bool LeagueLinked()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return AccountValid();
}

void LeagueLinkStart()
{
	if (!SteamOwnId()) {
		std::lock_guard<std::mutex> lock(g_mx);
		g_link.error = "Steam is not available. Open Steam and try again.";
		return;
	}
	unsigned serial = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_link.phase == LeagueLinkPhase::Starting || g_link.phase == LeagueLinkPhase::Waiting || g_link.phase == LeagueLinkPhase::Unlinking) return;
		serial = ++g_link.serial;
		g_link.phase = LeagueLinkPhase::Starting;
		g_link.error.clear();
	}
	LogI("league", "link: asking for a code");
	SafeThread(StartLink, serial).detach();
}

void LeagueLinkCancel()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_link.phase != LeagueLinkPhase::Starting && g_link.phase != LeagueLinkPhase::Waiting) return;
	const unsigned serial = g_link.serial + 1;
	g_link = Link();
	g_link.serial = serial;
	LogI("league", "link: cancelled");
}

void LeagueUnlink()
{
	unsigned serial = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		LoadAccount();
		if (g_account.token.empty() || g_link.phase != LeagueLinkPhase::Unlinked) return;
		serial = ++g_link.serial;
		g_link.phase = LeagueLinkPhase::Unlinking;
		g_link.error.clear();
	}
	SafeThread(Unlink, serial).detach();
}

void LeagueOpenLinkPage()
{
	std::string url;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		url = g_link.url;
	}
	Open(LeagueBrowserUrl(url));
}

LeagueMeView LeagueMeState()
{
	std::lock_guard<std::mutex> lock(g_mx);
	LeagueMeView v;
	if (!AccountValid()) return v;
	v.loaded = g_me.loaded;
	v.loading = g_me.loading;
	v.me = g_me.me;
	return v;
}

void LeagueMeRefresh(bool force)
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!AccountValid() || g_me.loading) return;
		if (!force && g_me.at && GetTickCount() - g_me.at < kMeKeepMs) return;
		g_me.loading = true;
	}
	SafeThread(FetchMe).detach();
}

void LeagueGameThreadTick()
{
	unsigned serial = 0;
	std::string pollToken;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_link.phase == LeagueLinkPhase::Waiting && !g_link.polling && static_cast<int>(GetTickCount() - g_link.nextPoll) >= 0) {
			if (g_link.expiresAt && NowMs() > g_link.expiresAt + 5000) {
				const unsigned next = g_link.serial + 1;
				g_link = Link();
				g_link.serial = next;
				g_link.error = "The code expired before it was confirmed. Ask for a new one.";
			}
			else {
				g_link.polling = true;
				serial = g_link.serial;
				pollToken = g_link.pollToken;
			}
		}
	}
	if (!pollToken.empty()) SafeThread(PollLink, serial, pollToken).detach();
	MatchTick();
	RankedTick();
	TourneyTick();
}
