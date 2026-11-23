// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

int TierNumber(LeagueTier t) { return static_cast<int>(t); }

std::string Join(const std::vector<uint32_t> &values)
{
	std::string out;
	for (uint32_t v : values) out += (out.empty() ? "" : ",") + std::to_string(v);
	return out;
}

void BaseUrl()
{
	CHECK_EQ(LeagueBaseUrl(""), std::string(kLeagueDefaultUrl));
	CHECK_EQ(LeagueBaseUrl("  https://api.imperivmresurrected.com/  "), std::string("https://api.imperivmresurrected.com"));
	CHECK_EQ(LeagueBaseUrl("http://localhost:8787"), std::string("http://localhost:8787"));
	CHECK_EQ(LeagueBaseUrl("http://127.0.0.1:8787/"), std::string("http://127.0.0.1:8787"));
	CHECK_EQ(LeagueBaseUrl("HTTP://LocalHost:8787"), std::string("HTTP://LocalHost:8787"));
	CHECK_EQ(LeagueBaseUrl("https://staging.example.com"), std::string("https://staging.example.com"));
	// Plain http only to this computer: a token never travels unencrypted.
	CHECK_EQ(LeagueBaseUrl("http://api.imperivmresurrected.com"), std::string(kLeagueDefaultUrl));
	CHECK_EQ(LeagueBaseUrl("http://localhost.evil.com"), std::string(kLeagueDefaultUrl));
	CHECK_EQ(LeagueBaseUrl("http://user@localhost.evil.com"), std::string(kLeagueDefaultUrl));
	CHECK_EQ(LeagueBaseUrl("ftp://localhost"), std::string(kLeagueDefaultUrl));
	CHECK_EQ(LeagueBaseUrl("https://"), std::string(kLeagueDefaultUrl));
	CHECK_EQ(LeagueBaseUrl("https://a b.com"), std::string(kLeagueDefaultUrl));
	CHECK(LeagueUrlIsDefault(LeagueBaseUrl("")));
	CHECK(!LeagueUrlIsDefault(LeagueBaseUrl("http://localhost:8787")));
}

void Tiers()
{
	CHECK_EQ(TierNumber(LeagueTierFromId("centurion")), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(TierNumber(LeagueTierFromId("Centuri\xC3\xB3n")), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(TierNumber(LeagueTierFromId("SIGN\xC3\x8D" "FERO")), TierNumber(LeagueTier::Signifer));
	CHECK_EQ(TierNumber(LeagueTierFromId("signifer")), TierNumber(LeagueTier::Signifer));
	CHECK_EQ(TierNumber(LeagueTierFromId("pretoriano")), TierNumber(LeagueTier::Praetorian));
	CHECK_EQ(TierNumber(LeagueTierFromId("caesar")), TierNumber(LeagueTier::None));
	const Json numbers = Parse("[1, 8, 0, 9, \"3\", \"legado\", {\"id\": \"tribuno\"}, null, true]");
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[0])), TierNumber(LeagueTier::Recruit));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[1])), TierNumber(LeagueTier::Imperator));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[2])), TierNumber(LeagueTier::None));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[3])), TierNumber(LeagueTier::None));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[4])), TierNumber(LeagueTier::Signifer));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[5])), TierNumber(LeagueTier::Legate));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[6])), TierNumber(LeagueTier::Tribune));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[7])), TierNumber(LeagueTier::None));
	CHECK_EQ(TierNumber(LeagueTierFromJson(&numbers.arr[8])), TierNumber(LeagueTier::None));
	CHECK_EQ(TierNumber(LeagueTierFromJson(nullptr)), TierNumber(LeagueTier::None));
	CHECK_EQ(std::string(LeagueTierId(LeagueTier::Signifer)), std::string("signifero"));
	CHECK_EQ(std::string(LeagueTierName(LeagueTier::Praetorian)), std::string("Praetorian"));
	CHECK_EQ(std::string(LeagueTierName(LeagueTier::None)), std::string("Unranked"));
	CHECK(LeagueTierHasDivisions(LeagueTier::Legate));
	CHECK(!LeagueTierHasDivisions(LeagueTier::Praetorian));
	CHECK(!LeagueTierHasDivisions(LeagueTier::None));
	CHECK_EQ(LeagueTierGloryFrom(LeagueTier::Recruit), 0);
	CHECK_EQ(LeagueTierGloryFrom(LeagueTier::Centurion), 900);
	CHECK_EQ(LeagueTierGloryFrom(LeagueTier::Legate), 1500);
	CHECK_EQ(LeagueTierGloryFrom(LeagueTier::Imperator), -1);
	CHECK_EQ(LeagueDivisionIndex(-40), 0);
	CHECK_EQ(LeagueDivisionIndex(99), 0);
	CHECK_EQ(LeagueDivisionIndex(450), 4);
	CHECK_EQ(LeagueDivisionIndex(1799), 17);
	CHECK_EQ(LeagueDivisionIndex(2600), 17);
	CHECK(LeagueIndexTier(0) == LeagueTier::Recruit);
	CHECK_EQ(LeagueIndexDivision(0), 3);
	CHECK(LeagueIndexTier(4) == LeagueTier::Legionary);
	CHECK_EQ(LeagueIndexDivision(4), 2);
	CHECK(LeagueIndexTier(17) == LeagueTier::Legate);
	CHECK_EQ(LeagueIndexDivision(17), 1);
	CHECK(LeagueIndexTier(40) == LeagueTier::Legate);
}

void Divisions()
{
	const Json values = Parse("[1, 3, 4, 0, \"II\", \"iii\", \"2\", \"IV\"]");
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[0]), 1);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[1]), 3);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[2]), 0);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[3]), 0);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[4]), 2);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[5]), 3);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[6]), 2);
	CHECK_EQ(LeagueDivisionFromJson(&values.arr[7]), 0);
	CHECK_EQ(LeagueDivisionFromJson(nullptr), 0);
	CHECK_EQ(LeagueDivisionText(3), std::string("III"));
	CHECK_EQ(LeagueDivisionText(0), std::string());
	CHECK_EQ(LeagueRankText("Centuri\xC3\xB3n", 2), std::string("Centuri\xC3\xB3n II"));
	CHECK_EQ(LeagueRankText("Imperator", 0), std::string("Imperator"));
}

void Badges()
{
	const Json rank = Parse("{\"kind\": \"rank\", \"id\": \"1v1\", \"tier\": \"centurion\", \"division\": 2}");
	LeagueBadge b = LeagueBadgeFromJson(&rank);
	CHECK(b.kind == LeagueBadgeKind::Rank);
	CHECK(b.tier == LeagueTier::Centurion);
	CHECK_EQ(b.division, 2);
	CHECK_EQ(b.id, std::string("1v1"));
	// The tier can come as the id alone; the elite ranks drop the division.
	const Json byId = Parse("{\"kind\": \"rank\", \"id\": \"imperator\", \"division\": 1}");
	b = LeagueBadgeFromJson(&byId);
	CHECK(b.tier == LeagueTier::Imperator);
	CHECK_EQ(b.division, 0);
	const Json unknown = Parse("{\"kind\": \"rank\", \"id\": \"1v1\"}");
	CHECK(!LeagueBadgeFromJson(&unknown).Shown());
	const Json season = Parse("{\"kind\": \"season\", \"id\": 1, \"tier\": 5}");
	b = LeagueBadgeFromJson(&season);
	CHECK(b.kind == LeagueBadgeKind::Season);
	CHECK_EQ(b.id, std::string("1"));
	CHECK(b.tier == LeagueTier::Tribune);
	const Json trophy = Parse("{\"kind\": \"trophy\", \"id\": \"cup-1\"}");
	CHECK(LeagueBadgeFromJson(&trophy).kind == LeagueBadgeKind::Trophy);
	// Without its metal a trophy is the champion's gold; the lookup sends the metal as the tier.
	CHECK(LeagueBadgeFromJson(&trophy).metal == LeagueMetal::Gold);
	CHECK_EQ(LeagueBadgeFromJson(&trophy).count, 0);
	const Json three = Parse("{\"kind\": \"trophy\", \"id\": 9, \"tier\": \"gold\", \"count\": 3}");
	CHECK_EQ(LeagueBadgeFromJson(&three).count, 3);
	const Json bronze = Parse("{\"kind\": \"trophy\", \"id\": 7, \"tier\": \"bronze\"}");
	b = LeagueBadgeFromJson(&bronze);
	CHECK(b.metal == LeagueMetal::Bronze);
	CHECK(b.tier == LeagueTier::None);
	CHECK_EQ(b.id, std::string("7"));
	CHECK(LeagueMetalFromId("Silver") == LeagueMetal::Silver);
	CHECK(LeagueMetalFromId("season") == LeagueMetal::Season);
	CHECK(LeagueMetalFromId("platinum") == LeagueMetal::None);
	const Json silver = Parse("{\"kind\": \"trophy\", \"id\": 7, \"tier\": \"silver\"}");
	CHECK(!(LeagueBadgeFromJson(&bronze) == LeagueBadgeFromJson(&silver)));
	const Json none = Parse("{\"kind\": \"none\"}");
	CHECK(!LeagueBadgeFromJson(&none).Shown());
	CHECK(!LeagueBadgeFromJson(nullptr).Shown());
	// The test season leaves the Founder's emblem, whose tier is no rank.
	const Json founder = Parse("{\"kind\": \"season\", \"id\": 0, \"tier\": \"founder\"}");
	b = LeagueBadgeFromJson(&founder);
	CHECK(b.kind == LeagueBadgeKind::Season);
	CHECK(b.founder);
	CHECK(b.tier == LeagueTier::None);
	CHECK(!(b == LeagueBadgeFromJson(&season)));
	CHECK(!LeagueBadgeFromJson(&season).founder);
}

void Lookup()
{
	const Json j = Parse("{\"76561198000000001\": {\"patron\": {\"tier\": \"gold\", \"place\": 1}, \"badge\": {\"kind\": \"rank\", \"id\": \"1v1\", "
						 "\"tier\": \"legado\", \"division\": 3}, \"linked\": true}, \"76561198000000002\": {\"patron\": null, \"badge\": null}, \"x\": {}}");
	std::map<uint64_t, LeagueLookupEntry> out;
	CHECK(LeagueParsePlayersLookup(j, out));
	CHECK_EQ(static_cast<unsigned>(out.size()), 2u);
	const LeagueLookupEntry &a = out[76561198000000001ULL];
	CHECK_EQ(a.patronTier, std::string("gold"));
	CHECK_EQ(a.patronPlace, 1);
	CHECK(a.badge.tier == LeagueTier::Legate);
	CHECK_EQ(a.badge.division, 3);
	CHECK(a.linked);
	const LeagueLookupEntry &b = out[76561198000000002ULL];
	CHECK(b.patronTier.empty());
	CHECK(!b.badge.Shown());
	CHECK(!b.linked);
	std::map<uint64_t, LeagueLookupEntry> none;
	CHECK(!LeagueParsePlayersLookup(Parse("[]"), none));
}

void Me()
{
	const Json j = Parse("{\"steamId\": \"76561198000000001\", \"name\": \"Marcus\", \"ranked\": {\"queue\": \"1v1\", \"tier\": \"centurion\", "
						 "\"division\": 1, \"glory\": 1134, \"placementLeft\": 0, \"shield\": 2, \"wins\": 12, \"losses\": 7, \"position\": 131, "
						 "\"elite\": false}, \"queueBanUntil\": 0, \"badge\": {\"kind\": \"rank\", \"id\": \"1v1\"}, \"badges\": [{\"kind\": \"season\", "
						 "\"id\": 1, \"season\": 1}, {\"kind\": \"bogus\"}]}");
	LeagueMe me;
	CHECK(LeagueParseMe(j, me));
	CHECK_EQ(me.steamId, std::string("76561198000000001"));
	CHECK(me.ranked.present);
	CHECK(me.ranked.tier == LeagueTier::Centurion);
	CHECK_EQ(me.ranked.division, 1);
	CHECK_EQ(me.ranked.glory, 1134);
	CHECK_EQ(me.ranked.position, 131);
	CHECK(!me.badge.Shown());
	CHECK_EQ(static_cast<unsigned>(me.badges.size()), 1u);
	CHECK_EQ(static_cast<int>(me.ban.until), 0);
	CHECK(me.ban.kind.empty());
	LeagueMe empty;
	CHECK(LeagueParseMe(Parse("{\"steamId\": \"1\", \"ranked\": null}"), empty));
	CHECK(!empty.ranked.present);
	CHECK(!empty.ranked2v2.present);
	CHECK(empty.queues.empty());
	CHECK(!empty.cupsKnown);

	// The finished cups: an official one with the place, a community one without a season, and rows without an id or a name left out.
	LeagueMe cups;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","cups":[{"id":7,"name":"Copa de los Idus","official":true,"mode":"1v1","format":"single","season":0,
		"finishedAt":1700000000000,"placement":1,"entries":8},{"id":9,"name":"Copa del Foro","official":false,"mode":"2v2","format":"swiss","season":null,
		"placement":null,"entries":6},{"id":0,"name":"x"},{"id":3,"name":"  "}]})"),
		cups));
	CHECK(cups.cupsKnown);
	CHECK_EQ(static_cast<unsigned>(cups.cups.size()), 2u);
	CHECK(cups.cups[0].official);
	CHECK_EQ(cups.cups[0].season, 0);
	CHECK_EQ(cups.cups[0].placement, 1);
	CHECK_EQ(cups.cups[0].entries, 8);
	CHECK(!cups.cups[1].official);
	CHECK_EQ(cups.cups[1].mode, std::string("2v2"));
	CHECK_EQ(cups.cups[1].season, -1);
	CHECK_EQ(cups.cups[1].placement, 0);

	// The 2v2 standing has the same shape, with its best, streak and placement inside; the duel's may also come beside it.
	LeagueMe both;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","queues":["1v1","2v2"],"peak":{"glory":700,"tier":"centurion","division":3},
		"placementResults":["win"],"ranked":{"tier":"signifer","division":2,"glory":480,"wins":3,"losses":2},
		"ranked2v2":{"queue":"2v2","tier":null,"glory":0,"placementLeft":3,"wins":1,"losses":1,"streak":-1,"placementResults":["loss","win"],
		"peak":null}})"),
		both));
	CHECK_EQ(both.queues, std::vector<std::string>({"1v1", "2v2"}));
	CHECK(both.ranked.present);
	CHECK_EQ(both.ranked.queue, std::string("1v1"));
	CHECK_EQ(both.ranked.peak.glory, 700);
	CHECK_EQ(static_cast<unsigned>(both.ranked.placement.size()), 1u);
	CHECK(both.ranked2v2.present);
	CHECK_EQ(both.ranked2v2.queue, std::string("2v2"));
	CHECK(both.ranked2v2.tier == LeagueTier::None);
	CHECK_EQ(both.ranked2v2.placementLeft, 3);
	CHECK_EQ(both.ranked2v2.streak, -1);
	CHECK(!both.ranked2v2.peak.present);
	CHECK_EQ(static_cast<unsigned>(both.ranked2v2.placement.size()), 2u);
	CHECK(both.ranked2v2.placement[0] == LeaguePlacementMark::Loss);
	// A server before the Glory history sends none: unknown, not an empty season.
	CHECK(!both.ranked.gloryKnown);
	CHECK(both.ranked.gloryHistory.empty());

	LeagueMe glory;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","ranked":{"glory":530,"gloryHistory":[{"at":1700000000000,"glory":500},{"at":1700000100000,"glory":-4},
		{"at":1700000200000},"x",{"at":1700000300000,"glory":530}]},"ranked2v2":{"queue":"2v2","gloryHistory":[]}})"),
		glory));
	CHECK(glory.ranked.gloryKnown);
	CHECK_EQ(static_cast<unsigned>(glory.ranked.gloryHistory.size()), 3u);
	CHECK_EQ(static_cast<unsigned long long>(glory.ranked.gloryHistory[0].at), 1700000000000ULL);
	CHECK_EQ(glory.ranked.gloryHistory[0].glory, 500);
	CHECK_EQ(glory.ranked.gloryHistory[1].glory, 0);
	CHECK_EQ(glory.ranked.gloryHistory[2].glory, 530);
	CHECK(glory.ranked2v2.gloryKnown);
	CHECK(glory.ranked2v2.gloryHistory.empty());

	// The lock out of the queue and why; an older server says only until when.
	LeagueMe banned;
	CHECK(LeagueParseMe(Parse("{\"steamId\": \"1\", \"ban\": {\"until\": 1700000300000, \"kind\": \"Abandoned\"}, \"queueBanUntil\": 1700000300000}"), banned));
	CHECK_EQ(static_cast<unsigned long long>(banned.ban.until), 1700000300000ULL);
	CHECK_EQ(banned.ban.kind, std::string("abandoned"));
	CHECK(LeagueParseMe(Parse("{\"steamId\": \"1\", \"ban\": null, \"queueBanUntil\": 99}"), banned));
	CHECK_EQ(static_cast<int>(banned.ban.until), 99);
	CHECK(banned.ban.kind.empty());
	CHECK(LeagueParseMe(Parse("{\"steamId\": \"1\", \"ban\": {\"until\": 77, \"kind\": \"desync\"}}"), banned));
	CHECK_EQ(static_cast<int>(banned.queueBanUntil), 77);
	CHECK_EQ(banned.ban.kind, std::string("desync"));
}

void GloryChart()
{
	// Round steps, at most five of them, with room over and under the line.
	LeagueGloryAxis a = LeagueGloryAxisOf({{1, 1000}, {2, 1180}, {3, 1090}});
	CHECK_EQ(a.step, 50);
	CHECK_EQ(a.low, 950);
	CHECK_EQ(a.high, 1200);
	a = LeagueGloryAxisOf({{1, 1050}});
	CHECK_EQ(a.step, 10);
	CHECK_EQ(a.low, 1040);
	CHECK_EQ(a.high, 1070);
	a = LeagueGloryAxisOf({{1, 0}, {2, 4}});
	CHECK_EQ(a.low, 0);
	CHECK_EQ(a.high, 20);
	a = LeagueGloryAxisOf({{1, 200}, {2, 2600}});
	CHECK_EQ(a.step, 1000);
	CHECK_EQ(a.low, 0);
	CHECK_EQ(a.high, 3000);
	a = LeagueGloryAxisOf({});
	CHECK(a.high > a.low);

	// The games spread over the width, the Glory up from the bottom.
	const LeagueGloryAxis axis{1000, 1200, 50};
	std::vector<LeagueChartDot> dots = LeagueGloryDots({{1, 1000}, {2, 1100}, {3, 1200}}, axis, 10, 20, 200, 100);
	CHECK_EQ(static_cast<unsigned>(dots.size()), 3u);
	CHECK_EQ(dots[0].x, 10);
	CHECK_EQ(dots[0].y, 120);
	CHECK_EQ(dots[1].x, 110);
	CHECK_EQ(dots[1].y, 70);
	CHECK_EQ(dots[2].x, 210);
	CHECK_EQ(dots[2].y, 20);
	dots = LeagueGloryDots({{1, 900}}, axis, 10, 20, 200, 100);
	CHECK_EQ(dots[0].x, 110);
	CHECK_EQ(dots[0].y, 120);
	CHECK(LeagueGloryDots({}, axis, 0, 0, 10, 10).empty());
}

void Link()
{
	LeagueLinkCode s;
	CHECK(LeagueParseLinkStart(Parse("{\"code\": \"ABCD-2345\", \"pollToken\": \"pt\", \"expiresAt\": 1800000000000, \"url\": "
									 "\"https://imperivmresurrected.com/vincular\"}"),
		s));
	CHECK_EQ(s.code, std::string("ABCD-2345"));
	CHECK_EQ(static_cast<unsigned long long>(s.expiresAt), 1800000000000ULL);
	CHECK(!LeagueParseLinkStart(Parse("{\"code\": \"ABCD-2345\"}"), s));

	CHECK(LeagueParseLinkPoll(200, Parse("{\"status\": \"pending\"}")).status == LeaguePollStatus::Pending);
	const LeagueLinkPoll linked = LeagueParseLinkPoll(200, Parse("{\"status\": \"linked\", \"gameToken\": \"tok\", \"steamId\": \"7656\", \"name\": \"M\"}"));
	CHECK(linked.status == LeaguePollStatus::Linked);
	CHECK_EQ(linked.gameToken, std::string("tok"));
	CHECK_EQ(linked.steamId, std::string("7656"));
	CHECK(LeagueParseLinkPoll(200, Parse("{\"status\": \"linked\"}")).status == LeaguePollStatus::Unknown);
	CHECK(LeagueParseLinkPoll(410, Parse("{\"error\": \"expired\"}")).status == LeaguePollStatus::Expired);
	CHECK(LeagueParseLinkPoll(200, Parse("{}")).status == LeaguePollStatus::Unknown);

	CHECK_EQ(LeagueCodeDisplay("abcd2345"), std::string("ABCD-2345"));
	CHECK_EQ(LeagueCodeDisplay("ABCD-2345"), std::string("ABCD-2345"));
	CHECK_EQ(LeagueCodeDisplay(" ab cd-23 45 "), std::string("ABCD-2345"));
	CHECK_EQ(LeagueCodeDisplay("XYZ"), std::string("XYZ"));
	CHECK_EQ(LeagueUrlDisplay("https://imperivmresurrected.com/vincular?code=ABCD-2345"), std::string("imperivmresurrected.com/vincular"));
	CHECK_EQ(LeagueUrlDisplay("https://www.imperivmresurrected.com/vincular/"), std::string("imperivmresurrected.com/vincular"));
	CHECK_EQ(LeagueUrlDisplay(""), std::string("imperivmresurrected.com/vincular"));
	CHECK_EQ(LeagueBrowserUrl("https://imperivmresurrected.com/vincular?code=X"), std::string("https://imperivmresurrected.com/vincular?code=X"));
	CHECK_EQ(LeagueBrowserUrl("http://localhost:8788/vincular"), std::string("http://localhost:8788/vincular"));
	CHECK_EQ(LeagueBrowserUrl("file:///C:/Windows/system32/calc.exe"), std::string(kLeagueLinkPage));
	CHECK_EQ(LeagueBrowserUrl("http://evil.example/vincular"), std::string(kLeagueLinkPage));
	CHECK_EQ(LeagueBrowserUrl("https://a.com/\" x"), std::string(kLeagueLinkPage));
	CHECK_EQ(LeagueBrowserUrl(""), std::string(kLeagueLinkPage));
}

void SteamLink()
{
	LeagueLinkCode s;
	CHECK(LeagueParseLinkStart(Parse("{\"code\": \"ABCD-2345\", \"pollToken\": \"pt\", \"steamUrl\": "
									 "\"https://api.imperivmresurrected.com/v1/link/steam?code=ABCD-2345&lang=es\"}"),
		s));
	CHECK_EQ(s.steamUrl, std::string("https://api.imperivmresurrected.com/v1/link/steam?code=ABCD-2345&lang=es"));
	CHECK(LeagueParseLinkStart(Parse("{\"code\": \"ABCD-2345\", \"pollToken\": \"pt\"}"), s));
	CHECK(s.steamUrl.empty());

	const std::string base = kLeagueDefaultUrl;
	const std::string page = base + "/v1/link/steam?code=ABCD-2345&lang=es";
	CHECK_EQ(LeagueSteamLinkUrl(page, base), page);
	CHECK_EQ(LeagueSteamLinkUrl("  " + page + " ", base), page);
	CHECK_EQ(LeagueSteamLinkUrl("HTTPS://API.imperivmresurrected.com/v1/link/steam?code=X", base),
		std::string("HTTPS://API.imperivmresurrected.com/v1/link/steam?code=X"));
	CHECK_EQ(LeagueSteamLinkUrl("http://localhost:8787/v1/link/steam?code=X", "http://localhost:8787"),
		std::string("http://localhost:8787/v1/link/steam?code=X"));
	// Only the league server's own page: no other host, port, scheme or path, and nothing a shell or a browser could misread.
	CHECK(LeagueSteamLinkUrl("", base).empty());
	CHECK(LeagueSteamLinkUrl("https://evil.example/v1/link/steam?code=X", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com.evil.example/v1/link/steam?code=X", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com@evil.example/v1/link/steam?code=X", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com:8443/v1/link/steam?code=X", base).empty());
	CHECK(LeagueSteamLinkUrl("http://api.imperivmresurrected.com/v1/link/steam?code=X", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com/v1/link/steamy?code=X", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com/v1/link/steam", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com/v1/link/steam?code=X#y", base).empty());
	CHECK(LeagueSteamLinkUrl("https://api.imperivmresurrected.com/v1/link/steam?code=\"x\" y", base).empty());
	CHECK(LeagueSteamLinkUrl("file:///C:/Windows/system32/calc.exe", "file:///C:").empty());
	CHECK(LeagueSteamLinkUrl("http://evil.example/v1/link/steam?code=X", "http://evil.example").empty());
}

void LinkOffer()
{
	CHECK(LeagueOfferLinkNow(false, false, true, false, false));
	CHECK(!LeagueOfferLinkNow(true, false, true, false, false));
	CHECK(!LeagueOfferLinkNow(false, true, true, false, false));
	CHECK(!LeagueOfferLinkNow(false, false, false, false, false));
	CHECK(!LeagueOfferLinkNow(false, false, true, true, false));
	CHECK(!LeagueOfferLinkNow(false, false, true, false, true));
}

void KeyAndTags()
{
	CHECK_EQ(LeagueMatchKey(109775243145236713ULL, 1763112345678LL), std::string("109775243145236713:1763112345678"));
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t id = 0;
	CHECK(LeagueParseRoomTag("ranked:123", kind, id));
	CHECK(kind == LeagueMatchKind::Ranked);
	CHECK_EQ(static_cast<unsigned long long>(id), 123ULL);
	CHECK(LeagueParseRoomTag("tournament:7", kind, id));
	CHECK(kind == LeagueMatchKind::Tournament);
	CHECK(!LeagueParseRoomTag("ranked:", kind, id));
	CHECK(!LeagueParseRoomTag("ranked:-3", kind, id));
	CHECK(!LeagueParseRoomTag("ranked:0", kind, id));
	CHECK(!LeagueParseRoomTag("clan:5", kind, id));
	CHECK(!LeagueParseRoomTag("", kind, id));
	CHECK_EQ(std::string(LeagueMatchKindId(LeagueMatchKind::Tournament)), std::string("tournament"));
}

void Map()
{
	LeagueMapFacts m = LeagueMapFromOptions("Map type: Crossroads\nMap size: Large\nGame type: Conquest\n", "Chosen in the room");
	CHECK_EQ(m.name, std::string("Crossroads"));
	CHECK_EQ(m.type, std::string("Crossroads"));
	CHECK_EQ(m.size, std::string("Large"));
	m = LeagueMapFromOptions("Map type: Scenario\nMap size: Medium\nScenario: Cannae\n", "");
	CHECK_EQ(m.name, std::string("Cannae"));
	CHECK_EQ(m.type, std::string("scenario"));
	CHECK_EQ(m.size, std::string("Medium"));
	m = LeagueMapFromOptions("", "Island War");
	CHECK_EQ(m.name, std::string("Island War"));
	CHECK(m.type.empty());
}

LeagueSeat Seat(int index, uint64_t id, int team, int state, bool local = false)
{
	LeagueSeat s;
	s.index = index;
	s.steamId = id;
	s.name = "P" + std::to_string(index);
	s.civ = "Carthage";
	s.team = team;
	s.state = state;
	s.local = local;
	return s;
}

void Resolve()
{
	// The game declared the winners.
	std::vector<LeagueSeat> seats = {Seat(0, 11, 0, 3, true), Seat(1, 22, 1, 2)};
	LeagueOutcome o = LeagueResolve(seats, false);
	CHECK_EQ(o.endedBy, std::string("victory"));
	CHECK_EQ(o.results, std::vector<std::string>({"win", "loss"}));
	CHECK(!o.localLeft);

	// The loser surrendered.
	seats[1].surrendered = true;
	CHECK_EQ(LeagueResolve(seats, false).endedBy, std::string("surrender"));

	// Only one team still standing, though the game did not say it won.
	seats = {Seat(0, 11, 0, 1, true), Seat(1, 22, 1, 2)};
	o = LeagueResolve(seats, false);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "loss"}));
	CHECK_EQ(o.endedBy, std::string("victory"));

	// The other player dropped while playing and never came back; this one closes the game afterwards.
	seats = {Seat(0, 11, 0, 1, true), Seat(1, 22, 1, 1)};
	seats[1].left = true;
	o = LeagueResolve(seats, false);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "left"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));
	CHECK(!o.localLeft);

	// This player closes a game that nobody has won: they left it.
	seats = {Seat(0, 11, 0, 1, true), Seat(1, 22, 1, 1)};
	o = LeagueResolve(seats, false);
	CHECK(o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"left", "win"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));

	// A desync voids it: nobody wins and nobody is said to have left; the report says it was a desync, not a drop.
	o = LeagueResolve(seats, true);
	CHECK(!o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"loss", "loss"}));
	CHECK_EQ(o.endedBy, std::string("desync"));

	// Two humans against an AI: while the AI stands, this player closing the game leaves it, and their partner has not won yet.
	// Once the AI is beaten, the humans' team is the only one left.
	seats = {Seat(0, 11, 0, 1, true), Seat(1, 22, 0, 1), Seat(2, 0, 2, 1)};
	o = LeagueResolve(seats, false);
	CHECK(o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"left", "loss", "loss"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));
	seats[2].state = 2;
	o = LeagueResolve(seats, false);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "win", "loss"}));
	CHECK_EQ(o.endedBy, std::string("victory"));

	// A defeated player who closes the game lost; it is not leaving.
	seats = {Seat(0, 11, 0, 2, true), Seat(1, 22, 1, 1), Seat(2, 33, 2, 1)};
	o = LeagueResolve(seats, false);
	CHECK(!o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"loss", "loss", "loss"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));
}

void ResolveRankedDrop()
{
	std::vector<LeagueSeat> seats = {Seat(0, 11, 0, 1, true), Seat(1, 22, 1, 1)};
	// People: the drop rules count only those.
	for (LeagueSeat &s : seats) s.human = true;
	seats[1].left = true;
	LeagueDropRules rules;
	rules.ranked = true;

	// Under the drop rules a 1v1's opponent gone for good (`left`: 5 minutes passed) leaves the game to this player, by "opponent_left".
	rules.team = true;
	LeagueOutcome o = LeagueResolve(seats, false, rules);
	CHECK(!o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "left"}));
	CHECK_EQ(o.endedBy, std::string("opponent_left"));

	// This player gone for good while the other stays: they left it.
	seats[0].left = true;
	seats[1].left = false;
	o = LeagueResolve(seats, false, rules);
	CHECK_EQ(o.results, std::vector<std::string>({"left", "win"}));

	// Closing the game while the opponent is still within their 5 minutes is leaving it: the game is theirs.
	seats[0].left = false;
	o = LeagueResolve(seats, false, rules);
	CHECK(o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"left", "win"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));
	seats[1].left = true;
	rules.team = false;

	// A normal game keeps its rules: the one who stayed wins, by a drop.
	o = LeagueResolve(seats, false);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "left"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));

	// Nobody left: a ranked game ends as any other.
	seats[1].left = false;
	seats[0].state = 3;
	seats[1].state = 2;
	o = LeagueResolve(seats, false, rules);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "loss"}));
	CHECK_EQ(o.endedBy, std::string("victory"));
	// A desync is a desync whoever stands.
	CHECK_EQ(LeagueResolve(seats, true, rules).endedBy, std::string("desync"));
	// A surrender of the loser stays a surrender.
	seats[1].surrendered = true;
	CHECK_EQ(LeagueResolve(seats, false, rules).endedBy, std::string("surrender"));
}

LeagueSeat Human(int index, uint64_t id, int team, int state, bool local = false)
{
	LeagueSeat s = Seat(index, id, team, state, local);
	s.human = true;
	return s;
}

void ResolveTeam()
{
	LeagueDropRules rules;
	rules.ranked = true;
	rules.team = true;
	// The game declared the winners; a player of the losing side who left for good is "left", whatever the AI did with their army.
	std::vector<LeagueSeat> seats = {Human(0, 11, 0, 3, true), Human(1, 22, 0, 3), Human(2, 33, 2, 2), Human(3, 44, 2, 1)};
	seats[3].left = true;
	seats[3].leftAtMs = 300000;
	LeagueOutcome o = LeagueResolve(seats, false, rules);
	CHECK(!o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "win", "loss", "left"}));
	CHECK_EQ(o.endedBy, std::string("victory"));
	std::vector<LeagueReportPlayer> players = LeagueReportPlayers(seats, o);
	CHECK_EQ(static_cast<unsigned>(players.size()), 4u);
	CHECK_EQ(static_cast<int>(players[3].leftAtMs), 300000);
	CHECK_EQ(static_cast<int>(players[2].leftAtMs), -1);
	// A player out when the game ended, within their five minutes: still on their side's result, with when they left.
	seats[2].leftAtMs = 1500000;
	o = LeagueResolve(seats, false, rules);
	players = LeagueReportPlayers(seats, o);
	CHECK_EQ(players[2].result, std::string("loss"));
	CHECK_EQ(static_cast<int>(players[2].leftAtMs), 1500000);
	// This computer's own time goes only when it left.
	seats[0].leftAtMs = 1600000;
	players = LeagueReportPlayers(seats, LeagueResolve(seats, false, rules));
	CHECK_EQ(static_cast<int>(players[0].leftAtMs), -1);

	// Nobody has won (a player who dropped is still within their 5 minutes, so not left) and this player closes the game: they leave it,
	// at the game's time they were given.
	seats = {Human(0, 11, 0, 1, true), Human(1, 22, 0, 1), Human(2, 33, 2, 1), Human(3, 44, 2, 1)};
	seats[0].leftAtMs = 420000;
	o = LeagueResolve(seats, false, rules);
	CHECK(o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"left", "loss", "loss", "loss"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));
	players = LeagueReportPlayers(seats, o);
	CHECK_EQ(static_cast<int>(players[0].leftAtMs), 420000);

	// Both of the other side left for good: closing the game is this team's win, "opponent_left".
	seats[2].left = seats[3].left = true;
	o = LeagueResolve(seats, false, rules);
	CHECK(!o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "win", "left", "left"}));
	CHECK_EQ(o.endedBy, std::string("opponent_left"));

	// The partner left for good and this player stays against two: closing the game is leaving it.
	seats = {Human(0, 11, 0, 1, true), Human(1, 22, 0, 1), Human(2, 33, 2, 1), Human(3, 44, 2, 1)};
	seats[1].left = true;
	o = LeagueResolve(seats, false, rules);
	CHECK(o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"left", "left", "win", "win"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));

	// The team that surrendered lost by surrender; a desync is a desync.
	seats = {Human(0, 11, 0, 2, true), Human(1, 22, 0, 2), Human(2, 33, 2, 3), Human(3, 44, 2, 3)};
	seats[0].surrendered = seats[1].surrendered = true;
	o = LeagueResolve(seats, false, rules);
	CHECK_EQ(o.results, std::vector<std::string>({"loss", "loss", "win", "win"}));
	CHECK_EQ(o.endedBy, std::string("surrender"));
	CHECK_EQ(LeagueResolve(seats, true, rules).endedBy, std::string("desync"));

	// The other side is beaten but the game has not said so yet: the only side standing won.
	seats = {Human(0, 11, 0, 1, true), Human(1, 22, 0, 1), Human(2, 33, 2, 2), Human(3, 44, 2, 2)};
	o = LeagueResolve(seats, false, rules);
	CHECK_EQ(o.results, std::vector<std::string>({"win", "win", "loss", "loss"}));
	CHECK_EQ(o.endedBy, std::string("victory"));
}

void ReportPlayers()
{
	std::vector<LeagueSeat> seats = {Seat(3, 33, 3, 2), Seat(0, 11, 0, 3, true), Seat(2, 0, 3, 2), Seat(1, 22, 0, 3)};
	const LeagueOutcome o = LeagueResolve(seats, false);
	const std::vector<LeagueReportPlayer> players = LeagueReportPlayers(seats, o);
	CHECK_EQ(static_cast<unsigned>(players.size()), 3u);
	CHECK_EQ(players[0].steamId, std::string("11"));
	CHECK_EQ(players[0].team, 1);
	CHECK_EQ(players[0].result, std::string("win"));
	CHECK_EQ(players[1].steamId, std::string("22"));
	CHECK_EQ(players[1].team, 1);
	CHECK_EQ(players[2].steamId, std::string("33"));
	CHECK_EQ(players[2].team, 2);
	CHECK_EQ(players[2].result, std::string("loss"));
}

void ReportJson()
{
	LeagueReport r;
	r.key = LeagueMatchKey(42, 1763112345678LL);
	r.map = {"Crossroads", "Crossroads", "Large"};
	r.contentKey = "c7d1e0aa";
	r.startedAt = 1763112345678LL;
	r.durationMs = 1874000;
	r.endedBy = "victory";
	LeagueReportPlayer p;
	p.steamId = "76561198000000001";
	p.name = "Marcus \"Rex\"";
	p.team = 1;
	p.civ = "Carthage";
	p.result = "win";
	r.players.push_back(p);
	r.hashes = {123456u, 4294967295u};
	const std::string text = LeagueReportJson(r).Dump();
	CHECK_EQ(text, std::string("{\"key\":\"42:1763112345678\",\"kind\":\"normal\",\"map\":{\"name\":\"Crossroads\",\"type\":\"Crossroads\",\"size\":\"Large\"},"
							   "\"contentKey\":\"c7d1e0aa\",\"startedAt\":1763112345678,\"durationMs\":1874000,\"endedBy\":\"victory\",\"dropAtMs\":null,\"players\":"
							   "[{\"steamId\":\"76561198000000001\",\"name\":\"Marcus \\\"Rex\\\"\",\"team\":1,\"civ\":\"Carthage\",\"result\":\"win\",\"leftAtMs\":null}],"
							   "\"hashes\":[123456, 4294967295]}"));
	// A normal game never carries a match id; a ranked one does.
	r.matchId = 9;
	CHECK(LeagueReportJson(r).Get("matchId") == nullptr);
	r.kind = LeagueMatchKind::Ranked;
	const Json ranked = LeagueReportJson(r);
	CHECK_EQ(ranked.GetString("kind"), std::string("ranked"));
	CHECK_EQ(static_cast<int>(ranked.GetNumber("matchId")), 9);
	// The game's time when the other side left; null when nobody did.
	CHECK(ranked.Get("dropAtMs") && ranked.Get("dropAtMs")->type == Json::Null);
	r.endedBy = "opponent_left";
	r.dropAtMs = 412000;
	const Json dropped = LeagueReportJson(r);
	CHECK_EQ(dropped.GetString("endedBy"), std::string("opponent_left"));
	CHECK_EQ(static_cast<int>(dropped.GetNumber("dropAtMs")), 412000);
	r.dropAtMs = 0;
	CHECK_EQ(static_cast<int>(LeagueReportJson(r).Get("dropAtMs")->type), static_cast<int>(Json::Number));
	// A player of a 2v2 who left for good carries the game's time they left.
	r.players[0].result = "left";
	r.players[0].leftAtMs = 1234567;
	const Json left = LeagueReportJson(r);
	const Json *players = left.Get("players");
	CHECK(players && players->type == Json::Array && players->arr.size() == 1);
	if (players && players->arr.size() == 1) CHECK_EQ(static_cast<int>(players->arr[0].GetNumber("leftAtMs")), 1234567);
}

void Checkpoints()
{
	LeagueCheckpoints c;
	for (uint32_t t = 0; t <= 3500; t++) c.Add(t, t * 7u);
	CHECK_EQ(Join(c.Hashes()), std::string("7000,14000,21000"));
	// The same tick again changes nothing; a tick back is a new game.
	c.Add(3500, 1);
	CHECK_EQ(static_cast<unsigned>(c.Hashes().size()), 3u);
	c.Add(5, 1);
	CHECK(c.Hashes().empty());
	for (uint32_t t = 6; t <= 2000; t++) c.Add(t, 99);
	CHECK_EQ(Join(c.Hashes()), std::string("99,99"));

	// A client that came in at tick 1500 has no first checkpoint, so it sends none.
	LeagueCheckpoints late;
	for (uint32_t t = 1500; t <= 4000; t++) late.Add(t, 5);
	CHECK(late.Hashes().empty());

	// A gap (this client was away) ends the list there.
	LeagueCheckpoints gap;
	for (uint32_t t = 1; t <= 1200; t++) gap.Add(t, 1);
	for (uint32_t t = 2500; t <= 3000; t++) gap.Add(t, 3);
	CHECK_EQ(Join(gap.Hashes()), std::string("1"));
}

std::string Tries(const std::vector<LeagueQueuedReport> &q)
{
	std::string out;
	for (const LeagueQueuedReport &r : q) out += (out.empty() ? "" : ",") + std::to_string(r.tries);
	return out;
}

LeagueQueuedReport Queued(const std::string &steamId, const std::string &key, int tries, int64_t createdAt)
{
	LeagueQueuedReport q;
	q.steamId = steamId;
	q.body = Json::MakeObject();
	q.body.Set("key", Json::MakeString(key));
	q.tries = tries;
	q.createdAt = createdAt;
	q.nextAt = createdAt + 1000;
	return q;
}

void Queue()
{
	std::vector<LeagueQueuedReport> q;
	LeagueQueueAdd(q, Queued("1", "a", 0, 100));
	LeagueQueueAdd(q, Queued("1", "b", 1, 200));
	LeagueQueueAdd(q, Queued("2", "a", 2, 300));
	// The same game of the same account replaces its report.
	LeagueQueueAdd(q, Queued("1", "a", 3, 400));
	CHECK_EQ(Tries(q), std::string("1,2,3"));

	const std::vector<LeagueQueuedReport> back = LeagueQueueDecode(LeagueQueueEncode(q));
	CHECK_EQ(Tries(back), std::string("1,2,3"));
	CHECK_EQ(back[2].body.GetString("key"), std::string("a"));
	CHECK_EQ(back[1].steamId, std::string("2"));
	CHECK_EQ(static_cast<unsigned long long>(back[2].nextAt), 1400ULL);
	CHECK(LeagueQueueDecode("not json").empty());
	CHECK(LeagueQueueDecode("[{\"steamId\": \"1\", \"report\": {}}, 5]").empty());

	for (int i = 0; i < 60; i++) LeagueQueueAdd(q, Queued("3", "k" + std::to_string(i), 0, 1000 + i));
	CHECK_EQ(static_cast<unsigned>(q.size()), static_cast<unsigned>(kLeagueQueueMax));
	CHECK_EQ(q.back().body.GetString("key"), std::string("k59"));

	std::vector<LeagueQueuedReport> old = {Queued("1", "a", 0, 0), Queued("1", "b", 0, kLeagueQueueKeepMs)};
	CHECK_EQ(static_cast<unsigned>(LeagueQueuePrune(old, kLeagueQueueKeepMs + 1)), 1u);
	CHECK_EQ(old[0].body.GetString("key"), std::string("b"));

	CHECK_EQ(static_cast<unsigned long long>(LeagueRetryDelayMs(1)), 30000ULL);
	CHECK_EQ(static_cast<unsigned long long>(LeagueRetryDelayMs(2)), 60000ULL);
	CHECK_EQ(static_cast<unsigned long long>(LeagueRetryDelayMs(0)), 30000ULL);
	CHECK_EQ(static_cast<unsigned long long>(LeagueRetryDelayMs(50)), 1800000ULL);
}

}

void TitlesAndRecap()
{
	LeagueMe me;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"76561198000000401","cupTitles":[{"kind":"traveling","name":"Corona del mes","tournament":{"id":3,"slug":"x","name":"Copa del mes"},"since":1,"season":2},{"kind":"season","name":""}],
		"recap":{"season":2,"name":"Hispania","queue":"1v1","games":7,"wins":3,"losses":4,"rewarded":false,"frame":false,"missing":{"vexillum":{"games":3,"wins":2},"frame":{"games":18,"wins":9}}}})"), me));
	CHECK(me.cupTitles.size() == 1);
	CHECK_EQ(me.cupTitles[0].name, std::string("Corona del mes"));
	CHECK_EQ(me.cupTitles[0].cupName, std::string("Copa del mes"));
	CHECK_EQ(me.cupTitles[0].season, 2);
	CHECK(me.recap.present && !me.recap.rewarded);
	CHECK_EQ(me.recap.games, 7);
	CHECK_EQ(me.recap.vexGames, 3);
	CHECK_EQ(me.recap.vexWins, 2);
	CHECK_EQ(me.recap.frameWins, 9);
	LeagueMe old;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"76561198000000401"})"), old));
	CHECK(!old.recap.present && old.cupTitles.empty());
}

int main()
{
	TitlesAndRecap();
	BaseUrl();
	Tiers();
	Divisions();
	Badges();
	Lookup();
	Me();
	GloryChart();
	Link();
	SteamLink();
	LinkOffer();
	KeyAndTags();
	Map();
	Resolve();
	ResolveRankedDrop();
	ResolveTeam();
	ReportPlayers();
	ReportJson();
	Checkpoints();
	Queue();
	return test::Summary("league");
}
