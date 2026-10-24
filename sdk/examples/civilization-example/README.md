# Example: civilization

Dacia: a new civilization (legacy explicit index 10, letter `D`) cloned from Gaul with [`tools/clone-civilization.js`](../../tools/clone-civilization.js). For a new pack, start without a numeric index:

```
node tools/extract-data.js "<game folder>" "<output folder>"
node tools/clone-civilization.js "<output folder>\DATA" "<new mod folder>" dacia
```

Run those commands from the SDK root, with a fresh output folder based on the [civilization template](../../templates/civilization). `extract-data.js` extracts `data.pak` to `<output folder>\DATA`, the folder the tool asks for. The command derives `Dac`, defaults to Gaul and omits `index` for automatic assignment. Choose `--letter Dac2` to override the prefix. The checked-in example deliberately keeps its published `D` and explicit index 10 unchanged. To reproduce those legacy identifiers, use `Dacia D Gaul 10` instead of `dacia` (or `dacia --letter D --index 10`); explicit indices are an advanced optional legacy setting, not a slot new authors must pick. See [Cloning a civilization](../../civilizations.md#cloning-a-civilization). The generated civilization is independent: it does not set `variant` or `replacesBase`, so Gaul stays selectable.

| File | What it is |
|---|---|
| `civs\dacia.json` | The civilization's declaration. It has `"template": "Dacian"`, added by hand. |
| `data\classes\D*.sc.xml` | A copy of each Gaul class (`DSwordsman`, `DBarracks`, `DVillage`…) with a new id and `race="Dacia"`. Full copies, not children: the game doesn't inherit the entity (the model) or the sounds from the parent. |
| `data\commands\Dacia.xml` | The Gaul orders that name its classes (`trainGSwordsman Dacia`, `trainpeasantd`, `hireheroD`…), pointing to the new classes. |
| `patches\data\commands\*.xml` | Add the new classes to the shared orders (repair, barracks upgrades, smithy…). |
| `patches\randommapsettlements\maps\1\map.obj.xml` | The "Dacian Town" and "Dacian Village" templates (a widened city). Made by `create-template.js`; see [City templates](../../city-templates.md). |

Dacia shows up in the multiplayer lobby dropdowns. It changes gameplay: every player has the mod. In `Logs\imperivm\imperivm-mod.log` (lines tagged `civilizations`) you can see it get registered and, with `LogChannels=civilizations:debug` under `[Debug]` in `imperivm-mod.ini`, which player is playing it.

Automatic indices and multi-character prefixes require the matching new DLL build. Keep the save's `.mods.json` index table and the original profile/content: automatic numbers can change when civilizations are added or removed, and mapping mismatches prevent loading. See [Civilizations](../../civilizations.md#numeric-indices-and-saved-games).
