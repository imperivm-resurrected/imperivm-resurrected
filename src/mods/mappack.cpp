// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/mappack.h"

#include <cctype>
#include "mods/modtext.h"

namespace {
using modtext::Lower;

bool EndsWith(const std::string &s, const char *suffix)
{
	const std::string x = suffix;
	return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

bool IsScenarioFile(const std::string &p)
{
	static const char kFolder[] = "scenarios/";
	if (p.compare(0, sizeof(kFolder) - 1, kFolder) != 0) return false;
	const std::string leaf = p.substr(sizeof(kFolder) - 1);
	if (leaf.empty() || leaf.find('/') != std::string::npos) return false;
	return EndsWith(leaf, ".bfhp") || EndsWith(leaf, ".bmp");
}

bool IsListedFromDisk(const std::string &p)
{
	static const char *const kFolders[] = {"scenarios/", "adventures/", "conquests/", "conquestmaps/"};
	for (const char *f : kFolders) {
		const std::string folder = f;
		if (p.size() > folder.size() && p.compare(0, folder.size(), folder) == 0) return true;
	}
	return false;
}

bool IsDocument(const std::string &p)
{
	if (p.find('/') != std::string::npos) return false;
	const size_t dot = p.find_last_of('.');
	if (dot == std::string::npos) return true;
	static const char *const kDocs[] = {".txt", ".md", ".pdf", ".html", ".htm", ".rtf", ".csv", ".db"};
	const std::string ext = p.substr(dot);
	for (const char *d : kDocs) {
		if (ext == d) return true;
	}
	return false;
}
}

bool PackLoadsWithoutRestart(const MapPackFacts &facts, const std::vector<std::string> &paths)
{
	if (facts.rules || facts.rleTail || facts.forbidden || facts.patchValues || facts.hidden) return false;
	for (const std::string &raw : paths) {
		const std::string p = Lower(raw);
		if (!IsListedFromDisk(p) && !IsDocument(p)) return false;
	}
	return true;
}

bool MapPackIsSimple(const MapPackFacts &facts, const std::vector<std::string> &paths)
{
	if (facts.rules || facts.rleTail || facts.forbidden || facts.patchValues || facts.hidden) return false;
	bool scenario = false;
	for (const std::string &raw : paths) {
		const std::string p = Lower(raw);
		if (IsScenarioFile(p)) {
			scenario = scenario || EndsWith(p, ".bfhp");
			continue;
		}
		if (!IsDocument(p)) return false;
	}
	return scenario;
}
