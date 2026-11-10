// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
bool CheatsPanelAvailable();
bool CheatsPanelOpen();
void CheatsPanelToggle();
bool CheatsPanelConsumeKeyOpen();
void CheatsRun(int verb, int amount);
void CheatsFrameTick();
int CheatsUnitCount();
int CheatsUnitGroup(int i);
const char *CheatsGroupName(int group);
const char *CheatsUnitName(int i);
bool CheatsUnitIsHero(int i);
const int kCheatsMaxSpawn = 100;

void CheatsSpawn(int index, int count);
bool CheatsHandleKey(unsigned msg, unsigned wp, long lp);
bool CheatsHandleCommand(void *cmd, const char *text, unsigned size);
