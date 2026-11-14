// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/basescenarios.h"

#include <cstring>

namespace {

// The files listed under Scenarios\ by the depot manifests, whose SHA-1 matched the installed copies these hashes come from.
const struct {
	const char *file, *sha256;
} kScenarios[] = {
	{"Allenamento scontri egitto.BFHP", "a55d83a79dee28a504ec7c3cf167b0c09ade77d0824869ea58cdaad7ea6ec0e5"},
	{"assedio al norte de africa.BFHP", "7cfe99e8e4846efbd7a85004b48f456625aceed64e276d8130b5eb7f39f94a41"},
	{"Balcans.BFHP", "b49d931ad401be2b70ce0fec92e460e701f0160de92608ed77c61b6772e1216a"},
	{"BATALLA_POR_IBERIA.BFHP", "f3a7c1f92ce883317cd61437c156acc8d9fe85bf1acccd7812fb53466bba9671"},
	{"Crossroads.BFHP", "6995a8f3bb405a025613fc7733788fe54fe0bcddfa931c86b10800b6633b835f"},
	{"El_hijo_del_Ankou.BFHP", "b0e5c05f042fcc2325dba67e39182525d7e7268ce4209413bc6acf1580e68f0a"},
	{"GreatBattlesZama.BFHP", "5754925983ecb68245af88e81c329c477ec7009b71660752b42f56a5922cc7f4"},
	{"guerra_de_las_naciones.BFHP", "e9c8a478297ca6ff6a2ba005f768cb5b5bce5a9978449009605475773e2406cb"},
	{"iberia_v_germania.BFHP", "6b1e4d061a1a6b22029a5cfe1c5ebe8b1df3d83537fe6dc864237ec6242fe21d"},
	{"Infierno_en_Iberia.BFHP", "1b5658deb0cc5006d13f989120b297a0623dd59bc9a030f0a8f19c86a4eef2c3"},
	{"Island War.BFHP", "1539cec8033f6dd0bd182689667318cab391da237c320c7d2e94e20ceaf934a0"},
	{"loco.BFHP", "68f836236ea3b309a1d642134d83e1ef5644f6251f3f9b98728ab2237057c7c3"},
	{"Map 1_vs_1_varios_by_LukkyFrost.BFHP", "3f427effca4af745528c37a2a7a238be65d76299da19db079ced5f863e36d2b5"},
	{"MapaFinal1vs1.BFHP", "b005c32b7749a9d4a7c17c6e984bd321c7ca57a9ab0c9bb750a582f9ef422a9a"},
	{"mapa_duelos_egipto.BFHP", "d648720d1d241c2e2666fcfe1616d32e5f3c31e447c509d9ae0a74c8698f21c4"},
	{"mapa_factor_egipcio.BFHP", "a3c1a12d9eed08808e45253b15dcf43f73b355613eda2858a39e6d0c1d9b2565"},
	{"mediterranean.BFHP", "611f962f9e80464890acd34d9e366e54fce863424a96d59da731ea03e621e499"},
	{"roma_contra_los_barbaros.BFHP", "d53d4b2a6f9785626564acd771e572b0fa8677cfe4f8fe24b5e4dc57cfe39f76"},
	{"roma_v_egipto_2_.BFHP", "ed1fd6600393e5a2b55d540630056a4ae8db9095e15cd6ed941a052c221c2a23"},
	{"Torneo_1on1_Brita_Ibe.BFHP", "683999252a0cf694530ee5a14cf7ebb57fa3c2d549b341626e06a0ff447e0350"},
	{"Torneo_1on1_Brita_Ibe_N.BFHP", "3e1911fd76a209aa9e712f593514cbbd0e86209ac16803b891a7dcc11932586d"},
	{"Torneo_1vs1_Egypt.BFHP", "7c4198a92ac1524d2aa26b48c25bb63914fe4a9cf3fdc691574ed5687a89ca35"},
	{"Torneo_2on2_AnyCiv_v01.BFHP", "fe0ea19716d68e43521a275e92e7455ba106702e4b0f397e88c9c35bacb3cd7e"},
	{"Torneo_2on2_Egyps.BFHP", "62ab0b679117125d085cc144a77f5dd410f92898caba3b119f8313c3b86be7eb"},
	{"Torneo_2on2_Ibe_Egyp.BFHP", "f243c743de2044b8db202f459e5d78cac283d4768061c99ff699676376114c51"},
	{"Tournament_1vs1_Varios_AnyCiv_by_Clan_LegioX.BFHP", "565c8b2bf409f5395385039c8c4a119c614e9d9aa7ccb2b0560f962a24f968c4"},
};

}

const char *BaseScenarioSha256(const std::string &fileName)
{
	for (const auto &s : kScenarios) {
		if (_stricmp(s.file, fileName.c_str()) == 0) return s.sha256;
	}
	return nullptr;
}

bool BaseScenarioIs(const std::string &fileName, const std::string &sha256)
{
	const char *own = BaseScenarioSha256(fileName);
	return own && sha256 == own;
}
