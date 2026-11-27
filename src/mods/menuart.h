// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <functional>
#include <string>
#include <vector>

bool ScaleBmpToCover(const std::vector<unsigned char> &src, int w, int h, std::vector<unsigned char> &out);

bool ExtendBarBmp(const std::vector<unsigned char> &src, int w, std::vector<unsigned char> &out);

bool WidenBarRects(std::string &ini, int from, int to);

bool MenuLayoutIsFixed(const std::string &mainMenu, const std::string &tmpl);

// The files of the game's main menu screen: its layout (mainmenu*.ini), the background shared by the menus (menuback*.ini) and the
// pictures of that background (CurrentLang/MenuBackground*.bmp). The mod draws its own main menu over that screen, so they are always
// the game's: a content pack's copy would add its own panels (tips, logos, credits) and background under it. Any spelling of the path.
bool IsMenuShellFile(const std::string &path);

bool FitFullFrameImages(std::string &ini, const std::function<std::string(const std::string &image)> &fit);
