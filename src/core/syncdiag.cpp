// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/syncdiag.h"

#include <cstdio>
#include <utility>

namespace {
const char *const kSyncCounterNames[8] = {"slots", "threads", "pathfinder", "exploration", "scriptstate", "netcmds", "extra", "ai"};
}

const char *SyncCounterName(int index)
{
	return index >= 0 && index < 8 ? kSyncCounterNames[index] : "?";
}

namespace {

const char kSyncHeader[] = "SYNC1";

std::vector<std::string> Split(const std::string &text, char sep)
{
	std::vector<std::string> out;
	size_t p = 0;
	for (;;) {
		const size_t e = text.find(sep, p);
		out.push_back(text.substr(p, e == std::string::npos ? std::string::npos : e - p));
		if (e == std::string::npos) break;
		p = e + 1;
	}
	return out;
}

std::vector<std::pair<std::string, std::string>> SignatureParts(const std::string &signature)
{
	std::vector<std::pair<std::string, std::string>> out;
	if (signature.empty()) return out;
	for (const std::string &part : Split(signature, ',')) {
		const size_t eq = part.find('=');
		if (eq == std::string::npos) out.push_back({part, ""});
		else out.push_back({part.substr(0, eq), part.substr(eq + 1)});
	}
	return out;
}

std::string PartName(const std::string &key)
{
	if (key == "nt") return "network turns";
	if (key == "pf") return "Config.ini NewPF";
	if (key == "ai") return "Config.ini AIEnabled";
	if (key == "it") return "Config.ini InvalidTerrains";
	if (key == "cf") return "crash protection";
	if (key == "rj") return "rejoin patches";
	if (key == "nd") return "drop patches";
	return key;
}

}

uint32_t SyncSnapshot::Hash() const
{
	uint32_t h = 0;
	for (uint32_t c : counters) h ^= c;
	return h;
}

std::string SyncCounterDiff(const uint32_t a[8], const uint32_t b[8])
{
	std::string out;
	for (int i = 0; i < 8; i++) {
		if (a[i] == b[i]) continue;
		if (!out.empty()) out += ", ";
		out += kSyncCounterNames[i];
	}
	return out;
}

std::string SyncSnapshotDiff(const SyncSnapshot &a, const SyncSnapshot &b)
{
	std::string out = SyncCounterDiff(a.counters, b.counters);
	auto add = [&](bool differs, const char *what) {
		if (!differs) return;
		if (!out.empty()) out += ", ";
		out += what;
	};
	add(a.turn != b.turn, "turn size");
	add(a.mask != b.mask, "players");
	add(a.speed != b.speed, "speed");
	add(a.clock != b.clock, "clock");
	return out;
}

bool SyncFirstDifference(const std::vector<SyncSnapshot> &mine, const std::vector<SyncSnapshot> &theirs, int32_t &tick, std::string &what)
{
	bool found = false;
	for (const SyncSnapshot &t : theirs) {
		const SyncSnapshot *m = SyncFind(mine, t.tick);
		if (!m || (found && t.tick >= tick)) continue;
		const std::string diff = SyncSnapshotDiff(*m, t);
		if (diff.empty()) continue;
		found = true;
		tick = t.tick;
		what = diff;
	}
	return found;
}

std::string SyncSnapshotLine(const SyncSnapshot &s)
{
	char line[200];
	sprintf_s(line, "%d %08x %08x %08x %08x %08x %08x %08x %08x %u %u %02x %u %u", s.tick, s.counters[0], s.counters[1], s.counters[2], s.counters[3],
		s.counters[4], s.counters[5], s.counters[6], s.counters[7], s.turn, s.delay, s.mask, s.speed, s.clock);
	return line;
}

bool SyncSnapshotParse(const std::string &line, SyncSnapshot &out)
{
	SyncSnapshot s;
	const int n = sscanf_s(line.c_str(), "%d %x %x %x %x %x %x %x %x %u %u %x %u %u", &s.tick, &s.counters[0], &s.counters[1], &s.counters[2], &s.counters[3],
		&s.counters[4], &s.counters[5], &s.counters[6], &s.counters[7], &s.turn, &s.delay, &s.mask, &s.speed, &s.clock);
	if (n != 14) return false;
	out = s;
	return true;
}

std::string SyncEncode(bool reply, int32_t desyncTick, const std::vector<SyncSnapshot> &snapshots)
{
	std::string out = std::string(kSyncHeader) + " " + (reply ? "1" : "0") + " " + std::to_string(desyncTick) + "\n";
	for (const SyncSnapshot &s : snapshots) out += SyncSnapshotLine(s) + "\n";
	return out;
}

bool SyncDecode(const std::string &text, bool &reply, int32_t &desyncTick, std::vector<SyncSnapshot> &snapshots)
{
	snapshots.clear();
	const std::vector<std::string> lines = Split(text, '\n');
	char head[8] = {};
	int r = 0, tick = 0;
	if (lines.empty() || sscanf_s(lines[0].c_str(), "%7s %d %d", head, static_cast<unsigned>(sizeof(head)), &r, &tick) != 3) return false;
	if (std::string(head) != kSyncHeader || (r != 0 && r != 1)) return false;
	for (size_t i = 1; i < lines.size(); i++) {
		if (lines[i].empty()) continue;
		SyncSnapshot s;
		if (!SyncSnapshotParse(lines[i], s)) return false;
		snapshots.push_back(s);
	}
	reply = r == 1;
	desyncTick = tick;
	return true;
}

const SyncSnapshot *SyncFind(const std::vector<SyncSnapshot> &snapshots, int32_t tick)
{
	for (const SyncSnapshot &s : snapshots) {
		if (s.tick == tick) return &s;
	}
	return nullptr;
}

const SyncSnapshot *SyncFindHash(const std::vector<SyncSnapshot> &snapshots, uint32_t hash)
{
	for (const SyncSnapshot &s : snapshots) {
		if (s.Hash() == hash) return &s;
	}
	return nullptr;
}

std::string SimSignatureText(const SimInputs &in)
{
	std::string nt = "off";
	if (in.turnsForced) {
		nt = std::to_string(in.minTick) + "." + std::to_string(in.maxTick) + "." + std::to_string(in.tickPercent) + "." + std::to_string(in.lambda);
	}
	return "nt=" + nt + ",pf=" + std::to_string(in.newPf) + ",ai=" + std::to_string(in.aiEnabled) + ",it=" + std::to_string(in.invalidTerrains) +
		",cf=" + std::to_string(in.crashFixSites) + ",rj=" + (in.rejoinHooks ? "1" : "0") + ",nd=" + (in.dropHooks ? "1" : "0");
}

std::string SimSignatureDiff(const std::string &mine, const std::string &theirs)
{
	const auto a = SignatureParts(mine), b = SignatureParts(theirs);
	std::string out;
	auto add = [&](const std::string &text) { out += (out.empty() ? "" : "; ") + text; };
	for (const auto &p : a) {
		const std::string *other = nullptr;
		for (const auto &q : b) {
			if (q.first == p.first) other = &q.second;
		}
		if (!other) add(PartName(p.first) + " " + p.second + " vs none");
		else if (*other != p.second) add(PartName(p.first) + " " + p.second + " vs " + *other);
	}
	for (const auto &q : b) {
		bool known = false;
		for (const auto &p : a) known = known || p.first == q.first;
		if (!known) add(PartName(q.first) + " none vs " + q.second);
	}
	return out;
}

std::string RulesLevelWithSim(const std::string &rulesLevel, const std::string &signature)
{
	return signature.empty() ? rulesLevel : rulesLevel + "+" + signature;
}

void SplitRulesLevel(const std::string &irlvl, std::string &rulesLevel, std::string &signature)
{
	const size_t plus = irlvl.find('+');
	rulesLevel = irlvl.substr(0, plus);
	signature = plus == std::string::npos ? std::string() : irlvl.substr(plus + 1);
}
