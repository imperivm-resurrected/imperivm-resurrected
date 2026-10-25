# Changes

The SDK version and which mods you can make with each one. The script API version (`Mod_ApiVersion()`) is separate: it's currently 7.

## 1.0

Needs Imperivm Resurrected 1.0 or later.

The first numbered version of the SDK. It is in English only.

- **Templates** in [`templates/`](templates): one empty mod for each kind of content (scenario, adventure, conquest, units, civilization, gameplay, audio, translation, graphics) and a full one with every folder the game reads. Each has its `mod.json`, a README with the steps and the folder layout, and a `PUT-YOUR-FILES-HERE.txt` note in every empty folder. They pass the validator.
- **Guides** for everything a mod can do: languages and texts, keys, patches, scripts, civilizations, city and village templates, units, stats, specials, abilities, items, formations, outposts, victory modes, starting bonuses, game options, random maps, images, graphics, [scenarios, adventures and conquests](maps.md) (with cutscenes) and [music, sounds and voices](audio.md), plus the validator, publishing on GameBanana and a reference.
- **Tools** in `tools\`, written in English: `create-unit`, `create-item`, `create-ability`, `create-formation`, `create-option`, `create-outpost`, `create-random-map`, `create-template`, `clone-civilization`, `convert-image`, `convert-sprite`, `create-entity`, `read-rle`, `extract-data`, `validate-mod` and `shrink-mod`. Their options are English (`--name`, `--player`, `--output`). In `create-unit`, `create-ability` and `create-option`, `--name` is the English text and the Spanish one goes in `--name-es`.
- **Examples** in `examples\`, all validating, with English ids (`example-unit`, `example-options`…). `draw-soldier.js`, which redraws the sprites of `unit-graphics-example`, is in `tools\`.
- The Spanish keys of an old `mod.json` are still read by the validator.
- `validate-mod.js` reads `requires`: what the required mods ship (classes, entities, sprites, scripts, civilizations) is not reported as missing. It looks for them with `--mods <folder>`, in `<game>\mods` and next to the mod ([Validating](validating.md)).
