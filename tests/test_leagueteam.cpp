// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

int TierNumber(LeagueTier t) { return static_cast<int>(t); }

void Queues()
{
	CHECK_EQ(LeagueQueueId("2v2"), std::string("2v2"));
	CHECK_EQ(LeagueQueueId("2V2"), std::string("2v2"));
	CHECK_EQ(LeagueQueueId("1v1"), std::string("1v1"));
	CHECK_EQ(LeagueQueueId(""), std::string("1v1"));
	CHECK(LeagueQueueIsTeam("2v2"));
	CHECK(!LeagueQueueIsTeam("1v1"));

	LeagueMe me;
	me.ranked.glory = 400;
	me.ranked2v2.glory = 120;
	CHECK_EQ(LeagueMeRanked(me, "2v2").glory, 120);
	CHECK_EQ(LeagueMeRanked(me, "1v1").glory, 400);

	// The season says which queues it plays and the 2v2's maps; a server before the 2v2 plays only the duel.
	LeagueSeason s;
	CHECK(LeagueParseSeason(Parse(R"({"number":2,"endsAt":5,"queues":["1v1","2v2"],"maps":[{"name":"continent","type":"random","size":"small"}],
		"maps2v2":[{"name":"continent","type":"random","size":"large"},{"name":"islands","type":"random","size":"huge"},null]})"),
		s));
	CHECK_EQ(s.queues, std::vector<std::string>({"1v1", "2v2"}));
	CHECK(LeagueSeasonHasQueue(s, "2v2"));
	CHECK_EQ(static_cast<unsigned>(s.maps2v2.size()), 2u);
	CHECK_EQ(s.maps2v2[1].name, std::string("islands"));
	CHECK_EQ(s.maps2v2[0].size, std::string("large"));
	CHECK(LeagueParseSeason(Parse(R"({"number":1,"endsAt":5})"), s));
	CHECK_EQ(s.queues, std::vector<std::string>({"1v1"}));
	CHECK(!LeagueSeasonHasQueue(s, "2v2"));
	CHECK(s.maps2v2.empty());
}

void Found()
{
	// Every answer of the queue says which one; the duo's partner sees the leader's search as theirs.
	LeagueQueueAnswer q;
	CHECK(LeagueParseQueue(Parse(R"({"status":"searching","queue":"2v2","since":10,"searching":3,"estimateSec":60})"), q));
	CHECK_EQ(q.queue, std::string("2v2"));
	CHECK(q.status == LeagueQueueStatus::Searching);

	CHECK(LeagueParseQueue(Parse(R"({"status":"found","queue":"2v2","serverNow":100,"match":{"id":77,"opponent":null,
		"map":{"name":"continent","type":"random","size":"large"},"acceptBy":20100,"accepted":false,"expected":0.462,"mySide":2,
		"teams":[{"side":1,"players":[{"steamId":"1","name":"Varo","tier":"centurion","division":2,"glory":930,"wins":10,"losses":4,"place":3,"duo":true},
			{"steamId":"2","name":"Tulia","tier":"tribune","division":3,"glory":1210,"wins":20,"losses":9,"place":1,"duo":true}]},
		{"side":2,"players":[{"steamId":"3","name":"Livia","tier":"signifer","division":1,"glory":880,"wins":5,"losses":5,"place":8,"duo":false},
			{"steamId":"4","name":"Brennus","tier":null,"division":null,"glory":0,"wins":0,"losses":0,"place":null,"duo":false}]}]}})"),
		q));
	CHECK(q.status == LeagueQueueStatus::Found);
	CHECK_EQ(static_cast<unsigned long long>(q.matchId), 77ULL);
	CHECK(q.opponent.steamId.empty());
	CHECK_EQ(q.mySide, 2);
	CHECK(q.expected > 0.46 && q.expected < 0.47);
	CHECK_EQ(static_cast<unsigned>(q.teams.size()), 2u);
	CHECK_EQ(q.teams[0].side, 1);
	CHECK_EQ(static_cast<unsigned>(q.teams[0].players.size()), 2u);
	CHECK(q.teams[0].players[0].duo);
	CHECK_EQ(q.teams[0].players[1].team, 1);
	CHECK_EQ(q.teams[0].players[1].glory, 1210);
	CHECK(q.teams[0].players[1].standing);
	CHECK(!q.teams[1].players[0].duo);
	CHECK_EQ(TierNumber(q.teams[1].players[1].tier), TierNumber(LeagueTier::None));
	CHECK_EQ(q.teams[1].players[1].place, 0);
	CHECK_EQ(LeagueSideOf(q.teams, "3"), 2);
	CHECK_EQ(LeagueSideOf(q.teams, "9"), 0);
	const LeaguePlayerCard *partner = LeagueTeamPartner(q.teams, "3");
	CHECK(partner && partner->name == "Brennus");
	const std::vector<LeaguePlayerCard> others = LeagueTeamOpponents(q.teams, "3");
	CHECK_EQ(static_cast<unsigned>(others.size()), 2u);
	CHECK_EQ(others[0].name, std::string("Varo"));
	CHECK(!LeagueTeamPartner(q.teams, "9"));
	// A side other than 1 or 2 is not a side.
	CHECK(LeagueParseQueue(Parse(R"({"status":"found","match":{"id":5,"teams":[{"side":3,"players":[]}],"mySide":7}})"), q));
	CHECK(q.teams.empty());
	CHECK_EQ(q.mySide, 0);
}

void Ticket()
{
	LeagueMatchTicket t;
	CHECK(LeagueParseMatch(Parse(R"({"id":77,"status":"accepting","queue":"2v2","host":"3","acceptBy":20100,"expected":0.462,"mySide":2,
		"players":[{"steamId":"1","name":"Varo","accepted":true,"team":1},{"steamId":"2","name":"Tulia","accepted":false,"team":1},
		{"steamId":"3","name":"Livia","accepted":true,"team":2,"inRoom":true},{"steamId":"4","name":"Brennus","accepted":true,"team":2}]})"),
		t));
	CHECK_EQ(t.queue, std::string("2v2"));
	CHECK_EQ(t.mySide, 2);
	CHECK(t.teams.empty());
	CHECK_EQ(LeagueAcceptedCount(t), 3);
	// Without `teams`, its players grouped by their team.
	const std::vector<LeagueMatchTeam> teams = LeagueTicketTeams(t);
	CHECK_EQ(static_cast<unsigned>(teams.size()), 2u);
	CHECK_EQ(teams[1].side, 2);
	CHECK_EQ(teams[1].players[0].name, std::string("Livia"));
	CHECK(teams[1].players[0].inRoom);
	const LeaguePlayerCard *card = LeagueTicketCard(t, "2");
	CHECK(card && !card->accepted);
	CHECK(!LeagueTicketCard(t, "9"));
	CHECK(!LeagueTicketCard(t, ""));
	// With every player in the room, whoever leaves it carries the lock, as in a duel.
	CHECK(LeagueParseMatch(Parse(R"({"id":77,"status":"playing","queue":"2v2","host":"3","lobby":"5","startedAt":null,"players":[{"steamId":"1","inRoom":true},
		{"steamId":"2","inRoom":true},{"steamId":"3","inRoom":true},{"steamId":"4","inRoom":true}]})"),
		t));
	CHECK(LeagueAbandonCosts(t, "1", 0));
	t.players[2].inRoom = false;
	CHECK(!LeagueAbandonCosts(t, "1", 0));
	// The ticket's own teams come first.
	CHECK(LeagueParseMatch(Parse(R"({"id":77,"status":"ready","teams":[{"side":1,"players":[{"steamId":"1"}]},{"side":2,"players":[{"steamId":"3"}]}],
		"players":[{"steamId":"1","team":2},{"steamId":"3","team":1}]})"),
		t));
	CHECK_EQ(LeagueSideOf(LeagueTicketTeams(t), "1"), 1);
}

void StatsHistoryErrors()
{
	LeagueStats s;
	CHECK(LeagueParseStats(Parse(R"({"searching":5,"playing":2,"gamesToday":40,"byQueue":{"1v1":{"searching":3,"playing":1,"gamesToday":30},
		"2v2":{"searching":2,"playing":1,"gamesToday":10}}})"),
		s));
	CHECK_EQ(LeagueStatsOf(s, "2v2").searching, 2);
	CHECK_EQ(LeagueStatsOf(s, "1v1").gamesToday, 30);
	CHECK_EQ(LeagueStatsOf(s, "3v3").searching, 0);
	// A server without them: the totals are the duel's, and the 2v2 has nothing.
	CHECK(LeagueParseStats(Parse(R"({"searching":4,"playing":1,"gamesToday":9})"), s));
	CHECK_EQ(LeagueStatsOf(s, "1v1").searching, 4);
	CHECK_EQ(LeagueStatsOf(s, "2v2").searching, 0);

	// A 2v2 game of the history names the partner and both opponents.
	LeagueHistoryGame g;
	CHECK(LeagueParseHistoryGame(Parse(R"({"id":"12","playedAt":1000,"durationMs":1500000,"result":"win","gloryDelta":18,
		"partner":{"steamId":"2","name":"Tulia"},"opponents":[{"steamId":"3","name":"Livia","tier":"signifer","division":1,"civ":"egypt"},
		{"steamId":"4","name":"Brennus","tier":null,"division":null,"civ":null}]})"),
		g));
	CHECK_EQ(g.partner.name, std::string("Tulia"));
	CHECK_EQ(static_cast<unsigned>(g.opponents.size()), 2u);
	// Each opponent's civilization, empty where no report named it.
	CHECK_EQ(static_cast<unsigned>(g.opponentCivs.size()), 2u);
	CHECK_EQ(g.opponentCivs[0], std::string("egypt"));
	CHECK(g.opponentCivs[1].empty());
	CHECK_EQ(TierNumber(g.opponents[0].tier), TierNumber(LeagueTier::Signifer));
	CHECK_EQ(g.opponent.name, std::string("Livia"));
	// A duel's has neither.
	CHECK(LeagueParseHistoryGame(Parse(R"({"id":"13","opponent":{"steamId":"5","name":"Ambiorix"}})"), g));
	CHECK(g.partner.steamId.empty());
	CHECK(g.opponents.empty());
	CHECK_EQ(g.opponent.name, std::string("Ambiorix"));

	// The two of a duo too far apart.
	const LeagueError e = LeagueParseError(Parse(R"({"error":"rank_gap","message":"Too far apart","gap":720,"max":600})"));
	CHECK_EQ(e.code, std::string("rank_gap"));
	CHECK_EQ(e.gap, 720);
	CHECK_EQ(e.max, 600);
	CHECK(e.who.empty());
	// The partner's lock is not this player's.
	const LeagueError partner = LeagueParseError(Parse(R"({"error":"banned","who":"partner","steamId":"2","until":5000,"kind":"declined"})"));
	CHECK_EQ(partner.who, std::string("partner"));
	CHECK_EQ(static_cast<int>(partner.until), 5000);
	CHECK_EQ(partner.steamId, std::string("2"));
	const LeagueError duo = LeagueParseError(Parse(R"({"error":"already_in_party","steamId":"76561198000000002"})"));
	CHECK_EQ(duo.steamId, std::string("76561198000000002"));
}

void PollPace()
{
	CHECK_EQ(LeaguePartyPollMs(true, true, true, true, false), 600000);
	CHECK_EQ(LeaguePartyPollMs(false, true, false, false, false), 5000);
	CHECK_EQ(LeaguePartyPollMs(false, false, true, true, false), 5000);
	CHECK_EQ(LeaguePartyPollMs(false, false, false, true, false), 60000);
	CHECK_EQ(LeaguePartyPollMs(false, false, false, false, false), 120000);
	CHECK_EQ(LeaguePartyPollMs(false, true, true, false, true), 60000);
	CHECK_EQ(LeaguePartyPollMs(false, false, true, true, true), 300000);
	CHECK_EQ(LeaguePartyPollMs(false, false, false, false, true), 600000);
	// Under the 20 s a found match has to be accepted, wherever the partner is.
	CHECK_EQ(LeagueDuoQueuePollMs(true, true), 5000);
	CHECK_EQ(LeagueDuoQueuePollMs(false, true), 10000);
	CHECK_EQ(LeagueDuoQueuePollMs(true, false), 15000);
}

void Party()
{
	LeaguePartyState p;
	CHECK(LeagueParseParty(Parse(R"({"party":{"id":"p9","leader":"1","members":[{"steamId":"1","name":"Varo","accepted":true},
		{"steamId":"2","name":"Tulia","accepted":false}],"expiresAt":900000},"invites":[{"id":"i1","from":{"steamId":"5","name":"Livia"},"expiresAt":300000},
		{"id":"i2","from":{"steamId":"6","name":"Brennus"},"expiresAt":300500},{"from":{"steamId":"7"}}]})"),
		p));
	CHECK(p.party.present);
	CHECK_EQ(p.party.id, std::string("p9"));
	CHECK_EQ(p.party.leader, std::string("1"));
	CHECK_EQ(static_cast<unsigned>(p.party.members.size()), 2u);
	CHECK_EQ(static_cast<unsigned long long>(p.party.expiresAt), 900000ULL);
	CHECK(!LeaguePartyReady(p.party));
	const LeaguePartyMember *partner = LeaguePartyPartner(p.party, "1");
	CHECK(partner && partner->name == "Tulia");
	CHECK(LeagueDuoRoleOf(p.party, "1") == LeagueDuoRole::Waiting);
	CHECK(LeagueDuoRoleOf(p.party, "2") == LeagueDuoRole::Waiting);
	CHECK_EQ(static_cast<unsigned>(p.invites.size()), 2u);
	CHECK_EQ(p.invites[0].fromName, std::string("Livia"));
	CHECK_EQ(p.invites[1].fromId, std::string("6"));

	// Once both accepted, the leader searches and the other one follows.
	p.party.members[1].accepted = true;
	CHECK(LeaguePartyReady(p.party));
	CHECK(LeagueDuoRoleOf(p.party, "1") == LeagueDuoRole::Leader);
	CHECK(LeagueDuoRoleOf(p.party, "2") == LeagueDuoRole::Member);

	// No duo, or one with nobody else in it, is playing alone.
	CHECK(LeagueParseParty(Parse(R"({"party":null,"invites":[]})"), p));
	CHECK(!p.party.present);
	CHECK(LeagueDuoRoleOf(p.party, "1") == LeagueDuoRole::Solo);
	CHECK(p.invites.empty());
	const Json alone = Parse(R"({"id":"p1","leader":"1","members":[{"steamId":"1","accepted":true}]})");
	CHECK(LeagueDuoRoleOf(LeaguePartyOf(&alone), "1") == LeagueDuoRole::Solo);
	// The invitation's answer is {party}.
	CHECK(LeagueParseParty(Parse(R"({"party":{"id":12,"leader":"1","members":[]}})"), p));
	CHECK_EQ(p.party.id, std::string("12"));
	CHECK(!LeagueParseParty(Parse("[]"), p));

	// Only the invitations not seen yet call the player.
	std::vector<LeaguePartyInvite> invites(3);
	invites[0].id = "a";
	invites[1].id = "b";
	invites[2].id = "c";
	const std::vector<LeaguePartyInvite> fresh = LeagueNewInvites(invites, {"a", "c"});
	CHECK_EQ(static_cast<unsigned>(fresh.size()), 1u);
	CHECK_EQ(fresh[0].id, std::string("b"));
}

void Paths()
{
	// The duel's requests stay the ones a server before the 2v2 knows.
	CHECK_EQ(LeagueBoardPath("1v1"), std::string("/v1/league/leaderboard?queue=1v1"));
	CHECK_EQ(LeagueBoardPath(""), std::string("/v1/league/leaderboard?queue=1v1"));
	CHECK_EQ(LeagueHistoryPath(0, 5, "1v1"), std::string("/v1/league/matches?mine=1&limit=5"));
	CHECK_EQ(LeagueHistoryPath(1700000000000LL, 10, "1v1"), std::string("/v1/league/matches?mine=1&limit=10&before=1700000000000"));
	CHECK_EQ(LeaguePlayerPath("76561198000000001", -1, "1v1"), std::string("/v1/league/players/76561198000000001"));
	CHECK_EQ(LeaguePlayerPath("76561198000000001", 2, "1v1"), std::string("/v1/league/players/76561198000000001?season=2"));
	CHECK_EQ(LeagueHallPath(1, "es", "1v1"), std::string("/v1/league/seasons/1/hall?lang=es"));
	CHECK_EQ(LeagueCeremonySeenPath(1, "1v1"), std::string("/v1/league/me/ceremony/1/seen"));
	// The 2v2's say so.
	CHECK_EQ(LeagueBoardPath("2v2"), std::string("/v1/league/leaderboard?queue=2v2"));
	CHECK_EQ(LeagueHistoryPath(0, 5, "2v2"), std::string("/v1/league/matches?mine=1&limit=5&queue=2v2"));
	CHECK_EQ(LeagueHistoryPath(99, 10, "2v2"), std::string("/v1/league/matches?mine=1&limit=10&before=99&queue=2v2"));
	CHECK_EQ(LeaguePlayerPath("5", -1, "2v2"), std::string("/v1/league/players/5?queue=2v2"));
	CHECK_EQ(LeaguePlayerPath("5", 3, "2v2"), std::string("/v1/league/players/5?season=3&queue=2v2"));
	CHECK_EQ(LeagueHallPath(1, "it", "2v2"), std::string("/v1/league/seasons/1/hall?lang=it&queue=2v2"));
	CHECK_EQ(LeagueCeremonySeenPath(4, "2v2"), std::string("/v1/league/me/ceremony/4/seen?queue=2v2"));
	// The duo's invitations are answered by their id.
	CHECK_EQ(LeaguePartyAnswerPath("12", true), std::string("/v1/league/party/12/accept"));
	CHECK_EQ(LeaguePartyAnswerPath("12", false), std::string("/v1/league/party/12/decline"));
}

void DropWait()
{
	CHECK(!LeagueTeamDropOver(60000, 60000));
	CHECK(!LeagueTeamDropOver(60000, 60000 + kLeagueTeamDropWaitMs - 1));
	CHECK(LeagueTeamDropOver(60000, 60000 + kLeagueTeamDropWaitMs));
	CHECK_EQ(static_cast<unsigned>(LeagueTeamDropLeftMs(60000, 120000)), static_cast<unsigned>(kLeagueTeamDropWaitMs - 60000));
	CHECK_EQ(static_cast<unsigned>(LeagueTeamDropLeftMs(60000, 60000 + kLeagueTeamDropWaitMs + 5)), 0u);
	// A clock read before the drop (a reload) leaves the whole wait.
	CHECK_EQ(static_cast<unsigned>(LeagueTeamDropLeftMs(60000, 1000)), static_cast<unsigned>(kLeagueTeamDropWaitMs));
	CHECK(!LeagueTeamDropOver(60000, 1000));
}

void Vote()
{
	// Before minute 5 nobody surrenders; alone, at once; with the partner, it is proposed once.
	CHECK(LeagueSurrenderOnPress(kLeagueSurrenderFromMs - 1, true, false) == LeagueSurrenderStep::TooEarly);
	CHECK(LeagueSurrenderOnPress(kLeagueSurrenderFromMs - 1, false, false) == LeagueSurrenderStep::TooEarly);
	CHECK(LeagueSurrenderOnPress(kLeagueSurrenderFromMs, false, false) == LeagueSurrenderStep::Alone);
	CHECK(LeagueSurrenderOnPress(kLeagueSurrenderFromMs, true, false) == LeagueSurrenderStep::Propose);
	CHECK(LeagueSurrenderOnPress(kLeagueSurrenderFromMs, true, true) == LeagueSurrenderStep::Pending);
	// A cup game of teams asks the partner from the start.
	CHECK(LeagueSurrenderOnPress(0, true, false, false) == LeagueSurrenderStep::Propose);
	CHECK(LeagueSurrenderOnPress(0, false, false, false) == LeagueSurrenderStep::Alone);

	LeagueVote v;
	CHECK_EQ(static_cast<int>(LeagueVoteLeftMs(v, 0)), 0);
	CHECK(!LeagueVoteExpired(v, 999999));
	LeagueVoteOpen(v, 2, 1000);
	CHECK(v.open);
	CHECK_EQ(v.proposer, 2);
	CHECK_EQ(static_cast<int>(LeagueVoteLeftMs(v, 1000)), static_cast<int>(kLeagueVoteMs));
	CHECK_EQ(static_cast<int>(LeagueVoteLeftMs(v, 11000)), 20000);
	CHECK(!LeagueVoteExpired(v, 1000 + kLeagueVoteMs - 1));
	CHECK(LeagueVoteExpired(v, 1000 + kLeagueVoteMs));
	CHECK_EQ(static_cast<int>(LeagueVoteLeftMs(v, 1000 + kLeagueVoteMs + 50)), 0);
	// The answers count by the game's time they ran at, the same on every computer.
	CHECK(LeagueVoteAnswerCounts(v, 1000 + kLeagueVoteMs - 1));
	CHECK(!LeagueVoteAnswerCounts(v, 1000 + kLeagueVoteMs));
	CHECK(!LeagueVoteAnswerCounts(LeagueVote(), 1000));

	// The messages every computer reads on the same tick.
	const std::string propose = LeagueVoteMessage(LeagueVoteVerb::Propose), yes = LeagueVoteMessage(LeagueVoteVerb::Yes), no = LeagueVoteMessage(LeagueVoteVerb::No);
	CHECK_EQ(propose, std::string("#IRV:p"));
	CHECK(LeagueVoteParse(propose.c_str(), static_cast<unsigned>(propose.size())) == LeagueVoteVerb::Propose);
	CHECK(LeagueVoteParse(yes.c_str(), static_cast<unsigned>(yes.size())) == LeagueVoteVerb::Yes);
	CHECK(LeagueVoteParse(no.c_str(), static_cast<unsigned>(no.size())) == LeagueVoteVerb::No);
	CHECK(LeagueVoteMessage(LeagueVoteVerb::None).empty());
	const char *other = "#RJ:3", *longer = "#IRV:pp", *unknown = "#IRV:x";
	CHECK(LeagueVoteParse(other, static_cast<unsigned>(strlen(other))) == LeagueVoteVerb::None);
	CHECK(LeagueVoteParse(longer, static_cast<unsigned>(strlen(longer))) == LeagueVoteVerb::None);
	CHECK(LeagueVoteParse(unknown, static_cast<unsigned>(strlen(unknown))) == LeagueVoteVerb::None);
	CHECK(LeagueVoteParse(nullptr, 6) == LeagueVoteVerb::None);
}

// The command that defeats a player gone for good, on every computer at the same tick.
void Gone()
{
	const std::string three = LeagueGoneMessage(3);
	CHECK_EQ(three, std::string("#IRG:3"));
	CHECK_EQ(LeagueGoneParse(three.c_str(), static_cast<unsigned>(three.size())), 3);
	const std::string first = LeagueGoneMessage(0), last = LeagueGoneMessage(7);
	CHECK_EQ(LeagueGoneParse(first.c_str(), static_cast<unsigned>(first.size())), 0);
	CHECK_EQ(LeagueGoneParse(last.c_str(), static_cast<unsigned>(last.size())), 7);
	CHECK(LeagueGoneMessage(-1).empty());
	CHECK(LeagueGoneMessage(8).empty());
	const char *vote = "#IRV:p", *outside = "#IRG:8", *longer = "#IRG:33", *letter = "#IRG:x";
	CHECK_EQ(LeagueGoneParse(vote, static_cast<unsigned>(strlen(vote))), -1);
	CHECK_EQ(LeagueGoneParse(outside, static_cast<unsigned>(strlen(outside))), -1);
	CHECK_EQ(LeagueGoneParse(longer, static_cast<unsigned>(strlen(longer))), -1);
	CHECK_EQ(LeagueGoneParse(letter, static_cast<unsigned>(strlen(letter))), -1);
	CHECK_EQ(LeagueGoneParse(nullptr, 6), -1);
	CHECK(LeagueVoteParse(three.c_str(), static_cast<unsigned>(three.size())) == LeagueVoteVerb::None);
}

}

int main()
{
	Queues();
	Found();
	Ticket();
	StatsHistoryErrors();
	Party();
	PollPace();
	Paths();
	DropWait();
	Vote();
	Gone();
	return test::Summary("leagueteam");
}
