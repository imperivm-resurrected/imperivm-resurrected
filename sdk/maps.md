# Scenarios, adventures and conquests

The three kinds of map the game has. They are made in the game's map editor and saved as a `.bfhp` file; a mod only has to put that file, and the few files that go with it, in the right folder.

| Kind | Folder in the mod | Files | Template |
|---|---|---|---|
| Scenario (skirmish and multiplayer map) | `scenarios\` | `MyMap.bfhp` | [scenario](templates/scenario) |
| Adventure (single-player missions and campaigns) | `adventures\` | `MyAdventure.bfhp` and `MyAdventure.bmp` | [adventure](templates/adventure) |
| Conquest | `conquests\` and `conquestmaps\` | `MyConquest.bfhp` and a folder of images | [conquest](templates/conquest) |

The kind is not chosen by the folder: the map itself states it, according to what its author picked in the editor. The folder has to match. An adventure or a conquest in `scenarios\` does not appear in the Scenarios list, and the [validator](validating.md) warns about it.

## Scenarios

Put the `.bfhp` in `scenarios\`. When someone picks it in the room, the game draws the preview from the map's own terrain, with its players and their civilizations: there is no image to provide. The colors come from the game's minimap tiles, so a new terrain looks right if it brings its tile in `minimapzoom8\terrain`.

In `mod.json` use `"category": "Scenarios"` and tell how many players the map has with a `Players` tag (`"Players: 2"`, `"Players: 2-4"`). See [Publishing](publishing.md#tags).

## Adventures

Each adventure is two files with the same name, in `adventures\`: the `.bfhp` and a `.bmp` (`The Punic Wars.BFHP` and `The Punic Wars.bmp`). The game's own campaigns keep each series in a subfolder: `Adventures\GreatBattles\` and `Adventures\GreatChallenges\`, with the missions numbered.

In `mod.json` use `"category": "Adventures"` and a `Language` tag with the language of its texts.

## Conquests

A conquest is a `.bfhp` in `conquests\` and a folder in `conquestmaps\` with the images and the list of territories. The game's own, `Conquests\mediterranean.BFHP`, stores the name of its folder inside: `ConquestMaps/1 - GBR Europe`. Your conquest does the same, so **the folder of `conquestmaps\` has to be called exactly what the map says**.

The folder of the game's conquest has these files:

| File | What it is |
|---|---|
| `territories.xml` | The territories (`id`, `index`, `state`, `neighbours`) and the colors the map uses for owned, enemy and disabled territories. `data=` repeats the folder name. |
| `territories.bmp` | 1024x768, 8-bit (256 colors). |
| `global.bmp` | 1024x768, 16-bit. |
| `Shield<Name>.bmp` | 74x79, 16-bit, one per territory (`ShieldSpain.bmp` for `Spain`), and `ShieldArrow.bmp`. |

The `territories.xml` stored inside the conquest's `.bfhp` has more per territory (`visualname`, `mapname`, `description`, `bonus`...). Its `interface` says which civilization the player plays when they choose that territory: 0 to 7 for the game's civilizations (Gaul, RepublicanRome, Carthage, Iberia, ImperialRome, Britain, Egypt, Germany) or the `index` of a [civilization](civilizations.md) of a mod (10 to 127). Needs the DLL build that has this; the original 1.0 release only reads 0 to 7.

The territories of the game's conquest are `Spain`, `Britain`, `Gaul`, `Italy`, `Carthage`, `Egypt` and `Germany`. To make yours, copy the folder of the game (`<game folder>\ConquestMaps\1 - GBR europe`) and change it: it is the quickest way to get `index` and `state` right. The [conquest template](templates/conquest) has the layout and a trimmed `territories.xml`.

## Cutscenes

An adventure or a conquest can play videos. Put each `.avi` in `movies\` in the mod and play it from the map's script, in the editor:

```
PlayMovie(Translate("movies\\MyVideo.avi"));
```

That is how the game's own adventures call theirs (`movies\\1_Intro_Great_B_Zama.avi`). The game looks for the file in the active mods first, in [load order](reference.md#1-mod-structure), and then in its own `Movies\` folder, so a mod can also replace one of the game's videos.

The game plays videos through Windows (DirectShow), so the computer has to be able to decode the format. The game's own videos are AVI files with Xvid video (1920x1080, 25 frames per second) and PCM audio: use the same format.

Videos are the same in every language and do not count as gameplay.

## In multiplayer

Maps, adventures and conquests are gameplay files: everyone in the room needs the same one. With `"redistributable": true` in `mod.json` the host can send the mod to players who do not have it.

## Check and publish

```
node tools\validate-mod.js <your mod folder>
```

Then follow [Publishing on GameBanana](publishing.md). A `.zip` that only holds the `.bfhp`, without folders, is placed by the game itself when it is installed, by the kind the map states.
