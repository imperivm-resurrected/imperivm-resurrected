// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/gbdownload.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/guard.h"
#include "core/json.h"
#include "core/log.h"
#include "mods/gbref.h"
#include "mods/serverpick.h"

const wchar_t *DownloadUserAgent() { return L"Imperivm-mod/0.9"; }

namespace {
const uint64_t kMaxBytes = 4ull << 30;
const DWORD kWindowMs = 8000;
const DWORD kReceiveTimeoutMs = 15000;
const DWORD kRetryDelayMs = 3000;
const int kMaxFailuresInARow = 5;
const int kMaxReconnects = 40;
const int64_t kServerListSeconds = 3600;
const size_t kMaxListBytes = 1u << 20;
const char kStateFile[] = "gamebanana-servers.json";

std::mutex g_stateLock;

std::string Narrow(const std::wstring &w)
{
	if (w.empty()) return "";
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}

std::wstring Wide(const std::string &s)
{
	if (s.empty()) return L"";
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(n > 0 ? n : 0, L'\0');
	if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

bool CrackHttps(const std::wstring &url, std::string &host, std::wstring &object, INTERNET_PORT &port)
{
	URL_COMPONENTS uc = {sizeof(uc)};
	wchar_t hostBuf[256] = {}, pathBuf[4096] = {};
	uc.lpszHostName = hostBuf;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = pathBuf;
	uc.dwUrlPathLength = 4096;
	uc.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
	host = Narrow(std::wstring(uc.lpszHostName, uc.dwHostNameLength));
	object = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) + (uc.lpszExtraInfo ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");
	port = uc.nPort;
	return !host.empty();
}

std::string Mb(double bytesPerSecond)
{
	char buf[32];
	sprintf_s(buf, "%.2f MB/s", bytesPerSecond / 1048576.0);
	return buf;
}

class Connection {
public:
	~Connection() { Close(); }

	std::string Open(const std::string &host, INTERNET_PORT port, const std::wstring &object, uint64_t from, bool followRedirects, DWORD timeoutMs)
	{
		Close();
		status = 0;
		total = 0;
		m_s = WinHttpOpen(DownloadUserAgent(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!m_s) m_s = WinHttpOpen(DownloadUserAgent(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!m_s) return "could not connect";
		WinHttpSetTimeouts(m_s, 0, static_cast<int>(timeoutMs), 30000, static_cast<int>(timeoutMs));
		m_c = WinHttpConnect(m_s, Wide(host).c_str(), port, 0);
		m_r = m_c ? WinHttpOpenRequest(m_c, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
				  : nullptr;
		if (!m_r) return "could not connect";
		if (!followRedirects) {
			DWORD feature = WINHTTP_DISABLE_REDIRECTS;
			WinHttpSetOption(m_r, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
		}
		if (from) {
			const std::wstring range = L"Range: bytes=" + std::to_wstring(from) + L"-";
			WinHttpAddRequestHeaders(m_r, range.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
		}
		if (!WinHttpSendRequest(m_r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(m_r, nullptr)) {
			char buf[64];
			sprintf_s(buf, "network error %lu", GetLastError());
			return buf;
		}
		DWORD size = sizeof(status);
		WinHttpQueryHeaders(m_r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
		wchar_t text[128] = {};
		DWORD textSize = sizeof(text);
		if (status == 206 && WinHttpQueryHeaders(m_r, WINHTTP_QUERY_CONTENT_RANGE, nullptr, text, &textSize, nullptr)) {
			const wchar_t *slash = wcschr(text, L'/');
			if (slash && slash[1] != L'*') total = _wcstoui64(slash + 1, nullptr, 10);
		}
		else if (status == 200 && WinHttpQueryHeaders(m_r, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, text, &textSize, nullptr)) {
			total = _wcstoui64(text, nullptr, 10);
		}
		textSize = sizeof(text);
		contentType = WinHttpQueryHeaders(m_r, WINHTTP_QUERY_CONTENT_TYPE, nullptr, text, &textSize, nullptr) ? Narrow(text) : "";
		finalHost = host;
		finalObject = object;
		finalPort = port;
		wchar_t url[4096] = {};
		DWORD urlSize = sizeof(url);
		if (WinHttpQueryOption(m_r, WINHTTP_OPTION_URL, url, &urlSize)) CrackHttps(url, finalHost, finalObject, finalPort);
		return "";
	}

	std::string RawHeaders()
	{
		DWORD size = 0;
		if (!m_r) return "";
		WinHttpQueryHeaders(m_r, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
		if (!size) return "";
		std::wstring text(size / sizeof(wchar_t) + 1, L'\0');
		if (!WinHttpQueryHeaders(m_r, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &text[0], &size, WINHTTP_NO_HEADER_INDEX)) return "";
		text.resize(size / sizeof(wchar_t));
		return Narrow(text);
	}

	bool Read(char *buf, DWORD size, DWORD &got, DWORD &why)
	{
		got = 0;
		why = 0;
		if (m_r && WinHttpReadData(m_r, buf, size, &got)) return true;
		why = GetLastError();
		return false;
	}

	void Close()
	{
		if (m_r) WinHttpCloseHandle(m_r);
		if (m_c) WinHttpCloseHandle(m_c);
		if (m_s) WinHttpCloseHandle(m_s);
		m_r = m_c = m_s = nullptr;
	}

	DWORD status = 0;
	uint64_t total = 0;
	std::string contentType;
	std::string finalHost;
	std::wstring finalObject;
	INTERNET_PORT finalPort = INTERNET_DEFAULT_HTTPS_PORT;

private:
	HINTERNET m_s = nullptr, m_c = nullptr, m_r = nullptr;
};

struct ServerState {
	int64_t checked = 0;
	std::vector<FileServer> servers;
	std::vector<double> recentRates;
};

ServerState LoadState(const std::string &path)
{
	std::lock_guard<std::mutex> lock(g_stateLock);
	ServerState st;
	Json j;
	if (!ReadJsonFile(path, j) || j.type != Json::Object) return st;
	st.checked = static_cast<int64_t>(j.GetNumber("checked"));
	if (const Json *rates = j.Get("recentBytesPerSecond")) {
		for (const Json &r : rates->arr) {
			if (r.type == Json::Number && r.num > 0) st.recentRates.push_back(r.num);
		}
	}
	if (const Json *list = j.Get("servers")) {
		for (const Json &n : list->arr) {
			FileServer s;
			s.host = n.GetString("host");
			s.weight = n.GetNumber("weight", 1);
			if (n.type == Json::Object && IsGbFileServer(s.host)) st.servers.push_back(s);
		}
	}
	return st;
}

void SaveState(const std::string &path, const ServerState &st)
{
	std::lock_guard<std::mutex> lock(g_stateLock);
	CreateDirectoryA(path.substr(0, path.find_last_of('\\')).c_str(), nullptr);
	Json j = Json::MakeObject();
	j.Set("checked", Json::MakeNumber(static_cast<double>(st.checked)));
	Json list = Json::MakeArray();
	for (const FileServer &s : st.servers) {
		Json e = Json::MakeObject();
		e.Set("host", Json::MakeString(s.host));
		e.Set("weight", Json::MakeNumber(s.weight));
		list.arr.push_back(e);
	}
	j.Set("servers", list);
	Json rates = Json::MakeArray();
	for (double r : st.recentRates) rates.arr.push_back(Json::MakeNumber(r));
	j.Set("recentBytesPerSecond", rates);
	WriteJsonFile(path, j);
}

std::vector<FileServer> FetchFileservers(std::string &error)
{
	std::string host;
	std::wstring object;
	INTERNET_PORT port = 0;
	if (!CrackHttps(Wide(std::string(GbApiBase()) + "/Util/Fileservers?_nPage=1"), host, object, port)) {
		error = "invalid address";
		return {};
	}
	Connection c;
	error = c.Open(host, port, object, 0, true, kReceiveTimeoutMs);
	if (error.empty() && c.status != 200) error = "the server responded " + std::to_string(c.status);
	std::string body;
	std::vector<char> buf(64 * 1024);
	while (error.empty()) {
		DWORD n = 0, why = 0;
		if (!c.Read(buf.data(), static_cast<DWORD>(buf.size()), n, why)) error = "connection interrupted";
		else if (!n) break;
		else if (body.size() + n > kMaxListBytes) error = "response too large";
		else body.append(buf.data(), n);
	}
	Json j;
	if (error.empty() && !Json::Parse(body, j)) error = "invalid response";
	return error.empty() ? ParseFileservers(j) : std::vector<FileServer>();
}

struct ServerProbe {
	std::mutex lock;
	bool started = false, done = false;
	std::vector<FileServer> servers;
};

bool FreshServers(const std::string &statePath, std::vector<FileServer> &out)
{
	const ServerState st = LoadState(statePath);
	if (st.servers.empty() || static_cast<int64_t>(time(nullptr)) - st.checked >= kServerListSeconds) return false;
	out = st.servers;
	return true;
}

void StartProbe(const std::shared_ptr<ServerProbe> &probe, const std::string &statePath)
{
	{
		std::lock_guard<std::mutex> lock(probe->lock);
		if (probe->started) return;
		probe->started = true;
	}
	SafeThread([probe, statePath] {
		std::string error;
		std::vector<FileServer> list = FetchFileservers(error);
		if (!error.empty()) LogW("download", "gamebanana: could not read the list of file servers (%s)", error.c_str());
		else LogI("download", "gamebanana: %u file servers are up", static_cast<unsigned>(list.size()));
		if (!list.empty()) {
			ServerState st = LoadState(statePath);
			st.servers = list;
			st.checked = static_cast<int64_t>(time(nullptr));
			SaveState(statePath, st);
		}
		std::lock_guard<std::mutex> lock(probe->lock);
		probe->servers = std::move(list);
		probe->done = true;
	}).detach();
}

bool ProbeResult(const std::shared_ptr<ServerProbe> &probe, std::vector<FileServer> &out)
{
	std::lock_guard<std::mutex> lock(probe->lock);
	if (!probe->done) return false;
	out = probe->servers;
	return true;
}

bool LacksTheFile(const std::string &rawHeaders, const std::string &host)
{
	std::string h = rawHeaders;
	for (char &c : h) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	const std::string name = "filecache" + std::to_string(GbFileServerNumber(host));
	for (size_t at = h.find("x-gbfcp:"); at != std::string::npos; at = h.find("x-gbfcp:", at + 1)) {
		const size_t end = h.find('\n', at);
		const std::string line = h.substr(at, end == std::string::npos ? std::string::npos : end - at);
		if (line.find(name + " ") != std::string::npos && line.find("miss") != std::string::npos) return true;
	}
	return false;
}

void SaveConnectionRate(const std::string &statePath, double fastest)
{
	if (fastest <= 0) return;
	ServerState st = LoadState(statePath);
	st.recentRates = RememberRate(st.recentRates, fastest);
	SaveState(statePath, st);
}

const char *StepText(ServerPicker::Step s)
{
	switch (s) {
	case ServerPicker::kTry: return "trying";
	case ServerPicker::kKeepNew: return "keeping";
	case ServerPicker::kBack: return "back to";
	default: return "staying with";
	}
}

std::string SizeText(uint64_t bytes)
{
	char buf[32];
	if (bytes < 1024) sprintf_s(buf, "%llu B", static_cast<unsigned long long>(bytes));
	else if (bytes < 1024 * 1024) sprintf_s(buf, "%.0f KB", bytes / 1024.0);
	else sprintf_s(buf, "%.1f MB", bytes / 1048576.0);
	return buf;
}

const char *const kNotAvailable = "GameBanana did not give the file: it is private, was removed or was uploaded a moment ago.";

// A web page instead of the file is GameBanana saying no: a private or removed file answers 403 with its page.
std::string NotTheFile(uint64_t got, uint64_t expected, const std::string &contentType)
{
	if (contentType.find("text/html") != std::string::npos) return kNotAvailable;
	return "GameBanana sent " + SizeText(got) + " instead of the " + SizeText(expected) +
		" file. If it was uploaded a moment ago, try again in a few minutes.";
}

struct HostStats {
	uint64_t bytes = 0;
	DWORD ms = 0;
};
}

std::string DownloadFile(const std::string &url, uint64_t expectedBytes, const std::string &toFile, const std::string &cacheDir,
	bool allowOtherServers, const DownloadHooks &hooks, bool &usedOtherServers)
{
	usedOtherServers = false;
	std::string host;
	std::wstring object;
	INTERNET_PORT port = 0;
	if (!CrackHttps(Wide(url), host, object, port)) return "invalid address";
	HANDLE file = CreateFileA(toFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return "could not create " + toFile;
	Connection conn;
	std::string err = conn.Open(host, port, object, 0, true, kReceiveTimeoutMs);
	if (err.empty() && (conn.status == 403 || conn.status == 404)) err = kNotAvailable;
	if (err.empty() && conn.status != 200) err = "the server responded " + std::to_string(conn.status);
	const uint64_t total = conn.total;
	if (err.empty() && total > kMaxBytes) err = "the file is too large";
	if (err.empty() && expectedBytes && total && total != expectedBytes) {
		LogW("download", "%s answered %lu with %llu bytes (%s) instead of the %llu-byte file", conn.finalHost.c_str(), conn.status,
			static_cast<unsigned long long>(total), conn.contentType.c_str(), static_cast<unsigned long long>(expectedBytes));
		err = NotTheFile(total, expectedBytes, conn.contentType);
	}
	const std::string assigned = conn.finalHost;
	const std::wstring path = conn.finalObject;
	const INTERNET_PORT serverPort = conn.finalPort;
	const std::string statePath = cacheDir + "\\" + kStateFile;
	const bool pick = err.empty() && allowOtherServers && total > 0 && IsGbFileServer(assigned);
	ServerPicker picker(assigned, pick ? ConnectionRate(LoadState(statePath).recentRates) : 0);
	std::string current = assigned;
	if (pick) LogI("download", "gamebanana: %s assigned by GameBanana", assigned.c_str());
	std::map<std::string, HostStats> stats;
	std::vector<char> buf(256 * 1024);
	uint64_t pos = 0, windowBytes = 0;
	DWORD windowStart = 0, hostStart = 0;
	int failuresInARow = 0, reconnects = 0;
	auto closeHostStats = [&] {
		if (hostStart) stats[current].ms += GetTickCount() - hostStart;
		hostStart = 0;
	};
	const std::shared_ptr<ServerProbe> probe = std::make_shared<ServerProbe>();
	bool probing = false;
	auto loadCandidates = [&](bool wait) -> bool {
		if (picker.CandidatesKnown()) return true;
		std::vector<FileServer> servers;
		if (!probing && !FreshServers(statePath, servers)) {
			probing = true;
			StartProbe(probe, statePath);
		}
		while (probing && !ProbeResult(probe, servers)) {
			if (!wait || (hooks.cancelled && hooks.cancelled())) return false;
			Sleep(100);
		}
		const std::string p = Narrow(path);
		picker.SetCandidates(RendezvousOrder(p.substr(0, p.find('?')), servers));
		return true;
	};
	auto openAt = [&](const std::string &target) -> std::string {
		closeHostStats();
		current = target;
		windowStart = 0;
		windowBytes = 0;
		std::string e = conn.Open(target, serverPort, path, pos, false, kReceiveTimeoutMs);
		if (!e.empty()) return e;
		if (conn.status != 206 && !(pos == 0 && conn.status == 200)) return "the server responded " + std::to_string(conn.status);
		if (conn.total != total) return "the server has a different file (" + std::to_string(conn.total) + " bytes)";
		if (target != assigned && LacksTheFile(conn.RawHeaders(), target)) return "it does not have the file yet";
		return "";
	};
	auto follow = [&](ServerPicker::Step step, double rate, const char *why) {
		LogI("download", "gamebanana: %s at %s (%s); %s %s", current.c_str(), Mb(rate).c_str(), why, StepText(step), picker.Current().c_str());
	};
	auto moveTo = [&](std::string target) -> std::string {
		for (int guard = 0; guard <= ServerPicker::kMaxTries + 1; guard++) {
			const std::string e = openAt(target);
			if (e.empty()) {
				if (target != assigned) usedOtherServers = true;
				return "";
			}
			LogW("download", "gamebanana: %s could not continue the download (%s)", target.c_str(), e.c_str());
			if (!pick) return e;
			loadCandidates(true);
			const ServerPicker::Step step = picker.OnFailure();
			if (picker.Current() == target && step == ServerPicker::kStay) return e;
			target = picker.Current();
		}
		return "no server could continue the download";
	};
	if (hooks.progress) hooks.progress(0, total);
	while (err.empty()) {
		if (hooks.cancelled && hooks.cancelled()) {
			err = "canceled";
			break;
		}
		DWORD n = 0, why = 0;
		const bool read = conn.Read(buf.data(), static_cast<DWORD>(buf.size()), n, why);
		const DWORD now = GetTickCount();
		if (read && n > 0) {
			if (!hostStart) hostStart = now;
			DWORD written = 0;
			if (!WriteFile(file, buf.data(), n, &written, nullptr) || written != n) {
				err = "could not write to disk";
				break;
			}
			pos += n;
			stats[current].bytes += n;
			failuresInARow = 0;
			if (pos > kMaxBytes || (total && pos > total)) {
				err = "response too large";
				break;
			}
			if (hooks.progress) hooks.progress(pos, total);
			if (total && pos >= total) break;
			if (!windowStart) {
				windowStart = now;
				windowBytes = 0;
				continue;
			}
			windowBytes += n;
			if (!pick || now - windowStart < kWindowMs) continue;
			const double rate = windowBytes * 1000.0 / (now - windowStart);
			if (picker.WantsCandidates(rate) && !loadCandidates(false)) continue;
			windowStart = now;
			windowBytes = 0;
			const ServerPicker::Step step = picker.OnWindow(rate);
			if (picker.Current() == current) {
				if (step != ServerPicker::kStay) follow(step, rate, "measured");
				continue;
			}
			follow(step, rate, "measured");
			const std::string e = moveTo(picker.Current());
			if (!e.empty()) LogW("download", "gamebanana: %s", e.c_str());
			continue;
		}
		if (read && n == 0 && (!total || pos >= total)) break;
		reconnects++;
		failuresInARow++;
		LogW("download", "gamebanana: connection to %s interrupted at %llu of %llu bytes (WinHTTP error %lu)", current.c_str(),
			static_cast<unsigned long long>(pos), static_cast<unsigned long long>(total), why);
		if (!total || reconnects > kMaxReconnects) {
			err = "connection interrupted";
			break;
		}
		std::string target = current;
		if (pick) {
			const double rate = windowStart && now - windowStart >= kWindowMs ? windowBytes * 1000.0 / (now - windowStart) : -1;
			if (rate >= 0 && (!picker.WantsCandidates(rate) || loadCandidates(false))) {
				const ServerPicker::Step step = picker.OnWindow(rate);
				if (picker.Current() != current) follow(step, rate, "stalled");
			}
			else if (picker.Testing() || failuresInARow >= 2) {
				loadCandidates(true);
				const ServerPicker::Step step = picker.OnFailure();
				if (picker.Current() != current) follow(step, 0, "keeps failing");
			}
			target = picker.Current();
		}
		if (target == current) {
			if (failuresInARow > kMaxFailuresInARow) {
				err = "connection interrupted";
				break;
			}
			for (DWORD waited = 0; waited < kRetryDelayMs && !(hooks.cancelled && hooks.cancelled()); waited += 100) Sleep(100);
		}
		const std::string e = moveTo(target);
		if (!e.empty()) LogW("download", "gamebanana: %s", e.c_str());
	}
	closeHostStats();
	CloseHandle(file);
	conn.Close();
	if (pick) {
		for (const auto &kv : stats) {
			if (kv.second.ms >= kWindowMs / 4) picker.NoteRate(kv.first, kv.second.bytes * 1000.0 / kv.second.ms);
		}
		if (picker.RateWorthRemembering()) SaveConnectionRate(statePath, picker.FastestSeen());
	}
	if (err.empty() && expectedBytes && pos != expectedBytes) {
		LogW("download", "%s sent %llu bytes (%s) instead of the %llu-byte file", current.c_str(), static_cast<unsigned long long>(pos),
			conn.contentType.c_str(), static_cast<unsigned long long>(expectedBytes));
		err = NotTheFile(pos, expectedBytes, conn.contentType);
	}
	if (!err.empty()) {
		DeleteFileA(toFile.c_str());
		return err;
	}
	if (pick) {
		std::string summary;
		for (const auto &kv : stats) {
			if (!kv.second.bytes) continue;
			summary += (summary.empty() ? "" : ", ") + kv.first + " " + std::to_string(kv.second.bytes / 1048576) + " MB" +
				(kv.second.ms ? " at " + Mb(kv.second.bytes * 1000.0 / kv.second.ms) : "");
		}
		LogI("download", "gamebanana: downloaded %llu bytes: %s", static_cast<unsigned long long>(pos), summary.c_str());
	}
	return "";
}
