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
	std::map<std::string, std::string> pub;
	const std::string &text = st.room.info.optionsText;
	for (size_t a = 0; a < text.size();) {
		size_t b = text.find('\n', a);
		if (b == std::string::npos) b = text.size();
		const std::string lineText = text.substr(a, b - a);
		const size_t colon = lineText.find(": ");
		if (colon != std::string::npos) pub[lineText.substr(0, colon)] = lineText.substr(colon + 2);
		a = b + 1;
	}
	return pub;
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
const int kCellH = 28, kSectionH = 26, kHelpH = 38;

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
		y += 6;
		if (Shown(kSectionH - 6)) {
			p.Font(4);
			p.Text(x, y, w, kSectionH - 8, title, pal::section, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			p.Fill(x, y + kSectionH - 9, w, 1, pal::lineDim);
		}
		y += kSectionH - 6;
	}
	// Hover and the help line of a cell; returns whether the mouse is on it.
	bool Hover(const char *cellHelp)
	{
		const int cx = CellX();
		const bool hov = Shown(kCellH) && p.Hover(cx - 4, y, colW + 8, kCellH, st.mouseX, st.mouseY) && p.Hover(x - 8, top, w + 16, bottom - top, st.mouseX, st.mouseY);
		if (hov) {
			p.Fill(cx - 4, y + 1, colW + 8, kCellH - 2, RGB(38, 30, 20));
			if (cellHelp && *cellHelp) help = cellHelp;
		}
		return hov;
	}
	void Combo(const char *label, const std::string &value, bool special, bool enabled, int kind, int index, const char *cellHelp)
	{
		Hover(cellHelp);
		const int cx = CellX(), boxW = std::min(178, colW / 2 + 24), boxX = cx + colW - boxW, cy = y + (kCellH - 24) / 2;
		p.Font(3);
		p.Text(cx, y, boxX - cx - 8, kCellH, label, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (Shown(kCellH)) {
			if (enabled) {
				mpui_impl::Combo(p, st, boxX, cy, boxW, 24, value, kind, index, true);
				if (special) p.Fill(boxX + 1, cy + 22, boxW - 26, 1, kSpecial);
			}
			else {
				p.Frame(boxX, cy, boxW, 24, pal::lineDim);
				p.Font(special ? 4 : 0);
				p.Text(boxX + 8, cy, boxW - 14, 24, value, special ? kSpecial : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
		}
		Next();
	}
	void Toggle(const char *label, bool on, bool enabled, int action, int arg, const char *cellHelp)
	{
		const bool hov = Hover(cellHelp) && enabled;
		const int cx = CellX(), tx = cx + colW - kBigToggleW, ty = y + (kCellH - kBigToggleH) / 2;
		p.Font(3);
		p.Text(cx, y, tx - cx - 8, kCellH, label, on ? kText : kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (Shown(kCellH)) {
			PaintBigToggle(p, tx, ty, on, enabled, hov);
			if (enabled) p.AddHit(cx - 4, y, colW + 8, kCellH, action, arg);
		}
		Next();
	}
};

std::string SpectatorChoice(const State &st)
{
	static const char *const kSpecNames[] = {"No spectators", "With a 30 s delay", "With a 60 s delay", "With a 120 s delay", "Live"};
	if (st.game.host) return kSpecNames[std::max(0, std::min(4, st.createSpec))];
	return SpectatorText(st.room.info.specLobby != 0, st.room.info.specDelay);
}

void PaintRoomName(Painter &p, const State &st, int x, int y, int w)
{
	const GameRoomSnapshot &g = st.game;
	p.Font(4);
	p.Text(x, y, 130, 28, "Room name", pal::section, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	const int fx = x + 136, fw = std::min(460, w - 136);
	if (g.host) {
		const bool focused = st.focus == kRoomNameField;
		PaintField(p, st, fx, y, fw, 28, focused ? st.roomNameInput : st.room.info.name, "", focused, kFieldRoomName);
		if (focused) {
			p.Font(3);
			p.Text(fx + fw + 12, y, x + w - fx - fw - 12, 28, "Press Enter to rename the room", kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	else {
		p.Font(0);
		p.Text(fx, y, fw, 28, st.room.info.name, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintMapCard(Painter &p, const State &st, int x, int y, int w, int h)
{
	const GameRoomSnapshot &g = st.game;
	PaintCard(p, x, y, w, h);
	const int pad = 12, size = w - 2 * pad;
	PaintMapThumb(p, st, x + pad, y + pad, size, true);
	int ty = y + pad + size + 10;
	const int tx = x + pad, tw = size;
	p.Font(1);
	const std::string map = RoomMapLabel(st);
	p.Text(tx, ty, tw, 22, map.empty() ? std::string("—") : map, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	ty += 26;
	p.Font(3);
	auto line = [&](const std::string &label, const std::string &value, COLORREF color) {
		if (ty + 18 > y + h - 8) return;
		p.Text(tx, ty, tw, 18, label, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Text(tx, ty, tw, 18, value.empty() ? std::string("—") : value, color, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Fill(tx, ty + 18, tw, 1, RGB(42, 33, 22));
		ty += 20;
	};
	const std::shared_ptr<const MapPreviewData> d = g.scenario.empty() ? nullptr : MapPreviewGet(g.scenario);
	if (d && !d->failed) {
		if (d->humans > 0) line(Tr("Players"), std::to_string(d->humans), kText);
		if (d->info.units > 0) line(Tr("Size"), Tr(MapSizeName(d->info.units)), kText);
		if (d->origin.kind == kMapMod) line(Tr("From: "), d->origin.modName, kGold);
	}
	else {
		line(Tr("Size"), RoomOptionText(st, 0x01000008, "Map size"), kText);
	}
	int positions = 0;
	for (const GameRoomSlot &s : g.slots) positions += s.exists ? 1 : 0;
	if (positions < 8) line(Tr("Positions"), std::to_string(positions), kText);
	COLORREF color = kDim;
	const std::string notice = RoomNotice(st, color);
	if (!notice.empty() && ty + 20 < y + h - 8) {
		ty += 6;
		p.Clip(tx, ty, tw, y + h - 8 - ty);
		p.TextWrapped(tx, ty, tw, y + h - 8 - ty, notice, color);
		p.Unclip();
	}
}

void PaintOptionsCard(Painter &p, const State &st, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const bool load = g.online && (g.host ? !st.hostSave.name.empty() : !st.room.info.saveName.empty());
	const bool host = g.host;
	const bool canEdit = host && !(g.online && LocalReady(g));
	const std::string saveName = g.host ? st.hostSave.name : st.room.info.saveName;
	PaintCard(p, x, y, w, h);
	const int listTop = y + 4, listBottom = y + h - kHelpH, listH = listBottom - listTop;
	const int gx = x + 16, gw = w - 32 - 10;
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
		p.TextWrapped(gx, grid.y + 12, gw, 60, "The host is choosing the map and options.", kDim);
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
				"Classic: like the original game. Competitive: 5000 gold, normal population, no bonuses, unexplored map with fog and diplomacy locked. Various: 10000 gold, high population, with bonuses, explored map and no fog.");
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
		checkRow(0x1007, "Fog of war", "No fog", true, "Whatever your troops can't see stays hidden.");
		checkRow(0x1008, "Explored map", "Map pre-explored", false, "The terrain of the whole map is visible from the start.");
		checkRow(0x100a, "Shared control", "Shared control", false, "Allies can give orders to each other's troops.");
		checkRow(0x10a7, "Bonuses", "Disable bonuses", true, "At the start, each player picks a boost: gold or a level 12 hero.");
		if (!load) ruleRow(kRuleBlindPick, "Hide civilizations", false, "In the room nobody sees the others' civilization, only their own.");
		ruleRow(kRuleFreeAttach, "Free unit attachment", true, "Troops of another civilization can be placed under a hero's command (as in Alternative mode).");
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
				grid.Combo(mo.name.c_str(), mo.values[v < static_cast<int>(mo.values.size()) ? v : 0], v != 0, canEdit, kDropModOption, static_cast<int>(oi),
					mo.description.c_str());
			}
		}
		if (g.online && st.room.id) {
			grid.Section("CONTENT");
			grid.y = PaintLinesInline(p, OnlineLobbyLines(st, st.room.info, false), gx, grid.y, gw, 20) + 4;
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
		ScrollbarPaint(p, x + w - 11, listTop, listH, contentH, listH, view, geom, mx, my);
		g_optGeomOut = geom;
	}
	p.Fill(x + 12, listBottom, w - 24, 1, pal::lineDim);
	p.Font(3);
	const std::string help = grid.help ? std::string(grid.help) : !host && g.online ? std::string("Only the host can change the options.") : std::string();
	p.Clip(x + 12, listBottom + 2, w - 24, kHelpH - 4);
	p.TextWrapped(x + 16, listBottom + 4, w - 32, kHelpH - 6, help, kDim);
	p.Unclip();
}
}

void PaintGameTab(Painter &p, const State &st, int top)
{
	const GameRoomSnapshot &g = st.game;
	int y = top;
	if (g.online && st.room.id) {
		PaintRoomName(p, st, 24, y + 2, kW - 48);
		y += 38;
	}
	const int bottom = kH - 16, mapW = 300;
	PaintMapCard(p, st, 16, y, mapW, bottom - y);
	PaintOptionsCard(p, st, 16 + mapW + 12, y, kW - 32 - mapW - 12, bottom - y);
}
}
