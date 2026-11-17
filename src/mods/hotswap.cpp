// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/hotswap.h"

#include "addr/net.h"
#include "addr/world.h"
#include "mods/savelib.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
std::string Normal(std::string s)
{
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}
}

namespace {
bool StartsWith(const std::string &s, const char *prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

std::string Extension(const std::string &f)
{
	const size_t dot = f.find_last_of('.'), slash = f.find_last_of('/');
	return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : f.substr(dot);
}
}

namespace {
// The folders of gameres/ (ui/) whose images are made again for every match: the icons and buttons of the command bar and the info bar, the queue
// icons and portraits, the notification icons, the help pictures and the conversation portraits (CCmdRegistry, the item, skill and class tables, the
// quick icon bars and the building queues are made in a match's data and in the match's windows). Everything else under gameres/ is read once.
// The folder of a match's images that real (ui/...) is in, or nullptr.
const char *MatchFolderOf(const std::string &real)
{
	static const char *const folders[] = {"ui/icons/", "ui/cmdbar/", "ui/infobar/", "ui/noteicons/", "ui/help/", "ui/conversations/"};
	for (const char *folder : folders) {
		if (StartsWith(real, folder)) return folder;
	}
	return nullptr;
}

bool MatchArtFolder(const std::string &real)
{
	const char *folder = MatchFolderOf(real);
	return folder && real.size() > strlen(folder);
}

// gameres/x is ui/x: the game names a file with the volume, the caches and the files of a mod with either.
std::string RealPath(const std::string &f)
{
	return StartsWith(f, "gameres/") ? "ui/" + f.substr(8) : f;
}

bool IsImage(const std::string &ext)
{
	return ext == ".bmp" || ext == ".rle";
}

// minimap/zoom<n>/entities/<name>.bmp: made for each minimap picture (GenerateMiniMapImage keeps its entity cache on the stack).
bool MinimapEntityPicture(const std::string &f)
{
	if (!StartsWith(f, "minimap/zoom")) return false;
	const size_t slash = f.find('/', 12);
	if (slash == std::string::npos || slash == 12) return false;
	for (size_t i = 12; i < slash; i++) {
		if (!isdigit(static_cast<unsigned char>(f[i]))) return false;
	}
	if (f.compare(slash, 10, "/entities/") != 0) return false;
	return f.size() > slash + 10 && f.find('/', slash + 10) == std::string::npos && Extension(f) == ".bmp";
}
}

bool HotSwapTextReadOnce(const std::string &raw)
{
	const std::string f = Normal(raw);
	const size_t slash = f.find_last_of('/');
	const std::string leaf = slash == std::string::npos ? f : f.substr(slash + 1);
	return leaf == "splash.ini" || leaf == "endsplash1.ini" || leaf == "endsplash2.ini";
}

namespace {
// A text the game reads each time it needs it or when a match starts: the DLL's loc\ files that are merged into game.loc.xml, and the .xml, .txt and .ini
// of the language folders (local\<language>\, currentlang\ and patches\currentlang\; not voices, not paks, not pictures) except the splash screens.
bool TextFile(const std::string &f, const std::string &ext)
{
	if (ext != ".xml" && ext != ".txt" && ext != ".ini") return false;
	if (StartsWith(f, "loc/")) return ext == ".xml" && f.size() > 4;
	std::string rest;
	if (StartsWith(f, "patches/currentlang/")) rest = f.substr(20);
	else if (StartsWith(f, "currentlang/")) rest = f.substr(12);
	else if (StartsWith(f, "local/")) {
		const size_t slash = f.find('/', 6);
		if (slash == std::string::npos || slash == 6) return false;
		rest = f.substr(slash + 1);
	}
	else return false;
	if (rest.empty() || StartsWith(rest, "voices/")) return false;
	return !HotSwapTextReadOnce(f);
}
}

namespace {
bool SoundExtension(const std::string &ext)
{
	return ext == ".wav" || ext == ".ogg";
}

// An audio file the game opens each time it plays it: sounds\, music\, and the voices of the language folders.
bool SoundFile(const std::string &f, const std::string &ext)
{
	if (!SoundExtension(ext)) return false;
	if (StartsWith(f, "sounds/") || StartsWith(f, "music/") || StartsWith(f, "currentlang/voices/")) return f.find('/') != f.size() - 1;
	if (StartsWith(f, "local/")) {
		const size_t slash = f.find('/', 6);
		return slash != std::string::npos && slash > 6 && f.compare(slash, 8, "/voices/") == 0 && f.size() > slash + 8;
	}
	return false;
}

// The definitions of sounds the game reads once, into the table of its sound manager.
bool SoundEntityFile(const std::string &f, const std::string &ext)
{
	if (ext != ".xml") return false;
	const char *folders[] = {"data/sound entities/", "patches/data/sound entities/"};
	for (const char *folder : folders) {
		if (StartsWith(f, folder)) return f.size() > strlen(folder);
	}
	return false;
}
}

namespace {
// The art of an entity: its definition and the pictures it names, in the folder of the game's units, buildings, map objects and visuals.
bool EntityArtFile(const std::string &f)
{
	const char *roots[] = {"units/", "buildings/", "mapobjects/", "visuals/"};
	bool inRoot = false;
	for (const char *root : roots) inRoot = inRoot || (StartsWith(f, root) && f.size() > strlen(root));
	if (!inRoot || f.back() == '/') return false;
	const std::string ext = Extension(f);
	return ext == ".rle" || ext == ".bmp" || (ext == ".xml" && f.size() > 8 && f.compare(f.size() - 8, 8, ".ent.xml") == 0) ||
		(ext == ".mmp" && f.size() > 8 && f.compare(f.size() - 8, 8, ".rle.mmp") == 0);
}
}

namespace {
// The maps the game lists from disk each time a window asks (the lobby's maps, the campaigns, the conquests, the random map types) and opens when a game is made.
bool MapFile(const std::string &f, const std::string &ext)
{
	static const char *const listed[] = {"scenarios/", "adventures/", "conquests/", "conquestmaps/"};
	for (const char *folder : listed) {
		if (StartsWith(f, folder)) return f.size() > strlen(folder) && f.back() != '/' && (ext == ".bfhp" || ext == ".bmp" || ext == ".txt" || ext == ".md");
	}
	// The packs the random map generator and the editor read by name, one level.
	if (StartsWith(f, "packs/")) return f.size() > 6 && f.find('/', 6) == std::string::npos && ext == ".bfhp";
	// The pictures of the random map types (randommap\<Name>\010.bmp) and the templates of the cities of the random maps (the files of the bfhp that holds them, loose).
	if (StartsWith(f, "randommap/")) return f.size() > 10 && f.back() != '/' && ext == ".bmp";
	if (StartsWith(f, "randommapsettlements/")) return f.size() > 21 && f.back() != '/' && (ext == ".xml" || ext == ".grid" || ext == ".bmp");
	return false;
}
}

bool HotSwapReadOnce(const std::string &raw)
{
	const std::string f = Normal(raw);
	const std::string real = RealPath(f);
	if (HotSwapTextReadOnce(f)) return true;
	if (StartsWith(f, "fonts/") || StartsWith(f, "menuini/") || StartsWith(f, "gameini/") || StartsWith(f, "commonini/") || StartsWith(f, "editorini/") ||
		StartsWith(f, "cursors/") || StartsWith(f, "data/interface/") || StartsWith(f, "patches/data/interface/") || f == "data/cursors.xml" ||
		f == "patches/data/cursors.xml") {
		return true;
	}
	// Anything else under ui/ that is not in one of a match's folders: selections, cursors, menu, interface, common, editor, update, debug...
	return StartsWith(real, "ui/") && !MatchFolderOf(real);
}

unsigned HotSwapKindOf(const std::string &raw)
{
	const std::string f = Normal(raw);
	const std::string ext = Extension(f);
	if (f == "hidden.txt") return kHotHidden;
	if (HotSwapReadOnce(f)) return kHotOther;
	const std::string real = RealPath(f);
	if (TextFile(f, ext)) return kHotText;
	if (SoundEntityFile(f, ext)) return kHotSoundData;
	if (EntityArtFile(f)) return kHotEntity;
	// The tail of a ruleset: the file at the root of its folder (a package's own is not in this list).
	if (f == "rle.mmp.tail") return kHotTail;
	if (MapFile(f, ext)) return kHotMap;
	if (SoundFile(f, ext)) return kHotSound;
	if (IsImage(ext) && MatchArtFolder(real)) return kHotArt;
	if (MinimapEntityPicture(f)) return kHotArt;
	// A file in a subfolder or with no name is not one of these.
	if (StartsWith(f, "patches/data/") && f.size() > 13 && (ext == ".xml" || ext == ".ini" || ext == ".vs")) return kHotPatch;
	if (StartsWith(f, "data/") && f.size() > 5 && (ext == ".xml" || ext == ".ini" || ext == ".vs")) return kHotLoose;
	if (StartsWith(f, "modes/") && f.size() > 6 && f.find('/', 6) == std::string::npos && ext == ".json") return kHotModes;
	return kHotOther;
}

unsigned HotSwapKinds(const std::vector<std::string> &files)
{
	unsigned kinds = kHotNone;
	for (const std::string &f : files) kinds |= HotSwapKindOf(f);
	return kinds;
}

bool HotSwapLayerOk(const std::vector<std::string> &files)
{
	return (HotSwapKinds(files) & kHotOther) == 0;
}

std::string HotSwapKindsText(unsigned kinds)
{
	std::string out;
	auto add = [&](unsigned bit, const char *name) {
		if (kinds & bit) out += (out.empty() ? "" : ", ") + std::string(name);
	};
	add(kHotPatch, "patches of data/");
	add(kHotLoose, "files in data/");
	add(kHotModes, "modes");
	add(kHotHidden, "hidden.txt");
	add(kHotArt, "interface images");
	add(kHotText, "texts");
	add(kHotSound, "sounds");
	add(kHotSoundData, "sound entities");
	add(kHotEntity, "entity art");
	add(kHotMap, "maps");
	add(kHotTail, "graphics tail");
	add(kHotOther, "other files");
	return out.empty() ? "nothing" : out;
}

bool SaveSwitchesInPlace(bool plainGame, bool needsActivation, bool rulesetOnly, bool somethingExtra, bool switchQualifies)
{
	return plainGame && needsActivation && rulesetOnly && !somethingExtra && switchQualifies;
}

bool ConstIniHasGamePlay(const std::string &text)
{
	size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	while (at < text.size()) {
		size_t end = text.find_first_of("\r\n", at);
		if (end == std::string::npos) end = text.size();
		size_t from = at, to = end;
		while (from < to && isspace(static_cast<unsigned char>(text[from]))) from++;
		while (to > from && isspace(static_cast<unsigned char>(text[to - 1]))) to--;
		if (Normal(text.substr(from, to - from)) == "[gameplay]") return true;
		at = end + 1;
	}
	return false;
}

std::vector<std::string> HotSwapExpandPaks(const std::vector<std::string> &files, const std::vector<HotSwapPak> &paks, const std::string &language)
{
	const std::string lang = Normal(language);
	std::vector<std::string> out;
	for (const std::string &raw : files) {
		const std::string f = Normal(raw);
		if (Extension(f) != ".pak") {
			out.push_back(raw);
			continue;
		}
		const HotSwapPak *pak = nullptr;
		for (const HotSwapPak &p : paks) {
			if (Normal(p.rel) == f) pak = &p;
		}
		const bool flatLocal = StartsWith(f, "local/") && f.find('/', 6) == std::string::npos;
		if (!pak) {
			// The game mounts only the language pak of its language.
			if (flatLocal && !lang.empty() && f.compare(6, lang.size(), lang) != 0) continue;
			out.push_back(raw);
			continue;
		}
		if (pak->unreadable) {
			out.push_back(raw);
			continue;
		}
		for (const std::string &m : pak->members) {
			if (!pak->language || StartsWith(m, "currentlang/")) out.push_back(m);
			else out.push_back("local/" + lang + "/voices/" + m);
		}
	}
	return out;
}

std::vector<std::string> HotSwapEntityFolders(const std::vector<std::string> &files)
{
	std::vector<std::string> out;
	for (const std::string &raw : files) {
		const std::string f = Normal(raw);
		if (HotSwapKindOf(f) != kHotEntity) continue;
		const std::string folder = f.substr(0, f.find_last_of('/'));
		if (std::find(out.begin(), out.end(), folder) == out.end()) out.push_back(folder);
	}
	return out;
}

std::vector<std::string> HotSwapArtFiles(const std::vector<std::string> &files)
{
	std::vector<std::string> out;
	for (const std::string &raw : files) {
		const std::string f = Normal(raw);
		if (HotSwapKindOf(f) != kHotArt) continue;
		const std::string real = RealPath(f);
		if (std::find(out.begin(), out.end(), real) == out.end()) out.push_back(real);
	}
	return out;
}

RestartReason HotSwapRestartReason(const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	RestartReason reason;
	auto collect = [&](bool once) {
		for (const std::vector<std::string> *list : {&from, &to}) {
			for (const std::string &f : *list) {
				if (HotSwapKindOf(f) != kHotOther || HotSwapReadOnce(f) != once) continue;
				if (reason.files.size() < kRestartReasonFiles && std::find(reason.files.begin(), reason.files.end(), f) == reason.files.end()) reason.files.push_back(f);
			}
		}
	};
	collect(true);
	if (!reason.files.empty()) {
		reason.why = RestartWhy::kReadOnce;
		return reason;
	}
	collect(false);
	if (!reason.files.empty()) reason.why = RestartWhy::kOtherFiles;
	return reason;
}

HotSwap SwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	if (sameRuleset) return HotSwap::kNothing;
	return HotSwapLayerOk(from) && HotSwapLayerOk(to) ? HotSwap::kReload : HotSwap::kRestart;
}

std::vector<std::string> SwapFilesOf(const std::vector<std::string> &layerFiles, const std::vector<std::string> &packGlobal, bool samePackAsOther)
{
	std::vector<std::string> out = layerFiles;
	if (!samePackAsOther) out.insert(out.end(), packGlobal.begin(), packGlobal.end());
	return out;
}

GameKind GameKindOfVtable(uintptr_t vtable)
{
	if (vtable == addr::kGameSingleVtable) return GameKind::kSkirmish;
	if (vtable == addr::kMultiplayerVtable) return GameKind::kMultiplayer;
	if (vtable == addr::kGameAdvVtable) return GameKind::kAdventure;
	if (vtable == addr::kGameConqVtable) return GameKind::kConquest;
	if (vtable == addr::kGameEditVtable) return GameKind::kEditor;
	return GameKind::kUnknown;
}

const char *GameKindName(GameKind kind)
{
	switch (kind) {
	case GameKind::kSkirmish: return "single-player";
	case GameKind::kMultiplayer: return "multiplayer";
	case GameKind::kAdventure: return "adventure";
	case GameKind::kConquest: return "conquest";
	case GameKind::kEditor: return "editor";
	default: return "unknown";
	}
}

bool SaveKindSwitchesInPlace(int saveKind)
{
	return saveKind == kSaveStrategy || saveKind == kSaveAdventure || saveKind == kSaveConquest;
}

bool GameKindCanSwitchWhileRunning(GameKind kind)
{
	return kind == GameKind::kSkirmish || kind == GameKind::kAdventure || kind == GameKind::kConquest;
}

bool PendingSwitchStillValid(unsigned chosenTick, unsigned nowTick)
{
	return nowTick - chosenTick <= kPendingSwitchMs;
}
