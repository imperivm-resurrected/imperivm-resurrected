// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <ctime>

#include "ui/colorwheel.h"
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/config.h"
#include "core/names.h"
#include "mods/modoptions.h"
#include "game/civs.h"
#include "net/rejoin.h"

#include "ui/leagueui.h"
#include "ui/mpui_internal.h"
#include "ui/mpui_tourney.h"
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
	static_cast<ScreenLayout &>(l) = ScreenLayoutFor(w, h, false);
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
	// The result of a ranked game and the end of a season have a header of their own.
	if (st.ranked.resultOpen || st.ranked.ceremonyOpen) return;
	const int mx = st.mouseX, my = st.mouseY;
	const LobbyLayout l = LobbyLayoutFor(st.viewW, st.viewH);
	if (st.view == kViewTournaments && PaintTourneyHeader(p, st)) return;
	// The title and the tabs keep their place whether the search's pill is there or not: the pill takes what is left before "Back".
	const int right = PaintHeaderActions(p, l, {{"Back", kBack, true, true}}, mx, my);
	const int tabsEnd = PaintScreenTitle(p, l, "Multiplayer",
		{{"Rooms", kListView, 0, st.view == kViewList || st.view == kViewCreate, true}, {"Ranked", kRanked, 0, st.view == kViewRanked, true},
			{"Tournaments", kTourney, kTourneyShowArg, st.view == kViewTournaments, true}},
		right, mx, my);
	PaintRankedPill(p, st, right, tabsEnd + 16);
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

// A player's name in the box (x, y, w, h): the patron's laurel before it, and the league badge after it, which takes its room from the
// name. `color` and `fmt` are the name's without a laurel. Returns where the name starts.
int PaintLaurelName(Painter &p, const State &st, int x, int y, int w, int h, const std::string &name, uint64_t id, const PatronInfo &patron,
	const LeagueBadge &badge, COLORREF color, UINT fmt)
{
	if (patron.tier == PatronTier::None) {
		const int nameW = w - LeagueBadgeRoom(badge);
		p.Text(x, y, nameW, h, name, color, fmt);
		DrawBadgeAfterName(p, x, y, nameW, h, name, badge, st.mouseX, st.mouseY, id);
		return x;
	}
	const int laurelY = y + (h - 20) / 2, nameX = x + 24, nameW = w - 24 - LeagueBadgeRoom(badge);
	DrawPatron(p, x, laurelY, 20, nameX, y, nameW, h, name, patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	PatronHotspot(p, x, laurelY, 20, patron, st.mouseX, st.mouseY, kPatronLink);
	DrawBadgeAfterName(p, nameX, y, nameW, h, name, badge, st.mouseX, st.mouseY, id);
	return nameX;
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
			vis.empty() ? "When there are rooms, pick one to see its content and join, or make your own with \"Create a room\"."
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
	// The last frame's end of the content: a room whose content shrank does not stay scrolled past it.
	const int scrolled = st.detailGeom.visible > 0 ? std::min(st.detail.scroll, st.detailGeom.maxScroll) : st.detail.scroll;
	int y = startY - scrolled;
	ClipArea view(p, dx + 1, viewTop, dw - 2, viewH);

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
	// Who hosts it and how full it is, under its name.
	p.Font(3);
	p.raw = true;
	p.Text(x, y, w, 22, TrFmt("Hosted by %s", {g.host}) + "  \xC2\xB7  " + (g.playing ? Tr("In game") : TrFmt("%s of %s players", {std::to_string(g.players), std::to_string(g.maxPlayers)})),
		pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	y += 26;

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
	// The players there, then how many places are left: a list of empty rows says less.
	int freePlaces = 0;
	if (!g.slots.empty()) {
		int n = 0;
		bool hostShown = false;
		for (const OnlineSlot &s : g.slots) {
			// The host publishes 0 open, 1 AI, 2 closed, 3 a player, 4 no position on the map, 5 a player in a slot the map no longer has.
			if (s.type == 0) freePlaces++;
			if (s.type != 1 && s.type != 3 && s.type != 5) continue;
			n++;
			const bool human = s.type != 1;
			p.Text(x, y, 28, 26, std::to_string(n), pal::textDim);
			const uint64_t pid = human ? LobbyPlayerId(g, s.name) : 0;
			const PatronInfo patron = human ? PatronOf(pid) : PatronInfo();
			const LeagueBadge badge = pid ? PlayerBadgeOf(pid) : LeagueBadge();
			const std::string shown = human || !s.name.empty() ? s.name : "AI";
			const int nameX = PaintLaurelName(p, st, x + 30, y, w - 30 - 90, 26, shown, pid, patron, badge, human ? pal::text : pal::textDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			if (pid) ProfileHotspot(p, st, nameX, y, w - (nameX - x) - 90, 26, s.name, kProfileLobbySlot + static_cast<int>(&s - g.slots.data()));
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
		freePlaces = std::max(0, g.maxPlayers - static_cast<int>(g.playerNames.size()));
		for (int i = 0; i < static_cast<int>(g.playerNames.size()); i++) {
			p.Text(x, y, 28, 26, std::to_string(i + 1), pal::textDim);
			const uint64_t pid = LobbyPlayerId(g, g.playerNames[i]);
			const PatronInfo patron = PatronOf(pid);
			const LeagueBadge badge = pid ? PlayerBadgeOf(pid) : LeagueBadge();
			const int nameW = w - 30 - (patron.tier != PatronTier::None || i == 0 ? 90 : 0);
			const int nameX = PaintLaurelName(p, st, x + 30, y, nameW, 26, g.playerNames[i], pid, patron, badge, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			if (pid) ProfileHotspot(p, st, nameX, y, w - (nameX - x) - 90, 26, g.playerNames[i], kProfileLobbyName + i);
			if (i == 0) p.Text(x + w - 100, y, 100, 26, "host", pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			y += 28;
		}
	}

	if (freePlaces > 0) {
		p.Font(3);
		p.raw = true;
		p.Text(x, y, w, 24, freePlaces == 1 ? Tr("1 free place") : TrFmt("%s free places", {std::to_string(freePlaces)}), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		y += 30;
	}

	section("Options");
	{
		// Same rows, labels and order as the room's "Game options" panel (mpui_paint_room.cpp), read from the text the host publishes.
		std::vector<std::string> published;
		std::map<std::string, std::string> pub = ParsePublishedOptions(g.optionsText, &published);
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
			(yes("Diplomacy locked") ? kRuleLockDiplomacy : 0) | (yes("Cheats") ? kRuleCheats : 0) | (pub.count("Respawn timers") && !yes("Respawn timers") ? kRuleNoTimers : 0);
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
				const int chosen = ModOptionGet(mo, g.rules);
				if (!randomMap) valueRow(mo.name, Tr("Only on random maps"), pal::textDim);
				else if (has && chosen >= 0 && chosen < static_cast<int>(mo.values.size())) valueRow(mo.name, mo.values[chosen], chosen != 0 ? kSpecial : pal::text);
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
			checkRow("No fog of war", "No fog", false);
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
				const int chosen = ModOptionGet(mo, g.rules);
				if (mo.values.empty()) toggleRow(mo.name, v == "yes");
				else valueRow(mo.name, chosen >= 0 && chosen < static_cast<int>(mo.values.size()) ? mo.values[chosen] : v, chosen != 0 ? kSpecial : pal::text);
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
	view.End();

	const int total = (y + scrolled) - startY + 12;
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
	// A room of the local network has no content to download: its host's content profile is checked when joining.
	const std::string label = g.kickedMe ? "You were kicked from the room" : g.playing ? "Game in progress" : full ? "Room full" : same || g.lan ? "Join" : missing.empty() ? "Restart and join" : "Download and join";
	if (TourneyLobbyWatchOnly(g)) {
		// Only its two players enter the room of a tournament match; anyone else watches it once it starts.
		if (g.playing && g.specLobby) p.Button(bx, by, bw, bh, "Watch the game", kSpectate, 0, mx, my, st.ready, true);
		else p.Button(bx, by, bw, bh, "Tournament match: watch it once it starts", kJoin, 0, mx, my, false, false);
	}
	else if (g.specOnly) {
		if (g.playing) p.Button(bx, by, bw, bh, "Watch the game", kSpectate, 0, mx, my, st.ready, true);
		else p.Button(bx, by, bw, bh, "Room full", kJoin, 0, mx, my, false, false);
	}
	else if (g.playing && g.specLobby) {
		p.Button(bx, by, bw, bh, "Watch the game", kSpectate, 0, mx, my, st.ready, !canReturn);
	}
	else if (!canReturn) {
		p.Button(bx, by, bw, bh, label, kJoin, 0, mx, my, !g.playing && !full && !g.kickedMe && (st.ready || g.lan), true);
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

void SteamProfileHotspot(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, int mx, int my)
{
	// An individual account in the public universe: 0x01100001 in the high half.
	if ((id >> 32) != 0x01100001ULL) return;
	const int textW = std::min(w, p.TextWidth(name));
	if (textW <= 0) return;
	p.AddHit(x, y, textW, h, kSteamProfile, static_cast<int>(static_cast<uint32_t>(id)));
	if (p.Hover(x, y, textW, h, mx, my)) p.Fill(x, y + h / 2 + std::max(9, h / 4), textW, 1, pal::redText);
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

// The line under a room's name: the saved game it continues or the game being played, a Steam friend in it (`friends` sorted), its
// password, and its spectators.
std::string RoomSubline(const OnlineLobby &g, const std::vector<uint64_t> &friends, long long nowMs)
{
	std::vector<std::string> parts;
	if (g.playing) {
		const long long minutes = g.startedMs > 0 ? (nowMs - g.startedMs) / 60000 : -1;
		parts.push_back(minutes >= 0 && minutes < 24 * 60 ? TrFmt("Playing for %s min", {std::to_string(minutes)}) : Tr("In game"));
		if (g.spectators > 0) parts.push_back(TrFmt("%s watching", {std::to_string(g.spectators)}));
	}
	else if (!g.saveName.empty()) parts.push_back(TrFmt("Continues the saved game \"%s\"", {g.saveName}));
	for (size_t i = 0; i < g.playerIds.size() && i < g.playerNames.size(); i++) {
		if (g.playerIds[i] == g.owner || !std::binary_search(friends.begin(), friends.end(), g.playerIds[i])) continue;
		parts.push_back(TrFmt("with your friend %s", {g.playerNames[i]}));
		break;
	}
	if (g.password) parts.push_back(Tr("Password protected"));
	if (g.spectate && !g.playing) parts.push_back(g.specDelay > 0 ? TrFmt("spectators with a %s s delay", {std::to_string(g.specDelay)}) : Tr("spectators allowed"));
	if (parts.empty()) parts.push_back(g.players < g.maxPlayers ? TrFmt("%s free places", {std::to_string(g.maxPlayers - g.players)}) : Tr("Full"));
	std::string out;
	for (const std::string &part : parts) out += (out.empty() ? "" : "  \xC2\xB7  ") + part;
	return out;
}

void PaintList(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	LobbyLayout l = LobbyLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, toolY = l.toolY;
	// The filters as tabs with their counts: every room, those that can be joined, the games that can be watched and the friends'.
	const int refreshW = std::max(GhostButtonWidth(p, "Refresh", false), GhostButtonWidth(p, "Searching...", false));
	const int refreshX = m + l.tableW - refreshW;
	static const struct {
		int filter;
		const char *label;
	} kTabs[] = {{0, "Every room"}, {6, "Open to join"}, {7, "Live"}, {1, "Friends'"}, {8, "Local network"}};
	const int kTabCount = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
	int counts[kTabCount] = {};
	for (const OnlineLobby &g : st.lobbies) {
		if (!RoomMatchesSearch(st, g)) continue;
		for (int t = 0; t < kTabCount; t++) counts[t] += RoomInFilter(g, kTabs[t].filter) ? 1 : 0;
	}
	// A tab's parts: its padding, the letter spacing of its label and the gap before its count.
	struct TabLook {
		int pad, spacing, gap, countPad, end;
	};
	const TabLook roomy = {18, 2, 8, 0, 18}, tight = {12, 1, 6, 0, 12};
	auto tabWidth = [&](int t, const TabLook &k, int &textW, int &countW) {
		SelectObject(p.dc, BrandFont(BrandFace::UiStrong, 13, p.s));
		textW = p.CapsWidth(kTabs[t].label, k.spacing);
		p.raw = true;
		SelectObject(p.dc, BrandFont(BrandFace::Ui, 13, p.s));
		countW = p.TextWidth(std::to_string(counts[t])) + k.countPad;
		p.raw = false;
		return k.pad + textW + k.gap + countW + k.end;
	};
	auto tabsWidth = [&](const TabLook &k) {
		int total = 0, textW = 0, countW = 0;
		for (int t = 0; t < kTabCount; t++) total += tabWidth(t, k, textW, countW) - 1;
		return total;
	};
	// On a narrow table the tabs close up first; when the search still has no room beside them it goes to a row of its own under them.
	constexpr int kMinSearchW = 160, kToolGap = 12;
	const int roomyW = tabsWidth(roomy), tightW = tabsWidth(tight);
	const bool searchBeside = roomyW + 2 * kToolGap + kMinSearchW + refreshW <= l.tableW || tightW + 2 * kToolGap + kMinSearchW + refreshW <= l.tableW;
	const TabLook &look = roomyW + kToolGap + (searchBeside ? kToolGap + kMinSearchW : 0) + refreshW <= l.tableW ? roomy : tight;
	if (!searchBeside) {
		l.bodyY += kToolH + kToolGap;
		l.bodyH -= kToolH + kToolGap;
	}
	int fx = m;
	for (int t = 0; t < kTabCount; t++) {
		const int count = counts[t];
		const bool on = st.filter == kTabs[t].filter;
		int textW = 0, countW = 0;
		const int w = tabWidth(t, look, textW, countW);
		const std::string n = std::to_string(count);
		const bool hot = !on && p.Hover(fx, toolY, w, kToolH, mx, my);
		p.WellSegment(fx, toolY, w, kToolH, on, hot);
		SelectObject(p.dc, BrandFont(BrandFace::UiStrong, 13, p.s));
		p.TextCaps(fx + look.pad, toolY, textW + 2, kToolH, kTabs[t].label, on || hot ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE, look.spacing);
		const int cx = fx + look.pad + textW + look.gap;
		SelectObject(p.dc, BrandFont(BrandFace::Ui, 13, p.s));
		p.raw = true;
		p.Text(cx, toolY, countW, kToolH, n, on && count ? pal::redText : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		if (!on) p.AddHit(fx, toolY, w, kToolH, kRoomFilter, kTabs[t].filter);
		fx += w - 1;
	}
	if (searchBeside) {
		const int searchX = fx + kToolGap, searchW = std::min(420, refreshX - kToolGap - searchX);
		PaintField(p, st, searchX, toolY, searchW, kToolH, st.searchInput, "Search room, host or map...", st.focus == kSearchField, kFieldSearch);
	}
	else {
		PaintField(p, st, m, toolY + kToolH + kToolGap, std::min(420, l.tableW), kToolH, st.searchInput, "Search room, host or map...", st.focus == kSearchField,
			kFieldSearch);
	}
	PaintGhostButton(p, refreshX, toolY, refreshW, kToolH, st.loading ? "Searching..." : "Refresh", kRefresh, false, st.ready && !st.loading, mx, my);
	// Over the room's card, the main action: making a room.
	p.Button(l.w - m - l.detailW, toolY, l.detailW, kToolH, "Create a room", kCreate, 0, mx, my, true, true);

	const int tx = m, tw = l.tableW, ty = l.bodyY, th = l.bodyH, headH = 44, rowH = l.roomy ? 52 : 46;
	PaintGlassCard(p, tx, ty, tw, th);
	static const char *const kCols[] = {"Room", "Host", "Players", "Map", "Content"};
	const float unit = static_cast<float>(tw) / kColumnBaseW;
	// The columns the player sized, narrowed together when they would leave the content less than its shortest text.
	p.Font(0);
	const int contentMin = p.TextWidth("Original game") + 58;
	int widths[4], sized = 0;
	for (int c = 0; c < 4; c++) sized += widths[c] = static_cast<int>(st.colW[c] * unit + 0.5f);
	const int room = tw - 40 - contentMin;
	if (sized > room && sized > 0) {
		for (int c = 0; c < 4; c++) widths[c] = widths[c] * std::max(0, room) / sized;
	}
	int cx[6];
	cx[0] = tx + 20;
	for (int c = 0; c < 4; c++) cx[c + 1] = cx[c] + widths[c];
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
	// Rooms still being searched for show as the rows they will fill, without the invitation under them.
	const bool skeleton = vis.empty() && ((!st.ready && st.status.empty()) || (st.ready && st.loading));
	if (skeleton) {
		std::vector<std::pair<int, int>> cols;
		for (int c = 0; c < 5; c++) cols.push_back({textX(c), cx[c + 1] - textX(c)});
		PaintSkeletonRows(p, tx + 1, rowsTop, tw - 2, rowH, std::min(8, (th - headH) / rowH), cols);
	}
	else if (vis.empty()) {
		p.Font(0);
		p.TextWrapped(tx + 24, rowsTop + 24, tw - 48, 120, !st.ready ? st.status : std::string("There are no rooms with this filter."), pal::textDim);
	}
	// The rooms in their groups, each under its heading with its count. The list scrolls when it is longer than the table, and the arrows
	// scroll it to the room they select.
	const int count = static_cast<int>(vis.size()), groupH = 30, bodyH = th - headH - 1;
	auto groupOf = [&](int r) { return RoomGroup(st.lobbies[vis[r]]); };
	auto startsGroup = [&](int r) { return r == 0 || groupOf(r) != groupOf(r - 1); };
	int groupCount[3] = {};
	for (int r = 0; r < count; r++) groupCount[groupOf(r)]++;
	std::vector<int> rowTop(count);
	int contentH = 0;
	for (int r = 0; r < count; r++) {
		if (startsGroup(r)) contentH += groupH;
		rowTop[r] = contentH;
		contentH += rowH;
	}
	int scroll = st.scrolls.Of(kPaneLobbyRooms);
	if (st.lobbyReveal && count > 0) {
		const int r = std::max(0, std::min(st.selected, count - 1)), top = rowTop[r] - (startsGroup(r) ? groupH : 0);
		if (top < scroll) scroll = top;
		else if (rowTop[r] + rowH > scroll + bodyH) scroll = rowTop[r] + rowH - bodyH;
	}
	scroll = ScrollClamp(scroll, PaneMax(contentH, bodyH));
	g_lobbyScrollOut = scroll;
	// Whose rooms have a Steam friend in them.
	std::vector<uint64_t> friends = SteamFriendIds();
	std::sort(friends.begin(), friends.end());
	const long long nowMs = static_cast<long long>(std::time(nullptr)) * 1000;
	int cursor = rowsTop;
	{
		PaneSpec spec;
		spec.id = kPaneLobbyRooms;
		spec.x = tx + 1;
		spec.y = rowsTop;
		spec.w = tw - 2;
		spec.h = bodyH;
		spec.contentH = contentH;
		spec.barX = tx + tw - 10;
		spec.drag = &st.dropBar;
		spec.mx = mx;
		spec.my = my;
		ScrollPane pane(p, spec, scroll);
		for (int r = 0; r < count; r++) {
			const OnlineLobby &g = st.lobbies[vis[r]];
			const int y = pane.Top() + rowTop[r];
			cursor = y + rowH;
			if (startsGroup(r) && pane.Shows(y - groupH, groupH)) {
				static const char *const kGroups[] = {"Open rooms", "Live \xC2\xB7 you can watch", "Not available"};
				const int group = groupOf(r), gy = y - groupH;
				p.Font(4);
				const int labelW = p.CapsWidth(kGroups[group], 2) + 4;
				p.TextCaps(tx + 20, gy, labelW, groupH, kGroups[group], group == 1 ? pal::redText : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
				p.Font(3);
				p.raw = true;
				p.Text(tx + 20 + labelW + 8, gy, 60, groupH, std::to_string(groupCount[group]), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				p.raw = false;
				p.Fill(tx + 1, gy + groupH - 1, tw - 2, 1, pal::hairline);
			}
			if (!pane.Shows(y, rowH)) continue;
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
			// The name over a line of what is played and how.
			const int nameY = y + 3, nameH = rowH / 2;
			p.Font(1);
			if (g.password) DrawLock(p, cx[0] + 4, nameY + (nameH - 16) / 2, pal::redText);
			int lockW = g.password ? 22 : 0;
			if (TourneyLobby(g)) {
				PaintBadge(p, cx[0] + 2 + lockW, nameY + (nameH - 22) / 2, 112, 22, "TOURNAMENT", pal::red, pal::onRed);
				lockW += 120;
				p.Font(1);
			}
			p.Text(cx[0] + 2 + lockW, nameY, cx[1] - cx[0] - 12 - lockW, nameH, g.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(3);
			p.raw = true;
			p.Text(cx[0] + 2, nameY + nameH, cx[1] - cx[0] - 14, rowH - nameH - 7, RoomSubline(g, friends, nowMs), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
			p.Font(0);
			const PatronInfo hostPatron = PatronOf(g.owner);
			const LeagueBadge hostBadge = PlayerBadgeOf(g.owner);
			const std::string hostText = g.friendHost ? g.host + Tr("  (friend)") : !g.friendIn.empty() ? g.host + "  ·  " + TrFmt("with %s", {g.friendIn}) : g.host;
			PaintLaurelName(p, st, textX(1), y, cx[2] - textX(1) - 12, rowH, hostText, g.owner, hostPatron, hostBadge,
				g.friendHost || !g.friendIn.empty() ? pal::redText : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			cell(2, g.playing ? std::string("In game") : std::to_string(g.players) + " / " + std::to_string(g.maxPlayers),
				g.players < g.maxPlayers && !g.playing ? pal::text : pal::textDim);
			cell(3, LobbyMapLabel(g), pal::textDim);
			// What the room is played with, after a dot that says whether this PC has it: green, amber when the game must restart, red when
			// the content must be downloaded.
			{
				const OnlineMod *main = nullptr;
				for (const OnlineMod &mod : g.mods) {
					if (!main || (mod.gameplay && !main->gameplay)) main = &mod;
				}
				std::string content = main ? main->name : Tr("Original game");
				if (g.mods.size() > 1) content += " +" + std::to_string(g.mods.size() - 1);
				COLORREF dot = pal::ok;
				const std::vector<OnlineMod> missing = g.lan ? std::vector<OnlineMod>() : MissingMods(st, g);
				if (g.lan) {
					content = Tr("Local network");
					if (!g.gameplayKey.empty() && !st.ownKey.empty() && g.gameplayKey != st.ownKey) dot = pal::bad;
				}
				else if (!missing.empty()) {
					uint64_t bytes = 0;
					for (const OnlineMod &mod : missing) bytes += mod.bytes;
					dot = pal::bad;
					content += "  \xC2\xB7  " + TrFmt("download %s", {HumanBytes(bytes)});
				}
				else if (SteamRoomContent(g, st.ownKey) == RoomContent::kOtherGameplay) {
					dot = pal::warn;
					content += "  \xC2\xB7  " + Tr("restart");
				}
				const int dx = textX(4);
				p.Ellipse(dx, y + (rowH - 8) / 2, 8, 8, dot);
				p.raw = true;
				p.Text(dx + 16, y, cx[5] - dx - 28, rowH, content, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				p.raw = false;
			}
		}
	}
	// Under a short list, an invitation to make a room, so the table is not an empty box.
	const int panelTop = vis.empty() ? rowsTop + 96 : cursor + 36, panelH = 160;
	if (st.ready && !skeleton && panelTop + panelH <= ty + th - 16) {
		const int pw = std::min(560, tw - 80), px = tx + (tw - pw) / 2;
		p.Font(4);
		p.TextCaps(px, panelTop, pw, 22, vis.empty() ? "No rooms here yet" : "None suits you?", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
		p.Font(0);
		p.TextWrapped(px, panelTop + 32, pw, 48, "Make your own room: it shows in this list for everyone looking for a game.", pal::textDim);
		const int bw = std::max(220, GhostButtonWidth(p, "Create a room", false));
		PaintGhostButton(p, px + (pw - bw) / 2, panelTop + 96, bw, 44, "Create a room", kCreate, false, true, mx, my);
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
	// The account grows with the badges and seasons the player earns: past the card it scrolls.
	const LeagueLinkActions actions = {kLeagueLink, kLeagueOpen, kLeagueCancel, kLeagueUnlink};
	PaneSpec spec;
	spec.id = kPaneLeagueCard;
	spec.x = m + 1;
	spec.y = top + pad;
	spec.w = cw - 2;
	spec.h = h - 2 * pad;
	spec.contentH = MeasuredHeight(p, [&] { return PaintLeagueAccount(p, m + pad, 0, cw - 2 * pad, actions, mx, my); });
	spec.barX = m + cw - 10;
	spec.drag = &st.dropBar;
	spec.mx = mx;
	spec.my = my;
	{
		ScrollPane pane(p, spec, st.scrolls.Of(kPaneLeagueCard));
		PaintLeagueAccount(p, m + pad, pane.Top(), cw - 2 * pad, actions, mx, my);
	}

	// The ranks of the league, with the emblems players will wear next to their names.
	const LeagueMeView me = LeagueMeState();
	const LeagueTier mine = me.loaded && me.me.ranked.present && me.me.ranked.placementLeft <= 0 ? me.me.ranked.tier : LeagueTier::None;
	const int lx = m + cw + kCardGap;
	PaintRankLadder(p, lx, top, l.w - m - lx, h, mine);
}

bool LinkOfferShown(const State &st)
{
	return st.linkOffer && !st.room.id && !st.game.active && (st.view == kViewRanked || st.view == kViewTournaments) && !LeagueLinked();
}

void PaintLinkOffer(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 580, 252, "League");
	const int mark = 48, tx = d.BodyX() + mark + 20, tw = d.BodyW() - mark - 20;
	DrawSteamMark(p, d.BodyX(), d.y + 80, mark, pal::text);
	p.Font(0);
	const std::string lead = "Link your Steam account to play ranked and tournaments.";
	const int leadH = p.Measure(tw, lead);
	p.TextWrapped(tx, d.y + 76, tw, leadH + 2, lead, pal::text);
	p.Font(3);
	const std::string note = "Sign in to Steam once in the window that opens. There is nothing to type.";
	p.TextWrapped(tx, d.y + 84 + leadH, tw, p.Measure(tw, note) + 2, note, pal::textDim);
	p.Font(1);
	const int noW = std::max(120, p.CapsWidth("Not now", 1) + 44), yesW = std::max(220, p.CapsWidth("Link with Steam", 1) + 76);
	const int by = d.FooterTop(), right = d.x + d.w - d.pad;
	PaintSteamButton(p, right - noW - 12 - yesW, by, yesW, kDialogBtnH, "Link with Steam", kLinkOfferYes, 0, mx, my, true, true);
	p.Button(right - noW, by, noW, kDialogBtnH, "Not now", kLinkOfferNo, 0, mx, my);
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
	static const char *const kVisibility[] = {"Public", "Password protected", "Friends only", "Local network"};
	p.Text(lx, y, 190, fieldH, "Visibility", pal::textDim);
	Combo(p, st, fx, y, fw, fieldH, kVisibility[st.createVisibility % 4], kDropVisibility, 0, true);
	const bool lan = st.createVisibility == 3;
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
	Combo(p, st, fx, y, fw, fieldH, !lan && st.createSave >= 0 && st.createSave < static_cast<int>(st.saves.size()) ? SaveLabel(st.saves[st.createSave]) : "New game",
		kDropSaveGame, 0, !st.saves.empty() && !lan);
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
	if (!lan && st.createSave >= 0 && st.createSave < static_cast<int>(st.saves.size())) {
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
	p.Button(x + (w - bw) / 2, by, bw, bh, restart ? "Restart with this content" : "Create room", kCreateOk, 0, mx, my, st.ready || restart || lan, true);
}

ListGeom g_syncGeomOut;
int g_lobbyScrollOut = -1;

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
	const char *separately = watch ? "Download separately and watch" : "Download separately and join";
	const char *update = watch ? "Update my version and watch" : "Update my version and join";
	int dialogW = updatable ? 760 : 560;
	if (updatable) {
		// The three answers side by side, as DialogButtons lays them out.
		p.Font(1);
		int buttonsW = 2 * 12 + 2 * 24;
		for (const char *label : {separately, update, "Cancel"}) buttonsW += std::max(120, p.CapsWidth(label, 1) + 44);
		dialogW = std::max(dialogW, std::min(buttonsW, st.viewW - 64));
	}
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, dialogW, updatable ? 400 : 340, watch ? "This game uses other content" : "This room uses other content");
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
		DialogButtons(p, d, {{separately, kSyncAccept, 0, true}, {update, kSyncUpdate}, {"Cancel", kSyncCancel}}, mx, my);
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
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 560, 250, "Your last online game");
	p.Font(0);
	const std::string text = Tr("Your game with ") + st.recover.hostName + Tr(" was cut off. If it's still going you can return to it: the game will pause "
		"for a few seconds while you join.");
	p.TextWrapped(d.x + 24, d.y + 62, d.w - 48, 110, text, pal::text);
	DialogButtons(p, d, {{"Return to the game", kRecoverYes, 0, true}, {"Not now", kRecoverNo}}, mx, my);
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
	if (st.dropKind == kDropSaveGame) {
		sel = st.createSave + 1;
		std::vector<std::string> items = {"New game"};
		for (const SaveEntry &e : st.saves) items.push_back(SaveLabel(e));
		return items;
	}
	if (st.dropKind == kDropTourney) {
		sel = st.tourneyDropSel;
		return st.tourneyDropItems;
	}
	if (st.dropKind == kDropRankedSeason) return RankedSeasonItems(st, sel);
	if (st.dropKind == kDropSpeed) {
		std::vector<std::string> items;
		sel = -1;
		for (int i = 0; i < kSpeedChoiceCount; i++) {
			items.push_back(std::to_string(kSpeedChoices[i].percent) + " %");
			if (kSpeedChoices[i].percent == SpeedPercent(st.game.gameSpeed)) sel = i;
		}
		return items;
	}
	if (st.dropKind == kDropVisibility) {
		sel = st.createVisibility;
		return {"Public", "Password protected", "Friends only", "Local network"};
	}
	if (st.dropKind == kDropSpectators) {
		sel = st.createSpec == 5 ? 4 : st.createSpec == 4 ? 5 : st.createSpec;
		return {"No spectators", "With a 30 s delay", "With a 60 s delay", "With a 120 s delay", "With a 300 s delay", "Live"};
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
		if (st.game.host) {
			const int setup = MatchRoomSetup(st);
			if (setup >= 0) sel = kPresetCount + setup;
			for (const RoomSetup &s : st.setups) items.push_back(s.name);
			items.push_back(Tr("Save this setup..."));
		}
		return items;
	}
	if (st.dropKind == kDropSlot) {
		std::vector<std::string> items;
		for (const SlotChoice &c : SlotChoices(st.game, st.game.slots[idx & 7], sel)) items.push_back(c.label);
		return items;
	}
	if (st.dropKind == kDropRace) {
		const GameRoomSlot &s = st.game.slots[idx & 7];
		sel = s.race;
		if (RaceSeparator(s.raceNames)) {
			const std::vector<int> order = CivsRoomOrder(static_cast<int>(s.raceNames.size()));
			std::vector<std::string> shown;
			sel = -1;
			for (size_t k = 0; k < order.size(); k++) {
				if (order[k] == s.race) sel = static_cast<int>(k);
				shown.push_back(s.raceNames[order[k]]);
			}
			return shown;
		}
		return s.raceNames;
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
	if (st.dropKind == kDropColor && st.colorWheel) {
		// The slot's own colour: the wheel, what it looks like, and the buttons to take it or go back to the twelve.
		const int mx = st.mouseX, my = st.mouseY, pad = 12, btnH = 34;
		const int w = pad * 2 + colorwheel::kWidth, h = pad * 2 + colorwheel::kWheel + 10 + 28 + 10 + btnH;
		const int x = std::min<int>(g_dropAnchor.left, st.viewW - 16 - w);
		int y = g_dropAnchor.bottom + 4;
		if (y + h > st.viewH - 8) y = g_dropAnchor.top - 4 - h;
		p.hits->clear();
		p.AddHit(0, 0, st.viewW, st.viewH, kGDropClose, 0);
		p.Fill(x + 4, y + 4, w, h, pal::shadow);
		p.Fill(x, y, w, h, pal::panel);
		p.Frame(x, y, w, h, pal::lineStrong);
		p.AddHit(x, y, w, h, kNone, 0);
		colorwheel::Paint(p, x + pad, y + pad, st.colorTry, pal::panel, kGColorWheel, kGColorValue, st.dropIndex);
		const int sy = y + pad + colorwheel::kWheel + 10;
		p.Fill(x + pad, sy, colorwheel::kWidth, 28, colorwheel::Ref(st.colorTry));
		p.Frame(x + pad, sy, colorwheel::kWidth, 28, pal::edge);
		const int by = sy + 28 + 10, half = (colorwheel::kWidth - 8) / 2;
		p.Button(x + pad, by, half, btnH, "Back", kGColorBack, 0, mx, my, true, false, 3);
		p.Button(x + pad + half + 8, by, colorwheel::kWidth - half - 8, btnH, "Use", kGColorUse, 0, mx, my, true, true, 3);
		return;
	}
	if (st.dropKind == kDropColor) {
		const int mx = st.mouseX, my = st.mouseY, cell = 34, gap = 6, pad = 10, customH = 30;
		const int w = pad * 2 + 6 * cell + 5 * gap, h = pad * 2 + 2 * cell + gap + gap + customH;
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
		{
			const int cy = y + pad + 2 * cell + 2 * gap, cw = w - 2 * pad;
			const bool own = CustomColorOf(st, st.game, st.dropIndex & 7) != 0;
			p.Button(x + pad, cy, cw, customH, own ? "Own colour (in use)..." : "Own colour...", kGColorCustom, 0, mx, my, true, own, 3);
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

// The badge after a sender's name in the room's chat, unless the options hide it.
LeagueBadge ChatBadgeOf(uint64_t id)
{
	return id && GetConfig().chatBadges ? PlayerBadgeOf(id) : LeagueBadge();
}

// A sender's name, its badge and the colon: the badge goes between them, as in the game's chat.
void ChatNameBadge(Painter &p, int nameX, int y, int w, int h, const std::string &name, const LeagueBadge &badge, COLORREF colon, int mx, int my)
{
	p.Font(1);
	DrawBadgeAfterName(p, nameX, y, w, h, name, badge, mx, my);
	const int x = nameX + std::min(std::max(0, w), p.TextWidth(name)) + LeagueBadgeRoom(badge) + 1;
	p.raw = true;
	p.Text(x, y, 12, h, ":", colon, DT_LEFT | DT_TOP | DT_SINGLELINE);
	p.raw = false;
}

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
	// A margin on the left, past the patron's stripe, so the time does not touch it.
	const int pad = 10, barW = 6, textW = w - pad * 2 - barW - 2;
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
			const float nameW = p.TextWidthExact(l.who + ":") + LeagueBadgeRoom(ChatBadgeOf(l.id));
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
			// The laurel centred on the name, which is in a larger type than the line is measured in.
			const int laurelY = py + 1 + (p.Measure(nameW + 40, "A") - kChatLaurel) / 2;
			DrawPatron(p, x + pad + 2, laurelY, kChatLaurel, nameX, py + 1, nameW, lineH, l.who, joinedTier, DT_LEFT | DT_TOP | DT_SINGLELINE);
			if (laurelY >= y && laurelY + kChatLaurel <= y + h) PatronHotspot(p, x + pad + 2, laurelY, kChatLaurel, PatronOf(l.id), st.mouseX, st.mouseY, kPatronLink);
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
		const LeagueBadge badge = ChatBadgeOf(l.id);
		// With a badge the name goes alone, the badge after it and then the colon.
		const std::string who = badge.Shown() ? l.who : l.who + ":";
		p.Font(1);
		// The name is in a larger type than the line: it sits in the middle of the line's height, as the time and the message do.
		const int nameH = p.Measure(200, "A"), nameY = py + (it.lineH - nameH) / 2;
		if (tier != PatronTier::None) {
			const int nameX = laurelX + kChatLaurel + 3, nameW = x + pad + textW - nameX;
			const int laurelY = nameY + (nameH - kChatLaurel) / 2;
			DrawPatron(p, laurelX, laurelY, kChatLaurel, nameX, nameY, nameW, nameH, who, tier, DT_LEFT | DT_TOP | DT_SINGLELINE);
			if (laurelY >= y && laurelY + kChatLaurel <= y + h) PatronHotspot(p, laurelX, laurelY, kChatLaurel, PatronOf(l.id), st.mouseX, st.mouseY, kPatronLink);
			if (badge.Shown()) ChatNameBadge(p, nameX, nameY, nameW - LeagueBadgeRoom(badge) - 12, nameH, l.who, badge, pal::text, st.mouseX, st.mouseY);
		}
		else {
			const int nameW = textW - it.timeW - 6;
			p.Text(laurelX, nameY, nameW, nameH, who, me ? pal::redText : pal::text, DT_LEFT | DT_TOP | DT_SINGLELINE);
			if (badge.Shown()) ChatNameBadge(p, laurelX, nameY, nameW - LeagueBadgeRoom(badge) - 12, nameH, l.who, badge, me ? pal::redText : pal::text, st.mouseX, st.mouseY);
		}
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
