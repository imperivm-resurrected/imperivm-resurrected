// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "mods/layerdiag.h"
#include "mods/volumes.h"

namespace {

void AVolumeIsItsRealFolder()
{
	CHECK(VolumeRealPath("gameres/icons/a.bmp") == "ui/icons/a.bmp");
	CHECK(VolumeRealPath("gameini/cmdbar/empty_rome.ini") == "data/interface/cmdbar/empty_rome.ini");
	CHECK(VolumeRealPath("commonres/x.bmp") == "ui/common/x.bmp");
	CHECK(VolumeRealPath("menuini/a.ini") == "data/interface/menu/a.ini");
	// Anything else, and a name that only looks like a volume, is left as it is.
	CHECK(VolumeRealPath("sounds/ui/click.wav") == "sounds/ui/click.wav");
	CHECK(VolumeRealPath("gameresx/a.bmp") == "gameresx/a.bmp");
	CHECK(VolumeRealPath("gameres") == "gameres");
	CHECK(VolumeOf("units/a") == nullptr);
	CHECK(VolumeOf("gameres/a") != nullptr);
}

void TheKeyIgnoresCaseSlashesAndTheVolume()
{
	CHECK(LayerDiagKey("GameRes\\Icons\\RHastatus.BMP") == "ui/icons/rhastatus.bmp");
	CHECK(LayerDiagKey("UI/ICONS/RHASTATUS.BMP") == LayerDiagKey("gameres/icons/rhastatus.bmp"));
}

void TheEngineAsksForTheLayersFiles()
{
	LayerDiag d;
	d.Reset({"ui/cmdbar/Actions/train RHastatus.bmp", "gameres/icons/RHastatus.bmp", "sounds/Notifications/barrack idle.wav",
		"currentlang/voices/RHastatus/1.wav"});
	CHECK(d.Total() == 4);
	CHECK(d.OpenedCount() == 0);
	// The engine's names: with the volume, in lowercase.
	CHECK(d.Opened("gameres/cmdbar/actions/train rhastatus.bmp") == "ui/cmdbar/Actions/train RHastatus.bmp");
	CHECK(d.Opened("gameres/icons/rhastatus.bmp") == "gameres/icons/RHastatus.bmp");
	// The second time, and a file the layer does not have, give nothing.
	CHECK(d.Opened("gameres/icons/rhastatus.bmp").empty());
	CHECK(d.Opened("gameres/icons/oracul.bmp").empty());
	CHECK(d.Opened("sounds/ui/click.wav").empty());
	CHECK(d.OpenedCount() == 2);
	const std::vector<std::string> left = d.NotOpened();
	CHECK(left.size() == 2);
	CHECK(left[0] == "currentlang/voices/RHastatus/1.wav");
	CHECK(left[1] == "sounds/Notifications/barrack idle.wav");
}

void AResetForgetsTheAccount()
{
	LayerDiag d;
	d.Reset({"a/b.bmp"});
	CHECK(d.Opened("a/b.bmp") == "a/b.bmp");
	d.Reset({"a/b.bmp", "c.wav"});
	CHECK(d.OpenedCount() == 0);
	CHECK(d.NotOpened().size() == 2);
	d.Reset({});
	CHECK(d.Total() == 0 && d.NotOpened().empty() && d.Opened("a/b.bmp").empty());
}

}

int main()
{
	AVolumeIsItsRealFolder();
	TheKeyIgnoresCaseSlashesAndTheVolume();
	TheEngineAsksForTheLayersFiles();
	AResetForgetsTheAccount();
	return test::Summary("layerdiag");
}
