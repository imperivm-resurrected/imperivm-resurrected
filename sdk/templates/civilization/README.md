# Civilization template

A new civilization that can be picked in the lobby, with its units, buildings and AI.

## Steps

1. Copy this folder into `<game folder>\mods\` and rename it.
2. In `mod.json` change `id` (lowercase letters, numbers and hyphens: it identifies your mod), `name`, `authors` and `description`.
3. Open a terminal in the SDK root (the folder containing `tools`). Extract the game's data once: `node tools\extract-data.js "<game folder>" "<output folder>"`.
4. Clone a civilization from the game:

   ```
   node tools\clone-civilization.js "<output folder>\DATA" "<your mod folder>" myciv --base Gaul
   ```

   It derives the prefix `Myc` and writes `civs\myciv.json`, classes such as `MycTownhall`, commands such as `trainpeasantmyc`, and AI aliases. Your civilization appears **alongside Gaul**, not instead of it: the tool does not enable `variant` or `replacesBase`. Only add those if you deliberately want the behavior in [Variants](../../civilizations.md#variants).

   The required `id` (`myciv`) identifies your civilization; `base` defaults to Gaul. The JSON omits `index`: the engine assigns a free number automatically. Check the [prefix registry](../../prefixes.md), which reserves no numeric slots. `--index 10` (or the old positional argument) is an advanced legacy option only: keep already published explicit numbers unchanged. To choose another prefix before generating, add `--letter Myc2`. The game letters `G R C I M B E T` are reserved and the cloner rejects them. The tool refuses to overwrite generated files: to try different identifiers later, use a fresh output folder and merge deliberately rather than rerunning over your edits.
5. Optional: its own city and village for random maps. `create-template.js` writes them in `patches\randommapsettlements\` ([City and village templates](../../city-templates.md)).
6. Delete the `PUT-YOUR-FILES-HERE.txt` notes and validate the mod with the command below. Enable it in a test profile, restart, and pick the civilization in a local room with a random map. Check recruitment, heroes, sentries, an AI explicitly set to your civilization, and saving/loading before publishing. It initially looks and plays like Gaul; edit its classes and commands to make it different.

## Folder layout

```
my-civilization\
  mod.json
  README.md
  civs\
    myciv.json
  data\
    classes\
      Myc....sc.xml            one per unit and building
    commands\
      Myciv.xml
  patches\
    randommapsettlements\     optional: city and village templates
  loc\
    spanish.loc.xml
```

## Notes

- A civilization changes what happens in the game: everyone in the room needs the mod and the same effective civilization mapping.
- Automatic indices and multi-character prefixes require the matching new DLL build. Indices run from 10 to 127 (118 slots in theory); only a few civilizations at a time have been tested.
- Keep the save's `.mods.json` index table and original profile/content. Adding or removing civilizations can change automatic numbers; a missing or mismatched table prevents loading. See [numeric indices and saved games](../../civilizations.md#numeric-indices-and-saved-games).
- The finished example is [civilization-example](../../examples/civilization-example).

## Check and publish

```
node tools\validate-mod.js "<your mod folder>" --data "<output folder>\DATA"
```

The validator reports problems with file and line without opening the game ([Validating a mod](../../validating.md)). Zip the mod folder with its `mod.json` inside and publish it as described in [Publishing on GameBanana](../../publishing.md).
