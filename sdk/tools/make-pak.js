'use strict';
// Packs a folder into a game .pak, for a ruleset's packs\ folder: every file of the folder becomes a member, named by its path ("ui/icons/RHastatus.bmp").
//   node tools/make-pak.js <folder> <output.pak>
// The names in the pak are the game's paths: the same ones a loose file would have in the mod's folder. A pak is mounted by the game once, when it starts;
// in a ruleset (rulesets\<id>\packs\x.pak) the DLL mounts it and takes it out again when the ruleset changes, without restarting the game. Its members shadow the
// game's files of the same name, as loose files do (and loose files of the ruleset win over the pak).
const fs = require('fs');
const path = require('path');
const { buildPak } = require('./lib/pakwrite');

function collect(dir, rel, out) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
		if (e.isDirectory()) collect(path.join(dir, e.name), rel + e.name + '/', out);
		else out.push({ name: rel + e.name, data: fs.readFileSync(path.join(dir, e.name)) });
	}
	return out;
}

if (require.main === module) {
	const [folder, output] = process.argv.slice(2);
	if (!folder || !output) {
		console.log('Usage: node make-pak.js <folder> <output.pak>');
		process.exit(2);
	}
	if (!fs.existsSync(folder) || !fs.statSync(folder).isDirectory()) {
		console.log(`"${folder}" is not a folder`);
		process.exit(1);
	}
	try {
		const files = collect(folder, '', []);
		if (!files.length) throw new Error('the folder has no files');
		fs.mkdirSync(path.dirname(path.resolve(output)), { recursive: true });
		fs.writeFileSync(output, buildPak(files));
		console.log(`${output}: ${files.length} members`);
	}
	catch (e) {
		console.log(e.message);
		process.exit(1);
	}
}

module.exports = { collect };
