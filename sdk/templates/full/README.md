# Full template

Every folder the game reads from a mod, each with a note about what goes in it. Use it when your mod does several things; for one thing, the other templates are shorter:

| You make | Template |
|---|---|
| A map for skirmish and multiplayer | [scenario](../scenario) |
| An adventure or campaign, with or without videos | [adventure](../adventure) |
| A conquest, with or without videos | [conquest](../conquest) |
| A unit or hero | [units](../units) |
| A civilization | [civilization](../civilization) |
| Rules, stats, AI, victory modes, options | [gameplay](../gameplay) |
| Music, sounds, voices | [audio](../audio) |
| The game in another language | [translation](../translation) |
| Textures, sprites, icons, menus | [graphics](../graphics) |

## Steps

1. Copy this folder into `<game folder>\mods\` and rename it.
2. In `mod.json` change `id` (lowercase letters, numbers and hyphens: it identifies your mod), `name`, `authors` and `description`.
3. Pick `category` from the list in [Publishing](../../publishing.md). `tags` is only needed for a map (players) or an adventure, translation or audio pack (language).
4. **Delete every folder you do not use.** An empty folder with its note is harmless, but one that stays under `scenarios\`, `data\` or `patches\` makes the game treat the mod as gameplay: everyone online would need it.
5. In the folders you keep, delete the `PUT-YOUR-FILES-HERE.txt` note when you add your files.

## Folders

| Folder | What goes in it | Counts as gameplay |
|---|---|---|
| `data\classes\` | Unit, building and hero classes (.sc.xml): new ones or copies of the game's. [guide](../../units.md) | yes |
| `data\commands\` | Orders and their buttons (.xml). [guide](../../units.md) | yes |
| `data\GameScripts\` | Victory modes: one .vs per mode. [guide](../../victory-modes.md) | yes |
| `data\BonusScripts\` | Starting bonuses: NNN NAME.vs. [guide](../../bonuses.md) | yes |
| `data\mod\` | Your own scripts, called from events and classes. [guide](../../scripts.md) | yes |
| `patches\data\` | Patches over the game's files, with the same path and name. [guide](../../patches.md) | depends on the file |
| `events\` | Event scripts: OnGameStart.vs, OnObjectDie.vs and so on. [guide](../../scripts.md) | yes |
| `civs\` | New civilizations: <id>.json, one per civilization (omit index for automatic assignment; explicit 10–127 is an optional legacy override). Its units and buildings go in data/classes and its orders in data/commands; clone-civilization.js writes all of it. [guide](../../civilizations.md) | yes |
| `specials\` | New unit specials: *.json. [guide](../../specials.md) | yes |
| `options\` | Lobby switches: *.json. [guide](../../options.md) | yes |
| `scenarios\` | Skirmish and multiplayer maps (.bfhp). | yes |
| `adventures\` | Adventures (.bfhp and the .bmp of the same name). | yes |
| `conquests\` | Conquests (.bfhp), with their folder in conquestmaps. | yes |
| `conquestmaps\` | The folder of each conquest map: territories.xml and its images. | yes |
| `movies\` | Cutscenes (.avi) that a map plays with PlayMovie. | no |
| `music\` | Music (.ogg), same name as the game's to replace it. | no |
| `sounds\` | Sounds (.wav), same path as the game's. | no |
| `loc\` | Texts: <language>.loc.xml. [guide](../../languages.md) | no |
| `units\` | Unit sprites (.rle) and entity files. The .xml and .pass count. [guide](../../graphics.md) | only .xml and .pass |
| `buildings\` | Building sprites and entity files. The .xml and .pass count. [guide](../../graphics.md) | only .xml and .pass |
| `mapobjects\` | Map object files. The .xml and .pass count. | only .xml and .pass |
| `visuals\` | Effects and projectiles. Only the .xml counts. | only .xml |
| `gameres\` | Loose interface images: icons and buttons. [guide](../../images.md) | no |
| `packs\` | Maps (.bfhp) and packs (.pak) mounted next to the game's. | depends on the contents |
| `randommap\` | Random map shapes. [guide](../../random-maps.md) | yes |
| `randommapsettlements\` | City and village templates. [guide](../../city-templates.md) | yes |

The `patches\` folder is not in the template (a note file there would be reported as a bad patch): create it with the path of the game file you patch. Files that are not listed go in the same path they have in the game. Executables and system scripts are not allowed (`.exe .dll .asi .bat .cmd .com .scr .ps1 .vbs .js .msi .sys .cpl .lnk .reg .jar .hta .wsf .pif`): a mod with one does not load. The complete reference is [Reference](../../reference.md).

## Check and publish

```
node tools\validate-mod.js <your mod folder>
```

The validator reports problems with file and line without opening the game ([Validating a mod](../../validating.md)). Zip the mod folder with its `mod.json` inside and publish it as described in [Publishing on GameBanana](../../publishing.md).
