// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "mods/modoptions.h"
#include "net/rejoin.h"
#include "net/spectator.h"

#include "ui/mpui_internal.h"
#include "ui/patronui.h"
#include "game/gamemodes.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
const int kRaceX = 262, kRaceW = 122, kBonusX = 388, kBonusW = 92, kTeamX = 484, kTeamW = 34, kBadgeX = 524, kBadgeW = 70, kPlayersW = 600;

void PaintGameRoom(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const bool load = g.online && (g.host ? !st.hostSave.name.empty() : !st.room.info.saveName.empty());
	const COLORREF panel = pal::wellRaised, line = pal::line, rowAlt = pal::dialog;

	const int rowH = g.online ? 37 : 50;
	const int px = 16, pw = kPlayersW, py = 78, ph = 52 + 8 * rowH + 8;
	p.Fill(px, py, pw, ph, panel);
	p.Frame(px, py, pw, ph, line);
	p.Font(1);
	p.Text(px + 14, py + 6, 200, 24, "Players", kGold);
	int humans = 0, readyCount = 0;
	for (const GameRoomSlot &s : g.slots) {
		if (s.used && s.type == 3) {
			humans++;
			if (s.ready) readyCount++;
		}
	}
	p.Font(3);
	int mapPositions = 0;
	for (const GameRoomSlot &s : g.slots) mapPositions += s.exists ? 1 : 0;
	std::string headInfo = mapPositions < 8 ? Tr("Map for ") + std::to_string(mapPositions) : "";
	if (g.online) headInfo += (headInfo.empty() ? "" : "  ·  ") + std::to_string(readyCount) + Tr(" of ") + std::to_string(humans) + Tr(" ready");
	p.Text(px + pw - 320, py + 6, 306, 24, headInfo, kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.Text(px + 48, py + 30, 180, 18, "Player", kDim);
	p.Text(px + kRaceX, py + 30, kRaceW, 18, "Nation", kDim);
	p.Text(px + kBonusX + 8, py + 30, kBonusW - 8, 18, "Bonus", kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.Text(px + kTeamX - 6, py + 30, 50, 18, "Team", kDim);
	std::string arriving[8];
	for (uint64_t &id : g_flow.arrivingIds) id = 0;
	if (g.online) {
		struct Waiting {
			std::string label;
			uint64_t id;
			std::string name;
		};
		std::vector<Waiting> waiting;
		for (const OnlineMember &m : st.room.members) {
			if (m.me) continue;
			bool inGame = false;
			for (const GameRoomSlot &s : g.slots) {
				if (s.used && s.type == 3 && PlayerNameMatches(s.name, m.name)) inGame = true;
			}
			if (inGame) continue;
			if (m.receiving >= 0) {
				waiting.push_back({m.name + Tr("  ·  downloading content ") +
					std::to_string(m.receiving) + " %", m.id, m.name});
			}
			else {
				waiting.push_back({m.name + Tr("  ·  joining..."), m.id, m.name});
			}
		}
		for (const OnlineMember &m : st.room.restarting) {
			waiting.push_back({m.name + Tr("  ·  restarting the game with the content..."), m.id, m.name});
		}
		auto freeSlot = [&](int i) { return !g.slots[i].used && !g.slots[i].local && g.slots[i].type == 0 && arriving[i].empty(); };
		std::vector<bool> placed(waiting.size(), false);
		if (load) {
			for (size_t w = 0; w < waiting.size(); w++) {
				for (size_t j = 0; j < st.savePlayers.size() && j < 8; j++) {
					const MpSavePlayer &sp = st.savePlayers[j];
					if (!sp.used() || sp.ai() || _stricmp(sp.name.c_str(), waiting[w].name.c_str()) != 0) continue;
					if (freeSlot(static_cast<int>(j))) {
						arriving[j] = waiting[w].label;
						g_flow.arrivingIds[j] = waiting[w].id;
						placed[w] = true;
					}
					break;
				}
			}
		}
		size_t next = 0;
		for (int i = 0; i < 8; i++) {
			while (next < waiting.size() && placed[next]) next++;
			if (next >= waiting.size()) break;
			if (g.slots[i].joining && freeSlot(i)) {
				arriving[i] = waiting[next].label;
				g_flow.arrivingIds[i] = waiting[next].id;
				placed[next] = true;
			}
		}
		next = 0;
		for (int i = 0; i < 8; i++) {
			while (next < waiting.size() && placed[next]) next++;
			if (next >= waiting.size()) break;
			if (freeSlot(i)) {
				arriving[i] = waiting[next].label;
				g_flow.arrivingIds[i] = waiting[next].id;
				next++;
			}
		}
	}
	const bool movingRows = g.host && st.moveFrom >= 0 && st.moveFrom < 8;
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &s = g.slots[i];
		const int rowY = py + 52 + i * rowH, y = rowY + (rowH - 37) / 2;
		if (!s.exists && !s.used && arriving[i].empty()) continue;
		if (!arriving[i].empty()) {
			if (i % 2) p.Fill(px + 1, rowY, pw - 2, rowH - 2, rowAlt);
			p.Frame(px + 12, y + 7, 24, 22, pal::edge);
			p.Font(0);
			int tx = PaintAvatar(p, px + 48, y + 5, 26, g_flow.arrivingIds[i]) ? px + 82 : px + 48;
			const PatronTier arrivingTier = PatronOf(g_flow.arrivingIds[i]).tier;
			if (arrivingTier != PatronTier::None) tx += 26;
			const UINT rowFmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
			const int arrivingW = (g.host ? pw - 150 : pw - 60) - (tx - px - 48);
			if (arrivingTier != PatronTier::None) DrawPatron(p, tx - 26, y + 7, 22, tx, y, arrivingW, 36, arriving[i], arrivingTier, rowFmt);
			else p.Text(tx, y, arrivingW, 36, arriving[i], kText, rowFmt);
			if (g.host) p.Button(px + pw - 94, y + 6, 74, 24, "Kick", kGKickJoining, i, mx, my, true, false, 0);
			continue;
		}
		if (s.local) p.Fill(px + 1, rowY, pw - 2, rowH - 2, RGB(56, 44, 28));
		else if (i % 2) p.Fill(px + 1, rowY, pw - 2, rowH - 2, rowAlt);
		if (s.used) {
			const bool colorEditable = !load && (s.local || (g.host && s.type == 1)) && !SlotFrozen(g, s);
			p.Fill(px + 12, y + 7, 24, 22, PlayerColor(g, s.color));
			p.Frame(px + 12, y + 7, 24, 22, colorEditable && p.Hover(px + 12, y + 7, 24, 22, mx, my) ? pal::focus : kGold);
			if (colorEditable) p.AddHit(px + 12, y + 7, 24, 22, kGColor, i);
			if (st.dropOpen && st.dropKind == kDropColor && st.dropIndex == i) g_dropAnchor = {px + 12, y + 7, px + 36, y + 29};
		}
		else {
			p.Frame(px + 12, y + 7, 24, 22, RGB(70, 56, 38));
		}
		if (g.host && !s.local && (s.type == 0 || s.type == 2) && load) {
			const bool human = i < static_cast<int>(st.savePlayers.size()) && st.savePlayers[i].used() && !st.savePlayers[i].ai();
			p.Font(3);
			const std::string freeText = human ? Tr("Slot for ") + st.savePlayers[i].name : "Open slot";
			p.Text(px + 48, y, s.type == 0 ? 144 : 400, 36, s.type == 0 ? freeText : "Closed", kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (s.type == 0 && !movingRows) p.Button(px + 196, y + 6, 60, 24, "Take", kGSeat, i, mx, my, true, false, 0);
			continue;
		}
		if (g.host && !s.local && (s.type == 0 || s.type == 2) && g.online && !s.exists) {
			p.Font(3);
			p.Text(px + 48, y, 400, 36, "This map has no more positions", kDim);
			continue;
		}
		if (g.host && !s.local && (s.type == 0 || s.type == 2 || s.type == 1) && !load && (s.type == 1 || s.aiAllowed || g.online)) {
			int sel = 0;
			const std::vector<SlotChoice> choices = SlotChoices(g, s, sel);
			Combo(p, st, px + 44, y + 6, 146, 24, choices[sel].label, kDropSlot, i, choices.size() > 1 && !movingRows && !(g.online && LocalReady(g)));
			p.Font(0);
			if (!movingRows && (s.type == 1 || (s.type == 0 && s.aiAllowed))) p.Button(px + 196, y + 6, 60, 24, "Take", kGSeat, i, mx, my, true, false, 0);
			if (s.type != 1) continue;
		}
		else if (g.host && !s.local && (s.type == 0 || s.type == 2)) {
			p.Font(3);
			p.Text(px + 48, y, 400, 36, "This map has no more positions", kDim);
			continue;
		}
		else if (s.used) {
			const std::string label = SlotLabel(s);
			p.Font(s.local ? 1 : 0);
			const bool kickable = g.host && g.online && s.type == 3 && !s.local;
			const bool human = s.local || (g.online && s.type == 3);
			const uint64_t who = human ? (s.local ? SteamOwnId() : MemberIdByName(st, s.name)) : 0;
			const bool avatar = human && PaintAvatar(p, px + 48, y + 5, 26, who);
			int tx = avatar ? px + 80 : px + 48;
			const PatronTier tier = who ? PatronOf(who).tier : PatronTier::None;
			if (tier != PatronTier::None) tx += 26;
			const bool seat = s.type == 1 && !s.local;
			const bool hostMoves = g.host && s.type == 3;
			const bool asks = !g.host && g.online && s.type == 3 && !s.local;
			const int nameW = kickable && hostMoves ? 76 : kickable ? 128 : (seat || asks || (hostMoves && s.local)) ? 144 : 206;
			const UINT nameFmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
			if (tier != PatronTier::None) DrawPatron(p, tx - 26, y + 7, 22, tx, y, nameW - (tx - px - 48), 36, label, tier, nameFmt);
			else p.Text(tx, y, nameW - (tx - px - 48), 36, label, s.local ? kGold : kText, nameFmt);
			if (kickable && !movingRows) p.Button(px + 180, y + 6, 74, 24, "Kick", kGKick, i, mx, my, true, false, 0);
			if (seat && !movingRows) p.Button(px + 196, y + 6, 60, 24, "Take", kGSeat, i, mx, my, true, false, 0);
		}
		else if (load && s.type == 0) {
			const std::string owner = SaveSlotNameFromOptions(st.room.info.optionsText, i);
			p.Font(3);
			p.Text(px + 48, y, 144, 36, owner.empty() ? "Open slot" : Tr("Slot for ") + owner, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Button(px + 196, y + 6, 60, 24, "Take", kGSeat, i, mx, my, true, false, 0);
		}
		else {
			p.Font(3);
			const bool seat = g.online && s.type == 0 && s.aiAllowed && s.exists;
			p.Text(px + 48, y, seat ? 144 : 400, 36, !s.exists || s.type == 4 ? "This map has no more positions" : s.type == 2 ? "Closed" : "Open slot", kDim);
			if (seat && !movingRows) p.Button(px + 196, y + 6, 60, 24, "Take", kGSeat, i, mx, my, true, false, 0);
		}
		if (!s.used) continue;
		const bool raceEditable = RaceEditable(g, s, load) && !SlotFrozen(g, s);
		const bool hidden = (g.rules & kRuleBlindPick) && !load && !s.local && !(g.host && s.type == 1);
		const std::string race = hidden ? "Hidden"
			: s.race >= 0 && s.race < static_cast<int>(s.raceNames.size()) ? (RaceSeparator(s.raceNames) && s.race == 9 ? "—" : s.raceNames[s.race]) : "?";
		p.Font(0);
		if (raceEditable) {
			Combo(p, st, px + kRaceX, y + 6, kRaceW, 24, race, kDropRace, i, true);
		}
		else {
			p.Text(px + kRaceX, y, kRaceW, 36, race, hidden ? kDim : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		{
			const bool off = BonusesDisabled(g);
			const bool known = s.bonus > 0 && s.bonus < static_cast<int>(g.bonuses.size());
			const std::string bonus = off || s.bonus <= 0 ? "—" : known ? g.bonuses[s.bonus].name : "?";
			p.Font(3);
			if (!load && !off && g.bonuses.size() > 1 && !GameModeLocksOption(GameModeFor(g.mode.c_str()), 0x10a7) && (s.local || (g.host && s.type == 1)) &&
				!SlotFrozen(g, s)) {
				Combo(p, st, px + kBonusX, y + 6, kBonusW, 24, bonus, kDropBonus, i, true);
			}
			else {
				p.Text(px + kBonusX + 8, y, kBonusW - 8, 36, bonus, off || s.bonus <= 0 ? kDim : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
			p.Font(0);
		}
		const std::string team = s.team <= 0 || s.team > 4 ? "—" : std::to_string(s.team);
		if (!load && (s.local || (g.host && s.type == 1)) && !SlotFrozen(g, s)) p.Button(px + kTeamX, y + 6, kTeamW, 24, team, kGTeam, i, mx, my);
		else p.Text(px + kTeamX, y, kTeamW, 36, team, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (s.type == 1) PaintBadge(p, px + kBadgeX, y + 8, kBadgeW, 20, "AI", RGB(60, 52, 80), RGB(210, 200, 240));
		else if (!g.online) PaintBadge(p, px + kBadgeX, y + 8, kBadgeW, 20, "You", RGB(56, 44, 28), kGold);
		else if (s.ready) PaintBadge(p, px + kBadgeX, y + 8, kBadgeW, 20, "Ready", RGB(40, 80, 40), RGB(200, 240, 190));
		else PaintBadge(p, px + kBadgeX, y + 8, kBadgeW, 20, "Waiting", RGB(70, 56, 38), kDim);
	}

	{
		int positions = 0, without = 0;
		for (const GameRoomSlot &s : g.slots) {
			if (s.exists) positions++;
			if (s.used && s.type == 3 && !s.exists) without++;
		}
		if (without) {
			p.Font(3);
			p.Text(px + 120, py + 6, pw - 350, 24, Tr("The map is for ") + std::to_string(positions) + Tr(" players"), kBad, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
		const bool moving = g.host && st.moveFrom >= 0 && st.moveFrom < 8;
		for (int i = 0; i < 8; i++) {
			const GameRoomSlot &s = g.slots[i];
			const int rowY = py + 52 + i * rowH, y = rowY + (rowH - 37) / 2;
			if (!arriving[i].empty()) continue;
			if (s.used && s.type == 3 && !s.exists) PaintBadge(p, px + kBadgeX, y + 8, kBadgeW, 20, "No slot", RGB(90, 36, 30), RGB(255, 200, 190));
			p.Font(3);
			if (moving) {
				if (i == st.moveFrom) {
					p.Frame(px + 2, rowY, pw - 4, rowH - 2, kGold);
					p.Button(px + 196, y + 6, 60, 24, "Cancel", kGMove, i, mx, my, true, false, 3);
				}
				else if (s.exists && s.type != 4 && s.type != 2) {
					p.Button(px + 196, y + 6, 60, 24, "Here", kGMoveTo, i, mx, my, true, true, 3);
				}
				continue;
			}
			if (!s.used || s.type != 3) continue;
			if (g.host) {
				p.Button(s.local ? px + 196 : px + 128, y + 6, s.local ? 60 : 48, 24, "Move", kGMove, i, mx, my, true, false, 3);
			}
			else if (!s.local && g.online) {
				const bool asked = st.swapPendingTo && st.swapPendingName == s.name && GetTickCount() - st.swapPendingTime < 20000;
				p.Button(px + 196, y + 6, 60, 24, asked ? "Asked…" : "Swap", kGSwapAsk, i, mx, my, !asked, false, 3);
			}
		}
	}

	const GameRoomSlot &me = g.slots[g.localSlot & 7];
	const int ox = kPlayersW + 16, ow = kW - ox - 16, oy = py, oh = ph;
	p.Fill(ox, oy, ow, oh, panel);
	p.Frame(ox, oy, ow, oh, line);
	{
		std::map<std::string, std::string> pub;
		for (size_t a = 0; a < st.room.info.optionsText.size();) {
			size_t b = st.room.info.optionsText.find('\n', a);
			if (b == std::string::npos) b = st.room.info.optionsText.size();
			const std::string lineText = st.room.info.optionsText.substr(a, b - a);
			const size_t colon = lineText.find(": ");
			if (colon != std::string::npos) pub[lineText.substr(0, colon)] = lineText.substr(colon + 2);
			a = b + 1;
		}
		const bool host = g.host;
		const bool canEdit = host && !(g.online && LocalReady(g));
		int y = oy + 8;
		p.Font(1);
		p.Text(ox + 12, y, ow - 24, 24, "Game options", kGold);
		y += 30;
		const std::string saveName = g.host ? st.hostSave.name : st.room.info.saveName;
		if (g.online && !saveName.empty()) {
			const int bx = ox + 8, bw = ow - 16;
			p.Fill(bx, y, bw, 50, RGB(44, 34, 20));
			p.Frame(bx, y, bw, 50, pal::edge);
			p.Font(1);
			p.Text(bx + 10, y + 3, bw - 20, 22, Tr("Saved game: ") + saveName, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			std::string sub;
			COLORREF color = kDim;
			if (g.host) {
				std::string list = SavePlayersText(st.savePlayers);
				for (size_t k = 0; (k = list.find(10, k)) != std::string::npos;) list.replace(k, 1, "   ");
				sub = list.empty() ? std::string() : Tr("Slots: ") + list;
				if (!st.saveMapError.empty()) {
					sub = Tr("Map: ") + st.saveMapError + (sub.empty() ? "" : "  ·  " + sub);
					color = kBad;
				}
				else if (st.saveMapStep < 2) {
					sub = Tr("Choosing the game's map...") + (sub.empty() ? "" : "  ·  " + sub);
				}
			}
			else if (st.save.state == 1) {
				const uint64_t total = std::max<uint64_t>(1, st.save.total);
				sub = Tr("Receiving it from the host: ") + std::to_string(std::min<uint64_t>(100, st.save.got * 100 / total)) + " %";
			}
			else if (st.save.state == 2) {
				sub = Tr("Could not receive: ") + st.save.error;
				color = kBad;
			}
			else {
				sub = st.save.ready ? "You have it: it will load when starting." : "Checking...";
			}
			p.Font(3);
			p.Text(bx + 10, y + 26, bw - 20, 20, sub, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			y += 56;
		}
		const int listTop = y, listBottom = oy + oh - 6, listH = listBottom - listTop;
		const int ix = ox + 12, iw = ow - 24 - 10, rowH = 20, valX = ix + 150, valW = ix + iw - valX;
		int scroll;
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			scroll = std::max(0, std::min(st.opt.scroll, g_optGeomOut.maxScroll));
		}
		y = listTop - scroll;
		const int contentTop = y;
		p.Clip(ox + 1, listTop, ow - 2, listH);
		if (!host && st.room.info.optionsText.empty()) {
			p.Font(3);
			p.TextWrapped(ix, y, iw, 60, "The host is choosing the map and options.", kDim);
			y += 60;
		}
		else {
			auto shown = [&](int h) { return y >= listTop - 2 && y + h <= listBottom + 2; };
			auto hoverRow = [&](bool enabled, const char *label, const char *help) {
				const bool hov = shown(rowH) && p.Hover(ix - 4, y, iw + 8, rowH, mx, my) && p.Hover(ox, listTop, ow, listH, mx, my);
				(void)enabled;
				(void)label;
				(void)help;
				return hov;
			};
			auto rowHover = [&](bool enabled) { p.Fill(ix - 4, y, iw + 8, rowH, enabled ? pal::fieldOpen : RGB(36, 29, 20)); };
			auto row = [&](const char *label, const std::string &value, bool special, bool enabled, bool drop, int action, int arg,
						   const char *help) {
				const bool hovAny = hoverRow(enabled, label, help), hov = hovAny && enabled;
				const bool open = drop && st.dropOpen && st.dropKind == (action & 15) && st.dropIndex == arg;
				if (hovAny || open) rowHover(enabled || open);
				p.Fill(ix, y + rowH - 1, iw, 1, RGB(40, 31, 21));
				p.Font(3);
				p.Text(ix, y, valX - ix - 8, rowH, label, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				const int arrowW = drop && enabled ? 14 : 0;
				if (special) p.Font(4);
				p.Text(valX, y, valW - arrowW, rowH, value, !enabled ? (special ? RGB(196, 160, 96) : RGB(150, 140, 120)) : special ? kSpecial : kText,
					DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				if (drop && enabled) p.ArrowDown(ix + iw - 9, y + rowH / 2 - 2, 8, 4, hov || open ? pal::focus : kGold);
				if (enabled && shown(rowH)) {
					if (drop) p.AddHit(ix - 4, y, iw + 8, rowH, kGDropOpen, (action & 15) | (arg << 4));
					else p.AddHit(ix - 4, y, iw + 8, rowH, action, arg);
				}
				if (open) g_dropAnchor = {valX - 60, y, ix + iw, y + rowH};
				y += rowH;
			};
			auto toggleRow = [&](const char *label, bool on, bool enabled, int action, int arg, const char *help) {
				const bool hovAny = hoverRow(enabled, label, help), hov = hovAny && enabled;
				if (hovAny) rowHover(enabled);
				p.Fill(ix, y + rowH - 1, iw, 1, RGB(40, 31, 21));
				const int sw = Painter::kToggleW, sh = Painter::kToggleH, sx = ix + iw - sw, sy = y + (rowH - sh) / 2;
				p.Font(3);
				p.Text(ix, y, sx - ix - 10, rowH, label, on ? (enabled ? kText : RGB(190, 180, 160)) : kDim,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				p.Toggle(sx, sy, on, enabled, hov);
				if (enabled && shown(rowH)) p.AddHit(ix - 4, y, iw + 8, rowH, action, arg);
				y += rowH;
			};
			auto section = [&](const char *title) {
				y += 9;
				p.Font(3);
				p.Text(ix, y, iw, 15, title, pal::section, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				y += 16;
			};
			auto gameValue = [&](int id, const char *published, int &index, bool &enabled) {
				std::string v = "—";
				index = -1;
				enabled = false;
				if (host) {
					for (size_t i = 0; i < g.options.size(); i++) {
						const GameRoomOption &o = g.options[i];
						if (o.id != id) continue;
						index = static_cast<int>(i);
						if (o.checkbox) v = o.value ? "yes" : "no";
						else if (o.sel >= 0 && o.sel < static_cast<int>(o.items.size()) && !o.items[o.sel].empty()) v = o.items[o.sel];
						const bool mapByHand = id == 0x1000 && st.saveMapStep >= 2 && st.saveMap.empty() && !st.saveMapRandom;
						enabled = canEdit && (o.checkbox || o.items.size() > 1) && (!load || mapByHand) && !GameModeLocksOption(GameModeFor(g.mode.c_str()), id);
					}
				}
				else {
					const auto it = pub.find(published);
					if (it != pub.end() && !it->second.empty()) v = it->second;
				}
				return v;
			};
			auto comboRow = [&](int id, const char *label, const char *published, const char *help) {
				int index;
				bool enabled;
				std::string v = gameValue(id, published, index, enabled);
				if (id == 0x1000) {
					const std::string scenario = host ? (g.scenario.empty() ? "" : g.scenario.substr(g.scenario.find_last_of("/\\") + 1))
													  : (pub.count("Scenario") ? pub["Scenario"] : "");
					if (!scenario.empty()) v = Tr("Scenario: ") + scenario;
					const std::string prefix = "Random map: ";
					if (v.compare(0, prefix.size(), prefix) == 0) v = Tr("Random: ") + v.substr(prefix.size());
				}
				row(label, v, false, enabled && index >= 0, true, kDropOption, index < 0 ? 0 : index, help);
			};
			auto checkRow = [&](int id, const char *label, const char *published, bool inverted, const char *help) {
				int index;
				bool enabled;
				const bool checked = gameValue(id, published, index, enabled) == "yes";
				toggleRow(label, checked != inverted, enabled && index >= 0, kGOptToggle, id, help);
			};
			auto ruleRow = [&](unsigned bit, const char *label, bool inLoad, const char *help) {
				const bool set = (g.rules & bit) != 0;
				toggleRow(label, set, canEdit && (!load || inLoad), kGRuleToggle, static_cast<int>(bit), help);
			};
			if (saveName.empty() || !g.online) {
				const int preset = MatchPreset(g.siteFlags, g.rules, [&](int id, const char *key) {
					int index;
					bool enabled;
					return gameValue(id, key, index, enabled);
				});
				row("Preset", preset >= 0 ? kPresets[preset].name : "Custom setup", preset != 0, canEdit, true, kDropPreset, 0,
					"Classic: like the original game. Competitive: 5000 gold, normal population, no bonuses, unexplored map with fog and diplomacy locked. Various: 10000 gold, high population, with bonuses, explored map and no fog.");
			}
			section("MAP");
			comboRow(0x1000, "Map type", "Map type", nullptr);
			comboRow(0x01000008, "Size", "Map size", nullptr);
			if (!load) {
				std::string removed;
				static const struct {
					unsigned bit;
					const char *name;
				} kSites[] = {{kSiteRuins, "Ruins"}, {kSiteCaves, "Caves"}, {kSiteItems, "Items"}, {kSiteStrongholds, "Strongholds"}};
				for (const auto &site : kSites) {
					if (g.siteFlags & site.bit) removed += (removed.empty() ? "" : ", ") + Tr(site.name);
				}
				row("Remove from map", !g.randomMap ? "Only on random maps" : removed.empty() ? "None" : removed, g.randomMap && !removed.empty(),
					canEdit && g.randomMap, true, kDropSites, 0,
					"Neutral sites that are not generated on the random map: ruins, caves, item locations and strongholds (these only appear on large maps).");
				const std::vector<ModOption> &mods = ModOptions();
				for (size_t oi = 0; oi < mods.size(); oi++) {
					const ModOption &mo = mods[oi];
					if (mo.id != kSeasonOptionId) continue;
					const int v = ModOptionGet(mo, g.rules);
					row(mo.name.c_str(), !g.randomMap ? Tr("Only on random maps") : mo.values[v < static_cast<int>(mo.values.size()) ? v : 0], g.randomMap && v != 0,
						canEdit && g.randomMap, true, kDropModOption, static_cast<int>(oi), mo.description.c_str());
				}
			}
			section("GAME");
			comboRow(0x1002, "Mode", "Game type", nullptr);
			comboRow(0x1003, "Limit", "Limit", nullptr);
			comboRow(0x1004, "Population", "Population", nullptr);
			comboRow(0x1005, "Starting gold", "Starting gold", nullptr);
			if (g.online) {
				if (host) {
					static const char *const kSpecNames[] = {"No spectators", "With a 30 s delay", "With a 60 s delay", "With a 120 s delay", "Live"};
					const int sv = std::max(0, std::min(4, st.createSpec));
					row("Spectators", kSpecNames[sv], false, canEdit, true, kDropSpectators, 0,
						"Who can watch the game without playing, and with how much delay. It can be changed while the room is open.");
				}
				else {
					row("Spectators", SpectatorText(st.room.info.specLobby != 0, st.room.info.specDelay), false, false, false, 0, 0,
						"The host decides whether the game can be watched and with how much delay.");
				}
			}
			section("RULES");
			checkRow(0x1007, "Fog of war", "No fog", true, "Whatever your troops can't see stays hidden.");
			checkRow(0x1008, "Explored map", "Map pre-explored", false, "The terrain of the whole map is visible from the start.");
			checkRow(0x100a, "Shared control", "Shared control", false, "Allies can give orders to each other's troops.");
			checkRow(0x10a7, "Bonuses", "Disable bonuses", true, "At the start, each player picks a boost: gold or a level 12 hero.");
			if (!load) ruleRow(kRuleBlindPick, "Hide civilizations", false, "In the room nobody sees the others' civilization, only their own.");
			ruleRow(kRuleFreeAttach, "Free unit attachment", true,
				"Troops of another civilization can be placed under a hero's command (as in Alternative mode).");
			ruleRow(kRuleLockDiplomacy, "Lock diplomacy", true, "Alliances can't be changed during the game.");
			ruleRow(kRuleCheats, "Allow cheats", true, "Players can type /gold, /food or /map in the game chat.");
			{
				const std::vector<ModOption> &mods = ModOptions();
				for (size_t oi = 0; oi < mods.size(); oi++) {
					const ModOption &mo = mods[oi];
					if (mo.id == kSeasonOptionId) continue;
					if (mo.values.empty()) {
						ruleRow(mo.bit, mo.name.c_str(), true, mo.description.c_str());
						continue;
					}
					const int v = ModOptionGet(mo, g.rules);
					row(mo.name.c_str(), mo.values[v < static_cast<int>(mo.values.size()) ? v : 0], v != 0, canEdit, true, kDropModOption, static_cast<int>(oi),
						mo.description.c_str());
				}
			}
			if (g.online) {
				section("CONTENT");
				y = PaintLinesInline(p, OnlineLobbyLines(st, st.room.info, false), ix, y, iw, 20) + 4;
			}

			if (!host && st.map.state == 1) {
				const uint64_t total = std::max<uint64_t>(1, st.map.total);
				const int pct = static_cast<int>(std::min<uint64_t>(100, st.map.got * 100 / total));
				y += 8;
				p.Font(3);
				p.Text(ix, y, iw, 18, Tr("Receiving scenario \"") + st.map.name + "\": " + std::to_string(pct) + " %", kGold,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				p.Fill(ix, y + 20, iw, 8, pal::well);
				p.Fill(ix, y + 20, iw * pct / 100, 8, kGold);
				y += 30;
			}
			else if (!host && st.map.state == 2 && st.map.needsPack) {
				y += 8;
				p.Font(3);
				std::string text = TrFmt("This room's map comes from \"%s\", which you do not have loaded.", {st.map.packName});
				if (!st.map.error.empty()) text += " " + Tr(st.map.error) + ".";
				y += p.TextWrapped(ix, y, iw, 36, text, kBad);
			}
			else if (!host && st.map.state == 2) {
				y += 8;
				p.Font(3);
				y += p.TextWrapped(ix, y, iw, 36, Tr("Couldn't receive the scenario: ") + st.map.error, kBad);
			}
			y += 4;
		}
		p.Unclip();
		const int contentH = y - contentTop, maxScroll = std::max(0, contentH - listH);
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			g_optRect = p.R(ox, listTop, ow, listH);
			OffsetRect(&g_optRect, p.ox, p.oy);
		}
		{
			ListState view = st.opt;
			view.scroll = std::min(scroll, maxScroll);
			ListGeom geom;
			ScrollbarPaint(p, ox + ow - 11, listTop, listH, contentH, listH, view, geom, mx, my);
			g_optGeomOut = geom;
		}
	}

	if (!g.online) {
		const int by = kH - 16 - 52;
		p.Button(16, by, 260, 52, "Back to menu", kGCancel, 0, mx, my);
		p.Button(kW - 16 - 372, by, 372, 52, "Start the game", kGStart, 0, mx, my, true, true);
		return;
	}
	const int cx = 16, cw = kPlayersW, cy = py + ph + 12, ch = kH - 16 - cy;
	p.Fill(cx, cy, cw, ch, pal::well);
	p.Frame(cx, cy, cw, ch, line);
	p.Font(3);
	PaintChatLog(p, st, cx + 8, cy + 8, cw - 16, ch - 8 - 44);
	PaintField(p, st, cx + 8, cy + ch - 36, cw - 112, 28, st.chatInput, "Type a message...",
		st.focus == kChatField, kChatFocus);
	p.Button(cx + cw - 98, cy + ch - 36, 90, 28, "Send", kChatSend, 0, mx, my);

	const bool saveReady = g.host || st.room.info.saveName.empty() || st.save.ready;
	const bool mapReady = st.map.state == 0 && saveReady;
	const int ax = ox + 12, aw = ow - 12;
	const int hostBh = std::min(42, (ch - 3 * 8) / 4), gap = (ch - 4 * hostBh) / 3;
	const int count = g.host ? 4 : 3, bh = (ch - (count - 1) * gap) / count;
	int step = 0;
	auto yAt = [&](int k) { return cy + (ch - bh) * k / (count - 1); };
	int by = yAt(step++);
	if (!g.host && st.map.needsPack) {
		p.Button(ax, by, aw, bh, "Download and rejoin", kGResync, 0, mx, my, true, true);
	}
	else {
		p.Button(ax, by, aw, bh, !saveReady ? "Waiting for the saved game..." : !mapReady ? "Waiting for the scenario..." : me.ready ? "Not ready" : "Ready", kGReady, 0, mx,
			my, mapReady || me.ready, !me.ready && mapReady);
	}
	if (g.host) {
		const bool allReady = humans > 0 && readyCount == humans;
		const StartBlock block = StartBlocker(st, g);
		const bool canStart = allReady && block.toast.empty();
		by = yAt(step++);
		p.Button(ax, by, aw, bh, block.label.empty() ? "Start" : block.label, kGStart, 0, mx, my, canStart, canStart);
	}
	const bool canInvite = !RoomHasNoSeat(st);
	by = yAt(step++);
	p.Button(ax, by, aw, bh, canInvite ? "Invite" : "No free slots to invite", kInvite, 0, mx, my, canInvite);
	by = yAt(step++);
	p.Button(ax, by, aw, bh, "Leave", kGCancel, 0, mx, my);

	if (st.dropAsk) {
		const int w = 520, h = 210, x = (kW - w) / 2, y = 180;
		p.hits->clear();
		p.Fill(x - 2, y - 2, w + 4, h + 4, RGB(10, 8, 6));
		p.Fill(x, y, w, h, RGB(34, 27, 18));
		p.Frame(x, y, w, h, kGold);
		p.Font(1);
		p.Text(x + 20, y + 14, w - 40, 26, Tr("This map is for ") + std::to_string(st.dropPositions) + Tr(" players"), kGold);
		p.Font(0);
		std::string who;
		for (size_t i = 0; i < st.dropNames.size(); i++) who += (i ? (i + 1 == st.dropNames.size() ? Tr(" and ") : ", ") : "") + st.dropNames[i];
		p.Text(x + 20, y + 48, w - 40, 90,
			(st.dropNames.empty() ? std::string("There are more players than positions.") : Tr("There is no room for ") + who + ".") +
				Tr(" If you use this map, they will leave the room (they can come back in if you switch to a larger map)."),
			kText, DT_LEFT | DT_WORDBREAK);
		p.Button(x + 20, y + h - 46, 236, 32, "Remove them and use this map", kDropYes, 0, mx, my, true, false);
		p.Button(x + w - 256, y + h - 46, 236, 32, "Keep the map as it was", kDropNo, 0, mx, my, true, true);
	}
	else if (st.swapAskFrom) {
		const int w = 470, h = 170, x = (kW - w) / 2, y = 190;
		p.hits->clear();
		p.Fill(x - 2, y - 2, w + 4, h + 4, RGB(10, 8, 6));
		p.Fill(x, y, w, h, RGB(34, 27, 18));
		p.Frame(x, y, w, h, kGold);
		p.Font(1);
		p.Text(x + 20, y + 14, w - 40, 26, "Slot swap", kGold);
		p.Font(0);
		int theirs = -1;
		for (int i = 0; i < 8; i++) {
			if (g.slots[i].used && g.slots[i].type == 3 && PlayerNameMatches(g.slots[i].name, st.swapAskName)) theirs = i;
		}
		p.Text(x + 20, y + 46, w - 40, 60,
			(theirs >= 0 ? TrFmt("%s wants to swap slots with you (you would move to slot %d).", {st.swapAskName, std::to_string(theirs + 1)}) : TrFmt("%s wants to swap slots with you.", {st.swapAskName})),
			kText, DT_LEFT | DT_WORDBREAK);
		p.Button(x + w - 300, y + h - 46, 136, 32, "OK", kSwapAccept, 0, mx, my, true, true);
		p.Button(x + w - 156, y + h - 46, 136, 32, "Decline", kSwapDecline, 0, mx, my);
	}
}

void PaintConnecting(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const bool creating = st.autoHost || st.room.owner;
	const bool waiting = !st.connecting && st.room.id && !st.room.info.hosting;
	const Dialog d = DialogBegin(p, kW, kH, 480, 200, creating ? "Creating the game..." : waiting ? "Waiting for the host..." : "Joining the game...");
	PaintBusyBar(p, d.BodyX(), d.y + 72, d.BodyW(), 8, GetTickCount());
	p.Font(3);
	p.Text(d.BodyX(), d.y + 90, d.BodyW(), 24,
		creating ? "Setting up the game room"
		: waiting ? Tr("You'll join as soon as ") + st.room.info.host + Tr(" creates the game")
							: Tr("Connecting to ") + (st.room.info.host.empty() ? std::string("the host") : st.room.info.host) + Tr(" over the Steam network"),
		kDim);
	DialogButtons(p, d, {{"Cancel", kLeave}}, mx, my, true);
}

void PaintSpectate(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const OnlineLobby &g = st.room.info;
	const Dialog d = DialogBegin(p, kW, kH, 520, 188, TrFmt("Watching %s's game", {g.host}));
	p.Font(3);
	p.Text(d.BodyX(), d.y + 56, d.BodyW(), 20, g.name, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const int progress = SpectatorProgress();
	if (progress >= 0) PaintProgress(p, d.BodyX(), d.y + 82, d.BodyW(), 14, progress / 100.0f);
	else PaintBusyBar(p, d.BodyX(), d.y + 86, d.BodyW(), 8, GetTickCount());
	const std::string status = SpectatorStatusText();
	p.Font(0);
	p.Text(d.BodyX(), d.y + 106, d.BodyW(), 26, status.empty() ? std::string("Connecting to the host...") : status, kGold,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DialogButtons(p, d, {{"Leave", kLeave}}, mx, my, true);
}
}
}

namespace mpui_impl {
void PaintScreen(Painter &p, const State &st)
{
	PaintWindowFrame(p, kW, kH);
	p.noHover = st.dropOpen || st.passPrompt || st.syncConfirm || st.syncPhase || st.searching || st.connecting || st.recoverAsk ||
		(st.recoverWaitHost && !st.room.id) || RejoinClientActive() || RejoinClientFailed() || (st.game.active && st.game.picker);
	PaintHeader(p, st);
	if (st.game.active) {
		PaintGameRoom(p, st);
		if (st.game.picker && !SaveMapPicking(st)) {
			PaintScenarioPicker(p, st);
			PaintDropList(p, st);
			return;
		}
		PaintDropList(p, st);
		return;
	}
	if (st.view == kViewRanked) PaintRanked(p, st);
	else if (st.view == kViewCreate) PaintCreate(p, st);

	else PaintList(p, st);
	PaintChat(p, st);
	if (!st.searching && !st.connecting) PaintDropList(p, st);
	if (st.searching) PaintSearch(p, st);
	if (RejoinClientActive() || RejoinClientFailed()) {
		PaintRejoin(p, st);
		return;
	}
	if (st.recoverAsk || (st.recoverWaitHost && !st.room.id)) {
		PaintRecover(p, st);
		return;
	}
	if (st.passPrompt) PaintPassPrompt(p, st);
	else if (st.syncConfirm) PaintSyncConfirm(p, st);
	else if (st.syncPhase) PaintSyncProgress(p, st);
	if (st.room.id && st.room.info.spectate && !st.game.active && !st.syncPhase && !st.syncConfirm) {
		PaintSpectate(p, st);
		return;
	}
	if (st.connecting || (st.room.id && !st.game.active && !st.syncPhase && !st.syncConfirm && !st.gameClosedAt)) PaintConnecting(p, st);
}
}
