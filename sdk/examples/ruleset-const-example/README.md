# Example: rulesets that only change CONST.INI

One package with no files in its root and two rulesets in `rulesets\`. The two differ in **one** file, `patches\data\const.ini`, so the host can switch between them without restarting the game (see [Switching without a restart](../../rulesets.md#switching-without-a-restart)). Any other file in a ruleset, even a second one, makes the switch restart the game as usual.

What is in it:

- `slow-economy`: `ProductionGoldBase = 6`, `ProductionFoodBase = 6`, `DamageAmplify = 100`.
- `fast-economy`: `ProductionGoldBase = 96`, `ProductionFoodBase = 96`, `DamageAmplify = 300`.

What to look at:

- **The gold counter in the top bar.** Every 2 seconds (`ProductionInterval = 2000`) each town adds `population * ProductionGoldBase / 100` gold, and each village the same in food with `ProductionFoodBase`. With the same population, `fast-economy` gives 16 times what `slow-economy` gives: start a skirmish as any civilization, spend nothing and watch the counter for 20 seconds.
- **`DamageAmplify`**, which the game copies into a global the first time a settlement is created and never reads again. It is the value that shows a switch really replaced everything the game had copied: in `gbr.exe`, the dword at `0x009bf7a0` is `DamageAmplify` and the one at `0x009bf7a4` is `ProductionGoldBase`, as the game holds them (100 and 6, or 300 and 96, once a settlement exists).

To try it, copy this folder to `<game>\mods\`, enable it in the content manager (with no other package that patches `data\const.ini`), and add this to `imperivm-mod.ini`:

```ini
[Mods]
ConstHotReload=1
```

Restart the game, pick **Slow economy** in the **Rules** row of the skirmish room (if the package was only just enabled, the game restarts once for that), play, go back to the menu and pick **Fast economy** in the same row: the game does not restart. `Logs\imperivm\imperivm-mod.log` says `ruleset changed without a restart` when you pick it and `const reload` lines when the next match starts (with the old and new `DamageAmplify` and how many copies were set back).

It changes gameplay: in multiplayer everyone must have the package. A player whose ruleset is not the room's still restarts into it.
