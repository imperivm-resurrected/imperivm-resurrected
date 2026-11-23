# Scripts

Start from the [gameplay template](templates/gameplay), an empty mod with the folders this guide uses.

Scripts (`.vs`) are the language the game uses to program AI, abilities, victory conditions, and the behavior of buildings and units. A mod can ship its own and use Imperivm Resurrected's new functions (`Mod_...`).

Example to copy: [`examples/scripts-example`](examples/scripts-example). Heads up: that example changes the balance (hastati deal double damage and heal when they kill), so don't enable it in a serious game.

## What a script looks like

A `.vs` text file. The first line is a comment with the signature: what it returns and the parameters it receives. Every place a script is used expects a specific signature. There are three exceptions, which the game always loads with their signature, with the line or without it: a class's behaviors (`<behavior script="...">`), as `void, Obj This`; victory modes (`data/GameScripts`), as `void, int player, str param`; and item scripts (see [Items](items.md)), with the signature of their attribute.

```
// void, Obj This

Sleep(rand(500) + 100);
Mod_Log("A Roman hastatus entered the map");
while(true) Sleep(50000);
```

To see how the game writes its own, dump them with `Dump=` (see [Patches](patches.md); you have to give each file by name, no wildcards) or look in the `DATA` folder that `extract-data.js` extracts: `data/subai/*.vs` (behaviors and abilities), `data/ai/*.vs` (AI) and `data/GameScripts/*.vs` (victory). All the language's functions, with their signatures, are in [funciones-del-juego.txt](game-functions.txt).

## Where a script hooks in

| Place | How | Signature |
|---|---|---|
| A class behavior | `<behavior script="data/mod/mi.vs"/>` in the class's `.sc.xml` (with a [patch](patches.md) if it's a game class). Runs for each object of the class while it exists. | `// void, Obj This` |
| Command or ability | `<method sig="miOrden" vs="data/mod/mi.vs"/>` in the class, and a `<cmd name="miOrden">` in `data\commands` | depends on the command: look at the game's |
| When a unit dies | `<method sig="ondie" vs="data/mod/mi.vs"/>` in the class (and `sig="onkill"` for when it kills) | see `data/subai/unit_on_kill.vs` |
| Victory condition | a `.vs` in `data/GameScripts` (see [Victory modes](victory-modes.md)) | `// void, int player, str param` |
| `items.xml` items | attributes like `use_script` or `equip_script` (see [Items](items.md)) | depends on the attribute |

`ondie` and `onkill` are methods (`<method sig=...>`), not attributes. If you write `ondie="..."` on the class, nothing happens, and the game doesn't warn you.

A behavior script must not end: finish it with `while(true) Sleep(50000);`.

A class takes only 8 `<behavior>` elements.

## Events

Put a script in `events\<Event>.vs` inside your mod and the game runs it when the event happens, on all PCs at once. If several mods have the same event, all of them run. To test without creating a mod, put it in `<game>\mod\events\<Event>.vs`.

| Event | When | Signature |
|---|---|---|
| `OnGameStart` | When a skirmish, scenario or multiplayer game starts, right after the victory scripts start. Can use `Sleep` and is saved with the game. | `// void` |
| `OnObjectDie` | When any object on the map dies (units, heroes, buildings, decorations…), right before the game processes its death: the object still exists. Same moment as its class's `ondie`. | `// void, Obj victim` |
| `OnObjectKill` | When a hit kills an object. `killer` dealt the hit, `victim` died. Same moment as the attacker's class `onkill`. | `// void, Obj killer, Obj victim` |
| `OnSettlementCaptured` | When a settlement changes owner during the game (capture, or a script that gives it to someone else). Doesn't fire while the map is being set up. `building` is its central building. Players 1 to 16: unowned settlements start under players 15 and 16; 0 only appears if there's no owner. | `// void, Obj building, int oldPlayer, int newPlayer` |
| `OnAttackResolved` | On every hit that deals damage, right before it's applied: armor, unit specials and bonuses already count. `damage` is what the victim is about to take; `Mod_SetAttackDamage(n)` changes it (0 cancels it). Doesn't fire if the hit deals no damage (deflections, parry). | `// void, Obj attacker, Obj victim, int damage` |
| `OnPlayerDefeated` | When the game declares a player defeated (1 to 8), right after marking them. Once per player. | `// void, int player` |

Except for `OnGameStart`, events run immediately and can't wait: no `Sleep`. `OnAttackResolved` and `OnObjectDie` fire many times a second in a battle. Filter out what you don't care about early (`if (!victim.IsValid) return;`, `victim.IsMilitary`, `victim.player`, `victim.IsHeirOf("RHastatus")`…) and don't do expensive work.

In `OnSettlementCaptured`, `building` is an `Obj`. To get to its settlement, go through `AsBuilding`: `building.AsBuilding.settlement`.

`OnObjectKill` lets you make passive abilities. A hastatus that heals when it kills:

```
// void, Obj killer, Obj victim
if (!killer.IsValid) return;
if (!killer.IsHeirOf("RHastatus")) return;
if (!victim.IsMilitary) return;
killer.Heal(25);
```

With several mods on `OnAttackResolved`, `damage` is always the original damage: it's computed once. Two mods that each do `Mod_SetAttackDamage(damage * 2)` don't stack. To build on what another mod already changed, start from `Mod_AttackDamage()`.

### Reacting to an object appearing

There's no event for this (objects are created in dozens of places, including when loading a map or a game). Use a behavior: a `<behavior>` added with a patch to a base class (`Military` for all soldiers, `Unit` for all units) is inherited by every class under it, and runs once for each object when it appears. See `patches\data\classes\Military.sc.xml` and `data\mod\soldier.vs` in the example.

## `Mod_` functions

The current API version is 7. `Mod_ApiVersion()` returns it, and a mod can require a minimum.

| Function | What it does |
|---|---|
| `Mod_Log(str texto)` | Writes `script: texto` to `Logs\imperivm\imperivm-mod.log` (level `I`). Up to 2,000 lines per session. |
| `Mod_LogInt(str texto, int valor)` | Writes `script: texto: valor`. |
| `Mod_IsActive(str id)` : `bool` | Is the mod with that `id` (the one in its `mod.json`) active in the profile? |
| `Mod_ApiVersion()` : `int` | The API version. |
| `Mod_SetInt(str nombre, int valor)` | Stores a variable (API 2). |
| `Mod_GetInt(str nombre)` : `int` | Reads a variable; 0 if it doesn't exist (API 2). |
| `Mod_AddInt(str nombre, int suma)` : `int` | Adds to a variable and returns the new value (API 2). |
| `Mod_HasKey(str nombre)` : `bool` | Does the variable exist? (API 2). |
| `Mod_AttackDamage()` : `int` | Only in `OnAttackResolved`: the damage of the hit being resolved. Outside it returns 0 (API 3). |
| `Mod_SetAttackDamage(int damage)` | Only in `OnAttackResolved`: changes the damage before it's applied. Outside it does nothing (API 3). |
| `Mod_HasSpecial(Unit unidad, int bit)` : `bool` | Does the unit have that unit special (bit 0 to 63)? (API 4). See [Unit specials](specials.md). |
| `Mod_SetSpecial(Unit unidad, int bit, bool puesta)` | Sets or clears a unit special (API 4). |
| `Mod_TrueRace(Obj objeto)` : `int` | An object's real civilization (10 or higher if it's from a mod), even though for the AI its race is its base's (API 5). See [Civilizations](civilizations.md). |
| `Mod_GetOption(str id)` : `bool` | Is option `id` (the one in `options\<id>.json`) checked in the lobby? Same on all PCs (API 6). See [Game options](options.md). |
| `Mod_GetOptionValue(str id)` : `int` | The chosen value of option `id`: 0 or 1 for a switch, and the number of the chosen entry (from 0) for a list (API 6). |
| `Mod_PlayerCiv(int player)` : `int` | The civilization the player chose in the lobby (10 or higher if it's from a mod), even if it's a variant and `GetPlayerRace` returns its base. -1 if there's no such player (API 7). See [Civilizations](civilizations.md). |
| `Mod_CanExecCmd(Obj objeto, str orden)` : `bool` | Could the player press that command's button on that object now? It's the game's own check: the object has it, it isn't disabled (`CmdDisable`) and its verifier (`groupverifier` or the player one) accepts it. It doesn't look at the cost (API 7). |

New functions are added at the end and existing ones don't change. If your mod uses one, it needs a version of Imperivm Resurrected that has it. The API version is part of the multiplayer check when there are gameplay mods: everyone must have the same one.

### Mod variables

A name → integer table so your mod can remember things (counters, flags, times):

```
Mod_AddInt("captures", 1);
if (Mod_GetInt("captures") >= 3) ...
```

- They're saved with the game (autosaves too) and come back when you load it. A new game starts without variables.
- They're the same on all PCs because game scripts run the same everywhere. Change them only from game scripts, never based on anything local.
- Names up to 64 characters, up to 1,024 variables, 32-bit integers.
- They belong to the whole game, not to a player. For a per-player one, add the player to the name (`"oro1"`, `"oro2"`).
- They're shared among all active mods: use a prefix of your own (`"mymod_captures"`).

### Kill and death counters for the statistics

The game's own count of kills and losses can miss some deaths. If your scripts keep a better one, name it like this and the player's statistics (Statistics, Civilizations tab) use it for the kills, the deaths and the K/D of that game:

| Variable | What it holds |
|---|---|
| `EEE_kills<n>` | Enemy units player `n` killed. |
| `EEE_deaths<n>` | Units player `n` lost. |

- `n` is the player's number as `Mod_PlayerCiv` takes it: `EEE_kills1`, `EEE_deaths1` for the first player. The names are case-sensitive.
- Set them with `Mod_SetInt` or `Mod_AddInt`. Only mod variables are read, not variables declared in a script.
- Both must exist for a player, or the game's own count is used.
- They're read once, when the local player wins or loses the game. A game that's quit before that keeps the game's count.

## Multiplayer

Scripts are part of the simulation: they run on all PCs, at the same moment, and must do the same thing on all of them.

- Read only things that are the same for everyone: map objects, game time, the game's `rand()`.
- Don't read anything local: the camera, the viewing player, the music, the mouse, the system clock. If the script changes the game based on something local, the game desyncs.
- If something comes from a key or the interface, it has to go as a game command, which travels over the network. See [Mod keys](keys.md).

## Saved games

A saved game contains the code of the scripts that were running. That's why SDK functions never change number and are only added at the end: what's saved keeps loading. Don't reuse the game's function names (`Run`, `Write`…) as if they were yours.

## Errors

- A script that doesn't compile leaves the error (file, line and column) in `Logs\imperivm\imperivm-mod.log`, as `script: compilation error: …` (level `E`), and also in `Logs\comp_out.txt` and `Logs\vxlog.log`. The game carries on, but that script doesn't run.
- Runtime errors are almost never logged. In events they are: the log notes `Event X (…): the script ended with an error`, up to 5 times per script. For everything else, use `Mod_Log` and check the log.
