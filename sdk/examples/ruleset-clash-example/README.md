# Example: two packages with a file at the same path

What happens when two packages put a file at the **same game path**? This example has two packages, to be copied together to `<game>\mods\`:

| | `clash-a` | `clash-b` |
|---|---|---|
| What it is | a civilization package (a variant of Republican Rome, `civs\rome-clash-a.json`) | a package with **no files in its root** and two rulesets, `blue` and `plain` |
| The shared file | `gameres\icons\RHastatus.bmp` **in its root**, with a **RED** pattern: a *global* file, which the root of a civilization package may not have | the same `gameres\icons\RHastatus.bmp` in the ruleset `blue`, with a **BLUE** pattern: where it belongs |
| More | the Roman barracks button `gameres\cmdbar\Actions\train RHastatus.bmp` (**RED**) and the gold icon of the costs `gameres\infobar\common\gold_cost.bmp` (a **RED** square) | the same two files in blue, and `minimap\zoom{8,16,32}\entities\...` for a ruin (`DRUINS1`) and for a tree (`TR1`, three seasons): bright strategic-map icons of **neutral** map objects, which are global art too |
| Kind | rules (it has `civs\`) | cosmetic (only images, in rulesets) |

`gameres\icons\RHastatus.bmp` is the queue portrait of the Roman hastatus. The game draws it through a **circular mask** with the progress bar over it, so the portraits are not solid colours but a test pattern: four quadrants, a white diagonal stripe and a small ringed square. HD's own portrait has none of that, and the two packages differ in colours, in the direction of the stripe (red: from top-left to bottom-right; blue: from top-right to bottom-left) and in the place of the square (red: left; blue: right), so you can tell which one you see even when the mask hides part of it.

## How to try it

1. Copy `clash-a` and `clash-b` to `<game>\mods\`. In **Options > Content**, a profile with **only these two**.
2. In the skirmish room, the **Ruleset** row (first of *Game options*): **Blue · Clash example B**, then **Plain**, then **Clash example A**. None of these changes restarts the game: the images are **switched in place** (the game's picture caches are emptied between the two matches, see "Images in place" below). Between *Plain* and *Original* nothing changes.
3. Play as the game's **Republican Rome**, select the **barracks** (the hastatus button is flat blue, original or red), hover the buttons (the gold icon of the price is a blue, original or red square), queue two hastati and look at the portraits in the queue (blue, original, red). Zoom out to the strategic view and look at the trees (cyan squares) and, if the map has ruins, the ruin (blue square). Then leave the match, change the row and start another: the match must show the new images, with no restart.
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

Switching to or from *Blue* or *Clash example A* is made in place: the files that come and go are images of the interface (a portrait, a button, a cost icon, a strategic-map icon), which the game reads again for each match once its picture caches are empty (next section). A room that already has the right row needs nothing.

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

## Images in place (`clash-a` and `clash-b`, with `gameres\`)

Every ruleset below is switched **without restarting**, between two matches or from the in-game menu (Load): the game keeps the images of its interface in caches that last until it exits (the file cache, by name; the cache of the interface's windows; the frame of the command bar), so a new match would be handed the old picture of the same name. The DLL empties them between the two matches, after the old game's tables and windows have let go of their pictures. If the name of an image that changes is still in use by something that stays loaded, the switch **does not happen in place** and the game restarts instead of showing the old picture: the dialog says so, with the names, when the game already knows it (the windows of the interface are asked live; a name that an earlier switch found in use is remembered for the session); a name that nobody knew about is found right after the caches are emptied, before the match is entered, and the game restarts then.

| Ruleset | Barracks button | Gold icon in the prices | Queue icon and portrait of the hastatus | Strategic map (ruin, trees) |
|---|---|---|---|---|
| **Original** | the game's | the game's (gold coin) | the game's | the game's |
| **Plain** (`clash-b`) | the game's | the game's | the game's | the game's |
| **Blue** (`clash-b`) | flat **BLUE** (three frames: normal, lighter, gray) | **BLUE** square | the **BLUE** pattern | cyan trees, blue ruin |
| **Clash example A** | flat **RED** | **RED** square | the **RED** pattern | the game's |

The switches to try, none of which restarts: Blue -> Plain -> Original -> Blue -> Clash example A -> Plain -> Blue (and the same from the in-game menu: Load a save made under another ruleset). **Predicted, not yet seen in the game**: all of it.

What the log says for each switch (`Logs\imperivm\imperivm-mod.log`):

```text
content: ruleset changed without a restart: clash-b:blue -> clash-b:plain (files of the new one: nothing; ...)
game: hot reload: starting
game: art cache (before the reload): file cache N entries (H in use; the game counts N), interface cache B bytes unreferenced
game: hot reload: image caches emptied in T ms: files N entries (H in use) -> M (K in use), interface B -> 0 bytes unreferenced, command-bar frame set to load again, purge file 1; 5 images change, 0 of them still cached
game: art cache (after the reload): file cache M entries (K in use ...), interface cache 0 bytes unreferenced
```

`5 images change, 0 of them still cached` is the proof that no old picture can come back; a number other than 0 would be the restart (`hot reload: images that this switch replaces are still in use by the game (...)`) with the names. `purge file 1` is the first of the files the DLL loads only to make the game's own purge run; a higher number means the first ones were in use.

### Stress: five rounds and the memory

Start the game with `[Debug] MemoryTrace=1` in `imperivm-mod.ini` (slower loads: a second or two per game). Pick *Blue*, start a skirmish, leave it, pick *Plain*, start, leave, *Original*, start, leave, *Blue*..., five times round. Then:

```text
node sdk\tools\memory-report.js "<game>\Logs\imperivm\imperivm-mod.log"
```

prints, per game that started, the process commit and working set, the entity cache, the image caches and the heap checks, and the growth from the first game to the last. What to read: `failed HeapValidate` is 0 in every row; the file cache entries and the unreferenced bytes of the interface cache do not grow from round to round (they are emptied at every switch); the commit grows by the weight of one extra game, not by one per switch.

## Second round: `clash-c` and `clash-d` (what the root rule does with a data file, a new name and the menu skin)

The first round is about an image. These two packages are about the three kinds of root file the rule treats differently, and about a package **without civilizations**:

| | `clash-c` | `clash-d` |
|---|---|---|
| What it is | a civilization package (`civs\rome-clash-c.json`, a variant of Republican Rome) | a **rules package without civilizations** (`"kind": "rules"`): its root is its ruleset |
| (a) a **game data file** in its root, which replaces the game's | `data\commands\RBarracks.xml`: a copy of the game's command file of the Roman barracks with the hastatus at **77** gold | the same file with the hastatus at **88** gold |
| (a2) a **root patch** that **changes** a game value | `patches\data\commands\RBarracks.xml`: `<set>` of the Roman **archer** `costgold` 80 -> **55** (the same file also holds the new command of (b): a patch that mixes an add and a change) | `patches\data\commands\RBarracks.xml`: `<set>` of the Roman **scout** `costgold` 140 -> **99** |
| (b2) a **root patch** that only **adds** | the new command `trainClashC` (see (b)) | a new command `trainClashD`, a third hastatus button with the game's own images, key `k`, price 100 |
| (b) a **new-name** file | `gameres\icons\ClashCUnit.bmp` and `gameres\cmdbar\Actions\ClashCUnit.bmp`, used by a **new command** `trainClashC` that `patches\data\commands\RBarracks.xml` adds to the barracks: a second hastatus button with a green-and-yellow icon, at the game's price (100) | nothing |
| (c) a **menu skin** file | `gameres\menu\button.bmp`: flat **green menu buttons** | nothing |

The two copies of the game's command file are **not in the repository** (a file of the game does not go in it): `make-clash-files.js` makes them from your own game (it reads `Packs\data.pak`, takes `DATA\COMMANDS\RBARRACKS.XML` out and changes only the `costgold` of `trainRHastatus`). Without them both packages are valid and do nothing about prices.

### Setup

1. Copy `clash-b`, `clash-c` and `clash-d` to `<game>\mods\` (`clash-a` can stay out: it is the first round).
2. Make the two files, from the folder of this example: `node make-clash-files.js "<game folder>" "<game folder>\mods"`. It prints the two files it wrote (`clash-c\data\commands\RBarracks.xml`, `clash-d\data\commands\RBarracks.xml`).
3. In **Options > Content**, a profile with **only `clash-b`, `clash-c` and `clash-d`**. Restart the game.
4. Add to `imperivm-mod.ini`, to read the price and the files without guessing: `[Debug]` `Dump=data/commands/RBarracks.xml` (the file the game read, patches applied, is copied to `mod-dump\data\commands\RBarracks.xml`: look at the `costgold` of `trainRHastatus`) and, for the files, `LogFiles=1` and `LogLevel=debug`.
5. In the skirmish room play the game's **Republican Rome**, build a barracks and look at the **Hastatus** button: its price is in the tooltip (and the queue shows the portrait).

### What must happen, per ruleset (the Ruleset row)

| Ruleset | Hastatus price | Second hastatus button `trainClashC` | Menu buttons | Switching to it from *Original* |
|---|---:|---|---|---|
| **Original** (the game's own) | **100** | yes (green icon, 100) | green | - |
| **Blue · Clash example B** | **100** | yes | green | in place (its images are switched in place) |
| **Plain · Clash example B** | **100** | yes | green | in place |
| **Clash example C** (the package) | **77** | yes | green | in place (the only global file is `data\...xml`) |
| **Clash example D** (the package) | **88** | yes | green | in place |

**Patches (the root patches of (a2) and (b2)).** A root patch may add anywhere but its operations that change what the game has apply only under the package's own ruleset. So, in the barracks, besides the hastatus:

| Ruleset | Archer | Scout | Buttons `trainClashC` and `trainClashD` |
|---|---:|---:|---|
| **Original**, **Blue**, **Plain** | **80** | **140** | both (adds apply everywhere) |
| **Clash example C** | **55** | **140** | both |
| **Clash example D** | **80** | **99** | both |

Under *Clash example C* the scout stays at the game's 140 (`clash-d`'s change is skipped there) and under *D* the archer stays at 80 (`clash-c`'s is skipped). *Original*, *Plain* and *Blue* have both skipped. Switching between *Original*, *Plain*, *C* and *D* stays in place (the patches are data).

The price is the only thing that follows the ruleset among the **files**: **(a)** is global, so it is mounted only under the package's own entry. **(b)** and **(c)** never change: a new name is always mounted, and the menu skin is exempt. Switching between *Original*, *Plain*, *C* and *D* does not restart (a data file); *Blue* is switched in place too (its images). The `Dump` file shows the same prices after any of them.

### The log lines, per ruleset

`content: clash-c: 1 files of its root replace the game's and ...` and `content: clash-d: 1 files of its root replace the game's and ...`, once at the start of the game (after a restart) and again after an in-place switch:

| Ruleset | `clash-c` says | `clash-d` says |
|---|---|---|
| Original | `... and are ignored (the ruleset in use is the game's own): the game's own are used; they apply only under a ruleset of this pack` | the same |
| Blue | `... (the ruleset in use is clash-b:blue) ...` | `... (the ruleset in use is clash-b:blue) ...` |
| Plain | `... (the ruleset in use is clash-b:plain) ...` | `... (the ruleset in use is clash-b:plain) ...` |
| Clash example C | `content: clash-c: 1 files of its root replace the game's and are mounted, because its own ruleset is in use` | `... and are ignored (the ruleset in use is clash-c): ...` |
| Clash example D | `... and are ignored (the ruleset in use is clash-d): ...` | `content: clash-d: 1 files of its root replace the game's and are mounted, because its own ruleset is in use` |

For the patches, once per package at the start of the game and after every switch: `patch: clash-c: 1 operations of its root patches change what the game has and are skipped: they apply only under a ruleset of this pack (the ruleset in use is the game's own)` (`... and are applied, because its own ruleset is in use (the ruleset in use is clash-c)` under C), the same for `clash-d` (the scout), and for the file: `patch: (clash-c) data/commands/rbarracks.xml applied; 1 operations skipped: they change what the game has and apply only when a ruleset of this pack is chosen (set /commands/cmd[@name='trainRArcher'])` (it is `applied` because the add of `trainClashC` applies; under C the line has no `skipped` part). At the scan: `content: clash-c: 1 root patch files compared with the game's own: 1 operations change what the game has (N ms)`. The two cards say *1 operations of its root patches change what the game has and are skipped: they apply only when a ruleset of this pack is chosen (data/commands/rbarracks.xml: set /commands/cmd[@name='trainRArcher'])* (or *...they apply because a ruleset of this pack is in use*). `Dump=data/commands/RBarracks.xml` shows the file as the game read it: the `costgold` of `trainRArcher` and `trainRScout` follow the table.

After an in-place switch there is also `content: ruleset changed without a restart: ... -> ...` and, when the next game starts, `game: hot reload (after Init): trainRHastatus costgold = 77, costfood = 0; ...` (**100** under Original, Plain; **77** under C; **88** under D). With `LogFiles=1` and `LogLevel=debug` the log has `vfs: opens data/commands/rbarracks.xml` and, for the files that are always mounted, `vfs: opens gameres/icons/clashcunit.bmp` and `vfs: opens gameres/cmdbar/actions/clashcunit.bmp` (no `-> does not exist`) under every ruleset; the green menu buttons are the way to see the skin (`vfs: opens menures/button.bmp` or `gameres/menu/button.bmp`, whichever name the menu uses).

### The cards and the validator

In **Options > Content** the cards of `clash-c` and `clash-d` have the gold line (a notice) *1 files of its root replace the game's and are ignored: they apply only when a ruleset of this pack is chosen (data/commands/rbarracks.xml)* under every ruleset but their own, and *1 files of its root replace the game's: they apply because a ruleset of this pack is in use* under their own. `clash-d` is a rules package without civilizations that has an entry of its own in the Ruleset row (**Clash example D**) because its root has a file that replaces the game's. The new name and the menu skin are not counted.

```text
node tools\validate-mod.js sdk\examples\ruleset-clash-example\clash-c --game "<game folder>"
WARNING  data/commands/RBarracks.xml  global in-game file in the root of a rules package (a civilization pack is one): it is ignored unless a ruleset of this package is chosen; put it in a ruleset layer (rulesets/<id>/)
```

followed by `WARNING  patches/data/commands/RBarracks.xml  1 operation(s) of this patch change what the game has (set /commands/cmd[@name='trainRArcher']): in the root of a rules package they are skipped unless a ruleset of this package is chosen; move the file to a rulesets/<id>/ layer (adding is fine anywhere)` (the patch warning appears with `--game` even without the made file; the first warning only once the file is made; one each for `clash-c` and `clash-d`, with the scout for `clash-d`: not for `ClashCUnit.bmp`, not for the menu button). `tests/test_clashexample.js` runs the generator and the validator on a made-up game folder.

## What is not here

A **sprite** of a neutral building (a loose `.ent.xml` and `.rle`) is not in this example. `mapobjects\` and `buildings\` entities are gameplay files (they define the passability), so a ruleset that holds one is a **rules** ruleset: it would rank with `clash-a` and change the prediction above. The bitmaps of the strategic map are cosmetic.
