// Converts the files of an Imperivm I adventure into those of a Great Battles of Rome adventure.
//
// Input: the files of the .bfhp (Map path -> Buffer), the Spanish texts and voices of local.pak and the catalogs of both
// games. Output: the files of the new .bfhp and a report of what was converted, renamed, substituted and dropped.
'use strict';
const S = require('./script');
const X = require('./xml');
const G = require('./grid');
const M = require('./mapping');
const { addProbe, censusSequence } = require('./probe');

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
	// Decors of Great Battles of Rome (the rocks that stand for Imperivm I's cliffs and teleport rocks, the Roman arch) go
	// outside settlements, with no player and flags 0x80000000, as in its own maps; Imperivm I has the arch as a building
	// of a settlement.
	const isDecor = (id) => classes.gbrCpp(id) === 'CVXDecor';
	const moved = [];
	t = t.replace(/<settlement\b[\s\S]*?<\/settlement>/g, (block) =>
		block.replace(/[ \t]*<scriptobj\b[^>]*?\/>\r?\n?/g, (obj) => {
			const m = /\sclass="([^"]*)"/.exec(obj);
			if (!m || !isDecor(m[1])) return obj;
			moved.push(/\n$/.test(obj) ? obj : obj + EOL);
			report.count('Decors', `${m[1]} taken out of its settlement (a decor in Great Battles of Rome)`);
			return '';
		}),
	);
	if (moved.length) t = t.replace(/(<mapobject>\r?\n)/, `$1${moved.join('')}`);
	t = t.replace(/<scriptobj\b[^>]*?\/>/g, (obj) => {
		const m = /\sclass="([^"]*)"/.exec(obj);
		if (!m || !isDecor(m[1]) || (!/\splayer="/.test(obj) && /\sflags="0x80000000"/.test(obj))) return obj;
		report.count('Decors', `${m[1]} written without player, flags 0x80000000`);
		return obj.replace(/(\sflags=")0x[0-9a-fA-F]+(")/, '$10x80000000$2').replace(/\r?\n[ \t]*player="[^"]*"/, '');
	});
	// Imperivm I has no sentries; every settlement of Great Battles of Rome's maps says how many extra ones it starts with.
	t = t.replace(/(<settlement\b)([^>]*?)(\/?>)/g, (all, open, attrs, end) => {
		if (/\sextrasentries="/.test(attrs)) return all;
		const at = /(\smaxpopulation="[^"]*")/.exec(attrs) ? /(\smaxpopulation="[^"]*")/ : /(\splayer="[^"]*")/;
		const indent = (/\n([ \t]*)\S[^\n]*$/.exec(attrs) || [null, '\t\t\t'])[1];
		report.count('Sentries', 'settlements given extrasentries="0"');
		return open + attrs.replace(at, `$1${EOL}${indent}extrasentries="0"`) + end;
	});
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
	const ra = S.attackPointsToAdvance(s);
	s = ra.src;
	const mapId = (/^Maps\/([^/]+)\//i.exec(where) || [null, '(adventure)'])[1];
	if (ra.advanced) ctx.report.count('"attack" given a point, written "advance" (per map)', `map ${mapId}`, ra.advanced);
	if (ra.kept) ctx.report.count('"attack" given an object, kept (per map)', `map ${mapId}`, ra.kept);
	for (const u of ra.unknown) ctx.report.add('Check warnings', `${where}: ${u}: could not tell whether "attack" is given a point or an object; left as it is`);
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

// Imperivm I has no sentries. Great Battles of Rome's townhalls give each settlement 20 (InitialStrongholdSentries) once
// the settlement is ready and 4 more every 30 s, and its gates and walls place one at each post while the settlement has
// one available (Settlement::GetSentry, gbr.exe 005C49B0: takes one if +0xF4 > 0). The game's switch (NoSentries in
// data/const.ini) belongs to the game, not to an adventure, so every 2 s this sequence brings each settlement's maximum
// (+0x104, AddMaxSentries adds to it as it is, 005C2B30) to 0, takes the available ones (GetSentry) and gives each back
// as dead (DelSentry lowers the count, 005C2A60), and erases any sentry already placed. With the maximum at 0 the
// townhall adds none (it adds only while the count is under the maximum).
const NO_SENTRIES_SCRIPT = [
	'//void',
	'ObjList i1s_l;',
	'Settlement i1s_s;',
	'int i1s_p, i1s_i, i1s_m, i1s_k;',
	'',
	'while (1)',
	'{',
	'\tfor (i1s_p = 0; i1s_p <= 16; i1s_p += 1)',
	'\t{',
	'\t\ti1s_l = ClassPlayerObjs("Building", i1s_p).GetObjList();',
	'\t\tfor (i1s_i = 0; i1s_i < i1s_l.count; i1s_i += 1)',
	'\t\t{',
	'\t\t\ti1s_s = i1s_l[i1s_i].AsBuilding().settlement;',
	'\t\t\tif (i1s_s.IsValid())',
	'\t\t\t{',
	'\t\t\t\ti1s_m = i1s_s.GetMaxSentries();',
	'\t\t\t\tif (i1s_m != 0) i1s_s.AddMaxSentries(0 - i1s_m);',
	'\t\t\t\tfor (i1s_k = 0; i1s_k < 100; i1s_k += 1)',
	'\t\t\t\t{',
	'\t\t\t\t\tif (i1s_s.GetSentry() == 0) break;',
	'\t\t\t\t\ti1s_s.DelSentry();',
	'\t\t\t\t}',
	'\t\t\t}',
	'\t\t}',
	'\t\tClassPlayerObjs("Sentry", i1s_p).Erase();',
	'\t}',
	'\tSleep(2000);',
	'}',
	'',
].join(EOL);

// Adds a sequence to a map: a new seqN.vs and its entry in the map's sequences.xml. Returns the script's path.
function addMapSequence(out, mapId, name, script) {
	const dir = `Maps/${mapId}/Sequences/`;
	const keys = [...out.keys()];
	let xmlKey = keys.find((k) => lower(k) === lower(dir + 'sequences.xml'));
	if (!xmlKey) {
		xmlKey = dir + 'sequences.xml';
		out.set(xmlKey, bytes(`\t<sequences>${EOL}\t</sequences>${EOL}`));
	}
	const xml = text(out.get(xmlKey));
	let n = 0;
	for (const k of keys) {
		if (lower(k).startsWith(lower(dir))) {
			const m = /^seq(\d+)\.vs$/i.exec(k.slice(dir.length));
			if (m) n = Math.max(n, Number(m[1]) + 1);
		}
	}
	for (const m of xml.matchAll(/sequences\/seq(\d+)\.vs/gi)) n = Math.max(n, Number(m[1]) + 1);
	const file = `seq${n}.vs`;
	out.set(dir + file, bytes(script));
	const entry = `\t\t<sequence${EOL}\t\t\tname="${name}"${EOL}\t\t\twizard=""${EOL}\t\t\tscript="CurrentMap/sequences/${file}">${EOL}\t\t</sequence>${EOL}`;
	if (!/<\/sequences>/.test(xml)) throw new Error(`${xmlKey}: no </sequences>`);
	out.set(xmlKey, bytes(xml.replace(/([ \t]*<\/sequences>)/, `${entry}$1`)));
	return dir + file;
}

// Class display names of a game's translation table, by the context of the entry ("<class> class" in Imperivm I,
// "<class> class name" in Great Battles of Rome): Map lower-case class id -> translated name.
function classNameTable(buf, suffix) {
	const names = new Map();
	if (!buf) return names;
	const re = new RegExp(`\\scontext="([^"@]+) ${suffix}"`);
	for (const m of buf.toString('utf8').matchAll(/<translationtableentry\b[^>]*>/g)) {
		const c = re.exec(m[0]);
		const r = /\sresult="([^"]*)"/.exec(m[0]);
		if (c && r && r[1] && r[1] !== '(translate)' && !names.has(lower(c[1]))) names.set(lower(c[1]), r[1]);
	}
	return names;
}

// Text for an attribute of a translation table, whose values may already have entities (&amp;) in them.
const attrText = (s) => s.replace(/&(?![a-z]+;|#\d+;)/gi, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');

// Adds entries to a translation table (the text of the file); `entries`: [{text, result}], values already escaped.
function addEntries(src, entries) {
	if (!entries.length) return src;
	const lines = entries.map((e) => `<translationtableentry text="${e.text}" id="actor name" result="${e.result}"/>`).join('');
	if (/<translationtable\s*\/>/.test(src)) return src.replace(/<translationtable\s*\/>/, `<translationtable>${lines}</translationtable>`);
	return src.replace(/<\/translationtable>/, `${lines}</translationtable>`);
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
	// Map id -> name -> {num, i1: class, displayName}: the named objects themselves.
	const namedObjects = new Map();
	for (const [p, buf] of files) {
		const m = /^Maps\/([^/]+)\/map\.obj\.xml$/i.exec(p);
		if (!m) continue;
		const t = text(buf);
		const cls = new Map();
		const shown = new Map();
		for (const o of t.matchAll(/<scriptobj\s+class="([^"]*)"\s+num="(\d+)"([^>]*)>/g)) {
			cls.set(o[2], o[1]);
			const d = /\sdisplay_name="([^"]*)"/.exec(o[3]);
			if (d) shown.set(o[2], d[1]);
		}
		const named = new Map();
		const objs = new Map();
		for (const g of t.matchAll(/<group\s+name="([^"]*)"\s+type="0">([\s\S]*?)<\/group>/g)) {
			const nums = [...g[2].matchAll(/num="(\d+)"/g)];
			if (nums.length !== 1 || !cls.has(nums[0][1])) continue;
			const r = classes.resolve(cls.get(nums[0][1]));
			named.set(g[1], r.to || cls.get(nums[0][1]));
			objs.set(g[1], { num: nums[0][1], i1: cls.get(nums[0][1]), displayName: shown.get(nums[0][1]) || null });
		}
		namedByMap.set(m[1], named);
		namedObjects.set(m[1], objs);
	}
	// Groups spawned into a settlement of another player (see M.HOLDER_SPAWNS).
	const settlementOwner = new Map();
	const groupPlayers = new Map();
	for (const [p, buf] of files) {
		const m = /^Maps\/([^/]+)\/map\.obj\.xml$/i.exec(p);
		if (!m) continue;
		const t = text(buf);
		const owner = new Map();
		for (const o of t.matchAll(/<scriptobj\s+class="[^"]*"\s+num="(\d+)"([^>]*)>/g)) owner.set(o[1], (/\splayer="(\d+)"/.exec(o[2]) || [])[1]);
		for (const s of t.matchAll(/<settlement\b([^>]*)>/g)) {
			const n = /\sname="([^"]+)"/.exec(s[1]);
			if (n) settlementOwner.set(`${m[1]}/${n[1]}`, (/\splayer="(\d+)"/.exec(s[1]) || [])[1]);
		}
		for (const g of t.matchAll(/<group\s+name="([^"]*)"\s+type="\d+">([\s\S]*?)<\/group>/g)) {
			groupPlayers.set(`${m[1]}/${g[1]}`, [...new Set([...g[2].matchAll(/num="(\d+)"/g)].map((n) => owner.get(n[1])).filter(Boolean))]);
		}
	}
	const holderSpawns = new Map();
	for (const [p, buf] of files) {
		const m = /^Maps\/([^/]+)\/Sequences\/([^/]+\.vs)$/i.exec(p);
		if (!m || !referenced.has(lower(p))) continue;
		for (const c of text(buf).matchAll(/SpawnGroupInHolder\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)/g)) {
			const key = `${m[1]}/${c[1]}`;
			const players = groupPlayers.get(key);
			const sett = settlementOwner.get(`${m[1]}/${c[2]}`);
			if (!players || !sett || players.every((x) => x === sett)) continue;
			if (!holderSpawns.has(key)) holderSpawns.set(key, { map: m[1], group: c[1], settlement: c[2], sett, players, files: new Set() });
			holderSpawns.get(key).files.add(m[2]);
		}
	}
	for (const [key, s] of holderSpawns) {
		const what = `map ${s.map}: ${s.group} (player ${s.players.join('/')}) into ${s.settlement} (player ${s.sett})`;
		if (M.HOLDER_SPAWNS[key]) report.add('Groups spawned into another player\'s settlement', `${what}: changed, ${M.HOLDER_SPAWNS[key].why}`);
		else if (M.HOLDER_SPAWNS_KEPT[key]) report.add('Groups spawned into another player\'s settlement', `${what}: kept, ${M.HOLDER_SPAWNS_KEPT[key]}`);
		else report.add('Check warnings', `${what}: not reviewed; Great Battles of Rome's outpost guard may order them out`);
	}
	ctx.holderRules = (p) => {
		const m = /^Maps\/([^/]+)\/Sequences\/([^/]+\.vs)$/i.exec(p);
		if (!m) return [];
		return Object.entries(M.HOLDER_SPAWNS)
			.filter(([key]) => key.split('/')[0] === m[1])
			.map(([key, rule]) => ({ group: key.split('/')[1], rule, file: m[2], spawnsHere: holderSpawns.has(key) && holderSpawns.get(key).files.has(m[2]) }));
	};

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
			let s = convertScript(text(buf), ctx, p);
			for (const { group, rule, file, spawnsHere } of ctx.holderRules(p)) {
				if (spawnsHere) {
					const r = S.ownHolderSpawns(s, group, rule.owner);
					s = r.src;
					report.count('Groups spawned into another player\'s settlement', `${p}: ${group} given to player ${rule.owner} as it spawns`, r.count);
				}
				for (const [backFile, anchor, where, player] of rule.back) {
					if (lower(backFile) !== lower(file)) continue;
					const next = S.insertAtLine(s, anchor, where, `Group("${group}").SetPlayer(${player});`);
					if (next === null) throw new Error(`${p}: the place to hand ${group} back was not found (${anchor})`);
					s = next;
					report.count('Groups spawned into another player\'s settlement', `${p}: ${group} set to player ${player} ${where} ${anchor.source.replace(/^\^\\s\*/, '').replace(/\\/g, '')}`);
				}
			}
			const probed = input.probe ? addProbe(p, s) : null;
			if (probed) {
				s = probed;
				report.add('Probe', `${p}: I1PROBE diagnostics added (Mod_Log, needs Imperivm Resurrected)`);
			}
			// After the probe, whose anchors are the original Run() lines.
			const q = S.queueConversations(s);
			s = q.src;
			if (q.count) report.count('Conversations', 'Conversation.Run() calls made to wait for the running one, as in Imperivm I', q.count);
			out.set(p, bytes(s));
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

	// Sequences every map runs: no sentries, and with --probe the census of what is in the world.
	for (const id of mapNames.keys()) {
		addMapSequence(out, id, 'I1_NoSentries', NO_SENTRIES_SCRIPT);
		report.count('Sentries', 'maps given the I1_NoSentries sequence (no sentries, as in Imperivm I)');
		if (input.probe) {
			addMapSequence(out, id, 'I1_Census', censusSequence());
			report.count('Probe', 'maps given the I1_Census sequence (I1PROBE census lines)');
		}
	}

	const actorRows = conversationActors({ files, out, gbr, i1, classes, namedObjects, report });
	// Every adventure of Great Battles of Rome has these two folders, empty. Without "Conversations" the game cannot load
	// CurrentGame/conversations when a map starts ("Empty or invalid directory specified for CVXConvRegistry::Load" in
	// vxlog.log), and the code that loads the map's conversations throws when that load fails.
	const dirs = ['Conversations', 'Resources'];
	report.add('Adventure', 'empty Conversations and Resources folders, as in Great Battles of Rome\'s adventures');
	return { files: out, dirs, report, mapNames, customItems, terrain, decors, classes, actorRows };
}

// The actors of the conversations. A phrase's title is the actor's name looked up in the translation tables, and its
// picture the actor unit's Icon or else its class icon (CVXConversationWnd::ShowPhrase, gbr.exe 0050BBA3-0050BC8B and
// 0050BD72-0050BDAC; with no unit, no picture). Imperivm I titles a unit actor with its class name ("Jinete teutón"),
// so each actor the adventure does not translate gets an entry with its class name in the map's conversation table, and
// one whose class has no icon in Great Battles of Rome gets an Icon on its object. Returns rows for the audit:
// [map, conversation file, actor, I1 class, GBR class, Spanish title, picture].
function conversationActors({ files, out, gbr, i1, classes, namedObjects, report }) {
	const gbrById = new Map();
	for (const c of gbr.classes.values()) for (const id of [c.id, c.altid].filter(Boolean)) if (!gbrById.has(lower(id))) gbrById.set(lower(id), c);
	const i1ById = new Map();
	for (const c of i1.classes.values()) for (const id of [c.id, c.altid].filter(Boolean)) if (!i1ById.has(lower(id))) i1ById.set(lower(id), c);
	// The first value fn gives for the class or one of its parents.
	const inherited = (byId, id, fn) => {
		for (let c = byId.get(lower(id || '')), n = 0; c && n < 32; c = c.parent && byId.get(lower(c.parent)), n++) {
			const v = fn(c);
			if (v) return v;
		}
		return null;
	};
	const inheritedIcon = (byId, id) => inherited(byId, id, (c) => c.icon);
	const merge = (...maps) => new Map(maps.flatMap((m) => [...m].reverse()).reverse());
	const i1Es = merge(classNameTable(i1.local.read('SPANISH/TRANSLATION.LOC.XML'), 'class'), classNameTable(i1.local.read('SPANISH/GAME.LOC.XML'), 'class'));
	const gbrName = (lang) => merge(classNameTable(gbr.readLocal(lang, 'CURRENTLANG/TRANSLATION.LOC.XML'), 'class name'), classNameTable(gbr.readLocal(lang, 'CURRENTLANG/GAME.LOC.XML'), 'class name'));
	const gbrEs = gbrName('spanish');
	const gbrIt = gbrName('italian');
	// Texts of the game's own Spanish table ("Peasant", "Guard"...), for actors whose unit is only known at run time.
	const gameTexts = new Map();
	const gameEs = gbr.readLocal('spanish', 'CURRENTLANG/TRANSLATION.LOC.XML');
	if (gameEs) {
		for (const m of gameEs.toString('utf8').matchAll(/<translationtableentry\s+text="([^"@]*)"[^>]*?\sresult="([^"]*)"/g)) if (!gameTexts.has(m[1])) gameTexts.set(m[1], m[2]);
	}
	// Actor names the adventure's table translates. Imperivm I's table lists every actor ("NO_Teuton5" -> "NO_Teuton5"):
	// an entry that gives back the name itself is untranslated, and the converter gives it the class name.
	const advTexts = new Map();
	for (const [k, buf] of out) {
		if (!/^Local\/Spanish\/adventure\.loc\.xml$/i.test(k)) continue;
		for (const m of buf.toString('utf8').matchAll(/<translationtableentry\b[^>]*>/g)) {
			const t = /\stext="([^"]*)"/.exec(m[0]);
			const r = /\sresult="([^"]*)"/.exec(m[0]);
			if (t && !advTexts.has(t[1])) advTexts.set(t[1], r ? r[1] : '');
		}
	}
	// The Spanish table sometimes "translates" the name itself (NO_Larax -> NO_Lárax): still an object's name, not a title.
	const translated = (actor) => advTexts.has(actor) && advTexts.get(actor) !== actor && !/^NO_/i.test(advTexts.get(actor));
	// actor -> Spanish title -> {English, Italian, maps}: class names for the untranslated entries of the adventure.
	const advTitles = new Map();

	const rows = [];
	const mapIds = [...namedObjects.keys()].sort((a, b) => Number(a) - Number(b));
	// Heroes travel with the party: a map can talk about "Larax" without having him in its file.
	const anyMap = new Map();
	for (const id of mapIds) for (const [n, o] of namedObjects.get(id)) if (!anyMap.has(n)) anyMap.set(n, o);
	for (const id of mapIds) {
		const named = namedObjects.get(id);
		// SetActor("name", expr) and SetDefActor("name", "picture") of the map's scripts.
		const exprs = new Map();
		const runtime = new Set();
		const defaults = new Map();
		for (const [p, buf] of files) {
			if (!lower(p).startsWith(lower(`Maps/${id}/Sequences/`)) || !/\.vs$/i.test(p)) continue;
			const src = text(buf);
			const vars = new Map([...src.matchAll(/(\w+)\s*=\s*GetNamedObj\s*\(\s*"([^"]+)"\s*\)/g)].map((m) => [m[1], m[2]]));
			for (const m of src.matchAll(/\.SetActor\s*\(\s*"([^"]+)"\s*,\s*([^;]+?)\s*\)\s*;/g)) {
				const base = /^\(*\s*([A-Za-z_]\w*)/.exec(m[2]);
				const obj = /^GetNamedObj\s*\(\s*"([^"]+)"/.exec(m[2]);
				const known = (n) => named.has(n) || anyMap.has(n);
				const name = obj ? obj[1] : base && (known(base[1]) ? base[1] : vars.get(base[1]));
				if (name && known(name)) {
					if (!exprs.has(m[1])) exprs.set(m[1], name);
				} else runtime.add(m[1]);
			}
			for (const m of src.matchAll(/\.SetDefActor\s*\(\s*"([^"]+)"\s*,\s*"([^"]*)"\s*\)/g)) defaults.set(m[1], m[2]);
		}
		const titled = new Set();
		const convFiles = [...files.keys()].filter((k) => lower(k).startsWith(lower(`Maps/${id}/Conversations/`)) && /\.conv\.xml$/i.test(k)).sort();
		const mapObj = [...out.keys()].find((k) => lower(k) === lower(`Maps/${id}/map.obj.xml`));
		for (const cf of convFiles) {
			const file = cf.split('/').pop();
			const actors = [...new Set([...text(files.get(cf)).matchAll(/<actor\s+name="([^"]*)"/g)].map((m) => m[1]))];
			const entries = { Spanish: [], English: [], Italian: [] };
			for (const actor of actors) {
				const objName = named.has(actor) || anyMap.has(actor) ? actor : exprs.get(actor);
				const local = objName && named.has(objName);
				const obj = objName ? named.get(objName) || anyMap.get(objName) : null;
				if (!obj) {
					const def = defaults.get(actor);
					const title = translated(actor) ? `"${advTexts.get(actor)}" (adventure table)` : gameTexts.has(actor) ? `"${gameTexts.get(actor)}" (game table)` : `${actor} (untranslated)`;
					const picture = def !== undefined ? `SetDefActor picture ${def}` : runtime.has(actor) ? 'class icon of the unit the script picks at run time' : 'NONE: no unit found for this actor';
					rows.push([id, file, actor, runtime.has(actor) ? '(run time)' : '-', '-', title, picture]);
					if (def === undefined && !runtime.has(actor)) report.add('Conversation actors without a picture', `map ${id} ${file}: ${actor} (no named object or SetActor found)`);
					continue;
				}
				const gbrClass = classes.resolve(obj.i1).to || obj.i1;
				// The class's name, or its parent's (GVillager -> Peasant); a hero's own name as the adventure translates it.
				const i1Name = obj.displayName || inherited(i1ById, obj.i1, (c) => c.displayName) || inherited(gbrById, gbrClass, (c) => c.displayName) || actor;
				const ownName = (n) => (translated(n) ? advTexts.get(n) : null);
				let es = translated(actor)
					? null
					: ownName(i1Name) || (obj.displayName ? obj.displayName : inherited(i1ById, obj.i1, (c) => i1Es.get(lower(c.id))) || inherited(gbrById, gbrClass, (c) => gbrEs.get(lower(c.id))) || i1Name);
				const it = obj.displayName || inherited(gbrById, gbrClass, (c) => gbrIt.get(lower(c.id))) || i1Name;
				if (es !== null && advTexts.has(actor)) {
					if (!advTitles.has(actor)) advTitles.set(actor, new Map());
					const t = advTitles.get(actor);
					if (!t.has(es)) t.set(es, { English: i1Name, Italian: it, maps: new Set() });
					t.get(es).maps.add(id);
				} else if (es !== null && !titled.has(actor)) {
					titled.add(actor);
					entries.Spanish.push({ text: attrText(actor), result: attrText(es) });
					entries.English.push({ text: attrText(actor), result: attrText(i1Name) });
					entries.Italian.push({ text: attrText(actor), result: attrText(it) });
					report.count('Conversation actors', "actors titled with their class name in the map's table, as in Imperivm I");
				}
				if (es === null) es = `"${advTexts.get(actor)}" (adventure table)`;
				// The picture: the class icon, or else Imperivm I's icon of the class if Great Battles of Rome has it.
				let picture;
				const icon = inheritedIcon(gbrById, gbrClass);
				if (icon && gbr.hasGameres(icon)) picture = `class icon ${icon}`;
				else {
					const i1Icon = inheritedIcon(i1ById, obj.i1);
					const alt = i1Icon && 'gameres/icons/' + i1Icon.split('/').pop();
					if (alt && gbr.hasGameres(alt) && mapObj && local) {
						const t = text(out.get(mapObj));
						const re = new RegExp(`(<scriptobj\\s+class="[^"]*"\\s+num="${obj.num}"[^>]*?)(\\s+player=")`);
						if (re.test(t) && !new RegExp(`num="${obj.num}"[^>]*?\\sIcon="`).test(t)) out.set(mapObj, bytes(t.replace(re, `$1${EOL}\tIcon="${alt}"$2`)));
						picture = `Icon ${alt} (the class has none)`;
						report.count('Conversation actors', 'actors given an Icon (their class has no icon)');
					} else {
						picture = 'NONE: the class has no icon';
						report.add('Conversation actors without a picture', `map ${id} ${file}: ${actor} (${gbrClass})`);
					}
				}
				rows.push([id, file, actor, obj.i1, gbrClass, es, picture]);
			}
			for (const lang of LANGS) {
				if (!entries[lang].length) continue;
				const key = [...out.keys()].find((k) => lower(k) === lower(`Local/${lang}/Maps/${id}/conversations/${file}`));
				if (!key) {
					report.add('Check warnings', `Local/${lang}/Maps/${id}/conversations/${file} missing: actor names of ${file} not translated`);
					continue;
				}
				out.set(key, Buffer.from(addEntries(out.get(key).toString('utf8'), entries[lang]), 'utf8'));
			}
		}
	}
	// The untranslated entries of the adventure's table get the class name; an actor name that stands for units of
	// different classes in different maps gets the most common one.
	for (const [actor, titles] of advTitles) {
		const [es, pick] = [...titles].sort((a, b) => b[1].maps.size - a[1].maps.size)[0];
		if (titles.size > 1) {
			const others = [...titles].filter(([e]) => e !== es).map(([e, v]) => `"${e}" in maps ${[...v.maps].join(', ')}`);
			report.add('Conversation actors', `${actor}: titled "${es}" (maps ${[...pick.maps].join(', ')}); also stands for ${others.join('; ')}`);
		}
		for (const [lang, value] of [['Spanish', es], ['English', pick.English], ['Italian', pick.Italian]]) {
			const key = [...out.keys()].find((k) => lower(k) === lower(`Local/${lang}/adventure.loc.xml`));
			if (!key) continue;
			const next = out.get(key).toString('utf8').replace(/<translationtableentry\b[^>]*>/g, (e) => {
				const t = /\stext="([^"]*)"/.exec(e);
				if (!t || t[1] !== actor) return e;
				if (/\sresult="/.test(e)) return e.replace(/\sresult="[^"]*"/, ` result="${attrText(value)}"`);
				return e.replace(/\/?>$/, (end) => ` result="${attrText(value)}"${end}`);
			});
			out.set(key, Buffer.from(next, 'utf8'));
		}
		report.count('Conversation actors', "actor names of the adventure's table titled with their class name, as in Imperivm I");
	}
	for (const r of rows) {
		const t = advTitles.get(r[2]);
		if (t && !/adventure table/.test(r[5])) r[5] = `"${[...t].sort((a, b) => b[1].maps.size - a[1].maps.size)[0][0]}" (adventure table)`;
	}
	return rows;
}

module.exports = {
	convertAdventure,
	Report,
	terrainRemap,
	retranslate,
	cleanTable,
	convertNotes,
	convertMapXml,
	convertItems,
	convertObjects,
	addMapSequence,
	classNameTable,
	addEntries,
	NO_SENTRIES_SCRIPT,
};
