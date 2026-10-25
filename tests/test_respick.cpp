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
	CHECK(Same(BuildResolutionList(ultrawide, {3440, 1440}, {}), {{3440, 1440}, {2560, 1440}, {2560, 1080}, {1920, 1080}}));
	const std::vector<ResSize> fullHd = {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 1024}, {1280, 720}, {1024, 768}};
	CHECK(Same(BuildResolutionList(fullHd, {1366, 768}, {}), {{1366, 768}}));
	CHECK(Same(BuildResolutionList(fullHd, {1920, 1080}, {}), {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 1024}}));
	CHECK(Same(BuildResolutionList(fullHd, {1366, 768}, {{1704, 960}, {2048, 1152}}), {{2048, 1152}, {1704, 960}, {1366, 768}}));
	CHECK(Same(BuildResolutionList({{1280, 720}, {1024, 768}}, {1280, 720}, {}), {{1280, 720}}));
	CHECK(Same(BuildResolutionList({{7680, 4320}, {3840, 2160}}, {7680, 4320}, {}), {{3840, 2160}}));
	CHECK(Same(BuildResolutionList({}, {2560, 1080}, {}), {{2560, 1080}}));
}

int main()
{
	Saved();
	ScreenChanged();
	Fallbacks();
	Keep();
	Build();
	MonitorModes();
	return test::Summary("respick");
}
