# Changes

The SDK version and which mods you can make with each one. The script API version (`Mod_ApiVersion()`) is separate: it's currently 7.

## Next

- **Conquests:** the conquest screen shows a larger picture of a conquest's map when `imperivm-resurrected\conquest\<conquest file name>\map.png` (or `.jpg`) exists: framed like `global.bmp` (4:3, up to 4096x3072), with the territories where `territories.bmp` has them. The game's tint of each territory goes on it. Each territory also keeps the hardest difficulty it was won at, shown as a medal.
- **Credits:** the credits screen lays out `currentlang\credits.txt` in sections, roles and columns. See [Credits](languages.md#credits) for the format a mod's own file should follow.
- **Help:** the help screen (F1) builds its side bar, its grids of units and its unit cards from `currentlang\help.xml` and the classes the game loaded. See [Help](languages.md#help) for what a mod's own file should follow.
- **Statistics:** scripts that count kills and deaths in the mod variables `EEE_kills<n>` and `EEE_deaths<n>` replace the game's count in the player's statistics. See [Kill and death counters](scripts.md#kill-and-death-counters-for-the-statistics).

## 1.0

Needs Imperivm Resurrected 1.0 or later.

The first numbered version of the SDK. It is in English only.

- **Templates** in [`templates/`](templates): one empty mod for each kind of content (scenario, adventure, conquest, units, civilization, gameplay, audio, translation, graphics) and a full one with every folder the game reads. Each has its `mod.json`, a README with the steps and the folder layout, and a `PUT-YOUR-FILES-HERE.txt` note in every empty folder. They pass the validator.
- **Guides** for everything a mod can do: languages and texts, keys, patches, scripts, civilizations, city and village templates, units, stats, specials, abilities, items, formations, outposts, victory modes, starting bonuses, game options, random maps, images, graphics, [scenarios, adventures and conquests](maps.md) (with cutscenes) and [music, sounds and voices](audio.md), plus the validator, publishing on GameBanana and a reference.
- **Tools** in `tools\`, written in English: `create-unit`, `create-item`, `create-ability`, `create-formation`, `create-option`, `create-outpost`, `create-random-map`, `create-template`, `clone-civilization`, `convert-image`, `convert-sprite`, `create-entity`, `read-rle`, `extract-data`, `validate-mod` and `shrink-mod`. Their options are English (`--name`, `--player`, `--output`). In `create-unit`, `create-ability` and `create-option`, `--name` is the English text and the Spanish one goes in `--name-es`.
- **Examples** in `examples\`, all validating, with English ids (`example-unit`, `example-options`…). `draw-soldier.js`, which redraws the sprites of `unit-graphics-example`, is in `tools\`.
- The Spanish keys of an old `mod.json` are still read by the validator.
