// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <winhttp.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <set>
#include <atomic>
#include <thread>
#include <condition_variable>

#include "mods/downloads.h"
#include "core/json.h"
#include "core/guard.h"
#include "core/pathsafe.h"
#include "core/archivefile.h"
#include "core/versioncmp.h"
#include "core/zipfile.h"
#include "mods/mods.h"
#include "mods/mappack.h"
#include "mods/catalogfilter.h"
#include "mods/gbdownload.h"
#include "mods/gbcache.h"
#include "mods/gbref.h"
#include "mods/leftovers.h"
#include "mods/looselayout.h"
#include "i18n/tr.h"
#include "core/uiport.h"

namespace {

const uint64_t kMaxDownload = 4ull << 30;
const size_t kMaxJson = 8u << 20;
const size_t kMapHeaderBytes = 64 * 1024;

std::string GameDir()
{
	static std::string dir;
	if (dir.empty()) {
		HMODULE self = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&GameDir), &self);
		char path[MAX_PATH];
		GetModuleFileNameA(self, path, MAX_PATH);
		char *slash = strrchr(path, '\\');
		if (slash) *slash = 0;
		dir = path;
	}
	return dir;
}

std::string ModsDir() { return GameDir() + "\\mods"; }
std::string WorkDir() { return ModsDir() + "\\_downloads"; }
std::string IniPath() { return GameDir() + "\\imperivm-mod.ini"; }

std::string IniGet(const char *section, const char *key)
{
	char buf[2048] = {};
	GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), IniPath().c_str());
	return buf;
}

void MakeDirs(const std::string &path)
{
	for (size_t i = 3; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '\\') {
			CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
		}
	}
}

bool IsDir(const std::string &p)
{
	DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsFile(const std::string &p)
{
	DWORD a = GetFileAttributesA(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
	return s;
}

std::wstring Wide(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	return w;
}

std::string Narrow(const std::wstring &w, UINT cp = CP_UTF8)
{
	int n = WideCharToMultiByte(cp, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n - 1 : 0, '\0');
	if (n > 1) WideCharToMultiByte(cp, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
	return s;
}

void DeleteTree(const std::string &dir)
{
	if (dir.find("\\_downloads\\") == std::string::npos) {
		return;
	}
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			std::string n = fd.cFileName;
			if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					RemoveDirectoryA(p.c_str());
				}
				else {
					SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
					DeleteFileA(p.c_str());
				}
			}
			else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				DeleteTree(p);
			}
			else {
				SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileA(p.c_str());
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryA(dir.c_str());
}

std::string Timestamp()
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	char buf[32];
	sprintf_s(buf, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	return buf;
}

std::string HashFile(const std::string &path, LPCWSTR alg)
{
	BCRYPT_ALG_HANDLE a = nullptr;
	BCRYPT_HASH_HANDLE h = nullptr;
	std::string hex;
	if (BCryptOpenAlgorithmProvider(&a, alg, nullptr, 0) != 0) return hex;
	if (BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0) == 0) {
		HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (f != INVALID_HANDLE_VALUE) {
			std::vector<unsigned char> buf(1 << 20);
			DWORD n = 0;
			while (ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n) {
				BCryptHashData(h, buf.data(), n, 0);
			}
			CloseHandle(f);
			DWORD len = 0, got = 0;
			BCryptGetProperty(a, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof(len), &got, 0);
			std::vector<unsigned char> out(len);
			if (BCryptFinishHash(h, out.data(), len, 0) == 0) {
				static const char *d = "0123456789abcdef";
				for (unsigned char c : out) {
					hex += d[c >> 4];
					hex += d[c & 15];
				}
			}
		}
		BCryptDestroyHash(h);
	}
	BCryptCloseAlgorithmProvider(a, 0);
	return hex;
}

std::mutex g_lock;
DownloadStatus g_status;
std::atomic<bool> g_cancel{false};
std::vector<CatalogEntry> g_catalog;
bool g_catalogBusy = false;

struct Job {
	enum Kind { Entry, Archive, RoomPack } kind;
	CatalogEntry entry;
	std::string text;
};
std::deque<Job> g_jobs;
std::condition_variable g_cv;
bool g_workerStarted = false;

void SetProgress(uint64_t done, uint64_t total)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_status.done = done;
	g_status.total = total;
}

void SetUnpack(bool unpacking, uint64_t done, uint64_t total)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_status.unpacking = unpacking;
	g_status.unpacked = done;
	g_status.unpackTotal = total;
}

void Finish(bool ok, const std::string &msg)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_status.lastOk = ok;
		g_status.message = msg;
		g_status.finished++;
	}
	LogAt(ok ? kLogInfo : kLogWarn, "download", "%s", TrSource(msg).c_str());
	UiToast(msg, !ok);
	if (ok) UiRefreshMainMenuButton();
}

std::string HttpGet(const std::string &url, const std::vector<std::wstring> &headers, std::string *toMemory, const std::string &toFile)
{
	std::wstring wurl = Wide(url);
	URL_COMPONENTS uc = {sizeof(uc)};
	wchar_t host[256] = {}, path[4096] = {};
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 4096;
	uc.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
		return "invalid address";
	}
	if (uc.nScheme != INTERNET_SCHEME_HTTPS) {
		return "only https:// addresses are supported";
	}
	std::wstring object = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) +
		(uc.lpszExtraInfo ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");
	HINTERNET s = WinHttpOpen(DownloadUserAgent(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s) {
		s = WinHttpOpen(DownloadUserAgent(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	HINTERNET c = s ? WinHttpConnect(s, std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(), uc.nPort, 0) : nullptr;
	HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
					: nullptr;
	auto close = [&] {
		if (r) WinHttpCloseHandle(r);
		if (c) WinHttpCloseHandle(c);
		if (s) WinHttpCloseHandle(s);
	};
	if (!r) {
		close();
		return "could not connect";
	}
	for (const std::wstring &h : headers) {
		WinHttpAddRequestHeaders(r, h.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
	}
	if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, nullptr)) {
		char buf[64];
		sprintf_s(buf, "network error %lu", GetLastError());
		close();
		return buf;
	}
	DWORD code = 0, codeSize = sizeof(code);
	WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &codeSize, nullptr);
	if (code != 200) {
		std::string detail;
		char body[2048];
		DWORD got = 0;
		if (WinHttpReadData(r, body, sizeof(body) - 1, &got) && got) {
			body[got] = 0;
			Json j;
			if (Json::Parse(body, j)) {
				if (const Json *e = j.Get("error")) detail = e->GetString("message");
				else detail = j.GetString("_sErrorMessage", j.GetString("_sErrorCode"));
			}
		}
		close();
		return "the server responded " + std::to_string(code) + (detail.empty() ? "" : ": " + detail);
	}
	wchar_t lenText[32] = {};
	DWORD lenSize = sizeof(lenText);
	uint64_t total = 0;
	if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, lenText, &lenSize, nullptr)) {
		total = _wcstoui64(lenText, nullptr, 10);
	}
	if (total > kMaxDownload) {
		close();
		return "the file is too large";
	}
	HANDLE file = INVALID_HANDLE_VALUE;
	if (!toFile.empty()) {
		file = CreateFileA(toFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			close();
			return "could not create " + toFile;
		}
	}
	std::vector<char> buf(256 * 1024);
	uint64_t done = 0;
	std::string err;
	for (;;) {
		DWORD n = 0;
		if (!WinHttpReadData(r, buf.data(), static_cast<DWORD>(buf.size()), &n)) {
			err = "connection interrupted";
			break;
		}
		if (n == 0) break;
		done += n;
		if (done > kMaxDownload || (toMemory && done > kMaxJson)) {
			err = "response too large";
			break;
		}
		if (toMemory) toMemory->append(buf.data(), n);
		DWORD written = 0;
		if (file != INVALID_HANDLE_VALUE && (!WriteFile(file, buf.data(), n, &written, nullptr) || written != n)) {
			err = "could not write to disk";
			break;
		}
	}
	if (err.empty() && total && done < total) err = "connection interrupted";
	if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	if (!err.empty() && !toFile.empty()) DeleteFileA(toFile.c_str());
	close();
	return err;
}

std::string HttpGetJson(const std::string &url, const std::vector<std::wstring> &headers, Json &out)
{
	std::string body;
	std::string err = HttpGet(url, headers, &body, "");
	if (!err.empty()) return err;
	std::string perr;
	if (!Json::Parse(body, out, &perr)) return "invalid response (" + perr + ")";
	return "";
}


const int64_t kListTtl = 15 * 60;
const int64_t kDetailTtl = 30 * 60;
const int64_t kStartupTtl = 3 * 3600;
const int64_t kFreshTtl = 1;
std::atomic<DWORD> g_forceUntil{0};
std::atomic<DWORD> g_lastForce{0};
std::atomic<int64_t> g_catalogTtl{kDetailTtl};

std::string CacheRoot() { return GameDir() + "\\imperivm-resurrected\\_cache\\gb"; }

bool CacheBypass()
{
	const DWORD until = g_forceUntil;
	return until != 0 && static_cast<int>(until - GetTickCount()) > 0;
}

std::string HttpGetJsonCached(const std::string &url, int64_t ttl, Json &out, bool forceNet = false, bool *usedNet = nullptr)
{
	if (usedNet) *usedNet = false;
	const std::string dir = CacheRoot() + "\\api";
	const int64_t now = GbCacheNow();
	std::string cached, perr;
	bool stale = false;
	bool have = GbCacheRead(dir, url, ttl, cached, stale, now);
	if (have && !stale && !forceNet && !CacheBypass()) {
		if (Json::Parse(cached, out, &perr)) return "";
		GbCacheRemove(dir, url);
		have = false;
	}
	if (usedNet) *usedNet = true;
	std::string body;
	std::string err = HttpGet(url, {}, &body, "");
	if (err.empty()) {
		if (!Json::Parse(body, out, &perr)) err = "invalid response (" + perr + ")";
		else {
			if (!out.Get("_sErrorCode")) GbCacheWrite(dir, url, body, now);
			return "";
		}
	}
	if (have && Json::Parse(cached, out, &perr)) {
		LogW("download", "gamebanana: %s; the copy saved on this computer is used", err.c_str());
		return "";
	}
	return err;
}

std::string GbGame()
{
	const std::string g = IniGet("GameBanana", "GameId");
	return g.empty() ? "26069" : g;
}

std::string UrlEncode(const std::string &s)
{
	std::string out;
	char buf[4];
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
		else {
			sprintf_s(buf, "%%%02X", c);
			out += buf;
		}
	}
	return out;
}

std::string StripHtml(const std::string &html)
{
	std::string out;
	for (size_t i = 0; i < html.size();) {
		if (html[i] == '<') {
			const size_t end = html.find('>', i);
			if (end == std::string::npos) break;
			const std::string tag = Lower(html.substr(i + 1, end - i - 1));
			if (tag.compare(0, 2, "br") == 0 || tag == "/p" || tag == "/div" || tag.compare(0, 2, "/h") == 0) out += '\n';
			else if (tag.compare(0, 2, "li") == 0) out += "\n\xE2\x80\xA2 ";
			i = end + 1;
			continue;
		}
		if (html[i] == '&') {
			static const struct {
				const char *ent, *text;
			} kEnt[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#039;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
			bool done = false;
			for (const auto &e : kEnt) {
				const size_t n = strlen(e.ent);
				if (html.compare(i, n, e.ent) == 0) {
					out += e.text;
					i += n;
					done = true;
					break;
				}
			}
			if (done) continue;
		}
		out += html[i++];
	}
	std::string clean;
	int blanks = 0;
	for (char c : out) {
		if (c == '\n') {
			if (++blanks > 2) continue;
		}
		else if (c != ' ' && c != '\r') {
			blanks = 0;
		}
		clean += c;
	}
	const size_t first = clean.find_first_not_of("\n ");
	clean.erase(0, first == std::string::npos ? clean.size() : first);
	while (!clean.empty() && (clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();
	return clean;
}

std::string DateVersion(uint64_t ts)
{
	const time_t t = static_cast<time_t>(ts);
	struct tm tmv = {};
	if (!ts || gmtime_s(&tmv, &t) != 0) return "";
	char buf[16];
	strftime(buf, sizeof(buf), "%Y.%m.%d", &tmv);
	return buf;
}

bool SystemReadsWebp()
{
	static const bool webp = [] {
		const GUID kWebpContainer = {0xe094b0e2, 0x67f2, 0x45b3, {0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3}};
		const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		IWICImagingFactory *factory = nullptr;
		IWICBitmapDecoder *decoder = nullptr;
		const bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateDecoder(kWebpContainer, nullptr, &decoder));
		if (decoder) decoder->Release();
		if (factory) factory->Release();
		if (SUCCEEDED(co)) CoUninitialize();
		LogI("download", "images: this system %s WebP, %s", ok ? "reads" : "does not read", ok ? "GameBanana thumbnails are used" : "the original images are used");
		return ok;
	}();
	return webp;
}

void GbFillSummary(GbMod &r, const Json &m, bool sound)
{
	const uint64_t row = static_cast<uint64_t>(m.GetNumber("_idRow"));
	r.id = row && sound ? GbSoundKey(row) : row;
	r.name = m.GetString("_sName");
	r.page = m.GetString("_sProfileUrl");
	if (const Json *u = m.Get("_aSubmitter")) r.author = u->GetString("_sName");
	const std::vector<GbPicture> pictures = GbPictures(m, SystemReadsWebp());
	r.logoUrl = pictures.empty() ? "" : pictures.front().card;
	r.shots.clear();
	for (const GbPicture &p : pictures) {
		r.shots.push_back({p.thumb, p.big});
		if (r.shots.size() >= 14) break;
	}
	r.likes = static_cast<int>(m.GetNumber("_nLikeCount"));
	r.views = static_cast<uint64_t>(m.GetNumber("_nViewCount"));
	r.added = static_cast<uint64_t>(m.GetNumber("_tsDateAdded"));
	r.updated = std::max<uint64_t>(r.added, static_cast<uint64_t>(std::max(m.GetNumber("_tsDateUpdated"), m.GetNumber("_tsDateModified"))));
	r.hasFile = m.GetBool("_bHasFiles", true);
	r.tags.clear();
	const Json *cat = m.Get("_aRootCategory");
	if (!cat) cat = m.Get("_aCategory");
	if (cat) r.category = cat->GetString("_sName");
	if (!r.category.empty()) r.tags.push_back(r.category);
	if (const Json *sub = m.Get("_aSubCategory")) {
		const std::string n = sub->GetString("_sName");
		if (!n.empty() && n != r.category) r.tags.push_back(n);
	}
	if (const Json *t = m.Get("_aTags")) {
		for (const Json &tag : t->arr) {
			const std::string title = tag.GetString("_sTitle"), value = tag.GetString("_sValue");
			const std::string n = tag.type == Json::String ? tag.str : title.empty() ? value : value.empty() ? title : title + ": " + value;
			if (!n.empty() && std::find(r.tags.begin(), r.tags.end(), n) == r.tags.end()) r.tags.push_back(n);
		}
	}
	r.facts = CatalogFactsOf(r.category, r.tags);
	if (sound) {
		r.facts.category = kCatAudio;
		r.facts.categories |= 1u << kCatAudio;
	}
	CatalogEntry &e = r.entry;
	e.source = "GameBanana";
	e.id = GbCatalogId(r.id);
	e.name = r.name;
	e.page = r.page;
	e.gbId = r.id;
	e.authors.clear();
	if (!r.author.empty()) e.authors.push_back(r.author);
	e.tags = r.tags;
	e.version = m.GetString("_sVersion");
	if (e.version.empty()) e.version = DateVersion(r.updated);
}

size_t GbUsableFiles(const Json *files)
{
	size_t n = 0;
	if (files) {
		for (const Json &f : files->arr) {
			if (!f.GetBool("_bIsArchived", false)) n++;
		}
	}
	return n;
}

std::string GbFileVersion(const Json &file, const std::string &modVersion, size_t usableFiles)
{
	const std::string v = file.GetString("_sVersion");
	if (!v.empty()) return v;
	if (usableFiles == 1 && !modVersion.empty()) return modVersion;
	return DateVersion(static_cast<uint64_t>(file.GetNumber("_tsDateAdded")));
}

void GbFillDetails(GbMod &r, const Json &p, bool sound)
{
	GbFillSummary(r, p, sound);
	r.summary = p.GetString("_sDescription");
	r.descriptionHtml = p.GetString("_sText");
	r.description = StripHtml(r.descriptionHtml);
	r.downloads = static_cast<uint64_t>(p.GetNumber("_nDownloadCount"));
	CatalogEntry &e = r.entry;
	e.description = r.description.empty() ? r.summary : r.description;
	e.url.clear();
	const Json *best = nullptr;
	const Json *files = p.Get("_aFiles");
	if (files) {
		for (const Json &f : files->arr) {
			if (f.GetBool("_bIsArchived", false) || f.GetString("_sDownloadUrl").empty()) continue;
			if (!best || FileIsNewer(f.GetString("_sVersion"), static_cast<unsigned long long>(f.GetNumber("_tsDateAdded")), best->GetString("_sVersion"),
					static_cast<unsigned long long>(best->GetNumber("_tsDateAdded")))) {
				best = &f;
			}
		}
	}
	if (best) {
		e.url = best->GetString("_sDownloadUrl");
		e.bytes = static_cast<uint64_t>(best->GetNumber("_nFilesize"));
		e.md5 = Lower(best->GetString("_sMd5Checksum"));
		e.fileId = static_cast<uint64_t>(best->GetNumber("_idRow"));
		e.fileDate = static_cast<uint64_t>(best->GetNumber("_tsDateAdded"));
		e.fileCount = static_cast<unsigned>(GbUsableFiles(files));
		e.version = GbFileVersion(*best, p.GetString("_sVersion"), GbUsableFiles(files));
	}
	r.hasFile = !e.url.empty();
	r.deps.clear();
	if (const Json *req = p.Get("_aRequirements")) {
		for (const Json &q : req->arr) {
			if (q.type != Json::Array || q.arr.size() < 2 || q.arr[1].type != Json::String) continue;
			const uint64_t dep = GbKeyFromUrl(q.arr[1].str);
			if (dep && dep != r.id && std::find(r.deps.begin(), r.deps.end(), dep) == r.deps.end()) r.deps.push_back(dep);
		}
	}
	r.dependencies = !r.deps.empty();
	r.details = true;
}

std::string GbFetchMod(uint64_t id, GbMod &out, int64_t ttl = kDetailTtl)
{
	Json j;
	const std::string err = HttpGetJsonCached(std::string(GbApiBase()) + GbApiPath(id) + "/ProfilePage", ttl, j);
	if (!err.empty()) return err;
	if (!j.Get("_idRow")) return j.GetString("_sErrorCode", "empty response");
	if (j.GetBool("_bIsTrashed", false) || j.GetBool("_bIsWithheld", false) || j.GetBool("_bIsPrivate", false)) {
		return "the content is not available";
	}
	GbFillDetails(out, j, GbIsSound(id));
	return "";
}

std::string GbListSection(bool sound, std::vector<GbMod> &out, bool &net)
{
	std::set<uint64_t> seen;
	for (const GbMod &m : out) seen.insert(m.id);
	for (int page = 1; page <= 40; page++) {
		Json j;
		const std::string url = std::string(GbApiBase()) + (sound ? "/Sound" : "/Mod") + "/Index?_nPerpage=50&_nPage=" + std::to_string(page) +
			"&_aFilters%5BGeneric_Game%5D=" + GbGame();
		bool usedNet = false;
		const std::string err = HttpGetJsonCached(url, kListTtl, j, net, &usedNet);
		if (usedNet) net = true;
		if (!err.empty()) return err;
		const Json *recs = j.Get("_aRecords");
		if (!recs) return j.GetString("_sErrorCode", "empty response");
		if (recs->arr.empty() && page == 1) GbCacheRemove(CacheRoot() + "\\api", url);
		for (const Json &m : recs->arr) {
			GbMod r;
			GbFillSummary(r, m, sound);
			if (r.id && seen.insert(r.id).second) out.push_back(r);
		}
		const Json *meta = j.Get("_aMetadata");
		if (recs->arr.empty() || (meta && meta->GetBool("_bIsComplete", false))) break;
	}
	return "";
}

std::string GbListAll(std::vector<GbMod> &out)
{
	out.clear();
	bool net = false;
	const std::string err = GbListSection(false, out, net);
	if (!err.empty()) return err;
	std::vector<GbMod> sounds;
	const std::string soundErr = GbListSection(true, sounds, net);
	if (!soundErr.empty()) LogW("download", "gamebanana: could not read the Sounds section (%s)", soundErr.c_str());
	else out.insert(out.end(), sounds.begin(), sounds.end());
	return "";
}

void ReadGb(std::vector<CatalogEntry> &out, std::string &errors)
{
	for (const ModInfo &m : ModsList()) {
		if (!m.gbId) continue;
		if (std::find_if(out.begin(), out.end(), [&](const CatalogEntry &e) { return e.gbId == m.gbId; }) != out.end()) continue;
		GbMod r;
		const std::string err = GbFetchMod(m.gbId, r, g_catalogTtl);
		if (!err.empty()) {
			errors += (errors.empty() ? "" : "; ") + std::string("GameBanana ") + GbKeyText(m.gbId) + ": " + err;
			continue;
		}
		if (!r.hasFile) continue;
		r.entry.id = m.id;
		out.push_back(r.entry);
	}
}

void RefreshCatalogsNow()
{
	std::vector<CatalogEntry> entries;
	std::string errors;
	ReadGb(entries, errors);
	std::vector<CatalogEntry> unique;
	for (const CatalogEntry &e : entries) {
		auto it = std::find_if(unique.begin(), unique.end(), [&](const CatalogEntry &u) { return _stricmp(u.id.c_str(), e.id.c_str()) == 0; });
		if (it == unique.end()) unique.push_back(e);
		else if (CompareVersions(e.version, it->version) > 0) *it = e;
	}
	std::sort(unique.begin(), unique.end(), [](const CatalogEntry &a, const CatalogEntry &b) { return Lower(a.name) < Lower(b.name); });
	const size_t found = unique.size();
	std::lock_guard<std::mutex> lock(g_lock);
	g_catalog = std::move(unique);
	g_catalogBusy = false;
	LogAt(errors.empty() ? kLogInfo : kLogWarn, "download", "catalog: %u installed content packs checked on GameBanana%s%s", static_cast<unsigned>(found),
		errors.empty() ? "" : ". Errors: ", errors.c_str());
	UiRefreshMainMenuButton();
}

bool IsMarker(const std::string &name)
{
	static const char *kMarkers[] = {"mod.json", "packs", "local", "patches", "data", "scenarios", "adventures",
		"conquests", "conquestmaps", "ui", "units", "sounds", "currentlang", "music", "movies"};
	const std::string n = Lower(name);
	for (const char *m : kMarkers) {
		if (n == m) return true;
	}
	return false;
}

std::string FindModRoot(std::string dir)
{
	for (int depth = 0; depth < 4; depth++) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return dir;
		int dirs = 0, files = 0;
		bool marker = false;
		std::string only;
		do {
			std::string n = fd.cFileName;
			if (n == "." || n == "..") continue;
			marker = marker || IsMarker(n);
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				dirs++;
				only = n;
			}
			else {
				files++;
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
		if (marker || dirs != 1 || files > 0) return dir;
		dir += "\\" + only;
	}
	return dir;
}

int ReadMapKind(const std::string &file)
{
	FILE *fp = nullptr;
	if (fopen_s(&fp, file.c_str(), "rb") != 0 || !fp) return kMapUnknown;
	std::string head(kMapHeaderBytes, '\0');
	head.resize(fread(&head[0], 1, head.size(), fp));
	fclose(fp);
	return BfhpMapKind(head);
}

std::vector<std::string> ReadMapVideos(const std::string &file)
{
	std::vector<std::string> out;
	FILE *fp = nullptr;
	if (fopen_s(&fp, file.c_str(), "rb") != 0 || !fp) return out;
	const size_t chunk = 1u << 20, overlap = 256;
	std::string carry;
	std::string buf(chunk, '\0');
	for (;;) {
		const size_t got = fread(&buf[0], 1, chunk, fp);
		if (!got) break;
		const std::string text = carry + buf.substr(0, got);
		for (const std::string &v : BfhpVideoReferences(text)) {
			if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
		}
		carry = text.size() > overlap ? text.substr(text.size() - overlap) : text;
	}
	fclose(fp);
	return out;
}

void PlaceLooseVideos(const std::string &root)
{
	std::vector<std::string> videos;
	static const char *kMapFolders[] = {"", "\\adventures", "\\scenarios", "\\conquests"};
	for (const char *sub : kMapFolders) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((root + sub + "\\*.bfhp").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			for (const std::string &v : ReadMapVideos(root + sub + "\\" + fd.cFileName)) {
				if (std::find(videos.begin(), videos.end(), v) == videos.end()) videos.push_back(v);
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	if (videos.empty()) return;
	std::vector<std::string> rootFiles;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) rootFiles.push_back(fd.cFileName);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	for (const LooseMove &m : PlanLooseVideos(rootFiles, videos)) {
		const std::string dir = root + "\\" + m.folder;
		CreateDirectoryA(dir.c_str(), nullptr);
		if (MoveFileA((root + "\\" + m.file).c_str(), (dir + "\\" + m.file).c_str())) {
			LogI("download", "%s is a video the map plays and was loose at the root of the archive: placed in %s", m.file.c_str(), m.folder.c_str());
		}
		else {
			LogW("download", "could not place %s in %s (error %lu)", m.file.c_str(), m.folder.c_str(), GetLastError());
		}
	}
}

void PlaceLooseFiles(const std::string &root, int category)
{
	std::vector<LooseFile> files;
	bool gameFolders = false;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		const std::string low = Lower(n);
		const bool map = low.size() > 5 && low.compare(low.size() - 5, 5, ".bfhp") == 0;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) gameFolders = gameFolders || IsMarker(n);
		else files.push_back({n, map ? ReadMapKind(root + "\\" + n) : kMapUnknown});
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	for (const LooseMove &m : PlanLooseFiles(files, gameFolders, category)) {
		const std::string dir = root + "\\" + m.folder;
		CreateDirectoryA(dir.c_str(), nullptr);
		if (MoveFileA((root + "\\" + m.file).c_str(), (dir + "\\" + m.file).c_str())) {
			LogI("download", "%s was loose at the root of the archive: placed in %s", m.file.c_str(), m.folder.c_str());
		}
		else {
			LogW("download", "could not place %s in %s (error %lu)", m.file.c_str(), m.folder.c_str(), GetLastError());
		}
	}
}

void CheckFiles(const std::string &base, const std::string &rel, std::string &bad, unsigned &count)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((base + "\\" + rel + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !pathsafe::SafeComponent(n)) {
			if (bad.empty()) bad = rel + n;
			continue;
		}
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			CheckFiles(base, rel + n + "\\", bad, count);
		}
		else {
			count++;
			if (bad.empty() && !ModsAllowedFile(rel + n)) bad = rel + n;
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

std::string SafeFolder(const std::string &name)
{
	std::string out;
	for (unsigned char c : name) {
		out += (isalnum(c) || c >= 0x80 || (c != 0 && strchr(" -_.()+", c))) ? static_cast<char>(c) : '_';
	}
	while (!out.empty() && (out[0] == '_' || out[0] == '.' || out[0] == ' ')) out.erase(0, 1);
	while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
	if (out.empty()) return "mod";
	out = out.substr(0, 80);
	if (pathsafe::ReservedDeviceName(out)) out += "-mod";
	return out;
}

std::string TarPath()
{
	char sys[MAX_PATH];
	GetSystemDirectoryA(sys, MAX_PATH);
	return std::string(sys) + "\\tar.exe";
}

bool RunTar(const std::string &arguments, HANDLE out, DWORD timeoutMs, DWORD &code)
{
	const std::string cmd = "\"" + TarPath() + "\" " + arguments;
	STARTUPINFOA si = {sizeof(si)};
	if (out != INVALID_HANDLE_VALUE) {
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		si.hStdOutput = out;
		si.hStdError = out;
	}
	PROCESS_INFORMATION pi = {};
	std::vector<char> line(cmd.begin(), cmd.end());
	line.push_back(0);
	if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, out != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		return false;
	}
	code = 1;
	if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_TIMEOUT) {
		TerminateProcess(pi.hProcess, 1);
		WaitForSingleObject(pi.hProcess, 5000);
		LogW("download", "tar.exe took more than %lu s and was closed", timeoutMs / 1000);
	}
	else {
		GetExitCodeProcess(pi.hProcess, &code);
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

std::string ReadCapped(const std::string &path, size_t cap)
{
	std::string out;
	HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) return out;
	char buf[65536];
	DWORD got = 0;
	while (out.size() < cap && ReadFile(f, buf, sizeof(buf), &got, nullptr) && got) out.append(buf, got);
	CloseHandle(f);
	return out;
}

const size_t kMaxArchiveEntries = 60000;
const unsigned long long kMaxArchiveBytes = 6ull << 30;

std::string InspectArchive(const std::string &archive)
{
	const std::string listing = WorkDir() + "\\list-" + Timestamp() + ".txt";
	SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
	HANDLE out = CreateFileA(listing.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
	if (out == INVALID_HANDLE_VALUE) return "the extraction could not be prepared";
	DWORD code = 1;
	const bool ran = RunTar("-t -v -f \"" + archive + "\"", out, 5 * 60 * 1000, code);
	CloseHandle(out);
	const std::string text = ReadCapped(listing, 48u << 20);
	DeleteFileA(listing.c_str());
	if (!ran) return "could not run tar.exe (Windows 10 or later required)";
	if (code != 0) return "the archive could not be read (unsupported format or damaged file)";
	size_t entries = 0;
	unsigned long long bytes = 0;
	for (size_t pos = 0; pos < text.size();) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) end = text.size();
		const std::string row = text.substr(pos, end - pos);
		pos = end + 1;
		if (row.size() < 12 || !strchr("-dlhcbps", row[0]) || !strchr("r-", row[1]) || !strchr("w-", row[2])) continue;
		if (row[0] != '-' && row[0] != 'd') return "the archive contains links or special files: it will not be installed";
		entries++;
		size_t at = 0;
		for (int field = 0; field < 4; field++) {
			at = row.find_first_not_of(' ', at);
			if (at == std::string::npos) break;
			at = row.find(' ', at);
			if (at == std::string::npos) break;
		}
		if (at != std::string::npos) bytes += _strtoui64(row.c_str() + at, nullptr, 10);
	}
	if (entries > kMaxArchiveEntries || bytes > kMaxArchiveBytes) return "the archive is too large to install";
	return "";
}

std::string Extract(const std::string &archive, const std::string &dest, const ZipProgress &progress)
{
	ZipLimits limits;
	limits.maxEntries = kMaxArchiveEntries;
	limits.maxBytes = kMaxArchiveBytes;
	std::string ownError;
	if (IsZipFile(archive)) {
		ownError = ZipExtract(archive, dest, limits, progress);
		if (!ZipNotUnderstood(ownError)) return ownError;
	}
	else if (IsArchiveFile(archive)) {
		ownError = ArchiveExtract(archive, dest, limits, progress);
		if (!ArchiveNotUnderstood(ownError)) return ownError;
	}
	if (GetFileAttributesA(TarPath().c_str()) == INVALID_FILE_ATTRIBUTES) return !ownError.empty() ? ownError : "this system can only install .zip, .rar and .7z files, and this archive is in another format";
	const std::string bad = InspectArchive(archive);
	if (!bad.empty()) return bad;
	DWORD code = 1;
	if (!RunTar("-x -f \"" + archive + "\" -C \"" + dest + "\"", INVALID_HANDLE_VALUE, 30 * 60 * 1000, code)) {
		return "could not run tar.exe (Windows 10 or later required)";
	}
	return code == 0 ? "" : "could not extract (unsupported format or damaged file)";
}

std::mutex g_sourcesLock;

std::string SourcesFile() { return WorkDir() + "\\sources.json"; }

void SaveInstalledSource(const std::string &folder, const CatalogEntry &info)
{
	std::lock_guard<std::mutex> lock(g_sourcesLock);
	Json all;
	if (!ReadJsonFile(SourcesFile(), all) || all.type != Json::Object) all = Json::MakeObject();
	Json rec = Json::MakeObject();
	if (info.gbId && info.fileId) {
		rec.Set("gamebanana", Json::MakeNumber(static_cast<double>(info.gbId)));
		rec.Set("file", Json::MakeNumber(static_cast<double>(info.fileId)));
		rec.Set("date", Json::MakeNumber(static_cast<double>(info.fileDate)));
	}
	all.Set(folder, rec);
	MakeDirs(WorkDir());
	WriteJsonFile(SourcesFile(), all);
}

bool InstalledSource(const std::string &folder, uint64_t gbId, uint64_t &file, uint64_t &date)
{
	std::lock_guard<std::mutex> lock(g_sourcesLock);
	Json all;
	if (!ReadJsonFile(SourcesFile(), all) || all.type != Json::Object) return false;
	const Json *rec = all.Get(folder);
	if (!rec || static_cast<uint64_t>(rec->GetNumber("gamebanana")) != gbId) return false;
	file = static_cast<uint64_t>(rec->GetNumber("file"));
	date = static_cast<uint64_t>(rec->GetNumber("date"));
	return file != 0;
}

void ListRelativeFiles(const std::string &root, const std::string &rel, std::vector<std::string> &out)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((root + (rel.empty() ? "" : "\\" + rel) + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		const std::string name = fd.cFileName;
		if (name == "." || name == "..") continue;
		const std::string r = rel.empty() ? name : rel + "/" + name;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ListRelativeFiles(root, r, out);
		else out.push_back(Lower(r));
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

bool AcceptMapOnlyPack(const std::string &root, std::string &why)
{
	std::vector<std::string> paths;
	ListRelativeFiles(root, "", paths);
	paths.erase(std::remove(paths.begin(), paths.end(), "mod.json"), paths.end());
	Json manifest;
	if (ReadJsonFile(root + "\\mod.json", manifest) && manifest.type == Json::Object) {
		const Json *needs = manifest.Get("requires");
		if (ModsKindFromId(manifest.GetString("kind")) == kModRules || (needs && needs->type == Json::Array && !needs->arr.empty()) ||
			manifest.Get("commands")) {
			why = "the package brings more than maps";
			return false;
		}
	}
	if (!MapPackIsSimple(MapPackFacts(), paths)) {
		why = "the package brings more than maps";
		return false;
	}
	return true;
}

bool InstallArchiveNow(const std::string &archive, const CatalogEntry &info, const Json &sources, std::string &msg,
	const std::function<bool(const std::string &, std::string &)> &accept = nullptr)
{
	const std::string tmp = WorkDir() + "\\tmp-" + Timestamp();
	MakeDirs(tmp);
	msg = Extract(archive, tmp, [](uint64_t done, uint64_t total) {
		SetUnpack(true, done, total);
		return !g_cancel.load();
	});
	SetUnpack(false, 0, 0);
	if (!msg.empty()) {
		DeleteTree(tmp);
		return false;
	}
	const std::string root = FindModRoot(tmp);
	std::string bad;
	unsigned count = 0;
	CheckFiles(root, "", bad, count);
	if (count == 0) {
		DeleteTree(tmp);
		msg = "the file is empty";
		return false;
	}
	if (!bad.empty()) {
		DeleteTree(tmp);
		msg = Tr("not installed: it contains a disallowed file (") + bad + ")";
		return false;
	}
	PlaceLooseFiles(root, CatalogCategoryFromName(info.tags.empty() ? std::string() : info.tags.front()));
	PlaceLooseVideos(root);
	if (accept) {
		std::string why;
		if (!accept(root, why)) {
			DeleteTree(tmp);
			msg = why;
			return false;
		}
	}
	Json manifest;
	const std::string manifestPath = root + "\\mod.json";
	const bool hasManifest = ReadJsonFile(manifestPath, manifest) && manifest.type == Json::Object;
	if (!hasManifest) {
		manifest = Json::MakeObject();
		std::string base = archive.substr(archive.find_last_of('\\') + 1);
		base = base.substr(0, base.find_last_of('.'));
		manifest.Set("id", Json::MakeString(info.id.empty() ? Lower(SafeFolder(base)) : info.id));
		manifest.Set("name", Json::MakeString(info.name.empty() ? base : info.name));
		manifest.Set("version", Json::MakeString(info.version));
		Json authors = Json::MakeArray();
		for (const std::string &a : info.authors) authors.arr.push_back(Json::MakeString(a));
		manifest.Set("authors", authors);
		manifest.Set("description", Json::MakeString(info.description));
		manifest.Set("autoGenerated", Json::MakeBool(true));
	}
	if (sources.type == Json::Object && !manifest.Get("sources")) {
		manifest.Set("sources", sources);
	}
	else if (sources.type == Json::Object && GbKeyFromJson(sources)) {
		Json f = *manifest.Get("sources");
		if (f.type != Json::Object) f = Json::MakeObject();
		GbKeyToJson(f, GbKeyFromJson(sources));
		if (sources.Get("tags")) f.Set("tags", *sources.Get("tags"));
		if (!f.Get("page") && sources.Get("page")) f.Set("page", *sources.Get("page"));
		manifest.Set("sources", f);
	}
	WriteJsonFile(manifestPath, manifest);
	const std::string id = manifest.GetString("id");
	const std::string name = ModsLocalizedText(manifest, "name", id);
	const std::string version = manifest.GetString("version");
	std::string folder, best;
	for (const ModInfo &m : ModsList()) {
		const bool sameMod = (!id.empty() && _stricmp(m.id.c_str(), id.c_str()) == 0) || (info.gbId && m.gbId == info.gbId);
		if (!sameMod) continue;
		if (info.aside) {
			if (m.version == version && !info.otherCopy) {
				DeleteTree(tmp);
				msg = name + " " + version + Tr(" is already installed");
				return true;
			}
		}
		else if (info.replaceFolder.empty() && (folder.empty() || ModsCompareVersions(m.version, best) > 0)) {
			folder = m.folder;
			best = m.version;
		}
	}
	if (!info.replaceFolder.empty() && IsDir(ModsDir() + "\\" + info.replaceFolder)) folder = info.replaceFolder;
	if (folder.empty()) {
		const std::string base = SafeFolder(info.aside && !version.empty() ? name + " " + version : name);
		folder = base;
		for (int n = 2; IsDir(ModsDir() + "\\" + folder); n++) {
			folder = base + " (" + std::to_string(n) + ")";
		}
	}
	const std::string target = ModsDir() + "\\" + folder;
	bool pending = false;
	if (IsDir(target)) {
		const std::string backup = WorkDir() + "\\backup\\" + folder + "-" + Timestamp();
		MakeDirs(WorkDir() + "\\backup");
		if (!MoveFileExA(target.c_str(), backup.c_str(), 0)) {
			const std::string pend = WorkDir() + "\\pending\\" + folder;
			MakeDirs(WorkDir() + "\\pending");
			DeleteTree(pend);
			if (!MoveFileExA(root.c_str(), pend.c_str(), 0)) {
				DeleteTree(tmp);
				msg = Tr("could not prepare the update for ") + name;
				return false;
			}
			pending = true;
		}
		else {
			LogI("download", "previous version of %s saved in %s", folder.c_str(), backup.c_str());
		}
	}
	if (!pending && !MoveFileExA(root.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) {
		DeleteTree(tmp);
		msg = Tr("could not copy to the content folder (error ") + std::to_string(GetLastError()) + ")";
		return false;
	}
	DeleteTree(tmp);
	ModsCancelUninstall(folder);
	SaveInstalledSource(folder, info);
	ModsScan();
	const std::string label = name + (version.empty() || name.find(version) != std::string::npos ? "" : " " + version);
	bool inUse = false;
	for (const std::string &f : ModsMountedProfile().mods) inUse = inUse || _stricmp(f.c_str(), folder.c_str()) == 0;
	inUse = inUse && !ModsLoadsWithoutRestart(folder);
	msg = pending ? TrFmt("%s downloaded (applied on restart)", {label}) : inUse ? TrFmt("%s installed (applied on restart)", {label}) : TrFmt("%s installed", {label});
	return true;
}

std::string DownloadToTemp(const std::string &url, uint64_t expectedBytes, const std::string &nameHint, bool allowOtherServers, std::string &file,
	bool &usedOtherServers)
{
	usedOtherServers = false;
	MakeDirs(WorkDir());
	std::string ext = Lower(url.substr(0, url.find('?')));
	ext = ext.size() > 4 && ext.find_last_of('.') != std::string::npos ? ext.substr(ext.find_last_of('.')) : ".zip";
	if (ext.size() > 8 || ext.find('/') != std::string::npos) ext = ".zip";
	file = WorkDir() + "\\" + SafeFolder(nameHint) + "-" + Timestamp() + ext;
	DownloadHooks hooks;
	hooks.progress = SetProgress;
	hooks.cancelled = [] { return g_cancel.load(); };
	return DownloadFile(url, expectedBytes, file, GameDir() + "\\imperivm-resurrected\\_cache", allowOtherServers, hooks, usedOtherServers);
}

void RunInstall(const CatalogEntry &e, const Json &sources)
{
	std::string file;
	bool usedOtherServers = false;
	std::string err = DownloadToTemp(e.url, e.bytes, e.name.empty() ? "mod" : e.name, true, file, usedOtherServers);
	if (err.empty() && usedOtherServers && !e.md5.empty() && HashFile(file, BCRYPT_MD5_ALGORITHM) != e.md5) {
		LogW("download", "gamebanana: the MD5 hash does not match after changing server; downloading again from the assigned server only");
		DeleteFileA(file.c_str());
		err = DownloadToTemp(e.url, e.bytes, e.name.empty() ? "mod" : e.name, false, file, usedOtherServers);
	}
	if (err.empty() && !e.sha256.empty() && HashFile(file, BCRYPT_SHA256_ALGORITHM) != e.sha256) {
		err = "the SHA-256 hash does not match the catalog's (file tampered with or damaged)";
	}
	if (err.empty() && !e.md5.empty() && HashFile(file, BCRYPT_MD5_ALGORITHM) != e.md5) {
		err = "the MD5 hash does not match (damaged file)";
	}
	if (!err.empty()) {
		if (!file.empty()) DeleteFileA(file.c_str());
		Finish(false, TrFmt("Could not download %s: %s", {e.name.empty() ? e.url : e.name, Tr(err)}));
		return;
	}
	std::string msg;
	const bool ok = InstallArchiveNow(file, e, sources, msg);
	DeleteFileA(file.c_str());
	Finish(ok, ok ? msg : Tr("Could not install ") + (e.name.empty() ? e.url : e.name) + ": " + msg);
}

void Worker()
{
	for (;;) {
		Job job;
		{
			std::unique_lock<std::mutex> lock(g_lock);
			g_cv.wait(lock, [] { return !g_jobs.empty(); });
			job = g_jobs.front();
			g_jobs.pop_front();
			g_status.active = true;
			g_status.done = g_status.total = 0;
			g_status.unpacking = false;
			g_status.unpacked = g_status.unpackTotal = 0;
			g_status.queued = static_cast<int>(g_jobs.size());
			g_status.what = job.kind != Job::Archive
				? job.entry.name + (job.entry.version.empty() || job.entry.name.find(job.entry.version) != std::string::npos ? "" : " " + job.entry.version)
				: job.text;
		}
		g_cancel = false;
		LogI("download", "starting %s", g_status.what.c_str());
		if (job.kind == Job::Entry) {
			Json sources = Json::MakeObject();
			if (!job.entry.page.empty()) sources.Set("page", Json::MakeString(job.entry.page));
			GbKeyToJson(sources, job.entry.gbId);
			if (!job.entry.tags.empty()) {
				Json tags = Json::MakeArray();
				for (const std::string &t : job.entry.tags) tags.arr.push_back(Json::MakeString(t));
				sources.Set("tags", tags);
			}
			RunInstall(job.entry, sources);
		}
		else if (job.kind == Job::RoomPack) {
			Json sources = Json::MakeObject();
			GbKeyToJson(sources, job.entry.gbId);
			std::string msg;
			const bool ok = InstallArchiveNow(job.text, job.entry, sources, msg, AcceptMapOnlyPack);
			DeleteFileA(job.text.c_str());
			if (ok) Finish(true, msg);
			else LogI("download", "the package of the room's map was not installed (%s): only the map is kept", TrSource(msg).c_str());
		}
		else if (job.kind == Job::Archive) {
			std::string msg;
			const bool ok = InstallArchiveNow(job.text, CatalogEntry(), Json(), msg);
			Finish(ok, ok ? msg : Tr("Could not install ") + job.text + ": " + msg);
		}
		std::lock_guard<std::mutex> lock(g_lock);
		g_status.active = false;
		g_status.queued = static_cast<int>(g_jobs.size());
	}
}

void Enqueue(const Job &job)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_workerStarted) {
		g_workerStarted = true;
		SafeThread(Worker).detach();
	}
	g_jobs.push_back(job);
	g_status.queued = static_cast<int>(g_jobs.size());
	g_cv.notify_one();
}

}

uint64_t DlInstalledGbFile(const ModInfo &m)
{
	uint64_t file = 0, date = 0;
	return m.gbId && InstalledSource(m.folder, m.gbId, file, date) ? file : 0;
}

void DlRefreshCatalogs()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_catalogBusy) return;
		g_catalogBusy = true;
	}
	SafeThread(RefreshCatalogsNow).detach();
}

void DlStartUpdateCheck()
{
	SafeThread([] {
		Sleep(6000);
		const int64_t now = GbCacheNow();
		GbCachePrune(CacheRoot() + "\\img", 200ull << 20, 30 * 86400, now);
		GbCachePrune(CacheRoot() + "\\api", 20ull << 20, 7 * 86400, now);
	}).detach();
	if (IniGet("Mods", "UpdateNotices") == "0") return;
	SafeThread([] {
		Sleep(8000);
		g_catalogTtl = kStartupTtl;
		DlRefreshCatalogs();
		for (int i = 0; i < 120; i++) {
			Sleep(500);
			std::lock_guard<std::mutex> lock(g_lock);
			if (!g_catalogBusy) break;
		}
		g_catalogTtl = kDetailTtl;
		const std::vector<ModInfo> mods = ModsList();
		std::vector<std::string> news;
		for (const ModInfo &m : mods) {
			CatalogEntry e;
			if (DlUpdateFor(m, mods, e)) news.push_back(m.name + " " + e.version);
		}
		if (news.empty()) {
			LogI("download", "mods up to date");
			return;
		}
		std::string text;
		if (news.size() == 1) {
			text = TrFmt("Update available: %s", {news[0]});
		}
		else {
			text = Tr("Updates available:");
			for (size_t i = 0; i < news.size() && i < 4; i++) text += "\n" + news[i];
			if (news.size() > 4) text += "\n" + TrFmt("and %s more", {std::to_string(news.size() - 4)});
		}
		UiToast(text);
		UiRefreshMainMenuButton();
	}).detach();
}

bool DlFindUpdate(const std::string &modId, const std::string &installedVersion, CatalogEntry &out, uint64_t gbId)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const CatalogEntry &e : g_catalog) {
		const bool same = gbId && e.gbId ? e.gbId == gbId : _stricmp(e.id.c_str(), modId.c_str()) == 0;
		if (same && UpdateIsNewer(e.version, installedVersion)) {
			out = e;
			return true;
		}
	}
	return false;
}

void DlInstallEntry(const CatalogEntry &e)
{
	Job j{Job::Entry, e, ""};
	Enqueue(j);
}

void DlInstallArchive(const std::string &path)
{
	Job j{Job::Archive, CatalogEntry(), path};
	Enqueue(j);
}

std::string DlPickArchive()
{
	wchar_t file[MAX_PATH] = {};
	OPENFILENAMEW ofn = {sizeof(ofn)};
	ofn.hwndOwner = GetActiveWindow();
	std::wstring filter = Wide(Tr("Compressed content") + " (*.zip;*.7z;*.rar;*.tar;*.gz)");
	filter.push_back(L'\0');
	filter += L"*.zip;*.7z;*.rar;*.tar;*.gz;*.tgz";
	filter.push_back(L'\0');
	filter += Wide(Tr("All files"));
	filter.push_back(L'\0');
	filter += L"*.*";
	filter.push_back(L'\0');
	filter.push_back(L'\0');
	const std::wstring title = Wide(Tr("Install content from a file"));
	ofn.lpstrFilter = filter.c_str();
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title.c_str();
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&ofn)) return "";
	return Narrow(file, CP_ACP);
}

DownloadStatus DlStatus()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_status;
}

void DlCancel()
{
	g_cancel = true;
}

void DlApplyPendingInstalls()
{
	const std::string pendDir = WorkDir() + "\\pending";
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((pendDir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		std::string n = fd.cFileName;
		if (n == "." || n == ".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		const std::string target = ModsDir() + "\\" + n;
		if (IsDir(target)) {
			MakeDirs(WorkDir() + "\\backup");
			MoveFileExA(target.c_str(), (WorkDir() + "\\backup\\" + n + "-" + Timestamp()).c_str(), 0);
		}
		if (MoveFileExA((pendDir + "\\" + n).c_str(), target.c_str(), 0)) {
			LogI("download", "applied the pending update of %s", n.c_str());
		}
		else {
			LogW("download", "could not apply the pending update of %s (error %lu)", n.c_str(), GetLastError());
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

void DlCleanLeftovers()
{
	const std::string dir = WorkDir();
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	unsigned removed = 0;
	uint64_t bytes = 0;
	do {
		const std::string n = fd.cFileName;
		const bool folder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		if (!IsDownloadLeftover(n, folder)) continue;
		const std::string path = dir + "\\" + n;
		if (folder) {
			DeleteTree(path);
			if (!IsDir(path)) removed++;
		}
		else if (DeleteFileA(path.c_str())) {
			removed++;
			bytes += (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		}
		if (folder ? IsDir(path) : IsFile(path)) LogW("download", "could not remove the leftover %s (error %lu)", n.c_str(), GetLastError());
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	if (removed) LogI("download", "removed %u leftovers of interrupted downloads (%.1f MB)", removed, bytes / 1048576.0);
}

namespace {

std::mutex g_ioLock;
GbResults g_ioResults;
uint64_t g_ioWanted = 0;

std::vector<GbMod> g_gbAll;
DWORD g_gbAllStamp = 0;
bool g_gbAllValid = false;
const DWORD kGbListMs = 60 * 1000;

std::map<std::string, std::shared_ptr<const GbImage>> g_ioImages;
std::deque<std::string> g_ioImageQueue;
std::deque<std::string> g_bigOrder;
int g_ioImageThreads = 0;
const int kImageWorkers = 3;
std::condition_variable g_ioImageCv;

std::string Fold(const std::string &s)
{
	std::string out = Lower(s);
	static const struct {
		const char *from, *to;
	} kMap[] = {{"\xC3\xA1", "a"}, {"\xC3\xA9", "e"}, {"\xC3\xAD", "i"}, {"\xC3\xB3", "o"}, {"\xC3\xBA", "u"}, {"\xC3\xBC", "u"},
		{"\xC3\xB1", "n"}, {"\xC3\x81", "a"}, {"\xC3\x89", "e"}, {"\xC3\x8D", "i"}, {"\xC3\x93", "o"}, {"\xC3\x9A", "u"}, {"\xC3\x91", "n"}};
	for (const auto &m : kMap) {
		for (size_t at = out.find(m.from); at != std::string::npos; at = out.find(m.from, at)) out.replace(at, strlen(m.from), m.to);
	}
	return out;
}

void RunSearch(GbQuery q, uint64_t serial)
{
	GbResults r;
	r.query = q;
	r.serial = serial;
	std::vector<GbMod> all;
	std::string err;
	{
		std::lock_guard<std::mutex> lock(g_ioLock);
		if (g_gbAllValid && GetTickCount() - g_gbAllStamp < kGbListMs) all = g_gbAll;
	}
	if (all.empty()) {
		err = GbListAll(all);
		if (err.empty()) {
			std::lock_guard<std::mutex> lock(g_ioLock);
			g_gbAll = all;
			g_gbAllStamp = GetTickCount();
			g_gbAllValid = true;
		}
	}
	if (!err.empty()) {
		r.error = err;
		LogW("download", "gamebanana: could not read the mod list (%s)", err.c_str());
	}
	else {
		const std::string search = Fold(q.search);
		std::vector<GbMod> found;
		for (const GbMod &m : all) {
			if (!search.empty()) {
				std::string hay = Fold(m.name + " " + m.author);
				for (const std::string &t : m.tags) hay += " " + Fold(t);
				const uint32_t categories = CatalogCategoriesOf(m.facts);
				for (int c = 0; c < kCatCount; c++) {
					if ((categories >> c) & 1) hay += " " + Fold(Tr(CatalogCategoryLabel(c)));
				}
				if (hay.find(search) == std::string::npos) continue;
			}
			CatalogCount(m.facts, r.counts);
			if (CatalogMatches(m.facts, q.filter)) found.push_back(m);
		}
		std::stable_sort(found.begin(), found.end(), [&](const GbMod &a, const GbMod &b) {
			switch (q.sort) {
			case 1: return a.likes != b.likes ? a.likes > b.likes : a.views > b.views;
			case 2: return a.added > b.added;
			case 3: return a.updated > b.updated;
			default: return a.views != b.views ? a.views > b.views : a.likes > b.likes;
			}
		});
		r.total = static_cast<int>(found.size());
		const size_t from = static_cast<size_t>(std::max(0, q.page * q.perPage));
		for (size_t i = from; i < found.size() && i < from + static_cast<size_t>(q.perPage); i++) r.mods.push_back(found[i]);
		std::vector<std::thread> threads;
		for (GbMod &m : r.mods) {
			threads.push_back(SafeThread([&m] {
				GbMod full = m;
				if (GbFetchMod(m.id, full).empty()) m = full;
			}));
		}
		for (std::thread &t : threads) t.join();
	}
	std::lock_guard<std::mutex> lock(g_ioLock);
	if (serial == g_ioWanted) g_ioResults = std::move(r);
}

std::shared_ptr<GbImage> DecodeImage(const std::string &bytes, UINT maxWidth = 320)
{
	std::shared_ptr<GbImage> out;
	IWICImagingFactory *factory = nullptr;
	IWICStream *stream = nullptr;
	IWICBitmapDecoder *decoder = nullptr;
	IWICBitmapFrameDecode *frame = nullptr;
	IWICBitmapScaler *scaler = nullptr;
	IWICFormatConverter *conv = nullptr;
	UINT w = 0, h = 0;
	if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		SUCCEEDED(factory->CreateStream(&stream)) &&
		SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())), static_cast<DWORD>(bytes.size()))) &&
		SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
		SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h) {
		UINT tw = w, th = h;
		if (tw > maxWidth) {
			th = std::max<UINT>(1, th * maxWidth / tw);
			tw = maxWidth;
		}
		IWICBitmapSource *src = frame;
		if ((tw != w || th != h) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
			SUCCEEDED(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant))) {
			src = scaler;
		}
		else {
			tw = w;
			th = h;
		}
		if (SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
			SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
			auto img = std::make_shared<GbImage>();
			img->width = static_cast<int>(tw);
			img->height = static_cast<int>(th);
			img->bgra.resize(static_cast<size_t>(tw) * th);
			if (SUCCEEDED(conv->CopyPixels(nullptr, tw * 4, static_cast<UINT>(img->bgra.size() * 4), reinterpret_cast<BYTE *>(img->bgra.data())))) {
				out = img;
			}
		}
	}
	if (conv) conv->Release();
	if (scaler) scaler->Release();
	if (frame) frame->Release();
	if (decoder) decoder->Release();
	if (stream) stream->Release();
	if (factory) factory->Release();
	return out;
}

void ImageWorker();

void StartImageWorkers()
{
	while (g_ioImageThreads < kImageWorkers) {
		g_ioImageThreads++;
		SafeThread(ImageWorker).detach();
	}
}

void ImageWorker()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	for (;;) {
		std::string url;
		{
			std::unique_lock<std::mutex> lock(g_ioLock);
			g_ioImageCv.wait(lock, [] { return !g_ioImageQueue.empty(); });
			url = g_ioImageQueue.front();
			g_ioImageQueue.pop_front();
		}
		std::string body;
		std::shared_ptr<GbImage> img;
		const bool big = url.compare(0, 2, "B|") == 0;
		const std::string real = big ? url.substr(2) : url;
		const UINT width = big ? 1600 : 320;
		const std::string dir = CacheRoot() + "\\img";
		bool stale = false;
		static std::atomic<int> fromDisk{0}, fromNet{0};
		std::shared_ptr<GbImage> saved;
		if (GbCacheRead(dir, real, 60LL * 86400, body, stale, GbCacheNow())) {
			img = DecodeImage(body, width);
			if (!img) GbCacheRemove(dir, real);
			else if (stale) {
				saved = img;
				img.reset();
			}
			else fromDisk++;
		}
		if (!img) {
			body.clear();
			if (HttpGet(real, {}, &body, "").empty()) {
				img = DecodeImage(body, width);
				if (img) {
					GbCacheWrite(dir, real, body, GbCacheNow());
					fromNet++;
				}
			}
		}
		if (!img && saved) img = saved;
		static std::atomic<int> logged{0};
		const int shown = fromDisk + fromNet;
		if (shown >= logged + 8) {
			logged = shown;
			LogI("download", "images of GameBanana: %d from the disk, %d downloaded", fromDisk.load(), fromNet.load());
		}
		if (!img) {
			static int logged = 0;
			if (logged++ < 3) LogW("download", "gamebanana: could not show the image %s", url.c_str());
			img = std::make_shared<GbImage>();
		}
		std::lock_guard<std::mutex> lock(g_ioLock);
		g_ioImages[url] = img;
		if (big) {
			g_bigOrder.push_back(url);
			while (g_bigOrder.size() > 4) {
				g_ioImages.erase(g_bigOrder.front());
				g_bigOrder.pop_front();
			}
		}
	}
}

bool InstalledGb(uint64_t id)
{
	for (const ModInfo &m : ModsList()) {
		if (m.gbId == id) return true;
	}
	return false;
}

void InstallGbDependencies(const GbMod &m, int depth = 0)
{
	if (depth > 4) return;
	for (uint64_t depId : m.deps) {
		if (InstalledGb(depId)) continue;
		GbMod dep;
		const std::string err = GbFetchMod(depId, dep, kFreshTtl);
		if (!err.empty() || !dep.hasFile) {
			LogW("download", "gamebanana: %s needs %s, which could not be requested (%s)", m.name.c_str(), GbKeyText(depId).c_str(),
				err.empty() ? "no file" : err.c_str());
			continue;
		}
		InstallGbDependencies(dep, depth + 1);
		LogI("download", "gamebanana: %s needs %s; it is installed too", m.name.c_str(), dep.name.c_str());
		DlInstallEntry(dep.entry);
	}
}

void RunGbInstall(GbMod m)
{
	{
		GbMod fresh = m;
		const std::string err = GbFetchMod(m.id, fresh, kFreshTtl);
		if (err.empty() && fresh.hasFile) {
			m = fresh;
		}
		else if (!m.details || !m.hasFile) {
			Finish(false, TrFmt("Could not request %s from GameBanana", {m.name}) + (err.empty() ? "" : ": " + Tr(err)));
			return;
		}
	}
	InstallGbDependencies(m);
	DlInstallEntry(m.entry);
}

struct FilesEntry {
	std::shared_ptr<GbFiles> files;
	DWORD stamp = 0;
};
std::map<uint64_t, FilesEntry> g_ioFiles;

void FetchGbFiles(uint64_t id, int64_t ttl = kDetailTtl)
{
	auto r = std::make_shared<GbFiles>();
	r->loading = false;
	Json j;
	std::string err = HttpGetJsonCached(std::string(GbApiBase()) + GbApiPath(id) + "/ProfilePage", ttl, j);
	if (err.empty() && !j.Get("_idRow")) err = j.GetString("_sErrorCode", "empty response");
	if (!err.empty()) {
		r->error = err;
		LogW("download", "gamebanana: could not read the versions of %s (%s)", GbKeyText(id).c_str(), err.c_str());
	}
	else {
		const std::string modVersion = j.GetString("_sVersion");
		if (const Json *files = j.Get("_aFiles")) {
			for (const Json &f : files->arr) {
				if (f.GetBool("_bIsArchived", false)) continue;
				GbFile file;
				file.id = static_cast<uint64_t>(f.GetNumber("_idRow"));
				file.version = f.GetString("_sVersion");
				file.bytes = static_cast<uint64_t>(f.GetNumber("_nFilesize"));
				file.date = static_cast<uint64_t>(f.GetNumber("_tsDateAdded"));
				file.md5 = Lower(f.GetString("_sMd5Checksum"));
				file.unversioned = file.version.empty();
				if (file.unversioned) file.version = GbFileVersion(f, modVersion, GbUsableFiles(files));
				if (file.id) r->files.push_back(file);
			}
		}
		std::sort(r->files.begin(), r->files.end(), [](const GbFile &a, const GbFile &b) { return a.date > b.date; });
		Json u;
		if (HttpGetJsonCached(std::string(GbApiBase()) + GbApiPath(id) + "/Updates?_nPage=1&_nPerpage=50", ttl, u).empty()) {
			if (const Json *recs = u.Get("_aRecords")) {
				for (GbFile &f : r->files) {
					for (const Json &up : recs->arr) {
						const std::string name = up.GetString("_sName");
						if (!TitleHasVersion(name, f.version)) continue;
						std::string text = StripHtml(up.GetString("_sText"));
						if (const Json *cl = up.Get("_aChangeLog")) {
							for (const Json &c : cl->arr) {
								const std::string line = c.GetString("text", c.GetString("_sText"));
								if (!line.empty()) text += std::string(text.empty() ? "" : "\n") + "\xE2\x80\xA2 " + line;
							}
						}
						f.changelog = text;
						break;
					}
				}
			}
		}
	}
	std::lock_guard<std::mutex> lock(g_ioLock);
	g_ioFiles[id].files = r;
	g_ioFiles[id].stamp = GetTickCount();
}

void RunGbInstallVersion(GbMod m, GbFile f)
{
	if (!m.details) GbFetchMod(m.id, m, kFreshTtl);
	CatalogEntry e = m.entry;
	e.version = f.version;
	e.bytes = f.bytes;
	e.url = "https://gamebanana.com/dl/" + std::to_string(f.id);
	e.md5 = f.md5;
	e.fileId = f.id;
	e.fileDate = f.date;
	e.aside = true;
	InstallGbDependencies(m);
	DlInstallEntry(e);
}

}

GbFiles GbGetFiles(uint64_t modId)
{
	std::lock_guard<std::mutex> lock(g_ioLock);
	FilesEntry &fe = g_ioFiles[modId];
	if (fe.files && (fe.files->error.empty() || GetTickCount() - fe.stamp < 5000)) return *fe.files;
	if (!fe.files || !fe.files->loading) {
		auto pending = std::make_shared<GbFiles>();
		fe.files = pending;
		fe.stamp = GetTickCount();
		SafeThread(FetchGbFiles, modId, kDetailTtl).detach();
	}
	return *fe.files;
}

bool GbChangelog(uint64_t modId, const std::string &version, std::string &text, std::string &foundVersion)
{
	GbGetFiles(modId);
	std::lock_guard<std::mutex> lock(g_ioLock);
	const auto it = g_ioFiles.find(modId);
	if (it == g_ioFiles.end() || !it->second.files || it->second.files->loading) return false;
	const GbFile *pick = nullptr;
	if (version.empty()) {
		pick = GbLatestFile(it->second.files->files);
	}
	else {
		for (const GbFile &f : it->second.files->files) {
			if (CompareVersions(f.version, version) == 0) {
				pick = &f;
				break;
			}
		}
	}
	if (!pick || pick->changelog.empty()) return false;
	text = pick->changelog;
	foundVersion = pick->version;
	return true;
}

const GbFile *GbLatestFile(const std::vector<GbFile> &files)
{
	const GbFile *best = nullptr;
	for (const GbFile &f : files) {
		if (!best || FileIsNewer(f.unversioned ? "" : f.version, f.date, best->unversioned ? "" : best->version, best->date)) best = &f;
	}
	return best;
}

void GbInstallVersion(const GbMod &m, const GbFile &f)
{
	SafeThread(RunGbInstallVersion, m, f).detach();
}

void GbSearch(const GbQuery &q)
{
	uint64_t serial;
	{
		std::lock_guard<std::mutex> lock(g_ioLock);
		serial = ++g_ioWanted;
		g_ioResults.loading = true;
		g_ioResults.error.clear();
		g_ioResults.query = q;
	}
	SafeThread(RunSearch, q, serial).detach();
}

void GbForceRefresh()
{
	const DWORD now = GetTickCount();
	const DWORD last = g_lastForce;
	if (last && now - last < 5000) return;
	g_lastForce = now;
	g_forceUntil = now + 30000;
	std::lock_guard<std::mutex> lock(g_ioLock);
	g_gbAllValid = false;
}

GbResults GbGetResults()
{
	std::lock_guard<std::mutex> lock(g_ioLock);
	GbResults r = g_ioResults;
	r.loading = r.serial != g_ioWanted;
	return r;
}

std::shared_ptr<const GbImage> GbBigImage(const std::string &url)
{
	if (url.compare(0, 8, "https://") != 0) return nullptr;
	const std::string key = "B|" + url;
	std::lock_guard<std::mutex> lock(g_ioLock);
	const auto it = g_ioImages.find(key);
	if (it != g_ioImages.end()) return it->second && it->second->width ? it->second : nullptr;
	g_ioImages[key] = nullptr;
	g_ioImageQueue.push_back(key);
	StartImageWorkers();
	g_ioImageCv.notify_one();
	return nullptr;
}

std::shared_ptr<const GbImage> GbLogo(const std::string &url)
{
	if (url.compare(0, 8, "https://") != 0) return nullptr;
	std::lock_guard<std::mutex> lock(g_ioLock);
	const auto it = g_ioImages.find(url);
	if (it != g_ioImages.end()) return it->second && it->second->width ? it->second : nullptr;
	g_ioImages[url] = nullptr;
	g_ioImageQueue.push_back(url);
	StartImageWorkers();
	g_ioImageCv.notify_one();
	return nullptr;
}

namespace {
bool NewerFileOfSameVersion(const ModInfo &m, CatalogEntry &out)
{
	uint64_t file = 0, date = 0;
	if (!m.gbId || !InstalledSource(m.folder, m.gbId, file, date)) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const CatalogEntry &e : g_catalog) {
		if (e.gbId != m.gbId) continue;
		if (!GbSameVersionNewFile(e.fileCount, e.fileId, e.fileDate, file, date)) return false;
		out = e;
		return true;
	}
	return false;
}
}

bool DlUpdateFor(const ModInfo &m, const std::vector<ModInfo> &installed, CatalogEntry &out)
{
	if (!DlFindUpdate(m.id, m.version, out, m.gbId) && !NewerFileOfSameVersion(m, out)) return false;
	for (const ModInfo &o : installed) {
		if (o.folder != m.folder && ModsSameMod(o, m) && !UpdateIsNewer(out.version, o.version)) return false;
	}
	return true;
}

void DlUpdateMod(const std::string &folder, const CatalogEntry &e)
{
	SafeThread([folder, e] {
		CatalogEntry entry = e;
		entry.replaceFolder = folder;
		if (entry.gbId) {
			GbMod g;
			if (GbFetchMod(entry.gbId, g, kFreshTtl).empty()) {
				InstallGbDependencies(g);
				if (g.hasFile) {
					const std::string localId = entry.id;
					entry = g.entry;
					entry.id = localId;
					entry.replaceFolder = folder;
					entry.aside = e.aside;
				}
			}
		}
		DlInstallEntry(entry);
	}).detach();
}

void GbInstall(const GbMod &m)
{
	if (!m.hasFile) return;
	SafeThread(RunGbInstall, m).detach();
}

namespace {
std::map<uint64_t, std::shared_ptr<const GbMod>> g_ioInfo;
std::map<uint64_t, DWORD> g_ioInfoAsked;
const DWORD kGbInfoRetryMs = 30000;

void FetchGbInfo(uint64_t id)
{
	auto m = std::make_shared<GbMod>();
	if (!GbFetchMod(id, *m).empty()) m = std::make_shared<GbMod>();
	std::lock_guard<std::mutex> lock(g_ioLock);
	g_ioInfo[id] = m;
}
}

std::shared_ptr<const GbMod> GbInfo(uint64_t id)
{
	if (!id) return nullptr;
	std::lock_guard<std::mutex> lock(g_ioLock);
	const auto it = g_ioInfo.find(id);
	if (it != g_ioInfo.end() && it->second && it->second->id) return it->second;
	if (it != g_ioInfo.end() && GetTickCount() - g_ioInfoAsked[id] < kGbInfoRetryMs) return nullptr;
	g_ioInfo[id] = nullptr;
	g_ioInfoAsked[id] = GetTickCount();
	SafeThread(FetchGbInfo, id).detach();
	return nullptr;
}

namespace {
std::mutex g_roomLock;
RoomModStatus g_room;
std::atomic<bool> g_roomCancel{false};

bool IsRoomVersion(const ModInfo &m, const RoomModRequest &r)
{
	return ((r.gbId && m.gbId == r.gbId) || _stricmp(m.id.c_str(), r.id.c_str()) == 0) && m.version == r.version;
}

bool FindInstalledCopy(const RoomModRequest &r, std::string &folder, const std::set<std::string> &skip = {})
{
	for (const ModInfo &m : ModsList()) {
		if (IsRoomVersion(m, r) && !skip.count(m.folder) && ModsCompareRoomCopy(m, r.copyKey) != RoomCopy::kOther) {
			folder = m.folder;
			return true;
		}
	}
	return false;
}

void RoomFail(const std::string &id, const std::string &why)
{
	LogW("content-share", "room mods: %s could not be downloaded from GameBanana (%s); it will be requested from the host", id.c_str(), why.c_str());
	std::lock_guard<std::mutex> lock(g_roomLock);
	g_room.failed.push_back(id);
	g_room.note = why;
}

void RoomModsThread(std::vector<RoomModRequest> reqs)
{
	uint64_t doneBytes = 0;
	for (size_t i = 0; i < reqs.size() && !g_roomCancel; i++) {
		const RoomModRequest &r = reqs[i];
		{
			std::lock_guard<std::mutex> lock(g_roomLock);
			g_room.index = static_cast<int>(i) + 1;
			g_room.what = r.id;
		}
		std::string folder;
		if (FindInstalledCopy(r, folder)) {
			std::lock_guard<std::mutex> lock(g_roomLock);
			g_room.installed[r.id] = folder;
			continue;
		}
		std::set<std::string> otherCopies;
		for (const ModInfo &m : ModsList()) {
			if (IsRoomVersion(m, r)) otherCopies.insert(m.folder);
		}
		if (!r.gbId) {
			RoomFail(r.id, "not installed from GameBanana");
			continue;
		}
		const GbFiles files = [&] {
			FetchGbFiles(r.gbId, kFreshTtl);
			std::lock_guard<std::mutex> lock(g_ioLock);
			return g_ioFiles[r.gbId].files ? *g_ioFiles[r.gbId].files : GbFiles();
		}();
		const GbFile *file = nullptr;
		for (const GbFile &f : files.files) {
			if (r.gbFile && f.id == r.gbFile) file = &f;
		}
		for (const GbFile &f : files.files) {
			if (!file && CompareVersions(f.version, r.version) == 0) file = &f;
		}
		if (!file) {
			RoomFail(r.id, !files.error.empty() ? files.error : "GameBanana does not have version " + r.version);
			continue;
		}
		bool haveThatFile = false;
		for (const ModInfo &m : ModsList()) haveThatFile = haveThatFile || (otherCopies.count(m.folder) && DlInstalledGbFile(m) == file->id);
		if (haveThatFile) {
			RoomFail(r.id, "the file of GameBanana is the copy already installed, not the host's");
			continue;
		}
		GbMod mod;
		GbFetchMod(r.gbId, mod, kFreshTtl);
		CatalogEntry e = mod.entry;
		e.source = "GameBanana";
		e.gbId = r.gbId;
		e.id = r.id;
		if (e.name.empty()) e.name = r.id;
		e.version = r.version;
		e.bytes = file->bytes;
		e.url = "https://gamebanana.com/dl/" + std::to_string(file->id);
		e.md5 = file->md5;
		e.fileId = file->id;
		e.fileDate = file->date;
		e.aside = true;
		e.otherCopy = !otherCopies.empty();
		const uint64_t bytes = file->bytes;
		uint64_t before;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			before = g_status.finished;
		}
		LogI("content-share", "room mods: %s %s is downloaded from GameBanana (%.1f MB)", e.name.c_str(), r.version.c_str(), bytes / 1048576.0);
		DlInstallEntry(e);
		const DWORD t0 = GetTickCount();
		for (;;) {
			Sleep(120);
			bool finished;
			{
				std::lock_guard<std::mutex> lock(g_lock);
				finished = g_status.finished > before;
				std::lock_guard<std::mutex> lock2(g_roomLock);
				g_room.got = doneBytes + g_status.done;
				g_room.total = std::max(doneBytes + (g_status.total ? g_status.total : bytes), g_room.got);
				g_room.unpacking = g_status.unpacking;
				g_room.unpacked = g_status.unpacked;
				g_room.unpackTotal = g_status.unpackTotal;
			}
			if (finished || g_roomCancel || GetTickCount() - t0 > 20 * 60 * 1000) break;
		}
		doneBytes += bytes;
		if (g_roomCancel) break;
		if (FindInstalledCopy(r, folder, otherCopies)) {
			std::lock_guard<std::mutex> lock(g_roomLock);
			g_room.installed[r.id] = folder;
		}
		else {
			RoomFail(r.id, "installation failed for version " + r.version);
		}
	}
	std::lock_guard<std::mutex> lock(g_roomLock);
	g_room.active = false;
	g_room.done = !g_roomCancel;
}
}

void RoomModsFromGb(const std::vector<RoomModRequest> &mods)
{
	g_roomCancel = false;
	{
		std::lock_guard<std::mutex> lock(g_roomLock);
		g_room = RoomModStatus();
		g_room.active = true;
		g_room.count = static_cast<int>(mods.size());
	}
	SafeThread(RoomModsThread, mods).detach();
}

RoomModStatus RoomModsStatus()
{
	std::lock_guard<std::mutex> lock(g_roomLock);
	return g_room;
}

void RoomModsCancel()
{
	g_roomCancel = true;
	DlCancel();
	std::lock_guard<std::mutex> lock(g_roomLock);
	g_room.active = false;
}

std::string DlHttpGet(const std::string &url, std::string *toMemory, const std::string &toFile)
{
	return HttpGet(url, {}, toMemory, toFile);
}

std::string DlHttpGetJson(const std::string &url, Json &out)
{
	return HttpGetJson(url, {}, out);
}

std::string DlExtract(const std::string &archive, const std::string &dest)
{
	return Extract(archive, dest, nullptr);
}

std::string DlFileSha256(const std::string &path)
{
	return HashFile(path, BCRYPT_SHA256_ALGORITHM);
}

namespace {
bool FindFileNamed(const std::string &dir, const std::string &leaf, std::string &found)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	bool ok = false;
	do {
		const std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (FindFileNamed(dir + "\\" + n, leaf, found)) ok = true;
		}
		else if (_stricmp(n.c_str(), leaf.c_str()) == 0) {
			found = dir + "\\" + n;
			ok = true;
		}
	} while (!ok && FindNextFileA(h, &fd));
	FindClose(h);
	return ok;
}
}

std::string DlFetchRoomMap(uint64_t gbId, const std::string &version, const std::string &packId, const std::string &packName, const std::string &leaf,
	const std::string &outFile, const std::function<void(uint64_t, uint64_t)> &progress, const std::function<bool()> &cancelled)
{
	const uint64_t kMaxBytes = 120ull << 20;
	if (!gbId) return "the package is not from GameBanana";
	FetchGbFiles(gbId, kFreshTtl);
	GbFiles files;
	{
		std::lock_guard<std::mutex> lock(g_ioLock);
		if (g_ioFiles[gbId].files) files = *g_ioFiles[gbId].files;
	}
	const GbFile *file = nullptr;
	for (const GbFile &f : files.files) {
		if (CompareVersions(f.version, version) == 0) {
			file = &f;
			break;
		}
	}
	if (!file) return !files.error.empty() ? files.error : "GameBanana does not have version " + version;
	if (file->bytes > kMaxBytes) return "the package is too big to download only for its map";
	MakeDirs(WorkDir());
	const std::string tmp = WorkDir() + "\\map-" + Timestamp();
	const std::string archive = tmp + ".zip";
	MakeDirs(tmp);
	auto cleanup = [&](bool keepArchive) {
		if (!keepArchive) DeleteFileA(archive.c_str());
		DeleteTree(tmp);
	};
	DownloadHooks hooks;
	hooks.progress = progress;
	hooks.cancelled = cancelled;
	bool usedOtherServers = false;
	std::string err = DownloadFile("https://gamebanana.com/dl/" + std::to_string(file->id), file->bytes, archive, GameDir() + "\\imperivm-resurrected\\_cache", true, hooks,
		usedOtherServers);
	if (err.empty() && !file->md5.empty() && HashFile(archive, BCRYPT_MD5_ALGORITHM) != file->md5) err = "the downloaded file is not the published one (different hash)";
	if (err.empty()) err = Extract(archive, tmp, [&](uint64_t, uint64_t) { return !cancelled(); });
	std::string found;
	if (err.empty() && !FindFileNamed(tmp, leaf, found)) err = "the package does not contain the map";
	if (err.empty()) {
		MakeDirs(outFile.substr(0, outFile.find_last_of('\\')));
		if (!CopyFileA(found.c_str(), outFile.c_str(), FALSE)) err = "could not save the map";
	}
	cleanup(err.empty());
	if (err.empty()) {
		Job job;
		job.kind = Job::RoomPack;
		job.entry.id = packId;
		job.entry.name = packName;
		job.entry.version = version;
		job.entry.gbId = gbId;
		job.entry.fileId = file->id;
		job.entry.fileDate = file->date;
		job.entry.aside = true;
		job.text = archive;
		Enqueue(job);
	}
	return err;
}
