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

// The key two names of one file share: lowercase, "/" and the volume replaced by its real folder.
std::string LayerDiagKey(const std::string &path);
