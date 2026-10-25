# Changes

The SDK version and which mods you can make with each one. The script API version (`Mod_ApiVersion()`) is separate: it's currently 7.

## Unreleased

Multi-character prefixes and automatic civilization indices require a matching new Imperivm Resurrected DLL build; they are not supported by the original 1.0 release.

- **Civilization prefixes**: `letter` accepts 1–4 characters (uppercase letter followed by letters/digits); single letters and single digits stay compatible. The uppercase API preserves suffix case, the lowercase API lowers the whole prefix, and conflicting own prefixes are rejected case-insensitively, including the reserved game prefixes. `clone-civilization.js` derives `dacia` → `Dac`, accepts `--letter` overrides and the old positional form, and warns on community conflicts in the new [prefix registry](prefixes.md). It rejects reserved game prefixes and refuses to overwrite generated files. The template and guides now walk through creating an independent civilization, not replacing its base. The validator also catches duplicate civilization ids and non-integer indices. See [Civilizations](civilizations.md#cloning-a-civilization). An initial `Dac` gameplay test passed on Linux/Wine; test your own pack before publishing.

- **Conquests with a civilization of a mod**: the `interface` of a `<territory>` in the `territories.xml` inside the `.bfhp` accepts the `index` of a civilization (10-127), not only 0-7. Needs a matching new DLL build.

- **Optional civilization indices**: omit `index` to let the engine reserve explicit numbers first, then assign the lowest free numbers (10–127) in normalized-id order. The range has 118 slots in theory, but only a few civilizations at a time have been tested and the room list may fit fewer. 99 was the previous policy ceiling; 127 is the limit of the signed-byte fields, and higher values are unsupported. The cloner no longer emits `index` unless supplied via `--index` or the legacy positional argument; keep published explicit numbers unchanged. The validator accepts multiple index-less declarations and rejects invalid or duplicate explicit indices. The prefix registry reserves no numbers. Allocation is canonical per profile, with no persisted ledger: saves with automatic civilizations need their `.mods.json` index table and the same effective mapping, or loading fails closed. The table is bound to the completed save's SHA-256 to reject stale sidecars after a failed overwrite; metadata-write errors are reported, and rejected online saves never fall back to a new game. Changing the civilization set is not transparent save migration; restore the original profile/content. Multiplayer compatibility includes the mapping. See [Civilizations](civilizations.md#numeric-indices-and-saved-games).

## 1.0

Needs Imperivm Resurrected 1.0 or later.

The first numbered version of the SDK. It is in English only.

- **Templates** in [`templates/`](templates): one empty mod for each kind of content (scenario, adventure, conquest, units, civilization, gameplay, audio, translation, graphics) and a full one with every folder the game reads. Each has its `mod.json`, a README with the steps and the folder layout, and a `PUT-YOUR-FILES-HERE.txt` note in every empty folder. They pass the validator.
- **Guides** for everything a mod can do: languages and texts, keys, patches, scripts, civilizations, city and village templates, units, stats, specials, abilities, items, formations, outposts, victory modes, starting bonuses, game options, random maps, images, graphics, [scenarios, adventures and conquests](maps.md) (with cutscenes) and [music, sounds and voices](audio.md), plus the validator, publishing on GameBanana and a reference.
- **Tools** in `tools\`, written in English: `create-unit`, `create-item`, `create-ability`, `create-formation`, `create-option`, `create-outpost`, `create-random-map`, `create-template`, `clone-civilization`, `convert-image`, `convert-sprite`, `create-entity`, `read-rle`, `extract-data`, `validate-mod` and `shrink-mod`. Their options are English (`--name`, `--player`, `--output`). In `create-unit`, `create-ability` and `create-option`, `--name` is the English text and the Spanish one goes in `--name-es`.
- **Examples** in `examples\`, all validating, with English ids (`example-unit`, `example-options`…). `draw-soldier.js`, which redraws the sprites of `unit-graphics-example`, is in `tools\`.
- The Spanish keys of an old `mod.json` are still read by the validator.
- `validate-mod.js` reads `requires`: what the required mods ship (classes, entities, sprites, scripts, civilizations) is not reported as missing. It looks for them with `--mods <folder>`, in `<game>\mods` and next to the mod ([Validating](validating.md)).
