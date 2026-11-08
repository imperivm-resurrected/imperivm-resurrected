// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "ui/modui.h"
#include "mods/savelib.h"

#include "ui/mpui_internal.h"
#include "game/civs.h"
#include "core/log.h"

using namespace mpui_impl;

namespace mpui_impl {
bool ChatBarDown(POINT pt)
{
	std::lock_guard<std::mutex> lock(g_chat.mx);
	ListState &l = g_s.chat;
	l.scroll = g_s.chatGeom.maxScroll - g_chat.scroll;
	if (!ListDown(l, g_s.chatGeom, pt)) return false;
	g_chat.scroll = std::max(0, g_s.chatGeom.maxScroll - l.scroll);
	return true;
}

void ChatBarMove(POINT pt)
{
	if (!g_s.chat.drag) return;
	std::lock_guard<std::mutex> lock(g_chat.mx);
	ListMove(g_s.chat, g_s.chatGeom, pt);
	g_chat.scroll = std::max(0, g_s.chatGeom.maxScroll - g_s.chat.scroll);
}

RECT g_optRect = {};
ListGeom g_optGeomOut;
}

namespace mpui_impl {
namespace {
const int kDropRowH = 24;

bool PresetGameMatches(const Preset &pr, const OptionGetter &get)
{
	if (!pr.game) return true;
	return get(0x1005, "Starting gold") == pr.gold && get(0x1004, "Population") == pr.pop &&
		get(0x1007, "No fog") == (pr.fog ? "no" : "yes") && get(0x1008, "Map pre-explored") == (pr.explored ? "yes" : "no") &&
		get(0x10a7, "Disable bonuses") == (pr.bonuses ? "no" : "yes");
}
}
}

namespace mpui_impl {
int MatchPreset(unsigned sites, unsigned rules, const OptionGetter &get)
{
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < kPresetCount; i++) {
			if (kPresets[i].game != (pass == 0)) continue;
			if (kPresets[i].sites == (sites & kSiteMask) && kPresets[i].rules == (rules & kRuleBaseMask) && PresetGameMatches(kPresets[i], get)) return i;
		}
	}
	return -1;
}

OptionGetter HostOptionGetter(const GameRoomSnapshot &g)
{
	return [&g](int id, const char *) {
		for (const GameRoomOption &o : g.options) {
			if (o.id != id) continue;
			if (o.checkbox) return std::string(o.value ? "yes" : "no");
			if (o.sel >= 0 && o.sel < static_cast<int>(o.items.size())) return o.items[o.sel];
		}
		return std::string("—");
	};
}

StartBlock StartBlocker(const State &st, const GameRoomSnapshot &g)
{
	if (!g.active || !g.host || !g.online) return {};
	auto inRoom = [&](const std::string &name) {
		const std::string want = LowerName(name);
		for (const GameRoomSlot &s : g.slots) {
			if (!s.used || s.type != 3) continue;
			const std::string have = LowerName(s.name);
			if (have == want || (have.size() >= 8 && want.compare(0, have.size(), have) == 0)) return true;
		}
		return false;
	};
	int humans = 0;
	for (const GameRoomSlot &s : g.slots) humans += s.used && s.type == 3 ? 1 : 0;
	const bool missing = humans < static_cast<int>(st.room.members.size());
	for (const OnlineMember &m : st.room.members) {
		if (!missing || m.me || inRoom(m.name)) continue;
		if (m.receiving >= 0) {
			return {Tr("Waiting for ") + m.name + Tr(" (content ") + std::to_string(m.receiving) + " %)...",
				m.name + Tr(" is downloading the content (") +
					std::to_string(m.receiving) + Tr(" %). Wait for them to join or kick them from the room.")};
		}
		return {Tr("Waiting for ") + m.name + "...", m.name + Tr(" is joining the game. Wait for them to join or kick them from the room.")};
	}
	for (const OnlineMember &m : st.room.restarting) {
		return {Tr("Waiting for ") + m.name + "...", m.name + Tr(" is restarting the game with the content and will be right back. Wait or kick them from the room.")};
	}
	for (const std::string &name : SteamMissingScenario()) {
		int pct = -1;
		for (const OnlineMember &m : st.room.members) {
			if (PlayerNameMatches(m.name, name)) pct = m.receiving;
		}
		const std::string modName = SteamScenarioModName();
		if (!modName.empty()) {
			return {TrFmt("Missing the map for %s...", {name}),
				TrFmt("%s does not have the room map (\"%s\"). They must leave and join again to download it, or kick them from the room.", {name, modName})};
		}
		return {pct >= 0 ? TrFmt("Waiting for %s (scenario %d %%)...", {name, std::to_string(pct)}) : TrFmt("Waiting for %s (scenario)...", {name}),
			TrFmt("%s doesn't have the game's scenario yet. Wait until they finish receiving it.", {name})};
	}
	const bool load = !st.hostSave.name.empty() && st.hostSavePublished && st.savePlayersRead;
	if (load && st.saveMapStep < 2) return {"Choosing the map...", "Wait a moment: the map of the saved game is being chosen."};
	const DWORD now = GetTickCount();
	bool seating = load && st.saveSwapTick && now - st.saveSwapTick < 1500;
	for (const auto &p : st.pendingSeat) seating = seating || now - p.second.second < 3000;
	if (seating || g.busy) return {"Placing the players...", "Wait a moment: the room's slots are being changed."};
	if (st.moveFrom >= 0 && st.moveFrom < 8) return {"", "Finish moving the player (or press Cancel) before starting."};
	return {};
}

bool SaveMapPicking(const State &st)
{
	return st.game.host && !st.hostSave.name.empty() && st.hostSavePublished && st.savePlayersRead && st.saveMapStep < 2;
}

std::string SlotLabel(const GameRoomSlot &s)
{
	if (s.type != 1) return s.name;
	const int n = static_cast<int>(s.nameItems.size());
	return s.nameSel >= 0 && s.nameSel < n && !s.nameItems[s.nameSel].empty() ? Tr("AI ") + s.nameItems[s.nameSel] : "AI";
}

std::vector<SlotChoice> SlotChoices(const GameRoomSnapshot &g, const GameRoomSlot &s, int &sel)
{
	std::vector<SlotChoice> out;
	out.push_back({g.online ? "Open" : "Free", 0, -1});
	if (g.online) out.push_back({"Closed", 2, -1});
	if (s.type == 1 && !s.nameItems.empty()) {
		for (size_t k = 0; k < s.nameItems.size(); k++) {
			if (!s.nameItems[k].empty()) out.push_back({Tr("AI ") + s.nameItems[k], 1, static_cast<int>(k)});
		}
	}
	else if (s.aiAllowed || s.type == 1) {
		out.push_back({"AI", 1, -1});
	}
	sel = 0;
	for (size_t k = 0; k < out.size(); k++) {
		const SlotChoice &c = out[k];
		if (c.type == s.type && (c.type != 1 || c.aiItem < 0 || c.aiItem == s.nameSel)) {
			sel = static_cast<int>(k);
			break;
		}
	}
	return out;
}

std::string SavePlayersText(const std::vector<MpSavePlayer> &players)
{
	std::string out;
	for (size_t i = 0; i < players.size() && i < 8; i++) {
		const MpSavePlayer &p = players[i];
		if (!p.used()) continue;
		out += std::to_string(i + 1) + ". " + (p.ai() ? "AI" : p.name) + "\n";
	}
	return out;
}

std::vector<SaveEntry> ListSaves()
{
	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	std::string dir = exe;
	dir = dir.substr(0, dir.find_last_of('\\'));
	std::vector<SaveEntry> out;
	WIN32_FIND_DATAA pd;
	HANDLE ph = FindFirstFileA((dir + "\\Profiles\\*").c_str(), &pd);
	if (ph == INVALID_HANDLE_VALUE) return out;
	do {
		const std::string profile = pd.cFileName;
		if (!(pd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || profile == "." || profile == "..") continue;
		for (const SaveRec &r : SaveLibScan(profile)) {
			if (r.kind != kSaveStrategy) continue;
			SaveEntry e;
			e.name = r.baseName;
			e.vfsPath = r.vfsPath;
			e.file = r.diskPath;
			e.time = r.time;
			e.online = r.online;
			out.push_back(e);
		}
	} while (FindNextFileA(ph, &pd));
	FindClose(ph);
	std::sort(out.begin(), out.end(), [](const SaveEntry &a, const SaveEntry &b) { return a.time > b.time; });
	return out;
}

namespace {
void AddRulesets(const ModInfo &m, std::vector<RulesetChoice> &out)
{
	if (!m.forbidden.empty()) return;
	for (const ModLayer &l : m.layers) {
		RulesetChoice c;
		c.ruleset.pack = m.folder;
		c.ruleset.id = l.id;
		c.label = l.name + " \xC2\xB7 " + m.name;
		out.push_back(c);
	}
}
}

ProfileSummary ProfileSummaryFor(const std::string &profile)
{
	ProfileSummary out;
	for (const std::string &folder : ModsLoadProfile(profile).mods) {
		ModInfo m;
		ModRow r;
		if (!ModsFind(folder, m)) {
			r.name = folder;
			r.status = Tr("not installed");
			r.color = kBad;
		}
		else {
			r.name = m.name + (m.version.empty() || m.name.find(m.version) != std::string::npos ? "" : " " + m.version);
			r.kind = m.kind;
			AddRulesets(m, out.rulesets);
		}
		out.rows.push_back(r);
	}
	return out;
}

// What a profile mounts: the mounted profile has its ruleset already checked; another one is checked here.
ModRuleset ProfileRulesetFor(const std::string &profile)
{
	const ModProfile &mounted = ModsMountedProfile();
	return profile == mounted.name ? mounted.ruleset : ModsEffectiveRuleset(ModsLoadProfile(profile));
}

// The rulesets of the packages the game has loaded; it changes only when the mods are scanned again.
const std::vector<RulesetChoice> &MountedRulesets()
{
	static uint64_t serial = ~0ull;
	static std::vector<RulesetChoice> choices;
	if (serial != ModsListSerial()) {
		serial = ModsListSerial();
		choices.clear();
		for (const std::string &folder : ModsMountedProfile().mods) {
			ModInfo m;
			if (ModsFind(folder, m)) AddRulesets(m, choices);
		}
	}
	return choices;
}

// The skirmish room has no Create screen: its own row picks the ruleset of the content that is loaded.
bool SkirmishRoom(const State &st)
{
	return st.game.active && !st.game.online;
}

const std::vector<RulesetChoice> &RulesetChoicesShown(const State &st)
{
	return SkirmishRoom(st) ? MountedRulesets() : st.createProfileInfo.rulesets;
}

const ModRuleset &RulesetShown(const State &st)
{
	return SkirmishRoom(st) ? ModsMountedProfile().ruleset : st.createRuleset;
}

std::string RulesetLabel(const std::vector<RulesetChoice> &choices, const ModRuleset &ruleset)
{
	for (const RulesetChoice &c : choices) {
		if (ModsSameRuleset(c.ruleset, ruleset)) return c.label;
	}
	return Tr("Original (HD)");
}

void Predict(int kind, int index, int value)
{
	auto &list = g_s.predicted;
	list.erase(std::remove_if(list.begin(), list.end(), [&](const State::Predicted &p) { return p.kind == kind && p.index == index; }),
		list.end());
	list.push_back({kind, index, value, GetTickCount()});
}

bool SeatTakeable(const GameRoomSlot &s)
{
	return (!s.used && s.type == 0) || (s.used && s.type == 1);
}

bool BonusesDisabled(const GameRoomSnapshot &g)
{
	if (g.host) {
		for (const GameRoomOption &o : g.options) {
			if (o.id == 0x10a7) return o.value;
		}
		return false;
	}
	return g_s.room.info.optionsText.find("Disable bonuses: yes") != std::string::npos;
}

void ApplySeatDisplay(GameRoomSnapshot &g)
{
	if (!g.active || !g.host) {
		g_s.pendingSeat.clear();
		return;
	}
	const bool load = g_s.hostSavePublished && g_s.savePlayersRead;
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &rs = g.slots[i];
		if (!rs.used || rs.type != 3 || rs.name.empty()) continue;
		const std::string who = LowerName(rs.name);
		int target = -1;
		auto it = g_s.pendingSeat.find(who);
		if (it != g_s.pendingSeat.end()) {
			if (it->second.first == i || GetTickCount() - it->second.second > 3000) {
				g_s.pendingSeat.erase(it);
				continue;
			}
			target = it->second.first;
		}
		else if (load && !g_s.savePlaced.count(who)) {
			for (size_t j = 0; j < g_s.savePlayers.size() && j < 8; j++) {
				const MpSavePlayer &sp = g_s.savePlayers[j];
				if (sp.used() && !sp.ai() && LowerName(sp.name) == who) {
					target = static_cast<int>(j);
					break;
				}
			}
		}
		if (target < 0 || target == i || target >= 8 || !SeatTakeable(g.slots[target])) continue;
		std::swap(g.slots[i], g.slots[target]);
		if (g.localSlot == target) g.localSlot = i;
		if (g.localSlot == i) g.localSlot = target;
	}
}

int RaceIdToCombo(int id)
{
	static const int kCombo[9] = {2, 0, 4, 3, 1, 7, 6, 5, 8};
	return id >= 0 && id < 9 ? kCombo[id] : -1;
}

bool RaceEditable(const GameRoomSnapshot &g, const GameRoomSlot &s, bool load)
{
	return !load && (s.local || (g.host && s.type == 1)) && !s.raceNames.empty() && s.fixedRace < 0;
}

bool LocalReady(const GameRoomSnapshot &g)
{
	for (const GameRoomSlot &s : g.slots) {
		if (s.local && s.used && s.ready) return true;
	}
	return false;
}

std::string RoomConfigKey(const OnlineLobby &l)
{
	std::string key = l.optionsText + "|" + std::to_string(l.rules) + "|" + l.map + "|" + l.mode + "|" + l.civSets;
	for (const OnlineSlot &s : l.slots) {
		if (s.type != 3) key += "|" + std::to_string(s.type) + ":" + s.name;
	}
	return key;
}

bool SlotFrozen(const GameRoomSnapshot &g, const GameRoomSlot &s)
{
	if (!g.online) return false;
	return s.local ? s.ready : g.host && s.type == 1 && LocalReady(g);
}

bool RoomActionFrozen(const GameRoomSnapshot &g, int action, int arg)
{
	if (!g.online) return false;
	switch (action) {
	case kGTeam:
	case kGColor: return SlotFrozen(g, g.slots[arg & 7]);
	case kGRuleToggle:
	case kCivSetToggle:
	case kGSiteToggle:
	case kGOptToggle: return g.host && LocalReady(g);
	case kGDropOpen: {
		const int kind = arg & 15, slot = (arg >> 4) & 7;
		if (kind == kDropRace || kind == kDropBonus || kind == kDropColor) return SlotFrozen(g, g.slots[slot]);
		if (kind == kDropOption || kind == kDropPreset || kind == kDropSites || kind == kDropModOption || kind == kDropSpectators || kind == kDropSlot) {
			return g.host && LocalReady(g);
		}
		return false;
	}
	default: return false;
	}
}

void RedirectReplacedRaces(GameRoomSnapshot &g)
{
	if (!g.active) return;
	const bool load = g.online && (g.host ? !g_s.hostSave.name.empty() : !g_s.room.info.saveName.empty());
	for (int i = 0; i < 8; i++) {
		GameRoomSlot &s = g.slots[i];
		if (!s.used || !RaceEditable(g, s, load) || !RaceSeparator(s.raceNames)) continue;
		const int real = CivsRoomReplacement(s.race, static_cast<int>(s.raceNames.size()));
		if (real < 0) continue;
		static int logged = 0;
		if (logged++ < 8) {
			LogI("room", "slot %d had \"%s\", which a content pack replaces in the room; it now plays \"%s\"", i + 1, s.raceNames[s.race].c_str(),
				s.raceNames[real].c_str());
		}
		s.race = real;
		Predict(kPredRace, i, real);
		GameRoomSetRace(i, real);
	}
}

void ApplySaveLook(GameRoomSnapshot &g)
{
	if (!g.active || !g.online) return;
	const bool load = g.host ? !g_s.hostSave.name.empty() : !g_s.room.info.saveName.empty();
	if (!load) return;
	for (int i = 0; i < 8; i++) {
		GameRoomSlot &s = g.slots[i];
		if (!s.used) continue;
		int color = -1, race = -1;
		if (g.host) {
			color = g_s.saveColors[i];
			if (i < static_cast<int>(g_s.savePlayers.size()) && g_s.savePlayers[i].used()) race = RaceIdToCombo(g_s.savePlayers[i].type);
		}
		else {
			if (i < static_cast<int>(g_s.room.info.saveColors.size())) color = g_s.room.info.saveColors[i];
			if (i < static_cast<int>(g_s.room.info.saveRaces.size())) race = g_s.room.info.saveRaces[i];
		}
		if (color >= 0 && color < 12) s.color = color;
		if (race >= 0 && race < static_cast<int>(s.raceNames.size())) s.race = race;
	}
}

void ApplyPredicted(GameRoomSnapshot &g, std::vector<State::Predicted> &list)
{
	for (size_t i = 0; i < list.size();) {
		const State::Predicted &p = list[i];
		int *field = nullptr;
		int boolValue = 0;
		bool *flag = nullptr;
		GameRoomSlot &s = g.slots[p.index & 7];
		switch (p.kind) {
		case kPredTeam: field = &s.team; break;
		case kPredRace: field = &s.race; break;
		case kPredColor: field = &s.color; break;
		case kPredAi: field = &s.nameSel; break;
		case kPredReady: flag = &s.ready; break;
		default:
			for (GameRoomOption &o : g.options) {
				if (o.id != p.index) continue;
				if (p.kind == kPredOptSel) field = &o.sel;
				else flag = &o.value;
			}
		}
		if (flag) {
			boolValue = *flag ? 1 : 0;
			field = &boolValue;
		}
		if (!g.active || !field || *field == p.value || GetTickCount() - p.time > 1500) {
			list.erase(list.begin() + i);
			continue;
		}
		if (flag) *flag = p.value != 0;
		else *field = p.value;
		i++;
	}
}

GameMenuMsg_t g_origGameMenuMsg = nullptr;
UserQuitState g_userQuit;

unsigned __fastcall HookGameMenuMsg(void *dlg, void *edx, int *msg)
{
	if (msg && static_cast<unsigned>(msg[0x10 / 4]) == kMsgButtonClick) {
		void *sender = reinterpret_cast<void *>(msg[0xc / 4]);
		const int id = sender ? reinterpret_cast<GetId_t>((*reinterpret_cast<void ***>(sender))[0x1c / 4])(sender) : 0;
		if (id == 0x1005 || id == 0x1006) {
			g_userQuit.tick = GetTickCount();
			g_userQuit.id = id;
		}
	}
	return g_origGameMenuMsg(dlg, edx, msg);
}

void LeaveGameRoom()
{
	g_s.leftByUser = true;
	GameRoomCancel();
	if (g_s.game.online && g_s.room.id) {
		SteamLeaveLobby();
		g_s.room = OnlineRoom();
	}
}

bool SlotChangeBlocked(int slot, int newType)
{
	slot &= 7;
	const GameRoomSlot &s = g_s.game.slots[slot];
	int open = 0, joining = 0;
	for (int j = 0; j < 8; j++) {
		const GameRoomSlot &o = g_s.game.slots[j];
		if (o.exists && !o.used && !o.local && o.type == 0) open++;
		if (g_flow.arrivingIds[j] || o.joining) joining++;
	}
	if (g_flow.arrivingIds[slot] || s.joining) {
		ModUiToast("That slot is being taken by a player who is joining.");
		return true;
	}
	if (s.type == 0 && newType != 0 && open - 1 < joining) {
		ModUiToast("You can't close this slot: a player who is joining needs it.");
		return true;
	}
	return false;
}

void AbortJoin()
{
	g_autoJoin.active = false;
	g_autoJoin.selected = 0;
	g_s.connecting = false;
	g_s.classicPending = false;
	g_s.autoHost = false;
	g_flow.hostPending = false;
	if (g_flow.inJoin) g_flow.joinAbort = true;
	if (g_flow.inJoin || GetTickCount() - g_flow.lastLobbyUpdate < 1000) g_flow.backToMain = true;
}
}
