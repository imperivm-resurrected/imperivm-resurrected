'use strict';
// Makes the `tailed` ruleset of clash-b: the cube soldier of the blue ruleset as the hastatus, but with its pixels in an rle.mmp.tail instead of in .rle files:
// rulesets\tailed\rle.mmp.tail (the pixels, after the game's rle.mmp), rulesets\tailed\units\RHastatus\*.rle.mmp (the descriptors: each cell says at which byte of the
// combined rle.mmp its pixels are) and RHastatus.ent.xml (the same entity, naming the sheets without ".rle" as the game's own do). Nothing of it is in the repository:
// the tail says which game's rle.mmp (size and sha256) it was made for, and the offsets in the descriptors depend on its size, so it is made from the player's game.
//
//   node make-tailed-ruleset.js "<game folder>" ["<folder with clash-b>"]
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const SOURCE = path.join(__dirname, 'clash-b', 'rulesets', 'blue', 'units', 'RHastatus');

// The size and sha256 of the game's rle.mmp.
function baseOf(gameDir) {
	const file = path.join(gameDir, 'rle.mmp');
	if (!fs.existsSync(file)) throw new Error(`"${gameDir}" has no rle.mmp: it is not the game's folder`);
	const sha = crypto.createHash('sha256');
	const fd = fs.openSync(file, 'r');
	const buf = Buffer.alloc(1 << 22);
	let size = 0, n;
	while ((n = fs.readSync(fd, buf, 0, buf.length, size)) > 0) {
		sha.update(buf.subarray(0, n));
		size += n;
	}
	fs.closeSync(fd);
	return { size, sha256: sha.digest('hex') };
}

// A .rle ("IMGRLE", cells with "RLE2" and their pixels inline) as the game's .rle.mmp: the pixels of each cell go to the tail (4-byte aligned) and the cell keeps
// "pamm" and the offset of them in the combined rle.mmp. tail is a { chunks: [], length } the pixels are appended to; baseSize is the game's rle.mmp.
function toMmp(rle, baseSize, tail) {
	if (rle.toString('latin1', 0, 6) !== 'IMGRLE') throw new Error('not a .rle (IMGRLE signature missing)');
	const cols = rle.readUInt32LE(18), rows = rle.readUInt32LE(22);
	const out = [];
	let p = 26, copied = 0;
	for (let i = 0; i < cols * rows; i++) {
		const x0 = rle.readInt32LE(p), y0 = rle.readInt32LE(p + 4), x1 = rle.readInt32LE(p + 8), y1 = rle.readInt32LE(p + 12);
		p += 16;
		if (x0 > x1 || y0 > y1) continue;
		if (rle.toString('latin1', p, p + 4) !== 'RLE2') throw new Error('RLE2 missing in cell ' + i);
		const size = rle.readUInt32LE(p + 14);
		if (rle.toString('latin1', p + 30, p + 34) === 'pamm') throw new Error('the sheet already points into an rle.mmp');
		out.push(rle.subarray(copied, p + 30));
		const pad = (4 - (tail.length % 4)) % 4;
		if (pad) {
			tail.chunks.push(Buffer.alloc(pad));
			tail.length += pad;
		}
		const offset = Buffer.alloc(8);
		offset.write('pamm', 0, 'latin1');
		offset.writeUInt32LE(baseSize + tail.length, 4);
		out.push(offset);
		tail.chunks.push(rle.subarray(p + 30, p + 30 + size));
		tail.length += size;
		p += 30 + size;
		copied = p;
	}
	out.push(rle.subarray(copied));
	return Buffer.concat(out);
}

function header(baseSize, sha256) {
	const head = Buffer.alloc(56);
	head.write('IMPRTAIL', 0, 'latin1');
	head.writeUInt32LE(1, 8);
	head.writeUInt32LE(56, 12);
	head.writeBigUInt64LE(BigInt(baseSize), 16);
	Buffer.from(sha256, 'hex').copy(head, 24);
	return head;
}

function make(base, outDir, sourceDir) {
	const layer = path.join(outDir, 'rulesets', 'tailed');
	const units = path.join(layer, 'units', 'RHastatus');
	fs.mkdirSync(units, { recursive: true });
	const tail = { chunks: [], length: 0 };
	let sheets = 0;
	for (const name of fs.readdirSync(sourceDir).sort()) {
		const file = path.join(sourceDir, name);
		if (/\.rle$/i.test(name)) {
			fs.writeFileSync(path.join(units, name + '.mmp'), toMmp(fs.readFileSync(file), base.size, tail));
			sheets++;
		}
		else if (/\.ent\.xml$/i.test(name)) {
			// the sheets are named without their extension, as the game's own entities do: the game looks for X.rle and takes X.rle.mmp
			fs.writeFileSync(path.join(units, name), fs.readFileSync(file, 'utf8').replace(/file="([^"]+)\.rle"/g, 'file="$1"'));
		}
	}
	if (!sheets) throw new Error(`no .rle in ${sourceDir}`);
	fs.writeFileSync(path.join(layer, 'rle.mmp.tail'), Buffer.concat([header(base.size, base.sha256), ...tail.chunks]));
	return { layer, sheets, bytes: tail.length };
}

module.exports = { make, toMmp, header, baseOf, SOURCE };

if (require.main === module) {
	const [game, out] = process.argv.slice(2);
	if (!game) {
		console.error('usage: node make-tailed-ruleset.js "<game folder>" ["<folder with clash-b>"]');
		process.exit(2);
	}
	try {
		const r = make(baseOf(game), out || path.join(__dirname, 'clash-b'), SOURCE);
		console.log(`${r.layer}: ${r.sheets} sheets, ${(r.bytes / 1048576).toFixed(2)} MB of pixels in rle.mmp.tail`);
	}
	catch (e) {
		console.error(e.message);
		process.exit(1);
	}
}
