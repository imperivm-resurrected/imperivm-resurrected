// What changes name between Imperivm I and Great Battles of Rome. Only identifiers: class ids, race ids, terrain layer
// names and script function names.
'use strict';

// Classes of Imperivm I that Great Battles of Rome does not have, and the closest class it has (same role and race).
// A class listed here can still be kept with --keep-class when a mod provides it (a later phase brings the real sprites).
const CLASS_SUBSTITUTES = {
	// Same heroes under another id.
	GHero01: 'GHero1', // Mraxis
	GHero02: 'GHero2', // Ogox
	GHero03: 'GHero3', // Dumnorix
	NHero02: 'NHero1', // Heardred, mounted
	GVikingLord: 'BVikingLord', // Thoric, the Viking Lord (also known as "Thoric" in both games)
	// Same unit, the Teuton version of Great Battles of Rome.
	TeutonArcher: 'TTeutonArcher',
	// Imperivm I's TeutonWolf is the same rider as TeutonMetal (entity, stats and name "Teuton Raider", in Spanish
	// "Jinete teutón"); Great Battles of Rome's TTeutonWolf is another unit ("Teuton Wolf", "Asaltador teutón").
	TeutonWolf: 'TTeutonRider',
	TeutonMetal: 'TTeutonRider',
	GWFighter: 'GWomanWarrior',
	// Heroes without an equivalent: another hero of the same race; the object keeps the original name.
	NHero01: 'GHero4', // Brand
	RHero6: 'RHero1', // Marius
	RHero08: 'RHero2', // Tuvius
	RHero09: 'RHero3', // Julius
	RHero10: 'RHero2', // Cornelius
	RHero11: 'RHero3', // Pavonius
	// A Gaul storage building inside a settlement.
	GWarehouse: 'GHouse1',
	// The sparkle of a druid learning (druid_learn.vs: CreateFeedback("Teach", this)): Great Battles of Rome's is Experience.
	Teach: 'Experience',
	// Rocks and cliffs of Imperivm I: the large and medium rocks of Great Battles of Rome.
	Cliffs1: 'Rock Large 01',
	Cliffs2: 'Rock Large 02',
	Cliffs3: 'Rock Large 03',
	Cliffs4: 'Rock Large 01',
	Cliffs7: 'Rock Large 02',
	Cliffs8: 'Rock Large 03',
	GrObj01: 'Rock Medium 01',
	GrObj02: 'Rock Medium 02',
	TeleportRocks_3: 'Rock Medium 03',
	TeleportRocks_4: 'Rock Medium 04',
	TeleportRocks_5: 'Rock Medium 05',
};

// Races of the players (playerN.xml). Imperivm I's Romans are Caesar's legions.
const RACES = {
	Roman: 'RepublicanRome',
};

// Who controls each player (playerN.xml). Imperivm I's campaign marks all 16 players "Both"; every adventure of Great
// Battles of Rome marks them "Computer" and gives the human its player from start_player and the scripts' SetPlayer.
// gbr.exe reads Human = 0, Computer = 1, Both = 2, Rescue = 3.
const PLAYER_CONTROL = {
	Both: 'Computer',
};

// Script functions that Great Battles of Rome registers under another name, with the same parameters.
const FUNCTION_RENAMES = {
	PlaceEx: '_PlaceEx',
	PlaceBlockEx: '_PlaceBlockEx',
	PlayersShareControl: '_PlayersShareControl',
	PlayersShareView: '_PlayersShareView',
	PlayersShareSupport: '_PlayersShareSupport',
};

// Researches that scripts register (RegisterSettlementUpgrade) under another name: the horseshoes of the Roman blacksmith.
const RESEARCH_RENAMES = {
	'Roman Horseman': 'Roman Scout',
};

// Stonehenge rituals that Great Battles of Rome's Stonehenge cannot cast, with their length in Imperivm I (VXCONST.INI,
// StonehengeDurationGoldenRain = 120 seconds). Scripts wait for them to end, so they become a timer of the same length.
const TIMED_RITUALS = {
	golden_rain: 120000,
};

// Terrain layers of Imperivm I without a layer of the same texture or name: the layer used instead (by display name).
const TERRAIN_FALLBACKS = {
	'Grass 3': 'Grass 2',
};

// The adventure's game.xml: game_type 1 as in all of Great Battles of Rome's own adventures, and the user_interface of
// its Gaul campaign (El hijo del Ankou). Imperivm I marks an adventure with adventure="1" instead.
const GAME_PROPERTIES = {
	game_type: '1',
	user_interface: '5',
};

// Name and description of the adventure in the list, where Imperivm I has no text of its own: the Spanish description
// only when local.pak lacks one, and the Italian ones always (Imperivm I has no Italian texts).
const ADVENTURE_TEXTS = {
	Spanish: {
		description: 'La campaña de Imperivm I: el joven galo Lárax contra los teutones que arrasaron Kormaris.',
	},
	Italian: {
		name: 'Imperivm I - La guerra delle Gallie',
		description: 'La campagna di Imperivm I: il giovane gallo Larax contro i teutoni che hanno raso al suolo Kormaris.',
	},
};

// Groups that scripts spawn into a settlement of another player (SpawnGroupInHolder). Great Battles of Rome's outpost
// guard (outpost_behavior_guard.vs) orders every enemy unit inside its settlement out every 2.371 s; Imperivm I's only did
// it when the outpost changed owner. Groups meant to stay inside an enemy's settlement are therefore given to the
// settlement's owner right after they spawn and handed back to their own player where the script lets them out.
// `back`: [file, anchor (a statement line), 'before' | 'after', player].
const HOLDER_SPAWNS = {
	'11937/M1_Q_Prisoners': {
		owner: 4,
		back: [['seq18.vs', /^\s*NO_Prizon\.SetPlayer\(1\);/m, 'after', 1]],
		why: 'the villagers of player 1 are prisoners in the Teutons\' outpost (player 4) until Larax frees it; they become player 1 again when seq18 gives him the outpost',
	},
	'11945/M20_Q_MiddleInnerDefenders': {
		owner: 4,
		back: [
			// "attack" given a point is written "advance" before this rule runs.
			['seq34.vs', /^\s*M20_Q_MiddleInnerDefenders\.SetCommand\("advance"/m, 'before', 13],
			['seq34.vs', /^\s*M20_Q_MiddleInnerDefenders\.SetCommand\("enter"/m, 'before', 4],
		],
		why: 'player 13 defenders garrison S_Fortress (player 4, no ceasefire between them): they belong to player 4 while they are inside and to player 13 when seq34 sends them to attack',
	},
};

// The other spawns into another player's settlement, and why they are left as they are.
const HOLDER_SPAWNS_KEPT = {
	'11921/M4_Q_TeutonCampMain': 'players 4 and 5 are under ceasefire (seq15), and RunAIHelper sends them out to guard an area',
	'11939/T_DrozofilaArmy': 'players 1 and 2 are under ceasefire (seq1), and MoveToArea sends them out right away',
	'11941/M10_Q_EnemyDefenders': 'players 5 and 7 are under ceasefire (seq14)',
	'11941/M10_Q_EnemyDefenders1': 'players 5 and 7 are under ceasefire (seq14)',
	'11942/M14_Q_VillageSackers': 'players 2 and 3 are under ceasefire (seq1), and seq5 makes them player 2',
	'11945/M7_Q_AddToLarax': 'players 1 and 2 are under ceasefire (seq0)',
	'11945/M7_Q_UpperReinforcements': 'players 1 and 3 are under ceasefire (seq0)',
	'11945/M7_Q_FinalReinforcements': 'players 1 and 3 are under ceasefire (seq0)',
	'11945/M20_Q_WWForLarax': 'players 1 and 2 are under ceasefire (seq0)',
	'11945/M20_Q_HMForLarax': 'players 1 and 3 are under ceasefire (seq0)',
	'11945/M20_Q_LowerInnerDefenders': 'players 2 and 13 are under ceasefire (seq0)',
	'11945/M20_Q_UpperInnerDefenders': 'players 3 and 13 are under ceasefire (seq0)',
	'11945/M20_Q_OutpostDefence': 'seq33 sends them to attack Vercingetorix as soon as they spawn',
	'715/T_Bonus': 'players 1 and 3 are under ceasefire (seq15), and seq11 attaches them to a hero right away',
	'715/T_Reward1': 'seq3 spawns them only once Larax owns Outpost2, and players 1 and 3 are under ceasefire (seq15)',
	'715/T_Reward2': 'seq3 spawns them only once Larax owns Outpost3, and players 1 and 3 are under ceasefire (seq15)',
	'715/T_Reward3': 'seq3 spawns them only once Larax owns Outpost4, and players 1 and 3 are under ceasefire (seq15)',
	'716/T_Guards1': 'seq17 gives OutpostNorth to player 3, their own player, just before',
	'716/T_Guards2': 'seq17 gives OutpostWest to player 3, their own player, just before',
	'716/T_Guards3': 'seq17 gives OutpostEast to player 3, their own player, just before',
	'716/T_FixForce1': 'seq2 gives Outpost1 to player 1, their own player, just before, and attaches them to Larax',
	'835/T_Bonus': 'seq17 attaches them to Larax as they spawn',
};

// Stamina of the objects on the map: Great Battles of Rome's maps give 10 to every unit and 20 to every building, and
// Imperivm I's have none (by the class's cpp_class).
const STAMINA = {
	CVXUnit: 10,
	CVXHero: 10,
	CVXDruid: 10,
	CVXFlyingUnit: 10,
	CVXShip: 10,
	CVXWagon: 10,
	CVXBuilding: 20,
	CVXBarrack: 20,
	CVXGate: 20,
	CVXOutpost: 20,
	CVXTavern: 20,
	CVXTeleport: 20,
	CVXTownHall: 20,
};

// Behaviour scripts that Great Battles of Rome's classes run and Imperivm I's did not (data/subai/...), read from the
// game's own scripts: what each one does to a converted map. The audit lists them; one not here is a warning.
const GBR_BEHAVIORS = {
	'data/subai/gate_patrol.vs': 'sentries: the classes of the campaign do not inherit it (src/game/i1engine.cpp)',
	'data/subai/wall_patrol.vs': 'sentries: the classes of the campaign do not inherit it (src/game/i1engine.cpp)',
	'data/subai/townhall_sentries_control.vs': 'sentries: the classes of the campaign do not inherit it (src/game/i1engine.cpp)',
	'data/subai/townhall_autotrain.vs': 'trains the units inside only when the player turns "autotrain" on: idle in a scripted map',
	'data/subai/arena_behavior.vs': 'trains units only when the player orders it from the building: idle in a scripted map',
	'data/subai/settlement_behavior_horses.vs': 'wild horses (WildHorse) near the settlement go to player 15: the campaign has none',
	'data/subai/village_behavior_supply.vs': 'a village sends its gold and food to the settlement it supplies, only if the player sets one',
	'data/subai/outpost_behavior.vs': 'only for independent outposts: spawns their defenders and keeps loyalty at 100; also orders enemy units out (see the holder spawns)',
	'data/subai/teuton_feeding.vs': 'Teuton units given an order start "feeding" (Great Battles of Rome\'s Teuton upkeep); DIFFERS from Imperivm I',
	// No class of the campaign runs it since the druid is Imperivm I's own (I1_CLASSES); listed in case another class does.
	'data/subai/sneak_behavior.vs': 'the Gaul druid hides after 20 s without attacking; DIFFERS from Imperivm I, where druids hide only when told',
	'data/subai/hero_skill_behaviour.vs': 'computer heroes spend skill points and cast skills; DIFFERS from Imperivm I (no hero skills)',
	'data/subai/stonehenge_charge.vs': 'Stonehenge refills the stamina of units near it, one per second',
	'data/subai/summoning_behavior.vs': 'a summoned ghost loses stamina and dies when it runs out (the summon lasts a while)',
	'data/subai/teleport_force_exit.vs': 'units left inside a teleport\'s settlement are sent through it',
};

// Classes whose Great Battles of Rome version behaves otherwise than Imperivm I's, given to the campaign as classes of its
// own (lib/i1classes.js): `gbr` is the class the conversion would use, `i1` the Imperivm I class it comes from, `id` the new
// class. Every object of `gbr` in the campaign becomes `id`; the game's own `gbr` is left as it is for everything else.
//   TTent: Great Battles of Rome's tent raises its army only while it is independent (a neutral camp of a random map), so a
//     campaign tent, which has an owner, raised none and gave nothing to whoever captured it. Imperivm I's always raises
//     TTentInitialArmy Teutons for its owner, and TTentCapturedArmy for whoever captures it (ttent_behavior.vs).
//   GGhost: a summoned ghost in Great Battles of Rome (SummoningUnit, summoning_behavior.vs), dead within seconds when placed
//     on a map; in Imperivm I a Peaceful unit that lives until it is killed.
//   Teuton riders and archers: Great Battles of Rome's start "feeding" (teuton_feeding.vs, the upkeep of its Teutons) once
//     they are given an order; Imperivm I's have no upkeep.
//   GDruid: Great Battles of Rome's druid heals with rpriest_idle.vs and its druid_heal.vs, each heal costing stamina and
//     needing the Healing special, so a few druids soon stop healing; it also hides on its own (sneak_behavior.vs) and has
//     summonings Imperivm I did not have. Imperivm I's heals for free (druid_idle.vs, druid_heal.vs, the commands it
//     inherits from BaseMage), so druids heal each other for as long as they live.
//   DLleldoryn: the druid hero; in Great Battles of Rome a BaseMage without the druid's commands (heal, idle...), in
//     Imperivm I one with all of them and the rituals the campaign gives him (Global_LleldorynSpells).
//   GDruidHouse: trains Great Battles of Rome's druid and researches its rituals (Restoration, Eagle Summoning); Imperivm
//     I's trains Imperivm I's and researches Imperivm I's (Beast control, Invisibility, Mass Heal), the commands the
//     campaign's RegisterPlayerUpgrade calls name.
//   Ghost_sacrifice: what a druid's ghoul summoning places; Imperivm I's places Imperivm I's ghoul (ghost_sac_idle.vs).
//   Hide, Mist, Possess: what Imperivm I's druid places to cast invisibility, mist and beast control (CVXSacrifice objects
//     that the druids around feed). Great Battles of Rome has no Hide nor Possess, and its Mist is another object (a
//     summoned CVXScriptObj that fades), so they are made from Imperivm I's files (`fromI1`), with the entity of Great
//     Battles of Rome's `entityFrom` class. Hide keeps its name: Sacrifice::IsInvisibility, which keeps the druids feeding
//     the ritual hidden, checks that the class is exactly "Hide" (gbr.exe 00594f10, as Imperivm.exe 005bf270).
const I1_CLASSES = [
	{ id: 'I1TTent', gbr: 'TTent', i1: 'TTent' },
	{ id: 'I1GGhost', gbr: 'GGhost', i1: 'GGhost' },
	{ id: 'I1TeutonRider', gbr: 'TTeutonRider', i1: 'TeutonMetal' },
	{ id: 'I1TeutonArcher', gbr: 'TTeutonArcher', i1: 'TeutonArcher' },
	{ id: 'I1GDruid', gbr: 'GDruid', i1: 'GDruid' },
	{ id: 'I1DLleldoryn', gbr: 'DLleldoryn', i1: 'DLleldoryn' },
	{ id: 'I1GDruidHouse', gbr: 'GDruidHouse', i1: 'GDruidHouse' },
	{ id: 'I1GhostSacrifice', gbr: 'Ghost_sacrifice', i1: 'Ghost_sacrifice' },
	{ id: 'Hide', i1: 'Hide', fromI1: true },
	{ id: 'I1Mist', i1: 'Mist', fromI1: true, entityFrom: 'Mist' },
	{ id: 'I1Possess', i1: 'Possess', fromI1: true },
];

// The profile of Imperivm I's AI in the data mod (data/ai/I1/), and of its other profiles (data/ai/I1DEFENSIVE/...): the
// campaign's AIStart calls start them instead of the game's (imperivm-re/analisis/i1-vs-gbr/heroes-y-unidades.md §3.4).
const AI_PROFILE = 'I1';

// Classes that keep Great Battles of Rome's version although the others of their kind get Imperivm I's (lib/i1classes.js
// autoI1Classes), with why.
const I1_KEEP_GBR_CLASSES = {
	// Imperivm I's Stonehenge casts its rituals with natives Great Battles of Rome does not have (IsSpellCasted, GoldenRain,
	// FinalSacrifice, TeutonDisaster, GetGoldenRainObjects); the campaign's rituals are converted for the game's own
	// (TIMED_RITUALS).
	Stonehenge1: 'its rituals need natives Great Battles of Rome lacks',
	Stonehenge2: 'its rituals need natives Great Battles of Rome lacks',
};

// Imperivm I's script properties and methods that Great Battles of Rome has under another form, as the ported class
// scripts use them: the regular expression and what replaces it.
//   maxattack, minattack: "can it attack" (unit_idle.vs: u.maxattack > 0); Great Battles of Rome's one attack value.
//   IsGaul, IsRome and IsGaulVillage, IsRomeOutpost, IsGaulStronghold...: a settlement of that race, and of that kind
//   (settlement_behavior_ambient.vs, village_trainpeasant.vs, the AI's scripts). The race is its class's (GTownhall
//   "Gaul", RTownhall "RepublicanRome"), which the campaign's copies keep.
const SETTLEMENT_RACES = { Gaul: 'Gaul', Rome: 'RepublicanRome' };
const SCRIPT_PROPERTY_RENAMES = [
	[/\.(?:max|min)attack\b(?!\s*\()/g, '.damage'],
	// With a receiver it is a Settlement (the AI's `set.IsRomeStronghold`, `g.settlement.IsGaulOutpost`), whose race is its
	// central building's; without one, the building that runs the script (this, an Obj).
	[
		/(\b[A-Za-z_][\w.]*)?\.Is(Gaul|Rome)(Village|Outpost|Stronghold)?\b(?:\s*\(\s*\))?/g,
		(m, r = '', race, kind) => {
			const name = SETTLEMENT_RACES[race];
			if (r) return `(${kind ? `${r}.Is${kind} && ` : ''}${r}.GetCentralBuilding.raceStr == "${name}")`;
			return `(${kind ? `.AsBuilding.settlement.Is${kind} && ` : ''}.raceStr == "${name}")`;
		},
	],
];


module.exports = {
	I1_CLASSES,
	I1_KEEP_GBR_CLASSES,
	AI_PROFILE,
	SCRIPT_PROPERTY_RENAMES,
	ADVENTURE_TEXTS,
	HOLDER_SPAWNS,
	HOLDER_SPAWNS_KEPT,
	STAMINA,
	PLAYER_CONTROL,
	CLASS_SUBSTITUTES,
	RACES,
	FUNCTION_RENAMES,
	RESEARCH_RENAMES,
	TIMED_RITUALS,
	TERRAIN_FALLBACKS,
	GAME_PROPERTIES,
	GBR_BEHAVIORS,
};
