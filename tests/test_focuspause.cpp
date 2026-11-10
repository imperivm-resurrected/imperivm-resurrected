// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "hooks/focuspause.h"

static int g_gameA = 0, g_gameB = 0;

static void PausesAndResumes()
{
	FocusPause f;
	CHECK(f.Leave(true, &g_gameA, false, false));
	CHECK(f.Return(&g_gameA, true));
	CHECK(!f.Return(&g_gameA, true));
}

static void LeavesOthersAlone()
{
	FocusPause f;
	CHECK(!f.Leave(false, &g_gameA, false, false));
	CHECK(!f.Return(&g_gameA, true));
	CHECK(!f.Leave(true, nullptr, false, false));
	CHECK(!f.Leave(true, &g_gameA, true, false));
	CHECK(!f.Return(&g_gameA, true));
	// Paused by the player before leaving: it stays paused on return.
	CHECK(!f.Leave(true, &g_gameA, false, true));
	CHECK(!f.Return(&g_gameA, true));
}

static void MatchChangedOrResumed()
{
	FocusPause f;
	CHECK(f.Leave(true, &g_gameA, false, false));
	CHECK(!f.Return(&g_gameB, true));
	CHECK(f.Leave(true, &g_gameA, false, false));
	CHECK(!f.Return(&g_gameA, false));
	CHECK(f.Leave(true, &g_gameA, false, false));
	CHECK(!f.Return(nullptr, false));
}

int main()
{
	PausesAndResumes();
	LeavesOthersAlone();
	MatchChangedOrResumed();
	return test::Summary("focuspause");
}
