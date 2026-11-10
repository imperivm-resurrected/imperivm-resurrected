// Checks of sdk/examples/ruleset-clash-example: make-clash-files.js (the two copies of the game's own command file that are not in the repository) and
// what the validator says about clash-c and clash-d with and without them. No game files needed: the game folder is made up here with a tiny data.pak.
// Run: node tests/test_clashexample.js
const assert = require('node:assert/strict');
const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const example = path.join(root, 'sdk/examples/ruleset-clash-example');
const maker = require(path.join(example, 'make-clash-files.js'));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'imperivm-clashexample-'));

// A .pak with an index and the data of each entry after it, as the game writes them
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
	const indexSize = 0x28 + parts.reduce((a, p) => a + p.head.length + p.tail.length + 8, 0);
	let offset = indexSize;
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
	fs.mkdirSync(path.dirname(file), { recursive: true });
	fs.writeFileSync(file, Buffer.concat([head, ...index, ...parts.map(p => p.data)]));
}

const run = args => {
	const r = spawnSync(process.execPath, [path.join(root, 'sdk/tools/validate-mod.js'), ...args], { encoding: 'utf8' });
	assert.ifError(r.error);
	return { status: r.status, text: r.stdout + r.stderr };
};
const globals = text => text.split('\n').filter(l => /global in-game file/.test(l));

try {
	const commands = '<commands>\r\n<cmd name="RBarrack Level 3" priority="2"\r\n\tcostgold = "2000"\r\n\tmethod="research">\r\n\t<src obj="RBarracks"/>\r\n</cmd>\r\n' +
		'<cmd name="trainRHastatus" priority="3"\r\n\tkey="h"\r\n\tcostgold = "100"\r\n\tcostfood = "0"\r\n\tmethod="train">\r\n\t<src obj="RBarracks"/>\r\n</cmd>\r\n' +
		'<cmd name="trainRArcher" priority="4"\r\n\tcostgold = "120"\r\n\tmethod="train">\r\n</cmd>\r\n</commands>\r\n';

	// the price changes in that command only, and nothing else of the file
	const changed = maker.withPrice(commands, 77);
	assert.equal(changed, commands.replace('costgold = "100"', 'costgold = "77"'));
	assert.ok(changed.includes('costgold = "2000"') && changed.includes('costgold = "120"'));
	assert.throws(() => maker.withPrice('<commands><cmd name="other" costgold="1"></cmd></commands>', 77), /trainRHastatus is not in the file/);
	assert.throws(() => maker.withPrice('<commands><cmd name="trainRHastatus"></cmd></commands>', 77), /no costgold/);

	// a made-up game folder, and the two files made from it
	const game = path.join(temp, 'game');
	makePak(path.join(game, 'Packs', 'UI.pak'), { 'UI\\ICONS\\RHASTATUS.BMP': 'x', 'UI\\MENU\\BUTTON.BMP': 'x' });
	makePak(path.join(game, 'Packs', 'data.pak'), { 'DATA\\COMMANDS\\RBARRACKS.XML': commands, 'DATA\\COMMANDS\\BARRACKS.XML': '<commands/>' });
	assert.equal(maker.readPakEntry(path.join(game, 'Packs', 'data.pak'), 'DATA\\COMMANDS\\RBARRACKS.XML').toString('latin1'), commands);
	assert.equal(maker.readPakEntry(path.join(game, 'Packs', 'data.pak'), 'DATA\\NOTHING.XML'), null);
	const out = path.join(temp, 'example');
	fs.cpSync(example, out, { recursive: true });
	const written = maker.make(game, out);
	assert.equal(written.length, 2);
	assert.equal(fs.readFileSync(path.join(out, 'clash-c/data/commands/RBarracks.xml'), 'latin1'), commands.replace('costgold = "100"', 'costgold = "77"'));
	assert.equal(fs.readFileSync(path.join(out, 'clash-d/data/commands/RBarracks.xml'), 'latin1'), commands.replace('costgold = "100"', 'costgold = "88"'));
	assert.throws(() => maker.make(path.join(temp, 'nowhere'), out), /not the game's folder/);

	// the validator: with the files, the replaced game file is the one global file of each package (a new name, the new command and the menu skin are not)
	const c = run([path.join(out, 'clash-c'), '--game', game]);
	assert.equal(c.status, 0, c.text);
	assert.equal(globals(c.text).length, 1, c.text);
	assert.ok(globals(c.text)[0].includes('data/commands/RBarracks.xml'), c.text);
	assert.doesNotMatch(c.text, /ClashCUnit|button\.bmp/);
	const d = run([path.join(out, 'clash-d'), '--game', game]);
	assert.equal(d.status, 0, d.text);
	assert.equal(globals(d.text).length, 1, d.text);
	assert.match(d.text, /global in-game file in the root of a rules package/);
	// without the files (as in the repository) there is nothing to warn about, and both are valid
	for (const pack of ['clash-c', 'clash-d']) {
		const r = run([path.join(example, pack), '--game', game]);
		assert.equal(r.status, 0, r.text);
		assert.equal(globals(r.text).length, 0, r.text);
		assert.doesNotMatch(r.text, /ERROR/);
	}
	// the ids are the folders' names, and clash-d is declared a rules package
	for (const pack of ['clash-a', 'clash-b', 'clash-c', 'clash-d']) assert.equal(JSON.parse(fs.readFileSync(path.join(example, pack, 'mod.json'), 'utf8')).id, pack);
	assert.equal(JSON.parse(fs.readFileSync(path.join(example, 'clash-d/mod.json'), 'utf8')).kind, 'rules');
	assert.ok(fs.existsSync(path.join(example, 'clash-c/civs/rome-clash-c.json')) && !fs.existsSync(path.join(example, 'clash-d/civs')));
	console.log('clashexample: ok');
}
finally {
	fs.rmSync(temp, { recursive: true, force: true });
}
