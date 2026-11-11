// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

namespace addr {

inline constexpr uintptr_t kSendPacket = 0x00404df0;
inline constexpr uintptr_t kTransportLog = 0x00405660;
inline constexpr uintptr_t kBuildLocalTick = 0x00405bb0;
inline constexpr uintptr_t kNetReportDesync = 0x00406e30;
inline constexpr uintptr_t kTransportCtor = 0x004071b0;
inline constexpr uintptr_t kAdvanceTick = 0x004077c0;
inline constexpr uintptr_t kDelaySite = 0x00407ca1;
inline constexpr uintptr_t kDelayResume = 0x00407caa;
inline constexpr uintptr_t kAdvanceTickCaller = 0x00407fb8;
inline constexpr uintptr_t kGameAdvUpdate = 0x004ccc80;
inline constexpr uintptr_t kWantsToQuitExecute = 0x004e60c0;
inline constexpr uintptr_t kSurrenderExecute = 0x004e6250;
inline constexpr uintptr_t kNetworkSplitExecute = 0x004e68c0;
inline constexpr uintptr_t kChatMessageExecute = 0x004e7370;
inline constexpr uintptr_t kChatMessageCreate = 0x004e8f80;
inline constexpr uintptr_t kCmdMessageCreate = 0x004e9570;
inline constexpr uintptr_t kPlaybackRecordedTick = 0x004f64a0;
inline constexpr uintptr_t kCmdQueueSetDtor = 0x004f6770;
inline constexpr uintptr_t kCmdQueueSetCtor = 0x004f6960;
inline constexpr uintptr_t kAddCommand = 0x004f6dc0;
inline constexpr uintptr_t kCmdPumpExecuteQueued = 0x004f6e80;
inline constexpr uintptr_t kBucketPush = 0x004f7940;
inline constexpr uintptr_t kCmdPumpTick = 0x004f79d0;
inline constexpr uintptr_t kQueueCommand = 0x0051d3c0;
inline constexpr uintptr_t kGameSingleUpdate = 0x0051ea30;
inline constexpr uintptr_t kRunTicksUpTo = 0x00528b40;
inline constexpr uintptr_t kStartNetwork = 0x005504b0;
inline constexpr uintptr_t kReplaySettings = 0x0058d340;
inline constexpr uintptr_t kReplayCtor = 0x0058d450;
inline constexpr uintptr_t kReplayDtor = 0x0058d470;
inline constexpr uintptr_t kReplaySerialize = 0x0058d8c0;
inline constexpr uintptr_t kSendPlayerInfo = 0x0064c8c0;
inline constexpr uintptr_t kBroadcastGameDesc = 0x0064c990;
inline constexpr uintptr_t kAddFoundGame = 0x0064d090;
inline constexpr uintptr_t kShellDtor = 0x0064d660;
inline constexpr uintptr_t kShellRemovePlayer = 0x0064d780;
inline constexpr uintptr_t kCTCPAddrGetIP = 0x0064fdd0;
inline constexpr uintptr_t kNetComputeGameVersionHash = 0x006d98d0;
inline constexpr uintptr_t kNetUiCallback = 0x006e1b90;
inline constexpr uintptr_t kWantsToQuitVtable = 0x007bbcd0;
inline constexpr uintptr_t kCmdMessageExecSlot = 0x007bbe98;
inline constexpr uintptr_t kMultiplayerVtable = 0x007c2858;
inline constexpr uintptr_t kDelayMultiplier = 0x0081a2fc;
inline constexpr uintptr_t kTickPercentAddr = 0x0081a30c;
inline constexpr uintptr_t kQuitInitial = 0x0081a310;
inline constexpr uintptr_t kQuitSubsequent = 0x0081a314;
inline constexpr uintptr_t kClassNameBasicSettings = 0x008246c0;
inline constexpr uintptr_t kClassNameMultiSettings = 0x008246d4;
inline constexpr uintptr_t kClassNameCTCPAddr = 0x00829d84;
inline constexpr uintptr_t kNetProtocol = 0x00830214;
inline constexpr uintptr_t kTransport = 0x008379dc;
inline constexpr uintptr_t kSyncState = 0x009a704c;
inline constexpr uintptr_t kGameEndTick = 0x009a707c;
inline constexpr uintptr_t kTcpGateListBegin = 0x00a084c8;
inline constexpr uintptr_t kTcpGateListEnd = 0x00a084cc;

}
