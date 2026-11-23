# Rank emblems

The league's rank emblems (Recruit to Imperator) are drawn here as SVG and baked into the PNGs of `assets/league/`, which `src/app/winmm.rc` puts inside `winmm.dll`. The mod scales them with WIC to the size each screen shows (`src/ui/leaguerankart.cpp`): from `<rank>-256.png` above 64 pixels and from `<rank>-64.png` at 64 and below. The unranked card and the vexilla, phalerae and trophies are still drawn in code (`src/ui/leagueemblem.cpp`).

- `make.js` builds the SVG of each rank (128 units square). `node tools/rank-emblems/make.js` writes them, with a page that shows them at several sizes, to `build/rank-emblems/`.
- `crown.js` holds the Imperator's rays.
- `bake.js` renders the SVGs to `assets/league/<rank>-256.png` and `<rank>-64.png`.

## Changing an emblem

1. Edit `make.js` and look at `build/rank-emblems/preview.html`.
2. Bake the PNGs. `bake.js` needs [sharp](https://sharp.pixelplumbing.com/), which is not a dependency of the repository: install it in any folder and pass that folder.

   ```sh
   node tools/rank-emblems/bake.js C:\somewhere\node_modules\sharp
   ```

3. Commit `make.js` and the PNGs together, and rebuild `winmm.dll`.

A new rank also needs its two lines in `src/app/winmm.rc` and its entry in `src/ui/leaguerankart.cpp`.
