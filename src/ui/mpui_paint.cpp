// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "mods/modoptions.h"
#include "game/civs.h"
#include "net/rejoin.h"

#include "ui/leagueui.h"
#include "ui/mpui_internal.h"
#include "ui/patronui.h"

using namespace mpui_impl;

namespace mpui_impl {
void PaintField(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, const std::string &placeholder,
	bool focused, int action, bool secret)
{
	PaintEditField(p, x, y, w, h, text, placeholder, focused, secret, st.edit, action);
}
}

namespace mpui_impl {
namespace {
std::string ModsColumn(const State &st, const OnlineLobby &g, COLORREF &color)
{
	color = pal::textDim;
	const std::vector<OnlineMod> missing = MissingMods(st, g);
	if (!missing.empty()) {
		uint64_t bytes = 0;
		for (const OnlineMod &m : missing) bytes += m.bytes;
		color = pal::redText;
		return Tr("Missing ") + std::to_string(missing.size()) + " (" + HumanBytes(bytes) + ")";
	}
	if (SteamRoomContent(g, st.ownKey) == RoomContent::kOtherGameplay) {
		color = pal::redText;
		return "Restart";
	}
	if (g.mods.empty()) return "Original";
	color = pal::ok;
	return "OK";
}
}
}

namespace mpui_impl {
bool PaintAvatar(Painter &p, int x, int y, int size, uint64_t id)
{
	const std::shared_ptr<const OnlineAvatar> a = SteamAvatar(id);
	if (!a) return false;
	p.Image(x, y, size, size, a->bgra.data(), a->width, a->height);
	return true;
}
}

namespace mpui_impl {
namespace {
const ModNeeds &CreateSaveNeeds(const std::string &vfsPath, const std::string &profile)
{
	static std::string key;
	static ModNeeds needs;
	static DWORD when = 0;
	const std::string k = vfsPath + "|" + profile;
	if (k != key || GetTickCount() - when > 5000) {
		key = k;
		when = GetTickCount();
		needs = ModsNeededFor(vfsPath, profile);
	}
	return needs;
}
}
}

namespace mpui_impl {
std::string SaveSlotNameFromOptions(const std::string &opts, int i)
{
	const size_t start = opts.find("Players in the saved game:");
	if (start == std::string::npos) return "";
	const std::string key = "\n" + std::to_string(i + 1) + ". ";
	const size_t p = opts.find(key, start);
	if (p == std::string::npos) return "";
	const size_t b = p + key.size(), e = opts.find('\n', b);
	const std::string name = opts.substr(b, e == std::string::npos ? std::string::npos : e - b);
	return name == "AI" ? "" : name;
}

uint64_t MemberIdByName(const State &st, const std::string &name, bool skipMe)
{
	for (const OnlineMember &m : st.room.members) {
		if (!(skipMe && m.me) && SamePlayerName(m.name, name)) return m.id;
	}
	for (const OnlineMember &m : st.room.members) {
		if (!(skipMe && m.me) && PlayerNameMatches(m.name, name)) return m.id;
	}
	return 0;
}

}

namespace mpui_impl {
namespace {
const int kCardGap = 32, kToolH = 44;

// Where the parts of the lobby go, in the units of ScreenView: the header of the full screens, a row of tools under it, then the
// table of rooms with the card of the selected one at its right.
struct LobbyLayout : ScreenLayout {
	int toolY = 0, bodyY = 0, bodyH = 0, tableW = 0, detailW = 0;
};

LobbyLayout LobbyLayoutFor(int w, int h)
{
	LobbyLayout l;
	static_cast<ScreenLayout &>(l) = ScreenLayoutFor(w, h);
	l.toolY = l.top;
	l.bodyY = l.toolY + kToolH + (l.roomy ? 28 : 20);
	l.bodyH = l.h - l.margin - l.bodyY;
	const int inner = l.w - 2 * l.margin;
	l.detailW = std::max(420, std::min(560, inner * 3 / 10));
	l.tableW = inner - l.detailW - kCardGap;
	return l;
}
}

int LobbyTableWidth(int viewW, int viewH)
{
	return LobbyLayoutFor(viewW, viewH).tableW;
}

void PaintHeader(Painter &p, const State &st)
{
	// The result of a ranked game has a header of its own.
	if (st.ranked.resultOpen) return;
	const int mx = st.mouseX, my = st.mouseY;
	const LobbyLayout l = LobbyLayoutFor(st.viewW, st.viewH);
	const int right = PaintRankedPill(p, st, PaintHeaderActions(p, l, {{"Back", kBack, true, true}}, mx, my));
	PaintScreenTitle(p, l, "Multiplayer",
		{{"Games", kListView, 0, st.view == kViewList, true}, {"Create game", kCreate, 0, st.view == kViewCreate, true},
			{"Ranked", kRanked, 0, st.view == kViewRanked, true}},
		right, mx, my);

	// The caption line is the player's: avatar, name (with the laurel of a patron), the content profile and the open rooms.
	const int m = l.margin, y = l.infoY, h = l.infoH, avatar = h + 4, ay = y - 2;
	const PatronInfo own = PatronOf(SteamOwnId());
	const LeagueBadge ownBadge = PlayerBadgeOf(SteamOwnId());
	p.Font(1);
	const int laurel = own.tier != PatronTier::None ? 22 : 0, nameX = m + avatar + 12 + (laurel ? laurel + 4 : 0);
	const int nameW = std::min(320, p.TextWidth(st.player) + 4);
	DrawPatronRowGlow(p, m - 12, y - 8, nameX + nameW + 12 - (m - 12), h + 16, own.tier);
	if (!PaintAvatar(p, m, ay, avatar, SteamOwnId())) {
		p.Fill(m, ay, avatar, avatar, pal::hairline);
		p.Font(3);
		p.Text(m, ay, avatar, avatar, st.player.substr(0, 2), pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(1);
	}
	p.Frame(m, ay, avatar, avatar, pal::redText);
	if (laurel) {
		DrawPatron(p, m + avatar + 12, y + (h - laurel) / 2, laurel, nameX, y, nameW, h, st.player, own.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		PatronHotspot(p, m + avatar + 12, y + (h - laurel) / 2, laurel, own, mx, my, kPatronLink);
	}
	else p.Text(nameX, y, nameW, h, st.player, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DrawBadgeAfterName(p, nameX, y, nameW, h, st.player, ownBadge, mx, my);
	std::string info = Tr("Content profile: ") + ModsProfileLabel(ModsMountedProfile().name);
	COLORREF color = pal::caption;
	if (!st.ready) {
		info += "  \xC2\xB7  " + Tr(st.status.empty() ? std::string("Connecting to Steam...") : st.status);
		color = pal::bad;
	}
	else if (!st.room.id) {
		const size_t n = st.lobbies.size();
		info += "  \xC2\xB7  " + (n == 1 ? Tr("1 open room") : std::to_string(n) + Tr(" open rooms"));
	}
	p.Font(0);
	const int infoX = nameX + nameW + LeagueBadgeRoom(ownBadge) + 20;
	p.Text(infoX, y, l.w - m - infoX, h, "\xC2\xB7  " + info, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
}

namespace mpui_impl {
namespace {
void DrawLock(Painter &p, int x, int y, COLORREF c)
{
	p.Frame(x + 2, y, 8, 9, c);
	p.Fill(x, y + 7, 12, 9, c);
	p.Fill(x + 5, y + 10, 2, 4, pal::panel);
}

void PaintLobbyDetail(Painter &p, const State &st, const std::vector<int> &vis, const LobbyLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int dw = l.detailW, dx = l.w - l.margin - dw, dy = l.bodyY, dh = l.bodyH;
	PaintGlassCard(p, dx, dy, dw, dh);
	{
		std::lock_guard<std::mutex> lock(g_chat.mx);
		g_detail.rect = p.R(dx, dy, dw, dh);
		OffsetRect(&g_detail.rect, p.ox, p.oy);
		g_detail.rectValid = true;
	}
	if (st.selected < 0 || st.selected >= static_cast<int>(vis.size())) {
		g_detail.geomOut = ListGeom();
		p.Font(0);
		p.TextWrapped(dx + 24, dy + 24, dw - 48, 120,
			vis.empty() ? "When there are rooms, pick one to see its content and join, or press \"Quick game\" and we'll find "
						  "one for you."
						: "Pick a room from the list to see its content and join.",
			pal::textDim);
		return;
	}
	const OnlineLobby &g = st.lobbies[vis[st.selected]];
	const bool full = g.players >= g.maxPlayers;
	const bool same = SteamRoomContent(g, st.ownKey) != RoomContent::kOtherGameplay;
	const std::vector<OnlineMod> missing = MissingMods(st, g);
	uint64_t bytes = 0;
	for (const OnlineMod &m : missing) bytes += m.bytes;

	const bool canReturn = CanReturnToGame(st, g);
	const int returnRoom = canReturn && g.specLobby ? 60 : 0;
	const int viewTop = dy + 1, viewH = dh - 80 - returnRoom;
	const int x = dx + 24, w = dw - 52;
	const int startY = viewTop + 16;
	int y = startY - st.detail.scroll;
	p.Clip(dx + 1, viewTop, dw - 2, viewH);

	auto section = [&](const std::string &title) {
		y += 8;
		p.Font(1);
		p.Text(x, y, w, 28, title, pal::text);
		y += 30;
		p.Fill(x, y, w, 1, pal::hairline);
		y += 10;
		p.Font(3);
	};
	auto note = [&](COLORREF bar, COLORREF back, const std::string &text) {
		p.Font(3);
		const int th = p.Measure(w - 28, text);
		p.Fill(x, y, w, th + 18, back);
		p.Fill(x, y, 3, th + 18, bar);
		p.TextWrapped(x + 14, y + 9, w - 28, th + 2, text, pal::text);
		y += th + 28;
	};

	p.Font(2);
	const int nameH = std::max(40, p.Measure(w, g.name));
	p.TextWrapped(x, y, w, nameH, g.name, pal::redText);
	y += nameH;

	section("Game content");
	y = PaintLinesInline(p, OnlineLobbyLines(st, g, true), x, y, w, 26) + 8;
	if (!same) {
		y += 2;
		note(pal::bad, pal::selected,
			missing.empty() ? "Your content doesn't match the room's. The game will restart with the room's."
							: std::string(missing.size() == 1 ? Tr("You don't have the content this room needs. The game will download ")
															  : Tr("You don't have the content this room needs. The game will download these: ")) +
								std::to_string(missing.size()) + (missing.size() == 1 ? Tr(" content pack") : Tr(" content packs")) + " (" + HumanBytes(bytes) + Tr(") and restart."));
	}
	if (g.password) note(pal::redText, pal::raised, "Password protected");

	if (!g.saveName.empty()) note(pal::redText, pal::raised, Tr("Continues the saved game \"") + g.saveName + "\"");
	if (canReturn) note(pal::ok, RGB(26, 36, 22), "You were in this game and your seat is still free: you can return to it.");

	section("Players");
	if (!g.slots.empty()) {
		int n = 0;
		bool hostShown = false;
		for (const OnlineSlot &s : g.slots) {
			if (s.type == 2) continue;
			n++;
			const bool human = s.type == 3, ai = s.type == 1;
			p.Text(x, y, 28, 26, std::to_string(n), pal::textDim);
			const uint64_t pid = human ? LobbyPlayerId(g, s.name) : 0;
			const PatronInfo patron = human ? PatronOf(pid) : PatronInfo();
			const LeagueBadge badge = pid ? PlayerBadgeOf(pid) : LeagueBadge();
			if (patron.tier != PatronTier::None) {
				const int nameW = w - 54 - 90 - LeagueBadgeRoom(badge);
				DrawPatron(p, x + 30, y + 3, 20, x + 54, y, nameW, 26, s.name, patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				PatronHotspot(p, x + 30, y + 3, 20, patron, st.mouseX, st.mouseY, kPatronLink);
				DrawBadgeAfterName(p, x + 54, y, nameW, 26, s.name, badge, st.mouseX, st.mouseY);
			}
			else {
				const int nameW = w - 30 - 90 - LeagueBadgeRoom(badge);
				p.Text(x + 30, y, nameW, 26, human ? s.name : ai ? (s.name.empty() ? "AI" : s.name) : "Free", human ? pal::text : pal::textDim);
				if (human) DrawBadgeAfterName(p, x + 30, y, nameW, 26, s.name, badge, st.mouseX, st.mouseY);
			}
			if (human && LobbyPlayerId(g, s.name)) {
				const int nameX = patron.tier != PatronTier::None ? x + 54 : x + 30;
				ProfileHotspot(p, st, nameX, y, w - (nameX - x) - 90, 26, s.name, kProfileLobbySlot + static_cast<int>(&s - g.slots.data()));
			}
			if (human && !hostShown && PlayerNameMatches(s.name, g.host)) {
				hostShown = true;
				p.Text(x + w - 100, y, 100, 26, "host", pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			}
			y += 28;
		}
	}
	else if (g.playerNames.empty()) {
		p.Text(x, y, w, 26, std::to_string(g.players) + Tr(" of ") + std::to_string(g.maxPlayers) + Tr(" slots taken"), pal::textDim);
		y += 30;
	}
	else {
		for (int i = 0; i < std::max(g.maxPlayers, static_cast<int>(g.playerNames.size())); i++) {
			const bool taken = i < static_cast<int>(g.playerNames.size());
			p.Text(x, y, 28, 26, std::to_string(i + 1), pal::textDim);
			const uint64_t pid = taken ? LobbyPlayerId(g, g.playerNames[i]) : 0;
			const PatronInfo patron = taken ? PatronOf(pid) : PatronInfo();
			const LeagueBadge badge = pid ? PlayerBadgeOf(pid) : LeagueBadge();
			if (patron.tier != PatronTier::None) {
				const int nameW = w - 54 - 90 - LeagueBadgeRoom(badge);
				DrawPatron(p, x + 30, y + 3, 20, x + 54, y, nameW, 26, g.playerNames[i], patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				PatronHotspot(p, x + 30, y + 3, 20, patron, st.mouseX, st.mouseY, kPatronLink);
				DrawBadgeAfterName(p, x + 54, y, nameW, 26, g.playerNames[i], badge, st.mouseX, st.mouseY);
			}
			else {
				const int nameW = w - 30 - (i == 0 ? 90 : 0) - LeagueBadgeRoom(badge);
				p.Text(x + 30, y, nameW, 26, taken ? g.playerNames[i] : "Free", taken ? pal::text : pal::textDim);
				if (taken) DrawBadgeAfterName(p, x + 30, y, nameW, 26, g.playerNames[i], badge, st.mouseX, st.mouseY);
			}
			if (taken && LobbyPlayerId(g, g.playerNames[i])) {
				const int nameX = patron.tier != PatronTier::None ? x + 54 : x + 30;
				ProfileHotspot(p, st, nameX, y, w - (nameX - x) - 90, 26, g.playerNames[i], kProfileLobbyName + i);
			}
			if (taken && i == 0) p.Text(x + w - 100, y, 100, 26, "host", pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			y += 28;
		}
	}

	section("Options");
	{
		// Same rows, labels and order as the room's "Game options" panel (mpui_paint_room.cpp), read from the text the host publishes.
		std::map<std::string, std::string> pub;
		std::vector<std::string> published;
		for (size_t from = 0; from < g.optionsText.size();) {
			size_t to = g.optionsText.find('\n', from);
			if (to == std::string::npos) to = g.optionsText.size();
			const std::string line = g.optionsText.substr(from, to - from);
			from = to + 1;
			const size_t colon = line.find(": ");
			if (colon == std::string::npos) continue;
			const std::string key = line.substr(0, colon);
			if (!pub.count(key)) published.push_back(key);
			pub[key] = line.substr(colon + 2);
		}
		const bool load = !g.saveName.empty();
		std::set<std::string> used = {"Shared support", "Scenario"};
		auto take = [&](const std::string &key, std::string &out) {
			used.insert(key);
			const auto it = pub.find(key);
			if (it == pub.end()) return false;
			out = it->second;
			return true;
		};
		auto heading = [&](const char *title) {
			y += 6;
			p.Font(4);
			p.Text(x, y, w, 20, title, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			p.Font(3);
			y += 24;
		};
		auto valueRow = [&](const std::string &label, const std::string &value, COLORREF color) {
			p.Text(x, y, w / 2, 26, label, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Text(x + w / 2, y, w - w / 2, 26, value, color, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 28;
		};
		auto toggleRow = [&](const std::string &label, bool on) {
			p.Text(x, y, w - kBigToggleW - 12, 26, label, on ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			PaintBigToggle(p, x + w - kBigToggleW, y + (26 - kBigToggleH) / 2, on, true, false);
			y += 28;
		};
		auto comboRow = [&](const char *label, const char *key) {
			std::string v;
			if (take(key, v)) valueRow(label, v.empty() ? "—" : v, pal::text);
		};
		auto checkRow = [&](const char *label, const char *key, bool inverted) {
			std::string v;
			if (take(key, v)) toggleRow(label, (v == "yes") != inverted);
		};
		auto yes = [&](const char *key) {
			const auto it = pub.find(key);
			return it != pub.end() && it->second == "yes";
		};
		static const struct {
			unsigned bit;
			const char *key, *name;
		} kSites[] = {{kSiteRuins, "No ruins", "Ruins"}, {kSiteCaves, "No caves", "Caves"}, {kSiteItems, "No item sites", "Items"},
			{kSiteStrongholds, "No neutral strongholds", "Strongholds"}};
		bool randomMap = false;
		unsigned sites = 0;
		for (const auto &site : kSites) {
			randomMap = randomMap || pub.count(site.key) != 0;
			if (yes(site.key)) sites |= site.bit;
		}
		const unsigned rules = (yes("Hidden civilizations") ? kRuleBlindPick : 0) | (yes("Free unit attachment") ? kRuleFreeAttach : 0) |
			(yes("Diplomacy locked") ? kRuleLockDiplomacy : 0) | (yes("Cheats") ? kRuleCheats : 0);
		const std::vector<ModOption> &mods = ModOptions();

		if (!load && pub.count("Starting gold")) {
			const int preset = MatchPreset(sites, rules, [&](int, const char *key) {
				const auto it = pub.find(key);
				return it == pub.end() ? std::string("—") : it->second;
			});
			valueRow("Preset", preset >= 0 ? kPresets[preset].name : "Custom setup", preset != 0 ? kSpecial : pal::text);
		}
		if (!pub.empty()) {
			heading("MAP");
			{
				std::string v, scenario;
				if (take("Map type", v)) valueRow("Map type", take("Scenario", scenario) && !scenario.empty() ? Tr("Scenario: ") + scenario : MapTypeLabel(v), pal::text);
			}
			comboRow("Size", "Map size");
			for (const auto &site : kSites) used.insert(site.key);
			if (!load) {
				std::string removed;
				for (const auto &site : kSites) {
					if (sites & site.bit) removed += (removed.empty() ? "" : ", ") + Tr(site.name);
				}
				valueRow("Remove from map", !randomMap ? Tr("Only on random maps") : removed.empty() ? Tr("None") : removed,
					!randomMap ? pal::textDim : removed.empty() ? pal::text : kSpecial);
			}
			for (const ModOption &mo : mods) {
				if (mo.id != kSeasonOptionId) continue;
				std::string v;
				const bool has = take(mo.name, v);
				if (load) continue;
				if (!randomMap) valueRow(mo.name, Tr("Only on random maps"), pal::textDim);
				else if (has) valueRow(mo.name, v, !mo.values.empty() && v != mo.values[0] ? kSpecial : pal::text);
			}
		}
		heading("GAME");
		comboRow("Mode", "Game type");
		comboRow("Limit", "Limit");
		comboRow("Population", "Population");
		comboRow("Starting gold", "Starting gold");
		{
			const bool watchable = g.specLobby != 0;
			std::string spec = SpectatorText(watchable, g.specDelay);
			if (watchable && g.playing && g.spectators > 0) spec += "  ·  " + std::to_string(g.spectators) + Tr(" watching");
			valueRow("Spectators", spec, watchable ? pal::text : pal::textDim);
		}
		if (!pub.empty()) {
			heading("RULES");
			checkRow("Fog of war", "No fog", true);
			checkRow("Explored map", "Map pre-explored", false);
			checkRow("Shared control", "Shared control", false);
			checkRow("Bonuses", "Disable bonuses", true);
			if (!load) checkRow("Hide civilizations", "Hidden civilizations", false);
			else used.insert("Hidden civilizations");
			checkRow("Free unit attachment", "Free unit attachment", false);
			checkRow("Lock diplomacy", "Diplomacy locked", false);
			checkRow("Allow cheats", "Cheats", false);
			for (const ModOption &mo : mods) {
				if (mo.id == kSeasonOptionId) continue;
				std::string v;
				if (!take(mo.name, v)) continue;
				if (mo.values.empty()) toggleRow(mo.name, v == "yes");
				else valueRow(mo.name, v, v != mo.values[0] ? kSpecial : pal::text);
			}
			// Options of mods this PC doesn't have, as the host wrote them.
			for (const std::string &key : published) {
				if (used.count(key)) continue;
				const std::string &v = pub[key];
				if (v == "yes" || v == "no") toggleRow(key, v == "yes");
				else valueRow(key, v, pal::text);
			}
		}
	}
	p.Unclip();

	const int total = (y + st.detail.scroll) - startY + 12;
	{
		ListState view = st.detail;
		view.scroll = std::min(st.detail.scroll, std::max(0, total - viewH + 12));
		ListGeom geom;
		ScrollbarPaint(p, dx + dw - 14, viewTop, viewH, total + 12, viewH, view, geom, mx, my);
		g_detail.geomOut = geom;
	}

	p.Fill(dx + 1, dy + dh - 76 - returnRoom, dw - 2, 1, pal::hairline);
	const int bx = dx + 24, bw = dw - 48, bh = 52, by = dy + dh - 64;
	if (canReturn) p.Button(bx, by - returnRoom, bw, bh, "Return to the game", kReturnGame, 0, mx, my, st.ready && !RejoinBusy(), true);
	const std::string label = g.kickedMe ? "You were kicked from the room" : g.playing ? "Game in progress" : full ? "Room full" : same ? "Join" : missing.empty() ? "Restart and join" : "Download and join";
	if (g.specOnly) {
		if (g.playing) p.Button(bx, by, bw, bh, "Watch the game", kSpectate, 0, mx, my, st.ready, true);
		else p.Button(bx, by, bw, bh, "Room full", kJoin, 0, mx, my, false, false);
	}
	else if (g.playing && g.specLobby) {
		p.Button(bx, by, bw, bh, "Watch the game", kSpectate, 0, mx, my, st.ready, !canReturn);
	}
	else if (!canReturn) {
		p.Button(bx, by, bw, bh, label, kJoin, 0, mx, my, !g.playing && !full && !g.kickedMe && st.ready, true);
	}
}
}
}

namespace mpui_impl {
// The name underlines under the mouse, as a link does, and a click opens the player's Steam profile.
void ProfileHotspot(Painter &p, const State &st, int x, int y, int w, int h, const std::string &name, int arg)
{
	const int textW = std::min(w, p.TextWidth(name));
	if (textW <= 0) return;
	p.AddHit(x, y, textW, h, kProfile, arg);
	if (p.Hover(x, y, textW, h, st.mouseX, st.mouseY)) p.Fill(x, y + h / 2 + 9, textW, 1, pal::redText);
}

void PaintPassPrompt(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 480, 214, "Password-protected room", pal::redText, 26);
	DrawLock(p, d.x + 24, d.y + 22, pal::redText);
	DialogText(p, d, 58, "\"" + st.passLobby.name + Tr("\", hosted by ") + st.passLobby.host + Tr(". Enter the password to join."), pal::textDim, 3);
	PaintField(p, st, d.x + 24, d.y + 104, d.w - 48 - 98, 34, st.joinPassInput, "Password", st.focus == kJoinPassField, kFieldJoinPass, !st.passShow);
	p.Button(d.x + d.w - 24 - 90, d.y + 104, 90, 34, st.passShow ? "Hide" : "Show", kPassShow, 0, mx, my);
	DialogButtons(p, d, {{"OK", kPassOk, 0, true}, {"Cancel", kPassCancel}}, mx, my);
}

void PaintList(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LobbyLayout l = LobbyLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, toolY = l.toolY;
	static const char *const kFilterNames[] = {"All rooms", "Friends'", "No password", "Password protected", "Original game", "With my content", "With open slots", "In progress"};
	const int filterW = 260;
	Combo(p, st, m, toolY, filterW, kToolH, kFilterNames[st.filter < 0 || st.filter > 7 ? 0 : st.filter], kDropFilter, 0, true);
	// Refresh closes the row over the table; the main action, Quick game, sits over the room's card and its Join.
	const int refreshW = std::max(GhostButtonWidth(p, "Refresh", false), GhostButtonWidth(p, "Searching...", false));
	const int refreshX = m + l.tableW - refreshW, searchX = m + filterW + 12;
	PaintField(p, st, searchX, toolY, std::min(440, refreshX - 12 - searchX), kToolH, st.searchInput, "Search room, host or map...", st.focus == kSearchField,
		kFieldSearch);
	PaintGhostButton(p, refreshX, toolY, refreshW, kToolH, st.loading ? "Searching..." : "Refresh", kRefresh, false, st.ready && !st.loading, mx, my);
	p.Button(l.w - m - l.detailW, toolY, l.detailW, kToolH, "Quick game", kQuick, 0, mx, my, st.ready, true);

	const int tx = m, tw = l.tableW, ty = l.bodyY, th = l.bodyH, headH = 44, rowH = l.roomy ? 52 : 46;
	PaintGlassCard(p, tx, ty, tw, th);
	static const char *const kCols[] = {"Room", "Host", "Players", "Map", "Content"};
	const float unit = static_cast<float>(tw) / kColumnBaseW;
	int cx[6];
	cx[0] = tx + 20;
	for (int c = 0; c < 4; c++) cx[c + 1] = cx[c] + static_cast<int>(st.colW[c] * unit + 0.5f);
	cx[5] = tx + tw - 20;
	// Where a column's text starts: clear of the separator drawn at its left edge.
	auto textX = [&](int c) { return cx[c] + (c > 0 ? 14 : 2); };
	p.Fill(tx + 1, ty + headH - 1, tw - 2, 1, pal::lineStrong);
	p.Font(4);
	for (int c = 0; c < 5; c++) {
		const int w = cx[c + 1] - cx[c];
		const bool sorted = st.sortCol == c, hov = p.Hover(cx[c], ty + 1, w - 4, headH - 2, mx, my);
		p.TextCaps(textX(c), ty + 1, cx[c + 1] - textX(c) - 28, headH - 2, kCols[c], sorted ? pal::redText : hov ? pal::text : pal::caption,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
		if (sorted) {
			if (st.sortDesc) p.ArrowDown(cx[c + 1] - 22, ty + headH / 2 - 2, 10, 5, pal::redText);
			else p.ArrowUp(cx[c + 1] - 22, ty + headH / 2 - 3, 10, 5, pal::redText);
		}
		p.AddHit(cx[c], ty + 1, w - 4, headH - 2, kSort, c);
		if (c < 4) {
			const bool grab = st.resizeCol == c || p.Hover(cx[c + 1] - 5, ty + 1, 10, headH - 2, mx, my);
			p.Fill(cx[c + 1] - 1, ty + 12, 1, headH - 24, grab ? pal::redText : pal::lineStrong);
			p.AddHit(cx[c + 1] - 5, ty + 1, 10, headH - 2, kColResize, c);
		}
	}
	const std::vector<int> vis = Visible(st);
	{
		std::vector<uint64_t> ids;
		for (int i : vis) {
			const OnlineLobby &g = st.lobbies[i];
			if (g.owner) ids.push_back(g.owner);
			ids.insert(ids.end(), g.playerIds.begin(), g.playerIds.end());
		}
		PatronsWatch(ids);
	}
	const int rowsTop = ty + headH;
	if (vis.empty()) {
		p.Font(0);
		const std::string empty = !st.ready ? (st.status.empty() ? "Connecting to Steam..." : st.status)
			: st.loading ? "Searching for rooms..."
			: "There are no open rooms with this filter. Create one with \"Create game\" and invite your friends.";
		p.TextWrapped(tx + 24, rowsTop + 24, tw - 48, 120, empty, pal::textDim);
	}
	// The table has no scroll of its own: when the arrows take the selection past the rows that fit, the rows follow it.
	const int fit = std::max(1, (th - headH - 1) / rowH), count = static_cast<int>(vis.size());
	const int first = std::max(0, std::min(st.selected - fit + 1, count - fit));
	for (int r = first; r < count && r < first + fit; r++) {
		const OnlineLobby &g = st.lobbies[vis[r]];
		const int y = rowsTop + (r - first) * rowH;
		if (r == st.selected) {
			p.Fill(tx + 1, y, tw - 2, rowH, pal::selected);
			p.Fill(tx + 1, y, 3, rowH, pal::red);
		}
		else if (p.Hover(tx, y, tw, rowH, mx, my)) p.Fill(tx + 1, y, tw - 2, rowH, pal::raised);
		p.Fill(tx + 1, y + rowH - 1, tw - 2, 1, pal::hairline);
		p.AddHit(tx, y, tw, rowH, kSelect, r);
		auto cell = [&](int c, const std::string &text, COLORREF color) {
			p.Text(textX(c), y, cx[c + 1] - textX(c) - 12, rowH, text, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		};
		p.Font(1);
		if (g.password) DrawLock(p, cx[0] + 4, y + (rowH - 16) / 2, pal::redText);
		const int lockW = g.password ? 22 : 0;
		p.Text(cx[0] + 2 + lockW, y, cx[1] - cx[0] - 12 - lockW, rowH, g.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(0);
		const PatronInfo hostPatron = PatronOf(g.owner);
		const LeagueBadge hostBadge = PlayerBadgeOf(g.owner);
		const std::string hostText = g.friendHost ? g.host + Tr("  (friend)") : g.host;
		const int hx = textX(1), hw = cx[2] - textX(1) - 12;
		if (hostPatron.tier != PatronTier::None) {
			const int ly = y + (rowH - 20) / 2, nameW = hw - 24 - LeagueBadgeRoom(hostBadge);
			DrawPatron(p, hx, ly, 20, hx + 24, y, nameW, rowH, hostText, hostPatron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			PatronHotspot(p, hx, ly, 20, hostPatron, mx, my, kPatronLink);
			DrawBadgeAfterName(p, hx + 24, y, nameW, rowH, hostText, hostBadge, mx, my);
		}
		else {
			const int nameW = hw - LeagueBadgeRoom(hostBadge);
			p.Text(hx, y, nameW, rowH, hostText, g.friendHost ? pal::redText : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			DrawBadgeAfterName(p, hx, y, nameW, rowH, hostText, hostBadge, mx, my);
		}
		char watching[48];
		snprintf(watching, sizeof(watching), "In game  ·  %d watching", g.spectators);
		cell(2, g.playing ? (g.spectators > 0 ? std::string(watching) : std::string("In game")) : std::to_string(g.players) + " / " + std::to_string(g.maxPlayers),
			g.players < g.maxPlayers && !g.playing ? pal::text : pal::textDim);
		cell(3, LobbyMapLabel(g), pal::textDim);
		COLORREF color;
		const std::string mods = ModsColumn(st, g, color);
		cell(4, mods, color);
	}
	PaintLobbyDetail(p, st, vis, l);
}

void PaintRanked(Painter &p, const State &st)
{
	if (PaintRankedTab(p, st)) return;
	const int mx = st.mouseX, my = st.mouseY;
	const LobbyLayout l = LobbyLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, top = l.top, h = l.h - m - top, pad = l.roomy ? 32 : 24;
	const int cw = std::min(560, std::max(440, (l.w - 2 * m) * 3 / 10));
	LeagueMeRefresh();
	PatronsWatch({SteamOwnId()});
	PaintGlassCard(p, m, top, cw, h);
	PaintLeagueAccount(p, m + pad, top + pad, cw - 2 * pad, {kLeagueLink, kLeagueOpen, kLeagueCancel, kLeagueUnlink}, mx, my);

	// The ranks of the league, with the emblems players will wear next to their names.
	const int lx = m + cw + kCardGap, lw = l.w - m - lx, ix = lx + pad, iw = lw - 2 * pad;
	PaintGlassCard(p, lx, top, lw, h);
	PaintSectionLabel(p, ix, top + pad, iw, 22, "Ranks");
	int y = top + pad + 34;
	p.Font(0);
	const std::string intro = "Ranked 1 vs 1 is on its way. Six ranks of three divisions, 100 Glory each; above them, Praetorian and Imperator go to the best "
							  "of the leaderboard.";
	const int introH = p.Measure(iw, intro);
	p.TextWrapped(ix, y, iw, introH + 2, intro, pal::textDim);
	y += introH + 24;
	const LeagueMeView me = LeagueMeState();
	const LeagueTier mine = me.loaded && me.me.ranked.present ? me.me.ranked.tier : LeagueTier::None;
	const int cols = 4, gap = 16;
	const int cellW = (iw - gap * (cols - 1)) / cols, rowRoom = (top + h - pad - y - gap) / 2;
	const int emblem = std::max(40, std::min({cellW - 32, rowRoom - 84, 136}));
	const int cellH = emblem + 84;
	for (int i = 0; i < kLeagueTierCount; i++) {
		const LeagueTier tier = static_cast<LeagueTier>(i + 1);
		const int cx = ix + (i % cols) * (cellW + gap), cy = y + (i / cols) * (cellH + gap);
		const bool own = tier == mine;
		p.Fill(cx, cy, cellW, cellH, own ? pal::selected : pal::raised);
		p.Frame(cx, cy, cellW, cellH, own ? LeagueTierInk(tier) : pal::hairline);
		DrawLeagueEmblem(p, cx + (cellW - emblem) / 2, cy + 12, emblem, tier);
		p.Font(2);
		p.TextCaps(cx + 8, cy + 16 + emblem, cellW - 16, 34, LeagueTierName(tier), LeagueTierInk(tier), DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
		const int from = LeagueTierGloryFrom(tier);
		p.Font(3);
		p.raw = true;
		p.Text(cx + 8, cy + 52 + emblem, cellW - 16, 22,
			from >= 0 ? TrFmt("From %s Glory", {std::to_string(from)}) : Tr(tier == LeagueTier::Imperator ? "Places 1 to 10" : "Places 11 to 50"),
			own ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (own) {
			p.Font(4);
			p.TextCaps(cx + 10, cy + 8, cellW - 20, 18, "You", LeagueTierInk(tier), DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 2);
		}
	}
}

void PaintCreate(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LobbyLayout l = LobbyLayoutFor(st.viewW, st.viewH);
	const int w = std::min(l.w - 2 * l.margin, 820), x = (l.w - w) / 2, y0 = l.top, h = l.h - l.margin - l.top;
	PaintGlassCard(p, x, y0, w, h);
	const int lx = x + 32, fx = x + 230, fw = w - 262, fieldH = 40, step = 52;
	int y = y0 + 28;
	p.Font(0);
	p.Text(lx, y, 190, fieldH, "Name", pal::textDim);
	PaintField(p, st, fx, y, fw, fieldH, st.nameInput, Tr("Game of ") + st.player, st.focus == kNameField, kFieldName);
	y += step;
	p.Text(lx, y, 190, fieldH, "Players", pal::textDim);
	Combo(p, st, fx, y, 240, fieldH, std::to_string(st.createPlayers) + Tr(" players"), kDropPlayers, 0, true);
	y += step;
	static const char *const kVisibility[] = {"Public", "Password protected", "Friends only"};
	p.Text(lx, y, 190, fieldH, "Visibility", pal::textDim);
	Combo(p, st, fx, y, fw, fieldH, kVisibility[st.createVisibility % 3], kDropVisibility, 0, true);
	y += step;
	if (st.createVisibility == 1) {
		p.Font(0);
		p.Text(lx, y, 190, fieldH, "Password", pal::textDim);
		PaintField(p, st, fx, y, fw - 112, fieldH, st.passInput, "Room password", st.focus == kPassField, kFieldPass, !st.passShow);
		p.Button(fx + fw - 100, y, 100, fieldH, st.passShow ? "Hide" : "Show", kPassShow, 0, mx, my);
		p.Font(0);
		y += step;
	}
	p.Font(0);
	p.Text(lx, y, 190, fieldH, "Game", pal::textDim);
	Combo(p, st, fx, y, fw, fieldH, st.createSave >= 0 && st.createSave < static_cast<int>(st.saves.size()) ? SaveLabel(st.saves[st.createSave]) : "New game",
		kDropSaveGame, 0, !st.saves.empty());
	y += step;
	p.Font(0);
	p.Text(lx, y, 190, fieldH, "Content", pal::textDim);
	Combo(p, st, fx, y, fw, fieldH, ModsProfileLabel(st.createProfile), kDropProfile, 0, st.profiles.size() > 1);
	y += fieldH + 8;
	{
		const std::vector<ListLine> lines = BuildModLines(st.createProfileInfo.rows, "None: the original game.");
		const int rowH = 26, rows = std::min<int>(6, static_cast<int>(lines.size()));
		g_createModsGeomOut = ListGeom();
		ListState ls = st.createModsList;
		ListPaint(p, fx, y, fw, rows * rowH + 8, lines, ls, g_createModsGeomOut, 3, rowH);
		y += rows * rowH + 16;
	}
	if (st.createSave >= 0 && st.createSave < static_cast<int>(st.saves.size())) {
		const ModNeeds &needs = CreateSaveNeeds(st.saves[st.createSave].vfsPath, st.createProfile);
		if (!needs.Satisfied()) {
			y += 4;
			p.Font(3);
			p.Text(fx + 4, y, fw, 24, Tr("This game needs: ") + ShortList(needs.Names(true), 2), pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 24;
			p.Text(fx + 4, y, fw, 24, needs.AllInstalled() ? "They are activated when you create it (the game must restart)." : "You don't have it installed: Options > Content > Browse content.", pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 24;
		}
	}
	const bool restart = st.createProfile != ModsMountedProfile().name;
	const int bw = 360, bh = 52, by = std::max(y + 16, y0 + h - 28 - bh);
	p.Button(x + (w - bw) / 2, by, bw, bh, restart ? "Restart with this content" : "Create room", kCreateOk, 0, mx, my, st.ready || restart, true);
}

ListGeom g_syncGeomOut;

void PaintSyncConfirm(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const OnlineLobby &g = st.syncLobby;
	const std::vector<OnlineMod> missing = MissingMods(st, g);
	uint64_t bytes = 0;
	for (const OnlineMod &m : missing) bytes += m.bytes;
	bool updatable = false;
	for (const OnlineMod &m : missing) updatable = updatable || OlderInstalled(st, m) != nullptr;
	const bool watch = st.syncWatch;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, updatable ? 760 : 560, updatable ? 400 : 340, watch ? "This game uses other content" : "This room uses other content");
	const std::vector<ListLine> lines = OnlineLobbyLines(st, g, true);
	g_syncGeomOut = ListGeom();
	{
		ListState ls = st.syncList;
		ListPaint(p, d.x + 24, d.y + 56, d.w - 48, 6 * 24 + 8, lines, ls, g_syncGeomOut, 3, 24, mx, my);
	}
	p.Font(0);
	const std::string where = st.syncLive ? "They will be downloaded from GameBanana"
										  : "They will be downloaded from GameBanana; whatever isn't there, the host sends you through Steam";
	const char *ready = watch ? "You already have all its content. The game will restart with it and you'll go back to watching automatically."
							  : "You already have all its content. The game will restart with it and you'll return to the room automatically.";
	const char *then = watch ? "). Then the game will restart with that content and you'll go back to watching automatically."
							 : "). Then the game will restart with that content and you'll return to the room automatically.";
	p.TextWrapped(d.x + 24, d.y + d.h - 130, d.w - 48, 64, missing.empty() ? std::string(ready) : Tr(where) + " (" + HumanBytes(bytes) + Tr(then), pal::textDim);
	if (updatable) {
		p.TextWrapped(d.x + 24, d.y + d.h - 190, d.w - 48, 56,
			Tr("Download separately leaves your version as it is and uses the room's only for this game. Update my "
			"version replaces it on restart (the old one is kept as a backup in mods\\_downloads\\backup).") +
				(st.syncUsing.empty() ? std::string() : Tr(" Used by your profiles: ") + st.syncUsing + "."),
			pal::textDim);
		DialogButtons(p, d, {{watch ? "Download separately and watch" : "Download separately and join", kSyncAccept, 0, true},
			{watch ? "Update my version and watch" : "Update my version and join", kSyncUpdate}, {"Cancel", kSyncCancel}}, mx, my);
	}
	else {
		const char *accept = missing.empty() ? (watch ? "Restart and watch" : "Restart and enter") : (watch ? "Download and watch" : "Download and enter");
		DialogButtons(p, d, {{accept, kSyncAccept, 0, true}, {"Cancel", kSyncCancel}}, mx, my);
	}
}

void PaintSyncProgress(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const char *title = st.syncPhase == 1 ? (st.syncWatch ? "Entering the spectator room" : "Joining the room") : st.syncPhase == 2 ? "Receiving the room's content"
		: st.syncPhase == 3 ? "Checking the content" : "Failed";
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 520, 200, title, st.syncPhase == 4 ? pal::bad : pal::redText);
	p.Font(0);
	if (st.syncPhase == 4) {
		p.TextWrapped(d.x + 24, d.y + 60, d.w - 48, 70, st.syncError, pal::text);
		DialogButtons(p, d, {{"Close", kSyncCancel}}, mx, my);
		return;
	}
	if (st.syncPhase == 2 && st.syncStage == 1 && st.io.unpacking && st.io.unpackTotal) {
		p.Font(3);
		std::string what = Tr("Installing ") + st.io.what;
		if (st.io.count > 1) what += Tr("  ·  pack ") + std::to_string(std::max(1, st.io.index)) + Tr(" of ") + std::to_string(st.io.count);
		p.Text(d.x + 24, d.y + 56, d.w - 48, 18, what, pal::ok);
		PaintProgress(p, d.x + 24, d.y + 80, d.w - 48, 14, static_cast<float>(static_cast<double>(st.io.unpacked) / st.io.unpackTotal));
		p.Text(d.x + 24, d.y + 100, d.w - 48, 22, TransferText(st.io.unpacked, st.io.unpackTotal, 0), pal::textDim);
	}
	else if (st.syncPhase == 2) {
		const bool fromIo = st.syncStage == 1;
		const uint64_t got = fromIo ? st.io.got : st.dl.got, all = fromIo ? st.io.total : st.dl.total;
		const uint64_t total = std::max<uint64_t>(1, all);
		const int pct = static_cast<int>(std::min<uint64_t>(100, got * 100 / total));
		p.Font(3);
		std::string source = fromIo ? "Source: GameBanana" : "Source: the host";
		if (fromIo && st.io.count > 0) source += Tr("  ·  pack ") + std::to_string(std::max(1, st.io.index)) + Tr(" of ") + std::to_string(st.io.count);
		p.Text(d.x + 24, d.y + 56, d.w - 48, 18, source, fromIo ? pal::ok : pal::redText);
		PaintProgress(p, d.x + 24, d.y + 80, d.w - 48, 14, pct / 100.0f);
		p.Text(d.x + 24, d.y + 100, d.w - 48, 22, TransferText(got, all, st.rate.perSecond), pal::textDim);
	}
	else if (st.syncPhase == 3 && st.hashTotal) {
		p.Font(3);
		p.Text(d.x + 24, d.y + 56, d.w - 48, 18, "Checking the content's files; then the game restarts.", pal::redText);
		PaintProgress(p, d.x + 24, d.y + 80, d.w - 48, 14, static_cast<float>(static_cast<double>(st.hashDone) / st.hashTotal));
		p.Text(d.x + 24, d.y + 100, d.w - 48, 22, TransferText(st.hashDone, st.hashTotal, st.rate.perSecond), pal::textDim);
	}
	else {
		p.Font(3);
		p.Text(d.x + 24, d.y + 76, d.w - 48, 22,
			st.syncPhase == 1 ? "Connecting to the host..." : "Setting up the room's profile; then the game restarts.", pal::textDim);
	}
	DialogButtons(p, d, {{"Cancel", kSyncCancel}}, mx, my);
}

void PaintRejoin(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const bool failed = RejoinClientFailed();
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 560, 230, failed ? "Could not return" : "Returning to the game", failed ? pal::bad : pal::redText);
	p.Font(0);
	p.TextWrapped(d.x + 24, d.y + 62, d.w - 48, 80, RejoinClientStatus(), pal::text);
	if (failed) {
		DialogButtons(p, d, {{"Close", kRejoinClose}}, mx, my, true);
		return;
	}
	PaintBusyBar(p, d.x + 24, d.y + 140, d.w - 48, 8, GetTickCount());
	DialogButtons(p, d, {{"Cancel", kRejoinCancel}}, mx, my, true);
}

void PaintRecover(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const RecoverInfo &r = st.recover;
	const bool waiting = !st.recoverAsk;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 560, 250, waiting ? "Waiting for the host" : "Your last online game");
	p.Font(0);
	std::string text;
	if (waiting) {
		text = Tr("As soon as ") + r.hostName + Tr(" recovers the game, you'll join it automatically.");
	}
	else if (r.host) {
		const uint64_t min = r.saveAge / 60;
		text = Tr("With ") + (r.others.empty() ? Tr("other players") : r.others) + Tr(". Last saved: ") +
			(min < 1 ? Tr("less than a minute ago") : std::to_string(min) + (min == 1 ? Tr(" minute ago") : Tr(" minutes ago"))) +
			Tr(". Continue it? A room will be created that loads it and the others will be able to join.");
	}
	else {
		text = Tr("Your game with ") + r.hostName + Tr(" was cut off. If it's still going you can return to it: the game will pause for a few seconds while "
			"you join.");
	}
	p.TextWrapped(d.x + 24, d.y + 62, d.w - 48, 110, text, pal::text);
	if (waiting) {
		PaintBusyBar(p, d.x + 24, d.y + 150, d.w - 48, 8, GetTickCount());
		DialogButtons(p, d, {{"Cancel", kRecoverNo}}, mx, my, true);
		return;
	}
	DialogButtons(p, d, {{r.host ? "Continue the game" : "Return to the game", kRecoverYes, 0, true}, {"Not now", kRecoverNo}}, mx, my);
}

COLORREF PlayerColor(const GameRoomSnapshot &g, int index)
{
	const uint32_t c = g.colors[index >= 0 && index < 12 ? index : 0];
	return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

bool RaceSeparator(const std::vector<std::string> &names)
{
	return names.size() > 9 && names[9] == CivsRoomSeparator();
}

int RaceRealIndex(const std::vector<std::string> &names, int shown)
{
	if (!RaceSeparator(names)) return shown;
	const std::vector<int> order = CivsRoomOrder(static_cast<int>(names.size()));
	return shown >= 0 && shown < static_cast<int>(order.size()) ? order[shown] : shown;
}

std::vector<std::string> DropItems(const State &st, int &sel)
{
	sel = -1;
	const int idx = st.dropIndex;
	if (st.dropKind == kDropTeam) {
		const int team = st.game.slots[idx & 7].team;
		sel = team > 0 && team <= kRoomTeams ? team : 0;
		std::vector<std::string> items = {Tr("No team")};
		for (int t = 1; t <= kRoomTeams; t++) items.push_back(TrFmt("Team %d", {std::to_string(t)}));
		return items;
	}
	if (st.dropKind == kDropBonus) {
		sel = st.game.slots[idx & 7].bonus;
		std::vector<std::string> names;
		for (const BonusDef &b : st.game.bonuses) names.push_back(b.name);
		return names;
	}
	if (st.dropKind == kDropPickSort) {
		sel = st.pickSort;
		return {"Name", "Players", "Size"};
	}
	if (st.dropKind == kDropFilter) {
		sel = st.filter;
		return {"All rooms", "Friends'", "No password", "Password protected", "Original game", "With my content", "With open slots", "In progress"};
	}
	if (st.dropKind == kDropSaveGame) {
		sel = st.createSave + 1;
		std::vector<std::string> items = {"New game"};
		for (const SaveEntry &e : st.saves) items.push_back(SaveLabel(e));
		return items;
	}
	if (st.dropKind == kDropVisibility) {
		sel = st.createVisibility;
		return {"Public", "Password protected", "Friends only"};
	}
	if (st.dropKind == kDropSpectators) {
		sel = st.createSpec;
		return {"No spectators", "With a 30 s delay", "With a 60 s delay", "With a 120 s delay", "Live"};
	}
	if (st.dropKind == kDropPlayers) {
		sel = st.createPlayers - 2;
		return {"2 players", "3 players", "4 players", "5 players", "6 players", "7 players", "8 players"};
	}
	if (st.dropKind == kDropProfile) {
		std::vector<std::string> labels;
		for (size_t i = 0; i < st.profiles.size(); i++) {
			if (st.profiles[i] == st.createProfile) sel = static_cast<int>(i);
			labels.push_back(ModsProfileLabel(st.profiles[i]));
		}
		return labels;
	}
	if (st.dropKind == kDropColor) {
		sel = st.game.slots[idx & 7].color;
		return std::vector<std::string>(12);
	}
	if (st.dropKind == kDropSites) {
		sel = -1;
		std::vector<std::string> items;
		for (const SiteChoice &c : kSiteChoices) items.push_back(std::string((st.game.siteFlags & c.bit) ? "✓  " : "     ") + Tr(c.label));
		return items;
	}
	if (st.dropKind == kDropModOption) {
		const std::vector<ModOption> &mods = ModOptions();
		if (idx < 0 || idx >= static_cast<int>(mods.size())) return {};
		sel = ModOptionGet(mods[idx], st.game.rules);
		return mods[idx].values;
	}
	if (st.dropKind == kDropPreset) {
		sel = MatchPreset(st.game.siteFlags, st.game.rules, HostOptionGetter(st.game));
		std::vector<std::string> items;
		for (const Preset &pr : kPresets) items.push_back(Tr(pr.name));
		return items;
	}
	if (st.dropKind == kDropSlot) {
		std::vector<std::string> items;
		for (const SlotChoice &c : SlotChoices(st.game, st.game.slots[idx & 7], sel)) items.push_back(c.label);
		return items;
	}
	if (st.dropKind == kDropRace || st.dropKind == kDropAi) {
		const GameRoomSlot &s = st.game.slots[idx & 7];
		sel = st.dropKind == kDropRace ? s.race : s.nameSel;
		if (st.dropKind == kDropRace && RaceSeparator(s.raceNames)) {
			const std::vector<int> order = CivsRoomOrder(static_cast<int>(s.raceNames.size()));
			std::vector<std::string> shown;
			sel = -1;
			for (size_t k = 0; k < order.size(); k++) {
				if (order[k] == s.race) sel = static_cast<int>(k);
				shown.push_back(s.raceNames[order[k]]);
			}
			return shown;
		}
		return st.dropKind == kDropRace ? s.raceNames : s.nameItems;
	}
	if (idx >= 0 && idx < static_cast<int>(st.game.options.size())) {
		const GameRoomOption &o = st.game.options[idx];
		sel = o.sel;
		if (o.id != 0x1000) return o.items;
		std::vector<std::string> items;
		for (const std::string &item : o.items) items.push_back(MapTypeLabel(item));
		return items;
	}
	return {};
}

int ClampDropScroll(int scroll, int count)
{
	return std::max(0, std::min(scroll, count - kDropRows));
}

RECT g_dropAnchor = {};

void Combo(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, int kind, int index, bool enabled)
{
	PaintCombo(p, x, y, w, h, text, st.dropOpen && st.dropKind == kind && st.dropIndex == index, enabled, st.mouseX, st.mouseY, kGDropOpen,
		DropArg(kind, index), &g_dropAnchor);
}

void PaintPickCombo(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text)
{
	Combo(p, st, x, y, w, h, text, kDropPickSort, 0, st.pickDelete < 0);
}

void PaintDropList(Painter &p, const State &st)
{
	if (!st.dropOpen) return;
	p.noHover = false;
	int sel = -1;
	const std::vector<std::string> items = DropItems(st, sel);
	const int count = static_cast<int>(items.size());
	if (!count) return;
	if (st.dropKind == kDropColor) {
		const int mx = st.mouseX, my = st.mouseY, cell = 34, gap = 6, pad = 10;
		const int w = pad * 2 + 6 * cell + 5 * gap, h = pad * 2 + 2 * cell + gap;
		const int x = std::min<int>(g_dropAnchor.left, st.viewW - 16 - w);
		int y = g_dropAnchor.bottom + 4;
		if (y + h > st.viewH - 8) y = g_dropAnchor.top - 4 - h;
		p.hits->clear();
		p.AddHit(0, 0, st.viewW, st.viewH, kGDropClose, 0);
		p.Fill(x + 4, y + 4, w, h, pal::shadow);
		p.Fill(x, y, w, h, pal::panel);
		p.Frame(x, y, w, h, pal::lineStrong);
		for (int c = 0; c < 12; c++) {
			const int cx = x + pad + (c % 6) * (cell + gap), cy = y + pad + (c / 6) * (cell + gap);
			bool taken = false;
			for (int i = 0; i < 8; i++) {
				if (i != (st.dropIndex & 7) && st.game.slots[i].used && st.game.slots[i].color == c) taken = true;
			}
			const bool hov = !taken && p.Hover(cx, cy, cell, cell, mx, my);
			p.Fill(cx, cy, cell, cell, PlayerColor(st.game, c));
			if (taken) {
				const COLORREF pc = PlayerColor(st.game, c);
				p.Fill(cx, cy, cell, cell, RGB(GetRValue(pc) * 2 / 5 + 18, GetGValue(pc) * 2 / 5 + 14, GetBValue(pc) * 2 / 5 + 10));
				p.Line(cx + 3, cy + 3, cx + cell - 3, cy + cell - 3, pal::textDim, 2);
				p.Frame(cx, cy, cell, cell, pal::edge);
				continue;
			}
			if (c == sel) {
				p.Frame(cx - 3, cy - 3, cell + 6, cell + 6, pal::text);
				p.Frame(cx, cy, cell, cell, pal::panel);
			}
			else {
				p.Frame(cx, cy, cell, cell, hov ? pal::text : pal::lineStrong);
				if (hov) p.Frame(cx - 2, cy - 2, cell + 4, cell + 4, pal::edgeHot);
			}
			p.AddHit(cx, cy, cell, cell, kGDropPick, c);
		}
		return;
	}
	std::vector<DropItem> shown = ToDropItems(items);
	if (st.dropKind == kDropOption && st.dropIndex >= 0 && st.dropIndex < static_cast<int>(st.game.options.size())) {
		const GameRoomOption &o = st.game.options[st.dropIndex];
		for (size_t i = 0; i < shown.size() && i < o.itemDisabled.size(); i++) {
			shown[i].disabled = o.itemDisabled[i] != 0;
			if (shown[i].disabled) shown[i].tip = "This map type does not exist in that size";
		}
	}
	if (st.dropKind == kDropBonus) {
		for (size_t i = 0; i < shown.size() && i < st.game.bonuses.size(); i++) shown[i].tip = st.game.bonuses[i].description;
	}
	PaintDropMenu(p, g_dropAnchor, shown, sel, st.dropScroll, st.viewW, st.viewH, kGDropPick, kGDropClose, st.mouseX, st.mouseY, st.dropKind == kDropBonus ? 150 : 200,
		st.dropBar);
}
}

namespace mpui_impl {
namespace {
const int kChatLaurel = 15;

std::string ChatTime(int minute)
{
	char buf[8];
	sprintf_s(buf, "%02d:%02d", (minute / 60) % 24, minute % 60);
	return buf;
}
}
}

namespace mpui_impl {
void PaintChatLog(Painter &p, const State &st, int x, int y, int w, int h)
{
	std::lock_guard<std::mutex> lock(g_chat.mx);
	const std::vector<OnlineChatLine> &chat = st.room.chat;
	if (st.room.id != g_chat.room) {
		g_chat.room = st.room.id;
		g_chat.scroll = g_chat.lastTotal = g_chat.unread = 0;
		g_chat.serial = st.room.chatSerial;
	}
	g_chat.rect = p.R(x, y, w, h);
	OffsetRect(&g_chat.rect, p.ox, p.oy);
	g_chat.rectValid = true;
	if (chat.empty()) return;
	const int pad = 2, barW = 6, textW = w - pad * 2 - barW - 2;
	struct Item {
		int y, h, timeW, indent, lineH;
		std::string text;
		std::vector<std::wstring> lines;
	};
	std::vector<Item> items;
	int total = 4;
	p.Font(3);
	const int timeW = p.TextWidth("00:00");
	const int lineH = p.Measure(textW, "A");
	const std::string systemText = Tr("System") + " \xC2\xB7 ";
	const std::wstring systemTag = W(systemText);
	const int systemW = static_cast<int>(std::ceil(p.TextWidthExact(systemText)));
	for (const OnlineChatLine &l : chat) {
		Item it;
		it.indent = 0;
		it.lineH = lineH;
		if (l.system && l.patron && PatronOf(l.id).tier != PatronTier::None) {
			it.text = l.text;
			it.h = lineH + 8;
		}
		else if (l.system) {
			p.Font(3);
			it.indent = systemW;
			it.lines = p.WrapLines(l.text, it.indent, textW);
			it.h = static_cast<int>(it.lines.size()) * lineH;
		}
		else {
			p.Font(1);
			const float nameW = p.TextWidthExact(l.who + ":");
			p.Font(3);
			const int laurelW = PatronOf(l.id).tier != PatronTier::None ? kChatLaurel + 3 : 0;
			it.indent = static_cast<int>(std::ceil(timeW + 6 + laurelW + nameW + 6));
			it.lines = p.WrapLines(l.text, it.indent, textW);
			it.h = static_cast<int>(it.lines.size()) * lineH;
		}
		it.timeW = timeW;
		it.y = total;
		total += it.h + 4;
		items.push_back(it);
	}
	total += 2;
	if (g_chat.scroll > 0 && total > g_chat.lastTotal) g_chat.scroll += total - g_chat.lastTotal;
	if (g_chat.scroll > 0 && st.room.chatSerial > g_chat.serial) g_chat.unread += static_cast<int>(st.room.chatSerial - g_chat.serial);
	g_chat.serial = st.room.chatSerial;
	g_chat.lastTotal = total;
	const int maxScroll = std::max(0, total - h);
	g_chat.scroll = std::max(0, std::min(g_chat.scroll, maxScroll));
	if (g_chat.scroll == 0) g_chat.unread = 0;
	// Below zero while the lines don't fill the box: the newest line stays at its bottom, by the field.
	const int top = total - h - g_chat.scroll;
	p.Clip(x + 1, y + 1, w - 2, h - 2);
	for (size_t i = 0; i < chat.size(); i++) {
		const Item &it = items[i];
		const OnlineChatLine &l = chat[i];
		if (it.y + it.h < top || it.y > top + h) continue;
		const int py = y + it.y - top;
		// Patron lines, their join line and what they write, sit on a band like the bar of the website:
		// a tint of the laurel across the whole box and a stripe of it on the left.
		auto patronBand = [&](PatronTier t, int top, int height) {
			const PatronColors c = PatronColorsOf(t);
			p.Fill(x + 1, top, w - 2, height, PatronBlend(pal::bg, c.laurel, 14));
			p.Fill(x + 1, top, 3, height, c.laurel);
		};
		const PatronTier joinedTier = l.system && l.patron ? PatronOf(l.id).tier : PatronTier::None;
		if (joinedTier != PatronTier::None) {
			patronBand(joinedTier, py - 2, it.h);
			p.Font(1);
			const int nameX = x + pad + 2 + kChatLaurel + 4, nameW = std::min(p.TextWidth(l.who), textW / 2);
			DrawPatron(p, x + pad + 2, py + 2, kChatLaurel, nameX, py + 1, nameW, lineH, l.who, joinedTier, DT_LEFT | DT_TOP | DT_SINGLELINE);
			if (py + 2 >= y && py + 2 + kChatLaurel <= y + h) PatronHotspot(p, x + pad + 2, py + 2, kChatLaurel, PatronOf(l.id), st.mouseX, st.mouseY, kPatronLink);
			const std::string rest = l.text.compare(0, l.who.size(), l.who) == 0 ? l.text.substr(l.who.size()) : " " + l.text;
			p.Font(3);
			p.Text(nameX + nameW, py + 1, x + pad + textW - nameX - nameW, lineH, rest, pal::text, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
			continue;
		}
		if (l.system) {
			p.Font(3);
			p.LineW(x + pad, py, it.indent, it.lineH, systemTag, pal::caption);
			for (size_t k = 0; k < it.lines.size(); k++) {
				const int shift = k == 0 ? it.indent : 0;
				p.LineW(x + pad + shift, py + static_cast<int>(k) * it.lineH, textW - shift, it.lineH, it.lines[k], pal::textDim);
			}
			continue;
		}
		const bool me = l.id == SteamOwnId();
		const PatronTier tier = PatronOf(l.id).tier;
		if (tier != PatronTier::None) patronBand(tier, py - 2, it.h + 2);
		p.Font(3);
		// The boxes reach below the line: the name is in a larger type than the line is measured in, and DrawText clips.
		const int boxH = it.lineH + 6;
		p.Text(x + pad, py, it.timeW, boxH, ChatTime(l.minute), pal::textDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
		const int laurelX = x + pad + it.timeW + 6;
		p.Font(1);
		if (tier != PatronTier::None) {
			const int nameX = laurelX + kChatLaurel + 3;
			DrawPatron(p, laurelX, py + 1, kChatLaurel, nameX, py, x + pad + textW - nameX, boxH, l.who + ":", tier, DT_LEFT | DT_TOP | DT_SINGLELINE);
			if (py + 1 >= y && py + 1 + kChatLaurel <= y + h) PatronHotspot(p, laurelX, py + 1, kChatLaurel, PatronOf(l.id), st.mouseX, st.mouseY, kPatronLink);
		}
		else p.Text(laurelX, py, textW - it.timeW - 6, boxH, l.who + ":", me ? pal::redText : pal::text, DT_LEFT | DT_TOP | DT_SINGLELINE);
		p.Font(3);
		for (size_t k = 0; k < it.lines.size(); k++) {
			const int shift = k == 0 ? it.indent : 0;
			p.LineW(x + pad + shift, py + static_cast<int>(k) * it.lineH, textW - shift, it.lineH, it.lines[k], pal::text);
		}
	}
	p.Unclip();
	{
		ListState view = st.chat;
		view.scroll = std::max(0, maxScroll - g_chat.scroll);
		ListGeom geom;
		ScrollbarPaint(p, x + w - barW - 3, y + 2, h - 4, total, h, view, geom, st.mouseX, st.mouseY);
		g_chat.geomOut = geom;
	}
	if (g_chat.scroll > 0) {
		const std::string label = g_chat.unread > 0 ? "\xE2\x86\x93 " + std::to_string(g_chat.unread) : "\xE2\x86\x93";
		const int bw = g_chat.unread > 0 ? 46 : 30;
		p.Button(x + w - bw - barW - 10, y + h - 28, bw, 22, label, kChatBottom, 0, st.mouseX, st.mouseY, true, g_chat.unread > 0, 3);
	}
}
}
