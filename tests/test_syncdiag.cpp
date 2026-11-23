// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/syncdiag.h"

namespace {

SyncSnapshot Snap(int32_t tick, uint32_t base)
{
	SyncSnapshot s;
	s.tick = tick;
	for (int i = 0; i < 8; i++) s.counters[i] = base + static_cast<uint32_t>(i);
	s.turn = 120;
	s.delay = 7;
	s.mask = 0x03;
	s.speed = 1000;
	s.clock = 4321;
	return s;
}

void TheHashIsTheXorOfTheCounters()
{
	SyncSnapshot s;
	s.counters[0] = 0xf0f0f0f0;
	s.counters[7] = 0x0f0f0f0f;
	CHECK_EQ(s.Hash(), 0xffffffffu);
	s.counters[3] = 0xffffffff;
	CHECK_EQ(s.Hash(), 0u);
}

void TheDiffNamesTheCountersThatSplit()
{
	const SyncSnapshot a = Snap(10, 100);
	SyncSnapshot b = a;
	CHECK_EQ(SyncCounterDiff(a.counters, b.counters), std::string());
	b.counters[1]++;
	b.counters[7]++;
	CHECK_EQ(SyncCounterDiff(a.counters, b.counters), std::string("threads, ai"));
	CHECK_EQ(SyncSnapshotDiff(a, b), std::string("threads, ai"));
}

void TheTimeOfTheGameIsComparedButNotTheDelay()
{
	const SyncSnapshot a = Snap(10, 100);
	SyncSnapshot b = a;
	b.delay = 60;
	CHECK_EQ(SyncSnapshotDiff(a, b), std::string());
	b.turn = 140;
	b.clock = 5000;
	CHECK_EQ(SyncSnapshotDiff(a, b), std::string("turn size, clock"));
	b.counters[0]++;
	CHECK_EQ(SyncSnapshotDiff(a, b), std::string("slots, turn size, clock"));
}

void SnapshotsTravelAndComeBack()
{
	std::vector<SyncSnapshot> list = {Snap(41, 0xdeadbe00), Snap(42, 0x1000)};
	list[1].mask = 0xff;
	list[1].counters[5] = 0x12345678;
	CHECK(list[0].Hash() != list[1].Hash());
	const std::string text = SyncEncode(true, 42, list);
	bool reply = false;
	int32_t tick = 0;
	std::vector<SyncSnapshot> back;
	CHECK(SyncDecode(text, reply, tick, back));
	CHECK(reply);
	CHECK_EQ(tick, 42);
	CHECK_EQ(back.size(), static_cast<size_t>(2));
	CHECK_EQ(SyncSnapshotLine(back[0]), SyncSnapshotLine(list[0]));
	CHECK_EQ(SyncSnapshotLine(back[1]), SyncSnapshotLine(list[1]));
	CHECK_EQ(back[1].mask, 0xffu);
	CHECK_EQ(back[0].clock, 4321u);
	CHECK(SyncFind(back, 41) != nullptr);
	CHECK(SyncFind(back, 43) == nullptr);
	CHECK(SyncFindHash(back, list[1].Hash()) == &back[1]);
}

void AnEmptyListAndBadTextAreTold()
{
	bool reply = true;
	int32_t tick = 5;
	std::vector<SyncSnapshot> back;
	CHECK(SyncDecode(SyncEncode(false, 7, {}), reply, tick, back));
	CHECK(!reply);
	CHECK_EQ(tick, 7);
	CHECK(back.empty());
	CHECK(!SyncDecode("", reply, tick, back));
	CHECK(!SyncDecode("SYNC9 0 1\n", reply, tick, back));
	CHECK(!SyncDecode("SYNC1 0 1\n12 nothing\n", reply, tick, back));
}

void TheFirstDifferenceIsTheEarliestCommonTick()
{
	std::vector<SyncSnapshot> mine = {Snap(10, 1), Snap(11, 2), Snap(12, 3), Snap(13, 4)};
	std::vector<SyncSnapshot> theirs = {Snap(11, 2), Snap(12, 3), Snap(13, 4), Snap(14, 5)};
	int32_t tick = -1;
	std::string what;
	CHECK(!SyncFirstDifference(mine, theirs, tick, what));
	theirs[2].counters[4] ^= 1;
	theirs[1].turn = 200;
	CHECK(SyncFirstDifference(mine, theirs, tick, what));
	CHECK_EQ(tick, 12);
	CHECK_EQ(what, std::string("turn size"));
}

void TheSignatureNamesWhatDiffers()
{
	SimInputs in;
	in.turnsForced = true;
	in.minTick = 120;
	in.maxTick = 500;
	in.tickPercent = 70;
	in.lambda = 50;
	in.newPf = 1;
	in.aiEnabled = 1;
	in.crashFixSites = 5;
	in.rejoinHooks = in.dropHooks = true;
	const std::string a = SimSignatureText(in);
	CHECK_EQ(a, std::string("nt=120.500.70.50,pf=1,ai=1,it=0,cf=5,rj=1,nd=1"));
	CHECK_EQ(SimSignatureDiff(a, a), std::string());
	SimInputs other = in;
	other.newPf = 0;
	other.crashFixSites = 0;
	CHECK_EQ(SimSignatureDiff(a, SimSignatureText(other)), std::string("Config.ini NewPF 1 vs 0; crash protection 5 vs 0"));
	other = in;
	other.turnsForced = false;
	CHECK_EQ(SimSignatureDiff(a, SimSignatureText(other)), std::string("network turns 120.500.70.50 vs off"));
	CHECK_EQ(SimSignatureDiff("pf=1", "pf=1,zz=2"), std::string("zz none vs 2"));
}

void TheRulesLevelCarriesTheSignature()
{
	const std::string level = RulesLevelWithSim("gbr/sim4/mfs1/esq2/api3", "nt=off,pf=0");
	CHECK_EQ(level, std::string("gbr/sim4/mfs1/esq2/api3+nt=off,pf=0"));
	std::string rules, sim;
	SplitRulesLevel(level, rules, sim);
	CHECK_EQ(rules, std::string("gbr/sim4/mfs1/esq2/api3"));
	CHECK_EQ(sim, std::string("nt=off,pf=0"));
	SplitRulesLevel("gbr/sim4/esq2/api3", rules, sim);
	CHECK_EQ(rules, std::string("gbr/sim4/esq2/api3"));
	CHECK_EQ(sim, std::string());
	CHECK_EQ(RulesLevelWithSim("gbr/sim4", ""), std::string("gbr/sim4"));
}

}

int main()
{
	TheHashIsTheXorOfTheCounters();
	TheDiffNamesTheCountersThatSplit();
	TheTimeOfTheGameIsComparedButNotTheDelay();
	SnapshotsTravelAndComeBack();
	AnEmptyListAndBadTextAreTold();
	TheFirstDifferenceIsTheEarliestCommonTick();
	TheSignatureNamesWhatDiffers();
	TheRulesLevelCarriesTheSignature();
	return test::Summary("syncdiag");
}
