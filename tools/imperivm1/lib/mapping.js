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
	'data/subai/gate_patrol.vs': 'sentries at the gate: the I1_NoSentries sequence keeps every settlement without them',
	'data/subai/wall_patrol.vs': 'sentries on the walls: the I1_NoSentries sequence keeps every settlement without them',
	'data/subai/townhall_sentries_control.vs': 'gives the settlement 20 sentries and 4 more every 30 s: undone by the I1_NoSentries sequence',
	'data/subai/townhall_autotrain.vs': 'trains the units inside only when the player turns "autotrain" on: idle in a scripted map',
	'data/subai/arena_behavior.vs': 'trains units only when the player orders it from the building: idle in a scripted map',
	'data/subai/settlement_behavior_horses.vs': 'wild horses (WildHorse) near the settlement go to player 15: the campaign has none',
	'data/subai/village_behavior_supply.vs': 'a village sends its gold and food to the settlement it supplies, only if the player sets one',
	'data/subai/outpost_behavior.vs': 'only for independent outposts: spawns their defenders and keeps loyalty at 100; also orders enemy units out (see the holder spawns)',
	'data/subai/teuton_feeding.vs': 'Teuton units given an order start "feeding" (Great Battles of Rome\'s Teuton upkeep); DIFFERS from Imperivm I',
	'data/subai/sneak_behavior.vs': 'the Gaul druid hides after 20 s without attacking; DIFFERS from Imperivm I, where druids hide only when told',
	'data/subai/hero_skill_behaviour.vs': 'computer heroes spend skill points and cast skills; DIFFERS from Imperivm I (no hero skills)',
	'data/subai/stonehenge_charge.vs': 'Stonehenge refills the stamina of units near it, one per second',
	'data/subai/summoning_behavior.vs': 'a summoned ghost loses stamina and dies when it runs out (the summon lasts a while)',
	'data/subai/teleport_force_exit.vs': 'units left inside a teleport\'s settlement are sent through it',
};

module.exports = {
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
