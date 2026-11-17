// What kind of file a mod ships, for the warnings of validate-mod.js. See "Where art, sounds and texts go" in sdk/rulesets.md.
//
// The art, sounds, voices and texts of a big mod fall in three classes, and each one has its place:
//   1. the art of a civilization (its units, buildings, icons)  -> the package root, under names of its own
//   2. global in-game files that replace the game's             -> a ruleset layer (rulesets/<id>/), because the host chooses one
//   3. the menu skin: menu INIs, backgrounds, buttons, lobby pages, tips and help
//                                                               -> a skin of its own (a later feature), not a ruleset
// Only class 3 is listed here: it is a fixed list of paths. Class 2 is "a file the game already has", which needs the game's
// folder (--game) to know. Everything else is class 1.
'use strict';
const fs = require('fs'), path = require('path');

// Paths in their real form (lowercase, "/"): local/<language>/x is currentlang/x, and the folders the game mounts under another name are
// the real ones (gameres/ is ui/, menuini/ is data/interface/menu/). The table is the one of src/mods/volumes.h.
const VOLUMES = [['gameres/', 'ui/'], ['gameini/', 'data/interface/'], ['commonres/', 'ui/common/'], ['commonini/', 'data/interface/common/'],
	['menures/', 'ui/menu/'], ['menuini/', 'data/interface/menu/'], ['cursors/', 'ui/cursors/'], ['editorres/', 'ui/editor/'],
	['editorini/', 'data/interface/editor/']];
const MENU_SKIN = [
	/^data\/interface\/menu\//,                           // the menu INIs and the pages of the lobby
	/^ui\/menu\//,                                        // the buttons and pictures of the menus
	/^currentlang\/(menubackground|menu_16_9|menu_4_3)\.bmp$/,
	/^currentlang\/logo[^/]*\.bmp$/,
	/^currentlang\/(tips|help)\.xml$/,                    // the tips of the loading screen and the help (F1)
];

function gamePath(rel) {
	const p = rel.replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase();
	const m = p.match(/^local\/[^/]+\/(.+)$/);
	if (m) return 'currentlang/' + m[1];
	const v = VOLUMES.find(([name]) => p.startsWith(name));
	return v ? v[1] + p.slice(v[0].length) : p;
}

// A path that changes gameplay, as the DLL counts it (src/mods/modrules.cpp, IsGameplayPath): a package whose root has one is a rules package.
function isGameplayPath(rel) {
	const p = rel.replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase();
	if (/^(data|civs|specials|events|rules|modes|options|randommap|rulesets|randommapsettlements)\//.test(p)) return true;
	if (/^packs\/.*\.bfhp$/.test(p)) return true;
	if (/^units\/.*\.xml$/.test(p) || /^(buildings|mapobjects|visuals)\/.*\.xml$/.test(p)) return true;
	return /^(units|buildings|mapobjects)\/.*\.pass$/.test(p);
}

// Whether the files of a package root make it a rules package, as the DLL counts it: civilizations, gameplay files, or a patch of a gameplay file.
function isRulesPackage(files) {
	return files.some(f => /^civs\//i.test(f) || isGameplayPath(f) || (/^patches\//i.test(f) && isGameplayPath(f.slice(8))));
}

function isMenuSkin(rel) {
	const p = gamePath(rel);
	return MENU_SKIN.some(re => re.test(p));
}

// The names inside a .pak, as game paths. The index is at the start of the file: only that part is read.
function pakNames(file) {
	const fd = fs.openSync(file, 'r');
	try {
		const total = fs.fstatSync(fd).size;
		for (let size = 1 << 20; ; size *= 2) {
			const buf = Buffer.alloc(Math.min(size, total));
			const n = fs.readSync(fd, buf, 0, buf.length, 0);
			if (n < 0x28 || buf.toString('latin1', 0, 15) !== 'HMMSYS PackFile') return [];
			const count = buf.readUInt32LE(0x20);
			const names = [];
			let p = 0x28, prev = '', complete = true;
			for (let i = 0; i < count; i++) {
				if (p + 2 > n) { complete = false; break; }
				const len = buf[p], shared = buf[p + 1];
				p += 2;
				if (len < shared || p + len - shared + 8 > n) { complete = false; break; }
				prev = prev.slice(0, shared) + buf.toString('latin1', p, p + len - shared);
				p += len - shared + 8;
				names.push(prev);
			}
			if (complete || n >= total) return names;
		}
	}
	finally { fs.closeSync(fd); }
}

// The data of one entry of a .pak (the names are game paths, any case), or null. The entries are stored as they are.
function pakEntry(file, wanted) {
	const buf = fs.readFileSync(file);
	if (buf.length < 0x28 || buf.toString('latin1', 0, 15) !== 'HMMSYS PackFile') return null;
	const count = buf.readUInt32LE(0x20);
	let p = 0x28, prev = '';
	for (let i = 0; i < count; i++) {
		const len = buf[p], shared = buf[p + 1];
		p += 2;
		prev = prev.slice(0, shared) + buf.toString('latin1', p, p + len - shared);
		p += len - shared;
		if (prev.replace(/\\/g, '/').toLowerCase() === wanted) return buf.subarray(buf.readUInt32LE(p), buf.readUInt32LE(p) + buf.readUInt32LE(p + 4));
		p += 8;
	}
	return null;
}

// The text of one of the game's own files (a game path like data/commands/rbarracks.xml) from the .pak files of Packs/ of the game folder, or null.
function gameFileText(gameDir, wanted) {
	const dir = path.join(gameDir, 'Packs');
	if (!fs.existsSync(dir)) return null;
	for (const f of fs.readdirSync(dir)) {
		if (!/\.pak$/i.test(f)) continue;
		const data = pakEntry(path.join(dir, f), wanted);
		if (data) return data.toString('latin1');
	}
	return null;
}

// The loose files below a folder of the game, as game paths ("units/Ghero1/x.rle.mmp" is "units/ghero1/x.rle.mmp").
function looseFiles(dir, rel, out) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		if (e.isDirectory()) looseFiles(path.join(dir, e.name), rel + e.name + '/', out);
		else out.add((rel + e.name).toLowerCase());
	}
}

// Every file the game has, as game paths: Packs/*.pak, local/*.pak (only CURRENTLANG/) and the loose files of Units/ and Currentlang/ of the game folder.
// It is the list the DLL reads (src/mods/gamefiles.cpp): a root file of a civilization pack at one of these paths is global. null if there is no game folder.
function gameFiles(gameDir) {
	if (!gameDir) return null;
	const out = new Set();
	for (const dir of ['Packs', 'local']) {
		const full = path.join(gameDir, dir);
		if (!fs.existsSync(full)) continue;
		for (const f of fs.readdirSync(full)) {
			if (!/\.pak$/i.test(f)) continue;
			for (const name of pakNames(path.join(full, f))) {
				const p = name.replace(/\\/g, '/').toLowerCase();
				if (dir === 'local' && !p.startsWith('currentlang/')) continue;
				out.add(p);
			}
		}
	}
	for (const dir of ['Units', 'Currentlang']) {
		const full = path.join(gameDir, dir);
		if (fs.existsSync(full) && fs.statSync(full).isDirectory()) looseFiles(full, dir.toLowerCase() + '/', out);
	}
	return out.size ? out : null;
}

// Why a file in a ruleset layer keeps the restart when the player switches to or from that ruleset (the same answer as HotSwapKindOf in the DLL, src/mods/hotswap.cpp),
// or '' if it is switched in place.
const RESTART_KEPT = [
	[/(^|\/)rle\.mmp\.tail$/, 'the graphics tail: the game maps its sprite file (rle.mmp) once, when it starts, and every sprite keeps a pointer into it. Put the sprites as loose .rle files with their .ent.xml in units\\<name>\\ (or buildings, mapobjects, visuals): they switch in place'],
	[/^patches\/.*\.pak$/, 'a .pak in patches\\: only the paks of packs\\ and local\\ are mounted and taken out in place'],
	[/^(gameres|ui)\/(selections|cursors|menu|interface|common|editor|update|debug)\//, 'a picture the game reads once, at start or when a menu window is built'],
	[/^(gameres|ui)\/[^/]+\.bmp$/, 'a picture the game reads once, at start or when a menu window is built'],
	[/^(fonts\/|menuini\/|gameini\/|commonini\/|editorini\/|cursors\/|data\/interface\/|patches\/data\/interface\/)|^(patches\/)?data\/cursors\.xml$/, 'a font, a cursor or a layout of the interface: the game reads it once'],
	[/(^|\/)(splash|endsplash1|endsplash2)\.ini$/, 'a splash screen: the game reads it once, at start and exit'],
	[/^(civs|specials|options|events|rules)\//, 'a civilization, special, option, event or rule: the game reads it once, at start'],
];
function restartKept(rel) {
	const p = rel.replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase();
	for (const [re, why] of RESTART_KEPT) if (re.test(p)) return why;
	return '';
}

module.exports = { restartKept, MENU_SKIN, gamePath, isMenuSkin, isGameplayPath, isRulesPackage, pakEntry, gameFileText, pakNames, gameFiles };
