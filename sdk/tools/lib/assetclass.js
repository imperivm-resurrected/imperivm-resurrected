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

// Paths in the form the game uses (lowercase, "/"): local/<language>/x is currentlang/x, and the game's UI.pak is gameres/.
const MENU_SKIN = [
	/^menuini\//,                                         // the menu INIs and the pages of the lobby
	/^data\/interface\/menu\//,                           // the same, in data/
	/^ui\/menu\//, /^gameres\/menu\//,                    // the buttons and pictures of the menus
	/^currentlang\/(menubackground|menu_16_9|menu_4_3)\.bmp$/,
	/^currentlang\/logo[^/]*\.bmp$/,
	/^currentlang\/(tips|help)\.xml$/,                    // the tips of the loading screen and the help (F1)
];

function gamePath(rel) {
	const p = rel.replace(/\\/g, '/').replace(/^\/+/, '').toLowerCase();
	const m = p.match(/^local\/[^/]+\/(.+)$/);
	return m ? 'currentlang/' + m[1] : p;
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

// Every file the game has, as game paths: Packs/*.pak (UI/ is gameres/) and local/*.pak (CURRENTLANG/). null if there is no game folder.
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
				out.add(p.startsWith('ui/') ? 'gameres/' + p.slice(3) : p);
			}
		}
	}
	return out.size ? out : null;
}

module.exports = { MENU_SKIN, gamePath, isMenuSkin, pakNames, gameFiles };
