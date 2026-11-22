// Checks the Imperivm I converter: its parts with made-up data, and the whole conversion when both games are installed:
//   node tools/imperivm1/selfcheck.js
'use strict';
const { readHpfs, writeHpfs } = require('./lib/hpfs');
const G = require('./lib/grid');
const S = require('./lib/script');
const X = require('./lib/xml');
const B = require('./lib/bmp');
const { retranslate, cleanTable, convertNotes, convertMapXml } = require('./lib/adventure');

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
	const withDirs = readHpfs(writeHpfs(new Map([['game.xml', Buffer.from('x')]]), 512, ['Conversations', 'Resources']));
	check(['Conversations', 'Resources'].every((d) => withDirs.entries.some((e) => e.directory && e.path === d)), 'HPFS keeps empty folders');
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

	const src =
		'//void\n\nint n;\nwhile (1)\n{\n\tif (T_A.count < 5)\n\t{\n\t\tS.AddUnits(SpawnGroupInHolder("T_A" + n, "S_Tent"));\n' +
		'\t\tif (GetDifficulty() != 0)\n\t\t\tReduceGroup( "T_A" +n);\n\t}\n}\nReduceGroup("T_B");\n//ReduceGroup("x");\n';
	const r2 = S.expandReduceGroup(src);
	check(r2.count === 2, 'both ReduceGroup calls expanded');
	check(/^\/\/void\nObjList i1rg_list;\nint i1rg_i, i1rg_n;\n/.test(r2.src), 'ReduceGroup variables declared after the signature');
	check(r2.src.includes('if (GetDifficulty() != 0)\n\t\t\t{ i1rg_list = Group("T_A" +n).GetObjList();'), 'ReduceGroup becomes one block, so it stays the body of the if');
	check(r2.src.includes('i1rg_n = (i1rg_list.count * (2 - GetDifficulty())) / 3;'), 'two thirds, one third or none of the whole group');
	check(r2.src.includes('if (i1rg_list[i1rg_i].AsUnit().IsValid() && !i1rg_list[i1rg_i].AsHero().IsValid()) { i1rg_list[i1rg_i].Erase();'), 'the first units that are not heroes are erased');
	check(r2.src.includes('{ i1rg_list = Group("T_B").GetObjList();'), 'a ReduceGroup with no SpawnGroup before it reduces the group too');
	check(r2.src.includes('//ReduceGroup("x");'), 'comments kept');
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

	const conv = '//void\nConversation C_Conv , c_once;\nint n;\nC_Conv.Init("A");\nC_Conv.Run();\nif (n) c_once.Run();\nother.Run();\n';
	const rq = S.queueConversations(conv);
	check(rq.count === 2, 'two conversation runs queued');
	check(/^\/\/void\nint i1cv_w;\nConversation C_Conv , c_once;/.test(rq.src), 'queue variable declared after the signature');
	check(rq.src.includes('{ i1cv_w = 0; while (EnvReadInt("/I1/ConversationBusy") == 1 && i1cv_w < 600) { Sleep(100); i1cv_w += 1; } EnvWriteInt("/I1/ConversationBusy", 1); C_Conv.Run(); EnvWriteInt("/I1/ConversationBusy", 0); }'), 'a run waits, marks and clears');
	check(rq.src.includes('if (n) { i1cv_w = 0;') && rq.src.includes('other.Run();'), 'a run in an if stays its body; other objects are left alone');
	check(S.queueConversations('//void\nint n;\nx.Run();\n').count === 0, 'scripts without conversations are left alone');

	const hs = '//void\nif (x) SpawnGroupInHolder("G1", "S");\nSpawnGroupInHolder("G2", "S");\nSpawnGroupInHolder("G1", "T").SetCommand("move", p);\n\tNO_P.SetPlayer(1);\nMoveToArea(G1, "A");\n';
	const ho = S.ownHolderSpawns(hs, 'G1', 4);
	check(ho.count === 1 && ho.src.includes('if (x) { SpawnGroupInHolder("G1", "S"); Group("G1").SetPlayer(4); }'), 'a spawn into a holder is given to the owner in the same statement');
	check(ho.src.includes('SpawnGroupInHolder("G2", "S");') && ho.src.includes('SpawnGroupInHolder("G1", "T").SetCommand('), 'other groups and spawns inside expressions are left alone');
	const hb = S.insertAtLine(ho.src, /^\s*NO_P\.SetPlayer\(1\);/m, 'after', 'Group("G1").SetPlayer(1);');
	check(hb.includes('\tNO_P.SetPlayer(1);\n\tGroup("G1").SetPlayer(1);\nMoveToArea'), 'the group is handed back after the anchor, with its indentation');
	check(S.insertAtLine(hs, /^MoveToArea/m, 'before', 'X();').includes('X();\nMoveToArea'), 'a line can go before the anchor');
	check(S.insertAtLine(hs, /^Nothing/m, 'after', 'X();') === null, 'a missing anchor is reported');

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
	const table = cleanTable('<translationtable><translationtableentry text="" result=""/><translationtableentry text="A" result="1"/><translationtableentry text="A" result="2"/><translationtableentry text="B" result="3"/></translationtable>');
	check(table.empty === 1 && table.duplicate === 1 && table.src === '<translationtable><translationtableentry text="A" result="1"/><translationtableentry text="B" result="3"/></translationtable>', 'translation table without empty or repeated texts');
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

// "attack" given a point becomes "advance"; given an object it stays "attack".
{
	const src = [
		'//void',
		'point p_P1;',
		'NamedObj NO_Larax;',
		'ObjList OL_Enemy;',
		'int n_I;',
		'A.SetCommand("attack", AreaCenter("A_Gate"));',
		'B.SetCommand("attack", p_P1);',
		'C.SetCommand("attack", NO_Larax.obj.posRH);',
		'D.SetCommand("attack", AreaCenter("A") - Point(100, 400));',
		'E.SetCommandOffset("attack", AreaCenter("R") + Point(0, 3000));',
		'F.AddCommand(true, "attack", OL_Enemy[0].pos);',
		'G.SetCommand("attack", NO_Larax);',
		'H.SetCommand("attack", OL_Enemy[n_I]);',
		'I.SetCommand("attack", NO_Protector2);',
		'J.SetCommand("attack", Larax.obj.AsUnit());',
		'K.SetCommand("attack", n_I);',
		'L.SetCommand("move", p_P1);',
	].join('\n');
	const r = S.attackPointsToAdvance(src);
	const lines = r.src.split('\n');
	check(r.advanced === 6 && r.kept === 4 && r.unknown.length === 1, `attack: 6 points, 4 objects, 1 unknown (got ${r.advanced}/${r.kept}/${r.unknown.length})`);
	check(['A', 'B', 'C', 'D', 'E', 'F'].every((x) => lines.some((l) => l.startsWith(x + '.') && l.includes('"advance"'))), 'attack with a point written advance');
	check(['G', 'H', 'I', 'J', 'K'].every((x) => lines.some((l) => l.startsWith(x + '.') && l.includes('"attack"'))), 'attack with an object (or unknown) kept');
	check(lines.some((l) => l === 'L.SetCommand("move", p_P1);'), 'other commands untouched');
	check(S.targetKind('Caesar.obj.pos', new Map()) === 'point' && S.targetKind('Q_X.GetObjList()[0]', new Map()) === 'unknown', 'target kinds');
}

// Settlements get extrasentries="0" (Imperivm I has no sentries); a map sequence is added as the next seqN.vs.
{
	const A = require('./lib/adventure');
	const report = new A.Report();
	const classes = {
		resolve: (id) => ({ to: id, substituted: false }),
		gbrCpp: (id) => (id === 'RArch' ? 'CVXDecor' : 'CVXUnit'),
		i1DisplayName: () => null,
		gbrDisplayName: () => null,
	};
	const objects = [
		'\t<mapobject>',
		'\t\t<settlement',
		'\t\t\tid="1"',
		'\t\t\tplayer="5"',
		'\t\t\tmaxpopulation="0"',
		'\t\t\tname="S">',
		'\t<scriptobj',
		'\t\tclass="RArch"',
		'\t\tnum="7"',
		'\tplayer="5"',
		'\tflags="0x80800010"/>',
		'\t\t</settlement>',
		'\t</mapobject>',
		'',
	].join('\r\n');
	const t = A.convertObjects(objects, classes, { hasGameres: () => false }, report, 'map.obj.xml');
	const doc = X.parseXml(t).root;
	const sett = doc.children.find((c) => c.name === 'settlement');
	const arch = doc.children.find((c) => c.name === 'scriptobj');
	check(sett && sett.attrs.get('extrasentries') === '0', 'settlement given extrasentries="0"');
	check(arch && arch.attrs.get('flags') === '0x80000000' && !arch.attrs.has('player') && !sett.children.length, 'decor taken out of its settlement, without player');
	const out = new Map([['Maps/7/Sequences/sequences.xml', Buffer.from('\t<sequences>\r\n\t\t<sequence\r\n\t\t\tname="A"\r\n\t\t\twizard=""\r\n\t\t\tscript="CurrentMap/sequences/seq4.vs">\r\n\t\t</sequence>\r\n\t</sequences>\r\n')], ['Maps/7/Sequences/seq4.vs', Buffer.from('//void')]]);
	const p = A.addMapSequence(out, '7', 'I1_Census', '//void');
	const seqs = X.parseXml(out.get('Maps/7/Sequences/sequences.xml').toString()).root.children;
	check(p === 'Maps/7/Sequences/seq5.vs' && seqs.length === 2 && seqs[1].attrs.get('script') === 'CurrentMap/sequences/seq5.vs', 'map sequence added as the next seqN.vs');
	const table = A.addEntries('<translationtable/>', [{ text: 'NO_Teuton5', result: 'Jinete teutón' }]);
	check(/<translationtableentry id="actor name" map="" text="NO_Teuton5" result="Jinete teutón"\/>/.test(table), "actor name entry added to an empty table, in the format of Great Battles of Rome's adventure tables");
	const names = A.classNameTable(Buffer.from('<translationtable><translationtableentry text="Teuton Raider" context="TeutonWolf class" result="Jinete teutón"/></translationtable>'), 'class');
	check(names.get('teutonwolf') === 'Jinete teutón', 'class names read by context');
}

// Waits count only living units; UnitFlags keep only the bits Imperivm I reads.
{
	const A = require('./lib/adventure');
	const src = [
		'//void',
		'WaitEmptyQuery(Group("A"), -1);',
		'if (x) WaitQueryCountBetween(Q, 1, -1, 0);',
		'if ( WaitQueryCountBetween(Q, 0, 3, 500) ) n = 1;',
		'while ( !WaitQueryCountBetween(Group("B"), 0, 0, 1000) && n < 3) {',
		'\tn += 1;',
		'}',
		'else if (WaitEmptyQuery(Q, 0)) n = 2;',
	].join('\n');
	const r = S.livingWaits(src);
	check(r.count === 4 && r.skipped.length === 1 && /else if \(WaitEmptyQuery/.test(r.src), `living waits: 4 rewritten, the one after else left (got ${r.count}/${r.skipped.length})`);
	check(!/^WaitEmptyQuery\(Group\("A"\)/m.test(r.src) && /i1w_l = \(Group\("A"\)\)\.GetObjList\(\)/.test(r.src) && /IsAlive\(\)/.test(r.src), 'a wait statement becomes a loop over living units');
	check(/\{ i1w_t = 0;[^\n]*\} if \( \(i1w_r == 1\) \) n = 1;/.test(r.src), 'a wait in an if is computed just before it');
	check(/while \(1\) \{ \{ i1w_t = 0;[^\n]*if \(!\(!\(i1w_r == 1\) && n < 3\)\) break;/.test(r.src), 'a wait in a while condition runs at each turn');
	check(/^ObjList i1w_l;$/m.test(r.src), 'living waits declare their variables');
	const classes = { resolve: (id) => ({ to: id, substituted: false }), gbrCpp: () => 'CVXUnit', i1DisplayName: () => null, gbrDisplayName: () => null };
	const objs = '\t<mapobject>\r\n\t<scriptobj\r\n\t\tclass="GAxeman"\r\n\t\tnum="1"\r\n\tUnitFlags="262153"\r\n\tplayer="2"\r\n\tflags="0x80400002"/>\r\n\t</mapobject>\r\n';
	const t = A.convertObjects(objs, classes, { hasGameres: () => false }, new A.Report(), 'map.obj.xml');
	check(/UnitFlags="262144"/.test(t), 'UnitFlags low 16 bits cleared (262153 -> 262144)');
	const { readFunctions } = require('./lib/catalog');
	const fns = readFunctions(require('path').join(__dirname, '..', '..', 'sdk', 'game-functions.txt'));
	const bad = S.listCalls(r.src).filter((c) => !['if', 'for', 'while'].includes(c.name) && !(c.method ? fns.methods.has(c.name) : fns.functions.has(c.name)));
	check(!bad.length, `wait scripts call only game functions (${bad.map((c) => c.name).join(', ')})`);
}

// Audit: strings of an executable, calls with their arguments.
{
	const AU = require('./lib/audit');
	const s = AU.exeStrings(Buffer.from('\0persist_state\0x\0Icon\0\x01ab\0'));
	check(s.exact.has('persist_state') && s.exact.has('Icon') && !s.exact.has('x'), 'executable strings');
	const c = AU.calls('A.SetCommand("attack", AreaCenter("X"));\nDiplCeaseFire(1, 2, true);\nB.AddCommand(false, "move", p);');
	const set = c.find((x) => x.name === 'SetCommand');
	const dipl = c.find((x) => x.name === 'DiplCeaseFire');
	const add = c.find((x) => x.name === 'AddCommand');
	check(set && set.args === 2 && set.verb === 'attack' && dipl && dipl.args === 3 && !dipl.method && add.verb === 'move', 'calls, arguments and verbs');
}

// Imperivm I classes: Great Battles of Rome's file with Imperivm I's parent, behaviours, commands and game properties; the
// behaviour scripts with the campaign's classes and Imperivm I's constants.
{
	const IC = require('./lib/i1classes');
	const gbrText =
		'<class id="TTent" altid="TT" cpp_class="CVXTownHall" parent="Building" entity="Buildings/T.ent.xml">\r\n' +
		'\t<properties maxhealth="15000"/>\r\n\t<behavior script="data/subai/ttent_behavior.vs"/>\r\n' +
		'\t<method sig="createfoodmulesmall" vs="data/subai/m.vs"/>\r\n\t<method sig="enter_tent" vs="data/subai/e.vs"/>\r\n' +
		'\t<properties\r\n\t\tcan_be_captured="1"\r\n\t\tsettlement_food="2400"\r\n\t\tinterface="thumb,holder"/>\r\n</class>\r\n';
	const i1Text =
		'<class id="TTent" cpp_class="CVXTownHall" parent="Peaceful" entity="assets/x.ent.xml">\r\n<properties maxhealth="9" can_be_captured="0" ' +
		'settlement_food="0" interface="holder"/>\r\n<!-- <method sig="createfoodmulesmall" vs="x"/> -->\r\n<method sig="enter_tent" vs="data/subai/teuton_enter_tent.vs"/>\r\n</class>\r\n';
	const i1Parent = '<class id="Peaceful">\r\n<method sig="heal" vs="data/subai/druid_heal.vs" verify="data/subai/druid_heal_verify.vs"/>\r\n<method sig="enter_tent" vs="data/subai/other.vs"/>\r\n</class>\r\n';
	const methods = IC.i1Methods([i1Text, i1Parent]);
	check(methods.length === 2 && methods[0].sig === 'enter_tent' && methods[1].sig === 'heal', 'commands of the class, and of the ancestor only those the class does not define');
	check(methods[1].tag.includes('vs="data/mod/imperivm1/druid_heal.vs"') && methods[1].tag.includes('verify="data/mod/imperivm1/druid_heal_verify.vs"') && methods[1].scripts.length === 2, 'command scripts point to the ported ones');
	const p = IC.portScript(
		'x = GetConst("TTentInitialArmy"); y = GetConst("Other"); Place("TeutonMetal", p, 1); PlaceEx(1); t = .FindUnit2HTL(1); l = .FindUnit2HTL(3); ol = ObjsInCircle(.pos, r, "PossessAnimal");',
		{ TeutonMetal: 'I1TeutonRider' },
		IC.readConstants('TTentInitialArmy = 40 ; max\r\n'),
		{ PossessAnimal: ['Crow', 'Deer', 'Wolf'] },
	);
	check(p.inlined === 1 && p.src.includes('x = 40 /*') && p.src.includes('GetConst("Other")') && p.src.includes('Place("I1TeutonRider"') && p.src.includes('_PlaceEx(1)'), 'ported script: constants, classes and functions');
	check(p.src.includes('t = .FindUnitToHeal();') && p.src.includes('l = .FindUnitToLearn();'), 'FindUnit2HTL by mode');
	check(p.src.includes('ol = Union(Union(ObjsInCircle(.pos, r, "Crow"), ObjsInCircle(.pos, r, "Deer")), ObjsInCircle(.pos, r, "Wolf"));'), 'a query by a grouping class becomes the union of its classes');
	const h = IC.buildFromI1Text({ id: 'I1Mist' }, '<class id="Mist" cpp_class="CVXSacrifice" parent="Sacrifice"\r\n   entity="assets/fog.ent.xml"\r\n>\r\n<properties radius="100"/>\r\n<method sig="idle" vs="x"/>\r\n</class>\r\n', 'Visuals/fog.ent.xml', new Set(['sacrifice']), [], [{ sig: 'idle', tag: '<method sig="idle" vs="data/mod/imperivm1/mist_idle.vs"/>' }]);
	check(/^<class id="I1Mist" cpp_class="CVXSacrifice" parent="Sacrifice" entity="Visuals\/fog\.ent\.xml">/.test(h.text) && h.text.includes('mist_idle.vs') && !h.text.includes('vs="x"') && h.text.includes('radius="100"'), "a class made from Imperivm I's file, with Great Battles of Rome's entity");
	const i1Cmds =
		'<cmd name="trainGDruid" button="actions/train druid.bmp" method="train" param="GDruid" sclass="GDruid">\r\n\t<src obj="DruidHouse"/>\r\n</cmd>\r\n' +
		'<cmd name="hide" groupdispatch="data/subai/group_hide.vs" button="actions/hide.bmp">\r\n\t<cmdtext target="Unit" text="t"/>\r\n\t<src obj="GDruid" sticky="yes"/>\r\n\t<src obj="BDruid"/>\r\n\t<nsrc obj="Hero"/>\r\n</cmd>\r\n' +
		'<cmd name="other">\r\n\t<src obj="RPriest"/>\r\n</cmd>\r\n';
	const druid = { id: 'I1GDruid' };
	const house = { id: 'I1GDruidHouse' };
	const fakeGbr = {
		dataNames: () => ['DATA\\COMMANDS\\X.XML'],
		readData: () => Buffer.from('<cmd name="trainGDruid" button="actions/train GDruid.bmp">\r\n</cmd>\r\n<cmd name="hide" button="actions/invisibility.bmp">\r\n</cmd>\r\n'),
		hasGameres: (p) => !/train druid|actions\/hide/.test(p),
	};
	const pc = IC.portCommands(i1Cmds, new Map([['gdruid', [druid]], ['druidhouse', [house]]]), fakeGbr, { GDruid: 'I1GDruid' });
	check(pc.count === 2 && pc.names.join() === 'trainGDruid,hide', "only Imperivm I's commands of the new classes");
	check(pc.text.includes('param="I1GDruid" sclass="I1GDruid"') && pc.text.includes('<src obj="I1GDruidHouse"/>'), 'a training command trains the new class, from the new building');
	check(pc.text.includes('<src obj="I1GDruid" sticky="yes"/>') && !pc.text.includes('BDruid') && !pc.text.includes('nsrc') && pc.text.includes('<cmdtext target="Unit" text="t"/>'), "only the new classes as sources; the command's texts kept");
	check(pc.text.includes('groupdispatch="data/mod/imperivm1/group_hide.vs"') && pc.scripts.join() === 'data/subai/group_hide.vs', "the command's scripts ported");
	check(pc.text.includes('button="actions/train GDruid.bmp"') && pc.text.includes('button="actions/invisibility.bmp"'), "a button picture the game lacks becomes its own command's");

	// A full copy: Great Battles of Rome's file for what the player sees, Imperivm I's ancestry for the rest.
	const gx = '<class id="GX" altid="X" cpp_class="CVXUnit" parent="Melee" entity="u.ent.xml">\n\t<properties maxhealth="200" damage="12" unit_specials="Penetration"/>\n\t<properties icon="g.bmp"/>\n\t<behavior script="data/subai/sneak_behavior.vs"/>\n\t<method sig="glearn" vs="data/subai/druid_learn.vs"/>\n</class>\n';
	const ix = ['<class id="GX" parent="Melee"><properties maxhealth="250" minattack="6" maxattack="16" defense_slash="2" icon="i.bmp"/></class>', '<class id="Melee"><properties range="17" maxhealth="100"/><defaultcmd target="Unit"><cmd name="attack"/></defaultcmd></class>'];
	const fx = IC.buildFullText({ id: 'I1GX' }, gx, ix, 'Melee', ['data/mod/imperivm1/unit_idle.vs'], [{ sig: 'idle', tag: '<method sig="idle" vs="data/mod/imperivm1/unit_idle.vs"/>' }]);
	check(/<class id="I1GX" cpp_class="CVXUnit" parent="Melee" entity="u.ent.xml">/.test(fx.text), 'a full copy has the new id, no altid, and the parent given');
	check(fx.text.includes('maxhealth="250"') && fx.text.includes('range="17"') && fx.text.includes('icon="g.bmp"') && fx.text.includes('damage="12"'), "a full copy takes Imperivm I's properties of the whole ancestry, the nearest winning, and keeps the game's pictures and damage");
	check(fx.text.includes('unit_specials=""') && !fx.text.includes('sneak_behavior') && !fx.text.includes('glearn') && fx.text.includes('unit_idle.vs') && fx.text.includes('<defaultcmd target="Unit">'), "a full copy has no special, only Imperivm I's behaviours and commands, and its right-click commands");
	check(JSON.stringify(fx.stats) === '{"minattack":"6","maxattack":"16","defense_slash":"2"}', "Imperivm I's attack and defence go to the stats file");
	check(IC.portParam('1, 4, GVikingLord, Steel Weapons', { GVikingLord: 'I1GVikingLord' }) === '1, 4, I1GVikingLord, Steel Weapons' && IC.portParam('SetsSet, levels/GSwordsman, 4', { GSwordsman: 'I1GSwordsman' }) === 'SetsSet, levels/I1GSwordsman, 4', "the class names inside a command's param are the campaign's");
	const sp = IC.specialityClasses({ defs: [{ id: 'I2GHorseman', built: { stats: { speciality: 'charge' } } }, { id: 'I2CNoble', built: { stats: { speciality: 'defensive stand,full armor' } } }, { id: 'I2X', built: {} }] });
	const fl = IC.portScript('if (!.GetFlags(spec_charge)) x(); if (u.b.GetFlags(spec_full_armor)) y(); z = .GetFlags(spec_bleeding);', {}, new Map(), {}, 'Imperivm II', sp).src;
	check(fl === 'if (!(.IsHeirOf("I2GHorseman"))) x(); if ((u.b.IsHeirOf("I2CNoble"))) y(); z = false;', "GetFlags(spec_*) asks whether the unit is of a class with that speciality (gbr.exe's GetFlags reads other flags)");
	const ai = S.aiScriptFixes('//int, int idPlayer, GAIKA g\nint own;\ng.Eval(A, p, own, ally, enemy);\ng.Count(A, p, own, ally, enemy);\ns.Count("x");\nreturn own;\n', 'CalcMaxTake.vs').src;
	check(ai.includes('//int, int idPlayer, GAIKA g, int *pOverneed') && ai.includes('int i1ai_hidden;') && ai.includes('g.Eval(A, p, own, ally, enemy, i1ai_hidden)') && ai.includes('g.Count(A, p, own, ally, enemy, i1ai_hidden)') && ai.includes('s.Count("x")') && ai.includes('pOverneed = i1ai_hidden - i1ai_hidden;\nreturn own;'), "Imperivm I's AI scripts take Great Battles of Rome's GAIKA calls and CalcMaxTake signature");
	const ini = S.aiScriptFixes('Main.vs = void\r\nCalcMaxTake.vs = int, int idPlayer, GAIKA g\r\nEvalRecruit.vs = int, Squad sq, GAIKA g, GAIKA *gDepend\r\n', 'ai.ini');
	check(ini.count === 1 && ini.src === 'Main.vs = void\r\nCalcMaxTake.vs = int, int idPlayer, GAIKA g, int *pOverneed\r\nEvalRecruit.vs = int, Squad sq, GAIKA g, GAIKA *gDepend\r\n', "ai.ini declares CalcMaxTake.vs with gbr.exe's signature, which its scripts compile with");
	check(S.aiProfile('AIStart(2, "DEFAULT", d); AIStart(3); AIStart(4, "defensive"); AIStart(5, p);', 'I1').src === 'AIStart(2, "I1", d); AIStart(3, "I1"); AIStart(4, "I1DEFENSIVE"); AIStart(5, p);', "the campaign's computer players run Imperivm I's AI profiles");
	const dt = S.druidTargeting('// .FindUnit2HTL(1);\nUnit tgt;\ntgt = .FindUnit2HTL(action).AsUnit();\nx = .FindUnit2HTL(3);\ny = d.FindUnit2HTL(1, r);');
	check(dt.count === 2 && dt.script.includes('{ if (action == 1) tgt = .FindUnitToHeal(); else tgt = .FindUnitToLearn(); }') && dt.script.includes('x = .FindUnitToLearn();') && dt.script.startsWith('// .FindUnit2HTL(1);') && dt.unsupported.join() === '5', "the druid's FindUnit2HTL as Great Battles of Rome's methods, a mode in a variable too (Imperivm II)");
	check(S.bonusArguments('u.AddBonus(140, 200, 0, 5, 1600); v[i].RemoveBonus(1, f(2, 3), 4, 5, 6);').src === 'u.AddBonus(((140) + (200)) / 2, 0, 5, 1600, 0); v[i].RemoveBonus(((1) + (f(2, 3))) / 2, 4, 5, 6, 0);', "AddBonus and RemoveBonus in Great Battles of Rome's order");
}

// The real conversion, when both games are installed (I1_DIR / GBR_DIR, or their usual folders): it must convert with no
// check or audit error, so that no class, attribute or call Great Battles of Rome lacks slips in.
{
	const fs = require('fs');
	const path = require('path');
	const os = require('os');
	const i1 = process.env.I1_DIR || 'C:/Program Files (x86)/Imperivm. La guerra de las Galias';
	const gbr = process.env.GBR_DIR || 'C:/Program Files (x86)/Steam/steamapps/common/Imperivm - Great Battles of Rome HD';
	if (fs.existsSync(path.join(i1, 'data.pak')) && fs.existsSync(path.join(gbr, 'gbr.exe'))) {
		const out = fs.mkdtempSync(path.join(os.tmpdir(), 'i1audit-'));
		const r = require('child_process').spawnSync(process.execPath, [path.join(__dirname, 'convert.js'), '--i1', i1, '--gbr', gbr, '--out', out, '--dry-run', '--audit'], { encoding: 'utf8', maxBuffer: 1 << 28 });
		const auditFile = path.join(out, 'imperivm1-campaign', 'audit.txt');
		const audit = fs.existsSync(auditFile) ? fs.readFileSync(auditFile, 'utf8') : '';
		check(r.status === 0 && /\nErrors: 0\n/.test(audit), `the campaign converts with no check or audit error (see ${out})`);
		if (r.status === 0) fs.rmSync(out, { recursive: true, force: true });
	} else {
		console.log('skipped: the real conversion (Imperivm I or Great Battles of Rome not installed; set I1_DIR and GBR_DIR)');
	}
}

// Imperivm II's conversion (tools/imperivm2), on the same machinery, when it is installed: no script call Great Battles of
// Rome lacks.
{
	const fs = require('fs');
	const path = require('path');
	const os = require('os');
	const i2 = process.env.I2_DIR || 'C:/Program Files (x86)/Imperivm II - La Conquista de Hispania';
	const gbr = process.env.GBR_DIR || 'C:/Program Files (x86)/Steam/steamapps/common/Imperivm - Great Battles of Rome HD';
	if (fs.existsSync(path.join(i2, 'Imperivm II.exe')) && fs.existsSync(path.join(gbr, 'gbr.exe'))) {
		const out = fs.mkdtempSync(path.join(os.tmpdir(), 'i2convert-'));
		const r = require('child_process').spawnSync(process.execPath, [path.join(__dirname, '..', 'imperivm2', 'convert.js'), '--i2', i2, '--gbr', gbr, '--out', out], { encoding: 'utf8', maxBuffer: 1 << 28 });
		check(r.status === 0, `Imperivm II's campaigns convert with every script call in Great Battles of Rome (see ${out})`);
		if (r.status === 0) fs.rmSync(out, { recursive: true, force: true });
	} else {
		console.log('skipped: the Imperivm II conversion (Imperivm II or Great Battles of Rome not installed; set I2_DIR and GBR_DIR)');
	}
}

console.log(`${checks - failures} of ${checks} checks passed`);
process.exitCode = failures ? 1 : 0;
