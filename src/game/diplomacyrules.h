// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The four permissions one player grants another (CVXPlayer +0x24 + other * 4), as the game's diplomacy dialog sets them.
namespace diplomacy {

constexpr unsigned kCeasefire = 0x1, kSupport = 0x4, kVision = 0x10, kControl = 0x20;
constexpr unsigned kAllFlags = kCeasefire | kSupport | kVision | kControl;
constexpr unsigned kAllianceFlags = kCeasefire | kVision | kSupport;

enum Preset { kPresetWar, kPresetPeace, kPresetAlliance, kPresetCustom };

// Custom when the permissions are none of the other three.
Preset PresetOf(unsigned mine);
unsigned PresetFlags(Preset preset);

enum Status {
	kStatusWar,
	kStatusPeace,
	kStatusAllied,
	kStatusYouOffered,  // you hold the ceasefire, they do not
	kStatusTheyOfferPeace,
	kStatusTheyOfferAlliance,
	kStatusTheyGiveMore,
};

Status StatusOf(unsigned mine, unsigned theirs);
// What they grant you and you do not grant back.
unsigned Offered(unsigned mine, unsigned theirs);
bool IsOffer(Status status);

}
