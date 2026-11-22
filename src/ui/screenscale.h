// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <algorithm>
#include <cmath>

namespace ui {

// The interface is drawn for a 1920x1080 screen and scaled to the real one. Below kReadableScale the captions stop being readable, so
// a smaller screen gets fewer design units instead (the layouts tighten and scroll); kMinScale is the floor for what must fit whole.
constexpr int kDesignScreenW = 1920, kDesignScreenH = 1080;
constexpr float kMinScale = 0.6f, kReadableScale = 0.8f;
// How much larger than at 1080p a window may be drawn before it would cover the map it floats over.
constexpr float kWindowGrowth = 1.6f;

// How many times larger than the 1920x1080 design the screen is, 1 for smaller ones: everything sized for 1080p keeps its share of a
// 1440p or 4K screen instead of shrinking on it.
inline float ResolutionFactor(int screenW, int screenH)
{
	return std::max(1.0f, std::min(screenW / static_cast<float>(kDesignScreenW), screenH / static_cast<float>(kDesignScreenH)));
}

// The largest scale of a window on this screen: kWindowGrowth over a 1080p one, as much more on a larger one.
inline float MaxWindowScale(int screenW, int screenH) { return kWindowGrowth * ResolutionFactor(screenW, screenH); }

// The scale that fits a window of the design size, with `margin` pixels around it, in the screen.
inline float FitScale(int w, int h, int designW, int designH, int margin = 24)
{
	const float fit = std::min((w - margin) / static_cast<float>(designW), (h - margin) / static_cast<float>(designH));
	return std::max(kMinScale, std::min(MaxWindowScale(w, h), fit));
}

// The screens that fill the whole screen are laid out in pixels of a 1920x1080 one: the scale follows the height, so a 4K screen shows
// the 1080p layout twice as large, and never makes the design narrower than 1200 units.
struct ScreenView {
	float s = 1.0f;
	int w = kDesignScreenW, h = kDesignScreenH;
};

inline ScreenView ScreenViewFor(int screenW, int screenH)
{
	ScreenView v;
	v.s = std::max(kReadableScale, screenH / static_cast<float>(kDesignScreenH));
	v.s = std::max(kMinScale, std::min(v.s, screenW / 1200.0f));
	v.w = static_cast<int>(std::ceil(screenW / v.s - 0.01f));
	v.h = static_cast<int>(std::ceil(screenH / v.s - 0.01f));
	return v;
}

// Where the header of a full screen goes, in the units of ScreenView: the mockup's sizes on a 1920x1080 screen, tighter on a shorter one.
struct ScreenLayout {
	int w = kDesignScreenW, h = kDesignScreenH, margin = 64;
	bool roomy = true;
	int titleY = 0, titleH = 0, titleSize = 84, infoY = 0, infoH = 26;
	// Where the content starts under the header.
	int top = 0;
};

// Without `info` the screen has no line under its title, and its content starts right under the title and its tabs.
inline ScreenLayout ScreenLayoutFor(int w, int h, bool info = true)
{
	ScreenLayout l;
	l.w = w;
	l.h = h;
	l.margin = std::max(32, std::min(64, std::min(l.w * 64 / kDesignScreenW, l.h * 64 / kDesignScreenH)));
	l.roomy = l.h >= 1000;
	l.titleSize = l.roomy ? 84 : 68;
	l.titleH = l.titleSize + 20;
	l.titleY = l.margin - 14;
	l.infoY = l.titleY + l.titleH;
	if (!info) l.infoH = 0;
	l.top = l.infoY + l.infoH + (info ? (l.roomy ? 36 : 24) : (l.roomy ? 28 : 20));
	return l;
}

}
