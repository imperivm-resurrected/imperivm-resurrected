# Example: text, voice, sound and icons in a ruleset

One package with **no files in its root** and two rulesets in `rulesets\`. The rulesets differ in the in-game art, sounds, voices and texts that replace the game's: those belong to the ruleset because the host chooses one ruleset per game and only that one is mounted (see [Where art, sounds and texts go](../../rulesets.md#where-art-sounds-and-texts-go)).

| | `plain` | `assets` |
|---|---|---|
| A text (`loc\<language>.loc.xml`) | none | the rollover of the hastatus button reads **Equip Hastatus (assets layer)** (in Spanish *Equipar legionario (capa assets)*, in Italian *Equipaggia astato (livello assets)*) |
| A voice (`local\<language>\voices\RHastatus\1.wav` to `5.wav`) | none | the Roman hastatus answers with a **three-note chord** instead of its voice: rising C-E-G in English, G-C-E in Spanish, E-A-D in Italian |
| A sound (`sounds\Notifications\barrack idle.wav`) | none | the *Training completed* alert, when a barracks finishes a unit, is a **falling three-note bell** |
| A button (`ui\cmdbar\Actions\train RHastatus.bmp`) | none | the hastatus button in the barracks is flat **magenta** (three frames: normal, mouse-over, gray) |
| A portrait (`gameres\icons\RHastatus.bmp`) | none | the hastatus queue icon and portrait are flat **orange** |

The sounds are sine tones made for this example and the images flat colours: nothing is copied from the game. The images and sounds have the same format as the game's (16-bit 5-5-5 bitmaps of 153 x 51 and 65 x 76; 16-bit mono 44.1 kHz PCM sound). `plain` only holds a `README.txt` so that its folder exists.

## What to look at and listen to

Put only this package in a profile and start the game. In the skirmish room, the **Ruleset** row (the first of *Game options*) offers *Plain* and *Assets*. Changing it **restarts the game**: these are loose files that are not data, so they are not switched in place even with `FullHotReload=1`. After the restart, with **Assets**, all five should work:

1. **Text.** Start a game as the **Romans** (Republican Rome) and select the **barracks**; hover the hastatus button: *Equip Hastatus (assets layer)*.
2. **Voice.** Select the hastatus and give orders: the chord. The language of the game decides which one (English, Spanish or Italian); any other language plays the game's own voice, because the example has no folder for it.
3. **Button.** The hastatus button in the barracks is a flat magenta square.
4. **Portrait.** Train two hastati: the queue icons are flat orange, and so is the portrait of the selected unit.
5. **Sound.** Wait until a unit finishes training: the *Training completed* alert plays the falling bell instead of the game's.

With **Plain**, everything is the game's own. For another language, change it in *Options > Language* and restart: only the `local\<language>\` folder of that language is used.

### Why the paths are not all alike

The game turns some folder names into others before it looks for a file: `gameres\` is really `ui\`, `gameini\` is `data\interface\`, `menuini\` is `data\interface\menu\`. The mounted files of a mod are looked up with the **real** name, so the button is in `ui\cmdbar\Actions\` here. The name with `gameres\` in front, which this SDK has always documented, is also served by the game: the portrait is in `gameres\icons\` to show that. Both work with this DLL; the real name does not depend on it.

### Checking what the game used

`Logs\imperivm\imperivm-mod.log` has, at the start, `ruleset example-assets-rulesets/assets: N loose files to watch`, and then one line per file the first time the game opens it:

- `layer file served: ui/cmdbar/actions/train rhastatus.bmp (... bytes, opened as ...)`: the game got the file of the ruleset.
- `layer file NOT served: the engine asked for gameres/icons/rhastatus.bmp and got N bytes ...; the layer's ... has M`: the game opened a file with that name and got another one, its own or another mod's.
- At the end of the load and of the game: `layer files (...): K of N ... opened by the engine so far`, followed by one `not opened yet: <file>` line for each file that was never asked for (a sound that did not play yet is there).

It is cosmetic: other players don't need the package, and two players can play together with different rulesets of it.

## A sound you may not hear

The game plays its alerts on the **UI channel**, and its configuration allows very few sounds at once on a channel (`[SoundChannels]` in `Config.ini`; one for the UI in the copies of the file we have seen). A sound that finds the channel busy is dropped, not queued, and the click of a button uses that channel: an alert that comes right after a click, like *Not enough gold* when you press a train button, can be lost whatever the ruleset. That is why this example uses the *Training completed* alert.
