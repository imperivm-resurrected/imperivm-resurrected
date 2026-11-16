// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/json.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <io.h>
#include <cstdlib>

#include "core/legacy.h"
#include "i18n/tr.h"

const Json *Json::Get(const std::string &key) const
{
	for (const auto &kv : obj) {
		if (kv.first == key) {
			return &kv.second;
		}
	}
	if (const char *legacy = LegacyJsonKey(key)) {
		for (const auto &kv : obj) {
			if (kv.first == legacy) {
				return &kv.second;
			}
		}
	}
	return nullptr;
}

std::string Json::GetString(const std::string &key, const std::string &def) const
{
	const Json *v = Get(key);
	if (!v) {
		return def;
	}
	if (v->type == String) {
		return v->str;
	}
	if (v->type == Number) {
		char buf[32];
		sprintf_s(buf, "%g", v->num);
		return buf;
	}
	return def;
}

double Json::GetNumber(const std::string &key, double def) const
{
	const Json *v = Get(key);
	return v && v->type == Number ? v->num : def;
}

bool Json::GetBool(const std::string &key, bool def) const
{
	const Json *v = Get(key);
	return v && v->type == Bool ? v->b : def;
}

int Json::GetInt(const std::string &key, int def) const
{
	return static_cast<int>(GetNumber(key, def));
}

int64_t Json::GetInt64(const std::string &key, int64_t def) const
{
	return static_cast<int64_t>(GetNumber(key, static_cast<double>(def)));
}

std::string Json::GetIdText(const std::string &key) const
{
	const Json *v = Get(key);
	if (!v) return std::string();
	if (v->type == String) return v->str;
	if (v->type == Number) return std::to_string(static_cast<long long>(v->num));
	return std::string();
}

int Json::GetFlag(const std::string &key) const
{
	const Json *v = Get(key);
	if (!v || v->type != Bool) return -1;
	return v->b ? 1 : 0;
}

Json &Json::Set(const std::string &key, const Json &value)
{
	type = Object;
	if (const char *legacy = LegacyJsonKey(key)) {
		obj.erase(std::remove_if(obj.begin(), obj.end(), [&](const std::pair<std::string, Json> &kv) { return kv.first == legacy; }), obj.end());
	}
	for (auto &kv : obj) {
		if (kv.first == key) {
			kv.second = value;
			return kv.second;
		}
	}
	obj.emplace_back(key, value);
	return obj.back().second;
}

namespace {

struct Parser {
	const std::string &s;
	size_t i = 0;
	std::string error;

	explicit Parser(const std::string &text) : s(text) {}

	void Skip()
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
			i++;
		}
	}

	bool Fail(const char *what)
	{
		if (error.empty()) error = TrFmt("%s (position %s)", {Tr(what), std::to_string(i)});
		return false;
	}

	static void AppendUtf8(std::string &out, unsigned cp)
	{
		if (cp < 0x80) {
			out += static_cast<char>(cp);
		}
		else if (cp < 0x800) {
			out += static_cast<char>(0xc0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3f));
		}
		else if (cp < 0x10000) {
			out += static_cast<char>(0xe0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (cp & 0x3f));
		}
		else {
			out += static_cast<char>(0xf0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (cp & 0x3f));
		}
	}

	bool ParseString(std::string &out)
	{
		if (s[i] != '"') {
			return Fail("expected a string");
		}
		i++;
		while (i < s.size() && s[i] != '"') {
			char c = s[i++];
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i >= s.size()) {
				break;
			}
			char e = s[i++];
			switch (e) {
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case 'r': out += '\r'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'u': {
				if (i + 4 > s.size()) {
					return Fail("incomplete \\u");
				}
				for (size_t k = 0; k < 4; k++) {
					if (!isxdigit(static_cast<unsigned char>(s[i + k]))) return Fail("invalid \\u");
				}
				unsigned cp = strtoul(s.substr(i, 4).c_str(), nullptr, 16);
				i += 4;
				if (cp >= 0xd800 && cp < 0xdc00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
					unsigned lo = strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
					i += 6;
					cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				}
				if (cp == 0) cp = 0xfffd;
				AppendUtf8(out, cp);
				break;
			}
			default: out += e; break;
			}
		}
		if (i >= s.size()) {
			return Fail("unterminated string");
		}
		i++;
		return true;
	}

	bool ParseValue(Json &v, int depth)
	{
		if (depth > 64) {
			return Fail("nested too deeply");
		}
		Skip();
		if (i >= s.size()) {
			return Fail("unexpected end");
		}
		char c = s[i];
		if (c == '{') {
			v.type = Json::Object;
			i++;
			Skip();
			if (i < s.size() && s[i] == '}') {
				i++;
				return true;
			}
			for (;;) {
				Skip();
				std::string key;
				if (i >= s.size() || !ParseString(key)) {
					return Fail("expected a key");
				}
				Skip();
				if (i >= s.size() || s[i] != ':') {
					return Fail("expected ':'");
				}
				i++;
				Json child;
				if (!ParseValue(child, depth + 1)) {
					return false;
				}
				v.obj.emplace_back(key, std::move(child));
				Skip();
				if (i < s.size() && s[i] == ',') {
					i++;
					continue;
				}
				if (i < s.size() && s[i] == '}') {
					i++;
					return true;
				}
				return Fail("expected ',' or '}'");
			}
		}
		if (c == '[') {
			v.type = Json::Array;
			i++;
			Skip();
			if (i < s.size() && s[i] == ']') {
				i++;
				return true;
			}
			for (;;) {
				Json child;
				if (!ParseValue(child, depth + 1)) {
					return false;
				}
				v.arr.push_back(std::move(child));
				Skip();
				if (i < s.size() && s[i] == ',') {
					i++;
					continue;
				}
				if (i < s.size() && s[i] == ']') {
					i++;
					return true;
				}
				return Fail("expected ',' or ']'");
			}
		}
		if (c == '"') {
			v.type = Json::String;
			return ParseString(v.str);
		}
		if (s.compare(i, 4, "true") == 0) {
			v.type = Json::Bool;
			v.b = true;
			i += 4;
			return true;
		}
		if (s.compare(i, 5, "false") == 0) {
			v.type = Json::Bool;
			i += 5;
			return true;
		}
		if (s.compare(i, 4, "null") == 0) {
			i += 4;
			return true;
		}
		if (c == '-' || (c >= '0' && c <= '9')) {
			char *end = nullptr;
			v.type = Json::Number;
			v.num = strtod(s.c_str() + i, &end);
			i = end - s.c_str();
			return true;
		}
		return Fail("invalid value");
	}
};

void DumpString(std::string &out, const std::string &s)
{
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				sprintf_s(buf, "\\u%04x", c);
				out += buf;
			}
			else {
				out += static_cast<char>(c);
			}
		}
	}
	out += '"';
}

void DumpValue(std::string &out, const Json &v, int indent, int level)
{
	const std::string pad = indent ? "\n" + std::string(static_cast<size_t>(indent) * (level + 1), ' ') : "";
	const std::string padEnd = indent ? "\n" + std::string(static_cast<size_t>(indent) * level, ' ') : "";
	switch (v.type) {
	case Json::Null: out += "null"; break;
	case Json::Bool: out += v.b ? "true" : "false"; break;
	case Json::Number: {
		char buf[32];
		sprintf_s(buf, "%.15g", v.num);
		out += buf;
		break;
	}
	case Json::String: DumpString(out, v.str); break;
	case Json::Array: {
		if (v.arr.empty()) {
			out += "[]";
			break;
		}
		bool simple = true;
		for (const Json &c : v.arr) {
			simple = simple && c.type != Json::Array && c.type != Json::Object;
		}
		out += '[';
		for (size_t k = 0; k < v.arr.size(); k++) {
			out += simple ? (k ? " " : "") : pad;
			DumpValue(out, v.arr[k], indent, level + 1);
			if (k + 1 < v.arr.size()) {
				out += ',';
			}
		}
		out += simple ? "" : padEnd;
		out += ']';
		break;
	}
	case Json::Object: {
		if (v.obj.empty()) {
			out += "{}";
			break;
		}
		out += '{';
		for (size_t k = 0; k < v.obj.size(); k++) {
			out += pad;
			DumpString(out, v.obj[k].first);
			out += indent ? ": " : ":";
			DumpValue(out, v.obj[k].second, indent, level + 1);
			if (k + 1 < v.obj.size()) {
				out += ',';
			}
		}
		out += padEnd;
		out += '}';
		break;
	}
	}
}

}

bool Json::Parse(const std::string &text, Json &out, std::string *error)
{
	Parser p(text);
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef && static_cast<unsigned char>(text[1]) == 0xbb &&
		static_cast<unsigned char>(text[2]) == 0xbf) {
		p.i = 3;
	}
	out = Json();
	bool ok = p.ParseValue(out, 0);
	if (ok) {
		p.Skip();
		if (p.i != text.size()) {
			ok = p.Fail("extra text at the end");
		}
	}
	if (!ok && error) {
		*error = p.error;
	}
	return ok;
}

std::string Json::Dump(int indent) const
{
	std::string out;
	DumpValue(out, *this, indent, 0);
	return out;
}

static const size_t kMaxJsonFile = 16u << 20;

bool ReadJsonFile(const std::string &path, Json &out, std::string *error)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) {
		if (error) {
			*error = Tr("cannot open");
		}
		return false;
	}
	std::string text;
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		text.append(buf, n);
		if (text.size() > kMaxJsonFile) {
			fclose(f);
			if (error) *error = Tr("file too large");
			return false;
		}
	}
	fclose(f);
	return Json::Parse(text, out, error);
}

bool WriteJsonFile(const std::string &path, const Json &value)
{
	const std::string tmp = path + ".tmp";
	FILE *f = nullptr;
	if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) {
		return false;
	}
	const std::string text = value.Dump(2) + "\n";
	bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	ok = fflush(f) == 0 && ok;
	ok = _commit(_fileno(f)) == 0 && ok;
	fclose(f);
	return ok && MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
