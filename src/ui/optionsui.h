// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

void OptionsUiGameTick();
bool OptionsUiNeedsDraw();
void OptionsUiDraw(uint32_t *buf, int w, int h);
bool OptionsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
// The Controls screen is the last tab of the options: the other tabs' titles, one of them shown again, and Back, which closes the options
// as their own Back does.
std::vector<std::string> OptionsUiPageTitles();
void OptionsUiShowPage(int page);
void OptionsUiBack();
