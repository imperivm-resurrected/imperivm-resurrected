// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/controls.h"

void ModUiOpen();
void ModUiAskRestart(const std::string &reason = "", int largeAddress = -1, const std::vector<std::string> &items = {},
	const std::string &itemsTitle = "");
bool ModUiIsOpen();
// The cheats tab or panel drawn over a match covers this point.
bool ModUiCoversPoint(int x, int y);
int ModUiUpdatesAvailable();
void ModUiResetUpdates();
void ModUiToast(const std::string &text, bool error = false);
void ModUiNotice(const std::string &title, const std::string &text);
void ModUiAsk(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> onYes);

bool ModUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
bool ModUiHandleCornerMessage(UINT msg, WPARAM wp, LPARAM lp);

bool ModUiNeedsDraw();
void ModUiDraw(uint32_t *buf, int w, int h);

void ModUiDrawHoverCard(uint32_t *buf, int w, int h, const std::string &text, COLORREF accent, int atX, int centerY,
	const std::vector<ControlKey> &keys = {});
