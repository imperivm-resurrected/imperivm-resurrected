// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

int TierNumber(LeagueTier t) { return static_cast<int>(t); }

// Hundredths, so the bar's fractions compare as whole numbers.
int Pct(float v) { return static_cast<int>(v * 100 + 0.5f); }

void Season()
{
	LeagueSeason s;
	CHECK(LeagueParseSeason(Parse(R"({"number":0,"name":"Trial of Arms","names":{"es":"Prueba de armas","en":"Trial of Arms","it":"Prova d'armi"},
		"theme":"beta","startsAt":1000,"endsAt":86401000,"beta":true,"active":true,"queues":["1v1"],"contentKey":null,
		"maps":[{"name":"mediterranean","type":"random","size":"small"},{"name":"continent","type":"random","size":"small"}],
		"settings":{"preset":"competitive","hideCivs":true,"specDelay":120,"fog":true,"explored":false,"diplomacy":"locked","cheats":false,
		"pauses":{"count":3,"totalSec":180}},"tiers":[{"id":"recruit","name":"Recruit","from":0}]})"),
		s));
	CHECK_EQ(s.number, 0);
	CHECK(s.beta);
	CHECK(s.active);
	CHECK_EQ(s.theme, std::string("beta"));
	CHECK_EQ(s.contentKey, std::string());
	CHECK_EQ(LeagueSeasonName(s, "es"), std::string("Prueba de armas"));
	CHECK_EQ(LeagueSeasonName(s, "it"), std::string("Prova d'armi"));
	CHECK_EQ(LeagueSeasonName(s, "fr"), std::string("Trial of Arms"));
	CHECK_EQ(static_cast<int>(s.maps.size()), 2);
	CHECK_EQ(s.maps[0].name, std::string("mediterranean"));
	CHECK_EQ(s.maps[1].size, std::string("small"));
	CHECK_EQ(s.settings.preset, std::string("competitive"));
	CHECK_EQ(s.settings.hideCivs, 1);
	CHECK_EQ(s.settings.fog, 1);
	CHECK_EQ(s.settings.explored, 0);
	CHECK_EQ(s.settings.cheats, 0);
	CHECK_EQ(s.settings.specDelay, 120);
	CHECK_EQ(s.settings.diplomacy, std::string("locked"));
	CHECK_EQ(s.settings.pauseCount, 3);
	CHECK_EQ(s.settings.pauseSeconds, 180);
	// What the server leaves out stays unknown.
	const LeagueRoomSettings empty = LeagueParseRoomSettings(nullptr);
	CHECK_EQ(empty.fog, -1);
	CHECK_EQ(empty.specDelay, -1);
	const Json noSpectators = Parse(R"({"spectators":false})");
	CHECK_EQ(LeagueParseRoomSettings(&noSpectators).specDelay, 0);
	CHECK(!LeagueParseSeason(Parse(R"({"error":"no_season","message":"There is no season yet"})"), s));

	CHECK_EQ(LeagueDaysLeft(86401000, 1000), 1);
	CHECK_EQ(LeagueDaysLeft(86401000, 2000), 1);
	CHECK_EQ(LeagueDaysLeft(2 * 86400000LL + 1, 0), 3);
	CHECK_EQ(LeagueDaysLeft(1000, 5000), 0);
	CHECK_EQ(Pct(LeagueSeasonElapsed(s, 1000)), 0);
	CHECK_EQ(Pct(LeagueSeasonElapsed(s, 1000 + 43200000)), 50);
	CHECK_EQ(Pct(LeagueSeasonElapsed(s, 999999999)), 100);

	CHECK_EQ(LeagueLangCode("spanish"), std::string("es"));
	CHECK_EQ(LeagueLangCode("Italian"), std::string("it"));
	CHECK_EQ(LeagueLangCode("english"), std::string("en"));
	CHECK_EQ(LeagueLangCode("german"), std::string("en"));
}

void Queue()
{
	LeagueQueueAnswer q;
	CHECK(LeagueParseQueue(Parse(R"({"status":"searching","queue":"1v1","since":1700000000000,"searching":4,"estimateSec":110})"), q));
	CHECK(q.status == LeagueQueueStatus::Searching);
	CHECK_EQ(static_cast<unsigned long long>(q.since), 1700000000000ULL);
	CHECK_EQ(q.searching, 4);
	CHECK_EQ(q.estimateSec, 110);

	CHECK(LeagueParseQueue(Parse(R"({"status":"found","match":{"id":42,"opponent":{"steamId":"76561198000000002","name":"Ambiorix",
		"tier":"legionary","division":1},"map":{"name":"continent","type":"random","size":"small"},"acceptBy":1700000020000,"accepted":false}})"),
		q));
	CHECK(q.status == LeagueQueueStatus::Found);
	CHECK_EQ(static_cast<unsigned long long>(q.matchId), 42ULL);
	CHECK_EQ(q.opponent.name, std::string("Ambiorix"));
	CHECK_EQ(q.opponent.steamId, std::string("76561198000000002"));
	CHECK_EQ(TierNumber(q.opponent.tier), TierNumber(LeagueTier::Legionary));
	CHECK_EQ(q.opponent.division, 1);
	CHECK_EQ(q.map.name, std::string("continent"));
	CHECK_EQ(static_cast<unsigned long long>(q.acceptBy), 1700000020000ULL);
	CHECK(!q.accepted);

	// After both accepted the queue says idle, with the match being played: a mod that restarted finds it so.
	CHECK(LeagueParseQueue(Parse(R"({"status":"idle","matchId":42})"), q));
	CHECK(q.status == LeagueQueueStatus::Idle);
	CHECK_EQ(static_cast<unsigned long long>(q.matchId), 42ULL);
	CHECK(LeagueParseQueue(Parse(R"({"status":"idle","matchId":null})"), q));
	CHECK_EQ(static_cast<unsigned long long>(q.matchId), 0ULL);
	// An opponent in placement has no tier.
	CHECK(LeagueParseQueue(Parse(R"({"status":"found","match":{"id":"7","opponent":{"steamId":"1","name":null,"tier":null,"division":null},
		"map":null,"acceptBy":5}})"),
		q));
	CHECK_EQ(TierNumber(q.opponent.tier), TierNumber(LeagueTier::None));
	CHECK_EQ(q.opponent.division, 0);
	CHECK_EQ(static_cast<unsigned long long>(q.matchId), 7ULL);
	CHECK(!LeagueParseQueue(Parse(R"({"status":"found"})"), q));
	CHECK(!LeagueParseQueue(Parse(R"({"status":"weird"})"), q));
}

void Ticket()
{
	LeagueMatchTicket t;
	CHECK(LeagueParseMatch(Parse(R"({"id":42,"status":"ready","queue":"1v1","host":"76561198000000001","lobby":null,"lobbyKey":"irm=ranked:42",
		"map":{"name":"continent","type":"random","size":"small"},"settings":{"preset":"competitive","hideCivs":true,"specDelay":120},
		"contentKey":null,"acceptBy":1700000020000,
		"players":[{"steamId":"76561198000000001","name":"Marcus","tier":"centurion","division":2,"accepted":true,"host":true},
		{"steamId":"76561198000000002","name":"Ambiorix","tier":null,"division":null,"accepted":true,"host":false}],
		"outcome":null,"ranked":null})"),
		t));
	CHECK_EQ(static_cast<unsigned long long>(t.id), 42ULL);
	CHECK(t.status == LeagueMatchStatus::Ready);
	CHECK_EQ(t.host, std::string("76561198000000001"));
	CHECK_EQ(t.lobby, std::string());
	CHECK_EQ(static_cast<unsigned long long>(LeagueTicketLobby(t)), 0ULL);
	CHECK_EQ(t.lobbyKey, std::string("irm=ranked:42"));
	CHECK_EQ(t.settings.specDelay, 120);
	CHECK_EQ(static_cast<int>(t.players.size()), 2);
	CHECK(t.players[0].host);
	CHECK(t.players[1].accepted);
	CHECK(!t.ranked.present);
	const LeaguePlayerCard *me = LeagueTicketPlayer(t, "76561198000000001", false);
	const LeaguePlayerCard *other = LeagueTicketPlayer(t, "76561198000000001", true);
	CHECK(me && me->name == "Marcus");
	CHECK(other && other->name == "Ambiorix");
	CHECK(!LeagueTicketPlayer(LeagueMatchTicket(), "1", false));

	CHECK(LeagueParseMatch(Parse(R"({"id":42,"status":"playing","host":"1","lobby":"109775241032912345","players":[]})"), t));
	CHECK(t.status == LeagueMatchStatus::Playing);
	CHECK_EQ(static_cast<unsigned long long>(LeagueTicketLobby(t)), 109775241032912345ULL);

	CHECK(LeagueParseMatch(Parse(R"({"id":42,"status":"done","outcome":"a","ranked":{"gloryBefore":288,"gloryAfter":309,"delta":21,
		"tierBefore":"recruit","divisionBefore":1,"tierAfter":"legionary","divisionAfter":3,"promoted":true,"demoted":false,"shield":3,
		"placementLeft":0}})"),
		t));
	CHECK(t.status == LeagueMatchStatus::Done);
	CHECK(t.ranked.present);
	CHECK_EQ(t.ranked.delta, 21);
	CHECK_EQ(TierNumber(t.ranked.tierAfter), TierNumber(LeagueTier::Legionary));
	CHECK_EQ(t.ranked.divisionAfter, 3);
	CHECK(t.ranked.promoted);
	CHECK_EQ(t.ranked.shield, 3);
	CHECK(LeagueParseMatch(Parse(R"({"id":9,"status":"cancelled","outcome":"declined"})"), t));
	CHECK(t.status == LeagueMatchStatus::Cancelled);
	CHECK_EQ(t.outcome, std::string("declined"));
	CHECK(!LeagueParseMatch(Parse(R"({"id":9,"status":"lost"})"), t));
	CHECK(!LeagueParseMatch(Parse(R"({"error":"not_found","message":"Match not found"})"), t));

	// The tag the host's room carries is the one the report reads back.
	CHECK_EQ(LeagueRankedRoomTag(42), std::string("ranked:42"));
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t id = 0;
	CHECK(LeagueParseRoomTag(LeagueRankedRoomTag(42), kind, id));
	CHECK(kind == LeagueMatchKind::Ranked);
	CHECK_EQ(static_cast<unsigned long long>(id), 42ULL);
}

void Board()
{
	LeagueBoard b;
	CHECK(LeagueParseBoard(Parse(R"({"season":0,"total":612,"offset":140,"me":147,"rows":[
		{"position":141,"steamId":"11","name":"Corvus","tier":"signifer","division":1,"glory":812,"wins":40,"losses":30},
		{"position":147,"steamId":"76561198000000001","name":"Marcus","tier":"centurion","division":2,"glory":964,"wins":38,"losses":27},
		{"position":1,"steamId":"12","name":"Scipio","tier":"imperator","division":null,"glory":1811,"wins":112,"losses":41}]})"),
		b));
	CHECK_EQ(b.total, 612);
	CHECK_EQ(b.offset, 140);
	CHECK_EQ(b.me, 147);
	CHECK_EQ(static_cast<int>(b.rows.size()), 3);
	CHECK_EQ(TierNumber(b.rows[2].tier), TierNumber(LeagueTier::Imperator));
	CHECK_EQ(b.rows[2].division, 0);
	const LeagueBoardRow *mine = LeagueBoardFind(b, "76561198000000001");
	CHECK(mine && mine->position == 147 && mine->glory == 964);
	CHECK(!LeagueBoardFind(b, "99"));
	CHECK(!LeagueBoardFind(b, ""));
	CHECK(LeagueParseBoard(Parse(R"({"season":null,"total":0,"offset":0,"rows":[]})"), b));
	CHECK_EQ(b.total, 0);
	CHECK(!LeagueParseBoard(Parse(R"({"total":3})"), b));

	CHECK_EQ(LeagueBoardPageOf(1, 13), 0);
	CHECK_EQ(LeagueBoardPageOf(13, 13), 0);
	CHECK_EQ(LeagueBoardPageOf(14, 13), 13);
	CHECK_EQ(LeagueBoardPageOf(147, 13), 143);
	CHECK_EQ(LeagueBoardPageOf(0, 13), 0);
	CHECK_EQ(LeagueWinRate(38, 27), 58);
	CHECK_EQ(LeagueWinRate(0, 0), 0);
	CHECK_EQ(LeagueWinRate(1, 2), 33);
}

void ReportAndErrors()
{
	LeagueReportAnswer a;
	CHECK(LeagueParseReportAnswer(Parse(R"({"status":"confirmed","outcome":"a","ranked":{"gloryBefore":45,"gloryAfter":64,"delta":19,
		"tierBefore":"recruit","divisionBefore":3,"tierAfter":"recruit","divisionAfter":3,"promoted":false,"demoted":false,"shield":0,"placementLeft":0}})"),
		a));
	CHECK_EQ(a.status, std::string("confirmed"));
	CHECK(a.ranked.present);
	CHECK_EQ(a.ranked.gloryAfter, 64);
	CHECK(LeagueParseReportAnswer(Parse(R"({"status":"awaiting_other"})"), a));
	CHECK_EQ(a.status, std::string("awaiting_other"));
	CHECK(!a.ranked.present);
	CHECK(!LeagueParseReportAnswer(Parse(R"({"ok":true})"), a));

	LeagueError e = LeagueParseError(Parse(R"({"until":1700000300000,"error":"banned","message":"You cannot search for a while"})"));
	CHECK_EQ(e.code, std::string("banned"));
	CHECK_EQ(static_cast<unsigned long long>(e.until), 1700000300000ULL);
	e = LeagueParseError(Parse(R"({"matchId":42,"matchStatus":"ready","error":"in_match","message":"You already have a match"})"));
	CHECK_EQ(static_cast<unsigned long long>(e.matchId), 42ULL);
	e = LeagueParseError(Parse(R"({"contentKey":"c7d1","error":"wrong_content","message":"The season needs other gameplay content"})"));
	CHECK_EQ(e.contentKey, std::string("c7d1"));
	e = LeagueParseError(Parse(R"({"status":"cancelled","queueBanUntil":99})"));
	CHECK_EQ(static_cast<unsigned long long>(e.until), 99ULL);
}

void MeBadges()
{
	LeagueMe me;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","name":"Me","ranked":{"queue":"1v1","season":2,"tier":"centurion","division":2,"glory":964,
		"placementLeft":0,"shield":1,"wins":38,"losses":27,"position":147,"elite":false},"queueBanUntil":null,"badge":{"kind":"rank","id":"1v1"},
		"badges":[{"kind":"rank","id":"1v1","season":2,"tier":"centurion","division":2},{"kind":"season","id":1,"season":1,"tier":"tribune"},
		{"kind":"trophy","id":5,"season":1,"tier":"official","title":"Copa de Invierno"}]})"),
		me));
	CHECK_EQ(me.ranked.season, 2);
	CHECK_EQ(me.badgeKind, std::string("rank"));
	CHECK_EQ(me.badgeId, std::string("1v1"));
	CHECK_EQ(static_cast<int>(me.badges.size()), 3);
	CHECK_EQ(me.badges[1].season, 1);
	CHECK_EQ(me.badges[2].title, std::string("Copa de Invierno"));
	std::vector<LeagueBadgeChoice> choices = LeagueBadgeChoices(me);
	CHECK_EQ(static_cast<int>(choices.size()), 4);
	CHECK_EQ(choices[0].kind, std::string("rank"));
	CHECK(choices[0].chosen);
	CHECK(choices[0].look.kind == LeagueBadgeKind::Rank);
	CHECK_EQ(TierNumber(choices[0].look.tier), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(choices[1].kind, std::string("season"));
	CHECK_EQ(choices[1].id, std::string("1"));
	CHECK_EQ(choices[2].kind, std::string("trophy"));
	CHECK_EQ(choices[2].id, std::string("5"));
	CHECK_EQ(choices[3].kind, std::string("none"));
	CHECK(!choices[3].chosen);

	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","badge":{"kind":"none","id":null},"badges":[]})"), me));
	CHECK_EQ(me.badgeKind, std::string("none"));
	CHECK_EQ(me.badgeId, std::string());
	choices = LeagueBadgeChoices(me);
	CHECK_EQ(static_cast<int>(choices.size()), 2);
	CHECK(!choices[0].chosen);
	CHECK(choices[0].look.kind == LeagueBadgeKind::None);
	CHECK(choices[1].chosen);
	CHECK(LeagueParseMe(Parse(R"({"steamId":"1","badge":{"kind":"season","id":1},"badges":[{"kind":"season","id":1,"season":1,"tier":"tribune"}]})"), me));
	choices = LeagueBadgeChoices(me);
	CHECK(choices[1].chosen);
	CHECK(!choices[0].chosen);
}

void Glory()
{
	LeagueGloryPlace p = LeagueGloryPlaceOf(0);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Recruit));
	CHECK_EQ(p.division, 3);
	CHECK_EQ(p.into, 0);
	CHECK(p.capped);
	p = LeagueGloryPlaceOf(299);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Recruit));
	CHECK_EQ(p.division, 1);
	CHECK_EQ(p.into, 99);
	p = LeagueGloryPlaceOf(964);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(p.division, 3);
	CHECK_EQ(p.into, 64);
	p = LeagueGloryPlaceOf(1699);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Legate));
	CHECK_EQ(p.division, 2);
	CHECK(p.capped);
	// Legate I has no top: the Glory keeps adding up.
	p = LeagueGloryPlaceOf(1850);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Legate));
	CHECK_EQ(p.division, 1);
	CHECK_EQ(p.into, 150);
	CHECK(!p.capped);
	p = LeagueGloryPlaceOf(-20);
	CHECK_EQ(p.into, 0);

	LeagueTier t = LeagueTier::None;
	int d = 0;
	LeagueNextDivision(LeagueTier::Centurion, 2, t, d);
	CHECK_EQ(TierNumber(t), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(d, 1);
	LeagueNextDivision(LeagueTier::Legionary, 1, t, d);
	CHECK_EQ(TierNumber(t), TierNumber(LeagueTier::Signifer));
	CHECK_EQ(d, 3);
	LeagueNextDivision(LeagueTier::Legate, 1, t, d);
	CHECK_EQ(TierNumber(t), TierNumber(LeagueTier::None));
	LeagueNextDivision(LeagueTier::Imperator, 0, t, d);
	CHECK_EQ(TierNumber(t), TierNumber(LeagueTier::None));

	CHECK_EQ(LeagueWinsToNext(964), 2);
	CHECK_EQ(LeagueWinsToNext(999), 1);
	CHECK_EQ(LeagueWinsToNext(900), 5);
	CHECK_EQ(LeagueWinsToNext(1800), 0);
}

void Countdown()
{
	CHECK_EQ(Pct(LeagueCountdownFraction(20000, 0)), 100);
	CHECK_EQ(Pct(LeagueCountdownFraction(20000, 10000)), 50);
	CHECK_EQ(Pct(LeagueCountdownFraction(20000, 25000)), 0);
	CHECK_EQ(Pct(LeagueCountdownFraction(60000, 0)), 100);
	CHECK_EQ(LeagueCountdownSeconds(20000, 0), 20);
	CHECK_EQ(LeagueCountdownSeconds(20000, 7500), 13);
	CHECK_EQ(LeagueCountdownSeconds(20000, 19999), 1);
	CHECK_EQ(LeagueCountdownSeconds(20000, 20000), 0);
	CHECK_EQ(LeagueCountdownSeconds(20000, 30000), 0);
	CHECK_EQ(LeagueClockText(0), std::string("0:00"));
	CHECK_EQ(LeagueClockText(102000), std::string("1:42"));
	CHECK_EQ(LeagueClockText(-5), std::string("0:00"));
	CHECK_EQ(LeagueClockText(3723000), std::string("62:03"));
}

LeagueRankedResult Result(int before, int after, LeagueTier tb, int db, LeagueTier ta, int da, bool promoted, bool demoted, int shield = 0)
{
	LeagueRankedResult r;
	r.present = true;
	r.gloryBefore = before;
	r.gloryAfter = after;
	r.delta = after - before;
	r.tierBefore = tb;
	r.divisionBefore = db;
	r.tierAfter = ta;
	r.divisionAfter = da;
	r.promoted = promoted;
	r.demoted = demoted;
	r.shield = shield;
	return r;
}

void SteadyAnimation()
{
	const LeagueRankedResult win = Result(945, 964, LeagueTier::Centurion, 3, LeagueTier::Centurion, 3, false, false);
	CHECK(LeagueResultKindOf(win) == LeagueResultKind::Steady);
	CHECK_EQ(LeagueResultDurationMs(LeagueResultKind::Steady), 1200);
	LeagueResultFrame f = LeagueResultAt(win, 0);
	CHECK_EQ(Pct(f.bar), 45);
	CHECK_EQ(f.glory, 945);
	CHECK(!f.deltaShown);
	CHECK(!f.done);
	// From 300 ms the part to be won shows faintly and the delta appears.
	f = LeagueResultAt(win, 300);
	CHECK(f.deltaShown);
	CHECK_EQ(Pct(f.ghostFrom), 45);
	CHECK_EQ(Pct(f.ghostTo), 64);
	CHECK(!f.ghostLoss);
	float last = 0;
	int lastGlory = 0;
	for (uint32_t ms = 300; ms <= 1100; ms += 50) {
		f = LeagueResultAt(win, ms);
		CHECK(f.bar >= last);
		CHECK(f.glory >= lastGlory);
		last = f.bar;
		lastGlory = f.glory;
	}
	CHECK(LeagueResultAt(win, 1100).flash > 0);
	CHECK_EQ(LeagueResultAt(win, 1180).flash, 0);
	f = LeagueResultAt(win, 1200);
	CHECK(f.done);
	CHECK_EQ(Pct(f.bar), 64);
	CHECK_EQ(f.glory, 964);
	CHECK_EQ(Pct(f.ghostTo), 0);
	// Past the end, and skipping, is the end.
	CHECK_EQ(LeagueResultAt(win, 99999).glory, 964);

	const LeagueRankedResult loss = Result(964, 948, LeagueTier::Centurion, 3, LeagueTier::Centurion, 3, false, false);
	f = LeagueResultAt(loss, 400);
	CHECK(f.ghostLoss);
	CHECK_EQ(Pct(f.ghostFrom), 48);
	CHECK_EQ(Pct(f.ghostTo), 64);
	f = LeagueResultAt(loss, 1200);
	CHECK_EQ(Pct(f.bar), 48);
	CHECK_EQ(f.glory, 948);
}

void PromotionAnimation()
{
	const LeagueRankedResult up = Result(288, 309, LeagueTier::Recruit, 1, LeagueTier::Legionary, 3, true, false, 3);
	CHECK(LeagueResultKindOf(up) == LeagueResultKind::Promotion);
	LeagueResultFrame f = LeagueResultAt(up, 0);
	CHECK_EQ(Pct(f.bar), 88);
	CHECK(f.deltaShown);
	CHECK_EQ(TierNumber(f.tier), TierNumber(LeagueTier::Recruit));
	CHECK_EQ(f.oldAlpha, 255);
	f = LeagueResultAt(up, 650);
	CHECK_EQ(Pct(f.bar), 100);
	CHECK_EQ(f.glory, 300);
	CHECK(f.oldAlpha < 255 && f.oldAlpha > 102);
	CHECK(!f.switched);
	f = LeagueResultAt(up, 1100);
	CHECK(f.switched);
	CHECK_EQ(TierNumber(f.tier), TierNumber(LeagueTier::Legionary));
	CHECK_EQ(f.division, 3);
	CHECK(f.newScale > 85 && f.newScale < 100);
	CHECK(f.rays > 0);
	CHECK_EQ(Pct(f.bar), 0);
	CHECK(!f.shieldShown);
	f = LeagueResultAt(up, 2400);
	CHECK(f.done);
	CHECK_EQ(Pct(f.bar), 9);
	CHECK_EQ(f.glory, 309);
	CHECK_EQ(f.newScale, 100);
	CHECK_EQ(f.newAlpha, 255);
	CHECK(f.shieldShown);
	CHECK_EQ(f.rays, 160);

	// A division of the same rank: no rays.
	const LeagueRankedResult division = Result(1190, 1205, LeagueTier::Centurion, 1, LeagueTier::Tribune, 3, true, false, 3);
	CHECK(LeagueResultAt(division, 2400).rays > 0);
	const LeagueRankedResult same = Result(895, 912, LeagueTier::Signifer, 1, LeagueTier::Centurion, 3, true, false, 3);
	CHECK(LeagueResultAt(same, 2400).rays > 0);
	const LeagueRankedResult step = Result(1095, 1110, LeagueTier::Centurion, 2, LeagueTier::Centurion, 1, true, false, 0);
	CHECK_EQ(LeagueResultAt(step, 2400).rays, 0);
	CHECK(!LeagueResultAt(step, 2400).shieldShown);
}

void DemotionAndPlacement()
{
	const LeagueRankedResult down = Result(600, 575, LeagueTier::Signifer, 3, LeagueTier::Legionary, 1, false, true);
	CHECK(LeagueResultKindOf(down) == LeagueResultKind::Demotion);
	LeagueResultFrame f = LeagueResultAt(down, 200);
	CHECK_EQ(Pct(f.bar), 0);
	CHECK(f.deltaShown);
	CHECK_EQ(f.oldAlpha, 255);
	f = LeagueResultAt(down, 1000);
	CHECK(f.switched);
	CHECK_EQ(TierNumber(f.tier), TierNumber(LeagueTier::Legionary));
	CHECK_EQ(Pct(f.bar), 100);
	CHECK(f.newAlpha > 0 && f.newAlpha < 255);
	f = LeagueResultAt(down, 1800);
	CHECK(f.done);
	CHECK_EQ(Pct(f.bar), 75);
	CHECK_EQ(f.glory, 575);
	CHECK(f.ghostLoss);
	CHECK_EQ(Pct(f.ghostFrom), 75);
	CHECK_EQ(Pct(f.ghostTo), 100);
	CHECK_EQ(f.rays, 0);

	LeagueRankedResult placing = Result(0, 0, LeagueTier::None, 0, LeagueTier::None, 0, false, false);
	placing.placementLeft = 3;
	CHECK(LeagueResultKindOf(placing) == LeagueResultKind::Placement);
	CHECK(LeagueResultAt(placing, 600).done);
	CHECK(!LeagueResultAt(placing, 600).deltaShown);

	const LeagueRankedResult placed = Result(0, 812, LeagueTier::None, 0, LeagueTier::Signifer, 1, false, false);
	CHECK(LeagueResultKindOf(placed) == LeagueResultKind::PlacementDone);
	f = LeagueResultAt(placed, 300);
	CHECK_EQ(TierNumber(f.tier), TierNumber(LeagueTier::None));
	f = LeagueResultAt(placed, 2000);
	CHECK(f.done);
	CHECK_EQ(TierNumber(f.tier), TierNumber(LeagueTier::Signifer));
	CHECK_EQ(f.glory, 812);
	CHECK_EQ(Pct(f.bar), 12);
	CHECK(f.rays > 0);
}

}

int main()
{
	Season();
	Queue();
	Ticket();
	Board();
	ReportAndErrors();
	MeBadges();
	Glory();
	Countdown();
	SteadyAnimation();
	PromotionAnimation();
	DemotionAndPlacement();
	return test::Summary("leagueranked");
}
