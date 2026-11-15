// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>
#include <vector>

#include "core/basescenarios.h"
#include "core/json.h"
#include "core/league.h"
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
	CHECK_EQ(TourneyMapOfRound(t, 1, 3).name, std::string("mediterranean"));
	CHECK_EQ(TourneyMapOfRound(t, 2, 3).name, std::string("mountain_passes"));
	// A short list: the last map repeats for the later rounds.
	CHECK_EQ(TourneyMapOfRound(t, 3, 3).name, std::string("mountain_passes"));
	// A list that covers the bracket counts from the final: two players in a cup of four play the final's map.
	CHECK_EQ(TourneyMapOfRound(t, 1, 1).name, std::string("mountain_passes"));
	CHECK_EQ(TourneyMapOfRound(t, 1, 2).name, std::string("mediterranean"));
	CHECK_EQ(TourneyFullRounds(4), 2);
	CHECK_EQ(TourneyFullRounds(8), 3);
	CHECK_EQ(TourneyFullRounds(32), 5);
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

// The community cups (fase 2): who organises them, the organizer's views and the refusals of their actions.
void Community()
{
	std::vector<TourneySummary> list;
	CHECK(TourneyParseList(Parse(R"({"tournaments":[{"id":7,"slug":"copa-del-barrio","name":"Copa del barrio","official":false,"private":false,
"organizer":{"steamId":"76561198000000994","name":"Furio"},"status":"signup","maxEntries":8,"entries":3},
{"id":2,"slug":"copa-de-los-idus","name":"Copa de los Idus","official":true,"organizer":null,"status":"signup"}]})"), list));
	CHECK_EQ(static_cast<int>(list.size()), 2);
	CHECK(!list[0].official);
	CHECK(!list[0].isPrivate);
	CHECK(list[0].organizer.Present());
	CHECK(list[0].organizer.steamId == 76561198000000994ULL);
	CHECK_EQ(list[0].organizer.name, std::string("Furio"));
	CHECK(list[1].official);
	CHECK(!list[1].organizer.Present());

	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(R"({"id":7,"slug":"copa-del-barrio","private":true,"organizer":{"steamId":"76561198000000994","name":"Furio"},
"status":"checkin","entries":[{"entryId":3,"steamId":"76561198000000990","name":"Late","status":"signed","checkedIn":false}],"matches":[]})"), d));
	CHECK(d.isPrivate);
	CHECK_EQ(d.organizer.name, std::string("Furio"));

	TourneyNext n;
	CHECK(TourneyParseNext(Parse(R"({"tournament":{"id":7,"slug":"copa-del-barrio","name":"Copa del barrio","official":false,"private":true,
"organizer":{"steamId":"76561198000000994","name":"Furio"}},"match":9,"round":1,"rounds":2,"status":"ready"})"), n));
	CHECK(n.isPrivate);
	CHECK(n.organizer.steamId == 76561198000000994ULL);

	TourneyMine m;
	CHECK(TourneyParseMine(Parse(R"({"next":null,"checkin":null,"entries":[],
"organize":{"allowed":false,"gamesPlayed":3,"gamesNeeded":10,"active":{"id":7,"slug":"copa-del-barrio"}},
"organized":[{"id":7,"slug":"copa-del-barrio","name":"Copa del barrio","status":"checkin","private":true,"inviteCode":"K7RX2M","entries":5,
"checkedIn":3,"maxEntries":8,"minEntries":4,"startsAt":1794662871362,"checkinOpensAt":1794661071362,"openDisputes":2},
{"id":5,"slug":"vieja","name":"Vieja","status":"finished","private":false,"inviteCode":null,"entries":4,"checkedIn":4,"maxEntries":4,"minEntries":4,
"startsAt":1794000000000,"checkinOpensAt":1793998200000,"openDisputes":0},{"id":0,"slug":"bad"}]})"), m));
	CHECK(m.organize.known);
	CHECK(!m.organize.allowed);
	CHECK_EQ(m.organize.GamesMissing(), 7);
	CHECK(Same(m.organize.activeId, 7));
	CHECK_EQ(m.organize.activeSlug, std::string("copa-del-barrio"));
	CHECK_EQ(static_cast<int>(m.organized.size()), 2);
	const TourneyOrganized *own = TourneyOrganizedOf(m, 7);
	CHECK(own != nullptr);
	CHECK(own && own->status == TourneyStatus::Checkin && own->isPrivate && own->inviteCode == "K7RX2M");
	CHECK(own && own->entries == 5 && own->checkedIn == 3 && own->minEntries == 4 && own->openDisputes == 2);
	CHECK(own && Same(own->checkinOpensAt, 1794661071362LL));
	CHECK(TourneyOrganizedOf(m, 5) && TourneyOrganizedOf(m, 5)->inviteCode.empty());
	CHECK(TourneyOrganizedOf(m, 9) == nullptr);
	// A cup of the player's on makes the mod ask every 30 s, even when they don't play it.
	CHECK(TourneyHasActiveEntry(m));
	m.organized[0].status = TourneyStatus::Cancelled;
	CHECK(!TourneyHasActiveEntry(m));
	TourneyMine old;
	CHECK(TourneyParseMine(Parse(R"({"next":null,"entries":[]})"), old));
	CHECK(!old.organize.known);
	CHECK(old.organized.empty());
	TourneyOrganize allowed;
	allowed.gamesPlayed = 12;
	allowed.gamesNeeded = 10;
	CHECK_EQ(allowed.GamesMissing(), 0);

	std::vector<TourneyDispute> disputes;
	CHECK(TourneyParseDisputes(Parse(R"({"disputes":[{"matchId":12,"text":"He left and the mod gave him the win","openedBy":"76561198000000990",
"createdAt":1794663000000},{"matchId":13,"text":"x","openedBy":{"steamId":"76561198000000991","name":"Varo"},"createdAt":1},
{"matchId":14,"text":"y","openedBy":"Tulia"},{"text":"no match"}]})"), disputes));
	CHECK_EQ(static_cast<int>(disputes.size()), 3);
	CHECK(disputes.size() == 3 && Same(disputes[0].matchId, 12) && disputes[0].openedBy == 76561198000000990ULL && disputes[0].openedByName.empty());
	CHECK(disputes.size() == 3 && Same(disputes[0].createdAt, 1794663000000LL));
	CHECK(disputes.size() == 3 && disputes[1].openedBy == 76561198000000991ULL && disputes[1].openedByName == "Varo");
	CHECK(disputes.size() == 3 && disputes[2].openedBy == 0 && disputes[2].openedByName == "Tulia");
	CHECK(!TourneyParseDisputes(Parse(R"({"error":"not_organizer"})"), disputes));

	TourneyRefusal r = TourneyParseRefusal(Parse(R"({"error":"not_eligible","message":"Play more","gamesPlayed":4,"gamesNeeded":10})"));
	CHECK_EQ(r.code, std::string("not_eligible"));
	CHECK_EQ(r.gamesPlayed, 4);
	CHECK_EQ(r.gamesNeeded, 10);
	r = TourneyParseRefusal(Parse(R"({"error":"already_organizing","message":"x","slug":"copa-del-barrio"})"));
	CHECK_EQ(r.slug, std::string("copa-del-barrio"));
	r = TourneyParseRefusal(Parse(R"({"error":"not_enough","checkedIn":3,"needed":4})"));
	CHECK_EQ(r.code, std::string("not_enough"));
	CHECK_EQ(r.checkedIn, 3);
	CHECK_EQ(r.needed, 4);
	r = TourneyParseRefusal(Parse(R"({"error":"rate_limited","message":"Too many","retryAfter":3600})"));
	CHECK_EQ(r.retryAfter, 3600);
	for (const char *code : {"invite_required", "own_match", "not_organizer"}) {
		r = TourneyParseRefusal(Parse(std::string("{\"error\":\"") + code + "\",\"message\":\"m\"}"));
		CHECK_EQ(r.code, std::string(code));
		CHECK_EQ(r.message, std::string("m"));
	}
	CHECK(TourneyParseRefusal(Parse("[]")).code.empty());

	TourneyCreated c;
	CHECK(TourneyParseCreated(Parse(R"({"id":7,"slug":"copa-del-barrio","inviteCode":"K7RX2M"})"), c));
	CHECK(Same(c.id, 7));
	CHECK_EQ(c.inviteCode, std::string("K7RX2M"));
	CHECK(TourneyParseCreated(Parse(R"({"id":8,"slug":"abierta","inviteCode":null})"), c));
	CHECK(c.inviteCode.empty());
	CHECK(!TourneyParseCreated(Parse(R"({"error":"bad_request"})"), c));

	CHECK_EQ(TourneyTidyCode(" k7r-x2m "), std::string("K7RX2M"));
	CHECK(TourneyCodeValid("K7RX2M"));
	CHECK(TourneyCodeValid(TourneyTidyCode("k7rx2m")));
	CHECK(!TourneyCodeValid("K0RX2M"));
	CHECK(!TourneyCodeValid("KORX2M"));
	CHECK(!TourneyCodeValid("K1RX2M"));
	CHECK(!TourneyCodeValid("KIRX2M"));
	CHECK(!TourneyCodeValid("K7RX2"));
	CHECK(!TourneyCodeValid("K7RX2MM"));
	CHECK(!TourneyCodeValid("K7RX2!"));
}

bool HasProblem(const std::vector<TourneyCupProblem> &list, TourneyCupProblem p)
{
	for (TourneyCupProblem x : list) {
		if (x == p) return true;
	}
	return false;
}

void CupForm()
{
	const int64_t now = 1794600000000LL;
	TourneyCupForm f;
	f.name = "  Copa de Espa\xC3\xB1" "a  ";
	f.maps = {{"mediterranean", "random", "medium"}, {"island", "random", "small"}};
	f.startsAt = now + 3600000;
	CHECK(TourneyCheckCup(f, now).empty());
	CHECK_EQ(TourneyTextLength("Copa de Espa\xC3\xB1" "a"), 14);
	CHECK_EQ(TourneyTextLength("\xF0\x9F\x8F\x86"), 2);
	CHECK_EQ(TourneyTrim("  a b \t"), std::string("a b"));
	CHECK_EQ(TourneyTrim("   "), std::string());

	TourneyCupForm bad = f;
	bad.name = " ab ";
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::NameShort));
	bad.name = std::string(41, 'x');
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::NameLong));
	bad = f;
	bad.description = std::string(201, 'x');
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::DescriptionLong));
	bad.description = std::string(200, 'x');
	CHECK(TourneyCheckCup(bad, now).empty());
	bad = f;
	bad.maxEntries = 6;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Places));
	bad.maxEntries = 4;
	bad.minEntries = 5;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Minimum));
	bad.minEntries = 1;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Minimum));
	bad = f;
	bad.bestOf = 2;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Series));
	bad.bestOf = 5;
	CHECK(TourneyCheckCup(bad, now).empty());
	bad = f;
	bad.finalBestOf = 2;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Series));
	bad = f;
	bad.maps.clear();
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::NoMaps));
	bad.maps.assign(6, {"coastal", "random", "medium"});
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::TooManyMaps));
	// The Mediterranean has no small map for two.
	bad.maps = {{"mediterranean", "random", "small"}};
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::BadMap));
	bad = f;
	bad.startsAt = now + 19 * 60000;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::StartSoon));
	bad.startsAt = now + 20 * 60000;
	CHECK(TourneyCheckCup(bad, now).empty());
	bad.startsAt = now + 15 * 86400000LL;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::StartLate));
	bad = f;
	bad.checkinMinutes = 20;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Checkin));

	Json body = TourneyCupJson(f);
	Json back;
	CHECK(Json::Parse(body.Dump(), back));
	CHECK_EQ(back.GetString("name"), std::string("Copa de Espa\xC3\xB1" "a"));
	CHECK(back.Get("description") == nullptr);
	CHECK_EQ(static_cast<int>(back.GetNumber("maxEntries")), 8);
	CHECK_EQ(static_cast<int>(back.GetNumber("minEntries")), 4);
	CHECK_EQ(static_cast<int>(back.GetNumber("bestOf")), 1);
	CHECK_EQ(static_cast<int>(back.GetNumber("finalBestOf")), 3);
	CHECK_EQ(back.GetString("seeding"), std::string("rating"));
	CHECK(Same(static_cast<int64_t>(back.GetNumber("startsAt")), now + 3600000));
	CHECK_EQ(static_cast<int>(back.GetNumber("checkinMinutes")), 30);
	CHECK(!back.GetBool("private", true));
	CHECK(back.GetBool("play"));
	CHECK(back.Get("contentKey") == nullptr);
	CHECK(back.Get("contentName") == nullptr);
	const Json *maps = back.Get("maps");
	CHECK(maps && maps->type == Json::Array && maps->arr.size() == 2);
	CHECK(maps && maps->arr.size() == 2 && maps->arr[1].GetString("name") == "island" && maps->arr[1].GetString("size") == "small");
	f.description = " Una tarde ";
	f.randomSeeding = true;
	f.isPrivate = true;
	f.play = false;
	f.contentKey = "c35f0a1b2c3d4e5f";
	f.contentName = "Caesar Mod";
	CHECK(Json::Parse(TourneyCupJson(f).Dump(), back));
	CHECK_EQ(back.GetString("description"), std::string("Una tarde"));
	CHECK_EQ(back.GetString("seeding"), std::string("random"));
	CHECK(back.GetBool("private"));
	CHECK(!back.GetBool("play", true));
	CHECK_EQ(back.GetString("contentKey"), std::string("c35f0a1b2c3d4e5f"));
	CHECK_EQ(back.GetString("contentName"), std::string("Caesar Mod"));

	CHECK_EQ(TourneyCupDefaultMinimum(4), 4);
	CHECK_EQ(TourneyCupDefaultMinimum(16), 4);
	CHECK_EQ(TourneyCupDefaultMinimum(2), 2);
	CHECK_EQ(TourneyRoundsFor(4), 2);
	CHECK_EQ(TourneyRoundsFor(8), 3);
	CHECK_EQ(TourneyRoundsFor(16), 4);
	CHECK_EQ(TourneyRoundsFor(6), 3);
}

// The full form (bloque A): the three kinds of map, the room's rules, the requirements and the body of a cup made or changed.
const char *kUploadMap = "3f427effca4af745528c37a2a7a238be65d76299da19db079ced5f863e36d2b5";

void Maps()
{
	TourneyMap m = TourneyMapFromJson(nullptr);
	CHECK(!m.Known());
	Json j = Parse(R"({"name":"mountain_passes","type":"random","size":"medium"})");
	m = TourneyMapFromJson(&j);
	CHECK(m.kind == TourneyMapKind::Random);
	CHECK(!m.Scenario());
	CHECK(m.Known());
	CHECK_EQ(m.name, std::string("mountain_passes"));
	j = Parse(R"({"kind":"random","name":"island","type":"random","size":"small"})");
	m = TourneyMapFromJson(&j);
	CHECK(m.kind == TourneyMapKind::Random && m.size == "small");

	j = Parse(R"({"kind":"scenario","source":"game","title":"Torneo 1vs1 Egypt","file":"Torneo_1vs1_Egypt.BFHP",
"hash":"7C4198A92AC1524D2AA26B48C25BB63914FE4A9CF3FDC691574ED5687A89CA35","bytes":1003008,"players":2})");
	m = TourneyMapFromJson(&j);
	CHECK(m.kind == TourneyMapKind::Game);
	CHECK(m.Scenario() && m.Known());
	CHECK_EQ(m.file, std::string("Torneo_1vs1_Egypt.BFHP"));
	CHECK_EQ(m.hash, std::string("7c4198a92ac1524d2aa26b48c25bb63914fe4a9cf3fdc691574ed5687a89ca35"));
	CHECK(Same(m.bytes, 1003008));
	CHECK_EQ(m.players, 2);
	CHECK_EQ(TourneyScenarioStem(m.file), std::string("Torneo_1vs1_Egypt"));

	j = Parse(std::string(R"({"kind":"scenario","source":"upload","file":"Mi mapa.bfhp","hash":")") + kUploadMap + R"(","bytes":1421824,"players":4})");
	m = TourneyMapFromJson(&j);
	CHECK(m.kind == TourneyMapKind::Upload);
	// Without a title the file's name shows.
	CHECK_EQ(m.title, std::string("Mi mapa"));
	Json back;
	CHECK(Json::Parse(TourneyMapJson(m).Dump(), back));
	CHECK_EQ(back.GetString("kind"), std::string("scenario"));
	CHECK_EQ(back.GetString("source"), std::string("upload"));
	CHECK_EQ(back.GetString("file"), std::string("Mi mapa.bfhp"));
	CHECK_EQ(back.GetString("hash"), std::string(kUploadMap));
	CHECK_EQ(static_cast<int>(back.GetNumber("bytes")), 1421824);
	CHECK_EQ(static_cast<int>(back.GetNumber("players")), 4);
	TourneyMap again = TourneyMapFromJson(&back);
	CHECK(TourneySameMap(m, again));
	again.hash[0] = '0';
	CHECK(!TourneySameMap(m, again));
	CHECK(Json::Parse(TourneyMapJson({"coastal", "random", "large"}).Dump(), back));
	CHECK_EQ(back.GetString("kind"), std::string("random"));
	CHECK_EQ(back.GetString("type"), std::string("random"));
	CHECK_EQ(back.GetString("size"), std::string("large"));
	CHECK(TourneySameMap({"coastal", "random", "large"}, TourneyMapFromJson(&back)));
	CHECK(!TourneySameMap({"coastal", "random", "large"}, m));

	// A file the mod would not write is dropped.
	for (const char *file : {"..\\\\gbr.bfhp", "maps/a.bfhp", "a.exe", ".bfhp", "C:a.bfhp"}) {
		j = Parse(std::string(R"({"kind":"scenario","source":"upload","file":")") + file + R"(","hash":"x"})");
		CHECK(!TourneyMapFromJson(&j).Known());
		CHECK(!TourneyScenarioFileValid(file));
	}
	CHECK(TourneyScenarioFileValid("Island War.BFHP"));
	CHECK(TourneySha256Valid(kUploadMap));
	CHECK(!TourneySha256Valid("3F427EFFCA4AF745528C37A2A7A238BE65D76299DA19DB079CED5F863E36D2B5"));
	CHECK(!TourneySha256Valid("abc"));
}

void Settings()
{
	Json j = Parse(R"({"preset":"various","hideCivs":false,"fog":true,"explored":false,"diplomacy":"locked","specDelay":0})");
	TourneySettings s = TourneySettingsFromJson(&j);
	CHECK_EQ(s.fog, 1);
	CHECK_EQ(s.explored, 0);
	CHECK_EQ(s.diplomacy, std::string("locked"));
	// No spectators.
	CHECK_EQ(TourneySpecDelay(s), 0);
	TourneyRoomRules r = TourneyRulesOf(s);
	CHECK(r.preset == TourneyPreset::Various);
	CHECK(r.fog && !r.explored && r.lockedDiplomacy && !r.hideCivs);

	// The official tournaments only give the preset: the switches are the preset's.
	j = Parse(R"({"preset":"competitive","hideCivs":true,"specDelay":120})");
	s = TourneySettingsFromJson(&j);
	CHECK_EQ(s.fog, -1);
	r = TourneyRulesOf(s);
	CHECK(r.fog && !r.explored && r.lockedDiplomacy && r.hideCivs);
	r = TourneyPresetRules(TourneyPreset::Classic);
	CHECK(r.fog && !r.explored && !r.lockedDiplomacy);
	r = TourneyPresetRules(TourneyPreset::Various);
	CHECK(!r.fog && r.explored && !r.lockedDiplomacy);
	CHECK(TourneyPresetFromId("CLASSIC") == TourneyPreset::Classic);
	CHECK(TourneyPresetFromId("odd") == TourneyPreset::Competitive);
	CHECK(!TourneyPresetValid("odd"));

	TourneySettings d = TourneyCupDefaultSettings();
	CHECK_EQ(d.preset, std::string("competitive"));
	CHECK(d.hideCivs);
	CHECK_EQ(d.specDelay, 120);
	CHECK(d.fog == 1 && d.explored == 0 && d.diplomacy == "locked");
	Json back;
	CHECK(Json::Parse(TourneySettingsJson(d).Dump(), back));
	CHECK_EQ(back.GetString("preset"), std::string("competitive"));
	CHECK(back.GetBool("hideCivs"));
	CHECK(back.GetBool("fog"));
	CHECK(!back.GetBool("explored", true));
	CHECK_EQ(back.GetString("diplomacy"), std::string("locked"));
	CHECK_EQ(static_cast<int>(back.GetNumber("specDelay", -1)), 120);
	TourneySettings none;
	none.specDelay = 0;
	CHECK(Json::Parse(TourneySettingsJson(none).Dump(), back));
	CHECK_EQ(static_cast<int>(back.GetNumber("specDelay", -1)), 0);
	for (int ok : {0, 60, 120, 300}) CHECK(TourneySpecDelayValid(ok));
	CHECK(!TourneySpecDelayValid(90));
	TourneySettings classic;
	classic.preset = "classic";
	classic.fog = 0;
	const TourneySettings full = TourneyExplicitSettings(classic);
	CHECK(full.fog == 0 && full.explored == 0 && full.diplomacy == "free" && full.specDelay == 120);
}

void Requirements()
{
	TourneyRequirements r = TourneyRequirementsFromJson(nullptr);
	CHECK(!r.Any());
	Json j = Parse(R"({"minGames":10,"maxTier":"centurion"})");
	r = TourneyRequirementsFromJson(&j);
	CHECK(r.Any());
	CHECK_EQ(r.minGames, 10);
	CHECK(r.maxTier == LeagueTier::Centurion);
	j = Parse(R"({"minGames":0,"maxTier":null})");
	r = TourneyRequirementsFromJson(&j);
	CHECK(!r.Any());
	// Pretorian and Imperator are no cap.
	j = Parse(R"({"minGames":0,"maxTier":"imperator"})");
	CHECK(TourneyRequirementsFromJson(&j).maxTier == LeagueTier::None);

	TourneyRequirements cup;
	cup.minGames = 10;
	cup.maxTier = LeagueTier::Signifer;
	Json back;
	CHECK(Json::Parse(TourneyRequirementsJson(cup).Dump(), back));
	CHECK_EQ(static_cast<int>(back.GetNumber("minGames")), 10);
	CHECK_EQ(back.GetString("maxTier"), std::string("signifer"));
	CHECK(Json::Parse(TourneyRequirementsJson(TourneyRequirements()).Dump(), back));
	CHECK(back.Get("maxTier") && back.Get("maxTier")->type == Json::Null);

	CHECK(TourneyCheckRequirements(cup, 3, LeagueTier::Recruit) == TourneyUnmet::Games);
	CHECK(TourneyCheckRequirements(cup, 10, LeagueTier::Signifer) == TourneyUnmet::None);
	CHECK(TourneyCheckRequirements(cup, 12, LeagueTier::Centurion) == TourneyUnmet::Tier);
	// Without a rank this season the cap lets the player in.
	CHECK(TourneyCheckRequirements(cup, 40, LeagueTier::None) == TourneyUnmet::None);
	CHECK(TourneyCheckRequirements(TourneyRequirements(), 0, LeagueTier::Imperator) == TourneyUnmet::None);

	TourneyRefusal no = TourneyParseRefusal(Parse(R"({"error":"requirement","message":"m","kind":"games","have":3,"need":10})"));
	CHECK_EQ(no.code, std::string("requirement"));
	CHECK_EQ(no.requirement, std::string("games"));
	CHECK_EQ(no.have, std::string("3"));
	CHECK_EQ(no.need, std::string("10"));
	no = TourneyParseRefusal(Parse(R"({"error":"requirement","kind":"tier","have":"tribune","need":"centurion"})"));
	CHECK_EQ(no.requirement, std::string("tier"));
	CHECK_EQ(no.have, std::string("tribune"));
	CHECK_EQ(no.need, std::string("centurion"));
	no = TourneyParseRefusal(Parse(std::string(R"({"error":"map_missing","hash":")") + kUploadMap + "\"}"));
	CHECK_EQ(no.hash, std::string(kUploadMap));

	TourneySummary t;
	CHECK(TourneyParseSummary(Parse(R"({"id":7,"slug":"novatos","requirements":{"minGames":5,"maxTier":"legionary"},
"settings":{"preset":"classic","hideCivs":false,"fog":false,"explored":true,"diplomacy":"free","specDelay":300}})"), t));
	CHECK_EQ(t.requirements.minGames, 5);
	CHECK(t.requirements.maxTier == LeagueTier::Legionary);
	CHECK_EQ(TourneySpecDelay(t.settings), 300);
}

void CupFull()
{
	const int64_t now = 1794600000000LL;
	TourneyCupForm f;
	f.name = "Copa de novatos";
	f.startsAt = now + 3600000;
	TourneyMap game;
	game.kind = TourneyMapKind::Game;
	game.title = "Torneo 1vs1 Egypt";
	game.file = "Torneo_1vs1_Egypt.BFHP";
	game.hash = "7c4198a92ac1524d2aa26b48c25bb63914fe4a9cf3fdc691574ed5687a89ca35";
	game.bytes = 1003008;
	game.players = 2;
	TourneyMap upload = game;
	upload.kind = TourneyMapKind::Upload;
	upload.file = "Mi mapa.bfhp";
	upload.hash = kUploadMap;
	f.maps = {{"mediterranean", "random", "medium"}, game, upload};
	f.requirements.minGames = 10;
	f.requirements.maxTier = LeagueTier::Centurion;
	f.noshowMinutes = 10;
	CHECK(TourneyCheckCup(f, now).empty());

	TourneyCupForm bad = f;
	bad.maps[2].players = 1;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::BadMap));
	bad = f;
	bad.maps[1].hash = "abc";
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::BadMap));
	bad = f;
	bad.maps[2].bytes = kTourneyScenarioMaxBytes + 1;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::MapTooLarge));
	bad = f;
	bad.noshowMinutes = 30;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Noshow));
	bad = f;
	bad.requirements.minGames = 101;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Requirements));
	bad = f;
	bad.requirements.maxTier = LeagueTier::Praetorian;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Requirements));
	bad = f;
	bad.settings.specDelay = 30;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Settings));
	bad = f;
	bad.settings.preset = "chaos";
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Settings));

	Json back;
	CHECK(Json::Parse(TourneyCupJson(f).Dump(), back));
	CHECK_EQ(static_cast<int>(back.GetNumber("noshowMinutes")), 10);
	const Json *settings = back.Get("settings");
	CHECK(settings && settings->GetString("preset") == "competitive" && settings->GetBool("hideCivs") && settings->GetString("diplomacy") == "locked");
	CHECK(settings && static_cast<int>(settings->GetNumber("specDelay")) == 120);
	const Json *req = back.Get("requirements");
	CHECK(req && static_cast<int>(req->GetNumber("minGames")) == 10 && req->GetString("maxTier") == "centurion");
	const Json *maps = back.Get("maps");
	CHECK(maps && maps->arr.size() == 3);
	CHECK(maps && maps->arr.size() == 3 && maps->arr[0].GetString("kind") == "random" && maps->arr[0].GetString("name") == "mediterranean");
	CHECK(maps && maps->arr.size() == 3 && maps->arr[1].GetString("source") == "game" && maps->arr[1].GetString("file") == "Torneo_1vs1_Egypt.BFHP");
	CHECK(maps && maps->arr.size() == 3 && maps->arr[2].GetString("source") == "upload" && maps->arr[2].GetString("hash") == kUploadMap);
	CHECK(back.Get("description") == nullptr);
	CHECK(back.Get("contentKey") == nullptr);

	// A change writes the description and the content even when there are none, so that they go away.
	CHECK(Json::Parse(TourneyCupJson(f, true).Dump(), back));
	CHECK(back.Get("description") && back.GetString("description").empty());
	CHECK(back.Get("contentKey") && back.Get("contentKey")->type == Json::Null);
	CHECK(Json::Parse(TourneyCupDescriptionJson("  Solo la tarde  ").Dump(), back));
	CHECK_EQ(back.GetString("description"), std::string("Solo la tarde"));
	CHECK_EQ(static_cast<int>(back.obj.size()), 1);

	std::vector<TourneyMap> fit = {game};
	TourneyFitMaps(fit, 3);
	CHECK_EQ(static_cast<int>(fit.size()), 3);
	CHECK(fit.size() == 3 && TourneySameMap(fit[2], game));
	TourneyFitMaps(fit, 2);
	CHECK_EQ(static_cast<int>(fit.size()), 2);
	fit.clear();
	TourneyFitMaps(fit, 1);
	CHECK(fit.size() == 1 && fit[0].name == "mediterranean");

	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(std::string(R"({"id":7,"slug":"novatos","name":"Copa de novatos","description":"Para empezar","official":false,
"private":true,"organizer":{"steamId":"76561198000000994","name":"Furio"},"status":"signup","maxEntries":8,"minEntries":4,"bestOf":3,"finalBestOf":5,
"seeding":"random","contentKey":null,"settings":{"preset":"various","hideCivs":false,"specDelay":60},"requirements":{"minGames":5,"maxTier":null},
"maps":[{"kind":"scenario","source":"upload","title":"Mi mapa","file":"Mi mapa.bfhp","hash":")") + kUploadMap + R"(","bytes":1421824,"players":2}],
"startsAt":1794662871362,"checkinOpensAt":1794661971362,"noshowMinutes":20,
"entries":[{"entryId":1,"steamId":"76561198000000994","name":"Furio","status":"signed"}],"matches":[]})"), d));
	TourneyCupForm edit = TourneyCupFormOf(d);
	CHECK_EQ(edit.name, std::string("Copa de novatos"));
	CHECK_EQ(edit.checkinMinutes, 15);
	CHECK_EQ(edit.noshowMinutes, 20);
	CHECK(edit.isPrivate && edit.play && edit.randomSeeding);
	CHECK_EQ(edit.bestOf, 3);
	CHECK_EQ(edit.finalBestOf, 5);
	CHECK_EQ(static_cast<int>(edit.maps.size()), 3);
	CHECK(edit.maps.size() == 3 && edit.maps[2].kind == TourneyMapKind::Upload);
	CHECK(edit.settings.preset == "various" && edit.settings.fog == 0 && edit.settings.explored == 1 && edit.settings.specDelay == 60);
	CHECK_EQ(edit.requirements.minGames, 5);
	// Only the organizer is in: everything can still change.
	CHECK(!TourneyCupLocked(d));
	d.entrants.push_back({2, 76561198000000990ULL, "Varo", TourneyEntryStatus::Waitlist});
	CHECK(TourneyCupLocked(d));
	d.entrants.back().status = TourneyEntryStatus::Withdrawn;
	CHECK(!TourneyCupLocked(d));
	d.entrants.erase(d.entrants.begin());
	CHECK(!TourneyCupFormOf(d).play);
}

// More formats, the by-rounds pace and the series' maps (bloque B): what the views add and what the form sends.
void Formats()
{
	CHECK(TourneyFormatFromId("swiss") == TourneyFormat::Swiss);
	CHECK(TourneyFormatFromId("Double") == TourneyFormat::Double);
	CHECK(TourneyFormatFromId("single") == TourneyFormat::Single);
	CHECK(TourneyFormatFromId("league") == TourneyFormat::Single);
	CHECK_EQ(std::string(TourneyFormatId(TourneyFormat::Swiss)), std::string("swiss"));
	CHECK(TourneyPaceFromId("rounds") == TourneyPace::Rounds);
	CHECK(TourneyPaceFromId("") == TourneyPace::Live);
	CHECK_EQ(std::string(TourneyPaceId(TourneyPace::Rounds)), std::string("rounds"));
	for (int days : {1, 2, 3, 7}) CHECK(TourneyRoundDaysValid(days));
	for (int days : {0, 4, 5, 14}) CHECK(!TourneyRoundDaysValid(days));
	// By default ceil(log2(players)): 3 with 5 to 8, 4 with 9 to 16, 5 with 17 to 32; a choice of 3 to 6 stays.
	CHECK_EQ(TourneySwissRounds(0, 5), 3);
	CHECK_EQ(TourneySwissRounds(0, 8), 3);
	CHECK_EQ(TourneySwissRounds(0, 9), 4);
	CHECK_EQ(TourneySwissRounds(0, 16), 4);
	CHECK_EQ(TourneySwissRounds(0, 17), 5);
	CHECK_EQ(TourneySwissRounds(0, 32), 5);
	CHECK_EQ(TourneySwissRounds(6, 8), 6);
	CHECK_EQ(TourneySwissRounds(2, 8), 3);
	CHECK_EQ(TourneySwissRounds(7, 16), 4);

	std::vector<TourneySummary> list;
	CHECK(TourneyParseList(Parse(R"({"tournaments":[{"id":7,"slug":"suizo","name":"Suizo de otono","status":"running","format":"swiss","swissRounds":null,
"pace":"rounds","roundDays":2,"maxEntries":16,"entries":12,"bestOf":3,"finalBestOf":3,"currentRound":2,"roundEndsAt":1794950000000,
"seriesPool":[{"kind":"random","name":"mediterranean","type":"random","size":"medium"},{"kind":"random","name":"island","type":"random","size":"small"},
{"kind":"random","name":"coastal","type":"random","size":"large"}]},
{"id":8,"slug":"doble","name":"Doble","status":"signup","format":"double","pace":"live","maxEntries":8,"entries":3},
{"id":9,"slug":"vieja","name":"Vieja","status":"finished","maxEntries":8,"entries":8}]})"), list));
	CHECK_EQ(static_cast<int>(list.size()), 3);
	if (list.size() == 3) {
		const TourneySummary &s = list[0];
		CHECK(TourneyFormatOf(s) == TourneyFormat::Swiss);
		CHECK_EQ(s.swissRounds, 0);
		CHECK(s.pace == TourneyPace::Rounds);
		CHECK_EQ(s.roundDays, 2);
		CHECK_EQ(s.currentRound, 2);
		CHECK(Same(s.roundEndsAt, 1794950000000LL));
		CHECK_EQ(static_cast<int>(s.seriesPool.size()), 3);
		CHECK(s.seriesPool.size() == 3 && s.seriesPool[1].name == "island" && s.seriesPool[2].size == "large");
		// Twelve players in a Swiss of the automatic count: four rounds.
		CHECK_EQ(TourneyRoundCount(s), 4);
		CHECK(TourneyFormatOf(list[1]) == TourneyFormat::Double);
		CHECK(list[1].pace == TourneyPace::Live);
		CHECK(list[1].seriesPool.empty());
		CHECK_EQ(TourneyRoundCount(list[1]), 0);
		// The league's first tournaments say nothing of all this: single elimination, live.
		CHECK(TourneyFormatOf(list[2]) == TourneyFormat::Single);
		CHECK(list[2].pace == TourneyPace::Live);
		CHECK_EQ(list[2].currentRound, 0);
		CHECK_EQ(TourneyRoundCount(list[2]), 3);
	}

	const int64_t now = 1794600000000LL;
	TourneyCupForm f;
	f.name = "Suizo de otono";
	f.startsAt = now + 3600000;
	f.maxEntries = 16;
	f.format = TourneyFormat::Swiss;
	f.bestOf = 3;
	f.pace = TourneyPace::Rounds;
	f.roundDays = 3;
	CHECK_EQ(TourneyCupMapRounds(f), 4);
	TourneyFitMaps(f.maps, TourneyCupMapRounds(f));
	CHECK_EQ(static_cast<int>(f.maps.size()), 4);
	CHECK(TourneyCupHasSeries(f));
	CHECK(TourneyCheckCup(f, now).empty());
	TourneyCupForm bad = f;
	bad.swissRounds = 7;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Format));
	bad.swissRounds = 6;
	CHECK(TourneyCheckCup(bad, now).empty());
	CHECK_EQ(TourneyCupMapRounds(bad), 6);
	bad = f;
	bad.roundDays = 5;
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::Pace));
	bad.pace = TourneyPace::Live;
	CHECK(TourneyCheckCup(bad, now).empty());

	// The series' maps: none, or 3 to 5 different ones.
	const TourneyMap med = {"mediterranean", "random", "medium"}, island = {"island", "random", "small"}, coast = {"coastal", "random", "large"};
	bad = f;
	bad.seriesPool = {med, island};
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::PoolCount));
	bad.seriesPool = {med, island, med};
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::PoolMap));
	bad.seriesPool = {med, island, {"mediterranean", "random", "small"}};
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::PoolMap));
	bad.seriesPool = {med, island, coast, med, island, coast};
	CHECK(HasProblem(TourneyCheckCup(bad, now), TourneyCupProblem::PoolCount));
	bad.seriesPool = {med, island, coast};
	CHECK(TourneyCheckCup(bad, now).empty());
	TourneyMap large;
	large.kind = TourneyMapKind::Upload;
	large.title = "Grande";
	large.file = "Grande.bfhp";
	large.hash = kUploadMap;
	large.bytes = kTourneyScenarioMaxBytes + 1;
	large.players = 2;
	bad.seriesPool = {med, island, large};
	const std::vector<TourneyCupProblem> tooLarge = TourneyCheckCup(bad, now);
	CHECK(HasProblem(tooLarge, TourneyCupProblem::MapTooLarge));
	CHECK(!HasProblem(tooLarge, TourneyCupProblem::PoolMap));
	// Without series the pool is not looked at, nor sent.
	bad.bestOf = 1;
	bad.seriesPool = {med};
	CHECK(!TourneyCupHasSeries(bad));
	CHECK(TourneyCheckCup(bad, now).empty());
	Json back;
	CHECK(Json::Parse(TourneyCupJson(bad).Dump(), back));
	CHECK(back.Get("seriesPool") && back.Get("seriesPool")->type == Json::Array && back.Get("seriesPool")->arr.empty());
	// A Swiss's final is not a series of its own; an elimination's is.
	TourneyCupForm elim = f;
	elim.format = TourneyFormat::Double;
	elim.bestOf = 1;
	elim.finalBestOf = 3;
	CHECK(TourneyCupHasSeries(elim));
	elim.format = TourneyFormat::Swiss;
	CHECK(!TourneyCupHasSeries(elim));

	f.seriesPool = {med, island, coast};
	CHECK(Json::Parse(TourneyCupJson(f).Dump(), back));
	CHECK_EQ(back.GetString("format"), std::string("swiss"));
	CHECK(back.Get("swissRounds") && back.Get("swissRounds")->type == Json::Null);
	CHECK_EQ(static_cast<int>(back.GetNumber("bestOf")), 3);
	CHECK_EQ(static_cast<int>(back.GetNumber("finalBestOf")), 3);
	CHECK_EQ(back.GetString("pace"), std::string("rounds"));
	CHECK_EQ(static_cast<int>(back.GetNumber("roundDays")), 3);
	const Json *pool = back.Get("seriesPool");
	CHECK(pool && pool->arr.size() == 3 && pool->arr[0].GetString("kind") == "random" && pool->arr[2].GetString("name") == "coastal");
	f.swissRounds = 5;
	f.pace = TourneyPace::Live;
	CHECK(Json::Parse(TourneyCupJson(f).Dump(), back));
	CHECK_EQ(static_cast<int>(back.GetNumber("swissRounds")), 5);
	CHECK_EQ(back.GetString("pace"), std::string("live"));
	CHECK(back.Get("roundDays") == nullptr);
	f.format = TourneyFormat::Single;
	CHECK(Json::Parse(TourneyCupJson(f).Dump(), back));
	CHECK_EQ(back.GetString("format"), std::string("single"));
	CHECK(back.Get("swissRounds") == nullptr);

	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(R"({"id":7,"slug":"suizo","name":"Suizo","official":false,"organizer":{"steamId":"76561198000000994","name":"Furio"},
"status":"signup","format":"swiss","swissRounds":5,"pace":"rounds","roundDays":7,"maxEntries":16,"minEntries":4,"bestOf":5,"finalBestOf":5,
"seriesPool":[{"name":"mediterranean","type":"random","size":"medium"},{"name":"island","type":"random","size":"small"},
{"name":"coastal","type":"random","size":"large"}],"maps":[{"name":"island","type":"random","size":"small"}],"entries":[],"matches":[]})"), d));
	const TourneyCupForm edit = TourneyCupFormOf(d);
	CHECK(edit.format == TourneyFormat::Swiss);
	CHECK_EQ(edit.swissRounds, 5);
	CHECK(edit.pace == TourneyPace::Rounds);
	CHECK_EQ(edit.roundDays, 7);
	CHECK_EQ(edit.bestOf, 5);
	CHECK_EQ(static_cast<int>(edit.seriesPool.size()), 3);
	// Five rounds, five maps: the one given, repeated.
	CHECK_EQ(static_cast<int>(edit.maps.size()), 5);
}

void Series()
{
	// The player's semifinal, a best of three: game 1 lost on the round's map, the map of game 2 theirs to choose for 60 s.
	TourneyNext n;
	CHECK(TourneyParseNext(Parse(R"({"tournament":{"id":2,"slug":"copa","name":"Copa"},"match":5,"round":2,"rounds":3,"bestOf":3,"game":2,
"score":[1,0],"status":"ready","deadlineAt":1794950000000,"noshowFrom":1794863600000,"me":{"side":"b","seed":4},
"opponent":{"name":"Furio","steamId":"76561198000000994","seed":1},"map":{"name":"mountain_passes","type":"random","size":"small"},
"games":[{"no":2,"map":null,"winner":null},{"no":1,"map":{"name":"mountain_passes","type":"random","size":"small"},"winner":"a"}],
"pick":{"by":"b","options":[{"name":"mediterranean","type":"random","size":"medium"},{"name":"island","type":"random","size":"small"}],
"until":1794600060000}})"), n));
	CHECK_EQ(static_cast<int>(n.games.size()), 2);
	CHECK(n.games.size() == 2 && n.games[0].no == 1 && n.games[0].winner == "a" && n.games[0].map.name == "mountain_passes");
	CHECK(n.games.size() == 2 && n.games[1].no == 2 && n.games[1].winner.empty() && !n.games[1].map.Known());
	CHECK(n.pick.present);
	CHECK_EQ(n.pick.by, std::string("b"));
	CHECK_EQ(static_cast<int>(n.pick.options.size()), 2);
	CHECK(Same(n.pick.until, 1794600060000LL));
	CHECK(n.MyPick());
	// While it is being chosen the game has no map; the room waits for it.
	CHECK(!TourneyGameMap(n).Known());
	CHECK(n.pace == TourneyPace::Rounds);
	CHECK(Same(n.noshowFrom, 1794863600000LL));
	int a = 0, b = 0;
	TourneySeriesScore(n.games, a, b);
	CHECK(a == 1 && b == 0);
	CHECK(!TourneySeriesOver(n.games, 3));
	CHECK(TourneyGameOf(n.games, 1) != nullptr && TourneyGameOf(n.games, 3) == nullptr);

	TourneyMine mine;
	mine.next = n;
	TourneyTold told;
	std::vector<TourneyNotice> notices = TourneyNewNotices(mine, told);
	bool picked = false;
	for (const TourneyNotice &x : notices) picked = picked || (x.kind == TourneyNotice::PickOpen && x.game == 2 && Same(x.deadlineAt, 1794600060000LL));
	CHECK(picked);
	for (const TourneyNotice &x : TourneyNewNotices(mine, told)) CHECK(x.kind != TourneyNotice::PickOpen);
	// The rival's choice is not told.
	TourneyTold other;
	mine.next.pick.by = "a";
	CHECK(!mine.next.MyPick());
	for (const TourneyNotice &x : TourneyNewNotices(mine, other)) CHECK(x.kind != TourneyNotice::PickOpen);

	// Once chosen, game 2 is played on its map.
	n.pick = TourneyPick();
	n.games[1].map = {"island", "random", "small"};
	CHECK_EQ(TourneyGameMap(n).name, std::string("island"));
	n.games[1].map = TourneyMap();
	CHECK_EQ(TourneyGameMap(n).name, std::string("mountain_passes"));
	n.games[1].winner = "a";
	CHECK(TourneySeriesOver(n.games, 3));
	CHECK(!TourneySeriesOver(n.games, 5));

	// A pick without options or without who chooses is no pick.
	Json j = Parse(R"({"by":"b","options":[],"until":1})");
	CHECK(!TourneyPickFromJson(&j).present);
	j = Parse(R"({"by":"c","options":[{"name":"island","type":"random","size":"small"}],"until":1})");
	CHECK(!TourneyPickFromJson(&j).present);
	CHECK(!TourneyPickFromJson(nullptr).present);
	j = Parse(R"([{"no":0,"winner":"a"},{"no":3,"winner":"B"},"x"])");
	const std::vector<TourneyGame> games = TourneyGamesFromJson(&j);
	CHECK(games.size() == 1 && games[0].no == 3 && games[0].winner == "b");

	// A live match: no noshowFrom, no pick, the round's map.
	TourneyNext live;
	CHECK(TourneyParseNext(Parse(R"({"match":6,"round":1,"rounds":2,"bestOf":1,"status":"ready","me":{"side":"a"},
"map":{"name":"island","type":"random","size":"small"},"pick":null,"games":[]})"), live));
	CHECK(live.pace == TourneyPace::Live);
	CHECK(!live.pick.present);
	CHECK_EQ(TourneyGameMap(live).name, std::string("island"));
	TourneyNext named;
	CHECK(TourneyParseNext(Parse(R"({"tournament":{"id":2,"pace":"rounds"},"match":6,"status":"ready"})"), named));
	CHECK(named.pace == TourneyPace::Rounds);

	// The detail's matches carry the same.
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(R"({"id":2,"slug":"copa","status":"running","matches":[{"id":5,"round":1,"position":0,"bestOf":3,"status":"ready",
"noshowFrom":1794863600000,"games":[{"no":1,"map":{"name":"island","type":"random","size":"small"},"winner":"b"}],
"pick":{"by":"a","options":[{"name":"coastal","type":"random","size":"large"}],"until":1794600060000}}]})"), d));
	CHECK_EQ(static_cast<int>(d.matches.size()), 1);
	CHECK(d.matches.size() == 1 && d.matches[0].games.size() == 1 && d.matches[0].games[0].winner == "b");
	CHECK(d.matches.size() == 1 && d.matches[0].pick.present && d.matches[0].pick.by == "a");
	CHECK(d.matches.size() == 1 && Same(d.matches[0].noshowFrom, 1794863600000LL));

	// The answers to a pick out of turn or too late.
	for (const char *code : {"not_your_pick", "pick_closed"}) CHECK_EQ(TourneyParseRefusal(Parse(std::string("{\"error\":\"") + code + "\"}")).code, std::string(code));

	// The mod asks every 5 s while a pick is open and for two minutes after a game of a series.
	TourneyNext poll = n;
	poll.pick.present = true;
	CHECK_EQ(TourneySeriesPollMs(poll, -1), 5000);
	poll.pick.present = false;
	CHECK_EQ(TourneySeriesPollMs(poll, 30000), 5000);
	CHECK_EQ(TourneySeriesPollMs(poll, 130000), 0);
	CHECK_EQ(TourneySeriesPollMs(poll, -1), 0);
	poll.bestOf = 1;
	CHECK_EQ(TourneySeriesPollMs(poll, 30000), 0);
	CHECK_EQ(TourneySeriesPollMs(TourneyNext(), 1000), 0);
}

void Estimate()
{
	// The mockup's cup: eight players, rounds of one game and a final of three.
	TourneyEstimate e = TourneyEstimateFor(8, 1, 3);
	CHECK_EQ(e.rounds, 3);
	CHECK_EQ(e.series, 7);
	CHECK_EQ(e.maxGames, 9);
	CHECK_EQ(e.minutes, 40 * 5 + 10 * 2);
	e = TourneyEstimateFor(4, 1, 1);
	CHECK(e.rounds == 2 && e.series == 3 && e.maxGames == 3 && e.minutes == 90);
	e = TourneyEstimateFor(16, 3, 5);
	CHECK(e.rounds == 4 && e.series == 15 && e.maxGames == 47 && e.minutes == 40 * 14 + 30);
	e = TourneyEstimateFor(1, 1, 1);
	CHECK(e.rounds == 0 && e.minutes == 0);
	CHECK(TourneyEstimateFor(TourneyFormat::Single, 8, 1, 3, 0).minutes == TourneyEstimateFor(8, 1, 3).minutes);

	// A Swiss of eight: three rounds of four series.
	e = TourneyEstimateFor(TourneyFormat::Swiss, 8, 1, 3, 0);
	CHECK(e.rounds == 3 && e.series == 12 && e.maxGames == 12 && e.minutes == 40 * 3 + 10 * 2);
	e = TourneyEstimateFor(TourneyFormat::Swiss, 16, 3, 5, 6);
	CHECK(e.rounds == 6 && e.series == 48 && e.maxGames == 144 && e.minutes == 40 * 18 + 10 * 5);
	// A double elimination of eight: the first round, four of the losers' bracket, the grand final and its reset.
	e = TourneyEstimateFor(TourneyFormat::Double, 8, 1, 1, 0);
	CHECK(e.rounds == 7 && e.series == 15 && e.maxGames == 15 && e.minutes == 40 * 7 + 10 * 6);
	e = TourneyEstimateFor(TourneyFormat::Double, 4, 1, 3, 0);
	CHECK(e.rounds == 5 && e.series == 7 && e.maxGames == 5 + 6 && e.minutes == 40 * (3 + 6) + 10 * 4);
	CHECK_EQ(TourneyEstimateFor(TourneyFormat::Double, 1, 1, 1, 0).minutes, 0);

	TourneyCupForm f;
	f.maxEntries = 8;
	f.bestOf = 1;
	f.finalBestOf = 1;
	f.format = TourneyFormat::Double;
	CHECK(TourneyCupLongLive(f));
	CHECK_EQ(TourneyEstimateOf(f).days, 0);
	f.maxEntries = 4;
	CHECK(!TourneyCupLongLive(f));
	f.maxEntries = 8;
	f.pace = TourneyPace::Rounds;
	f.roundDays = 2;
	CHECK(!TourneyCupLongLive(f));
	CHECK_EQ(TourneyEstimateOf(f).days, 14);
	f.format = TourneyFormat::Swiss;
	f.pace = TourneyPace::Live;
	f.maxEntries = 16;
	f.bestOf = 5;
	CHECK(!TourneyCupLongLive(f));
	f.pace = TourneyPace::Rounds;
	f.roundDays = 7;
	CHECK_EQ(TourneyEstimateOf(f).days, 28);
}

void Calendar()
{
	const int64_t day = 86400000LL, hour = 3600000LL, minute = 60000LL;
	CHECK(Same(TourneyDayOf(0), 0));
	CHECK(Same(TourneyDayOf(day - 1), 0));
	CHECK(Same(TourneyDayOf(-1), -1));
	CHECK_EQ(TourneyWeekdayOf(0), 3);
	CHECK_EQ(TourneyWeekdayOf(-1), 2);
	// Saturday 14 November 2026.
	const int64_t today = 20771;
	CHECK_EQ(TourneyWeekdayOf(today), 5);
	int y = 0, m = 0, d = 0;
	TourneyCivilDate(today, y, m, d);
	CHECK(y == 2026 && m == 11 && d == 14);
	TourneyCivilDate(19782, y, m, d);
	CHECK(y == 2024 && m == 2 && d == 29);
	TourneyCivilDate(-1, y, m, d);
	CHECK(y == 1969 && m == 12 && d == 31);

	// From Monday 9 November to Saturday 28: three weeks.
	TourneyCalendar c = TourneyCalendarOf(today, today + 14);
	CHECK(Same(c.first, today - 5));
	CHECK_EQ(c.rows, 3);
	c = TourneyCalendarOf(today - 5, today - 5 + 13);
	CHECK(Same(c.first, today - 5));
	CHECK_EQ(c.rows, 2);

	// Now is 18:07: the earliest start is 18:27, the first quarter after it 18:30, and the last one two weeks on.
	const int64_t now = today * day + 18 * hour + 7 * minute, lo = now + 20 * minute, hi = now + 14 * day;
	CHECK(Same(TourneySnapStart(now, lo, hi), today * day + 18 * hour + 30 * minute));
	CHECK(Same(TourneySnapStart(today * day + 19 * hour + 1 * minute, lo, hi), today * day + 19 * hour + 15 * minute));
	CHECK(Same(TourneySnapStart(hi + day, lo, hi), (today + 14) * day + 18 * hour));
	CHECK(TourneyDayOpen(today, lo, hi));
	CHECK(TourneyDayOpen(today + 14, lo, hi));
	CHECK(!TourneyDayOpen(today - 1, lo, hi));
	CHECK(!TourneyDayOpen(today + 15, lo, hi));
	// Late at night today has no start left.
	const int64_t late = today * day + 23 * hour + 50 * minute;
	CHECK(!TourneyDayOpen(today, late + 20 * minute, late + 14 * day));
	// The same hour on another day, and back within the window on the last one.
	const int64_t at = today * day + 21 * hour;
	CHECK(Same(TourneyOnDay(at, today + 3, lo, hi), (today + 3) * day + 21 * hour));
	CHECK(Same(TourneyOnDay(at, today + 14, lo, hi), (today + 14) * day + 18 * hour));
	CHECK(Same(TourneyOnDay(today * day + 10 * hour, today, lo, hi), today * day + 18 * hour + 30 * minute));
	CHECK(Same(TourneyAtTime(at, 20, 45, lo, hi), today * day + 20 * hour + 45 * minute));
	CHECK(Same(TourneyAtTime(at, 9, 0, lo, hi), today * day + 18 * hour + 30 * minute));
	CHECK(Same(TourneyAtTime(at, 30, 99, lo, hi), (today + 1) * day));
}

void BaseScenarios()
{
	CHECK(BaseScenarioSha256("torneo_1vs1_egypt.bfhp") != nullptr);
	CHECK(BaseScenarioIs("Torneo_1vs1_Egypt.BFHP", "7c4198a92ac1524d2aa26b48c25bb63914fe4a9cf3fdc691574ed5687a89ca35"));
	CHECK(!BaseScenarioIs("Torneo_1vs1_Egypt.BFHP", kUploadMap));
	CHECK(BaseScenarioSha256("Hispania of Numida.BFHP") == nullptr);
	CHECK(BaseScenarioSha256("") == nullptr);
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
	Community();
	CupForm();
	Maps();
	Settings();
	Requirements();
	CupFull();
	Formats();
	Series();
	Estimate();
	Calendar();
	BaseScenarios();
	Clock();
	Bracket();
	return test::Summary("tournament");
}
