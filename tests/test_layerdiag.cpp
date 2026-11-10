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

void ASharedFileIsReportedOnceWithItsSources()
{
	// clash-b/blue is mounted before clash-a, as the load order puts a cosmetic pack above a rules pack.
	SharedFiles s;
	s.Reset({{"clash-b/blue", "mods/clash-b/rulesets/blue/", "C:\\g\\mods\\clash-b\\rulesets\\blue"}, {"clash-b", "mods/clash-b/", "C:\\g\\mods\\clash-b"},
				{"clash-a", "mods/clash-a/", "C:\\g\\mods\\clash-a"}},
		{{"gameres/icons/RHastatus.bmp", "minimap/zoom16/entities/a.bmp"}, {"ui/other.bmp"}, {"gameres/icons/RHastatus.bmp", "civs/rome.json"}});
	CHECK(s.Count() == 1);
	// The engine's name has the volume and lowercase; the sources spell it as they like.
	const std::vector<size_t> among = s.Asked("gameres/icons/rhastatus.bmp");
	CHECK(among.size() == 2 && among[0] == 0 && among[1] == 2);
	CHECK(s.Asked("gameres/icons/rhastatus.bmp").empty());
	CHECK(s.Asked("minimap/zoom16/entities/a.bmp").empty());
	CHECK(s.Asked("ui/other.bmp").empty());
	CHECK(s.PathIn(2, "gameres/icons/rhastatus.bmp") == "gameres/icons/RHastatus.bmp");
	CHECK(s.PathIn(1, "gameres/icons/rhastatus.bmp").empty());
	// Who a VFS name belongs to: the longest start wins, and only among the sources that have the file.
	CHECK(s.OwnerOf("mods/clash-b/rulesets/blue/gameres/icons/rhastatus.bmp", among) == 0);
	CHECK(s.OwnerOf("mods/Clash-A/gameres/icons/rhastatus.bmp", among) == 2);
	CHECK(s.OwnerOf("mods/clash-b/gameres/icons/rhastatus.bmp", among) == -1);
	CHECK(s.OwnerOf("gameres/icons/rhastatus.bmp", among) == -1);
	CHECK(s.OwnerOf("mods/clash-b/x.bmp", {1}) == 1);
}

void TwoSpellingsOfOneFileAreOneSharedFile()
{
	SharedFiles s;
	s.Reset({{"a", "mods/a/", "d\\a"}, {"b", "mods/b/", "d\\b"}}, {{"ui/icons/x.bmp"}, {"gameres/icons/x.bmp"}});
	CHECK(s.Count() == 1);
	CHECK(s.Asked("gameres/icons/x.bmp").size() == 2);
	// A file one source has twice, or alone, is not shared.
	s.Reset({{"a", "mods/a/", "d\\a"}}, {{"ui/icons/x.bmp", "gameres/icons/x.bmp"}});
	CHECK(s.Count() == 0 && s.Asked("gameres/icons/x.bmp").empty());
}

void AVolumeCoversItsRealFolderToo()
{
	CHECK(VolumeCovers("gameres/icons/a.bmp") && VolumeCovers("ui/icons/a.bmp") && VolumeCovers("data/interface/menu/a.ini"));
	CHECK(!VolumeCovers("sounds/a.wav") && !VolumeCovers("minimap/zoom16/a.bmp") && !VolumeCovers("data/classes/a.xml"));
}

}

int main()
{
	AVolumeIsItsRealFolder();
	TheKeyIgnoresCaseSlashesAndTheVolume();
	TheEngineAsksForTheLayersFiles();
	AResetForgetsTheAccount();
	ASharedFileIsReportedOnceWithItsSources();
	TwoSpellingsOfOneFileAreOneSharedFile();
	AVolumeCoversItsRealFolderToo();
	return test::Summary("layerdiag");
}
