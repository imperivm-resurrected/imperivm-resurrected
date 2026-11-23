// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include "ui/leaguemodsprep.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/guard.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/downloads.h"
#include "mods/gbref.h"
#include "mods/mods.h"
#include "mods/modsleague.h"

namespace {

enum class Phase { Idle, Working, Downloading, Ready, Restarting, Failed };

struct Prep {
	Phase phase = Phase::Idle;
	std::string owner, error, profile;
	// The required mods that are downloaded, in the order of the download's index.
	std::vector<LeagueModRef> asked;
	RoomModStatus io;
	bool restart = false;
};

std::mutex g_mx;
Prep g_p;

void Fail(const std::string &error)
{
	LogW("content", "league mods: %s", TrSource(error).c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_p.phase = Phase::Failed;
	g_p.error = error;
}

// The id the download names a mod by when its archive has no mod.json: an installed copy's own id, or GameBanana's.
std::string RequestId(uint64_t gbId)
{
	for (const ModInfo &m : ModsSummaries()) {
		if (m.gbId == gbId && !m.id.empty()) return m.id;
	}
	return GbCatalogId(gbId);
}

void Worker(LeagueModsRule rule)
{
	ModsScan();
	std::vector<std::string> folders;
	std::vector<RoomModRequest> reqs;
	std::vector<LeagueModRef> asked;
	for (const LeagueModRef &need : rule.mods) {
		const std::string folder = ModsFolderOfGbFile(need.gbId, need.fileId);
		if (!folder.empty()) {
			folders.push_back(folder);
			continue;
		}
		RoomModRequest r;
		r.id = RequestId(need.gbId);
		r.gbId = need.gbId;
		r.version = need.version;
		r.gbFile = need.fileId;
		r.exactFile = true;
		reqs.push_back(r);
		asked.push_back(need);
	}
	if (!reqs.empty()) {
		if (RoomModsStatus().active) {
			Fail(Tr("Another download of mods is under way: wait for it to end and try again."));
			return;
		}
		LogI("content", "league mods: %u of %u are downloaded from GameBanana", static_cast<unsigned>(reqs.size()), static_cast<unsigned>(rule.mods.size()));
		{
			std::lock_guard<std::mutex> lock(g_mx);
			g_p.phase = Phase::Downloading;
			g_p.asked = asked;
			g_p.io = RoomModStatus();
			g_p.io.active = true;
			g_p.io.count = static_cast<int>(reqs.size());
		}
		RoomModsFromGb(reqs);
		RoomModStatus st;
		for (;;) {
			Sleep(150);
			st = RoomModsStatus();
			{
				std::lock_guard<std::mutex> lock(g_mx);
				g_p.io = st;
			}
			if (!st.active) break;
		}
		{
			std::lock_guard<std::mutex> lock(g_mx);
			g_p.phase = Phase::Working;
		}
		// What the download says it installed is a hint: the copies are looked for again by the file they came from.
		ModsScan();
		for (size_t i = 0; i < asked.size(); i++) {
			const std::string folder = ModsFolderOfGbFile(asked[i].gbId, asked[i].fileId);
			if (!folder.empty()) {
				folders.push_back(folder);
				continue;
			}
			const bool gone = st.note == kRoomModFileGone && std::find(st.failed.begin(), st.failed.end(), reqs[i].id) != st.failed.end();
			const std::string name = LeagueModLabel(asked[i]);
			Fail(gone ? TrFmt("%s is no longer on GameBanana: the league has to choose another file.", {name})
					  : TrFmt("Could not download %s from GameBanana. Try again in a while.", {name}));
			return;
		}
	}
	const bool hadGameplay = !ModsOwnLeagueMods().empty();
	const ModProfile p = ModsLooseProfileWith(folders);
	// Restarting with the same mods mounted would change nothing (the league lists a mod this game does not take for a gameplay one).
	if (ModsSameSet(p.mods, ModsMountedProfile().mods)) {
		Fail(Tr("The mods asked for are already active, but they don't match: tell the league's team."));
		return;
	}
	if (!ModsSaveProfile(p)) {
		Fail(Tr("The profile with the mods could not be saved."));
		return;
	}
	LogI("content", "league mods: profile \"%s\" ready with %u mods", p.name.c_str(), static_cast<unsigned>(p.mods.size()));
	std::lock_guard<std::mutex> lock(g_mx);
	g_p.phase = Phase::Ready;
	g_p.profile = p.name;
	g_p.restart = !rule.mods.empty() || hadGameplay;
}

std::string Percent(uint64_t got, uint64_t total)
{
	const uint64_t pct = total ? std::min<uint64_t>(100, got * 100 / total) : 0;
	return std::to_string(pct) + " %";
}

}

LeagueModsCheck LeagueModsFitNow(const LeagueModsRule &rule)
{
	if (!rule.set) return LeagueModsCheck();
	return LeagueModsCompare(rule, ModsOwnLeagueMods());
}

std::string LeagueModsRuleText(const LeagueModsRule &rule)
{
	if (!rule.set) return std::string();
	if (rule.BaseGame()) return Tr("Played without gameplay mods");
	return TrFmt("Played with: %s", {LeagueModsNames(rule.mods)});
}

std::string LeagueModsProblemText(const LeagueModsRule &rule, const LeagueModsCheck &check)
{
	if (!rule.set) return std::string();
	switch (check.fit) {
	case LeagueModsFit::Missing: return TrFmt("Played with %s: you don't have that version active.", {LeagueModsNames(check.missing)});
	case LeagueModsFit::NotFromGameBanana:
		return TrFmt("Played only with %s: one of your gameplay mods is not from GameBanana.", {LeagueModsNames(rule.mods)});
	case LeagueModsFit::Extra:
		if (rule.BaseGame()) return Tr("Played without gameplay mods: yours have to be turned off.");
		return TrFmt("Played only with %s: turn off your other gameplay mods.", {LeagueModsNames(rule.mods)});
	default: return std::string();
	}
}

void LeagueModsPrepStart(const LeagueModsRule &rule, const std::string &owner)
{
	if (!rule.set) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_p.phase != Phase::Idle && g_p.phase != Phase::Failed) return;
		g_p = Prep();
		g_p.phase = Phase::Working;
		g_p.owner = owner;
	}
	LogI("content", "league mods: preparing %s for %s", rule.BaseGame() ? "the base game" : (std::to_string(rule.mods.size()) + " mods").c_str(), owner.c_str());
	SafeThread(Worker, rule).detach();
}

LeagueModsPrepView LeagueModsPrepState(const std::string &owner)
{
	LeagueModsPrepView v;
	std::lock_guard<std::mutex> lock(g_mx);
	switch (g_p.phase) {
	case Phase::Working:
		v.busy = true;
		v.line = Tr("Preparing the mods...");
		break;
	case Phase::Downloading: {
		v.busy = true;
		const RoomModStatus &io = g_p.io;
		const int count = std::max(1, io.count), index = std::max(1, std::min(io.index, count));
		const std::string name = index - 1 < static_cast<int>(g_p.asked.size()) ? LeagueModLabel(g_p.asked[index - 1]) : std::string();
		if (io.unpacking) v.line = TrFmt("Unpacking %s (%s of %s)...", {name, std::to_string(index), std::to_string(count)});
		else v.line = TrFmt("Downloading %s (%s of %s): %s", {name, std::to_string(index), std::to_string(count), Percent(io.got, io.total)});
		break;
	}
	case Phase::Ready:
		v.busy = true;
		v.line = g_p.restart ? Tr("The mods are ready: the game restarts as soon as you are free (no room, search or match).") : Tr("Preparing the mods...");
		break;
	case Phase::Restarting:
		v.busy = true;
		v.line = Tr("The mods are ready: restarting the game...");
		break;
	case Phase::Failed:
		if (g_p.owner == owner) {
			v.failed = true;
			v.line = g_p.error;
		}
		break;
	default: break;
	}
	return v;
}

bool LeagueModsPrepReady()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_p.phase == Phase::Ready;
}

void LeagueModsPrepTick(bool canRestart)
{
	std::string profile;
	bool restart = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_p.phase != Phase::Ready || (g_p.restart && !canRestart)) return;
		profile = g_p.profile;
		restart = g_p.restart;
		g_p.phase = restart ? Phase::Restarting : Phase::Idle;
	}
	ModsSetActiveProfileName(profile);
	if (!restart) return;
	LogI("content", "league mods: restarting with \"%s\"", profile.c_str());
	ModsRestartGame();
}
