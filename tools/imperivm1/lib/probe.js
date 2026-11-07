// Diagnostics for testing the converted campaign in the game (convert.js --probe). They write "I1PROBE ..." lines to
// Logs\imperivm\imperivm-mod.log through Imperivm Resurrected's Mod_Log, so they need the mod. They only observe: no
// command is given that the campaign does not give. The probe build also leaves player0.xml (the start player) with
// Imperivm I's control="Both" (see adventure.js).
//
// Kormaris (map 11937):
//   seq0, right after Larax is looked up, before any command: Larax's state and one Teuton's ("seq0 start").
//   seq0, right after his "hold_position": the same ("seq0 after hold_position").
//   seq0, around the goddess conversation: the time before and after C_Conv.Run(), to see whether it was played.
//   seq15, after the Teutons are sent at him, every 2 s for 30 s: Larax, his best target, each Teuton and the diplomacy.
'use strict';

const S = require('./script');

const DECLARATIONS = 'ObjList i1p_v;\nObj i1p_o, i1p_b;\nint i1p_k, i1p_i, i1p_p;\nstr i1p_s;\nSettlement i1p_h;\n';

// "if (cond) s = s + ' name=1'; else s = s + ' name=0';" since a bool does not convert to a string.
const flag = (name, cond) => `if (${cond}) i1p_s = i1p_s + " ${name}=1"; else i1p_s = i1p_s + " ${name}=0";`;

// Appends to i1p_s whether the unit `u` (an Obj expression) is in a holder, and the holder's settlement.
const holder = (u) => [
	flag('inholder', `${u}.AsUnit().InHolder()`),
	`if (${u}.AsUnit().InHolder()) {`,
	`\ti1p_h = ${u}.AsUnit().GetHolderSett();`,
	`\tif (i1p_h.IsValid()) i1p_s = i1p_s + " holder_sett=" + i1p_h.name + " holder_sett_player=" + i1p_h.player; else i1p_s = i1p_s + " holder_sett=invalid";`,
	'}',
];

// One line about Larax: tag, time, player, health, command, position, holder, alive, path, visible, AI.
const laraxLine = (tag) => {
	const l = 'NO_Larax.obj';
	return [
		`i1p_s = "I1PROBE ${tag} t=" + GetTime() + " larax player=" + ${l}.player + " health=" + ${l}.health + " command=" + ${l}.command + " x=" + ${l}.pos.x + " y=" + ${l}.pos.y;`,
		...holder(l),
		flag('alive', `${l}.IsAlive()`),
		flag('haspath', `${l}.AsUnit().HasPath()`),
		flag('visible', `${l}.IsVisible()`),
		flag('ai', `IsAIPlayer(${l}.player)`),
		`i1p_s = i1p_s + " sight=" + ${l}.sight + " units_in_sight=" + ObjsInSight(${l}, "Unit").count + " visible_units_in_sight=" + VisibleObjsInSight(${l}, "Unit").count;`,
		flag('enemies_in_sight', `${l}.AsUnit().EnemiesInSight()`),
		'Mod_Log(i1p_s);',
	];
};

// One line about a Teuton `o` (an Obj variable): position, holder, and whether Larax's player sees it.
const teutonLine = (tag, o) => {
	const l = 'NO_Larax.obj';
	return [
		`i1p_s = "I1PROBE ${tag} class=" + ${o}.class() + " player=" + ${o}.player + " health=" + ${o}.health + " command=" + ${o}.command + " dist=" + ${o}.DistTo(${l}) + " x=" + ${o}.pos.x + " y=" + ${o}.pos.y;`,
		...holder(o),
		flag('alive', `${o}.IsAlive()`),
		flag('visible', `${o}.IsVisible()`),
		flag('explored_p', `IsExplored(${o}.pos, ${l}.player)`),
		flag('explored_p_minus_1', `IsExplored(${o}.pos, ${l}.player - 1)`),
		flag('enemy_of_larax', `${o}.IsEnemy(${l})`),
		flag('valid_target_for_larax', `${l}.IsValidTarget(${o})`),
		`i1p_s = i1p_s + " sight=" + ${o}.sight + " units_in_its_sight=" + ObjsInSight(${o}, "Unit").count + " visible_units_in_its_sight=" + VisibleObjsInSight(${o}, "Unit").count;`,
		'Mod_Log(i1p_s);',
	];
};

// At the start of seq0: the first Teuton of M1_Q_Victims1.
const firstTeuton = (tag) => [
	'i1p_v = M1_Q_Victims1.GetObjList();',
	'if (i1p_v.count > 0) {',
	'\ti1p_o = i1p_v[0];',
	...indent(teutonLine(tag, 'i1p_o'), 1),
	'}',
];

const indent = (lines, n) => lines.map((l) => '\t'.repeat(n) + l);

function seq15Loop() {
	const l = 'NO_Larax.obj';
	return [
		'// I1PROBE: diagnostics added by convert.js --probe',
		'i1p_v = M1_Q_Victims1.GetObjList();',
		'Mod_LogInt("I1PROBE seq15 start, Teutons in M1_Q_Victims1", i1p_v.count);',
		'for (i1p_k = 0; i1p_k < 15; i1p_k += 1) {',
		...indent(laraxLine('seq15'), 1),
		`\ti1p_b = ${l}.AsUnit().BestTargetForPos();`,
		'\ti1p_s = "I1PROBE best target";',
		`\t${flag('valid', 'i1p_b.IsValid()')}`,
		'\tif (i1p_b.IsValid()) i1p_s = i1p_s + " player=" + i1p_b.player + " class=" + i1p_b.class();',
		'\tMod_Log(i1p_s);',
		`\ti1p_p = ${l}.player;`,
		'\ti1p_s = "I1PROBE diplomacy larax-4";',
		`\t${flag('ceasefire', 'DiplGetCeaseFire(i1p_p, 4)')}`,
		`\t${flag('ceasefire_back', 'DiplGetCeaseFire(4, i1p_p)')}`,
		`\t${flag('allied', 'DiplAreAllied(i1p_p, 4)')}`,
		`\t${flag('sharecontrol', 'DiplGetShareControl(i1p_p, 4)')}`,
		`\t${flag('ai4', 'IsAIPlayer(4)')}`,
		'\tMod_Log(i1p_s);',
		'\tfor (i1p_i = 0; i1p_i < i1p_v.count; i1p_i += 1) {',
		'\t\ti1p_o = i1p_v[i1p_i];',
		'\t\tif (i1p_o.IsValid()) {',
		...indent(teutonLine('teuton', 'i1p_o'), 3),
		'\t\t} else Mod_LogInt("I1PROBE teuton invalid", i1p_i);',
		'\t}',
		'\tSleep(2000);',
		'}',
		'Mod_Log("I1PROBE seq15 end");',
	];
}

// Where each probe goes: [file, anchor, 'before' | 'after' the anchor's line, lines].
const PROBES = [
	['Maps/11937/Sequences/seq0.vs', /^NO_Larax\s*=\s*GetNamedObj\("Larax"\);/m, 'after', [...laraxLine('seq0 start'), ...firstTeuton('seq0 start teuton')]],
	['Maps/11937/Sequences/seq0.vs', /^NO_Larax\.SetCommand\("hold_position"\);/m, 'after', laraxLine('seq0 after hold_position')],
	['Maps/11937/Sequences/seq0.vs', /^C_Conv\.Init\("TheGodess"\);/m, 'before', ['Mod_LogInt("I1PROBE seq0 goddess conversation starts, t", GetTime());']],
	['Maps/11937/Sequences/seq0.vs', /^C_Conv\.Run\(\);/m, 'after', ['Mod_LogInt("I1PROBE seq0 goddess conversation ended, t", GetTime());', ...laraxLine('seq0 after goddess')]],
	['Maps/11937/Sequences/seq15.vs', /^WaitEmptyQuery\s*\(\s*M1_Q_Victims1/m, 'before', seq15Loop()],
];

// Adds the probes of `path` to its script. Returns the new source, or null when the probe has nothing for this file.
function addProbe(path, src) {
	const mine = PROBES.filter(([file]) => file.toLowerCase() === path.toLowerCase());
	if (!mine.length) return null;
	let out = src;
	for (const [, anchor, where, lines] of mine) {
		const m = anchor.exec(out);
		if (!m) throw new Error(`${path}: the place for a probe was not found (${anchor})`);
		const lineStart = out.lastIndexOf('\n', m.index) + 1;
		const lineEnd = out.indexOf('\n', m.index) + 1 || out.length;
		const at = where === 'before' ? lineStart : lineEnd;
		out = out.slice(0, at) + lines.join('\r\n') + '\r\n' + out.slice(at);
	}
	return S.insertDeclarations(out, DECLARATIONS);
}

module.exports = { addProbe };
