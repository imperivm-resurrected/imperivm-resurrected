// Validates an Imperivm Resurrected mod without opening the game (SDK).

const LEGACY_KEYS = {"nombre":"name","descripcion":"description","autores":"authors","redistribuible":"redistributable","licencia":"license","cargarDespuesDe":"loadAfter","cargarAntesDe":"loadBefore","etiquetas":"tags","categoria":"category","tipo":"kind","idioma":"language","codigo":"code","ordenes":"commands","fuentes":"sources","archivos":"files","carpeta":"folder","perfil":"profile","requiere":"requires","incompatibleCon":"incompatibleWith","icono":"icon","indice":"index","bioma":"biome","plantilla":"template","nombreInterno":"internalName","letra":"letter","aleatoria":"random","ia":"ai","iaPropia":"ownAi","clases":"classes","valores":"values","defecto":"default"};
// Keys written with the Spanish names (before 0.31.51) still work: they are copied to the English ones before checking.
function modernKeys(o) {
	if (o && typeof o === 'object' && !Array.isArray(o)) {
		for (const [old, now] of Object.entries(LEGACY_KEYS)) if (o[old] !== undefined && o[now] === undefined) o[now] = o[old];
		if (o.alias && typeof o.alias === 'object') modernKeys(o.alias);
	}
	return o;
}

const LEGACY_DIRECTIVES = { principio: 'top', final: 'bottom', antes: 'before', despues: 'after', reemplazar: 'replace', fin: 'end' };
//
//   node validate-mod.js <mod folder> [--data <DATA folder of the extracted data.pak>]
//
// Checks the same things the game would check when loading it (and a few more), and reports them with file and line:
//   mod.json, allowed extensions, civs\*.json, specials\*.json, patches (XML and INI), classes, orders, texts (loc),
//   scripts (first line with the signature, events, Mod_* functions) and the UNIT_SPECIALS.INI bug.
// With --data it also checks that the files the mod patches exist in the game and that the classes and scripts it
// refers to exist. With --game (the game folder, read-only) it also checks the images and entities that the mod
// takes from the game itself (Units.pak, Buildings.pak, MapObjects.pak, Visuals.pak).
// Graphics (.ent.xml and .rle): missing images (the game HANGS when loading the class), repeated idx or outside 1..32, layers and
// animations that point to images that do not exist, variations different from columns, rows/columns that do not match the .rle,
// engine limits (32 images, 32 layers, 8 states, animations 1..28, 64 frames), corrupt .rle files and malformed .pass files.
// Exits with code 1 if there are errors.
const fs = require('fs'), path = require('path');

const args = process.argv.slice(2);
const modDir = args.find(a => !a.startsWith('--') && !['--data', '--game', '--mods'].includes(args[args.indexOf(a) - 1]));
const modsIdx = args.indexOf('--mods');
const dataIdx = args.indexOf('--data');
const dataDir = dataIdx >= 0 ? args[dataIdx + 1] : null;
const gameIdx = args.indexOf('--game');
const gameDir = gameIdx >= 0 ? args[gameIdx + 1] : null;  // game folder: the names of the graphics .pak files are read (read-only)
if (!modDir || !fs.existsSync(modDir) || !fs.statSync(modDir).isDirectory()) {
	console.error('usage: node validate-mod.js <mod folder> [--data <DATA folder of the extracted data.pak>] [--game <game folder>] [--mods <folder with the mods it requires>]');
	process.exit(2);
}

// --- what the game knows ----------------------------------------------------------------------------------------------
const ALLOWED = ['.pak', '.bfhp', '.xml', '.ini', '.vs', '.bmp', '.tga', '.png', '.jpg', '.wav', '.ogg', '.mp3', '.rle', '.mmp', '.tail', '.txt',
	'.json', '.loc', '.dat', '.sgt', '.dls', '.sty', '.avi', '.wmv', '.bik', '.fnt', '.ttf', '.md', '.pdf', '.html', '.htm', '.rtf', '.csv',
	'.sc', '.ent', '.cfg', '.lst', '.42', '.usr', '.sdw', '.pass'];
const FORBIDDEN = ['.exe', '.dll', '.asi', '.bat', '.cmd', '.com', '.scr', '.ps1', '.vbs', '.js', '.msi', '.sys', '.cpl', '.lnk', '.reg', '.jar', '.hta', '.wsf', '.pif'];
const GAME_CIVS = ['Gaul', 'RepublicanRome', 'Carthage', 'Iberia', 'ImperialRome', 'Britain', 'Egypt', 'Germany'];
const GAME_SPECIALS = ['Parry', 'Drain', 'Ferocity', 'Triple strike', 'Deflection', 'Active', 'Life steal', 'Determination', 'Combat skill',
	'Toughness', 'Offensive tactics', 'Defensive tactics', 'Penetration', 'Spike armor', 'Bleeding attack', 'Attack skill', 'Defense skill',
	'Death blow', 'Power strike', 'Expertise', 'Charge', 'Regeneration', 'Rage', 'Revenge', 'Triumph', 'Disease attack', 'Sneak', 'Invisibility',
	'Learning', 'Teaching', 'Freedom', 'Cripple', 'Healing', 'Keen sight', 'Trample', 'Curse'];
const EVENTS = {
	OnGameStart: ['void'],
	OnObjectDie: ['void', 'Obj'],
	OnObjectKill: ['void', 'Obj', 'Obj'],
	OnSettlementCaptured: ['void', 'Obj', 'int', 'int'],
	OnPlayerDefeated: ['void', 'int'],
	OnAttackResolved: ['void', 'Obj', 'Obj', 'int'],
};
const MOD_FUNCTIONS = ['Mod_Log', 'Mod_LogInt', 'Mod_IsActive', 'Mod_ApiVersion', 'Mod_SetInt', 'Mod_GetInt', 'Mod_AddInt', 'Mod_HasKey',
	'Mod_AttackDamage', 'Mod_SetAttackDamage', 'Mod_HasSpecial', 'Mod_SetSpecial', 'Mod_TrueRace', 'Mod_GetOption', 'Mod_GetOptionValue', 'Mod_PlayerCiv', 'Mod_CanExecCmd'];
const ID_RE = /^[A-Za-z][A-Za-z0-9_]{0,23}$/;

// --- report -----------------------------------------------------------------------------------------------------------
// kind is internal ('ERROR' | 'AVISO'); the printed label depends on the language
let errors = 0, warnings = 0;
const LABEL_ERROR = () => 'ERROR';
const LABEL_WARN = () => 'WARNING';
const report = (kind, file, line, msg) => {
	if (kind === 'ERROR') errors++; else warnings++;
	console.log(`${kind === 'ERROR' ? LABEL_ERROR() : LABEL_WARN()}  ${file}${line ? ':' + line : ''}  ${msg}`);
};
const error = (file, line, msg) => report('ERROR', file, line, msg);
const warn = (file, line, msg) => report('AVISO', file, line, msg);

// --- files ------------------------------------------------------------------------------------------------------------
function walk(dir, rel = '') {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const r = rel ? rel + '/' + e.name : e.name;
		if (e.isDirectory()) out.push(...walk(path.join(dir, e.name), r)); else out.push(r);
	}
	return out;
}
const files = walk(modDir).filter(f => !f.startsWith('.'));
const read = rel => fs.readFileSync(path.join(modDir, rel), 'utf8').replace(/^﻿/, '');
const lineOf = (text, index) => text.slice(0, index).split('\n').length;
const lower = s => s.toLowerCase();
const has = rel => files.some(f => lower(f) === lower(rel));
// The game treats the data.pak folders as case-insensitive: they are looked up ignoring case
function gameFile(rel) {
	if (!dataDir) return null;
	let cur = dataDir;
	for (const part of rel.replace(/^data\//i, '').split('/')) {
		const found = fs.existsSync(cur) && fs.readdirSync(cur).find(f => lower(f) === lower(part));
		if (!found) return null;
		cur = path.join(cur, found);
	}
	return cur;
}

// --- the mods this one requires -----------------------------------------------------------------------------------------
// What mod.json "requires" is the mod's to use: its classes, entities, sprites, scripts and civilizations are not missing. Each required mod is
// looked up (by folder name or by the "id" of its mod.json, and what it requires in turn) in --mods <folder>, in <game>\mods with --game, and next
// to this mod's folder.
const depRoots = [...(modsIdx >= 0 ? [args[modsIdx + 1]] : []), ...(gameDir ? [path.join(gameDir, 'mods')] : []), path.dirname(path.resolve(modDir))]
	.filter(d => d && fs.existsSync(d) && fs.statSync(d).isDirectory());
const depFiles = new Set(), depClassIds = new Set(), depCivs = new Set(), depNotFound = [];
function loadDeps(ids, seen = new Set()) {
	for (const id of ids) {
		if (typeof id !== 'string' || seen.has(lower(id))) continue;
		seen.add(lower(id));
		let dir = null, json = null;
		for (const root of depRoots) {
			for (const e of fs.readdirSync(root, { withFileTypes: true })) {
				const p = path.join(root, e.name);
				if (path.resolve(p) === path.resolve(modDir) || !fs.statSync(p, { throwIfNoEntry: false })?.isDirectory()) continue;  // (a link to a folder counts)
				let j = null;
				try { j = modernKeys(JSON.parse(fs.readFileSync(path.join(p, 'mod.json'), 'utf8').replace(/^\uFEFF/, ''))); } catch { /* no mod.json */ }
				if (lower(e.name) === lower(id) || (j && typeof j.id === 'string' && lower(j.id) === lower(id))) { dir = p; json = j; break; }
			}
			if (dir) break;
		}
		if (!dir) { depNotFound.push(id); continue; }
		for (const f of walk(dir)) {
			depFiles.add(lower(f));
			if (/^data\/classes\/.+\.sc\.xml$/i.test(f)) {
				const h = fs.readFileSync(path.join(dir, f), 'latin1').match(/<class\b[^>]*>/);
				for (const x of (h && h[0].match(/\b(?:id|altid)\s*=\s*"([^"]+)"/g)) || []) depClassIds.add(lower(x.replace(/^[a-z]+\s*=\s*"|"$/gi, '')));
			}
			else if (/^civs\/[^/]+\.json$/i.test(f)) {
				try { const c = modernKeys(JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8').replace(/^\uFEFF/, ''))); if (typeof c.id === 'string') depCivs.add(lower(c.id)); } catch { /* not valid: its own validation says so */ }
			}
		}
		if (json && Array.isArray(json.requires)) loadDeps(json.requires, seen);
	}
}
const hasDep = rel => depFiles.has(lower(rel));

// --- XML: well-formedness check ----------------------------------------------------------------------------------------
function checkXml(text) {
	const stack = [];
	let i = 0, roots = 0;
	const fail = (msg, at) => ({ line: lineOf(text, at), msg });
	while (i < text.length) {
		const lt = text.indexOf('<', i);
		if (lt < 0) break;
		if (text.startsWith('<!--', lt)) {
			const end = text.indexOf('-->', lt + 4);
			if (end < 0) return fail('unclosed comment', lt);
			i = end + 3; continue;
		}
		if (text.startsWith('<![CDATA[', lt)) {
			const end = text.indexOf(']]>', lt);
			if (end < 0) return fail('unclosed CDATA', lt);
			i = end + 3; continue;
		}
		if (text.startsWith('<?', lt) || text.startsWith('<!', lt)) {
			const end = text.indexOf('>', lt);
			if (end < 0) return fail('unclosed instruction', lt);
			i = end + 1; continue;
		}
		// tag: scanned while respecting quotes
		let j = lt + 1, quote = null;
		while (j < text.length) {
			const c = text[j];
			if (quote) { if (c === quote) quote = null; }
			else if (c === '"' || c === "'") quote = c;
			else if (c === '>') break;
			else if (c === '<') return fail('"<" inside a tag (is a ">" missing, or is there a "<" in a value not written as &lt;?)', lt);
			j++;
		}
		if (j >= text.length) return fail('unclosed tag', lt);
		const tag = text.slice(lt + 1, j);
		if (tag.startsWith('/')) {
			const name = tag.slice(1).trim();
			const top = stack.pop();
			if (!top) return fail(`</${name}> was never opened`, lt);
			if (top.name !== name) return fail(`</${name}> does not close <${top.name}> (opened on line ${top.line})`, lt);
		}
		else {
			const m = tag.match(/^([^\s/>]+)/);
			if (!m) return fail('empty tag', lt);
			const selfClosing = tag.endsWith('/');
			// attributes: name = "value"
			const attrText = tag.slice(m[0].length).replace(/\/$/, '');
			const rest = attrText.replace(/\s*[^\s=]+\s*=\s*("[^"]*"|'[^']*')/g, '').trim();
			if (rest) return fail(`bad attribute in <${m[1]}> (near "${rest.slice(0, 30)}"): values must be quoted`, lt);
			if (!selfClosing) stack.push({ name: m[1], line: lineOf(text, lt) });
			else if (stack.length === 0) roots++;
			if (stack.length === 1 && !selfClosing) roots++;
		}
		i = j + 1;
	}
	if (stack.length) return fail(`<${stack[stack.length - 1].name}> is not closed`, text.length);
	return null;
}

// --- mod.json ---------------------------------------------------------------------------------------------------------
if (!has('mod.json')) {
	warn('mod.json', 0, 'there is no mod.json: the game will create one named after the folder');
}
else {
	try {
		const m = modernKeys(JSON.parse(read('mod.json')));
		for (const k of ['id', 'name', 'version']) if (!m[k]) warn('mod.json', 0, `"${k}" is missing`);
		if (Array.isArray(m.requires)) {
			loadDeps(m.requires);
			for (const id of depNotFound) warn('mod.json', 0, `requires "${id}": its folder was not found in ${depRoots.join(', ') || 'any folder (use --mods <folder>)'}: what it provides is not checked, and a class, entity or sprite taken from it may be reported as missing`);
		}
		// name and description: a text, or an object with one text per language
		for (const k of ['name', 'description']) {
			const v = m[k];
			if (v && typeof v === 'object') {
				for (const [lang, text] of Object.entries(v)) if (typeof text !== 'string') error('mod.json', 0, `"${k}": the text for "${lang}" has to be a string`);
				if (!Object.keys(v).length) error('mod.json', 0, `"${k}" is empty`);
			}
			else if (v !== undefined && typeof v !== 'string') error('mod.json', 0, `"${k}" has to be a string or an object with one string per language`);
		}
		// category and tags (see publishing.md): the category is the main one in Browse content; the tags add categories and feed the players and language filters
		const kCategories = ['Gameplay & Balance', 'Units & Heroes', 'Civilizations', 'Scenarios', 'Adventures', 'Conquests', 'Graphics',
			'Audio & Voices', 'Translations'];
		const kOldCategories = { 'Maps': 'Scenarios', 'Campaigns': 'Adventures', 'Conquest': 'Conquests', 'Interface': 'Graphics',
			'Total Conversions': 'Gameplay & Balance' };
		const kWithLanguage = ['Adventures', 'Audio & Voices', 'Translations'];
		const kLanguages = ['Spanish', 'English', 'Italian', 'German', 'French', 'Polish', 'Multilingual'];
		if (m.category === undefined) {
			warn('mod.json', 0, `"category" is missing: the one you will pick when uploading to GameBanana (${kCategories.join(', ')})`);
		}
		else if (kOldCategories[m.category]) {
			warn('mod.json', 0, `"category": "${m.category}" is an old name: use "${kOldCategories[m.category]}"`);
		}
		else if (!kCategories.includes(m.category)) {
			error('mod.json', 0, `"category": "${m.category}" is not a GameBanana category (${kCategories.join(', ')})`);
		}
		const category = kOldCategories[m.category] || m.category;
		const tags = Array.isArray(m.tags) ? m.tags : [];
		if (m.tags !== undefined && !Array.isArray(m.tags)) error('mod.json', 0, '"tags" has to be a list');
		// each tag is "Title: value"; the game uses Type (more categories), Players and Language
		const title = t => { const i = t.indexOf(':'); return i < 0 ? '' : t.slice(0, i).trim().toLowerCase(); };
		const value = t => { const i = t.indexOf(':'); return i < 0 ? t.trim() : t.slice(i + 1).trim(); };
		const isPlayers = v => /^[2-8](-[2-8])?$/.test(v);
		const types = new Set(kCategories.includes(category) ? [category] : []);
		for (const t of tags.filter(t => title(t) === 'type')) {
			const v = value(t), fresh = kOldCategories[v] || v;
			if (!kCategories.includes(fresh)) {
				warn('mod.json', 0, `tag "${t}": "${v}" is not a category (${kCategories.join(', ')})`);
				continue;
			}
			if (fresh !== v) warn('mod.json', 0, `tag "${t}": "${v}" is an old name: use "Type: ${fresh}"`);
			if (types.has(fresh)) warn('mod.json', 0, `tag "${t}": ${fresh} is already one of its categories`);
			types.add(fresh);
		}
		const withPlayers = types.has('Scenarios'), withLanguage = kWithLanguage.some(c => types.has(c));
		let players = false, language = false;
		for (const t of tags) {
			const tit = title(t), v = value(t);
			if (tit === 'type') continue;
			if (t === 'Gameplay' || t === 'Cosmetic' || t === 'Online tested') {
				warn('mod.json', 0, `tag "${t}": no longer needed (the game knows from the files whether it changes gameplay)`);
			}
			else if (tit === 'players' || (!tit && /^[2-8](-[2-8])? players$/.test(t))) {
				const n = tit ? v : t.replace(/ players$/, '');
				if (!isPlayers(n)) {
					warn('mod.json', 0, `tag "${t}": players go from 2 to 8, like "Players: 4" or "Players: 2-4"`);
					continue;
				}
				players = true;
				if (!tit) warn('mod.json', 0, `tag "${t}": old form, write it as "Players: ${n}"`);
				if (!withPlayers) warn('mod.json', 0, `tag "${t}": players are only filtered in Scenarios (as the category or with "Type: Scenarios")`);
			}
			else if ((tit === 'language' || !tit) && kLanguages.includes(v)) {
				language = true;
				if (!tit) warn('mod.json', 0, `tag "${t}": old form, write it as "Language: ${v}"`);
				if (!withLanguage) warn('mod.json', 0, `tag "${t}": the language is only filtered in ${kWithLanguage.join(', ')}`);
			}
			else if (tit === 'language') {
				warn('mod.json', 0, `tag "${t}": unknown language (valid ones: ${kLanguages.join(', ')})`);
			}
			else if (tit) {
				warn('mod.json', 0, `tag "${t}": the game doesn't use it (only Type, Players and Language are valid); remove it`);
			}
			else if (!tit) {
				warn('mod.json', 0, `tag "${t}": the game doesn't filter by it (valid ones: "Type: …", "Players: …" and "Language: …")`);
			}
		}
		if (category === 'Scenarios' && !players) warn('mod.json', 0, 'scenario without a players tag: add "Players: 4" (or "Players: 2-4") to "tags" and to the GameBanana tags, or it won\'t show up when filtering by players');
		if (kWithLanguage.includes(category) && !language) warn('mod.json', 0, `no language tag: add "Language: English" (or the one it is: ${kLanguages.join(', ')}) to "tags" and to the GameBanana tags, or it won't show up when filtering by language`);
	}
	catch (e) { error('mod.json', 0, 'not valid JSON: ' + e.message); }
}

// --- extensions and paths ---------------------------------------------------------------------------------------------
for (const f of files) {
	const ext = path.extname(f);
	if (ext && FORBIDDEN.includes(lower(ext))) error(f, 0, `extension "${ext}" is forbidden (executables and system scripts): the mod would NOT load`);
	else if (ext && !ALLOWED.includes(lower(ext))) warn(f, 0, `extension "${ext}": the game does not use it, the file only takes up space`);
	if (f.includes('..') || /^[a-z]:/i.test(f)) error(f, 0, 'path not allowed');
}

// --- maps: the type the .bfhp itself states (game_type 0 scenario, 1 adventure, 2 conquest; adventure="1" in the old ones) -----
// The Scenarios list only shows type 0; the Adventures list shows everything in adventures\ (type 0 missions too).
const kMapFolders = ['scenarios', 'adventures', 'conquests'];
const kMapKinds = ['a scenario', 'an adventure', 'a conquest'];
const mapKind = head => {
	const start = head.indexOf('<properties');
	if (start < 0) return -1;
	const element = head.slice(start, head.indexOf('>', start));
	const attr = name => (element.match(new RegExp('\\s' + name + '="([^"]*)"')) || [])[1];
	const type = attr('game_type');
	let kind = type === undefined ? 0 : ['0', '1', '2'].indexOf(type);
	if (kind === 0 && Number(attr('adventure') || 0) !== 0) kind = 1;
	return kind;
};
for (const f of files.filter(f => /\.bfhp$/i.test(f))) {
	const kind = mapKind(fs.readFileSync(path.join(modDir, f)).subarray(0, 65536).toString('latin1'));
	if (kind < 0) continue;
	const folder = f.includes('/') ? lower(f.split('/')[0]) : '';
	const what = kMapKinds[kind];
	if (!folder) warn(f, 0, `loose map at the root: it is ${what}, so when installing the game places it in ${kMapFolders[kind]}\\${kind === 0 ? ' (in adventures\\ if the category is Adventures)' : ''}; better upload it already in its folder`);
	else if (folder === 'scenarios' && kind !== 0) warn(f, 0, `it is ${what} (game_type ${kind}): the Scenarios list only shows skirmish maps, so it won't show up; move it to ${kMapFolders[kind]}\\`);
	else if ((folder === 'adventures' && kind === 2) || (folder === 'conquests' && kind !== 2)) warn(f, 0, `it is ${what} (game_type ${kind}) but it is in ${folder}\\: move it to ${kMapFolders[kind]}\\`);
}

// --- game options (options/<id>.json) ---------------------------------------------------------------------------------
let optionBits = 0;
for (const f of files.filter(f => /^options\/[^/]+\.json$/i.test(f))) {
	let j;
	try { j = modernKeys(JSON.parse(read(f))); } catch (e) { error(f, 0, 'not valid JSON: ' + e.message); continue; }
	const id = j.id ?? path.basename(f, '.json');
	if (!/^[A-Za-z0-9_-]{1,32}$/.test(id)) error(f, 0, `the id "${id}" is not valid (letters, digits, _ and -; up to 32)`);
	if (j.name === undefined) warn(f, 0, '"name" is missing: the lobby will show the id');
	const defaultValue = j.default;
	const values = j.values;
	if (values === undefined) {
		if (defaultValue !== undefined && typeof defaultValue !== 'boolean') error(f, 0, '"default" of a switch is true or false');
		optionBits += 1;
	}
	else if (!Array.isArray(values) || values.length < 2 || values.length > 16) error(f, 0, '"values" is a list of 2 to 16 options');
	else {
		const ids = values.map(v => String(typeof v === 'string' ? v : (v && v.id) || '').toLowerCase());
		values.forEach((v, i) => { if (typeof v !== 'string' && !(v && typeof v === 'object')) error(f, 0, `values[${i}]: a string or an object with "name"`); });
		if (defaultValue !== undefined && !(Number.isInteger(defaultValue) && defaultValue >= 0 && defaultValue < values.length) && !(typeof defaultValue === 'string' && ids.includes(defaultValue.toLowerCase()))) {
			error(f, 0, '"default" of a list is the number of the value (from 0) or its id');
		}
		optionBits += Math.ceil(Math.log2(values.length));
	}
}
if (optionBits > 8) warn('options', 0, `the options of this mod take ${optionBits} bits and only 8 fit across all active mods: some will not fit`);

// --- city templates: every class must exist, or the random map is not generated and the game gives no warning -----------------
{
	const tf = files.find(x => lower(x) === 'patches/randommapsettlements/maps/1/map.obj.xml');
	if (tf && dataDir) {
		const known = new Set();
		const addClassFile = text => { const m = /<class\b[^>]*\bid\s*=\s*"([^"]+)"/i.exec(text); if (m) known.add(m[1].toLowerCase()); };
		const classDir = fs.readdirSync(dataDir).find(n => n.toLowerCase() === 'classes');
		if (classDir) {
			const walkClasses = d => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walkClasses(p); else if (/\.sc\.xml$/i.test(e.name)) addClassFile(fs.readFileSync(p, 'latin1')); } };
			walkClasses(path.join(dataDir, classDir));
		}
		for (const x of files.filter(x => /^data\/classes\/.*\.sc\.xml$/i.test(x))) addClassFile(read(x));
		depClassIds.forEach(c => known.add(c));
		const missing = new Map();
		read(tf).split('\n').forEach((l, i) => { const m = /class="([^"]+)"/.exec(l); if (m && !known.has(m[1].toLowerCase()) && !missing.has(m[1])) missing.set(m[1], i + 1); });
		for (const [c, line] of missing) error(tf, line, `the class "${c}" exists neither in the game nor in the mod: the random map would not be generated`);
	}
}

// --- civilizations ----------------------------------------------------------------------------------------------------
const myCivs = new Set(), civIndexes = new Map(), baseOverrides = new Map();
for (const f of files.filter(f => /^civs\/[^/]+\.json$/i.test(f))) {
	let j;
	try { j = modernKeys(JSON.parse(read(f))); } catch (e) { error(f, 0, 'not valid JSON: ' + e.message); continue; }
	if (!ID_RE.test(j.id || '')) error(f, 0, `the id "${j.id}" is not valid (letters, digits and _; must start with a letter; up to 24)`);
	else myCivs.add(lower(j.id));
	const idx = Number(j.index);
	if (!(idx >= 10 && idx <= 99)) error(f, 0, '"index" is missing or not between 10 and 99');
	else if (civIndexes.has(idx)) error(f, 0, `index ${idx} is already used by ${civIndexes.get(idx)}`);
	else civIndexes.set(idx, f);
	if (!GAME_CIVS.includes(j.base || 'Gaul')) error(f, 0, `unknown "base": ${j.base} (${GAME_CIVS.join(', ')})`);
	const biome = j.biome;
	if (biome !== undefined && !GAME_CIVS.includes(biome)) error(f, 0, `unknown "bioma": ${biome} (${GAME_CIVS.join(', ')})`);
	// Own city and village templates: "plantilla": "Dacian" looks for "Dacian Town N" and "Dacian Village N" in the patch at
	// patches/randommapsettlements/maps/1/map.obj.xml or in the mod's packs/randommapsettlements.bfhp; whatever is missing is filled in
	// from the base template
	const tpl = j.template;
	if (tpl !== undefined) {
		if (typeof tpl !== 'string' || !/^[A-Za-z][A-Za-z ]{1,30}$/.test(tpl)) error(f, 0, '"plantilla" is a string (letters and spaces, e.g. "Dacian")');
		else {
			const tf = files.find(x => lower(x) === 'patches/randommapsettlements/maps/1/map.obj.xml') ||
				files.find(x => lower(x) === 'packs/randommapsettlements.bfhp');
			if (!tf) warn(f, 0, `"plantilla": "${tpl}" but the mod has no patches/randommapsettlements/maps/1/map.obj.xml: the base ones will be used`);
			else {
				const text = /\.bfhp$/i.test(tf) ? fs.readFileSync(path.join(modDir, tf)).toString('latin1') : read(tf);
				for (const kind of ['Town', 'Village']) {
					if (!text.includes(`name="${tpl} ${kind}`)) warn(tf, 0, `there is no template "${tpl} ${kind} N": the ${kind === 'Town' ? 'towns' : 'villages'} of that civilization will use the base ones`);
				}
			}
		}
	}
	const letter = j.letter;
	if (letter !== undefined && !/^[A-Za-z0-9]$/.test(String(letter))) error(f, 0, '"letra" is a single letter or digit');
	// With "sustituyeBase", the letter and the template become those of the game's base civilization for the whole game
	if (j.replacesBase === true && GAME_CIVS.includes(j.base || 'Gaul')) {
		const b = j.base || 'Gaul';
		const prev = baseOverrides.get(b);
		if (prev && letter !== undefined && prev.letter !== undefined && lower(String(prev.letter)) !== lower(String(letter))) error(f, 0, `gives ${b} the letter ${letter}, but ${prev.file} gives it ${prev.letter}`);
		else if (prev && tpl !== undefined && prev.tpl !== undefined && prev.tpl !== tpl) error(f, 0, `gives ${b} the templates "${tpl}", but ${prev.file} gives it "${prev.tpl}"`);
		else if (!prev || (prev.letter === undefined && letter !== undefined) || (prev.tpl === undefined && tpl !== undefined))
			baseOverrides.set(b, { file: f, letter: letter !== undefined ? letter : prev && prev.letter, tpl: tpl !== undefined ? tpl : prev && prev.tpl });
	}
}

// --- game rules (rules/game.json) ------------------------------------------------------------------------------------
for (const f of files.filter(f => /^rules\//i.test(f))) {
	if (lower(f) !== 'rules/game.json') {
		warn(f, 0, 'the game only reads rules/game.json');
		continue;
	}
	let j;
	try { j = JSON.parse(read(f)); } catch (e) { error(f, 0, 'not valid JSON: ' + e.message); continue; }
	if (!j || typeof j !== 'object' || Array.isArray(j)) { error(f, 0, 'not an object'); continue; }
	for (const k of Object.keys(j)) {
		if (!['armyStaysWithHero', 'sentriesUnselectable', 'protectedClasses'].includes(k)) error(f, 0, `"${k}" is not a rule of the game`);
	}
	for (const k of ['armyStaysWithHero', 'sentriesUnselectable']) {
		if (j[k] !== undefined && typeof j[k] !== 'boolean') error(f, 0, `"${k}" is true or false`);
	}
	if (j.protectedClasses !== undefined) {
		if (!Array.isArray(j.protectedClasses)) error(f, 0, '"protectedClasses" is a list of class names');
		else for (const c of j.protectedClasses) {
			if (typeof c !== 'string' || !/^[A-Za-z0-9_]{1,64}$/.test(c)) error(f, 0, `"${c}" in "protectedClasses" is not a class name`);
		}
	}
}

// --- game modes (modes/*.json) ---------------------------------------------------------------------------------------
const modeKeys = new Map();
for (const f of files.filter(f => /^modes\/[^/]+\.json$/i.test(f))) {
	let j;
	try { j = JSON.parse(read(f)); } catch (e) { error(f, 0, 'not valid JSON: ' + e.message); continue; }
	if (!j || typeof j !== 'object' || Array.isArray(j)) { error(f, 0, 'not an object'); continue; }
	const key = j.mode !== undefined ? j.mode : path.basename(f).replace(/\.json$/i, '');
	if (typeof key !== 'string' || key.length < 1 || key.length > 31) error(f, 0, '"mode" is the name of the mode\'s script in data\\GameScripts without .vs (up to 31 characters)');
	else {
		if (modeKeys.has(lower(key))) error(f, 0, `the mode "${key}" is already set by ${modeKeys.get(lower(key))}`);
		modeKeys.set(lower(key), f);
		const script = 'data/gamescripts/' + lower(key) + '.vs';
		if (!files.some(x => lower(x) === script)) warn(f, 0, `the mod has no data/GameScripts/${key}.vs: if the mode is not one of the game's, it never applies`);
	}
	for (const k of Object.keys(j)) {
		if (!['mode', 'gold', 'population', 'noFog', 'explored', 'bonus', 'civilizations'].includes(k)) error(f, 0, `"${k}" is not a setting of a mode`);
	}
	for (const [k, lo, hi] of [['gold', 0, 10000000], ['population', 1, 100000], ['bonus', 0, 99]]) {
		if (j[k] !== undefined && !(Number.isInteger(j[k]) && j[k] >= lo && j[k] <= hi)) error(f, 0, `"${k}" is a whole number between ${lo} and ${hi}`);
	}
	for (const k of ['noFog', 'explored']) {
		if (j[k] !== undefined && typeof j[k] !== 'boolean') error(f, 0, `"${k}" is true or false`);
	}
	if (j.civilizations !== undefined) {
		if (!Array.isArray(j.civilizations)) error(f, 0, '"civilizations" is a list of civilizations');
		else for (const c of j.civilizations) {
			const known = typeof c === 'string' && (GAME_CIVS.some(g => lower(g) === lower(c)) || ['rome', 'roman', 'briton'].includes(lower(c)) || myCivs.has(lower(c)) || depCivs.has(lower(c)));
			if (!known) warn(f, 0, `"${c}" is not a civilization of the game or of this mod (if it belongs to another mod, that one has to be active)`);
		}
	}
}

// --- specialties ------------------------------------------------------------------------------------------------------
const mySpecials = new Set(), specialBits = new Map();
for (const f of files.filter(f => /^specials\/[^/]+\.json$/i.test(f))) {
	let j;
	try { j = modernKeys(JSON.parse(read(f))); } catch (e) { error(f, 0, 'not valid JSON: ' + e.message); continue; }
	if (!ID_RE.test(j.id || '')) error(f, 0, `the id "${j.id}" is not valid (letters, digits and _; must start with a letter; up to 24)`);
	else if (GAME_SPECIALS.some(g => lower(g) === lower(j.id))) error(f, 0, `"${j.id}" is a unit special of the game`);
	else mySpecials.add(lower(j.id));
	const bit = Number(j.bit);
	if (!(bit >= 36 && bit <= 63)) error(f, 0, '"bit" is missing or not between 36 and 63');
	else if (specialBits.has(bit)) error(f, 0, `bit ${bit} is already used by ${specialBits.get(bit)}`);
	else specialBits.set(bit, f);
	if (!j.name) warn(f, 0, 'no "name": the id will be shown');
}

// --- classes ----------------------------------------------------------------------------------------------------------
function checkClassFile(f, text, source) {
	text = text.replace(/<!--[\s\S]*?-->/g, c => c.replace(/[^\n]/g, " ")); // commented-out lines do not count; the blanks keep the line numbers
	const header = text.match(/<class\b[^>]*>/);
	if (!header) { warn(f, 0, 'there is no <class> tag'); return; }
	if (!/\bid\s*=\s*"[^"]+"/.test(header[0])) error(f, lineOf(text, header.index), '<class> without an id');
	for (const m of text.matchAll(/\brace\s*=\s*"([^"]*)"/g)) {
		if (!GAME_CIVS.includes(m[1]) && !myCivs.has(lower(m[1])) && !depCivs.has(lower(m[1])) && !['None', 'Random', 'Mutable'].includes(m[1]))
			warn(f, lineOf(text, m.index), `race="${m[1]}" is not a civilization of the game or of this mod`);
	}
	for (const m of text.matchAll(/\bunit_specials\s*=\s*"([^"]*)"/g)) {
		for (const name of m[1].split(',').map(s => s.trim()).filter(Boolean)) {
			if (GAME_SPECIALS.includes(name) || mySpecials.has(lower(name))) continue;
			const near = GAME_SPECIALS.find(g => lower(g) === lower(name));
			if (near) error(f, lineOf(text, m.index), `"${name}": the game is case-sensitive, it is written "${near}"`);
			else warn(f, lineOf(text, m.index), `unknown unit special "${name}": the game ignores it`);
		}
	}
	if (dataDir) {
		for (const m of text.matchAll(/<(?:method|behavior)\b[^>]*?\b(?:vs|script|verify|onfinish)\s*=\s*"([^"]+)"/g)) {
			const rel = m[1].replace(/\\/g, '/');
			if (!has(rel) && !hasDep(rel) && !gameFile(rel)) error(f, lineOf(text, m.index), `the script ${rel} is neither in the mod nor in the game`);
		}
	}
}
for (const f of files.filter(f => /^data\/classes\/.+\.sc\.xml$/i.test(f))) {
	const text = read(f);
	const bad = checkXml(text);
	if (bad) error(f, bad.line, 'malformed XML: ' + bad.msg);
	else checkClassFile(f, text);
}

// --- orders -----------------------------------------------------------------------------------------------------------
for (const f of files.filter(f => /^data\/commands\/[^/]+\.xml$/i.test(f))) {
	const text = read(f);
	const bad = checkXml(text);
	if (bad) { error(f, bad.line, 'malformed XML: ' + bad.msg); continue; }
	// Two commands with the same name each apply to the buildings of their <src>: the game binds them by position, not by name.
	// Whatever asks for the command by its name (scripts, <defaultcmd>) gets the first one. They only clash if both are for the same building
	const buildings = new Map();
	for (const m of text.matchAll(/<cmd\b[^>]*?\bname\s*=\s*"([^"]*)"[^>]*?(?:\/>|>([\s\S]*?)<\/cmd>)/g)) {
		const srcs = [...(m[2] || '').matchAll(/<src\b[^>]*?\bobj\s*=\s*"([^"]*)"/g)].map(s => lower(s[1]));
		const seen = buildings.get(m[1]) || new Set();
		const shared = [...new Set(srcs.filter(s => seen.has(s)))];
		if (shared.length) warn(f, lineOf(text, m.index), `the command "${m[1]}" was already there for ${shared.join(', ')}: that building will have two buttons with that name`);
		for (const s of srcs) seen.add(s);
		buildings.set(m[1], seen);
	}
}

// --- patches ----------------------------------------------------------------------------------------------------------
for (const f of files.filter(f => /^patches\//i.test(f))) {
	const target = f.slice('patches/'.length);
	const ext = lower(path.extname(f));
	if (ext !== '.xml' && ext !== '.ini' && ext !== '.vs') { error(f, 0, 'only .xml, .ini and .vs files can be patched'); continue; }
	if (dataDir && /^data\//i.test(target) && !gameFile(target) && !hasDep(target)) warn(f, 0, `the game has no ${target}: the patch will not be applied`);
	const text = read(f);
	if (ext === '.ini') {
		// The engine only recognizes comments that take up the whole line: a ';' after a key or a section breaks the patch
		text.split('\n').forEach((l, i) => {
			const t = l.trim();
			if (t && !t.startsWith(';') && t.includes(';')) warn(f, i + 1, 'comment at the end of a line: the engine does not strip it ("' + t.slice(0, 40) + '…"); put it on its own line');
		});
	}
	if (ext === '.vs') {
		let open = null, bodyLines = 0;
		let firstCode = null;
		const closeBlock = () => {
			if (open && !bodyLines) warn(f, open.line, `${open.word} has no code below it`);
			// scripts declare all their variables at the top: the code of a @top that is not a declaration ends up before the script's own and does not compile
			if (open && open.word === '@top' && firstCode && !/^\w+\s+\w+(\s*,\s*\w+)*\s*;/.test(firstCode)) warn(f, open.line, '@top puts code before the script\'s variable declarations and would not compile: use @before with its first line of code');
		};
		text.split('\n').forEach((raw, i) => {
			const l = raw.replace(/\r$/, '');
			if (!l.startsWith('@')) { if (open && l.trim()) { bodyLines++; if (!firstCode && !l.trim().startsWith('//')) firstCode = l.trim(); } return; }
			closeBlock();
			const m = l.match(/^@(\S*)\s*(.*)$/);
			const word = LEGACY_DIRECTIVES[m[1]] || m[1];
			if (word === 'end') { open = null; bodyLines = 0; return; }
			if (!['top', 'bottom', 'before', 'after', 'replace'].includes(word)) {
				error(f, i + 1, `the directive @${word} does not exist (@top, @bottom, @before, @after, @replace, @end)`);
				open = null; bodyLines = 0; return;
			}
			if (['before', 'after', 'replace'].includes(word) && !m[2].trim()) error(f, i + 1, `@${word} needs the text of the line to look for`);
			open = { word: '@' + word, line: i + 1 };
			bodyLines = 0;
			firstCode = null;
		});
		closeBlock();
		continue;
	}
	if (ext === '.xml') {
		const bad = checkXml(text);
		if (bad) { error(f, bad.line, 'malformed patch, it would not be applied at all: ' + bad.msg); continue; }
		if (!/^\s*(<\?[^>]*\?>\s*)?(<!--[\s\S]*?-->\s*)*<patch\b/.test(text)) error(f, 1, 'the patch has to start with <patch>');
		for (const m of text.matchAll(/<(set|add|remove|replace)\b([^>]*)>/g)) {
			const sel = m[2].match(/\bselect\s*=\s*("([^"]*)"|'([^']*)')/);
			if (!sel) { error(f, lineOf(text, m.index), `<${m[1]}> without select`); continue; }
			const s = sel[2] ?? sel[3];
			if (!s.startsWith('/')) warn(f, lineOf(text, m.index), `the selector "${s}" does not start with / or //: the game accepts it, but it is better to write it with /`);
			if ((s.match(/\[/g) || []).length !== (s.match(/\]/g) || []).length) error(f, lineOf(text, m.index), `selector with unclosed brackets: ${s}`);
		}
		if (/<patch\b[\s\S]*<(?!\/?(?:patch|set|add|remove|replace)\b)/.test(text.replace(/<(add|replace)\b[\s\S]*?<\/\1>/g, '').replace(/<!--[\s\S]*?-->/g, ''))) {
			warn(f, 0, 'inside <patch> there are tags that are not operations (set, add, remove, replace)');
		}
	}
	else {
		text.split('\n').forEach((raw, i) => {
			const l = raw.replace(/\r$/, '');
			if (!l.trim() || /^\s*;/.test(l) || /^\s*\[[^\]]+\]\s*(;.*)?$/.test(l) || /^\s*[-+]\S/.test(l) || /^[^=;]+=/.test(l)) return;
			warn(f, i + 1, `INI patch line that is not understood: "${l.trim().slice(0, 40)}"`);
		});
		// Game bug: a new section in UNIT_SPECIALS.INI overflows its memory
		if (/^unit_specials\.ini$/i.test(path.basename(f))) {
			for (const m of text.matchAll(/^\s*\[([^\]]+)\]/gm)) {
				if (!GAME_SPECIALS.some(g => lower(g) === lower(m[1])))
					error(f, lineOf(text, m.index), `the section [${m[1]}] is not a game unit special: the game writes outside its memory and may crash`);
			}
		}
	}
}
for (const f of files.filter(f => /(^|\/)unit_specials\.ini$/i.test(f) && !/^patches\//i.test(f))) {
	for (const m of read(f).matchAll(/^\s*\[([^\]]+)\]/gm)) {
		if (!GAME_SPECIALS.some(g => lower(g) === lower(m[1])))
			error(f, 0, `the section [${m[1]}] is not a game unit special: the game writes outside its memory and may crash`);
	}
}

// --- texts ------------------------------------------------------------------------------------------------------------
for (const f of files.filter(f => /^loc\/.+\.xml$/i.test(f))) {
	const text = read(f);
	const bad = checkXml(text);
	if (bad) { error(f, bad.line, 'malformed XML: ' + bad.msg); continue; }
	for (const m of text.matchAll(/<translationtableentry\b([^>]*)>/g)) {
		if (!/\btext\s*=/.test(m[1]) || !/\bresult\s*=/.test(m[1])) error(f, lineOf(text, m.index), 'translationtableentry needs text and result');
	}
}

// --- scripts ----------------------------------------------------------------------------------------------------------
const typesOf = sig => sig.split(',').map(s => s.trim().split(/\s+/)[0]).filter(Boolean);
// A behavior script (<behavior script="..."> of a class) needs no signature: the game always loads it as "void, Obj This"
// Neither do victory modes (data\gamescripts) and item scripts (the *_script of data\items.xml): the game passes them their signature
const behaviorScripts = new Set();
for (const f of files.filter(f => /^data\/classes\/.+\.sc\.xml$/i.test(f))) {
	for (const m of read(f).matchAll(/<behavior\b[^>]*?\bscript\s*=\s*"([^"]+)"/gi)) behaviorScripts.add(lower(m[1].replace(/\\/g, '/')));
}
const itemScripts = new Set();
for (const f of files.filter(f => /^data\/items\.xml$/i.test(f))) {
	for (const m of read(f).matchAll(/\b\w+_script\s*=\s*"file:\/\/([^"]+)"/gi)) itemScripts.add(lower(m[1].replace(/\\/g, '/')));
}
for (const f of files.filter(f => /\.vs$/i.test(f) && !/^patches\//i.test(f))) {  // script patches carry no signature: they are pieces of code with directives
	const text = read(f);
	const first = text.split('\n')[0].replace(/\r$/, '');
	if (!/^\s*\/\/\s*\S+/.test(first) && !behaviorScripts.has(lower(f)) && !itemScripts.has(lower(f)) && !/^data\/gamescripts\//i.test(f)) {
		error(f, 1, 'the first line has to be a comment with the signature, for example "// void, Obj This"');
	}
	const ev = f.match(/^events\/([^/]+)\.vs$/i);
	if (ev) {
		const name = Object.keys(EVENTS).find(e => lower(e) === lower(ev[1]));
		if (!name) warn(f, 0, `"${ev[1]}" is not a known event (${Object.keys(EVENTS).join(', ')})`);
		else {
			const got = typesOf(first.replace(/^\s*\/\/\s*/, ''));
			if (got.join(',') !== EVENTS[name].join(',')) error(f, 1, `the signature of ${name} is "// ${EVENTS[name].join(', ')}"` + ' with whatever parameter names you like');
			if (lower(ev[1]) !== lower(name) || ev[1] !== name) warn(f, 0, `the file name should be exactly ${name}.vs`);
		}
		if (/\b(Sleep|Wait)\s*\(/.test(text) && name && name !== 'OnGameStart') error(f, 0, `${name} runs immediately: it cannot use Sleep`);
	}
	for (const m of text.matchAll(/\b(Mod_[A-Za-z]+)\s*\(/g)) {
		if (!MOD_FUNCTIONS.includes(m[1])) error(f, lineOf(text, m.index), `the function ${m[1]} does not exist in the SDK (${MOD_FUNCTIONS.join(', ')})`);
	}
	if (/\bMod_(Set)?AttackDamage\b/.test(text) && !(ev && /^OnAttackResolved$/i.test(ev[1]))) {
		warn(f, 0, 'Mod_AttackDamage / Mod_SetAttackDamage only do something inside the OnAttackResolved event script');
	}
	for (const m of text.matchAll(/\bMod_(?:Has|Set)Special\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\b(?!\s*\.)/g)) {
		if (/^(attacker|victim|killer|this|me|obj)$/i.test(m[1])) warn(f, lineOf(text, m.index), 'Mod_HasSpecial / Mod_SetSpecial take a Unit: use object.AsUnit()');
	}
}

// --- encoding and references (failures the game does not report) ------------------------------------------------------------
// The game reads class XMLs as UTF-8: an accented letter saved as Latin-1 makes the class not load, with no warning other than a line in its log
const readBuf = rel => fs.readFileSync(path.join(modDir, rel));
const validUtf8 = buf => { try { new TextDecoder('utf-8', { fatal: true }).decode(buf); return true; } catch (e) { return false; } };
for (const f of files.filter(f => /\.(xml|vs|json|ini)$/i.test(f))) {
	if (validUtf8(readBuf(f))) continue;
	if (/^data\/classes\/.+\.sc\.xml$/i.test(f)) error(f, 0, 'not in UTF-8 (accented letters in Latin-1): the game cannot read the class and it does not load. Save it as UTF-8');
	else warn(f, 0, 'not in UTF-8 (accented letters in Latin-1): save it as UTF-8');
}

// Ids of the mod's classes (and the game's, with --data)
const myClassIds = new Set();
const classHeaders = new Map();  // id -> { file, header }
for (const f of files.filter(f => /^data\/classes\/.+\.sc\.xml$/i.test(f))) {
	const h = read(f).match(/<class\b[^>]*>/);
	const id = h && (h[0].match(/\bid\s*=\s*"([^"]+)"/) || [])[1];
	if (id) { myClassIds.add(lower(id)); classHeaders.set(lower(id), { file: f, header: h[0] }); }
}
let gameClassIds = null;
function classExists(id) {
	if (myClassIds.has(lower(id)) || depClassIds.has(lower(id))) return true;
	if (!dataDir) return null;  // unknown
	if (!gameClassIds) {
		gameClassIds = new Set();
		const dir = gameFile('data/classes');
		if (dir) {
			for (const g of fs.readdirSync(dir)) {
				if (!/\.sc\.xml$/i.test(g)) continue;
				const h = fs.readFileSync(path.join(dir, g), 'latin1').match(/<class\b[^>]*>/);
				const gid = h && (h[0].match(/\b(?:id|altid)\s*=\s*"([^"]+)"/g) || []).map(s => s.replace(/^[a-z]+\s*=\s*"|"$/gi, ''));
				if (gid) gid.forEach(x => gameClassIds.add(lower(x)));
			}
		}
	}
	return gameClassIds.has(lower(id));
}

// A class without an entity is created with no model and disappears (the game does not inherit entity from the parent)
const parents = new Set([...classHeaders.values()].map(c => (c.header.match(/\bparent\s*=\s*"([^"]+)"/) || [])[1]).filter(Boolean).map(lower));
for (const [id, c] of classHeaders) {
	if (/\bentity\w*\s*=/.test(c.header) || parents.has(id)) continue;
	if (/\bcpp_class\s*=\s*"CVX(Unit|Building|Outpost|TownHall|Hero|Gate|Tower)\b/.test(c.header)) {
		warn(c.file, lineOf(read(c.file), read(c.file).indexOf(c.header)), `the class ${id} has no entity (the model): the game does not inherit it from the parent, so it would be created without a model and vanish`);
	}
}

// Random map generation names classes in placeobjects.xml: if one does not exist, it hangs, with no warning
for (const f of files.filter(f => /^(patches\/)?data\/randommap\/placeobjects\.xml$/i.test(f))) {
	const text = read(f);
	for (const m of text.matchAll(/<GameObj\b[^>]*?\bclass\s*=\s*"([^"]+)"/g)) {
		if (m[1].includes('::')) continue;
		if (classExists(m[1]) === false) error(f, lineOf(text, m.index), `the class ${m[1]} does not exist: the random map would hang while being generated`);
	}
}

// A civilization's "alias" tables (used by the AI) must point to classes and orders that exist
const myCommandNames = new Set();
for (const f of files.filter(f => /^data\/commands\/[^/]+\.xml$/i.test(f))) {
	for (const m of read(f).matchAll(/<cmd\b[^>]*?\bname\s*=\s*"([^"]*)"/g)) myCommandNames.add(m[1]);
}
for (const f of files.filter(f => /^civs\/[^/]+\.json$/i.test(f))) {
	let j;
	try { j = modernKeys(JSON.parse(read(f))); } catch (e) { continue; }
	if (j.ai === true && j.variant !== true) {
		if (!j.alias && !j.ownAi) warn(f, 0, '"ia": true without an "alias" table: the game\'s AI scripts will not find your classes or commands. The cloning tool generates it');
	}
	for (const [base, clone] of Object.entries((j.alias && j.alias.classes) || {})) {
		if (!myClassIds.has(lower(clone))) error(f, 0, `alias.classes: "${base}" -> "${clone}", but the mod does not have that class`);
	}
	for (const [base, clone] of Object.entries((j.alias && j.alias.commands) || {})) {
		if (!myCommandNames.has(clone)) error(f, 0, `alias.commands: "${base}" -> "${clone}", but the mod does not have that command`);
	}
}

// --- graphics: .rle, .pass and entities (.ent.xml) ----------------------------------------------------------------------
// An image that does not exist, a corrupt .rle or a repeated idx makes the game crash (or hang) when loading the class, with no warning.
const rleLib = require('./lib/rle'), passLib = require('./lib/pass');
// Names of the game's graphics .pak files (only with --game): lowercase, with "/"
let gamePaths = null;
function gameHas(rel) {
	if (!gameDir) return null;
	if (!gamePaths) {
		gamePaths = new Set();
		for (const pk of ['Units', 'Buildings', 'MapObjects', 'Visuals']) {
			const pf = path.join(gameDir, 'Packs', pk + '.pak');
			if (!fs.existsSync(pf)) continue;
			const d = fs.readFileSync(pf), count = d.readUInt32LE(0x20);
			let p = 0x28, prev = '';
			for (let i = 0; i < count; i++) {
				const len = d[p], shared = d[p + 1];
				p += 2;
				const name = prev.slice(0, shared) + d.slice(p, p + len - shared).toString('latin1');
				p += len - shared + 8;
				prev = name;
				gamePaths.add(name.toLowerCase().replace(/\\/g, '/'));
			}
		}
		if (!gamePaths.size) { console.log(LABEL_WARN() + ('  --game: the .pak files were not found in ' + path.join(gameDir, 'Packs') + '; the game\'s images will not be checked')); gamePaths = null; return null; }
	}
	return gamePaths.has(lower(rel));
}
const rleCache = new Map(); // mod path (lowercase) -> sheet read | null if it is wrong
for (const f of files.filter(f => /\.rle$/i.test(f))) {
	let sheet = null;
	try {
		sheet = rleLib.parse(readBuf(f));
		if (sheet.cells.some(c => !c.empty && !c.data)) { error(f, 0, 'it is a .rle.mmp under another name (its pixels are in the game\'s rle.mmp): it cannot be loaded from a mod; you need a .rle with the pixels inside (convert-sprite.js)'); sheet = null; }
		else {
			if (sheet.trailing !== 0) warn(f, 0, `${sheet.trailing} extra bytes at the end of the .rle`);
			if (sheet.mode === rleLib.MODE_PAL || sheet.mode === rleLib.MODE_PLAYER) { if (sheet.paletteCount === 0) error(f, 0, 'palette mode but no colors in the palette: the game would read a null pointer'); }
			if (sheet.cells.every(c => c.empty)) warn(f, 0, 'all the frames of the .rle are empty');
			for (const c of sheet.cells) if (!c.empty) {
				try { rleLib.decodeFrame(c); } catch (e) { error(f, 0, 'a frame cannot be decoded (corrupt .rle): ' + e.message); sheet = null; break; }
			}
			if (sheet && sheet.rows > 62) warn(f, 0, `${sheet.rows} rows: the game allows 64 frames per animation including the 2 sentinels (62 rows)`);
		}
	}
	catch (e) { error(f, 0, 'invalid .rle (the game would crash loading it): ' + e.message); }
	rleCache.set(lower(f), sheet);
}
for (const f of files.filter(f => /\.pass$/i.test(f))) {
	try {
		const g = passLib.decode(readBuf(f));
		if (!g.cells.some(Boolean)) warn(f, 0, 'the footprint is empty: the building will take up no space on the map');
	} catch (e) { error(f, 0, 'invalid .pass: ' + e.message); }
}

for (const f of files.filter(f => /\.ent\.xml$/i.test(f))) {
	const text = read(f);
	const bad = checkXml(text);
	if (bad) { error(f, bad.line, 'malformed XML: ' + bad.msg); continue; }
	const dir = path.posix.dirname(f);
	const modOwnsSprites = files.some(x => /\.rle$/i.test(x) && path.posix.dirname(x) === dir); // the folder brings its own .rle files: it is a new entity
	const ent = text.match(/<entity\b[^>]*>/);
	const variations = ent && (ent[0].match(/\bvariations\s*=\s*"(\d+)"/) || [])[1];
	const passFile = ent && (ent[0].match(/\bpass_file\s*=\s*"([^"]*)"/) || [])[1];
	if (passFile) {
		const pf = (dir + '/' + passFile).replace(/^\.\//, '');
		if (!has(pf) && !hasDep(pf) && gameHas(pf) !== true) warn(f, lineOf(text, ent.index), `pass_file="${passFile}": ${pf} is not there (the footprint would be empty and the building would take up no space)`);
	}
	// images
	const images = new Map(); // idx -> {rows, columns, file}
	let nImages = 0;
	for (const m of text.matchAll(/<image\b[^>]*>/g)) {
		const line = lineOf(text, m.index);
		nImages++;
		const idx = (m[0].match(/\bidx\s*=\s*"(\d+)"/) || [])[1];
		const file = (m[0].match(/\bfile\s*=\s*"([^"]*)"/) || [])[1];
		const cols = (m[0].match(/\bcolumns\s*=\s*"(\d+)"/) || [])[1];
		const rows = (m[0].match(/\brows\s*=\s*"(\d+)"/) || [])[1];
		if (!idx || Number(idx) < 1 || Number(idx) > 32) error(f, line, `<image> with idx ${idx ?? '(missing)'}: it has to be between 1 and 32 (the game crashes on load)`);
		else if (images.has(idx)) error(f, line, `<image> with idx ${idx} repeated (the game crashes on load)`);
		else images.set(idx, { rows: Number(rows), columns: Number(cols), file });
		if (!file) { error(f, line, '<image> without file (the game does not load the entity)'); continue; }
		const base = (dir + '/' + file).replace(/^\.\//, '').replace(/\.rle$/i, '');
		// the game tries: the path as is (.rle/.bmp), .rle, .rle.mmp, .mmp, .bmp
		const inMod = ['.rle', '.bmp'].some(ext => has(base + ext)) || has(base) || ['.rle.mmp', '.mmp'].some(ext => has(base + ext));
		// (what a required mod ships counts as the game's: it is there when this one is active)
		const inGame = gameHas(base + '.rle.mmp') === true || gameHas(base + '.rle') === true || gameHas(base + '.bmp') === true
			|| ['.rle', '.bmp', '.rle.mmp', '.mmp', ''].some(ext => hasDep(base + ext));
		if (!inMod && !inGame) {
			const msg = `the image ${base}.rle (or .bmp) does not exist in the mod${gameDir ? ' or in the game' : ''}: the game HANGS when loading the class`;
			if (modOwnsSprites || gameDir) error(f, line, msg); else warn(f, line, msg + ' (unless it is one of the game\'s own images; --game checks that)');
		}
		else if (gameDir && has(base + '.rle') && gameHas(base + '.rle.mmp') === true) {
			warn(f, line, `the game already has ${base}.rle.mmp in a .pak and it wins over the mod's .rle: your .rle is ignored. Use another folder or file name`);
		}
		if (variations && cols && cols !== variations) warn(f, line, `columns="${cols}" does not match variations="${variations}" of <entity> (the game expects one column per direction)`);
		// rows/columns against the header of the .rle itself
		const sheet = rleCache.get(lower(base + '.rle')) || rleCache.get(lower(base));
		if (sheet) {
			if (cols && Number(cols) !== sheet.cols) warn(f, line, `columns="${cols}" but ${base}.rle has ${sheet.cols} columns`);
			if (rows && Number(rows) !== sheet.rows) warn(f, line, `rows="${rows}" but ${base}.rle has ${sheet.rows} rows`);
		}
	}
	if (nImages > 32) error(f, 0, `${nImages} images: the engine allows 32`);
	// layers
	let nLayers = 0;
	const layerIdx = new Set();
	for (const m of text.matchAll(/<layer\b[^>]*>/g)) {
		const line = lineOf(text, m.index);
		nLayers++;
		const idx = (m[0].match(/\bidx\s*=\s*"(\d+)"/) || [])[1], img = (m[0].match(/\bimage\s*=\s*"(\d+)"/) || [])[1];
		if (!idx || Number(idx) < 1 || Number(idx) > 32) error(f, line, `<layer> with idx ${idx ?? '(missing)'}: it has to be between 1 and 32`);
		else if (layerIdx.has(idx)) error(f, line, `<layer> with idx ${idx} repeated`);
		else layerIdx.add(idx);
		if (img && img !== '0' && !images.has(img)) error(f, line, `<layer> uses image="${img}" and there is no <image idx="${img}"> (the game crashes on load)`);
	}
	if (nLayers > 32) error(f, 0, `${nLayers} layers: the engine allows 32`);
	// states
	const states = [...text.matchAll(/<state\b[^>]*>/g)];
	if (states.length > 8) error(f, 0, `${states.length} states: the engine allows 8`);
	for (const m of states) {
		const idx = (m[0].match(/\bidx\s*=\s*"(\d+)"/) || [])[1];
		if (!idx || Number(idx) < 1 || Number(idx) > 8) error(f, lineOf(text, m.index), `<state> with idx ${idx ?? '(missing)'}: it has to be between 1 and 8`);
	}
	// animations
	const animIdx = new Set();
	for (const m of text.matchAll(/<anim\b[^>]*>[\s\S]*?<\/anim>/g)) {
		const line = lineOf(text, m.index), head = m[0].match(/<anim\b[^>]*>/)[0];
		const aidx = (head.match(/\bidx\s*=\s*"(\d+)"/) || [])[1];
		if (!aidx || Number(aidx) < 1 || Number(aidx) > 28) error(f, line, `<anim> with idx ${aidx ?? '(missing)'}: it has to be between 1 and 28`);
		else if (animIdx.has(aidx)) error(f, line, `<anim> with idx ${aidx} repeated`);
		else animIdx.add(aidx);
		const frames = Number((head.match(/\bframes\s*=\s*"(\d+)"/) || [])[1]);
		const frameTags = [...m[0].matchAll(/<frame\b[^>]*>/g)];
		if (frames > 64) error(f, line, `frames="${frames}": the engine allows 64 at most`);
		if (frames && frameTags.length && frameTags.length !== frames) warn(f, line, `the animation says frames="${frames}" but has ${frameTags.length} <frame>`);
		frameTags.forEach((t, k) => {
			const fi = Number((t[0].match(/\bidx\s*=\s*"(\d+)"/) || [])[1]);
			if (frames && (fi < 1 || fi > frames)) error(f, line, `<frame idx="${fi}" outside 1..${frames}: the game does not load the entity`);
		});
		if (frameTags.length >= 2) {
			const d0 = (frameTags[0][0].match(/\bduration\s*=\s*"(\d+)"/) || [])[1], d1 = (frameTags[frameTags.length - 1][0].match(/\bduration\s*=\s*"(\d+)"/) || [])[1];
			if (d0 !== '0' || d1 !== '0') warn(f, line, 'the first and last <frame> of an animation are the two sentinels and carry duration="0" (all the game\'s animations do this)');
		}
		for (const r of m[0].matchAll(/<replace\b[^>]*>/g)) {
			const lay = (r[0].match(/\blayer\s*=\s*"(\d+)"/) || [])[1], img = (r[0].match(/\bimage\s*=\s*"(\d+)"/) || [])[1];
			if (lay && !layerIdx.has(lay)) error(f, line, `<replace layer="${lay}"> and there is no <layer idx="${lay}">`);
			if (img && img !== '0') {
				const im = images.get(img);
				if (!im) error(f, line, `<replace image="${img}"> and there is no <image idx="${img}"> (the game crashes on load)`);
				else if (frames && im.rows) {
					if (frames - 2 > im.rows) warn(f, line, `the animation has ${frames - 2} usable frames (frames - 2) and image ${img} only ${im.rows} rows: it would read rows that do not exist`);
					else if (frames - 2 < im.rows) warn(f, line, `the animation uses ${frames - 2} frames and image ${img} has ${im.rows} rows (normally frames = rows + 2)`);
				}
			}
		}
	}
}
// The class that points to an entity that does not exist (in the mod or, with --game, in the game)
for (const [id, c] of classHeaders) {
	const e = (c.header.match(/\bentity\s*=\s*"([^"]*)"/) || [])[1];
	if (!e) continue;
	const rel = e.replace(/\\/g, '/');
	if (has(rel) || hasDep(rel)) continue;
	if (gameHas(rel) === false) error(c.file, 0, `the class ${id}: entity="${e}" exists neither in the mod nor in the game`);
}

// --- summary ----------------------------------------------------------------------------------------------------------
console.log('');
if (!errors && !warnings) console.log(`OK: ${files.length} files checked, no problems.`);
else console.log(`${files.length} files checked: ${errors} errors, ${warnings} warnings.`);
if (!dataDir) console.log('(Without --data, the tool does not check that the files and scripts the mod refers to exist in the game.)');
process.exit(errors ? 1 : 0);
