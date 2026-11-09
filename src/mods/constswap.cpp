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

ConstSwap ConstSwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	if (sameRuleset) return ConstSwap::kNothing;
	return ConstSwapLayerOk(from) && ConstSwapLayerOk(to) ? ConstSwap::kReload : ConstSwap::kRestart;
}
