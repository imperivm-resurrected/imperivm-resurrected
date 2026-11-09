// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/layerdiag.h"

#include <algorithm>
#include <cctype>

#include "mods/volumes.h"

std::string LayerDiagKey(const std::string &path)
{
	std::string p = path;
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return VolumeRealPath(p);
}

void LayerDiag::Reset(const std::vector<std::string> &files)
{
	files_.clear();
	opened_.clear();
	for (const std::string &f : files) files_.emplace(LayerDiagKey(f), f);
}

std::string LayerDiag::Opened(const std::string &rel)
{
	const std::string key = LayerDiagKey(rel);
	const auto it = files_.find(key);
	if (it == files_.end() || !opened_.insert(key).second) return "";
	return it->second;
}

std::vector<std::string> LayerDiag::NotOpened() const
{
	std::vector<std::string> out;
	for (const auto &f : files_) {
		if (!opened_.count(f.first)) out.push_back(f.second);
	}
	std::sort(out.begin(), out.end());
	return out;
}
