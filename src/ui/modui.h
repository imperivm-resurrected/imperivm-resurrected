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
// As ModUiAsk with two answers: `yes` and `alt` each run theirs, and a third button, "Later", and Esc only close it.
void ModUiChoose(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> onYes, const std::string &alt,
	std::function<void()> onAlt);
// As ModUiChoose without "Later": `text` shows the seconds left until `deadline` (GetTickCount) where it has "%s", and then, or on
// Esc, `onAlt` runs. The answers run with the screen's lock held.
void ModUiChooseTimed(const std::string &title, const std::string &text, const std::string &yes, void (*onYes)(), const std::string &alt, void (*onAlt)(),
	unsigned deadline);
void ModUiCancelTimed();

bool ModUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
bool ModUiHandleCornerMessage(UINT msg, WPARAM wp, LPARAM lp);

bool ModUiNeedsDraw();
void ModUiDraw(uint32_t *buf, int w, int h);

void ModUiDrawHoverCard(uint32_t *buf, int w, int h, const std::string &text, COLORREF accent, int atX, int centerY,
	const std::vector<ControlKey> &keys = {});
