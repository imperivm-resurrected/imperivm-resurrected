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

void SharedFiles::Reset(const std::vector<FileSource> &sources, const std::vector<std::vector<std::string>> &files)
{
	sources_ = sources;
	byKey_.assign(sources.size(), std::map<std::string, std::string>());
	shared_.clear();
	asked_.clear();
	std::map<std::string, std::vector<size_t>> owners;
	for (size_t i = 0; i < sources.size() && i < files.size(); i++) {
		for (const std::string &f : files[i]) {
			const std::string key = LayerDiagKey(f);
			if (byKey_[i].emplace(key, f).second) owners[key].push_back(i);
		}
	}
	for (auto &o : owners) {
		if (o.second.size() >= 2) shared_.emplace(o.first, std::move(o.second));
	}
}

std::vector<size_t> SharedFiles::Asked(const std::string &rel)
{
	if (shared_.empty()) return {};
	const std::string key = LayerDiagKey(rel);
	const auto it = shared_.find(key);
	if (it == shared_.end() || !asked_.insert(key).second) return {};
	return it->second;
}

std::string SharedFiles::PathIn(size_t i, const std::string &rel) const
{
	if (i >= byKey_.size()) return "";
	const auto it = byKey_[i].find(LayerDiagKey(rel));
	return it == byKey_[i].end() ? "" : it->second;
}

int SharedFiles::OwnerOf(const std::string &vfsName, const std::vector<size_t> &among) const
{
	std::string name = vfsName;
	for (char &c : name) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	int best = -1;
	size_t bestLen = 0;
	for (size_t i : among) {
		if (i >= sources_.size()) continue;
		std::string prefix = sources_[i].vfsPrefix;
		for (char &c : prefix) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		if (prefix.size() > bestLen && name.compare(0, prefix.size(), prefix) == 0) {
			best = static_cast<int>(i);
			bestLen = prefix.size();
		}
	}
	return best;
}
