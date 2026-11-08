// Audit of a conversion (convert.js --audit): proves, without playing, what the converted adventure keeps of Imperivm I's
// and what Great Battles of Rome reads of it. Four tables:
//
//   A. every element and attribute of the adventure's XML files: carried over or not, and whether Great Battles of Rome
//      reads it (used by its own adventures, or at least its name is a string of gbr.exe); and the other files.
//   B. diplomacy: the relations of the player files, the Dipl* calls of each map, and the gates with their owners.
//   C. the script API: every function, method and command verb the scripts use, checked against Great Battles of Rome
//      (exists, takes that many arguments, and for commands whether the script that runs them is the same).
//   D. classes: per map and class, the objects of the original and of the conversion, the class used, whether it
//      exists, and the behaviours Great Battles of Rome gives a class that Imperivm I did not.
//
// Anything that would change what happens in the game and is not explained in KNOWN below is an error. compareCensus
// reads the "I1PROBE census" lines of a --probe build's log and compares what was in the world with the map.
'use strict';
const X = require('./xml');
const S = require('./script');
const M = require('./mapping');

const lower = (s) => s.toLowerCase();
const text = (buf) => buf.toString('latin1');

// Explanations for what the audit would otherwise flag, keyed "file kind|element|attribute" (A), "function|Name",
// "method|Name" or "verb|name" (C). An explained entry is listed as handled, not as an error.
const KNOWN = {
	'game.xml|properties|adventure': 'removed: Great Battles of Rome finds the adventure by its file; its adventures have no such attribute',
	'game.xml|properties|game_type': 'added: Great Battles of Rome\'s adventures set it (1 = adventure)',
	'game.xml|properties|user_interface': 'added: Great Battles of Rome\'s adventures set it (5)',
	'map.xml|map|persist_state': 'added: Great Battles of Rome keeps a map\'s state when the adventure comes back to it only with persist_state="1"',
	'map.obj.xml|scriptobj|stamina': 'added: every unit and building of Great Battles of Rome\'s maps has one (10 units, 20 buildings)',
	'map.obj.xml|scriptobj|display_name': 'added to substituted heroes so they keep their Imperivm I name',
	'map.obj.xml|settlement|extrasentries': 'added as 0: Imperivm I has no sentries (see the no-sentries sequence)',
	'map.obj.xml|scriptobj|Icon': 'added to the objects that speak in conversations: their class portrait',
	'notes.xml|note|title': 'added: Great Battles of Rome shows the note by its title',
	'notes.xml|note|show_on_minimap': 'added: 1 when the note has a place on the map, as in Great Battles of Rome\'s adventures',
	'playerN.xml|playerdata|control': 'value "Both" written "Computer": every adventure of Great Battles of Rome does so and gives the human its player with SetPlayer',
	'playerN.xml|playerdata|race': 'value "Roman" written "RepublicanRome"',
	'labels.xml|root|': 'added: every map of Great Battles of Rome\'s adventures has a labels.xml',
	'sequences.xml|sequence|': 'sequences of the adventure, plus the ones the converter adds',
	'sequences.xml|sequence|name': 'sequences of the adventure, plus the ones the converter adds',
	'sequences.xml|sequence|wizard': 'sequences of the adventure, plus the ones the converter adds',
	'sequences.xml|sequence|script': 'sequences of the adventure, plus the ones the converter adds',
	'itemsCustom.xml|item|': 'items Imperivm I has and Great Battles of Rome lacks are added to the adventure\'s own',
	'conv.xml|conversation|': 'conversations are copied as they are',
	'itemsCustom.xml|bonus|attack': 'renamed: damage = the average of attack and maxattack',
	'itemsCustom.xml|bonus|maxattack': 'renamed: damage = the average of attack and maxattack',
	'itemsCustom.xml|bonus|defense_slash': 'renamed armor_slash',
	'itemsCustom.xml|bonus|defense_pierce': 'renamed armor_pierce',
	'itemsCustom.xml|item|usescript': 'renamed use_script (the script converted like the maps\' scripts)',
	'itemsCustom.xml|item|scripttarget': 'dropped: always "None" in the campaign, and Great Battles of Rome\'s items have no such attribute',
	'map.obj.xml|scriptobj|player': 'dropped from decors only (rocks, the Roman arch), as Great Battles of Rome\'s maps write them',
	'map.obj.xml|scriptobj|UnitFlags': 'low 16 bits cleared: Imperivm I\'s loader ignores them (Imperivm.exe 0050E50B), Great Battles of Rome would read 0x1 as blessed',
	'function|WaitEmptyQuery': 'reviewed: GBR counts a dead unit until its body goes (6-8 s in Kormaris), Imperivm I goes on at its death; written as a loop that counts living units (IsAlive)',
	'function|WaitQueryCountBetween': 'reviewed: GBR counts a dead unit until its body goes; written as a loop that counts living units (IsAlive)',
	'method|ClearDead': 'reviewed: in GBR it drops only objects that no longer exist (ObjList::ClearDead 0055F430), not dead units whose bodies remain',
	'method|AddBonus': 'added: soldiers get the damage, armor and maxhealth fitted to Imperivm I\'s hits (I1_Stats; the formulas are in adventure.statRules)',
	'method|SetSpeedFactor': 'added: soldiers move at Imperivm I\'s speed; the factor also marks a unit as done',
	'method|SetSight':'added: units get Imperivm I\'s sight, the radius in which they pick a target on their own',
	'verb|hide': 'Gaul druids cannot hide in Great Battles of Rome: the calls are written SetVisible(false) on the target',
	'verb|mist': 'Gaul druids cannot cast mist in Great Battles of Rome: the calls place the Mist object',
};

// What changes in the scripts that run commands, read by comparing both games' data/subai files ("GBR script": notes).
const SCRIPT_NOTES = {
	'data/subai/melee_attack.vs': 'returns at once when the target is not an enemy of another player (Imperivm I attacked it anyway); a hidden attacker stays hidden; takes an object only ("attack" with a point is written "advance")',
	'data/subai/unit_advance.vs': 'animals just move; skips targets that are not valid; engages units with attack > 0 (Imperivm I: maxattack > 0); computer units leave parry mode',
	'data/subai/unit_move.vs': 'units on another player\'s ship unboard first; computer units leave parry mode',
	'data/subai/unit_idle.vs': 'engages enemies with attack > 0, druids and wagons (Imperivm I: maxattack > 0); units in a settlement with autotrain on stay inside; computer units leave the coast',
	'data/subai/unit_hold_position.vs': 'computer units leave parry mode; a hidden attacker stays hidden',
	'data/subai/unit_stand_position.vs': 'stands at posRH; gives up on a target after 15 s (Imperivm I: never); stops when another command is queued',
	'data/subai/unit_engage.vs': 'gives up on a target after 15 s (Imperivm I: never); a hidden attacker stays hidden',
	'data/subai/unit_enter.vs': 'same checks, each followed by a short pause; a unit can enter a catapult with its hero',
	'data/subai/unit_attach.vs': 'heroes can attach to another hero; wagons, ships and peasants cannot; uses teleports to reach the hero; multiplayer-only race limits',
	'data/subai/unit_approach.vs': 'keeps trying while the target lives, 20 s per try (Imperivm I: one try without limit)',
	'data/subai/unit_standstill.vs': 'same wait, written as a loop on IsAlive',
	'data/subai/unit_protect.vs': 'keeps the offset from posRH instead of pos',
	'data/subai/unit_capture.vs': 'checks the owner first; walks to independent outposts and Teuton tents before capturing',
	'data/subai/hero_patrol.vs': 'patrols from posRH; stops when there is no path; gives up on a target after 15 s',
	'data/subai/hero_leave_army.vs': 'the army is told "idle" first',
};

// Files other than XML: what Great Battles of Rome does with them.
const KNOWN_FILES = {
	'.grid': 'map grids: terrain widened to 8 bits and remapped, decor renumbered, the others copied',
	'.vs': 'scripts, rewritten (see C)',
	'.rle': 'warehouse.rle of each map, copied as it is',
	'.42': 'env.42, the adventure\'s environment variables, copied as it is',
	'.bmp': 'pictures (global_map.bmp is also the adventure\'s preview)',
	'.wav': 'voices, under Local/Spanish and Local/English',
	'.xml - empty': '"sequences.xml - empty": copies the map editor left behind; neither game reads them',
};

// Bits of a map object's `flags` (gbr.exe, see imperivm-re P22/P19): what each one means in both games.
const FLAG_BITS = [
	[0x0000ffff, 'visibility per player (bit p-1)'],
	[0x00080000, 'party member (heroes and their companions)'],
	[0x00100000, 'unknown: only on Teuton rider templates; no code of either exe tests it directly'],
	[0x00200000, 'visible only once seen (fog)'],
	[0x00400000, 'unit'],
	[0x00800000, 'building'],
	[0x01000000, 'hero'],
	[0x04000000, 'never attacker nor target'],
	[0x08000000, 'template of a group (SpawnGroup), not in the world'],
	[0x20000000, 'set on heroes by the editor'],
	[0x80000000, 'set on every object by the editor'],
];

// Bits of a unit's UnitFlags in Great Battles of Rome (unit +0x194; imperivm-re P23, and the tests found in gbr.exe).
const UNIT_FLAG_BITS = {
	0x1: 'blessed (tested at 00423328, 0051B329...); in Imperivm I a run-time state the loader drops',
	0x2: 'warning effect; as a target, +200 to its score (005DB6D7); Imperivm I uses it in combat (004FCEE3), not from the map',
	0x4: 'cleared at 0052F7CD; no test found',
	0x8: 'no code of gbr.exe tests it directly',
	0x20: 'Imperivm I uses it in combat (004FCEA4), not from the map',
	0x10000: 'moving along a path',
	0x20000: 'wounded (repairable); the default of a unit without UnitFlags',
	0x40000: 'set on every unit of both games\' maps',
};

function kindOf(p) {
	if (/^local\//i.test(p)) return null;
	const b = lower(p.split('/').pop());
	if (b === 'map.obj.xml') return 'map.obj.xml';
	if (b === 'map.xml') return 'map.xml';
	if (/^player\d+\.xml$/.test(b)) return 'playerN.xml';
	if (b === 'game.xml') return 'game.xml';
	if (b === 'sequences.xml') return 'sequences.xml';
	if (b === 'notes.xml') return 'notes.xml';
	if (/\.conv\.xml$/.test(b)) return 'conv.xml';
	if (b === 'itemscustom.xml') return 'itemsCustom.xml';
	if (b === 'labels.xml') return 'labels.xml';
	if (/\.xml$/.test(b)) return b;
	return null;
}

// Strings of an executable: every run of 2+ printable ASCII characters ended by a NUL. {exact: Set, lower: Set}.
function exeStrings(buf) {
	const exact = new Set();
	let from = -1;
	for (let i = 0; i < buf.length; i++) {
		const c = buf[i];
		if (c >= 0x20 && c < 0x7f) {
			if (from < 0) from = i;
		} else {
			if (c === 0 && from >= 0 && i - from >= 2) exact.add(buf.toString('latin1', from, i));
			from = -1;
		}
	}
	return { exact, lower: new Set([...exact].map(lower)) };
}

// Elements and attributes of a set of files: Map "kind|element|attr" -> {count, values: Map}. attr '' is the element.
function inventory(files) {
	const inv = new Map();
	const add = (key, value) => {
		if (!inv.has(key)) inv.set(key, { count: 0, values: new Map() });
		const e = inv.get(key);
		e.count++;
		if (value !== undefined && (e.values.size < 64 || e.values.has(value))) e.values.set(value, (e.values.get(value) || 0) + 1);
	};
	const others = new Map();
	for (const [p, buf] of files) {
		const kind = kindOf(p);
		if (!kind) {
			if (/^local\//i.test(p)) continue;
			const ext = (/\.[^./]+$/.exec(p) || ['(none)'])[0].toLowerCase();
			others.set(ext, (others.get(ext) || 0) + 1);
			continue;
		}
		let doc;
		try {
			doc = X.parseXml(text(buf));
		} catch {
			continue;
		}
		for (const r of doc.roots) {
			for (const el of X.walk(r)) {
				add(`${kind}|${el.name}|`);
				for (const [k, v] of el.attrs) add(`${kind}|${el.name}|${k}`, v);
			}
		}
	}
	return { inv, others };
}

// Same key in any case, for evidence from the other game.
function findCi(inv, key) {
	if (inv.has(key)) return inv.get(key);
	const k = lower(key);
	for (const [x, v] of inv) if (lower(x) === k) return v;
	return null;
}

function tableA(i1Files, outFiles, refFiles, gbrExe, i1Exe, errors, warnings) {
	const a = inventory(i1Files);
	const b = inventory(outFiles);
	const r = inventory(refFiles);
	const rows = [];
	const keys = [...new Set([...a.inv.keys(), ...b.inv.keys()])].sort();
	for (const key of keys) {
		const [kind, el, attr] = key.split('|');
		const i1n = a.inv.has(key) ? a.inv.get(key).count : 0;
		const outn = b.inv.has(key) ? b.inv.get(key).count : 0;
		const ref = findCi(r.inv, key);
		const name = attr || el;
		const inGbr = gbrExe.exact.has(name) ? 'yes' : gbrExe.lower.has(lower(name)) ? 'yes (other case)' : 'no';
		const inI1 = i1Exe.exact.has(name) ? 'yes' : i1Exe.lower.has(lower(name)) ? 'yes (other case)' : 'no';
		const carried = i1n === 0 ? 'added' : outn === 0 ? 'dropped' : outn === i1n ? 'kept' : `${i1n} -> ${outn}`;
		const reads = ref ? 'yes: GBR adventures use it' : inGbr !== 'no' ? 'name in gbr.exe' : 'no';
		let status = 'ok';
		const note = KNOWN[key] || '';
		if (outn > 0 && reads === 'no') {
			if (inI1 === 'no') status = 'ignored by both games';
			else if (note) status = 'handled';
			else {
				status = 'ERROR';
				errors.push(`A: ${key}: Imperivm I reads it, Great Battles of Rome does not`);
			}
		}
		if (carried === 'dropped' && inI1 !== 'no') {
			if (note) status = 'handled';
			else {
				status = 'ERROR';
				errors.push(`A: ${key}: dropped by the converter, and Imperivm I reads it`);
			}
		}
		if (carried === 'added' && !note && reads === 'no') {
			status = 'ERROR';
			errors.push(`A: ${key}: added by the converter, Great Battles of Rome does not read it`);
		}
		// Values Great Battles of Rome's adventures never use, for attributes with a short list of values.
		let values = '';
		// Keywords: every value Great Battles of Rome's adventures write is a short word; a converted value that they never
		// write and that is not a string of gbr.exe either is probably a keyword gbr.exe does not know.
		const word = (v) => /^[A-Za-z_][A-Za-z_ ]{0,30}$/.test(v);
		if (attr && ref && ref.values.size <= 16 && b.inv.has(key)) {
			const known = new Set([...ref.values.keys()].map(lower));
			const enumLike = [...ref.values.keys()].every(word);
			const odd = [...b.inv.get(key).values.keys()].filter((v) => word(v) && !known.has(lower(v)) && !gbrExe.lower.has(lower(v)));
			if (enumLike && odd.length) {
				values = `values GBR never uses: ${odd.slice(0, 8).map((v) => `"${v}"`).join(', ')}${odd.length > 8 ? '...' : ''}`;
				if (!note && status === 'ok') {
					status = 'check';
					warnings.push(`A: ${key}: ${values}`);
				}
			}
		}
		rows.push([kind, el, attr || '(element)', String(i1n), String(outn), carried, reads, inI1, status, [note, values].filter(Boolean).join('; ')]);
	}
	const files = [];
	for (const ext of [...new Set([...a.others.keys(), ...b.others.keys()])].sort()) {
		const note = KNOWN_FILES[ext] || '';
		const st = r.others.has(ext) ? 'GBR adventures have them' : 'not in GBR adventures';
		if (!note && !r.others.has(ext)) errors.push(`A: files ${ext}: not in Great Battles of Rome's adventures and not explained`);
		files.push([ext, String(a.others.get(ext) || 0), String(b.others.get(ext) || 0), st, note]);
	}
	return { rows, files };
}

// Objects of a map.obj.xml: [{num, cls, player, flags, unitFlags, inSettlement, attrs}].
function mapObjects(buf) {
	const doc = X.parseXml(text(buf));
	const out = [];
	const visit = (el, inSett) => {
		if (el.name === 'scriptobj') {
			out.push({
				num: el.attrs.get('num'),
				cls: el.attrs.get('class'),
				player: el.attrs.get('player'),
				flags: parseInt(el.attrs.get('flags') || '0', 16) >>> 0,
				unitFlags: Number(el.attrs.get('UnitFlags') || 0) >>> 0,
				inSettlement: inSett,
				attrs: el.attrs,
			});
		}
		for (const c of el.children) visit(c, inSett || el.name === 'settlement');
	};
	for (const r of doc.roots) visit(r, false);
	return { doc, objects: out };
}

function classInfo(classes, id) {
	if (!id) return null;
	for (const c of classes.values()) if (lower(c.id) === lower(id) || (c.altid && lower(c.altid) === lower(id))) return c;
	return null;
}

// Walks a class and its parents: the first value fn returns that is not undefined.
function inherited(classes, id, fn) {
	for (let c = classInfo(classes, id), n = 0; c && n < 32; c = c.parent && classInfo(classes, c.parent), n++) {
		const v = fn(c);
		if (v !== undefined) return v;
	}
	return undefined;
}

const behaviorsOf = (classes, id) => {
	const all = [];
	for (let c = classInfo(classes, id), n = 0; c && n < 32; c = c.parent && classInfo(classes, c.parent), n++) all.push(...c.behaviors);
	return all.map(lower);
};

function tableD(i1Files, outFiles, ctx, errors, warnings) {
	const rows = [];
	const bitsRows = [];
	const removed = new Map(Object.entries(ctx.removedClasses || {}));
	const flagBits = { i1: 0, out: 0 };
	// UnitFlags bits: how many objects have each one, in the original and in the conversion.
	const unitBits = { i1: new Map(), out: new Map() };
	const countBits = (m, v) => {
		for (let b = 0; b < 32; b++) if ((v >>> b) & 1) m.set(b, (m.get(b) || 0) + 1);
	};
	const behaviors = new Map();
	for (const [p, buf] of i1Files) {
		const m = /^Maps\/([^/]+)\/map\.obj\.xml$/i.exec(p);
		if (!m) continue;
		const outKey = [...outFiles.keys()].find((k) => lower(k) === lower(p));
		const parsed = mapObjects(buf);
		const a = parsed.objects;
		const b = outKey ? mapObjects(outFiles.get(outKey)).objects : [];
		// Objects that belong to a group (named objects included): the ones scripts can reach.
		const grouped = new Set();
		for (const r of parsed.doc.roots) for (const el of X.walk(r)) if (el.name === 'group') for (const c of el.children) grouped.add(c.attrs.get('num'));
		const scriptedClasses = new Set(a.filter((o) => grouped.has(o.num)).map((o) => o.cls));
		const byNum = new Map(b.map((o) => [o.num, o]));
		const groups = new Map();
		for (const o of a) {
			const conv = byNum.get(o.num);
			const key = `${o.cls}`;
			if (!groups.has(key)) groups.set(key, { cls: o.cls, to: new Set(), placed: 0, tmpl: 0, outPlaced: 0, outTmpl: 0, players: new Set() });
			const g = groups.get(key);
			const tmpl = (o.flags & 0x08000000) !== 0;
			if (tmpl) g.tmpl++;
			else g.placed++;
			g.players.add(o.player);
			flagBits.i1 |= o.flags;
			if (o.attrs.has('UnitFlags')) countBits(unitBits.i1, o.unitFlags);
			if (conv) {
				g.to.add(conv.cls);
				if ((conv.flags & 0x08000000) !== 0) g.outTmpl++;
				else g.outPlaced++;
				flagBits.out |= conv.flags;
				if (conv.attrs.has('UnitFlags')) countBits(unitBits.out, conv.unitFlags);
			}
		}
		// Objects the conversion has that the original does not.
		for (const o of b) if (!a.some((x) => x.num === o.num)) errors.push(`D: map ${m[1]}: object ${o.num} (${o.cls}) is not in the original`);
		for (const g of [...groups.values()].sort((x, y) => x.cls.localeCompare(y.cls))) {
			const to = [...g.to];
			const gbrId = to.length === 1 ? to[0] : to.join('/');
			const gbrC = to.length === 1 ? classInfo(ctx.gbr.classes, to[0]) : null;
			const i1C = classInfo(ctx.i1.classes, g.cls);
			const how = to.length !== 1 ? 'SPLIT' : lower(to[0]) === lower(g.cls) ? 'same id' : 'substituted';
			let status = 'ok';
			const notes = [];
			if (!gbrC) {
				status = 'ERROR';
				errors.push(`D: map ${m[1]}: ${g.cls} -> ${gbrId}: not a class of Great Battles of Rome`);
			}
			const expected = removed.get(g.cls);
			if (g.placed !== g.outPlaced || g.tmpl !== g.outTmpl) {
				if (expected) notes.push(`removed on purpose: ${expected}`);
				else {
					status = 'ERROR';
					errors.push(`D: map ${m[1]}: ${g.cls}: ${g.placed}+${g.tmpl} objects in the original, ${g.outPlaced}+${g.outTmpl} converted`);
				}
			}
			if (gbrC && i1C) {
				const cppA = inherited(ctx.i1.classes, g.cls, (c) => c.cpp || undefined);
				const cppB = inherited(ctx.gbr.classes, to[0], (c) => c.cpp || undefined);
				if (cppA && cppB && cppA !== cppB) {
					if (cppB === 'CVXDecor' && !scriptedClasses.has(g.cls)) notes.push(`${cppA} -> ${cppB}: scenery no script reaches, placed as Great Battles of Rome places its decors`);
					else {
						status = 'ERROR';
						errors.push(`D: map ${m[1]}: ${g.cls} (${cppA}) -> ${to[0]} (${cppB}): another kind of object`);
					}
				}
				const parentA = i1C.parent;
				const parentB = gbrC.parent;
				if (parentA && parentB && lower(parentA) !== lower(parentB)) notes.push(`parent ${parentA} -> ${parentB}`);
				const extra = behaviorsOf(ctx.gbr.classes, to[0]).filter((x) => !behaviorsOf(ctx.i1.classes, g.cls).includes(x));
				for (const x of extra) {
					if (!behaviors.has(x)) behaviors.set(x, { classes: new Set(), maps: new Set() });
					behaviors.get(x).classes.add(`${g.cls}->${to[0]}`);
					behaviors.get(x).maps.add(m[1]);
				}
				if (extra.length) notes.push(`+ behaviour ${extra.map((x) => x.replace(/^data\/subai\//, '')).join(', ')}`);
				if (i1C.displayName && gbrC.displayName && i1C.displayName !== gbrC.displayName) notes.push(`"${i1C.displayName}" -> "${gbrC.displayName}"`);
			}
			rows.push([m[1], g.cls, gbrId, how, `${g.placed}/${g.outPlaced}`, `${g.tmpl}/${g.outTmpl}`, [...g.players].sort((x, y) => x - y).join(','), status, notes.join('; ')]);
		}
	}
	for (const [mask, meaning] of FLAG_BITS) {
		const used = (v) => ((v & mask) >>> 0 ? 'yes' : 'no');
		bitsRows.push([`0x${mask.toString(16).padStart(8, '0')}`, meaning, used(flagBits.i1), used(flagBits.out), used(ctx.refFlagBits || 0)]);
	}
	const unexplained = FLAG_BITS.reduce((acc, [mask]) => acc & ~mask, flagBits.out) >>> 0;
	if (unexplained) warnings.push(`D: flags bits 0x${unexplained.toString(16)} of the converted objects have no known meaning`);
	const behaviorRows = [];
	for (const [x, e] of [...behaviors].sort((p, q) => p[0].localeCompare(q[0]))) {
		const note = M.GBR_BEHAVIORS[x];
		const status = !note ? 'check' : /DIFFERS/.test(note) ? 'differs' : 'handled';
		if (!note) warnings.push(`D: Great Battles of Rome runs ${x} on ${[...e.classes].join(', ')}, Imperivm I did not`);
		const classes = [...e.classes];
		behaviorRows.push([x.replace(/^data\/subai\//, ''), classes.slice(0, 8).join(', ') + (classes.length > 8 ? ` (+${classes.length - 8})` : ''), String(e.maps.size), status, note || '']);
	}
	const unitFlagRows = [];
	for (let b = 0; b < 32; b++) {
		const a = unitBits.i1.get(b) || 0;
		const o = unitBits.out.get(b) || 0;
		const r = (ctx.refUnitBits && ctx.refUnitBits.get(b)) || 0;
		if (!a && !o && !r) continue;
		if (o && !r && !UNIT_FLAG_BITS[1 << b]) warnings.push(`D: UnitFlags bit 0x${((1 << b) >>> 0).toString(16)} is set on converted objects and on none of Great Battles of Rome's`);
		unitFlagRows.push([`0x${((1 << b) >>> 0).toString(16)}`, String(a), String(o), String(r), UNIT_FLAG_BITS[(1 << b) >>> 0] || '']);
	}
	return { rows, bitsRows, behaviorRows, unitFlagRows };
}

// Script literals that name a class, per map: [map, literal, class used, exists].
function classLiterals(i1Files, outFiles, ctx, errors) {
	const rows = [];
	const i1Ids = new Map();
	for (const c of ctx.i1.classes.values()) for (const id of [c.id, c.altid].filter(Boolean)) i1Ids.set(lower(id), c.id);
	const gbrIds = new Map();
	for (const c of ctx.gbr.classes.values()) for (const id of [c.id, c.altid].filter(Boolean)) gbrIds.set(lower(id), c.id);
	const count = (files) => {
		const per = new Map();
		for (const [p, buf] of files) {
			if (!/\.vs$/i.test(p)) continue;
			const map = (/^Maps\/([^/]+)\//i.exec(p) || [null, '(adventure)'])[1];
			for (const t of S.tokenize(text(buf))) {
				if (t.type !== 'string') continue;
				const v = t.text.slice(1, -1);
				if (!i1Ids.has(lower(v)) && !gbrIds.has(lower(v))) continue;
				const k = `${map}|${v}`;
				per.set(k, (per.get(k) || 0) + 1);
			}
		}
		return per;
	};
	const a = count(i1Files);
	const b = count(outFiles);
	for (const k of [...new Set([...a.keys(), ...b.keys()])].sort()) {
		const [map, v] = k.split('|');
		const exists = gbrIds.has(lower(v));
		if (b.has(k) && !exists) errors.push(`D: map ${map}: scripts name class "${v}", which Great Battles of Rome lacks`);
		rows.push([map, v, String(a.get(k) || 0), String(b.get(k) || 0), exists ? 'yes' : i1Ids.has(lower(v)) ? 'NO (Imperivm I only)' : 'no']);
	}
	return rows;
}

const KEYWORDS = new Set(['if', 'else', 'while', 'for', 'do', 'return', 'switch', 'case', 'break', 'continue', 'sizeof']);
const BUILTIN_MEMBERS = new Set(['x', 'y', 'Set', 'left', 'top', 'right', 'bottom']);

// Calls of a script with their number of arguments: [{name, method, args, verb}].
function calls(src) {
	const tokens = S.tokenize(src).filter((t) => t.type !== 'space' && t.type !== 'comment');
	const out = [];
	for (let i = 0; i < tokens.length; i++) {
		const t = tokens[i];
		if (t.type !== 'ident' || KEYWORDS.has(t.text)) continue;
		const method = i > 0 && tokens[i - 1].text === '.';
		const call = tokens[i + 1] && tokens[i + 1].text === '(';
		if (!method && !call) continue;
		// A declaration "Type name(...)" does not happen in the language; a cast-like "Type(" is a function.
		let args = null;
		const firstArgs = [];
		if (call) {
			let depth = 0;
			args = 0;
			let any = false;
			let cur = [];
			for (let j = i + 1; j < tokens.length; j++) {
				const x = tokens[j].text;
				if (x === '(' || x === '[') depth++;
				else if (x === ')' || x === ']') {
					depth--;
					if (depth === 0) {
						if (any) {
							args++;
							firstArgs.push(cur);
						}
						break;
					}
				} else if (x === ',' && depth === 1) {
					args++;
					firstArgs.push(cur);
					cur = [];
					continue;
				}
				if (depth >= 1 && !(depth === 1 && x === '(' && j === i + 1)) {
					any = true;
					cur.push(tokens[j]);
				}
			}
		}
		let verb = null;
		if (method && /^(SetCommand|SetCommandOffset|AddCommand|AddCommandOffset)$/.test(t.text)) {
			const at = /^Add/.test(t.text) ? 1 : 0;
			const arg = firstArgs[at];
			if (arg && arg.length === 1 && arg[0].type === 'string') verb = arg[0].text.slice(1, -1);
			else if (arg) verb = '(computed)';
		}
		out.push({ name: t.text, method, args, verb });
	}
	return out;
}

function normalizeScript(buf) {
	if (!buf) return null;
	return S.tokenize(text(buf))
		.filter((t) => t.type !== 'comment')
		.map((t) => (t.type === 'space' ? ' ' : t.text))
		.join('')
		.replace(/\s+/g, ' ')
		.trim();
}

function tableC(i1Files, outFiles, ctx, errors, warnings) {
	const gather = (files) => {
		const fn = new Map();
		for (const [p, buf] of files) {
			if (!/\.vs$/i.test(p)) continue;
			if (ctx.referenced && !ctx.referenced(p)) continue;
			for (const c of calls(text(buf))) {
				const key = c.method ? `method|${c.name}` : `function|${c.name}`;
				if (!fn.has(key)) fn.set(key, { count: 0, args: new Set(), verbs: new Map() });
				const e = fn.get(key);
				e.count++;
				if (c.args !== null) e.args.add(c.args);
				if (c.verb) e.verbs.set(c.verb, (e.verbs.get(c.verb) || 0) + 1);
			}
		}
		return fn;
	};
	const a = gather(i1Files);
	const b = gather(outFiles);
	const gbr = ctx.gbr;
	const methodArity = new Map();
	for (const [k, v] of gbr.arity) {
		if (!k.includes('::')) continue;
		const n = k.split('::')[1];
		if (!methodArity.has(n)) methodArity.set(n, new Set());
		for (const x of v) methodArity.get(n).add(x - 1);
	}
	const rows = [];
	for (const key of [...new Set([...a.keys(), ...b.keys()])].sort()) {
		const [kind, name] = key.split('|');
		const ea = a.get(key);
		const eb = b.get(key);
		const renamed = kind === 'function' && M.FUNCTION_RENAMES[name] ? M.FUNCTION_RENAMES[name] : null;
		let exists;
		let arity;
		if (kind === 'function') {
			exists = gbr.functions.has(name) || (ctx.modFunctions && ctx.modFunctions.has(name));
			arity = gbr.arity.get(name) || null;
		} else {
			exists = gbr.methods.has(name) || BUILTIN_MEMBERS.has(name);
			arity = methodArity.get(name) || null;
		}
		let status = 'ok';
		const notes = [];
		if (renamed) notes.push(`written ${renamed}`);
		if (eb && !exists) {
			status = 'ERROR';
			errors.push(`C: ${kind} ${name}: Great Battles of Rome has no such ${kind}`);
		}
		if (eb && exists && arity) {
			const bad = [...eb.args].filter((n) => !arity.has(n));
			if (bad.length) {
				status = 'ERROR';
				errors.push(`C: ${kind} ${name}: called with ${bad.join('/')} arguments, Great Battles of Rome takes ${[...arity].join('/')}`);
			}
		}
		if (ea && !eb && !renamed) notes.push('no longer called (rewritten)');
		if (KNOWN[key]) notes.push(KNOWN[key]);
		if (!ea && eb) notes.push('added by the converter');
		rows.push([kind, name, String(ea ? ea.count : 0), String(eb ? eb.count : 0), ea ? [...ea.args].sort().join('/') : '', exists ? 'yes' : 'NO', arity ? [...arity].sort().join('/') : '?', status, notes.join('; ')]);
	}

	// Command verbs: the script each class runs for them, Imperivm I against Great Battles of Rome.
	const verbs = new Map();
	for (const side of [a, b]) {
		for (const name of ['SetCommand', 'SetCommandOffset', 'AddCommand', 'AddCommandOffset']) {
			const e = side.get(`method|${name}`);
			if (!e) continue;
			for (const [v, n] of e.verbs) {
				if (!verbs.has(v)) verbs.set(v, { i1: 0, out: 0 });
				verbs.get(v)[side === a ? 'i1' : 'out'] += n;
			}
		}
	}
	// One row per verb and pair of scripts (Imperivm I's, Great Battles of Rome's), with the classes that run them.
	const verbRows = [];
	const pairs = ctx.classPairs || new Map();
	const lines = (t) => new Set((t || '').split(/[;{}]/).map((x) => x.trim()).filter(Boolean));
	for (const [verb, n] of [...verbs].sort((x, y) => x[0].localeCompare(y[0]))) {
		if (verb === '(computed)') {
			verbRows.push([verb, String(n.i1), String(n.out), '', '', '', '', 'check', 'the verb is computed at run time']);
			continue;
		}
		const groups = new Map();
		for (const [from, to] of pairs) {
			const sa = inherited(ctx.i1.classes, from, (c) => c.methodScripts.get(lower(verb))) || null;
			const sb = inherited(ctx.gbr.classes, to, (c) => c.methodScripts.get(lower(verb))) || null;
			if (!sa && !sb) continue;
			const key = `${sa}|${sb}`;
			if (!groups.has(key)) groups.set(key, { sa, sb, classes: [] });
			groups.get(key).classes.push(from === to ? from : `${from}->${to}`);
		}
		if (!groups.size) groups.set('none', { sa: null, sb: null, classes: [] });
		for (const g of groups.values()) {
			let diff;
			let status = 'ok';
			let note = '';
			if (g.sa && !g.sb) {
				diff = 'MISSING in GBR';
				note = KNOWN[`verb|${verb}`] || '';
				status = note ? 'handled' : n.out ? 'check' : 'ok';
			} else if (!g.sa && g.sb) {
				diff = 'GBR only';
			} else if (!g.sa && !g.sb) {
				diff = 'no class of the campaign has it';
				status = n.out ? 'check' : 'ok';
			} else {
				const ta = normalizeScript(ctx.i1.data.read(g.sa.replace(/^data\//i, '')));
				const tb = normalizeScript(gbr.readData(g.sb));
				if (ta !== null && ta === tb) diff = 'same script';
				else {
					const la = lines(ta);
					const lb = lines(tb);
					diff = `differs (-${[...la].filter((x) => !lb.has(x)).length} +${[...lb].filter((x) => !la.has(x)).length} statements)`;
					note = SCRIPT_NOTES[lower(g.sb)] || '';
					status = note ? 'differs' : 'check';
				}
			}
			if (status === 'check') warnings.push(`C: command "${verb}" (${g.classes.slice(0, 4).join(', ')}): ${diff}, not reviewed`);
			const classes = g.classes.slice(0, 5).join(', ') + (g.classes.length > 5 ? ` (+${g.classes.length - 5})` : '');
			verbRows.push([verb, String(n.i1), String(n.out), g.sa || '-', g.sb || '-', classes, diff, status, note]);
		}
	}
	return { rows, verbRows };
}

// Relations of the player files: "player: relation word to each player" (hex), I1 against the conversion.
function relations(files) {
	const out = new Map();
	for (const [p, buf] of files) {
		const m = /^player(\d+)\.xml$/i.exec(p);
		if (!m) continue;
		const t = text(buf);
		const r = (/\srelations="([0-9a-fA-F]*)"/.exec(t) || [])[1] || '';
		const allied = (/\sallied="([^"]*)"/.exec(t) || [])[1];
		out.set(Number(m[1]), { words: r.match(/.{8}/g) || [], allied });
	}
	return out;
}

function tableB(i1Files, outFiles, ctx, errors) {
	const ra = relations(i1Files);
	const rb = relations(outFiles);
	const relRows = [];
	for (const [p, v] of [...ra].sort((x, y) => x[0] - y[0])) {
		const w = rb.get(p);
		const nonSelf = v.words.map((x, i) => (i !== p && x !== '00000000' ? `${i}:${x}` : null)).filter(Boolean);
		const same = w && w.words.join() === v.words.join() && w.allied === v.allied;
		if (!same) errors.push(`B: player ${p}: relations or allied changed by the conversion`);
		relRows.push([String(p), `self ${v.words[p] || '?'}`, nonSelf.join(' ') || 'none (at war with everyone)', `allied=${v.allied}`, same ? 'kept' : 'CHANGED']);
	}
	// Dipl* calls per map, with the sequence that runs them.
	const callRows = [];
	const gateRows = [];
	for (const [p, buf] of outFiles) {
		const m = /^(?:Maps\/([^/]+)\/)?Sequences\/([^/]+\.vs)$/i.exec(p);
		if (!m) continue;
		if (ctx.referenced && !ctx.referenced(p)) continue;
		const lines = text(buf).split(/\r?\n/);
		lines.forEach((l, i) => {
			const code = l.replace(/\/\/.*$/, '');
			for (const c of code.matchAll(/\b(Dipl\w+|ClearDiplomacy|_?Players\w+)\s*\(([^)]*)\)/g)) {
				callRows.push([m[1] || '(adventure)', `${m[2]}:${i + 1}`, `${ctx.sequenceName ? ctx.sequenceName(p) : ''}`, `${c[1]}(${c[2].replace(/\s+/g, '')})`]);
			}
		});
	}
	for (const [p, buf] of outFiles) {
		const m = /^Maps\/([^/]+)\/map\.obj\.xml$/i.exec(p);
		if (!m) continue;
		const { doc, objects } = mapObjects(buf);
		const named = new Map();
		for (const r of doc.roots) {
			for (const el of X.walk(r)) {
				if (el.name !== 'group' || el.attrs.get('type') !== '0') continue;
				const nums = el.children.filter((c) => c.name === 'obj').map((c) => c.attrs.get('num'));
				if (nums.length === 1) named.set(nums[0], el.attrs.get('name'));
			}
		}
		const scripts = [...outFiles].filter(([q]) => q.toLowerCase().startsWith(`maps/${m[1]}/sequences/`.toLowerCase()) && /\.vs$/i.test(q));
		for (const o of objects) {
			const isGate = inherited(ctx.gbr.classes, o.cls, (c) => (c.cpp === 'CVXGate' ? true : undefined));
			if (!isGate || o.flags & 0x08000000) continue;
			const name = named.get(o.num) || '';
			const cmds = [];
			if (name) {
				const re = new RegExp(`(?:\\b${name}\\b|GetNamedObj\\s*\\(\\s*"${name}"\\s*\\))\\s*\\.\\s*SetCommand\\s*\\(\\s*"(\\w+)"`, 'g');
				for (const [q, sb] of scripts) for (const c of text(sb).matchAll(re)) cmds.push(`${c[1]} (${q.split('/').pop()})`);
			}
			gateRows.push([m[1], o.num, o.cls, name || '(unnamed)', o.player, cmds.join(', ') || 'none: opens for units whose player the owner is at cease-fire with']);
		}
	}
	return { relRows, callRows, gateRows };
}

function table(headers, rows) {
	const w = headers.map((h, i) => Math.min(60, Math.max(h.length, ...rows.map((r) => String(r[i] || '').length))));
	const line = (r) => r.map((c, i) => String(c || '').padEnd(w[i])).join(' | ').replace(/\s+$/, '');
	return [line(headers), w.map((n) => '-'.repeat(n)).join('-+-'), ...rows.map(line)].join('\n');
}

// input: {i1Files, outFiles, refFiles, gbr, i1, gbrExe: Buffer, i1Exe: Buffer, classPairs, referenced, sequenceName,
// removedClasses, knownBehaviors, modFunctions}. Returns {errors, warnings, text, tables}.
function auditAdventure(input) {
	const errors = [];
	const warnings = [];
	const gbrExe = exeStrings(input.gbrExe);
	const i1Exe = exeStrings(input.i1Exe);
	let refFlagBits = 0;
	const refUnitBits = new Map();
	for (const [p, buf] of input.refFiles) {
		if (!/map\.obj\.xml$/i.test(p)) continue;
		try {
			for (const o of mapObjects(buf).objects) {
				refFlagBits |= o.flags;
				if (o.attrs.has('UnitFlags')) for (let b = 0; b < 32; b++) if ((o.unitFlags >>> b) & 1) refUnitBits.set(b, (refUnitBits.get(b) || 0) + 1);
			}
		} catch {
			/* a reference map that does not parse says nothing */
		}
	}
	// Scripts that a sequences.xml runs, with the sequence's name; and the class each original class became.
	const seqNames = new Map();
	for (const files of [input.i1Files, input.outFiles]) {
		for (const [p, buf] of files) {
			const m = /^(Maps\/[^/]+\/)?Sequences\/sequences\.xml$/i.exec(p);
			if (!m) continue;
			for (const s of text(buf).matchAll(/<sequence\b[^>]*?\sname="([^"]*)"[^>]*?\sscript="Current(?:Map|Game)\/sequences\/([^"]+)"/gi)) {
				seqNames.set(lower((m[1] || '') + 'Sequences/' + s[2]), s[1]);
			}
		}
	}
	const classPairs = new Map();
	for (const [p, buf] of input.i1Files) {
		if (!/^Maps\/[^/]+\/map\.obj\.xml$/i.test(p)) continue;
		const outKey = [...input.outFiles.keys()].find((k) => lower(k) === lower(p));
		if (!outKey) continue;
		const conv = new Map(mapObjects(input.outFiles.get(outKey)).objects.map((o) => [o.num, o.cls]));
		for (const o of mapObjects(buf).objects) if (conv.has(o.num)) classPairs.set(o.cls, conv.get(o.num));
	}
	const ctx = {
		...input,
		refFlagBits,
		refUnitBits,
		classPairs,
		referenced: (p) => seqNames.has(lower(p)),
		sequenceName: (p) => seqNames.get(lower(p)) || '',
	};
	const A = tableA(input.i1Files, input.outFiles, input.refFiles, gbrExe, i1Exe, errors, warnings);
	const B = tableB(input.i1Files, input.outFiles, ctx, errors);
	const C = tableC(input.i1Files, input.outFiles, ctx, errors, warnings);
	const D = tableD(input.i1Files, input.outFiles, ctx, errors, warnings);
	const L = classLiterals(input.i1Files, input.outFiles, ctx, errors);
	const out = [];
	out.push('A. Elements and attributes of the XML files (count original / converted)');
	out.push(table(['file', 'element', 'attribute', 'I1', 'out', 'carried', 'GBR reads it', 'I1 exe', 'status', 'notes'], A.rows));
	out.push('', 'A. Other files (by extension)');
	out.push(table(['ext', 'I1', 'out', 'GBR', 'notes'], A.files));
	out.push('', 'B. Relations of the player files (hex word per player; bit 0 = cease-fire, the bit gates test)');
	out.push(table(['player', 'self', 'other relations', 'allied', 'status'], B.relRows));
	out.push('', 'B. Diplomacy calls of the converted scripts');
	out.push(table(['map', 'where', 'sequence', 'call'], B.callRows));
	out.push('', 'B. Gates (a gate opens for a unit whose player its owner is at cease-fire with, and closes for enemies: Imperivm.exe 004E112D, gbr.exe 00529580)');
	out.push(table(['map', 'num', 'class', 'name', 'owner', 'commands from scripts'], B.gateRows));
	out.push('', 'C. Functions and methods the scripts call');
	out.push(table(['kind', 'name', 'I1 calls', 'out calls', 'args used', 'in GBR', 'GBR args', 'status', 'notes'], C.rows));
	out.push('', 'C. Command verbs (SetCommand/AddCommand...): the script each class runs, Imperivm I against Great Battles of Rome');
	out.push(table(['verb', 'I1', 'out', 'I1 script', 'GBR script', 'classes', 'comparison', 'status', 'what changes'], C.verbRows));
	out.push('', 'D. Classes per map (objects placed in the world / group templates, original/converted)');
	out.push(table(['map', 'I1 class', 'GBR class', 'how', 'placed', 'templates', 'players', 'status', 'notes'], D.rows));
	out.push('', 'D. Behaviours Great Battles of Rome\'s classes run that Imperivm I\'s did not');
	out.push(table(['behaviour', 'classes (I1->GBR)', 'maps', 'status', 'what it does'], D.behaviorRows));
	out.push('', 'D. Class names in the scripts (count original / converted)');
	out.push(table(['map', 'class', 'I1', 'out', 'in GBR'], L));
	if (input.actorRows) {
		out.push('', 'D. Conversation actors: the object that speaks, the title of its phrases (Spanish) and its picture');
		out.push(table(['map', 'conversation', 'actor', 'I1 class', 'GBR class', 'title (es)', 'picture'], input.actorRows));
		for (const r of input.actorRows) if (/^NONE/.test(r[6])) warnings.push(`D: map ${r[0]} ${r[1]}: actor ${r[2]} has no picture (${r[6]})`);
	}
	if (input.sight) {
		out.push('', 'D. Unit sight, also the radius in which a unit picks a target on its own (gbr.exe 005DCA61): Great Battles of Rome -> Imperivm I, set by the I1_Sight sequence');
		out.push(table(['GBR class', 'GBR sight', 'I1 sight', 'I1 class'], input.sight.map((r) => [r.gbr, String(r.base), String(r.i1), r.from])));
	}
	if (input.stats) {
		out.push('', 'D. Combat stats (I1_Stats): Imperivm I min-max attack, defense slash/pierce, maxhealth, speed; Great Battles of Rome damage, armor slash/pierce, maxhealth, speed; what the unit gets (damage/armor fitted to Imperivm I\'s hits, see adventure.statRules)');
		out.push(
			table(
				['GBR class', 'I1 class', 'I1 attack', 'I1 def s/p', 'I1 hp', 'I1 speed', 'GBR dmg', 'GBR arm s/p', 'GBR hp', 'GBR speed', 'set dmg', 'set armor', 'set hp', 'speed %'],
				input.stats.map((r) => [r.gbr, r.from, `${r.i1.min}-${r.i1.max} ${r.i1.type}`, `${r.i1.slash}/${r.i1.pierce}`, String(r.i1.health), String(r.i1.speed), String(r.base.damage), `${r.base.slash}/${r.base.pierce}`, String(r.base.health), String(r.base.speed), String(r.set.damage), String(r.set.armor), String(r.set.health), String(r.set.speed)]),
			),
		);
	}
	out.push('', 'D. Bits of the objects\' flags');
	out.push(table(['bit', 'meaning', 'I1', 'out', 'GBR adventures'], D.bitsRows));
	out.push('', 'A/D. UnitFlags bits (objects with the bit: original / converted / GBR adventures). Imperivm I reads only the high 16 bits (Imperivm.exe 0050E50B), Great Battles of Rome all 32 (gbr.exe 005DD6A9)');
	out.push(table(['bit', 'I1', 'out', 'GBR', 'meaning'], D.unitFlagRows));
	out.push('', `Errors: ${errors.length}`, ...errors.map((e) => '  ' + e), `Warnings: ${warnings.length}`, ...warnings.map((w) => '  ' + w));
	return { errors, warnings, text: out.join('\n') + '\n', tables: { A, B, C, D, L } };
}

// "I1PROBE census ..." lines of a --probe build's log against the converted maps. Returns the text of a table.
function compareCensus(logText, outFiles, mapNames) {
	const seen = new Map();
	for (const line of logText.split(/\r?\n/)) {
		const m = /I1PROBE census map=(.*?) round=(\d+) player=(\d+) class=(.*?) kind=\S+.*?(?: inholder=(\d))?$/.exec(line);
		if (!m) continue;
		const key = `${m[1]}|${m[2]}`;
		if (!seen.has(key)) seen.set(key, new Map());
		const k = `${m[4]}|${m[3]}`;
		const e = seen.get(key).get(k) || { n: 0, held: 0 };
		e.n++;
		if (m[5] === '1') e.held++;
		seen.get(key).set(k, e);
	}
	const rows = [];
	const byName = new Map([...mapNames].map(([id, n]) => [lower(n), id]));
	for (const [key, counts] of seen) {
		const [mapName, round] = key.split('|');
		const id = byName.get(lower(mapName)) || mapName;
		const p = [...outFiles.keys()].find((k) => lower(k) === lower(`Maps/${id}/map.obj.xml`));
		const expected = new Map();
		if (p) {
			for (const o of mapObjects(outFiles.get(p)).objects) {
				if (o.flags & 0x08000000 || !(o.flags & 0xc00000)) continue;
				const k = `${o.cls}|${o.player}`;
				expected.set(k, (expected.get(k) || 0) + 1);
			}
		}
		for (const k of [...new Set([...expected.keys(), ...counts.keys()])].sort()) {
			const [cls, player] = k.split('|');
			const got = counts.get(k) || { n: 0, held: 0 };
			const want = expected.get(k) || 0;
			rows.push([id, round, cls, player, String(want), String(got.n), String(got.held), got.n === want ? '' : got.n < want ? 'MISSING' : 'more (spawned)']);
		}
	}
	return table(['map', 'round', 'class', 'player', 'in map', 'in world', 'in holder', ''], rows) + '\n';
}

module.exports = { auditAdventure, compareCensus, exeStrings, inventory, calls, mapObjects, kindOf, KNOWN, FLAG_BITS };
