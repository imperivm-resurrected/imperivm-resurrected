# Example: rulesets

One package with a civilization in its root and two rulesets in `rulesets\`. The host chooses one in the **Create room** dialog (**Rules**, next to **Content**) or in the skirmish room; only that one is mounted. Guide: [Rulesets](../../rulesets.md).

What is in it:

- The root brings `Gaul (example)`, a variant of Gaul (no classes of its own). It is selectable whatever the rules are.
- `cheap-army`: the Roman hastatus costs 60 gold in the barracks and `DamageAmplify` is 250 in `CONST.INI`.
- `costly-army`: the hastatus costs 150 gold and `DamageAmplify` is 150.

To see the difference, create a room with each ruleset and look at the price of the hastatus in the Roman barracks. Choosing another ruleset restarts the game, and players with another one restart into the room's through the usual content sync.

It changes gameplay: in multiplayer everyone must have the package. To test it, copy this folder to `<game>\mods\`, enable it in the content manager and pick the ruleset when creating the room. `Logs\imperivm\imperivm-mod.log` lists the ruleset that was mounted (lines tagged `content`) and what the patches applied (`patch`).
