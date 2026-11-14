// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

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

// Answers of the league server (imperivm-league, feat/league) for a cup of six players in a bracket of eight.
const char *kList = R"({"tournaments":[{"id":2,"slug":"copa-de-prueba-71362","name":"Copa de prueba 71362","description":"Prueba del mod","official":true,
"season":null,"format":"single","status":"running","maxEntries":8,"minEntries":4,"entries":6,"bestOf":1,"finalBestOf":3,"seeding":"random","contentKey":null,
"contentName":null,"settings":{"preset":"competitive","hideCivs":true,"specDelay":120},"maps":[{"name":"mediterranean","type":"random","size":"small"},
{"name":"mountain_passes","type":"random","size":"small"}],"signupOpensAt":1794661611362,"checkinOpensAt":1794661071362,"startsAt":1794662871362,
"noshowMinutes":10},{"id":3,"slug":"bad"},{"slug":"no-id"}]})";

const char *kDetail = R"({"id":2,"slug":"copa-de-prueba-71362","name":"Copa de prueba 71362","description":"Prueba del mod","official":true,"season":null,
"format":"single","status":"running","maxEntries":8,"minEntries":4,"entries":[
{"entryId":6,"steamId":"76561198000000994","name":"Furio","status":"playing","seed":1,"checkedIn":true,"placement":null},
{"entryId":5,"steamId":"76561198000000995","name":"Brenno","status":"playing","seed":2,"checkedIn":true,"placement":null},
{"entryId":3,"steamId":"76561198000000997","name":"Varo","status":"playing","seed":3,"checkedIn":true,"placement":null},
{"entryId":1,"steamId":"76561198000000999","name":"Me","status":"playing","seed":4,"checkedIn":true,"placement":null},
{"entryId":4,"steamId":"76561198000000996","name":"Tulia","status":"out","seed":5,"checkedIn":true,"placement":5},
{"entryId":2,"steamId":"76561198000000998","name":"Escipion","status":"playing","seed":6,"checkedIn":true,"placement":null},
{"entryId":9,"steamId":"76561198000000990","name":"Late","status":"missed_checkin","seed":null,"checkedIn":false,"placement":null}],
"bestOf":1,"finalBestOf":3,"seeding":"random","contentKey":null,"contentName":null,"settings":{"preset":"competitive","hideCivs":true,"specDelay":120},
"maps":[{"name":"mediterranean","type":"random","size":"small"},{"name":"mountain_passes","type":"random","size":"small"}],"signupOpensAt":1794661611362,
"checkinOpensAt":1794661071362,"startsAt":1794662871362,"noshowMinutes":10,"rounds":3,"matches":[
{"id":1,"round":1,"position":0,"stage":"quarterfinal","bestOf":1,"a":{"entryId":6,"steamId":"76561198000000994","name":"Furio","seed":1},"b":null,
"score":[0,0],"winner":6,"status":"bye","lobby":null,"specLobby":null,"readyAt":null,"deadlineAt":null,"startedAt":null,"finishedAt":1794661671671},
{"id":2,"round":1,"position":1,"stage":"quarterfinal","bestOf":1,"a":{"entryId":1,"steamId":"76561198000000999","name":"Me","seed":4},
"b":{"entryId":4,"steamId":"76561198000000996","name":"Tulia","seed":5},"score":[1,0],"winner":1,"status":"done","lobby":"109775241234567890",
"specLobby":"109775241234567891","readyAt":1794661671671,"deadlineAt":1794662271671,"startedAt":null,"finishedAt":1794663000000},
{"id":3,"round":1,"position":2,"stage":"quarterfinal","bestOf":1,"a":{"entryId":5,"steamId":"76561198000000995","name":"Brenno","seed":2},"b":null,
"score":[0,0],"winner":5,"status":"bye","lobby":null,"specLobby":null,"readyAt":null,"deadlineAt":null,"startedAt":null,"finishedAt":1794661671671},
{"id":4,"round":1,"position":3,"stage":"quarterfinal","bestOf":1,"a":{"entryId":3,"steamId":"76561198000000997","name":"Varo","seed":3},
"b":{"entryId":2,"steamId":"76561198000000998","name":"Escipion","seed":6},"score":[0,0],"winner":null,"status":"live","lobby":"109775241234567892",
"specLobby":null,"readyAt":1794661671671,"deadlineAt":1794662271671,"startedAt":1794662000000,"finishedAt":null},
{"id":5,"round":2,"position":0,"stage":"semifinal","bestOf":1,"a":{"entryId":6,"steamId":"76561198000000994","name":"Furio","seed":1},
"b":{"entryId":1,"steamId":"76561198000000999","name":"Me","seed":4},"score":[0,0],"winner":null,"status":"ready","lobby":null,"specLobby":null,
"readyAt":1794663000000,"deadlineAt":1794663600000,"startedAt":null,"finishedAt":null},
{"id":6,"round":2,"position":1,"stage":"semifinal","bestOf":1,"a":{"entryId":5,"steamId":"76561198000000995","name":"Brenno","seed":2},"b":null,
"score":[0,0],"winner":null,"status":"pending","lobby":null,"specLobby":null,"readyAt":null,"deadlineAt":null,"startedAt":null,"finishedAt":null},
{"id":7,"round":3,"position":0,"stage":"final","bestOf":3,"a":null,"b":null,"score":[0,0],"winner":null,"status":"pending","lobby":null,"specLobby":null,
"readyAt":null,"deadlineAt":null,"startedAt":null,"finishedAt":null}],
"log":[{"at":1794661671671,"actor":"staff","action":"seeded","detail":"6 players, random seeding"}]})";

const char *kMine = R"({"next":{"tournament":{"id":2,"slug":"copa-de-prueba-71362","name":"Copa de prueba 71362","official":true},"match":5,"round":2,
"rounds":3,"stage":"semifinal","bestOf":3,"game":2,"score":[1,0],"status":"ready","readyAt":1794661671671,"deadlineAt":1794662271671,
"me":{"side":"b","seed":4,"inRoom":false},"opponent":{"name":"Furio","steamId":"76561198000000994","seed":1,"inRoom":true},
"map":{"name":"mountain_passes","type":"random","size":"small"},"contentKey":"c35f","contentName":"Caesar Mod",
"settings":{"preset":"competitive","hideCivs":true,"specDelay":120},"lobby":"109775241234567890","specLobby":null,"lobbyKey":"irm=tournament:5",
"roomClaimBy":"76561198000000994","canClaimNoShow":true},
"checkin":{"tournament":{"id":4,"slug":"copa-de-los-idus-noviembre","name":"Copa de los Idus"},"opensAt":1794660000000,"closesAt":1794661800000,
"checkedIn":false},
"entries":[{"tournamentId":2,"slug":"copa-de-prueba-71362","name":"Copa de prueba 71362","tournamentStatus":"running","status":"playing",
"startsAt":1794662871362,"checkedIn":true,"seed":4,"placement":null},
{"tournamentId":1,"slug":"copa-vieja","name":"Copa vieja","tournamentStatus":"finished","status":"out","startsAt":1794000000000,"checkedIn":true,
"seed":2,"placement":2}]})";

void Statuses()
{
	CHECK(TourneyStatusFromId("checkin") == TourneyStatus::Checkin);
	CHECK(TourneyStatusFromId("Running") == TourneyStatus::Running);
	CHECK(TourneyStatusFromId("draft") == TourneyStatus::Unknown);
	CHECK(TourneyMatchStatusFromId("walkover") == TourneyMatchStatus::Walkover);
	CHECK(TourneyMatchOver(TourneyMatchStatus::Bye));
	CHECK(TourneyMatchOver(TourneyMatchStatus::Done));
	CHECK(!TourneyMatchOver(TourneyMatchStatus::Disputed));
	CHECK(TourneyEntryStatusFromId("missed_checkin") == TourneyEntryStatus::MissedCheckin);
	CHECK(TourneyEntryActive(TourneyEntryStatus::Waitlist));
	CHECK(!TourneyEntryActive(TourneyEntryStatus::Out));
	CHECK(TourneyStageFromId("round_of_16") == TourneyStage::RoundOf16);
	CHECK(TourneyStageOf(3, 3) == TourneyStage::Final);
	CHECK(TourneyStageOf(1, 3) == TourneyStage::Quarterfinal);
	CHECK(TourneyStageOf(1, 5) == TourneyStage::RoundOf32);
	CHECK(TourneyStageOf(1, 6) == TourneyStage::Unknown);
	CHECK_EQ(std::string(TourneyStageName(TourneyStage::Semifinal)), std::string("Semifinal"));
}

void List()
{
	std::vector<TourneySummary> list;
	CHECK(TourneyParseList(Parse(kList), list));
	// A tournament without its id or its slug is left out.
	CHECK_EQ(static_cast<int>(list.size()), 2);
	const TourneySummary &t = list[0];
	CHECK(Same(t.id, 2));
	CHECK_EQ(t.slug, std::string("copa-de-prueba-71362"));
	CHECK(t.official);
	CHECK_EQ(t.season, -1);
	CHECK(t.status == TourneyStatus::Running);
	CHECK_EQ(t.entries, 6);
	CHECK_EQ(t.maxEntries, 8);
	CHECK_EQ(t.finalBestOf, 3);
	CHECK(t.contentKey.empty());
	CHECK_EQ(t.settings.preset, std::string("competitive"));
	CHECK(t.settings.hideCivs);
	CHECK_EQ(t.settings.specDelay, 120);
	CHECK_EQ(static_cast<int>(t.maps.size()), 2);
	CHECK_EQ(TourneyMapOfRound(t, 1).name, std::string("mediterranean"));
	CHECK_EQ(TourneyMapOfRound(t, 2).name, std::string("mountain_passes"));
	// The last map repeats for the later rounds.
	CHECK_EQ(TourneyMapOfRound(t, 3).name, std::string("mountain_passes"));
	CHECK(Same(t.startsAt, 1794662871362LL));
	CHECK_EQ(t.noshowMinutes, 10);
	CHECK(!TourneyParseList(Parse("{\"error\":\"x\"}"), list));
}

void Detail()
{
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(kDetail), d));
	CHECK_EQ(d.rounds, 3);
	CHECK_EQ(static_cast<int>(d.entrants.size()), 7);
	// The page lists its entries: the count is of the ones that play.
	CHECK_EQ(d.entries, 6);
	CHECK_EQ(static_cast<int>(d.matches.size()), 7);
	const TourneyMatch *bye = TourneyMatchAt(d, 1, 0);
	CHECK(bye != nullptr);
	CHECK(bye && bye->status == TourneyMatchStatus::Bye);
	CHECK(bye && !bye->b.Present());
	CHECK(bye && Same(bye->winner, 6));
	const TourneyMatch *played = TourneyMatchAt(d, 1, 1);
	CHECK(played && played->a.steamId == 76561198000000999ULL);
	CHECK(played && played->lobby == 109775241234567890ULL);
	CHECK(played && played->specLobby == 109775241234567891ULL);
	CHECK(played && played->scoreA == 1 && played->scoreB == 0);
	CHECK(played && played->stage == TourneyStage::Quarterfinal);
	const TourneyMatch *final = TourneyMatchAt(d, 3, 0);
	CHECK(final && final->bestOf == 3 && !final->a.Present());
	CHECK(TourneyMatchAt(d, 4, 0) == nullptr);
	const TourneyEntrant *tulia = TourneyEntrantOf(d, 4);
	CHECK(tulia && tulia->status == TourneyEntryStatus::Out && tulia->placement == 5);
	CHECK(TourneyEntrantBySteamId(d, 76561198000000994ULL) && TourneyEntrantBySteamId(d, 76561198000000994ULL)->name == "Furio");
	const std::vector<const TourneyMatch *> path = TourneyPathOf(d, 1);
	CHECK_EQ(static_cast<int>(path.size()), 2);
	CHECK(path.size() == 2 && path[0]->round == 1 && path[1]->round == 2);
	int done = 0, total = 0;
	TourneyProgress(d, done, total);
	CHECK_EQ(total, 5);
	CHECK_EQ(done, 1);
	CHECK(Same(TourneyChampion(d), 0));
	CHECK_EQ(static_cast<int>(TourneyPlaced(d, 5).size()), 1);
	CHECK_EQ(static_cast<int>(d.log.size()), 1);
	CHECK_EQ(d.log[0].action, std::string("seeded"));
}

void Champion()
{
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(R"({"id":1,"slug":"x","rounds":1,"entries":[{"entryId":1,"name":"A","status":"out","seed":2,"placement":2},
{"entryId":2,"name":"B","status":"playing","seed":1,"placement":1}],"matches":[{"id":1,"round":1,"position":0,"a":{"entryId":2},"b":{"entryId":1},
"winner":2,"status":"done","score":[2,1]}]})"), d));
	CHECK(Same(TourneyChampion(d), 2));
	CHECK_EQ(TourneyPlaced(d, 2)[0]->name, std::string("A"));
	// Without the placements, the final's winner.
	d.entrants[1].placement = 0;
	CHECK(Same(TourneyChampion(d), 2));
	d.matches[0].status = TourneyMatchStatus::Live;
	CHECK(Same(TourneyChampion(d), 0));
}

void Mine()
{
	TourneyMine m;
	CHECK(TourneyParseMine(Parse(kMine), m));
	const TourneyNext &n = m.next;
	CHECK(n.present);
	CHECK(Same(n.match, 5));
	CHECK(Same(n.tournamentId, 2));
	CHECK_EQ(n.slug, std::string("copa-de-prueba-71362"));
	CHECK(n.stage == TourneyStage::Semifinal);
	CHECK(n.status == TourneyMatchStatus::Ready);
	CHECK_EQ(n.bestOf, 3);
	CHECK_EQ(n.game, 2);
	// The score is the server's [a, b]; the player is side b here.
	CHECK_EQ(n.scoreMe, 0);
	CHECK_EQ(n.scoreThem, 1);
	CHECK(n.hasOpponent);
	CHECK_EQ(n.opponent.name, std::string("Furio"));
	CHECK(n.opponent.steamId == 76561198000000994ULL);
	CHECK(n.opponent.inRoom);
	CHECK(!n.me.inRoom);
	CHECK_EQ(n.me.seed, 4);
	CHECK_EQ(n.map.name, std::string("mountain_passes"));
	CHECK_EQ(n.contentName, std::string("Caesar Mod"));
	CHECK(n.lobby == 109775241234567890ULL);
	CHECK(n.specLobby == 0);
	CHECK(n.roomClaimBy == 76561198000000994ULL);
	CHECK(n.canClaimNoShow);
	CHECK_EQ(n.lobbyKey, std::string("irm=tournament:5"));
	CHECK(m.checkin.present);
	CHECK(Same(m.checkin.tournamentId, 4));
	CHECK(!m.checkin.checkedIn);
	CHECK_EQ(static_cast<int>(m.entries.size()), 2);
	CHECK(m.entries[1].tournamentStatus == TourneyStatus::Finished);
	CHECK_EQ(m.entries[1].placement, 2);
	CHECK(TourneyMyEntryOf(m, 1) != nullptr);
	CHECK(TourneyMyEntryOf(m, 3) == nullptr);
	CHECK(TourneyHasActiveEntry(m));

	TourneyMine empty;
	CHECK(TourneyParseMine(Parse(R"({"next":null,"checkin":null,"entries":[]})"), empty));
	CHECK(!empty.next.present);
	CHECK(!empty.checkin.present);
	CHECK(!TourneyHasActiveEntry(empty));
	TourneyMine over;
	CHECK(TourneyParseMine(Parse(R"({"next":null,"entries":[{"tournamentId":1,"tournamentStatus":"finished","status":"out"},
{"tournamentId":2,"tournamentStatus":"signup","status":"withdrawn"}]})"), over));
	CHECK(!TourneyHasActiveEntry(over));
	TourneyMine signed_;
	CHECK(TourneyParseMine(Parse(R"({"entries":[{"tournamentId":3,"tournamentStatus":"signup","status":"waitlist"}]})"), signed_));
	CHECK(TourneyHasActiveEntry(signed_));

	CHECK_EQ(TourneyPollEveryMs(true, false), 30000);
	CHECK_EQ(TourneyPollEveryMs(true, true), 30000);
	CHECK_EQ(TourneyPollEveryMs(false, true), 60000);
	CHECK_EQ(TourneyPollEveryMs(false, false), 0);
}

void Claim()
{
	TourneyClaim c;
	CHECK(TourneyParseClaim(Parse(R"({"create":true,"lobbyKey":"irm=tournament:5","map":{"name":"mediterranean","type":"random","size":"small"},
"settings":{"preset":"competitive","hideCivs":true,"specDelay":90},"contentKey":null})"), c));
	CHECK(c.kind == TourneyClaim::Create);
	CHECK_EQ(c.map.size, std::string("small"));
	CHECK_EQ(TourneySpecDelay(c.settings), 90);
	CHECK(TourneyParseClaim(Parse(R"({"join":"109775241234567890","lobbyKey":"irm=tournament:5"})"), c));
	CHECK(c.kind == TourneyClaim::Join);
	CHECK(c.lobby == 109775241234567890ULL);
	CHECK(TourneyParseClaim(Parse(R"({"wait":true,"by":"76561198000000994"})"), c));
	CHECK(c.kind == TourneyClaim::Wait);
	CHECK(c.by == 76561198000000994ULL);
	CHECK(!TourneyParseClaim(Parse(R"({"error":"not_ready"})"), c));
	TourneySettings none;
	CHECK_EQ(TourneySpecDelay(none), 120);
	none.specDelay = 20;
	CHECK_EQ(TourneySpecDelay(none), 60);
}

void Tags()
{
	CHECK_EQ(TourneyRoomTag(412), std::string("tournament:412"));
	CHECK_EQ(TourneyRoomTag(0), std::string());
	CHECK(Same(TourneyMatchOfTag("tournament:412"), 412));
	CHECK(Same(TourneyMatchOfTag("ranked:412"), 0));
	CHECK(Same(TourneyMatchOfTag("tournament:"), 0));
	CHECK(Same(TourneyMatchOfTag("tournament:4x"), 0));
	CHECK(Same(TourneyMatchOfTag(""), 0));
}

void Notices()
{
	TourneyMine m;
	CHECK(TourneyParseMine(Parse(kMine), m));
	TourneyTold told;
	std::vector<TourneyNotice> first = TourneyNewNotices(m, told);
	// The match and the check-in are told; the cup that ended before this session is not.
	CHECK_EQ(static_cast<int>(first.size()), 2);
	CHECK(first.size() == 2 && first[0].kind == TourneyNotice::MatchReady && Same(first[0].match, 5) && first[0].opponent == "Furio");
	CHECK(first.size() == 2 && first[1].kind == TourneyNotice::CheckinOpen && Same(first[1].tournamentId, 4));
	CHECK(TourneyNewNotices(m, told).empty());
	// A tournament of the player's that ends now is told once.
	m.entries[0].tournamentStatus = TourneyStatus::Finished;
	std::vector<TourneyNotice> over = TourneyNewNotices(m, told);
	CHECK_EQ(static_cast<int>(over.size()), 1);
	CHECK(over.size() == 1 && over[0].kind == TourneyNotice::Finished && Same(over[0].tournamentId, 2));
	CHECK(TourneyNewNotices(m, told).empty());
	// A player already in the room is not told the match is ready.
	TourneyTold fresh;
	m.next.me.inRoom = true;
	m.checkin.checkedIn = true;
	CHECK(TourneyNewNotices(m, fresh).empty());
}

void Clock()
{
	CHECK_EQ(TourneyClock(552000), std::string("09:12"));
	CHECK_EQ(TourneyClock(551200), std::string("09:12"));
	CHECK_EQ(TourneyClock(0), std::string("00:00"));
	CHECK_EQ(TourneyClock(-5000), std::string("00:00"));
	CHECK_EQ(TourneyClock(3723000), std::string("1:02:03"));
}

void Bracket()
{
	// The mockup's bracket of 16: boxes of 236 x 58, rounds 300 apart, first-round rows 76 apart under the round names.
	const TourneyBracketMetrics m;
	CHECK_EQ(TourneyBoxX(m, 1), 0);
	CHECK_EQ(TourneyBoxX(m, 4), 900);
	CHECK_EQ(TourneyBoxY(m, 1, 0), 34);
	CHECK_EQ(TourneyBoxY(m, 1, 7), 566);
	CHECK_EQ(TourneyBoxY(m, 2, 0), 72);
	CHECK_EQ(TourneyBoxY(m, 2, 1), 224);
	CHECK_EQ(TourneyBoxY(m, 3, 1), 452);
	CHECK_EQ(TourneyBoxY(m, 4, 0), 300);
	// Every box sits halfway between the two that feed it.
	for (int round = 2; round <= 5; round++) {
		for (int p = 0; p < (16 >> (round - 1)); p++) {
			CHECK_EQ(TourneyBoxMidY(m, round, p) * 2, TourneyBoxMidY(m, round - 1, 2 * p) + TourneyBoxMidY(m, round - 1, 2 * p + 1));
		}
	}
	const std::vector<TourneySegment> line = TourneyConnector(m, 1, 1, 4);
	CHECK_EQ(static_cast<int>(line.size()), 3);
	if (line.size() == 3) {
		CHECK_EQ(line[0].x1, 236);
		CHECK_EQ(line[0].y1, 139);
		CHECK_EQ(line[0].x2, 268);
		CHECK_EQ(line[1].x1, 268);
		CHECK_EQ(line[1].y2, 101);
		CHECK_EQ(line[2].x2, 300);
		CHECK_EQ(line[2].y1, TourneyBoxMidY(m, 2, 0));
	}
	CHECK(TourneyConnector(m, 4, 0, 4).empty());
	CHECK(TourneyConnector(m, 0, 0, 4).empty());
	CHECK_EQ(TourneyBracketWidth(m, 4), 1136);
	CHECK_EQ(TourneyBracketHeight(m, 4), 624);
	CHECK_EQ(TourneyBracketHeight(m, 1), 92);
	CHECK_EQ(TourneyBracketWidth(m, 0), 0);
	CHECK_EQ(TourneyBracketHeight(m, 5), 34 + 15 * 76 + 58);
}

}

int main()
{
	Statuses();
	List();
	Detail();
	Champion();
	Mine();
	Claim();
	Tags();
	Notices();
	Clock();
	Bracket();
	return test::Summary("tournament");
}
