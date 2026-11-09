# Custom images

A mod can ship its own command buttons and unit portraits instead of reusing the game's. Draw in any program, save a PNG and convert it with a tool. You need [Node.js](https://nodejs.org).

Example to copy: [`examples/ability-example`](examples/ability-example) (the "Respiro" button).

## Convert a PNG

```
node tools/convert-image.js <entrada.png> <salida.bmp> --type button
node tools/convert-image.js <entrada.png> <salida.bmp> --type icon
```

- `boton`: a command's button (`button="..."` of a `<cmd>`). In the game it's 153 x 51: three 51 x 51 frames (normal, mouse-over and disabled). You supply a 51 x 51 PNG and the tool makes the other two frames (a lighter one and a gray one). If you already have all three, a 153 x 51 PNG is used as is. Any other size is scaled without distortion.
- `icon`: a unit's portrait (`icon="..."`) or its training queue portrait (`queueicon="..."`). It's 65 x 76. A PNG of any size works: it's scaled without distortion, and transparent areas become the pure green the game treats as transparent.

With `--background #RRGGBB` you pick the color that fills a button's transparent areas (by default, a dark brown like the game's buttons).

The PNG must be 8 bits per channel and non-interlaced, which is the norm when saving from any program. If it isn't, the tool says so.

## Where the file goes

With the same path the game uses, inside the mod folder. The rule for buttons is `gameres/cmdbar/<whatever button= says>`:

| What | In the XML | File in your mod |
|---|---|---|
| A command's button | `button="Actions/Respiro.bmp"` | `gameres/cmdbar/Actions/Respiro.bmp` |
| An upgrade's button | `button="research/Mejora.bmp"` | `gameres/cmdbar/research/Mejora.bmp` |
| A unit's portrait | `<properties icon="gameres/icons/MyUnit.bmp"/>` | `gameres/icons/MyUnit.bmp` |
| Queue portrait | `queueicon="gameres/icons/MyUnit.bmp"` | `gameres/icons/MyUnit.bmp` |

The game's commands also use the `upgrades`, `skills`, `states` and `specials` folders inside `cmdbar`. They work the same way.

Case doesn't matter in paths. If the game can't find an image, the button shows as a plain blue square.

To **replace** an image the game already has (the hastatus portrait, say), use the same path: `gameres/icons/RHastatus.bmp`. With the DLL of this version it wins over the game's own. Before that it was silently ignored for files the game has, because the game turns `gameres\` into `ui\` before it looks for the file; the folder name `ui\cmdbar\Actions\...` always worked. The same goes for `gameini\` (`data\interface\`), `menuini\` (`data\interface\menu\`) and the other folders the game mounts under another name. Put the images that replace the game's in a [ruleset](rulesets.md#where-art-sounds-and-texts-go).

## If it doesn't show

1. Set `LogFiles=1` in `[Debug]` of `imperivm-mod.ini`. The log notes every file the game opens and flags the ones that don't exist (`VFS: opens … -> does not exist`), so you can see the exact name it asks for.

## What you can't do

- Help images (`help/...`), the info bar images of each civilization and menu backgrounds have no converter.
- Unit and building models and animations (`.rle` and `.ent.xml`) are a different matter: see [Custom graphics](graphics.md).
