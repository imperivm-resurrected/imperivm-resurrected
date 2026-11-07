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
	TeutonWolf: 'TTeutonWolf',
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

module.exports = { ADVENTURE_TEXTS, STAMINA, PLAYER_CONTROL, CLASS_SUBSTITUTES, RACES, FUNCTION_RENAMES, RESEARCH_RENAMES, TIMED_RITUALS, TERRAIN_FALLBACKS, GAME_PROPERTIES };
