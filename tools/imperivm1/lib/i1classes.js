// The classes of an older game (Imperivm I, Imperivm II: `src`, lib/catalog.js legacySource) whose Great Battles of Rome
// counterpart behaves otherwise, written into the campaign's data mod so that only the campaign uses them. The text below
// says Imperivm I; Imperivm II's are made the same way, with its prefix (I2) and folder (data/mod/imperivm2/).
//
// A class is Imperivm I's, with its whole ancestry (buildFullText): its properties, behaviours, commands and right-click
// commands, under the class of Great Battles of Rome of its parent's name, and Great Battles of Rome's file only for what
// the player sees. Its combat values go to data/mod/imperivm1/stats.ini for the mod's DLL, which also keeps the class from
// running its parent's behaviours (src/game/i1engine.cpp). The scripts of its behaviours and commands are Imperivm I's,
// ported (renamed functions, class names of the conversion, GetConst() replaced by Imperivm I's constant) into
// data/mod/imperivm1/.
// Its buttons, trainings and researches are Imperivm I's commands, in data/commands/imperivm1.xml with only the new classes
// as sources (portCommands), so the game's own classes keep theirs; the game's commands Imperivm I's class did not have
// are taken from it by a patch (<nsrc obj="I1GTownhall"/>).
//
// Everything is made from the installed games when converting: no file of either game is in the repository.
'use strict';
const S = require('./script');
const M = require('./mapping');
const { parseClass } = require('./catalog');

const SCRIPT_DIR = 'data/mod/imperivm1/';
// Where a game's ported scripts go in the data mod.
const scriptDir = (src) => `data/mod/${src.dir}/`;
const lower = (s) => s.toLowerCase();
const live = (text) => text.replace(/<!--[\s\S]*?-->/g, '');

// Constants of an .ini ("Name = value ; comment"): Map lower-case name -> value.
function readConstants(text) {
	const out = new Map();
	for (const line of text.split(/\r?\n/)) {
		const m = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]*?)\s*(;.*)?$/.exec(line);
		if (m && m[2] !== '') out.set(lower(m[1]), m[2]);
	}
	return out;
}

// Value of an attribute anywhere in the text (first match), or null.
function attrValue(text, name) {
	const m = new RegExp(`\\s${name}\\s*=\\s*"([^"]*)"`).exec(text);
	return m ? m[1] : null;
}

function setHeaderAttr(head, name, value) {
	const re = new RegExp(`(\\s${name}\\s*=\\s*")[^"]*(")`);
	return re.test(head) ? head.replace(re, `$1${value}$2`) : head.replace(/^<class\b/, `<class ${name}="${value}"`);
}

// Where a script of the game goes in the mod.
const portedPath = (p, dir = SCRIPT_DIR) => dir + p.replace(/^.*[\\/]/, '').toLowerCase();
// The scripts a <method> tag runs (vs, verify, onfinish...).
const tagScripts = (tag) => [...tag.matchAll(/\s[a-z_]+\s*=\s*"(data\/[^"]+\.vs)"/gi)].map((m) => m[1]);

// The <method> tags of an Imperivm I class (texts[0]) and of the ancestors after it, those of an ancestor only for the
// commands the class does not define itself: [{sig, tag}] with the script paths pointing to the ported scripts.
function i1Methods(texts, dir = SCRIPT_DIR) {
	const out = [];
	const defined = new Set();
	texts.forEach((text, k) => {
		const here = new Set();
		for (const m of live(text).matchAll(/<method\b[^>]*\/>/g)) {
			const sig = lower(attrValue(m[0], 'sig') || '');
			if (k > 0 && defined.has(sig)) continue;
			here.add(sig);
			out.push({ sig, tag: m[0].replace(/\s+/g, ' '), scripts: tagScripts(m[0]) });
		}
		for (const s of here) defined.add(s);
	});
	for (const m of out) for (const s of m.scripts) m.tag = m.tag.split(`"${s}"`).join(`"${portedPath(s, dir)}"`);
	return out;
}

// Engine types whose objects take part in the game (units, heroes, buildings, ships, wagons): every class of these types
// that Imperivm I places, trains or summons gets its own version in the campaign (autoI1Classes).
const GAME_CPP = new Set(['CVXBarrack', 'CVXBuilding', 'CVXCatapult', 'CVXDruid', 'CVXFlyingUnit', 'CVXGate', 'CVXGhost', 'CVXHero', 'CVXOutpost', 'CVXShip', 'CVXTavern', 'CVXTownHall', 'CVXUnit', 'CVXWagon']);
// Properties a full copy keeps from Great Battles of Rome's class: what the player sees (pictures, names, help) and how
// its panel is laid out (interface: the two games' words differ, imperivm-re/analisis/i1-vs-gbr/propiedades.md).
const KEEP_GBR = new Set(['icon', 'help', 'edittree_pos', 'display_name', 'display_name_plural', 'settlement_icon_name', 'interface']);
// Imperivm I's properties Great Battles of Rome does not read with the same meaning: its attack and defence (another
// combat formula, done by the mod's DLL from the stats file, STAT_PROPS) and the ones Imperivm.exe itself ignores.
const STAT_PROPS = ['minattack', 'maxattack', 'defense_slash', 'defense_pierce', 'damage_type', 'speciality', 'heroarmyexpgain'];
const NOT_COPIED = new Set(['minattack', 'maxattack', 'defense_slash', 'defense_pierce', 'speciality', 'minrange', 'defence_slash', 'defence_pierce', 'attack_type', 'sound_onselect', 'description']);
// Renamed in Great Battles of Rome.
const PROP_RENAMES = { itemtypes: 'itemtype' };

// The properties of a class with its ancestors (texts: the class first, then each parent): Map lower name -> value, the
// nearest one winning.
function flatProps(texts) {
	const out = new Map();
	for (let k = texts.length - 1; k >= 0; k--) {
		for (const m of live(texts[k]).matchAll(/<properties\b([^>]*?)\/?>/g)) {
			for (const a of m[1].matchAll(/\s([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"/g)) out.set(lower(a[1]), a[2]);
		}
	}
	return out;
}

// The <defaultcmd> blocks of a class with its ancestors, one per target, the nearest one winning.
function flatDefaultCmds(texts) {
	const out = new Map();
	texts.forEach((text) => {
		for (const m of live(text).matchAll(/<defaultcmd\b[^>]*>[\s\S]*?<\/defaultcmd>/g)) {
			const target = lower(attrValue(m[0].replace(/>[\s\S]*$/, '>'), 'target') || '');
			if (!out.has(target)) out.set(target, m[0].replace(/\r?\n/g, '\n'));
		}
	});
	return [...out.values()];
}

// Sets a property of a class file: every place that writes it, or a new line after the class's head.
function setProp(text, name, value, eol) {
	const re = new RegExp(`(<properties\\b[^>]*?\\s${name}\\s*=\\s*")[^"]*(")`, 'gi');
	if (re.test(text)) return text.replace(re, `$1${value}$2`);
	return text.replace(/(<class\b[^>]*>)/, `$1${eol}\t<properties ${name}="${value}"/>`);
}

// A full copy of an Imperivm I class (every def but the `fromI1` ones): Great Battles of Rome's file of `def.gbr` for
// what the player sees, with Imperivm I's properties of the whole ancestry (`i1Texts`, the class first), its commands
// (`methods`), behaviours and right-click commands, under `parent` (a class of Great Battles of Rome: the one of the name
// of the nearest Imperivm I ancestor it has, so that what looks for a kind of object, "Unit", "Melee", "Building", still
// finds it). Great Battles of Rome's specials and hero skills are emptied: Imperivm I had neither. Returns {text, kept:
// [properties set], droppedMethods, droppedBehaviors, stats: STAT_PROPS of the ancestry, for the DLL}.
function buildFullText(def, gbrText, i1Texts, parent, behaviors, methods, dir = SCRIPT_DIR) {
	const eol = gbrText.includes('\r\n') ? '\r\n' : '\n';
	let t = live(gbrText);
	t = t.replace(/<class\b[^>]*>/, (head) => setHeaderAttr(setHeaderAttr(head, 'id', def.id), 'parent', parent).replace(/\s+altid\s*=\s*"[^"]*"/, ''));
	const droppedBehaviors = [];
	t = t.replace(/[ \t]*<behavior\s+script\s*=\s*"([^"]*)"[^>]*\/>[ \t]*(\r?\n)?/g, (all, script) => {
		if (!behaviors.some((b) => lower(b) === lower(portedPath(script, dir)))) droppedBehaviors.push(script);
		return '';
	});
	const sigs = new Set(methods.map((m) => m.sig));
	const droppedMethods = [];
	t = t.replace(/[ \t]*<method\s+sig\s*=\s*"([^"]*)"[^>]*\/>[ \t]*(\r?\n)?/g, (all, sig) => {
		if (!sigs.has(lower(sig)) && !droppedMethods.includes(sig)) droppedMethods.push(sig);
		return '';
	});
	t = t.replace(/[ \t]*<defaultcmd\b[^>]*>[\s\S]*?<\/defaultcmd>[ \t]*(\r?\n)?/g, '');
	const props = flatProps(i1Texts);
	const kept = [];
	for (const [k, v] of props) {
		if (NOT_COPIED.has(k) || KEEP_GBR.has(k)) continue;
		const name = PROP_RENAMES[k] || k;
		const before = new RegExp(`<properties\\b[^>]*?\\s${name}\\s*=\\s*"([^"]*)"`, 'i').exec(t);
		if (before && before[1] === v) continue;
		t = setProp(t, name, v, eol);
		kept.push(`${name}=${v}`);
	}
	t = setProp(t, 'unit_specials', '', eol);
	if (/\bcpp_class\s*=\s*"CVXHero"/i.test(t)) t = setProp(t, 'heroskills', '', eol);
	// A catapult's shot is not a hit of the DLL's formula (CVXCatapultShot): its damage is Imperivm I's maxattack, and the
	// DLL gives it Imperivm I's bonus against buildings (imperivm-re/analisis/i1-vs-gbr/combate.md §6).
	if (/\bcpp_class\s*=\s*"CVXCatapult"/i.test(t) && Number(props.get('maxattack')) > 0) {
		t = setProp(t, 'damage', props.get('maxattack'), eol);
		kept.push(`damage=${props.get('maxattack')}`);
	}
	// What attacks in Imperivm I and has no damage in Great Battles of Rome (its towers do not shoot): the mean of its
	// attack, for when the DLL does not fight with the stats file.
	if (props.has('maxattack') && Number(props.get('maxattack')) > 0 && !/<properties\b[^>]*?\sdamage\s*=/i.test(t)) {
		const mean = Math.round((Number(props.get('minattack') || 0) + Number(props.get('maxattack'))) / 2);
		t = setProp(t, 'damage', String(mean), eol);
		kept.push(`damage=${mean}`);
	}
	const stats = {};
	for (const p of STAT_PROPS) if (props.has(p)) stats[p] = props.get(p);
	const lines = [...behaviors.map((b) => `\t<behavior script="${b}"/>`), ...methods.map((m) => `\t${m.tag}`), ...flatDefaultCmds(i1Texts).map((b) => '\t' + b.replace(/\n/g, eol))];
	if (lines.length) t = t.replace(/(<class\b[^>]*>)/, `$1${eol}${lines.join(eol)}`);
	return { text: t, kept, droppedMethods, droppedBehaviors, stats };
}

// The classes of Imperivm I (lower id -> info) and Great Battles of Rome (lower id and altid -> info).
function classIndex(catalog) {
	const out = new Map();
	for (const c of catalog.classes.values()) {
		if (!out.has(lower(c.id))) out.set(lower(c.id), c);
		if (c.altid && !out.has(lower(c.altid))) out.set(lower(c.altid), c);
	}
	return out;
}

// A class and its ancestors, the class first.
function ancestry(index, id) {
	const out = [];
	let c = index.get(lower(id));
	while (c && out.length < 40 && !out.includes(c)) {
		out.push(c);
		c = c.parent ? index.get(lower(c.parent)) : null;
	}
	return out;
}

// Every class of the game of the GAME_CPP types that no other class derives from (the ones the maps place) and that
// Great Battles of Rome has under its id or a substitute (rules.substitutes), not given by `manual`: a def
// {id: prefix + id, gbr, i1} for each.
function autoI1Classes(gbr, src, manual, rules) {
	const gIndex = classIndex(gbr);
	const taken = new Set(manual.map((d) => lower(d.i1)));
	const ids = new Set(manual.map((d) => lower(d.id)));
	const parents = new Set([...src.classes.values()].map((c) => c.parent && lower(c.parent)).filter(Boolean));
	const out = [];
	for (const c of new Set(src.classes.values())) {
		if (!GAME_CPP.has(c.cpp) || taken.has(lower(c.id)) || parents.has(lower(c.id)) || rules.keepGbr[c.id]) continue;
		const target = gIndex.get(lower(c.id)) || (rules.substitutes[c.id] && gIndex.get(lower(rules.substitutes[c.id])));
		if (!target || ids.has(lower(src.prefix + c.id))) continue;
		out.push({ id: src.prefix + c.id, gbr: target.id, i1: c.id, auto: true });
	}
	return out;
}

// The class file of a `fromI1` def: Imperivm I's own file (a class Great Battles of Rome lacks, or has as something else),
// with the new id, the entity of Great Battles of Rome's `entityFrom` class (none when the original has none) and the
// methods given. Returns {text, kept, droppedMethods, droppedBehaviors}.
function buildFromI1Text(def, i1Text, gbrEntity, gbrClassIds, behaviors, methods) {
	const eol = i1Text.includes('\r\n') ? '\r\n' : '\n';
	let t = live(i1Text);
	t = t.replace(/<class\b[^>]*>/, (head) => {
		let h = setHeaderAttr(head.replace(/\s+/g, ' '), 'id', def.id).replace(/\s+altid\s*=\s*"[^"]*"/, '');
		const parent = attrValue(h, 'parent');
		if (parent && !gbrClassIds.has(lower(parent))) throw new Error(`I1 class ${def.id}: parent ${parent} is not a class of Great Battles of Rome`);
		if (attrValue(h, 'entity')) h = setHeaderAttr(h, 'entity', gbrEntity || '');
		return h.replace(/\s*>$/, '>');
	});
	t = t.replace(/[ \t]*<(behavior|method)\b[^>]*\/>[ \t]*(\r?\n)?/g, '');
	const lines = [...behaviors.map((b) => `\t<behavior script="${b}"/>`), ...methods.map((m) => `\t${m.tag}`)];
	if (lines.length) t = t.replace(/(<class\b[^>]*>)/, `$1${eol}${lines.join(eol)}`);
	return { text: t, kept: [], droppedMethods: [], droppedBehaviors: [] };
}

// A script of Imperivm I made to run in Great Battles of Rome with the campaign's classes. `classSets`: Imperivm I classes
// Great Battles of Rome lacks that only group others (PossessAnimal) -> the classes they grouped; a query by one of them,
// ObjsInCircle(p, r, "PossessAnimal"), becomes the union of the same query by each. `specialities`: speciality -> the
// campaign's classes that have it (specialityClasses).
function portScript(src, classLiterals, constants, classSets = {}, label = "Imperivm I", specialities = {}) {
	let s = S.renameFunctions(src, M.FUNCTION_RENAMES).src;
	// A script whose `this` is a Settlement (settlement_behavior_ambient.vs: "Settlement this;") asks it as a receiver.
	if (/^\s*Settlement\s+this\s*;/m.test(s)) s = s.replace(/(^|[^\w.)\]])\.Is(Gaul|Rome)/gm, '$1this.Is$2');
	for (const [re, to] of M.SCRIPT_PROPERTY_RENAMES) s = s.replace(re, to);
	// A shipyard builds its ship: the class is an argument in Great Battles of Rome (Settlement::CreateShip(str)).
	s = s.replace(/\.CreateShip\s*\(\s*\)/g, `.CreateShip("${classLiterals.ShipL || 'ShipL'}")`);
	s = s.replace(/\bObjsInCircle\s*\(([^;"]*?),\s*"([^"]+)"\s*\)/g, (all, args, cls) => {
		const set = classSets[cls];
		if (!set || !set.length) return all;
		return set.map((c) => `ObjsInCircle(${args.trim()}, "${c}")`).reduce((a, b) => `Union(${a}, ${b})`);
	});
	s = S.replaceStringLiterals(s, classLiterals).src;
	s = S.bonusArguments(s).src;
	s = S.druidTargeting(s).script;
	// Unit::GetFlags(spec_charge) asks a unit's specialities in Imperivm II (Imperivm II.exe 0051b9e0 registers spec_*);
	// gbr.exe's reads its state flags (+0x194, 005d7e30), and the campaign's classes keep their specialities in stats.ini for
	// the DLL: it asks whether the unit is of a class with that speciality.
	s = s.replace(/([\w.]*)\.GetFlags\s*\(\s*spec_(\w+)\s*\)/g, (all, receiver, name) => {
		const classes = specialities[name.replace(/_/g, ' ')] || [];
		return classes.length ? `(${classes.map((c) => `${receiver}.IsHeirOf("${c}")`).join(' || ')})` : 'false';
	});
	let inlined = 0;
	s = s.replace(/\bGetConst\s*\(\s*"([^"]+)"\s*\)/g, (all, name) => {
		if (!constants.has(lower(name))) return all;
		inlined++;
		return `${constants.get(lower(name))} /* ${label}'s ${name} */`;
	});
	return { src: s, inlined };
}

// Each speciality of the campaign's classes (stats.ini) -> the classes that have it.
function specialityClasses(plan) {
	const out = {};
	for (const d of plan.defs) {
		const value = d.built.stats && d.built.stats.speciality;
		for (const name of value ? value.split(',') : []) if (name) (out[name] = out[name] || []).push(d.id);
	}
	return out;
}

// The classes to add, before the conversion: {defs, classes: [parseClass info], byGbr: Map lower(gbr id) -> new id,
// byI1: Map lower(id in the game) -> new id for the classes made from the game's file}. gbr is Great Battles of Rome's
// catalog and src the game (lib/catalog.js); the new classes are also added to gbr.classes. rules: {manual: defs given by
// hand (M.I1_CLASSES), substitutes (M.CLASS_SUBSTITUTES), keepGbr (M.I1_KEEP_GBR_CLASSES)}.
function planI1Classes(gbr, src, report, rules) {
	const dir = scriptDir(src);
	const section = `${src.label} classes`;
	const gbrIds = new Set([...gbr.classes.values()].flatMap((c) => [c.id, c.altid].filter(Boolean)).map(lower));
	const findClass = (catalog, id) => [...catalog.classes.values()].find((c) => lower(c.id) === lower(id));
	const i1Index = classIndex(src);
	const gIndex = classIndex(gbr);
	const defs = [];
	for (const def of [...rules.manual, ...autoI1Classes(gbr, src, rules.manual, rules)]) {
		const g = def.fromI1 ? def.entityFrom && findClass(gbr, def.entityFrom) : findClass(gbr, def.gbr);
		// A full copy takes the whole Imperivm I ancestry; a `fromI1` class only its own file.
		const chain = def.fromI1 ? [findClass(src, def.i1)] : ancestry(i1Index, def.i1);
		const o = chain[0];
		if ((!g && (!def.fromI1 || def.entityFrom)) || !o) throw new Error(`${section}: ${def.id}: ${!g ? `${def.entityFrom || def.gbr} is not a class of Great Battles of Rome` : `${def.i1} is not in ${src.label}`}`);
		const i1Texts = chain.map((c) => src.readClass(c).toString('latin1'));
		// Commands and behaviours whose script Imperivm I does not have (ShipL's capture) never ran there: left out.
		const hasI1 = (s) => !!src.read(s);
		const missing = [];
		const methods = i1Methods(i1Texts, dir).filter((m) => m.scripts.every(hasI1) || (missing.push(...m.scripts.filter((s) => !hasI1(s))), false));
		const behaviors = [...new Set(chain.flatMap((c) => c.behaviors.filter((b) => hasI1(b) || (missing.push(b), false)).map((b) => portedPath(b, dir))))];
		if (missing.length) report.add(section, `${def.id}: left out, ${src.label} has no ${[...new Set(missing)].join(', ')}`);
		// Its parent: Great Battles of Rome's class of the name of the nearest Imperivm I ancestor it has.
		const parentClass = def.fromI1 ? null : chain.slice(1).map((c) => gIndex.get(lower(c.id))).find(Boolean);
		if (!def.fromI1 && !parentClass) throw new Error(`${section}: ${def.id}: no ancestor of ${def.i1} is a class of Great Battles of Rome`);
		const built = def.fromI1
			? buildFromI1Text(def, i1Texts[0], g ? g.entity : '', gbrIds, behaviors, methods)
			: buildFullText(def, gbr.readData(g.file).toString('latin1'), i1Texts, parentClass.id, behaviors, methods, dir);
		// An icon Great Battles of Rome does not have (its CHero4 names Anibal.bmp): the one named after the entity.
		const icon = attrValue(built.text, 'icon');
		const entity = attrValue(/<class\b[^>]*>/.exec(built.text)[0], 'entity');
		const byEntity = entity && `gameres/icons/${entity.replace(/^.*[\\/]/, '').replace(/(_as)?\.ent\.xml$/i, '')}.bmp`;
		if (icon && !gbr.hasGameres(icon) && byEntity && gbr.hasGameres(byEntity)) built.text = built.text.replace(`icon="${icon}"`, `icon="${byEntity}"`);
		const info = parseClass(built.text, `data/classes/${def.id}.sc.xml`);
		const scripts = [...new Set([...chain.flatMap((c) => c.behaviors.filter(hasI1)), ...methods.flatMap((m) => m.scripts)])];
		// The names it answers to in Imperivm I's command files, and in Great Battles of Rome's through its parent.
		const i1Names = new Set(chain.flatMap((c) => [c.id, c.altid].filter(Boolean).map(lower)));
		const gbrNames = new Set(parentClass ? ancestry(gIndex, parentClass.id).flatMap((c) => [c.id, c.altid].filter(Boolean).map(lower)) : []);
		defs.push({ ...def, scripts, built, info, sigs: new Set(methods.map((m) => m.sig)), i1Names, gbrNames });
		gbr.classes.set(info.id, info);
		for (const m of methods) gbr.commands.add(m.sig);
		report.add(
			section,
			`${def.id}: ${def.fromI1 ? `${src.label}'s ${def.i1} (entity ${info.entity || 'none'})` : `${def.gbr} with ${src.label}'s ${def.i1} and its ancestors`}: parent ${info.parent}; ` +
				`behaviours ${behaviors.length ? behaviors.join(', ') : 'none'}` +
				`${built.droppedBehaviors.length ? ` (not ${built.droppedBehaviors.join(', ')})` : ''}; ` +
				`commands ${methods.map((m) => m.sig).join(', ') || 'none'}` +
				`${built.droppedMethods.length ? ` (not ${built.droppedMethods.join(', ')})` : ''}` +
				`${built.kept.length ? `; ${built.kept.join(', ')}` : ''}`,
		);
	}
	return {
		defs,
		classes: defs.map((d) => d.info),
		byGbr: new Map(defs.filter((d) => !d.fromI1 && !d.auto).map((d) => [lower(d.gbr), d.id])),
		// By the id and the altid the game gives the class (Imperivm II's maps place GTower01 as "Tower01").
		byI1: new Map(
			defs.filter((d) => d.fromI1 || d.auto).flatMap((d) => {
				const c = classIndex(src).get(lower(d.i1));
				return [d.i1, c && c.altid].filter(Boolean).map((n) => [lower(n), d.id]);
			}),
		),
	};
}

// The names (ids and altids, lower case) each new class answers to in Imperivm I's command files: its class and its
// ancestors. Map lower name -> [def].
function i1CommandOwners(plan) {
	const owners = new Map();
	const add = (name, d) => {
		if (!name) return;
		if (!owners.has(lower(name))) owners.set(lower(name), []);
		if (!owners.get(lower(name)).includes(d)) owners.get(lower(name)).push(d);
	};
	for (const d of plan.defs) for (const n of d.i1Names) add(n, d);
	return owners;
}

// The objects each command of Great Battles of Rome is given to (its <src> tags): Map lower name -> Set of lower class names.
function gbrCommandSources(gbr) {
	const out = new Map();
	for (const name of gbr.dataNames(/^DATA\\COMMANDS\\[^\\]+\.XML$/i)) {
		for (const m of live(gbr.readData(name).toString('latin1')).matchAll(/<cmd\b([^>]*)>([\s\S]*?)<\/cmd>/g)) {
			const n = lower(attrValue(m[1], 'name') || '');
			if (!out.has(n)) out.set(n, new Set());
			for (const s of m[2].matchAll(/<src\s+obj\s*=\s*"([^"]*)"/g)) out.get(n).add(lower(s[1]));
		}
	}
	return out;
}

// A command's param with the campaign's class names: the whole value, or each of its comma-separated fields, as a class
// name or as the "levels/<class>" a research sets for the class it raises.
function portParam(value, classLiterals) {
	if (classLiterals[value]) return classLiterals[value];
	return value
		.split(',')
		.map((field) => {
			const t = field.trim();
			const lv = /^levels\/(.+)$/.exec(t);
			const to = classLiterals[t] || (lv && classLiterals[lv[1]] && `levels/${classLiterals[lv[1]]}`);
			return to ? field.replace(t, to) : field;
		})
		.join(',');
}

// Imperivm I's commands of the new classes (buttons, trainings, researches), as a commands file of their own: each <cmd> of
// Imperivm I with a <src> one of them answers to, with only the new classes as sources, its scripts ported, the class it
// trains (param, sclass) the campaign's, and pictures Great Battles of Rome has (pictureFor). Returns {text, scripts: [the
// game's paths], names, count}. opts: {dir: where the ported scripts are, gbrClassOf: the class of Great Battles of Rome a
// class of the campaign is drawn as}.
function portCommands(i1Commands, owners, gbr, classLiterals, rightClick = new Set(), opts = {}) {
	const dir = opts.dir || SCRIPT_DIR;
	const gbrClassOf = opts.gbrClassOf || ((id) => id);
	const gbrCmds = new Map();
	const byTrained = new Map();
	const byRollover = new Map();
	for (const name of gbr.dataNames(/^DATA\\COMMANDS\\[^\\]+\.XML$/i)) {
		for (const m of live(gbr.readData(name).toString('latin1')).matchAll(/<cmd\b([^>]*)>/g)) {
			const n = attrValue(m[1], 'name');
			if (n && !gbrCmds.has(lower(n))) gbrCmds.set(lower(n), m[1]);
			const trained = attrValue(m[1], 'sclass');
			if (trained && !byTrained.has(lower(trained))) byTrained.set(lower(trained), m[1]);
			const rollover = attrValue(m[1], 'rollover');
			if (rollover && !byRollover.has(lower(rollover))) byRollover.set(lower(rollover), m[1]);
		}
	}
	const iconOfClass = (id) => {
		const c = [...gbr.classes.values()].find((x) => lower(x.id) === lower(id));
		return c && c.icon;
	};
	// A picture of the game's command that Great Battles of Rome lacks (attribute `a`: button, under gameres/cmdbar/, or
	// queueicon): its own command's of the same name, the one of the command that trains the same class, the trained
	// class's own icon for a queue, or the one of a command with the same rollover text.
	const pictureFor = (a, value, name, attrs) => {
		const exists = (v) => !!v && gbr.hasGameres(a === 'button' ? `gameres/cmdbar/${v}` : v);
		if (exists(value)) return value;
		const trained = attrValue(` ${attrs}`, 'sclass');
		const gbrTrained = trained && gbrClassOf(trained);
		const rollover = attrValue(` ${attrs}`, 'rollover');
		const candidates = [
			gbrCmds.get(lower(name)) && attrValue(gbrCmds.get(lower(name)), a),
			gbrTrained && byTrained.get(lower(gbrTrained)) && attrValue(byTrained.get(lower(gbrTrained)), a),
			a === 'queueicon' && gbrTrained && iconOfClass(gbrTrained),
			rollover && byRollover.get(lower(rollover)) && attrValue(byRollover.get(lower(rollover)), a),
		];
		return candidates.find(exists) || value;
	};
	const gbrReach = gbrCommandSources(gbr);
	const scripts = new Set();
	const names = [];
	const out = ['<commands>', ''];
	for (const m of i1Commands.matchAll(/<cmd\b([^>]*)>([\s\S]*?)<\/cmd>/g)) {
		const name = attrValue(m[1], 'name');
		const nsrcs = new Set([...m[2].matchAll(/<nsrc\s+obj\s*=\s*"([^"]*)"/g)].map((x) => lower(x[1])));
		// A full copy keeps Great Battles of Rome's command of the same name when its parent already gives it that one: the
		// command runs the class's method, which is Imperivm I's.
		const theirs = gbrReach.get(lower(name || '')) || new Set();
		const srcs = [];
		for (const s of m[2].matchAll(/<src\s+obj\s*=\s*"([^"]*)"([^>]*?)\s*\/>/g)) {
			for (const d of owners.get(lower(s[1])) || []) {
				if (srcs.some((x) => x.id === d.id)) continue;
				if (d.i1Names && [...d.i1Names].some((n) => nsrcs.has(n))) continue;
				if (d.gbrNames && [...d.gbrNames].some((n) => theirs.has(n))) continue;
				srcs.push({ id: d.id, rest: s[2] });
			}
		}
		// A command with no source that a new class's right-click gives (help-sacrifice: join a ritual) and the game lacks.
		if (!srcs.length && !(rightClick.has(lower(name || '')) && !gbrCmds.has(lower(name || '')))) continue;
		let attrs = m[1].replace(/\s+/g, ' ').trim();
		for (const p of tagScripts(` ${attrs}`)) {
			scripts.add(p);
			attrs = attrs.split(`"${p}"`).join(`"${portedPath(p, dir)}"`);
		}
		for (const a of ['param', 'sclass']) {
			const v = attrValue(` ${attrs}`, a);
			if (v) attrs = attrs.replace(new RegExp(`(\\b${a}\\s*=\\s*")[^"]*(")`), `$1${portParam(v, classLiterals)}$2`);
		}
		for (const a of ['button', 'queueicon']) {
			const v = attrValue(` ${attrs}`, a);
			const to = v && pictureFor(a, v, name, attrs);
			if (to && to !== v) attrs = attrs.replace(new RegExp(`(\\b${a}\\s*=\\s*")[^"]*(")`), `$1${to}$2`);
		}
		const body = m[2]
			.replace(/[ \t]*<n?src\b[^>]*\/>[ \t]*(\r?\n)?/g, '')
			.replace(/\s+$/, '');
		out.push(`<cmd ${attrs}>${body}`, ...srcs.map((s) => `\t<src obj="${s.id}"${s.rest}/>`), '</cmd>', '');
		names.push(name);
	}
	out.push('</commands>', '');
	return { text: out.join('\r\n'), scripts: [...scripts], names, count: names.length };
}

// The texts of commands the player sees (rollover, description, param, cmdtext) that the game's Spanish tables do not
// translate, with Imperivm I's Spanish, as a loc file of the mod (the game shows an untranslated text in English).
// Returns {text, count} or null.
function spanishTexts(commandsText, gbr, src) {
	const keysOf = (buf) => {
		const keys = new Set();
		if (!buf) return keys;
		for (const m of buf.toString('utf8').matchAll(/<translationtableentry\b[^>]*?\stext="([^"]*)"/g)) keys.add(m[1].replace(/@.*$/, ''));
		return keys;
	};
	const gbrKeys = new Set([...keysOf(gbr.readLocal('spanish', 'CURRENTLANG/GAME.LOC.XML')), ...keysOf(gbr.readLocal('spanish', 'CURRENTLANG/TRANSLATION.LOC.XML'))]);
	const i1Es = src.spanish;
	const wanted = new Set();
	for (const m of commandsText.matchAll(/\s(rollover|description|param|text|default-text)\s*=\s*"([^"]+)"/g)) wanted.add(m[2]);
	const entries = [...wanted].filter((k) => !gbrKeys.has(k) && i1Es.has(k) && i1Es.get(k) !== k).sort();
	if (!entries.length) return null;
	const lines = ['<?xml version="1.0" encoding="utf-8"?>', '<translationtable>', ...entries.map((k) => `\t<translationtableentry text="${k}" result="${i1Es.get(k)}"/>`), '</translationtable>', ''];
	return { text: lines.join('\r\n'), count: entries.length };
}

// The files of the classes for the mod, after the conversion (classLiterals: Imperivm I class -> class of the campaign).
// Returns Map mod-relative path -> Buffer.
function i1ClassFiles(plan, gbr, src, classLiterals, report, aiProfile) {
	const files = new Map();
	const dir = scriptDir(src);
	const section = `${src.label} classes`;
	const commandsFile = `data/commands/${src.dir}.xml`;
	const constants = readConstants(src.constants);
	const i1Commands = live(src.commands);
	// The commands the new classes' right-click names (<defaultcmd><cmd name="..."/>).
	const rightClick = new Set();
	for (const d of plan.defs) {
		for (const b of d.built.text.matchAll(/<defaultcmd\b[\s\S]*?<\/defaultcmd>/g)) {
			for (const c of b[0].matchAll(/<cmd\s+name\s*=\s*"([^"]+)"/g)) rightClick.add(lower(c[1]));
		}
	}
	const gbrOf = new Map(plan.defs.filter((d) => d.gbr).map((d) => [lower(d.id), d.gbr]));
	const commands = portCommands(i1Commands, i1CommandOwners(plan), gbr, classLiterals, rightClick, { dir, gbrClassOf: (id) => gbrOf.get(lower(id)) || id });
	if (commands.count) {
		for (const n of commands.names) gbr.commands.add(lower(n));
		files.set(commandsFile, Buffer.from(commands.text, 'latin1'));
		report.add(section, `${commandsFile}: ${src.label}'s ${commands.count} commands of the new classes (${commands.names.join(', ')})`);
		const es = spanishTexts(commands.text, gbr, src);
		if (es) {
			files.set('loc/spanish.loc.xml', Buffer.from(es.text, 'utf8'));
			report.add(section, `loc/spanish.loc.xml: ${es.count} texts of those commands the game does not translate, in ${src.label}'s Spanish`);
		}
	}
	// Imperivm I classes that are only the parent of others and that Great Battles of Rome lacks, with their children.
	const gbrIds = new Set([...gbr.classes.values()].map((c) => lower(c.id)));
	const classSets = {};
	const specialities = specialityClasses(plan);
	for (const c of new Set(src.classes.values())) {
		if (gbrIds.has(lower(c.id)) || classLiterals[c.id]) continue;
		const kids = [...new Set(src.classes.values())].filter((k) => k.parent && lower(k.parent) === lower(c.id)).map((k) => classLiterals[k.id] || k.id);
		if (kids.length && kids.every((k) => gbrIds.has(lower(k)))) classSets[c.id] = kids;
	}
	for (const d of plan.defs) files.set(`data/classes/${d.id}.sc.xml`, Buffer.from(d.built.text, 'latin1'));
	for (const s of [...new Set([...plan.defs.flatMap((d) => d.scripts), ...commands.scripts])]) {
		const to = portedPath(s, dir);
		const buf = src.read(s);
		if (!buf) throw new Error(`${section}: ${src.label} has no ${s}`);
		const p = portScript(buf.toString('latin1'), classLiterals, constants, classSets, src.label, specialities);
		files.set(to, Buffer.from(p.src, 'latin1'));
		report.add(section, `${to}: ${src.label}'s ${s}${p.inlined ? `, ${p.inlined} constants of ${src.label}` : ''}`);
	}
	// The game's commands a full copy would get through its parent and Imperivm I did not give that class (autotrain,
	// healing, divine grace, charms, slaves...): excluded by class name (<nsrc obj="I1GTownhall"/>). Its buttons are Great
	// Battles of Rome's commands Imperivm I also had, and Imperivm I's own in data/commands/imperivm1.xml.
	const i1Reach = new Map();
	for (const m of i1Commands.matchAll(/<cmd\b([^>]*)>([\s\S]*?)<\/cmd>/g)) {
		const n = lower(attrValue(m[1], 'name') || '');
		const src = new Set([...m[2].matchAll(/<src\s+obj\s*=\s*"([^"]*)"/g)].map((x) => lower(x[1])));
		const nsrc = new Set([...m[2].matchAll(/<nsrc\s+obj\s*=\s*"([^"]*)"/g)].map((x) => lower(x[1])));
		i1Reach.set(n, { src, nsrc });
	}
	const i1Gives = (cmd, d) => {
		const r = i1Reach.get(lower(cmd));
		return !!r && [...d.i1Names].some((n) => r.src.has(n)) && ![...d.i1Names].some((n) => r.nsrc.has(n));
	};
	const excluded = new Map();
	for (const name of gbr.dataNames(/^DATA\\COMMANDS\\[^\\]+\.XML$/i)) {
		const t = live(gbr.readData(name).toString('latin1'));
		const adds = [];
		for (const m of t.matchAll(/<cmd\b([^>]*)>([\s\S]*?)<\/cmd>/g)) {
			const cmd = attrValue(m[1], 'name');
			if (!cmd || /'/.test(cmd)) continue;
			const src = [...m[2].matchAll(/<src\s+obj\s*=\s*"([^"]*)"/g)].map((x) => lower(x[1]));
			const method = lower(attrValue(m[1], 'method') || cmd);
			for (const d of plan.defs) {
				if (!d.gbrNames || d.fromI1 || !src.some((s) => d.gbrNames.has(s)) || i1Gives(cmd, d)) continue;
				// What Imperivm I's class can do (a method of its own, or a right-click command) stays.
				if (d.sigs.has(method) || new RegExp(`<cmd\\s+name\\s*=\\s*"${cmd.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}"`, 'i').test(d.built.text.replace(/<cmd\b[^>]*\bmethod\b[^>]*>/g, ''))) continue;
				adds.push(`\t<add select="//cmd[@name='${cmd}']"><nsrc obj="${d.id}"/></add>`);
				if (!excluded.has(cmd)) excluded.set(cmd, 0);
				excluded.set(cmd, excluded.get(cmd) + 1);
			}
		}
		if (!adds.length) continue;
		const rel = 'patches/' + name.replace(/\\/g, '/').toLowerCase();
		files.set(rel, Buffer.from(['<patch>', ...adds, '</patch>', ''].join('\r\n'), 'latin1'));
		report.add(section, `${rel}: ${adds.length} exclusions by class name given to the new classes`);
	}
	if (excluded.size) report.add(section, `Great Battles of Rome's commands ${src.label} did not have, taken from the new classes: ${[...excluded].map(([c, n]) => `${c} (${n})`).join(', ')}`);
	// Imperivm I's AI, as profiles of the data mod: the game looks for "<PROFILE>/<script>.vs" in data/ai/ and then for
	// "<script>.vs" (gbr.exe ResolveAIScriptPath 0043d030), so each profile holds all of Imperivm I's scripts: its root ones
	// (data/ai/I1/) and, for another profile of Imperivm I (defensive, _teuton), those with its own on top. Imperivm I's
	// Main.vs does not start the game's TacticMonitor.vs (tactics, hero skills, counter-units), which Imperivm I had not.
	const base = src.ai.get('') || [];
	let aiCount = 0;
	for (const [profile, own] of src.ai) {
		const aiDir = `data/ai/${lower(aiProfile + profile)}/`;
		const byName = new Map(base.map((f) => [f.name, f]));
		for (const f of own) byName.set(f.name, f);
		for (const [name, f] of byName) {
			let text = f.read().toString('latin1');
			if (/\.vs$/.test(name)) {
				text = portScript(text, classLiterals, constants, classSets, src.label, specialities).src;
				text = S.aiScriptFixes(text, name).src;
			} else {
				for (const [from, to] of Object.entries(classLiterals)) text = text.replace(new RegExp(`\\b${from}\\b`, 'g'), to);
				text = S.aiScriptFixes(text, name).src;
			}
			files.set(aiDir + name, Buffer.from(text, 'latin1'));
			aiCount++;
		}
	}
	if (aiCount) report.add(section, `data/ai/${lower(aiProfile)}*/: ${src.label}'s AI, ${src.ai.size} profiles, ${aiCount} files`);
	// Imperivm I's combat values, which Great Battles of Rome's classes cannot hold: the mod's DLL reads them to fight as
	// Imperivm I did (imperivm-re/analisis/i1-vs-gbr/combate.md).
	const stats = plan.defs.filter((d) => d.built.stats && Object.keys(d.built.stats).length);
	if (stats.length) {
		const lines = [`; ${src.label} combat values of the campaign classes, written by its converter. Read by the mod.`, ''];
		// The game's [GamePlay] constants of its combat, levels and ghosts (VXCONST.INI; Imperivm II's CONST.INI adds its own).
		const gameplay = ['ExpA', 'ExpB', 'ExpT', 'DeathBlowPerc', 'BerserkPerc', 'SpreadPerc', 'ChargeTime', 'ChargeBonusPerc', 'SpikePerc', 'TramplePerc', 'VampireBlowPerc', 'SacraficeBlowPerc', 'HealthPerLevel', 'HeroHealthPerLevel', 'ExpFromArmyDivider', 'GhostMinHealthPercent', 'LevelExpDivider', 'RagePerc', 'BleedingPerc', 'FullArmor', 'MinDamage', 'CoverOfMercy'];
		lines.push('[GamePlay]', ...gameplay.filter((k) => constants.has(lower(k))).map((k) => `${k}=${constants.get(lower(k))}`), '');
		// Imperivm I's bonus.xml: the percent of each hit of an attacker class against a kind of target (Catapult against
		// Building, 600), given to the classes that derive from the attacker.
		const bonuses = [...live(src.bonus).matchAll(/<bonus\b([^>]*)\/?>/g)].map((m) => ({
			attacker: lower(attrValue(m[1], 'attacker') || ''),
			defender: lower(attrValue(m[1], 'defender') || ''),
			bonus: attrValue(m[1], 'bonus'),
		}));
		for (const d of stats) {
			const own = bonuses.filter((b) => d.i1Names && d.i1Names.has(b.attacker)).map((b) => `bonus_${b.defender}=${b.bonus}`);
			lines.push(`[${d.id}]`, ...Object.entries(d.built.stats).map(([k, v]) => `${k}=${v}`), ...own, '');
		}
		files.set(dir + 'stats.ini', Buffer.from(lines.join('\r\n'), 'latin1'));
		report.add(section, `${dir}stats.ini: ${src.label}'s combat values of ${stats.length} classes`);
	}
	return files;
}

module.exports = { planI1Classes, specialityClasses, i1ClassFiles, buildFromI1Text, buildFullText, portParam, i1Methods, portScript, portCommands, readConstants, SCRIPT_DIR };
