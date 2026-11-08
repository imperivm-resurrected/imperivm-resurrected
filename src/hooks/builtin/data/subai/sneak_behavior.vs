@game-file-sha256 8fe330c486b6dcc43edfa055563cca78a85dcf107cc2af9ea495e7bd905538c0
@replace if (.TimeWithoutAttack >= 20000)
	// Units of players with I1NoSneak set (the converted Imperivm I campaign) never hid on their own in their game.
	if (.TimeWithoutAttack >= 20000 && EnvReadInt(.player, "I1NoSneak") != 1)
