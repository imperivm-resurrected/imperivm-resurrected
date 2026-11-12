// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "mods/savename.h"

namespace {

void Check(const std::string &baseName, const std::vector<std::string> &titles, const char *name, const char *campaign, int title)
{
	const SaveNameParts p = SaveNameSplit(baseName, titles);
	CHECK_EQ(p.name, std::string(name));
	CHECK_EQ(p.campaign, std::string(campaign));
	CHECK_EQ(p.title, title);
	if (!p.campaign.empty()) CHECK_EQ(p.name + "-" + p.campaign, baseName);
	else CHECK_EQ(p.name, baseName);
}

void TitlesWithDashesStayWhole()
{
	Check("guardar rapidamente-Imperivm I - La guerra de las Galias", {}, "guardar rapidamente", "Imperivm I - La guerra de las Galias", -1);
	Check("antes-del-campamento-Imperivm I - La guerra de las Galias", {}, "antes-del-campamento", "Imperivm I - La guerra de las Galias", -1);
	Check("Autoguardado 17h27-Desembarco en Africa - 204 a.C.", {}, "Autoguardado 17h27", "Desembarco en Africa - 204 a.C.", -1);
	Check("Dia 3 - tarde-El sitio de Alesia - 52 a.C.", {}, "Dia 3 - tarde", "El sitio de Alesia - 52 a.C.", -1);
	Check("mi partida-Galias", {}, "mi partida", "Galias", -1);
}

void KnownTitlesWin()
{
	const std::vector<std::string> titles = {"Galias", "Imperivm I - La guerra de las Galias", "Desembarco en Africa - 204 a.C.", "Guerras Greco-Punicas"};
	Check("guardar rapidamente-Imperivm I - La guerra de las Galias", titles, "guardar rapidamente", "Imperivm I - La guerra de las Galias", 1);
	Check("antes-del-campamento-Imperivm I - La guerra de las Galias", titles, "antes-del-campamento", "Imperivm I - La guerra de las Galias", 1);
	Check("autoguardado 17h27-desembarco en africa - 204 a.C.", titles, "autoguardado 17h27", "desembarco en africa - 204 a.C.", 2);
	Check("mi partida-Guerras Greco-Punicas", titles, "mi partida", "Guerras Greco-Punicas", 3);
	Check("mi partida-Galias", titles, "mi partida", "Galias", 0);
	Check("mi partida-Guerras Greco-Punicas", {}, "mi partida-Guerras Greco", "Punicas", -1);
	Check("mi partida-Otra campana", titles, "mi partida", "Otra campana", -1);
	Check("-Galias", titles, "-Galias", "", -1);
	Check("Galias", titles, "Galias", "", -1);
}

void NamesWithoutACampaignStayWhole()
{
	Check("partida", {}, "partida", "", -1);
	Check("", {}, "", "", -1);
	Check("-partida", {}, "-partida", "", -1);
	Check("partida-", {}, "partida-", "", -1);
	Check("a - b", {}, "a ", " b", -1);
}

}

int main()
{
	TitlesWithDashesStayWhole();
	KnownTitlesWin();
	NamesWithoutACampaignStayWhole();
	return test::Summary("savename");
}
