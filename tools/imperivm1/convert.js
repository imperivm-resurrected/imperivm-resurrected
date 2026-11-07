// Converts the campaign of Imperivm I ("Celtic Kings Adventure") into an adventure for Great Battles of Rome HD.
//
//   node tools/imperivm1/convert.js --i1 "<Imperivm I folder>" --gbr "<Great Battles of Rome folder>" --out <folder>
//        [--name "<adventure name>"] [--dry-run] [--keep-class <I1 class>]... [--movies] [--unpacked]
//
//   --i1          the Imperivm I install (read only): adventures\Celtic Kings Adventure.bfhp, data.pak, assets.pak, local.pak
//   --gbr         the Great Battles of Rome install (read only): Packs\data.pak, MapObjects.pak and UI.pak
//   --out         where the adventure is written: "<name>.bfhp", "<name>.bmp" (the 440x360 preview) and report.txt
//   --name        name of the adventure and of its files (default "Imperivm I - La guerra de las Galias")
//   --dry-run     converts and checks everything but writes only report.txt
//   --keep-class  keeps an Imperivm I class instead of substituting it, for a mod that provides it
//   --movies      also converts the two movies of Imperivm I (Packs\data*.bfhp, MPEG-1) into <out>\Movies\ and plays them
//                 from movies\ (they have to be copied to the game's Movies folder); without it, the calls are commented out
//   --unpacked    also writes the files of the adventure, unpacked, to <out>\unpacked\
//
// The output is a conversion of your own copy of the game, for your own use: do not redistribute it.
'use strict';
const fs = require('fs');
const path = require('path');
const { readHpfs, writeHpfs } = require('./lib/hpfs');
const { loadGbr, loadI1, findFile } = require('./lib/catalog');
const { convertAdventure } = require('./lib/adventure');
const { validateAdventure } = require('./lib/validate');
const { readBmp, writeBmp24, coverResize, fillKey } = require('./lib/bmp');

const ADVENTURE = 'Celtic Kings Adventure';

function parseArgs(argv) {
	const opts = { keep: new Set(), name: 'Imperivm I - La guerra de las Galias' };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		const value = () => {
			if (i + 1 >= argv.length) throw new Error(`${a} needs a value`);
			return argv[++i];
		};
		if (a === '--i1') opts.i1 = value();
		else if (a === '--gbr') opts.gbr = value();
		else if (a === '--out') opts.out = value();
		else if (a === '--name') opts.name = value();
		else if (a === '--keep-class') opts.keep.add(value());
		else if (a === '--dry-run') opts.dryRun = true;
		else if (a === '--movies') opts.movies = true;
		else if (a === '--unpacked') opts.unpacked = true;
		else throw new Error(`unknown option ${a}`);
	}
	if (!opts.i1 || !opts.gbr || !opts.out) throw new Error('--i1, --gbr and --out are required');
	if (/[\\/:*?"<>|]/.test(opts.name)) throw new Error('the name cannot have \\ / : * ? " < > |');
	return opts;
}

// Imperivm I's movies are MPEG-1 streams named Packs\data<N>.bfhp. Returns Map "data200" -> output file name.
function findMovies(i1Dir) {
	const movies = new Map();
	const packs = path.join(i1Dir, 'Packs');
	if (!fs.existsSync(packs)) return movies;
	for (const f of fs.readdirSync(packs)) {
		const m = /^(data\d+)\.bfhp$/i.exec(f);
		if (!m) continue;
		const head = Buffer.alloc(4);
		const fd = fs.openSync(path.join(packs, f), 'r');
		fs.readSync(fd, head, 0, 4, 0);
		fs.closeSync(fd);
		if (head.readUInt32BE(0) === 0x000001ba) movies.set(m[1].toLowerCase(), { file: path.join(packs, f), name: `imperivm1_${m[1].toLowerCase()}.mpg` });
	}
	return movies;
}

function main() {
	const opts = parseArgs(process.argv.slice(2));
	const functionsFile = path.join(__dirname, '..', '..', 'sdk', 'game-functions.txt');
	console.log('reading Great Battles of Rome...');
	const gbr = loadGbr(opts.gbr, functionsFile);
	console.log('reading Imperivm I...');
	const i1 = loadI1(opts.i1);
	const advFile = findFile(path.join(opts.i1, 'adventures'), ADVENTURE + '.bfhp');
	const adv = readHpfs(fs.readFileSync(advFile));
	const files = new Map(adv.entries.filter((e) => !e.directory).map((e) => [e.path, e.data]));
	const movies = opts.movies ? findMovies(opts.i1) : null;

	console.log(`converting ${files.size} files...`);
	const result = convertAdventure({
		files,
		gbr,
		i1,
		local: i1.local,
		advName: ADVENTURE,
		name: opts.name,
		keep: opts.keep,
		movies: movies && new Map([...movies].map(([k, v]) => [k, v.name])),
	});
	const { report } = result;
	if (movies) {
		for (const [k, v] of movies) {
			if (!report.lines('Movies').some((l) => l.includes(v.name))) movies.delete(k);
		}
	}

	console.log('checking...');
	const check = validateAdventure(result.files, { gbr, i1Classes: i1.classes, customItems: result.customItems });
	const pkg = writeHpfs(result.files);
	// The written package must read back to exactly the same files.
	const back = readHpfs(pkg);
	const backFiles = new Map(back.entries.filter((e) => !e.directory).map((e) => [e.path, e.data]));
	let roundTrip = backFiles.size === result.files.size;
	for (const [p, buf] of result.files) if (!backFiles.has(p) || !backFiles.get(p).equals(buf)) roundTrip = false;
	if (!roundTrip) check.errors.push('the written package does not read back to the same files');
	const stats = check.stats;
	report.add(
		'Checks',
		`${stats.xml} XML files, ${stats.objects} map objects, ${stats.scripts} scripts, ${stats.grids} grids, ${stats.changeMaps} ChangeMap calls, ${stats.phrases} phrases, ${stats.voices} voice references`,
	);
	report.add('Checks', `package: ${result.files.size} files, ${(pkg.length / 1048576).toFixed(1)} MB, read back ${roundTrip ? 'identical' : 'DIFFERENT'}`);
	for (const e of check.errors) report.add('Check errors', e);
	for (const w of check.warnings) report.add('Check warnings', w);
	if (!check.errors.length) report.add('Check errors', 'none');

	fs.mkdirSync(opts.out, { recursive: true });
	const reportText = report.toText();
	fs.writeFileSync(path.join(opts.out, 'report.txt'), reportText);
	if (!opts.dryRun) {
		if (check.errors.length) {
			console.log(reportText);
			console.error(`${check.errors.length} errors: nothing written but report.txt`);
			process.exit(1);
		}
		fs.writeFileSync(path.join(opts.out, opts.name + '.bfhp'), pkg);
		const map = files.get('global_map.bmp');
		if (map) fs.writeFileSync(path.join(opts.out, opts.name + '.bmp'), writeBmp24(coverResize(fillKey(readBmp(map), [34, 26, 18]), 440, 360)));
		if (movies && movies.size) {
			fs.mkdirSync(path.join(opts.out, 'Movies'), { recursive: true });
			for (const v of movies.values()) fs.copyFileSync(v.file, path.join(opts.out, 'Movies', v.name));
		}
		if (opts.unpacked) {
			for (const [p, buf] of result.files) {
				const dest = path.join(opts.out, 'unpacked', ...p.split('/'));
				fs.mkdirSync(path.dirname(dest), { recursive: true });
				fs.writeFileSync(dest, buf);
			}
		}
	}
	console.log(reportText);
	console.log(`${check.errors.length} errors, ${check.warnings.length} warnings${opts.dryRun ? ' (dry run: only report.txt written)' : ''}`);
	process.exit(check.errors.length ? 1 : 0);
}

try {
	main();
} catch (e) {
	console.error(e.message);
	process.exit(1);
}
