// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/diplomacyrules.h"

namespace diplomacy {

namespace {
bool Has(unsigned flags, unsigned want) { return (flags & want) == want; }
}

Preset PresetOf(unsigned mine)
{
	switch (mine & kAllFlags) {
	case 0: return kPresetWar;
	case kCeasefire: return kPresetPeace;
	case kAllianceFlags: return kPresetAlliance;
	default: return kPresetCustom;
	}
}

unsigned PresetFlags(Preset preset)
{
	switch (preset) {
	case kPresetPeace: return kCeasefire;
	case kPresetAlliance: return kAllianceFlags;
	default: return 0;
	}
}

unsigned Offered(unsigned mine, unsigned theirs)
{
	return theirs & ~mine & kAllFlags;
}

Status StatusOf(unsigned mine, unsigned theirs)
{
	const unsigned offered = Offered(mine, theirs);
	if (offered) {
		if (Has(theirs, kAllianceFlags) && !Has(mine, kAllianceFlags)) return kStatusTheyOfferAlliance;
		if (offered & kCeasefire) return kStatusTheyOfferPeace;
		return kStatusTheyGiveMore;
	}
	const bool ours = (mine & kCeasefire) != 0, theirsToo = (theirs & kCeasefire) != 0;
	if (!ours) return kStatusWar;
	if (!theirsToo) return kStatusYouOffered;
	return Has(mine, kAllianceFlags) && Has(theirs, kAllianceFlags) ? kStatusAllied : kStatusPeace;
}

bool IsOffer(Status status)
{
	return status == kStatusTheyOfferPeace || status == kStatusTheyOfferAlliance || status == kStatusTheyGiveMore;
}

}
