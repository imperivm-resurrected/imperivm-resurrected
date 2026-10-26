// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>
#include <atomic>

#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
OnMessage_t g_origMainMenu = nullptr;

ThisOnly_t g_origLobbyUpdate = nullptr;

JoinFlowState g_flow;
AutoJoinState g_autoJoin;

std::string SaveLabel(const SaveEntry &e) { return Tr("Load: ") + e.name + (e.online ? Tr(" (online)") : ""); }

ListGeom g_createModsGeomOut;
}

namespace mpui_impl {
namespace {
std::string ModRowText(const ModRow &r)
{
	return r.status.empty() ? r.name : r.name + "  \xC2\xB7  " + r.status;
}
}
}

namespace mpui_impl {
std::vector<ListLine> BuildModLines(const std::vector<ModRow> &rows, const std::string &emptyText)
{
	std::vector<ListLine> lines;
	std::vector<const ModRow *> rules, optional;
	for (const ModRow &r : rows) (r.kind == 2 ? rules : optional).push_back(&r);
	if (rows.empty()) {
		lines.push_back({Tr(emptyText), kDim, false});
		return lines;
	}
	if (!rules.empty()) {
		lines.push_back({Tr("Required content") + " (" + std::to_string(rules.size()) + ")", kGold, true});
		for (const ModRow *r : rules) lines.push_back({"\xE2\x80\xA2  " + ModRowText(*r), r->color, false});
	}
	if (!optional.empty()) {
		lines.push_back({Tr("Optional content") + " (" + std::to_string(optional.size()) + ")", kDim, true});
		for (const ModRow *r : optional) {
			const std::string tag = r->kind == 1 ? Tr("maps and campaigns") : Tr("cosmetic");
			lines.push_back({"\xE2\x80\xA2  " + r->name + "  \xC2\xB7  " + tag + (r->status.empty() ? "" : "  \xC2\xB7  " + r->status), r->color == kText ? kDim : r->color, false});
		}
	}
	return lines;
}

int PaintLinesInline(Painter &p, const std::vector<ListLine> &lines, int x, int y, int w, int rowH)
{
	for (const ListLine &l : lines) {
		p.Font(l.heading ? 4 : 3);
		p.Text(x, y, w, rowH, l.text, l.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += rowH;
	}
	return y;
}

std::mutex g_lock;
State g_s;
std::atomic<int> g_gameModal{0};

ChatPaneState g_chat;

DetailPaneState g_detail;
void *g_mainMenu = nullptr;
Canvas g_canvas;
}

namespace mpui_impl {
namespace {
const char *kMaps[] = {"Random map: Continent", "Random map: Islands", "Numantia", "Alesia", "Zama"};

const ModInfo *Installed(const State &st, const OnlineMod &m)
{
	for (const ModInfo &i : st.mods) {
		if (_stricmp(i.id.c_str(), m.id.c_str()) == 0) return &i;
	}
	return nullptr;
}

bool HasAllMods(const State &st, const OnlineLobby &g)
{
	for (const OnlineMod &m : g.mods) {
		if (!Installed(st, m)) return false;
	}
	return true;
}
}
}

namespace mpui_impl {
std::string SpectatorText(bool enabled, int delay)
{
	if (!enabled) return "No spectators";
	if (delay <= 0) return "Live";
	if (delay == 30) return "With a 30 s delay";
	if (delay == 60) return "With a 60 s delay";
	if (delay == 120) return "With a 120 s delay";
	return Tr("With ") + std::to_string(delay) + Tr(" s delay");
}

bool RoomHasNoSeat(const State &st)
{
	const OnlineLobby &info = st.room.info;
	return st.room.id && info.maxPlayers > 0 && info.players >= info.maxPlayers;
}
}

namespace mpui_impl {
namespace {
bool Contains(const std::string &text, const std::string &part)
{
	std::string a = text, b = part;
	for (char &c : a) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	for (char &c : b) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return a.find(b) != std::string::npos;
}
}
}

namespace mpui_impl {
std::vector<int> Visible(const State &st)
{
	std::vector<int> out;
	for (size_t i = 0; i < st.lobbies.size(); i++) {
		const OnlineLobby &g = st.lobbies[i];
		if (g.playing && !g.friendHost && !g.specLobby) continue;
		if (st.filter == 1 && !g.friendHost) continue;
		if (st.filter == 2 && g.password) continue;
		if (st.filter == 3 && !g.password) continue;
		if (st.filter == 4 && !g.mods.empty()) continue;
		if (st.filter == 5 && (g.mods.empty() || !HasAllMods(st, g))) continue;
		if (st.filter == 6 && (g.playing || g.players >= g.maxPlayers)) continue;
		if (st.filter == 7 && !(g.playing && g.specLobby)) continue;
		if (!st.searchInput.empty() && !Contains(g.name, st.searchInput) && !Contains(g.host, st.searchInput) &&
			!Contains(LobbyMapLabel(g), st.searchInput)) {
			continue;
		}
		out.push_back(static_cast<int>(i));
	}
	auto key = [&](const OnlineLobby &g) -> std::string {
		char num[16];
		switch (st.sortCol) {
		case 1: return g.host;
		case 2: sprintf_s(num, "%03d", g.maxPlayers - g.players); return num;
		case 3: return LobbyMapLabel(g);
		case 4: sprintf_s(num, "%03d", static_cast<int>(g.mods.size())); return num;
		default: return g.name;
		}
	};
	std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
		const std::string ka = key(st.lobbies[a]), kb = key(st.lobbies[b]);
		const int c = _stricmp(ka.c_str(), kb.c_str());
		if (c != 0) return st.sortDesc ? c > 0 : c < 0;
		return _stricmp(st.lobbies[a].name.c_str(), st.lobbies[b].name.c_str()) < 0;
	});
	return out;
}
}

namespace mpui_impl {
namespace {
const ModInfo *ExactMod(const State &st, const OnlineMod &m)
{
	for (const ModInfo &i : st.mods) {
		if (_stricmp(i.id.c_str(), m.id.c_str()) == 0 && i.version == m.version && ModsCompareRoomCopy(i, m.copyKey) != RoomCopy::kOther) return &i;
	}
	return nullptr;
}

bool OtherCopy(const State &st, const OnlineMod &m)
{
	for (const ModInfo &i : st.mods) {
		if (_stricmp(i.id.c_str(), m.id.c_str()) == 0 && i.version == m.version) return true;
	}
	return false;
}
}
}

namespace mpui_impl {
const ModInfo *OlderInstalled(const State &st, const OnlineMod &m)
{
	const ModInfo *best = nullptr;
	for (const ModInfo &i : st.mods) {
		if (_stricmp(i.id.c_str(), m.id.c_str()) != 0 || ModsCompareVersions(i.version, m.version) >= 0) continue;
		if (!best || ModsCompareVersions(i.version, best->version) > 0) best = &i;
	}
	return best;
}
}

namespace mpui_impl {
namespace {
std::vector<ModRow> OnlineModRows(const State &st, const std::vector<OnlineMod> &mods, bool withStatus)
{
	std::vector<ModRow> rows;
	for (const OnlineMod &m : mods) {
		ModRow r;
		r.name = m.name + (m.version.empty() || m.name.find(m.version) != std::string::npos ? "" : " " + m.version);
		r.kind = m.kind;
		if (withStatus) {
			const bool have = ExactMod(st, m) != nullptr;
			const bool other = !have && OtherCopy(st, m);
			const ModInfo *old = have || other ? nullptr : OlderInstalled(st, m);
			r.status = have ? Tr("you have it")
				: other ? Tr("you have another copy") + " \xC2\xB7 " + HumanBytes(m.bytes)
				: old ? Tr("you have ") + old->version + " \xC2\xB7 " + HumanBytes(m.bytes)
				: HumanBytes(m.bytes);
			r.color = have ? kOk : kGold;
		}
		rows.push_back(r);
	}
	return rows;
}
}
}

namespace mpui_impl {
// The game names its random maps "Random map: Continent", in its own language; the list of map types
// already says they are random, so only the part after the colon is shown.
std::string MapTypeLabel(const std::string &item)
{
	const size_t colon = item.find(": ");
	return colon == std::string::npos ? item : item.substr(colon + 2);
}

// The map chosen in the room, from the options the host publishes: the scenario, or the random map type
// without its "Random map: " part. Rooms made outside the room screen only have the name given at creation.
std::string LobbyMapLabel(const OnlineLobby &g)
{
	std::string type;
	for (size_t a = 0; a < g.optionsText.size();) {
		size_t b = g.optionsText.find('\n', a);
		if (b == std::string::npos) b = g.optionsText.size();
		const std::string line = g.optionsText.substr(a, b - a);
		if (line.compare(0, 10, "Scenario: ") == 0 && line.size() > 10) return line.substr(10);
		if (line.compare(0, 10, "Map type: ") == 0) type = line.substr(10);
		a = b + 1;
	}
	if (!type.empty()) return MapTypeLabel(type);
	return g.map.empty() || g.map == "Chosen in the room" ? Tr("In the room") : g.map;
}

uint64_t LobbyPlayerId(const OnlineLobby &g, const std::string &name)
{
	if (g.owner && PlayerNameMatches(name, g.host)) return g.owner;
	for (size_t i = 0; i < g.playerIds.size(); i++) {
		if (PlayerNameMatches(name, g.playerNames[i])) return g.playerIds[i];
	}
	return 0;
}

std::vector<ListLine> OnlineLobbyLines(const State &st, const OnlineLobby &g, bool withStatus)
{
	std::vector<ModRow> rows = OnlineModRows(st, g.mods, withStatus);
	if (g.modsRequiredOnly) {
		for (ModRow &r : rows) r.kind = 2;
	}
	std::vector<ListLine> lines = BuildModLines(rows, "None: the original game");
	return lines;
}

std::vector<OnlineMod> MissingMods(const State &st, const OnlineLobby &g)
{
	std::vector<OnlineMod> out;
	for (const OnlineMod &m : g.mods) {
		if (!ExactMod(st, m)) out.push_back(m);
	}
	return out;
}

std::string g_syncResult;

ListGeom g_pickGeomOut;
}

std::vector<uint32_t> g_bg;
