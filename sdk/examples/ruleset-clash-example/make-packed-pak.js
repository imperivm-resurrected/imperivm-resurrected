'use strict';
// Makes the pak of the `packed` ruleset of clash-b: rulesets\packed\packs\packed.pak, with four kinds of member (an image, the barracks selection sounds, a data
// file and a text). Nothing of it is in the repository: the image, the sounds and the data file are generated here, and the text is the game's own
// CURRENTLANG\GAME.LOC.XML (taken from the game's local\<language>.pak) with one entry added, so it is made from the player's own game, like the price files of clash-c.
//
//   node make-packed-pak.js "<game folder>" ["<folder with clash-b>"] [english|spanish|italian]
const fs = require('fs');
const path = require('path');
const { buildPak } = require('../../tools/lib/pakwrite');
const { pakEntry } = require('../../tools/lib/assetclass');

const RESULTS = { english: 'Equip Hastatus (packed)', spanish: 'Equipar legionario (empaquetado)', italian: 'Equipaggia astato (impacchettato)' };

// A 16-bit 5-5-5 bitmap of w x h: green, with a white diagonal stripe and a dark border (the game's portraits are 65 x 76, drawn through a circular mask).
function portrait(w, h) {
	const row = w * 2, pad = (4 - (row % 4)) % 4, size = 54 + (row + pad) * h;
	const out = Buffer.alloc(size);
	out.write('BM', 0);
	out.writeUInt32LE(size, 2);
	out.writeUInt32LE(54, 10);
	out.writeUInt32LE(40, 14);
	out.writeInt32LE(w, 18);
	out.writeInt32LE(h, 22);
	out.writeUInt16LE(1, 26);
	out.writeUInt16LE(16, 28);
	out.writeUInt32LE((row + pad) * h, 34);
	const rgb = (r, g, b) => (r << 10) | (g << 5) | b;
	for (let y = 0; y < h; y++) {
		for (let x = 0; x < w; x++) {
			let c = rgb(2, 22, 4);
			if (Math.abs(x - (w * y) / h) < 4) c = rgb(31, 31, 31);
			if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) c = rgb(1, 6, 1);
			out.writeUInt16LE(c, 54 + (h - 1 - y) * (row + pad) + x * 2);
		}
	}
	return out;
}

// A mono 16-bit wav at 22050 Hz: two descending notes (the chime of the blue ruleset rises).
function chime() {
	const rate = 22050, notes = [[1175, 0.25], [880, 0.45]];
	const samples = [];
	for (const [freq, seconds] of notes) {
		const n = Math.floor(rate * seconds);
		for (let i = 0; i < n; i++) samples.push(Math.round(9000 * Math.min(1, i / 300) * (1 - i / n) ** 1.5 * Math.sin((2 * Math.PI * freq * i) / rate)));
	}
	const data = Buffer.alloc(samples.length * 2);
	samples.forEach((s, i) => data.writeInt16LE(s, i * 2));
	const head = Buffer.alloc(44);
	head.write('RIFF', 0);
	head.writeUInt32LE(36 + data.length, 4);
	head.write('WAVEfmt ', 8);
	head.writeUInt32LE(16, 16);
	head.writeUInt16LE(1, 20);
	head.writeUInt16LE(1, 22);
	head.writeUInt32LE(rate, 24);
	head.writeUInt32LE(rate * 2, 28);
	head.writeUInt16LE(2, 32);
	head.writeUInt16LE(16, 34);
	head.write('data', 36);
	head.writeUInt32LE(data.length, 40);
	return Buffer.concat([head, data]);
}

const MAP_TYPE = `<?xml version="1.0"?>

<map-template name="Packed Continent" >

	<bitmap file="RandomMap/Continent/"/>

	<transform allow-rotate="1" allow-mirror="1" />

	<distortion amp="0.2" first-oct="2" number-octs="4" persist="0.5"/>

	<color-code red="0"   green="0"   blue="0"   meaning="sea"/>
	<color-code red="255" green="255" blue="255" meaning="land"/>
	<color-code red="255" green="0"   blue="0"   meaning="player-stronghold"/>
	<color-code red="255"   green="0" blue="255"   meaning="mountain"/>

	<limit map-size="small" max-players="2" no_ruins="1"/>
	<limit map-size="medium" max-players="6" no_ruins="1"/>
	<limit map-size="large" max-players="8" no_ruins="1"/>

</map-template>
`;

// The game's own table of texts of a language with the entry of the button of the hastatus added (an entry later in the file wins).
function withText(table, result) {
	const text = table.toString('utf8');
	const close = text.toLowerCase().lastIndexOf('</translationtable');
	if (close < 0) throw new Error('the game\'s game.loc.xml does not end with </translationtable>');
	return Buffer.from(text.slice(0, close) + `<translationtableentry text="Equip Hastatus" result="${result}"/>\r\n` + text.slice(close), 'utf8');
}

function make(gameDir, outDir, language) {
	const members = [{ name: 'ui/icons/RHastatus.bmp', data: portrait(65, 76) }, { name: 'data/randommap/Packed Continent.rm.xml', data: Buffer.from(MAP_TYPE) }];
	for (let i = 1; i <= 4; i++) members.push({ name: `sounds/selection/baraks/barraks_${i}.wav`, data: chime() });
	const pak = path.join(gameDir, 'local', language + '.pak');
	const table = fs.existsSync(pak) ? pakEntry(pak, 'currentlang/game.loc.xml') : null;
	if (table) members.push({ name: 'currentlang/game.loc.xml', data: withText(table, RESULTS[language]) });
	const target = path.join(outDir, 'rulesets', 'packed', 'packs', 'packed.pak');
	fs.mkdirSync(path.dirname(target), { recursive: true });
	fs.writeFileSync(target, buildPak(members));
	return { target, text: !!table, members: members.length };
}

module.exports = { make, portrait, chime, withText, MAP_TYPE };

if (require.main === module) {
	const [game, out, language] = process.argv.slice(2);
	if (!game) {
		console.error('usage: node make-packed-pak.js "<game folder>" ["<folder with clash-b>"] [english|spanish|italian]');
		process.exit(2);
	}
	const lang = language || 'english';
	if (!RESULTS[lang]) {
		console.error('the language is english, spanish or italian');
		process.exit(2);
	}
	try {
		const r = make(game, out || path.join(__dirname, 'clash-b'), lang);
		console.log(`${r.target}: ${r.members} members${r.text ? '' : ' (no text: the game has no local\\' + lang + '.pak with CURRENTLANG\\GAME.LOC.XML)'}`);
	}
	catch (e) {
		console.error(e.message);
		process.exit(1);
	}
}
