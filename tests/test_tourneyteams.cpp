// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/tournament.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

bool Same(int64_t a, int64_t b) { return a == b; }

const uint64_t kVaro = 76561198000000001ULL, kTulia = 76561198000000002ULL, kFurio = 76561198000000003ULL, kBrenno = 76561198000000004ULL;

// A cup of teams as the league sends it (PLAN-LIGA, block C): the entries and the match cards with their members, the team's name in
// `name` and the captain in `steamId`.
const char *kDetail = R"({"id":7,"slug":"copa-parejas","name":"Copa de parejas","official":false,"mode":"2v2","format":"single","status":"running",
"maxEntries":4,"minEntries":2,"entries":[
{"entryId":11,"steamId":"76561198000000001","name":"Los lobos","status":"playing","seed":1,"checkedIn":true,"placement":null,
"members":[{"steamId":"76561198000000001","name":"Varo"},{"steamId":"76561198000000002","name":"Tulia"}]},
{"entryId":12,"steamId":"76561198000000003","name":"Furio y Brenno","status":"playing","seed":2,"checkedIn":true,"placement":null,
"members":[{"steamId":"76561198000000003","name":"Furio"},{"steamId":"76561198000000004","name":"Brenno"}]},
{"entryId":13,"steamId":"76561198000000005","name":"Equipo viejo","status":"withdrawn","seed":null,"checkedIn":false,"placement":null,
"members":[{"steamId":"76561198000000005","name":"Escipion"},{"steamId":"76561198000000002","name":"Tulia"}]},
{"entryId":14,"steamId":"76561198000000006","name":"Craso & Furio","status":"pending_partner","seed":null,"checkedIn":false,"placement":null,
"members":[{"steamId":"76561198000000006","name":"Craso"}],"teamName":null,"partner":{"steamId":"76561198000000003","name":"Furio","accepted":false}}],
"bestOf":1,"finalBestOf":1,"seeding":"rating","maps":[{"name":"mediterranean","type":"random","size":"medium"}],"rounds":1,"matches":[
{"id":21,"round":1,"position":0,"stage":"final","bestOf":1,"a":{"entryId":11,"steamId":"76561198000000001","name":"Los lobos","seed":1,
"members":[{"steamId":"76561198000000001","name":"Varo"},{"steamId":"76561198000000002","name":"Tulia"}]},
"b":{"entryId":12,"steamId":"76561198000000003","name":"Furio y Brenno","seed":2,
"members":[{"steamId":"76561198000000003","name":"Furio"},{"steamId":"76561198000000004","name":"Brenno"},{"name":"no id"}]},
"score":[0,0],"winner":null,"status":"ready"}],
"standings":[]})";

const char *kNext = R"({"tournament":{"id":7,"slug":"copa-parejas","name":"Copa de parejas","mode":"2v2"},"match":21,"round":1,"rounds":1,
"stage":"final","bestOf":1,"game":1,"score":[0,0],"status":"ready","deadlineAt":1794662271671,
"me":{"side":"a","seed":1,"inRoom":false},
"team":{"entryId":11,"name":"Los lobos","captain":"76561198000000001","members":[{"steamId":"76561198000000001","name":"Varo","inRoom":true},{"steamId":"76561198000000002","name":"Tulia","inRoom":false}]},
"opponent":{"name":"Furio y Brenno","steamId":"76561198000000003","seed":2,"inRoom":false,
"members":[{"steamId":"76561198000000003","name":"Furio","inRoom":true},{"steamId":"76561198000000004","name":"Brenno"}]}})";

const char *kMine = R"({"next":null,"entries":[
{"tournamentId":7,"slug":"copa-parejas","name":"Copa de parejas","tournamentStatus":"signup","status":"pending_partner","entryId":31,
"teamName":"Los lobos","partner":{"steamId":"76561198000000002","name":"Tulia"},"startsAt":1794662871362,"checkedIn":false,"seed":null,"placement":null}],
"invites":[
{"tournamentId":8,"slug":"otra-copa","name":"Otra copa","entryId":41,"from":{"steamId":"76561198000000003","name":"Furio"},"teamName":"Furio y tu",
"expiresAt":1794662000000},
{"tournamentId":9,"slug":"la-tercera","name":"La tercera","entryId":42,"from":{"steamId":"76561198000000004","name":"Brenno"},"teamName":null,
"expiresAt":1794661000000},
{"tournamentId":0,"entryId":43}]})";

void Modes()
{
	CHECK(TourneyModeFromId("2v2") == TourneyMode::Teams);
	CHECK(TourneyModeFromId("2V2") == TourneyMode::Teams);
	CHECK(TourneyModeFromId("1v1") == TourneyMode::Solo);
	CHECK(TourneyModeFromId("") == TourneyMode::Solo);
	CHECK(TourneyModeFromId("3v3") == TourneyMode::Solo);
	CHECK_EQ(std::string(TourneyModeId(TourneyMode::Teams)), std::string("2v2"));
	CHECK_EQ(std::string(TourneyModeId(TourneyMode::Solo)), std::string("1v1"));
	CHECK_EQ(TourneySidePlayers(TourneyMode::Teams), 2);
	CHECK_EQ(TourneySidePlayers(TourneyMode::Solo), 1);
	CHECK_EQ(TourneyGameMinutes(TourneyMode::Solo), 40);
	CHECK_EQ(TourneyGameMinutes(TourneyMode::Teams), 45);
	CHECK(TourneyEntryStatusFromId("pending_partner") == TourneyEntryStatus::PendingPartner);
	CHECK(!TourneyEntryActive(TourneyEntryStatus::PendingPartner));

	TourneySummary s;
	CHECK(TourneyParseSummary(Parse(R"({"id":1,"slug":"a","mode":"2v2"})"), s));
	CHECK(s.Teams());
	CHECK(TourneyParseSummary(Parse(R"({"id":1,"slug":"a"})"), s));
	CHECK(!s.Teams());
}

void Detail()
{
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(kDetail), d));
	CHECK(d.Teams());
	CHECK_EQ(d.entries, 2);
	CHECK_EQ(static_cast<int>(d.entrants.size()), 4);
	if (d.entrants.size() < 4 || d.matches.empty()) return;
	// A team waiting for its partner: only its captain among its members, the partner apart.
	CHECK(d.entrants[3].status == TourneyEntryStatus::PendingPartner);
	CHECK_EQ(static_cast<int>(d.entrants[3].members.size()), 1);
	CHECK(d.entrants[3].partner.steamId == kFurio && d.entrants[3].partner.name == "Furio");
	CHECK(d.entrants[3].teamName.empty());
	// Furio is only invited there: his team is the one he plays in.
	CHECK(TourneyEntrantBySteamId(d, kFurio) && TourneyEntrantBySteamId(d, kFurio)->entryId == 12);
	CHECK_EQ(static_cast<int>(d.entrants[0].members.size()), 2);
	CHECK_EQ(d.entrants[0].members[1].name, std::string("Tulia"));
	CHECK(d.entrants[0].members[1].steamId == kTulia);
	CHECK_EQ(d.entrants[0].members[1].inRoom, -1);
	// The partner finds their team, and the one still in the cup before the one they left.
	const TourneyEntrant *mine = TourneyEntrantBySteamId(d, kTulia);
	CHECK(mine && mine->entryId == 11);
	const TourneyEntrant *captain = TourneyEntrantBySteamId(d, kVaro);
	CHECK(captain && captain->entryId == 11);
	CHECK(TourneyEntrantBySteamId(d, 76561198000000005ULL) && TourneyEntrantBySteamId(d, 76561198000000005ULL)->entryId == 13);
	CHECK(!TourneyEntrantBySteamId(d, 0));
	CHECK(!TourneyEntrantBySteamId(d, 76561198000000099ULL));
	// A member without a SteamID is left out.
	const TourneyMatch &m = d.matches[0];
	CHECK_EQ(static_cast<int>(m.a.members.size()), 2);
	CHECK_EQ(static_cast<int>(m.b.members.size()), 2);
	CHECK(TourneyMemberOf(m.b.members, kBrenno) && TourneyMemberOf(m.b.members, kBrenno)->name == "Brenno");
	CHECK(!TourneyMemberOf(m.b.members, kVaro));
	CHECK(TourneyNameIsMembers(m.b.name, m.b.members));
	CHECK(!TourneyNameIsMembers(m.a.name, m.a.members));
	// A 1 vs 1 card lists its player as its one member.
	TourneyCard solo;
	solo.entryId = 1;
	solo.steamId = kFurio;
	solo.name = "Furio";
	const std::vector<TourneyMember> players = TourneyPlayersOf(solo);
	CHECK_EQ(static_cast<int>(players.size()), 1);
	CHECK(!players.empty() && players[0].steamId == kFurio && players[0].name == "Furio");
	CHECK(TourneyPlayersOf(TourneyCard()).empty());
	CHECK_EQ(static_cast<int>(TourneyPlayersOf(d.entrants[1]).size()), 2);
	CHECK(!TourneyNameIsMembers("Furio", players));

	// Standings take the members of their entries when they don't list them.
	TourneyDetail swiss;
	CHECK(TourneyParseDetail(Parse(R"({"id":7,"slug":"s","mode":"2v2","format":"swiss","status":"running","entries":[
{"entryId":11,"steamId":"76561198000000001","name":"Los lobos","status":"playing","members":[{"steamId":"76561198000000001","name":"Varo"},
{"steamId":"76561198000000002","name":"Tulia"}]}],"standings":[{"entryId":11,"points":1,"wins":1,"losses":0,"buchholz":0,"byes":0,"place":1}]})"),
		swiss));
	CHECK(!swiss.standings.empty() && swiss.standings[0].members.size() == 2 && swiss.standings[0].name == "Los lobos");
}

void Next()
{
	TourneyNext n;
	CHECK(TourneyParseNext(Parse(kNext), n));
	CHECK(n.Teams());
	CHECK_EQ(n.team.name, std::string("Los lobos"));
	CHECK(n.team.steamId == kVaro);
	CHECK_EQ(n.team.side, std::string("a"));
	CHECK_EQ(static_cast<int>(n.team.members.size()), 2);
	if (n.team.members.size() == 2) {
		CHECK_EQ(n.team.members[0].inRoom, 1);
		CHECK_EQ(n.team.members[1].inRoom, 0);
	}
	CHECK_EQ(n.opponent.name, std::string("Furio y Brenno"));
	CHECK_EQ(static_cast<int>(n.opponent.members.size()), 2);
	if (n.opponent.members.size() == 2) CHECK_EQ(n.opponent.members[1].inRoom, -1);
	std::vector<uint64_t> players = TourneyMatchPlayers(n, kVaro);
	CHECK_EQ(static_cast<int>(players.size()), 4);
	for (uint64_t id : {kVaro, kTulia, kFurio, kBrenno}) CHECK(std::find(players.begin(), players.end(), id) != players.end());

	// The mode from the list of players when the view does not name it, and the team as its players alone.
	CHECK(TourneyParseNext(Parse(R"({"match":3,"me":{"side":"b"},"team":[{"steamId":"1","name":"A"},{"steamId":"2","name":"B"}],
"opponent":{"name":"C","steamId":"3"}})"), n));
	CHECK(n.Teams());
	CHECK_EQ(static_cast<int>(n.team.members.size()), 2);
	CHECK_EQ(n.team.side, std::string("b"));
	CHECK(TourneyParseNext(Parse(R"({"match":3,"mode":"2v2","team":"Los lobos","opponent":null})"), n));
	CHECK(n.Teams());
	CHECK_EQ(n.team.name, std::string("Los lobos"));
	// One against one: a single opponent, no team.
	CHECK(TourneyParseNext(Parse(R"({"match":3,"me":{"side":"a"},"opponent":{"name":"C","steamId":"3"}})"), n));
	CHECK(!n.Teams());
	CHECK(n.team.members.empty());
	players = TourneyMatchPlayers(n, kVaro);
	CHECK_EQ(static_cast<int>(players.size()), 2);
}

void Claim()
{
	TourneyClaim c;
	CHECK(TourneyParseClaim(Parse(R"({"create":true,"lobbyKey":"irm=tournament:21","map":{"name":"mediterranean","type":"random","size":"medium"},
"teams":[["76561198000000001","76561198000000002"],["76561198000000003",76561198000000004]]})"), c));
	CHECK(c.kind == TourneyClaim::Create);
	CHECK_EQ(static_cast<int>(c.teams.size()), 2);
	CHECK_EQ(TourneyTeamOf(c.teams, kVaro), 1);
	CHECK_EQ(TourneyTeamOf(c.teams, kTulia), 1);
	CHECK_EQ(TourneyTeamOf(c.teams, kFurio), 2);
	CHECK_EQ(TourneyTeamOf(c.teams, 76561198000000099ULL), 0);
	CHECK_EQ(TourneyTeamOf(c.teams, 0), 0);
	CHECK(TourneyParseClaim(Parse(R"({"wait":true,"by":"76561198000000001","teams":[["76561198000000001"]]})"), c));
	CHECK(c.kind == TourneyClaim::Wait);
	CHECK(c.teams.empty());
	CHECK(TourneyParseClaim(Parse(R"({"join":"109775241234567890"})"), c));
	CHECK(c.teams.empty());
	CHECK_EQ(TourneyTeamOf(c.teams, kVaro), 0);
}

void Mine()
{
	TourneyMine m;
	CHECK(TourneyParseMine(Parse(kMine), m));
	CHECK_EQ(static_cast<int>(m.entries.size()), 1);
	CHECK_EQ(static_cast<int>(m.invites.size()), 2);
	if (m.entries.empty() || m.invites.size() < 2) return;
	const TourneyMyEntry &e = m.entries[0];
	CHECK(e.Pending());
	CHECK(Same(e.entryId, 31));
	CHECK_EQ(e.teamName, std::string("Los lobos"));
	CHECK(TourneyPartnerOf(e, kVaro).steamId == kTulia);
	// The invitation that expires first goes first.
	CHECK(Same(m.invites[0].entryId, 42));
	CHECK_EQ(m.invites[0].from.name, std::string("Brenno"));
	CHECK(m.invites[0].teamName.empty());
	CHECK_EQ(m.invites[1].teamName, std::string("Furio y tu"));
	CHECK(m.invites[1].from.steamId == kFurio);
	CHECK(TourneyInviteOf(m, 8) && Same(TourneyInviteOf(m, 8)->entryId, 41));
	CHECK(!TourneyInviteOf(m, 7));
	CHECK(TourneyHasActiveEntry(m));
	TourneyMine pendingOnly = m;
	pendingOnly.invites.clear();
	CHECK(TourneyHasActiveEntry(pendingOnly));
	TourneyMine invitesOnly = m;
	invitesOnly.entries.clear();
	CHECK(TourneyHasActiveEntry(invitesOnly));
	CHECK(!TourneyHasActiveEntry(TourneyMine()));
	CHECK_EQ(TourneyPollEveryMs(false, false), kTourneyIdlePollMs);

	// The league's own form of an entry of a cup of teams: {mode, entryId, team: {name, captain, members}}.
	TourneyMine league;
	CHECK(TourneyParseMine(Parse(R"({"entries":[{"tournamentId":5,"slug":"x","name":"X","tournamentStatus":"signup","status":"signed","mode":"2v2",
"entryId":51,"team":{"name":"Varo & Tulia","captain":"76561198000000001","members":[{"steamId":"76561198000000001","name":"Varo"},
{"steamId":"76561198000000002","name":"Tulia"}]}}],"invites":[]})"), league));
	CHECK(!league.entries.empty() && Same(league.entries[0].entryId, 51) && league.entries[0].teamName == "Varo & Tulia");
	CHECK(!league.entries.empty() && league.entries[0].members.size() == 2);
	CHECK(!league.entries.empty() && TourneyPartnerOf(league.entries[0], kTulia).steamId == kVaro);
	CHECK(!league.entries.empty() && TourneyNameIsMembers(league.entries[0].teamName, league.entries[0].members));

	// The partner from the team's members when the league names no partner.
	TourneyMyEntry team;
	team.members = {{kVaro, "Varo", -1}, {kTulia, "Tulia", -1}};
	CHECK(TourneyPartnerOf(team, kTulia).steamId == kVaro);
	CHECK(TourneyPartnerOf(TourneyMyEntry(), kVaro).steamId == 0);

	// Every invitation is told once, those of the first answer too.
	TourneyTold told;
	std::vector<TourneyNotice> notices = TourneyNewNotices(m, told);
	int invites = 0;
	for (const TourneyNotice &n : notices) {
		if (n.kind != TourneyNotice::Invite) continue;
		invites++;
		if (n.entry == 41) {
			CHECK_EQ(n.from.name, std::string("Furio"));
			CHECK_EQ(n.teamName, std::string("Furio y tu"));
			CHECK(Same(n.tournamentId, 8));
			CHECK(Same(n.deadlineAt, 1794662000000LL));
		}
	}
	CHECK_EQ(invites, 2);
	notices = TourneyNewNotices(m, told);
	for (const TourneyNotice &n : notices) CHECK(n.kind != TourneyNotice::Invite);
}

void Refusals()
{
	const TourneyRefusal r = TourneyParseRefusal(Parse(R"({"error":"requirement","who":"partner","kind":"games","have":2,"need":10,"message":"x"})"));
	CHECK_EQ(r.code, std::string("requirement"));
	CHECK_EQ(r.who, std::string("partner"));
	CHECK_EQ(r.requirement, std::string("games"));
	CHECK_EQ(r.have, std::string("2"));
	CHECK_EQ(r.need, std::string("10"));
	CHECK(TourneyParseRefusal(Parse(R"({"error":"requirement","kind":"tier","have":"tribune","need":"centurion"})")).who.empty());
	CHECK_EQ(TourneyParseRefusal(Parse(R"({"error":"partner_taken"})")).code, std::string("partner_taken"));

	Json body = TourneyTeamEntryJson(kTulia, "  Los lobos ", "ABC234");
	CHECK_EQ(body.GetString("partner"), std::string("76561198000000002"));
	CHECK_EQ(body.GetString("teamName"), std::string("Los lobos"));
	CHECK_EQ(body.GetString("code"), std::string("ABC234"));
	body = TourneyTeamEntryJson(kTulia, "   ", "");
	CHECK(!body.Get("teamName"));
	CHECK(!body.Get("code"));
}

void Maps()
{
	std::vector<TourneyMapType> types;
	CHECK(TourneyParseMapTypes(Parse(R"({"types":[{"id":"coastal","sizes":["medium","large"],"sizes4":["large","medium"]},
{"id":"continent","sizes":["small","medium","large"],"sizes4":["large"]},{"id":"island","sizes":["small","medium","large"]},{"id":""}],
"sizes":[{"id":"small"}]})"), types));
	CHECK_EQ(static_cast<int>(types.size()), 3);
	CHECK(!TourneyParseMapTypes(Parse(R"({"sizes":[]})"), types));
	CHECK(TourneyParseMapTypes(Parse(R"({"types":[{"id":"coastal","sizes":["medium","large"],"sizes4":["large","medium"]},
{"id":"continent","sizes":["small","medium","large"],"sizes4":["large"]},{"id":"island","sizes":["small","medium","large"]}]})"), types));
	// sizes4 in the room's order; without it, every size but the small one.
	std::vector<std::string> sizes = TourneySizesFor(types, "coastal", TourneyMode::Teams);
	CHECK_EQ(static_cast<int>(sizes.size()), 2);
	CHECK(sizes.size() == 2 && sizes[0] == "medium" && sizes[1] == "large");
	sizes = TourneySizesFor(types, "continent", TourneyMode::Teams);
	CHECK(sizes.size() == 1 && sizes[0] == "large");
	sizes = TourneySizesFor(types, "island", TourneyMode::Teams);
	CHECK(sizes.size() == 2 && sizes[0] == "medium");
	sizes = TourneySizesFor({}, "randommap", TourneyMode::Teams);
	CHECK(sizes.size() == 2 && sizes[0] == "medium" && sizes[1] == "large");
	sizes = TourneySizesFor(types, "continent", TourneyMode::Solo);
	CHECK_EQ(static_cast<int>(sizes.size()), 3);

	const TourneyMap tiny = {"continent", "random", "small"}, normal = {"continent", "random", "medium"}, huge = {"continent", "random", "large"};
	CHECK(TourneyMapFits(tiny, TourneyMode::Solo, types));
	CHECK(!TourneyMapFits(tiny, TourneyMode::Teams, types));
	CHECK(!TourneyMapFits(normal, TourneyMode::Teams, types));
	CHECK(TourneyMapFits(huge, TourneyMode::Teams, types));
	CHECK(TourneyMapFits(normal, TourneyMode::Teams, {}));
	TourneyMap scenario;
	scenario.kind = TourneyMapKind::Game;
	scenario.file = "Torneo_1vs1_Egypt.BFHP";
	scenario.players = 2;
	CHECK(TourneyMapFits(scenario, TourneyMode::Solo, types));
	CHECK(!TourneyMapFits(scenario, TourneyMode::Teams, types));
	scenario.players = 4;
	CHECK(TourneyMapFits(scenario, TourneyMode::Teams, types));

	// A small map grows to the next size that has room for four; a scenario for two becomes the default map.
	TourneyMap fitted = TourneyMapForMode(tiny, TourneyMode::Teams, types);
	CHECK(fitted.name == "continent" && fitted.size == "large");
	fitted = TourneyMapForMode({"island", "random", "small"}, TourneyMode::Teams, types);
	CHECK(fitted.name == "island" && fitted.size == "medium");
	fitted = TourneyMapForMode(huge, TourneyMode::Teams, types);
	CHECK(fitted.size == "large");
	scenario.players = 2;
	fitted = TourneyMapForMode(scenario, TourneyMode::Teams, types);
	CHECK(!fitted.Scenario() && fitted.name == "mediterranean" && fitted.size == "medium");
}

void CupForm()
{
	const int64_t now = 1794600000000LL;
	std::vector<TourneyMapType> types;
	CHECK(TourneyParseMapTypes(Parse(R"({"types":[{"id":"continent","sizes":["small","medium","large"],"sizes4":["medium","large"]}]})"), types));
	TourneyCupForm f;
	f.name = "Copa de parejas";
	f.startsAt = now + 3600000;
	f.maps = {{"continent", "random", "small"}, {"continent", "random", "medium"}};
	CHECK(TourneyCheckCup(f, now, types).empty());
	f.mode = TourneyMode::Teams;
	std::vector<TourneyCupProblem> problems = TourneyCheckCup(f, now, types);
	CHECK(problems.size() == 1 && problems[0] == TourneyCupProblem::MapSeats);
	// A map that can't be used at all is a bad map, not one without seats.
	f.maps[0] = {"nowhere", "random", "small"};
	problems = TourneyCheckCup(f, now, types);
	CHECK(problems.size() == 1 && problems[0] == TourneyCupProblem::BadMap);
	f.maps[0] = {"continent", "random", "small"};
	// The series' maps count when the cup has series, before the problems of the pool.
	f.bestOf = 3;
	TourneyMap two;
	two.kind = TourneyMapKind::Game;
	two.title = "Egypt";
	two.file = "Egypt.bfhp";
	two.hash = std::string(64, 'a');
	two.bytes = 100;
	two.players = 2;
	f.seriesPool = {{"continent", "random", "large"}, two};
	problems = TourneyCheckCup(f, now, types);
	CHECK(problems.size() == 2 && problems[0] == TourneyCupProblem::MapSeats && problems[1] == TourneyCupProblem::PoolCount);
	TourneyFitCupToMode(f, types);
	CHECK(f.maps.size() == 2 && f.maps[0].size == "medium" && f.maps[1].size == "medium");
	CHECK_EQ(static_cast<int>(f.seriesPool.size()), 1);
	f.seriesPool.clear();
	f.bestOf = 1;
	CHECK(TourneyCheckCup(f, now, types).empty());

	// The mode goes with the cup; a cup of teams does not sign its organizer up on its own.
	Json body = TourneyCupJson(f);
	CHECK_EQ(body.GetString("mode"), std::string("2v2"));
	CHECK(body.Get("play") && body.Get("play")->type == Json::Bool && !body.Get("play")->b);
	CHECK(!TourneyCupJson(f, true).Get("play"));
	f.mode = TourneyMode::Solo;
	CHECK(TourneyCupJson(f, true).GetBool("play"));
	body = TourneyCupJson(f);
	CHECK_EQ(body.GetString("mode"), std::string("1v1"));
	CHECK(body.GetBool("play"));

	// The form of a cup of teams keeps its mode.
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(kDetail), d));
	CHECK(TourneyCupFormOf(d).mode == TourneyMode::Teams);

	// 45 minutes a game two against two: 8 teams, single elimination with a final of 3, 2 x 45 + 3 x 45 + 20.
	TourneyCupForm e;
	e.maxEntries = 8;
	e.bestOf = 1;
	e.finalBestOf = 3;
	CHECK_EQ(TourneyEstimateOf(e).minutes, 40 * 5 + 20);
	e.mode = TourneyMode::Teams;
	CHECK_EQ(TourneyEstimateOf(e).minutes, 45 * 5 + 20);
	CHECK_EQ(TourneyEstimateFor(TourneyFormat::Swiss, 8, 1, 1, 0, 45).minutes, 45 * 3 + 20);
	CHECK_EQ(TourneyEstimateFor(8, 1, 3, 45).minutes, 45 * 5 + 20);
}

}

int main()
{
	Modes();
	Detail();
	Next();
	Claim();
	Mine();
	Refusals();
	Maps();
	CupForm();
	return test::Summary("tourneyteams");
}
