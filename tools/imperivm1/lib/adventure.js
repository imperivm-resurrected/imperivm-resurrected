// Converts the files of an Imperivm I adventure into those of a Great Battles of Rome adventure.
//
// Input: the files of the .bfhp (Map path -> Buffer), the Spanish texts and voices of local.pak and the catalogs of both
// games. Output: the files of the new .bfhp and a report of what was converted, renamed, substituted and dropped.
'use strict';
const S = require('./script');
const X = require('./xml');
const G = require('./grid');
const M = require('./mapping');

const EOL = '\r\n';
const LANGS = ['English', 'Spanish', 'Italian'];

class Report {
	constructor() {
		this.sections = new Map();
	}

	section(name) {
		if (!this.sections.has(name)) this.sections.set(name, { lines: [], counts: new Map() });
		return this.sections.get(name);
	}

	add(name, line) {
		this.section(name).lines.push(line);
	}

	count(name, key, n = 1) {
		const m = this.section(name).counts;
		m.set(key, (m.get(key) || 0) + n);
	}

	lines(name) {
		return this.sections.has(name) ? this.sections.get(name).lines : [];
	}

	toText() {
		const out = [];
		for (const [name, s] of this.sections) {
			out.push(`== ${name}`);
			for (const l of s.lines) out.push(`  ${l}`);
			for (const [k, n] of [...s.counts].sort()) out.push(`  ${k}: ${n}`);
			out.push('');
		}
		return out.join('\n');
	}
}

const text = (buf) => buf.toString('latin1');
const bytes = (s) => Buffer.from(s, 'latin1');
const lower = (s) => s.toLowerCase();

// I1 layer -> GBR layer: same texture first, then the same name, then the fallback table, then the first of the same type.
function terrainRemap(i1, gbr, report) {
	const remap = [];
	for (const l of i1) {
		let to = gbr.find((g) => g.image && g.image === l.image && !/^winter|^autumn/i.test(g.display));
		let how = 'texture';
		if (!to && l.display) {
			to = gbr.find((g) => lower(g.display) === lower(l.display));
			how = 'name';
		}
		if (!to && M.TERRAIN_FALLBACKS[l.display]) {
			to = gbr.find((g) => g.display === M.TERRAIN_FALLBACKS[l.display]);
			how = 'closest';
		}
		if (!to) {
			to = gbr.find((g) => g.type === l.type);
			how = 'same type';
		}
		if (!to) throw new Error(`terrain layer ${l.z} (${l.display}) has no equivalent`);
		remap[l.z] = to.z;
		report.add('Terrain layers (Imperivm I -> Great Battles of Rome)', `${l.z} ${l.display || '(waves)'} -> ${to.z} ${to.display || '(waves)'} [${how}]`);
	}
	return remap;
}

function decorRemap(i1, gbr, report) {
	const bySection = new Map([...gbr].map(([t, s]) => [lower(s), t]));
	const remap = {};
	let same = 0;
	for (const [type, section] of i1) {
		const to = bySection.get(lower(section));
		if (to === undefined) report.add('Decors', `type ${type} [${section}] does not exist in Great Battles of Rome: kept`);
		else if (to !== type) {
			remap[type] = to;
			report.add('Decors', `type ${type} [${section}] -> ${to}`);
		} else same++;
	}
	report.add('Decors', `${same} of ${i1.size} decor types have the same number in both games`);
	return remap;
}

// Class id (any case) -> {id, gbr} for every class of Imperivm I; gbr is the GBR id to write or null if there is none.
function classResolver(i1, gbr, keep, report) {
	const gbrIds = new Map();
	for (const c of gbr.classes.values()) {
		gbrIds.set(lower(c.id), c.id);
		if (c.altid) gbrIds.set(lower(c.altid), c.altid);
	}
	const substitutes = {};
	for (const [from, to] of Object.entries(M.CLASS_SUBSTITUTES)) {
		if (keep.has(from)) continue;
		if (!gbrIds.has(lower(to))) throw new Error(`substitute ${to} for ${from} is not a class of Great Battles of Rome`);
		substitutes[lower(from)] = to;
	}
	return {
		gbrIds,
		substitutes,
		resolve(id) {
			if (gbrIds.has(lower(id))) return { to: id, substituted: false };
			if (substitutes[lower(id)]) return { to: substitutes[lower(id)], substituted: true };
			return { to: null, substituted: false };
		},
		i1DisplayName(id) {
			const c = [...i1.classes.values()].find((x) => lower(x.id) === lower(id));
			return c ? c.displayName : null;
		},
		gbrDisplayName(id) {
			const c = [...gbr.classes.values()].find((x) => lower(x.id) === lower(id) || (x.altid && lower(x.altid) === lower(id)));
			return c ? c.displayName : null;
		},
		gbrCpp(id) {
			if (!this.cppById) {
				this.cppById = new Map();
				for (const c of gbr.classes.values()) {
					this.cppById.set(lower(c.id), c.cpp);
					if (c.altid && !this.cppById.has(lower(c.altid))) this.cppById.set(lower(c.altid), c.cpp);
				}
			}
			return this.cppById.get(lower(id)) || null;
		},
	};
}

function convertAssetPath(p, gbr, report, where) {
	if (!p || !/^assets\//i.test(p)) return p;
	let m;
	if ((m = /^assets\/items\/(.+)$/i.exec(p))) {
		const to = 'gameres/icons/items/' + m[1];
		if (gbr.hasGameres(to)) return to;
	}
	if ((m = /^assets\/interface\/icons\/(.+)$/i.exec(p))) {
		const to = 'gameres/icons/' + m[1];
		if (gbr.hasGameres(to)) return to;
	}
	// Settlement and note icons: Great Battles of Rome's own adventures leave them empty.
	if (/^assets\/interface\/settlement icon$/i.test(p) || /^assets\/entities\/notifications\/note\.rle$/i.test(p)) return '';
	report.add('Dropped', `${where}: picture ${p} has no equivalent, left empty`);
	return '';
}

function convertGameXml(src, name, report) {
	let t = src.replace(/\r?\n\s*adventure="[^"]*"/, '');
	for (const [k, v] of Object.entries(M.GAME_PROPERTIES)) {
		const r = X.mapAttribute(t, k, () => v);
		t = r.count ? r.text : t.replace(/(<properties)/, `$1${EOL}\t\t\t${k}="${v}"`);
	}
	t = X.mapAttribute(t, 'name', () => X.gameEscape(name)).text;
	report.add('Adventure', `game.xml: game_type=${M.GAME_PROPERTIES.game_type}, user_interface=${M.GAME_PROPERTIES.user_interface}, name "${name}"`);
	return t;
}

function convertNotes(src, gbr, report, where) {
	let t = X.mapAttribute(src, 'icon', (v) => convertAssetPath(v, gbr, report, where)).text;
	t = t.replace(/<note\b([^>]*?)(\/?>)/g, (all, attrs, end) => {
		if (/\stitle="/.test(attrs)) return all;
		const id = /\sid="([^"]*)"/.exec(attrs);
		const x = /\slocationx="(-?\d+)"/.exec(attrs);
		const onMap = x && Number(x[1]) >= 0 ? '1' : '0';
		let a = attrs.replace(/(\sid="[^"]*")/, `$1${EOL}\t\t\ttitle="${id ? id[1] : ''}"`);
		a = /\smap="/.test(a) ? a.replace(/(\smap="[^"]*")/, `$1${EOL}\t\t\tshow_on_minimap="${onMap}"`) : a + `${EOL}\t\t\tshow_on_minimap="${onMap}"`;
		report.count('Notes', 'notes given a title and show_on_minimap');
		return '<note' + a + end;
	});
	return t;
}

function convertMapXml(src) {
	if (/\spersist_state="/.test(src)) return X.mapAttribute(src, 'persist_state', () => '1').text;
	return src.replace(/(<map\b[^>]*?)(\s*)>/, `$1${EOL}\t\tpersist_state="1">`);
}

function convertObjects(src, classes, gbr, report, where) {
	let t = src;
	const rewriteClass = (attrName) => {
		t = t.replace(new RegExp(`(<(scriptobj|obj|settlement)\\b[^>]*?\\s${attrName}=")([^"]*)(")`, 'g'), (all, a, el, id, b) => {
			const r = classes.resolve(id);
			if (!r.to) {
				report.add('Errors', `${where}: class ${id} does not exist in Great Battles of Rome and has no substitute`);
				return all;
			}
			if (!r.substituted) return all;
			report.count('Classes substituted (objects)', `${id} -> ${r.to}`);
			return a + r.to + b;
		});
	};
	// Heroes keep their name when the substitute is another character: display_name is added to the object.
	t = t.replace(/(<scriptobj\b)([^>]*?)(\/?>)/g, (all, open, attrs, end) => {
		const m = /\sclass="([^"]*)"/.exec(attrs);
		if (!m || /\sdisplay_name="/.test(attrs)) return all;
		const r = classes.resolve(m[1]);
		if (!r.substituted) return all;
		const before = classes.i1DisplayName(m[1]);
		const after = classes.gbrDisplayName(r.to);
		if (!before || before === after || !/hero/i.test(m[1])) return all;
		report.count('Hero names kept on substituted heroes', `${m[1]} "${before}"`);
		return open + attrs.replace(/(\snum="[^"]*")/, `$1${EOL}\tdisplay_name="${X.gameEscape(before)}"`) + end;
	});
	rewriteClass('class');
	rewriteClass('classoffirstbuilding');
	t = t.replace(/(<scriptobj\b)([^>]*?)(\/?>)/g, (all, open, attrs, end) => {
		const m = /\sclass="([^"]*)"/.exec(attrs);
		if (!m || /\sstamina="/.test(attrs)) return all;
		const stamina = M.STAMINA[classes.gbrCpp(m[1])];
		if (stamina === undefined) return all;
		const at = /(\shealthperc="[^"]*")/.test(attrs) ? /(\shealthperc="[^"]*")/ : /(\snum="[^"]*")/;
		report.count('Map objects given stamina, as in Great Battles of Rome\'s maps', `stamina="${stamina}"`);
		return open + attrs.replace(at, `$1${EOL}\tstamina="${stamina}"`) + end;
	});
	t = X.mapAttribute(t, 'icon', (v) => convertAssetPath(v, gbr, report, where)).text;
	t = X.mapAttribute(t, 'Icon', (v) => convertAssetPath(v, gbr, report, where)).text;
	return t;
}

// Applies every script rewrite; returns the new source.
function convertScript(src, ctx, where) {
	let s = src;
	const r1 = S.renameFunctions(s, M.FUNCTION_RENAMES);
	s = r1.src;
	if (r1.count) ctx.report.count('Script functions renamed', `${where}`, r1.count);
	const r2 = S.expandReduceGroup(s);
	s = r2.src;
	if (r2.count) ctx.report.count('ReduceGroup rewritten as script', where, r2.count);
	const r3 = S.rewriteRituals(s, M.TIMED_RITUALS);
	s = r3.src;
	if (r3.rituals) ctx.report.count('Stonehenge rituals Great Battles of Rome lacks, rewritten as a timer', where, r3.rituals);
	if (r3.checks) ctx.report.count('IsSpellCasted rewritten (timer or command check)', where, r3.checks);
	const rh = S.rewriteDruidCommands(s, (receiver, cmd) => {
		const cls = ctx.receiverClass(receiver, s, where);
		if (!cls) {
			ctx.report.add('Check warnings', `${where}: ${receiver}.SetCommand("${cmd}") left as it is (could not tell the class of ${receiver})`);
			return false;
		}
		return !ctx.gbr.classHasMethod(cls, cmd);
	});
	s = rh.src;
	if (rh.counts.hide) ctx.report.count('Druid "hide" rewritten as SetVisible(false)', where, rh.counts.hide);
	if (rh.counts.mist) ctx.report.count('Druid "mist" rewritten as placing the Mist', where, rh.counts.mist);
	for (const k of rh.skipped) ctx.report.add('Check warnings', `${where}: ${k} left as it is (Gaul druids cannot do it in Great Battles of Rome)`);
	s = S.rewriteCall(s, 'PlayMovie', (arg) => ctx.movie(arg, where)).src;
	// The silence Imperivm I plays under its intro movie is a file Great Battles of Rome does not have.
	s = S.rewriteCall(s, 'PlayMusic', (arg) => {
		if (!/^"packs[\\/]+silence\.wav"$/i.test(arg)) return arg;
		ctx.report.add('Movies', `${where}: PlayMusic(${arg}) commented out (no such file in Great Battles of Rome)`);
		return null;
	}).src;
	// Commands given to named objects whose class in Great Battles of Rome has no such method.
	if (ctx.receiverClass) {
		const tokens = S.tokenize(s).filter((t) => t.type !== 'space' && t.type !== 'comment');
		for (let i = 2; i + 2 < tokens.length; i++) {
			if (!/^(SetCommand|AddCommand)$/.test(tokens[i].text) || tokens[i - 1].text !== '.' || tokens[i + 1].text !== '(') continue;
			const str = tokens.slice(i + 2, i + 6).find((t) => t.type === 'string');
			if (!str) continue;
			const cmd = str.text.slice(1, -1);
			let k = i - 2;
			while (k > 0 && (tokens[k - 1].text === '.' || (tokens[k].text === ')' && tokens[k - 1].text === '('))) k -= tokens[k].text === ')' ? 3 : 2;
			const receiver = tokens.slice(Math.max(k, 0), i - 1).map((t) => t.text).join('');
			const cls = ctx.receiverClass(receiver, s, where);
			if (cls) ctx.report.count('Checks', 'commands to named objects checked against their class');
			if (cls && !ctx.gbr.classHasMethod(cls, cmd)) ctx.report.add('Check warnings', `${where}: ${receiver} (${cls}) is told "${cmd}", which ${cls} cannot do in Great Battles of Rome`);
		}
	}
	const r5 = S.replaceStringLiterals(s, ctx.classLiterals);
	s = r5.src;
	for (const [k, n] of Object.entries(r5.hits)) ctx.report.count('Classes substituted (script strings)', `${k} -> ${ctx.classLiterals[k]}`, n);
	// Pictures named in scripts (the portrait of a conversation actor).
	const paths = {};
	for (const t of S.tokenize(s)) {
		const body = t.type === 'string' ? t.text.slice(1, -1) : '';
		if (/^assets\//i.test(body)) paths[body] = convertAssetPath(body, ctx.gbr, ctx.report, where);
	}
	const rp = S.replaceStringLiterals(s, paths);
	s = rp.src;
	for (const [k, n] of Object.entries(rp.hits)) ctx.report.count('Pictures renamed in scripts', `${k} -> ${paths[k] || '(none)'}`, n);
	const r6 = S.replaceStringLiterals(s, M.RESEARCH_RENAMES);
	s = r6.src;
	for (const [k, n] of Object.entries(r6.hits)) ctx.report.count('Researches renamed', `${k} -> ${M.RESEARCH_RENAMES[k]}`, n);
	return s;
}

// The adventure's own items, plus `extra`: items of Imperivm I's ITEMS.XML that the maps use and Great Battles of Rome
// does not have (parsed <item> elements).
function convertItems(src, ctx, extra = []) {
	const doc = X.parseXml(src);
	const items = [];
	const out = ['\t<items>'];
	for (const it of [...doc.root.children.filter((c) => c.name === 'item'), ...extra]) {
		const a = (k, d = '') => (it.attrs.has(k) ? it.attrs.get(k) : d);
		const bonus = it.children.find((c) => c.name === 'bonus');
		const b = (k) => Number((bonus && bonus.attrs.get(k)) || 0);
		const id = a('id');
		items.push(id);
		let use = a('usescript');
		const vsFile = a('vs_file');
		if (!use && vsFile) {
			if (ctx.gbr.hasData(vsFile)) use = 'file://' + vsFile;
			else if (ctx.i1Data(vsFile)) use = X.gameEscape(ctx.i1Data(vsFile).toString('latin1'));
			else ctx.report.add('Dropped', `item "${id}": script ${vsFile} not found`);
		}
		if (use && !/^file:\/\//.test(use)) use = X.gameEscape(convertScript(X.gameUnescape(use), ctx, `itemsCustom.xml "${id}"`));
		if (a('scripttarget', 'None') !== 'None') ctx.report.add('Dropped', `item "${id}": scripttarget=${a('scripttarget')} (Great Battles of Rome has none)`);
		const damage = Math.round((b('attack') + b('maxattack')) / 2);
		if (b('attack') || b('maxattack')) ctx.report.add('Items', `"${id}": attack ${b('attack')} / max attack ${b('maxattack')} -> damage ${damage} (their average)`);
		const fields = [
			['id', id],
			['name', id],
			['image', convertAssetPath(a('image'), ctx.gbr, ctx.report, `item "${id}"`)],
			['sound', 'data/sound entities/itemuse.xml'],
			['level', a('level', '0')],
			['description', a('description')],
			['usecount', a('usecount', '0') || '0'],
			['customdata', '0'],
			['use_script', use],
			['location_script', ''],
			['object_script', ''],
			['kill_script', ''],
			['die_script', ''],
			['attachedkill_script', ''],
			['attacheddie_script', ''],
			['equip_script', ''],
			['remove_script', ''],
			['important', a('important', 'no')],
			['cursed', a('cursed', '0') || '0'],
		];
		out.push('\t\t<item');
		fields.forEach(([k, v], i) => out.push(`\t\t\t${k}="${v}"${i === fields.length - 1 ? '>' : ''}`));
		const bonusFields = [
			['health', b('health')],
			['damage', damage],
			['armor_slash', b('defense_slash')],
			['armor_pierce', b('defense_pierce')],
			['health_percent', 0],
			['damage_percent', 0],
			['armor_slash_percent', 0],
			['armor_pierce_percent', 0],
			['level', b('level')],
			['experience', b('experience')],
		];
		out.push('\t\t\t<bonus');
		bonusFields.forEach(([k, v], i) => out.push(`\t\t\t\t${k}="${v}"${i === bonusFields.length - 1 ? '/>' : ''}`));
		out.push('\t\t</item>');
		if (extra.includes(it)) ctx.report.add('Items', `"${id}": an Imperivm I item the maps give, added to the adventure`);
		else ctx.report.count('Items', 'custom items converted (usescript -> use_script, attack/defense -> damage/armor)');
	}
	out.push('\t</items>', '');
	return { text: out.join(EOL), items };
}

// The local.pak folder of the adventure: Map upper-case relative path -> Buffer.
function localFiles(local, advName, lang) {
	const prefix = `ADVENTURES\\${advName.toUpperCase()}\\${lang.toUpperCase()}\\`;
	const files = new Map();
	for (const e of local.index.values()) {
		const up = e.name.toUpperCase();
		if (up.startsWith(prefix)) files.set(up.slice(prefix.length).replace(/\\/g, '/'), local.read(e.name));
	}
	return files;
}

// Turns a translation table of the source language into another one: `result` from `pick(attrs)`, sound paths through
// `sound(path)` (null drops the attribute).
function retranslate(src, pick, sound) {
	return src.replace(/<translationtableentry\b([^>]*?)\/>/g, (all, attrs) => {
		let a = attrs;
		const t = /\stext="([^"]*)"/.exec(a);
		const r = /\sresult="([^"]*)"/.exec(a);
		const value = pick(t ? t[1] : '', r ? r[1] : '');
		a = r ? a.replace(/\sresult="[^"]*"/, ` result="${value}"`) : `${a} result="${value}"`;
		a = a.replace(/\ssound="([^"]*)"/, (s, p) => {
			const next = sound(p);
			return next === null ? '' : ` sound="${next}"`;
		});
		return `<translationtableentry${a}/>`;
	});
}

// The game keys a translation table by `text`: an entry with an empty text, or a text already in the table, is refused
// ("KeyTextIsAlreadyInTheTable" in vxlog.log). Keeps the first entry of each text. Returns {src, empty, duplicate}.
function cleanTable(src) {
	const seen = new Set();
	let empty = 0;
	let duplicate = 0;
	const out = src.replace(/<translationtableentry\b([^>]*?)\/>/g, (all, attrs) => {
		const t = /\stext="([^"]*)"/.exec(attrs);
		if (!t || !t[1]) {
			empty++;
			return '';
		}
		if (seen.has(t[1])) {
			duplicate++;
			return '';
		}
		seen.add(t[1]);
		return all;
	});
	return { src: out, empty, duplicate };
}

// Sets the result of the entry with this id ("adventure name", "adventure description").
function setResult(src, id, value) {
	return src.replace(new RegExp(`(<translationtableentry\\b[^>]*\\sid="${id}"[^>]*?\\sresult=")[^"]*(")`), (all, a, b) => a + X.entityEscape(value) + b);
}

function convertAdventure(input) {
	const { files, gbr, i1, local, advName, name, keep, movies } = input;
	const report = new Report();
	const out = new Map();
	const terrain = terrainRemap(i1.terrains, gbr.terrains, report);
	const decors = decorRemap(i1.decors, gbr.decors, report);
	const classes = classResolver(i1, gbr, keep, report);
	const classLiterals = {};
	for (const c of i1.classes.values()) {
		const r = classes.resolve(c.id);
		if (r.substituted) classLiterals[c.id] = r.to;
	}
	for (const [from, to] of Object.entries(M.CLASS_SUBSTITUTES)) {
		if (keep.has(from)) report.add('Classes kept for a mod to provide', from);
		else report.add('Substitution table', `${from} (${classes.i1DisplayName(from) || 'object'}) -> ${to} (${classes.gbrDisplayName(to) || 'object'})`);
	}
	const ctx = {
		report,
		gbr,
		classLiterals,
		movie(arg, where) {
			const m = /^"packs[\\/]+(data\d+)\.bfhp"$/i.exec(arg);
			if (m && movies && movies.has(m[1].toLowerCase())) {
				report.add('Movies', `${where}: PlayMovie(${arg}) -> movies\\${movies.get(m[1].toLowerCase())}`);
				return `"movies\\\\${movies.get(m[1].toLowerCase())}"`;
			}
			report.add('Movies', `${where}: PlayMovie(${arg}) commented out`);
			return null;
		},
	};

	// Map folder id -> map name, and the sequence files each map runs.
	const mapNames = new Map();
	const referenced = new Set();
	for (const [p, buf] of files) {
		let m;
		if ((m = /^Maps\/([^/]+)\/map\.xml$/i.exec(p))) {
			const n = /\sname="([^"]*)"/.exec(text(buf));
			mapNames.set(m[1], n ? n[1] : m[1]);
		}
		if ((m = /^(Maps\/[^/]+\/)?Sequences\/sequences\.xml$/i.exec(p))) {
			for (const s of text(buf).matchAll(/script="Current(?:Map|Game)\/sequences\/([^"]+)"/gi)) referenced.add(lower((m[1] || '') + 'Sequences/' + s[1]));
		}
	}
	report.add('Adventure', `${mapNames.size} maps: ${[...mapNames].map(([id, n]) => `${n} (${id})`).join(', ')}`);

	// Named objects of each map (groups of type 0 with one object) and their class in Great Battles of Rome.
	const namedByMap = new Map();
	for (const [p, buf] of files) {
		const m = /^Maps\/([^/]+)\/map\.obj\.xml$/i.exec(p);
		if (!m) continue;
		const t = text(buf);
		const cls = new Map();
		for (const o of t.matchAll(/<scriptobj\s+class="([^"]*)"\s+num="(\d+)"/g)) cls.set(o[2], o[1]);
		const named = new Map();
		for (const g of t.matchAll(/<group\s+name="([^"]*)"\s+type="0">([\s\S]*?)<\/group>/g)) {
			const nums = [...g[2].matchAll(/num="(\d+)"/g)];
			if (nums.length !== 1 || !cls.has(nums[0][1])) continue;
			const r = classes.resolve(cls.get(nums[0][1]));
			named.set(g[1], r.to || cls.get(nums[0][1]));
		}
		namedByMap.set(m[1], named);
	}
	// Class of the receiver of a command in a script: a named object, directly (GetNamedObj("X")) or through a variable.
	ctx.receiverClass = (receiver, src, where) => {
		const map = /^Maps\/([^/]+)\//i.exec(where);
		const named = map && namedByMap.get(map[1]);
		if (!named) return null;
		const vars = new Map([...src.matchAll(/(\w+)\s*=\s*GetNamedObj\s*\(\s*"([^"]+)"\s*\)/g)].map((m) => [m[1], m[2]]));
		let m;
		if ((m = /^GetNamedObj\s*\(\s*"([^"]+)"\s*\)(\.obj)?$/.exec(receiver))) return named.get(m[1]) || null;
		// GetNamedObj("NO_Priest" + n): the class all the numbered objects with that prefix share.
		if ((m = /^GetNamedObj\s*\(\s*"([^"]+)"\s*\+[^)]*\)(\.obj)?$/.exec(receiver))) {
			const prefix = new RegExp('^' + m[1].replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\d+$');
			const found = new Set([...named].filter(([n]) => prefix.test(n)).map(([, c]) => c));
			return found.size === 1 ? [...found][0] : null;
		}
		if ((m = /^(\w+)(\.obj)?$/.exec(receiver)) && vars.has(m[1])) return named.get(vars.get(m[1])) || null;
		return null;
	};

	let customItems = [];
	for (const [p, buf] of files) {
		const base = p.split('/').pop();
		let t;
		if (/^game\.xml$/i.test(p)) {
			out.set(p, bytes(convertGameXml(text(buf), name, report)));
		} else if (/^player\d+\.xml$/i.test(p)) {
			t = X.mapAttribute(text(buf), 'race', (v) => {
				if (!M.RACES[v]) return v;
				report.count('Player races', `${v} -> ${M.RACES[v]}`);
				return M.RACES[v];
			}).text;
			t = X.mapAttribute(t, 'control', (v) => {
				if (!M.PLAYER_CONTROL[v]) return v;
				report.count('Player control', `${v} -> ${M.PLAYER_CONTROL[v]}`);
				return M.PLAYER_CONTROL[v];
			}).text;
			out.set(p, bytes(t));
		} else if (/^itemsCustom\.xml$/i.test(p)) {
			continue;
		} else if (/(^|\/)Notes\.xml$/i.test(p)) {
			out.set(p, bytes(convertNotes(text(buf), gbr, report, p)));
		} else if (/^Maps\/[^/]+\/map\.xml$/i.test(p)) {
			out.set(p, bytes(convertMapXml(text(buf))));
			report.count('Maps', 'map.xml given persist_state="1"');
		} else if (/^Maps\/[^/]+\/map\.obj\.xml$/i.test(p)) {
			out.set(p, bytes(convertObjects(text(buf), classes, gbr, report, p)));
		} else if (/Terrain\.terrain\.grid$/i.test(p)) {
			const r = G.widenTerrain(buf, terrain);
			out.set(p, r.buf);
			report.count('Grids', 'terrain grids widened from 4 to 8 bits and remapped');
		} else if (/Terrain\.decor\.grid$/i.test(p)) {
			const r = G.remapDecor(buf, decors);
			out.set(p, r.buf);
			if (r.changed) report.count('Grids', 'decor cells renumbered', r.changed);
		} else if (/\.vs$/i.test(p)) {
			if (!referenced.has(lower(p))) {
				report.add('Dropped', `${p}: not run by any sequences.xml (left over from the map's development)`);
				continue;
			}
			out.set(p, bytes(convertScript(text(buf), ctx, p)));
		} else {
			out.set(p, buf);
			report.count('Copied as they are', /\.[a-z0-9]+$/i.exec(base) ? /\.[a-z0-9]+$/i.exec(base)[0].toLowerCase() : base);
		}
	}
	// Items: the adventure's own, and the items of Imperivm I that the maps give but Great Battles of Rome lacks.
	const itemsPath = [...files.keys()].find((k) => /^itemsCustom\.xml$/i.test(k)) || 'itemsCustom.xml';
	const ownItems = files.has(itemsPath) ? text(files.get(itemsPath)) : '\t<items>\r\n\t</items>\r\n';
	const known = new Set([...gbr.items, ...[...ownItems.matchAll(/\sid="([^"]*)"/g)].map((m) => m[1])].map(lower));
	const wanted = new Set();
	for (const [p, buf] of out) {
		if (/map\.obj\.xml$/i.test(p)) for (const m of text(buf).matchAll(/\sslot\d+="([^"]+)"/g)) wanted.add(m[1]);
		if (/\.vs$/i.test(p)) {
			const tokens = S.tokenize(text(buf)).filter((t) => t.type !== 'space' && t.type !== 'comment');
			for (let i = 0; i + 2 < tokens.length; i++) {
				if (/^(AddItem|FindItem|RemoveItem)$/.test(tokens[i].text) && tokens[i + 1].text === '(' && tokens[i + 2].type === 'string') wanted.add(tokens[i + 2].text.slice(1, -1));
			}
		}
	}
	const i1Items = X.parseXml(i1.items).roots.flatMap((r) => [...X.walk(r)]).filter((e) => e.name === 'item');
	const extra = [];
	for (const w of wanted) {
		if (known.has(lower(w))) continue;
		const it = i1Items.find((e) => lower(e.attrs.get('id') || '') === lower(w));
		if (it) extra.push(it);
		else report.add('Check warnings', `item "${w}" exists in neither game`);
	}
	ctx.i1Data = (p) => i1.data.read(p.replace(/^data\//i, ''));
	const items = convertItems(ownItems, ctx, extra);
	customItems = items.items;
	out.set(itemsPath, bytes(items.text));

	for (const id of mapNames.keys()) {
		const labels = `Maps/${id}/labels.xml`;
		if (![...out.keys()].some((k) => lower(k) === lower(labels))) {
			out.set(labels, bytes(`\t<root>${EOL}\t</root>${EOL}`));
			report.count('Maps', 'empty labels.xml added');
		}
	}

	// Texts and voices. Spanish comes from local.pak; English and Italian are the English text of the adventure.
	const es = localFiles(local, advName, 'Spanish');
	const nameToId = new Map([...mapNames].map(([id, n]) => [lower(n), id]));
	const soundPath = (p) => {
		const m = /^([^/]+)\/conversations\/(.+)$/i.exec(p.replace(/\\/g, '/'));
		if (!m) return p;
		const id = nameToId.get(lower(m[1])) || m[1];
		return `${id}/conversations/${lower(m[2])}`;
	};
	const adventureEntry = (t) =>
		t.replace(/(<translationtableentry\b[^>]*\sid="adventure name"[^>]*\/>)/, (e) =>
			e.replace(/\stext="[^"]*"/, ` text="${X.entityEscape(name)}"`).replace(/\sresult="[^"]*"/, ` result="${X.entityEscape(name)}"`),
		);
	const targets = {
		'ADVENTURE.LOC.XML': 'adventure.loc.xml',
		'ITEMS.XML': 'itemsCustom.loc.xml',
		'NOTES.XML': 'notes.xml',
		'MAPS/NOTES.XML': 'Maps/notes.xml',
	};
	let voices = 0;
	const rootNotes = new Set();
	if (es.has('NOTES.XML')) for (const m of es.get('NOTES.XML').toString('utf8').matchAll(/\stext="([^"]*)"/g)) rootNotes.add(m[1]);
	const put = (lang, dest, src) => {
		const c = cleanTable(src);
		if (c.empty) report.count('Translation tables', `${lang}: entries without text dropped`, c.empty);
		if (c.duplicate) report.count('Translation tables', `${lang}: repeated texts dropped (the game keeps one per text)`, c.duplicate);
		out.set(`Local/${lang}/${dest}`, Buffer.from(c.src, 'utf8'));
	};
	for (const [p, buf] of es) {
		let dest = targets[p];
		let m;
		if (!dest && (m = /^MAPS\/([^/]+)\/NOTES\.XML$/.exec(p))) dest = `Maps/${m[1]}/notes.xml`;
		if (!dest && (m = /^MAPS\/([^/]+)\/CONVERSATIONS\/(.+)$/.exec(p))) dest = `Maps/${m[1]}/conversations/${lower(m[2])}`;
		if (!dest) {
			report.add('Dropped', `local.pak Spanish/${p}: not used by Great Battles of Rome (translator's working file)`);
			continue;
		}
		if (/\.wav$/i.test(dest)) {
			for (const lang of ['Spanish', 'English']) out.set(`Local/${lang}/${dest}`, buf);
			voices++;
			continue;
		}
		const src = adventureEntry(buf.toString('utf8'));
		// Imperivm I repeats every note of the adventure in MAPS/NOTES.XML; Great Battles of Rome's adventures leave it empty.
		if (dest === 'Maps/notes.xml' && [...src.matchAll(/\stext="([^"]*)"/g)].every((m) => rootNotes.has(m[1]))) {
			for (const lang of LANGS) out.set(`Local/${lang}/${dest}`, bytes('<translationtable/>' + EOL));
			report.add('Translation tables', 'Maps/notes.xml left empty: Imperivm I repeats there the notes of notes.xml');
			continue;
		}
		const spanish = (t, r) => (r && r !== '(translate)' ? r : t);
		let es1 = retranslate(src, spanish, soundPath);
		let it1 = retranslate(src, (t) => t, () => null);
		if (dest === 'adventure.loc.xml') {
			const d = /<translationtableentry\b[^>]*\sid="adventure description"[^>]*\/>/.exec(es1);
			const translated = d && /\sresult="([^"]*)"/.exec(d[0]);
			const textOf = d && /\stext="([^"]*)"/.exec(d[0]);
			if (!translated || !textOf || translated[1] === textOf[1]) {
				es1 = setResult(es1, 'adventure description', M.ADVENTURE_TEXTS.Spanish.description);
				report.add('Translation tables', 'Spanish description of the adventure written by the converter (none in local.pak)');
			} else {
				report.add('Translation tables', 'Spanish name and description of the adventure: the description is Imperivm I\'s own (local.pak)');
			}
			es1 = setResult(es1, 'adventure name', name);
			it1 = setResult(setResult(it1, 'adventure description', M.ADVENTURE_TEXTS.Italian.description), 'adventure name', M.ADVENTURE_TEXTS.Italian.name);
			report.add('Translation tables', 'Italian name and description of the adventure written by the converter (Imperivm I has no Italian texts)');
		}
		put('Spanish', dest, es1);
		put('English', dest, retranslate(src, (t) => t, soundPath));
		put('Italian', dest, it1);
	}
	report.add('Texts and voices', `Spanish texts from local.pak; English and Italian use the English text of the adventure`);
	report.add('Texts and voices', `${voices} Spanish voices under Local/Spanish and Local/English (Imperivm I has no English voices); Italian has text only`);
	for (const lang of LANGS) {
		if (![...out.keys()].some((k) => lower(k) === lower(`Local/${lang}/itemsCustom.loc.xml`))) out.set(`Local/${lang}/itemsCustom.loc.xml`, bytes('<translationtable/>' + EOL));
	}
	// Every adventure of Great Battles of Rome has these two folders, empty. Without "Conversations" the game cannot load
	// CurrentGame/conversations when a map starts ("Empty or invalid directory specified for CVXConvRegistry::Load" in
	// vxlog.log), and the code that loads the map's conversations throws when that load fails.
	const dirs = ['Conversations', 'Resources'];
	report.add('Adventure', 'empty Conversations and Resources folders, as in Great Battles of Rome\'s adventures');
	return { files: out, dirs, report, mapNames, customItems, terrain, decors, classes };
}

module.exports = { convertAdventure, Report, terrainRemap, retranslate, cleanTable, convertNotes, convertMapXml, convertItems };
