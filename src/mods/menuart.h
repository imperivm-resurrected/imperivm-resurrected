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

bool FitFullFrameImages(std::string &ini, const std::function<std::string(const std::string &image)> &fit);
