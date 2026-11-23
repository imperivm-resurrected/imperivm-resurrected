// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/playeralert.h"

#include <algorithm>
#include <cmath>

PlayerAlertPlan PlayerAlertPlanFor(PlayerAlert kind, bool foreground, const PlayerAlertOptions &o)
{
	PlayerAlertPlan p;
	if (kind == PlayerAlert::GameStarts && foreground) return p;
	const bool league = kind == PlayerAlert::MatchFound || kind == PlayerAlert::CupMatchReady;
	p.sound = o.sound && (!league || o.matchSound);
	p.flash = !foreground;
	p.front = !foreground && o.front;
	return p;
}

bool PlayerAlertGate::Allow(uint64_t room, uint32_t now, uint32_t quietMs)
{
	if (any_ && room == room_ && now - at_ < quietMs) return false;
	any_ = true;
	room_ = room;
	at_ = now;
	return true;
}

std::vector<float> PlayerAlertCornu(unsigned rate)
{
	if (rate < 8000) return {};
	struct Note {
		float freq, start, length;
	};
	const Note kNotes[] = {{196.0f, 0.0f, 0.24f}, {293.66f, 0.22f, 0.24f}, {392.0f, 0.44f, 0.62f}};
	// The partials of a brass tone; the high ones come in as the note speaks.
	const float kPartials[] = {1.0f, 0.78f, 0.58f, 0.44f, 0.31f, 0.21f, 0.13f, 0.08f};
	const float attack = 0.06f, release = 0.16f, twoPi = 6.2831853f;
	const float total = kNotes[2].start + kNotes[2].length + release;
	std::vector<float> out(static_cast<size_t>(total * rate) + 1, 0.0f);
	for (const Note &n : kNotes) {
		const size_t first = static_cast<size_t>(n.start * rate);
		const size_t count = static_cast<size_t>((n.length + release) * rate);
		double phase = 0.0;
		for (size_t i = 0; i < count && first + i < out.size(); i++) {
			const float t = static_cast<float>(i) / rate;
			float env = t < attack ? 0.5f - 0.5f * cosf(3.14159265f * t / attack) : 1.0f;
			// A small swell once the note stands, then the breath ends.
			env *= 0.86f + 0.14f * std::min(1.0f, t / 0.3f);
			if (t > n.length) env *= std::max(0.0f, 0.5f + 0.5f * cosf(3.14159265f * (t - n.length) / release));
			const float bright = std::min(1.0f, t / 0.09f);
			// The lips find the pitch from a little under it; the long note takes a slight vibrato.
			const float scoop = 1.0f - 0.03f * expf(-t / 0.025f);
			const float depth = 0.006f * std::min(1.0f, std::max(0.0f, (t - 0.15f) / 0.2f));
			const float vibrato = 1.0f + depth * sinf(twoPi * 5.2f * t);
			phase += twoPi * n.freq * scoop * vibrato / rate;
			if (phase > twoPi) phase -= twoPi;
			float v = 0.0f;
			for (int k = 0; k < 8; k++) {
				const float a = kPartials[k] * (k ? powf(bright, 0.5f * k) : 1.0f);
				v += a * sinf(static_cast<float>(phase * (k + 1)));
			}
			out[first + i] += v * env;
		}
	}
	float peak = 0.0f;
	for (float v : out) peak = std::max(peak, fabsf(v));
	if (peak <= 0.0f) return out;
	// A soft saturation for the horn's edge, then to 0.9 at the loudest.
	const float drive = 1.4f, norm = tanhf(drive);
	float after = 0.0f;
	for (float &v : out) {
		v = tanhf(drive * v / peak) / norm;
		after = std::max(after, fabsf(v));
	}
	for (float &v : out) v *= 0.9f / after;
	return out;
}
