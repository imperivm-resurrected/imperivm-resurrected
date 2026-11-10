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

bool FullSwapLayerOk(const std::vector<std::string> &files)
{
	static const char kPrefix[] = "patches/data/";
	for (const std::string &raw : files) {
		const std::string f = Normal(raw);
		if (f.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0 || f.size() == sizeof(kPrefix) - 1) return false;
		const size_t dot = f.find_last_of('.');
		const std::string ext = dot == std::string::npos ? std::string() : f.substr(dot);
		if (ext != ".xml" && ext != ".ini" && ext != ".vs") return false;
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

ConstSwap SwapDecide(bool sameRuleset, bool full, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	const ConstSwap simple = ConstSwapDecide(sameRuleset, from, to);
	if (simple != ConstSwap::kRestart) return simple;
	return full && FullSwapLayerOk(from) && FullSwapLayerOk(to) ? ConstSwap::kFull : ConstSwap::kRestart;
}
