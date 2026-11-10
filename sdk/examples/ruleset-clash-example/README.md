# Example: two packages with a file at the same path

What happens when two packages put a file at the **same game path**? This example has two packages, to be copied together to `<game>\mods\`:

| | `clash-a` | `clash-b` |
|---|---|---|
| What it is | a civilization package (a variant of Republican Rome, `civs\rome-clash-a.json`) | a package with **no files in its root** and two rulesets, `blue` and `plain` |
| The shared file | `gameres\icons\RHastatus.bmp` **in its root**, with a **RED** pattern: where it should not be | the same `gameres\icons\RHastatus.bmp` in the ruleset `blue`, with a **BLUE** pattern: where it belongs |
| More | nothing | `minimap\zoom{8,16,32}\entities\...` for a ruin (`DRUINS1`) and for a tree (`TR1`, three seasons): bright strategic-map icons of **neutral** map objects, which are global art too |
| Kind | rules (it has `civs\`) | cosmetic (only images, in rulesets) |

`gameres\icons\RHastatus.bmp` is the queue portrait of the Roman hastatus. The game draws it through a **circular mask** with the progress bar over it, so the portraits are not solid colours but a test pattern: four quadrants, a white diagonal stripe and a small ringed square. HD's own portrait has none of that, and the two packages differ in colours, in the direction of the stripe (red: from top-left to bottom-right; blue: from top-right to bottom-left) and in the place of the square (red: left; blue: right), so you can tell which one you see even when the mask hides part of it.

## How to try it

1. Copy `clash-a` and `clash-b` to `<game>\mods\`. In **Options > Content**, a profile with **only these two**.
2. In the skirmish room, the **Ruleset** row (first of *Game options*): **Blue · Clash example B**. Changing it restarts the game.
3. Play as the game's **Republican Rome**, queue two hastati in the barracks and look at the portraits in the queue. Zoom out to the strategic view and look at the trees (cyan squares) and, if the map has ruins, the ruin (blue square).
4. Open **Options > Content**, select each package and read its card.
5. Read `Logs\imperivm\imperivm-mod.log` (at the start of the match and at its end).

## What the code says should happen

Before the run, from `ModsLoadOrder` / `ClashAnalyze` / `MountProfileLayers` (the PR that added this example has the file and line of each):

| Setup | Who wins the portrait | Why |
|---|---|---|
| **As it is** (ruleset `blue`) | **BLUE** (clash-b) | The load order sorts packages by kind, *cosmetic above content above rules*, then by id. `clash-b` is cosmetic (its rulesets only hold images), `clash-a` is rules (it has `civs\`), so `clash-b` is mounted first and wins. Its ruleset sits right above its own root, which has no files. |
| Ruleset `plain` (or `clash-a`'s own) | **RED** | `blue` is not mounted: only `clash-a` has the file. |
| *Use this pack's files* on the `clash-a` card (content manager) | **RED** | A file that only changes images is *choosable*: the player's choice overrides the load order. After a restart. |
| `"kind": "rules"` added to `clash-b\mod.json`, ruleset `blue` | **RED** | Both are rules now, the tie is broken by id (`clash-a` < `clash-b`) and a ruleset sits at the place of its package, **below** the root of `clash-a`: a root file of another package beats the ruleset the host chose. This is an open design question (see the pull request). |

In the content manager:

- The card of **each** package has the line *Same files as (the other pack) (1): ...* in gold, not a red problem: the two packages only share an image, which is not gameplay, so it is information. `clash-a` says the files of `clash-b` are used; `clash-b` says its own are.
- `clash-a`'s card offers **Use this pack's files**; `clash-b`'s card offers **Use the files of Clash example A...**. They are there because the shared file is an image (loose, not inside a `.pak`).
- *Shared files (1)* lists `gameres/icons/rhastatus.bmp`.

In the log (the DLL says, once per name, who served a file that several packs have):

```text
content: 1 files are in more than one active pack or ruleset: the log says which one serves each
content: file gameres/icons/rhastatus.bmp served from clash-b/blue (also in: clash-a)
content: layer file served: gameres/icons/rhastatus.bmp (10086 bytes, opened as mods/clash-b/rulesets/blue/gameres/icons/rhastatus.bmp)
content: layer file served: minimap/zoom16/entities/mapobjects-decors-tr1-s-tr1_s.bmp (...)
```

If the load order and what was served disagree, there is a second line, a warning: `... comes first in the load order, but ... served it`. At the end of the game, `layer files (...): K of N ... opened` lists what was never asked for (the ruin, if the map had none).

The validator sees the mistake too:

```text
node tools\validate-mod.js sdk\examples\ruleset-clash-example\clash-a --game "<game folder>"
WARNING  gameres/icons/RHastatus.bmp  global in-game file in the root of a civilization pack: put it in a ruleset layer (rulesets/<id>/)
```

## What is not here

A **sprite** of a neutral building (a loose `.ent.xml` and `.rle`) is not in this example. `mapobjects\` and `buildings\` entities are gameplay files (they define the passability), so a ruleset that holds one is a **rules** ruleset: it would rank with `clash-a` and change the prediction above. The bitmaps of the strategic map are cosmetic.
