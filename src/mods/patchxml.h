// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct PatchedValue {
	std::string file;
	std::string key;
	std::string label;
	std::string value;
	bool removed = false;
};

// The root of a rules package may ADD to the game but not CHANGE what the game already has (see mods/rootrules.h). With addOnly an operation that adds a node,
// an attribute, a section, a key, a line or script code applies as always; one that replaces or deletes what the game has (an XML <replace>, <remove> or
// <set> of an attribute the game's node has with another value, an INI key the game's file has with another value, -Key of one of its keys, an @replace of a
// script line) is skipped, and counted. What the patch itself added counts as added, so an operation on it applies. Operations are counted one per XML
// operation, INI line (a -Key and the Key = value that follows it count once) and script directive.
struct PatchPolicy {
	bool addOnly = false;
	// The game's own INI file: "[section]key" (lowercase) to its value ("" for a line without =). Needed for INI when addOnly.
	const std::map<std::string, std::string> *gameIni = nullptr;
	unsigned skipped = 0;
	std::vector<std::string> skippedWhat;
};

// What the INI lines of a file are, in the form PatchPolicy::gameIni has.
std::map<std::string, std::string> PatchIniKeys(const std::string &data);

// The operations of a patch for a game file that would be skipped with addOnly, run on the game's file as it is given (nothing is changed).
std::vector<std::string> PatchSkippedOps(const std::string &file, const std::string &base, const std::string &patch);

class XmlPatchRun {
public:
	explicit XmlPatchRun(const std::string &base);
	~XmlPatchRun();
	XmlPatchRun(const XmlPatchRun &) = delete;
	XmlPatchRun &operator=(const XmlPatchRun &) = delete;
	bool Ok() const;
	bool Apply(const std::string &patch, std::vector<std::string> &errors, std::vector<PatchedValue> *values = nullptr, PatchPolicy *policy = nullptr);
	std::string Text() const;

private:
	struct State;
	std::unique_ptr<State> state;
};

bool PatchXmlText(std::string &data, const std::string &patch, std::vector<std::string> &errors);

bool PatchIniText(std::string &data, const std::string &patch, std::vector<std::string> &errors, std::set<std::string> *removed = nullptr, PatchPolicy *policy = nullptr);

bool PatchScriptText(std::string &data, const std::string &patch, std::vector<std::string> &errors, PatchPolicy *policy = nullptr);

std::vector<PatchedValue> PatchValues(const std::string &file, const std::string &patch);

std::vector<std::vector<PatchedValue>> PatchXmlResolve(const std::string &file, const std::string &base, const std::vector<std::string> &before,
	const std::vector<std::string> &patches);
