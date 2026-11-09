// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/constswap.h"

#include <cctype>

namespace {
std::string Normal(std::string s)
{
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}
}

bool ConstSwapLayerOk(const std::vector<std::string> &files)
{
	for (const std::string &f : files) {
		if (Normal(f) != "patches/data/const.ini") return false;
	}
	return true;
}

bool ConstIniHasGamePlay(const std::string &text)
{
	size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	while (at < text.size()) {
		size_t end = text.find_first_of("\r\n", at);
		if (end == std::string::npos) end = text.size();
		size_t from = at, to = end;
		while (from < to && isspace(static_cast<unsigned char>(text[from]))) from++;
		while (to > from && isspace(static_cast<unsigned char>(text[to - 1]))) to--;
		if (Normal(text.substr(from, to - from)) == "[gameplay]") return true;
		at = end + 1;
	}
	return false;
}

ConstSwap ConstSwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	if (sameRuleset) return ConstSwap::kNothing;
	return ConstSwapLayerOk(from) && ConstSwapLayerOk(to) ? ConstSwap::kReload : ConstSwap::kRestart;
}
