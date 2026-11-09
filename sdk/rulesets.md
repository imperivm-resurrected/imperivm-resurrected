# Rulesets

A ruleset is the global rules of a mod in a folder that is only mounted when the host of a room chooses it. One package can carry its civilizations **and** the rules of its original mod, and the rules of different mods never overlap, because a room mounts only one ruleset.

Use it when a mod changes more than its own civilizations: `CONST.INI` values, the stats and orders of the game's own units, game modes, options, scripts that the game reads by name, items, neutral classes and graphics, `hidden.txt`. If that content sits in the package root, it is mounted whenever the package is enabled, it changes the base game for every room and it collides with the same files of other mods.

Example: [`ruleset-example`](examples/ruleset-example). Template: [`ruleset`](templates/ruleset).

## What goes where

| Where | What | When it is mounted |
|---|---|---|
| The package root | Civilizations (`civs\`, their classes, `specials\`) and whatever the package needs in every room | Whenever the package is enabled, as always |
| `rulesets\<id>\` | The global rules, laid out like a mod root: `patches\`, `data\`, `packs\`, `local\`, `modes\`, `options\`, `events\`, `rules\game.json`, `hidden.txt`, `rle.mmp.tail`... | Only when a room chooses that ruleset |

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
| `name` | Text, or object per language. What the host sees in the **Rules** combo and the room shows. If it is missing, the `id` |
| `description` | Text, or object per language |
| `path` | The folder, `rulesets/<id>` if omitted. It has to be inside `rulesets\`, and no ruleset can be inside another one or share the folder of another |

A ruleset that breaks one of these rules is ignored, and its package's card in the content manager says why.

## How the host chooses it

- In **Create room**, the **Rules** combo, next to **Content**, lists the game's own rules (*Original (HD)*), the rulesets of the packages that are in the chosen content profile, and one entry *<package> (original rules)* for each package of the profile that has civilizations (`civs\*.json`) and **no** ruleset. If the choice is not the one that is loaded, the button reads *Restart with these rules*: the game restarts with it and the host creates the room again, as when changing the content.
- In the skirmish room, the **Rules** row under **Content** does the same: picking another ruleset restarts the game, and nothing is lost because the match has not started.
- The ruleset is saved in the profile (`"ruleset": {"pack": "<folder>", "id": "<id>"}`). A profile mounts at most one. `"id": ""` (or no `id`) is the entry *<package> (original rules)*: that package without a layer. It mounts no extra files, so choosing it never needs a restart as long as no ruleset is mounted (going from a ruleset to it, or the other way, does restart).

### Which civilizations a room starts with

One Rules entry is one pack of civilizations, strictly, not a mix:

| Entry | Civilizations a new room allows |
|---|---|
| *Original (HD)* | only the game's |
| A ruleset of package P, or *P (original rules)*, where P has civilizations | only P's |
| A ruleset of package P, where P has no civilizations (rules only) | only the game's |

The civilizations of the other enabled packages are not in that default. The host adds them, or takes the default away, in the room lobby: **Game options > RULES > Edit civilizations** lists every set of the content profile as a toggle (at least one stays on), applies at once and sends a player whose civilization is left out back to *Random*. A package with civilizations **and** rulesets keeps one entry per ruleset and has no *(original rules)* entry. If you want players to be able to pick your civilizations in the base game's rules, a package without `rulesets` already does that: it appears as *(original rules)*.

A player who joins a room with another ruleset gets the usual *This room uses other content* dialog: the game downloads what is missing, restarts with the room's ruleset and returns to the room.

## What it does to the rest

- **Order.** The mounted ruleset goes right above its package: where both have the same file, the ruleset's is used. Against other packages it follows the package's place in the [load order](reference.md#1-mod-structure).
- **Patches, civilizations, modes, events, options, `rules\game.json`, `hidden.txt`, `local\` and `packs\`** inside a ruleset are read exactly as in a root.
- **Multiplayer.** The gameplay hash of the package covers its root only; each ruleset has its own, and the room checks the root of every package plus the ruleset it mounts. Two players can have another copy of a ruleset the room does not use.
- **Saved games.** A save records its ruleset. Loading it under another one (or under none) behaves like a rules pack that is not enabled: the game offers to restart with the saved one. Whether the game survives a save loaded under other rules, for example when a unit class is removed, has not been tested.
- **Content manager.** The card of a package lists its rulesets. The ones that change the same gameplay files as the ruleset of another package are shown as an alternative to it, not as a problem: they can never be mounted together.

## Limits

- A profile can use only one `rle.mmp.tail`. The one in the ruleset that is mounted counts as its package's, so a package cannot have a tail in its root and in a ruleset; two rulesets can each have theirs, because only one is mounted.
- Do not put `[Resolutions]` in the `const.ini` of a ruleset: the screen resolutions are not part of the rules a room chooses. Keep that section in the package root.
- The host chooses the ruleset before the room exists. It cannot be changed in an open room, two rulesets cannot be mixed, and the game cannot switch without restarting.
- The **Rules** combo only lists rulesets of packages that are in the profile.
- Some files, such as `AI.INI` and `SKILLS.INI`, are read by the game in ways this guide cannot describe. Try your ruleset in a room before publishing it.

## Validating

```
node tools\validate-mod.js <your mod folder>
```

The validator checks each ruleset as a root of its own, with the folder's path cut off, and reports the problems with the path of the file. In `mod.json` it reports an `id` that is not valid or is used twice, a `path` outside `rulesets\`, a folder that does not exist, rulesets inside one another, and an `rle.mmp.tail` both in the root and in a ruleset. In a ruleset it reports `[Resolutions]`. Files under `rulesets\` that no ruleset holds are reported as a warning. See [Validating a mod](validating.md).
