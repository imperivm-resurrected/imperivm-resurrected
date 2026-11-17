// Checks of the validator's warnings about where art, sounds and texts go (sdk/tools/lib/assetclass.js). No game files needed:
// the game folder is made up here with two tiny .pak files. Run: node tests/test_assetclass.js
const assert = require('node:assert/strict');
const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const assets = require(path.join(root, 'sdk/tools/lib/assetclass'));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'imperivm-assetclass-'));
const write = (file, text = 'x') => { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, text); };

// A .pak with only its index (names with the part shared with the previous one cut off, as the game writes them)
function makePak(file, names) {
	const parts = [];
	let prev = '';
	for (const name of [...names].sort()) {
		let shared = 0;
		while (shared < prev.length && shared < name.length && prev[shared] === name[shared]) shared++;
		parts.push(Buffer.from([name.length, shared]), Buffer.from(name.slice(shared), 'latin1'), Buffer.alloc(8));
		prev = name;
	}
	const head = Buffer.alloc(0x28);
	head.write('HMMSYS PackFile', 0, 'latin1');
	head.writeUInt32LE(names.length, 0x20);
	write(file, Buffer.concat([head, ...parts]));
}

try {
	// class 3: the menu skin, with the paths as they are in a mod and as the game has them
	for (const f of ['menuini/Menu.ini', 'Data/Interface/Menu/lobby.ini', 'ui/menu/button.bmp', 'gameres/menu/button.bmp', 'currentlang/MenuBackground.bmp',
		'currentlang/logo.bmp', 'currentlang/logo_small.bmp', 'currentlang/tips.xml', 'currentlang/help.xml', 'local/spanish/tips.xml', 'Local\\english\\HELP.XML',
		'local/french/menu_16_9.bmp']) assert.ok(assets.isMenuSkin(f), f);
	for (const f of ['gameres/icons/RHastatus.bmp', 'data/classes/RHastatus.sc.xml', 'local/spanish/voices/RHastatus/1.wav', 'local/spanish/credits.txt',
		'sounds/ui/click.wav', 'data/interface/ranks.xml', 'menuinix/a.ini', 'units/menuini/a.ini']) assert.ok(!assets.isMenuSkin(f), f);
	assert.equal(assets.gamePath('Local\\Spanish\\Voices\\A\\1.WAV'), 'currentlang/voices/a/1.wav');
	assert.equal(assets.gamePath('GameRes/Icons/A.bmp'), 'ui/icons/a.bmp');
	assert.equal(assets.gamePath('ui/icons/a.bmp'), 'ui/icons/a.bmp');
	assert.equal(assets.gamePath('menuini/a.ini'), 'data/interface/menu/a.ini');

	// the names of a .pak, also when its index is longer than the first piece that is read
	const many = Array.from({ length: 30000 }, (_, i) => 'DATA\\CLASSES\\UNIT' + String(i).padStart(6, '0') + '.SC.XML');
	makePak(path.join(temp, 'big.pak'), many);
	assert.equal(assets.pakNames(path.join(temp, 'big.pak')).length, 30000);
	write(path.join(temp, 'notpak.pak'), 'nothing');
	assert.deepEqual(assets.pakNames(path.join(temp, 'notpak.pak')), []);

	// the game's files with their real paths, and local/ only gives what is under CURRENTLANG/
	const game = path.join(temp, 'game');
	makePak(path.join(game, 'Packs', 'UI.pak'), ['UI\\ICONS\\RHASTATUS.BMP', 'UI\\MENU\\BUTTON.BMP']);
	makePak(path.join(game, 'Packs', 'data.pak'), ['DATA\\CLASSES\\RHASTATUS.SC.XML']);
	makePak(path.join(game, 'local', 'english.pak'), ['CURRENTLANG\\VOICES\\RHASTATUS\\1.WAV', 'CURRENTLANG\\TIPS.XML', 'RHASTATUS\\1.WAV']);
	const files = assets.gameFiles(game);
	assert.deepEqual([...files].sort(), ['currentlang/tips.xml', 'currentlang/voices/rhastatus/1.wav', 'data/classes/rhastatus.sc.xml', 'ui/icons/rhastatus.bmp',
		'ui/menu/button.bmp']);
	// the loose files of Units/ and Currentlang/ of the game folder count too (the DLL reads the same ones); other folders do not
	write(path.join(game, 'Units', 'Ghero1', 'LARAX_WALK.RLE.MMP'));
	write(path.join(game, 'Currentlang', 'MENUBACKGROUND.bmp'));
	write(path.join(game, 'Music', 'GBR_TRACK_1.ogg'));
	const withLoose = assets.gameFiles(game);
	assert.ok(withLoose.has('units/ghero1/larax_walk.rle.mmp') && withLoose.has('currentlang/menubackground.bmp'));
	assert.ok(!withLoose.has('music/gbr_track_1.ogg'));
	fs.rmSync(path.join(game, 'Units'), { recursive: true });
	fs.rmSync(path.join(game, 'Currentlang'), { recursive: true });
	assert.equal(assets.gameFiles(path.join(temp, 'nowhere')), null);
	assert.equal(assets.gameFiles(null), null);

	// the validator: only warnings, never errors
	const run = args => {
		const r = spawnSync(process.execPath, [path.join(root, 'sdk/tools/validate-mod.js'), ...args], { encoding: 'utf8' });
		assert.ifError(r.error);
		return { status: r.status, text: r.stdout + r.stderr };
	};
	const mod = path.join(temp, 'mod');
	write(path.join(mod, 'mod.json'), JSON.stringify({ id: 'asset-test', name: 'Asset test', version: '1', category: 'Civilizations', authors: ['x'], rulesets: [{ id: 'r', name: 'R' }] }));
	write(path.join(mod, 'civs', 'civ10.json'), JSON.stringify({ id: 'civ10', index: 10, base: 'Gaul' }));
	write(path.join(mod, 'gameres', 'icons', 'RHastatus.bmp'));          // the game has it: global
	write(path.join(mod, 'gameres', 'icons', 'MyUnit.bmp'));             // its own name: the civilization's art
	write(path.join(mod, 'ui', 'icons', 'RHastatus.bmp'));              // the same game file under its real folder name: global too
	write(path.join(mod, 'gameres', 'menu', 'button.bmp'));              // the game has it, but it is the menu skin: not this warning
	write(path.join(mod, 'local', 'english', 'voices', 'RHastatus', '1.wav'));
	write(path.join(mod, 'rulesets', 'r', 'gameres', 'icons', 'RHastatus.bmp'));   // in a layer: where it belongs
	write(path.join(mod, 'rulesets', 'r', 'menuini', 'Menu.ini'));
	write(path.join(mod, 'rulesets', 'r', 'local', 'english', 'tips.xml'));
	const without = run([mod]);
	assert.equal(without.status, 0, without.text);
	assert.doesNotMatch(without.text, /global in-game file/, 'without --game nothing can be compared');
	assert.match(without.text, /rulesets\/r\/menuini\/Menu\.ini .*menu skin file in a ruleset layer/);
	assert.match(without.text, /rulesets\/r\/local\/english\/tips\.xml .*menu skin file in a ruleset layer/);
	const withGame = run([mod, '--game', game]);
	assert.equal(withGame.status, 0, withGame.text);
	const globals = withGame.text.split('\n').filter(l => /global in-game file/.test(l));
	assert.equal(globals.length, 3, withGame.text);
	assert.ok(globals.some(l => l.includes('gameres/icons/RHastatus.bmp')) && globals.some(l => l.includes('ui/icons/RHastatus.bmp')) &&
		globals.some(l => l.includes('local/english/voices/RHastatus/1.wav')));
	assert.doesNotMatch(withGame.text, /MyUnit|gameres\/menu\/button/);
	assert.doesNotMatch(withGame.text, /ERROR/);
	// a package without civilizations and without gameplay files is not a rules package: its root is not what the rule is about
	fs.rmSync(path.join(mod, 'civs'), { recursive: true });
	assert.doesNotMatch(run([mod, '--game', game]).text, /global in-game file/);
	// a rules package without civilizations is: its root is its ruleset, and a game file in it applies only when that ruleset is chosen
	write(path.join(mod, 'data', 'classes', 'NewClass.sc.xml'));
	assert.equal(run([mod, '--game', game]).text.split('\n').filter(l => /global in-game file/.test(l)).length, 3);
	assert.ok(assets.isGameplayPath('Data\\Classes\\X.xml') && assets.isGameplayPath('units/A/a.ent.xml') && assets.isGameplayPath('packs/x.bfhp'));
	assert.ok(!assets.isGameplayPath('gameres/icons/a.bmp') && !assets.isGameplayPath('patches/data/const.ini') && !assets.isGameplayPath('units/A/a.rle'));
	// a .pak in the root of a rules package is mounted whole: the rule cannot leave out its files that replace the game's
	write(path.join(mod, 'Packs', 'extra.pak'));
	assert.match(run([mod]).text, /WARNING  Packs\/extra\.pak  \.pak in the root of a rules package/);
	fs.rmSync(path.join(mod, 'Packs'), { recursive: true });
	fs.rmSync(path.join(mod, 'data'), { recursive: true });
	// the clash example: the root file of clash-a is the mistake the warning is for, and clash-b does it right
	const clash = path.join(root, 'sdk/examples/ruleset-clash-example');
	const wrong = run([path.join(clash, 'clash-a'), '--game', game]);
	assert.equal(wrong.status, 0, wrong.text);
	assert.match(wrong.text, /WARNING  gameres\/icons\/RHastatus\.bmp  global in-game file in the root of a rules package \(a civilization pack is one\): it is ignored unless a ruleset of this package is chosen/);
	assert.equal(wrong.text.split('\n').filter(l => /global in-game file/.test(l)).length, 1, wrong.text);
	assert.doesNotMatch(run([path.join(clash, 'clash-a')]).text, /global in-game file/, 'without --game nothing can be compared');
	const right = run([path.join(clash, 'clash-b'), '--game', game]);
	assert.equal(right.status, 0, right.text);
	assert.doesNotMatch(right.text, /WARNING/);
	console.log('assetclass: ok');
}
finally {
	fs.rmSync(temp, { recursive: true, force: true });
}
