// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/playeralert.h"

namespace {

void BackgroundGetsEverything()
{
	const PlayerAlertPlan p = PlayerAlertPlanFor(PlayerAlert::MatchFound, false, PlayerAlertOptions());
	CHECK(p.sound);
	CHECK(p.flash);
	CHECK(p.front);
}

void ForegroundOnlySounds()
{
	const PlayerAlertPlan p = PlayerAlertPlanFor(PlayerAlert::Countdown, true, PlayerAlertOptions());
	CHECK(p.sound);
	CHECK(!p.flash);
	CHECK(!p.front);
}

void GameStartsOnlyInTheBackground()
{
	const PlayerAlertPlan fore = PlayerAlertPlanFor(PlayerAlert::GameStarts, true, PlayerAlertOptions());
	CHECK(!fore.sound && !fore.flash && !fore.front);
	const PlayerAlertPlan back = PlayerAlertPlanFor(PlayerAlert::GameStarts, false, PlayerAlertOptions());
	CHECK(back.sound && back.flash && back.front);
}

void OptionsAreRespected()
{
	PlayerAlertOptions o;
	o.matchSound = false;
	// The sound of a match found is the league's option; the room's countdown keeps the online sounds.
	CHECK(!PlayerAlertPlanFor(PlayerAlert::MatchFound, false, o).sound);
	CHECK(!PlayerAlertPlanFor(PlayerAlert::CupMatchReady, false, o).sound);
	CHECK(PlayerAlertPlanFor(PlayerAlert::Countdown, false, o).sound);
	o = PlayerAlertOptions();
	o.sound = false;
	CHECK(!PlayerAlertPlanFor(PlayerAlert::MatchFound, false, o).sound);
	CHECK(!PlayerAlertPlanFor(PlayerAlert::Countdown, false, o).sound);
	// Without sounds the taskbar still flashes.
	CHECK(PlayerAlertPlanFor(PlayerAlert::Countdown, false, o).flash);
	o = PlayerAlertOptions();
	o.front = false;
	const PlayerAlertPlan p = PlayerAlertPlanFor(PlayerAlert::MatchFound, false, o);
	CHECK(!p.front);
	CHECK(p.flash);
}

void GateQuietsARestartedCountdown()
{
	PlayerAlertGate g;
	CHECK(g.Allow(7, 1000, 20000));
	CHECK(!g.Allow(7, 6000, 20000));
	CHECK(!g.Allow(7, 20999, 20000));
	CHECK(g.Allow(7, 21000, 20000));
	// Another room calls at once.
	CHECK(g.Allow(8, 21500, 20000));
	CHECK(!g.Allow(8, 22000, 20000));
	// Across the tick count's wrap.
	PlayerAlertGate w;
	CHECK(w.Allow(1, 0xFFFFF000u, 20000));
	CHECK(!w.Allow(1, 0x00000100u, 20000));
	CHECK(w.Allow(1, 0x00010000u, 20000));
}

void CornuIsAShortHornCall()
{
	const unsigned rate = 48000;
	const std::vector<float> s = PlayerAlertCornu(rate);
	const double secs = static_cast<double>(s.size()) / rate;
	CHECK(secs > 1.0 && secs < 1.5);
	float peak = 0.0f;
	bool finite = true;
	for (float v : s) {
		finite = finite && std::isfinite(v);
		peak = (std::max)(peak, std::fabs(v));
	}
	CHECK(finite);
	CHECK(peak > 0.85f && peak <= 0.95f);
	// It starts from silence and rises slowly (no click), and it ends in silence.
	CHECK(std::fabs(s[0]) < 0.01f);
	auto rms = [&](size_t from, size_t n) {
		double sum = 0.0;
		for (size_t i = from; i < from + n && i < s.size(); i++) sum += s[i] * s[i];
		return std::sqrt(sum / n);
	};
	const size_t ms5 = rate / 200;
	CHECK(rms(0, ms5) < rms(rate / 10, ms5));
	CHECK(rms(0, ms5) < 0.2);
	CHECK(std::fabs(s.back()) < 0.01f);
	// The held note is loud for most of its length.
	CHECK(rms(static_cast<size_t>(0.8 * rate), rate / 20) > 0.3);
	CHECK(PlayerAlertCornu(0).empty());
}

}

int main()
{
	BackgroundGetsEverything();
	ForegroundOnlySounds();
	GameStartsOnlyInTheBackground();
	OptionsAreRespected();
	GateQuietsARestartedCountdown();
	CornuIsAShortHornCall();
	return test::Summary("playeralert");
}
