// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/bonuses.h"

#include <cctype>

#include "core/strutil.h"

namespace {
std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
	while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
	return s.substr(a, b - a);
}

using strutil::Lower;

struct Builtin {
	const char *file;
	const char *marker;
	const char *name;
	const char *description;
};

const Builtin kBuiltins[] = {
	{"none", "", "No bonus", "No starting bonus."},
	{"wealth", "set.gold+2000", "2000 gold", "2000 gold at the start, plus 20 gold every 2 seconds."},
	{"riches", "set.gold+5000", "5000 gold", "5000 gold at the start, plus 50 gold every 2 seconds."},
	{"hero", "setlevel(12)", "Hero", "A level 12 hero in the town hall at the start."},
};

std::string CodeOnly(const std::string &script)
{
	std::string out;
	size_t at = 0;
	while (at <= script.size()) {
		size_t end = script.find('\n', at);
		if (end == std::string::npos) end = script.size();
		std::string line = script.substr(at, end - at);
		const size_t comment = line.find("//");
		if (comment != std::string::npos) line.erase(comment);
		for (char c : line) {
			if (!isspace(static_cast<unsigned char>(c))) out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}
		at = end + 1;
	}
	return out;
}
}

int BonusFileNumber(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
	size_t n = 0;
	int value = 0;
	while (n < file.size() && isdigit(static_cast<unsigned char>(file[n])) && n < 6) value = value * 10 + (file[n++] - '0');
	if (!n || n >= file.size() || file[n] != ' ') return -1;
	return value;
}

std::string BonusScriptNote(const std::string &script, const std::string &key, const std::string &language)
{
	const std::string wanted = Lower(key), lang = Lower(language);
	std::string generic, english, exact;
	size_t at = 0;
	while (at < script.size()) {
		size_t end = script.find('\n', at);
		if (end == std::string::npos) end = script.size();
		const std::string line = Trim(script.substr(at, end - at));
		at = end + 1;
		if (line.empty()) continue;
		if (line.compare(0, 2, "//") != 0) break;
		const std::string body = Trim(line.substr(2));
		if (body.empty() || body[0] != '@') continue;
		size_t split = 1;
		while (split < body.size() && !isspace(static_cast<unsigned char>(body[split]))) split++;
		std::string tag = Lower(body.substr(1, split - 1));
		const std::string value = Trim(body.substr(split));
		std::string tagLang;
		const size_t dot = tag.find('.');
		if (dot != std::string::npos) {
			tagLang = tag.substr(dot + 1);
			tag = tag.substr(0, dot);
		}
		if (tag != wanted || value.empty()) continue;
		if (tagLang.empty()) generic = value;
		else if (tagLang == lang) exact = value;
		else if (tagLang == "en") english = value;
	}
	if (!exact.empty()) return exact;
	if (!generic.empty()) return generic;
	return english;
}

std::string BonusFileKey(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
	const size_t dot = file.find_last_of('.');
	if (dot != std::string::npos) file.erase(dot);
	const size_t space = file.find(' ');
	if (BonusFileNumber(path) >= 0 && space != std::string::npos) file.erase(0, space + 1);
	return Trim(Lower(file));
}

BonusDef BonusDefine(const std::string &listName, const std::string &path, const std::string &script, const std::string &language)
{
	BonusDef out;
	out.name = BonusScriptNote(script, "name", language);
	out.description = BonusScriptNote(script, "description", language);
	const std::string key = BonusFileKey(path), code = CodeOnly(script);
	for (const Builtin &b : kBuiltins) {
		if (key != b.file) continue;
		if (b.marker[0] ? code.find(b.marker) == std::string::npos : !code.empty()) continue;
		if (out.name.empty()) out.name = b.name;
		if (out.description.empty()) out.description = b.description;
	}
	if (out.name.empty()) out.name = listName;
	return out;
}
