// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/tournament.h"

// The formats of block B (PLAN-LIGA.md): a double elimination and a Swiss tournament as the league's detail sends them, and the geometry
// of the double bracket on screen.

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

bool Same(int64_t a, int64_t b) { return a == b; }

std::string Card(int entry, const char *name, int seed)
{
	return R"({"entryId":)" + std::to_string(entry) + R"(,"steamId":"7656119800000000)" + std::to_string(entry) + R"(","name":")" + name + R"(","seed":)" +
		std::to_string(seed) + "}";
}

std::string Match(int id, const char *bracket, int round, int position, const std::string &a, const std::string &b, int winner, const char *status,
	int bestOf = 1, const char *score = "[1,0]")
{
	return R"({"id":)" + std::to_string(id) + R"(,"bracket":")" + bracket + R"(","round":)" + std::to_string(round) + R"(,"position":)" +
		std::to_string(position) + R"(,"bestOf":)" + std::to_string(bestOf) + R"(,"a":)" + a + R"(,"b":)" + b + R"(,"score":)" + score + R"(,"winner":)" +
		(winner ? std::to_string(winner) : std::string("null")) + R"(,"status":")" + status + R"(","finishedAt":)" + std::to_string(1794670000000LL + id * 60000) +
		"}";
}

// Eight players: Tulia loses the winners' final to Varo, comes back through the losers' bracket and wins the grand final and its reset.
std::string DoubleDetail(bool withReset, const char *status)
{
	const std::string varo = Card(1, "Varo", 1), tulia = Card(2, "Tulia", 2), brenno = Card(3, "Brenno", 3), furio = Card(4, "Furio", 4),
					  escipion = Card(5, "Escipion", 5), calpurnia = Card(6, "Calpurnia", 6), indibil = Card(7, "Indibil", 7), cneo = Card(8, "Cneo", 8);
	std::string matches = Match(1, "winners", 1, 0, varo, cneo, 1, "done") + "," + Match(2, "winners", 1, 1, furio, escipion, 5, "done", 1, "[0,1]") + "," +
		Match(3, "winners", 1, 2, brenno, calpurnia, 3, "done") + "," + Match(4, "winners", 1, 3, tulia, indibil, 2, "done") + "," +
		Match(5, "winners", 2, 0, varo, escipion, 1, "done") + "," + Match(6, "winners", 2, 1, brenno, tulia, 2, "done", 1, "[0,1]") + "," +
		Match(7, "winners", 3, 0, varo, tulia, 1, "done") + "," + Match(8, "losers", 1, 0, cneo, furio, 4, "done", 1, "[0,1]") + "," +
		Match(9, "losers", 1, 1, calpurnia, indibil, 7, "done", 1, "[0,1]") + "," + Match(10, "losers", 2, 0, furio, brenno, 3, "done", 1, "[0,1]") + "," +
		Match(11, "losers", 2, 1, indibil, escipion, 5, "done", 1, "[0,1]") + "," + Match(12, "losers", 3, 0, brenno, escipion, 5, "done", 1, "[0,1]") + "," +
		Match(13, "losers", 4, 0, escipion, tulia, 2, "done", 1, "[0,1]") + "," + Match(14, "final", 1, 0, varo, tulia, 2, "done", 3, "[1,2]");
	if (withReset) matches += "," + Match(15, "reset", 1, 0, varo, tulia, 2, "done", 3, "[0,2]");
	const bool over = std::string(status) == "finished";
	const auto entry = [over](int id, const char *name, int seed, int placement) {
		return R"({"entryId":)" + std::to_string(id) + R"(,"steamId":"7656119800000000)" + std::to_string(id) + R"(","name":")" + name +
			R"(","status":")" + (placement == 1 || !over ? "playing" : "out") + R"(","seed":)" + std::to_string(seed) + R"(,"checkedIn":true,"placement":)" +
			(over ? std::to_string(placement) : std::string("null")) + "}";
	};
	return R"({"id":7,"slug":"copa-doble","name":"Copa doble","official":false,"format":"double","pace":"live","status":")" + std::string(status) +
		R"(","maxEntries":8,"minEntries":4,"bestOf":1,"finalBestOf":3,"rounds":3,"entries":[)" + entry(1, "Varo", 1, 2) + "," + entry(2, "Tulia", 2, 1) + "," +
		entry(3, "Brenno", 3, 4) + "," + entry(4, "Furio", 4, 5) + "," + entry(5, "Escipion", 5, 3) + "," + entry(6, "Calpurnia", 6, 7) + "," +
		entry(7, "Indibil", 7, 5) + "," + entry(8, "Cneo", 8, 7) + R"(],"matches":[)" + matches + "]}";
}

// Five players over three rounds paced two days each: Cneo and then Brenno rest with a bye, and the second round is being played.
const char *kSwiss = R"({"id":9,"slug":"copa-suiza","name":"Copa suiza","official":true,"format":"swiss","swissRounds":null,"pace":"Rounds",
"roundDays":2,"currentRound":2,"roundEndsAt":1795000000000,"status":"running","maxEntries":8,"minEntries":4,"bestOf":1,"finalBestOf":1,"entries":[
{"entryId":1,"steamId":"76561198000000001","name":"Varo","status":"playing","seed":1,"checkedIn":true,"placement":null},
{"entryId":2,"steamId":"76561198000000002","name":"Tulia","status":"playing","seed":2,"checkedIn":true,"placement":null},
{"entryId":3,"steamId":"76561198000000003","name":"Brenno","status":"playing","seed":3,"checkedIn":true,"placement":null},
{"entryId":4,"steamId":"76561198000000004","name":"Furio","status":"playing","seed":4,"checkedIn":true,"placement":null},
{"entryId":5,"steamId":"76561198000000005","name":"Cneo","status":"playing","seed":5,"checkedIn":true,"placement":null}],
"matches":[
{"id":1,"bracket":"swiss","round":1,"position":1,"a":{"entryId":2,"name":"Tulia","seed":2},"b":{"entryId":4,"name":"Furio","seed":4},"score":[0,1],
"winner":4,"status":"done","finishedAt":1794700000000},
{"id":2,"bracket":"swiss","round":1,"position":0,"a":{"entryId":1,"name":"Varo","seed":1},"b":{"entryId":3,"name":"Brenno","seed":3},"score":[1,0],
"winner":1,"status":"done","finishedAt":1794710000000},
{"id":3,"round":1,"position":2,"a":{"entryId":5,"name":"Cneo","seed":5},"b":null,"score":[0,0],"winner":5,"status":"bye","finishedAt":1794800000000},
{"id":4,"bracket":"swiss","round":2,"position":0,"a":{"entryId":1,"name":"Varo","seed":1},"b":{"entryId":4,"name":"Furio","seed":4},"score":[0,0],
"winner":null,"status":"live","startedAt":1794900000000},
{"id":5,"bracket":"swiss","round":2,"position":1,"a":{"entryId":5,"name":"Cneo","seed":5},"b":{"entryId":2,"name":"Tulia","seed":2},"score":[0,1],
"winner":2,"status":"done","finishedAt":1794950000000},
{"id":6,"bracket":"swiss","round":2,"position":2,"a":{"entryId":3,"name":"Brenno","seed":3},"b":null,"score":[0,0],"winner":3,"status":"bye"}],
"standings":[
{"entryId":2,"steamId":"76561198000000002","name":"Tulia","points":1,"wins":1,"losses":1,"buchholz":3,"byes":0,"place":4},
{"entryId":1,"steamId":"76561198000000001","name":"Varo","points":1,"wins":1,"losses":0,"buchholz":1,"byes":0,"place":2},
{"entryId":5,"name":"","points":1,"wins":1,"losses":1,"buchholz":2.0,"byes":1,"place":3},
{"entryId":4,"steamId":"76561198000000004","name":"Furio","points":1,"wins":1,"losses":0,"buchholz":2,"byes":0,"place":1},
{"entryId":3,"steamId":"76561198000000003","name":"Brenno","points":1,"wins":0,"losses":1,"buchholz":2,"byes":1,"place":5}]})";

void Brackets()
{
	CHECK(TourneyBracketFromId("losers") == TourneyBracket::Losers);
	CHECK(TourneyBracketFromId("Final") == TourneyBracket::Final);
	CHECK(TourneyBracketFromId("reset") == TourneyBracket::Reset);
	CHECK(TourneyBracketFromId("swiss") == TourneyBracket::Swiss);
	CHECK(TourneyBracketFromId("winners") == TourneyBracket::Winners);
	CHECK(TourneyBracketFromId("") == TourneyBracket::Winners);
	TourneySummary t;
	CHECK(!TourneyIsDouble(t) && !TourneyIsSwiss(t));
	t.format = "Double";
	CHECK(TourneyIsDouble(t) && !TourneyIsSwiss(t));
	t.format = "swiss";
	CHECK(TourneyIsSwiss(t));
}

void Double()
{
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(DoubleDetail(true, "finished")), d));
	CHECK(TourneyIsDouble(d));
	CHECK(!TourneyByRounds(d));
	CHECK_EQ(static_cast<int>(d.matches.size()), 15);
	// The rounds are the winners' bracket's, and the losers' bracket counts its own.
	CHECK_EQ(d.rounds, 3);
	CHECK_EQ(d.losersRounds, 4);
	const TourneyMatch *w1 = TourneyMatchAt(d, 1, 0);
	CHECK(w1 && w1->id == 1 && w1->bracket == TourneyBracket::Winners);
	CHECK(w1 && w1->stage == TourneyStage::Quarterfinal);
	const TourneyMatch *l1 = TourneyMatchAt(d, 1, 0, TourneyBracket::Losers);
	CHECK(l1 && l1->id == 8 && l1->stage == TourneyStage::Unknown);
	const TourneyMatch *wFinal = TourneyMatchAt(d, 3, 0);
	CHECK(wFinal && wFinal->stage == TourneyStage::Final);
	CHECK(TourneyMatchAt(d, 4, 0) == nullptr);
	CHECK(TourneyMatchAt(d, 4, 0, TourneyBracket::Losers) != nullptr);
	const TourneyMatch *grand = TourneyMatchAt(d, 1, 0, TourneyBracket::Final);
	CHECK(grand && grand->id == 14 && grand->bestOf == 3);
	// The reset decides the champion once it is there.
	const TourneyMatch *decider = TourneyDecider(d);
	CHECK(decider && decider->id == 15);
	CHECK(Same(TourneyChampion(d), 2));
	CHECK(!TourneyResetDue(d));
	CHECK(Same(TourneyFinishedAt(d), 1794670000000LL + 15 * 60000));
	for (TourneyEntrant &e : d.entrants) e.placement = 0;
	CHECK(Same(TourneyChampion(d), 2));
	int done = 0, total = 0;
	TourneyProgress(d, done, total);
	CHECK_EQ(total, 15);
	CHECK_EQ(done, 15);
	CHECK_EQ(static_cast<int>(TourneyRoundMatches(d, 2, TourneyBracket::Losers).size()), 2);
	CHECK_EQ(static_cast<int>(TourneyRoundMatches(d, 1, TourneyBracket::Winners).size()), 4);

	// The champion's path through both brackets: the winners' rounds, then the losers' final, the grand final and the reset.
	const std::vector<const TourneyMatch *> path = TourneyPathOf(d, 2);
	CHECK_EQ(static_cast<int>(path.size()), 6);
	if (path.size() == 6) {
		CHECK(Same(path[0]->id, 4));
		CHECK(Same(path[2]->id, 7));
		CHECK(Same(path[3]->id, 13));
		CHECK(Same(path[4]->id, 14));
		CHECK(Same(path[5]->id, 15));
	}
	const std::vector<const TourneyMatch *> third = TourneyPathOf(d, 5);
	CHECK_EQ(static_cast<int>(third.size()), 5);
	if (third.size() == 5) {
		CHECK(third[1]->bracket == TourneyBracket::Winners && third[1]->round == 2);
		CHECK(third[2]->bracket == TourneyBracket::Losers && third[2]->round == 2);
		CHECK(third[4]->bracket == TourneyBracket::Losers && third[4]->round == 4);
	}

	// The placements of the podium, and the ranges the places below share.
	TourneyDetail placed;
	CHECK(TourneyParseDetail(Parse(DoubleDetail(true, "finished")), placed));
	CHECK_EQ(TourneyPlaced(placed, 3)[0]->name, std::string("Escipion"));
	CHECK_EQ(static_cast<int>(TourneyPlaced(placed, 5).size()), 2);
	CHECK_EQ(TourneyDoublePlaceEnd(1), 1);
	CHECK_EQ(TourneyDoublePlaceEnd(4), 4);
	CHECK_EQ(TourneyDoublePlaceEnd(5), 6);
	CHECK_EQ(TourneyDoublePlaceEnd(6), 6);
	CHECK_EQ(TourneyDoublePlaceEnd(7), 8);
	CHECK_EQ(TourneyDoublePlaceEnd(9), 12);
	CHECK_EQ(TourneyDoublePlaceEnd(13), 16);
	CHECK_EQ(TourneyDoublePlaceEnd(17), 24);
	CHECK_EQ(TourneyDoublePlaceEnd(25), 32);
}

void DoubleReset()
{
	// The losers' bracket player won the grand final: until the reset is played there is no champion.
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(DoubleDetail(false, "running")), d));
	CHECK(TourneyResetDue(d));
	CHECK(Same(TourneyChampion(d), 0));
	const TourneyMatch *decider = TourneyDecider(d);
	CHECK(decider && decider->id == 14);
	// Had the winners' bracket player won it, they would be the champion with no reset.
	for (TourneyMatch &m : d.matches) {
		if (m.bracket == TourneyBracket::Final) m.winner = 1;
	}
	CHECK(!TourneyResetDue(d));
	CHECK(Same(TourneyChampion(d), 1));
	// Once the tournament is over, the grand final's winner is the champion whatever came before.
	TourneyDetail over;
	CHECK(TourneyParseDetail(Parse(DoubleDetail(false, "finished")), over));
	for (TourneyEntrant &e : over.entrants) e.placement = 0;
	CHECK(!TourneyResetDue(over));
	CHECK(Same(TourneyChampion(over), 2));
}

void Swiss()
{
	TourneyDetail d;
	CHECK(TourneyParseDetail(Parse(kSwiss), d));
	CHECK(TourneyIsSwiss(d));
	CHECK(TourneyByRounds(d));
	CHECK_EQ(d.pace, std::string("rounds"));
	CHECK_EQ(d.roundDays, 2);
	CHECK_EQ(d.currentRound, 2);
	CHECK(Same(d.roundEndsAt, 1795000000000LL));
	CHECK_EQ(d.swissRounds, 0);
	// Five players and no rounds chosen: three, one for each doubling.
	CHECK_EQ(d.rounds, 3);
	CHECK_EQ(TourneySwissTotalRounds(d), 3);
	// Every match is a Swiss round's, also the one that does not say it.
	for (const TourneyMatch &m : d.matches) CHECK(m.bracket == TourneyBracket::Swiss && m.stage == TourneyStage::Unknown);
	CHECK(TourneyMatchAt(d, 1, 2) == nullptr);
	CHECK(TourneyMatchAt(d, 1, 2, TourneyBracket::Swiss) != nullptr);
	const std::vector<const TourneyMatch *> first = TourneyRoundMatches(d, 1, TourneyBracket::Swiss);
	CHECK_EQ(static_cast<int>(first.size()), 3);
	if (first.size() == 3) CHECK(first[0]->id == 2 && first[1]->id == 1 && first[2]->status == TourneyMatchStatus::Bye);
	CHECK_EQ(TourneyCurrentRound(d), 2);
	d.currentRound = 0;
	CHECK_EQ(TourneyCurrentRound(d), 2);

	// The standings, by place, with the names the entries give.
	CHECK_EQ(static_cast<int>(d.standings.size()), 5);
	if (d.standings.size() == 5) {
		CHECK_EQ(d.standings[0].name, std::string("Furio"));
		CHECK_EQ(d.standings[0].place, 1);
		CHECK_EQ(d.standings[2].name, std::string("Cneo"));
		CHECK(d.standings[2].steamId == 76561198000000005ULL);
		CHECK_EQ(d.standings[2].buchholz, 2);
		CHECK_EQ(d.standings[2].byes, 1);
		CHECK_EQ(d.standings[4].name, std::string("Brenno"));
	}
	const TourneyStanding *tulia = TourneyStandingOf(d, 2);
	CHECK(tulia && tulia->wins == 1 && tulia->losses == 1 && tulia->buchholz == 3);
	CHECK(TourneyStandingOf(d, 99) == nullptr);

	// A bye is worth a win; the round being played does not count yet.
	CHECK_EQ(TourneySwissPointsBefore(d, 5, 2), 1);
	CHECK_EQ(TourneySwissPointsBefore(d, 3, 2), 0);
	CHECK_EQ(TourneySwissPointsBefore(d, 3, 3), 1);
	CHECK_EQ(TourneySwissPointsBefore(d, 2, 3), 1);
	CHECK_EQ(TourneySwissPointsBefore(d, 1, 3), 1);
	CHECK_EQ(TourneySwissPointsBefore(d, 1, 1), 0);

	// Four series played or being played, and the third round's two still to pair.
	int done = 0, total = 0;
	TourneyProgress(d, done, total);
	CHECK_EQ(done, 3);
	CHECK_EQ(total, 6);

	// The champion and the podium come from the standings once it is over.
	CHECK(Same(TourneyChampion(d), 0));
	CHECK(TourneyDecider(d) == nullptr);
	d.status = TourneyStatus::Finished;
	CHECK(Same(TourneyChampion(d), 4));
	CHECK_EQ(TourneyPlaced(d, 2)[0]->name, std::string("Varo"));
	CHECK_EQ(static_cast<int>(TourneyPlaced(d, 6).size()), 0);
	// The rounds it was made with, or the ones the server says.
	TourneyDetail chosen;
	std::string text = kSwiss;
	text.replace(text.find("\"swissRounds\":null"), 18, "\"swissRounds\":4");
	CHECK(TourneyParseDetail(Parse(text), chosen));
	CHECK_EQ(chosen.rounds, 4);
	text.replace(text.find("\"swissRounds\":4"), 15, "\"swissRounds\":4,\"rounds\":5");
	CHECK(TourneyParseDetail(Parse(text), chosen));
	CHECK_EQ(chosen.rounds, 5);
	CHECK_EQ(TourneySwissRoundsFor(4), 2);
	CHECK_EQ(TourneySwissRoundsFor(5), 3);
	CHECK_EQ(TourneySwissRoundsFor(8), 3);
	CHECK_EQ(TourneySwissRoundsFor(9), 4);
	CHECK_EQ(TourneySwissRoundsFor(16), 4);
	CHECK_EQ(TourneySwissRoundsFor(17), 5);
	CHECK_EQ(TourneySwissRoundsFor(32), 5);
}

void LosersRounds()
{
	CHECK_EQ(TourneyLosersRounds(1), 0);
	CHECK_EQ(TourneyLosersRounds(2), 2);
	CHECK_EQ(TourneyLosersRounds(3), 4);
	CHECK_EQ(TourneyLosersRounds(4), 6);
	CHECK_EQ(TourneyLosersRounds(5), 8);
	const int eight[] = {2, 2, 1, 1};
	for (int r = 1; r <= 4; r++) CHECK_EQ(TourneyLosersMatches(3, r), eight[r - 1]);
	const int sixteen[] = {4, 4, 2, 2, 1, 1};
	for (int r = 1; r <= 6; r++) CHECK_EQ(TourneyLosersMatches(4, r), sixteen[r - 1]);
	CHECK_EQ(TourneyLosersMatches(5, 1), 8);
	CHECK_EQ(TourneyLosersMatches(5, 8), 1);
	CHECK_EQ(TourneyLosersMatches(2, 1), 1);
	CHECK_EQ(TourneyLosersMatches(2, 2), 1);
	CHECK_EQ(TourneyLosersMatches(3, 0), 0);
	CHECK_EQ(TourneyLosersMatches(3, 5), 0);
	// Every player of the losers' bracket but its champion goes out once: the places less two (the grand final's).
	for (int w = 2; w <= 5; w++) {
		int matches = 0;
		for (int r = 1; r <= TourneyLosersRounds(w); r++) matches += TourneyLosersMatches(w, r);
		CHECK_EQ(matches, (1 << w) - 2);
	}
}

void DoubleLayout()
{
	// Eight places with the mockup's boxes: the winners' bracket under a title of 30, 40 between the brackets and a reset.
	const TourneyBracketMetrics m;
	const TourneyDoubleLayout l = TourneyDoubleLayoutOf(m, 3, 0, true, 30, 40);
	CHECK_EQ(l.winnersRounds, 3);
	CHECK_EQ(l.losersRounds, 4);
	CHECK_EQ(l.winnersY, 30);
	CHECK_EQ(l.losersY, 30 + TourneyBracketHeight(m, 3) + 40 + 30);
	CHECK_EQ(l.losersY, 420);
	CHECK_EQ(TourneyWinnersBoxY(m, l, 1, 0), 64);
	CHECK_EQ(TourneyLosersBoxY(m, l, 1, 0), 454);
	CHECK_EQ(TourneyLosersBoxY(m, l, 1, 1), 530);
	// The grand final goes right of the losers' four rounds, halfway between the two finals.
	CHECK_EQ(l.finalX, 1200);
	CHECK(l.finalX >= TourneyBoxX(m, 4) + m.boxW);
	const int winnersMid = TourneyWinnersBoxY(m, l, 3, 0) + m.boxH / 2, losersMid = TourneyLosersBoxY(m, l, 4, 0) + m.boxH / 2;
	CHECK_EQ(winnersMid, 207);
	CHECK_EQ(losersMid, 521);
	CHECK_EQ(l.finalY, 335);
	CHECK_EQ(l.resetX, 1500);
	CHECK_EQ(l.width, 1736);
	CHECK_EQ(l.champX, 1776);
	CHECK_EQ(l.height, 420 + TourneyBracketHeight(m, 2));
	// The losers' bracket never reaches into the winners'.
	CHECK(TourneyLosersBoxY(m, l, 1, 0) - m.top > TourneyWinnersBoxY(m, l, 1, 3) + m.boxH);
	const TourneyDoubleLayout plain = TourneyDoubleLayoutOf(m, 3, 0, false, 30, 40);
	CHECK_EQ(plain.resetX, 0);
	CHECK_EQ(plain.width, 1436);
	CHECK_EQ(plain.champX, 1476);
	// Four places: two losers' rounds in the winners' two columns.
	const TourneyDoubleLayout four = TourneyDoubleLayoutOf(m, 2, 0, false, 30, 40);
	CHECK_EQ(four.losersRounds, 2);
	CHECK_EQ(four.finalX, 600);
	CHECK_EQ(TourneyLosersBoxY(m, four, 2, 0), TourneyLosersBoxY(m, four, 1, 0));

	// Into an even round the line runs straight on; into an odd one it meets its neighbour's halfway.
	const std::vector<TourneySegment> straight = TourneyLosersConnector(m, l, 1, 1);
	CHECK_EQ(static_cast<int>(straight.size()), 1);
	if (straight.size() == 1) {
		CHECK_EQ(straight[0].x1, 236);
		CHECK_EQ(straight[0].x2, 300);
		CHECK_EQ(straight[0].y1, 559);
		CHECK_EQ(straight[0].y2, TourneyLosersBoxY(m, l, 2, 1) + m.boxH / 2);
	}
	const std::vector<TourneySegment> joined = TourneyLosersConnector(m, l, 2, 1);
	CHECK_EQ(static_cast<int>(joined.size()), 3);
	if (joined.size() == 3) {
		CHECK_EQ(joined[0].x1, 536);
		CHECK_EQ(joined[0].x2, 568);
		CHECK_EQ(joined[1].y1, 559);
		CHECK_EQ(joined[1].y2, 521);
		CHECK_EQ(joined[2].x2, 600);
		CHECK_EQ(joined[2].y1, TourneyLosersBoxY(m, l, 3, 0) + m.boxH / 2);
	}
	CHECK_EQ(static_cast<int>(TourneyLosersConnector(m, l, 3, 0).size()), 1);
	CHECK(TourneyLosersConnector(m, l, 4, 0).empty());
	CHECK(TourneyLosersConnector(m, l, 0, 0).empty());
	// Sixteen places: each box of an odd round halfway between its two feeders, and level with its one feeder in an even round.
	const TourneyDoubleLayout big = TourneyDoubleLayoutOf(m, 4, 0, false, 30, 40);
	for (int r = 2; r <= 6; r++) {
		for (int p = 0; p < TourneyLosersMatches(4, r); p++) {
			const int mid = TourneyLosersBoxY(m, big, r, p) * 2;
			if (r % 2) CHECK_EQ(mid, TourneyLosersBoxY(m, big, r - 1, 2 * p) + TourneyLosersBoxY(m, big, r - 1, 2 * p + 1));
			else CHECK_EQ(mid, TourneyLosersBoxY(m, big, r - 1, p) * 2);
		}
	}

	// The two finals join right of the losers' bracket and go into the grand final, which goes on into the reset.
	const std::vector<TourneySegment> grand = TourneyGrandFinalConnector(m, l);
	CHECK_EQ(static_cast<int>(grand.size()), 5);
	if (grand.size() == 5) {
		CHECK_EQ(grand[0].x1, 836);
		CHECK_EQ(grand[0].y1, 207);
		CHECK_EQ(grand[0].x2, 1168);
		CHECK_EQ(grand[1].x1, 1136);
		CHECK_EQ(grand[1].y1, 521);
		CHECK_EQ(grand[2].x1, 1168);
		CHECK_EQ(grand[2].y1, 207);
		CHECK_EQ(grand[2].y2, 521);
		CHECK_EQ(grand[3].y1, l.finalY + m.boxH / 2);
		CHECK_EQ(grand[3].x2, l.finalX);
		CHECK_EQ(grand[4].x1, l.finalX + m.boxW);
		CHECK_EQ(grand[4].x2, l.resetX);
	}
	CHECK_EQ(static_cast<int>(TourneyGrandFinalConnector(m, plain).size()), 4);
}

void SwissLayout()
{
	// The strip: the standings and four rounds share 1086 up to 190 each; six rounds in 1086 make them narrower.
	CHECK_EQ(TourneyStripCellWidth(1086, 5, 2, 190), 190);
	CHECK_EQ(TourneyStripCellWidth(1086, 7, 2, 190), 153);
	CHECK_EQ(TourneyStripCellWidth(100, 0, 2, 190), 100);
	CHECK_EQ(TourneyStripCellWidth(10, 7, 2, 190), 0);
	// The standings in the tab of a 720p screen (1066 wide) over three rounds: the figures from the right, the rounds before them.
	const TourneyStandingsColumns c = TourneyStandingsColumnsFor(1066, 3);
	CHECK_EQ(c.byes, 970);
	CHECK_EQ(c.buchholz, 858);
	CHECK_EQ(c.buchholzW, 112);
	CHECK_EQ(c.record, 762);
	CHECK_EQ(c.points, 666);
	CHECK_EQ(c.roundsW, 90);
	CHECK_EQ(c.rounds, 576);
	CHECK(c.showRounds);
	CHECK_EQ(c.nameW, 576 - 64 - 16);
	CHECK_EQ(TourneyRoundSquareX(c, 1), 588);
	CHECK_EQ(TourneyRoundSquareX(c, 3), 632);
	CHECK(TourneyRoundSquareX(c, 3) + 16 <= c.points);
	// Too narrow for the rounds: the name keeps their room.
	const TourneyStandingsColumns narrow = TourneyStandingsColumnsFor(800, 6);
	CHECK(!narrow.showRounds);
	CHECK_EQ(narrow.nameW, narrow.points - narrow.name - 16);
	// A pairing's row: the score halfway between the table and the state, the two sides either side of it.
	const TourneyPairingRow r = TourneyPairingRowFor(1066);
	CHECK_EQ(r.stateW, 213);
	CHECK_EQ(r.stateX, 1066 - 213 - 16);
	CHECK_EQ(r.left, 60);
	CHECK_EQ(r.right, r.stateX - 24);
	const int lean = (r.scoreX - r.left) - (r.right - (r.scoreX + r.scoreW));
	CHECK(lean >= -1 && lean <= 1);
	CHECK_EQ(TourneyPairingRowFor(1600).stateW, 240);
}

}

int main()
{
	Brackets();
	Double();
	DoubleReset();
	Swiss();
	LosersRounds();
	DoubleLayout();
	SwissLayout();
	return test::Summary("tourneyformats");
}
