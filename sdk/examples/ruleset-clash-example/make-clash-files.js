'use strict';
// Makes the two files of this example that are copies of the game's own: the command file of the Roman barracks (data\commands\RBarracks.xml) with the price
// of the hastatus changed, for clash-c (77 gold) and clash-d (88 gold). They are not in the repository (a file of the game does not go in it): this
// reads the game's own data.pak, takes DATA\COMMANDS\RBARRACKS.XML out of it, changes the cost of the command trainRHastatus and writes the result
// to clash-c\data\commands\RBarracks.xml and clash-d\data\commands\RBarracks.xml, next to this script or in the folder you give as the second
// argument (give "<game folder>\mods" to write them into the copies of the packages that are already there).
//
//   node make-clash-files.js "<game folder>" ["<folder with clash-c and clash-d>"]
const fs = require('fs');
const path = require('path');

const WANTED = 'DATA\\COMMANDS\\RBARRACKS.XML';
const PRICES = { 'clash-c': 77, 'clash-d': 88 };

// The entries of a .pak (HMMSYS PackFile): the names are stored with the length they share with the previous one, then offset and size, uncompressed.
function readPakEntry(file, name) {
	const buf = fs.readFileSync(file);
	if (buf.toString('latin1', 0, 15) !== 'HMMSYS PackFile') return null;
	const count = buf.readUInt32LE(0x20);
	let p = 0x28, prev = '';
	for (let i = 0; i < count; i++) {
		const len = buf[p], shared = buf[p + 1];
		p += 2;
		prev = prev.slice(0, shared) + buf.toString('latin1', p, p + len - shared);
		p += len - shared;
		if (prev.toUpperCase() === name) return buf.subarray(buf.readUInt32LE(p), buf.readUInt32LE(p) + buf.readUInt32LE(p + 4));
		p += 8;
	}
	return null;
}

// The text of the command file with the cost of trainRHastatus changed (only that line of that command).
function withPrice(text, price) {
	const start = text.search(/<cmd\s+name="trainRHastatus"/i);
	if (start < 0) throw new Error('the command trainRHastatus is not in the file');
	const end = text.indexOf('</cmd>', start);
	const cmd = text.slice(start, end);
	const changed = cmd.replace(/(costgold\s*=\s*")\d+(")/i, `$1${price}$2`);
	if (changed === cmd) throw new Error('the command trainRHastatus has no costgold');
	return text.slice(0, start) + changed + text.slice(end);
}

function make(gameDir, outDir) {
	const packs = path.join(gameDir, 'Packs');
	if (!fs.existsSync(packs)) throw new Error(`"${gameDir}" has no Packs folder: it is not the game's folder`);
	let data = null;
	for (const f of fs.readdirSync(packs)) {
		if (/\.pak$/i.test(f) && (data = readPakEntry(path.join(packs, f), WANTED))) break;
	}
	if (!data) throw new Error(`${WANTED} is not in any .pak of ${packs}`);
	const text = data.toString('latin1');
	const written = [];
	for (const [pack, price] of Object.entries(PRICES)) {
		const target = path.join(outDir, pack, 'data', 'commands', 'RBarracks.xml');
		fs.mkdirSync(path.dirname(target), { recursive: true });
		fs.writeFileSync(target, Buffer.from(withPrice(text, price), 'latin1'));
		written.push(target);
	}
	return written;
}

module.exports = { make, withPrice, readPakEntry };

if (require.main === module) {
	const game = process.argv[2];
	if (!game) {
		console.error('usage: node make-clash-files.js "<game folder>" ["<folder with clash-c and clash-d>"]');
		process.exit(2);
	}
	try {
		for (const f of make(game, process.argv[3] || __dirname)) console.log('written: ' + f);
	}
	catch (e) {
		console.error(e.message);
		process.exit(1);
	}
}
