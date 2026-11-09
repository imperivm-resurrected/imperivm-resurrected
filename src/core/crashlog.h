// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

void InstallCrashLog();
void CrashLogLoadingBegin();
// True when this call ended a load (the first game tick after it began): the caller can do its own report of the load.
bool CrashLogLoadingEnd();

extern thread_local int g_crashLogQuiet;
