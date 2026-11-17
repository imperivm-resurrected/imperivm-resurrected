'use strict';
// sdk/tools/make-pak.js and lib/pakwrite.js: a pak made by them is read back by the readers the SDK has for the game's own paks (lib/assetclass.js), with the
// names the game finds files by, and has the header the game's paks have.
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const assets = require('../sdk/tools/lib/assetclass');
const { buildPak, packName } = require('../sdk/tools/lib/pakwrite');

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'imperivm-pak-'));
const write = (rel, text) => {
	const f = path.join(temp, 'src', rel);
	fs.mkdirSync(path.dirname(f), { recursive: true });
	fs.writeFileSync(f, text);
};
write('ui/icons/RHastatus.bmp', 'BM-picture');
write('Sounds/selection/Baraks/Barraks_1.wav', 'RIFF-one');
write('Sounds/selection/Baraks/Barraks_2.wav', 'RIFF-two');
write('data/randommap/Packed Continent.rm.xml', '<map-template name="x"/>');
write('CurrentLang/game.loc.xml', '<translationtable/>');

const out = path.join(temp, 'packs', 'x.pak');
const run = spawnSync(process.execPath, [path.join(__dirname, '..', 'sdk', 'tools', 'make-pak.js'), path.join(temp, 'src'), out], { encoding: 'utf8' });
assert.equal(run.status, 0, run.stdout + run.stderr);
assert.match(run.stdout, /5 members/);

const buf = fs.readFileSync(out);
assert.equal(buf.toString('latin1', 0, 15), 'HMMSYS PackFile');
assert.equal(buf[15], 0x0a);
assert.equal(buf[16], 0x1a);
assert.equal(buf.readUInt32LE(0x20), 5);
// the directory length of the header is where the directory ends, and the data starts after it
const dirEnd = 0x28 + buf.readUInt32LE(0x24);
assert.ok(dirEnd < buf.length);

const names = assets.pakNames(out);
assert.deepEqual(names, ['CURRENTLANG\\GAME.LOC.XML', 'DATA\\RANDOMMAP\\PACKED CONTINENT.RM.XML', 'SOUNDS\\SELECTION\\BARAKS\\BARRAKS_1.WAV',
	'SOUNDS\\SELECTION\\BARAKS\\BARRAKS_2.WAV', 'UI\\ICONS\\RHASTATUS.BMP']);
assert.equal(assets.pakEntry(out, 'ui/icons/rhastatus.bmp').toString(), 'BM-picture');
assert.equal(assets.pakEntry(out, 'sounds/selection/baraks/barraks_2.wav').toString(), 'RIFF-two');
assert.equal(assets.pakEntry(out, 'data/randommap/packed continent.rm.xml').toString(), '<map-template name="x"/>');
assert.equal(assets.pakEntry(out, 'nothing/here'), null);

// names: upper case, backslashes, no leading slash; what the game cannot read is refused; two members of one name are refused
assert.equal(packName('/ui/Icons/a.bmp'), 'UI\\ICONS\\A.BMP');
assert.throws(() => packName('ui/\u00e9.bmp'), /cannot be in a pak/);
assert.throws(() => packName('a'.repeat(256)), /cannot be in a pak/);
assert.throws(() => buildPak([{ name: 'a/b', data: Buffer.alloc(1) }, { name: 'A\\B', data: Buffer.alloc(1) }]), /two members/);
assert.equal(buildPak([]).readUInt32LE(0x20), 0);

// the names share their beginning with the one before (the game's format): a long common prefix is not written twice
const big = buildPak([1, 2, 3].map(i => ({ name: `units/rhastatus/anim${i}.rle`, data: Buffer.alloc(4) })));
assert.ok(big.length < 0x28 + 3 * (2 + 25 + 8) + 16 + 12, 'the shared prefix is written once');

const bad = spawnSync(process.execPath, [path.join(__dirname, '..', 'sdk', 'tools', 'make-pak.js'), path.join(temp, 'none'), out], { encoding: 'utf8' });
assert.equal(bad.status, 1);
console.log('pakwrite: ok');
