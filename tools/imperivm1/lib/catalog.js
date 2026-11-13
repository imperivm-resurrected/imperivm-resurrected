// What the converter needs to know about each game, read from the installed games themselves: class ids, terrain layers,
// decor types, items, icons and the script functions.
'use strict';
const fs = require('fs');
const path = require('path');
const { openPak } = require('./pak');

function findFile(dir, name) {
	const hit = fs.readdirSync(dir).find((f) => f.toLowerCase() === name.toLowerCase());
	if (!hit) throw new Error(`${name} not found in ${dir}`);
	return path.join(dir, hit);
}

function attr(text, name) {
	const m = new RegExp('\\s' + name + '\\s*=\\s*"([^"]*)"').exec(text);
	return m ? m[1] : null;
}

// One .sc.xml file: {id, altid, parent, cpp, entity, displayName, icon, sight, methods, methodScripts, behaviors, file}, or
// null when it is not a class.
function parseClass(text, file) {
	const head = /<class\b[^>]*>/.exec(text);
	if (!head) return null;
	const id = attr(head[0], 'id');
	if (!id) return null;
	const live = text.replace(/<!--[\s\S]*?-->/g, '');
	return {
		id,
		altid: attr(head[0], 'altid'),
		parent: attr(head[0], 'parent'),
		cpp: attr(head[0], 'cpp_class'),
		entity: attr(head[0], 'entity'),
		displayName: attr(live, 'display_name'),
		icon: (/<properties\s+icon\s*=\s*"([^"]*)"/.exec(live) || [])[1] || null,
		sight: (/<properties\b[^>]*\ssight\s*=\s*"(\d+)"/.exec(live) || [])[1] || null,
		methods: new Set([...live.matchAll(/<method\s+sig\s*=\s*"([^"]*)"/g)].map((m) => m[1].toLowerCase())),
		// sig (lower case) -> the script that runs the command, as the class file writes it.
		methodScripts: new Map([...live.matchAll(/<method\s+sig\s*=\s*"([^"]*)"\s+vs\s*=\s*"([^"]*)"/g)].map((m) => [m[1].toLowerCase(), m[2]])),
		behaviors: [...live.matchAll(/<behavior\s+script\s*=\s*"([^"]*)"/g)].map((m) => m[1]),
		file,
	};
}

// Classes from the .sc.xml files of a package: Map id -> parseClass(...), file being the name in the package.
function readClasses(pak, prefix) {
	const classes = new Map();
	for (const e of pak.index.values()) {
		const up = e.name.toUpperCase();
		if (!up.startsWith(prefix) || !up.endsWith('.SC.XML')) continue;
		const info = parseClass(pak.read(e.name).toString('latin1'), e.name);
		if (info && !classes.has(info.id)) classes.set(info.id, info);
	}
	return classes;
}

// Terrain layers: [{z, type, display, image}] from a TERRAINS.XML.
function readTerrains(text) {
	const layers = [];
	for (const m of text.matchAll(/<layer\b([^>]*)\/?>/g)) {
		const a = m[1];
		layers.push({
			z: Number(attr(a, 'z')),
			type: attr(a, 'type'),
			display: attr(a, 'display') || '',
			image: (attr(a, 'image') || '').toLowerCase().replace(/^assets\//, '').replace(/%season%/i, '%season%'),
		});
	}
	return layers;
}

// Decor types: Map type -> section name, from a decors.ini.
function readDecors(text) {
	const types = new Map();
	let section = null;
	for (const line of text.split(/\r?\n/)) {
		const s = /^\s*\[([^\]]+)\]/.exec(line);
		if (s) {
			section = s[1];
			continue;
		}
		const t = /^\s*type\s*=\s*(\d+)/i.exec(line);
		if (t && section) types.set(Number(t[1]), section);
	}
	return types;
}

// Number of parameters of a signature of sdk/game-functions.txt, written "ret(a, b)" or "ret, a, b"; null if unknown.
function paramCount(sig) {
	const s = sig.trim().replace(/\s+extra=\S+$/, '');
	let m;
	if ((m = /^[\w*]+\s*\((.*)\)$/.exec(s))) return m[1].trim() ? m[1].split(',').length : 0;
	if ((m = /^[\w*]+\s*,(.*)$/.exec(s))) return m[1].split(',').length;
	if (/^[\w*]+$/.test(s)) return 0;
	return null;
}

// Script functions of Great Battles of Rome from sdk/game-functions.txt: {functions: Set, methods: Set, arity: Map}.
// arity: "Name" or "Type::Name" -> Set of parameter counts of its overloads (a method's count includes the receiver).
function readFunctions(file) {
	const functions = new Set();
	const methods = new Set();
	const arity = new Map();
	for (const line of fs.readFileSync(file, 'latin1').split(/\r?\n/)) {
		const m = /^0x[0-9a-f]+\s+(?:L\s+)?(\S+)\s+\|([^|]*)\|/.exec(line);
		if (!m) continue;
		if (m[1].includes('::')) methods.add(m[1].split('::')[1]);
		else functions.add(m[1]);
		const n = paramCount(m[2]);
		if (n === null) continue;
		if (!arity.has(m[1])) arity.set(m[1], new Set());
		arity.get(m[1]).add(n);
	}
	return { functions, methods, arity };
}

function loadGbr(gbrDir, functionsFile) {
	const packs = path.join(gbrDir, 'Packs');
	const data = openPak(findFile(packs, 'data.pak'));
	const mapObjects = openPak(findFile(packs, 'MapObjects.pak'));
	const ui = openPak(findFile(packs, 'UI.pak'));
	const localPaks = new Map();
	const classes = readClasses(data, 'DATA\\CLASSES\\');
	const items = new Set();
	for (const m of data.read('DATA/ITEMS.XML').toString('latin1').matchAll(/<item\s+id="([^"]*)"/g)) items.add(m[1]);
	// "gameres/..." in the game's files is the UI.pak package, whose names start with UI\.
	const icons = new Set([...ui.index.keys()].map((k) => k.toLowerCase().replace(/\\/g, '/')));
	// Commands a script can give: the methods of the classes and the commands of the command bars.
	const commands = new Set();
	for (const e of data.index.values()) {
		const up = e.name.toUpperCase();
		if (!/^DATA\\(CLASSES|COMMANDS)\\.*\.XML$/.test(up)) continue;
		for (const m of data.read(e.name).toString('latin1').matchAll(/(?:\bsig|<cmd\s+name)\s*=\s*"([^"]*)"/g)) commands.add(m[1].toLowerCase());
	}
	// Whether objects of a class can be given a command: a method of the class or of one of its parents. Looked up in
	// `classes` at each call, so that classes added after loading (the campaign's own, lib/i1classes.js) count too.
	let index = null;
	let indexed = -1;
	const byLower = (id) => {
		if (indexed !== classes.size) {
			index = new Map([...classes.values()].flatMap((c) => [[c.id.toLowerCase(), c], ...(c.altid ? [[c.altid.toLowerCase(), c]] : [])]));
			indexed = classes.size;
		}
		return index.get(id.toLowerCase());
	};
	const classHasMethod = (id, sig) => {
		for (let c = byLower(id), n = 0; c && n < 32; c = c.parent && byLower(c.parent), n++) {
			if (c.methods.has(sig.toLowerCase())) return true;
		}
		return false;
	};
	return {
		classes,
		classHasMethod,
		terrains: readTerrains(data.read('DATA/TERRAINS.XML').toString('latin1')),
		decors: readDecors(mapObjects.read('MAPOBJECTS/DECORS/DECORS.INI').toString('latin1')),
		items,
		commands,
		hasGameres: (p) => icons.has('ui/' + p.toLowerCase().replace(/^gameres\//, '')),
		hasData: (p) => !!data.read(p),
		readData: (p) => data.read(p),
		// Names in data.pak (as the package writes them, with '\') that match the regular expression.
		dataNames: (re) => [...data.index.values()].map((e) => e.name).filter((n) => re.test(n)),
		// A file of the game's texts in a language (local\<lang>.pak), or null.
		readLocal: (lang, name) => {
			const dir = path.join(gbrDir, 'local');
			if (!fs.existsSync(dir)) return null;
			const hit = fs.readdirSync(dir).find((f) => f.toLowerCase() === `${lang.toLowerCase()}.pak`);
			if (!hit) return null;
			if (!localPaks.has(hit)) localPaks.set(hit, openPak(path.join(dir, hit)));
			return localPaks.get(hit).read(name);
		},
		...readFunctions(functionsFile),
	};
}

function loadI1(i1Dir) {
	const data = openPak(findFile(i1Dir, 'data.pak'));
	const assets = openPak(findFile(i1Dir, 'assets.pak'));
	const local = openPak(findFile(i1Dir, 'local.pak'));
	return {
		classes: readClasses(data, 'CLASSES\\'),
		terrains: readTerrains(assets.read('TERRAIN/TERRAINS.XML').toString('latin1')),
		decors: readDecors(assets.read('DECORS/DECORS.INI').toString('latin1')),
		items: data.read('ITEMS.XML').toString('latin1'),
		local,
		data,
	};
}

// The translations of a loc file of a package: Map text -> result (entries keyed by context, "...@...", are left out).
function locEntries(pak, names) {
	const out = new Map();
	for (const name of names) {
		const buf = pak && pak.read(name);
		if (!buf) continue;
		for (const m of buf.toString('utf8').matchAll(/<translationtableentry\b([^>]*)\/?>/g)) {
			const key = attr(m[1], 'text');
			const result = attr(m[1], 'result');
			if (key && result && !/@/.test(key) && !out.has(key)) out.set(key, result);
		}
	}
	return out;
}

// An older game as lib/i1classes.js ports it (Imperivm I, Imperivm II): {label, prefix of its classes in the campaign,
// dir of its files in the data mod, classes (Map id -> parseClass info), readClass(info), read('data/...'): a file of its
// data or null, commands (text of its command files), constants (its [GamePlay] .ini text), bonus (its bonus.xml),
// ai: Map profile ('' for the default one) -> [{name, read}], spanish: Map text -> its Spanish}.
function legacySource(fields) {
	return { ...fields, ai: fields.ai || new Map(), spanish: fields.spanish || new Map() };
}

// The AI scripts of a package under `root` (AI\ or DATA\AI\): its own files and, per profile (a folder), the profile's.
function aiFiles(pak, root) {
	const ai = new Map();
	for (const e of pak.index.values()) {
		const m = new RegExp(`^${root.replace(/\\/g, '\\\\')}(?:([^\\\\]+)\\\\)?([^\\\\]+\\.(?:vs|ini))$`, 'i').exec(e.name);
		if (!m) continue;
		const profile = m[1] || '';
		if (!ai.has(profile)) ai.set(profile, []);
		ai.get(profile).push({ name: m[2].toLowerCase(), read: () => pak.read(e.name) });
	}
	return ai;
}

function i1Source(i1) {
	return legacySource({
		label: 'Imperivm I',
		prefix: 'I1',
		dir: 'imperivm1',
		classes: i1.classes,
		readClass: (c) => i1.data.read(c.file),
		read: (p) => i1.data.read(p.replace(/^data[\\/]/i, '')),
		commands: (i1.data.read('COMMANDS.XML') || Buffer.alloc(0)).toString('latin1'),
		constants: (i1.data.read('VXCONST.INI') || Buffer.alloc(0)).toString('latin1'),
		bonus: (i1.data.read('BONUS.XML') || Buffer.alloc(0)).toString('latin1'),
		ai: aiFiles(i1.data, 'AI\\'),
		spanish: locEntries(i1.local, ['SPANISH/GAME.LOC.XML', 'SPANISH/TRANSLATION.LOC.XML']),
	});
}

// Imperivm II: its files are laid out as Great Battles of Rome's (Packs\data.pak under DATA\, Local\spanish.pak).
function loadI2(i2Dir) {
	const data = openPak(findFile(findFile(i2Dir, 'Packs'), 'data.pak'));
	const localDir = findFile(i2Dir, 'Local');
	const spanish = fs.readdirSync(localDir).find((f) => /^spanish\.pak$/i.test(f));
	const local = spanish ? openPak(path.join(localDir, spanish)) : null;
	const commands = [...data.index.values()].filter((e) => /^DATA\\COMMANDS\\[^\\]+\.XML$/i.test(e.name)).map((e) => data.read(e.name).toString('latin1'));
	const constText = (data.read('DATA/CONST.INI') || Buffer.alloc(0)).toString('latin1');
	return legacySource({
		label: 'Imperivm II',
		prefix: 'I2',
		dir: 'imperivm2',
		data,
		classes: readClasses(data, 'DATA\\CLASSES\\'),
		readClass: (c) => data.read(c.file),
		read: (p) => data.read(p),
		commands: commands.join('\r\n'),
		constants: (/^\[GamePlay\][^\r\n]*\r?\n([\s\S]*?)(?=^\[|(?![\s\S]))/im.exec(constText) || [])[1] || '',
		bonus: (data.read('DATA/BONUS.XML') || Buffer.alloc(0)).toString('latin1'),
		ai: aiFiles(data, 'DATA\\AI\\'),
		spanish: locEntries(local, ['CURRENTLANG/GAME.LOC.XML', 'CURRENTLANG/TRANSLATION.LOC.XML', 'SPANISH/GAME.LOC.XML', 'SPANISH/TRANSLATION.LOC.XML']),
	});
}

module.exports = { loadGbr, loadI1, loadI2, i1Source, legacySource, readTerrains, readDecors, readFunctions, findFile, parseClass };
