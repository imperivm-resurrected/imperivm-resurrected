# Rulesets

A ruleset is the global rules of a mod in a folder that is only mounted when the host of a room chooses it. One package can carry its civilizations **and** the rules of its original mod, and the rules of different mods never overlap, because a room mounts only one ruleset.

Use it when a mod changes more than its own civilizations: `CONST.INI` values, the stats and orders of the game's own units, game modes, options, scripts that the game reads by name, items, neutral classes and graphics, `hidden.txt`. If that content sits in the package root, it is mounted whenever the package is enabled, it changes the base game for every room and it collides with the same files of other mods.

Example: [`ruleset-example`](examples/ruleset-example). Template: [`ruleset`](templates/ruleset).

## What goes where

| Where | What | When it is mounted |
|---|---|---|
| The package root | Civilizations (`civs\`, their classes, `specials\`) and whatever the package needs in every room | Whenever the package is enabled, as always |
| `rulesets\<id>\` | The global rules, laid out like a mod root: `patches\`, `data\`, `packs\`, `local\`, `modes\`, `options\`, `events\`, `rules\game.json`, `hidden.txt`, `rle.mmp.tail`... | Only when a room chooses that ruleset |

A package counts as a **rules** package (required content in a room, not "optional"; a change to it needs a restart) if its root or any of its rulesets holds gameplay files, `patches\data\const.ini` included. A package with only `mod.json` in its root and all its gameplay in `rulesets\` is a rules package. `"kind"` in `mod.json` can only raise this, never lower it.

The civilizations of every enabled package stay selectable in a room whatever the rules are (the host adds them with **Edit civilizations**, see below), but a room starts with only one pack of them. Nothing forces you to move the files of a mod that already works: the root keeps working as before, and `rulesets` is optional.

## Declaring them

In `mod.json`:

```json
"rulesets": [
  {
    "id": "community",
    "name": {"english": "Community rules", "spanish": "Reglas de la comunidad"},
    "description": {"english": "The balance of the community mod."},
    "path": "rulesets/community"
  }
]
```

| Field | What it does |
|---|---|
| `id` | Lowercase letters, numbers, `-` and `_`; up to 24; unique in the package. The game stores it in the profile, the room and the saves, so do not change it once published |
| `name` | Text, or object per language. What the host sees in the **Ruleset** combo and the room shows. If it is missing, the `id` |
| `description` | Text, or object per language |
| `path` | The folder, `rulesets/<id>` if omitted. It has to be inside `rulesets\`, and no ruleset can be inside another one or share the folder of another |

A ruleset that breaks one of these rules is ignored, and its package's card in the content manager says why.

## How the host chooses it

- In **Create room**, the **Ruleset** combo, next to **Content**, lists the game's own rules (*Original (HD)*), the rulesets of the packages that are in the chosen content profile, and one entry *<package> (original ruleset)* for each package of the profile that has civilizations (`civs\*.json`) and **no** ruleset. If the choice is not the one that is loaded and cannot be [switched without a restart](#switching-without-a-restart), the button reads *Restart with this ruleset*: the game restarts with it and the host creates the room again, as when changing the content.
- In the skirmish room, the **Ruleset** row, the first of **Game options**, does the same: picking another ruleset switches it at once when it can be [switched without a restart](#switching-without-a-restart), and otherwise restarts the game (nothing is lost, because the match has not started).
- The ruleset is saved in the profile (`"ruleset": {"pack": "<folder>", "id": "<id>"}`). A profile mounts at most one. `"id": ""` (or no `id`) is the entry *<package> (original ruleset)*: that package without a layer. It mounts no extra files, so choosing it never needs a restart as long as no ruleset is mounted (going from a ruleset to it, or the other way, restarts unless both rulesets qualify for the [switch without a restart](#switching-without-a-restart)).

### Which civilizations a room starts with

One Ruleset entry is one pack of civilizations, strictly, not a mix:

| Entry | Civilizations a new room allows |
|---|---|
| *Original (HD)* | only the game's |
| A ruleset of package P, or *P (original ruleset)*, where P has civilizations | only P's |
| A ruleset of package P, where P has no civilizations (rules only) | only the game's |

The civilizations of the other enabled packages are not in that default. The host adds them, or takes the default away, in the room lobby: **Game options > RULES > Edit civilizations** lists every set of the content profile as a toggle (at least one stays on), applies at once and sends a player whose civilization is left out back to *Random*. A package with civilizations **and** rulesets keeps one entry per ruleset and has no *(original ruleset)* entry. If you want players to be able to pick your civilizations in the base game's rules, a package without `rulesets` already does that: it appears as *(original ruleset)*.

A player who joins a room with another ruleset switches to it by itself when the pair can be [switched without a restart](#switching-without-a-restart) and the player has the exact package; otherwise gets the usual *This room uses other content* dialog: the game downloads what is missing, restarts with the room's ruleset and returns to the room.

## What it does to the rest

- **Order.** The mounted ruleset goes right above its package: where both have the same file, the ruleset's is used. Against other packages it follows the package's place in the [load order](reference.md#1-mod-structure).
- **Patches, civilizations, modes, events, options, `rules\game.json`, `hidden.txt`, `local\` and `packs\`** inside a ruleset are read exactly as in a root.
- **Multiplayer.** The gameplay hash of the package covers its root only; each ruleset has its own, and the room checks the root of every package plus the ruleset it mounts. Two players can have another copy of a ruleset the room does not use.
- **Saved games.** A save records its ruleset. Loading it under another one (or under none) switches to the saved one without a restart when the pair qualifies for the [switch without a restart](#switching-without-a-restart); otherwise it behaves like a rules pack that is not enabled: the game offers to restart with the saved one. Whether the game survives a save loaded under other rules, for example when a unit class is removed, has not been tested.
- **Content manager.** The card of a package lists its rulesets. The ones that change the same gameplay files as the ruleset of another package are shown as an alternative to it, not as a problem: they can never be mounted together.

## Switching without a restart

Choosing another ruleset normally restarts the game, because the game reads almost everything a ruleset carries once, when it starts. When the ruleset only holds the kinds of files below, the DLL makes the game read its data again between two matches and **no restart is needed**. There is nothing to turn on.

The game reads classes, commands, items, skills, specials, scripts and `CONST.INI` once, when it starts. When the next match (or the loaded game) starts, the DLL frees those tables, reads them again with the rules that are mounted now, and sets back the values the game had looked up once. This is done at the start of the match, before anything of it exists.

| Kind of file in the ruleset folder | What the DLL does when the ruleset is switched |
|---|---|
| `patches\data\...` (`.xml`, `.ini`, `.vs`): class and command XML, `items.xml`, `bonus.xml`, `AI.INI`, `CONST.INI`... | builds the patched copies again |
| `data\...` (`.xml`, `.ini`, `.vs`): new or replacing files, such as game modes in `data\GameScripts` or item scripts | mounts the ruleset's folder of loose files in place (and takes the other one out) |
| `modes\*.json` | builds the lobby's list of modes again |
| `hidden.txt` | sets the hidden files from the new ruleset |

A ruleset may also hold nothing at all (the game's own rules and *<package> (original ruleset)* do).

**Where it works.** The skirmish room and *Create room*; the host can also change the **Ruleset** row of an **open online room**: the entries that cannot be switched in place say *(needs a restart)*. A guest whose room changes its ruleset, or who joins a room with another one, switches its own by itself if it has the exact package and the pair qualifies; otherwise it does what it did before (download what is missing and restart). A saved game made with another ruleset loads without the restart too. Every computer reloads at its own `LoadGameData`, from the ruleset it has mounted, so all of them build the match with the same tables; the log prints the checksum of the command names, which is the same line on every computer.

**What still restarts**: a ruleset with anything else (`.pak` files, art, sounds, voices, translations, `Packs\*.bfhp`, `rle.mmp.tail`, civilizations, specials, options, events, `rules\`, patches to `units\`, `buildings\` or `currentlang\`, a file that is not `.xml`, `.ini` or `.vs` under `data\`); campaigns, conquests and the editor; spectators and rejoining. **If the reload fails or finds something it cannot set back, the game restarts by itself with the ruleset you chose**: it never goes on half switched. The log (`game: hot reload`) says which step. The line `hot reload (after Init): ... trainRHastatus costgold = N` shows the cost the game holds for the Roman hastatus, which tells whether the new rules are live.

What the player sees: the **Ruleset** row of the skirmish room changes at once, the button of *Create room* keeps reading *Create room* when the pair qualifies, and the next match plays with the new rules. For any other pair the button reads *Restart with this ruleset* and the game restarts.

Things to know when you use it:

- The game copies some constants into private variables the first time it needs them and never looks at the file again (`DamageAmplify`, the production of settlements, the combat costs of hero skills...). The DLL sets those copies back when it loads the new file, so every constant follows the ruleset. A constant that no ruleset changes is not affected.
- A saved game keeps what it stored (timers already running when it was saved); load it under the ruleset it was saved with, or let the game switch to it.
- In a room, the host's key and the `mods` line follow the ruleset that is loaded.
- A loose `data\const.ini` (not a patch) replaces the game's file instead of patching it; it is a file in `data\` like the others.
- `[Resolutions]` and the other sections the game reads only at start-up keep the values the game started with.
- Loose files of the ruleset that another package also ships take priority over the other package's, while the ruleset was switched in place (it is mounted in front of the list).
- The list of modes of a dialog that is already open is not refreshed until the dialog is shown again.
- The game keeps one list of random-map templates for the whole session; a switch does not change it.
- An old `imperivm-mod.ini` that still has `ConstHotReload` or `FullHotReload` under `[Mods]` is fine: both keys are ignored.

Examples: [`ruleset-const-example`](examples/ruleset-const-example) (only `CONST.INI`), [`ruleset-rooms-example`](examples/ruleset-rooms-example) (a command, a mode, `hidden.txt`).

## Where art, sounds and texts go

The files of a big mod that you can see and hear fall in three classes. Put each one where the host's choice of ruleset can pick it:

| Class | Examples | Where |
|---|---|---|
| **1. The art of a civilization**, under names of its own | `units\MyUnit\...`, `buildings\...`, `gameres\icons\MyUnit.bmp`, `gameres\cmdbar\Actions\train MyUnit.bmp`, its sounds and voices | the **package root**, as always |
| **2. Global in-game files that replace the game's** | a new `gameres\icons\RHastatus.bmp`, `sounds\Notifications\barrack idle.wav`, `local\<language>\voices\RHastatus\1.wav`, `loc\<language>.loc.xml` that rewrites the game's texts, a replaced `.ent.xml`, `.rle` or `.pass` of a game unit | a **ruleset** (`rulesets\<id>\`), because it is the host who chooses it and only one is mounted. The room's key does not cover images, sounds and texts, so players may have different ones |
| **3. The menu skin**: menu INIs, backgrounds, buttons, lobby pages, tips and help (`menuini\`, `data\interface\menu\`, `gameres\menu\`, `menubackground.bmp`, `logo*.bmp`, `tips.xml`, `help.xml`) | the look of the menus | **not a ruleset**: it is a skin, a feature of its own that is not there yet. Leave it in the package root until then |

A ruleset is mounted like a package root, so everything the root can hold works in it with the same paths: loose files at the game's paths, `loc\<language>.loc.xml`, `local\<language>\` (loose or `local\<language>.pak`, only for players of that language) and `rle.mmp.tail`. If two rulesets of a package replace the same file, only the one the host chose is used. Example: [`ruleset-assets-example`](examples/ruleset-assets-example) (a text, a voice, a sound, a button and a portrait). The log (`layer file served: ...`) says which files of the ruleset the game opened.

If two packages put a file at the **same path**, the one that is first in the load order is used: packages are sorted by kind (cosmetic above content above rules), then by `id`, with `requires`, `loadAfter` and `loadBefore` first. A ruleset sits right above its own package, so it wins over the root of that package, but **not** over the root of another package that comes before it: a civilization pack that ships a game file in its root can beat the ruleset the host chose. That is one more reason to keep such files out of a civilization's root (the validator warns). In **Options > Content** both packages say they share the file, and the player can pick which one to use when it is only an image or a sound. [`ruleset-clash-example`](examples/ruleset-clash-example) shows it; the game's log says which package served each shared file.

None of these files can be switched in place: a ruleset that has any of them is not one of the cases of [Switching without a restart](#switching-without-a-restart), so changing to it restarts the game.

The validator helps (warnings, never errors): in a package with `civs\`, a file of the root that the game already has (`--game <game folder>` is needed to compare) is reported as *global in-game file in the root of a civilization pack*; a menu skin file inside a ruleset is reported as *menu skin file in a ruleset layer*.

## Limits

- A profile can use only one `rle.mmp.tail`. The one in the ruleset that is mounted counts as its package's, so a package cannot have a tail in its root and in a ruleset; two rulesets can each have theirs, because only one is mounted.
- Do not put `[Resolutions]` in the `const.ini` of a ruleset: the screen resolutions are not part of the rules a room chooses. Keep that section in the package root.
- The host chooses the ruleset before the room exists. The host can change it in an open room when the switch needs no restart; two rulesets cannot be mixed. Switching restarts the game, except in the cases of [Switching without a restart](#switching-without-a-restart).
- The **Ruleset** combo only lists rulesets of packages that are in the profile.
- Some files, such as `AI.INI` and `SKILLS.INI`, are read by the game in ways this guide cannot describe. Try your ruleset in a room before publishing it.

## Validating

```
node tools\validate-mod.js <your mod folder>
```

The validator checks each ruleset as a root of its own, with the folder's path cut off, and reports the problems with the path of the file. In `mod.json` it reports an `id` that is not valid or is used twice, a `path` outside `rulesets\`, a folder that does not exist, rulesets inside one another, and an `rle.mmp.tail` both in the root and in a ruleset. In a ruleset it reports `[Resolutions]`. Files under `rulesets\` that no ruleset holds are reported as a warning. See [Validating a mod](validating.md).
