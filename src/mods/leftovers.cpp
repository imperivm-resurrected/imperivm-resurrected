// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/leftovers.h"

#include <cctype>
#include <cstring>
#include "mods/modtext.h"

namespace {
using modtext::Lower;

bool StartsWith(const std::string &s, const char *prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }

bool EndsWithStamp(const std::string &stem)
{
	const size_t n = stem.size();
	if (n < 16 || stem[n - 16] != '-' || stem[n - 7] != '-') return false;
	for (size_t i = n - 15; i < n; i++) {
		if (i != n - 7 && !isdigit(static_cast<unsigned char>(stem[i]))) return false;
	}
	return true;
}
}

bool IsDownloadLeftover(const std::string &name, bool directory)
{
	const std::string n = Lower(name);
	if (n.empty() || n == "." || n == "..") return false;
	if (directory) return n == "room" || StartsWith(n, "update-") || (StartsWith(n, "tmp-") && EndsWithStamp(n));
	if (StartsWith(n, "update-") && n.size() > 11 && n.compare(n.size() - 4, 4, ".zip") == 0) return true;
	const size_t dot = n.find_last_of('.');
	return dot != std::string::npos && EndsWithStamp(n.substr(0, dot));
}
