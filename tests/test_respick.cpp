// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "hooks/respick.h"

static const std::vector<ResSize> kOn1080 = {{1920, 1080}, {1600, 900}, {1366, 768}, {1280, 720}, {2400, 1344}};
static const std::vector<ResSize> kOn768 = {{1366, 768}, {1280, 720}, {1024, 768}, {1704, 960}, {1920, 1080}};

static void Saved()
{
	ResPick p = PickStartResolution(kOn1080, {1366, 768}, {1920, 1080}, {1920, 1080});
	CHECK_EQ(p.index, 2);
	CHECK(p.why == ResChoice::kSaved);
	p = PickStartResolution(kOn1080, {1366, 768}, {}, {1920, 1080});
	CHECK_EQ(p.index, 2);
	CHECK(p.why == ResChoice::kSaved);
	p = PickStartResolution(kOn1080, {2400, 1344}, {1920, 1080}, {1920, 1080});
	CHECK_EQ(p.index, 4);
	CHECK(p.why == ResChoice::kSaved);
}

static void ScreenChanged()
{
	ResPick p = PickStartResolution(kOn768, {1920, 1080}, {1920, 1080}, {1366, 768});
	CHECK_EQ(p.index, 0);
	CHECK(p.why == ResChoice::kScreen);
	p = PickStartResolution(kOn768, {1920, 1080}, {}, {1366, 768});
	CHECK_EQ(p.index, 0);
	CHECK(p.why == ResChoice::kScreen);
	p = PickStartResolution(kOn768, {1704, 960}, {1366, 768}, {1366, 768});
	CHECK_EQ(p.index, 3);
	CHECK(p.why == ResChoice::kSaved);
	p = PickStartResolution(kOn768, {1280, 720}, {1920, 1080}, {1366, 768});
	CHECK_EQ(p.index, 1);
	CHECK(p.why == ResChoice::kSaved);
}

static void Fallbacks()
{
	ResPick p = PickStartResolution(kOn1080, {}, {}, {1920, 1080});
	CHECK_EQ(p.index, 0);
	CHECK(p.why == ResChoice::kScreen);
	p = PickStartResolution(kOn1080, {}, {}, {5120, 2880});
	CHECK_EQ(p.index, 0);
	CHECK(p.why == ResChoice::kFirst);
	p = PickStartResolution({}, {1920, 1080}, {}, {1920, 1080});
	CHECK_EQ(p.index, -1);
	CHECK(p.why == ResChoice::kNone);
}

static void Keep()
{
	CHECK_EQ(KeepResolution(kOn768, {1280, 720}, {1366, 768}), 1);
	CHECK_EQ(KeepResolution(kOn768, {2400, 1344}, {1366, 768}), 0);
	CHECK_EQ(KeepResolution({{1280, 720}}, {2400, 1344}, {1366, 768}), -1);
}

static bool Same(const std::vector<ResSize> &a, const std::vector<ResSize> &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].w != b[i].w || a[i].h != b[i].h) return false;
	}
	return true;
}

static void MonitorModes()
{
	const std::vector<ResSize> fullHd = {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 1024}, {1280, 720}};
	CHECK(Same(BuildResolutionList(fullHd, {1366, 768}, {}, true), {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 1024}}));
	CHECK(Same(BuildResolutionList(fullHd, {1366, 768}, {}, false), {{1366, 768}}));
	const std::vector<ResSize> list = {{1920, 1080}, {1680, 1050}, {1366, 768}};
	ResPick p = PickStartResolution(list, {1920, 1080}, {1920, 1080}, {1366, 768}, true);
	CHECK_EQ(p.index, 0);
	CHECK(p.why == ResChoice::kSaved);
	p = PickStartResolution(list, {1920, 1080}, {1920, 1080}, {1366, 768}, false);
	CHECK_EQ(p.index, 2);
	CHECK(p.why == ResChoice::kScreen);
}

static void Build()
{
	const std::vector<ResSize> ultrawide = {{3440, 1440}, {2560, 1440}, {2560, 1080}, {1920, 1080}, {1920, 1080}, {1280, 720}, {800, 600}, {1024, 768}};
	CHECK(Same(BuildResolutionList(ultrawide, {3440, 1440}, {}), {{3440, 1440}, {2560, 1440}, {2560, 1080}, {1920, 1080}, {1024, 768}}));
	const std::vector<ResSize> fullHd = {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 1024}, {1280, 720}, {1024, 768}};
	CHECK(Same(BuildResolutionList(fullHd, {1366, 768}, {}), {{1366, 768}, {1024, 768}}));
	CHECK(Same(BuildResolutionList(fullHd, {1920, 1080}, {}), {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 1024}, {1024, 768}}));
	CHECK(Same(BuildResolutionList(fullHd, {1366, 768}, {{1704, 960}, {2048, 1152}}), {{2048, 1152}, {1704, 960}, {1366, 768}, {1024, 768}}));
	CHECK(Same(BuildResolutionList({{1280, 720}, {1024, 768}}, {1280, 720}, {}), {{1280, 720}}));
	CHECK(Same(BuildResolutionList({{7680, 4320}, {3840, 2160}}, {7680, 4320}, {}), {{3840, 2160}}));
	CHECK(Same(BuildResolutionList({}, {2560, 1080}, {}), {{2560, 1080}}));
}

// What a 4K monitor reports: the same sizes at several refresh rates and depths, DCI 4K above the desktop, and
// old modes below what the game's menus fit in.
static const std::vector<ResSize> k4k = {{3840, 2160}, {3840, 2160}, {4096, 2160}, {3200, 1800}, {2560, 1600}, {2560, 1440}, {2560, 1440}, {2048, 1152},
	{1920, 1200}, {1920, 1080}, {1920, 1080}, {1680, 1050}, {1600, 1200}, {1600, 900}, {1440, 900}, {1366, 768}, {1360, 768}, {1280, 1024}, {1280, 960},
	{1280, 800}, {1280, 768}, {1280, 720}, {1176, 664}, {1152, 864}, {1024, 768}, {800, 600}, {720, 576}, {720, 480}, {640, 480}};

static void Monitor4k()
{
	const std::vector<ResSize> below = {{3840, 2160}, {3200, 1800}, {2560, 1600}, {2560, 1440}, {2048, 1152}, {1920, 1200}, {1920, 1080}, {1680, 1050},
		{1600, 1200}, {1600, 900}, {1440, 900}, {1366, 768}, {1360, 768}, {1280, 1024}, {1280, 960}, {1280, 800}, {1280, 768}, {1152, 864}, {1024, 768}};
	CHECK(Same(BuildResolutionList(k4k, {3840, 2160}, {}), below));
	std::vector<ResSize> full = below;
	full.insert(full.begin(), {4096, 2160});
	CHECK(Same(BuildResolutionList(k4k, {3840, 2160}, {}, true), full));
	// Windows at 1920x1080 on the 4K monitor: in a window only up to the desktop, in full screen every mode.
	const std::vector<ResSize> windowed = BuildResolutionList(k4k, {1920, 1080}, {});
	CHECK_EQ(static_cast<int>(windowed.size()), 12);
	CHECK(windowed.front().w == 1920 && windowed.front().h == 1080);
	CHECK(windowed.back().w == 1024 && windowed.back().h == 768);
	CHECK(Same(BuildResolutionList(k4k, {1920, 1080}, {}, true), full));
	CHECK(Same(BuildResolutionList(k4k, {1280, 720}, {}), {{1280, 720}}));
}

int main()
{
	Saved();
	ScreenChanged();
	Fallbacks();
	Keep();
	Build();
	MonitorModes();
	Monitor4k();
	return test::Summary("respick");
}
