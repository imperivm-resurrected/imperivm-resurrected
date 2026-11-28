// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <iterator>
#include <vector>
#include "addr/state.h"
#include "net/spectator.h"
#include "net/steamnet.h"

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
namespace {
const int kHudTopBand = 80;
const int kStatusTop = 10;
const int kStatusLineHalf = 8;
const int kStatusRightMargin = 14;
const int kBadgeFontPx = 17;
const ShapePoint kEyeOutline[] = {
	{0.4945f, 0.0009f}, {0.4270f, 0.0092f}, {0.3588f, 0.0263f}, {0.2904f, 0.0518f}, {0.2222f, 0.0856f}, {0.1547f, 0.1276f},
	{0.0883f, 0.1774f}, {0.0751f, 0.1884f}, {0.0620f, 0.1998f}, {0.0490f, 0.2113f}, {0.0365f, 0.2230f}, {0.0247f, 0.2345f},
	{0.0136f, 0.2456f}, {0.0001f, 0.2595f}, {0.0072f, 0.2673f}, {0.0142f, 0.2746f}, {0.0236f, 0.2839f}, {0.0346f, 0.2944f},
	{0.0464f, 0.3056f}, {0.0585f, 0.3167f}, {0.0699f, 0.3270f}, {0.1247f, 0.3721f}, {0.1801f, 0.4113f}, {0.2359f, 0.4444f},
	{0.2919f, 0.4714f}, {0.3479f, 0.4922f}, {0.4039f, 0.5066f}, {0.4220f, 0.5099f}, {0.4388f, 0.5126f}, {0.4549f, 0.5145f},
	{0.4712f, 0.5158f}, {0.4884f, 0.5165f}, {0.5070f, 0.5167f}, {0.5205f, 0.5167f}, {0.5318f, 0.5165f}, {0.5417f, 0.5161f},
	{0.5512f, 0.5154f}, {0.5613f, 0.5143f}, {0.5730f, 0.5129f}, {0.6265f, 0.5032f}, {0.6795f, 0.4881f}, {0.7320f, 0.4675f},
	{0.7837f, 0.4415f}, {0.8345f, 0.4102f}, {0.8841f, 0.3737f}, {0.9043f, 0.3571f}, {0.9242f, 0.3397f}, {0.9434f, 0.3216f},
	{0.9617f, 0.3033f}, {0.9786f, 0.2851f}, {0.9940f, 0.2673f}, {1.0001f, 0.2601f}, {0.9980f, 0.2576f}, {0.9795f, 0.2361f},
	{0.9605f, 0.2152f}, {0.9410f, 0.1950f}, {0.9213f, 0.1756f}, {0.9014f, 0.1572f}, {0.8815f, 0.1401f}, {0.8206f, 0.0944f},
	{0.7580f, 0.0576f}, {0.6940f, 0.0299f}, {0.6287f, 0.0111f}, {0.5621f, 0.0014f}, {0.4945f, 0.0009f},
};
const ShapePoint kEyeIris[] = {
	{0.5308f, 0.0465f}, {0.5446f, 0.0485f}, {0.5580f, 0.0512f}, {0.5711f, 0.0548f}, {0.5838f, 0.0591f}, {0.5965f, 0.0642f},
	{0.6091f, 0.0702f}, {0.6439f, 0.0924f}, {0.6733f, 0.1200f}, {0.6968f, 0.1520f}, {0.7138f, 0.1877f}, {0.7238f, 0.2261f},
	{0.7263f, 0.2664f}, {0.7243f, 0.2890f}, {0.7200f, 0.3109f}, {0.7134f, 0.3320f}, {0.7045f, 0.3524f}, {0.6933f, 0.3720f},
	{0.6800f, 0.3906f}, {0.6758f, 0.3956f}, {0.6703f, 0.4015f}, {0.6641f, 0.4078f}, {0.6578f, 0.4140f}, {0.6518f, 0.4195f},
	{0.6469f, 0.4237f}, {0.6307f, 0.4356f}, {0.6136f, 0.4458f}, {0.5956f, 0.4545f}, {0.5768f, 0.4616f}, {0.5572f, 0.4672f},
	{0.5367f, 0.4712f}, {0.5274f, 0.4722f}, {0.5167f, 0.4729f}, {0.5053f, 0.4731f}, {0.4938f, 0.4729f}, {0.4829f, 0.4722f},
	{0.4734f, 0.4712f}, {0.4520f, 0.4670f}, {0.4314f, 0.4611f}, {0.4118f, 0.4534f}, {0.3931f, 0.4440f}, {0.3753f, 0.4328f},
	{0.3586f, 0.4199f}, {0.3535f, 0.4154f}, {0.3479f, 0.4099f}, {0.3421f, 0.4040f}, {0.3364f, 0.3979f}, {0.3313f, 0.3921f},
	{0.3271f, 0.3869f}, {0.3162f, 0.3713f}, {0.3068f, 0.3549f}, {0.2990f, 0.3379f}, {0.2927f, 0.3201f}, {0.2879f, 0.3018f},
	{0.2848f, 0.2828f}, {0.2839f, 0.2728f}, {0.2836f, 0.2618f}, {0.2838f, 0.2505f}, {0.2845f, 0.2391f}, {0.2858f, 0.2281f},
	{0.2875f, 0.2179f}, {0.3014f, 0.1745f}, {0.3240f, 0.1357f}, {0.3540f, 0.1025f}, {0.3905f, 0.0759f}, {0.4322f, 0.0569f},
	{0.4781f, 0.0466f}, {0.4855f, 0.0459f}, {0.4946f, 0.0456f}, {0.5045f, 0.0454f}, {0.5144f, 0.0455f}, {0.5234f, 0.0459f},
	{0.5308f, 0.0465f},
};
const ShapePoint kEyePupil[] = {
	{0.4941f, 0.1407f}, {0.4729f, 0.1447f}, {0.4531f, 0.1521f}, {0.4349f, 0.1627f}, {0.4189f, 0.1760f}, {0.4053f, 0.1919f},
	{0.3946f, 0.2102f}, {0.3886f, 0.2252f}, {0.3848f, 0.2407f}, {0.3831f, 0.2564f}, {0.3836f, 0.2721f}, {0.3862f, 0.2877f},
	{0.3909f, 0.3030f}, {0.3986f, 0.3191f}, {0.4085f, 0.3339f}, {0.4205f, 0.3471f}, {0.4341f, 0.3584f}, {0.4494f, 0.3677f},
	{0.4659f, 0.3746f}, {0.4731f, 0.3769f}, {0.4792f, 0.3786f}, {0.4850f, 0.3797f}, {0.4912f, 0.3804f}, {0.4983f, 0.3807f},
	{0.5070f, 0.3808f}, {0.5142f, 0.3808f}, {0.5194f, 0.3807f}, {0.5234f, 0.3805f}, {0.5268f, 0.3801f}, {0.5301f, 0.3795f},
	{0.5340f, 0.3786f}, {0.5394f, 0.3773f}, {0.5441f, 0.3759f}, {0.5485f, 0.3745f}, {0.5529f, 0.3729f}, {0.5573f, 0.3710f},
	{0.5621f, 0.3688f}, {0.5783f, 0.3593f}, {0.5928f, 0.3477f}, {0.6052f, 0.3341f}, {0.6155f, 0.3187f}, {0.6233f, 0.3018f},
	{0.6287f, 0.2836f}, {0.6296f, 0.2773f}, {0.6301f, 0.2692f}, {0.6302f, 0.2602f}, {0.6299f, 0.2512f}, {0.6293f, 0.2430f},
	{0.6284f, 0.2367f}, {0.6258f, 0.2269f}, {0.6225f, 0.2175f}, {0.6186f, 0.2086f}, {0.6139f, 0.2001f}, {0.6085f, 0.1918f},
	{0.6023f, 0.1838f}, {0.5881f, 0.1697f}, {0.5718f, 0.1580f}, {0.5538f, 0.1491f}, {0.5345f, 0.1432f}, {0.5145f, 0.1403f},
	{0.4941f, 0.1407f},
};

const std::vector<std::vector<ShapePoint>> &EyeShape()
{
	static const std::vector<std::vector<ShapePoint>> shape = {
		{std::begin(kEyeOutline), std::end(kEyeOutline)},
		{std::begin(kEyeIris), std::end(kEyeIris)},
		{std::begin(kEyePupil), std::end(kEyePupil)},
	};
	return shape;
}
}
}


namespace modui_impl {
void DrawSpectatorBar(uint32_t *buf, int W, int H, float s, int topY)
{
	if (SpectatorCovering()) return;
	SpectatorState st;
	if (!SpectatorControls(st)) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_spec.barShown = false;
		g_spec.drag = false;
		g_spec.move = false;
		return;
	}
	POINT mouse;
	bool drag;
	double dragFrac;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		mouse = {g_hover.x, g_hover.y};
		drag = g_spec.drag;
		dragFrac = g_spec.dragFrac;
	}
	double posX = 0.5, posTop = -1;
	SpectatorBarPos(posX, posTop, 0);
	EnsureFonts(s);
	const COLORREF panel = pal::panel, frameOuter = pal::hairline, frameInner = pal::lineStrong;
	const COLORREF btn = pal::button, btnHot = pal::buttonHover, strong = pal::red, strongHot = pal::redPressed;
	const COLORREF label = pal::body, red = pal::redText;
	const float line = std::max(1.0f, s), rad = 2.0f * s;
	const float pad = 12 * s, gap = 8 * s;
	const float pillH = 32 * s;
	const bool collapsed = st.collapsed;
	const bool pinned = st.broadcast && st.atLive;
	const unsigned shownCur = pinned ? st.total : st.cur;
	const bool following = st.canFollow && st.follow >= 0;
	const float barH = collapsed ? 40 * s : 58 * s;
	const float barW = collapsed ? (following ? 214 : 196) * s : std::min(static_cast<float>(W) - 24, (st.broadcast ? 1150.0f : 880.0f) * s);
	float bx = static_cast<float>(posX * W - barW / 2), by =posTop >= 0 ? static_cast<float>(posTop * H) : static_cast<float>(topY);
	bx = std::max(4.0f, std::min(static_cast<float>(W) - barW - 4, std::floor(bx)));
	by = std::max(2.0f, std::min(static_cast<float>(H) - barH - 4, by));
	const float cy = by + barH / 2;

	const float mx = static_cast<float>(mouse.x), my = static_cast<float>(mouse.y);
	FillRound(buf, W, H, bx, by, barW, barH, rad, panel, 0.96f);
	StrokeRound(buf, W, H, bx, by, barW, barH, rad, line, frameOuter, 1.0f);

	auto button = [&](const RectF &r, bool isStrong, bool active, bool hot) {
		const bool useStrong = isStrong || active;
		FillRound(buf, W, H, r.x, r.y, r.w, r.h, rad, useStrong ? (hot ? strongHot : strong) : (hot ? btnHot : btn), 1.0f);
	};
	auto chevron = [&](float cx, float cyy, float hgt, int dir, COLORREF c) {
		const float wd = hgt * 0.62f;
		if (dir < 0) FillTriangle(buf, W, H, cx + wd / 2, cyy - hgt / 2, cx + wd / 2, cyy + hgt / 2, cx - wd / 2, cyy, c, 1.0f);
		else FillTriangle(buf, W, H, cx - wd / 2, cyy - hgt / 2, cx - wd / 2, cyy + hgt / 2, cx + wd / 2, cyy, c, 1.0f);
	};
	auto arrow = [&](float cx, float cyy, float wd, bool up, COLORREF c) {
		if (up) FillTriangle(buf, W, H, cx - wd / 2, cyy + wd / 4, cx + wd / 2, cyy + wd / 4, cx, cyy - wd / 4, c, 1.0f);
		else FillTriangle(buf, W, H, cx - wd / 2, cyy - wd / 4, cx + wd / 2, cyy - wd / 4, cx, cyy + wd / 4, c, 1.0f);
	};

	RectF rCollapse{bx + pad, cy - 13 * s, 26 * s, 26 * s};
	RectF rBack, rPlay, rFwd, rSpeed, rUi, rLive, rFollow, rCur, rTotal, rTrack;
	RectF rSpeeds[4];
	// A slash over an icon: a thin quad of two triangles.
	auto slash = [&](float x0, float y0, float x1, float y1, float th, COLORREF c) {
		const float dx = x1 - x0, dy = y1 - y0, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy)), nx = -dy / l * th / 2, ny = dx / l * th / 2;
		FillTriangle(buf, W, H, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c, 1.0f);
		FillTriangle(buf, W, H, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c, 1.0f);
	};
	std::string tipFor;
	std::vector<ControlKey> tipKeys;
	float tipX = bx;

	if (collapsed) {
		rCollapse = {bx, by, barW, barH};
		const bool h = rCollapse.Has(mx, my);
		if (h) FillRound(buf, W, H, bx + line, by + line, barW - 2 * line, barH - 2 * line, rad, btnHot, 1.0f);
		float x = bx + pad;
		if (st.paused) FillTriangle(buf, W, H, x, cy - 7 * s, x, cy + 7 * s, x + 11 * s, cy, label, 1.0f);
		else {
			FillRound(buf, W, H, x, cy - 7 * s, 4 * s, 14 * s, 1.0f * s, label, 1.0f);
			FillRound(buf, W, H, x + 7 * s, cy - 7 * s, 4 * s, 14 * s, 1.0f * s, label, 1.0f);
		}
		x += 22 * s;
		DrawLabel(buf, W, H, ClockText(shownCur), x, by, 48 * s, barH, DT_RIGHT, pal::text, 1.0f);
		x += 52 * s;
		DrawLabel(buf, W, H, L"/ " + ClockText(st.total), x, by, 64 * s, barH, DT_LEFT, pal::textDim, 1.0f);
		float rx = bx + barW - pad;
		arrow(rx - 6 * s, cy, 12 * s, false, label);
		rx -= 22 * s;
		if (following) FillDisc(buf, W, H, rx - 5 * s, cy, 5 * s, RGB((st.followRgb >> 16) & 0xff, (st.followRgb >> 8) & 0xff, st.followRgb & 0xff), 1.0f);
		// The progress, a line along the pill's foot.
		const double frac = st.total ? std::min(1.0, static_cast<double>(shownCur) / st.total) : 0.0;
		const float lx = bx + rad, lw = barW - 2 * rad, ly = by + barH - 4 * s;
		FillRound(buf, W, H, lx, ly, lw, 2 * s, 1.0f * s, pal::bg, 1.0f);
		if (frac > 0) FillRound(buf, W, H, lx, ly, std::max(2 * s, static_cast<float>(lw * frac)), 2 * s, 1.0f * s, pal::redText, 1.0f);
		if (h) { tipFor = "Show the bar"; tipKeys = {ControlsModKey(kModKeySpecBar)}; tipX = rCollapse.x; }
	}
	else {
		float x = rCollapse.x + rCollapse.w + gap;
		rBack = {x, cy - pillH / 2, 64 * s, pillH};
		x += rBack.w + gap;
		rPlay = {x, cy - 19 * s, 54 * s, 38 * s};
		x += rPlay.w + gap;
		rFwd = {x, cy - pillH / 2, 64 * s, pillH};
		x += rFwd.w + gap * 1.5f;
		float rx = bx + barW - pad;
		if (st.broadcast) {
			rLive = {rx - 104 * s, cy - pillH / 2, 104 * s, pillH};
			rx -= rLive.w + gap;
		}
		if (st.canFollow) {
			rFollow = {rx - 170 * s, cy - pillH / 2, 170 * s, pillH};
			rx -= rFollow.w + gap;
		}
		rUi = {rx - 40 * s, cy - pillH / 2, 40 * s, pillH};
		rx -= rUi.w + gap;
		rSpeed = {rx - 4 * 36 * s, cy - pillH / 2, 4 * 36 * s, pillH};
		for (int k = 0; k < 4; k++) rSpeeds[k] = {rSpeed.x + k * 36 * s, rSpeed.y, 36 * s, pillH};
		rx -= rSpeed.w + gap * 1.5f;
		const float lab = 50 * s;
		rCur = {x, cy - 12 * s, lab, 24 * s};
		x += lab + gap * 0.5f;
		rTotal = {rx - lab, cy - 12 * s, lab, 24 * s};
		rx -= lab + gap * 0.5f;
		rTrack = {x, cy - 14 * s, std::max(40.0f, rx - x), 28 * s};

		const bool hCollapse = rCollapse.Has(mx, my), hBack = rBack.Has(mx, my), hPlay = rPlay.Has(mx, my), hFwd = rFwd.Has(mx, my);
		const bool hSpeed = rSpeed.Has(mx, my), hUi = rUi.Has(mx, my), hLive = st.broadcast && rLive.Has(mx, my), hTrack = rTrack.Has(mx, my) || drag;
		const bool hFollow = st.canFollow && rFollow.Has(mx, my);

		button(rCollapse, false, false, hCollapse);
		arrow(rCollapse.x + rCollapse.w / 2, cy, 12 * s, true, label);
		button(rBack, false, false, hBack);
		{
			const float cx = rBack.x + rBack.w / 2;
			chevron(cx - 17 * s, cy, 12 * s, -1, label);
			chevron(cx - 9 * s, cy, 12 * s, -1, label);
			DrawLabel(buf, W, H, L"10", cx - 1 * s, cy - 12 * s, 26 * s, 24 * s, DT_LEFT, label, 1.0f);
		}
		button(rFwd, false, false, hFwd);
		{
			const float fx = rFwd.x + rFwd.w / 2;
			DrawLabel(buf, W, H, L"10", fx - 25 * s, cy - 12 * s, 24 * s, 24 * s, DT_RIGHT, label, 1.0f);
			chevron(fx + 9 * s, cy, 12 * s, 1, label);
			chevron(fx + 17 * s, cy, 12 * s, 1, label);
		}
		button(rPlay, true, false, hPlay);
		{
			const float cx = rPlay.x + rPlay.w / 2;
			if (st.paused) {
				FillTriangle(buf, W, H, cx - 5 * s, cy - 9 * s, cx - 5 * s, cy + 9 * s, cx + 9 * s, cy, label, 1.0f);
			}
			else {
				FillRound(buf, W, H, cx - 7 * s, cy - 8 * s, 5 * s, 16 * s, 1.0f * s, label, 1.0f);
				FillRound(buf, W, H, cx + 2 * s, cy - 8 * s, 5 * s, 16 * s, 1.0f * s, label, 1.0f);
			}
		}
		// The speeds side by side, the one in use in red; faster ones are dim at the live edge.
		FillRound(buf, W, H, rSpeed.x, rSpeed.y, rSpeed.w, rSpeed.h, rad, btn, 1.0f);
		for (int k = 0; k < 4; k++) {
			const int v = 1 << k;
			const bool on = st.speed == v, hot = !on && rSpeeds[k].Has(mx, my);
			if (on || hot) FillRound(buf, W, H, rSpeeds[k].x, rSpeeds[k].y, rSpeeds[k].w, rSpeeds[k].h, rad, on ? strong : btnHot, 1.0f);
			wchar_t text[8];
			swprintf_s(text, L"%d×", v);
			const bool dim = !on && v > st.speed && st.broadcast && st.atLive;
			DrawLabel(buf, W, H, text, rSpeeds[k].x, rSpeeds[k].y, rSpeeds[k].w, rSpeeds[k].h, DT_CENTER, on ? pal::onRed : dim ? pal::textDim : label, 1.0f);
		}
		// The game's interface: an eye, struck through while it is hidden.
		button(rUi, false, false, hUi);
		{
			const float eyeW = 20 * s, eyeH = eyeW * 0.5172f, ex = rUi.x + (rUi.w - eyeW) / 2, ey = cy - eyeH / 2;
			FillShape(buf, W, H, EyeShape(), ex, ey, eyeW, st.uiHidden ? pal::textDim : label, 1.0f);
			if (st.uiHidden) slash(ex, ey - 2 * s, ex + eyeW, ey + eyeH + 2 * s, 2.0f * s, label);
		}
		// Following: the player's colour and name; nobody, a sight and the word.
		if (st.canFollow) {
			button(rFollow, false, st.follow >= 0 && !st.followPaused, hFollow);
			float tx = rFollow.x + 12 * s;
			std::string text;
			if (st.follow < 0) {
				StrokeRound(buf, W, H, tx, cy - 7 * s, 14 * s, 14 * s, 7 * s, 1.5f * s, label, 1.0f);
				FillDisc(buf, W, H, tx + 7 * s, cy, 2.5f * s, label, 1.0f);
				tx += 22 * s;
				text = Tr("Follow");
			}
			else {
				const COLORREF dot = RGB((st.followRgb >> 16) & 0xff, (st.followRgb >> 8) & 0xff, st.followRgb & 0xff);
				FillDisc(buf, W, H, tx + 6 * s, cy, 6 * s, dot, 1.0f);
				tx += 20 * s;
				const std::string name = st.followName.empty() ? Tr("Player") : st.followName;
				text = st.followPaused ? TrFmt("%s (paused)", {name}) : name;
			}
			DrawLabel(buf, W, H, ::W(text), tx, rFollow.y, rFollow.x + rFollow.w - 10 * s - tx, rFollow.h, DT_LEFT | DT_END_ELLIPSIS, label, 1.0f);
		}

		if (st.broadcast) {
			button(rLive, st.ended, st.atLive, hLive);
			const float dx = rLive.x + 16 * s;
			if (st.atLive && !st.ended) FillDisc(buf, W, H, dx, cy, 6.5f * s, red, 0.3f);
			if (!st.ended) FillDisc(buf, W, H, dx, cy, 4 * s, st.atLive ? red : pal::caption, 1.0f);
			std::wstring text = ::W(Tr(st.ended ? "Leave" : "Go live"));
			CharUpperBuffW(&text[0], static_cast<DWORD>(text.size()));
			DrawLabel(buf, W, H, text, st.ended ? rLive.x : rLive.x + 28 * s, rLive.y, st.ended ? rLive.w : rLive.w - 34 * s, rLive.h, st.ended ? DT_CENTER : (DT_LEFT | DT_END_ELLIPSIS), label, 1.0f);
		}
		double frac = st.total ? std::min(1.0, static_cast<double>(shownCur) / st.total) : 0.0;
		if (drag) frac = dragFrac;
		const float trackX0 = rTrack.x + 6 * s, trackX1 = rTrack.x + rTrack.w - 6 * s, trackLen = std::max(1.0f, trackX1 - trackX0);
		{
			const float th = (hTrack ? 10.0f : 8.0f) * s;
			FillRound(buf, W, H, trackX0, cy - th / 2, trackLen, th, 1.0f * s, pal::bg, 1.0f);
			StrokeRound(buf, W, H, trackX0, cy - th / 2, trackLen, th, 1.0f * s, line, frameInner, 1.0f);
			const float fillLen = static_cast<float>(trackLen * frac);
			if (fillLen > 1.0f) FillRound(buf, W, H, trackX0 + line, cy - th / 2 + line, std::max(1.0f, fillLen - line), th - 2 * line, 0.5f * s, st.busy ? RGB(120, 160, 210) : pal::redText, 1.0f);
			const float hw = (hTrack ? 10.0f : 8.0f) * s, hh = (hTrack ? 22.0f : 18.0f) * s, hx = trackX0 + fillLen;
			FillRound(buf, W, H, hx - hw / 2, cy - hh / 2, hw, hh, rad, hTrack ? strongHot : strong, 1.0f);
			DrawLabel(buf, W, H, ClockText(drag ? static_cast<unsigned>(dragFrac * st.total) : shownCur), rCur.x, rCur.y, rCur.w, rCur.h, DT_RIGHT, pal::text, 1.0f);
			DrawLabel(buf, W, H, ClockText(st.total), rTotal.x, rTotal.y, rTotal.w, rTotal.h, DT_LEFT, pal::textDim, 1.0f);
			if (hTrack && !drag) {
				const double f = std::max(0.0, std::min(1.0, static_cast<double>((mx - trackX0) / trackLen)));
				const std::wstring t = ClockText(static_cast<unsigned>(f * st.total));
				const float bw = 58 * s, bh = 26 * s, bxx = std::max(bx + 4 * s, std::min(bx + barW - bw - 4 * s, mx - bw / 2));
				const float byy = by + barH + 8 * s + bh > H ? by - bh - 8 * s : by + barH + 8 * s;
				FillRound(buf, W, H, bxx, byy, bw, bh, rad, panel, 0.97f);
				StrokeRound(buf, W, H, bxx, byy, bw, bh, rad, line, frameOuter, 1.0f);
				DrawLabel(buf, W, H, t, bxx, byy, bw, bh, DT_CENTER, label, 1.0f);
			}
		}
		if (!drag) {
			if (hCollapse) { tipFor = "Hide the bar"; tipKeys = {ControlsModKey(kModKeySpecBar)}; tipX = rCollapse.x; }
			else if (hBack) { tipFor = "Back 10 s"; tipKeys = {ControlsModKey(kModKeySpecBack)}; tipX = rBack.x; }
			else if (hPlay) { tipFor = st.paused ? "Resume" : "Pause"; tipKeys = {ControlsModKey(kModKeySpecPause)}; tipX = rPlay.x; }
			else if (hFwd) { tipFor = "Forward 10 s"; tipKeys = {ControlsModKey(kModKeySpecForward)}; tipX = rFwd.x; }
			else if (hSpeed) { tipFor = "Speed"; tipKeys = {ControlsModKey(kModKeySpecFaster), ControlsModKey(kModKeySpecSlower)}; tipX = rSpeed.x - 40 * s; }
			else if (hUi) { tipFor = st.uiHidden ? "Show the game interface" : "Hide the game interface"; tipKeys = {ControlsModKey(kModKeySpecUi)}; tipX = rUi.x - 100 * s; }
			else if (hLive) { tipFor = st.ended ? "Leave to the menu" : "Jump to live"; tipKeys = {ControlsModKey(kModKeySpecLive)}; tipX = rLive.x - 40 * s; }
			else if (hFollow) {
				tipFor = st.followPaused ? "Follow the player again" : "Follow a player's view: each click picks the next one";
				tipKeys = {};
				tipX = rFollow.x - 120 * s;
			}
		}
	}
	if (!tipFor.empty()) {
		const float tipY = by + barH + 30 * s > H - 40 * s ? by - 30 * s : by + barH + 30 * s;
		ModUiDrawHoverCard(buf, W, H, tipFor, pal::redText, static_cast<int>(std::max(4.0f, tipX)), static_cast<int>(tipY), KnownKeys(tipKeys));
	}
	std::lock_guard<std::mutex> lock(g_lock);
	auto store = [&](int i, const RectF &r) { g_spec.btn[i] = {static_cast<LONG>(r.x), static_cast<LONG>(r.y), static_cast<LONG>(r.x + r.w), static_cast<LONG>(r.y + r.h)}; };
	for (int i = 0; i < 13; i++) g_spec.btn[i] = RECT{};
	if (collapsed) {
		store(7, rCollapse);
	}
	else {
		store(0, rPlay);
		for (int k = 0; k < 4; k++) store(9 + k, rSpeeds[k]);
		store(2, rBack);
		store(3, rFwd);
		if (st.broadcast) store(4, rLive);
		store(6, rUi);
		store(7, rCollapse);
		if (st.canFollow) store(8, rFollow);
		const float trackX0 = rTrack.x + 6 * s, trackX1 = rTrack.x + rTrack.w - 6 * s;
		g_spec.btn[5] = {static_cast<LONG>(trackX0), static_cast<LONG>(rTrack.y), static_cast<LONG>(trackX1), static_cast<LONG>(rTrack.y + rTrack.h)};
	}
	g_spec.bar = {static_cast<LONG>(bx), static_cast<LONG>(by), static_cast<LONG>(bx + barW), static_cast<LONG>(by + barH)};
	g_spec.winW = W;
	g_spec.winH = H;
	g_spec.barShown = true;
}

void DrawWatchersBadge(uint32_t *buf, int W, int H)
{
	const int watchers = SteamRoomWatchers();
	if (watchers <= 0 || !*reinterpret_cast<void **>(addr::kGame)) return;
	SpectatorState watching;
	if (SpectatorControls(watching)) return;
	static HFONT font = CreateFontW(-kBadgeFontPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	const COLORREF panel = pal::panel, frame = pal::hairline, ink = pal::body;
	wchar_t text[16];
	swprintf_s(text, L"%d", watchers);
	if (!g_spec.text.Ensure(400, 64)) return;
	SelectObject(g_spec.text.dc, font);
	SIZE size = {};
	GetTextExtentPoint32W(g_spec.text.dc, text, static_cast<int>(wcslen(text)), &size);
	const float pad = 9, gap = 6, eyeW = 21, h = 25, textW = static_cast<float>(size.cx), w = pad + eyeW + gap + textW + pad;
	const float x = static_cast<float>(W - kStatusRightMargin) - w, y = static_cast<float>(kHudTopBand + kStatusTop + kStatusLineHalf) - h / 2;
	FillRound(buf, W, H, x, y, w, h, 4, panel, 0.88f);
	StrokeRound(buf, W, H, x, y, w, h, 4, 1.0f, frame, 1.0f);
	const float eyeH = eyeW * 0.5172f;
	FillShape(buf, W, H, EyeShape(), x + pad, y + (h - eyeH) / 2, eyeW, ink, 1.0f);
	DrawLabel(buf, W, H, text, x + pad + eyeW + gap, y, textW + 2, h, DT_LEFT, ink, 1.0f, font);
}
}
