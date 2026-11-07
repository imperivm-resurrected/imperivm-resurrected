// Diagnostics for testing the converted campaign in the game (convert.js --probe). They write "I1PROBE ..." lines to
// Logs\imperivm\imperivm-mod.log through Imperivm Resurrected's Mod_Log, so they need the mod.
//
// The Kormaris opening (map 11937, seq15): three Teutons are sent at Larax and the script waits for them to die. Every 2
// seconds for 30 seconds the probe logs Larax, his best target, each of the Teutons and the diplomacy between their
// players; after 15 seconds it orders Larax to attack the first Teuton still alive.
'use strict';

const S = require('./script');

// "if (cond) s = s + ' name=1'; else s = s + ' name=0';" for a bool, since a bool does not convert to a string.
const flag = (name, cond) => `if (${cond}) i1p_s = i1p_s + " ${name}=1"; else i1p_s = i1p_s + " ${name}=0";`;

function kormarisProbe() {
	const larax = 'NO_Larax.obj';
	const lines = [
		'// I1PROBE: diagnostics added by convert.js --probe',
		'i1p_v = M1_Q_Victims1.GetObjList();',
		'Mod_LogInt("I1PROBE start, Teutons in M1_Q_Victims1", i1p_v.count);',
		'for (i1p_k = 0; i1p_k < 15; i1p_k += 1) {',
		`\ti1p_p = ${larax}.player;`,
		`\ti1p_s = "I1PROBE t=" + GetTime() + " larax player=" + i1p_p + " health=" + ${larax}.health + " command=" + ${larax}.command;`,
		`\t${flag('alive', `${larax}.IsAlive()`)}`,
		`\t${flag('haspath', `${larax}.AsUnit().HasPath()`)}`,
		`\t${flag('visible', `${larax}.IsVisible()`)}`,
		`\t${flag('ai', 'IsAIPlayer(i1p_p)')}`,
		'\tMod_Log(i1p_s);',
		`\ti1p_b = ${larax}.AsUnit().BestTargetForPos();`,
		'\ti1p_s = "I1PROBE best target";',
		`\t${flag('valid', 'i1p_b.IsValid()')}`,
		'\tif (i1p_b.IsValid()) i1p_s = i1p_s + " player=" + i1p_b.player + " class=" + i1p_b.class();',
		'\tMod_Log(i1p_s);',
		'\ti1p_s = "I1PROBE diplomacy larax-4";',
		`\t${flag('ceasefire', 'DiplGetCeaseFire(i1p_p, 4)')}`,
		`\t${flag('ceasefire_back', 'DiplGetCeaseFire(4, i1p_p)')}`,
		`\t${flag('allied', 'DiplAreAllied(i1p_p, 4)')}`,
		`\t${flag('sharecontrol', 'DiplGetShareControl(i1p_p, 4)')}`,
		`\t${flag('sharesupport', 'DiplGetShareSupport(i1p_p, 4)')}`,
		`\t${flag('ai4', 'IsAIPlayer(4)')}`,
		`\t${flag('ceasefire12', 'DiplGetCeaseFire(1, 2)')}`,
		`\t${flag('ceasefire14', 'DiplGetCeaseFire(1, 4)')}`,
		'\tMod_Log(i1p_s);',
		'\tfor (i1p_i = 0; i1p_i < i1p_v.count; i1p_i += 1) {',
		'\t\ti1p_o = i1p_v[i1p_i];',
		'\t\ti1p_s = "I1PROBE teuton " + i1p_i;',
		'\t\tif (i1p_o.IsValid()) {',
		`\t\t\ti1p_s = i1p_s + " class=" + i1p_o.class() + " player=" + i1p_o.player + " health=" + i1p_o.health + " command=" + i1p_o.command + " dist=" + i1p_o.DistTo(${larax});`,
		`\t\t\t${flag('alive', 'i1p_o.IsAlive()')}`,
		`\t\t\t${flag('enemy_of_larax', `i1p_o.IsEnemy(${larax})`)}`,
		`\t\t\t${flag('larax_sees_enemy', `${larax}.IsEnemy(i1p_o)`)}`,
		`\t\t\t${flag('valid_target_for_larax', `${larax}.IsValidTarget(i1p_o)`)}`,
		`\t\t\t${flag('larax_valid_target_for_it', `i1p_o.IsValidTarget(${larax})`)}`,
		'\t\t} else i1p_s = i1p_s + " invalid";',
		'\t\tMod_Log(i1p_s);',
		'\t}',
		'\tif (i1p_k == 7) {',
		'\t\tfor (i1p_i = 0; i1p_i < i1p_v.count; i1p_i += 1) {',
		'\t\t\tif (i1p_v[i1p_i].IsAlive()) {',
		'\t\t\t\tMod_LogInt("I1PROBE ordering Larax to attack teuton", i1p_i);',
		'\t\t\t\tNO_Larax.obj.SetCommand("attack", i1p_v[i1p_i]);',
		'\t\t\t\tbreak;',
		'\t\t\t}',
		'\t\t}',
		'\t}',
		'\tSleep(2000);',
		'}',
		'Mod_Log("I1PROBE end");',
	];
	return lines.join('\r\n') + '\r\n';
}

const DECLARATIONS = 'ObjList i1p_v;\nObj i1p_o, i1p_b;\nint i1p_k, i1p_i, i1p_p;\nstr i1p_s;\n';

// Adds the probe to the script of `path` when it is one the probe knows. Returns the new source, or null.
function addProbe(path, src) {
	if (!/^Maps\/11937\/Sequences\/seq15\.vs$/i.test(path)) return null;
	const at = src.search(/WaitEmptyQuery\s*\(\s*M1_Q_Victims1/);
	if (at < 0) throw new Error(`${path}: the place for the probe was not found`);
	const lineStart = src.lastIndexOf('\n', at) + 1;
	return S.insertDeclarations(src.slice(0, lineStart) + kormarisProbe() + src.slice(lineStart), DECLARATIONS);
}

module.exports = { addProbe };
