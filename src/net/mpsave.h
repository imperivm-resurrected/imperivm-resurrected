// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
void InstallMpSave(bool knownExe);
bool MpSaveInMultiplayer();
#include <string>
void MpSaveSetPendingLoad(const std::string &path);
std::string MpSavePendingLoad();
#include <vector>
struct MpSavePlayer {
	int type = 4;
	int color555 = 0;
	int control = 0;
	bool used() const { return !name.empty(); }
	bool ai() const { return name.size() > 4 && (name.find("(IA)") != std::string::npos || name.find("(AI)") != std::string::npos); }
	std::string name;
};
struct MpSaveInfo {
	bool valid = false;
	std::vector<MpSavePlayer> players;
	std::string map;
	bool random = false;
	int sizeX = -1, sizeY = -1;
};
MpSaveInfo MpSaveReadInfo(const std::string &path);
void MpSaveSetPlayerCamera(int player, int x, int y);
bool MpSaveLocalCamera(int &player, int &x, int &y);
void MpSaveSetPendingCameras(const std::string &text);
