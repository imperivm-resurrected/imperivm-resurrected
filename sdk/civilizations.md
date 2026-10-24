# Civilizations

Start from the [civilization template](templates/civilization), an empty mod with the folders this guide uses.

The game ships with 8 civilizations, numbered 0 to 7: Gaul, Republican Rome, Carthage, Iberia, Imperial Rome, Britain, Egypt and Germany.

Imperivm Resurrected lets you declare new civilizations, numbered from 10 up. Each one inherits everything your mod doesn't define from a game civilization (its
*base*). The tools need Node.js.

Ready-to-copy example: [`examples/civilization-example`](examples/civilization-example), which keeps the legacy prefix `D`. For a new civilization with its own classes, follow [Cloning a civilization](#cloning-a-civilization).

A new civilization appears **alongside** its base by default: Dacia adds a choice; Gaul remains available. `variant` and `replacesBase` are independent, optional settings, not steps required to create a civilization.

## Declaring a civilization

A `civs\<id>.json` file in your mod:

```json
{
  "id": "dacia",
  "base": "Gaul",
  "name": {"spanish": "Dacia", "english": "Dacia", "italian": "Dacia"},
  "letter": "Dac"
}
```

| Field | What it is |
|---|---|
| `id` | The name: letters, numbers and `_`, starting with a letter, up to 24 characters. You use it in `race="..."` and in scripts. |
| `index` | (Advanced, optional) An explicit integer from 10 to 127 for legacy packs. Omit it for automatic engine assignment; `null`, strings and fractions are errors. Keep published explicit numbers unchanged: saves remember them, and two active civilizations cannot share one. |
| `base` | The game civilization it inherits from: `Gaul`, `RepublicanRome`, `Carthage`, `Iberia`, `ImperialRome`, `Britain`, `Egypt` or `Germany`. Defaults to `Gaul`. |
| `name` | Its name in each language, or a single string. |
| `letter` | Class and command prefix, 1 to 4 characters: an uppercase letter followed by letters or digits (`Dac`, `Ga`, `Dac2`). Existing single letters (including lowercase, normalized to uppercase) and single digits (`2Archer`) still work. Omit it to inherit the base's prefix. Only set your own if your classes and commands use it: the game builds names such as the peasant training command from it. See the convention and [registry](prefixes.md). |
| `template` | (Optional) Prefix of your city and village templates, so the random map doesn't use the base's. See [Templates](city-templates.md). |
| `internalName` | (Optional) The name the game uses to find the civilization's interface files; defaults to the base's. |
| `folder` | (Optional) The name shown in selection texts. |
| `random` | (Optional, defaults to `true`) Whether it's included in the random-civilization draw, also on random maps. The AI only draws it if it also has `"ai": true`. Set `false` to exclude it. |
| `biome` | (Optional) A game civilization: the terrain, trees and objects the random map places around its start are that civilization's. Without it, the base's. See *The biome*. |
| `ai` | (Optional) With `true`, the AI plays with your classes and commands. Otherwise it plays as the base civilization. The cloning tool sets it to `true`. See *The AI*. |
| `ownAi` | (Optional, with `ai`) For mods that ship all their own AI scripts and want to see the civilization as itself (`Dacia`, not its base). |
| `alias` | (Optional, written by the cloning tool) `classes` and `commands`: name in the base to name in your civilization (`"GArcher": "DacArcher"`, `"trainGSwordsman": "trainGSwordsman Dacia"`). The AI uses them to translate. |
| `variant` | (Optional) With `true`, it's a variant of its base: it plays with the base's classes and scripts see it as the base. See *Variants*. |
| `replacesBase` | (Optional) With `true`, the base no longer shows in the lobby and this civilization takes its place. See *Variants*. |

With just `id` you get a playable copy using Gaul's existing classes under another name (`base` defaults to `Gaul`, and `index` is assigned automatically). The declaration above instead asks for your own `Dac` classes and commands: the JSON alone does not create them. Generate those with the cloning tool before testing it. Changing `letter` later does not rename existing files or references.

## Numeric indices and saved games

Keep `id` as the civilization's stable identity; new packs should omit `index`. For each active profile, the engine first reserves all explicit indices, then sorts the remaining civilizations by normalized (lowercase) `id` and assigns each the lowest free number from 10 through 127. Multiple declarations without `index` are valid, including within one pack. The [prefix registry](prefixes.md) reserves no numeric slots.

There are **118 custom civilization slots per active profile**, shared by automatic and explicit indices. The old ceiling of 99 was policy; 127 is the limit of the signed-byte fields used for random maps. Only a few civilizations at a time have been tested, and the room's list may fit fewer than 118 (not verified). Values above 127 are not supported: the engine's reading of those bytes has not been audited. Automatic assignment and multi-character prefixes both require the matching new `winmm.dll` build; older releases do not support them.

Allocation is canonical for the effective profile, not a persisted allocation ledger: adding or removing civilizations can change automatic numbers. Saves using automatic civilizations require their accompanying `.mods.json` saved index table and the same effective civilization mapping. Missing or mismatched tables fail closed; restore the original profile and content to load the save. Do not expect transparent migration after changing the civilization set. Replays also require their original civilization set and mapping; the saved-game sidecar guard does not migrate replay files. Keep the sidecar with the save when copying or backing it up. Its `civSaveHash` binds the table to the completed binary save (SHA-256): an old sidecar cannot authorize a different overwritten file, and a metadata-write failure is reported rather than silently trusting a stale table. Multiplayer compatibility also includes the effective mapping; players must use matching content and DLL builds.

For an existing pack with an explicit `index`, preserve that number rather than removing or renumbering it. The flag `--index` and the legacy positional argument are available for that purpose; new authors do not need to find a free numeric slot.

## Prefix convention

Keep the historical field name `letter`, even for a multi-character prefix. Prefer the first three letters of the id, with only the first capitalized: `dacia` → `Dac`. The clone tool removes underscores and takes up to three letters/digits (`new_rome` → `New`, `a` → `A`). You can choose another prefix of up to four characters explicitly.

Class names start with the **whole prefix followed by an uppercase letter**: `DacTownhall`, `DacSwordsman`, `GaTownhall`, `GalTownhall`. When matching a prefix, check that next uppercase character: `GTownhall` belongs to `G`, but `GaTownhall` and `GalTownhall` do not. Keep the game's legacy lowercase `house` names working too (`Rhouse03`). Do not replace every occurrence of a prefix in a file: image paths and unrelated class names must stay unchanged.

The uppercase API (`GetRaceStrPref` and `Obj::raceStrPref`, also the engine's `GetRaceLetter`) returns the prefix **as written**, except that a legacy lowercase single letter is capitalized: `Dac` stays `Dac`, not `DAC`. The lowercase API (`GetRaceStrPrefLow`, `Obj::raceStrPrefLow`) lowers the **entire** prefix: `Dac` → `dac`. Commands built from those values must match (`trainpeasantdac`, `hireheroDac`).

Prefixes therefore clash case-insensitively: `Dac` and `DaC` cannot coexist. The first loaded civilization owns its prefix; a second one with the same own prefix is ignored, and the content manager names the owner and its pack. The eight game prefixes are reserved even when their bases are replaced. Omitting `letter`, or explicitly using your own base's single letter, still inherits that base and can be shared by variants. Using another base's letter is rejected. Check [prefixes.md](prefixes.md) before publishing; existing community civilizations keep their single-character prefixes.

A `Dac` clone has passed an initial Linux/Wine gameplay test with units, a hero and sentries. Test your own pack too: validation does not run the game, and this does not establish every prefix or replacement configuration. Multi-character prefixes need a matching Imperivm Resurrected build with this feature; older releases reject them.

## Using it in your classes and scripts

In each unit's or building's `.sc.xml`:

```xml
<properties race="Dacia"/>
```

The game only knows the names of its 8 civilizations. The mod recognizes your civilizations' `id` (case-insensitive) and sets it on the
class. If you set `internalName` by hand, that works too. The default one (the base's) doesn't count, so `race="Gaul"` is still
Gaul even if you have a civilization cloned from it.

A class with `parent=` inherits stats and behaviors, but not the entity (`entity`, the model) or the sounds: set them again or
the unit spawns without a model and vanishes. That's why the cloning tool copies whole classes.

In scripts, each declared civilization is a constant named after its `id` as you write it, with a capital first letter (`dacia` gives
`Dacia`, `romeImperial` gives `RomeImperial`). The rest in lowercase also works (`Romeimperial`):

```
if (GetPlayerRace(player) == Dacia)
	...
```

The game's AI scripts compare against `Gaul`, `RepublicanRome`… and don't know yours. With `"ai": true` they see the base. To find out which
civilization an object really plays as, use `Mod_TrueRace(objeto)`.

## In the lobby and on the map

Declared civilizations appear in the multiplayer lobby's dropdowns, after the game's. On a random map, yours uses its biome's terrain and starts with a city and a village built from its classes
(`DacTownhall`, `DacBarracks`…). If you don't give it templates, the building layout is the base's. The player's interface (command bars, corners, quick icons and the
end screen) is the base's.

If something doesn't show up, check `Logs\imperivm\imperivm-mod.log`: the lines tagged `civilizations` say what was registered and what failed (an invalid `id`,
an invalid or duplicate explicit `index`, exhausted numeric slots, a `base` that doesn't exist, an invalid or conflicting prefix). Errors also appear on the mod's card in the manager.

## Variants

A variant is a game civilization with another name in the lobby and something of its own at the start of the game. For example, a mod
where Rome chooses at the start between republican, imperial and punic can offer all three in the lobby:

```json
{
  "id": "RomeImperial",
  "base": "RepublicanRome",
  "name": {"spanish": "Roma imperial", "english": "Roman Empire", "italian": "Roma imperiale"},
  "variant": true,
  "replacesBase": true
}
```

With `"variant": true`:

- It plays with the base's classes and commands. It has no classes of its own and no `letter`, unless it replaces the whole base (see below).
- `GetPlayerRace` returns the base, so the game's and other mods' scripts treat it like the base (ships, sentries, AI).
- `Mod_PlayerCiv(player)` returns the civilization chosen in the lobby (its assigned index, exposed as `RomeImperial` in the example). What sets it apart goes in a
  [script patch](patches.md), for example researching the right Rome at the start:

```
@before while (EnvReadInt(set, "ChooseRomanRace") != 1)
	if (Mod_PlayerCiv(.player) == RomeImperial && set.CanResearch("ChooseImperialRome"))
		set.Research("ChooseImperialRome");
```

- The AI plays it too: `ai` is `true` unless you say otherwise.
- With `"replacesBase": true` the base no longer shows in the lobby and its variants take its place. In the random draw, the base's
  share is split among them. A player who had the base chosen moves to the first variant. Maps and saved games that fix the base still
  use it.
- A variant with `"replacesBase": true` can also change its base's prefix and templates for the whole game. With `letter`, the game
  builds the names of the base's classes and commands with that whole prefix (`GetRaceStrPref`, peasant training, barracks, sentries, hero
  hiring). With `template`, the base's towns and villages, including the neutral ones of the random map, come from your
  `<template> Town N` and `<template> Village N` templates. This is what replacing a whole civilization takes. For example, Greece takes
  the place of Imperial Rome with `"base": "ImperialRome"`, `"letter": "H"` and `"template": "Greek"`, and its classes have
  `race="ImperialRome"` and the letter `H` (`HTownhall`, `HHoplite`). If two active variants give the same base different letters or
  templates, the first one counts and the manager warns on the other.

## Cloning a civilization

Writing a civilization's dozens of classes and commands by hand is no fun. `tools/clone-civilization.js` clones one from the game.
Start with a copy of the [civilization template](templates/civilization), edit its `mod.json`, and run these commands **from the SDK root**, the folder containing `tools`. The tool needs the game's `DATA` folder, which you get with [`extract-data.js`](tools/extract-data.js):

```
node tools/extract-data.js "<game folder>" "<output folder>"
node tools/clone-civilization.js "<output folder>\DATA" "<mod folder>" dacia --base Gaul
node tools/validate-mod.js "<mod folder>" --data "<output folder>\DATA"
```

Without `--letter`, this derives `Dac`. To override it, add `--letter Dac2`. The old positional form still works unchanged, including single digits:

```
node tools/clone-civilization.js "<output folder>\DATA" "<mod folder>" Dacia D Gaul 10
node tools/clone-civilization.js "<output folder>\DATA" "<mod folder>" Dacia 2 Gaul 10 --all-orders
```

The optional flags are `--letter`, `--base`, `--index` and `--all-orders` (clone even shared orders that name no class). Use the flag form when omitting the positional letter. The base defaults to `Gaul`. Without `--index` or its positional equivalent, the generated JSON omits `index` so the engine assigns it. Explicit indices (10–127) are an advanced legacy option; preserve numbers already published in existing packs.

The eight game prefixes (`G R C I M B E T`) are rejected by the cloner, even the base's own prefix: new classes with those ids would overwrite the originals. A registered community conflict produces a warning with the owner and pack, but keeps your prefix; it does not silently select another one. Check [the registry](prefixes.md) and choose another with `--letter`.

Generation refuses to overwrite any of its target files and stops before writing if one already exists. To regenerate with different identifiers, use a fresh output folder; do not change only the JSON or copy over edited classes. To combine several generated civilizations into a pack, merge their classes, declarations and commands and combine the operations in shared patch files rather than overwriting those files. The validator checks only that pack; conflicts with other active packs are reported by the content manager at runtime.

With the derived `Dac`, it generates `civs\dacia.json`, a copy of each of the base's classes (`DacSwordsman` is a copy of `GSwordsman`, with `race="Dacia"`), the commands that
name base classes (`trainGSwordsman Dacia`, `trainpeasantdac`, `hireheroDac`…) and patches that add the new classes to the
shared commands. It also clones the heroes `GHero1`…`GHero7`, which hang from `GaulHero` and have no `race`: without heroes that have a model, hiring one
crashes the game. From there you edit whatever you want. The sample result is in
[`examples/civilization-example`](examples/civilization-example), which keeps the legacy prefix `D`.

The generated declaration has `"ai": true` and the AI aliases, but no `variant` or `replacesBase`: it is an independent civilization. It looks like the base until you edit it. Enable it in an isolated test profile, restart, and test it in a local room with a random map: recruitment, heroes, sentries, AI, then saving/loading. Do this before publishing.

The tool doesn't write the `template` field. If you want your own templates, add it yourself ([Templates](city-templates.md)).

The copies don't carry the originals' `altid`. The `altid` is a unique alias (`Barrack`, `Village`) that scripts and the AI use to look up a
civilization's "barracks". If two classes share one, one civilization's AI stops seeing its buildings. If you write classes by hand, don't copy it.

## The AI

By default, the AI that picks a mod civilization plays as its base. With `"ai": true` it plays with your classes and commands. The game's AI scripts
decide based on the civilization of the central building and build names from it, so the game makes them see the base and
translates what they ask for:

- The commands and research they ask for by name (`trainGSwordsman`, `Gaul Axeman`, `hireheroG`…) are mapped to your civilization's, according to
  `alias.commands`, only on objects of an AI of your civilization.
- Your classes count as the base's: `DacArcher` is a `GArcher` for counts, barracks, temples and counter-units.

The cloning tool fills in both tables. If you write your classes by hand, fill them in yourself. Nothing changes for the human player.

AI limits:

- Your AI behaves like the base's, unless you give it another tactic.
- A unit that isn't in the base's six slots won't be recruited.
- New research (not cloned from one of the base's) is unknown to it.
- The costs it reads before paying are the base's, even if the command charges your values.

**A different tactic for your civilization.** The game picks each AI's tactic at start, in `data\ai\GetTacticScript.vs`. A
[script patch](patches.md) can add a piece to it for your civilization. Since the AI sees the base, ask for the real civilization with
`Mod_TrueRace(set.GetCentralBuilding)`. The example [`examples/ai-tactic-example`](examples/ai-tactic-example) makes Dacia use a game tactic
that no AI uses. Patches from several mods add up, so two civilizations can pick their tactic at the same time.

With `"ownAi": true` the mod has to ship all the AI scripts, because the game's see the custom civilization's numeric index and their tables don't know it.
Normally you leave `"ai": true` and change only the tactic.

## The biome

On a random map, what surrounds each player's start (grass, sand, trees, decorations) is decided by their civilization. Yours uses its
`base`'s, or whatever you set in `biome`:

```json
{ "id": "dacia", "base": "Gaul", "biome": "Britain" }
```

The game only has four terrain-and-tree biomes, so civilizations share them in pairs:

| Terrain and trees | Used by |
|---|---|
| 1st | Gaul and Imperial Rome |
| 2nd | Republican Rome and Britain |
| 3rd | Carthage and Egypt |
| 4th | Iberia and Germany |

Map objects (ruins, palm trees, rocks) do tell all eight apart; Carthage and Egypt act as a "desert" versus "temperate" switch.
The starting city is still the base's unless you use `template`. A fully custom biome, with new terrain, isn't possible.

## What you can't do

- Games against the AI outside the multiplayer lobby (scenarios and campaigns) with mod civilizations.
- Map editor: your civilization's classes show up mixed into its base's folders.
- New AI scripts without shipping all of them (see *The AI*).
