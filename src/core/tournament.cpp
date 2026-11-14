// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/tournament.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "core/leaguemaps.h"

namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

int Int(const Json &j, const char *key, int def = 0)
{
	const Json *v = j.Get(key);
	return v && v->type == Json::Number ? static_cast<int>(v->num) : def;
}

int64_t Int64(const Json &j, const char *key)
{
	const Json *v = j.Get(key);
	return v && v->type == Json::Number ? static_cast<int64_t>(std::llround(v->num)) : 0;
}

// A SteamID or a lobby: 64-bit numbers the server sends as text.
uint64_t Id64(const Json *v)
{
	if (!v) return 0;
	if (v->type == Json::String) return _strtoui64(v->str.c_str(), nullptr, 10);
	if (v->type == Json::Number && v->num > 0) return static_cast<uint64_t>(v->num);
	return 0;
}

std::string Text(const Json &j, const char *key)
{
	const Json *v = j.Get(key);
	return v && v->type == Json::String ? v->str : std::string();
}

TourneyCard CardFromJson(const Json *v)
{
	TourneyCard c;
	if (!v || v->type != Json::Object) return c;
	c.entryId = Int64(*v, "entryId");
	c.steamId = Id64(v->Get("steamId"));
	c.name = Text(*v, "name");
	c.seed = Int(*v, "seed");
	return c;
}

bool Active(TourneyStatus s)
{
	return s == TourneyStatus::Signup || s == TourneyStatus::Checkin || s == TourneyStatus::Running;
}

TourneyNextSide SideFromJson(const Json *v)
{
	TourneyNextSide s;
	if (!v || v->type != Json::Object) return s;
	s.steamId = Id64(v->Get("steamId"));
	if (!s.steamId) s.steamId = Id64(v->Get("steamid"));
	s.name = Text(*v, "name");
	s.side = Text(*v, "side");
	s.seed = Int(*v, "seed");
	s.inRoom = v->GetBool("inRoom");
	return s;
}

}

TourneyStatus TourneyStatusFromId(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "signup") return TourneyStatus::Signup;
	if (s == "checkin") return TourneyStatus::Checkin;
	if (s == "running") return TourneyStatus::Running;
	if (s == "finished") return TourneyStatus::Finished;
	if (s == "cancelled") return TourneyStatus::Cancelled;
	return TourneyStatus::Unknown;
}

TourneyMatchStatus TourneyMatchStatusFromId(const std::string &id)
{
	static const std::pair<const char *, TourneyMatchStatus> kNames[] = {{"pending", TourneyMatchStatus::Pending}, {"ready", TourneyMatchStatus::Ready},
		{"live", TourneyMatchStatus::Live}, {"disputed", TourneyMatchStatus::Disputed}, {"done", TourneyMatchStatus::Done}, {"bye", TourneyMatchStatus::Bye},
		{"walkover", TourneyMatchStatus::Walkover}};
	const std::string s = Lower(id);
	for (const auto &n : kNames) {
		if (s == n.first) return n.second;
	}
	return TourneyMatchStatus::Unknown;
}

bool TourneyMatchOver(TourneyMatchStatus s)
{
	return s == TourneyMatchStatus::Done || s == TourneyMatchStatus::Bye || s == TourneyMatchStatus::Walkover;
}

TourneyEntryStatus TourneyEntryStatusFromId(const std::string &id)
{
	static const std::pair<const char *, TourneyEntryStatus> kNames[] = {{"signed", TourneyEntryStatus::Signed}, {"waitlist", TourneyEntryStatus::Waitlist},
		{"playing", TourneyEntryStatus::Playing}, {"out", TourneyEntryStatus::Out}, {"withdrawn", TourneyEntryStatus::Withdrawn},
		{"disqualified", TourneyEntryStatus::Disqualified}, {"missed_checkin", TourneyEntryStatus::MissedCheckin}};
	const std::string s = Lower(id);
	for (const auto &n : kNames) {
		if (s == n.first) return n.second;
	}
	return TourneyEntryStatus::Unknown;
}

bool TourneyEntryActive(TourneyEntryStatus s)
{
	return s == TourneyEntryStatus::Signed || s == TourneyEntryStatus::Waitlist || s == TourneyEntryStatus::Playing;
}

TourneyStage TourneyStageFromId(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "final") return TourneyStage::Final;
	if (s == "semifinal") return TourneyStage::Semifinal;
	if (s == "quarterfinal") return TourneyStage::Quarterfinal;
	if (s == "round_of_16") return TourneyStage::RoundOf16;
	if (s == "round_of_32") return TourneyStage::RoundOf32;
	return TourneyStage::Unknown;
}

TourneyStage TourneyStageOf(int round, int rounds)
{
	switch (rounds - round) {
	case 0: return TourneyStage::Final;
	case 1: return TourneyStage::Semifinal;
	case 2: return TourneyStage::Quarterfinal;
	case 3: return TourneyStage::RoundOf16;
	case 4: return TourneyStage::RoundOf32;
	default: return TourneyStage::Unknown;
	}
}

const char *TourneyStageName(TourneyStage s)
{
	switch (s) {
	case TourneyStage::Final: return "Final";
	case TourneyStage::Semifinal: return "Semifinal";
	case TourneyStage::Quarterfinal: return "Quarterfinal";
	case TourneyStage::RoundOf16: return "Round of 16";
	case TourneyStage::RoundOf32: return "Round of 32";
	default: return "";
	}
}

TourneyMap TourneyMapFromJson(const Json *v)
{
	TourneyMap m;
	if (!v || v->type != Json::Object) return m;
	if (Lower(Text(*v, "kind")) == "scenario") {
		m.kind = Lower(Text(*v, "source")) == "upload" ? TourneyMapKind::Upload : TourneyMapKind::Game;
		m.title = Text(*v, "title");
		m.file = Text(*v, "file");
		m.hash = Lower(Text(*v, "hash"));
		m.bytes = Int64(*v, "bytes");
		m.players = Int(*v, "players");
		// A file the mod would not write is no file: the round shows no map rather than a path out of Scenarios.
		if (!TourneyScenarioFileValid(m.file)) m.file.clear();
		if (m.title.empty()) m.title = TourneyScenarioStem(m.file);
		return m;
	}
	m.name = Text(*v, "name");
	m.type = Text(*v, "type");
	m.size = Text(*v, "size");
	return m;
}

Json TourneyMapJson(const TourneyMap &m)
{
	Json j = Json::MakeObject();
	if (!m.Scenario()) {
		j.Set("kind", Json::MakeString("random"));
		j.Set("name", Json::MakeString(m.name));
		j.Set("type", Json::MakeString(m.type.empty() ? "random" : m.type));
		j.Set("size", Json::MakeString(m.size));
		return j;
	}
	j.Set("kind", Json::MakeString("scenario"));
	j.Set("source", Json::MakeString(m.kind == TourneyMapKind::Upload ? "upload" : "game"));
	j.Set("title", Json::MakeString(m.title.empty() ? TourneyScenarioStem(m.file) : m.title));
	j.Set("file", Json::MakeString(m.file));
	j.Set("hash", Json::MakeString(m.hash));
	j.Set("bytes", Json::MakeNumber(static_cast<double>(m.bytes)));
	j.Set("players", Json::MakeNumber(m.players));
	return j;
}

bool TourneySameMap(const TourneyMap &a, const TourneyMap &b)
{
	if (a.Scenario() != b.Scenario()) return false;
	if (!a.Scenario()) return a.name == b.name && a.size == b.size;
	return a.kind == b.kind && _stricmp(a.file.c_str(), b.file.c_str()) == 0 && a.hash == b.hash;
}

bool TourneyScenarioFileValid(const std::string &file)
{
	if (file.size() < 6 || file.size() > 120 || _stricmp(file.c_str() + file.size() - 5, ".bfhp") != 0) return false;
	if (file[0] == '.' || file[0] == ' ' || file.find("..") != std::string::npos) return false;
	for (unsigned char c : file) {
		if (c < 0x20 || strchr("\\/:*?\"<>|", c)) return false;
	}
	return true;
}

std::string TourneyScenarioStem(const std::string &file)
{
	const size_t dot = file.find_last_of('.');
	return dot == std::string::npos ? file : file.substr(0, dot);
}

bool TourneySha256Valid(const std::string &hash)
{
	if (hash.size() != 64) return false;
	for (char c : hash) {
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	}
	return true;
}

const char *TourneyPresetId(TourneyPreset p)
{
	switch (p) {
	case TourneyPreset::Classic: return "classic";
	case TourneyPreset::Various: return "various";
	default: return "competitive";
	}
}

TourneyPreset TourneyPresetFromId(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "classic") return TourneyPreset::Classic;
	if (s == "various") return TourneyPreset::Various;
	return TourneyPreset::Competitive;
}

bool TourneyPresetValid(const std::string &id)
{
	const std::string s = Lower(id);
	return s == "competitive" || s == "classic" || s == "various";
}

TourneySettings TourneySettingsFromJson(const Json *v)
{
	TourneySettings s;
	if (!v || v->type != Json::Object) return s;
	s.preset = Text(*v, "preset");
	s.hideCivs = v->GetBool("hideCivs");
	if (const Json *fog = v->Get("fog"); fog && fog->type == Json::Bool) s.fog = fog->b ? 1 : 0;
	if (const Json *explored = v->Get("explored"); explored && explored->type == Json::Bool) s.explored = explored->b ? 1 : 0;
	const std::string diplomacy = Lower(Text(*v, "diplomacy"));
	if (diplomacy == "locked" || diplomacy == "free") s.diplomacy = diplomacy;
	if (const Json *delay = v->Get("specDelay"); delay && delay->type == Json::Number) s.specDelay = std::max(0, static_cast<int>(delay->num));
	return s;
}

Json TourneySettingsJson(const TourneySettings &s)
{
	const TourneyRoomRules r = TourneyRulesOf(s);
	Json j = Json::MakeObject();
	j.Set("preset", Json::MakeString(TourneyPresetId(r.preset)));
	j.Set("hideCivs", Json::MakeBool(r.hideCivs));
	j.Set("fog", Json::MakeBool(r.fog));
	j.Set("explored", Json::MakeBool(r.explored));
	j.Set("diplomacy", Json::MakeString(r.lockedDiplomacy ? "locked" : "free"));
	j.Set("specDelay", Json::MakeNumber(TourneySpecDelay(s)));
	return j;
}

TourneyRoomRules TourneyPresetRules(TourneyPreset p)
{
	TourneyRoomRules r;
	r.preset = p;
	r.fog = p != TourneyPreset::Various;
	r.explored = p == TourneyPreset::Various;
	r.lockedDiplomacy = p == TourneyPreset::Competitive;
	return r;
}

TourneyRoomRules TourneyRulesOf(const TourneySettings &s)
{
	TourneyRoomRules r = TourneyPresetRules(TourneyPresetFromId(s.preset));
	r.hideCivs = s.hideCivs;
	if (s.fog >= 0) r.fog = s.fog != 0;
	if (s.explored >= 0) r.explored = s.explored != 0;
	if (!s.diplomacy.empty()) r.lockedDiplomacy = s.diplomacy == "locked";
	return r;
}

int TourneySpecDelay(const TourneySettings &s)
{
	if (s.specDelay < 0) return 120;
	if (s.specDelay == 0) return 0;
	return std::max(60, std::min(600, s.specDelay));
}

bool TourneySpecDelayValid(int seconds)
{
	return seconds == 0 || seconds == 60 || seconds == 120 || seconds == 300;
}

TourneySettings TourneyCupDefaultSettings()
{
	TourneySettings s;
	s.preset = TourneyPresetId(TourneyPreset::Competitive);
	s.hideCivs = true;
	s.specDelay = 120;
	return TourneyExplicitSettings(s);
}

TourneySettings TourneyExplicitSettings(const TourneySettings &s)
{
	const TourneyRoomRules r = TourneyRulesOf(s);
	TourneySettings out = s;
	out.preset = TourneyPresetId(r.preset);
	out.fog = r.fog ? 1 : 0;
	out.explored = r.explored ? 1 : 0;
	out.diplomacy = r.lockedDiplomacy ? "locked" : "free";
	out.specDelay = TourneySpecDelay(s);
	return out;
}

TourneyRequirements TourneyRequirementsFromJson(const Json *v)
{
	TourneyRequirements r;
	if (!v || v->type != Json::Object) return r;
	r.minGames = std::max(0, Int(*v, "minGames"));
	const Json *tier = v->Get("maxTier");
	r.maxTier = tier && tier->type == Json::String ? LeagueTierFromId(tier->str) : LeagueTier::None;
	if (!TourneyMaxTierValid(r.maxTier)) r.maxTier = LeagueTier::None;
	return r;
}

Json TourneyRequirementsJson(const TourneyRequirements &r)
{
	Json j = Json::MakeObject();
	j.Set("minGames", Json::MakeNumber(r.minGames));
	// The league's tier ids are the English names ("signifer"), not LeagueTierId's Spanish ones.
	j.Set("maxTier", TourneyMaxTierValid(r.maxTier) ? Json::MakeString(Lower(LeagueTierName(r.maxTier))) : Json());
	return j;
}

bool TourneyMaxTierValid(LeagueTier t)
{
	return t >= LeagueTier::Recruit && t <= LeagueTier::Legate;
}

TourneyUnmet TourneyCheckRequirements(const TourneyRequirements &r, int gamesPlayed, LeagueTier tier)
{
	if (r.minGames > 0 && gamesPlayed < r.minGames) return TourneyUnmet::Games;
	if (TourneyMaxTierValid(r.maxTier) && tier != LeagueTier::None && static_cast<int>(tier) > static_cast<int>(r.maxTier)) return TourneyUnmet::Tier;
	return TourneyUnmet::None;
}

TourneyOrganizer TourneyOrganizerFromJson(const Json *v)
{
	TourneyOrganizer o;
	if (!v || v->type != Json::Object) return o;
	o.steamId = Id64(v->Get("steamId"));
	o.name = Text(*v, "name");
	return o;
}

bool TourneyParseSummary(const Json &j, TourneySummary &out)
{
	if (j.type != Json::Object) return false;
	out.id = Int64(j, "id");
	out.slug = Text(j, "slug");
	if (!out.id || out.slug.empty()) return false;
	out.name = Text(j, "name");
	out.description = Text(j, "description");
	out.official = j.GetBool("official");
	out.isPrivate = j.GetBool("private");
	out.organizer = TourneyOrganizerFromJson(j.Get("organizer"));
	const Json *season = j.Get("season");
	out.season = season && season->type == Json::Number ? static_cast<int>(season->num) : -1;
	out.format = Text(j, "format");
	out.status = TourneyStatusFromId(Text(j, "status"));
	out.maxEntries = Int(j, "maxEntries");
	out.minEntries = Int(j, "minEntries");
	// The page lists the entries instead of counting them; TourneyParseDetail counts them.
	out.entries = Int(j, "entries");
	out.bestOf = std::max(1, Int(j, "bestOf", 1));
	out.finalBestOf = std::max(1, Int(j, "finalBestOf", out.bestOf));
	out.seeding = Text(j, "seeding");
	out.contentKey = Text(j, "contentKey");
	out.contentName = Text(j, "contentName");
	out.settings = TourneySettingsFromJson(j.Get("settings"));
	out.requirements = TourneyRequirementsFromJson(j.Get("requirements"));
	out.maps.clear();
	if (const Json *maps = j.Get("maps"); maps && maps->type == Json::Array) {
		for (const Json &m : maps->arr) out.maps.push_back(TourneyMapFromJson(&m));
	}
	out.signupOpensAt = Int64(j, "signupOpensAt");
	out.checkinOpensAt = Int64(j, "checkinOpensAt");
	out.startsAt = Int64(j, "startsAt");
	out.noshowMinutes = Int(j, "noshowMinutes");
	return true;
}

bool TourneyParseList(const Json &j, std::vector<TourneySummary> &out)
{
	const Json *list = j.Get("tournaments");
	if (!list || list->type != Json::Array) return false;
	out.clear();
	for (const Json &t : list->arr) {
		TourneySummary s;
		if (TourneyParseSummary(t, s)) out.push_back(s);
	}
	return true;
}

int TourneyFullRounds(int maxEntries)
{
	int rounds = 0;
	for (int n = 1; n < maxEntries; n *= 2) rounds++;
	return rounds;
}

TourneyMap TourneyMapOfRound(const TourneySummary &t, int round, int rounds)
{
	if (t.maps.empty()) return TourneyMap();
	const int count = static_cast<int>(t.maps.size());
	// A list that covers every round is read from the final backwards, as the server does.
	if (rounds > 0 && count >= rounds) return t.maps[static_cast<size_t>(std::max(0, std::min(count - 1, count - rounds + round - 1)))];
	return t.maps[static_cast<size_t>(std::max(1, std::min(round, count))) - 1];
}

bool TourneyParseDetail(const Json &j, TourneyDetail &out)
{
	out = TourneyDetail();
	if (!TourneyParseSummary(j, out)) return false;
	out.rounds = Int(j, "rounds");
	int counted = 0;
	if (const Json *list = j.Get("entries"); list && list->type == Json::Array) {
		for (const Json &e : list->arr) {
			if (e.type != Json::Object) continue;
			TourneyEntrant x;
			x.entryId = Int64(e, "entryId");
			x.steamId = Id64(e.Get("steamId"));
			x.name = Text(e, "name");
			x.status = TourneyEntryStatusFromId(Text(e, "status"));
			x.seed = Int(e, "seed");
			x.placement = Int(e, "placement");
			x.checkedIn = e.GetBool("checkedIn");
			if (x.status == TourneyEntryStatus::Signed || x.status == TourneyEntryStatus::Playing || x.status == TourneyEntryStatus::Out) counted++;
			out.entrants.push_back(x);
		}
		out.entries = counted;
	}
	if (const Json *list = j.Get("matches"); list && list->type == Json::Array) {
		for (const Json &m : list->arr) {
			if (m.type != Json::Object) continue;
			TourneyMatch x;
			x.id = Int64(m, "id");
			x.round = Int(m, "round");
			x.position = Int(m, "position");
			x.stage = TourneyStageFromId(Text(m, "stage"));
			x.bestOf = std::max(1, Int(m, "bestOf", 1));
			x.a = CardFromJson(m.Get("a"));
			x.b = CardFromJson(m.Get("b"));
			if (const Json *score = m.Get("score"); score && score->type == Json::Array && score->arr.size() >= 2) {
				x.scoreA = static_cast<int>(score->arr[0].num);
				x.scoreB = static_cast<int>(score->arr[1].num);
			}
			x.winner = Int64(m, "winner");
			x.status = TourneyMatchStatusFromId(Text(m, "status"));
			x.lobby = Id64(m.Get("lobby"));
			x.specLobby = Id64(m.Get("specLobby"));
			x.readyAt = Int64(m, "readyAt");
			x.deadlineAt = Int64(m, "deadlineAt");
			x.startedAt = Int64(m, "startedAt");
			x.finishedAt = Int64(m, "finishedAt");
			out.rounds = std::max(out.rounds, x.round);
			out.matches.push_back(x);
		}
	}
	for (TourneyMatch &m : out.matches) {
		if (m.stage == TourneyStage::Unknown) m.stage = TourneyStageOf(m.round, out.rounds);
	}
	if (const Json *list = j.Get("log"); list && list->type == Json::Array) {
		for (const Json &l : list->arr) {
			if (l.type != Json::Object) continue;
			out.log.push_back({Int64(l, "at"), Text(l, "actor"), Text(l, "action"), Text(l, "detail")});
		}
	}
	return true;
}

const TourneyMatch *TourneyMatchAt(const TourneyDetail &d, int round, int position)
{
	for (const TourneyMatch &m : d.matches) {
		if (m.round == round && m.position == position) return &m;
	}
	return nullptr;
}

const TourneyEntrant *TourneyEntrantOf(const TourneyDetail &d, int64_t entryId)
{
	for (const TourneyEntrant &e : d.entrants) {
		if (entryId && e.entryId == entryId) return &e;
	}
	return nullptr;
}

const TourneyEntrant *TourneyEntrantBySteamId(const TourneyDetail &d, uint64_t steamId)
{
	for (const TourneyEntrant &e : d.entrants) {
		if (steamId && e.steamId == steamId) return &e;
	}
	return nullptr;
}

std::vector<const TourneyMatch *> TourneyPathOf(const TourneyDetail &d, int64_t entryId)
{
	std::vector<const TourneyMatch *> out;
	for (const TourneyMatch &m : d.matches) {
		if (m.Has(entryId)) out.push_back(&m);
	}
	std::sort(out.begin(), out.end(), [](const TourneyMatch *a, const TourneyMatch *b) { return a->round < b->round; });
	return out;
}

void TourneyProgress(const TourneyDetail &d, int &done, int &total)
{
	done = total = 0;
	for (const TourneyMatch &m : d.matches) {
		if (m.status == TourneyMatchStatus::Bye) continue;
		total++;
		if (TourneyMatchOver(m.status)) done++;
	}
}

int64_t TourneyChampion(const TourneyDetail &d)
{
	for (const TourneyEntrant &e : d.entrants) {
		if (e.placement == 1) return e.entryId;
	}
	const TourneyMatch *final = d.rounds > 0 ? TourneyMatchAt(d, d.rounds, 0) : nullptr;
	return final && TourneyMatchOver(final->status) ? final->winner : 0;
}

std::vector<const TourneyEntrant *> TourneyPlaced(const TourneyDetail &d, int place)
{
	std::vector<const TourneyEntrant *> out;
	for (const TourneyEntrant &e : d.entrants) {
		if (e.placement == place) out.push_back(&e);
	}
	std::sort(out.begin(), out.end(), [](const TourneyEntrant *a, const TourneyEntrant *b) { return (a->seed ? a->seed : 999) < (b->seed ? b->seed : 999); });
	return out;
}

bool TourneyParseNext(const Json &j, TourneyNext &out)
{
	out = TourneyNext();
	if (j.type != Json::Object) return false;
	out.match = Int64(j, "match");
	if (!out.match) return false;
	out.isPrivate = j.GetBool("private");
	out.organizer = TourneyOrganizerFromJson(j.Get("organizer"));
	if (const Json *t = j.Get("tournament"); t && t->type == Json::Object) {
		out.tournamentId = Int64(*t, "id");
		out.slug = Text(*t, "slug");
		out.name = Text(*t, "name");
		out.official = t->GetBool("official");
		out.isPrivate = out.isPrivate || t->GetBool("private");
		if (!out.organizer.Present()) out.organizer = TourneyOrganizerFromJson(t->Get("organizer"));
	}
	out.round = Int(j, "round");
	out.rounds = Int(j, "rounds");
	out.stage = TourneyStageFromId(Text(j, "stage"));
	if (out.stage == TourneyStage::Unknown && out.rounds) out.stage = TourneyStageOf(out.round, out.rounds);
	out.bestOf = std::max(1, Int(j, "bestOf", 1));
	out.game = std::max(1, Int(j, "game", 1));
	out.status = TourneyMatchStatusFromId(Text(j, "status"));
	out.readyAt = Int64(j, "readyAt");
	out.deadlineAt = Int64(j, "deadlineAt");
	out.me = SideFromJson(j.Get("me"));
	const Json *opponent = j.Get("opponent");
	out.hasOpponent = opponent && opponent->type == Json::Object;
	out.opponent = SideFromJson(opponent);
	int a = 0, b = 0;
	if (const Json *score = j.Get("score"); score && score->type == Json::Array && score->arr.size() >= 2) {
		a = static_cast<int>(score->arr[0].num);
		b = static_cast<int>(score->arr[1].num);
	}
	const bool sideB = out.me.side == "b";
	out.scoreMe = sideB ? b : a;
	out.scoreThem = sideB ? a : b;
	out.map = TourneyMapFromJson(j.Get("map"));
	out.contentKey = Text(j, "contentKey");
	out.contentName = Text(j, "contentName");
	out.settings = TourneySettingsFromJson(j.Get("settings"));
	out.lobby = Id64(j.Get("lobby"));
	out.specLobby = Id64(j.Get("specLobby"));
	out.lobbyKey = Text(j, "lobbyKey");
	out.roomClaimBy = Id64(j.Get("roomClaimBy"));
	out.canClaimNoShow = j.GetBool("canClaimNoShow");
	out.present = true;
	return true;
}

bool TourneyParseMine(const Json &j, TourneyMine &out)
{
	out = TourneyMine();
	if (j.type != Json::Object) return false;
	if (const Json *next = j.Get("next"); next && next->type == Json::Object) TourneyParseNext(*next, out.next);
	if (const Json *c = j.Get("checkin"); c && c->type == Json::Object) {
		TourneyCheckin &k = out.checkin;
		if (const Json *t = c->Get("tournament"); t && t->type == Json::Object) {
			k.tournamentId = Int64(*t, "id");
			k.slug = Text(*t, "slug");
			k.name = Text(*t, "name");
		}
		k.opensAt = Int64(*c, "opensAt");
		k.closesAt = Int64(*c, "closesAt");
		k.checkedIn = c->GetBool("checkedIn");
		k.present = k.tournamentId != 0;
	}
	if (const Json *list = j.Get("entries"); list && list->type == Json::Array) {
		for (const Json &e : list->arr) {
			if (e.type != Json::Object) continue;
			TourneyMyEntry x;
			x.tournamentId = Int64(e, "tournamentId");
			x.slug = Text(e, "slug");
			x.name = Text(e, "name");
			x.tournamentStatus = TourneyStatusFromId(Text(e, "tournamentStatus"));
			x.status = TourneyEntryStatusFromId(Text(e, "status"));
			x.startsAt = Int64(e, "startsAt");
			x.checkedIn = e.GetBool("checkedIn");
			x.seed = Int(e, "seed");
			x.placement = Int(e, "placement");
			if (x.tournamentId) out.entries.push_back(x);
		}
	}
	if (const Json *o = j.Get("organize"); o && o->type == Json::Object) {
		TourneyOrganize &g = out.organize;
		g.known = true;
		g.allowed = o->GetBool("allowed");
		g.gamesPlayed = Int(*o, "gamesPlayed");
		g.gamesNeeded = Int(*o, "gamesNeeded");
		if (const Json *a = o->Get("active"); a && a->type == Json::Object) {
			g.activeId = Int64(*a, "id");
			g.activeSlug = Text(*a, "slug");
		}
	}
	if (const Json *list = j.Get("organized"); list && list->type == Json::Array) {
		for (const Json &c : list->arr) {
			if (c.type != Json::Object) continue;
			TourneyOrganized x;
			x.id = Int64(c, "id");
			x.slug = Text(c, "slug");
			x.name = Text(c, "name");
			x.status = TourneyStatusFromId(Text(c, "status"));
			x.isPrivate = c.GetBool("private");
			x.inviteCode = Text(c, "inviteCode");
			x.entries = Int(c, "entries");
			x.checkedIn = Int(c, "checkedIn");
			x.maxEntries = Int(c, "maxEntries");
			x.minEntries = Int(c, "minEntries");
			x.startsAt = Int64(c, "startsAt");
			x.checkinOpensAt = Int64(c, "checkinOpensAt");
			x.openDisputes = Int(c, "openDisputes");
			if (x.id && !x.slug.empty()) out.organized.push_back(x);
		}
	}
	return true;
}

const TourneyMyEntry *TourneyMyEntryOf(const TourneyMine &m, int64_t tournamentId)
{
	for (const TourneyMyEntry &e : m.entries) {
		if (tournamentId && e.tournamentId == tournamentId) return &e;
	}
	return nullptr;
}

const TourneyOrganized *TourneyOrganizedOf(const TourneyMine &m, int64_t tournamentId)
{
	for (const TourneyOrganized &c : m.organized) {
		if (tournamentId && c.id == tournamentId) return &c;
	}
	return nullptr;
}

bool TourneyHasActiveEntry(const TourneyMine &m)
{
	for (const TourneyMyEntry &e : m.entries) {
		if (Active(e.tournamentStatus) && TourneyEntryActive(e.status)) return true;
	}
	for (const TourneyOrganized &c : m.organized) {
		if (Active(c.status)) return true;
	}
	return m.next.present;
}

int TourneyPollEveryMs(bool activeEntry, bool tabOpen)
{
	if (activeEntry) return 30000;
	return tabOpen ? 60000 : 0;
}

bool TourneyParseClaim(const Json &j, TourneyClaim &out)
{
	out = TourneyClaim();
	if (j.type != Json::Object) return false;
	out.lobbyKey = Text(j, "lobbyKey");
	if (j.GetBool("create")) {
		out.kind = TourneyClaim::Create;
		out.map = TourneyMapFromJson(j.Get("map"));
		out.settings = TourneySettingsFromJson(j.Get("settings"));
		out.contentKey = Text(j, "contentKey");
	}
	else if (const uint64_t lobby = Id64(j.Get("join"))) {
		out.kind = TourneyClaim::Join;
		out.lobby = lobby;
	}
	else if (j.GetBool("wait")) {
		out.kind = TourneyClaim::Wait;
		out.by = Id64(j.Get("by"));
	}
	return out.kind != TourneyClaim::None;
}

std::string TourneyRoomTag(int64_t match)
{
	return match > 0 ? "tournament:" + std::to_string(match) : std::string();
}

int64_t TourneyMatchOfTag(const std::string &tag)
{
	static const char kPrefix[] = "tournament:";
	if (tag.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return 0;
	const std::string digits = tag.substr(sizeof(kPrefix) - 1);
	if (digits.empty() || digits.size() > 15 || digits.find_first_not_of("0123456789") != std::string::npos) return 0;
	return _atoi64(digits.c_str());
}

std::vector<TourneyNotice> TourneyNewNotices(const TourneyMine &mine, TourneyTold &told)
{
	std::vector<TourneyNotice> out;
	const TourneyNext &n = mine.next;
	if (n.present && n.status == TourneyMatchStatus::Ready && told.ready.insert(n.match).second && !n.me.inRoom) {
		TourneyNotice x;
		x.kind = TourneyNotice::MatchReady;
		x.tournamentId = n.tournamentId;
		x.match = n.match;
		x.slug = n.slug;
		x.name = n.name;
		x.opponent = n.opponent.name;
		x.stage = n.stage;
		x.round = n.round;
		x.deadlineAt = n.deadlineAt;
		out.push_back(x);
	}
	const TourneyCheckin &c = mine.checkin;
	if (c.present && !c.checkedIn && told.checkin.insert(c.tournamentId).second) {
		TourneyNotice x;
		x.kind = TourneyNotice::CheckinOpen;
		x.tournamentId = c.tournamentId;
		x.slug = c.slug;
		x.name = c.name;
		x.deadlineAt = c.closesAt;
		out.push_back(x);
	}
	for (const TourneyMyEntry &e : mine.entries) {
		if (e.tournamentStatus != TourneyStatus::Finished || !told.finished.insert(e.tournamentId).second || !told.primed) continue;
		TourneyNotice x;
		x.kind = TourneyNotice::Finished;
		x.tournamentId = e.tournamentId;
		x.slug = e.slug;
		x.name = e.name;
		out.push_back(x);
	}
	told.primed = true;
	return out;
}

std::string TourneyClock(int64_t ms)
{
	const int64_t s = std::max<int64_t>(0, (ms + 999) / 1000);
	char buf[32];
	if (s >= 3600) sprintf_s(buf, "%lld:%02lld:%02lld", static_cast<long long>(s / 3600), static_cast<long long>(s / 60 % 60), static_cast<long long>(s % 60));
	else sprintf_s(buf, "%02lld:%02lld", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
	return buf;
}

bool TourneyParseDisputes(const Json &j, std::vector<TourneyDispute> &out)
{
	const Json *list = j.Get("disputes");
	if (!list || list->type != Json::Array) return false;
	out.clear();
	for (const Json &d : list->arr) {
		if (d.type != Json::Object) continue;
		TourneyDispute x;
		x.matchId = Int64(d, "matchId");
		x.text = Text(d, "text");
		x.createdAt = Int64(d, "createdAt");
		const Json *by = d.Get("openedBy");
		if (by && by->type == Json::Object) {
			x.openedBy = Id64(by->Get("steamId"));
			x.openedByName = Text(*by, "name");
		}
		else if (by && by->type == Json::String && !by->str.empty() && by->str.find_first_not_of("0123456789") == std::string::npos) x.openedBy = Id64(by);
		else if (by && by->type == Json::String) x.openedByName = by->str;
		if (x.matchId) out.push_back(x);
	}
	return true;
}

TourneyRefusal TourneyParseRefusal(const Json &j)
{
	TourneyRefusal r;
	if (j.type != Json::Object) return r;
	r.code = Text(j, "error");
	r.message = Text(j, "message");
	r.contentName = Text(j, "contentName");
	r.slug = Text(j, "slug");
	r.until = Int64(j, "until");
	r.gamesPlayed = Int(j, "gamesPlayed");
	r.gamesNeeded = Int(j, "gamesNeeded");
	r.checkedIn = Int(j, "checkedIn");
	r.needed = Int(j, "needed");
	r.retryAfter = std::max(0, Int(j, "retryAfter"));
	r.requirement = Text(j, "kind");
	for (const auto &field : {std::make_pair("have", &r.have), std::make_pair("need", &r.need)}) {
		const Json *v = j.Get(field.first);
		if (v && v->type == Json::Number) *field.second = std::to_string(static_cast<long long>(std::llround(v->num)));
		else if (v && v->type == Json::String) *field.second = v->str;
	}
	r.hash = Lower(Text(j, "hash"));
	return r;
}

bool TourneyParseCreated(const Json &j, TourneyCreated &out)
{
	out = TourneyCreated();
	if (j.type != Json::Object) return false;
	out.id = Int64(j, "id");
	out.slug = Text(j, "slug");
	out.inviteCode = Text(j, "inviteCode");
	return out.id != 0 && !out.slug.empty();
}

std::string TourneyTidyCode(const std::string &typed)
{
	std::string out;
	for (char c : typed) {
		if (c == ' ' || c == '-' || c == '.' || c == '_') continue;
		out += static_cast<char>(toupper(static_cast<unsigned char>(c)));
	}
	return out;
}

bool TourneyCodeValid(const std::string &code)
{
	if (static_cast<int>(code.size()) != kTourneyCodeLength) return false;
	for (char c : code) {
		const bool letter = c >= 'A' && c <= 'Z' && c != 'O' && c != 'I', digit = c >= '2' && c <= '9';
		if (!letter && !digit) return false;
	}
	return true;
}

int TourneyTextLength(const std::string &text)
{
	int n = 0;
	for (unsigned char c : text) {
		// JavaScript counts UTF-16 units: a character past the first 65536 (four bytes in UTF-8) is two of them.
		if ((c & 0xC0) != 0x80) n += c >= 0xF0 ? 2 : 1;
	}
	return n;
}

std::string TourneyTrim(const std::string &text)
{
	const size_t a = text.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	return text.substr(a, text.find_last_not_of(" \t\r\n") - a + 1);
}

int TourneyCupDefaultMinimum(int places)
{
	return std::max(2, std::min(4, places));
}

int TourneyRoundsFor(int places)
{
	int rounds = 0;
	while ((1 << rounds) < places && rounds < 16) rounds++;
	return rounds;
}

std::vector<TourneyCupProblem> TourneyCheckCup(const TourneyCupForm &f, int64_t nowMs)
{
	std::vector<TourneyCupProblem> out;
	const int name = TourneyTextLength(TourneyTrim(f.name));
	if (name < kTourneyCupNameMin) out.push_back(TourneyCupProblem::NameShort);
	else if (name > kTourneyCupNameMax) out.push_back(TourneyCupProblem::NameLong);
	if (TourneyTextLength(TourneyTrim(f.description)) > kTourneyCupDescriptionMax) out.push_back(TourneyCupProblem::DescriptionLong);
	const bool places = f.maxEntries == 4 || f.maxEntries == 8 || f.maxEntries == 16;
	if (!places) out.push_back(TourneyCupProblem::Places);
	else if (f.minEntries < 2 || f.minEntries > f.maxEntries) out.push_back(TourneyCupProblem::Minimum);
	if ((f.bestOf != 1 && f.bestOf != 3) || (f.finalBestOf != 1 && f.finalBestOf != 3 && f.finalBestOf != 5)) out.push_back(TourneyCupProblem::Series);
	if (!TourneyPresetValid(f.settings.preset) || !TourneySpecDelayValid(f.settings.specDelay)) out.push_back(TourneyCupProblem::Settings);
	if (f.maps.empty()) out.push_back(TourneyCupProblem::NoMaps);
	else if (static_cast<int>(f.maps.size()) > kTourneyCupMapsMax) out.push_back(TourneyCupProblem::TooManyMaps);
	else {
		bool bad = false, large = false;
		for (const TourneyMap &m : f.maps) {
			if (!m.Scenario()) {
				bad = bad || !LeagueMapValid(m.name, m.size);
				continue;
			}
			bad = bad || !TourneyScenarioFileValid(m.file) || !TourneySha256Valid(m.hash) || m.bytes <= 0 || m.players < 2;
			large = large || m.bytes > kTourneyScenarioMaxBytes;
		}
		if (bad) out.push_back(TourneyCupProblem::BadMap);
		else if (large) out.push_back(TourneyCupProblem::MapTooLarge);
	}
	if (f.startsAt < nowMs + kTourneyCupLeadMinMs) out.push_back(TourneyCupProblem::StartSoon);
	else if (f.startsAt > nowMs + kTourneyCupLeadMaxMs) out.push_back(TourneyCupProblem::StartLate);
	if (f.checkinMinutes != 15 && f.checkinMinutes != 30) out.push_back(TourneyCupProblem::Checkin);
	if (f.noshowMinutes != 10 && f.noshowMinutes != 15 && f.noshowMinutes != 20) out.push_back(TourneyCupProblem::Noshow);
	const TourneyRequirements &r = f.requirements;
	if (r.minGames < 0 || r.minGames > kTourneyMinGamesMax || (r.maxTier != LeagueTier::None && !TourneyMaxTierValid(r.maxTier))) out.push_back(TourneyCupProblem::Requirements);
	return out;
}

Json TourneyCupJson(const TourneyCupForm &f, bool edit)
{
	Json body = Json::MakeObject();
	body.Set("name", Json::MakeString(TourneyTrim(f.name)));
	const std::string description = TourneyTrim(f.description);
	if (!description.empty() || edit) body.Set("description", Json::MakeString(description));
	body.Set("maxEntries", Json::MakeNumber(f.maxEntries));
	body.Set("minEntries", Json::MakeNumber(f.minEntries));
	body.Set("bestOf", Json::MakeNumber(f.bestOf));
	body.Set("finalBestOf", Json::MakeNumber(f.finalBestOf));
	body.Set("seeding", Json::MakeString(f.randomSeeding ? "random" : "rating"));
	Json maps = Json::MakeArray();
	for (const TourneyMap &m : f.maps) maps.arr.push_back(TourneyMapJson(m));
	body.Set("maps", maps);
	body.Set("startsAt", Json::MakeNumber(static_cast<double>(f.startsAt)));
	body.Set("checkinMinutes", Json::MakeNumber(f.checkinMinutes));
	body.Set("noshowMinutes", Json::MakeNumber(f.noshowMinutes));
	body.Set("settings", TourneySettingsJson(f.settings));
	body.Set("requirements", TourneyRequirementsJson(f.requirements));
	body.Set("private", Json::MakeBool(f.isPrivate));
	body.Set("play", Json::MakeBool(f.play));
	if (!f.contentKey.empty()) {
		body.Set("contentKey", Json::MakeString(f.contentKey));
		if (!f.contentName.empty() || edit) body.Set("contentName", f.contentName.empty() ? Json() : Json::MakeString(f.contentName));
	}
	else if (edit) {
		body.Set("contentKey", Json());
		body.Set("contentName", Json());
	}
	return body;
}

Json TourneyCupDescriptionJson(const std::string &description)
{
	Json body = Json::MakeObject();
	body.Set("description", Json::MakeString(TourneyTrim(description)));
	return body;
}

void TourneyFitMaps(std::vector<TourneyMap> &maps, int rounds)
{
	rounds = std::max(1, std::min(rounds, kTourneyCupMapsMax));
	if (maps.empty()) maps.push_back({"mediterranean", "random", "medium"});
	while (static_cast<int>(maps.size()) < rounds) maps.push_back(maps.back());
	maps.resize(static_cast<size_t>(rounds));
}

TourneyCupForm TourneyCupFormOf(const TourneyDetail &d)
{
	TourneyCupForm f;
	f.name = d.name;
	f.description = d.description;
	f.maxEntries = d.maxEntries;
	f.minEntries = d.minEntries;
	f.bestOf = d.bestOf;
	f.finalBestOf = d.finalBestOf;
	f.randomSeeding = Lower(d.seeding) == "random";
	f.maps = d.maps;
	TourneyFitMaps(f.maps, TourneyRoundsFor(d.maxEntries));
	f.startsAt = d.startsAt;
	const int64_t checkin = d.checkinOpensAt && d.startsAt > d.checkinOpensAt ? (d.startsAt - d.checkinOpensAt + 30000) / 60000 : 30;
	f.checkinMinutes = checkin <= 22 ? 15 : 30;
	f.noshowMinutes = d.noshowMinutes == 10 || d.noshowMinutes == 20 ? d.noshowMinutes : 15;
	f.isPrivate = d.isPrivate;
	const TourneyEntrant *own = TourneyEntrantBySteamId(d, d.organizer.steamId);
	f.play = own && TourneyEntryActive(own->status);
	f.contentKey = d.contentKey;
	f.contentName = d.contentName;
	TourneySettings s = d.settings;
	if (!TourneyPresetValid(s.preset)) s.preset = TourneyPresetId(TourneyPreset::Competitive);
	if (!TourneySpecDelayValid(TourneySpecDelay(s))) s.specDelay = 120;
	f.settings = TourneyExplicitSettings(s);
	f.requirements = d.requirements;
	return f;
}

bool TourneyCupLocked(const TourneyDetail &d)
{
	for (const TourneyEntrant &e : d.entrants) {
		if (e.steamId && e.steamId == d.organizer.steamId) continue;
		if (TourneyEntryActive(e.status) || e.status == TourneyEntryStatus::Out) return true;
	}
	return false;
}

TourneyEstimate TourneyEstimateFor(int places, int bestOf, int finalBestOf)
{
	TourneyEstimate e;
	if (places < 2) return e;
	bestOf = std::max(1, bestOf);
	finalBestOf = std::max(1, finalBestOf);
	e.rounds = TourneyRoundsFor(places);
	e.series = places - 1;
	e.maxGames = (places - 2) * bestOf + finalBestOf;
	e.minutes = 40 * ((e.rounds - 1) * bestOf + finalBestOf) + 10 * (e.rounds - 1);
	return e;
}

int64_t TourneyDayOf(int64_t localMs)
{
	const int64_t day = 86400000LL;
	return localMs >= 0 ? localMs / day : -((-localMs + day - 1) / day);
}

int TourneyWeekdayOf(int64_t day)
{
	// 1 January 1970 was a Thursday.
	return static_cast<int>(((day + 3) % 7 + 7) % 7);
}

void TourneyCivilDate(int64_t day, int &year, int &month, int &dayOfMonth)
{
	// Howard Hinnant's civil_from_days.
	const int64_t z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
	const int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	dayOfMonth = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
	month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
	year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

TourneyCalendar TourneyCalendarOf(int64_t firstDay, int64_t lastDay)
{
	TourneyCalendar c;
	c.first = firstDay - TourneyWeekdayOf(firstDay);
	c.rows = static_cast<int>((std::max(lastDay, firstDay) - c.first) / 7 + 1);
	return c;
}

int64_t TourneySnapStart(int64_t wantMs, int64_t minMs, int64_t maxMs)
{
	const int64_t q = kTourneyQuarterMs;
	const auto up = [q](int64_t v) { return v >= 0 ? (v + q - 1) / q * q : -(-v / q * q); };
	const auto down = [q](int64_t v) { return v >= 0 ? v / q * q : -((-v + q - 1) / q * q); };
	const int64_t lo = up(minMs), hi = std::max(lo, down(maxMs));
	return std::max(lo, std::min(hi, up(wantMs)));
}

bool TourneyDayOpen(int64_t day, int64_t minMs, int64_t maxMs)
{
	const int64_t start = day * 86400000LL, end = start + 86400000LL - kTourneyQuarterMs;
	const int64_t lo = TourneySnapStart(minMs, minMs, maxMs), hi = TourneySnapStart(maxMs, minMs, maxMs);
	return lo <= end && hi >= start;
}

int64_t TourneyOnDay(int64_t localMs, int64_t day, int64_t minMs, int64_t maxMs)
{
	const int64_t timeOfDay = localMs - TourneyDayOf(localMs) * 86400000LL;
	return TourneySnapStart(day * 86400000LL + timeOfDay, minMs, maxMs);
}

int64_t TourneyAtTime(int64_t localMs, int hour, int minute, int64_t minMs, int64_t maxMs)
{
	hour = std::max(0, std::min(23, hour));
	minute = std::max(0, std::min(59, minute));
	return TourneySnapStart(TourneyDayOf(localMs) * 86400000LL + (hour * 60LL + minute) * 60000LL, minMs, maxMs);
}

int TourneyBoxX(const TourneyBracketMetrics &m, int round)
{
	return (std::max(1, round) - 1) * m.colPitch;
}

int TourneyBoxY(const TourneyBracketMetrics &m, int round, int position)
{
	// A round's boxes are 2^(round-1) first-round rows apart, the first one halfway down its feeders.
	const int span = 1 << std::max(0, std::min(round, 16) - 1);
	return m.top + (span * position) * m.rowPitch + (span - 1) * m.rowPitch / 2;
}

int TourneyBoxMidY(const TourneyBracketMetrics &m, int round, int position)
{
	return TourneyBoxY(m, round, position) + m.boxH / 2;
}

std::vector<TourneySegment> TourneyConnector(const TourneyBracketMetrics &m, int round, int position, int rounds)
{
	std::vector<TourneySegment> out;
	if (round < 1 || round >= rounds) return out;
	const int x = TourneyBoxX(m, round) + m.boxW, nextX = TourneyBoxX(m, round + 1), joint = x + (nextX - x) / 2;
	const int y = TourneyBoxMidY(m, round, position), nextY = TourneyBoxMidY(m, round + 1, position / 2);
	out.push_back({x, y, joint, y});
	out.push_back({joint, y, joint, nextY});
	out.push_back({joint, nextY, nextX, nextY});
	return out;
}

int TourneyBracketWidth(const TourneyBracketMetrics &m, int rounds)
{
	return rounds > 0 ? TourneyBoxX(m, rounds) + m.boxW : 0;
}

int TourneyBracketHeight(const TourneyBracketMetrics &m, int rounds)
{
	if (rounds <= 0) return 0;
	const int first = 1 << std::min(rounds - 1, 16);
	return TourneyBoxY(m, 1, first - 1) + m.boxH;
}
