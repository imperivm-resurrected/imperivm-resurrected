@game-file-sha256 f9e76d98893e61b9a4bdfef000482251182259ca704c594826e7b96915e79a5a
@replace if(u.IsAlive())
	// BestTargetForPos only takes targets inside the hero's own reach (17), while melee units hit from up to 40:
	// a hero holding position stood still under their blows. Those close enough to hit him are answered too.
	if(!u.IsValid()) u = .BestTargetInRange(.pos, 100);
	if(u.IsAlive())
