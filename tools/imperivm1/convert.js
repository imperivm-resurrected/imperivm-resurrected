// Converts the campaign of Imperivm I ("Celtic Kings Adventure") into two mods of Imperivm Resurrected for Great Battles
// of Rome HD:
//   <id>         the adventure alone (mod.json, adventures\"<name>.bfhp" and "<name>.bmp", the 440x360 preview, and the
//                report): a mod of maps only loads without restarting the game, so a new conversion does too.
//   <id>-data    what the adventure needs from the game's data: Imperivm I's own versions of the classes that behave otherwise
//                in Great Battles of Rome (lib/i1classes.js), which only the campaign uses, their command patches and the
//                movies. It changes the game's data, so enabling it restarts the game; it rarely changes.
// The adventure requires the data mod. Copy both folders into the game's mods\ and enable them in the profile.
//
//   node tools/imperivm1/convert.js --i1 "<Imperivm I folder>" --gbr "<Great Battles of Rome folder>" --out <folder>
//        [--name "<adventure name>"] [--mod-id <id>] [--dry-run] [--keep-class <I1 class>]... [--movies] [--unpacked]
//        [--audit]
//
//   --i1          the Imperivm I install (read only): adventures\Celtic Kings Adventure.bfhp, data.pak, assets.pak, local.pak
//   --gbr         the Great Battles of Rome install (read only): Packs\data.pak, MapObjects.pak and UI.pak
//   --out         where the two mod folders are written; report.txt goes in the adventure's
//   --name        name of the adventure and of its files (default "Imperivm I - La guerra de las Galias")
//   --mod-id      id of the adventure's mod (default "imperivm1-campaign"); the data mod is "<id>-data"
//   --dry-run     converts and checks everything but writes only report.txt
//   --keep-class  keeps an Imperivm I class instead of substituting it, for a mod that provides it
//   --movies      also converts the two movies of Imperivm I (Packs\data*.bfhp, MPEG-1) into the data mod's movies\ and plays
//                 them from movies\ (the game looks for them in the active mods); without it, the calls are commented out
//   --unpacked    also writes the files of the adventure, unpacked, to <out>\unpacked\
//   --audit       writes audit.txt next to report.txt: what the conversion keeps of each XML attribute, the diplomacy, the script API
//                 and the classes, against Great Battles of Rome's own adventures and gbr.exe (lib/audit.js); its errors
//                 fail the conversion
//
// The output is a conversion of your own copy of the game, for your own use: do not redistribute it.
'use strict';
const fs = require('fs');
const path = require('path');
const { readHpfs, writeHpfs } = require('./lib/hpfs');
const { loadGbr, loadI1, i1Source, findFile } = require('./lib/catalog');
const M = require('./lib/mapping');
const { convertAdventure, Report } = require('./lib/adventure');
const { planI1Classes, i1ClassFiles } = require('./lib/i1classes');
const { validateAdventure } = require('./lib/validate');
const { readBmp, writeBmp24, coverResize, fillKey } = require('./lib/bmp');
const { auditAdventure } = require('./lib/audit');

const ADVENTURE = 'Celtic Kings Adventure';

function parseArgs(argv) {
	const opts = { keep: new Set(), name: 'Imperivm I - La guerra de las Galias', modId: 'imperivm1-campaign' };
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
		else if (a === '--mod-id') opts.modId = value();
		else if (a === '--keep-class') opts.keep.add(value());
		else if (a === '--dry-run') opts.dryRun = true;
		else if (a === '--movies') opts.movies = true;
		else if (a === '--unpacked') opts.unpacked = true;
		else if (a === '--audit') opts.audit = true;
		else throw new Error(`unknown option ${a}`);
	}
	if (!opts.i1 || !opts.gbr || !opts.out) throw new Error('--i1, --gbr and --out are required');
	if (/[\\/:*?"<>|]/.test(opts.name)) throw new Error('the name cannot have \\ / : * ? " < > |');
	if (!/^[a-z0-9][a-z0-9-]*$/.test(opts.modId)) throw new Error('--mod-id takes lowercase letters, numbers and hyphens');
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

// The files of Great Battles of Rome's own adventures (read only), for the audit; not the package named `skip`.
function referenceAdventures(gbrDir, skip) {
	const files = new Map();
	const walk = (dir, rel) => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const full = path.join(dir, e.name);
			if (e.isDirectory()) walk(full, `${rel}${e.name}/`);
			else if (/\.bfhp$/i.test(e.name) && e.name.toLowerCase() !== `${skip}.bfhp`.toLowerCase()) {
				let entries;
				try {
					entries = readHpfs(fs.readFileSync(full)).entries;
				} catch {
					continue;
				}
				// The XML files with their text; the others by name only (the audit counts them).
				for (const x of entries) if (!x.directory) files.set(`${rel}${e.name}/${x.path}`, /\.xml$/i.test(x.path) ? x.data : Buffer.alloc(0));
			}
		}
	};
	walk(path.join(gbrDir, 'adventures'), '');
	return files;
}

// The cards of the two mods. Not redistributable: they are made from the player's own copy of Imperivm I.
function modJsons(opts) {
	const own = {
		spanish: ' Convertido desde tu propia copia de Imperivm I: para uso propio, no lo compartas.',
		english: ' Converted from your own copy of Imperivm I: for your own use, do not share it.',
		italian: ' Convertito dalla tua copia di Imperivm I: per uso personale, non condividerlo.',
	};
	const describe = (texts) => Object.fromEntries(Object.entries(texts).map(([k, v]) => [k, v + own[k]]));
	const common = { version: '1.0', authors: ['Imperivm Resurrected converter'], redistributable: false };
	return {
		adventure: {
			id: opts.modId,
			name: opts.name,
			...common,
			description: describe({
				spanish: 'La campaña de Imperivm I. Necesita el contenido "Imperivm I: clases de la campaña".',
				english: 'The campaign of Imperivm I. It needs the content "Imperivm I: campaign classes".',
				italian: 'La campagna di Imperivm I. Richiede il contenuto "Imperivm I: classi della campagna".',
			}),
			category: 'Adventures',
			tags: ['Language: Multilingual'],
			requires: [`${opts.modId}-data`],
		},
		data: {
			id: `${opts.modId}-data`,
			name: { spanish: 'Imperivm I: clases de la campaña', english: 'Imperivm I: campaign classes', italian: 'Imperivm I: classi della campagna' },
			...common,
			description: describe({
				spanish: 'Las unidades y edificios de Imperivm I que se comportan distinto en Great Battles of Rome (la tienda teutona, los ghouls, los teutones), solo para su campaña.',
				english: "Imperivm I's units and buildings that behave otherwise in Great Battles of Rome (the Teuton tent, the ghouls, the Teutons), for its campaign only.",
				italian: 'Le unità e gli edifici di Imperivm I che si comportano diversamente in Great Battles of Rome (la tenda teutonica, i ghoul, i teutoni), solo per la sua campagna.',
			}),
			category: 'Gameplay & Balance',
		},
	};
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
	const report = new Report();
	const source = i1Source(i1);
	const plan = planI1Classes(gbr, source, report, { manual: M.I1_CLASSES, substitutes: M.CLASS_SUBSTITUTES, keepGbr: M.I1_KEEP_GBR_CLASSES });
	const result = convertAdventure({
		files,
		gbr,
		i1,
		local: i1.local,
		advName: ADVENTURE,
		name: opts.name,
		keep: opts.keep,
		movies: movies && new Map([...movies].map(([k, v]) => [k, v.name])),
		report,
		i1Classes: plan,
	});
	const modFiles = i1ClassFiles(plan, gbr, source, result.classLiterals, report, M.AI_PROFILE);
	if (movies) {
		for (const [k, v] of movies) {
			if (!report.lines('Movies').some((l) => l.includes(v.name))) movies.delete(k);
		}
	}

	console.log('checking...');
	// The mod's class files and scripts are checked with the adventure's (as "mod/<path>").
	const checked = new Map([...result.files, ...[...modFiles].map(([p, buf]) => [`mod/${p}`, buf])]);
	const check = validateAdventure(checked, { gbr, i1Classes: i1.classes, customItems: result.customItems });
	const pkg = writeHpfs(result.files, 512, result.dirs);
	// The written package must read back to exactly the same files, and have the empty folders.
	const back = readHpfs(pkg);
	const backFiles = new Map(back.entries.filter((e) => !e.directory).map((e) => [e.path, e.data]));
	let roundTrip = backFiles.size === result.files.size;
	for (const [p, buf] of result.files) if (!backFiles.has(p) || !backFiles.get(p).equals(buf)) roundTrip = false;
	for (const d of result.dirs) if (!back.entries.some((e) => e.directory && e.path === d)) roundTrip = false;
	if (!roundTrip) check.errors.push('the written package does not read back to the same files');
	let audit = null;
	if (opts.audit) {
		console.log('auditing...');
		audit = auditAdventure({
			i1Files: files,
			outFiles: result.files,
			refFiles: referenceAdventures(opts.gbr, opts.name),
			gbr,
			i1,
			gbrExe: fs.readFileSync(findFile(opts.gbr, 'gbr.exe')),
			i1Exe: fs.readFileSync(findFile(opts.i1, 'Imperivm.exe')),
			actorRows: result.actorRows,
		});
		for (const e of audit.errors) check.errors.push(`audit: ${e}`);
		report.add('Audit', `${audit.errors.length} errors, ${audit.warnings.length} warnings (audit.txt)`);
	}
	const stats = check.stats;
	report.add(
		'Checks',
		`${stats.xml} XML files, ${stats.objects} map objects, ${stats.scripts} scripts, ${stats.grids} grids, ${stats.changeMaps} ChangeMap calls, ${stats.phrases} phrases, ${stats.voices} voice references`,
	);
	report.add('Checks', `package: ${result.files.size} files, ${(pkg.length / 1048576).toFixed(1)} MB, read back ${roundTrip ? 'identical' : 'DIFFERENT'}`);
	for (const e of check.errors) report.add('Check errors', e);
	for (const w of check.warnings) report.add('Check warnings', w);
	if (!check.errors.length) report.add('Check errors', 'none');

	const advDir = path.join(opts.out, opts.modId);
	const dataDir = path.join(opts.out, `${opts.modId}-data`);
	fs.mkdirSync(advDir, { recursive: true });
	const reportText = report.toText();
	if (audit) {
		fs.writeFileSync(path.join(advDir, 'audit.txt'), audit.text);
	}
	fs.writeFileSync(path.join(advDir, 'report.txt'), reportText);
	if (!opts.dryRun) {
		if (check.errors.length) {
			console.log(reportText);
			console.error(`${check.errors.length} errors: nothing written but report.txt`);
			process.exit(1);
		}
		const cards = modJsons(opts);
		fs.writeFileSync(path.join(advDir, 'mod.json'), JSON.stringify(cards.adventure, null, '\t') + '\n');
		fs.mkdirSync(path.join(advDir, 'adventures'), { recursive: true });
		fs.writeFileSync(path.join(advDir, 'adventures', opts.name + '.bfhp'), pkg);
		const map = files.get('global_map.bmp');
		if (map) fs.writeFileSync(path.join(advDir, 'adventures', opts.name + '.bmp'), writeBmp24(coverResize(fillKey(readBmp(map), [34, 26, 18]), 440, 360)));
		fs.mkdirSync(dataDir, { recursive: true });
		fs.writeFileSync(path.join(dataDir, 'mod.json'), JSON.stringify(cards.data, null, '\t') + '\n');
		for (const [p, buf] of modFiles) {
			const dest = path.join(dataDir, ...p.split('/'));
			fs.mkdirSync(path.dirname(dest), { recursive: true });
			fs.writeFileSync(dest, buf);
		}
		if (movies && movies.size) {
			fs.mkdirSync(path.join(dataDir, 'movies'), { recursive: true });
			for (const v of movies.values()) fs.copyFileSync(v.file, path.join(dataDir, 'movies', v.name));
		}
		if (opts.unpacked) {
			for (const d of result.dirs) fs.mkdirSync(path.join(opts.out, 'unpacked', ...d.split('/')), { recursive: true });
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
