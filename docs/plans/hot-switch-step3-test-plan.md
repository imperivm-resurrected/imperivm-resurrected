# Step 3 (#59): the single in-game test plan for 3a-3h

One setup, one DLL (the CI build of the top of the stack, PR `feat/hot-switch-rletail`), one profile. **Nothing in this plan has been run in the game**: every expected result below is **predicted** from the code and from the decompiled engine; the test is what proves or disproves it. Mark each line ok / not ok and send the log.

What step 3 does: a ruleset switch no longer restarts the game when it changes images (3a), texts (3b), sounds, voices and sound entities (3c), entity art (3d), maps (3e), `.pak` files in the ruleset's own folder (3g) or its own `rle.mmp.tail` (3h). Files the game reads once and the structural data (civilizations, specials, options, events, `rules\`) keep the restart, and so does a ruleset's tail when a package's root has one; the dialog says why.

## 1. Setup (once)

1. Copy to `<game>\mods\`: `clash-a`, `clash-b`, `clash-c`, `clash-d` (from `sdk\examples\ruleset-clash-example`) and `ruleset-assets-example` (from `sdk\examples`).
2. Make the two files that are copies of the game's own (not in the repository): from `sdk\examples\ruleset-clash-example`, `node make-clash-files.js "<game>" "<game>\mods"`.
3. Make the random map of the blue ruleset (6 MB of generated pictures, not in the repository), from the repository root:
   `node sdk\tools\create-random-map.js "<game>\mods\clash-b\rulesets\blue" Anillo sdk\examples\random-map-example\drawings\ring.png --players 8`
3b. Make the pak of the `packed` ruleset (3g) from your own game, once: from `sdk\examples\ruleset-clash-example`, `node make-packed-pak.js "<game>" "<game>\mods\clash-b" english` (or `spanish`, `italian`: the language you play in). It writes `clash-b\rulesets\packed\packs\packed.pak`.
4. **Options > Content**: a profile with **only those five packages** (call it *Step 3*). Restart the game (this is the only restart the whole plan needs, apart from the ones it tries to provoke in section 9).
5. `imperivm-mod.ini` (game folder), for sections 2-9: `[Debug]` `LogLevel=debug` is not needed; keep the default. For the stress run (section 10) add `MemoryTrace=1`, and only then (it makes loads slower).
6. Play the game's **Republican Rome** in a skirmish. The **Ruleset** row is the first row of *Game options* in the skirmish room. The log is `Logs\imperivm\imperivm-mod.log`; open it after each section and search for the lines below.

Rulesets you will see in the row: *Original (HD)*, *Clash example A*, *Blue* and *Plain* (`clash-b`), *Clash example C*, *Clash example D*, *Assets* and *Plain* (`ruleset-assets-example`).

## 2. Baseline: Original

Start a match, select the **Roman barracks** and write down what you see (this is what every other ruleset changes):

| Look at | The game's own |
|---|---|
| the hastatus button | the game's picture; hover it: *Equip Hastatus*, price **100**, a gold coin icon in the price |
| train two hastati | the game's queue icons and portrait; legionaries on the map |
| select the barracks again | one of the game's four selection sounds |
| lobby, *Map type* | no *Random: Anillo* |

## 3. Blue (images, text, sound, entity art, maps)

Leave the match, set the **Ruleset** row to **Blue** (the row says no *(needs a restart)*), start a match. **No restart.** In a Blue match:

| Look at | Expected (predicted) | Category |
|---|---|---|
| the hastatus button | flat **blue**, three frames (normal, lighter, gray) | 3a |
| hover it | the gold icon in the price is a **blue** square; the text is **Equip Hastatus (blue ruleset)** (Spanish *Equipar legionario (conjunto azul)*, Italian *Equipaggia astato (set blu)*) | 3a, 3b |
| queue two hastati | queue icons and portrait have the **blue** pattern; the soldiers on the map are **cubes** (6 animations: idle, walk, fight, die, transition, taunt) | 3a, 3d |
| select the barracks | a rising **three-note chime** every time | 3c |
| zoom out to the strategic view | trees are cyan squares; a ruin, if the map has one, is a blue square | 3a (minimap) |
| lobby *Map type* (open it after the switch) | **Random: Anillo**; start it: a ring of land with a sea in the middle | 3e |

Log lines to read (they appear at the start of the match, in this order; `N`, `M` and the times are what the game says):

```text
content: ruleset changed without a restart: <Original> -> clash-b:blue (files of the new one: patches of data/, files in data/, interface images, texts, sounds, sound entities, entity art, maps; ...)
game: hot reload: LoadGameData of a single-player game (vtable ...): the rules mounted are not the ones the game holds, they are loaded now
game: hot reload: texts read again: N -> M entries                      (M is larger than N: the layer's text)
language: K mod texts added to game.loc.xml
game: hot reload: sound entities read again: N -> N entities            (same count: the patch changes what an entity plays)
game: art cache (before the reload): file cache N entries (H in use ...), interface cache B bytes unreferenced
game: hot reload: image caches emptied in T ms: files N entries (H in use) -> M (K in use), interface B -> 0 bytes unreferenced, command-bar frame set to load again, purge file 1; 15 images change, 0 of them still cached
game: art cache (after the reload): file cache M entries (...)
game: hot reload: entity art: 1 folders changed, 1 of the N cached entities taken out: Units/RHastatus/RHastatus.ent.xml      (0 of the N if no match used the hastatus yet)
game: hot reload (after Init): trainRHastatus costgold = 100, costfood = ...
```

`15 images change, 0 of them still cached` is the proof that no old picture can come back. If the line says another number, or the log has `images that this switch replaces are still in use by the game (<names>)`, the game restarted by itself (a window told you): send me that line, it is the case the live check could not know.

## 4. Back: Plain, Original, again Blue

One match each, in this order, no restart on any: **Plain** (`clash-b`) -> **Original** -> **Blue** -> **Plain**.

Expected: Plain and Original show exactly the table of section 2 (the game's own button, icon, text, sound, legionary, no Anillo); Blue shows section 3 again. Log: the same lines, with `... -> clash-b:plain` (the `15 images change` line, `texts read again: M -> N`, `sound entities read again`, `entity art: 1 folders changed, 1 of ... taken out`), and **no** switch line between Plain and Original (they mount the same files).

## 5. Clash example A (red images, the package as ruleset)

Switch to **Clash example A**. Expected: hastatus button **red**, price icon a **red** square, queue icon and portrait the **red** pattern; text, sound and soldier are the game's; no Anillo. Then back to Blue, then to Original: no restart.

## 6. Prices (data, already proven in #67/#68, a quick regression)

**Clash example C**: hastatus **77**, archer **55**, scout **140**. **Clash example D**: **88**, **80**, **99**. Plain/Original/Blue: **100**, **80**, **140**. Log: `hot reload (after Init): trainRHastatus costgold = N`.

## 7. Assets example (voice, sound, text, button, portrait)

Switch to **Assets** (`ruleset-assets-example`): select the hastatus and give it orders: a **three-note chord** instead of its voice; the *training completed* alert (train one, wait) is a **falling bell**; the button is magenta, the portrait orange, the text *Equip Hastatus (assets layer)*. Back to its **Plain**: all the game's own. No restart on either. This is the same-name voice and file path (no table change); the entity table is exercised by the barracks chime in Blue.

## 8. From a running game and from saves

1. In a **Blue** match open the in-game menu, **Load** a save made under **Plain**: no restart; the new match shows the game's images, text, sound and legionaries. Log: `content: ruleset ... chosen while a game is running: it is mounted when the next game starts`, then the section 3 lines.
2. Save one game under each of **Blue**, **Plain**, **Clash example A**. Load each from each: the saved ruleset's images and soldiers show, no *different content* warning (`W vfs: saved game ...` must not appear), no restart.
3. In the **Load** screen, a save of a ruleset that needs a restart (section 9) says why in its details and in the dialog.

## 9. What must still restart, and say why (EN, and switch the game language to es/it once)

Make a throw-away copy of `clash-b` named `clash-t` (change `id` in `mod.json`, add it to the profile) and put one file at a time in its `blue` layer; choosing Blue of `clash-t` must show the dialog *The game will restart with the ruleset you chose.* plus the sentence, and the log `content: a change of ruleset keeps the restart: ...`:

| File to add | The sentence says |
|---|---|
| `gameres/selections/x.rle` (any bytes with the name) | *It has files the game reads only once, when it starts: gameres/selections/x.rle.* |
| `local/<language>/splash.ini` | the same, with that file |
| `civs/x.json` | *It has files the game cannot swap in a running session (civilizations, specials, options, events, rle.mmp.tail...): civs/x.json.* |
| `patches/x.pak` | the same sentence with that file (a pak in `packs/` is judged by its members, section 12) |

Also read the validator: `node sdk\tools\validate-mod.js "<game>\mods\clash-t"` prints one `switching to or from this ruleset restarts the game: ...` warning per file.

## 10. Stress: Blue -> Plain -> Original -> Blue, five rounds

1. Add `MemoryTrace=1` to `[Debug]` in `imperivm-mod.ini`, restart the game, play one match as **Original**.
2. Five times: set Blue, start a skirmish, play ~30 seconds, leave; Plain, start, leave; Original, start, leave. (15 matches, 14 switches.)
3. Quit the game and run `node sdk\tools\memory-report.js "<game>\Logs\imperivm\imperivm-mod.log"` (add the older `imperivm-mod.N.log` first if the log rotated). Send the table.

Read it as: `failed heaps` is **0** in every row; `file cache` entries and `interface bytes` do **not** grow from round to round (they are emptied at every switch with images); the `entities` column does not grow beyond the entities the matches used (the cube soldier replaces the legionary, it does not add one per switch); the commit grows by the weight of one extra match, not by one per switch (about 3 MB per switch was the 1b figure).

## 11. Two PCs, same build (steps 4 and the open items of #67)

1. Host **Blue**, guest **Original**: the keys differ, the guest follows to Blue in place and sees section 3's images (no restart); the host sets **Plain**: both original.
2. Host **Clash example C**, guest **Original** (item 11 of #67): the guest follows to C and sees the 77 price.
3. A pure-ADD civilization package: host on its entry, guest on Original: the keys are equal, no switch asked (item 12 of #67).

## 12. Paks in a ruleset (3g): the **Packed** ruleset

Set the **Ruleset** row to **Packed** (`clash-b`; the row says no *(needs a restart)*), start a match. **No restart.** Expected (predicted):

| Look at | Under Packed | Under Blue, Plain, Original, Clash A |
|---|---|---|
| queue two hastati | the queue icons and the portrait have the **green** pattern with a white stripe (from `UI\ICONS\RHASTATUS.BMP` in the pak) | blue / the game's / red |
| select the barracks | a **falling two-note chime** (the pak's `SOUNDS\SELECTION\BARAKS\BARRAKS_1..4.WAV` beat the game's `Sounds.pak`) | Blue: its rising chime; others: the game's |
| hover the hastatus button | *Equip Hastatus (packed)* (or the Spanish/Italian text you made it with) | Blue: *(blue ruleset)*; others: *Equip Hastatus* |
| lobby *Map type* (open it after the switch) | **Random: Packed Continent** (a data file in the pak, using the game's own Continent pictures); start it: a continent map | not there |

Log lines: the first time, `content: ruleset clash-b/rulesets/packed: 1 pak file systems mounted in the engine's list, below the loose layers (N ms)`; later `... put back in the engine's list ...`; and when you leave it `... pak file systems taken out of the engine's list`. The usual `texts read again`, `sound entities read again` (the pak has a `DATA\RANDOMMAP` file, not an entity: the line appears only if a pak had one), `image caches emptied ... N images change, 0 of them still cached`.

Order of the switches to try, one match each: Original -> **Packed** -> Plain -> **Packed** (the second time the same file systems are put back, no new object) -> **Blue** (loose layer and pak layer swap) -> **Packed** -> Original. In each, the table above holds. **Shadowing**: under Packed the game's own barracks sounds are not heard (the pak's members beat the game's `Sounds.pak`). "A loose file beats a pak member" cannot be seen with this profile (only one ruleset is mounted); it is covered by the order the DLL inserts the pak file systems in (below the loose layers) and is not proven in game.

**Held image check (3a) with a pak:** switch to Packed, play a match (the green portrait is cached), switch to Plain: the log must say `image caches emptied ... 0 of them still cached` and the portrait of the next match is the game's: the pak was taken out while its picture was cached, and the cache was emptied for its name.

**Still restarts, with the member named** (a throw-away copy of the `packed` ruleset named `clash-t`, repacked with `node sdk\tools\make-pak.js <folder> <out.pak>` with one extra member): a member `CIVS\X.JSON`, `UI\CURSORS\A.CUR` or `FONTS\A.APF` -> the dialog says *It has files the game cannot swap...* / *reads only once...* and names that member; `node sdk\tools\validate-mod.js "<game>\mods\clash-t"` prints `member <NAME>: switching to or from this ruleset restarts the game: ...`.

## 13. The graphics tail of a ruleset (3h): the **Tailed** ruleset

Make it once from your game (a few seconds, it hashes the 391 MB `rle.mmp`): from `sdk\examples\ruleset-clash-example`, `node make-tailed-ruleset.js "<game>" "<game>\mods\clash-b"`. It writes `rulesets\tailed\rle.mmp.tail`, `units\RHastatus\*.rle.mmp` and the entity. Restart the game once so it scans the new files (the profile of section 1 does not change).

Set **Tailed**, start a match: **no restart**. Train two hastati: they are the **cube soldier**, with the six animations, **read from the tail** (the Blue ruleset's cube is read from loose `.rle` files: they look the same, the log tells them apart). Log, the first time: `graphics: clash-b was made for this game's rle.mmp (checked in N s)`, `graphics: the tail of clash-b (0 MB) answers the game's sprites past the end of rle.mmp (N ms)`, `game: hot reload: entity art: 1 folders changed, 1 of the N cached entities taken out: Units/RHastatus/RHastatus.ent.xml`.

Order, one match each, no restart on any: Original -> **Tailed** -> Plain -> **Tailed** (no new check: the view is kept) -> **Blue** -> **Tailed** -> **Packed** -> Original. In each: the legionary under Original, Plain, Packed and Clash A; a cube under Blue and Tailed. Leaving Tailed the log says `graphics: no tail answers past the end of rle.mmp any more`. A match played under Tailed must not crash on the way out or when the next one loads (a sprite made from the tail is freed with its entity: the tail memory is never freed).

**Held/stale check:** Tailed, play, then Plain: the legionary comes back (the entity of the folder was taken out, not kept with a pointer into the tail's view).

**Conflict (needs a package with a root tail, e.g. the 18 MB `mediterranevm-alpha` of the ported packs; skip if you do not have it):** add it to the profile and restart. Then choosing Tailed asks for the restart with *It has a graphics tail (rle.mmp.tail), and a package of the profile already has one in its root: only one tail can be mounted...*, and the log has `a change of ruleset keeps the restart: its graphics tail cannot be served beside the one of a package's root`.

**Wrong game version (cannot be seen without another `rle.mmp`):** a tail whose header says another size or sha256 is refused at the switch (`extends an rle.mmp of ...`, the card says *Made for another version of the game's graphics*), and the switch restarts instead.

## 14. What is proven, what is predicted

| | State |
|---|---|
| Code compiles in CI; unit tests (`test_hotswap` 358, `test_artwalk`, `test_entitywalk`, `test_assetclass.js`, `test_clashexample.js`, ...) pass | proven by CI |
| everything in sections 2-11 | **predicted** (nothing has run in the game) |
| #67 items 11-12 and the two-PC items above | never run |
| the walkers of the game's caches (`artwalk`, `entitywalk`) | tested on a synthetic tree made with the VS2003 layout read from the decompile, not on the game's own memory |

Known limits, to report if you meet them: the first switch of a session can restart by itself (with a window) if some long-lived holder uses a changed image that nothing has seen yet; a menu window that was open before a switch keeps its old texts and pictures until it is built again; `.pak` files and `rle.mmp.tail` in a ruleset restart; the first flush after the game starts reads a few hundred images again.
