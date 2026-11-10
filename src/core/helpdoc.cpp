// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/helpdoc.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

const size_t kMaxHistory = 100;
const int kMaxDepth = 24;

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && IsSpace(s[a])) a++;
	while (b > a && IsSpace(s[b - 1])) b--;
	return s.substr(a, b - a);
}

std::string FirstLine(const std::string &s)
{
	return Trim(s.substr(0, s.find('\n')));
}

bool ValidUtf8(const std::string &s)
{
	return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) > 0;
}

std::wstring Wide(const std::string &s, UINT cp)
{
	if (s.empty()) return std::wstring();
	const int n = MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(n > 0 ? n : 0, L'\0');
	if (n > 0) MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

std::string Utf8(const std::wstring &w)
{
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}

// The game's files are UTF-8; an old one saved in the Windows code page is read as such.
std::string AsUtf8(const std::string &raw)
{
	std::string s = raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0 ? raw.substr(3) : raw;
	return ValidUtf8(s) ? s : Utf8(Wide(s, 1252));
}

void AppendCodePoint(std::string &out, unsigned cp)
{
	if (cp == 0 || cp > 0x10ffff) return;
	if (cp < 0x80) out += static_cast<char>(cp);
	else if (cp < 0x800) {
		out += static_cast<char>(0xc0 | cp >> 6);
		out += static_cast<char>(0x80 | (cp & 0x3f));
	}
	else if (cp < 0x10000) {
		out += static_cast<char>(0xe0 | cp >> 12);
		out += static_cast<char>(0x80 | (cp >> 6 & 0x3f));
		out += static_cast<char>(0x80 | (cp & 0x3f));
	}
	else {
		out += static_cast<char>(0xf0 | cp >> 18);
		out += static_cast<char>(0x80 | (cp >> 12 & 0x3f));
		out += static_cast<char>(0x80 | (cp >> 6 & 0x3f));
		out += static_cast<char>(0x80 | (cp & 0x3f));
	}
}

std::string Unescape(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '&') {
			out += s[i];
			continue;
		}
		const size_t semi = s.find(';', i);
		if (semi == std::string::npos || semi - i > 10) {
			out += s[i];
			continue;
		}
		const std::string name = s.substr(i + 1, semi - i - 1);
		if (name == "amp") out += '&';
		else if (name == "lt") out += '<';
		else if (name == "gt") out += '>';
		else if (name == "quot") out += '"';
		else if (name == "apos") out += '\'';
		else if (name.size() > 1 && name[0] == '#') {
			const bool hex = name[1] == 'x' || name[1] == 'X';
			AppendCodePoint(out, static_cast<unsigned>(strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10)));
		}
		else {
			out += s.substr(i, semi - i + 1);
		}
		i = semi;
	}
	return out;
}

struct XmlAttr {
	std::string name, value;
};

struct XmlHandler {
	std::function<void(const std::string &, const std::vector<XmlAttr> &)> start;
	std::function<void(const std::string &)> end;
	std::function<void(const std::string &)> text;
};

bool IsNameChar(char c) { return !IsSpace(c) && c != '>' && c != '/' && c != '=' && c != '<'; }

// Elements, attributes, comments and character data: what the game's help and tables use.
void ScanXml(const std::string &s, const XmlHandler &h)
{
	size_t i = 0;
	const size_t n = s.size();
	while (i < n) {
		if (s[i] != '<') {
			const size_t next = s.find('<', i);
			const size_t stop = next == std::string::npos ? n : next;
			if (h.text) h.text(Unescape(s.substr(i, stop - i)));
			i = stop;
			continue;
		}
		if (s.compare(i, 4, "<!--") == 0) {
			const size_t close = s.find("-->", i + 4);
			i = close == std::string::npos ? n : close + 3;
			continue;
		}
		if (s.compare(i, 9, "<![CDATA[") == 0) {
			const size_t close = s.find("]]>", i + 9);
			const size_t stop = close == std::string::npos ? n : close;
			if (h.text) h.text(s.substr(i + 9, stop - i - 9));
			i = close == std::string::npos ? n : close + 3;
			continue;
		}
		if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) {
			const size_t close = s.find('>', i);
			i = close == std::string::npos ? n : close + 1;
			continue;
		}
		const bool closing = i + 1 < n && s[i + 1] == '/';
		size_t j = i + (closing ? 2 : 1);
		const size_t nameStart = j;
		while (j < n && IsNameChar(s[j])) j++;
		const std::string name = s.substr(nameStart, j - nameStart);
		std::vector<XmlAttr> attrs;
		bool selfClosing = false;
		while (j < n && s[j] != '>') {
			if (IsSpace(s[j])) {
				j++;
				continue;
			}
			if (s[j] == '/') {
				selfClosing = true;
				j++;
				continue;
			}
			const size_t an = j;
			while (j < n && IsNameChar(s[j])) j++;
			XmlAttr a;
			a.name = s.substr(an, j - an);
			while (j < n && IsSpace(s[j])) j++;
			if (j < n && s[j] == '=') {
				j++;
				while (j < n && IsSpace(s[j])) j++;
				if (j < n && (s[j] == '"' || s[j] == '\'')) {
					const char q = s[j++];
					const size_t vs = j;
					while (j < n && s[j] != q) j++;
					a.value = Unescape(s.substr(vs, j - vs));
					if (j < n) j++;
				}
				else {
					const size_t vs = j;
					while (j < n && !IsSpace(s[j]) && s[j] != '>') j++;
					a.value = Unescape(s.substr(vs, j - vs));
				}
			}
			if (j == an) j++;
			if (!a.name.empty()) attrs.push_back(a);
		}
		i = j < n ? j + 1 : n;
		if (name.empty()) continue;
		if (closing) {
			if (h.end) h.end(name);
			continue;
		}
		if (h.start) h.start(name, attrs);
		if (selfClosing && h.end) h.end(name);
	}
}

const std::string *Attr(const std::vector<XmlAttr> &attrs, const char *name)
{
	for (const XmlAttr &a : attrs) {
		if (_stricmp(a.name.c_str(), name) == 0) return &a.value;
	}
	return nullptr;
}

bool Truthy(const std::string *v)
{
	if (!v) return false;
	const std::string l = Lower(Trim(*v));
	return l == "1" || l == "true" || l == "yes";
}

// The lines of the entry without the indentation of the file, with at most one empty line between paragraphs.
std::string CleanText(const std::string &raw)
{
	std::vector<std::string> lines;
	std::string cur;
	for (size_t i = 0; i <= raw.size(); i++) {
		const char c = i < raw.size() ? raw[i] : '\n';
		if (c == '\r') continue;
		if (c == '\n') {
			lines.push_back(Trim(cur));
			cur.clear();
		}
		else {
			cur += c == '\t' ? ' ' : c;
		}
	}
	std::string out;
	bool blank = false;
	for (const std::string &l : lines) {
		if (l.empty()) {
			blank = !out.empty();
			continue;
		}
		if (!out.empty()) out += blank ? "\n\n" : "\n";
		out += l;
		blank = false;
	}
	return out;
}

bool IsWeb(const std::string &link)
{
	const std::string l = Lower(Trim(link));
	return l.compare(0, 7, "http://") == 0 || l.compare(0, 8, "https://") == 0;
}

std::vector<std::string> SplitPath(const std::string &path)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : path) {
		if (c == '/') {
			if (!cur.empty()) out.push_back(cur);
			cur.clear();
		}
		else {
			cur += c;
		}
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}

int Walk(const HelpDoc &doc, std::vector<int> level, const std::vector<std::string> &segments)
{
	int found = -1;
	for (const std::string &seg : segments) {
		found = -1;
		for (int c : level) {
			if (doc.topics[c].id == seg) {
				found = c;
				break;
			}
		}
		if (found < 0) return -1;
		level = doc.topics[found].children;
	}
	return found;
}

std::vector<std::string> Words(const std::string &s)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (IsSpace(c)) {
			if (!cur.empty()) out.push_back(cur);
			cur.clear();
		}
		else {
			cur += c;
		}
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}

bool StartsWord(const std::string &text, size_t at)
{
	return at == 0 || !isalnum(static_cast<unsigned char>(text[at - 1]));
}

// The words around the first one that holds the token.
std::string Snippet(const std::string &paragraph, const std::string &token)
{
	const std::vector<std::string> words = Words(paragraph);
	size_t hit = 0;
	for (size_t i = 0; i < words.size(); i++) {
		if (HelpFold(words[i]).find(token) != std::string::npos) {
			hit = i;
			break;
		}
	}
	const size_t from = hit > 8 ? hit - 8 : 0, to = std::min(words.size(), from + 30);
	std::string out = from > 0 ? "\xE2\x80\xA6 " : "";
	for (size_t i = from; i < to; i++) out += (i > from ? " " : "") + words[i];
	if (to < words.size()) out += " \xE2\x80\xA6";
	return out;
}

}

HelpDoc ParseHelpXml(const std::string &xml)
{
	HelpDoc doc;
	std::vector<int> stack;
	bool inEntry = false;
	HelpEntry entry;
	std::string raw;
	auto finishEntry = [&]() {
		if (!inEntry) return;
		inEntry = false;
		entry.text = CleanText(raw);
		if (!stack.empty()) doc.topics[stack.back()].entries.push_back(entry);
	};
	XmlHandler h;
	h.start = [&](const std::string &name, const std::vector<XmlAttr> &attrs) {
		if (_stricmp(name.c_str(), "topic") == 0) {
			finishEntry();
			HelpTopic t;
			const std::string *id = Attr(attrs, "id");
			t.id = id ? Trim(*id) : std::string();
			t.parent = stack.empty() ? -1 : stack.back();
			const int index = static_cast<int>(doc.topics.size());
			doc.topics.push_back(t);
			if (t.parent >= 0) doc.topics[t.parent].children.push_back(index);
			else doc.roots.push_back(index);
			stack.push_back(index);
		}
		else if (_stricmp(name.c_str(), "entry") == 0 && !stack.empty()) {
			finishEntry();
			inEntry = true;
			entry = HelpEntry();
			raw.clear();
			const std::string *font = Attr(attrs, "font");
			entry.large = font && Lower(Trim(*font)) == "large";
			entry.hcenter = Truthy(Attr(attrs, "hcenter"));
			entry.vcenter = Truthy(Attr(attrs, "vcenter"));
			if (const std::string *v = Attr(attrs, "image")) entry.image = Trim(*v);
			if (const std::string *v = Attr(attrs, "link")) entry.link = Trim(*v);
			if (const std::string *v = Attr(attrs, "vspacing")) entry.vspacing = atoi(v->c_str());
		}
	};
	h.end = [&](const std::string &name) {
		if (_stricmp(name.c_str(), "entry") == 0) finishEntry();
		else if (_stricmp(name.c_str(), "topic") == 0) {
			finishEntry();
			if (!stack.empty()) stack.pop_back();
		}
	};
	h.text = [&](const std::string &t) {
		if (inEntry) raw += t;
	};
	ScanXml(AsUtf8(xml), h);
	finishEntry();
	for (size_t t = 0; t < doc.topics.size(); t++) {
		for (HelpEntry &e : doc.topics[t].entries) {
			if (e.link.empty()) continue;
			e.web = IsWeb(e.link);
			if (!e.web) e.target = HelpResolve(doc, static_cast<int>(t), e.link);
		}
	}
	return doc;
}

int HelpResolve(const HelpDoc &doc, int from, const std::string &link)
{
	const std::string path = Trim(link);
	if (path.empty() || IsWeb(path)) return -1;
	const std::vector<std::string> segments = SplitPath(path);
	if (segments.empty()) return -1;
	const bool absolute = path[0] == '/';
	if (!absolute && from >= 0 && from < static_cast<int>(doc.topics.size())) {
		const int found = Walk(doc, doc.topics[from].children, segments);
		if (found >= 0) return found;
	}
	const int found = Walk(doc, doc.roots, segments);
	if (found >= 0) return found;
	const std::string &last = segments.back();
	for (size_t t = 0; t < doc.topics.size(); t++) {
		if (doc.topics[t].id == last) return static_cast<int>(t);
	}
	for (size_t t = 0; t < doc.topics.size(); t++) {
		if (_stricmp(doc.topics[t].id.c_str(), last.c_str()) == 0) return static_cast<int>(t);
	}
	return -1;
}

std::string HelpPath(const HelpDoc &doc, int topic)
{
	std::string path;
	for (int t = topic, depth = 0; t >= 0 && t < static_cast<int>(doc.topics.size()) && depth < kMaxDepth; t = doc.topics[t].parent, depth++) {
		path = "/" + doc.topics[t].id + path;
	}
	return path;
}

HelpIndex BuildHelpIndex(const HelpDoc &doc)
{
	HelpIndex x;
	const int n = static_cast<int>(doc.topics.size());
	x.titles.assign(n, std::string());
	x.sections.assign(n, {});
	x.listing.assign(n, -1);
	x.section.assign(n, -1);
	x.paragraphs.assign(n, {});
	x.folded.assign(n, {});
	x.foldedTitles.assign(n, std::string());
	x.home = doc.roots.empty() ? -1 : doc.roots[0];
	for (int t = 0; t < n; t++) {
		const std::vector<HelpEntry> &es = doc.topics[t].entries;
		std::vector<HelpSection> secs;
		int pictureLinks = 0, others = 0;
		for (size_t i = 0; i < es.size(); i++) {
			const HelpEntry &e = es[i];
			if (e.large && !e.text.empty()) {
				if (!secs.empty()) secs.back().end = static_cast<int>(i);
				secs.push_back({FirstLine(e.text), static_cast<int>(i), static_cast<int>(es.size())});
				continue;
			}
			if (e.target >= 0 && !e.image.empty() && e.text.find('\n') == std::string::npos) pictureLinks++;
			else if (!e.text.empty()) others++;
		}
		if (secs.size() >= 2 && pictureLinks >= static_cast<int>(secs.size()) && pictureLinks * 3 >= (pictureLinks + others) * 2) x.sections[t] = secs;
	}
	for (int l = 0; l < n; l++) {
		for (size_t s = 0; s < x.sections[l].size(); s++) {
			const HelpSection &sec = x.sections[l][s];
			for (int i = sec.first + 1; i < sec.end; i++) {
				const int to = doc.topics[l].entries[i].target;
				if (to < 0 || to == l || x.listing[to] >= 0) continue;
				x.listing[to] = l;
				x.section[to] = static_cast<int>(s);
			}
		}
	}
	std::vector<std::string> linkText(n);
	std::vector<bool> fromParent(n, false);
	for (int u = 0; u < n; u++) {
		for (const HelpEntry &e : doc.topics[u].entries) {
			const int to = e.target;
			const std::string label = FirstLine(e.text);
			if (to < 0 || label.empty() || fromParent[to]) continue;
			const bool parent = doc.topics[to].parent == u;
			if (linkText[to].empty() || parent) {
				linkText[to] = label;
				fromParent[to] = parent;
			}
		}
	}
	for (int t = 0; t < n; t++) {
		const std::vector<HelpEntry> &es = doc.topics[t].entries;
		const std::string own = !es.empty() && es[0].large ? FirstLine(es[0].text) : std::string();
		std::string title;
		if (t == x.home) title.clear();
		else if (!x.sections[t].empty()) title = !linkText[t].empty() ? linkText[t] : own;
		else title = !own.empty() ? own : linkText[t];
		if (title.empty() && t != x.home) title = doc.topics[t].id;
		x.titles[t] = title;
		x.foldedTitles[t] = HelpFold(title);
		for (size_t i = 0; i < es.size(); i++) {
			const HelpEntry &e = es[i];
			if (e.text.empty() || e.target >= 0 || e.web) continue;
			if (i == 0 && e.large && FirstLine(e.text) == title) continue;
			for (size_t from = 0; from < e.text.size();) {
				const size_t gap = e.text.find("\n\n", from);
				const std::string para = e.text.substr(from, gap == std::string::npos ? std::string::npos : gap - from);
				x.paragraphs[t].push_back(para);
				x.folded[t].push_back(HelpFold(para));
				from = gap == std::string::npos ? e.text.size() : gap + 2;
			}
		}
	}
	if (x.home >= 0) {
		for (const HelpEntry &e : doc.topics[x.home].entries) {
			if (e.large && !e.text.empty()) {
				x.groups.push_back({FirstLine(e.text), {}});
				continue;
			}
			if (e.target < 0 || e.target == x.home) continue;
			if (x.groups.empty()) x.groups.push_back({std::string(), {}});
			HelpNavItem item;
			item.topic = e.target;
			item.label = FirstLine(e.text);
			if (item.label.empty()) item.label = x.titles[e.target];
			for (const HelpSection &s : x.sections[e.target]) item.sections.push_back(s.heading);
			x.groups.back().items.push_back(item);
		}
		x.groups.erase(std::remove_if(x.groups.begin(), x.groups.end(), [](const HelpNavGroup &g) { return g.items.empty(); }), x.groups.end());
	}
	return x;
}

void HelpHistory::Go(const HelpPlace &place)
{
	if (const HelpPlace *cur = Current()) {
		if (*cur == place) return;
	}
	places.resize(at + 1);
	places.push_back(place);
	if (places.size() > kMaxHistory) places.erase(places.begin());
	at = static_cast<int>(places.size()) - 1;
}

void HelpHistory::Replace(const HelpPlace &place)
{
	if (!Current()) {
		Go(place);
		return;
	}
	places[at] = place;
}

bool HelpHistory::Back()
{
	if (!CanBack()) return false;
	at--;
	return true;
}

bool HelpHistory::Forward()
{
	if (!CanForward()) return false;
	at++;
	return true;
}

std::vector<HelpCrumb> HelpCrumbs(const HelpDoc &doc, const HelpIndex &index, const HelpPlace &place)
{
	const int n = static_cast<int>(doc.topics.size());
	std::vector<HelpCrumb> out;
	const HelpCrumb home = {std::string(), {index.home, -1, std::string()}};
	if (!place.query.empty()) {
		out.push_back(home);
		out.push_back({place.query, place});
		return out;
	}
	if (place.topic < 0 || place.topic >= n) {
		out.push_back(home);
		return out;
	}
	std::vector<HelpPlace> chain = {place};
	std::set<std::pair<int, int>> seen = {{place.topic, place.section}};
	HelpPlace cur = place;
	while (static_cast<int>(chain.size()) < kMaxDepth) {
		HelpPlace up;
		if (cur.section >= 0) up.topic = cur.topic;
		else if (index.listing[cur.topic] >= 0) {
			up.topic = index.listing[cur.topic];
			up.section = index.section[cur.topic];
		}
		else if (doc.topics[cur.topic].parent >= 0) up.topic = doc.topics[cur.topic].parent;
		else break;
		if (!seen.insert({up.topic, up.section}).second) break;
		chain.push_back(up);
		cur = up;
	}
	if (chain.back().topic != index.home && index.home >= 0) chain.push_back(home.place);
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		HelpCrumb c;
		c.place = *it;
		if (it->section >= 0 && it->section < static_cast<int>(index.sections[it->topic].size())) c.label = index.sections[it->topic][it->section].heading;
		else c.label = index.titles[it->topic];
		out.push_back(c);
	}
	return out;
}

HelpNavSpot HelpNavFor(const HelpDoc &doc, const HelpIndex &index, const HelpPlace &place)
{
	if (!place.query.empty()) return {};
	const std::vector<HelpCrumb> crumbs = HelpCrumbs(doc, index, place);
	for (auto it = crumbs.rbegin(); it != crumbs.rend(); ++it) {
		const HelpPlace &c = it->place;
		for (size_t g = 0; g < index.groups.size(); g++) {
			const std::vector<HelpNavItem> &items = index.groups[g].items;
			for (size_t i = 0; i < items.size(); i++) {
				if (items[i].topic != c.topic) continue;
				if (c.section >= 0 && c.section >= static_cast<int>(items[i].sections.size())) continue;
				return {static_cast<int>(g), static_cast<int>(i), c.section};
			}
		}
	}
	return {};
}

std::string HelpFold(const std::string &utf8)
{
	std::wstring w = Wide(utf8, CP_UTF8);
	if (w.empty()) return std::string();
	CharLowerBuffW(&w[0], static_cast<DWORD>(w.size()));
	const int n = FoldStringW(MAP_COMPOSITE, w.c_str(), static_cast<int>(w.size()), nullptr, 0);
	std::wstring d(n > 0 ? n : 0, L'\0');
	if (n > 0) FoldStringW(MAP_COMPOSITE, w.c_str(), static_cast<int>(w.size()), &d[0], n);
	else d = w;
	std::wstring out;
	out.reserve(d.size());
	for (wchar_t c : d) {
		if (c >= 0x300 && c <= 0x36f) continue;
		out += c == L'\n' || c == L'\t' ? L' ' : c;
	}
	return Utf8(out);
}

std::vector<HelpHit> HelpSearch(const HelpDoc &doc, const HelpIndex &index, const std::string &query, size_t limit)
{
	std::vector<HelpHit> hits;
	const std::vector<std::string> tokens = Words(HelpFold(query));
	if (tokens.empty()) return hits;
	const int n = static_cast<int>(doc.topics.size());
	for (int t = 0; t < n; t++) {
		for (size_t s = 0; s < index.sections[t].size(); s++) {
			const std::string heading = HelpFold(index.sections[t][s].heading);
			bool all = true;
			int score = 12;
			for (const std::string &tok : tokens) {
				const size_t at = heading.find(tok);
				all = all && at != std::string::npos;
				if (at != std::string::npos && StartsWord(heading, at)) score += 5;
			}
			if (all) hits.push_back({t, static_cast<int>(s), score, std::string()});
		}
		const std::string &title = index.foldedTitles[t];
		const std::vector<std::string> &folded = index.folded[t];
		int score = 0;
		bool all = true;
		std::string snippet;
		for (const std::string &tok : tokens) {
			int tokenScore = 0;
			const size_t at = title.find(tok);
			if (at != std::string::npos) tokenScore += StartsWord(title, at) ? 15 : 10;
			int count = 0;
			for (size_t p = 0; p < folded.size(); p++) {
				for (size_t from = folded[p].find(tok); from != std::string::npos && count < 5; from = folded[p].find(tok, from + tok.size())) {
					if (count == 0 && snippet.empty() && at == std::string::npos) snippet = Snippet(index.paragraphs[t][p], tok);
					count++;
				}
			}
			tokenScore += count;
			if (tokenScore == 0) {
				all = false;
				break;
			}
			score += tokenScore;
		}
		if (!all) continue;
		if (title == HelpFold(Trim(query))) score += 20;
		if (snippet.empty() && !index.paragraphs[t].empty()) snippet = Snippet(index.paragraphs[t][0], tokens[0]);
		hits.push_back({t, -1, score, snippet});
	}
	std::stable_sort(hits.begin(), hits.end(), [](const HelpHit &a, const HelpHit &b) { return a.score > b.score; });
	if (hits.size() > limit) hits.resize(limit);
	return hits;
}

HelpCounters ParseCounterUnits(const std::string &xml)
{
	HelpCounters out;
	std::string enemy;
	XmlHandler h;
	h.start = [&](const std::string &name, const std::vector<XmlAttr> &attrs) {
		const std::string *cls = Attr(attrs, "class");
		if (_stricmp(name.c_str(), "unit") == 0) {
			enemy = cls ? Lower(Trim(*cls)) : std::string();
			if (!enemy.empty()) out.of[enemy];
		}
		else if (_stricmp(name.c_str(), "counter") == 0 && !enemy.empty() && cls) {
			const std::string *coeff = Attr(attrs, "coeff");
			out.of[enemy].push_back({Trim(*cls), coeff ? atoi(coeff->c_str()) : 0});
		}
	};
	h.end = [&](const std::string &name) {
		if (_stricmp(name.c_str(), "unit") == 0) enemy.clear();
	};
	ScanXml(AsUtf8(xml), h);
	return out;
}

HelpMatchups HelpMatchupsFor(const HelpCounters &counters, const std::string &cls)
{
	HelpMatchups m;
	const std::string me = Lower(Trim(cls));
	if (me.empty()) return m;
	auto add = [](std::vector<HelpCounter> &list, const std::string &name, int coeff) {
		if (coeff <= 0) return;
		for (HelpCounter &c : list) {
			if (Lower(c.cls) != Lower(name)) continue;
			c.coeff = std::max(c.coeff, coeff);
			return;
		}
		list.push_back({name, coeff});
	};
	const auto own = counters.of.find(me);
	if (own != counters.of.end()) {
		for (const HelpCounter &c : own->second) {
			if (Lower(c.cls) != me) add(m.weak, c.cls, c.coeff);
		}
	}
	for (const auto &kv : counters.of) {
		if (kv.first == me) continue;
		for (const HelpCounter &c : kv.second) {
			if (Lower(c.cls) == me) add(m.strong, kv.first, c.coeff);
		}
	}
	const auto byCoeff = [](const HelpCounter &a, const HelpCounter &b) { return a.coeff > b.coeff; };
	std::stable_sort(m.weak.begin(), m.weak.end(), byCoeff);
	std::stable_sort(m.strong.begin(), m.strong.end(), byCoeff);
	return m;
}

bool HelpUnitIsRanged(const HelpUnitStats &s)
{
	return s.damageType == kHelpDamagePierce || s.damageType == kHelpDamageSiege || s.range >= 100;
}
