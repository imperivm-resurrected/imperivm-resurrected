// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

// The engine mounts some folders as translated volumes (the [mount] section of Config.ini, "gameres/=trans, UI/"): the game asks for
// "gameres/icons/x.bmp", the volume turns it into "ui/icons/x.bmp" and only then asks the file systems underneath, the loose layer of a
// mod among them. So a file of a mod answers the game's name only at its real path (ui/icons/x.bmp); the name with the volume in front
// (gameres/icons/x.bmp) is the one the SDK has always documented, and the DLL has to serve it itself.
//
// The table lists the volumes whose real folder is known from the game's own packs (UI\ICONS, DATA\INTERFACE\CMDBAR...) and its log.
struct VolumeName {
	const char *name; // with the slash, lowercase
	const char *real; // where the volume points, lowercase, with the slash
};
inline constexpr VolumeName kVolumeNames[] = {
	{"gameres/", "ui/"},
	{"gameini/", "data/interface/"},
	{"commonres/", "ui/common/"},
	{"commonini/", "data/interface/common/"},
	{"menures/", "ui/menu/"},
	{"menuini/", "data/interface/menu/"},
	{"cursors/", "ui/cursors/"},
	{"editorres/", "ui/editor/"},
	{"editorini/", "data/interface/editor/"},
};
inline constexpr unsigned kVolumeCount = sizeof(kVolumeNames) / sizeof(kVolumeNames[0]);

// The volume that rel (lowercase, "/") starts with, or nullptr.
const VolumeName *VolumeOf(const std::string &rel);

// rel with the volume replaced by its real folder: "gameres/icons/x.bmp" is "ui/icons/x.bmp". Anything else is returned as it came.
std::string VolumeRealPath(const std::string &rel);
