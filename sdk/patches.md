# Patches

Start from the [gameplay template](templates/gameplay), an empty mod with the folders this guide uses.

A patch changes part of a game file without replacing the whole thing. Two mods can patch the same file and both changes add up. If each shipped its own copy of the file, the second would overwrite the first.

XML files (commands, items, bonuses, unit definitions), INI files (constants, interface) and `.vs` scripts (AI, behaviors) all work. The file has to exist in the game. Example to copy: [`examples/patches-example`](examples/patches-example).


## Where it goes

In your mod, inside `patches`, with the same path the file has in the game:

```
mods\my-mod\
  mod.json
  patches\data\commands\RBarracks.xml     patches data\commands\RBarracks.xml
  patches\data\const.ini                  patches data\const.ini
```

- Every time the game opens that file, the patches of the active mods are applied to it. The originals are never touched.
- With several mods, patches apply from the last mod in the [load order](reference.md#1-mod-structure) to the first: the first has the last word. The game works that order out and it's the same on every computer with the same mods; the multiplayer check doesn't depend on it.
- If your mod also ships its own copy of the file (without `patches`), the other mods' patches are applied on top of it.
- To test a patch without creating a mod, put it in `<game>\mod\patches\<path>`. It's applied last, on top of everything.
- A patch changes gameplay if the file does (`data\...`, maps, units). Then all players must run the same mod.
- In the root of a **rules package** (one with `civs\`, or any package whose root has gameplay files) a patch may **add** to the game's files anywhere, but the operations that **change or delete what the game already has** apply only when a ruleset of that package is chosen: see [Rulesets](rulesets.md#patches-in-the-root-add-anywhere-change-only-under-your-own-ruleset). Patches in a `rulesets\<id>\` layer have no such limit. Patches of the interface (`menuini\`) are exempt.
- Patches are read when the game opens each file. If you change one, restart the game.

## See the result

Add this to `imperivm-mod.ini` and open the game:

```ini
[Debug]
Dump=data/commands/RBarracks.xml,data/const.ini
```

Each file is copied to `mod-dump\<path>` exactly as the game reads it, with the patches already applied. To see the original, use the Original profile. Give the exact name of each file, separated by commas; the whole line can't exceed 1,023 characters. A lone `*` dumps every file the game opens: that's thousands, so be careful.

## XML patches

A `<patch>` with operations that apply in order:

```xml
<patch>
  <set select="/commands/cmd[@name='trainRHastatus']" costgold="60" execdelay="3000" unset="costpop"/>
  <add select="/commands"><cmd name="miOrden" method="train" .../></add>
  <remove select="//cmd[@name='otraOrden']"/>
  <replace select="/commands/cmd[@name='trainRArcher']/src"><src obj="RBarracks"/></replace>
</patch>
```

- `<set select="…" attribute="value" …/>`: changes or adds those attributes on the selected elements. `unset="a b"` removes attributes.
- `<add select="…">…</add>`: appends the content to the end of the selected element. With `position="start"` it goes at the start; with `"before"` or `"after"`, next to the selected element instead of inside it.
- `<remove select="…"/>`: removes the selected elements.
- `<replace select="…">…</replace>`: replaces the selected elements with the content.

If what you're looking for may not exist and you don't want an error, put `optional="yes"` on the operation.

### The selector

A path like the ones for folders, with conditions in square brackets. Always start it with `/` or `//`: the validator reports an error if you don't.

| Selector | Selects |
|---|---|
| `/commands` | the root `<commands>` |
| `/commands/cmd` | all `<cmd>` inside `<commands>` |
| `/commands/cmd[@name='trainRHastatus']` | the `<cmd>` with that `name` |
| `//cmd[@name='trainRHastatus']` | that `<cmd>`, wherever it is |
| `/commands/cmd[@key]` | the `<cmd>` elements that have a `key` attribute |
| `/commands/cmd[3]` | the third `<cmd>` |
| `/commands/cmd[@name='x']/src` | the `<src>` elements of that `<cmd>` |
| `//*[@name='x']` | any element with `name="x"` |

Only these conditions exist: `[@a='v']`, `[@a]` and `[n]`. You can chain several (`[@a='v'][2]`). There's no `and`, `contains`, `text()` or `..`. Case doesn't matter, and values go in single or double quotes.

### Rules

- The patch has to be well-formed XML. In values, write `&` as `&amp;` and `<` as `&lt;`. If it's malformed, the whole patch is skipped, and the error shows in the log and on the mod's card.
- An operation that fails (a selector that matches nothing) is skipped with a warning. The others still apply.
- The patch doesn't change the encoding: it uses the game file's (almost always ASCII).
- The result loses the attribute indentation (attributes end up on one line). The content is the same.
- If you remove or rename something other files use, it breaks. The game's AIs call commands by name (`trainRScout`): remove the command and the AI will fail.

## INI patches

An INI with the sections and keys you want to change:

```ini
; Change gameplay constants
[GamePlay]
DamageAmplify = 250
-TeachingLevel2

; In lists of bare names (no =), + adds a line if it isn't there
[GameOptions Objects]
+MiBotonNuevo
-RSText
```

- `Key = value` changes or adds the key in that section.
- `-Key` removes the key (or the line that is exactly that text).
- `+Line` adds a bare line to the section if it isn't there already.
- If the section doesn't exist, it's created at the end of the file.
- Removing a key that doesn't exist gives an error in the log.
- Trailing comments on the original file's lines (`Key = 20 ;comment`) are lost on the line you change.

**Don't put comments after a line in the patch.** The engine doesn't strip them: `-Key ; text` looks for a key with that name and fails, `[Section] ; text` isn't recognized as a section, and `Key = 250 ; text` stores the comment inside the value. Comments go on their own lines starting with `;`. The validator doesn't warn about this.

## Script patches

A patch for a `.vs` (for example `patches\data\ai\GetTacticScript.vs`) is text with directives that start with `@` in the first column. The code you write under each one is added to the game's script:

```
@before nRace = set.GetCentralBuilding.race;
// MOD: my civilization
if (Mod_TrueRace(set.GetCentralBuilding) == Dacia)
	return TS_GaulTactic2;

@after player = set.player;
Mod_Log("GetTacticScript: choosing a tactic");
```

`Dacia` isn't a game constant: it exists if there's an active civilization with that name (the `id` of its `civs\<id>.json`). See [Civilizations](civilizations.md).

| Directive | Where it puts the code |
|---|---|
| `@top` | Right after the script's first line (the signature). Only good for declaring variables or comments. |
| `@bottom` | At the end of the script. |
| `@before TEXT` | Before the first script line that contains `TEXT`. |
| `@after TEXT` | After that line. |
| `@replace TEXT` | In place of that line. |
| `@end` | Closes the previous block. Optional: anything after it up to the next directive is ignored. |

- `TEXT` is a piece of a script line, exactly as written (not a regular expression). It anchors to the first line that contains it, so pick text that appears only once; otherwise your code ends up somewhere you didn't expect. If no line contains it, the error shows in the log and the directive is skipped.
- In scripts, variables are all declared at the top and code comes after. An `if` or a `return` placed with `@top` lands before the declarations and doesn't compile (`Unexpected starting token`). Use `@before` with the script's first line of code.
- Patches from several mods add up, so two mods can change the same script (for example, two civilizations that pick their tactic in `GetTacticScript.vs`).
- The resulting script has to compile. If it doesn't, the error shows in `Logs\imperivm\imperivm-mod.log` as `script: compilation error`.
- The validator doesn't check the code in these patches, nor the `Mod_*` functions, nor the signature.

## If something goes wrong

Look in `Logs\imperivm\imperivm-mod.log` for the lines tagged `patch`: they say which file was patched and which operation failed. Errors also show on the mod's card. Compile errors from a script patch show as `script: compilation error`.
