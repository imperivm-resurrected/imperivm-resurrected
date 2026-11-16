// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/looselayout.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "mods/catalogfilter.h"
#include "mods/modtext.h"

namespace {
using modtext::Lower;

std::string Extension(const std::string &file)
{
	const size_t dot = file.find_last_of('.');
	return dot == std::string::npos ? "" : Lower(file.substr(dot));
}

std::string Stem(const std::string &file)
{
	const size_t dot = file.find_last_of('.');
	return Lower(dot == std::string::npos ? file : file.substr(0, dot));
}

bool Attribute(const std::string &element, const std::string &name, std::string &value)
{
	for (size_t at = element.find(name); at != std::string::npos; at = element.find(name, at + 1)) {
		const size_t open = at + name.size();
		if (at == 0 || !isspace(static_cast<unsigned char>(element[at - 1]))) continue;
		if (element.compare(open, 2, "=\"") != 0) continue;
		const size_t close = element.find('"', open + 2);
		if (close == std::string::npos) return false;
		value = element.substr(open + 2, close - open - 2);
		return true;
	}
	return false;
}

int FolderKind(int mapKind, int category)
{
	if (mapKind == kMapUnknown) {
		if (category == kCatAdventures) return kMapAdventure;
		if (category == kCatConquests) return kMapConquest;
		return kMapScenario;
	}
	if (mapKind == kMapScenario && category == kCatAdventures) return kMapAdventure;
	return mapKind;
}
}

int BfhpMapKind(const std::string &head)
{
	const size_t start = head.find("<properties");
	if (start == std::string::npos) return kMapUnknown;
	const size_t end = head.find('>', start);
	if (end == std::string::npos) return kMapUnknown;
	const std::string element = head.substr(start, end - start);
	std::string type, adventure;
	int kind = kMapScenario;
	if (Attribute(element, "game_type", type)) {
		if (type == "0") kind = kMapScenario;
		else if (type == "1") kind = kMapAdventure;
		else if (type == "2") kind = kMapConquest;
		else return kMapUnknown;
	}
	if (kind == kMapScenario && Attribute(element, "adventure", adventure) && atol(adventure.c_str()) != 0) kind = kMapAdventure;
	return kind;
}

const char *MapKindFolder(int mapKind)
{
	if (mapKind == kMapAdventure) return "adventures";
	if (mapKind == kMapConquest) return "conquests";
	return "scenarios";
}

std::vector<LooseMove> PlanLooseFiles(const std::vector<LooseFile> &rootFiles, bool hasGameFolders, int category)
{
	std::vector<LooseMove> out;
	if (hasGameFolders) return out;
	std::vector<std::string> adventures;
	for (const LooseFile &f : rootFiles) {
		if (Extension(f.name) != ".bfhp") continue;
		const int kind = FolderKind(f.mapKind, category);
		out.push_back({f.name, MapKindFolder(kind)});
		if (kind == kMapAdventure) adventures.push_back(Stem(f.name));
	}
	for (const LooseFile &f : rootFiles) {
		if (Extension(f.name) != ".bmp") continue;
		for (const std::string &a : adventures) {
			if (Stem(f.name) == a) out.push_back({f.name, MapKindFolder(kMapAdventure)});
		}
	}
	return out;
}

std::vector<std::string> BfhpVideoReferences(const std::string &text)
{
	static const char kFolder[] = "movies";
	const size_t folderLen = sizeof(kFolder) - 1;
	std::vector<std::string> out;
	for (size_t at = 0; at + folderLen < text.size(); at++) {
		if (tolower(static_cast<unsigned char>(text[at])) != 'm' || Lower(text.substr(at, folderLen)) != kFolder) continue;
		size_t name = at + folderLen;
		if (text[name] != '\\' && text[name] != '/') continue;
		while (name < text.size() && (text[name] == '\\' || text[name] == '/')) name++;
		size_t end = name;
		while (end < text.size() && end - name < 120 && (isalnum(static_cast<unsigned char>(text[end])) || strchr("_ .()-", text[end]))) end++;
		const std::string run = Lower(text.substr(name, end - name));
		const size_t ext = run.find(".avi");
		if (ext == std::string::npos || ext == 0) continue;
		const std::string lower = run.substr(0, ext + 4);
		if (std::find(out.begin(), out.end(), lower) == out.end()) out.push_back(lower);
	}
	return out;
}

std::vector<LooseMove> PlanLooseVideos(const std::vector<std::string> &rootFiles, const std::vector<std::string> &referenced)
{
	std::vector<LooseMove> out;
	for (const std::string &f : rootFiles) {
		if (Extension(f) != ".avi") continue;
		if (std::find(referenced.begin(), referenced.end(), Lower(f)) != referenced.end()) out.push_back({f, "movies"});
	}
	return out;
}
