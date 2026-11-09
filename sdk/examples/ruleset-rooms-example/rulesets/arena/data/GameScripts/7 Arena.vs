//void, int player, str param

// Victory mode "Arena": whoever controls 2 settlements wins. Elimination still applies: whoever is left with no town halls and no troops loses.
// The game runs one thread of this script per player.
Query q;
int i, nMine, nTheirs;

q = ClassPlayerObjs("BaseTownhall", player);

EnvWriteInt("modo", 1);
EnvWriteInt(player, "elimination", 0);
SetPlayerStatus(player, 0, Translatef("Arena (%s1)", 2), false);

while (1) {
	Sleep(1000);
	nMine = q.count;
	nTheirs = 0;
	for (i=1; i<=8; i+=1) {
		if (i != player && !DiplAreAllied(i, player)) {
			if (ClassPlayerObjs("BaseTownhall", i).count > nTheirs) nTheirs = ClassPlayerObjs("BaseTownhall", i).count;
		}
	}
	if (nMine >= 2) {
		EndGame(player, true);
		return;
	}
	if (nMine == 0 && MilUnits(player) == 0) {
		EndGame(player, false);
		return;
	}
}
