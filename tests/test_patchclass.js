// Checks of the classification of what the patches of a root change of the game's files (sdk/tools/lib/patchclass.js, the same rules as the DLL's PatchPolicy in
// src/mods/patchxml.cpp, which tests/test_patchxml.cpp checks) and of the validator's warning about it. No game files needed: the game folder is made up here.
// Run: node tests/test_patchclass.js
const assert = require('node:assert/strict');
const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const pc = require(path.join(root, 'sdk/tools/lib/patchclass'));
const assets = require(path.join(root, 'sdk/tools/lib/assetclass'));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'imperivm-patchclass-'));
const write = (file, text = 'x') => { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, text); };

function makePak(file, entries) {
	const names = Object.keys(entries).sort();
	const parts = [];
	let prev = '';
	for (const name of names) {
		let shared = 0;
		while (shared < prev.length && shared < name.length && prev[shared] === name[shared]) shared++;
		parts.push({ head: Buffer.from([name.length, shared]), tail: Buffer.from(name.slice(shared), 'latin1'), data: Buffer.from(entries[name], 'latin1') });
		prev = name;
	}
	let offset = 0x28 + parts.reduce((a, p) => a + p.head.length + p.tail.length + 8, 0);
	const index = [];
	for (const p of parts) {
		const ref = Buffer.alloc(8);
		ref.writeUInt32LE(offset, 0);
		ref.writeUInt32LE(p.data.length, 4);
		offset += p.data.length;
		index.push(p.head, p.tail, ref);
	}
	const head = Buffer.alloc(0x28);
	head.write('HMMSYS PackFile', 0, 'latin1');
	head.writeUInt32LE(names.length, 0x20);
	write(file, Buffer.concat([head, ...index, ...parts.map(p => p.data)]));
}

try {
	const xml = '<commands><cmd name="a" costgold="100" costpop="1"/><cmd name="b" costgold="5"/></commands>';
	const skipped = (patch, base = xml, file = 'data/commands/x.xml') => pc.skippedOps(file, base, patch);
	// XML: adds apply, a changed attribute, remove, replace and unset of the game's are skipped (one per operation)
	assert.deepEqual(skipped('<patch><add select="/commands"><cmd name="n" c="1"/></add><set select="/commands/cmd[@name=\'a\']" extra="1"/></patch>'), []);
	assert.equal(skipped('<patch><set select="/commands/cmd[@name=\'a\']" costgold="60"/><set select="/commands/cmd[@name=\'a\']" unset="costpop"/>' +
		'<remove select="//cmd[@name=\'b\']"/><replace select="/commands/cmd[@name=\'a\']"><cmd name="z"/></replace></patch>').length, 4);
	// the same value as the game's, and an operation on a missing target, are nothing
	assert.deepEqual(skipped('<patch><set select="/commands/cmd[@name=\'a\']" costgold="100"/><set select="/commands/cmd[@name=\'zz\']" costgold="1"/></patch>'), []);
	// a patch with both: only the change is reported; a set with an added and a changed attribute is one skipped operation
	assert.equal(skipped('<patch><add select="/commands"><cmd name="n"/></add><set select="/commands/cmd[@name=\'a\']" costgold="1" rollover="r"/></patch>').length, 1);
	// what the patch added is its own: it can be changed and removed
	assert.deepEqual(skipped('<patch><add select="/commands"><cmd name="n" c="1"/></add><set select="/commands/cmd[@name=\'n\']" c="2"/><remove select="/commands/cmd[@name=\'n\']"/></patch>'), []);
	// INI
	const ini = '[GamePlay]\r\nDamageAmplify = 200 ;c\r\n[Vars.All]\r\nAIV_ExcessGold = 10000\r\nBare\r\n';
	assert.deepEqual(skipped('[GamePlay]\nNew = 1\nDamageAmplify = 200\n[Mine]\nA=1\n[Vars.All]\n+Line\n', ini, 'data/const.ini'), []);
	assert.deepEqual(skipped('[GamePlay]\nDamageAmplify = 250\n[Vars.All]\n-AIV_ExcessGold\nAIV_ExcessGold = 7000\n-Bare\n', ini, 'data/const.ini'),
		['[GamePlay] DamageAmplify', '[Vars.All] -AIV_ExcessGold', '[Vars.All] -Bare']);
	assert.deepEqual(skipped('[GamePlay]\nMy = 1\nMy = 2\n-My\n', ini, 'data/const.ini'), []);
	// scripts: @replace only
	assert.deepEqual(skipped('@top\nint x;\n@before return\nint y;\n@after a\nb\n@bottom\nc\n', 'return 0;', 'data/ai/x.vs'), []);
	assert.deepEqual(skipped('@replace return 0;\nreturn 1;\n', 'return 0;', 'data/ai/x.vs'), ['@replace return 0;']);
	// the interface is exempt, and a file the patcher does not read has nothing
	assert.deepEqual(skipped('[A]\nB = 2\n', '[A]\nB = 1\n', 'data/interface/menu/mainmenu.ini'), []);
	assert.deepEqual(skipped('x', 'y', 'data/x.bin'), []);

	// the validator, on a made-up game
	const game = path.join(temp, 'game');
	makePak(path.join(game, 'Packs', 'data.pak'), { 'DATA\\COMMANDS\\RBARRACKS.XML': xml, 'DATA\\CONST.INI': ini, 'DATA\\INTERFACE\\MENU\\MAINMENU.INI': '[A]\r\nB = 1\r\n' });
	assert.equal(assets.gameFileText(game, 'data/const.ini'), ini);
	assert.equal(assets.gameFileText(game, 'data/nothing.xml'), null);
	const run = args => {
		const r = spawnSync(process.execPath, [path.join(root, 'sdk/tools/validate-mod.js'), ...args], { encoding: 'utf8' });
		assert.ifError(r.error);
		return { status: r.status, text: r.stdout + r.stderr };
	};
	const warnings = text => text.split('\n').filter(l => /operation\(s\) of this patch change what the game has/.test(l));
	const mod = path.join(temp, 'mod');
	write(path.join(mod, 'mod.json'), JSON.stringify({ id: 'patch-test', name: 'Patch test', version: '1', category: 'Civilizations', authors: ['x'] }));
	write(path.join(mod, 'civs', 'civ10.json'), JSON.stringify({ id: 'civ10', index: 10, base: 'Gaul' }));
	write(path.join(mod, 'patches', 'data', 'commands', 'RBarracks.xml'), '<patch><add select="/commands"><cmd name="n"/></add><set select="/commands/cmd[@name=\'a\']" costgold="1"/></patch>');
	write(path.join(mod, 'patches', 'data', 'const.ini'), '[GamePlay]\nNew = 1\n');
	write(path.join(mod, 'patches', 'menuini', 'MAINMENU.INI'), '[A]\nB = 2\n');
	let r = run([mod, '--game', game]);
	assert.equal(r.status, 0, r.text);
	assert.equal(warnings(r.text).length, 1, r.text);
	assert.match(warnings(r.text)[0], /patches\/data\/commands\/RBarracks\.xml.*1 operation\(s\).*skipped unless a ruleset of this package is chosen.*rulesets\/<id>\//);
	// without --game nothing can be compared; a package that is not a rules package (cosmetic: only a menu patch) is not warned
	assert.equal(warnings(run([mod]).text).length, 0);
	fs.rmSync(path.join(mod, 'civs'), { recursive: true });
	assert.equal(warnings(run([mod, '--game', game]).text).length, 1, 'a patch of a gameplay file makes it a rules package without civilizations');
	fs.rmSync(path.join(mod, 'patches', 'data'), { recursive: true });
	assert.equal(warnings(run([mod, '--game', game]).text).length, 0);
	console.log('patchclass: ok');
}
finally {
	fs.rmSync(temp, { recursive: true, force: true });
}
