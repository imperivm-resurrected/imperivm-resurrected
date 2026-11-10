// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/modcredits.h"

#include <string>

#include "core/version.h"
#include "i18n/tr.h"

ModCredits ResurrectedCredits()
{
	ModCredits c;
	c.name = "Imperivm Resurrected";
	c.version = ModVersionText();
	c.tagline = Tr("Mod for Imperivm HD / Great Battles of Rome");
	c.author = "THIONG";
	c.summary = {Tr("Modern rendering and audio, Steam multiplayer, content manager with GameBanana,"), Tr("saved-games screens and much more")};
	c.groups = {
		{Tr("Thanks to"), {"Syncrash - AlvaroPeyleth", "ImperivmVoicesPatch - Upercat"}},
		{Tr("Libraries"), {"MinHook - Tsuda Kageyu", "miniaudio - David Reid", "miniz - Rich Geldreich", "Steamworks SDK - Valve"}},
	};
	return c;
}

std::string ResurrectedCreditsText(const ModCredits &c)
{
	const std::string bar = "==================================================";
	const std::string nl = "\r\n";
	std::string t;
	t += c.name + " " + c.version + nl;
	t += c.tagline + nl + nl + nl;
	t += bar + nl + nl + nl;
	t += c.name + nl;
	t += c.author + nl + nl;
	for (const std::string &line : c.summary) t += line + nl;
	t += nl + nl;
	for (const ModCreditsGroup &g : c.groups) {
		t += g.title + nl + nl;
		for (const std::string &name : g.names) t += name + nl;
		t += nl + nl;
	}
	t += bar + nl + nl + nl;
	return t;
}
