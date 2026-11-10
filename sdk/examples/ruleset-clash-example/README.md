# Example: two packages with a file at the same path

What happens when two packages put a file at the **same game path**? This example has two packages, to be copied together to `<game>\mods\`:

| | `clash-a` | `clash-b` |
|---|---|---|
| What it is | a civilization package (a variant of Republican Rome, `civs\rome-clash-a.json`) | a package with **no files in its root** and two rulesets, `blue` and `plain` |
| The shared file | `gameres\icons\RHastatus.bmp` **in its root**, with a **RED** pattern: a *global* file, which the root of a civilization package may not have | the same `gameres\icons\RHastatus.bmp` in the ruleset `blue`, with a **BLUE** pattern: where it belongs |
| More | nothing | `minimap\zoom{8,16,32}\entities\...` for a ruin (`DRUINS1`) and for a tree (`TR1`, three seasons): bright strategic-map icons of **neutral** map objects, which are global art too |
| Kind | rules (it has `civs\`) | cosmetic (only images, in rulesets) |

`gameres\icons\RHastatus.bmp` is the queue portrait of the Roman hastatus. The game draws it through a **circular mask** with the progress bar over it, so the portraits are not solid colours but a test pattern: four quadrants, a white diagonal stripe and a small ringed square. HD's own portrait has none of that, and the two packages differ in colours, in the direction of the stripe (red: from top-left to bottom-right; blue: from top-right to bottom-left) and in the place of the square (red: left; blue: right), so you can tell which one you see even when the mask hides part of it.

## How to try it

1. Copy `clash-a` and `clash-b` to `<game>\mods\`. In **Options > Content**, a profile with **only these two**.
2. In the skirmish room, the **Ruleset** row (first of *Game options*): **Blue · Clash example B**, then **Plain**, then **Clash example A**. The game restarts when the files that come or go are art (a portrait): between *Blue* and *Clash example A*, or from one of them to *Plain* or *Original*. Between *Plain* and *Original* nothing changes.
3. Play as the game's **Republican Rome**, queue two hastati in the barracks and look at the portraits in the queue (blue, original, red). Zoom out to the strategic view and look at the trees (cyan squares) and, if the map has ruins, the ruin (blue square).
4. Open **Options > Content**, select each package and read its card.
5. Read `Logs\imperivm\imperivm-mod.log` (at the start of the match and at its end).

## What the code says should happen

A rules package's root (a civilization package is one) may **add** files but may not **replace** the game's. `clash-a` has `civs\`, and `gameres\icons\RHastatus.bmp` is a file the game already has (`ui\icons\RHastatus.bmp` in its packs), so that file in `clash-a`'s root is **global**: it is mounted only when the ruleset in use is one of `clash-a`'s own (here, the package itself: it declares none), and ignored under any other, the game's own included. The game's file is the one used then. A file with a name the game does not have is always mounted.

| Ruleset in the room | The portrait is | Why |
|---|---|---|
| **Blue · Clash example B** | **BLUE** (`clash-b`) | `clash-b`'s ruleset has the file and is mounted. `clash-a`'s root file is global and its ruleset is not the one in use: ignored. Nothing is shared between the two any more. |
| **Plain · Clash example B** | the game's **ORIGINAL** | `plain` has no files. `clash-a`'s root file is ignored. |
| The game's own rules (*Original*) | the game's **ORIGINAL** | The same: no ruleset of `clash-a` is in use. |
| **Clash example A** (the package itself) | **RED** (`clash-a`) | Its own ruleset is in use: its global root file is mounted. `clash-b` is not the ruleset: its files are not mounted. |

Before this rule the red portrait won with `plain` and with *Original*, and an `"kind": "rules"` in `clash-b` made `clash-a`'s root beat the ruleset the host chose. None of that happens now: a root can no longer beat a ruleset, and the *Use this pack's files* choice has nothing to choose, because two packages never share a mounted file.

Switching to or from *Blue* or *Clash example A* asks for a restart: the files that come and go are art (a portrait), and art is not switched in place yet. A room that already has the right row needs nothing.

In the content manager, the card of `clash-a` has a line in gold (a notice, not a problem): *1 files of its root replace the game's and are ignored: they apply only when a ruleset of this pack is chosen (gameres/icons/rhastatus.bmp)* under *Blue*, *Plain* and *Original*; under **Clash example A** it says *1 files of its root replace the game's: they apply because a ruleset of this pack is in use*. Neither card offers *Use this pack's files* or *Shared files*.

In the log, `content: clash-a: 1 files of its root replace the game's and are ignored (the ruleset in use is ...)` at the start of the game; with the example's own row, `content: clash-a: 1 files of its root replace the game's and are mounted, because its own ruleset is in use`. With `blue`:

```text
content: layer file served: gameres/icons/rhastatus.bmp (10086 bytes, opened as mods/clash-b/rulesets/blue/gameres/icons/rhastatus.bmp)
content: layer file served: minimap/zoom16/entities/mapobjects-decors-tr1-s-tr1_s.bmp (...)
```

At the end of the game, `layer files (...): K of N ... opened` lists what was never asked for (the ruin, if the map had none).

The validator sees the mistake too, and says what happens to it:

```text
node tools\validate-mod.js sdk\examples\ruleset-clash-example\clash-a --game "<game folder>"
WARNING  gameres/icons/RHastatus.bmp  global in-game file in the root of a rules package (a civilization pack is one): it is ignored unless a ruleset of this package is chosen; put it in a ruleset layer (rulesets/<id>/)
```

## What is not here

A **sprite** of a neutral building (a loose `.ent.xml` and `.rle`) is not in this example. `mapobjects\` and `buildings\` entities are gameplay files (they define the passability), so a ruleset that holds one is a **rules** ruleset: it would rank with `clash-a` and change the prediction above. The bitmaps of the strategic map are cosmetic.
