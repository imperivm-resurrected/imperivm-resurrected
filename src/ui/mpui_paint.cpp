// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "mods/modoptions.h"
#include "game/civs.h"
#include "net/rejoin.h"

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
	color = kDim;
	const std::vector<OnlineMod> missing = MissingMods(st, g);
	if (!missing.empty()) {
		uint64_t bytes = 0;
		for (const OnlineMod &m : missing) bytes += m.bytes;
		color = kGold;
		return Tr("Missing ") + std::to_string(missing.size()) + " (" + HumanBytes(bytes) + ")";
	}
	if (SteamRoomContent(g, st.ownKey) == RoomContent::kOtherGameplay) {
		color = kGold;
		return "Restart";
	}
	if (g.mods.empty()) return "Original";
	color = kOk;
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

void PaintHeader(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	if (st.game.active) {
		p.Font(2);
		const std::string title = !st.game.online ? "Skirmish"
								: st.room.id      ? st.room.info.name
												  : "Local network game";
		p.Text(16, 14, kW - 32, 44, title, kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Fill(16, 66, kW - 32, 1, pal::line);
		return;
	}
	// Lined up with the rule under the header, which it ends on.
	DrawPatronRowGlow(p, 16, 12, 760, 54, PatronOf(SteamOwnId()).tier);
	if (!PaintAvatar(p, 20, 16, 44, SteamOwnId())) {
		p.Fill(20, 16, 44, 44, pal::lineDim);
		p.Font(2);
		p.Text(20, 16, 44, 44, st.player.substr(0, 2), kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	p.Frame(20, 16, 44, 44, kGold);
	p.Font(1);
	const PatronTier ownTier = PatronOf(SteamOwnId()).tier;
	if (ownTier != PatronTier::None) {
		DrawPatron(p, 74, 15, 22, 100, 14, 274, 24, st.player, ownTier, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		PatronHotspot(p, 74, 15, 22, PatronOf(SteamOwnId()), mx, my, kPatronLink);
	}
	else p.Text(74, 14, 300, 24, st.player, kText);
	p.Font(3);
	p.Text(74, 38, 400, 20, Tr("Content profile: ") + ModsProfileLabel(ModsMountedProfile().name), kDim);
	if (!st.ready) {
		p.Text(500, 14, 480, 22, st.status.empty() ? "Connecting to Steam..." : st.status, kBad, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	else if (!st.room.id) {
		const size_t n = st.lobbies.size();
		p.Text(500, 14, 480, 22, n == 1 ? "1 open room" : std::to_string(n) + Tr(" open rooms"), kDim,
			DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	p.Fill(16, 66, kW - 32, 1, pal::line);
	PaintTabs(p, 20, 72,
		{{"Games", kListView, 0, st.view == kViewList, true},
			{"Create game", kCreate, 0, st.view == kViewCreate, true},
			{"Ranked", kRanked, 0, st.view == kViewRanked, true}},
		mx, my);
	p.Fill(16, 118, kW - 32, 1, pal::line);
}
}

namespace mpui_impl {
namespace {
void DrawLock(Painter &p, int x, int y, COLORREF c)
{
	p.Frame(x + 2, y, 8, 9, c);
	p.Fill(x, y + 7, 12, 9, c);
	p.Fill(x + 5, y + 10, 2, 4, RGB(30, 22, 12));
}

void PaintLobbyDetail(Painter &p, const State &st, const std::vector<int> &vis)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int dx = 632, dw = kW - dx - 16, dy = 172, dh = 396;
	p.Fill(dx, dy, dw, dh, pal::wellRaised);
	p.Frame(dx, dy, dw, dh, pal::line);
	{
		std::lock_guard<std::mutex> lock(g_chat.mx);
		g_detail.rect = p.R(dx, dy, dw, dh);
		OffsetRect(&g_detail.rect, p.ox, p.oy);
		g_detail.rectValid = true;
	}
	if (st.selected < 0 || st.selected >= static_cast<int>(vis.size())) {
		g_detail.geomOut = ListGeom();
		p.Font(0);
		p.TextWrapped(dx + 20, dy + 20, dw - 40, 80,
			vis.empty() ? "When there are rooms, pick one to see its content and join, or press \"Quick game\" and we'll find "
						  "one for you."
						: "Pick a room from the list to see its content and join.",
			kDim);
		return;
	}
	const OnlineLobby &g = st.lobbies[vis[st.selected]];
	const bool full = g.players >= g.maxPlayers;
	const bool same = SteamRoomContent(g, st.ownKey) != RoomContent::kOtherGameplay;
	const std::vector<OnlineMod> missing = MissingMods(st, g);
	uint64_t bytes = 0;
	for (const OnlineMod &m : missing) bytes += m.bytes;

	const bool canReturn = CanReturnToGame(st, g);
	const int returnRoom = canReturn && g.specLobby ? 48 : 0;
	const int viewTop = dy + 1, viewH = dh - 70 - returnRoom;
	const int x = dx + 16, w = dw - 36;
	const int startY = viewTop + 12;
	int y = startY - st.detail.scroll;
	p.Clip(dx + 1, viewTop, dw - 2, viewH);

	auto section = [&](const std::string &title) {
		y += 6;
		p.Font(1);
		p.Text(x, y, w, 22, title, kText);
		y += 24;
		p.Fill(x, y, w, 1, pal::lineDim);
		y += 8;
		p.Font(3);
	};
	auto note = [&](COLORREF bar, COLORREF back, const std::string &text) {
		p.Font(3);
		const int th = p.Measure(w - 22, text);
		p.Fill(x, y, w, th + 14, back);
		p.Fill(x, y, 3, th + 14, bar);
		p.TextWrapped(x + 12, y + 7, w - 22, th + 2, text, kText);
		y += th + 22;
	};

	p.Font(2);
	const int nameH = std::max(30, p.Measure(w, g.name));
	p.TextWrapped(x, y, w, nameH, g.name, kGold);
	y += nameH;

	section("Game content");
	y = PaintLinesInline(p, OnlineLobbyLines(st, g, true), x, y, w, 20) + 6;
	if (!same) {
		y += 2;
		note(kBad, RGB(44, 26, 20),
			missing.empty() ? "Your content doesn't match the room's. The game will restart with the room's."
							: std::string(missing.size() == 1 ? Tr("You don't have the content this room needs. The game will download ")
															  : Tr("You don't have the content this room needs. The game will download these: ")) +
								std::to_string(missing.size()) + (missing.size() == 1 ? Tr(" content pack") : Tr(" content packs")) + " (" + HumanBytes(bytes) + Tr(") and restart."));
	}
	if (g.password) note(kGold, RGB(34, 27, 18), "Password protected");

	if (!g.saveName.empty()) note(kGold, RGB(34, 27, 18), Tr("Continues the saved game \"") + g.saveName + "\"");
	if (canReturn) note(kOk, RGB(26, 36, 22), "You were in this game and your seat is still free: you can return to it.");

	section("Players");
	if (!g.slots.empty()) {
		int n = 0;
		bool hostShown = false;
		for (const OnlineSlot &s : g.slots) {
			if (s.type == 2) continue;
			n++;
			const bool human = s.type == 3, ai = s.type == 1;
			p.Text(x, y, 24, 20, std::to_string(n), kDim);
			const PatronInfo patron = human ? PatronOf(LobbyPlayerId(g, s.name)) : PatronInfo();
			if (patron.tier != PatronTier::None) {
				DrawPatron(p, x + 26, y + 2, 16, x + 46, y, w - 46 - 80, 20, s.name, patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				PatronHotspot(p, x + 26, y + 2, 16, patron, st.mouseX, st.mouseY, kPatronLink);
			}
			else p.Text(x + 26, y, w - 26 - 80, 20, human ? s.name : ai ? (s.name.empty() ? "AI" : s.name) : "Free", human ? kText : kDim);
			if (human && !hostShown && PlayerNameMatches(s.name, g.host)) {
				hostShown = true;
				p.Text(x + w - 90, y, 90, 20, "host", kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			}
			y += 22;
		}
	}
	else if (g.playerNames.empty()) {
		p.Text(x, y, w, 20, std::to_string(g.players) + Tr(" of ") + std::to_string(g.maxPlayers) + Tr(" slots taken"), kDim);
		y += 24;
	}
	else {
		for (int i = 0; i < std::max(g.maxPlayers, static_cast<int>(g.playerNames.size())); i++) {
			const bool taken = i < static_cast<int>(g.playerNames.size());
			p.Text(x, y, 24, 20, std::to_string(i + 1), kDim);
			const PatronInfo patron = taken ? PatronOf(LobbyPlayerId(g, g.playerNames[i])) : PatronInfo();
			if (patron.tier != PatronTier::None) {
				DrawPatron(p, x + 26, y + 2, 16, x + 46, y, w - 46 - 80, 20, g.playerNames[i], patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				PatronHotspot(p, x + 26, y + 2, 16, patron, st.mouseX, st.mouseY, kPatronLink);
			}
			else p.Text(x + 26, y, w - 26, 20, taken ? g.playerNames[i] : "Free", taken ? kText : kDim);
			if (taken && i == 0) p.Text(x + w - 90, y, 90, 20, "host", kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			y += 22;
		}
	}

	section("Options");
	{
		size_t from = 0;
		while (from < g.optionsText.size()) {
			size_t to = g.optionsText.find('\n', from);
			if (to == std::string::npos) to = g.optionsText.size();
			const std::string line = g.optionsText.substr(from, to - from);
			from = to + 1;
			if (line.empty()) continue;
			const size_t colon = line.find(": ");
			const std::string key = colon == std::string::npos ? line : line.substr(0, colon);
			const std::string value = colon == std::string::npos ? std::string() : key == "Map type" ? MapTypeLabel(line.substr(colon + 2)) : line.substr(colon + 2);
			const bool yes = value == "yes", no = value == "no";
			p.Text(x, y, (yes || no) ? w - Painter::kToggleW - 10 : w / 2, 20, colon == std::string::npos ? line : line.substr(0, colon), kDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (yes || no) p.Toggle(x + w - Painter::kToggleW, y + (20 - Painter::kToggleH) / 2, yes, true, false);
			else if (colon != std::string::npos) p.Text(x + w / 2, y, w - w / 2, 20, value, kText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			y += 22;
		}
		const bool watchable = g.specLobby != 0;
		std::string spec = SpectatorText(watchable, g.specDelay);
		if (watchable && g.playing && g.spectators > 0) spec += "  ·  " + std::to_string(g.spectators) + Tr(" watching");
		p.Text(x, y, w / 2, 20, "Spectators", kDim);
		p.Text(x + w / 2, y, w - w / 2, 20, spec, watchable ? kText : kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += 22;
	}
	p.Unclip();

	const int total = (y + st.detail.scroll) - startY + 12;
	{
		ListState view = st.detail;
		view.scroll = std::min(st.detail.scroll, std::max(0, total - viewH + 12));
		ListGeom geom;
		ScrollbarPaint(p, dx + dw - 11, viewTop, viewH, total + 12, viewH, view, geom, mx, my);
		g_detail.geomOut = geom;
	}

	p.Fill(dx + 1, dy + dh - 66 - returnRoom, dw - 2, 1, pal::lineDim);
	if (canReturn) p.Button(x, dy + dh - 56 - returnRoom, dw - 32, 42, "Return to the game", kReturnGame, 0, mx, my, st.ready && !RejoinBusy(), true);
	const std::string label = g.kickedMe ? "You were kicked from the room" : g.playing ? "Game in progress" : full ? "Room full" : same ? "Join" : missing.empty() ? "Restart and join" : "Download and join";
	if (g.specOnly) {
		if (g.playing) p.Button(x, dy + dh - 56, dw - 32, 42, "Watch the game", kSpectate, 0, mx, my, st.ready, true);
		else p.Button(x, dy + dh - 56, dw - 32, 42, "Room full", kJoin, 0, mx, my, false, false);
	}
	else if (g.playing && g.specLobby) {
		p.Button(x, dy + dh - 56, dw - 32, 42, "Watch the game", kSpectate, 0, mx, my, st.ready, !canReturn);
	}
	else if (!canReturn) {
		p.Button(x, dy + dh - 56, dw - 32, 42, label, kJoin, 0, mx, my, !g.playing && !full && !g.kickedMe && st.ready, true);
	}
}
}
}

namespace mpui_impl {
void PaintPassPrompt(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, kW, kH, 480, 214, "Password-protected room", kGold, 26);
	DrawLock(p, d.x + 24, d.y + 22, kGold);
	DialogText(p, d, 58, "\"" + st.passLobby.name + Tr("\", hosted by ") + st.passLobby.host + Tr(". Enter the password to join."), kDim, 3);
	PaintField(p, st, d.x + 24, d.y + 104, d.w - 48, 34, st.joinPassInput, "Password", st.focus == kJoinPassField, kFieldJoinPass, true);
	DialogButtons(p, d, {{"OK", kPassOk, 0, true}, {"Cancel", kPassCancel}}, mx, my);
}

void PaintList(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	static const char *const kFilterNames[] = {"All rooms", "Friends'", "No password", "Password protected", "Original game", "With my content", "With open slots", "In progress"};
	Combo(p, st, 16, 132, 190, 30, kFilterNames[st.filter < 0 || st.filter > 7 ? 0 : st.filter], kDropFilter, 0, true);
	PaintField(p, st, 214, 132, 254, 30, st.searchInput, "Search room, host or map...", st.focus == kSearchField, kFieldSearch);
	p.Button(476, 132, 140, 30, st.loading ? "Searching..." : "Refresh", kRefresh, 0, mx, my, st.ready && !st.loading);
	p.Button(632, 132, kW - 632 - 16, 30, "Quick game", kQuick, 0, mx, my, st.ready, true);
	const int tx = 16, tw = 600, ty = 172, rowH = 32;
	p.Fill(tx, ty, tw, 396, pal::well);
	p.Frame(tx, ty, tw, 396, pal::line);
	static const char *const kCols[] = {"Room", "Host", "Players", "Map", "Content"};
	int cx[6];
	cx[0] = tx + 8;
	for (int c = 0; c < 4; c++) cx[c + 1] = cx[c] + st.colW[c];
	cx[5] = tx + tw - 8;
	p.Fill(tx + 1, ty + 1, tw - 2, 26, pal::dialog);
	p.Fill(tx + 1, ty + 27, tw - 2, 1, pal::divider);
	for (int c = 0; c < 5; c++) {
		const int w = cx[c + 1] - cx[c];
		const bool sorted = st.sortCol == c, hov = p.Hover(cx[c], ty + 1, w - 4, 26, mx, my);
		p.Font(3);
		p.Text(cx[c] + 2, ty + 1, w - 20, 26, kCols[c], sorted ? kGold : hov ? kText : kDim);
		if (sorted) {
			if (st.sortDesc) p.ArrowDown(cx[c + 1] - 16, ty + 12, 8, 4, kGold);
			else p.ArrowUp(cx[c + 1] - 16, ty + 11, 8, 4, kGold);
		}
		p.AddHit(cx[c], ty + 1, w - 4, 26, kSort, c);
		if (c < 4) {
			const bool grab = st.resizeCol == c || p.Hover(cx[c + 1] - 4, ty + 1, 8, 26, mx, my);
			p.Fill(cx[c + 1] - 1, ty + 5, 1, 18, grab ? kGold : pal::divider);
			p.AddHit(cx[c + 1] - 4, ty + 1, 8, 26, kColResize, c);
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
	if (vis.empty()) {
		p.Font(0);
		const std::string empty = !st.ready ? (st.status.empty() ? "Connecting to Steam..." : st.status)
			: st.loading ? "Searching for rooms..."
			: "There are no open rooms with this filter. Create one with \"Create game\" and invite your friends.";
		p.TextWrapped(tx + 14, ty + 40, tw - 28, 80, empty, kDim);
	}
	for (int r = 0; r < static_cast<int>(vis.size()) && r < 11; r++) {
		const OnlineLobby &g = st.lobbies[vis[r]];
		const int y = ty + 28 + r * rowH;
		if (r == st.selected) p.Fill(tx + 1, y, tw - 2, rowH, pal::lineDim);
		else if (p.Hover(tx, y, tw, rowH, mx, my)) p.Fill(tx + 1, y, tw - 2, rowH, RGB(44, 34, 24));
		p.AddHit(tx, y, tw, rowH, kSelect, r);
		auto cell = [&](int c, const std::string &text, COLORREF color) {
			p.Text(cx[c] + 2, y, cx[c + 1] - cx[c] - 8, rowH, text, color);
		};
		p.Font(1);
		if (g.password) DrawLock(p, cx[0] + 4, y + (rowH - 16) / 2, kGold);
		p.Text(cx[0] + 2 + (g.password ? 20 : 0), y, cx[1] - cx[0] - 8 - (g.password ? 20 : 0), rowH, g.name, kText);
		p.Font(3);
		const PatronInfo hostPatron = PatronOf(g.owner);
		if (hostPatron.tier != PatronTier::None) {
			const int hx = cx[1] + 2, hw = cx[2] - cx[1] - 8, ly = y + (rowH - 16) / 2;
			DrawPatron(p, hx, ly, 16, hx + 20, y, hw - 20, rowH, g.friendHost ? g.host + Tr("  (friend)") : g.host, hostPatron.tier,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			PatronHotspot(p, hx, ly, 16, hostPatron, mx, my, kPatronLink);
		}
		else cell(1, g.friendHost ? g.host + Tr("  (friend)") : g.host, g.friendHost ? kGold : kDim);
		char watching[48];
		snprintf(watching, sizeof(watching), "In game  ·  %d watching", g.spectators);
		cell(2, g.playing ? (g.spectators > 0 ? std::string(watching) : std::string("In game")) : std::to_string(g.players) + " / " + std::to_string(g.maxPlayers),
			g.players < g.maxPlayers && !g.playing ? kText : kDim);
		cell(3, LobbyMapLabel(g), kDim);
		COLORREF color;
		const std::string mods = ModsColumn(st, g, color);
		cell(4, mods, color);
	}
	PaintLobbyDetail(p, st, vis);
}

void PaintRanked(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	p.Fill(16, 132, 380, 430, pal::wellRaised);
	p.Frame(16, 132, 380, 430, pal::line);
	p.Fill(150, 184, 112, 112, RGB(70, 54, 24));
	p.Frame(150, 184, 112, 112, kGold);
	p.Frame(156, 190, 100, 100, pal::line);
	p.Font(2);
	p.Text(150, 184, 112, 112, "LEAGUES", kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Font(2);
	p.Text(16, 322, 380, 40, "Coming soon", kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Font(1);
	p.Text(16, 364, 380, 30, "Ranked play", kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Fill(176, 404, 60, 1, kGold);
	p.Font(3);
	p.Text(40, 420, 332, 96, "Elo rating, leagues and seasons will arrive in a future update.", kDim, DT_CENTER | DT_WORDBREAK);
	const int lx = 412, lw = kW - lx - 16;
	p.Fill(lx, 132, lw, 430, pal::well);
	p.Frame(lx, 132, lw, 430, pal::line);
	p.Font(1);
	p.Text(lx + 12, 138, lw - 160, 26, "Season ranking", kGold);
	p.Font(3);
	p.Fill(lx + lw - 136, 142, 124, 22, RGB(70, 54, 24));
	p.Frame(lx + lw - 136, 142, 124, 22, kGold);
	p.Text(lx + lw - 136, 142, 124, 22, "PREVIEW", kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	struct Row {
		const char *name, *league;
		int elo, wins, losses;
	} rows[] = {{"Scipio", "Caesar", 2210, 88, 21}, {"Vercingetorix", "Caesar", 2154, 75, 30}, {"Hannibal", "Praetorian", 1987, 64, 33},
		{"Boudica", "Praetorian", 1902, 51, 29}, {"Spartacus", "Praetorian", 1866, 70, 52}, {"Cleopatra", "Gold I", 1790, 40, 25},
		{"Marco", "Gold I", 1754, 38, 30}, {"Livia", "Gold II", 1702, 33, 28}};
	const COLORREF dimText = RGB(150, 138, 118);
	p.Text(lx + 14, 176, 40, 20, "#", kDim);
	p.Text(lx + 54, 176, 200, 20, "Player", kDim);
	p.Text(lx + 262, 176, 110, 20, "League", kDim);
	p.Text(lx + 382, 176, 80, 20, "Elo", kDim);
	p.Text(lx + 462, 176, 100, 20, "W-L", kDim);
	p.Fill(lx + 8, 198, lw - 16, 1, pal::line);
	int y = 204;
	for (int i = 0; i < 8; i++, y += 36) {
		if (i % 2 == 0) p.Fill(lx + 8, y, lw - 16, 34, pal::wellRaised);
		p.Text(lx + 14, y, 40, 34, std::to_string(i + 1), dimText);
		p.Text(lx + 54, y, 200, 34, rows[i].name, dimText);
		p.Text(lx + 262, y, 110, 34, rows[i].league, dimText);
		p.Text(lx + 382, y, 80, 34, std::to_string(rows[i].elo), dimText);
		p.Text(lx + 462, y, 100, 34, std::to_string(rows[i].wins) + "-" + std::to_string(rows[i].losses), dimText);
	}
	p.Button(lx + lw - 180, 132 + 430 - 42, 168, 32, "Back to list", kListView, 0, mx, my);
}

void PaintCreate(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int x = 190, w = 620, y0 = 134, h = 478;
	p.Fill(x, y0, w, h, pal::wellRaised);
	p.Frame(x, y0, w, h, pal::line);
	const int lx = x + 24, fx = x + 170, fw = w - 194;
	int y = y0 + 22;
	p.Font(0);
	p.Text(lx, y, 140, 30, "Name", kDim);
	PaintField(p, st, fx, y, fw, 30, st.nameInput, Tr("Game of ") + st.player, st.focus == kNameField, kFieldName);
	y += 42;
	p.Text(lx, y, 140, 30, "Players", kDim);
	Combo(p, st, fx, y, 200, 30, std::to_string(st.createPlayers) + Tr(" players"), kDropPlayers, 0, true);
	y += 42;
	static const char *const kVisibility[] = {"Public", "Password protected", "Friends only"};
	p.Text(lx, y, 140, 30, "Visibility", kDim);
	Combo(p, st, fx, y, fw, 30, kVisibility[st.createVisibility % 3], kDropVisibility, 0, true);
	y += 42;
	if (st.createVisibility == 1) {
		p.Font(0);
		p.Text(lx, y, 140, 30, "Password", kDim);
		PaintField(p, st, fx, y, fw - 98, 30, st.passInput, "Room password", st.focus == kPassField, kFieldPass, !st.passShow);
		p.Font(3);
		p.Button(fx + fw - 90, y, 90, 30, st.passShow ? "Hide" : "Show", kPassShow, 0, mx, my);
		p.Font(0);
		y += 42;
	}
	p.Font(0);
	p.Text(lx, y, 140, 30, "Game", kDim);
	Combo(p, st, fx, y, fw, 30, st.createSave >= 0 && st.createSave < static_cast<int>(st.saves.size()) ? SaveLabel(st.saves[st.createSave]) : "New game",
		kDropSaveGame, 0, !st.saves.empty());
	y += 42;
	p.Font(0);
	p.Text(lx, y, 140, 30, "Content", kDim);
	Combo(p, st, fx, y, fw, 30, ModsProfileLabel(st.createProfile), kDropProfile, 0, st.profiles.size() > 1);
	y += 38;
	{
		const std::vector<ListLine> lines = BuildModLines(st.createProfileInfo.rows, "None: the original game.");
		const int rows = std::min<int>(6, static_cast<int>(lines.size()));
		g_createModsGeomOut = ListGeom();
		ListState ls = st.createModsList;
		ListPaint(p, fx, y, fw, rows * 22 + 8, lines, ls, g_createModsGeomOut, 3, 22);
		y += rows * 22 + 14;
	}
	if (st.createSave >= 0 && st.createSave < static_cast<int>(st.saves.size())) {
		const ModNeeds &needs = CreateSaveNeeds(st.saves[st.createSave].vfsPath, st.createProfile);
		if (!needs.Satisfied()) {
			y += 4;
			p.Text(fx + 4, y, fw, 20, Tr("This game needs: ") + ShortList(needs.Names(true), 2), kBad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 20;
			p.Text(fx + 4, y, fw, 20, needs.AllInstalled() ? "They are activated when you create it (the game must restart)." : "You don't have it installed: Options > Content > Browse content.", kDim,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 20;
		}
	}
	const bool restart = st.createProfile != ModsMountedProfile().name;
	const int by = y0 + h - 60;
	const int bw = 320;
	p.Button(x + (w - bw) / 2, by, bw, 42, restart ? "Restart with this content" : "Create room", kCreateOk, 0, mx, my,
		st.ready || restart, true);
}

void PaintChat(Painter &p, const State &st)
{
	p.Button(kW - 16 - 140, kH - 16 - 44, 140, 44, "Back", kBack, 0, st.mouseX, st.mouseY);
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
	const Dialog d = DialogBegin(p, kW, kH, updatable ? 760 : 560, updatable ? 400 : 340, watch ? "This game uses other content" : "This room uses other content");
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
	p.TextWrapped(d.x + 24, d.y + d.h - 130, d.w - 48, 64, missing.empty() ? std::string(ready) : Tr(where) + " (" + HumanBytes(bytes) + Tr(then), kDim);
	if (updatable) {
		p.TextWrapped(d.x + 24, d.y + d.h - 190, d.w - 48, 56,
			Tr("Download separately leaves your version as it is and uses the room's only for this game. Update my "
			"version replaces it on restart (the old one is kept as a backup in mods\\_downloads\\backup).") +
				(st.syncUsing.empty() ? std::string() : Tr(" Used by your profiles: ") + st.syncUsing + "."),
			kDim);
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
	const Dialog d = DialogBegin(p, kW, kH, 520, 200, title, st.syncPhase == 4 ? kBad : kGold);
	p.Font(0);
	if (st.syncPhase == 4) {
		p.TextWrapped(d.x + 24, d.y + 60, d.w - 48, 70, st.syncError, kText);
		DialogButtons(p, d, {{"Close", kSyncCancel}}, mx, my);
		return;
	}
	if (st.syncPhase == 2 && st.syncStage == 1 && st.io.unpacking && st.io.unpackTotal) {
		p.Font(3);
		std::string what = Tr("Installing ") + st.io.what;
		if (st.io.count > 1) what += Tr("  ·  pack ") + std::to_string(std::max(1, st.io.index)) + Tr(" of ") + std::to_string(st.io.count);
		p.Text(d.x + 24, d.y + 56, d.w - 48, 18, what, kOk);
		PaintProgress(p, d.x + 24, d.y + 80, d.w - 48, 14, static_cast<float>(static_cast<double>(st.io.unpacked) / st.io.unpackTotal));
		p.Text(d.x + 24, d.y + 100, d.w - 48, 22, TransferText(st.io.unpacked, st.io.unpackTotal, 0), kDim);
	}
	else if (st.syncPhase == 2) {
		const bool fromIo = st.syncStage == 1;
		const uint64_t got = fromIo ? st.io.got : st.dl.got, all = fromIo ? st.io.total : st.dl.total;
		const uint64_t total = std::max<uint64_t>(1, all);
		const int pct = static_cast<int>(std::min<uint64_t>(100, got * 100 / total));
		p.Font(3);
		std::string source = fromIo ? "Source: GameBanana" : "Source: the host";
		if (fromIo && st.io.count > 0) source += Tr("  ·  pack ") + std::to_string(std::max(1, st.io.index)) + Tr(" of ") + std::to_string(st.io.count);
		p.Text(d.x + 24, d.y + 56, d.w - 48, 18, source, fromIo ? kOk : kGold);
		PaintProgress(p, d.x + 24, d.y + 80, d.w - 48, 14, pct / 100.0f);
		p.Text(d.x + 24, d.y + 100, d.w - 48, 22, TransferText(got, all, st.rate.perSecond), kDim);
	}
	else if (st.syncPhase == 3 && st.hashTotal) {
		p.Font(3);
		p.Text(d.x + 24, d.y + 56, d.w - 48, 18, "Checking the content's files; then the game restarts.", kGold);
		PaintProgress(p, d.x + 24, d.y + 80, d.w - 48, 14, static_cast<float>(static_cast<double>(st.hashDone) / st.hashTotal));
		p.Text(d.x + 24, d.y + 100, d.w - 48, 22, TransferText(st.hashDone, st.hashTotal, st.rate.perSecond), kDim);
	}
	else {
		p.Font(3);
		p.Text(d.x + 24, d.y + 76, d.w - 48, 22,
			st.syncPhase == 1 ? "Connecting to the host..." : "Setting up the room's profile; then the game restarts.", kDim);
	}
	DialogButtons(p, d, {{"Cancel", kSyncCancel}}, mx, my);
}

void PaintRejoin(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const bool failed = RejoinClientFailed();
	const Dialog d = DialogBegin(p, kW, kH, 560, 230, failed ? "Could not return" : "Returning to the game", failed ? kBad : kGold);
	p.Font(0);
	p.TextWrapped(d.x + 24, d.y + 62, d.w - 48, 80, RejoinClientStatus(), kText);
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
	const Dialog d = DialogBegin(p, kW, kH, 560, 250, waiting ? "Waiting for the host" : "Your last online game");
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
	p.TextWrapped(d.x + 24, d.y + 62, d.w - 48, 110, text, kText);
	if (waiting) {
		PaintBusyBar(p, d.x + 24, d.y + 150, d.w - 48, 8, GetTickCount());
		DialogButtons(p, d, {{"Cancel", kRecoverNo}}, mx, my, true);
		return;
	}
	DialogButtons(p, d, {{r.host ? "Continue the game" : "Return to the game", kRecoverYes, 0, true}, {"Not now", kRecoverNo}}, mx, my);
}

void PaintSearch(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const DWORD secs = (GetTickCount() - st.searchStart) / 1000;
	char t[32];
	sprintf_s(t, "%lu:%02lu", secs / 60, secs % 60);
	if (!st.found) {
		const Dialog d = DialogBegin(p, kW, kH, 460, 190, Tr("Searching ") + st.searchMode);
		p.Font(0);
		p.Text(d.BodyX(), d.y + 62, d.BodyW(), 26, std::string("Time: ") + t + Tr("   ·   preview"), kText);
		PaintBusyBar(p, d.BodyX(), d.y + 100, d.BodyW(), 8, GetTickCount());
		DialogButtons(p, d, {{"Cancel", kCancelSearch}}, mx, my, true);
	}
	else {
		const int left = 10 - static_cast<int>((GetTickCount() - st.foundAt) / 1000);
		const Dialog d = DialogBegin(p, kW, kH, 460, 190, "Match found (example)");
		p.Font(0);
		p.Text(d.BodyX(), d.y + 58, d.BodyW(), 24, "1v1 vs. Lucius (Gold I, 1,702)  ·  Numantia", kText);
		p.Text(d.BodyX(), d.y + 84, d.BodyW(), 22, Tr("Accept in ") + std::to_string(std::max(0, left)) + " s", kDim);
		DialogButtons(p, d, {{"OK", kAccept, 0, true}, {"Decline", kDecline}}, mx, my);
	}
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
		kind | (index << 4), &g_dropAnchor);
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
		const int x = std::min<int>(g_dropAnchor.left, kW - 16 - w);
		int y = g_dropAnchor.bottom + 4;
		if (y + h > kH - 8) y = g_dropAnchor.top - 4 - h;
		p.hits->clear();
		p.AddHit(0, 0, kW, kH, kGDropClose, 0);
		p.Fill(x + 4, y + 4, w, h, pal::shadow);
		p.Fill(x, y, w, h, pal::dialog);
		p.Frame(x, y, w, h, kGold);
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
				p.Line(cx + 3, cy + 3, cx + cell - 3, cy + cell - 3, RGB(230, 210, 170), 2);
				p.Frame(cx, cy, cell, cell, RGB(70, 56, 38));
				continue;
			}
			if (c == sel) {
				p.Frame(cx - 3, cy - 3, cell + 6, cell + 6, pal::focus);
				p.Frame(cx, cy, cell, cell, RGB(20, 15, 10));
			}
			else {
				p.Frame(cx, cy, cell, cell, hov ? pal::focus : pal::line);
				if (hov) p.Frame(cx - 2, cy - 2, cell + 4, cell + 4, pal::edgeStrong);
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
	PaintDropMenu(p, g_dropAnchor, shown, sel, st.dropScroll, kW, kH, kGDropPick, kGDropClose, st.mouseX, st.mouseY, st.dropKind == kDropBonus ? 150 : 200);
}
}

namespace mpui_impl {
namespace {
const int kChatLaurel = 15;

COLORREF ChatColor(uint64_t id, bool me)
{
	static const COLORREF kNames[] = {RGB(120, 190, 235), RGB(140, 205, 120), RGB(232, 140, 110), RGB(196, 152, 236),
		RGB(110, 214, 204), RGB(238, 154, 196)};
	return me ? kGold : kNames[id % 6];
}

COLORREF ChatNameColor(const State &st, const OnlineChatLine &l, bool me)
{
	if (st.game.active) {
		for (int i = 0; i < 8; i++) {
			const GameRoomSlot &s = st.game.slots[i];
			if (!s.used || s.type != 3 || !(me ? s.local : PlayerNameMatches(s.name, l.who))) continue;
			COLORREF c = PlayerColor(st.game, s.color);
			int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
			if (r * 3 + g * 6 + b < 900) {
				r += (255 - r) * 45 / 100;
				g += (255 - g) * 45 / 100;
				b += (255 - b) * 45 / 100;
			}
			return RGB(r, g, b);
		}
	}
	return ChatColor(l.id, me);
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
	p.Fill(x, y, w, h, RGB(12, 9, 6));
	p.Frame(x, y, w, h, pal::lineDim);
	g_chat.rect = p.R(x, y, w, h);
	OffsetRect(&g_chat.rect, p.ox, p.oy);
	g_chat.rectValid = true;
	if (chat.empty()) return;
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
	for (const OnlineChatLine &l : chat) {
		Item it;
		it.indent = 0;
		it.lineH = lineH;
		if (l.system && l.patron && PatronOf(l.id).tier != PatronTier::None) {
			it.text = l.text;
			it.h = lineH + 8;
		}
		else if (l.system) {
			it.text = "\xE2\x80\x94 " + l.text;
			p.Font(3);
			it.h = p.Measure(textW, it.text);
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
	const int top = total > h ? total - h - g_chat.scroll : 0;
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
			p.Fill(x + 1, top, w - 2, height, PatronBlend(RGB(12, 9, 6), c.laurel, 14));
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
			p.Text(nameX + nameW, py + 1, x + pad + textW - nameX - nameW, lineH, rest, kText, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
			continue;
		}
		if (l.system) {
			p.Font(3);
			p.TextWrapped(x + pad, py, textW, it.h + 2, it.text, kDim);
			continue;
		}
		const bool me = l.id == SteamOwnId();
		const PatronTier tier = PatronOf(l.id).tier;
		if (tier != PatronTier::None) patronBand(tier, py - 2, it.h + 2);
		p.Font(3);
		p.Text(x + pad, py, it.timeW, 18, ChatTime(l.minute), kDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
		const int laurelX = x + pad + it.timeW + 6;
		p.Font(1);
		if (tier != PatronTier::None) {
			const int nameX = laurelX + kChatLaurel + 3;
			DrawPatron(p, laurelX, py + 1, kChatLaurel, nameX, py, x + pad + textW - nameX, 18, l.who + ":", tier, DT_LEFT | DT_TOP | DT_SINGLELINE);
			if (py + 1 >= y && py + 1 + kChatLaurel <= y + h) PatronHotspot(p, laurelX, py + 1, kChatLaurel, PatronOf(l.id), st.mouseX, st.mouseY, kPatronLink);
		}
		else p.Text(laurelX, py, textW - it.timeW - 6, 18, l.who + ":", ChatNameColor(st, l, me), DT_LEFT | DT_TOP | DT_SINGLELINE);
		p.Font(3);
		for (size_t k = 0; k < it.lines.size(); k++) {
			const int shift = k == 0 ? it.indent : 0;
			p.LineW(x + pad + shift, py + static_cast<int>(k) * it.lineH, textW - shift, it.lineH, it.lines[k], kText);
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
