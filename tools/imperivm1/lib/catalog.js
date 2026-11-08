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

// Classes from the .sc.xml files of a package: Map id -> {id, altid, parent, displayName, cpp}.
function readClasses(pak, prefix) {
	const classes = new Map();
	for (const e of pak.index.values()) {
		const up = e.name.toUpperCase();
		if (!up.startsWith(prefix) || !up.endsWith('.SC.XML')) continue;
		const text = pak.read(e.name).toString('latin1');
		const head = /<class\b[^>]*>/.exec(text);
		if (!head) continue;
		const id = attr(head[0], 'id');
		if (!id) continue;
		const live = text.replace(/<!--[\s\S]*?-->/g, '');
		const info = {
			id,
			altid: attr(head[0], 'altid'),
			parent: attr(head[0], 'parent'),
			cpp: attr(head[0], 'cpp_class'),
			entity: attr(head[0], 'entity'),
			displayName: attr(live, 'display_name'),
			icon: (/<properties\s+icon\s*=\s*"([^"]*)"/.exec(live) || [])[1] || null,
			sight: (/<properties\b[^>]*\ssight\s*=\s*"(\d+)"/.exec(live) || [])[1] || null,
			// Combat stats as the class file writes them (strings; null when the class does not set them).
			stats: Object.fromEntries(
				['minattack', 'maxattack', 'defense_slash', 'defense_pierce', 'maxhealth', 'damage', 'armor_slash', 'armor_pierce', 'speed', 'damage_type', 'range'].map((k) => [
					k,
					(new RegExp(`<properties\\b[^>]*\\s${k}\\s*=\\s*"([^"]*)"`).exec(live) || [])[1] || null,
				]),
			),
			methods: new Set([...live.matchAll(/<method\s+sig\s*=\s*"([^"]*)"/g)].map((m) => m[1].toLowerCase())),
			// sig (lower case) -> the script that runs the command, as the class file writes it.
			methodScripts: new Map([...live.matchAll(/<method\s+sig\s*=\s*"([^"]*)"\s+vs\s*=\s*"([^"]*)"/g)].map((m) => [m[1].toLowerCase(), m[2]])),
			behaviors: [...live.matchAll(/<behavior\s+script\s*=\s*"([^"]*)"/g)].map((m) => m[1]),
		};
		if (!classes.has(id)) classes.set(id, info);
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
	const byLower = new Map([...classes.values()].flatMap((c) => [[c.id.toLowerCase(), c], ...(c.altid ? [[c.altid.toLowerCase(), c]] : [])]));
	// Whether objects of a class can be given a command: a method of the class or of one of its parents.
	const classHasMethod = (id, sig) => {
		for (let c = byLower.get(id.toLowerCase()), n = 0; c && n < 32; c = c.parent && byLower.get(c.parent.toLowerCase()), n++) {
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

module.exports = { loadGbr, loadI1, readTerrains, readDecors, readFunctions, findFile };
