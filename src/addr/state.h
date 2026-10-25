// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

namespace addr {

inline constexpr uintptr_t kCmdContextInts = 0x0081b2d0;
inline constexpr uintptr_t kCmdContextParam = 0x008210c8;
inline constexpr uintptr_t kCmdContextText2 = 0x008210e4;
inline constexpr uintptr_t kCmdContextText3 = 0x00821100;
inline constexpr uintptr_t kSaveName = 0x00830254;
inline constexpr uintptr_t kCmdRegistry = 0x009969dc;
inline constexpr uintptr_t kMapView = 0x00996ff0;
inline constexpr uintptr_t kGame = 0x00996ff4;
inline constexpr uintptr_t kPausedMenu = 0x0099700c;
inline constexpr uintptr_t kSyncHashes = 0x009a7054;
inline constexpr uintptr_t kMap = 0x009a721c;
inline constexpr uintptr_t kDecorEntities = 0x009a7a78;
inline constexpr uintptr_t kNoteManager = 0x009bdaa8;
inline constexpr uintptr_t kPlayerProfile = 0x009bdac0;
inline constexpr uintptr_t kLoadStream = 0x009bdaf8;
inline constexpr uintptr_t kPersistVersion = 0x009bdafc;
inline constexpr uintptr_t kSelectionManager = 0x009c0938;
inline constexpr uintptr_t kVisibleMgr = 0x009edd18;
inline constexpr uintptr_t kLanguageSettings = 0x00a6a618;
inline constexpr uintptr_t kUiLocalization = 0x00a77e94;
inline constexpr uintptr_t kSoundManager = 0x00a78420;
inline constexpr uintptr_t kUIRoot = 0x00a7abf0;
inline constexpr uintptr_t kMenuFrame = 0x00a8336c;
inline constexpr uintptr_t kWinOs = 0x00a87340;
inline constexpr uintptr_t kEditorMode = 0x00a8734c;
inline constexpr uintptr_t kSaveRequest = 0x00a87350;
inline constexpr uintptr_t kLoadRequest = 0x00a87354;
inline constexpr uintptr_t kReplayRequest = 0x00a87358;
inline constexpr uintptr_t kLoadingSavedGame = 0x00a87390;
inline constexpr uintptr_t kGameSettings = 0x00a87c48;

}
