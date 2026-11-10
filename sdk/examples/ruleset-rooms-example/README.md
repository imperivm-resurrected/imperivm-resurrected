# Example: rulesets that switch in a room

One package with no files in its root and two rulesets in `rulesets\`. They differ in everything that can be switched in place without restarting the game (see [Switching without a restart](../../rulesets.md#switching-without-a-restart)):

| | `classic` | `arena` |
|---|---|---|
| A patch of a command (`patches\data\commands\RBarracks.xml`) | the Roman hastatus costs **60** gold | it costs **150** gold |
| A new file in `data\` (`data\GameScripts\7 Arena.vs`) | none | the victory mode **7 Arena** |
| A mode file (`modes\7 Arena.json`) | none | 7 Arena starts with 5000 gold |
| `hidden.txt` | none | hides `data\GameScripts\2 SCORE LIMIT.VS` |

What to look at, with a profile with only this package:

- **The price**: in a match as the Romans, select the barracks and read the hastatus price (60 or 150).
- **The mode list**: in the room's victory-mode dropdown, `7 Arena` is there and `2 SCORE LIMIT` is not with `arena`; with `classic` it is the other way round. (The dropdown fills when the room dialog is shown: if you switch with the room open and the list does not change, leave the room screen and come back.)
- **No restart**: switch in the **Ruleset** row of the skirmish room, in *Create room*, or (as the host) in the **Ruleset** row of the open room; the log says `ruleset changed without a restart` and, when the match starts, `hot reload: ... trainRHastatus costgold = N`.

A guest that has this package in its profile switches to the host's ruleset by itself, without restarting. It changes gameplay: in multiplayer everyone must have the package.
