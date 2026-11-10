// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "game/diplomacyrules.h"

using namespace diplomacy;

int main()
{
	CHECK_EQ(PresetOf(0), kPresetWar);
	CHECK_EQ(PresetOf(kCeasefire), kPresetPeace);
	CHECK_EQ(PresetOf(kCeasefire | kVision | kSupport), kPresetAlliance);
	CHECK_EQ(PresetOf(kAllFlags), kPresetCustom);
	CHECK_EQ(PresetOf(kVision), kPresetCustom);
	CHECK_EQ(PresetOf(kCeasefire | kVision), kPresetCustom);
	// Bits the dialog does not show do not turn a preset into Custom.
	CHECK_EQ(PresetOf(kCeasefire | 0x8), kPresetPeace);
	CHECK_EQ(PresetFlags(kPresetWar), 0u);
	CHECK_EQ(PresetFlags(kPresetPeace), kCeasefire);
	CHECK_EQ(PresetFlags(kPresetAlliance), kAllianceFlags);
	CHECK_EQ(PresetFlags(kPresetCustom), 0u);
	for (int p = kPresetWar; p <= kPresetAlliance; p++) CHECK_EQ(PresetOf(PresetFlags(static_cast<Preset>(p))), static_cast<Preset>(p));

	CHECK_EQ(StatusOf(0, 0), kStatusWar);
	CHECK_EQ(StatusOf(kCeasefire, 0), kStatusYouOffered);
	CHECK_EQ(StatusOf(kAllianceFlags, 0), kStatusYouOffered);
	CHECK_EQ(StatusOf(0, kCeasefire), kStatusTheyOfferPeace);
	CHECK_EQ(StatusOf(0, kAllianceFlags), kStatusTheyOfferAlliance);
	CHECK_EQ(StatusOf(kCeasefire, kCeasefire), kStatusPeace);
	CHECK_EQ(StatusOf(kCeasefire, kAllianceFlags), kStatusTheyOfferAlliance);
	CHECK_EQ(StatusOf(kAllianceFlags, kAllianceFlags), kStatusAllied);
	CHECK_EQ(StatusOf(kAllFlags, kAllianceFlags), kStatusAllied);
	CHECK_EQ(StatusOf(kAllianceFlags, kAllFlags), kStatusTheyGiveMore);
	CHECK_EQ(StatusOf(kAllianceFlags, kCeasefire), kStatusPeace);
	CHECK_EQ(StatusOf(kCeasefire | kVision, kCeasefire | kSupport), kStatusTheyGiveMore);
	CHECK_EQ(StatusOf(0, kVision), kStatusTheyGiveMore);
	CHECK(IsOffer(kStatusTheyOfferPeace) && IsOffer(kStatusTheyOfferAlliance) && IsOffer(kStatusTheyGiveMore));
	CHECK(!IsOffer(kStatusWar) && !IsOffer(kStatusPeace) && !IsOffer(kStatusAllied) && !IsOffer(kStatusYouOffered));

	CHECK_EQ(Offered(0, kAllFlags), kAllFlags);
	CHECK_EQ(Offered(kCeasefire, kAllianceFlags), kVision | kSupport);
	CHECK_EQ(Offered(kAllFlags, kCeasefire), 0u);
	// Accepting grants back what they give: the result is never an offer any more.
	for (unsigned mine = 0; mine <= kAllFlags; mine++) {
		for (unsigned theirs = 0; theirs <= kAllFlags; theirs++) {
			if ((mine | theirs) & ~kAllFlags) continue;
			CHECK(!IsOffer(StatusOf(mine | Offered(mine, theirs), theirs)));
			CHECK_EQ(IsOffer(StatusOf(mine, theirs)), Offered(mine, theirs) != 0);
		}
	}
	return test::Summary("diplomacy");
}
