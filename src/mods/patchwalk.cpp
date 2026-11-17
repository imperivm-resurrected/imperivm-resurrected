// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/patchwalk.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "mods/rootrules.h"

namespace fs = std::filesystem;

namespace {
// The patches of the menus and the rest of the interface are the look of the game, not its rules: they apply whatever the ruleset is, until the skins exist.
bool Interface(const std::string &rel)
{
	return mods_impl::RootGamePath(rel).compare(0, 15, "data/interface/") == 0;
}
}

std::vector<PatchFileFound> PatchWalk(const std::string &dir, bool addOnly)
{
	std::vector<PatchFileFound> out;
	std::error_code ec;
	const fs::path root(dir);
	for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code kind;
		if (!it->is_regular_file(kind) || kind) continue;
		std::string rel = it->path().lexically_relative(root).generic_string();
		for (char &c : rel) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		out.push_back({rel, it->path().string(), addOnly && !Interface(rel)});
	}
	return out;
}
