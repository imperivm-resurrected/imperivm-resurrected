// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/diplomacyui_view.h"

#include <algorithm>
#include <string>
#include <vector>

#include "game/diplomacyrules.h"
#include "ui/fullscreen.h"
#include "ui/widgets.h"

using namespace ui;
using namespace diplomacy;

namespace diplomacyui {

namespace {

constexpr int kPad = 24, kColGap = 16, kRowGap = 14, kCardH = 262, kVisibleRows = 2, kCardsTop = 96, kNoticeH = 30, kGutter = 18;
constexpr int kFooterH = 48, kFooterGap = 16, kBottomPad = 24;
constexpr int kInnerPad = 14, kStatusY = 40, kStatusH = 34, kSegY = 84, kSegH = 30, kTableY = 126, kTableRowH = 26, kColW = 92;

struct Permission {
	unsigned bit;
	const char *label;
	const char *tip;
};

const Permission kPermissions[4] = {
	{kCeasefire, "Ceasefire", "Your units and towers do not attack theirs, and your outposts can send them supplies. It does not stop them from attacking you."},
	{kVision, "Shared vision", "They see what your units see and the map you explored, and their troops can enter your settlements."},
	{kSupport, "Food from villages", "Your villages feed their troops nearby and can send food to their settlements."},
	{kControl, "Command of troops", "They can select your units and buildings and give them orders. Everything stays yours."},
};

const char *const kPresetLabels[4] = {"War", "Peace", "Alliance", "Custom"};

COLORREF ColorOf(uint32_t rgb) { return RGB(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255); }

int Rows(const View &v) { return (static_cast<int>(v.cards.size()) + 1) / 2; }

int CardsTop(const View &v) { return kCardsTop + (v.locked ? kNoticeH : 0); }

int CardsBottom(const View &v, bool fullHeight = false)
{
	const int rows = fullHeight ? kVisibleRows : std::max(1, std::min(kVisibleRows, Rows(v)));
	return CardsTop(v) + rows * kCardH + (rows - 1) * kRowGap;
}

std::string StatusText(const Card &c, Status s)
{
	switch (s) {
	case kStatusWar: return Tr("You are at war.");
	case kStatusPeace: return Tr("You are at peace.");
	case kStatusAllied: return TrFmt("%s is your ally.", {c.name});
	case kStatusYouOffered: return Tr(c.ai ? "You offered peace. The AI does not answer offers." : "You offered peace. No answer yet.");
	case kStatusTheyOfferPeace: return TrFmt("%s offers you peace.", {c.name});
	case kStatusTheyOfferAlliance: return TrFmt("%s offers you an alliance.", {c.name});
	case kStatusTheyGiveMore: return TrFmt("%s gives you more than you give back.", {c.name});
	}
	return "";
}

COLORREF StatusColor(Status s)
{
	switch (s) {
	case kStatusWar: return pal::bad;
	case kStatusAllied: return pal::ok;
	case kStatusYouOffered: return pal::redText;
	case kStatusPeace: return pal::text;
	default: return pal::text;
	}
}

void Raw(Painter &p, bool on) { p.raw = on; }

void PaintCheck(Painter &p, int x, int y, int size, bool on, bool enabled, bool hot)
{
	p.Fill(x, y, size, size, on && enabled ? pal::red : pal::field);
	p.Frame(x, y, size, size, !enabled ? pal::hairline : on ? (hot ? pal::redText : pal::red) : hot ? pal::edgeHot : pal::edge);
	if (on) p.Fill(x + 4, y + 4, size - 8, size - 8, enabled ? pal::onRed : pal::disabled);
}

void PaintDot(Painter &p, int cx, int cy, bool on)
{
	if (on) {
		p.Ellipse(cx - 6, cy - 6, 12, 12, pal::red);
	}
	else {
		p.Ellipse(cx - 6, cy - 6, 12, 12, pal::hairline);
		p.Ellipse(cx - 4, cy - 4, 8, 8, pal::field);
	}
}

void PaintSegments(Painter &p, const View &v, int index, int x, int y, int w, Preset current)
{
	const int segW = w / 4;
	for (int i = 0; i < 4; i++) {
		const int sx = x + i * segW, sw = i == 3 ? w - segW * 3 : segW;
		const bool active = i == current, custom = i == kPresetCustom;
		const bool clickable = !v.locked && !custom && !active;
		const bool hot = clickable && p.Hover(sx, y, sw, kSegH, v.mouseX, v.mouseY);
		if (active) p.Fill(sx, y, sw, kSegH, v.locked ? pal::lineStrong : pal::red);
		else p.Fill(sx, y, sw, kSegH, hot ? pal::buttonHover : pal::field);
		if (i > 0) p.Fill(sx, y + 1, 1, kSegH - 2, pal::hairline);
		p.Font(active ? 1 : 0);
		const COLORREF c = active ? (v.locked ? pal::text : pal::onRed) : custom || v.locked ? pal::disabled : pal::textDim;
		p.Text(sx, y, sw, kSegH, kPresetLabels[i], c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (clickable) p.AddHit(sx, y, sw, kSegH, kActPreset, index * kArgStride + i);
	}
	p.Frame(x, y, w, kSegH, v.locked ? pal::hairline : pal::edge);
}

// The hovered permission's explanation, drawn last so that it lies over the cards.
struct Tip {
	int x = 0, y = 0;
	const char *text = nullptr;
};

void PaintPlayerCard(Painter &p, const View &v, int index, int x, int y, int w, Tip &tip)
{
	const Card &c = v.cards[index];
	const Status status = StatusOf(c.mine, c.theirs);
	PaintGlassCard(p, x, y, w, kCardH);
	p.Fill(x, y, 6, kCardH, ColorOf(c.rgb));
	p.Fill(x + 6, y, 1, kCardH, pal::shadow);

	const int ix = x + kInnerPad + 6, iw = w - kInnerPad * 2 - 6;
	std::string tag = Tr(c.ai ? "AI" : "Human");
	if (c.team > 0) tag += "  ·  " + TrFmt("Team %d", {std::to_string(c.team)});
	p.Font(3);
	Raw(p, true);
	int right = ix + iw;
	// A human's messages and marks can be hidden on this computer: two small switches, crossed out while muted.
	if (!c.ai) {
		const struct {
			const char *label;
			bool muted;
			int action;
		} mutes[2] = {{"Marks", c.mutedMarks, kActMuteMarks}, {"Chat", c.mutedChat, kActMuteChat}};
		for (const auto &m : mutes) {
			const std::string label = Tr(m.label);
			const int bw = p.TextWidth(label) + 18, bx = right - bw, by = y + 12;
			const bool hot = p.Hover(bx, by, bw, 24, v.mouseX, v.mouseY);
			p.Fill(bx, by, bw, 24, m.muted ? pal::selected : hot ? pal::button : pal::bg);
			p.Frame(bx, by, bw, 24, m.muted ? pal::redText : hot ? pal::edgeHot : pal::edge);
			p.Text(bx, by, bw, 24, label, m.muted ? pal::redText : pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (m.muted) p.Line(bx + 6, by + 12, bx + bw - 6, by + 12, pal::redText, 1);
			p.AddHit(bx, by, bw, 24, m.action, index);
			if (hot) {
				tip.text = m.muted ? (m.action == kActMuteChat ? "Show this player's messages again" : "Show this player's marks again")
								   : (m.action == kActMuteChat ? "Hide this player's messages, only on your screen" : "Hide this player's marks on the map, only on your screen");
				tip.x = bx;
				tip.y = by + 28;
			}
			right = bx - 6;
		}
	}
	const int tagW = std::min((right - ix) / 2, p.TextWidth(tag) + 4);
	p.Text(right - tagW, y + 12, tagW, 24, tag, pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.Font(1);
	p.Text(ix, y + 12, right - tagW - 10 - ix, 24, c.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	const std::string text = StatusText(c, status);
	if (IsOffer(status)) {
		const int by = y + kStatusY, bw = iw, accW = v.locked ? 0 : 96;
		p.Fill(ix, by, bw, kStatusH, pal::selected);
		p.Frame(ix, by, bw, kStatusH, pal::redText);
		p.Diamond(ix + 14, by + kStatusH / 2, 5, pal::redText);
		p.Font(1);
		p.Text(ix + 28, by, bw - 40 - accW, kStatusH, text, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		Raw(p, false);
		if (!v.locked) p.Button(ix + bw - accW - 4, by + 4, accW, kStatusH - 8, "Accept", kActAccept, index, v.mouseX, v.mouseY, true, true);
	}
	else {
		p.Diamond(ix + 6, y + kStatusY + kStatusH / 2, 5, StatusColor(status));
		p.Font(0);
		p.Text(ix + 20, y + kStatusY, iw - 20, kStatusH, text, StatusColor(status), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		Raw(p, false);
	}

	PaintSegments(p, v, index, ix, y + kSegY, iw, PresetOf(c.mine));

	const int youX = ix + iw - kColW * 2, theyX = ix + iw - kColW;
	p.Font(3);
	p.Text(ix, y + kTableY, iw - kColW * 2, 20, "Permission", pal::textDim);
	p.Text(youX, y + kTableY, kColW, 20, "You give", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Text(theyX, y + kTableY, kColW, 20, "They give", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Fill(ix, y + kTableY + 21, iw, 1, pal::hairline);
	for (int i = 0; i < 4; i++) {
		const Permission &perm = kPermissions[i];
		const int ry = y + kTableY + 24 + i * kTableRowH;
		const bool rowHot = p.Hover(ix, ry, iw, kTableRowH, v.mouseX, v.mouseY);
		if (rowHot) {
			p.Fill(ix, ry, iw, kTableRowH, pal::raised);
			tip = {ix + 8, ry + kTableRowH + 4, perm.tip};
		}
		p.Font(0);
		p.Text(ix + 4, ry, iw - kColW * 2 - 4, kTableRowH, perm.label, pal::text);
		const bool on = (c.mine & perm.bit) != 0, enabled = !v.locked;
		const int size = 18, cx = youX + (kColW - size) / 2, cy = ry + (kTableRowH - size) / 2;
		PaintCheck(p, cx, cy, size, on, enabled, enabled && rowHot && p.Hover(youX, ry, kColW, kTableRowH, v.mouseX, v.mouseY));
		if (enabled) p.AddHit(youX, ry, kColW, kTableRowH, kActToggle, index * kArgStride + i);
		PaintDot(p, theyX + kColW / 2, ry + kTableRowH / 2, (c.theirs & perm.bit) != 0);
	}
}

void PaintTip(Painter &p, const Tip &tip, int panelH)
{
	if (!tip.text) return;
	constexpr int w = 330;
	p.Font(3);
	const int h = p.Measure(w - 20, tip.text) + 14;
	int y = tip.y;
	if (y + h > panelH - 8) y = std::max(8, tip.y - kTableRowH - 8 - h);
	const int x = std::min(tip.x, kPanelW - 8 - w);
	p.Fill(x + 3, y + 3, w, h, pal::shadow);
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, pal::edge);
	p.TextWrapped(x + 10, y + 7, w - 20, h - 10, tip.text, pal::text);
}

void PaintCardsBar(Painter &p, const View &v, int x, int top, int h)
{
	ScrollbarLook look;
	look.minThumb = 30;
	look.grip = 6;
	look.track = pal::bg;
	PaintScrollbar(p, kBarCards, x, top, h, {v.scroll, MaxScroll(v), kVisibleRows}, v.bar, v.mouseX, v.mouseY, look);
}

}

int MaxScroll(const View &v) { return std::max(0, Rows(v) - kVisibleRows); }

int PanelHeight(const View &v, bool fullHeight) { return CardsBottom(v, fullHeight) + kFooterGap + 1 + kFooterGap + kFooterH + kBottomPad; }

void PaintDiplomacy(Painter &p, const View &v)
{
	const int h = PanelHeight(v);
	PaintWindowCard(p, kPanelW, h);
	PaintWindowHeading(p, kPanelW, "Diplomacy", "Choose what you give each player; what they give you is their choice.");
	if (v.locked) {
		p.Font(4);
		p.Text(kPad, kCardsTop - 4, kPanelW - kPad * 2, kNoticeH - 6, "Diplomacy is locked in this game: you can look but not change it.", pal::redText,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	const int count = static_cast<int>(v.cards.size()), scroll = std::max(0, std::min(v.scroll, MaxScroll(v)));
	const bool scrolling = MaxScroll(v) > 0;
	const int top = CardsTop(v), bottom = CardsBottom(v);
	const int areaW = kPanelW - kPad * 2 - (scrolling ? kGutter : 0), cardW = (areaW - kColGap) / 2;
	Tip tip;
	p.Clip(0, top, kPanelW, bottom - top);
	for (int i = scroll * 2; i < count && i < (scroll + kVisibleRows) * 2; i++) {
		const int row = i / 2 - scroll, col = i % 2;
		int x = kPad + col * (cardW + kColGap);
		if (count == 1) x = (kPanelW - cardW) / 2;
		PaintPlayerCard(p, v, i, x, top + row * (kCardH + kRowGap), cardW, tip);
	}
	p.Unclip();
	if (scrolling) PaintCardsBar(p, v, kPanelW - kPad - 6, top, bottom - top);

	const int ruleY = bottom + kFooterGap;
	PaintRule(p, ruleY, kPanelW);
	const int fy = ruleY + 1 + kFooterGap;
	const bool hot = !v.locked && p.Hover(kPad, fy, 420, kFooterH, v.mouseX, v.mouseY);
	PaintCheck(p, kPad, fy + 4, 20, v.alliedVictory, !v.locked, hot);
	p.Font(1);
	p.Text(kPad + 32, fy, 420, 26, "Allied victory", v.locked ? pal::textDim : pal::text);
	p.Font(3);
	p.Text(kPad + 32, fy + 24, 470, 22, "Win together with your allies. Both of you need it on.", pal::textDim);
	if (!v.locked) p.AddHit(kPad, fy, 420, kFooterH, kActAllied);

	constexpr int btnW = 150, btnH = 42, btnGap = 12;
	const int bx = kPanelW - kPad - btnW;
	p.Button(bx - btnW - btnGap, fy + (kFooterH - btnH) / 2, btnW, btnH, v.locked ? "Close" : "Cancel", kActCancel, 0, v.mouseX, v.mouseY);
	p.Button(bx, fy + (kFooterH - btnH) / 2, btnW, btnH, "Apply", kActApply, 0, v.mouseX, v.mouseY, !v.locked, true);
	PaintTip(p, tip, h);
}

}
