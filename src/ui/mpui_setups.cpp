// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>

#include "ui/mpui_internal.h"
#include "core/json.h"
#include "core/log.h"
#include "core/winpath.h"
#include "game/gamemodes.h"
#include "i18n/tr.h"
#include "ui/modui.h"

namespace mpui_impl {
namespace {
std::string SetupsPath()
{
	return winpath::GameDir() + "\\imperivm-setups.json";
}

// The map type's list: its first item opens the game's scenario selector, the others are the random maps.
constexpr int kMapTypeOption = 0x1000;

// Shared supplies are always on (the room's tick turns them on): not part of a setup.
bool SetupKeeps(int id)
{
	return id != 0x1009;
}

const GameRoomOption *FindOption(const GameRoomSnapshot &g, int id)
{
	for (const GameRoomOption &o : g.options) {
		if (o.id == id) return &o;
	}
	return nullptr;
}

// The item of a list the setup wants: by the league's id of the item, by its text, then by its place. -1 when the list does not have it
// or the game has it disabled.
int WantedItem(const GameRoomOption &o, const RoomSetupOption &so)
{
	const int n = static_cast<int>(o.items.size());
	int found = -1;
	for (int i = 0; i < n && found < 0 && !so.key.empty(); i++) {
		if (i < static_cast<int>(o.keys.size()) && o.keys[i] == so.key) found = i;
	}
	for (int i = 0; i < n && found < 0; i++) {
		if (o.items[i] == so.text) found = i;
	}
	// The same list in another language.
	if (found < 0 && so.sel >= 0 && so.sel < n) found = so.sel;
	if (found >= 0 && found < static_cast<int>(o.itemDisabled.size()) && o.itemDisabled[found]) return -1;
	return found;
}

// An option the room cannot take as the setup has it counts as the same.
bool OptionMatches(const GameRoomOption &o, const RoomSetupOption &so)
{
	if (o.checkbox != so.checkbox) return true;
	if (o.checkbox) return o.value == so.value;
	const int want = WantedItem(o, so);
	return want < 0 || want == o.sel;
}

// A map's path as the selector's are compared (as StepSaveMap does).
std::string MapKey(std::string p)
{
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (p.size() > 5 && p.compare(p.size() - 5, 5, ".bfhp") == 0) p.resize(p.size() - 5);
	return p;
}

std::string FileOf(const std::string &path)
{
	return path.substr(path.find_last_of("/\\") + 1);
}

// The setup's map in the room, one step at a time as a player picks it: true once it is there, or cannot be.
bool StepSetupMap(const GameRoomSnapshot &g, SetupApply &a)
{
	const RoomSetup &s = a.setup;
	const GameRoomOption *type = FindOption(g, kMapTypeOption);
	if (!type || type->checkbox || type->items.size() < 2 || (!s.random && s.scenario.empty())) return a.mapDone = true;
	if (s.random) {
		if (g.picker) {
			GameRoomPickerCancel();
			return false;
		}
		int want = -1;
		for (const RoomSetupOption &so : s.options) {
			if (so.id == kMapTypeOption) want = WantedItem(*type, so);
		}
		if (want <= 0) want = type->sel > 0 ? type->sel : 1;
		if ((type->sel == want && g.scenario.empty()) || a.mapTries >= 4) return a.mapDone = true;
		a.mapTries++;
		GameRoomSetOption(kMapTypeOption, want);
		return false;
	}
	const std::string want = MapKey(s.scenario);
	if (!g.picker && !g.scenario.empty() && MapKey(g.scenario) == want) return a.mapDone = true;
	auto missing = [&] {
		if (g.picker) GameRoomPickerCancel();
		ModUiToast(TrFmt("You don't have the scenario \"%s\"", {FileOf(s.scenario)}), true);
		LogW("room", "setup \"%s\": the scenario %s is not in the selector", s.name.c_str(), s.scenario.c_str());
		return a.mapDone = true;
	};
	if (a.mapTries >= 8) return missing();
	if (g.picker) {
		if (a.pick >= 0) {
			GameRoomPickerAccept();
			a.pick = -1;
			a.mapTries++;
			return false;
		}
		int idx = -1;
		for (size_t i = 0; i < g.pickerPaths.size() && idx < 0; i++) {
			if (MapKey(g.pickerPaths[i]) == want) idx = static_cast<int>(i);
		}
		const std::string file = FileOf(want);
		for (size_t i = 0; i < g.pickerPaths.size() && idx < 0; i++) {
			if (FileOf(MapKey(g.pickerPaths[i])) == file) idx = static_cast<int>(i);
		}
		// The game's selector holds no map until one is offered to it.
		if (idx < 0 && a.offered != want) {
			GameRoomPickerOffer(s.scenario, false);
			a.offered = want;
			return false;
		}
		if (idx < 0 || idx >= static_cast<int>(g.pickerItems.size())) return missing();
		GameRoomPickerSelect(idx);
		a.pick = idx;
		return false;
	}
	// The first item opens the selector; when it is already the chosen one, a random map first.
	a.mapTries++;
	GameRoomSetOption(kMapTypeOption, type->sel == 0 ? 1 : 0);
	return false;
}

Json SetupJson(const RoomSetup &s)
{
	Json j = Json::MakeObject();
	j.Set("name", Json::MakeString(s.name));
	j.Set("random", Json::MakeBool(s.random));
	if (!s.scenario.empty()) j.Set("scenario", Json::MakeString(s.scenario));
	j.Set("rules", Json::MakeNumber(s.rules));
	j.Set("sites", Json::MakeNumber(s.sites));
	j.Set("speed", Json::MakeNumber(s.speed));
	j.Set("teamColors", Json::MakeBool(s.teamColors));
	Json opts = Json::MakeArray();
	for (const RoomSetupOption &so : s.options) {
		Json o = Json::MakeObject();
		o.Set("id", Json::MakeNumber(so.id));
		if (so.checkbox) o.Set("on", Json::MakeBool(so.value));
		else {
			o.Set("text", Json::MakeString(so.text));
			o.Set("sel", Json::MakeNumber(so.sel));
			if (!so.key.empty()) o.Set("key", Json::MakeString(so.key));
		}
		opts.arr.push_back(o);
	}
	j.Set("options", opts);
	return j;
}

void WriteSetups(const std::vector<RoomSetup> &list)
{
	Json arr = Json::MakeArray();
	for (const RoomSetup &s : list) arr.arr.push_back(SetupJson(s));
	Json root = Json::MakeObject();
	root.Set("setups", arr);
	if (!WriteJsonFile(SetupsPath(), root)) LogW("room", "could not write %s", SetupsPath().c_str());
}
}
}

using namespace mpui_impl;

std::vector<RoomSetup> ReadRoomSetups()
{
	std::vector<RoomSetup> out;
	Json root;
	if (!ReadJsonFile(SetupsPath(), root)) return out;
	const Json *arr = root.Get("setups");
	if (!arr || arr->type != Json::Array) return out;
	for (const Json &j : arr->arr) {
		RoomSetup s;
		s.name = j.GetString("name");
		if (s.name.empty()) continue;
		s.random = j.GetBool("random");
		s.scenario = j.GetString("scenario");
		s.rules = static_cast<unsigned>(j.GetInt64("rules"));
		s.sites = static_cast<unsigned>(j.GetInt64("sites"));
		const int speed = j.GetInt("speed", kRoomSpeedProfile);
		s.speed = speed == -1 || (speed >= 1 && speed <= 100) ? speed : kRoomSpeedProfile;
		s.teamColors = j.GetBool("teamColors");
		const Json *opts = j.Get("options");
		for (size_t i = 0; opts && opts->type == Json::Array && i < opts->arr.size(); i++) {
			const Json &o = opts->arr[i];
			RoomSetupOption so;
			so.id = static_cast<int>(o.GetInt64("id"));
			const int on = o.GetFlag("on");
			so.checkbox = on >= 0;
			so.value = on == 1;
			so.text = o.GetString("text");
			so.key = o.GetString("key");
			so.sel = o.GetInt("sel", -1);
			if (so.id) s.options.push_back(so);
		}
		out.push_back(s);
	}
	return out;
}

std::string SetupName(const std::string &typed)
{
	const size_t a = typed.find_first_not_of(" \t");
	if (a == std::string::npos) return std::string();
	return typed.substr(a, typed.find_last_not_of(" \t") - a + 1);
}

int MatchRoomSetup(const State &st)
{
	const GameRoomSnapshot &g = st.game;
	if (!g.host) return -1;
	for (size_t i = 0; i < st.setups.size(); i++) {
		const RoomSetup &s = st.setups[i];
		if ((g.rules & kRuleMask) != (s.rules & kRuleMask) || (g.siteFlags & kSiteMask) != (s.sites & kSiteMask)) continue;
		if ((s.speed != kRoomSpeedProfile && s.speed != g.gameSpeed) || s.teamColors != st.teamColors) continue;
		if (s.random && !g.scenario.empty()) continue;
		if (!s.scenario.empty() && (g.scenario.empty() || MapKey(g.scenario) != MapKey(s.scenario))) continue;
		bool same = true;
		for (const RoomSetupOption &so : s.options) {
			const GameRoomOption *o = FindOption(g, so.id);
			if (o && !OptionMatches(*o, so)) same = false;
		}
		if (same) return static_cast<int>(i);
	}
	return -1;
}

void SaveRoomSetup(State &st, const std::string &name)
{
	const GameRoomSnapshot &g = st.game;
	RoomSetup s;
	s.name = name;
	s.random = g.scenario.empty() && g.randomMap;
	s.scenario = g.scenario;
	for (const GameRoomOption &o : g.options) {
		// A scenario's map type is the selector's item: the scenario stands for it.
		if (!SetupKeeps(o.id) || (o.id == kMapTypeOption && !s.random)) continue;
		RoomSetupOption so;
		so.id = o.id;
		so.checkbox = o.checkbox;
		if (o.checkbox) so.value = o.value;
		else {
			if (o.sel < 0 || o.sel >= static_cast<int>(o.items.size())) continue;
			so.sel = o.sel;
			so.text = o.items[o.sel];
			if (o.sel < static_cast<int>(o.keys.size())) so.key = o.keys[o.sel];
		}
		s.options.push_back(so);
	}
	s.rules = g.rules & kRuleMask;
	s.sites = g.siteFlags & kSiteMask;
	s.speed = g.gameSpeed;
	s.teamColors = st.teamColors;
	bool replaced = false;
	for (RoomSetup &old : st.setups) {
		if (old.name != name) continue;
		old = s;
		replaced = true;
	}
	if (!replaced) st.setups.push_back(s);
	WriteSetups(st.setups);
	LogI("room", "setup \"%s\" saved (%u options)", name.c_str(), static_cast<unsigned>(s.options.size()));
}

void DeleteRoomSetup(State &st, const std::string &name)
{
	const size_t before = st.setups.size();
	st.setups.erase(std::remove_if(st.setups.begin(), st.setups.end(), [&](const RoomSetup &s) { return s.name == name; }), st.setups.end());
	if (st.setups.size() == before) return;
	WriteSetups(st.setups);
	LogI("room", "setup \"%s\" deleted", name.c_str());
}

void StartRoomSetup(State &st, const RoomSetup &s)
{
	st.setupApply = SetupApply();
	st.setupApply.on = true;
	st.setupApply.setup = s;
	// What takes no list of the game's goes at once; the map and the lists then one at a time (StepRoomSetup).
	st.game.rules = (st.game.rules & ~kRuleMask) | (s.rules & kRuleMask);
	GameRoomSetRules(st.game.rules);
	st.game.siteFlags = (st.game.siteFlags & ~kSiteMask) | (s.sites & kSiteMask);
	GameRoomSetSiteFlags(st.game.siteFlags);
	if (s.speed != kRoomSpeedProfile) {
		st.game.gameSpeed = s.speed;
		GameRoomSetSpeed(s.speed);
	}
	st.teamColors = s.teamColors;
	LogI("room", "setup \"%s\"", s.name.c_str());
}

void StepRoomSetup(State &st, const GameRoomSnapshot &g)
{
	SetupApply &a = st.setupApply;
	if (!a.on) return;
	if (!g.host || g.optionsLocked) {
		a.on = false;
		return;
	}
	// The game rebuilds lists when others change (the sizes with the map, the limits with the mode): one change, then the room is read again.
	const DWORD now = GetTickCount();
	if (a.tick && now - a.tick < 600) return;
	a.tick = now;
	const RoomSetup &s = a.setup;
	if (++a.tries > 40) {
		if (g.picker && !a.mapDone) GameRoomPickerCancel();
		a.on = false;
		LogW("room", "setup \"%s\": not all of it could be set", s.name.c_str());
		return;
	}
	if (!a.mapDone && !StepSetupMap(g, a)) return;
	if (g.picker) return;
	const GameMode *mode = GameModeFor(g.mode.c_str());
	for (const RoomSetupOption &so : s.options) {
		if (so.id == kMapTypeOption || !SetupKeeps(so.id) || GameModeLocksOption(mode, so.id)) continue;
		const GameRoomOption *o = FindOption(g, so.id);
		if (!o || OptionMatches(*o, so)) continue;
		// What the game does not take after two tries (a choice it has disabled) is left as it is.
		int &tried = a.tried[so.id];
		if (tried >= 2) continue;
		tried++;
		if (o->checkbox) {
			Predict(kPredOptVal, o->id, so.value);
			GameRoomToggleOption(o->id);
		}
		else {
			const int item = WantedItem(*o, so);
			Predict(kPredOptSel, o->id, item);
			GameRoomSetOption(o->id, item);
		}
		return;
	}
	a.on = false;
	LogI("room", "setup \"%s\" in the room", s.name.c_str());
}

void PaintSetupPrompt(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 440, 176, "Save setup");
	PaintField(p, st, d.BodyX(), d.BodyTop() + 6, d.BodyW(), 36, st.setupInput, "Setup name", st.focus == kSetupNameField, kFieldSetupName);
	const std::string name = SetupName(st.setupInput);
	bool exists = false;
	for (const RoomSetup &s : st.setups) exists = exists || s.name == name;
	DialogButtons(p, d, {{exists ? "Overwrite" : "Save", kSetupSave, 0, true, !name.empty()}, {"Cancel", kSetupCancel}}, mx, my);
}

namespace {
// The map, the size, the mode and the speed of a setup, as the room's lists had them when it was saved.
std::string SetupSummary(const RoomSetup &s)
{
	auto text = [&s](int id) {
		for (const RoomSetupOption &so : s.options) {
			if (so.id == id && !so.checkbox) return so.text;
		}
		return std::string();
	};
	std::vector<std::string> parts;
	if (!s.scenario.empty()) {
		std::string file = FileOf(s.scenario);
		if (file.size() > 5 && _stricmp(file.c_str() + file.size() - 5, ".bfhp") == 0) file.resize(file.size() - 5);
		parts.push_back(file);
	}
	else if (s.random) {
		if (!text(kMapTypeOption).empty()) parts.push_back(MapTypeLabel(text(kMapTypeOption)));
		if (!text(0x01000008).empty()) parts.push_back(text(0x01000008));
	}
	if (!text(0x1002).empty()) parts.push_back(text(0x1002));
	if (s.speed != kRoomSpeedProfile) parts.push_back(TrFmt("Speed %s", {std::to_string(SpeedPercent(s.speed)) + " %"}));
	std::string out;
	for (const std::string &part : parts) out += (out.empty() ? "" : "  \xC2\xB7  ") + part;
	return out;
}
}

void PaintSetupLoad(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY, rowH = 54;
	const int n = static_cast<int>(st.setups.size()), shown = std::max(1, std::min(kSetupRows, n));
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 680, 60 + shown * rowH + 14 + 56, "Load setup");
	const int current = MatchRoomSetup(st), first = std::max(0, std::min(st.setupScroll, n - kSetupRows));
	const int delW = p.ButtonWidth(st.setupDeleteAsk >= 0 ? "Delete?" : "Delete", 104, 1), loadW = p.ButtonWidth("Load", 104, 1);
	const int delX = d.x + d.w - d.pad - delW, loadX = delX - 8 - loadW, textW = loadX - d.BodyX() - 16;
	int ry = d.BodyTop();
	for (int i = first; i < n && i < first + kSetupRows; i++, ry += rowH) {
		const RoomSetup &s = st.setups[i];
		if (p.Hover(d.x + 1, ry, d.w - 2, rowH, mx, my)) p.Fill(d.x + 1, ry + 1, d.w - 2, rowH - 2, pal::selected);
		// The one the room has now.
		if (i == current) p.Fill(d.x + 1, ry + 8, 3, rowH - 16, pal::red);
		const bool raw = p.raw;
		p.raw = true;
		p.Font(1);
		p.Text(d.BodyX(), ry + 6, textW, 24, s.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(d.BodyX(), ry + 29, textW, 20, SetupSummary(s), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = raw;
		const int bh = 32, by = ry + (rowH - bh) / 2;
		p.Button(loadX, by, loadW, bh, "Load", kSetupLoad, i, mx, my, true, true);
		p.Button(delX, by, delW, bh, st.setupDeleteAsk == i ? "Delete?" : "Delete", kSetupDelete, i, mx, my, true, false);
		if (i + 1 < n && i + 1 < first + kSetupRows) p.Fill(d.BodyX(), ry + rowH - 1, d.BodyW(), 1, pal::hairline);
	}
	// More than the dialog shows: where the rows shown are among them (the wheel moves them).
	if (n > kSetupRows) {
		const int trackY = d.BodyTop(), trackH = shown * rowH, barH = std::max(24, trackH * kSetupRows / n);
		const int barY = trackY + (trackH - barH) * first / (n - kSetupRows);
		p.Fill(d.x + d.w - 8, trackY, 3, trackH, pal::field);
		p.Fill(d.x + d.w - 8, barY, 3, barH, pal::edgeHot);
	}
	DialogButtons(p, d, {{"Close", kSetupCancel}}, mx, my);
}
