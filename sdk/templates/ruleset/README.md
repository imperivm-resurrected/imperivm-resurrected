# Ruleset template

One package for a mod that has civilizations **and** global rules (the `CONST.INI` values, a game's units and orders, game modes, options, scripts, neutral art...). The civilizations go in the root and are available whatever the rules are. The global rules go in a **ruleset**, a folder that only the room that chooses it mounts, so the rules of two different mods never overlap. Guide: [Rulesets](../../rulesets.md).

## Steps

1. Copy this folder into `<game folder>\mods\` and rename it.
2. In `mod.json` change `id` (lowercase letters, numbers and hyphens), `name`, `authors` and `description`. Under `rulesets`, give each ruleset an `id` (lowercase letters, numbers, `-` and `_`, up to 24), a `name` and a `description` for the room, and its `path` (`rulesets/<id>`). For several rulesets, repeat the entry and the folder.
3. Put your civilizations in `civs\` (and their classes in `data\`, as in the [civilization template](../civilization)). Put the global rules in `rulesets\<id>\`, which has the shape of a mod root: `patches\`, `data\`, `modes\`, `options\`, `events\`, `packs\`, `local\`, `hidden.txt`... Delete the note files and the folders you do not use.
4. Enable the mod in your profile. The host chooses the ruleset in **Create room** (**Ruleset**) or in the skirmish room; the game restarts when it is not the one that is loaded.
5. Read `Logs\imperivm\imperivm-mod.log`, in the game folder: the lines tagged `content` say which ruleset was mounted.

## Folder layout

```
my-ruleset-mod\
  mod.json                       lists the rulesets
  README.md
  civs\                          civilizations: available whatever the rules are
  data\classes\                  their classes
  rulesets\
    my-rules\                    a ruleset: laid out like a mod root
      patches\                   the paths of the game files it changes
      data\GameScripts\          victory modes
      modes\
      options\
      events\
```

## Notes

- Only one ruleset is mounted at a time. Two rulesets may change the same file, and rulesets of different packages are alternatives.
- Do not put `[Resolutions]` in the `const.ini` of a ruleset: the screen resolutions belong to the package root.
- A ruleset is gameplay: every player in the room mounts the same one. The game checks it and restarts whoever has another.
- The finished example is [`ruleset-example`](../../examples/ruleset-example).

## Check and publish

```
node tools\validate-mod.js <your mod folder>
```

The validator checks each ruleset as a root of its own ([Validating a mod](../../validating.md)). Publish it as described in [Publishing on GameBanana](../../publishing.md).
