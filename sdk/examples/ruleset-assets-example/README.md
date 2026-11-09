# Example: text, voice, sound and icons in a ruleset

One package with **no files in its root** and two rulesets in `rulesets\`. The rulesets differ in the in-game art, sounds, voices and texts that replace the game's: those belong to the ruleset because the host chooses one ruleset per game and only that one is mounted (see [Where art, sounds and texts go](../../rulesets.md#where-art-sounds-and-texts-go)).

| | `plain` | `assets` |
|---|---|---|
| A text (`loc\<language>.loc.xml`) | none | the rollover of the hastatus button reads **Equip Hastatus (assets layer)** (in Spanish *Equipar legionario (capa assets)*, in Italian *Equipaggia astato (livello assets)*) |
| A voice (`local\<language>\voices\RHastatus\1.wav` to `5.wav`) | none | the Roman hastatus answers with a **three-note chord** instead of its voice: rising C-E-G in English, G-C-E in Spanish, E-A-D in Italian |
| A sound (`sounds\Notifications\not enough.wav`) | none | the **not enough gold** alert is a low double buzz |
| A button (`gameres\cmdbar\Actions\train RHastatus.bmp`) | none | the hastatus button in the barracks is flat **magenta** (three frames: normal, mouse-over, gray) |
| A portrait (`gameres\icons\RHastatus.bmp`) | none | the hastatus queue icon and portrait are flat **orange** |

The sounds are sine tones made for this example and the images are flat colours: nothing is copied from the game. `plain` only holds a `README.txt` so that its folder exists.

## What to look at and listen to

Put only this package in a profile and start the game. In the skirmish room, the **Ruleset** row (the first of *Game options*) offers *Plain* and *Assets*. Changing it **restarts the game**: these are loose files that are not data, so they are not switched in place even with `FullHotReload=1`. After the restart, with **Assets**:

1. Start a game as the **Romans** (Republican Rome) and select the **barracks**. The hastatus button is a flat magenta square; hover it: the text reads *Equip Hastatus (assets layer)*.
2. Train one: the queue icon is orange. Select the unit and give it an order: it answers with a chord (the language of the game decides which: English, Spanish or Italian; any other language plays the game's own voice, because the example has no folder for it).
3. Try to train a unit with less gold than it costs: the *not enough gold* alert sounds low.

With **Plain**, everything is the game's own. For another language, change it in *Options > Language* and restart: only the `local\<language>\` folder of that language is used.

`Logs\imperivm\imperivm-mod.log` shows what was mounted: `ruleset "assets" of ... mounted`, and with `[Debug] LogFiles=1` every file the game opens, so you can see `currentlang/voices/RHastatus/1.wav` served from `mods/example-assets-rulesets/rulesets/assets/Local/<language>/...`.

It is cosmetic: other players don't need the package, and two players can play together with different rulesets of it.
