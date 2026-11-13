# Imperivm II campaigns

Converts the adventures of Imperivm II (La Conquista de Hispania) to Great Battles of Rome HD, from local installations of
both games:

```text
node tools/imperivm2/convert.js --i2 "<Imperivm II>" --gbr "<HD>" --out "<new folder>" [--adventure "Numantia.BFHP"]
```

It writes `imperivm2-data` (the classes, scripts, commands, AI and movies) and one mod per adventure (`Hannibal's Campaigns`,
`The Punic Wars`, `Numantia`; the `Tutorial` only with `--adventure "Tutorial.BFHP"`, since it teaches Imperivm II's
interface), with `report.txt`.

The converter is a thin layer over Imperivm I's (`tools/imperivm1/lib`): `i1classes.js` gives every unit, hero and building
of Imperivm II its own class (`I2<Class>`) with its properties, scripts and commands, writes its combat values to
`data/mod/imperivm2/stats.ini` and its AI as the `I2` profiles; the mod's DLL (`src/game/i1engine.cpp`) fights, levels and
heals those classes as Imperivm II did. What is Imperivm II's own lives in `convert.js`: its adventures' files (races, notes,
movies) and the scripts they run.

Stonehenge and the Shrine keep Great Battles of Rome's classes: Imperivm II's rituals use natives the HD does not have.
