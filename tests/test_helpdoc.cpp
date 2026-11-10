// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/helpdoc.h"

namespace {

// The shape of the game's help, in a few topics.
const char *const kSample =
	"\xEF\xBB\xBF<help language=\"english\" vspacing=\"10\">\r\n"
	"\t<topic id=\"contents\" vcenter=\"1\">\r\n"
	"\t\t<entry hcenter=\"1\" font=\"large\">Basics</entry>\r\n"
	"\t\t<entry link=\"fog\">Fog of war</entry>\r\n"
	"\t\t<entry link=\"stats\">Unit stats</entry>\r\n"
	"\t\t<entry/>\r\n"
	"\t\t<entry hcenter=\"1\" font=\"large\">Lists</entry>\r\n"
	"\t\t<entry link=\"units\"> Units </entry>\r\n"
	"\t\t<entry link=\"items\">Items &amp; amulets</entry>\r\n"
	"\t\t<entry hcenter=\"1\" font=\"large\">Other</entry>\r\n"
	"\t\t<entry link=\"http://example.com\">Web site</entry>\r\n"
	"\t\t<topic id=\"fog\">\r\n"
	"\t\t\t<entry hcenter=\"1\" font=\"large\">Fog of war</entry>\r\n"
	"\t\t\t<entry>\r\n"
	"Parts of the map stay dark until your units explore them.\r\n"
	"\r\n"
	"\r\n"
	"Explored land keeps a light mist over enemy units.\r\n"
	"\t\t\t</entry>\r\n"
	"\t\t</topic>\r\n"
	"\t\t<topic id=\"stats\">\r\n"
	"\t\t\t<entry font=\"large\">Unit stats</entry>\r\n"
	"\t\t\t<entry link=\"health\">Health</entry>\r\n"
	"\t\t\t<topic id=\"health\"><entry font=\"large\">Health</entry><entry>How much damage the unit takes.</entry></topic>\r\n"
	"\t\t</topic>\r\n"
	"\t\t<!--<topic id=\"bless\"><entry>Gone</entry></topic>-->\r\n"
	"\t\t<topic id=\"units\">\r\n"
	"\t\t\t<entry hcenter=\"1\" font=\"large\">Republican Rome</entry>\r\n"
	"\t\t\t<entry vcenter=\"1\" image=\"UI/icons/RHastatus.bmp\" link=\"RHastatus\"> Hastatus </entry>\r\n"
	"\t\t\t<entry vcenter=\"1\" image=\"UI/icons/RArcher.bmp\" link=\"RArcher\"> Archer </entry>\r\n"
	"\t\t\t<entry hcenter=\"1\" font=\"large\">Gaul</entry>\r\n"
	"\t\t\t<entry vcenter=\"1\" image=\"UI/icons/GSwordsman.bmp\" link=\"GSwordsman\"> Swordsman </entry>\r\n"
	"\t\t\t<entry vcenter=\"1\" image=\"UI/icons/RArcher.bmp\" link=\"GArcher\"> Archer </entry>\r\n"
	"\t\t\t<topic id=\"RHastatus\">\r\n"
	"\t\t\t\t<entry hcenter=\"1\" font=\"large\">Hastatus</entry>\r\n"
	"\t\t\t\t<entry image=\"UI/icons/RHastatus.bmp\">The backbone of the Roman legion.</entry>\r\n"
	"\t\t\t\t<entry link=\"/contents/health\">Élite health</entry>\r\n"
	"\t\t\t\t<entry link=\"/contents/stats/health\">Health</entry>\r\n"
	"\t\t\t</topic>\r\n"
	"\t\t\t<topic id=\"RArcher\"><entry font=\"large\">Archer</entry><entry>Shoots arrows at the enemy.</entry></topic>\r\n"
	"\t\t\t<topic id=\"GSwordsman\"><entry font=\"large\">Swordsman</entry><entry>A Gaulish warrior with a sword.</entry></topic>\r\n"
	"\t\t\t<topic id=\"GArcher\"><entry font=\"large\">Archer</entry><entry>Gaulish archer.</entry></topic>\r\n"
	"\t\t</topic>\r\n"
	"\t\t<topic id=\"items\">\r\n"
	"\t\t\t<entry font=\"large\">Items &amp; amulets</entry>\r\n"
	"\t\t\t<entry link=\"Amulet of Triumph\" image=\"UI/icons/items/a.bmp\">\r\nAmulet of triumph\r\nGives back 100 health.\r\n</entry>\r\n"
	"\t\t</topic>\r\n"
	"\t</topic>\r\n"
	"</help>\r\n";

int Find(const HelpDoc &doc, const char *id)
{
	for (size_t i = 0; i < doc.topics.size(); i++) {
		if (doc.topics[i].id == id) return static_cast<int>(i);
	}
	return -1;
}

void Parsing()
{
	const HelpDoc doc = ParseHelpXml(kSample);
	CHECK_EQ(static_cast<int>(doc.roots.size()), 1);
	CHECK_EQ(Find(doc, "bless"), -1);
	const int contents = Find(doc, "contents"), fog = Find(doc, "fog"), units = Find(doc, "units");
	CHECK_EQ(contents, 0);
	CHECK_EQ(doc.topics[fog].parent, contents);
	CHECK_EQ(static_cast<int>(doc.topics[contents].entries.size()), 9);
	CHECK(doc.topics[contents].entries[0].large);
	CHECK(doc.topics[contents].entries[0].hcenter);
	CHECK_EQ(doc.topics[contents].entries[3].text, std::string());
	CHECK_EQ(doc.topics[contents].entries[6].text, std::string("Items & amulets"));
	CHECK(doc.topics[contents].entries[8].web);
	CHECK_EQ(doc.topics[contents].entries[8].target, -1);
	CHECK_EQ(doc.topics[fog].entries[1].text, std::string("Parts of the map stay dark until your units explore them.\n\nExplored land keeps a light mist over enemy units."));
	CHECK_EQ(doc.topics[units].entries[1].text, std::string("Hastatus"));
	CHECK_EQ(doc.topics[units].entries[1].image, std::string("UI/icons/RHastatus.bmp"));
	CHECK(doc.topics[units].entries[1].vcenter);
	CHECK_EQ(HelpPath(doc, Find(doc, "RHastatus")), std::string("/contents/units/RHastatus"));
	const int items = Find(doc, "items");
	CHECK_EQ(doc.topics[items].entries[1].text, std::string("Amulet of triumph\nGives back 100 health."));
	// A text saved in the Windows code page.
	const HelpDoc old = ParseHelpXml("<help><topic id=\"a\"><entry>Caracter\xEDsticas</entry></topic></help>");
	CHECK_EQ(old.topics[0].entries[0].text, std::string("Caracter\xC3\xADsticas"));
	// Empty and broken files give an empty document, not a crash.
	CHECK_EQ(static_cast<int>(ParseHelpXml("").topics.size()), 0);
	CHECK_EQ(static_cast<int>(ParseHelpXml("<help><topic id=\"x\"><entry font=\"large\">Cut").topics.size()), 1);
}

void Links()
{
	const HelpDoc doc = ParseHelpXml(kSample);
	const int contents = Find(doc, "contents"), units = Find(doc, "units"), hast = Find(doc, "RHastatus"), health = Find(doc, "health");
	CHECK_EQ(doc.topics[contents].entries[1].target, Find(doc, "fog"));
	CHECK_EQ(doc.topics[units].entries[1].target, hast);
	// Relative links look among the children only: "RHastatus" from the contents is not found that way, but by its id.
	CHECK_EQ(HelpResolve(doc, contents, "RHastatus"), hast);
	CHECK_EQ(HelpResolve(doc, contents, "units/RHastatus"), hast);
	CHECK_EQ(HelpResolve(doc, -1, "/contents/units/RHastatus"), hast);
	CHECK_EQ(HelpResolve(doc, -1, "contents/units/RHastatus"), hast);
	// "/contents/health" is not where the game would look, but there is one topic with that id.
	CHECK_EQ(doc.topics[hast].entries[2].target, health);
	CHECK_EQ(doc.topics[hast].entries[3].target, health);
	CHECK_EQ(Find(doc, "items") >= 0 ? doc.topics[Find(doc, "items")].entries[1].target : 0, -1);
	CHECK_EQ(HelpResolve(doc, contents, ""), -1);
	CHECK_EQ(HelpResolve(doc, contents, "/nowhere/at/all"), -1);
	CHECK_EQ(HelpResolve(doc, contents, "rhastatus"), hast);
}

void Index()
{
	const HelpDoc doc = ParseHelpXml(kSample);
	const HelpIndex x = BuildHelpIndex(doc);
	const int contents = Find(doc, "contents"), units = Find(doc, "units"), hast = Find(doc, "RHastatus"), garcher = Find(doc, "GArcher");
	CHECK_EQ(x.home, contents);
	CHECK_EQ(x.titles[contents], std::string());
	// A listing takes the name of the link to it, not of its first heading.
	CHECK_EQ(x.titles[units], std::string("Units"));
	CHECK_EQ(x.titles[hast], std::string("Hastatus"));
	CHECK_EQ(x.titles[Find(doc, "fog")], std::string("Fog of war"));
	CHECK_EQ(static_cast<int>(x.sections[units].size()), 2);
	CHECK_EQ(x.sections[units][1].heading, std::string("Gaul"));
	CHECK_EQ(x.sections[units][1].first, 3);
	CHECK_EQ(x.sections[units][1].end, 6);
	CHECK(x.sections[contents].empty());
	CHECK(x.sections[Find(doc, "items")].empty());
	CHECK_EQ(x.listing[garcher], units);
	CHECK_EQ(x.section[garcher], 1);
	CHECK_EQ(x.listing[Find(doc, "fog")], -1);
	// The side bar: the headings of the contents with their links; the group of the web link is left out.
	CHECK_EQ(static_cast<int>(x.groups.size()), 2);
	CHECK_EQ(x.groups[0].label, std::string("Basics"));
	CHECK_EQ(static_cast<int>(x.groups[0].items.size()), 2);
	CHECK_EQ(x.groups[1].items[0].label, std::string("Units"));
	CHECK_EQ(x.groups[1].items[0].sections, std::vector<std::string>({"Republican Rome", "Gaul"}));
	CHECK(x.groups[1].items[1].sections.empty());
}

void Crumbs()
{
	const HelpDoc doc = ParseHelpXml(kSample);
	const HelpIndex x = BuildHelpIndex(doc);
	const int units = Find(doc, "units"), garcher = Find(doc, "GArcher"), health = Find(doc, "health");
	std::vector<HelpCrumb> c = HelpCrumbs(doc, x, {garcher, -1, ""});
	CHECK_EQ(static_cast<int>(c.size()), 4);
	CHECK_EQ(c[0].label, std::string());
	CHECK_EQ(c[1].label, std::string("Units"));
	CHECK_EQ(c[2].label, std::string("Gaul"));
	CHECK(c[2].place == (HelpPlace{units, 1, ""}));
	CHECK_EQ(c[3].label, std::string("Archer"));
	c = HelpCrumbs(doc, x, {health, -1, ""});
	CHECK_EQ(static_cast<int>(c.size()), 3);
	CHECK_EQ(c[1].label, std::string("Unit stats"));
	c = HelpCrumbs(doc, x, {-1, -1, "fog"});
	CHECK_EQ(static_cast<int>(c.size()), 2);
	CHECK_EQ(c[1].label, std::string("fog"));
	HelpNavSpot s = HelpNavFor(doc, x, {garcher, -1, ""});
	CHECK_EQ(s.group, 1);
	CHECK_EQ(s.item, 0);
	CHECK_EQ(s.section, 1);
	s = HelpNavFor(doc, x, {health, -1, ""});
	CHECK_EQ(s.group, 0);
	CHECK_EQ(s.item, 1);
	CHECK_EQ(s.section, -1);
	CHECK_EQ(HelpNavFor(doc, x, {x.home, -1, ""}).group, -1);
}

void Search()
{
	const HelpDoc doc = ParseHelpXml(kSample);
	const HelpIndex x = BuildHelpIndex(doc);
	CHECK_EQ(HelpFold("\xC3\x89LITE Ni\xC3\xB1o"), std::string("elite nino"));
	std::vector<HelpHit> hits = HelpSearch(doc, x, "archer");
	CHECK_EQ(static_cast<int>(hits.size()), 2);
	// The Gaulish archer also says so in its text.
	CHECK_EQ(hits[0].topic, Find(doc, "GArcher"));
	CHECK_EQ(hits[1].topic, Find(doc, "RArcher"));
	// Every word must be found; text that is only a link does not count for the topic that holds it.
	hits = HelpSearch(doc, x, "gaulish sword");
	CHECK_EQ(static_cast<int>(hits.size()), 1);
	CHECK_EQ(hits[0].topic, Find(doc, "GSwordsman"));
	CHECK_EQ(hits[0].snippet, std::string("A Gaulish warrior with a sword."));
	hits = HelpSearch(doc, x, "MIST");
	CHECK_EQ(static_cast<int>(hits.size()), 1);
	CHECK_EQ(hits[0].topic, Find(doc, "fog"));
	CHECK_EQ(hits[0].snippet, std::string("Explored land keeps a light mist over enemy units."));
	// A heading of a listing is a place of its own.
	hits = HelpSearch(doc, x, "gaul");
	CHECK(!hits.empty());
	CHECK_EQ(hits[0].topic, Find(doc, "units"));
	CHECK_EQ(hits[0].section, 1);
	CHECK(HelpSearch(doc, x, "   ").empty());
	CHECK(HelpSearch(doc, x, "zzz").empty());
	CHECK_EQ(static_cast<int>(HelpSearch(doc, x, "a", 3).size()), 3);
}

void History()
{
	HelpHistory h;
	CHECK(!h.CanBack());
	CHECK(h.Current() == nullptr);
	h.Go({1, -1, ""});
	h.Go({2, -1, ""});
	h.Go({2, -1, ""});
	CHECK_EQ(static_cast<int>(h.places.size()), 2);
	h.Go({3, 0, ""});
	CHECK(h.Back());
	CHECK_EQ(h.Current()->topic, 2);
	CHECK(h.CanForward());
	CHECK(h.Back());
	CHECK(!h.Back());
	CHECK(h.Forward());
	// Going somewhere new drops the places ahead.
	h.Go({7, -1, ""});
	CHECK(!h.CanForward());
	CHECK_EQ(static_cast<int>(h.places.size()), 3);
	h.Replace({-1, -1, "fog"});
	CHECK_EQ(h.Current()->query, std::string("fog"));
	CHECK_EQ(static_cast<int>(h.places.size()), 3);
	for (int i = 0; i < 300; i++) h.Go({i, -1, ""});
	CHECK(h.places.size() <= 100);
	CHECK_EQ(h.Current()->topic, 299);
}

void Counters()
{
	const HelpCounters c = ParseCounterUnits(
		"<counterunits>\n"
		"\t<unit class=\"RHastatus\">\n\t\t<counter class=\"RPrinciple\" coeff=\"64\"/>\n\t\t<counter class=\"GSwordsman\" coeff=\"33\"/>\n"
		"\t\t<counter class=\"CNoble\" coeff=\"97\"/>\n\t\t<counter class=\"Zero\" coeff=\"0\"/>\n\t</unit>\n"
		"\t<unit class=\"RArcher\">\n\t\t<counter class=\"RHastatus\" coeff=\"76\"/>\n\t</unit>\n"
		"\t<unit class=\"GArcher\">\n\t\t<counter class=\"rhastatus\" coeff=\"40\"/>\n\t</unit>\n"
		"</counterunits>\n");
	CHECK_EQ(static_cast<int>(c.of.size()), 3);
	const HelpMatchups m = HelpMatchupsFor(c, "RHastatus");
	CHECK_EQ(static_cast<int>(m.weak.size()), 3);
	CHECK_EQ(m.weak[0].cls, std::string("CNoble"));
	CHECK_EQ(m.weak[2].cls, std::string("GSwordsman"));
	CHECK_EQ(static_cast<int>(m.strong.size()), 2);
	CHECK_EQ(m.strong[0].cls, std::string("rarcher"));
	CHECK_EQ(m.strong[0].coeff, 76);
	CHECK_EQ(m.strong[1].cls, std::string("garcher"));
	CHECK(HelpMatchupsFor(c, "Nobody").weak.empty());
	HelpUnitStats s;
	s.range = 40;
	s.damageType = kHelpDamageSlash;
	CHECK(!HelpUnitIsRanged(s));
	s.damageType = kHelpDamagePierce;
	CHECK(HelpUnitIsRanged(s));
}

}

int main()
{
	Parsing();
	Links();
	Index();
	Crumbs();
	Search();
	History();
	Counters();
	return test::Summary("helpdoc");
}
