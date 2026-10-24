// Focused SDK regression checks; no game files needed. Run: node tests/test_civprefix.js
const assert = require('node:assert/strict');
const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'imperivm-civprefix-'));
const write = (file, text) => { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, text); };
const run = (tool, args) => {
	const r = spawnSync(process.execPath, [path.join(root, 'sdk/tools', tool), ...args], { encoding: 'utf8' });
	assert.ifError(r.error);
	return { status: r.status, text: r.stdout + r.stderr };
};
const civ = (letter, index = 10, base = 'Gaul') => ({ id: 'civ' + index, index, base, ...(letter === undefined ? {} : { letter }) });
try {
	const mod = path.join(temp, 'validation');
	const validate = civs => {
		fs.rmSync(mod, { recursive: true, force: true });
		for (const c of civs) write(path.join(mod, 'civs', c.id + '.json'), JSON.stringify(c));
		return run('validate-mod.js', [mod]);
	};
	for (const letter of [undefined, 'D', 'd', '0', '2', '9', 'Ga', 'Gal', 'Dac', 'DaC', 'Dac2', 'A123']) {
		const r = validate([civ(letter)]);
		assert.equal(r.status, 0, `${letter}: ${r.text}`);
	}
	for (const letter of ['', 'Dacia', 'dac', 'dAc', '22', '2D', 'D_ac', 'D-', ' D', 'D ', 'É', 'D\n', 'D\0a', 2, null, {}, []]) {
		const r = validate([civ(letter)]);
		assert.equal(r.status, 1, `${JSON.stringify(letter)}: ${r.text}`);
		assert.match(r.text, /"letter" must be 1 to 4/);
	}
	for (const pair of [['Dac', 'DaC'], ['d', 'D'], ['2', '2']]) {
		const r = validate([civ(pair[0]), civ(pair[1], 11)]);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /already used by civ10 in civs\/civ10.json/);
	}
	assert.equal(validate([civ('Ga'), civ('Gal', 11), civ('G', 12)]).status, 0);
	for (const index of [10, 100, 127]) {
		const r = validate([{ ...civ('Dac'), index }]);
		assert.equal(r.status, 0, r.text);
	}
	const autoCiv = (id, letter) => ({ id, base: 'Gaul', letter });
	const automatic = validate([autoCiv('dacia', 'Dac'), autoCiv('galatia', 'Gal'), civ('New')]);
	assert.equal(automatic.status, 0, automatic.text);
	validate([autoCiv('dacia', 'Dac')]);
	write(path.join(mod, 'civs', 'duplicate.json'), JSON.stringify(autoCiv('DACIA', 'Gal')));
	assert.match(run('validate-mod.js', [mod]).text, /id "DACIA" is already used/);
	for (const index of [10.5, '10', null, true, 9, 128]) {
		const r = validate([{ ...civ('Dac'), index }]);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /"index" must be a whole number between 10 and 127/);
	}
	const duplicateIndex = validate([civ('Dac'), { ...civ('Gal'), id: 'other' }]);
	assert.equal(duplicateIndex.status, 1, duplicateIndex.text);
	assert.match(duplicateIndex.text, /index 10 is already used/);
	validate([civ('Dac')]);
	write(path.join(mod, 'civs', 'other.json'), JSON.stringify({ ...civ('Gal', 11), id: 'CIV10' }));
	assert.match(run('validate-mod.js', [mod]).text, /id "CIV10" is already used/);
	validate([]);
	write(path.join(mod, 'civs', 'fallback.json'), JSON.stringify({ index: 10, base: 'Gaul' }));
	assert.equal(run('validate-mod.js', [mod]).status, 0);
	for (const value of [null, [], 'not an object']) {
		write(path.join(mod, 'civs', 'fallback.json'), JSON.stringify(value));
		const r = run('validate-mod.js', [mod]);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /not an object/);
		assert.doesNotMatch(r.text, /TypeError/);
	}
	const bases = ['Gaul', 'RepublicanRome', 'Carthage', 'Iberia', 'ImperialRome', 'Britain', 'Egypt', 'Germany'];
	for (const [i, letter] of [...'GRCIMBET'].entries()) {
		assert.equal(validate([civ(letter, 10, bases[i]), civ(letter.toLowerCase(), 11, bases[i]), civ(undefined, 12, bases[i])]).status, 0);
		const r = validate([civ(letter, 10, bases[(i + 1) % 8])]);
		assert.equal(r.status, 1, r.text);
		assert.ok(r.text.includes(`already used by ${bases[i]} from the original game`), r.text);
	}

	const data = path.join(temp, 'DATA');
	const addClass = (id, properties = '', parent = '') => write(path.join(data, 'classes', id + '.sc.xml'),
		`<class id="${id}"${parent ? ` parent="${parent}"` : ''}><properties ${properties}/></class>`);
	for (const id of ['GTownhall', 'GSwordsman', 'GaulHero', 'GaTownhall', 'GalTownhall']) addClass(id, 'race="Gaul"');
	addClass('GHero1', '', 'GaulHero');
	addClass('GSentry'); addClass('GSentry1', '', 'GSentry'); addClass('GGuardPostSentry', '', 'GSentry');
	addClass('Rhouse03', 'race="RepublicanRome"');
	write(path.join(data, 'commands', 'base.xml'), `<commands>
<cmd name="trainpeasantg" param="GSwordsman"><src obj="GTownhall"/></cmd>
<cmd name="hireheroG" param="GaulHero"><src obj="GTownhall"/></cmd>
<cmd name="GTribute" param="10"><src obj="GTownhall"/></cmd>
<cmd name="GAddPop" param="10"><src obj="GTownhall"/></cmd>
<cmd name="shared" param="10"><src obj="GTownhall"/></cmd>
<cmd name="other" param="GaTownhall GalTownhall"><src obj="GaTownhall"/></cmd>
</commands>`);
	const clone = (id, args = []) => {
		const dest = path.join(temp, 'clone-' + id);
		const r = run('clone-civilization.js', [data, dest, id, ...args]);
		assert.equal(r.status, 0, r.text);
		const j = JSON.parse(fs.readFileSync(path.join(dest, 'civs', id.toLowerCase() + '.json')));
		return { ...r, dest, j };
	};
	const d = clone('dacia');
	assert.equal(d.j.letter, 'Dac');
	assert.equal(Object.hasOwn(d.j, 'index'), false);
	assert.equal(d.j.alias.classes.GTownhall, 'DacTownhall');
	assert.equal(d.j.alias.classes.GSentry1, 'DacSentry1');
	assert.equal(d.j.alias.classes.GaulHero, 'DaciaHero');
	assert.equal(d.j.alias.classes.GHero1, 'DacHero1');
	assert.equal(d.j.alias.classes.GaTownhall, undefined);
	assert.equal(d.j.alias.classes.GalTownhall, undefined);
	assert.equal(fs.existsSync(path.join(d.dest, 'data/classes/DacaTownhall.sc.xml')), false);
	assert.equal(d.j.alias.commands.trainpeasantg, 'trainpeasantdac');
	assert.equal(d.j.alias.commands.hireheroG, 'hireheroDac');
	assert.equal(d.j.alias.commands.GTribute, 'DacTribute');
	assert.equal(d.j.alias.commands.GAddPop, 'DacAddPop');
	assert.match(fs.readFileSync(path.join(d.dest, 'data/commands/Dacia.xml'), 'utf8'), /param="DacSwordsman"/);
	assert.match(fs.readFileSync(path.join(d.dest, 'patches/data/commands/base.xml'), 'utf8'), /obj="DacTownhall"/);
	assert.equal(run('validate-mod.js', [d.dest, '--data', data]).status, 0);
	const mixed = clone('mixed', ['--letter', 'DaC2', '--index', '12', '--all-orders']);
	assert.equal(mixed.j.letter, 'DaC2');
	assert.equal(mixed.j.index, 12);
	assert.equal(mixed.j.alias.classes.GTownhall, 'DaC2Townhall');
	assert.equal(mixed.j.alias.commands.trainpeasantg, 'trainpeasantdac2');
	assert.equal(mixed.j.alias.commands.hireheroG, 'hireheroDaC2');
	assert.equal(mixed.j.alias.commands.shared, 'shared Mixed');
	assert.equal(run('validate-mod.js', [mixed.dest, '--data', data]).status, 0);
	for (const index of [100, 127]) {
		const explicit = clone('explicit' + index, ['--letter', 'Exp', '--index', String(index)]);
		assert.equal(explicit.j.index, index);
		assert.equal(run('validate-mod.js', [explicit.dest, '--data', data]).status, 0);
	}
	const legacy = clone('legacy', ['d', 'Gaul', '14']);
	assert.equal(legacy.j.letter, 'D');
	assert.equal(legacy.j.index, 14);
	const digit = clone('digit', ['2', 'Gaul', '15', '--all-orders']);
	assert.equal(digit.j.letter, '2');
	assert.equal(digit.j.index, 15);
	assert.equal(digit.j.alias.commands.shared, 'shared Digit');
	const positionalAuto = clone('positional', ['Pos', 'Gaul', '--all-orders']);
	assert.equal(Object.hasOwn(positionalAuto.j, 'index'), false);
	assert.equal(positionalAuto.j.alias.commands.shared, 'shared Positional');
	assert.equal(clone('positional127', ['Pos', 'Gaul', '127']).j.index, 127);
	assert.equal(clone('roman', ['--letter', 'Rom', '--base', 'RepublicanRome']).j.alias.classes.Rhouse03, 'Romhouse03');
	const registered = clone('h'); // Derived H is already Greece's prefix; never silently choose another.
	assert.equal(registered.j.letter, 'H');
	assert.match(registered.text, /WARNING: prefix "H" is registered to Greece .*Greek Civilization Mod/);
	for (const prefix of 'GRCIMBETgrcimbet') {
		const dest = path.join(temp, 'reserved');
		const r = run('clone-civilization.js', [data, dest, 'reserved', '--letter', prefix]);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /would overwrite game classes/);
		assert.equal(fs.existsSync(dest), false);
	}

	const firstMod = path.join(temp, 'my first civilization');
	fs.cpSync(path.join(root, 'sdk/templates/civilization'), firstMod, { recursive: true });
	const manifest = fs.readFileSync(path.join(firstMod, 'mod.json'));
	const firstArgs = ['myciv', '--base', 'Gaul'];
	assert.ok(fs.readFileSync(path.join(firstMod, 'README.md'), 'utf8').includes(firstArgs.join(' ')));
	const first = run('clone-civilization.js', [data, firstMod, ...firstArgs]);
	assert.equal(first.status, 0, first.text);
	const myciv = JSON.parse(fs.readFileSync(path.join(firstMod, 'civs/myciv.json')));
	assert.equal(myciv.letter, 'Myc');
	assert.equal(myciv.base, 'Gaul');
	assert.equal(Object.hasOwn(myciv, 'index'), false);
	assert.equal(myciv.ai, true);
	assert.equal(myciv.variant, undefined);
	assert.equal(myciv.replacesBase, undefined);
	assert.deepEqual(fs.readFileSync(path.join(firstMod, 'mod.json')), manifest);
	assert.equal(run('validate-mod.js', [firstMod, '--data', data]).status, 0);

	const snapshot = dir => fs.readdirSync(dir, { recursive: true }).sort()
		.filter(f => fs.statSync(path.join(dir, f)).isFile())
		.map(f => [f, fs.readFileSync(path.join(dir, f))]);
	const editedClass = path.join(firstMod, 'data/classes/MycTownhall.sc.xml');
	fs.appendFileSync(editedClass, '\n<!-- author edits must survive regeneration -->');
	const before = snapshot(firstMod);
	for (const args of [firstArgs, ['myciv', '--letter', 'New'], ['another', '--letter', 'Myc'], ['another', '--letter', 'New']]) {
		const r = run('clone-civilization.js', [data, firstMod, ...args]);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /refusing to overwrite/);
		assert.deepEqual(snapshot(firstMod), before);
	}
	for (const [i, file] of ['Data/Classes/MycTownhall.sc.xml', 'data/classes/myctownhall.SC.XML', 'PATCHES/DATA/COMMANDS/BASE.XML', 'CIVS/MYCIV.JSON'].entries()) {
		const dest = path.join(temp, 'case-conflict-' + i);
		write(path.join(dest, file), 'existing author content');
		const beforeCase = snapshot(dest);
		const r = run('clone-civilization.js', [data, dest, ...firstArgs]);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /refusing to overwrite/);
		assert.deepEqual(snapshot(dest), beforeCase);
	}
	const invalidDest = path.join(temp, 'invalid-data-output');
	for (const input of [path.join(temp, 'missing-data'), temp]) {
		const r = run('clone-civilization.js', [input, invalidDest, 'newciv']);
		assert.equal(r.status, 1, r.text);
		assert.match(r.text, /DATA folder/);
		assert.equal(fs.existsSync(invalidDest), false);
	}
	for (const args of [['--letter'], ['--letter', 'dac'], ['--letter', '123'], ['--unknown'],
		...['128', '9', '10.5', 'null', '"10"'].flatMap(index => [['--index', index], ['Bad', 'Gaul', index]])]) {
		const dest = path.join(temp, 'bad');
		const r = run('clone-civilization.js', [data, dest, 'bad', ...args]);
		assert.equal(r.status, 1, r.text);
		assert.equal(fs.existsSync(dest), false);
	}
	assert.equal(run('clone-civilization.js', [data, path.join(temp, 'missing-id')]).status, 1);
	console.log('SDK civilization prefixes: optional indices, validation, collisions, clone expansion, legacy CLI and registry warnings passed');
}
finally { fs.rmSync(temp, { recursive: true, force: true }); }
