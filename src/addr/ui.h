// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

namespace addr {

inline constexpr uintptr_t kIsNewUpdateAvailable = 0x00408a30;
inline constexpr uintptr_t kUpdateXmlExists = 0x00408ab0;
inline constexpr uintptr_t kChatSend = 0x004e51e0;
inline constexpr uintptr_t kRegisterCommands = 0x004ee870;
inline constexpr uintptr_t kSelectionRevalidate = 0x005b9560;
inline constexpr uintptr_t kRebuildCommandList = 0x005e7710;
inline constexpr uintptr_t kWindowHasFlags = 0x00631ca0;
inline constexpr uintptr_t kModifyFlags = 0x00631cc0;
inline constexpr uintptr_t kBringChildToTop = 0x00631ed0;
inline constexpr uintptr_t kInvalidateRect = 0x00631f60;
inline constexpr uintptr_t kComboGetSelected = 0x006617d0;
inline constexpr uintptr_t kComboSetSelected = 0x006617f0;
inline constexpr uintptr_t kSetSelectedItem = 0x00664a50;
inline constexpr uintptr_t kItemListRemove = 0x00664ef0;
inline constexpr uintptr_t kListSelectExclusive = 0x00664ff0;
inline constexpr uintptr_t kItemListInsert = 0x00665a00;
inline constexpr uintptr_t kShowModalMenu = 0x0066c1c0;
inline constexpr uintptr_t kScrollSetRange = 0x0066c650;
inline constexpr uintptr_t kTextGetTextA = 0x0066e370;
inline constexpr uintptr_t kGetTextUtf8 = 0x0066e3b0;
inline constexpr uintptr_t kSetTextUtf8 = 0x00670570;
inline constexpr uintptr_t kPumpMessages = 0x00676960;
inline constexpr uintptr_t kOnCommandClicked = 0x006b3500;
inline constexpr uintptr_t kOnHotkeyChar = 0x006b3620;
inline constexpr uintptr_t kCmdBarRebuild = 0x006b3d60;
inline constexpr uintptr_t kShowEndMultiplayerDlg = 0x006b46d0;
inline constexpr uintptr_t kNotificationOnKey = 0x006b6f10;
inline constexpr uintptr_t kShowConquestGameDialog = 0x006c9080;
inline constexpr uintptr_t kEndMenuOnShow = 0x006cd7f0;
inline constexpr uintptr_t kGameMenuOnShow = 0x006cef30;
inline constexpr uintptr_t kGameMenuOnMessage = 0x006cf150;
inline constexpr uintptr_t kMainMenuDtor = 0x006d7e90;
inline constexpr uintptr_t kMainMenuOnShow = 0x006d82c0;
inline constexpr uintptr_t kMainMenuOnMessage = 0x006d8590;
inline constexpr uintptr_t kMenuSwitchInvalidate = 0x006d924a;
inline constexpr uintptr_t kGetGameOptions = 0x006d9780;
inline constexpr uintptr_t kGetLocalSlot = 0x006d97d0;
inline constexpr uintptr_t kIsHostOrOffline = 0x006d98b0;
inline constexpr uintptr_t kPublishGameOptions = 0x006d9ba0;
inline constexpr uintptr_t kGetSlotInfo = 0x006d9f20;
inline constexpr uintptr_t kUpdatePlayerCounts = 0x006da190;
inline constexpr uintptr_t kFindFreeColor = 0x006da450;
inline constexpr uintptr_t kUpdateSlotRow = 0x006da4e0;
inline constexpr uintptr_t kSetSlotInfo = 0x006db880;
inline constexpr uintptr_t kBuildSettings = 0x006dbba0;
inline constexpr uintptr_t kSwapSlots = 0x006dc210;
inline constexpr uintptr_t kRefreshOptions = 0x006dcd10;
inline constexpr uintptr_t kMpGameOnShow = 0x006dcfe0;
inline constexpr uintptr_t kMpGameDtor = 0x006de1e0;
inline constexpr uintptr_t kFnHostGame = 0x006e1ed0;
inline constexpr uintptr_t kJoinGameByIP = 0x006e2c10;
inline constexpr uintptr_t kStartLanBrowser = 0x006e3660;
inline constexpr uintptr_t kJoinSelectedGame = 0x006e3e20;
inline constexpr uintptr_t kOnRefreshClicked = 0x006e3f20;
inline constexpr uintptr_t kLobbyUpdate = 0x006e4cd0;
inline constexpr uintptr_t kPopulateControls = 0x006e75d0;
inline constexpr uintptr_t kReadControls = 0x006e78a0;
inline constexpr uintptr_t kApplyGameOptions = 0x006e7ff0;
inline constexpr uintptr_t kOptionsOnMessage = 0x006e8240;
inline constexpr uintptr_t kShowOptions = 0x006e9140;
inline constexpr uintptr_t kFillFileList = 0x006f3640;
inline constexpr uintptr_t kShowSaveDialog = 0x006f4510;
inline constexpr uintptr_t kShowLoadDialog = 0x006f45c0;
inline constexpr uintptr_t kAskYesNo = 0x006f5bf0;
inline constexpr uintptr_t kApplyToGame = 0x006f5d30;
inline constexpr uintptr_t kOnApplyOptions = 0x006f6320;
inline constexpr uintptr_t kPickerDtor = 0x006f9480;
inline constexpr uintptr_t kPickerOnShow = 0x006f9de0;
inline constexpr uintptr_t kClassNameCWindow = 0x00826c1c;
inline constexpr uintptr_t kClassNameCUIButton = 0x00829df4;
inline constexpr uintptr_t kClassNameCUIComboBox = 0x00829df8;
inline constexpr uintptr_t kClassNameCUIImgButton = 0x00829e14;
inline constexpr uintptr_t kClassNameCUISList = 0x00829e18;
inline constexpr uintptr_t kClassNameCUIScroll = 0x0082a300;
inline constexpr uintptr_t kClassNameCUITextW = 0x0082a308;
inline constexpr uintptr_t kClassNameCVXUIConfirm = 0x0082e7c4;
inline constexpr uintptr_t kClassNameCVXUIGameMenu = 0x0082e808;
inline constexpr uintptr_t kClassNameCVXUIOptions = 0x0082ef54;
inline constexpr uintptr_t kPlayerColorTable = 0x009d152c;
inline constexpr uintptr_t kZoomDialog = 0x009d2cc4;
inline constexpr uintptr_t kOptionsDialog = 0x00a83530;
inline constexpr uintptr_t kSaveDlg = 0x00a83714;
inline constexpr uintptr_t kLoadDlg = 0x00a83718;
inline constexpr uintptr_t kGameOptionsDialog = 0x00a83764;

}
