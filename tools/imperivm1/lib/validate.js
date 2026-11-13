// Offline checks of a converted adventure: what can be proved without starting the game.
'use strict';
const S = require('./script');
const X = require('./xml');
const G = require('./grid');

const KEYWORDS = new Set(['if', 'else', 'while', 'for', 'do', 'return', 'switch', 'case', 'break', 'continue', 'sizeof']);
// Members of the built-in types (point, rect...) that are not in the list of registered functions.
const BUILTIN_MEMBERS = new Set(['x', 'y', 'Set', 'left', 'top', 'right', 'bottom']);

// files: Map path -> Buffer of the converted adventure. ctx: {gbr, i1Classes, customItems}.
// Returns {errors: [], warnings: [], stats: {}}.
function validateAdventure(files, ctx) {
	const errors = [];
	const warnings = [];
	const stats = { xml: 0, scripts: 0, objects: 0, grids: 0, changeMaps: 0, phrases: 0, voices: 0 };
	const lower = (s) => s.toLowerCase();
	const keys = new Set([...files.keys()].map(lower));
	const has = (p) => keys.has(lower(p));
	const gbrClass = new Set();
	for (const c of ctx.gbr.classes.values()) {
		gbrClass.add(lower(c.id));
		if (c.altid) gbrClass.add(lower(c.altid));
	}
	const i1Class = new Set([...ctx.i1Classes.values()].flatMap((c) => [c.id, c.altid].filter(Boolean)).map(lower));
	const items = new Set([...ctx.gbr.items, ...ctx.customItems].map(lower));
	const parsed = new Map();

	// Every XML parses.
	for (const [p, buf] of files) {
		if (!/\.xml$/i.test(p)) continue;
		stats.xml++;
		try {
			parsed.set(p, X.parseXml(/^Local\//i.test(p) ? buf.toString('utf8') : buf.toString('latin1')));
		} catch (e) {
			errors.push(`${p}: ${e.message}`);
		}
	}
	for (const [p, doc] of parsed) {
		for (const r of doc.roots) {
			for (const el of X.walk(r)) {
				for (const [k, v] of el.attrs) {
					if (/^assets\//i.test(v)) errors.push(`${p}: ${k}="${v}" is an Imperivm I path`);
					if (/^gameres\/.*\.bmp$/i.test(v) && !ctx.gbr.hasGameres(v)) warnings.push(`${p}: picture ${v} not found`);
				}
			}
		}
	}

	// Map names, sizes and areas.
	const maps = new Map();
	for (const [p, doc] of parsed) {
		const m = /^Maps\/([^/]+)\/map\.xml$/i.exec(p);
		if (!m) continue;
		const size = doc.root.children.find((c) => c.name === 'size');
		maps.set(m[1], { name: doc.root.attrs.get('name'), x: size && Number(size.attrs.get('x')), y: size && Number(size.attrs.get('y')) });
		if (doc.root.attrs.get('persist_state') !== '1') errors.push(`${p}: persist_state is not 1`);
	}
	const mapByName = new Map([...maps].map(([id, v]) => [v.name, id]));

	// Classes of every object, and items in inventories.
	for (const [p, doc] of parsed) {
		if (!/map\.obj\.xml$/i.test(p)) continue;
		for (const el of X.walk(doc.root)) {
			for (const a of ['class', 'classoffirstbuilding']) {
				const v = el.attrs.get(a);
				if (!v) continue;
				stats.objects++;
				if (!gbrClass.has(lower(v))) errors.push(`${p}: class ${v} is not a class of Great Battles of Rome`);
			}
			for (const [k, v] of el.attrs) {
				if (/^slot\d+$/.test(k) && v && !items.has(lower(v))) warnings.push(`${p}: item "${v}" is not an item of Great Battles of Rome`);
				if (/^icon$/i.test(k) && v && /^gameres\//i.test(v) && !ctx.gbr.hasGameres(v)) warnings.push(`${p}: picture ${v} not found`);
				if (/^icon$/i.test(k) && /^assets\//i.test(v)) errors.push(`${p}: picture ${v} is an Imperivm I path`);
			}
		}
	}

	// Scripts: sequence files and inline item scripts.
	const scripts = [];
	for (const [p, buf] of files) if (/\.vs$/i.test(p)) scripts.push([p, buf.toString('latin1')]);
	for (const [p, doc] of parsed) {
		for (const el of X.walk(doc.root)) {
			for (const [k, v] of el.attrs) if (/_script$/.test(k) && v && !/^file:\/\//.test(v)) scripts.push([`${p} (${el.attrs.get('id') || el.name} ${k})`, X.gameUnescape(v)]);
		}
	}
	const missing = new Map();
	for (const [p, src] of scripts) {
		stats.scripts++;
		for (const c of S.listCalls(src)) {
			if (KEYWORDS.has(c.name)) continue;
			const known = c.method ? ctx.gbr.methods.has(c.name) || ctx.gbr.functions.has(c.name) || BUILTIN_MEMBERS.has(c.name) : ctx.gbr.functions.has(c.name);
			if (known) continue;
			const k = (c.method ? '.' : '') + c.name;
			if (!missing.has(k)) missing.set(k, new Set());
			missing.get(k).add(p);
		}
		for (const t of S.tokenize(src)) {
			if (t.type !== 'string') continue;
			const body = t.text.slice(1, -1);
			if (i1Class.has(lower(body)) && !gbrClass.has(lower(body)) && !ctx.gbr.commands.has(lower(body))) warnings.push(`${p}: string "${body}" is a class of Imperivm I only`);
			if (/^assets\//i.test(body)) errors.push(`${p}: "${body}" is an Imperivm I path`);
			if (/^gameres\//i.test(body) && !ctx.gbr.hasGameres(body)) warnings.push(`${p}: picture ${body} not found`);
		}
		const tokens = S.tokenize(src).filter((t) => t.type !== 'space' && t.type !== 'comment');
		for (let i = 0; i + 2 < tokens.length; i++) {
			if (tokens[i].text === 'ChangeMap' && tokens[i + 1].text === '(' && tokens[i + 2].type === 'string') {
				stats.changeMaps++;
				const target = tokens[i + 2].text.slice(1, -1);
				if (!mapByName.has(target)) errors.push(`${p}: ChangeMap to "${target}", which is not the name of a map`);
				const area = tokens[i + 4] && tokens[i + 4].type === 'string' ? tokens[i + 4].text.slice(1, -1) : null;
				const objs = files.get(`Maps/${mapByName.get(target)}/map.obj.xml`);
				if (area && objs && !objs.toString('latin1').includes(`"${area}"`)) warnings.push(`${p}: area "${area}" not found in map ${target}`);
			}
			if (/^(SetCommand|AddCommand|ExecCmd|RegisterPlayerUpgrade|RegisterSettlementUpgrade)$/.test(tokens[i].text) && tokens[i + 1].text === '(') {
				let depth = 0;
				for (let k = i + 2; k < Math.min(tokens.length, i + 12) && depth >= 0; k++) {
					if (tokens[k].text === '(') depth++;
					if (tokens[k].text === ')') depth--;
					if (tokens[k].type !== 'string' || depth) continue;
					stats.commands = (stats.commands || 0) + 1;
					const cmd = tokens[k].text.slice(1, -1);
					if (!ctx.gbr.commands.has(lower(cmd))) warnings.push(`${p}: command "${cmd}" does not exist in Great Battles of Rome`);
					break;
				}
			}
			if (/^(AddItem|FindItem|RemoveItem)$/.test(tokens[i].text) && tokens[i + 1].text === '(' && tokens[i + 2].type === 'string') {
				const item = tokens[i + 2].text.slice(1, -1);
				if (!items.has(lower(item))) warnings.push(`${p}: item "${item}" is not an item of Great Battles of Rome or of the adventure`);
			}
		}
	}
	for (const [k, where] of missing) errors.push(`script function ${k} does not exist in Great Battles of Rome (${[...where].slice(0, 4).join(', ')}${where.size > 4 ? ', ...' : ''})`);

	// Sequence files that sequences.xml runs exist.
	for (const [p, doc] of parsed) {
		const m = /^(Maps\/[^/]+\/)?Sequences\/sequences\.xml$/i.exec(p);
		if (!m) continue;
		for (const el of X.walk(doc.root)) {
			const s = el.attrs.get('script');
			if (!s) continue;
			const rel = s.replace(/^Current(Map|Game)\//i, '');
			if (!has((m[1] || '') + rel)) errors.push(`${p}: runs ${s}, which is missing`);
		}
	}

	// Grids: bits and sizes of Great Battles of Rome, and the size of the map.
	const bitsOf = { terrain: 8, trans: 4, decor: 16, pass: 1, height: 8, light: 8 };
	for (const [p, buf] of files) {
		const m = /^Maps\/([^/]+)\/Terrain\.(\w+)\.grid$/i.exec(p);
		if (!m) continue;
		stats.grids++;
		try {
			const g = G.readGrid(buf);
			const want = bitsOf[lower(m[2])];
			if (want && g.bits !== want) errors.push(`${p}: ${g.bits} bits per cell, Great Battles of Rome uses ${want}`);
			const map = maps.get(m[1]);
			if (map && (g.width !== map.x || g.height !== map.y)) errors.push(`${p}: ${g.width}x${g.height}, the map is ${map.x}x${map.y}`);
			if (lower(m[2]) === 'terrain') {
				const layers = new Set(ctx.gbr.terrains.map((l) => l.z));
				for (const v of new Set(G.cellsOf(g))) if (!layers.has(v)) errors.push(`${p}: terrain layer ${v} does not exist`);
			}
		} catch (e) {
			errors.push(`${p}: ${e.message}`);
		}
	}

	// Conversations: every phrase of every map has its text in each language and its voice where voices are shipped.
	for (const [p, doc] of parsed) {
		const m = /^Maps\/([^/]+)\/Conversations\/(cnv\d+)\.conv\.xml$/i.exec(p);
		if (!m) continue;
		const phrases = [...X.walk(doc.root)].filter((e) => e.name === 'phrase').length;
		stats.phrases += phrases;
		for (const lang of ['English', 'Spanish', 'Italian']) {
			const loc = `Local/${lang}/Maps/${m[1]}/conversations/${lower(m[2])}.conv.xml`;
			const ldoc = [...parsed].find(([k]) => lower(k) === lower(loc));
			if (!ldoc) {
				warnings.push(`${p}: no ${lang} text (${loc})`);
				continue;
			}
			for (const e of X.walk(ldoc[1].root)) {
				const sound = e.attrs.get('sound');
				if (!sound) continue;
				if (has(`Local/${lang}/Maps/${sound}`)) stats.voices++;
				else errors.push(`${loc}: voice ${sound} is missing`);
			}
		}
	}
	return { errors, warnings, stats };
}

module.exports = { validateAdventure };
