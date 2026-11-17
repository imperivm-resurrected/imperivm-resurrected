// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/profilecode.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "core/base64.h"
#include "core/json.h"

namespace {

const char kPrefix[] = "IMPV";
const int kVersion = 1;
const size_t kMaxText = 64 * 1024;
const size_t kMaxEntries = 1000;

typedef std::vector<std::pair<std::string, std::string>> Pairs;

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsCodeChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || IsDigit(c) || c == '-' || c == '_' || c == '='; }

char Upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; }

Json Strings(const std::vector<std::string> &list)
{
	Json a = Json::MakeArray();
	for (const std::string &s : list) a.arr.push_back(Json::MakeString(s));
	return a;
}

bool FindCode(const std::string &text, int &version, std::string &body)
{
	const size_t prefix = sizeof(kPrefix) - 1;
	for (size_t at = 0; at + prefix + 2 <= text.size(); at++) {
		if (at > 0 && IsCodeChar(text[at - 1])) continue;
		bool match = true;
		for (size_t k = 0; k < prefix && match; k++) match = Upper(text[at + k]) == kPrefix[k];
		if (!match) continue;
		size_t p = at + prefix;
		const size_t digits = p;
		version = 0;
		while (p < text.size() && IsDigit(text[p])) {
			if (version < 10000) version = version * 10 + (text[p] - '0');
			p++;
		}
		if (p == digits || p >= text.size() || text[p] != '-') continue;
		size_t end = p + 1;
		while (end < text.size() && IsCodeChar(text[end])) end++;
		body = text.substr(p + 1, end - p - 1);
		return true;
	}
	return false;
}

bool TextAt(const Json &list, size_t i, std::string &out)
{
	out.clear();
	if (i >= list.arr.size()) return true;
	if (list.arr[i].type != Json::String) return false;
	out = list.arr[i].str;
	return true;
}

bool GbIdOf(const Json &v, uint64_t &out)
{
	out = 0;
	if (v.type == Json::Number) {
		if (v.num < 0 || v.num > 9007199254740991.0 || v.num != static_cast<double>(static_cast<uint64_t>(v.num))) return false;
		out = static_cast<uint64_t>(v.num);
		return true;
	}
	if (v.type != Json::String || v.str.empty() || v.str.size() > 20) return false;
	for (char c : v.str) {
		const uint64_t digit = static_cast<uint64_t>(c - '0');
		if (!IsDigit(c) || out > (UINT64_MAX - digit) / 10) return false;
		out = out * 10 + digit;
	}
	return true;
}

bool PairsOf(const Json &j, const char *key, Pairs &out)
{
	const Json *list = j.Get(key);
	if (!list) return true;
	if (list->type != Json::Array || list->arr.size() > kMaxEntries) return false;
	for (const Json &v : list->arr) {
		if (v.type != Json::Array || v.arr.size() != 2 || v.arr[0].type != Json::String || v.arr[1].type != Json::String) return false;
		if (v.arr[0].str.empty() || v.arr[1].str.empty()) return false;
		out.push_back({v.arr[0].str, v.arr[1].str});
	}
	return true;
}

int Decode(const std::string &text, ModsSharedProfile &out)
{
	if (text.size() > kMaxText) return kProfileCodeNotACode;
	int version = 0;
	std::string body;
	if (!FindCode(text, version, body)) return kProfileCodeNotACode;
	if (version > kVersion) return kProfileCodeNewer;
	if (version != kVersion) return kProfileCodeNotACode;
	std::string data;
	Json j;
	if (body.empty() || !Base64UrlDecode(body, data) || !Json::Parse(data, j) || j.type != Json::Object) return kProfileCodeDamaged;
	const Json *name = j.Get("n");
	const Json *mods = j.Get("m");
	if ((name && name->type != Json::String) || !mods || mods->type != Json::Array || mods->arr.size() > kMaxEntries) return kProfileCodeDamaged;
	out.name = name ? name->str : "";
	for (const Json &m : mods->arr) {
		if (m.type != Json::Array || m.arr.empty() || m.arr.size() > 4) return kProfileCodeDamaged;
		ModsSharedMod sm;
		if (!TextAt(m, 0, sm.id) || sm.id.empty() || !TextAt(m, 1, sm.name) || !TextAt(m, 2, sm.version)) return kProfileCodeDamaged;
		if (m.arr.size() > 3 && !GbIdOf(m.arr[3], sm.gbId)) return kProfileCodeDamaged;
		out.mods.push_back(sm);
	}
	Pairs choices, files;
	if (!PairsOf(j, "c", choices) || !PairsOf(j, "f", files)) return kProfileCodeDamaged;
	for (const auto &c : choices) out.choices.push_back({c.first, c.second});
	for (const auto &f : files) out.files.push_back({f.first, f.second});
	return kProfileCodeOk;
}

}

std::string EncodeProfileCode(const ModsSharedProfile &sp)
{
	Json j = Json::MakeObject();
	j.Set("n", Json::MakeString(sp.name));
	Json mods = Json::MakeArray();
	for (const ModsSharedMod &m : sp.mods) {
		if (m.id.empty()) continue;
		std::vector<std::string> fields = {m.id, m.name, m.version};
		if (m.gbId) fields.push_back(std::to_string(m.gbId));
		mods.arr.push_back(Strings(fields));
	}
	j.Set("m", mods);
	Json choices = Json::MakeArray();
	for (const ModChoice &c : sp.choices) {
		if (!c.use.empty() && !c.over.empty()) choices.arr.push_back(Strings({c.use, c.over}));
	}
	if (!choices.arr.empty()) j.Set("c", choices);
	Json files = Json::MakeArray();
	for (const ModFileChoice &f : sp.files) {
		if (!f.path.empty() && !f.use.empty()) files.arr.push_back(Strings({f.path, f.use}));
	}
	if (!files.arr.empty()) j.Set("f", files);
	return std::string(kPrefix) + std::to_string(kVersion) + "-" + Base64UrlEncode(j.Dump());
}

bool DecodeProfileCode(const std::string &text, ModsSharedProfile &out, int *problem)
{
	out = ModsSharedProfile();
	const int result = Decode(text, out);
	if (result != kProfileCodeOk) out = ModsSharedProfile();
	if (problem) *problem = result;
	return result == kProfileCodeOk;
}
