// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

namespace addr {

inline constexpr uintptr_t kGameCodeBegin = 0x00400000;
inline constexpr uintptr_t kStringAssign = 0x00401ec0;
inline constexpr uintptr_t kStringAssignCstr = 0x00402160;
inline constexpr uintptr_t kStringAppend = 0x004045c0;
inline constexpr uintptr_t kVectorPointPushBack = 0x0040a2a0;
inline constexpr uintptr_t kObjListCtor = 0x0041edd0;
inline constexpr uintptr_t kDequeUShortPushBack = 0x0041f290;
inline constexpr uintptr_t kConsolePrintf = 0x00619060;
inline constexpr uintptr_t kVfsFindFirst = 0x0063a4a0;
inline constexpr uintptr_t kVfsFindNext = 0x0063a550;
inline constexpr uintptr_t kVfsFindClose = 0x0063a5c0;
inline constexpr uintptr_t kVfsFileExists = 0x0063a790;
inline constexpr uintptr_t kRandomRange = 0x0063ff00;
inline constexpr uintptr_t kVfsOpenCompressedAware = 0x00644560;
inline constexpr uintptr_t kMemLock = 0x00646380;
inline constexpr uintptr_t kMemUnlock = 0x00646510;
inline constexpr uintptr_t kCTransFSCtor = 0x00660ab0;
inline constexpr uintptr_t kStrDupPool = 0x00665030;
inline constexpr uintptr_t kFreePoolString = 0x00665070;
inline constexpr uintptr_t kCWinFileServiceCtor = 0x00674ea0;
inline constexpr uintptr_t kParseFile = 0x0067a880;
inline constexpr uintptr_t kOpenMappedFileSource = 0x0067b390;
inline constexpr uintptr_t kLanguagePersist = 0x0067b920;
inline constexpr uintptr_t kHasLocalNotes = 0x0067ba00;
inline constexpr uintptr_t kStreamLoadCtor = 0x00683ad0;
inline constexpr uintptr_t kSaveStreamCtor = 0x006851d0;
inline constexpr uintptr_t kUnlinkThread = 0x00687b50;
inline constexpr uintptr_t kStopThread = 0x00687bf0;
inline constexpr uintptr_t kDeleteThread = 0x00687ce0;
inline constexpr uintptr_t kLinkThread = 0x00688060;
inline constexpr uintptr_t kRescheduleThread = 0x006880f0;
inline constexpr uintptr_t kStartThread = 0x00688150;
inline constexpr uintptr_t kSetThreadTime = 0x00688260;
inline constexpr uintptr_t kLocFormatV = 0x006897a0;
inline constexpr uintptr_t kLocFormat = 0x00689c90;
inline constexpr uintptr_t kEnvGetInt = 0x006ac740;
inline constexpr uintptr_t kMountPakFilesInDir = 0x0074c400;
inline constexpr uintptr_t kLoadConfigIni = 0x0074cd20;
inline constexpr uintptr_t kOperatorNew = 0x00762870;
inline constexpr uintptr_t kGameFree = 0x007629b9;
inline constexpr uintptr_t kPersistVersionSrc = 0x008212f4;
inline constexpr uintptr_t kVfsOpenFilePtr = 0x00829b30;
inline constexpr uintptr_t kPfnVfsFileExists = 0x00829b50;
inline constexpr uintptr_t kGameCodeEnd = 0x00900000;

}
