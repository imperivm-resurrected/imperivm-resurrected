// Checks the pure parts of the Imperivm I converter with made-up data (no game files needed):
//   node tools/imperivm1/selfcheck.js
'use strict';
const { readHpfs, writeHpfs } = require('./lib/hpfs');
const G = require('./lib/grid');
const S = require('./lib/script');
const X = require('./lib/xml');
const B = require('./lib/bmp');
const { retranslate, convertNotes, convertMapXml } = require('./lib/adventure');

let failures = 0;
let checks = 0;
function check(cond, what) {
	checks++;
	if (!cond) {
		failures++;
		console.log('FAIL: ' + what);
	}
}

// HPFS: write -> read gives back the same files, with long streams (index pages), nested folders and folder names that only
// differ in case merged.
{
	const big = Buffer.alloc(512 * 300 + 17);
	for (let i = 0; i < big.length; i++) big[i] = (i * 7 + (i >> 9)) & 255;
	const huge = Buffer.alloc(512 * 126 * 130 + 5, 0xab);
	const files = new Map([
		['game.xml', Buffer.from('<game/>')],
		['Maps/1/map.xml', Buffer.from('<map name="Map1"/>')],
		['maps/1/Terrain.terrain.grid', big],
		['Local/Spanish/Maps/1/conversations/cnv0_phrase0.wav', huge],
		['empty.txt', Buffer.alloc(0)],
	]);
	const pkg = writeHpfs(files);
	check(pkg.toString('latin1', 0, 4) === 'HPFS' && pkg.readUInt32LE(4) === 512, 'HPFS header');
	check(pkg.readUInt32LE(8) * 512 === pkg.length, 'HPFS page count');
	check(pkg.readUInt32LE(0x1c) === 1, 'HPFS root folder on page 1');
	const back = readHpfs(pkg);
	const got = new Map(back.entries.filter((e) => !e.directory).map((e) => [e.path.toLowerCase(), e.data]));
	check(got.size === files.size, 'HPFS file count');
	for (const [p, data] of files) check(got.has(p.toLowerCase()) && got.get(p.toLowerCase()).equals(data), 'HPFS round trip of ' + p);
	const dirs = back.entries.filter((e) => e.directory).map((e) => e.path);
	check(dirs.filter((d) => d.toLowerCase() === 'maps').length === 1, 'HPFS merges folders that differ in case');
	const root = back.entries.filter((e) => !e.path.includes('/')).map((e) => e.path);
	check(root.join(',') === 'empty.txt,game.xml,Local,Maps', 'HPFS sorts names without case: ' + root.join(','));
	let threw = false;
	try {
		writeHpfs(new Map([['a.xml', Buffer.alloc(1)], ['A.XML', Buffer.alloc(1)]]));
	} catch (e) {
		threw = true;
	}
	check(threw, 'HPFS refuses two files whose names differ only in case');
}

// Grids: a 4-bit terrain grid is widened to 8 bits with its layers remapped; decor types are renumbered.
{
	const cells = [0, 1, 2, 5, 9, 15, 3, 4];
	const g4 = Buffer.concat([G.header(64, 4, 128, 256), Buffer.from([cells[0] | (cells[1] << 4), cells[2] | (cells[3] << 4), cells[4] | (cells[5] << 4), cells[6] | (cells[7] << 4)])]);
	const remap = [0, 1, 0, 3, 4, 4, 6, 7, 8, 5, 10, 11, 12, 13, 14, 15];
	const r = G.widenTerrain(g4, remap);
	const g8 = G.readGrid(r.buf);
	check(g8.bits === 8 && g8.width === 128 && g8.height === 256 && r.buf.length === 20 + 8, 'terrain grid header after widening');
	check([...G.cellsOf(g8)].join(',') === cells.map((c) => remap[c]).join(','), 'terrain cells remapped in order (low nibble first)');
	let threw = false;
	try {
		G.widenTerrain(g4, [0, 1]);
	} catch (e) {
		threw = true;
	}
	check(threw, 'a terrain layer without equivalent is an error');
	const d = Buffer.concat([G.header(64, 16, 128, 64), Buffer.from([0x02, 0xbb, 0x00, 0x00])]);
	const dr = G.remapDecor(d, { 2: 7 });
	check(dr.changed === 1 && dr.buf.readUInt16LE(20) === 0xbb07 && dr.buf.readUInt16LE(22) === 0, 'decor type renumbered, variant kept');
}

// Scripts.
{
	const r1 = S.renameFunctions('// void\nPlaceEx("A", 1, 2, 3); x.PlaceEx(1); // PlaceEx(\n"PlaceEx(";', { PlaceEx: '_PlaceEx' });
	check(r1.count === 1 && r1.src.startsWith('// void\n_PlaceEx("A"') && r1.src.includes('x.PlaceEx(1)') && r1.src.includes('// PlaceEx(') && r1.src.includes('"PlaceEx("'), 'rename only free calls outside comments and strings');

	const src = '//void\n\nint n;\nSpawnGroup("T_A");\nif (GetDifficulty() != 0)\n\tReduceGroup("T_A" + n);\n//ReduceGroup("x");\n';
	const r2 = S.expandReduceGroup(src);
	check(r2.count === 1, 'one ReduceGroup expanded');
	check(/^\/\/void\nObjList i1rg_list;\nint i1rg_i, i1rg_n;\n/.test(r2.src), 'ReduceGroup variables declared after the signature');
	check(r2.src.includes('if (GetDifficulty() != 0)\n\t{ i1rg_list = Group("T_A" + n).GetObjList();'), 'ReduceGroup becomes one block, so it stays the body of the if');
	check(r2.src.includes('(i1rg_list.count * (2 - GetDifficulty())) / 3') && r2.src.includes('//ReduceGroup("x");'), 'ReduceGroup amount and comments kept');
	check(!S.listCalls(r2.src).some((c) => c.name === 'ReduceGroup'), 'no ReduceGroup call left');

	const r3 = S.rewriteRituals('NO_T.obj.ExecCmd("golden_rain", p, o, false);\nwhile ( NO_T.obj.AsBuilding().IsSpellCasted() ) Sleep(1);\nif (NO_K.obj.AsBuilding().IsSpellCasted() == false) x();', { golden_rain: 120000 });
	check(r3.rituals === 1 && r3.checks === 2, 'rituals and checks counted');
	check(r3.src.startsWith('EnvWriteInt("/I1Ritual/NO_T", GetTime() + 120000) /* was NO_T.obj.ExecCmd("golden_rain", p, o, false) */;'), 'timed ritual becomes a timer');
	check(r3.src.includes('while ( (GetTime() < EnvReadInt("/I1Ritual/NO_T")) )'), 'IsSpellCasted of a timed ritual checks the timer');
	check(r3.src.includes('if ((NO_K.obj.AsBuilding().command != "idle") == false)'), 'other IsSpellCasted checks the command');

	const druid = '// void\nNamedObj NO_D, NO_P;\nNO_D.SetCommand( "hide" , NO_L.obj );\nNO_D.SetCommand("mist", AreaCenter("A"));\nNO_P.SetCommand("mist", AreaCenter("B"));\nGetNamedObj("X" + n).SetCommand("mist", pt);\nq.u.SetCommand("mist", pt);\n';
	const r4 = S.rewriteDruidCommands(druid, (receiver) => receiver === 'NO_D' || receiver === 'q.u');
	check(r4.counts.hide === 1 && r4.counts.mist === 1, 'druid commands rewritten only where the class lacks them');
	check(r4.src.includes('NO_L.obj.SetVisible(false) /* was'), 'hide becomes SetVisible(false)');
	check(r4.src.includes('Place("Mist", AreaCenter("A"), NO_D.obj.player).SetStamina(10)'), 'mist becomes Place("Mist")');
	check(r4.src.includes('NO_P.SetCommand("mist", AreaCenter("B"));'), 'a class that can cast mist keeps the command');
	check(r4.skipped.length === 1, 'a receiver of unknown type is reported');

	const r5 = S.rewriteCall('PlayMovie("packs\\\\data100.bfhp");\nif (x) PlayMovie("a");\nb = PlayMovie("c");', 'PlayMovie', () => null);
	check(r5.src.startsWith('/* PlayMovie("packs\\\\data100.bfhp") (Imperivm I only) */;'), 'a movie statement becomes a comment and keeps its ;');
	check(r5.src.includes('if (x) /* PlayMovie("a") (Imperivm I only) */;'), 'an if body stays a statement');
	check(r5.src.includes('b = /* PlayMovie("c") (Imperivm I only) */ false;'), 'a movie call inside an expression becomes false');

	const r6 = S.replaceStringLiterals('a("TeutonWolf"); b(\'TeutonWolf\'); c("TeutonWolfX"); // "TeutonWolf"', { TeutonWolf: 'TTeutonWolf' });
	check(r6.hits.TeutonWolf === 2 && r6.src.includes('"TTeutonWolf"') && r6.src.includes("'TTeutonWolf'") && r6.src.includes('"TeutonWolfX"') && r6.src.includes('// "TeutonWolf"'), 'class names replaced only in whole string literals');
}

// XML.
{
	const doc = X.parseXml('\t<a\r\n\t\tx="1"\r\n\t\ts="//void\\n\\nif (a \\l b) x(\\\'q\\\');">\r\n\t\t<b y="2"/>\r\n\t</a>\r\n');
	check(doc.root.name === 'a' && doc.root.children[0].attrs.get('y') === '2', 'parse game XML');
	check(X.gameUnescape(doc.root.attrs.get('s')) === "//void\n\nif (a < b) x('q');", 'game escapes decoded');
	check(X.gameEscape("if (a < b && c > d) x(\"q\");\n") === "if (a \\l b \\a\\a c \\g d) x(\\'q\\');\\n", 'game escapes encoded');
	for (const bad of ['<a x="1">', '<a x="1"></b>', '<a x=1/>', '<a x="1" x="2"/>', '<a x="<"/>']) {
		let threw = false;
		try {
			X.parseXml(bad);
		} catch (e) {
			threw = true;
		}
		check(threw, 'bad XML refused: ' + bad);
	}
	const loc = '<translationtable><translationtableentry text="Hi" map="Map1" sound="Map1/conversations/CNV0_PHRASE0.WAV" result="Hola"/></translationtable>';
	const en = retranslate(loc, (t) => t, (p) => p.replace('Map1', '7').toLowerCase());
	check(en.includes('result="Hi"') && en.includes('sound="7/conversations/cnv0_phrase0.wav"'), 'translation table to English with the voice path of the map id');
	check(!retranslate(loc, (t, r) => r, () => null).includes('sound='), 'voice attribute dropped');
	const notes = convertNotes('\t<notes>\r\n\t\t<note\r\n\t\t\tid="Go."\r\n\t\t\ttext="Go there."\r\n\t\t\ticon="assets/entities/notifications/note.rle"\r\n\t\t\tmap="Map1"\r\n\t\t\tlocationx="5"\r\n\t\t\tlocationy="6"/>\r\n\t</notes>\r\n', { hasGameres: () => false }, { add() {}, count() {} }, 'Notes.xml');
	const n = X.parseXml(notes).root.children[0].attrs;
	check(n.get('title') === 'Go.' && n.get('show_on_minimap') === '1' && n.get('icon') === '', 'notes get a title and show_on_minimap, and lose the Imperivm I icon');
	const map = X.parseXml(convertMapXml('\t<map\r\n\t\tname="Map1"\r\n\t\tdescr="">\r\n\t</map>\r\n')).root;
	check(map.attrs.get('persist_state') === '1', 'map.xml gets persist_state="1"');
}

// BMP: write -> read, cover resize, key colour.
{
	const img = { width: 4, height: 2, rgb: Buffer.from([255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]) };
	const back = B.readBmp(B.writeBmp24(img));
	check(back.width === 4 && back.height === 2 && back.rgb.equals(img.rgb), 'BMP 24-bit round trip');
	const small = B.coverResize(img, 2, 2);
	check(small.width === 2 && small.height === 2 && small.rgb.length === 12, 'cover resize size');
	check(B.fillKey(img, [1, 1, 1]).rgb.slice(3, 6).equals(Buffer.from([1, 1, 1])), 'green key painted');
}

console.log(`${checks - failures} of ${checks} checks passed`);
process.exitCode = failures ? 1 : 0;
