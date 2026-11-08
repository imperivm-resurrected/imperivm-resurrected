// Diagnostics for testing the converted campaign in the game (convert.js --probe). They write "I1PROBE ..." lines to
// Logs\imperivm\imperivm-mod.log through Imperivm Resurrected's Mod_Log, so they need the mod. They only observe: no
// command is given that the campaign does not give.
//
// Kormaris (map 11937):
//   seq0, right after Larax is looked up, before any command: Larax's state and one Teuton's ("seq0 start").
//   seq0, right after his "hold_position": the same ("seq0 after hold_position").
//   seq0, around the goddess conversation: the time before and after C_Conv.Run(), to see whether it was played.
//   seq15, after the Teutons are sent at him, every 2 s for 30 s: Larax, his best target, each Teuton and the diplomacy,
//   and the target searches of Larax, up to 3 other units of his player, up to 3 Gaul allies and one Teuton.
//   seq18, after the prisoners and guards are spawned into S_Prizon, every second for 30 s: the prison and its prisoners.
'use strict';

const S = require('./script');

const DECLARATIONS = 'ObjList i1p_v, i1p_w;\nObj i1p_o, i1p_b, i1p_c;\nint i1p_k, i1p_i, i1p_p, i1p_j, i1p_n;\nstr i1p_s, i1p_t;\nSettlement i1p_h;\n';

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
		`i1p_s = "I1PROBE ${tag} t=" + GetTime() + " larax player=" + ${l}.player + " health=" + ${l}.health + " command=" + ${l}.command + " x=" + ${l}.pos.x + " y=" + ${l}.pos.y + " rh_x=" + ${l}.posRH.x + " rh_y=" + ${l}.posRH.y;`,
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
		`i1p_s = "I1PROBE ${tag} class=" + ${o}.class() + " player=" + ${o}.player + " health=" + ${o}.health + " command=" + ${o}.command + " dist=" + ${o}.DistTo(${l}) + " x=" + ${o}.pos.x + " y=" + ${o}.pos.y + " rh_x=" + ${o}.posRH.x + " rh_y=" + ${o}.posRH.y + " radius=" + ${o}.radius + " range=" + ${o}.range + " min_range=" + ${o}.min_range;`,
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

// The combat numbers of one object `o` (an Obj expression), in i1p_t, and the groups of the opening it belongs to.
const statsLine = (tag, o) => [
	`i1p_t = "I1PROBE ${tag} class=" + ${o}.class() + " name=" + ${o}.name + " player=" + ${o}.player + " attack=" + ${o}.attack + " damage=" + ${o}.damage + " health=" + ${o}.health + " maxhealth=" + ${o}.maxhealth + " stamina=" + ${o}.stamina + " x=" + ${o}.pos.x + " y=" + ${o}.pos.y + " rh_x=" + ${o}.posRH.x + " rh_y=" + ${o}.posRH.y + " radius=" + ${o}.radius + " range=" + ${o}.range + " min_range=" + ${o}.min_range;`,
	`if (${o}.IsInGroup("M1_Q_Victims1")) i1p_t = i1p_t + " in_victims1=1"; else i1p_t = i1p_t + " in_victims1=0";`,
	`if (${o}.IsInGroup("M1_Q_StandingGround")) i1p_t = i1p_t + " in_standing_ground=1"; else i1p_t = i1p_t + " in_standing_ground=0";`,
	'Mod_Log(i1p_t);',
];

// The target searches of one unit `o` (an Obj expression): which of them find a target, and its command. The target
// BestTargetForPos finds gets a stats line of its own.
const targetLine = (tag, o) => [
	`i1p_s = "I1PROBE ${tag} class=" + ${o}.class() + " player=" + ${o}.player + " command=" + ${o}.command + " dist_to_larax=" + ${o}.DistTo(NO_Larax.obj);`,
	`i1p_b = ${o}.AsUnit().BestTargetForPos();`,
	'i1p_c = i1p_b;',
	'if (i1p_b.IsValid()) i1p_s = i1p_s + " forpos=" + i1p_b.class() + "/" + i1p_b.player; else i1p_s = i1p_s + " forpos=none";',
	`i1p_b = ${o}.AsUnit().BestTargetInSquadSight();`,
	'if (i1p_b.IsValid()) i1p_s = i1p_s + " squadsight=" + i1p_b.class() + "/" + i1p_b.player; else i1p_s = i1p_s + " squadsight=none";',
	`i1p_b = ${o}.AsUnit().BestTargetInSquadSight_PreferUndiseased();`,
	'if (i1p_b.IsValid()) i1p_s = i1p_s + " undiseased=" + i1p_b.class() + "/" + i1p_b.player; else i1p_s = i1p_s + " undiseased=none";',
	flag('enemy_in_squad_sight', `${o}.AsUnit().IsEnemyInSquadSight()`),
	flag('enemies_in_sight', `${o}.AsUnit().EnemiesInSight()`),
	'Mod_Log(i1p_s);',
	'if (i1p_c.IsValid()) {',
	...statsLine(`${tag} forpos target`, 'i1p_c').map((l) => '\t' + l),
	'}',
];

// The target searches of Larax, of up to 3 other units of his player, of up to 3 units of player 2 (his Gaul allies) in
// M1_Q_StandingGround and of the first Teuton of M1_Q_Victims1.
const targetLines = (tag) => [
	...targetLine(`${tag} target larax`, 'NO_Larax.obj'),
	'i1p_w = GetPlayerUnits(NO_Larax.obj.player);',
	'i1p_n = 0;',
	'for (i1p_j = 0; i1p_j < i1p_w.count && i1p_n < 3; i1p_j += 1) {',
	'\tif (i1p_w[i1p_j].IsValid() && i1p_w[i1p_j].IsAlive() && i1p_w[i1p_j].class() != "Larax") {',
	...indent(targetLine(`${tag} target own unit`, 'i1p_w[i1p_j]'), 2),
	'\t\ti1p_n += 1;',
	'\t}',
	'}',
	'i1p_w = M1_Q_StandingGround.GetObjList();',
	'i1p_n = 0;',
	'for (i1p_j = 0; i1p_j < i1p_w.count && i1p_n < 3; i1p_j += 1) {',
	'\tif (i1p_w[i1p_j].IsValid() && i1p_w[i1p_j].IsAlive() && i1p_w[i1p_j].player == 2) {',
	...indent(targetLine(`${tag} target ally unit`, 'i1p_w[i1p_j]'), 2),
	...indent(statsLine(`${tag} stats ally unit`, 'i1p_w[i1p_j]'), 2),
	'\t\ti1p_n += 1;',
	'\t}',
	'}',
	'i1p_w = M1_Q_Victims1.GetObjList();',
	'if (i1p_w.count > 0) {',
	'\tif (i1p_w[0].IsValid() && i1p_w[0].IsAlive()) {',
	...indent(targetLine(`${tag} target teuton`, 'i1p_w[0]'), 2),
	'\t}',
	'}',
];

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
		...indent(statsLine('stats larax', 'NO_Larax.obj'), 1),
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
		...indent(statsLine('stats victims1 rider', 'i1p_o'), 3),
		'\t\t} else Mod_LogInt("I1PROBE teuton invalid", i1p_i);',
		'\t}',
		...indent(targetLines('seq15'), 1),
		'\tSleep(2000);',
		'}',
		'Mod_Log("I1PROBE seq15 end");',
	];
}

// seq18 (the prison): every second for 30 s after the prisoners and their guards are spawned into S_Prizon, the two
// groups, the settlement and each prisoner.
function prisonLoop() {
	const p = 'i1p_w[i1p_j]';
	return [
		'// I1PROBE: diagnostics added by convert.js --probe',
		'for (i1p_k = 0; i1p_k < 30; i1p_k += 1) {',
		'\ti1p_h = GetSettlement("S_Prizon");',
		'\ti1p_s = "I1PROBE prison t=" + GetTime() + " prisoners=" + M1_Q_Prisoners.count + " guards=" + M1_Q_PrisonGuards.count;',
		'\tif (i1p_h.IsValid()) i1p_s = i1p_s + " sett_player=" + i1p_h.player + " food=" + i1p_h.food + " gold=" + i1p_h.gold + " population=" + i1p_h.population + " max_population=" + i1p_h.max_population + " units=" + i1p_h.UnitsCount() + " loyalty=" + i1p_h.loyalty; else i1p_s = i1p_s + " sett=invalid";',
		`\t${flag('independent', 'i1p_h.IsIndependent()')}`,
		'\tMod_Log(i1p_s);',
		'\ti1p_w = M1_Q_Prisoners.GetObjList();',
		'\tfor (i1p_j = 0; i1p_j < i1p_w.count; i1p_j += 1) {',
		`\t\ti1p_s = "I1PROBE prisoner " + i1p_j + " class=" + ${p}.class() + " player=" + ${p}.player + " health=" + ${p}.health + " command=" + ${p}.command + " x=" + ${p}.pos.x + " y=" + ${p}.pos.y;`,
		...indent(holder(p), 2),
		`\t\t${flag('alive', `${p}.IsAlive()`)}`,
		`\t\t${flag('visible', `${p}.IsVisible()`)}`,
		'\t\tMod_Log(i1p_s);',
		'\t}',
		'\tSleep(1000);',
		'}',
		'Mod_Log("I1PROBE prison end");',
	];
}

// Where each probe goes: [file, anchor, 'before' | 'after' the anchor's line, lines].
const PROBES = [
	['Maps/11937/Sequences/seq0.vs', /^NO_Larax\s*=\s*GetNamedObj\("Larax"\);/m, 'after', [...laraxLine('seq0 start'), ...firstTeuton('seq0 start teuton'), ...targetLines('seq0 start')]],
	['Maps/11937/Sequences/seq0.vs', /^NO_Larax\.SetCommand\("hold_position"\);/m, 'after', laraxLine('seq0 after hold_position')],
	['Maps/11937/Sequences/seq0.vs', /^C_Conv\.Init\("TheGodess"\);/m, 'before', ['Mod_LogInt("I1PROBE seq0 goddess conversation starts, t", GetTime());']],
	['Maps/11937/Sequences/seq0.vs', /^C_Conv\.Run\(\);/m, 'after', ['Mod_LogInt("I1PROBE seq0 goddess conversation ended, t", GetTime());', ...laraxLine('seq0 after goddess')]],
	['Maps/11937/Sequences/seq18.vs', /^SpawnGroupInHolder\("M1_Q_PrisonGuards", "S_Prizon"\);/m, 'after', prisonLoop()],
	['Maps/11937/Sequences/seq15.vs', /^C_Conv\.Init\("OpeningConversation"\);/m, 'before', ['Mod_LogInt("I1PROBE seq15 opening conversation starts, t", GetTime());']],
	['Maps/11937/Sequences/seq15.vs', /^C_Conv\.Run\(\);/m, 'after', ['Mod_LogInt("I1PROBE seq15 opening conversation ended, t", GetTime());']],
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

// A sequence added to every map that lists what is in the world: one "I1PROBE census" line per unit and building of every
// player (gates say whether they are open), and one per player with its cease-fire towards player 1 and back, 3 s, 60 s
// and 5 min after the map starts. audit.compareCensus reads them back.
function censusSequence() {
	return [
		'//void',
		'ObjList i1c_l;',
		'Obj i1c_o;',
		'int i1c_r, i1c_p, i1c_i, i1c_k;',
		'str i1c_s, i1c_kind;',
		'',
		'for (i1c_r = 0; i1c_r < 3; i1c_r += 1)',
		'{',
		'\tif (i1c_r == 0) Sleep(3000);',
		'\tif (i1c_r == 1) Sleep(57000);',
		'\tif (i1c_r == 2) Sleep(240000);',
		'\tMod_Log("I1PROBE census begin map=" + MapName() + " round=" + i1c_r + " t=" + GetTime());',
		'\tfor (i1c_k = 0; i1c_k < 2; i1c_k += 1)',
		'\t{',
		'\t\tif (i1c_k == 0) i1c_kind = "Unit"; else i1c_kind = "Building";',
		'\t\tfor (i1c_p = 0; i1c_p <= 16; i1c_p += 1)',
		'\t\t{',
		'\t\t\ti1c_l = ClassPlayerObjs(i1c_kind, i1c_p).GetObjList();',
		'\t\t\tfor (i1c_i = 0; i1c_i < i1c_l.count; i1c_i += 1)',
		'\t\t\t{',
		'\t\t\t\ti1c_o = i1c_l[i1c_i];',
		'\t\t\t\ti1c_s = "I1PROBE census map=" + MapName() + " round=" + i1c_r + " player=" + i1c_p + " class=" + i1c_o.class() + " kind=" + i1c_kind + " x=" + i1c_o.pos.x + " y=" + i1c_o.pos.y + " health=" + i1c_o.health;',
		'\t\t\t\tif (i1c_k == 0) { if (i1c_o.AsUnit().InHolder()) i1c_s = i1c_s + " inholder=1"; else i1c_s = i1c_s + " inholder=0"; }',
		'\t\t\t\tif (i1c_k == 1) { if (i1c_o.AsGate().IsValid()) { if (i1c_o.AsGate().IsOpened()) i1c_s = i1c_s + " gate=open"; else i1c_s = i1c_s + " gate=closed"; } }',
		'\t\t\t\tMod_Log(i1c_s);',
		'\t\t\t}',
		'\t\t}',
		'\t}',
		'\tfor (i1c_p = 0; i1c_p <= 16; i1c_p += 1)',
		'\t{',
		'\t\ti1c_s = "I1PROBE census dipl map=" + MapName() + " round=" + i1c_r + " player=" + i1c_p;',
		'\t\tif (DiplGetCeaseFire(i1c_p, 1)) i1c_s = i1c_s + " ceasefire_to_1=1"; else i1c_s = i1c_s + " ceasefire_to_1=0";',
		'\t\tif (DiplGetCeaseFire(1, i1c_p)) i1c_s = i1c_s + " ceasefire_from_1=1"; else i1c_s = i1c_s + " ceasefire_from_1=0";',
		'\t\tMod_Log(i1c_s);',
		'\t}',
		'\tMod_Log("I1PROBE census end map=" + MapName() + " round=" + i1c_r);',
		'}',
		'',
	].join('\r\n');
}

module.exports = { addProbe, censusSequence };
