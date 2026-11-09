// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

namespace addr {

inline constexpr uintptr_t kFindClassByName = 0x00409e40;
inline constexpr uintptr_t kScriptFnSquadEvalAttach = 0x00424550;
inline constexpr uintptr_t kScriptFnSettlementTSResearch = 0x00426ab0;
inline constexpr uintptr_t kScriptFnHeroTSAdvHeroSkills = 0x00426c00;
inline constexpr uintptr_t kScriptFnSettlementMaxAffordCount = 0x00428a10;
inline constexpr uintptr_t kScriptFnSettlementCanAfford = 0x00428ba0;
inline constexpr uintptr_t kScriptFnSettlementCanAffordN = 0x00428d40;
inline constexpr uintptr_t kScriptFnSettlementCheckTechBudget = 0x00428ef0;
inline constexpr uintptr_t kScriptFnGateInside = 0x00429310;
inline constexpr uintptr_t kScriptFnSettlementReserveFor = 0x0042cd60;
inline constexpr uintptr_t kScriptFnSettlementFindResearchLab = 0x0042d0d0;
inline constexpr uintptr_t kScriptFnSettlementResearch = 0x0042d1b0;
inline constexpr uintptr_t kScriptFnIsResearched = 0x00433790;
inline constexpr uintptr_t kScriptFnIsResearchedPlayer = 0x00433b70;
inline constexpr uintptr_t kScriptFnIsResearching = 0x00434000;
inline constexpr uintptr_t kScriptFnIsResearchingPlayer = 0x004343e0;
inline constexpr uintptr_t kScriptFnSettlementCanResearch = 0x00434870;
inline constexpr uintptr_t kScriptFnBuildingResearch = 0x004dfcd0;
inline constexpr uintptr_t kRunPlayerVerifier = 0x004eaa40;
inline constexpr uintptr_t kRunGroupVerifier = 0x004eac10;
inline constexpr uintptr_t kIsDerivedFrom = 0x0059a220;
inline constexpr uintptr_t kIsKindOfName = 0x0059c020;
inline constexpr uintptr_t kClassStartElement = 0x0059f540;
inline constexpr uintptr_t kCreateObject = 0x005a66a0;
inline constexpr uintptr_t kScriptGetRaceStrPref = 0x005a7d40;
inline constexpr uintptr_t kScriptGetRaceStrPrefLow = 0x005a7e00;
inline constexpr uintptr_t kScriptGetPlayerRace = 0x005a80e0;
inline constexpr uintptr_t kScriptObjRaceStrPref = 0x005aab60;
inline constexpr uintptr_t kScriptObjRaceStrPrefLow = 0x005aac90;
inline constexpr uintptr_t kScriptFnObjPlayer = 0x005ab290;
inline constexpr uintptr_t kObjRace = 0x005ada20;
inline constexpr uintptr_t kScriptFnObjCmdCount = 0x005ae4d0;
inline constexpr uintptr_t kScriptCommandDefQueueRead = 0x005ae717;
inline constexpr uintptr_t kScriptCommandDefNone = 0x005ae74a;
inline constexpr uintptr_t kScriptFnObjExecCmd = 0x005b5470;
inline constexpr uintptr_t kScriptFnUnitHasFreedom = 0x005d7d30;
inline constexpr uintptr_t kReportError = 0x0068f3b0;
inline constexpr uintptr_t kAllocScriptString = 0x00695e10;
inline constexpr uintptr_t kScriptStringRelease = 0x00695e70;
inline constexpr uintptr_t kRegisterFunctionBySig = 0x00699d20;
inline constexpr uintptr_t kRegisterIntConstant = 0x0069c3a0;
inline constexpr uintptr_t kScriptEngineInit = 0x0069c5e0;
inline constexpr uintptr_t kGetScriptData = 0x0069f950;
inline constexpr uintptr_t kRunScriptByName = 0x006a0360;
inline constexpr uintptr_t kStartScriptByName = 0x006a07e0;
inline constexpr uintptr_t kLoadScriptFile = 0x006a2110;
inline constexpr uintptr_t kReloadConstIni = 0x006a4120;
inline constexpr uintptr_t kScriptEngine = 0x0082a500;
inline constexpr uintptr_t kCurScriptId = 0x0082e5d8;
inline constexpr uintptr_t kRegistry = 0x009bdb00;
inline constexpr uintptr_t kMutatingUnit = 0x009c0824;
inline constexpr uintptr_t kHandleFreeList = 0x00a0a608;
inline constexpr uintptr_t kHandleFirstFreeSync = 0x00a2a608;
inline constexpr uintptr_t kHandleFirstFreeAsync = 0x00a2a60c;
inline constexpr uintptr_t kHandleTable = 0x00a2a610;
inline constexpr uintptr_t kHandleLastFreeAsync = 0x00a6a610;
inline constexpr uintptr_t kHandleLastFreeSync = 0x00a6a614;
inline constexpr uintptr_t kCurInterpreter = 0x00a77ea8;
inline constexpr uintptr_t kCurScriptable = 0x00a77ebc;
inline constexpr uintptr_t kScriptLibrary = 0x00a77ec0;

}
