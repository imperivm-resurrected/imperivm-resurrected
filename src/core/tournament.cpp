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
#include "i18n/tr.h"

namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
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

// A team without a name of its own: the league names it after its captain and its partner, "Varo & Tulia", which is said in the
// player's language, "Varo y Tulia". Any other name is the team's own.
std::string TeamNameOf(const std::string &name, const std::string &captain, const std::string &partner)
{
	const std::string a = captain.empty() ? "?" : captain, b = partner.empty() ? "?" : partner;
	return name == a + " & " + b ? TrFmt("%s and %s", {a, b}) : name;
}

std::string TeamNameOf(const std::string &name, const std::vector<TourneyMember> &members)
{
	return members.size() == 2 ? TeamNameOf(name, members[0].name, members[1].name) : name;
}

TourneyCard CardFromJson(const Json *v)
{
	TourneyCard c;
	if (!v || v->type != Json::Object) return c;
	c.entryId = Int64(*v, "entryId");
	c.steamId = Id64(v->Get("steamId"));
	c.name = Text(*v, "name");
	c.seed = v->GetInt("seed");
	c.members = TourneyMembersFromJson(v->Get("members"));
	c.name = TeamNameOf(c.name, c.members);
	return c;
}

TourneyMember MemberFromJson(const Json *v)
{
	TourneyMember m;
	if (!v || v->type != Json::Object) return m;
	m.steamId = Id64(v->Get("steamId"));
	if (!m.steamId) m.steamId = Id64(v->Get("steamid"));
	m.name = Text(*v, "name");
	if (const Json *in = v->Get("inRoom"); in && in->type == Json::Bool) m.inRoom = in->b ? 1 : 0;
	return m;
}

std::vector<TourneyMember> PlayersOf(uint64_t steamId, const std::string &name, const std::vector<TourneyMember> &members)
{
	if (!members.empty()) return members;
	std::vector<TourneyMember> out;
	if (steamId || !name.empty()) out.push_back({steamId, name, -1});
	return out;
}

bool Active(TourneyStatus s)
{
	return s == TourneyStatus::Signup || s == TourneyStatus::Checkin || s == TourneyStatus::Running;
}

std::vector<TourneyMap> MapsFromJson(const Json *v)
{
	std::vector<TourneyMap> out;
	if (!v || v->type != Json::Array) return out;
	for (const Json &m : v->arr) out.push_back(TourneyMapFromJson(&m));
	return out;
}

// "a" or "b"; anything else is nobody.
std::string SideOf(const Json &j, const char *key)
{
	const std::string s = Lower(Text(j, key));
	return s == "a" || s == "b" ? s : std::string();
}

TourneyNextSide SideFromJson(const Json *v)
{
	TourneyNextSide s;
	if (!v || v->type != Json::Object) return s;
	s.steamId = Id64(v->Get("steamId"));
	if (!s.steamId) s.steamId = Id64(v->Get("steamid"));
	s.name = Text(*v, "name");
	s.side = Text(*v, "side");
	s.seed = v->GetInt("seed");
	s.inRoom = v->GetBool("inRoom");
	s.members = TourneyMembersFromJson(v->Get("members"));
	s.name = TeamNameOf(s.name, s.members);
	return s;
}

// The player's team in the next match: {entryId, name, captain, members} as a side, its members alone, or its name alone.
TourneyNextSide TeamFromJson(const Json *v)
{
	TourneyNextSide s;
	if (!v) return s;
	if (v->type == Json::Object) {
		s = SideFromJson(v);
		if (!s.steamId) s.steamId = Id64(v->Get("captain"));
		return s;
	}
	if (v->type == Json::Array) s.members = TourneyMembersFromJson(v);
	else if (v->type == Json::String) s.name = v->str;
	return s;
}

std::vector<std::string> Strings(const Json *v)
{
	std::vector<std::string> out;
	if (!v || v->type != Json::Array) return out;
	for (const Json &s : v->arr) {
		if (s.type == Json::String && !s.str.empty()) out.push_back(Lower(s.str));
	}
	return out;
}

bool Contains(const std::vector<std::string> &list, const std::string &s)
{
	return std::find(list.begin(), list.end(), s) != list.end();
}

}

TourneyMode TourneyModeFromId(const std::string &id)
{
	return Lower(id) == "2v2" ? TourneyMode::Teams : TourneyMode::Solo;
}

const char *TourneyModeId(TourneyMode m)
{
	return m == TourneyMode::Teams ? "2v2" : "1v1";
}

int TourneySidePlayers(TourneyMode m)
{
	return m == TourneyMode::Teams ? 2 : 1;
}

int TourneyGameMinutes(TourneyMode m)
{
	return m == TourneyMode::Teams ? 45 : 40;
}

std::vector<TourneyMember> TourneyMembersFromJson(const Json *v)
{
	std::vector<TourneyMember> out;
	if (!v || v->type != Json::Array) return out;
	for (const Json &m : v->arr) {
		const TourneyMember x = MemberFromJson(&m);
		if (x.steamId && !TourneyMemberOf(out, x.steamId)) out.push_back(x);
	}
	return out;
}

const TourneyMember *TourneyMemberOf(const std::vector<TourneyMember> &members, uint64_t steamId)
{
	for (const TourneyMember &m : members) {
		if (steamId && m.steamId == steamId) return &m;
	}
	return nullptr;
}

bool TourneyNameIsMembers(const std::string &name, const std::vector<TourneyMember> &members)
{
	if (members.size() < 2 || name.empty()) return false;
	for (const TourneyMember &m : members) {
		if (m.name.empty() || name.find(m.name) == std::string::npos) return false;
	}
	return true;
}

std::vector<TourneyMember> TourneyPlayersOf(const TourneyEntrant &e)
{
	return PlayersOf(e.steamId, e.name, e.members);
}

std::vector<TourneyMember> TourneyPlayersOf(const TourneyCard &c)
{
	return PlayersOf(c.steamId, c.name, c.members);
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
		{"disqualified", TourneyEntryStatus::Disqualified}, {"missed_checkin", TourneyEntryStatus::MissedCheckin}, {"pending_partner", TourneyEntryStatus::PendingPartner}};
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

TourneyBracket TourneyBracketFromId(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "losers") return TourneyBracket::Losers;
	if (s == "final") return TourneyBracket::Final;
	if (s == "reset") return TourneyBracket::Reset;
	if (s == "swiss") return TourneyBracket::Swiss;
	return TourneyBracket::Winners;
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
		m.players = v->GetInt("players");
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
	s.fog = v->GetFlag("fog");
	s.explored = v->GetFlag("explored");
	s.timers = v->GetFlag("timers");
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
	if (s.timers >= 0) j.Set("timers", Json::MakeBool(s.timers != 0));
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
	if (s.timers >= 0) r.timers = s.timers != 0;
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
	return seconds == 0 || seconds == 30 || seconds == 60 || seconds == 120 || seconds == 300;
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
	out.timers = r.timers ? 1 : 0;
	out.specDelay = TourneySpecDelay(s);
	return out;
}

TourneyRequirements TourneyRequirementsFromJson(const Json *v)
{
	TourneyRequirements r;
	if (!v || v->type != Json::Object) return r;
	r.minGames = std::max(0, v->GetInt("minGames"));
	const Json *tier = v->Get("maxTier");
	r.maxTier = tier && tier->type == Json::String ? LeagueTierFromId(tier->str) : LeagueTier::None;
	if (!TourneyMaxTierValid(r.maxTier)) r.maxTier = LeagueTier::None;
	r.maxPlace = std::max(0, v->GetInt("maxPlace"));
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

TourneyFormat TourneyFormatFromId(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "double") return TourneyFormat::Double;
	if (s == "swiss") return TourneyFormat::Swiss;
	return TourneyFormat::Single;
}

const char *TourneyFormatId(TourneyFormat f)
{
	switch (f) {
	case TourneyFormat::Double: return "double";
	case TourneyFormat::Swiss: return "swiss";
	default: return "single";
	}
}

TourneyPace TourneyPaceFromId(const std::string &id)
{
	return Lower(id) == "rounds" ? TourneyPace::Rounds : TourneyPace::Live;
}

const char *TourneyPaceId(TourneyPace p)
{
	return p == TourneyPace::Rounds ? "rounds" : "live";
}

bool TourneyRoundDaysValid(int days)
{
	return days == 1 || days == 2 || days == 3 || days == 7;
}

int TourneySwissRounds(int chosen, int players)
{
	if (chosen >= kTourneySwissRoundsMin && chosen <= kTourneySwissRoundsMax) return chosen;
	int rounds = 0;
	while ((1 << rounds) < players && rounds < 16) rounds++;
	return std::max(1, rounds);
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
	out.maxEntries = j.GetInt("maxEntries");
	out.minEntries = j.GetInt("minEntries");
	// The page lists the entries instead of counting them; TourneyParseDetail counts them.
	out.entries = j.GetInt("entries");
	out.bestOf = std::max(1, j.GetInt("bestOf", 1));
	out.finalBestOf = std::max(1, j.GetInt("finalBestOf", out.bestOf));
	out.seeding = Text(j, "seeding");
	out.contentKey = Text(j, "contentKey");
	out.contentName = Text(j, "contentName");
	out.mods = LeagueParseModsRule(j.Get("mods"));
	out.settings = TourneySettingsFromJson(j.Get("settings"));
	out.requirements = TourneyRequirementsFromJson(j.Get("requirements"));
	out.maps = MapsFromJson(j.Get("maps"));
	out.signupOpensAt = Int64(j, "signupOpensAt");
	out.checkinOpensAt = Int64(j, "checkinOpensAt");
	out.startsAt = Int64(j, "startsAt");
	out.noshowMinutes = j.GetInt("noshowMinutes");
	out.swissRounds = std::max(0, j.GetInt("swissRounds"));
	out.pace = TourneyPaceFromId(Text(j, "pace"));
	out.roundDays = std::max(0, j.GetInt("roundDays"));
	out.seriesPool = MapsFromJson(j.Get("seriesPool"));
	out.currentRound = std::max(0, j.GetInt("currentRound"));
	out.roundEndsAt = Int64(j, "roundEndsAt");
	out.mode = TourneyModeFromId(Text(j, "mode"));
	out.teamDraw = out.Teams() && j.GetBool("teamDraw");
	return true;
}

TourneyFormat TourneyFormatOf(const TourneySummary &t)
{
	return TourneyFormatFromId(t.format);
}

int TourneyRoundCount(const TourneySummary &t)
{
	const int players = t.entries > 1 ? t.entries : t.maxEntries;
	switch (TourneyFormatOf(t)) {
	case TourneyFormat::Swiss: return TourneySwissRounds(t.swissRounds, players);
	case TourneyFormat::Double: return 0;
	default: return players > 1 ? TourneyRoundsFor(players) : 0;
	}
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
	return TourneyRoundsFor(maxEntries);
}

TourneyMap TourneyMapOfRound(const TourneySummary &t, int round, int rounds)
{
	if (t.maps.empty()) return TourneyMap();
	const int count = static_cast<int>(t.maps.size());
	// A list that covers every round is read from the final backwards, as the server does.
	if (rounds > 0 && count >= rounds) return t.maps[static_cast<size_t>(std::max(0, std::min(count - 1, count - rounds + round - 1)))];
	return t.maps[static_cast<size_t>(std::max(1, std::min(round, count))) - 1];
}

std::vector<TourneyGame> TourneyGamesFromJson(const Json *v)
{
	std::vector<TourneyGame> out;
	if (!v || v->type != Json::Array) return out;
	for (const Json &g : v->arr) {
		if (g.type != Json::Object) continue;
		TourneyGame x;
		x.no = g.GetInt("no");
		if (x.no < 1) continue;
		x.map = TourneyMapFromJson(g.Get("map"));
		x.winner = SideOf(g, "winner");
		out.push_back(x);
	}
	std::sort(out.begin(), out.end(), [](const TourneyGame &a, const TourneyGame &b) { return a.no < b.no; });
	return out;
}

TourneyPick TourneyPickFromJson(const Json *v)
{
	TourneyPick p;
	if (!v || v->type != Json::Object) return p;
	p.by = SideOf(*v, "by");
	p.options = MapsFromJson(v->Get("options"));
	p.until = Int64(*v, "until");
	p.present = !p.by.empty() && !p.options.empty();
	return p;
}

const TourneyGame *TourneyGameOf(const std::vector<TourneyGame> &games, int no)
{
	for (const TourneyGame &g : games) {
		if (g.no == no) return &g;
	}
	return nullptr;
}

void TourneySeriesScore(const std::vector<TourneyGame> &games, int &a, int &b)
{
	a = b = 0;
	for (const TourneyGame &g : games) {
		if (g.winner == "a") a++;
		else if (g.winner == "b") b++;
	}
}

bool TourneySeriesOver(const std::vector<TourneyGame> &games, int bestOf)
{
	int a = 0, b = 0;
	TourneySeriesScore(games, a, b);
	const int need = std::max(1, bestOf) / 2 + 1;
	return a >= need || b >= need;
}

bool TourneyParseDetail(const Json &j, TourneyDetail &out)
{
	out = TourneyDetail();
	if (!TourneyParseSummary(j, out)) return false;
	out.rounds = j.GetInt("rounds");
	out.testBots = j.GetBool("testBots");
	const int sentRounds = out.rounds;
	int counted = 0;
	if (const Json *list = j.Get("entries"); list && list->type == Json::Array) {
		for (const Json &e : list->arr) {
			if (e.type != Json::Object) continue;
			TourneyEntrant x;
			x.entryId = Int64(e, "entryId");
			x.steamId = Id64(e.Get("steamId"));
			x.name = Text(e, "name");
			x.status = TourneyEntryStatusFromId(Text(e, "status"));
			x.seed = e.GetInt("seed");
			x.placement = e.GetInt("placement");
			x.checkedIn = e.GetBool("checkedIn");
			x.members = TourneyMembersFromJson(e.Get("members"));
			x.teamName = Text(e, "teamName");
			x.partner = MemberFromJson(e.Get("partner"));
			// A team waiting for its partner lists only its captain: the partner is apart.
			if (x.teamName.empty() && x.members.size() == 2) x.name = TeamNameOf(x.name, x.members);
			else if (x.teamName.empty() && x.partner.steamId) {
				const TourneyMember *captain = TourneyMemberOf(x.members, x.steamId);
				x.name = TeamNameOf(x.name, captain ? captain->name : std::string(), x.partner.name);
			}
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
			x.round = m.GetInt("round");
			x.position = m.GetInt("position");
			x.stage = TourneyStageFromId(Text(m, "stage"));
			x.bracket = TourneyBracketFromId(Text(m, "bracket"));
			x.bestOf = std::max(1, m.GetInt("bestOf", 1));
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
			x.games = TourneyGamesFromJson(m.Get("games"));
			x.pick = TourneyPickFromJson(m.Get("pick"));
			x.noshowFrom = Int64(m, "noshowFrom");
			out.rounds = std::max(out.rounds, x.round);
			out.matches.push_back(x);
		}
	}
	const bool swiss = TourneyIsSwiss(out), twice = TourneyIsDouble(out);
	int winners = 0;
	for (TourneyMatch &m : out.matches) {
		// A Swiss tournament has nothing but rounds, whatever its matches say.
		if (swiss) m.bracket = TourneyBracket::Swiss;
		if (m.bracket == TourneyBracket::Winners) winners = std::max(winners, m.round);
		if (m.bracket == TourneyBracket::Losers) out.losersRounds = std::max(out.losersRounds, m.round);
	}
	// A double elimination's rounds are its winners' bracket's; the other parts count their own.
	if (twice && winners) out.rounds = winners;
	if (twice) out.losersRounds = std::max(out.losersRounds, TourneyLosersRounds(out.rounds));
	if (swiss && !sentRounds) out.rounds = std::max(out.rounds, out.swissRounds ? out.swissRounds : TourneySwissRounds(0, out.entries));
	for (TourneyMatch &m : out.matches) {
		if (m.stage == TourneyStage::Unknown && m.bracket == TourneyBracket::Winners) m.stage = TourneyStageOf(m.round, out.rounds);
	}
	if (const Json *list = j.Get("standings"); list && list->type == Json::Array) {
		const auto whole = [](const Json &v, const char *key) {
			const Json *n = v.Get(key);
			return n && n->type == Json::Number ? static_cast<int>(std::llround(n->num)) : 0;
		};
		for (const Json &s : list->arr) {
			if (s.type != Json::Object) continue;
			TourneyStanding x;
			x.entryId = Int64(s, "entryId");
			x.steamId = Id64(s.Get("steamId"));
			x.members = TourneyMembersFromJson(s.Get("members"));
			x.name = TeamNameOf(Text(s, "name"), x.members);
			if (const TourneyEntrant *e = TourneyEntrantOf(out, x.entryId)) {
				if (x.members.empty()) x.members = e->members;
				x.name = x.name.empty() ? e->name : TeamNameOf(x.name, x.members);
				if (!x.steamId) x.steamId = e->steamId;
			}
			x.points = whole(s, "points");
			x.wins = whole(s, "wins");
			x.losses = whole(s, "losses");
			x.buchholz = whole(s, "buchholz");
			x.byes = whole(s, "byes");
			x.place = std::max(0, whole(s, "place"));
			if (x.entryId || !x.name.empty()) out.standings.push_back(x);
		}
		std::stable_sort(out.standings.begin(), out.standings.end(), [](const TourneyStanding &a, const TourneyStanding &b) {
			if ((a.place > 0) != (b.place > 0)) return a.place > 0;
			if (a.place != b.place) return a.place < b.place;
			return a.points > b.points;
		});
	}
	if (const Json *list = j.Get("log"); list && list->type == Json::Array) {
		for (const Json &l : list->arr) {
			if (l.type != Json::Object) continue;
			out.log.push_back({Int64(l, "at"), Text(l, "actor"), Text(l, "action"), Text(l, "detail")});
		}
	}
	return true;
}

const TourneyMatch *TourneyMatchAt(const TourneyDetail &d, int round, int position, TourneyBracket bracket)
{
	for (const TourneyMatch &m : d.matches) {
		if (m.round == round && m.position == position && m.bracket == bracket) return &m;
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
	if (!steamId) return nullptr;
	// An entry still in the cup first: one that withdrew, or a team that was taken off, may name the player too.
	const TourneyEntrant *any = nullptr;
	for (const TourneyEntrant &e : d.entrants) {
		if (e.steamId != steamId && !TourneyMemberOf(e.members, steamId)) continue;
		if (TourneyEntryActive(e.status) || e.status == TourneyEntryStatus::Out) return &e;
		if (!any) any = &e;
	}
	return any;
}

std::vector<const TourneyMatch *> TourneyPathOf(const TourneyDetail &d, int64_t entryId)
{
	std::vector<const TourneyMatch *> out;
	for (const TourneyMatch &m : d.matches) {
		if (m.Has(entryId)) out.push_back(&m);
	}
	// A player only drops to the losers' bracket after their last match in the winners', and the finals come after both.
	const auto part = [](TourneyBracket b) { return b == TourneyBracket::Losers ? 1 : b == TourneyBracket::Final ? 2 : b == TourneyBracket::Reset ? 3 : 0; };
	std::sort(out.begin(), out.end(), [&part](const TourneyMatch *a, const TourneyMatch *b) {
		if (part(a->bracket) != part(b->bracket)) return part(a->bracket) < part(b->bracket);
		return a->round < b->round;
	});
	return out;
}

void TourneyProgress(const TourneyDetail &d, int &done, int &total)
{
	done = total = 0;
	int paired = 0;
	for (const TourneyMatch &m : d.matches) {
		paired = std::max(paired, m.round);
		if (m.status == TourneyMatchStatus::Bye) continue;
		total++;
		if (TourneyMatchOver(m.status)) done++;
	}
	if (TourneyIsSwiss(d) && !d.matches.empty()) total += std::max(0, TourneySwissTotalRounds(d) - paired) * (d.entries / 2);
}

int64_t TourneyChampion(const TourneyDetail &d)
{
	for (const TourneyEntrant &e : d.entrants) {
		if (e.placement == 1) return e.entryId;
	}
	if (TourneyIsSwiss(d)) {
		if (d.status != TourneyStatus::Finished) return 0;
		for (const TourneyStanding &s : d.standings) {
			if (s.place == 1) return s.entryId;
		}
		return 0;
	}
	const TourneyMatch *decider = TourneyDecider(d);
	if (!decider || !TourneyMatchOver(decider->status) || TourneyResetDue(d)) return 0;
	return decider->winner;
}

std::vector<const TourneyEntrant *> TourneyPlaced(const TourneyDetail &d, int place)
{
	std::vector<const TourneyEntrant *> out;
	for (const TourneyEntrant &e : d.entrants) {
		if (e.placement == place) out.push_back(&e);
	}
	if (out.empty() && TourneyIsSwiss(d) && d.status == TourneyStatus::Finished) {
		for (const TourneyStanding &s : d.standings) {
			const TourneyEntrant *e = TourneyEntrantOf(d, s.entryId);
			if (s.place == place && e && !e->placement) out.push_back(e);
		}
	}
	std::sort(out.begin(), out.end(), [](const TourneyEntrant *a, const TourneyEntrant *b) { return (a->seed ? a->seed : 999) < (b->seed ? b->seed : 999); });
	return out;
}

bool TourneyIsDouble(const TourneySummary &t)
{
	return Lower(t.format) == "double";
}

bool TourneyIsSwiss(const TourneySummary &t)
{
	return Lower(t.format) == "swiss";
}

bool TourneyByRounds(const TourneyDetail &d)
{
	return d.pace == TourneyPace::Rounds;
}

int TourneySwissTotalRounds(const TourneyDetail &d)
{
	if (d.rounds > 0) return d.rounds;
	return d.swissRounds > 0 ? d.swissRounds : TourneySwissRounds(0, d.entries);
}

int TourneyCurrentRound(const TourneyDetail &d)
{
	if (d.currentRound > 0) return d.currentRound;
	int round = 0;
	for (const TourneyMatch &m : d.matches) {
		if (m.bracket == TourneyBracket::Swiss) round = std::max(round, m.round);
	}
	return round;
}

std::vector<const TourneyMatch *> TourneyRoundMatches(const TourneyDetail &d, int round, TourneyBracket bracket)
{
	std::vector<const TourneyMatch *> out;
	for (const TourneyMatch &m : d.matches) {
		if (m.round == round && m.bracket == bracket) out.push_back(&m);
	}
	std::sort(out.begin(), out.end(), [](const TourneyMatch *a, const TourneyMatch *b) { return a->position < b->position; });
	return out;
}

int TourneySwissPointsBefore(const TourneyDetail &d, int64_t entryId, int round)
{
	int points = 0;
	for (const TourneyMatch &m : d.matches) {
		if (m.bracket == TourneyBracket::Swiss && m.round < round && m.Has(entryId) && TourneyMatchOver(m.status) && m.winner == entryId) points++;
	}
	return points;
}

const TourneyStanding *TourneyStandingOf(const TourneyDetail &d, int64_t entryId)
{
	for (const TourneyStanding &s : d.standings) {
		if (entryId && s.entryId == entryId) return &s;
	}
	return nullptr;
}

const TourneyMatch *TourneyDecider(const TourneyDetail &d)
{
	if (TourneyIsSwiss(d)) return nullptr;
	if (!TourneyIsDouble(d)) return d.rounds > 0 ? TourneyMatchAt(d, d.rounds, 0) : nullptr;
	const TourneyMatch *final = nullptr;
	for (const TourneyMatch &m : d.matches) {
		if (m.bracket == TourneyBracket::Reset) return &m;
		if (m.bracket == TourneyBracket::Final && !final) final = &m;
	}
	return final;
}

bool TourneyResetDue(const TourneyDetail &d)
{
	if (!TourneyIsDouble(d) || d.status == TourneyStatus::Finished) return false;
	const TourneyMatch *final = TourneyDecider(d);
	if (!final || final->bracket != TourneyBracket::Final || !TourneyMatchOver(final->status) || !final->winner) return false;
	const TourneyMatch *winners = d.rounds > 0 ? TourneyMatchAt(d, d.rounds, 0) : nullptr;
	return winners && TourneyMatchOver(winners->status) && winners->winner && winners->winner != final->winner;
}

int64_t TourneyFinishedAt(const TourneyDetail &d)
{
	int64_t at = 0;
	for (const TourneyMatch &m : d.matches) {
		if (m.status != TourneyMatchStatus::Bye) at = std::max(at, m.finishedAt);
	}
	return at;
}

int TourneyDoublePlaceEnd(int placement)
{
	if (placement <= 4) return placement;
	// Two losers' rounds in a row knock out as many players, and each pair of rounds half as many as the one before it.
	int start = 5, size = 2;
	for (int i = 0; start <= 1024; i++) {
		if (placement < start + size) return start + size - 1;
		start += size;
		if (i % 2) size *= 2;
	}
	return placement;
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
	out.round = j.GetInt("round");
	out.rounds = j.GetInt("rounds");
	out.stage = TourneyStageFromId(Text(j, "stage"));
	out.bracket = TourneyBracketFromId(Text(j, "bracket"));
	// Only the winners' bracket has stages counted from its final.
	if (out.stage == TourneyStage::Unknown && out.rounds && out.bracket == TourneyBracket::Winners) out.stage = TourneyStageOf(out.round, out.rounds);
	out.bestOf = std::max(1, j.GetInt("bestOf", 1));
	out.game = std::max(1, j.GetInt("game", 1));
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
	out.mods = LeagueParseModsRule(j.Get("mods"));
	out.settings = TourneySettingsFromJson(j.Get("settings"));
	out.lobby = Id64(j.Get("lobby"));
	out.specLobby = Id64(j.Get("specLobby"));
	out.lobbyKey = Text(j, "lobbyKey");
	out.roomClaimBy = Id64(j.Get("roomClaimBy"));
	out.canClaimNoShow = j.GetBool("canClaimNoShow");
	out.games = TourneyGamesFromJson(j.Get("games"));
	out.pick = TourneyPickFromJson(j.Get("pick"));
	out.noshowFrom = Int64(j, "noshowFrom");
	std::string pace = Text(j, "pace"), format = Text(j, "format"), mode = Text(j, "mode");
	if (const Json *t = j.Get("tournament"); t && t->type == Json::Object) {
		if (pace.empty()) pace = Text(*t, "pace");
		if (format.empty()) format = Text(*t, "format");
		if (mode.empty()) mode = Text(*t, "mode");
	}
	out.pace = TourneyPaceFromId(pace);
	out.format = TourneyFormatFromId(format);
	out.team = TeamFromJson(j.Get("team"));
	if (out.team.members.empty()) out.team.members = out.me.members;
	if (out.team.side.empty()) out.team.side = out.me.side;
	// A match of teams says so by its mode, or by the players it lists.
	out.mode = TourneyModeFromId(mode);
	if (out.team.members.size() > 1 || out.opponent.members.size() > 1) out.mode = TourneyMode::Teams;
	// The view may name the pace only through noshowFrom, which only the by-rounds pace has.
	if (out.noshowFrom) out.pace = TourneyPace::Rounds;
	out.present = true;
	return true;
}

std::vector<uint64_t> TourneyMatchPlayers(const TourneyNext &n, uint64_t me)
{
	std::vector<uint64_t> out;
	const auto add = [&out](uint64_t id) {
		if (id && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
	};
	add(me);
	for (const TourneyMember &m : n.team.members) add(m.steamId);
	add(n.opponent.steamId);
	for (const TourneyMember &m : n.opponent.members) add(m.steamId);
	return out;
}

TourneyMap TourneyGameMap(const TourneyNext &n)
{
	if (n.pick.present) return TourneyMap();
	if (const TourneyGame *g = TourneyGameOf(n.games, n.game); g && g->map.Known()) return g->map;
	return n.map;
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
			x.seed = e.GetInt("seed");
			x.placement = e.GetInt("placement");
			x.entryId = Int64(e, "entryId");
			if (!x.entryId) x.entryId = Int64(e, "id");
			x.teamName = Text(e, "teamName");
			x.partner = MemberFromJson(e.Get("partner"));
			x.members = TourneyMembersFromJson(e.Get("members"));
			// The league's form: {team: {name, captain, members}}.
			if (const Json *team = e.Get("team"); team && team->type == Json::Object) {
				if (x.members.empty()) x.members = TourneyMembersFromJson(team->Get("members"));
				if (x.teamName.empty()) x.teamName = TeamNameOf(Text(*team, "name"), x.members);
			}
			if (x.tournamentId) out.entries.push_back(x);
		}
	}
	if (const Json *list = j.Get("invites"); list && list->type == Json::Array) {
		for (const Json &v : list->arr) {
			if (v.type != Json::Object) continue;
			TourneyInvite x;
			x.tournamentId = Int64(v, "tournamentId");
			x.entryId = Int64(v, "entryId");
			x.slug = Text(v, "slug");
			x.name = Text(v, "name");
			x.teamName = Text(v, "teamName");
			x.from = MemberFromJson(v.Get("from"));
			x.expiresAt = Int64(v, "expiresAt");
			if (x.tournamentId && x.entryId) out.invites.push_back(x);
		}
		std::stable_sort(out.invites.begin(), out.invites.end(), [](const TourneyInvite &a, const TourneyInvite &b) {
			if ((a.expiresAt > 0) != (b.expiresAt > 0)) return a.expiresAt > 0;
			return a.expiresAt < b.expiresAt;
		});
	}
	if (const Json *o = j.Get("organize"); o && o->type == Json::Object) {
		TourneyOrganize &g = out.organize;
		g.known = true;
		g.allowed = o->GetBool("allowed");
		g.gamesPlayed = o->GetInt("gamesPlayed");
		g.gamesNeeded = o->GetInt("gamesNeeded");
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
			x.entries = c.GetInt("entries");
			x.checkedIn = c.GetInt("checkedIn");
			x.maxEntries = c.GetInt("maxEntries");
			x.minEntries = c.GetInt("minEntries");
			x.startsAt = Int64(c, "startsAt");
			x.checkinOpensAt = Int64(c, "checkinOpensAt");
			x.openDisputes = c.GetInt("openDisputes");
			if (x.id && !x.slug.empty()) out.organized.push_back(x);
		}
	}
	return true;
}

const TourneyMyEntry *TourneyMyEntryOf(const TourneyMine &m, int64_t tournamentId)
{
	// A cup of teams can list the player twice: a team of theirs that was withdrawn, and the one they joined after.
	const TourneyMyEntry *pending = nullptr, *any = nullptr;
	for (const TourneyMyEntry &e : m.entries) {
		if (!tournamentId || e.tournamentId != tournamentId) continue;
		if (TourneyEntryActive(e.status) || e.status == TourneyEntryStatus::Out || e.placement > 0) return &e;
		if (e.Pending() && !pending) pending = &e;
		if (!any) any = &e;
	}
	return pending ? pending : any;
}

const TourneyOrganized *TourneyOrganizedOf(const TourneyMine &m, int64_t tournamentId)
{
	for (const TourneyOrganized &c : m.organized) {
		if (tournamentId && c.id == tournamentId) return &c;
	}
	return nullptr;
}

const TourneyInvite *TourneyInviteOf(const TourneyMine &m, int64_t tournamentId)
{
	for (const TourneyInvite &i : m.invites) {
		if (tournamentId && i.tournamentId == tournamentId) return &i;
	}
	return nullptr;
}

TourneyMember TourneyPartnerOf(const TourneyMyEntry &e, uint64_t me)
{
	if (e.partner.steamId || !e.partner.name.empty()) return e.partner;
	for (const TourneyMember &m : e.members) {
		if (m.steamId != me) return m;
	}
	return TourneyMember();
}

bool TourneyHasActiveEntry(const TourneyMine &m)
{
	for (const TourneyMyEntry &e : m.entries) {
		if (Active(e.tournamentStatus) && (TourneyEntryActive(e.status) || e.Pending())) return true;
	}
	for (const TourneyOrganized &c : m.organized) {
		if (Active(c.status)) return true;
	}
	return m.next.present || !m.invites.empty();
}

int TourneyPollEveryMs(bool activeEntry, bool tabOpen, bool away)
{
	// Whatever came meanwhile is read as soon as the player is back; in a cup of their own a match ready flashes the game, so not then.
	if (away && !activeEntry) return kTourneyAwayPollMs;
	// In a cup with its tab open the next match, its room and the start come in at once (the league takes a read every 5 s).
	if (activeEntry) return tabOpen ? 5000 : 30000;
	return tabOpen ? 60000 : kTourneyIdlePollMs;
}

int TourneySeriesPollMs(const TourneyNext &n, int64_t msSinceGame)
{
	if (!n.present) return 0;
	if (n.pick.present) return 5000;
	return n.bestOf > 1 && msSinceGame >= 0 && msSinceGame < 120000 ? 5000 : 0;
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
		out.mods = LeagueParseModsRule(j.Get("mods"));
	}
	else if (const uint64_t lobby = Id64(j.Get("join"))) {
		out.kind = TourneyClaim::Join;
		out.lobby = lobby;
	}
	else if (j.GetBool("wait")) {
		out.kind = TourneyClaim::Wait;
		out.by = Id64(j.Get("by"));
		out.pick = TourneyPickFromJson(j.Get("pick"));
	}
	if (const Json *teams = j.Get("teams"); teams && teams->type == Json::Array && teams->arr.size() == 2) {
		for (const Json &side : teams->arr) {
			std::vector<uint64_t> ids;
			if (side.type == Json::Array) {
				for (const Json &id : side.arr) {
					if (const uint64_t steamId = Id64(&id)) ids.push_back(steamId);
				}
			}
			out.teams.push_back(ids);
		}
	}
	return out.kind != TourneyClaim::None;
}

int TourneyTeamOf(const std::vector<std::vector<uint64_t>> &teams, uint64_t steamId)
{
	for (size_t i = 0; i < teams.size() && i < 2; i++) {
		if (steamId && std::find(teams[i].begin(), teams[i].end(), steamId) != teams[i].end()) return static_cast<int>(i) + 1;
	}
	return 0;
}

std::string TourneyRoomTag(int64_t match)
{
	return match > 0 ? "tournament:" + std::to_string(match) : std::string();
}

bool TourneyIsTestBot(uint64_t steamId)
{
	const uint64_t base = 76561190000000000ULL;
	return steamId > base && steamId < base + 100000ULL;
}

int64_t TourneyMatchOfTag(const std::string &tag)
{
	static const char kPrefix[] = "tournament:";
	if (tag.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return 0;
	const std::string digits = tag.substr(sizeof(kPrefix) - 1);
	if (digits.empty() || digits.size() > 15 || digits.find_first_not_of("0123456789") != std::string::npos) return 0;
	return _atoi64(digits.c_str());
}

namespace {

// A notice of the player's next match: its tournament, the match and who it is against.
TourneyNotice NextMatchNotice(TourneyNotice::Kind kind, const TourneyNext &n)
{
	TourneyNotice x;
	x.kind = kind;
	x.tournamentId = n.tournamentId;
	x.match = n.match;
	x.slug = n.slug;
	x.name = n.name;
	x.opponent = n.opponent.name;
	x.stage = n.stage;
	x.bracket = n.bracket;
	x.round = n.round;
	return x;
}

}

std::vector<TourneyNotice> TourneyNewNotices(const TourneyMine &mine, TourneyTold &told)
{
	std::vector<TourneyNotice> out;
	const TourneyNext &n = mine.next;
	if (n.present && n.status == TourneyMatchStatus::Ready && told.ready.insert(n.match).second && !n.me.inRoom) {
		TourneyNotice x = NextMatchNotice(TourneyNotice::MatchReady, n);
		x.deadlineAt = n.deadlineAt;
		out.push_back(x);
	}
	if (n.present && n.MyPick() && told.picks.insert({n.match, n.game}).second) {
		TourneyNotice x = NextMatchNotice(TourneyNotice::PickOpen, n);
		x.game = n.game;
		x.deadlineAt = n.pick.until;
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
	// An invitation waits for an answer: those that came before the game started are told too.
	for (const TourneyInvite &i : mine.invites) {
		if (!told.invites.insert(i.entryId).second) continue;
		TourneyNotice x;
		x.kind = TourneyNotice::Invite;
		x.tournamentId = i.tournamentId;
		x.entry = i.entryId;
		x.slug = i.slug;
		x.name = i.name;
		x.teamName = i.teamName;
		x.from = i.from;
		x.deadlineAt = i.expiresAt;
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
	r.mods = LeagueParseModsRule(j.Get("mods"));
	r.slug = Text(j, "slug");
	r.until = Int64(j, "until");
	r.gamesPlayed = j.GetInt("gamesPlayed");
	r.gamesNeeded = j.GetInt("gamesNeeded");
	r.checkedIn = j.GetInt("checkedIn");
	r.needed = j.GetInt("needed");
	r.retryAfter = std::max(0, j.GetInt("retryAfter"));
	r.requirement = Text(j, "kind");
	r.who = Lower(Text(j, "who"));
	for (const auto &field : {std::make_pair("have", &r.have), std::make_pair("need", &r.need)}) {
		const Json *v = j.Get(field.first);
		if (v && v->type == Json::Number) *field.second = std::to_string(static_cast<long long>(std::llround(v->num)));
		else if (v && v->type == Json::String) *field.second = v->str;
	}
	r.hash = Lower(Text(j, "hash"));
	r.noshowFrom = Int64(j, "noshowFrom");
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

Json TourneyTeamEntryJson(uint64_t partner, const std::string &teamName, const std::string &code)
{
	Json body = Json::MakeObject();
	body.Set("partner", Json::MakeString(std::to_string(partner)));
	const std::string name = TourneyTrim(teamName);
	if (!name.empty()) body.Set("teamName", Json::MakeString(name));
	if (!code.empty()) body.Set("code", Json::MakeString(code));
	return body;
}

bool TourneyParseMapTypes(const Json &j, std::vector<TourneyMapType> &out)
{
	const Json *list = j.Get("types");
	if (!list || list->type != Json::Array) return false;
	out.clear();
	for (const Json &t : list->arr) {
		if (t.type != Json::Object) continue;
		TourneyMapType x;
		x.id = Lower(Text(t, "id"));
		x.sizes = Strings(t.Get("sizes"));
		const Json *four = t.Get("sizes4");
		x.hasSizes4 = four && four->type == Json::Array;
		x.sizes4 = Strings(four);
		if (!x.id.empty()) out.push_back(x);
	}
	return true;
}

std::vector<std::string> TourneySizesFor(const std::vector<TourneyMapType> &types, const std::string &type, TourneyMode mode)
{
	const std::vector<std::string> sizes = LeagueMapSizesOf(type);
	if (mode != TourneyMode::Teams) return sizes;
	std::vector<std::string> out;
	for (const TourneyMapType &t : types) {
		if (t.id != type || !t.hasSizes4) continue;
		// In the order of the room's list, as the 1 vs 1 sizes.
		for (const char *s : {"small", "medium", "large"}) {
			if (Contains(t.sizes4, s)) out.push_back(s);
		}
		return out;
	}
	for (const std::string &s : sizes) {
		if (s != "small") out.push_back(s);
	}
	return out;
}

bool TourneyMapFits(const TourneyMap &m, TourneyMode mode, const std::vector<TourneyMapType> &types)
{
	if (m.Scenario()) return m.players >= 2 * TourneySidePlayers(mode);
	return Contains(TourneySizesFor(types, m.name, mode), m.size);
}

TourneyMap TourneyMapForMode(const TourneyMap &m, TourneyMode mode, const std::vector<TourneyMapType> &types)
{
	if (TourneyMapFits(m, mode, types)) return m;
	if (!m.Scenario()) {
		const std::vector<std::string> sizes = TourneySizesFor(types, m.name, mode);
		if (!sizes.empty()) {
			// The size next up, as a small map grows to a normal one; the largest when there is none above it.
			static const char *const kOrder[] = {"small", "medium", "large"};
			int at = 0;
			for (int i = 0; i < 3; i++) {
				if (m.size == kOrder[i]) at = i;
			}
			for (int i = at; i < 3; i++) {
				if (Contains(sizes, kOrder[i])) return {m.name, m.type.empty() ? "random" : m.type, kOrder[i]};
			}
			return {m.name, m.type.empty() ? "random" : m.type, sizes.back()};
		}
	}
	TourneyMap fallback = {"mediterranean", "random", "medium"};
	if (!TourneyMapFits(fallback, mode, types)) {
		const std::vector<std::string> sizes = TourneySizesFor(types, fallback.name, mode);
		if (!sizes.empty()) fallback.size = sizes.front();
	}
	return fallback;
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

std::string TourneyCutText(const std::string &text, size_t maxBytes)
{
	if (text.size() <= maxBytes) return text;
	size_t end = maxBytes;
	while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
	return text.substr(0, end);
}

// A cup of four starts with three (one goes straight on); a bigger one with four.
int TourneyCupDefaultMinimum(int places)
{
	return std::max(2, places <= 4 ? std::min(3, places) : 4);
}

int TourneyRoundsFor(int places)
{
	int rounds = 0;
	while ((1 << rounds) < places && rounds < 16) rounds++;
	return rounds;
}

std::vector<TourneyCupProblem> TourneyCheckCup(const TourneyCupForm &f, int64_t nowMs, const std::vector<TourneyMapType> &types)
{
	std::vector<TourneyCupProblem> out;
	const int name = TourneyTextLength(TourneyTrim(f.name));
	if (name < kTourneyCupNameMin) out.push_back(TourneyCupProblem::NameShort);
	else if (name > kTourneyCupNameMax) out.push_back(TourneyCupProblem::NameLong);
	if (TourneyTextLength(TourneyTrim(f.description)) > kTourneyCupDescriptionMax) out.push_back(TourneyCupProblem::DescriptionLong);
	const bool places = f.maxEntries == 4 || f.maxEntries == 8 || f.maxEntries == 16 || f.maxEntries == 32;
	// A double elimination of 32 would last far more than an evening: up to 16 places, as the league has it.
	if (!places || (f.maxEntries > 16 && f.format == TourneyFormat::Double)) out.push_back(TourneyCupProblem::Places);
	else if (f.minEntries < 2 || f.minEntries > f.maxEntries) out.push_back(TourneyCupProblem::Minimum);
	if (f.format == TourneyFormat::Swiss && f.swissRounds != 0 && (f.swissRounds < kTourneySwissRoundsMin || f.swissRounds > kTourneySwissRoundsMax)) {
		out.push_back(TourneyCupProblem::Format);
	}
	// A community cup's rounds are of one game or best of three; its final may also be best of five.
	if ((f.bestOf != 1 && f.bestOf != 3) || (f.finalBestOf != 1 && f.finalBestOf != 3 && f.finalBestOf != 5)) out.push_back(TourneyCupProblem::Series);
	if (!TourneyPresetValid(f.settings.preset) || !TourneySpecDelayValid(f.settings.specDelay)) out.push_back(TourneyCupProblem::Settings);
	bool large = false;
	const auto usable = [&large](const TourneyMap &m) {
		if (!m.Scenario()) return LeagueMapValid(m.name, m.size);
		large = large || m.bytes > kTourneyScenarioMaxBytes;
		return TourneyScenarioFileValid(m.file) && TourneySha256Valid(m.hash) && m.bytes > 0 && m.players >= 2;
	};
	if (f.maps.empty()) out.push_back(TourneyCupProblem::NoMaps);
	else if (static_cast<int>(f.maps.size()) > kTourneyCupMapsMax) out.push_back(TourneyCupProblem::TooManyMaps);
	else {
		bool bad = false;
		for (const TourneyMap &m : f.maps) bad = !usable(m) || bad;
		if (bad) out.push_back(TourneyCupProblem::BadMap);
	}
	if (TourneyCupHasSeries(f) && !f.seriesPool.empty()) {
		const int n = static_cast<int>(f.seriesPool.size());
		if (n < kTourneyPoolMin || n > kTourneyPoolMax) out.push_back(TourneyCupProblem::PoolCount);
		bool bad = false;
		for (size_t i = 0; i < f.seriesPool.size(); i++) {
			bad = !usable(f.seriesPool[i]) || bad;
			for (size_t k = 0; k < i; k++) bad = bad || TourneySameMap(f.seriesPool[i], f.seriesPool[k]);
		}
		if (bad) out.push_back(TourneyCupProblem::PoolMap);
	}
	if (large && std::find(out.begin(), out.end(), TourneyCupProblem::BadMap) == out.end()) {
		out.insert(std::find_if(out.begin(), out.end(), [](TourneyCupProblem q) { return q == TourneyCupProblem::PoolCount || q == TourneyCupProblem::PoolMap; }),
			TourneyCupProblem::MapTooLarge);
	}
	// A cup of teams needs room for four in every map it can play, the series' too; a map that can't be used at all is told as such.
	bool seats = false;
	std::vector<TourneyMap> played = f.maps;
	if (TourneyCupHasSeries(f)) played.insert(played.end(), f.seriesPool.begin(), f.seriesPool.end());
	for (const TourneyMap &m : played) {
		const bool valid = m.Scenario() ? m.players >= 2 : LeagueMapValid(m.name, m.size);
		if (f.mode == TourneyMode::Teams && valid && !TourneyMapFits(m, f.mode, types)) seats = true;
	}
	if (seats) {
		out.insert(std::find_if(out.begin(), out.end(), [](TourneyCupProblem q) { return q == TourneyCupProblem::PoolCount || q == TourneyCupProblem::PoolMap; }),
			TourneyCupProblem::MapSeats);
	}
	if (f.startsAt < nowMs + kTourneyCupLeadMinMs) out.push_back(TourneyCupProblem::StartSoon);
	else if (f.startsAt > nowMs + kTourneyCupLeadMaxMs) out.push_back(TourneyCupProblem::StartLate);
	if (f.pace == TourneyPace::Rounds && !TourneyRoundDaysValid(f.roundDays)) out.push_back(TourneyCupProblem::Pace);
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
	body.Set("mode", Json::MakeString(TourneyModeId(f.mode)));
	body.Set("teamDraw", Json::MakeBool(f.mode == TourneyMode::Teams && f.teamDraw));
	body.Set("format", Json::MakeString(TourneyFormatId(f.format)));
	// The Swiss's rounds, null for the automatic count and outside a Swiss; the days of a round, null when it is played live.
	const bool chosen = f.format == TourneyFormat::Swiss && f.swissRounds >= kTourneySwissRoundsMin && f.swissRounds <= kTourneySwissRoundsMax;
	body.Set("swissRounds", chosen ? Json::MakeNumber(f.swissRounds) : Json());
	body.Set("bestOf", Json::MakeNumber(f.bestOf));
	// A Swiss has no final: its series are all the rounds'.
	body.Set("finalBestOf", Json::MakeNumber(f.format == TourneyFormat::Swiss ? f.bestOf : f.finalBestOf));
	body.Set("pace", Json::MakeString(TourneyPaceId(f.pace)));
	body.Set("roundDays", f.pace == TourneyPace::Rounds ? Json::MakeNumber(f.roundDays) : Json());
	body.Set("seeding", Json::MakeString(f.randomSeeding ? "random" : "rating"));
	Json maps = Json::MakeArray();
	for (const TourneyMap &m : f.maps) maps.arr.push_back(TourneyMapJson(m));
	body.Set("maps", maps);
	Json pool = Json::MakeArray();
	if (TourneyCupHasSeries(f)) {
		for (const TourneyMap &m : f.seriesPool) pool.arr.push_back(TourneyMapJson(m));
	}
	body.Set("seriesPool", pool);
	body.Set("startsAt", Json::MakeNumber(static_cast<double>(f.startsAt)));
	body.Set("checkinMinutes", Json::MakeNumber(f.checkinMinutes));
	body.Set("noshowMinutes", Json::MakeNumber(f.noshowMinutes));
	body.Set("settings", TourneySettingsJson(f.settings));
	body.Set("requirements", TourneyRequirementsJson(f.requirements));
	body.Set("private", Json::MakeBool(f.isPrivate));
	// In a cup of teams the organizer's entry needs a partner: they sign their team up from the cup's page, so a new cup is made without
	// them, and a change leaves out "play", which would otherwise withdraw the team they signed up.
	if (f.mode != TourneyMode::Teams || f.teamDraw) body.Set("play", Json::MakeBool(f.play));
	else if (!edit) body.Set("play", Json::MakeBool(false));
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

void TourneyFitCupToMode(TourneyCupForm &f, const std::vector<TourneyMapType> &types)
{
	for (TourneyMap &m : f.maps) m = TourneyMapForMode(m, f.mode, types);
	f.seriesPool.erase(std::remove_if(f.seriesPool.begin(), f.seriesPool.end(), [&f, &types](const TourneyMap &m) { return !TourneyMapFits(m, f.mode, types); }),
		f.seriesPool.end());
}

TourneyCupForm TourneyCupFormOf(const TourneyDetail &d)
{
	TourneyCupForm f;
	f.name = d.name;
	f.description = d.description;
	f.maxEntries = d.maxEntries;
	f.minEntries = d.minEntries;
	f.mode = d.mode;
	f.teamDraw = d.teamDraw;
	f.bestOf = d.bestOf;
	f.finalBestOf = d.finalBestOf;
	f.randomSeeding = Lower(d.seeding) == "random";
	// What the list's view has too, read as the summary's.
	const TourneySummary &summary = d;
	f.format = TourneyFormatOf(summary);
	f.swissRounds = summary.swissRounds >= kTourneySwissRoundsMin && summary.swissRounds <= kTourneySwissRoundsMax ? summary.swissRounds : 0;
	f.pace = summary.pace;
	f.roundDays = TourneyRoundDaysValid(summary.roundDays) ? summary.roundDays : 2;
	f.seriesPool = summary.seriesPool;
	if (f.seriesPool.size() > static_cast<size_t>(kTourneyPoolMax)) f.seriesPool.resize(static_cast<size_t>(kTourneyPoolMax));
	f.maps = d.maps;
	TourneyFitMaps(f.maps, TourneyCupMapRounds(f));
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

TourneyEstimate TourneyEstimateFor(int places, int bestOf, int finalBestOf, int gameMinutes)
{
	TourneyEstimate e;
	if (places < 2) return e;
	bestOf = std::max(1, bestOf);
	finalBestOf = std::max(1, finalBestOf);
	e.rounds = TourneyRoundsFor(places);
	e.series = places - 1;
	e.maxGames = (places - 2) * bestOf + finalBestOf;
	e.minutes = gameMinutes * ((e.rounds - 1) * bestOf + finalBestOf) + 10 * (e.rounds - 1);
	return e;
}

TourneyEstimate TourneyEstimateFor(TourneyFormat format, int places, int bestOf, int finalBestOf, int swissRounds, int gameMinutes)
{
	if (format == TourneyFormat::Single) return TourneyEstimateFor(places, bestOf, finalBestOf, gameMinutes);
	TourneyEstimate e;
	if (places < 2) return e;
	bestOf = std::max(1, bestOf);
	finalBestOf = std::max(1, finalBestOf);
	if (format == TourneyFormat::Swiss) {
		e.rounds = TourneySwissRounds(swissRounds, places);
		e.series = e.rounds * (places / 2);
		e.maxGames = e.series * bestOf;
		e.minutes = gameMinutes * bestOf * e.rounds + 10 * (e.rounds - 1);
		return e;
	}
	// The losers' bracket of a bracket of k rounds plays 2(k-1) rounds, its first after the winners' first; each of the winners' later rounds
	// is played alongside one of them. Then the grand final, and its reset when the one from the losers' bracket wins it.
	const int k = TourneyRoundsFor(places);
	e.rounds = 2 * k + 1;
	e.series = 2 * places - 1;
	e.maxGames = (2 * places - 3) * bestOf + 2 * finalBestOf;
	e.minutes = gameMinutes * ((2 * k - 1) * bestOf + 2 * finalBestOf) + 10 * (e.rounds - 1);
	return e;
}

int64_t TourneyEstimatedEnd(const TourneyDetail &d)
{
	if (TourneyByRounds(d) || d.startsAt <= 0) return 0;
	const int places = d.entries >= 2 ? d.entries : d.maxEntries;
	const TourneyEstimate e = TourneyEstimateFor(TourneyFormatOf(d), places, d.bestOf, d.finalBestOf, d.swissRounds, TourneyGameMinutes(d.mode));
	return e.minutes > 0 ? d.startsAt + static_cast<int64_t>(e.minutes) * 60000 : 0;
}

TourneyEstimate TourneyEstimateOf(const TourneyCupForm &f)
{
	TourneyEstimate e = TourneyEstimateFor(f.format, f.maxEntries, f.bestOf, f.finalBestOf, f.swissRounds, TourneyGameMinutes(f.mode));
	if (f.pace == TourneyPace::Rounds) e.days = e.rounds * std::max(1, f.roundDays);
	return e;
}

bool TourneyCupLongLive(const TourneyCupForm &f)
{
	return f.format == TourneyFormat::Double && f.pace == TourneyPace::Live && TourneyEstimateOf(f).minutes > kTourneyLiveWarnMinutes;
}

bool TourneyCupHasSeries(const TourneyCupForm &f)
{
	return f.bestOf > 1 || (f.format != TourneyFormat::Swiss && f.finalBestOf > 1);
}

int TourneyCupMapRounds(const TourneyCupForm &f)
{
	if (f.format == TourneyFormat::Swiss) return TourneySwissRounds(f.swissRounds, f.maxEntries);
	return TourneyRoundsFor(f.maxEntries);
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

int TourneyLosersRounds(int winnersRounds)
{
	return winnersRounds > 1 ? 2 * (winnersRounds - 1) : 0;
}

int TourneyLosersMatches(int winnersRounds, int round)
{
	if (round < 1 || round > TourneyLosersRounds(winnersRounds)) return 0;
	return 1 << std::max(0, std::min(16, winnersRounds - (round + 1) / 2 - 1));
}

namespace {

// Two losers' rounds in a row stand in one column of the single bracket's height: the second only meets the players dropping in.
int LosersLevel(int round)
{
	return (std::max(1, round) + 1) / 2;
}

}

TourneyDoubleLayout TourneyDoubleLayoutOf(const TourneyBracketMetrics &m, int winnersRounds, int losersRounds, bool reset, int titleH, int gap)
{
	TourneyDoubleLayout l;
	l.winnersRounds = std::max(1, winnersRounds);
	l.losersRounds = std::max(losersRounds, TourneyLosersRounds(l.winnersRounds));
	l.reset = reset;
	l.winnersY = titleH;
	l.losersY = l.winnersY + TourneyBracketHeight(m, l.winnersRounds) + gap + titleH;
	const int columns = std::max(l.winnersRounds, l.losersRounds);
	l.finalX = TourneyBoxX(m, columns + 1);
	const int winnersMid = l.winnersY + TourneyBoxMidY(m, l.winnersRounds, 0);
	const int losersMid = l.losersRounds ? TourneyLosersBoxY(m, l, l.losersRounds, 0) + m.boxH / 2 : winnersMid;
	l.finalY = (winnersMid + losersMid) / 2 - m.boxH / 2;
	l.resetX = reset ? l.finalX + m.colPitch : 0;
	const int last = reset ? l.resetX : l.finalX;
	l.width = last + m.boxW;
	l.champX = l.width + 40;
	l.height = l.losersRounds ? l.losersY + TourneyBracketHeight(m, LosersLevel(l.losersRounds)) : l.winnersY + TourneyBracketHeight(m, l.winnersRounds);
	return l;
}

int TourneyWinnersBoxY(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l, int round, int position)
{
	return l.winnersY + TourneyBoxY(m, round, position);
}

int TourneyLosersBoxY(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l, int round, int position)
{
	return l.losersY + TourneyBoxY(m, LosersLevel(round), position);
}

std::vector<TourneySegment> TourneyLosersConnector(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l, int round, int position)
{
	std::vector<TourneySegment> out;
	if (round < 1 || round >= l.losersRounds) return out;
	const int x = TourneyBoxX(m, round) + m.boxW, nextX = TourneyBoxX(m, round + 1);
	const int y = TourneyLosersBoxY(m, l, round, position) + m.boxH / 2;
	if (round % 2) {
		out.push_back({x, y, nextX, y});
		return out;
	}
	const int joint = x + (nextX - x) / 2, nextY = TourneyLosersBoxY(m, l, round + 1, position / 2) + m.boxH / 2;
	out.push_back({x, y, joint, y});
	out.push_back({joint, y, joint, nextY});
	out.push_back({joint, nextY, nextX, nextY});
	return out;
}

std::vector<TourneySegment> TourneyGrandFinalConnector(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l)
{
	std::vector<TourneySegment> out;
	const int joint = l.finalX - (m.colPitch - m.boxW) / 2, mid = l.finalY + m.boxH / 2;
	const int wx = TourneyBoxX(m, l.winnersRounds) + m.boxW, wy = TourneyWinnersBoxY(m, l, l.winnersRounds, 0) + m.boxH / 2;
	const int lx = TourneyBoxX(m, std::max(1, l.losersRounds)) + m.boxW, ly = l.losersRounds ? TourneyLosersBoxY(m, l, l.losersRounds, 0) + m.boxH / 2 : wy;
	out.push_back({wx, wy, joint, wy});
	out.push_back({lx, ly, joint, ly});
	out.push_back({joint, wy, joint, ly});
	out.push_back({joint, mid, l.finalX, mid});
	if (l.reset) out.push_back({l.finalX + m.boxW, mid, l.resetX, mid});
	return out;
}

int TourneyStripCellWidth(int width, int cells, int gap, int maxW)
{
	cells = std::max(1, cells);
	return std::max(0, std::min(maxW, (width - gap * (cells - 1)) / cells));
}

TourneyStandingsColumns TourneyStandingsColumnsFor(int width, int rounds)
{
	TourneyStandingsColumns c;
	c.figureW = 96;
	c.buchholzW = c.figureW + 16;
	c.byes = width - c.figureW;
	c.buchholz = c.byes - c.buchholzW;
	c.record = c.buchholz - c.figureW;
	c.points = c.record - c.figureW;
	c.name = 64;
	c.roundsW = std::max(0, rounds) * 22 + 24;
	c.rounds = c.points - c.roundsW;
	// A name narrower than this is cut more than the rounds are worth.
	c.showRounds = rounds > 0 && c.rounds - c.name >= 280;
	c.nameW = std::max(0, (c.showRounds ? c.rounds : c.points) - c.name - 16);
	return c;
}

int TourneyRoundSquareX(const TourneyStandingsColumns &c, int round)
{
	return c.rounds + 12 + (std::max(1, round) - 1) * 22;
}

TourneyPairingRow TourneyPairingRowFor(int width)
{
	TourneyPairingRow r;
	r.stateW = std::min(240, width / 5);
	r.stateX = width - r.stateW - 16;
	r.left = r.tableW + 16;
	r.right = r.stateX - 24;
	r.scoreX = r.left + (r.right - r.left - r.scoreW) / 2;
	return r;
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
