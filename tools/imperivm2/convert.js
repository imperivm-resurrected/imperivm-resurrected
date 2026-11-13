// Converts the adventures of Imperivm II (La Conquista de Hispania) to Great Battles of Rome HD with the same machinery as
// Imperivm I's (tools/imperivm1/lib): every unit, hero and building of Imperivm II gets its own class in the data mod
// (I2<Class>, with its properties, scripts, commands and combat values for the mod's DLL), its AI is the I2 profile, and
// each adventure is a mod of its own that requires the data mod.
//
//   node tools/imperivm2/convert.js --i2 "<Imperivm II folder>" --gbr "<Great Battles of Rome folder>" --out <folder>
//        [--adventure "<file>.BFHP"]...
//
//   --adventure   one of Imperivm II's adventures (Hannibal's Campaigns.BFHP, The Punic Wars.BFHP, Numantia.BFHP,
//                 Tutorial.BFHP); without it, all but the tutorial, which teaches Imperivm II's interface, not the HD's
//
// Writes <out>\imperivm2-data\ (the classes, scripts, commands, AI and movies the adventures use), one
// <out>\imperivm2-<adventure>\ per adventure and <out>\report.txt. The output is a conversion of your own copy of the
// game, for your own use: do not redistribute it.
'use strict';
const fs = require('fs');
const path = require('path');
const { loadGbr, loadI2, findFile, readFunctions } = require('../imperivm1/lib/catalog');
const { readHpfs, writeHpfs } = require('../imperivm1/lib/hpfs');
const { planI1Classes, specialityClasses, i1ClassFiles, portScript, readConstants } = require('../imperivm1/lib/i1classes');
const { Report } = require('../imperivm1/lib/adventure');
const S = require('../imperivm1/lib/script');
const M = require('../imperivm1/lib/mapping');
const { gameEscape, gameUnescape } = require('../imperivm1/lib/xml');

const DATA_ID = 'imperivm2-data';
const AI_PROFILE = 'I2';
const TITLES = {
	"hannibal's campaigns.bfhp": 'Las campañas de Aníbal',
	'the punic wars.bfhp': 'Las guerras púnicas',
	'numantia.bfhp': 'Numancia',
	'tutorial.bfhp': 'Tutorial',
};
const TUTORIAL = 'tutorial.bfhp';
// Imperivm II's races under Great Battles of Rome's names.
const RACES = { rome: 'RepublicanRome', roman: 'RepublicanRome', iberian: 'Iberia' };
// Classes that keep Great Battles of Rome's version: Imperivm II's Stonehenge casts its rituals with natives Great Battles
// of Rome does not have (IsSpellCasted, GoldenRain, FinalSacrifice, TeutonDisaster).
const KEEP_GBR = { stonehenge: 'its rituals need natives Great Battles of Rome lacks', Stonehenge1: 'idem', Stonehenge2: 'idem', Shrine: 'a Stonehenge' };

const lower = (s) => s.toLowerCase();
const text = (b) => Buffer.from(b).toString('latin1');
const bytes = (s) => Buffer.from(s, 'latin1');
const attr = (tag, name) => (new RegExp(`\\s${name}\\s*=\\s*"([^"]*)"`, 'i').exec(tag) || [])[1];
const replaceAttr = (xml, name, fn) => xml.replace(new RegExp(`(\\s${name}\\s*=\\s*")([^"]*)(")`, 'gi'), (all, a, v, b) => a + fn(v) + b);
const modId = (file) => 'imperivm2-' + lower(file.replace(/\.bfhp$/i, '')).replace(/['’]/g, '').replace(/[^a-z0-9]+/g, '-');

function parseArgs(argv) {
	const opts = { adventures: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		const value = () => {
			if (i + 1 >= argv.length) throw new Error(`${a} needs a value`);
			return argv[++i];
		};
		if (a === '--i2') opts.i2 = value();
		else if (a === '--gbr') opts.gbr = value();
		else if (a === '--out') opts.out = value();
		else if (a === '--adventure') opts.adventures.push(value());
		else throw new Error(`unknown option ${a}`);
	}
	if (!opts.i2 || !opts.gbr || !opts.out) throw new Error('--i2, --gbr and --out are required');
	return opts;
}

// Classes Great Battles of Rome lacks under Imperivm II's id but has with the same picture (IHero01 is IHero1).
function sameEntitySubstitutes(gbr, src) {
	const byEntity = new Map();
	for (const c of gbr.classes.values()) if (c.entity && !byEntity.has(lower(c.entity))) byEntity.set(lower(c.entity), c.id);
	const ids = new Set([...gbr.classes.values()].flatMap((c) => [c.id, c.altid].filter(Boolean)).map(lower));
	const out = {};
	for (const c of new Set(src.classes.values())) {
		if (ids.has(lower(c.id)) || (c.altid && ids.has(lower(c.altid))) || !c.entity) continue;
		const to = byEntity.get(lower(c.entity));
		if (to) out[c.id] = to;
	}
	return out;
}

// Every name the game's files use for a class of Imperivm II -> the campaign's class.
function classLiterals(gbr, src, plan, substitutes) {
	const gbrIds = new Map([...gbr.classes.values()].flatMap((c) => [c.id, c.altid].filter(Boolean).map((n) => [lower(n), n])));
	const out = Object.create(null);
	for (const c of new Set(src.classes.values())) {
		for (const name of [c.id, c.altid].filter(Boolean)) {
			const to = plan.byI1.get(lower(name)) || plan.byI1.get(lower(c.id)) || gbrIds.get(lower(name)) || substitutes[c.id] || substitutes[name];
			if (to && to !== name) out[name] = to;
		}
	}
	return out;
}

// A script of an adventure, or a data script it runs: Imperivm II's code with the campaign's classes, Great Battles of
// Rome's natives, the I2 AI, and its movies and data scripts where the data mod has them.
function adventureScript(src, ctx, where) {
	for (const line of S.druidTargeting(src).unsupported) ctx.report.add('Scripts', `${where}:${line}: FindUnit2HTL with arguments it cannot convert`);
	let s = S.expandReduceGroup(src).src;
	s = portScript(s, ctx.literals, ctx.constants, {}, 'Imperivm II', ctx.specialities).src;
	s = S.aiProfile(s, AI_PROFILE).src;
	s = s.replace(/(["'])movies[\\/]+([^"'\\/]+\.avi)\1/gi, (all, q, movie) => {
		const file = ctx.movies.get(lower(movie));
		if (!file) {
			ctx.report.add('Movies', `${where}: ${movie} is not in Imperivm II's Movies folder`);
			return all;
		}
		ctx.files.set(`movies/imperivm2-${lower(movie)}`, fs.readFileSync(file));
		return `${q}movies\\\\imperivm2-${lower(movie)}${q}`;
	});
	return s.replace(/(["'])(data[\\/][^"'\r\n]+\.vs)\1/gi, (all, q, name) => {
		const k = lower(name.replace(/\\/g, '/'));
		if (!ctx.src.read(k)) {
			ctx.report.add('Scripts', `${where}: ${name} is not in Imperivm II`);
			return all;
		}
		const to = `data/mod/imperivm2/${path.posix.basename(k)}`;
		if (!ctx.files.has(to) && !ctx.pending.has(k)) ctx.pending.set(k, to);
		return q + to + q;
	});
}

function convertAdventure(file, buf, ctx) {
	const pkg = readHpfs(buf);
	const out = new Map();
	for (const e of pkg.entries) {
		if (e.directory) continue;
		const name = lower(e.path);
		let data = Buffer.from(e.data);
		if (/\.vs$/.test(name)) data = bytes(adventureScript(text(data), ctx, `${file}:${e.path}`));
		else if (name === 'game.xml') data = bytes(text(data).replace(/\sadventure\s*=\s*"1"/i, ' game_type="1"'));
		else if (/^player\d+\.xml$/.test(name)) data = bytes(replaceAttr(text(data), 'race', (r) => RACES[lower(r)] || r));
		else if (/\/map\.xml$/.test(name) && !/\spersist_state\s*=/.test(text(data))) data = bytes(text(data).replace(/<map\b/, '<map persist_state="1"'));
		else if (/\/map\.obj\.xml$/.test(name)) {
			let xml = text(data);
			for (const a of ['class', 'classoffirstbuilding']) xml = replaceAttr(xml, a, (c) => ctx.literals[c] || ctx.literalsLower.get(lower(c)) || c);
			data = bytes(xml);
		} else if (!name.startsWith('local/') && /(^|\/)notes\.xml$/.test(name)) {
			// Great Battles of Rome's notes have a title and say whether they show on the minimap.
			data = bytes(
				text(data).replace(/<note\b[^>]*>/gi, (tag) => {
					const id = attr(tag, 'id');
					if (attr(tag, 'title') !== undefined || !id) return tag;
					const onMap = Number(attr(tag, 'locationx') ?? -1) >= 0 && Number(attr(tag, 'locationy') ?? -1) >= 0;
					return tag.replace(/^<note\b/i, `<note title="${id}" show_on_minimap="${onMap ? 1 : 0}"`);
				}),
			);
		} else if (name === 'itemscustom.xml') {
			data = bytes(
				text(data).replace(/(\s\w+_script\s*=\s*")([^"]*)(")/gi, (all, a, v, b) =>
					!v || /^file:\/\//i.test(v) ? all : a + gameEscape(adventureScript(gameUnescape(v), ctx, `${file}:itemsCustom.xml`)) + b,
				),
			);
		}
		out.set(e.path, data);
	}
	const written = writeHpfs(out, pkg.pageSize, pkg.entries.filter((e) => e.directory).map((e) => e.path));
	const check = readHpfs(written).entries.filter((e) => !e.directory);
	if (check.length !== out.size || check.some((e) => !out.get(e.path) || !out.get(e.path).equals(Buffer.from(e.data)))) throw new Error(`${file}: the package does not read back`);
	return written;
}

function main() {
	const opts = parseArgs(process.argv.slice(2));
	const functionsFile = path.join(__dirname, '..', '..', 'sdk', 'game-functions.txt');
	console.log('reading Great Battles of Rome...');
	const gbr = loadGbr(opts.gbr, functionsFile);
	console.log('reading Imperivm II...');
	const src = loadI2(opts.i2);
	const advDir = findFile(opts.i2, 'Adventures');
	const all = fs.readdirSync(advDir).filter((f) => /\.bfhp$/i.test(f));
	const wanted = opts.adventures.length ? opts.adventures.map((a) => all.find((f) => lower(f) === lower(a)) || a) : all.filter((f) => lower(f) !== TUTORIAL);
	for (const a of wanted) if (!all.includes(a)) throw new Error(`${a} is not in ${advDir}`);
	const moviesDir = findFile(opts.i2, 'Movies');
	const movies = new Map(fs.readdirSync(moviesDir).filter((f) => /\.avi$/i.test(f)).map((f) => [lower(f), path.join(moviesDir, f)]));

	const report = new Report();
	const substitutes = { ...M.CLASS_SUBSTITUTES, ...sameEntitySubstitutes(gbr, src) };
	for (const [from, to] of Object.entries(sameEntitySubstitutes(gbr, src))) report.add('Classes', `${from} -> ${to} (the same picture)`);
	const plan = planI1Classes(gbr, src, report, { manual: [], substitutes, keepGbr: KEEP_GBR });
	const literals = classLiterals(gbr, src, plan, substitutes);
	const ctx = {
		src,
		report,
		literals,
		literalsLower: new Map(Object.entries(literals).map(([k, v]) => [lower(k), v])),
		constants: readConstants(src.constants),
		specialities: specialityClasses(plan),
		movies,
		files: new Map(),
		pending: new Map(),
	};

	const outputs = [];
	for (const file of wanted) {
		console.log(`converting ${file}...`);
		const id = modId(file);
		const title = 'Imperivm II - ' + file.replace(/\.bfhp$/i, '');
		const pkg = convertAdventure(file, fs.readFileSync(path.join(advDir, file)), ctx);
		const card = { id, version: '0.2.0', redistributable: false, category: 'Adventures', tags: ['Language: Spanish'], requires: [DATA_ID], name: { spanish: `Imperivm II - ${TITLES[lower(file)] || title}`, english: title } };
		outputs.push([`${id}/mod.json`, Buffer.from(JSON.stringify(card, null, 2) + '\n')], [`${id}/adventures/${title}.bfhp`, pkg]);
	}
	// The data scripts the adventures run (Run("data/...")), with the scripts they run in turn.
	while (ctx.pending.size) {
		const [k, to] = ctx.pending.entries().next().value;
		ctx.pending.delete(k);
		ctx.files.set(to, bytes(adventureScript(text(src.read(k)), ctx, k)));
	}
	const data = i1ClassFiles(plan, gbr, src, literals, report, AI_PROFILE);
	for (const [k, v] of ctx.files) if (!data.has(k)) data.set(k, v);

	// Script functions Great Battles of Rome does not have.
	const fns = readFunctions(functionsFile);
	const unknown = new Map();
	const scripts = [...data].filter(([k]) => /\.vs$/.test(k)).map(([k, v]) => [k, text(v)]);
	for (const [k, v] of scripts) {
		for (const c of S.listCalls(v)) {
			if (['if', 'for', 'while', 'switch', 'return', 'sizeof'].includes(c.name) || (c.method ? fns.methods.has(c.name) : fns.functions.has(c.name))) continue;
			const key = (c.method ? '.' : '') + c.name;
			if (!unknown.has(key)) unknown.set(key, new Set());
			unknown.get(key).add(k);
		}
	}
	for (const [k, where] of unknown) report.add('Script functions Great Battles of Rome lacks', `${k}: ${[...where].slice(0, 4).join(', ')}${where.size > 4 ? ` +${where.size - 4}` : ''}`);

	const dataCard = { id: DATA_ID, version: '0.2.0', redistributable: false, category: 'Gameplay & Balance', name: { spanish: 'Imperivm II: clases de las campañas', english: 'Imperivm II: campaign classes' } };
	outputs.push([`${DATA_ID}/mod.json`, Buffer.from(JSON.stringify(dataCard, null, 2) + '\n')], ...[...data].map(([k, v]) => [`${DATA_ID}/${k}`, v]));
	for (const [name] of outputs) if (fs.existsSync(path.join(opts.out, name.split('/')[0], 'mod.json'))) throw new Error(`${path.join(opts.out, name.split('/')[0])} already exists: use a new output folder`);
	for (const [name, buf] of outputs) {
		const dest = path.join(opts.out, name);
		fs.mkdirSync(path.dirname(dest), { recursive: true });
		fs.writeFileSync(dest, buf);
	}
	fs.writeFileSync(path.join(opts.out, 'report.txt'), report.toText());
	console.log(`${wanted.length} adventures, ${plan.defs.length} classes, ${scripts.length} scripts; ${unknown.size} script functions Great Battles of Rome lacks (report.txt)`);
	if (unknown.size) process.exitCode = 1;
}

try {
	main();
} catch (error) {
	console.error(error.message);
	process.exitCode = 1;
}
