// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/modrules.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/sha256.h"
#include "mods/mods.h"

namespace mods_impl {

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
	return s;
}

std::string NormPath(std::string s)
{
	std::replace(s.begin(), s.end(), '\\', '/');
	s = Lower(s);
	while (!s.empty() && s[0] == '/') {
		s.erase(0, 1);
	}
	return s;
}

bool StartsWith(const std::string &s, const char *prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }

bool EndsWith(const std::string &s, const char *suffix)
{
	size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string SafeFileName(const std::string &name)
{
	std::string out;
	for (char c : name) {
		out += strchr("\\/:*?\"<>|", c) ? '_' : c;
	}
	return out.empty() ? "profile" : out;
}

std::string MakeId(const std::string &folder)
{
	std::string id;
	for (unsigned char c : Lower(folder)) {
		if (isalnum(c)) {
			id += static_cast<char>(c);
		}
		else if (!id.empty() && id.back() != '-') {
			id += '-';
		}
	}
	while (!id.empty() && id.back() == '-') {
		id.pop_back();
	}
	return id.empty() ? "mod" : id;
}

bool IsDocumentation(const std::string &rel)
{
	static const char *kDocuments[] = {".txt", ".md", ".pdf", ".html", ".htm", ".rtf"};
	const size_t dot = rel.find_last_of('.');
	if (dot == std::string::npos || rel.find('/') != std::string::npos) return false;
	const std::string ext = rel.substr(dot);
	for (const char *d : kDocuments) {
		if (ext == d) return true;
	}
	return false;
}

bool AllowedExtension(const std::string &rel)
{
	static const char *kAllowed[] = {".pak", ".bfhp", ".xml", ".ini", ".vs", ".bmp", ".tga", ".png", ".jpg", ".wav",
		".ogg", ".mp3", ".rle", ".mmp", ".txt", ".json", ".loc", ".dat", ".sgt", ".dls", ".sty", ".avi", ".wmv", ".bik",
		".fnt", ".ttf", ".md", ".pdf", ".html", ".htm", ".rtf", ".csv", ".sc", ".ent", ".cfg", ".lst", ".42", ".usr", ".sdw", ".pass"};
	size_t dot = rel.find_last_of('.');
	size_t slash = rel.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
		return true;
	}
	const std::string ext = rel.substr(dot);
	for (const char *a : kAllowed) {
		if (ext == a) {
			return true;
		}
	}
	static const char *kForbidden[] = {".exe", ".dll", ".asi", ".bat", ".cmd", ".com", ".scr", ".ps1", ".vbs", ".js",
		".msi", ".sys", ".cpl", ".lnk", ".reg", ".jar", ".hta", ".wsf", ".pif"};
	for (const char *f : kForbidden) {
		if (ext == f) {
			return false;
		}
	}
	return true;
}

bool IsGameplayPath(const std::string &p)
{
	if (StartsWith(p, "data/")) {
		return true;
	}
	if (StartsWith(p, "civs/") || StartsWith(p, "specials/") || StartsWith(p, "events/") || StartsWith(p, "rules/") || StartsWith(p, "modes/")) {
		return true;
	}
	if (StartsWith(p, "options/") || StartsWith(p, "randommap/")) {
		return true;
	}
	// Whatever is left under rulesets/ after the declared layers have taken their files.
	if (StartsWith(p, "rulesets/")) {
		return true;
	}
	if (StartsWith(p, "randommapsettlements/")) {
		return true;
	}
	if (StartsWith(p, "packs/") && EndsWith(p, ".bfhp")) {
		return true;
	}
	if (StartsWith(p, "units/") && EndsWith(p, ".xml")) {
		return true;
	}
	if ((StartsWith(p, "buildings/") || StartsWith(p, "mapobjects/") || StartsWith(p, "visuals/")) && EndsWith(p, ".xml")) {
		return true;
	}
	if ((StartsWith(p, "units/") || StartsWith(p, "buildings/") || StartsWith(p, "mapobjects/")) && EndsWith(p, ".pass")) {
		return true;
	}
	return false;
}

bool IsContentPath(const std::string &p)
{
	return StartsWith(p, "scenarios/") || StartsWith(p, "adventures/") || StartsWith(p, "conquests/") || StartsWith(p, "conquestmaps/");
}

std::string RulesFingerprint(std::vector<RulesPart> parts)
{
	std::sort(parts.begin(), parts.end(), [](const RulesPart &a, const RulesPart &b) {
		const std::string ia = Lower(a.id), ib = Lower(b.id);
		if (ia != ib) return ia < ib;
		return a.layer != b.layer ? a.layer < b.layer : a.gameplayHash < b.gameplayHash;
	});
	std::string text = std::string(kGameId) + "|sim:" + std::to_string(kSimLevel) + "|esq:" + std::to_string(kRulesSchema) + "|";
	for (const RulesPart &p : parts) {
		if (p.gameplayHash.empty()) {
			return "";
		}
		text += p.id + (p.layer.empty() ? "" : "#" + p.layer) + "@" + p.gameplayHash + "|";
	}
	if (!parts.empty()) text += "api:" + std::to_string(kScriptApiVersion) + "|";
	return HashString(text);
}

}
