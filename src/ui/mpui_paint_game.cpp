// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "game/gamemodes.h"
#include "game/mappreview.h"
#include "mods/modoptions.h"
#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
std::map<std::string, std::string> PublishedOptions(const State &st)
{
	return ParsePublishedOptions(st.room.info.optionsText);
}

std::string RoomOptionText(const State &st, int id, const char *published)
{
	const GameRoomSnapshot &g = st.game;
	if (g.host || !g.online) {
		for (const GameRoomOption &o : g.options) {
			if (o.id != id) continue;
			if (o.checkbox) return o.value ? "yes" : "no";
			if (o.sel >= 0 && o.sel < static_cast<int>(o.items.size()) && !o.items[o.sel].empty()) return o.items[o.sel];
		}
		return "";
	}
	const std::map<std::string, std::string> pub = PublishedOptions(st);
	const auto it = pub.find(published);
	return it == pub.end() ? std::string() : it->second;
}

std::string RoomMapLabel(const State &st)
{
	const GameRoomSnapshot &g = st.game;
	if (!g.host && g.online) return st.room.id ? LobbyMapLabel(st.room.info) : std::string();
	if (!g.scenario.empty()) {
		std::string name = g.scenario.substr(g.scenario.find_last_of("/\\") + 1);
		if (name.size() > 5 && _stricmp(name.c_str() + name.size() - 5, ".bfhp") == 0) name.resize(name.size() - 5);
		return name;
	}
	return MapTypeLabel(RoomOptionText(st, 0x1000, "Map type"));
}
}

namespace mpui_impl {
namespace {
const int kCellH = 40, kSectionH = 36, kHelpH = 56, kBoxH = 30;

struct OptionGrid {
	Painter &p;
	const State &st;
	int x, w, top, bottom;
	int colW, y;
	int col = 0;
	const char *help = nullptr;

	OptionGrid(Painter &painter, const State &state, int gx, int gw, int gtop, int gbottom, int startY)
		: p(painter), st(state), x(gx), w(gw), top(gtop), bottom(gbottom), colW((gw - 20) / 2), y(startY)
	{
	}

	bool Shown(int h) const { return y >= top - 2 && y + h <= bottom + 2; }
	int CellX() const { return x + col * (colW + 20); }
	void Next()
	{
		if (++col == 2) {
			col = 0;
			y += kCellH;
		}
	}
	void EndRow()
	{
		if (col) {
			col = 0;
			y += kCellH;
		}
	}
	void Section(const char *title)
	{
		EndRow();
		y += 10;
		if (Shown(kSectionH - 10)) {
			p.Font(4);
			p.TextCaps(x, y, w, kSectionH - 14, title, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
			p.Fill(x, y + kSectionH - 13, w, 1, pal::hairline);
		}
		y += kSectionH - 10;
	}
	// Hover and the help line of a cell; returns whether the mouse is on it.
	bool Hover(const char *cellHelp)
	{
		const int cx = CellX();
		const bool hov = Shown(kCellH) && p.Hover(cx - 4, y, colW + 8, kCellH, st.mouseX, st.mouseY) && p.Hover(x - 8, top, w + 16, bottom - top, st.mouseX, st.mouseY);
		if (hov) {
			p.Fill(cx - 4, y + 1, colW + 8, kCellH - 2, pal::selected);
			if (cellHelp && *cellHelp) help = cellHelp;
		}
		return hov;
	}
	void Combo(const char *label, const std::string &value, bool special, bool enabled, int kind, int index, const char *cellHelp)
	{
		Hover(cellHelp);
		const int cx = CellX(), boxW = std::min(240, colW / 2 + 24), boxX = cx + colW - boxW, cy = y + (kCellH - kBoxH) / 2;
		p.Font(3);
		p.Text(cx, y, boxX - cx - 8, kCellH, label, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (Shown(kCellH)) {
			if (enabled) {
				mpui_impl::Combo(p, st, boxX, cy, boxW, kBoxH, value, kind, index, true);
				if (special) p.Fill(boxX + 1, cy + kBoxH - 2, boxW - 26, 1, kSpecial);
			}
			else {
				p.Frame(boxX, cy, boxW, kBoxH, pal::hairline);
				p.Font(special ? 1 : 0);
				p.Text(boxX + 8, cy, boxW - 14, kBoxH, value, special ? kSpecial : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
		}
		Next();
	}
	void Toggle(const char *label, bool on, bool enabled, int action, int arg, const char *cellHelp)
	{
		const bool hov = Hover(cellHelp) && enabled;
		const int cx = CellX(), tx = cx + colW - kBigToggleW, ty = y + (kCellH - kBigToggleH) / 2;
		p.Font(3);
		p.Text(cx, y, tx - cx - 8, kCellH, label, on ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (Shown(kCellH)) {
			PaintBigToggle(p, tx, ty, on, enabled, hov);
			if (enabled) p.AddHit(cx - 4, y, colW + 8, kCellH, action, arg);
		}
		Next();
	}
};

std::string SpectatorChoice(const State &st)
{
	static const char *const kSpecNames[] = {"No spectators", "With a 30 s delay", "With a 60 s delay", "With a 120 s delay", "Live", "With a 300 s delay"};
	if (st.game.host) return kSpecNames[std::max(0, std::min(5, st.createSpec))];
	return SpectatorText(st.room.info.specLobby != 0, st.room.info.specDelay);
}

void PaintRoomName(Painter &p, const State &st, int x, int y, int w)
{
	const GameRoomSnapshot &g = st.game;
	p.Font(4);
	p.TextCaps(x, y, 160, 38, "Room name", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	const int fx = x + 168, fw = std::min(560, w - 168);
	if (g.host && !RankedRoomLocked(st)) {
		const bool focused = st.focus == kRoomNameField;
		PaintField(p, st, fx, y, fw, 38, focused ? st.roomNameInput : st.room.info.name, "", focused, kFieldRoomName);
		if (focused) {
			p.Font(3);
			p.Text(fx + fw + 16, y, x + w - fx - fw - 16, 38, "Press Enter to rename the room", pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	else {
		p.Font(0);
		p.Text(fx, y, fw, 38, st.room.info.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintMapCard(Painter &p, const State &st, int x, int y, int w, int h)
{
	const GameRoomSnapshot &g = st.game;
	PaintGlassCard(p, x, y, w, h);
	const int pad = 20, size = std::min(w - 2 * pad, h / 2);
	PaintMapThumb(p, st, x + (w - size) / 2, y + pad, size, true);
	int ty = y + pad + size + 16;
	const int tx = x + pad, tw = w - 2 * pad;
	p.Font(1);
	const std::string map = RoomMapLabel(st);
	p.Text(tx, ty, tw, 26, map.empty() ? std::string("—") : map, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	ty += 34;
	p.Font(3);
	auto line = [&](const std::string &label, const std::string &value, COLORREF color) {
		if (ty + 26 > y + h - pad) return;
		p.Text(tx, ty, tw, 26, label, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Text(tx, ty, tw, 26, value.empty() ? std::string("—") : value, color, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Fill(tx, ty + 26, tw, 1, pal::hairline);
		ty += 30;
	};
	const std::shared_ptr<const MapPreviewData> d = g.scenario.empty() ? nullptr : MapPreviewGet(g.scenario);
	if (d && !d->failed) {
		if (d->humans > 0) line(Tr("Players"), std::to_string(d->humans), pal::text);
		if (d->info.units > 0) line(Tr("Size"), Tr(MapSizeName(d->info.units)), pal::text);
		if (d->origin.kind == kMapMod) line(Tr("From: "), d->origin.modName, pal::redText);
	}
	else {
		line(Tr("Size"), RoomOptionText(st, 0x01000008, "Map size"), pal::text);
	}
	int positions = 0;
	for (const GameRoomSlot &s : g.slots) positions += s.exists ? 1 : 0;
	if (positions < 8) line(Tr("Positions"), std::to_string(positions), pal::text);
	COLORREF color = pal::textDim;
	const std::string notice = RoomNotice(st, color);
	// Each seat of the scenario as the map chooser shows it, in the colour of its marker and with whoever sits in it. The room's
	// notice (a save being received, a map that failed) keeps two lines under it.
	const int seatsBottom = y + h - pad - (notice.empty() ? 0 : 50);
	if (d && !d->failed && !d->info.players.empty() && ty + 28 < seatsBottom) {
		auto seated = [&g](const MapPlayer &pl) { return pl.human && pl.slot >= 0 && pl.slot < 8 && g.slots[pl.slot].used; };
		ty = PaintScenarioPlayers(p, *d, tx, ty + 6, tw, seatsBottom, 1,
				 [&](const MapPlayer &pl) { return seated(pl) ? PlayerColor(g, g.slots[pl.slot].color) : RGB((pl.color >> 16) & 255, (pl.color >> 8) & 255, pl.color & 255); },
				 [&](const MapPlayer &pl) { return seated(pl) ? g.slots[pl.slot].name : std::string(); }) +
			4;
	}
	if (!notice.empty() && ty + 24 < y + h - pad) {
		ty += 8;
		p.Clip(tx, ty, tw, y + h - pad - ty);
		p.TextWrapped(tx, ty, tw, y + h - pad - ty, notice, color);
		p.Unclip();
	}
}

void PaintOptionsCard(Painter &p, const State &st, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const bool load = g.online && (g.host ? !st.hostSave.name.empty() : !st.room.info.saveName.empty());
	const bool host = g.host;
	const bool canEdit = host && !(g.online && LocalReady(g)) && !RankedRoomLocked(st);
	const std::string saveName = g.host ? st.hostSave.name : st.room.info.saveName;
	PaintGlassCard(p, x, y, w, h);
	const int listTop = y + 8, listBottom = y + h - kHelpH, listH = listBottom - listTop;
	const int gx = x + 28, gw = w - 56 - 10;
	int scroll;
	{
		std::lock_guard<std::mutex> lock(g_chat.mx);
		scroll = std::max(0, std::min(st.opt.scroll, g_optGeomOut.maxScroll));
	}
	OptionGrid grid(p, st, gx, gw, listTop, listBottom, listTop - scroll);
	const int contentTop = grid.y;
	p.Clip(x + 1, listTop, w - 2, listH);
	if (!host && g.online && st.room.info.optionsText.empty()) {
		p.Font(3);
		p.TextWrapped(gx, grid.y + 16, gw, 60, "The host is choosing the map and options.", pal::textDim);
		grid.y += 72;
	}
	else {
		const std::map<std::string, std::string> pub = PublishedOptions(st);
		auto gameValue = [&](int id, const char *published, int &index, bool &enabled) {
			std::string v = "—";
			index = -1;
			enabled = false;
			if (host || !g.online) {
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
		auto comboRow = [&](int id, const char *label, const char *published) {
			int index;
			bool enabled;
			std::string v = gameValue(id, published, index, enabled);
			if (id == 0x1000) {
				const auto scenario = pub.find("Scenario");
				const std::string name = host || !g.online ? (g.scenario.empty() ? "" : g.scenario.substr(g.scenario.find_last_of("/\\") + 1))
														  : (scenario != pub.end() ? scenario->second : "");
				v = name.empty() ? MapTypeLabel(v) : Tr("Scenario: ") + name;
			}
			grid.Combo(label, v, false, enabled && index >= 0, kDropOption, index < 0 ? 0 : index, nullptr);
		};
		auto checkRow = [&](int id, const char *label, const char *published, bool inverted, const char *help) {
			int index;
			bool enabled;
			const bool checked = gameValue(id, published, index, enabled) == "yes";
			grid.Toggle(label, checked != inverted, enabled && index >= 0, kGOptToggle, id, help);
		};
		auto ruleRow = [&](unsigned bit, const char *label, bool inLoad, const char *help) {
			grid.Toggle(label, (g.rules & bit) != 0, canEdit && (!load || inLoad), kGRuleToggle, static_cast<int>(bit), help);
		};
		grid.Section("MAP");
		if (saveName.empty() || !g.online) {
			const int preset = MatchPreset(g.siteFlags, g.rules, [&](int id, const char *key) {
				int index;
				bool enabled;
				return gameValue(id, key, index, enabled);
			});
			grid.Combo("Preset", preset >= 0 ? kPresets[preset].name : "Custom setup", preset != 0, canEdit, kDropPreset, 0,
				"Classic: like the original game. Competitive: 5000 gold, normal population, no bonuses, unexplored map with fog and diplomacy locked. Various: 10000 gold, high population, with bonuses, explored map and no fog. Competitive and Various also turn on shared control.");
		}
		comboRow(0x1000, "Map type", "Map type");
		comboRow(0x01000008, "Size", "Map size");
		if (!load) {
			std::string removed;
			static const struct {
				unsigned bit;
				const char *name;
			} kSites[] = {{kSiteRuins, "Ruins"}, {kSiteCaves, "Caves"}, {kSiteItems, "Items"}, {kSiteStrongholds, "Strongholds"}};
			for (const auto &site : kSites) {
				if (g.siteFlags & site.bit) removed += (removed.empty() ? "" : ", ") + Tr(site.name);
			}
			grid.Combo("Remove from map", !g.randomMap ? "Only on random maps" : removed.empty() ? "None" : removed, g.randomMap && !removed.empty(),
				canEdit && g.randomMap, kDropSites, 0,
				"Neutral sites that are not generated on the random map: ruins, caves, item locations and strongholds (these only appear on large maps).");
			const std::vector<ModOption> &mods = ModOptions();
			for (size_t oi = 0; oi < mods.size(); oi++) {
				const ModOption &mo = mods[oi];
				if (mo.id != kSeasonOptionId) continue;
				const int v = ModOptionGet(mo, g.rules);
				grid.Combo(mo.name.c_str(), !g.randomMap ? Tr("Only on random maps") : mo.values[v < static_cast<int>(mo.values.size()) ? v : 0], g.randomMap && v != 0,
					canEdit && g.randomMap, kDropModOption, static_cast<int>(oi), mo.description.c_str());
			}
		}
		grid.Section("GAME");
		comboRow(0x1002, "Mode", "Game type");
		comboRow(0x1003, "Limit", "Limit");
		comboRow(0x1004, "Population", "Population");
		comboRow(0x1005, "Starting gold", "Starting gold");
		if (g.online) {
			grid.Combo("Spectators", SpectatorChoice(st), false, host && canEdit, kDropSpectators, 0,
				host ? "Who can watch the game without playing, and with how much delay. It can be changed while the room is open."
					 : "The host decides whether the game can be watched and with how much delay.");
		}
		grid.Section("RULES");
		checkRow(0x1007, "No fog of war", "No fog", false, "You see everything on the map, also where your troops are not.");
		checkRow(0x1008, "Explored map", "Map pre-explored", false, "The terrain of the whole map is visible from the start.");
		checkRow(0x100a, "Shared control", "Shared control", false, "Allies can give orders to each other's troops.");
		checkRow(0x10a7, "Bonuses", "Disable bonuses", true, "At the start, each player picks a boost: gold or a level 12 hero.");
		if (!load) ruleRow(kRuleBlindPick, "Hide civilizations", false, "In the room nobody sees the others' civilization, only their own.");
		ruleRow(kRuleFreeAttach, "Free unit attachment", true, "Troops of another civilization can be placed under a hero's command (as in Alternative mode).");
		ruleRow(kRuleLockDiplomacy, "Lock diplomacy", true, "The ceasefire can't be changed during the game.");
		ruleRow(kRuleCheats, "Allow cheats", true, "Players can open the cheats panel during the game.");
		// Stored the other way round, so that rooms and saves of older builds allow them.
		grid.Toggle("Respawn timers", (g.rules & kRuleNoTimers) == 0, canEdit, kGRuleToggle, static_cast<int>(kRuleNoTimers),
			"Each player may see on the big map when the mushrooms, healing water and ruins they have seen have their item again.");
		if (!load && (!g.online || st.room.id)) {
			grid.Toggle("Team colors", TeamColorsOn(st, g), canEdit, kGTeamColors, 0,
				"Each team gets a family of colors: team 1 reds, team 2 blues, team 3 greens and team 4 yellows. While it is on, the colors follow the teams.");
		}
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
				grid.Combo(mo.name.c_str(), mo.values[v < static_cast<int>(mo.values.size()) ? v : 0], v != 0, canEdit, kDropModOption, static_cast<int>(oi),
					mo.description.c_str());
			}
		}
		if (g.online && st.room.id) {
			grid.Section("CONTENT");
			grid.y = PaintLinesInline(p, OnlineLobbyLines(st, st.room.info, false), gx, grid.y, gw, 26) + 4;
		}
		grid.EndRow();
	}
	grid.y += 6;
	p.Unclip();
	const int contentH = grid.y - contentTop, maxScroll = std::max(0, contentH - listH);
	{
		std::lock_guard<std::mutex> lock(g_chat.mx);
		g_optRect = p.R(x, listTop, w, listH);
		OffsetRect(&g_optRect, p.ox, p.oy);
	}
	{
		ListState view = st.opt;
		view.scroll = std::min(scroll, maxScroll);
		ListGeom geom;
		ScrollbarPaint(p, x + w - 14, listTop, listH, contentH, listH, view, geom, mx, my);
		g_optGeomOut = geom;
	}
	p.Fill(x + 20, listBottom, w - 40, 1, pal::hairline);
	p.Font(3);
	const std::string help = grid.help ? std::string(grid.help) : !host && g.online ? std::string("Only the host can change the options.") : std::string();
	p.Clip(x + 20, listBottom + 2, w - 40, kHelpH - 4);
	p.TextWrapped(x + 28, listBottom + 8, w - 56, kHelpH - 10, help, pal::textDim);
	p.Unclip();
}
}

void PaintGameTab(Painter &p, const State &st, int x, int y, int w, int h)
{
	const GameRoomSnapshot &g = st.game;
	const int bottom = y + h, gap = 24;
	if (g.online && st.room.id) {
		PaintRoomName(p, st, x, y, w);
		y += 38 + 20;
	}
	const int mapW = std::max(300, std::min(440, w / 4));
	PaintMapCard(p, st, x, y, mapW, bottom - y);
	PaintOptionsCard(p, st, x + mapW + gap, y, w - mapW - gap, bottom - y);
}
}
