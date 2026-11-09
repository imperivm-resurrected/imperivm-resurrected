// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "mods/modclash.h"

namespace {

ClashMod Mod(const std::string &id, std::vector<std::string> loose, std::vector<std::string> packed = {}, std::vector<std::string> declares = {})
{
	ClashMod m;
	m.folder = id + "-folder";
	m.id = id;
	m.loose = loose;
	m.paths = loose;
	m.paths.insert(m.paths.end(), packed.begin(), packed.end());
	m.declares = declares;
	return m;
}

ClashMod Patcher(const std::string &id, std::vector<PatchedValue> values, std::vector<std::string> declares = {})
{
	ClashMod m = Mod(id, {}, {}, declares);
	m.values = values;
	return m;
}

PatchedValue Set(const std::string &key, const std::string &value, const std::string &file = "data/units.xml")
{
	PatchedValue v;
	v.file = file;
	v.key = key;
	v.label = key;
	v.value = value;
	return v;
}

PatchedValue Unset(const std::string &key)
{
	PatchedValue v = Set(key, "");
	v.removed = true;
	return v;
}

const ValueClash *Lost(const ClashReport &r, const std::string &used, const std::string &lost)
{
	for (const ValueClash &c : r.values) {
		if (c.used == used + "-folder" && c.lost == lost + "-folder") return &c;
	}
	return nullptr;
}

const ClashFile *File(const ClashReport &r, const std::string &path)
{
	for (const ClashFile &f : r.files) {
		if (f.path == path) return &f;
	}
	return nullptr;
}

const ClashPair *Pair(const ClashReport &r, const std::string &a, const std::string &b)
{
	for (const ClashPair &p : r.pairs) {
		if ((p.a == a + "-folder" && p.b == b + "-folder") || (p.a == b + "-folder" && p.b == a + "-folder")) return &p;
	}
	return nullptr;
}

std::string Used(const ClashReport &r, const std::string &path)
{
	const ClashFile *f = File(r, path);
	return f ? f->used : "(none)";
}

void Kinds()
{
	CHECK_EQ(ClashKindOf("data/units.xml"), static_cast<int>(kClashGameplay));
	CHECK_EQ(ClashKindOf("units/legionary/legionary.xml"), static_cast<int>(kClashGameplay));
	CHECK_EQ(ClashKindOf("units/legionary/walk.rle"), static_cast<int>(kClashCosmetic));
	CHECK_EQ(ClashKindOf("scenarios/alesia.bfhp"), static_cast<int>(kClashContent));
	CHECK_EQ(ClashKindOf("gameres/interface/menu.tga"), static_cast<int>(kClashCosmetic));
	CHECK_EQ(ClashKindOf("currentlang/voices/hero/01.ogg"), static_cast<int>(kClashCosmetic));
}

void NothingShared()
{
	const ClashReport r = ClashAnalyze({Mod("a", {"gameres/a.tga"}), Mod("b", {"gameres/b.tga"})}, {});
	CHECK(r.files.empty());
	CHECK(r.pairs.empty());
	CHECK(r.values.empty());
}

void FirstInOrderWins()
{
	const ClashReport r = ClashAnalyze({Mod("skin", {"gameres/menu.tga"}), Mod("other", {"gameres/menu.tga"})}, {});
	CHECK_EQ(r.files.size(), static_cast<size_t>(1));
	CHECK_EQ(Used(r, "gameres/menu.tga"), std::string("skin-folder"));
	CHECK_EQ(File(r, "gameres/menu.tga")->fallback, std::string("skin-folder"));
	CHECK_EQ(File(r, "gameres/menu.tga")->folders, (std::vector<std::string>{"skin-folder", "other-folder"}));
	const ClashPair *p = Pair(r, "skin", "other");
	CHECK(p != nullptr);
	CHECK_EQ(p->files, 1);
	CHECK(p->choosable);
	CHECK(!p->problem);
}

void LooseBeatsPak()
{
	const ClashReport r = ClashAnalyze({Mod("first", {}, {"gameres/menu.tga"}), Mod("second", {"gameres/menu.tga"})}, {});
	CHECK_EQ(Used(r, "gameres/menu.tga"), std::string("second-folder"));
	const ClashReport both = ClashAnalyze({Mod("first", {}, {"gameres/menu.tga"}), Mod("second", {}, {"gameres/menu.tga"})}, {});
	CHECK_EQ(Used(both, "gameres/menu.tga"), std::string("first-folder"));
}

void GameplayClashIsAProblem()
{
	const ClashReport r = ClashAnalyze({Mod("a", {"data/units.xml", "gameres/menu.tga"}), Mod("b", {"data/units.xml", "gameres/menu.tga"})}, {});
	const ClashPair *p = Pair(r, "a", "b");
	CHECK(p != nullptr);
	CHECK_EQ(p->files, 2);
	CHECK_EQ(p->gameplay, 1);
	CHECK(p->problem);
	CHECK(!p->declared);
	CHECK(!p->choosable);
}

void DeclaredClashIsWanted()
{
	for (const char *how : {"A", "a"}) {
		const ClashReport r = ClashAnalyze({Mod("b", {"data/units.xml"}, {}, {how}), Mod("A", {"data/units.xml"})}, {});
		const ClashPair *p = Pair(r, "b", "A");
		CHECK(p != nullptr);
		CHECK(p->declared);
		CHECK(!p->problem);
	}
	const ClashReport r = ClashAnalyze({Mod("addon", {"data/units.xml"}), Mod("base", {"data/units.xml"}, {}, {"addon"})}, {});
	CHECK(!Pair(r, "addon", "base")->problem);
}

void CompatibilityPatchSolvesIt()
{
	const ClashMod patch = Mod("patch", {"data/units.xml"}, {}, {"a", "b"});
	const ClashReport r = ClashAnalyze({patch, Mod("a", {"data/units.xml"}), Mod("b", {"data/units.xml"})}, {});
	CHECK_EQ(Used(r, "data/units.xml"), std::string("patch-folder"));
	for (const ClashPair &p : r.pairs) CHECK(!p.problem);
}

void PatchInsideAPakLosesToLooseFiles()
{
	const ClashMod patch = Mod("patch", {}, {"data/units.xml"}, {"a", "b"});
	const ClashReport r = ClashAnalyze({patch, Mod("a", {"data/units.xml"}), Mod("b", {"data/units.xml"})}, {});
	CHECK_EQ(Used(r, "data/units.xml"), std::string("a-folder"));
	CHECK(Pair(r, "a", "b")->problem);
	CHECK(!Pair(r, "patch", "a")->problem);
	CHECK(!Pair(r, "patch", "b")->problem);
}

void OnlyTheWinnerIsBlamed()
{
	const ClashReport r = ClashAnalyze({Mod("a", {"data/units.xml"}, {}, {"b"}), Mod("b", {"data/units.xml"}), Mod("c", {"data/units.xml"})}, {});
	CHECK(!Pair(r, "a", "b")->problem);
	CHECK(Pair(r, "a", "c")->problem);
	CHECK(!Pair(r, "b", "c")->problem);
}

void MapsAreNotChoosable()
{
	const ClashReport r = ClashAnalyze({Mod("a", {"scenarios/x.bfhp", "gameres/menu.tga"}), Mod("b", {"scenarios/x.bfhp", "gameres/menu.tga"})},
		{{"b", "a"}});
	const ClashPair *p = Pair(r, "a", "b");
	CHECK_EQ(p->content, 1);
	CHECK(!p->problem);
	CHECK(!p->choosable);
	CHECK_EQ(Used(r, "gameres/menu.tga"), std::string("a-folder"));
}

void ChoiceForCosmeticFiles()
{
	const std::vector<ClashMod> mods = {Mod("a", {"gameres/menu.tga", "sounds/click.wav"}), Mod("b", {"gameres/menu.tga"}, {"sounds/click.wav"})};
	const ClashReport plain = ClashAnalyze(mods, {});
	CHECK_EQ(Used(plain, "gameres/menu.tga"), std::string("a-folder"));
	CHECK_EQ(Used(plain, "sounds/click.wav"), std::string("a-folder"));
	const ClashReport chosen = ClashAnalyze(mods, {{"B", "a"}});
	CHECK_EQ(Used(chosen, "gameres/menu.tga"), std::string("b-folder"));
	CHECK_EQ(File(chosen, "gameres/menu.tga")->fallback, std::string("a-folder"));
	CHECK_EQ(Used(chosen, "sounds/click.wav"), std::string("a-folder"));
	const ClashReport same = ClashAnalyze(mods, {{"a", "b"}});
	CHECK_EQ(Used(same, "gameres/menu.tga"), std::string("a-folder"));
}

void ChoiceNeverTouchesGameplay()
{
	const std::vector<ClashMod> mods = {Mod("a", {"data/units.xml", "gameres/menu.tga"}, {}, {"b"}), Mod("b", {"data/units.xml", "gameres/menu.tga"})};
	const ClashReport r = ClashAnalyze(mods, {{"b", "a"}});
	CHECK_EQ(Used(r, "data/units.xml"), std::string("a-folder"));
	CHECK_EQ(Used(r, "gameres/menu.tga"), std::string("a-folder"));
	CHECK(!Pair(r, "a", "b")->choosable);
}

void ChoiceWithUnknownModsIsIgnored()
{
	const ClashReport r = ClashAnalyze({Mod("a", {"gameres/menu.tga"}), Mod("b", {"gameres/menu.tga"})}, {{"gone", "a"}, {"b", "gone"}, {"a", "a"}});
	CHECK_EQ(Used(r, "gameres/menu.tga"), std::string("a-folder"));
}

void SameValueFirstInOrderWins()
{
	const ClashReport r = ClashAnalyze({Patcher("a", {Set("/unit/@hp", "10"), Set("/unit/@speed", "2")}), Patcher("b", {Set("/unit/@hp", "20")})}, {});
	CHECK(r.files.empty());
	CHECK(r.pairs.empty());
	CHECK_EQ(r.values.size(), size_t(1));
	const ValueClash *c = Lost(r, "a", "b");
	CHECK(c != nullptr);
	if (c) {
		CHECK_EQ(c->values.size(), size_t(1));
		CHECK_EQ(c->values[0].key, std::string("/unit/@hp"));
		CHECK_EQ(c->values[0].value, std::string("10"));
	}
}

void EqualValuesDoNotClash()
{
	CHECK(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "10")}), Patcher("b", {Set("/unit/@hp", "10")})}, {}).values.empty());
	CHECK(ClashAnalyze({Patcher("a", {Unset("/unit/@hp")}), Patcher("b", {Unset("/unit/@hp")})}, {}).values.empty());
	CHECK(Lost(ClashAnalyze({Patcher("a", {Unset("/unit/@hp")}), Patcher("b", {Set("/unit/@hp", "")})}, {}), "a", "b") != nullptr);
	CHECK(Lost(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "10")}), Patcher("b", {Unset("/unit/@hp")})}, {}), "a", "b") != nullptr);
}

void OtherFilesAndKeysDoNotClash()
{
	CHECK(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@hp", "2", "data/other.xml")})}, {}).values.empty());
	CHECK(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@armor", "2")})}, {}).values.empty());
}

void DeclaredValuesAreWanted()
{
	CHECK(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}, {"B"}), Patcher("b", {Set("/unit/@hp", "2")})}, {}).values.empty());
	CHECK(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@hp", "2")}, {"a"})}, {}).values.empty());
	const ClashReport r = ClashAnalyze(
		{Patcher("patch", {Set("/unit/@hp", "3")}, {"a", "b"}), Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@hp", "2")})}, {});
	CHECK(r.values.empty());
}

void OnlyTheWinnerTakesValues()
{
	const ClashReport r =
		ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@hp", "2")}), Patcher("c", {Set("/unit/@hp", "3")})}, {});
	CHECK_EQ(r.values.size(), size_t(2));
	CHECK(Lost(r, "a", "b") != nullptr);
	CHECK(Lost(r, "a", "c") != nullptr);
	CHECK(Lost(r, "b", "c") == nullptr);
	const ClashReport same =
		ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@hp", "2")}), Patcher("c", {Set("/unit/@hp", "1")})}, {});
	CHECK_EQ(same.values.size(), size_t(1));
	CHECK(Lost(same, "a", "b") != nullptr);
}

void LastValueOfAPackCounts()
{
	CHECK(ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1"), Set("/unit/@hp", "2")}), Patcher("b", {Set("/unit/@hp", "2")})}, {}).values.empty());
	const ClashReport r = ClashAnalyze({Patcher("a", {Set("/unit/@hp", "1")}), Patcher("b", {Set("/unit/@hp", "1"), Unset("/unit/@hp")})}, {});
	CHECK(Lost(r, "a", "b") != nullptr);
}

void ValuesOfAPairAreGrouped()
{
	const ClashMod a = Patcher("a", {Set("/unit/@speed", "1"), Set("/unit/@hp", "1"), Set("[game]speed", "1", "options/game.ini")});
	const ClashMod b = Patcher("b", {Set("/unit/@hp", "2"), Set("/unit/@speed", "2"), Set("[game]speed", "2", "options/game.ini")});
	const ClashReport r = ClashAnalyze({a, b}, {});
	CHECK_EQ(r.values.size(), size_t(1));
	const ValueClash *c = Lost(r, "a", "b");
	CHECK(c != nullptr);
	if (c) {
		CHECK_EQ(c->values.size(), size_t(3));
		if (c->values.size() == 3) {
			CHECK_EQ(c->values[0].key, std::string("/unit/@hp"));
			CHECK_EQ(c->values[1].key, std::string("/unit/@speed"));
			CHECK_EQ(c->values[2].file, std::string("options/game.ini"));
		}
	}
}

void DifferentSelectorsSameValue()
{
	const std::string base = "<units>\n\t<unit name=\"legionary\" hp=\"100\"/>\n\t<unit name=\"archer\" hp=\"50\"/>\n</units>\n";
	const std::string first = "<patch><set select=\"//unit[@name='legionary']\" hp=\"150\"/></patch>";
	const std::string second = "<patch><set select=\"/units/unit[1]\" hp=\"120\"/></patch>";
	const std::vector<std::vector<PatchedValue>> resolved = PatchXmlResolve("data/units.xml", base, {}, {second, first});
	CHECK_EQ(resolved.size(), size_t(2));
	if (resolved.size() != 2) return;
	const ClashReport r = ClashAnalyze({Patcher("a", resolved[1]), Patcher("b", resolved[0])}, {});
	CHECK_EQ(r.values.size(), size_t(1));
	const ValueClash *c = Lost(r, "a", "b");
	CHECK(c != nullptr);
	if (c && c->values.size() == 1) {
		CHECK_EQ(c->values[0].label, std::string("//unit[@name='legionary']/@hp"));
		CHECK_EQ(c->values[0].value, std::string("150"));
	}
	const ClashReport byText = ClashAnalyze({Patcher("a", PatchValues("data/units.xml", first)), Patcher("b", PatchValues("data/units.xml", second))}, {});
	CHECK(byText.values.empty());
	const std::string other = "<patch><set select=\"/units/unit[2]\" hp=\"120\"/></patch>";
	const std::vector<std::vector<PatchedValue>> apart = PatchXmlResolve("data/units.xml", base, {}, {other, first});
	if (apart.size() == 2) CHECK(ClashAnalyze({Patcher("a", apart[1]), Patcher("b", apart[0])}, {}).values.empty());
}

void ChoiceAmongThree()
{
	const std::vector<ClashMod> mods = {Mod("a", {"gameres/menu.tga"}), Mod("b", {"gameres/menu.tga"}), Mod("c", {"gameres/menu.tga"})};
	CHECK_EQ(Used(ClashAnalyze(mods, {{"c", "a"}}), "gameres/menu.tga"), std::string("c-folder"));
	CHECK_EQ(Used(ClashAnalyze(mods, {{"c", "b"}}), "gameres/menu.tga"), std::string("a-folder"));
	CHECK_EQ(Used(ClashAnalyze(mods, {{"b", "a"}, {"c", "b"}}), "gameres/menu.tga"), std::string("c-folder"));
	const std::string cycle = Used(ClashAnalyze(mods, {{"b", "a"}, {"c", "b"}, {"a", "c"}}), "gameres/menu.tga");
	CHECK_EQ(cycle, Used(ClashAnalyze(mods, {{"b", "a"}, {"c", "b"}, {"a", "c"}}), "gameres/menu.tga"));
}

}

RulesetClashMod Ruleset(const std::string &pack, const std::string &layer, std::vector<std::string> paths, std::vector<PatchedValue> values = {})
{
	RulesetClashMod r;
	r.pack = pack;
	r.layer = layer;
	r.paths = paths;
	r.loose = paths;
	r.values = values;
	return r;
}

// A room mounts one ruleset, so rulesets that change the same gameplay files are alternatives; the comparison only reports them.
void RulesetsOfOtherPacksAreAlternatives()
{
	const std::vector<RulesetShared> r = RulesetCompare({Ruleset("cm", "rules", {"data/classes/a.sc.xml", "data/classes/b.sc.xml", "gameres/x.tga"}),
		Ruleset("mci", "rules", {"data/classes/a.sc.xml", "data/classes/c.sc.xml", "gameres/x.tga"}),
		Ruleset("other", "rules", {"data/classes/z.sc.xml"})});
	CHECK_EQ(r.size(), static_cast<size_t>(1));
	if (r.size() != 1) return;
	CHECK_EQ(r[0].packA, "cm");
	CHECK_EQ(r[0].packB, "mci");
	CHECK_EQ(r[0].files, 1);
	CHECK_EQ(r[0].values, 0);
}

void RulesetsOfOnePackAreNotCompared()
{
	CHECK(RulesetCompare({Ruleset("cm", "cheap", {"data/const.ini"}), Ruleset("cm", "costly", {"data/const.ini"})}).empty());
}

void RulesetsShareValuesToo()
{
	const std::vector<RulesetShared> r = RulesetCompare({Ruleset("cm", "a", {}, {Set("hp", "10", "data/const.ini"), Set("gold", "5", "data/const.ini")}),
		Ruleset("mci", "b", {}, {Set("hp", "20", "data/const.ini"), Set("gold", "5", "data/const.ini")})});
	CHECK_EQ(r.size(), static_cast<size_t>(1));
	if (r.size() != 1) return;
	CHECK_EQ(r[0].files, 0);
	CHECK_EQ(r[0].values, 1);
	CHECK_EQ(r[0].layerA, "a");
	CHECK_EQ(r[0].layerB, "b");
}

void CosmeticFilesAreNotAlternatives()
{
	CHECK(RulesetCompare({Ruleset("cm", "a", {"gameres/x.tga"}), Ruleset("mci", "b", {"gameres/x.tga"})}).empty());
	CHECK(RulesetCompare({}).empty());
}

int main()
{
	RulesetsOfOtherPacksAreAlternatives();
	RulesetsOfOnePackAreNotCompared();
	RulesetsShareValuesToo();
	CosmeticFilesAreNotAlternatives();
	Kinds();
	NothingShared();
	FirstInOrderWins();
	LooseBeatsPak();
	GameplayClashIsAProblem();
	DeclaredClashIsWanted();
	CompatibilityPatchSolvesIt();
	PatchInsideAPakLosesToLooseFiles();
	OnlyTheWinnerIsBlamed();
	MapsAreNotChoosable();
	ChoiceForCosmeticFiles();
	ChoiceNeverTouchesGameplay();
	ChoiceWithUnknownModsIsIgnored();
	ChoiceAmongThree();
	SameValueFirstInOrderWins();
	EqualValuesDoNotClash();
	OtherFilesAndKeysDoNotClash();
	DeclaredValuesAreWanted();
	OnlyTheWinnerTakesValues();
	LastValueOfAPackCounts();
	ValuesOfAPairAreGrouped();
	DifferentSelectorsSameValue();
	return test::Summary("modclash");
}
