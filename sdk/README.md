# Imperivm Resurrected SDK

Imperivm Resurrected is a mod for Imperivm HD (Great Battles of Rome) that opens the game up to mods that weren't possible before: new civilizations, abilities, items, victory modes, languages, scripts that react to the game, and more. These are the guides and tools for making them. Each guide comes with an example mod in [`examples/`](examples).

## Start here

1. Install Imperivm Resurrected: copy its contents into the game folder (`...\steamapps\common\Imperivm - Great Battles of Rome HD`) and launch the game from Steam.
2. Copy a folder from [`templates/`](templates) into `mods\`, inside the game folder: it has the exact layout the game expects for what you want to make (see [Templates](#templates)). To see a finished mod, copy one from [`examples/`](examples), for example `patches-example`.
3. In the main menu, click **Additional content** and enable the mod in your profile.
4. Restart the game if it didn't restart on its own.
5. Open `Logs\imperivm\imperivm-mod.log`, in the game folder. It lists your mod's errors and what it loaded.

Before you launch the game, run your mod through the [validator](validating.md).

## What a mod looks like

A folder inside `mods\` with a `mod.json` and the files it changes or adds, using the same paths as the game:

```
mods\
  my-mod\
    mod.json
    data\classes\MyUnit.sc.xml       <- new file, or one that replaces the game's
    loc\english.loc.xml              <- texts added to the game's
```

## Templates

A template is an empty mod with the folders the game reads, a valid `mod.json` and a README with the steps. Copy the one that matches what you make, rename it, fill in `mod.json` and put your files where the notes say. Each empty folder has a `PUT-YOUR-FILES-HERE.txt` note: delete it when you add your files.

| You make | Template | Guide |
|---|---|---|
| A map for skirmish and multiplayer | [`scenario`](templates/scenario) | [Maps](maps.md) |
| An adventure or campaign, with or without videos | [`adventure`](templates/adventure) | [Maps](maps.md) |
| A conquest, with or without videos | [`conquest`](templates/conquest) | [Maps](maps.md) |
| A unit or hero | [`units`](templates/units) | [New units](units.md) |
| A civilization | [`civilization`](templates/civilization) | [Civilizations](civilizations.md) |
| Rules, stats, AI, victory modes, options | [`gameplay`](templates/gameplay) | [Patches](patches.md), [Scripts](scripts.md) |
| Music, sounds, voices | [`audio`](templates/audio) | [Audio](audio.md) |
| The game in another language | [`translation`](templates/translation) | [Languages](languages.md) |
| Textures, sprites, icons, menus | [`graphics`](templates/graphics) | [Custom graphics](graphics.md) |
| A civilization package with its own global rules, chosen per room | [`ruleset`](templates/ruleset) | [Rulesets](rulesets.md) |
| Several of the above | [`full`](templates/full) | [Reference](reference.md) |

Don't include executables or system scripts (`.exe .dll .asi .bat .cmd .com .scr .ps1 .vbs .js .msi .sys .cpl .lnk .reg .jar .hta .wsf .pif`). If there's one, the mod won't load. A stray `.js`, like a generator of your own, counts.

## `mod.json`

```json
{
  "id": "my-mod",
  "name": "My mod",
  "version": "1.0",
  "authors": ["Me"],
  "description": "What my mod does.",
  "category": "Gameplay & Balance",
  "redistributable": true
}
```

| Field | What it's for |
|---|---|
| `id`, `name`, `version`, `authors`, `description` | The mod's card in the content manager. `name` and `description` take either a single text or an object with one text per language (the game language is shown; if missing, English; if missing, the first). |
| `category`, `tags` | The main GameBanana category and the tags: more categories (`Type`), players (`Players`) and language (`Language`). See [Publishing](publishing.md). |
| `redistributable` | With `true`, the lobby host can send the mod to players who don't have it. Defaults to `false`. |
| `license` | Free text with the license. |
| `requires` | Mods it needs: a list of ids, or of objects `{"id": "...", "version": "..."}`. |
| `incompatibleWith` | List of mod ids it can't be enabled with. |
| `loadAfter`, `loadBefore` | What you ask of the [load order](reference.md#1-mod-structure), by id; the game works out the rest. With `loadAfter`, your mod goes on top of that one: its files, patches and texts win (except a file the other one ships loose and yours inside a `.pak`: the loose one wins). |
| `sources` | `{"url": "...", "gamebanana": 123456}`. The game writes it when you install from Browse content; it's used to flag new versions. If it comes from the Sounds section, the number is in `gamebananaSound`. |
| `language` | Declares a new language. See [Languages](languages.md). |
| `commands` | Name of each command file on the Controls screen. See [Mod keys](keys.md). |
| `rulesets` | Folders of the mod with its global rules, of which the host of a room chooses one. See [Rulesets](rulesets.md). |

> Up to 0.31.50 the names of these fields were in Spanish (`nombre`, `autores`, `descripcion`, `etiquetas`, `categoria`, `redistribuible`, `licencia`, `requiere`, `incompatibleCon`, `cargarDespuesDe`, `cargarAntesDe`, `fuentes`, `idioma`, `ordenes`…). The old ones are still read, but write the new ones.

## Guides

- [Languages and texts](languages.md): change game texts or add a language without replacing the whole file.
- [Mod keys](keys.md): how your mod's commands show up in Controls and how to name them.
- [Patches](patches.md): change part of a game XML, INI or script without replacing it; two mods can touch the same file.
- [Scripts](scripts.md): `.vs` scripts, events and the `Mod_...` functions.
- [Civilizations](civilizations.md): declare a new civilization or clone one from the game, with its AI, and play it in the lobby.
- [City and village templates](city-templates.md): a mod civilization's own starting city and village (`create-template.js`).
- [New units](units.md): class, recruit command, images and texts, reusing another unit's model.
- [Unit stats](stats.md): which attribute does what, which ones do nothing and which ones crash the game.
- [Unit specials](specials.md): new unit specials and their effect through scripts.
- [Troop abilities](abilities.md): new abilities for units or heroes (a command with button and key, plus a script method).
- [New items](items.md): hero amulets, herbs and gems.
- [New formations](formations.md): hero formations with their bonuses and button.
- [Outposts and buildings](outposts.md): new neutral outposts on random maps (`create-outpost.js`) and tower, wall and gate classes.
- [Victory modes](victory-modes.md): a script in `data\GameScripts` adds a mode to the lobby's "Mode" dropdown.
- [Starting bonuses](bonuses.md): scripts in `data\BonusScripts` add choices to the "Bonus" column of the lobby.
- [Rulesets](rulesets.md): a package keeps its civilizations in the root and its global rules in a folder the host chooses per room.
- [Custom game options](options.md): new switches in the lobby rules, which scripts read with `Mod_GetOption`.
- [Scenarios, adventures and conquests](maps.md): the three kinds of map, the files each needs and cutscenes.
- [Music, sounds and voices](audio.md): replace or add audio.
- [Random map types](random-maps.md): a four-color drawing becomes a new map type (`create-random-map.js`).
- [Custom images](images.md): command buttons and unit icons from a PNG.
- [Custom graphics](graphics.md): custom sprites for units and buildings (`convert-sprite.js`, `create-entity.js`, `read-rle.js`).
- [Validating a mod](validating.md): check a mod without opening the game, with the file and line of each problem.
- [Publishing on GameBanana](publishing.md): categories, tags, uploading, and mods made before the SDK.
- [Reference](reference.md): the full list of what a mod can declare (files, fields, patches, events, functions and limits).

## Tools

They're in `tools\` and need [Node.js](https://nodejs.org). Open a terminal in this folder and run them from here (`node tools\validate-mod.js ...`). Each one explains its usage when you run it with no arguments, and the topic's guide has examples.

| You want… | Use |
|---|---|
| A new unit | `create-unit.js` ([guide](units.md)) |
| A new item | `create-item.js` ([guide](items.md)) |
| A new ability | `create-ability.js` ([guide](abilities.md)) |
| A new formation | `create-formation.js` ([guide](formations.md)) |
| A game option | `create-option.js` ([guide](options.md)) |
| A new civilization | `clone-civilization.js` ([guide](civilizations.md)) |
| A civilization's own city and village | `create-template.js` ([guide](city-templates.md)) |
| A neutral outpost | `create-outpost.js` ([guide](outposts.md)) |
| A random map type | `create-random-map.js` ([guide](random-maps.md)) |
| Buttons and icons from a PNG | `convert-image.js` ([guide](images.md)) |
| Sprites from PNG, and a unit or building's entity file | `convert-sprite.js`, `create-entity.js`, `read-rle.js` ([guide](graphics.md)) |
| Redraw the sprites of `unit-graphics-example` | `draw-soldier.js` ([example](examples/unit-graphics-example)) |
| Check a mod without opening the game | `validate-mod.js` ([guide](validating.md)) |
| Adapt a mod made before the SDK | `shrink-mod.js` ([guide](publishing.md)) |
| Extract the game data | `extract-data.js` (below) |

## Game data

Several tools ask for the game's `DATA` folder. `extract-data.js` extracts it (you need [Node.js](https://nodejs.org)):

```
node tools/extract-data.js "<game folder>" <output folder>
```

It leaves `data.pak` in `<output folder>\DATA`. That's the folder that `clone-civilization.js`, `create-outpost.js` and `validate-mod.js --data` ask for.

## What counts as gameplay?

In multiplayer, everyone must run the same mods if they change what happens in the game. The game works this out from the folders your mod touches: `data/`, `scenarios/`, `adventures/`, `conquests/`, `civs/`, `specials/`, `events/`, `options/`, `rules/`, `modes/`, `randommap/`, `randommapsettlements/`, the `.xml` and `.pass` files in `units/`, `buildings/` and `mapobjects/`, the `.xml` files in `visuals/`, the `.bfhp` maps in `packs/`, and patches (depending on the file they patch).

Graphics, sounds, texts (`loc/`) and languages don't count: each player can use their own.

## If something fails

Your mod's errors show on its card in the content manager and in `Logs\imperivm\imperivm-mod.log`. Each line of the log has the time, a level (`I` information, `W` warning, `E` error) and a tag for the part of the mod it comes from: `patch`, `language`, `civilizations`, `specials`, `script`, `event`. Look at the `W` and `E` lines of your mod's tags first.

The log keeps the last five sessions (`imperivm-mod.1.log` to `imperivm-mod.4.log`) and, if the game closed badly, `imperivm-mod.crashed.log`. For more detail about one part, put its tag and a level under `[Debug]` in `imperivm-mod.ini`, for example `LogChannels=patch:debug`; `LogLevel=debug` does it for everything.

To ask for help, click the heart at the bottom left of the main menu and then **Create report**. It makes a zip with the logs and your system, without your Windows user name, Steam IDs or IP addresses, and sends nothing: you decide whether to share it.

## Version and license

The SDK version and what changed in each one are in [Changes](CHANGELOG.md). The license is in [License](LICENSE.md).
