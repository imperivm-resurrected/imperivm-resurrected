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
}

void Lookup()
{
	const Json j = Parse("{\"76561198000000001\": {\"patron\": {\"tier\": \"gold\", \"place\": 1}, \"badge\": {\"kind\": \"rank\", \"id\": \"1v1\", "
						 "\"tier\": \"legado\", \"division\": 3}}, \"76561198000000002\": {\"patron\": null, \"badge\": null}, \"x\": {}}");
	std::map<uint64_t, LeagueLookupEntry> out;
	CHECK(LeagueParsePlayersLookup(j, out));
	CHECK_EQ(static_cast<unsigned>(out.size()), 2u);
	const LeagueLookupEntry &a = out[76561198000000001ULL];
	CHECK_EQ(a.patronTier, std::string("gold"));
	CHECK_EQ(a.patronPlace, 1);
	CHECK(a.badge.tier == LeagueTier::Legate);
	CHECK_EQ(a.badge.division, 3);
	const LeagueLookupEntry &b = out[76561198000000002ULL];
	CHECK(b.patronTier.empty());
	CHECK(!b.badge.Shown());
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
	LeagueMe empty;
	CHECK(LeagueParseMe(Parse("{\"steamId\": \"1\", \"ranked\": null}"), empty));
	CHECK(!empty.ranked.present);
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

	// A desync voids it: nobody wins and nobody is said to have left.
	o = LeagueResolve(seats, true);
	CHECK(!o.localLeft);
	CHECK_EQ(o.results, std::vector<std::string>({"loss", "loss"}));
	CHECK_EQ(o.endedBy, std::string("disconnect"));

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
							   "\"contentKey\":\"c7d1e0aa\",\"startedAt\":1763112345678,\"durationMs\":1874000,\"endedBy\":\"victory\",\"players\":"
							   "[{\"steamId\":\"76561198000000001\",\"name\":\"Marcus \\\"Rex\\\"\",\"team\":1,\"civ\":\"Carthage\",\"result\":\"win\"}],"
							   "\"hashes\":[123456, 4294967295]}"));
	// A normal game never carries a match id; a ranked one does.
	r.matchId = 9;
	CHECK(LeagueReportJson(r).Get("matchId") == nullptr);
	r.kind = LeagueMatchKind::Ranked;
	const Json ranked = LeagueReportJson(r);
	CHECK_EQ(ranked.GetString("kind"), std::string("ranked"));
	CHECK_EQ(static_cast<int>(ranked.GetNumber("matchId")), 9);
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

int main()
{
	BaseUrl();
	Tiers();
	Divisions();
	Badges();
	Lookup();
	Me();
	Link();
	SteamLink();
	LinkOffer();
	KeyAndTags();
	Map();
	Resolve();
	ReportPlayers();
	ReportJson();
	Checkpoints();
	Queue();
	return test::Summary("league");
}
