'use strict';
// sdk/examples/ruleset-clash-example/make-tailed-ruleset.js: the .rle of the cube soldier become .rle.mmp descriptors whose "pamm" offsets point into an rle.mmp.tail that
// has the header the DLL reads (RleTailParse: size and sha256 of the game's rle.mmp) and the same pixels, 4-byte aligned, after the game's rle.mmp.
const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');
const maker = require('../sdk/examples/ruleset-clash-example/make-tailed-ruleset.js');

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'imperivm-tail-'));
const game = path.join(temp, 'game');
fs.mkdirSync(game);
const fake = crypto.randomBytes(1000);
fs.writeFileSync(path.join(game, 'rle.mmp'), fake);
const base = maker.baseOf(game);
assert.equal(base.size, 1000);
assert.equal(base.sha256, crypto.createHash('sha256').update(fake).digest('hex'));

const out = path.join(temp, 'clash-b');
const r = maker.make(base, out, maker.SOURCE);
assert.equal(r.sheets, 12);
const layer = path.join(out, 'rulesets', 'tailed');
const tail = fs.readFileSync(path.join(layer, 'rle.mmp.tail'));
assert.equal(tail.toString('latin1', 0, 8), 'IMPRTAIL');
assert.equal(tail.readUInt32LE(8), 1);
assert.equal(tail.readUInt32LE(12), 56);
assert.equal(tail.readBigUInt64LE(16), 1000n);
assert.equal(tail.subarray(24, 56).toString('hex'), base.sha256);
assert.equal(tail.length, 56 + r.bytes);

// every cell of every descriptor points at the bytes of the original cell inside the tail, and the entity names the sheets without ".rle"
let cells = 0;
for (const name of fs.readdirSync(maker.SOURCE).filter(f => /\.rle$/i.test(f))) {
	const src = fs.readFileSync(path.join(maker.SOURCE, name));
	const mmp = fs.readFileSync(path.join(layer, 'units', 'RHastatus', name + '.mmp'));
	assert.ok(mmp.length < src.length, name + ' has no pixels inside any more');
	const count = src.readUInt32LE(18) * src.readUInt32LE(22);
	let p = 26, q = 26;
	for (let i = 0; i < count; i++) {
		const box = src.subarray(p, p + 16);
		assert.ok(box.equals(mmp.subarray(q, q + 16)));
		p += 16;
		q += 16;
		if (box.readInt32LE(0) > box.readInt32LE(8) || box.readInt32LE(4) > box.readInt32LE(12)) continue;
		const size = src.readUInt32LE(p + 14);
		assert.ok(src.subarray(p, p + 30).equals(mmp.subarray(q, q + 30)), 'the cell header is the same');
		assert.equal(mmp.toString('latin1', q + 30, q + 34), 'pamm');
		const offset = mmp.readUInt32LE(q + 34);
		assert.ok(offset >= 1000 && offset % 4 === 0 || (offset - 1000) % 4 === 0, 'in the tail, aligned');
		assert.ok(src.subarray(p + 30, p + 30 + size).equals(tail.subarray(56 + offset - 1000, 56 + offset - 1000 + size)), 'the pixels are where the cell says');
		p += 30 + size;
		q += 38;
		cells++;
	}
	assert.equal(src.subarray(p).compare(mmp.subarray(q)), 0, 'what follows the cells (the palette) is the same');
}
assert.ok(cells > 50);
const ent = fs.readFileSync(path.join(layer, 'units', 'RHastatus', 'RHastatus.ent.xml'), 'utf8');
assert.ok(!/\.rle"/.test(ent) && /file="Idle"/.test(ent) && /file="Walk_shadow"/.test(ent));

// a sheet that already points into an rle.mmp, or is not a sheet, is refused
assert.throws(() => maker.toMmp(Buffer.from('nothing'), 1000, { chunks: [], length: 0 }), /IMGRLE/);
assert.throws(() => maker.baseOf(path.join(temp, 'no-game')), /no rle\.mmp/);
console.log('tailexample: ok');
