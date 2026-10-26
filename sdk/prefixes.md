# Civilization prefix registry

Check this list before choosing `letter` for a [civilization](civilizations.md). Add a row when publishing a new civilization; keep existing prefixes stable so classes, commands and saved content keep working. Compare prefixes **case-insensitively**: `Dac` and `DaC` both become `dac` in the lowercase API.

The cloning tool rejects the eight game prefixes: using one for new classes would overwrite the game's classes, even if it is your base's own prefix. For a registered community conflict it warns but keeps the requested or derived prefix; it never silently picks an alternative. Choose another explicitly with `--letter` before generating your final mod. The registry is advisory, not a list of every mod in existence; the runtime rejects conflicting own prefixes among active packs and reserves the eight game prefixes. A hand-written variant may inherit its own base's prefix (omitting `letter`, or explicitly using that base's letter) without cloning its classes. This is different from asking the cloning tool to generate new classes with that reserved prefix.

The registry covers prefixes only and reserves **no numeric slots**. Choose a distinct civilization `id`; omit `index` for automatic engine assignment within the active profile (10–127; 118 slots in theory). Explicit indices are an advanced legacy option: keep existing published numbers unchanged. Automatic indices and multi-character prefixes need the matching new DLL build. See [numeric indices and saved games](civilizations.md#numeric-indices-and-saved-games) before changing a profile's civilization set.

Class names start with the prefix followed by an uppercase letter: `GaTownhall` and `GalTownhall` are distinct from `GTownhall`. See the [naming convention](civilizations.md#prefix-convention).

## Game (reserved)

| Prefix | Civilization | Pack |
|---|---|---|
| `G` | Gaul | Original game |
| `R` | Republican Rome | Original game |
| `C` | Carthage | Original game |
| `I` | Iberia | Original game |
| `M` | Imperial Rome | Original game |
| `B` | Britain | Original game |
| `E` | Egypt | Original game |
| `T` | Germany | Original game |

## Community mods

Snapshot of `community-mods/ported`: 28 civilization declarations, with all existing prefixes unchanged.

| Prefix | Civilization | Pack |
|---|---|---|
| `K` | Greece (`greece`) | CGWAR - Greek civilization Mod |
| `H` | Greece (`greek`) | Greek Civilization Mod |
| `X` | Gaul (`gaul42`) | Greek Civilization Mod |
| `Y` | Britain (`britain42`) | Greek Civilization Mod |
| `Z` | Iberia (`iberia42`) | Greek Civilization Mod |
| `1` | Egypt (`egypt42`) | Greek Civilization Mod |
| `3` | Germany (`germany42`) | Greek Civilization Mod |
| `4` | Republican Rome (`romerep42`) | Greek Civilization Mod |
| `5` | Imperial Rome (`romeimp42`) | Greek Civilization Mod |
| `6` | Punic Rome (`romepun42`) | Greek Civilization Mod |
| `7` | Numidia (`carthnum42`) | Greek Civilization Mod |
| `8` | Mauritania (`carthmau42`) | Greek Civilization Mod |
| `Q` | Greece (`greek18`) | Greek Civilization Mod 1.8 |
| `J` | Republican Rome (`rome18`) | Greek Civilization Mod 1.8 |
| `L` | Gaul (`gaul18`) | Greek Civilization Mod 1.8 |
| `P` | Carthage (`carthage18`) | Greek Civilization Mod 1.8 |
| `O` | Britain (`britain18`) | Greek Civilization Mod 1.8 |
| `U` | Iberia (`iberia18`) | Greek Civilization Mod 1.8 |
| `V` | Egypt (`egypt18`) | Greek Civilization Mod 1.8 |
| `F` | Germany (`germany18`) | Greek Civilization Mod 1.8 |
| `Mga` | Gaul (`mgaul`) | Mediterranevm (alpha) |
| `Mro` | Republican Rome (`mrome`) | Mediterranevm (alpha) |
| `Mim` | Imperial Rome (`mimperial`) | Mediterranevm (alpha) |
| `Mca` | Carthage (`mcarthage`) | Mediterranevm (alpha) |
| `Mbr` | Britain (`mbritain`) | Mediterranevm (alpha) |
| `Mib` | Iberia (`miberia`) | Mediterranevm (alpha) |
| `Meg` | Egypt (`megypt`) | Mediterranevm (alpha) |
| `Mge` | Germany (`mgermany`) | Mediterranevm (alpha) |
