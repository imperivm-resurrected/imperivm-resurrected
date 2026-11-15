// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

std::string Text(const char *t)
{
	return t ? std::string(t) : std::string("(none)");
}

void PauseLimits()
{
	LeagueRoomSettings s;
	LeaguePauseBudget b = LeaguePauseBudgetOf(s);
	CHECK_EQ(b.count, 3);
	CHECK_EQ(static_cast<int>(b.totalMs), 180000);
	s.pauseCount = 2;
	s.pauseSeconds = 90;
	b = LeaguePauseBudgetOf(s);
	CHECK_EQ(b.count, 2);
	CHECK_EQ(static_cast<int>(b.totalMs), 90000);
	s.pauseCount = 0;
	b = LeaguePauseBudgetOf(s);
	CHECK(!LeaguePauseCanStart(b));

	const Json full = Parse(R"({"pauses":{"count":4,"totalSec":240}})"), count = Parse(R"({"pauses":5})");
	LeagueRoomSettings fromJson = LeagueParseRoomSettings(&full);
	CHECK_EQ(fromJson.pauseCount, 4);
	CHECK_EQ(fromJson.pauseSeconds, 240);
	fromJson = LeagueParseRoomSettings(&count);
	CHECK_EQ(fromJson.pauseCount, 5);
	CHECK_EQ(fromJson.pauseSeconds, -1);
}

void PauseCount()
{
	LeaguePauseBudget b;
	for (int i = 0; i < 3; i++) {
		CHECK(LeaguePauseCanStart(b));
		LeaguePauseStarted(b, 1000 * i);
		CHECK(!LeaguePauseCanStart(b));
		LeaguePauseEnded(b, 1000 * i + 500);
	}
	CHECK_EQ(b.used, 3);
	CHECK_EQ(static_cast<int>(b.usedMs), 1500);
	CHECK(!LeaguePauseCanStart(b));
	CHECK_EQ(static_cast<int>(LeaguePauseLeftMs(b, 99999)), 178500);
	// Ending a pause that is not on, or starting one twice, changes nothing.
	LeaguePauseEnded(b, 50000);
	CHECK_EQ(static_cast<int>(b.usedMs), 1500);
	LeaguePauseBudget c;
	LeaguePauseStarted(c, 0);
	LeaguePauseStarted(c, 100);
	CHECK_EQ(c.used, 1);
	CHECK_EQ(static_cast<int>(c.since), 0);
}

void PauseTime()
{
	LeaguePauseBudget b;
	LeaguePauseStarted(b, 10000);
	CHECK_EQ(static_cast<int>(LeaguePauseLeftMs(b, 70000)), 120000);
	CHECK(!LeaguePauseRunOut(b, 70000));
	LeaguePauseEnded(b, 130000);
	CHECK_EQ(static_cast<int>(LeaguePauseLeftMs(b, 130000)), 60000);
	CHECK(LeaguePauseCanStart(b));
	LeaguePauseStarted(b, 200000);
	CHECK(!LeaguePauseRunOut(b, 259999));
	CHECK(LeaguePauseRunOut(b, 260000));
	CHECK_EQ(static_cast<int>(LeaguePauseLeftMs(b, 400000)), 0);
	LeaguePauseEnded(b, 261000);
	CHECK(!LeaguePauseRunOut(b, 261000));
	// Out of time, with a pause still in the count: no more.
	CHECK_EQ(b.used, 2);
	CHECK(!LeaguePauseCanStart(b));
	// A clock that went back never gives time.
	LeaguePauseBudget d;
	LeaguePauseStarted(d, 5000);
	CHECK_EQ(static_cast<int>(LeaguePauseLeftMs(d, 1000)), 180000);
	LeaguePauseEnded(d, 1000);
	CHECK_EQ(static_cast<int>(d.usedMs), 0);
}

void Evenness()
{
	CHECK_EQ(Text(LeagueEvennessText(0.5)), std::string("Even match"));
	CHECK_EQ(Text(LeagueEvennessText(0.4)), std::string("Even match"));
	CHECK_EQ(Text(LeagueEvennessText(0.6)), std::string("Even match"));
	CHECK_EQ(Text(LeagueEvennessText(0.61)), std::string("You are the favourite"));
	CHECK_EQ(Text(LeagueEvennessText(1.0)), std::string("You are the favourite"));
	CHECK_EQ(Text(LeagueEvennessText(0.39)), std::string("Your opponent is the favourite"));
	CHECK_EQ(Text(LeagueEvennessText(0.0)), std::string("Your opponent is the favourite"));
	CHECK_EQ(Text(LeagueEvennessText(-1)), std::string("(none)"));
	CHECK_EQ(Text(LeagueEvennessText(1.2)), std::string("(none)"));
}

void Window()
{
	LeagueSearchWindow w;
	CHECK(!LeagueWindowText(w).text);
	w.present = true;
	w.from = 1450;
	w.to = 1650;
	LeagueText t = LeagueWindowText(w);
	CHECK_EQ(Text(t.text), std::string("Opponents between %s and %s Glory"));
	CHECK_EQ(t.args, std::vector<std::string>({"1450", "1650"}));
	w.from = 1650;
	w.to = 1450;
	CHECK_EQ(LeagueWindowText(w).args, std::vector<std::string>({"1450", "1650"}));
	w.from = -1;
	w.to = 150;
	t = LeagueWindowText(w);
	CHECK_EQ(Text(t.text), std::string("Opponents up to %s Glory"));
	CHECK_EQ(t.args, std::vector<std::string>({"150"}));
	w.from = 900;
	w.to = -1;
	t = LeagueWindowText(w);
	CHECK_EQ(Text(t.text), std::string("Opponents from %s Glory"));
	w.from = w.to = -1;
	CHECK_EQ(Text(LeagueWindowText(w).text), std::string("Opponents of any Glory"));
}

void Presence()
{
	CHECK(LeaguePresenceOf(false, false) == LeaguePresence::None);
	CHECK(LeaguePresenceOf(true, false) == LeaguePresence::Searching);
	CHECK(LeaguePresenceOf(true, true) == LeaguePresence::Playing);
	CHECK(LeaguePresenceOf(false, true) == LeaguePresence::Playing);
	CHECK_EQ(Text(LeaguePresenceLabel(LeaguePresence::Searching)), std::string("Searching ranked 1 vs 1"));
	CHECK_EQ(Text(LeaguePresenceLabel(LeaguePresence::Playing)), std::string("Ranked 1 vs 1"));
	CHECK(!LeaguePresenceLabel(LeaguePresence::None));
	CHECK_EQ(LeaguePresenceText("Clasificatoria 1 contra 1", "Centurión II"), std::string("Clasificatoria 1 contra 1 \xC2\xB7 Centurión II"));
	CHECK_EQ(LeaguePresenceText("Ranked 1 vs 1", ""), std::string("Ranked 1 vs 1"));
}

void QueueAnswers()
{
	LeagueQueueAnswer q;
	CHECK(LeagueParseQueue(Parse(R"({"status":"searching","since":5,"searching":3,"estimateSec":40,"window":{"from":1450,"to":1650}})"), q));
	CHECK(q.window.present);
	CHECK_EQ(q.window.from, 1450);
	CHECK_EQ(q.window.to, 1650);
	CHECK(LeagueParseQueue(Parse(R"({"status":"searching","window":{"from":0,"to":null}})"), q));
	CHECK(q.window.present);
	CHECK_EQ(q.window.from, -1);
	CHECK_EQ(q.window.to, -1);
	CHECK(LeagueParseQueue(Parse(R"({"status":"searching"})"), q));
	CHECK(!q.window.present);

	CHECK(LeagueParseQueue(Parse(R"({"status":"found","match":{"id":7,"opponent":{"steamId":"765","name":"Ambiorix","tier":"legionary","division":1,
		"wins":41,"losses":33,"glory":988,"place":203},"map":{"name":"coastal","type":"random","size":"medium"},"acceptBy":9,"expected":0.42}})"), q));
	CHECK(q.opponent.standing);
	CHECK_EQ(q.opponent.wins, 41);
	CHECK_EQ(q.opponent.losses, 33);
	CHECK_EQ(q.opponent.glory, 988);
	CHECK_EQ(q.opponent.place, 203);
	CHECK(q.expected > 0.419 && q.expected < 0.421);
	CHECK(LeagueParseQueue(Parse(R"({"status":"found","match":{"id":7,"opponent":{"steamId":"765","name":"A"}}})"), q));
	CHECK(!q.opponent.standing);
	CHECK(q.expected < 0);
}

void Ticket()
{
	LeagueMatchTicket t;
	CHECK(LeagueParseMatch(Parse(R"({"id":12,"status":"accepting","players":[{"steamId":"1","name":"Me","accepted":true},
		{"steamId":"2","name":"Rival","wins":3,"losses":1,"glory":1310,"place":0}],"opponent":{"steamId":"2","name":"Rival","wins":3,"losses":1,
		"glory":1310,"place":0},"expected":0.7})"), t));
	CHECK(t.opponent.standing);
	CHECK_EQ(t.opponent.steamId, std::string("2"));
	CHECK_EQ(t.opponent.glory, 1310);
	CHECK_EQ(t.players[1].wins, 3);
	CHECK(!t.players[0].standing);
	CHECK(t.expected > 0.69 && t.expected < 0.71);
	CHECK(LeagueParseMatch(Parse(R"({"id":12,"status":"ready","expected":"high"})"), t));
	CHECK(t.expected < 0);
}

void ErrorsAndMe()
{
	const LeagueError e = LeagueParseError(Parse(R"({"error":"wrong_content","message":"x","contentKey":"abcd1234abcd1234","contentName":"Caesar Mod 1.1.1"})"));
	CHECK_EQ(e.code, std::string("wrong_content"));
	CHECK_EQ(e.contentKey, std::string("abcd1234abcd1234"));
	CHECK_EQ(e.contentName, std::string("Caesar Mod 1.1.1"));

	LeagueMe me;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","ranked":{"tier":"centurion","division":2,"glory":1164,"wins":38,"losses":27,"streak":4}})"), me));
	CHECK_EQ(me.ranked.streak, 4);
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","ranked":{"tier":"centurion","division":2,"glory":1164},"streak":-2})"), me));
	CHECK_EQ(me.ranked.streak, -2);
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1"})"), me));
	CHECK_EQ(me.ranked.streak, 0);
}

}

int main()
{
	PauseLimits();
	PauseCount();
	PauseTime();
	Evenness();
	Window();
	Presence();
	QueueAnswers();
	Ticket();
	ErrorsAndMe();
	return test::Summary("leagueplay");
}
