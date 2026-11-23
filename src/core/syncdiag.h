// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The name of one of the eight counters of the game's sync state, in its order (0 slots ... 7 ai): the hash every computer compares on each
// tick is their XOR.
const char *SyncCounterName(int index);

// The sync state as the game hashed it on one tick, with what decided how far that tick moved the game.
struct SyncSnapshot {
	int32_t tick = 0;
	uint32_t counters[8] = {};
	// The turn size and the added delay (CNetTransport +0x70, +0x74), the players in the lockstep (this one included), the game's speed and
	// clock.
	uint32_t turn = 0, delay = 0, mask = 0, speed = 0, clock = 0;
	uint32_t Hash() const;
};

// The names of the counters that differ, "threads, ai"; empty when all are equal.
std::string SyncCounterDiff(const uint32_t a[8], const uint32_t b[8]);
// The same for two snapshots, with "turn size", "players", "speed" and "clock" after the counters: a turn or a clock apart says the game's
// time split, counters alone that its state did. The delay is each computer's own and is not compared.
std::string SyncSnapshotDiff(const SyncSnapshot &a, const SyncSnapshot &b);
// The first tick both lists have where the snapshots differ; false when they agree on every common tick.
bool SyncFirstDifference(const std::vector<SyncSnapshot> &mine, const std::vector<SyncSnapshot> &theirs, int32_t &tick, std::string &what);
std::string SyncSnapshotLine(const SyncSnapshot &s);
bool SyncSnapshotParse(const std::string &line, SyncSnapshot &out);
// What a computer sends the others when its game desyncs: the tick, whether it answers another's, and its last snapshots.
std::string SyncEncode(bool reply, int32_t desyncTick, const std::vector<SyncSnapshot> &snapshots);
bool SyncDecode(const std::string &text, bool &reply, int32_t &desyncTick, std::vector<SyncSnapshot> &snapshots);
const SyncSnapshot *SyncFind(const std::vector<SyncSnapshot> &snapshots, int32_t tick);
const SyncSnapshot *SyncFindHash(const std::vector<SyncSnapshot> &snapshots, uint32_t hash);

// What decides the simulation besides the mod's build and its rules: two computers that differ in it must not play together.
struct SimInputs {
	// The network turns forced by the mod (nettune), with their sizes and smoothing; false when the game's own values are used.
	bool turnsForced = false;
	unsigned minTick = 0, maxTick = 0, tickPercent = 0, lambda = 0;
	// Config.ini [system]: the pathfinder, whether the AI runs, and the terrains it cannot walk.
	int newPf = 0, aiEnabled = 0, invalidTerrains = 0;
	// The crash protection's patched sites (0 when it is off), and the mod's own patches of the simulation installed.
	int crashFixSites = 0;
	bool rejoinHooks = false, dropHooks = false;
};
std::string SimSignatureText(const SimInputs &in);
// What differs between two signatures, for the log: "NewPF 1 vs 0; crash protection 5 vs 0".
std::string SimSignatureDiff(const std::string &mine, const std::string &theirs);
// The rules level a room announces ("irlvl"), with the signature after a '+'; and back.
std::string RulesLevelWithSim(const std::string &rulesLevel, const std::string &signature);
void SplitRulesLevel(const std::string &irlvl, std::string &rulesLevel, std::string &signature);
