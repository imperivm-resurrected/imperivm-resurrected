// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

// Which files of the mounted ruleset layer the engine has asked for. The log of the VFS is rate limited and says nothing about who served a
// file; this keeps the account the layer's author needs: "the engine opened these, and never asked for those".
// A file of the layer and a name the engine asks for are the same file when their real paths are (gameres/icons/x.bmp is ui/icons/x.bmp).
class LayerDiag {
public:
	// The loose files of the layer: their paths inside it.
	void Reset(const std::vector<std::string> &files);
	// The engine asked for rel. Returns the layer's own path of that file the first time, and "" for anything else.
	std::string Opened(const std::string &rel);
	size_t Total() const { return files_.size(); }
	size_t OpenedCount() const { return opened_.size(); }
	// The layer's files that the engine has not asked for, in order.
	std::vector<std::string> NotOpened() const;

private:
	std::map<std::string, std::string> files_; // real path -> path in the layer
	std::set<std::string> opened_;
};

// A place the loose files of the active packs come from: the root of a pack, or the ruleset of a pack that the profile mounts.
struct FileSource {
	std::string label;     // "clash-a" for a root, "clash-b/blue" for a ruleset
	std::string vfsPrefix; // how the VFS names its folder: "mods/clash-b/rulesets/blue/"
	std::string diskDir;   // the folder on disk, without a final slash
};

// The files that two or more sources have under one name, to say who served them (a package root against the ruleset of another package,
// or two roots). The sources come in the order the packs are mounted: the first is the one the load order puts first.
class SharedFiles {
public:
	// files[i] are the loose paths of sources[i].
	void Reset(const std::vector<FileSource> &sources, const std::vector<std::vector<std::string>> &files);
	// The sources that have the file the engine asked for, the first time it is one that several have (indexes, in mount order). Otherwise empty.
	std::vector<size_t> Asked(const std::string &rel);
	size_t Count() const { return shared_.size(); }
	const FileSource &Source(size_t i) const { return sources_[i]; }
	// The path source i has for the name rel (as it is in that source), or "".
	std::string PathIn(size_t i, const std::string &rel) const;
	// Which of the sources `among` a VFS name belongs to (the one whose folder is the longest start of it), or -1.
	int OwnerOf(const std::string &vfsName, const std::vector<size_t> &among) const;

private:
	std::vector<FileSource> sources_;
	std::vector<std::map<std::string, std::string>> byKey_; // per source: key -> its path
	std::map<std::string, std::vector<size_t>> shared_;     // key -> the sources that have it (two or more)
	std::set<std::string> asked_;
};

// The key two names of one file share: lowercase, "/" and the volume replaced by its real folder.
std::string LayerDiagKey(const std::string &path);
