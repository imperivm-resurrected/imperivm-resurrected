// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/folderlist.h"

#include <string.h>

#include <algorithm>

std::vector<std::string> ParseFolderList(const std::string &text)
{
	std::vector<std::string> out;
	for (size_t p = 0; p < text.size();) {
		const size_t q = text.find('|', p);
		const std::string f = text.substr(p, q == std::string::npos ? std::string::npos : q - p);
		if (!f.empty()) out.push_back(f);
		p = q == std::string::npos ? text.size() : q + 1;
	}
	return out;
}

std::string JoinFolderList(const std::vector<std::string> &list)
{
	std::string s;
	for (const std::string &f : list) s += (s.empty() ? "" : "|") + f;
	return s;
}

bool RemoveFolderFrom(std::vector<std::string> &list, const std::string &folder)
{
	const size_t before = list.size();
	list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string &f) { return _stricmp(f.c_str(), folder.c_str()) == 0; }), list.end());
	return list.size() != before;
}
