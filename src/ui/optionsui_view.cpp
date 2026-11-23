// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/optionsui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/logcore.h"
#include "core/version.h"
#include "ui/colorwheel.h"
#include "ui/fullscreen.h"
#include "ui/scrollpane.h"
#include "ui/widgets.h"

using namespace ui;

namespace optionsui {
namespace {

bool Check(const Values &v, int id)
{
	const auto it = v.checks.find(id);
	return it != v.checks.end() && it->second;
}

int Scroll(const Values &v, int id)
{
	const auto it = v.scrolls.find(id);
	return it == v.scrolls.end() ? 0 : it->second;
}

bool Disabled(const Values &v, int id)
{
	const auto it = v.disabled.find(id);
	return it != v.disabled.end() && it->second;
}

Row Toggle(const Values &v, const char *label, int id, const char *hint = "", bool restart = false)
{
	Row r;
	r.label = label;
	r.hint = hint;
	r.kind = kRowToggle;
	r.id = id;
	r.on = Check(v, id);
	r.restart = restart;
	r.enabled = !Disabled(v, id);
	return r;
}

Row Slider(const Values &v, const char *label, int id, int min, int max, const char *hint = "")
{
	Row r;
	r.label = label;
	r.hint = hint;
	r.kind = kRowSlider;
	r.id = id;
	r.value = Scroll(v, id);
	r.min = min;
	r.max = max;
	r.enabled = !Disabled(v, id);
	return r;
}

// The game turns its slider into a pace with value * 0x8fd / 100 + 700, where 1000 is the normal speed.
int GameSpeedPercent(int value)
{
	const int percent = (value * 0x8fd / 100 + 700) / 10;
	return (percent + 2) / 5 * 5;
}

Row ToggleSlider(const Values &v, const char *label, int toggleId, int id, int min, int max)
{
	Row r = Slider(v, label, id, min, max);
	r.kind = kRowToggleSlider;
	r.toggleId = toggleId;
	r.on = Check(v, toggleId);
	r.enabled = r.on && !Disabled(v, id);
	return r;
}

Row Combo(const Values &v, const char *label, int id, const char *hint = "", bool restart = false, bool enabled = true)
{
	Row r;
	r.label = label;
	r.hint = hint;
	r.kind = kRowCombo;
	r.id = id;
	const auto items = v.items.find(id);
	if (items != v.items.end()) r.items = items->second;
	const auto sel = v.combos.find(id);
	r.selected = sel == v.combos.end() ? -1 : sel->second;
	r.restart = restart;
	r.enabled = enabled && !Disabled(v, id);
	return r;
}

Row ActionRow(const char *label, int id, const char *button, const char *hint = "")
{
	Row r;
	r.label = label;
	r.hint = hint;
	r.kind = kRowAction;
	r.id = id;
	r.action = button;
	return r;
}

Row Info(const char *label, int id, const std::string &text, const std::string &badge = "")
{
	Row r;
	r.label = label;
	r.kind = kRowInfo;
	r.id = id;
	r.text = text;
	r.badge = badge;
	return r;
}

Row Colors(const Values &v, const char *label, const std::vector<std::pair<int, const char *>> &colors)
{
	Row r;
	r.label = label;
	r.kind = kRowColors;
	for (const auto &c : colors) {
		r.colorIds.push_back(c.first);
		const auto it = v.combos.find(c.first);
		r.colors.push_back(it != v.combos.end() ? static_cast<uint32_t>(it->second) : 0);
		r.items.push_back(c.second);
	}
	return r;
}

// Where the open picker hangs from: the colour it changes, as painted this frame.
RECT g_colorAnchor = {};

COLORREF RowText(const Row &r) { return r.enabled || r.kind == kRowToggleSlider ? pal::text : pal::textDim; }

constexpr int kContentX = 24, kContentRight = kScreenW - 18, kContentTop = 100;
constexpr int kFooterTop = 622, kFooterButtonsY = 632, kFooterButtonH = 36;

// The sizes of the cards and their rows: the window's, and the larger ones of the full screen. A page with more rows than its space
// holds shortens them down to minRowH, and scrolls what still doesn't fit.
struct Metrics {
	int rowH, minRowH, cardHead, cardPad, cardGap, comboW, controlH, sliderW, valueW, segW, actionW;
	// How tall the label is, and the label over its hint: the hint starts hintY below the label's top. Both are centred in the row.
	int labelH, hintY, hintH;
	int badgeW, badgeH, titleY, titleH;
	bool screen;
};
constexpr Metrics kWindowMetrics = {40, 36, 30, 14, 12, 180, 28, 150, 44, 128, 130, 22, 20, 16, 54, 16, 7, 18, false};
constexpr Metrics kScreenMetrics = {56, 46, 46, 24, 24, 260, 38, 240, 64, 160, 170, 24, 24, 20, 76, 22, 16, 22, true};
// The pages' columns were drawn for this width: a page's leftW is the left one's part of it, and the window and the screen scale it to theirs.
constexpr int kColumnsW = 822;

// A hint wraps to a second line rather than being cut on a narrow screen or in a long language.
constexpr int kHintLines = 2;

// The width the label and its hint get beside the row's control, as PaintRow lays them out.
int LabelWidth(const Metrics &m, const Row &r, int w)
{
	switch (r.kind) {
	case kRowToggle: return w - kBigToggleW - 12;
	case kRowSlider: return w - m.valueW - m.sliderW - kSliderMargin * 2;
	case kRowToggleSlider: return w - m.valueW - m.sliderW - kSliderMargin * 2 - kBigToggleW - 8;
	case kRowCombo: return w - m.comboW - 10;
	case kRowSegment: return w - m.segW * static_cast<int>(r.items.size()) - 8;
	case kRowAction: return w - m.actionW - 8;
	case kRowColors: {
		const int n = static_cast<int>(r.colors.size()), chipW = m.screen ? 130 : 96;
		return w - n * chipW - (n - 1) * 8 - 8;
	}
	default: return w / 2;
	}
}

// A row with a hint grows by the hint's lines beyond the first.
int RowHeight(Painter &p, const Metrics &m, const Row &r, int w)
{
	if (r.hint.empty()) return m.rowH;
	p.Font(3);
	const int lines = std::max(1, std::min(kHintLines, (p.Measure(std::max(40, LabelWidth(m, r, w)), r.hint) + m.hintH / 2) / m.hintH));
	return m.rowH + (lines - 1) * m.hintH;
}

void PaintRowLabel(Painter &p, const Metrics &m, const Row &r, int x, int y, int w)
{
	const bool hint = !r.hint.empty();
	p.Font(0);
	const int labelY = y + (m.rowH - (hint ? m.hintY + m.hintH : m.labelH)) / 2;
	p.Text(x, labelY, w, m.labelH, r.label, RowText(r), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (r.restart) {
		const int used = std::min(w - m.badgeW - 8, p.TextWidth(r.label));
		PaintBadge(p, x + used + 8, labelY + (m.labelH - m.badgeH) / 2, m.badgeW, m.badgeH, "Restarts", pal::hairline, pal::redText);
	}
	if (hint) {
		p.Font(3);
		// The label and the hint's first line are centred in the row; a second line takes what RowHeight added under it.
		p.TextWrapped(x, labelY + m.hintY, w, m.hintH * kHintLines, r.hint, pal::textDim);
	}
}

void PaintSliderControl(Painter &p, const Metrics &m, const Row &r, const View &v, int x, int y, int w)
{
	const int cy = y + m.rowH / 2;
	const float f = r.max > r.min ? static_cast<float>(r.value - r.min) / (r.max - r.min) : 0.0f;
	const bool hot = r.enabled && (v.dragId == r.id || p.Hover(x - kSliderMargin, y + 4, w + kSliderMargin * 2, m.rowH - 8, v.mouseX, v.mouseY));
	PaintSlider(p, x, cy - 10, w, 20, f, r.enabled, hot);
	p.Font(3);
	p.Text(x + w + 8, cy - 10, m.valueW - 8, 20, std::to_string(r.shown >= 0 ? r.shown : r.value) + " %", r.enabled ? pal::text : pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	if (r.enabled) p.AddHit(x - kSliderMargin, y + 4, w + kSliderMargin * 2, m.rowH - 8, kActSlider, r.id);
}

void PaintRow(Painter &p, const Metrics &m, const Row &r, const View &v, int x, int y, int w, RECT &dropAnchor, std::vector<std::string> &dropItems, int &dropSel)
{
	const int right = x + w, cy = y + m.rowH / 2, toggleY = cy - kBigToggleH / 2, controlY = cy - m.controlH / 2;
	switch (r.kind) {
	case kRowToggle: {
		const int tx = right - kBigToggleW;
		const bool hot = p.Hover(tx, toggleY, kBigToggleW, kBigToggleH, v.mouseX, v.mouseY);
		PaintRowLabel(p, m, r, x, y, w - kBigToggleW - 12);
		PaintBigToggle(p, tx, toggleY, r.on, r.enabled, hot);
		if (r.enabled) p.AddHit(tx, toggleY - 3, kBigToggleW, kBigToggleH + 6, kActToggle, r.id);
		break;
	}
	case kRowSlider: {
		const int sx = right - m.valueW - m.sliderW;
		PaintRowLabel(p, m, r, x, y, sx - x - kSliderMargin * 2);
		PaintSliderControl(p, m, r, v, sx, y, m.sliderW);
		break;
	}
	case kRowToggleSlider: {
		const int sx = right - m.valueW - m.sliderW;
		const int tx = sx - kSliderMargin * 2 - kBigToggleW;
		const bool hot = p.Hover(tx, toggleY, kBigToggleW, kBigToggleH, v.mouseX, v.mouseY);
		PaintRowLabel(p, m, r, x, y, tx - x - 8);
		PaintBigToggle(p, tx, toggleY, r.on, true, hot);
		p.AddHit(tx, toggleY - 3, kBigToggleW, kBigToggleH + 6, kActToggle, r.toggleId);
		PaintSliderControl(p, m, r, v, sx, y, m.sliderW);
		break;
	}
	case kRowCombo: {
		const int cx = right - m.comboW;
		PaintRowLabel(p, m, r, x, y, w - m.comboW - 10);
		const std::string text = r.selected >= 0 && r.selected < static_cast<int>(r.items.size()) ? r.items[r.selected] : std::string("—");
		PaintCombo(p, cx, controlY, m.comboW, m.controlH, text, v.dropId == r.id, r.enabled, v.mouseX, v.mouseY, kActCombo, r.id,
			v.dropId == r.id ? &dropAnchor : nullptr);
		if (v.dropId == r.id) {
			dropItems = r.items;
			dropSel = r.selected;
		}
		break;
	}
	case kRowSegment: {
		const int segW = m.segW, segH = m.controlH, sx = right - segW * static_cast<int>(r.items.size());
		PaintRowLabel(p, m, r, x, y, sx - x - 8);
		for (size_t i = 0; i < r.items.size(); i++) {
			const int bx = sx + static_cast<int>(i) * segW;
			const bool on = static_cast<int>(i) == r.selected;
			const bool hot = !on && p.Hover(bx, controlY, segW, segH, v.mouseX, v.mouseY);
			if (on) p.Fill(bx, controlY, segW, segH, pal::text);
			else p.Fill(bx, controlY, segW, segH, hot ? pal::button : pal::bg);
			p.Frame(bx, controlY, segW, segH, on ? pal::text : pal::edge);
			p.Font(0);
			p.Text(bx, controlY, segW, segH, r.items[i], on ? pal::onLight : pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (!on) p.AddHit(bx, controlY, segW, segH, kActSegment, r.id << 4 | static_cast<int>(i));
		}
		break;
	}
	case kRowAction: {
		const int bw = m.actionW, bx = right - bw;
		PaintRowLabel(p, m, r, x, y, bx - x - 8);
		p.Button(bx, controlY, bw, m.controlH, r.action, kActRow, r.id, v.mouseX, v.mouseY, r.enabled, false, 0);
		break;
	}
	case kRowColors: {
		const int n = static_cast<int>(r.colors.size()), chipW = m.screen ? 130 : 96, gap = 8, sw = m.controlH;
		const int cx0 = right - n * chipW - (n - 1) * gap;
		PaintRowLabel(p, m, r, x, y, cx0 - x - 8);
		for (int i = 0; i < n; i++) {
			const int cx = cx0 + i * (chipW + gap);
			const bool open = v.colorId == r.colorIds[i];
			const bool hot = r.enabled && p.Hover(cx, controlY, chipW, m.controlH, v.mouseX, v.mouseY);
			p.Fill(cx, controlY, chipW, m.controlH, open || hot ? pal::button : pal::bg);
			p.Frame(cx, controlY, chipW, m.controlH, open ? pal::text : pal::edge);
			p.Fill(cx + 4, controlY + 4, sw - 8, m.controlH - 8, r.enabled ? colorwheel::Ref(r.colors[i]) : pal::edge);
			p.Font(3);
			p.Text(cx + sw + 2, controlY, chipW - sw - 6, m.controlH, r.items[i], r.enabled ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (r.enabled) p.AddHit(cx, controlY, chipW, m.controlH, kActColor, r.colorIds[i]);
			if (open) g_colorAnchor = {cx, controlY, cx + chipW, controlY + m.controlH};
		}
		break;
	}
	case kRowInfo: {
		p.Font(0);
		const int textW = std::min(w / 2, p.TextWidth(r.text) + 4), labelY = y + (m.rowH - m.labelH) / 2;
		int tx = right - textW;
		p.Text(tx, labelY, textW, m.labelH, r.text, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		if (!r.badge.empty()) {
			tx -= 8 + m.badgeW;
			PaintBadge(p, tx, labelY + (m.labelH - m.badgeH) / 2, m.badgeW, m.badgeH, r.badge, pal::hairline, pal::redText);
		}
		PaintRowLabel(p, m, r, x, y, tx - x - 8);
		break;
	}
	}
}

int PaintCard(Painter &p, const Metrics &m, const Card &c, const View &v, int x, int y, int w, RECT &dropAnchor, std::vector<std::string> &dropItems,
	int &dropSel)
{
	std::vector<int> heights;
	int h = m.cardHead + 4;
	for (const Row &r : c.rows) {
		heights.push_back(RowHeight(p, m, r, w - m.cardPad * 2));
		h += heights.back();
	}
	p.Font(4);
	if (m.screen) {
		PaintGlassCard(p, x, y, w, h);
		p.TextCaps(x + m.cardPad, y + m.titleY, w - m.cardPad * 2, m.titleH, c.title, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	}
	else {
		PaintCard(p, x, y, w, h);
		p.Text(x + m.cardPad, y + m.titleY, w - m.cardPad * 2, m.titleH, c.title, pal::redText);
	}
	int ry = y + m.cardHead;
	for (size_t i = 0; i < c.rows.size(); ry += heights[i], i++) {
		if (i) p.Fill(x + m.cardPad, ry, w - m.cardPad * 2, 1, pal::hairline);
		PaintRow(p, m, c.rows[i], v, x + m.cardPad, ry, w - m.cardPad * 2, dropAnchor, dropItems, dropSel);
	}
	return h;
}

// How tall a column of cards `w` wide is with rows `rowH` tall, and the second lines of the hints that wrap (RowHeight).
int ColumnHeight(Painter &p, const Metrics &m, const std::vector<Card> &cards, int rowH, int w)
{
	int h = 0;
	for (size_t i = 0; i < cards.size(); i++) {
		h += (i ? m.cardGap : 0) + m.cardHead + static_cast<int>(cards[i].rows.size()) * rowH + 4;
		for (const Row &r : cards[i].rows) h += RowHeight(p, m, r, w - m.cardPad * 2) - m.rowH;
	}
	return h;
}

int ColumnRows(const std::vector<Card> &cards)
{
	int rows = 0;
	for (const Card &c : cards) rows += static_cast<int>(c.rows.size());
	return rows;
}

// The rows as tall as the space lets the taller column be, between the metrics' two heights.
Metrics FitMetrics(Painter &p, const Metrics &base, const Page &page, int avail, int leftW, int rightW)
{
	Metrics m = base;
	for (const auto &column : {std::make_pair(&page.left, leftW), std::make_pair(&page.right, rightW)}) {
		const int rows = ColumnRows(*column.first);
		m.rowH = std::min(m.rowH, FitRowHeight(avail, ColumnHeight(p, base, *column.first, 0, column.second), rows, base.rowH, base.minRowH));
	}
	return m;
}

// Both columns of a page in one pane from `top`, `avail` tall, which scrolls them together when they don't fit; its bar goes at barX.
void PaintColumns(Painter &p, const Metrics &base, const Page &page, const View &v, int x, int top, int contentW, int gap, int avail, int barX,
	RECT &dropAnchor, std::vector<std::string> &dropItems, int &dropSel)
{
	const int leftW = (contentW - gap) * page.leftW / kColumnsW, rightX = x + leftW + gap, rightW = contentW - leftW - gap;
	const Metrics m = FitMetrics(p, base, page, avail, leftW, rightW);
	PaneSpec spec;
	spec.id = kPaneColumns;
	spec.x = x;
	spec.y = top;
	spec.w = barX + spec.look.width - x;
	spec.h = avail;
	spec.contentH = std::max(ColumnHeight(p, m, page.left, m.rowH, leftW), ColumnHeight(p, m, page.right, m.rowH, rightW));
	spec.barX = barX;
	spec.drag = &v.dropBar;
	spec.mx = v.mouseX;
	spec.my = v.mouseY;
	ScrollPane pane(p, spec, v.scroll.Of(kPaneColumns));
	int y = pane.Top();
	for (const Card &c : page.left) y += PaintCard(p, m, c, v, x, y, leftW, dropAnchor, dropItems, dropSel) + m.cardGap;
	y = pane.Top();
	for (const Card &c : page.right) y += PaintCard(p, m, c, v, rightX, y, rightW, dropAnchor, dropItems, dropSel) + m.cardGap;
	// A combo half scrolled out opens its menu from the part in view, not over the header or the footer.
	if (dropAnchor.bottom > dropAnchor.top) {
		dropAnchor.top = std::max<LONG>(dropAnchor.top, top);
		dropAnchor.bottom = std::max(dropAnchor.top, std::min<LONG>(dropAnchor.bottom, top + avail));
	}
	// The same for the colour whose picker is open.
	if (v.colorId && g_colorAnchor.bottom > g_colorAnchor.top) {
		g_colorAnchor.top = std::max<LONG>(g_colorAnchor.top, top);
		g_colorAnchor.bottom = std::max(g_colorAnchor.top, std::min<LONG>(g_colorAnchor.bottom, top + avail));
	}
}

}

std::vector<Page> BuildPages(const Values &v)
{
	std::vector<Page> pages;
	{
		Page page;
		page.title = "Display";
		page.blurb = "How the game looks on your monitor.";
		Row mode;
		mode.label = "Mode";
		mode.hint = "Alt+Enter switches";
		mode.kind = kRowSegment;
		mode.id = kWindowed;
		mode.items = {"Full screen", "Window"};
		mode.selected = Check(v, kWindowed) ? 1 : 0;
		const bool windowed = Check(v, kWindowed);
		page.left.push_back({"Size and mode", {Combo(v, "Monitor", kMonitor), mode, Combo(v, "Resolution", kResolution, windowed ? "Also the window size" : ""),
			Toggle(v, "Trap the mouse", kTrapMouse, "The cursor stays inside the game window")}});
		const bool vsync = Check(v, kVSync);
		page.right.push_back({"Image", {Slider(v, "Brightness", kBrightness, 80, 130), Toggle(v, "Vertical sync", kVSync, "Avoids tearing in the image"),
			Combo(v, "FPS limit", kFpsLimit, vsync ? "Off with vertical sync" : "", false, !vsync)}});
		Row compact = Toggle(v, "Compact panel", kPanelCompact), opacity = Slider(v, "Panel opacity", kPanelOpacity, 30, 100);
		compact.enabled = opacity.enabled = Check(v, kShowPerf) || Check(v, kShowNet);
		page.right.push_back({"Performance panel", {Toggle(v, "Show FPS and memory", kShowPerf), Toggle(v, "Network statistics", kShowNet), compact, opacity}});
		pages.push_back(page);
	}
	{
		Page page;
		page.title = "Sound";
		page.blurb = "Turn each kind of sound on or off and set its volume.";
		page.leftW = 500;
		// Below 1 nature sounds follow the effects volume.
		Row nature = ToggleSlider(v, "Nature sounds", kNatureOn, kNatureVolume, 1, 100);
		if (nature.value < 1) nature.value = std::max(1, Scroll(v, kEffectsVolume));
		page.left.push_back({"Volume", {Slider(v, "General volume", kMasterVolume, 0, 100), ToggleSlider(v, "Music", kMusicOn, kMusicVolume, 1, 100),
			ToggleSlider(v, "Sound effects", kEffectsOn, kEffectsVolume, 1, 100), nature, ToggleSlider(v, "Unit speech", kSpeechOn, kSpeechVolume, 1, 100),
			ToggleSlider(v, "Conversations", kConversationsOn, kConversationVolume, 1, 100), ToggleSlider(v, "Cutscenes", kMovieOn, kMovieVolume, 0, 100)}});
		page.right.push_back({"Output", {Combo(v, "Device", kOutputDevice)}});
		page.right.push_back({"Behaviour", {Toggle(v, "Mute in the background", kMuteBackground),
			Toggle(v, "Chat and invitation alerts", kAlerts)}});
		pages.push_back(page);
	}
	{
		Page page;
		page.title = "Game";
		page.blurb = "How you play and how your games are saved.";
		Row speed = Slider(v, "Game speed", kGameSpeed, 1, 100);
		speed.shown = GameSpeedPercent(speed.value);
		page.left.push_back({"Pace", {speed, Slider(v, "Scroll speed", kScrollSpeed, 1, 100), Toggle(v, "Smooth camera", kSmoothCamera),
			Toggle(v, "Pause in the background", kPauseInBackground, "Single player, when you switch to another window")}});
		Row bars = Combo(v, "Health bars", kHealthBars, "Their key shows and hides them; Ctrl and the key change whose");
		bars.items = {"No bars", "All units", "My units", "Enemy units"};
		Row heroes = Toggle(v, "Heroes only", kHealthBarHeroes, "Also when their key shows them");
		Row chat = ActionRow("Chat position", kResetChatPlace, "Reset", "Back above the command bar");
		chat.enabled = !(v.disabled.count(kResetChatPlace) && v.disabled.at(kResetChatPlace));
		Row palette = Combo(v, "Palette", kAllyPalette);
		palette.items = {"Classic", "Colour blindness", "Custom"};
		palette.enabled = Check(v, kAllyColors);
		Row colors = Colors(v, "Colors", {{kAllySelf, "You"}, {kAllyAllies, "Allies"}, {kAllyEnemies, "Enemies"}});
		colors.enabled = Check(v, kAllyColors);
		page.left.push_back({"Interface", {Combo(v, "Language", kLanguage, "", true), bars, heroes,
			Toggle(v, "Ally and enemy colors", kAllyColors, "Each side in its own colour, only on your screen"), palette,
			colors, chat}});
		const bool autosave = Check(v, kAutosave);
		page.right.push_back({"Autosave", {Toggle(v, "Autosave", kAutosave), Combo(v, "Every", kAutosaveEvery, "", false, autosave), Combo(v, "Copies", kAutosaveCopies, "", false, autosave),
			Toggle(v, "Save when leaving the game", kSaveOnExit)}});
		page.right.push_back({"Multiplayer", {Toggle(v, "Show badges in the chat", kChatBadges, "The rank or emblem after each name"),
			Toggle(v, "Sound when a match is found", kMatchFoundSound, "Ranked and cups: a horn, also with the game in the background"),
			Toggle(v, "Bring the game to the front", kAlertFront, "When a match is found or a room starts its countdown")}});
		page.right.push_back({"Big map", {Toggle(v, "Respawn timers", kRespawnTimers, "When the mushrooms, water and ruins you saw have their item again")}});
		pages.push_back(page);
	}
	{
		Page page;
		page.title = "System";
		page.blurb = "Updates of the mod, and what to do when something fails.";
		page.left.push_back({"About", {Info("Mod version", kModVersion, ModVersionText(), IsBetaVersion(ModVersionText()) ? "Beta" : "")}});
		Row check = ActionRow("New version", kCheckUpdates, "Check now");
		const auto status = v.texts.find(kCheckUpdates);
		if (status != v.texts.end()) check.hint = status->second;
		check.enabled = !Disabled(v, kCheckUpdates);
		page.left.push_back({"Updates", {Combo(v, "Updates", kUpdates), check}});
		page.right.push_back({"If something fails", {Combo(v, "Log detail", kLogLevel, "Detailed helps to find a problem"), ActionRow("Logs folder", kOpenLogs, "Open folder"),
			ActionRow("Problem report", kCreateReport, "Create", "A zip with your private data hidden")}});
		pages.push_back(page);
	}
	return pages;
}

namespace {
void PaintColorPicker(Painter &p, const View &v, int canvasW, int canvasH)
{
	if (!v.colorId) return;
	const auto it = v.values.combos.find(v.colorId);
	const uint32_t color = it != v.values.combos.end() ? static_cast<uint32_t>(it->second) : 0;
	const int pad = 14, wheel = colorwheel::kWheel, w = pad * 2 + colorwheel::kWidth, h = pad * 2 + wheel + 44;
	int x = g_colorAnchor.right - w, y = g_colorAnchor.bottom + 6;
	if (x < 4) x = 4;
	if (y + h > canvasH - 4) y = g_colorAnchor.top - 6 - h;
	if (x + w > canvasW - 4) x = canvasW - 4 - w;
	// A click anywhere else closes it.
	p.AddHit(0, 0, canvasW, canvasH, kActColorClose, 0);
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::lineStrong);
	p.AddHit(x, y, w, h, kActNone, 0);
	const int wx = x + pad, wy = y + pad;
	colorwheel::Paint(p, wx, wy, color, pal::raised, kActColorWheel, kActColorValue, v.colorId);
	p.Fill(wx, wy + wheel + 12, colorwheel::kWidth, 30, colorwheel::Ref(color));
	p.Frame(wx, wy + wheel + 12, colorwheel::kWidth, 30, pal::edge);
}

void PaintDrop(Painter &p, const View &v, const RECT &dropAnchor, const std::vector<std::string> &dropItems, int dropSel, int comboW, int canvasW, int canvasH)
{
	if (v.dropId && !dropItems.empty()) {
		PaintDropMenu(p, dropAnchor, ToDropItems(dropItems), dropSel, v.dropScroll, canvasW, canvasH, kActPick, kActDropClose, v.mouseX, v.mouseY, comboW,
			v.dropBar);
	}
}

std::vector<TabSpec> PageTabs(const std::vector<Page> &pages, int current)
{
	std::vector<TabSpec> tabs;
	for (size_t i = 0; i < pages.size(); i++) tabs.push_back({pages[i].title, kActPage, static_cast<int>(i), static_cast<int>(i) == current, true});
	tabs.push_back({"Controls", kActControls, 0, false, true});
	return tabs;
}

// Opened from the main menu: the pages are tabs beside the title, and the cards fill the screen over the backdrop.
void PaintFullScreen(Painter &p, const View &v, const std::vector<Page> &pages, int current)
{
	const Metrics &m = kScreenMetrics;
	const int mx = v.mouseX, my = v.mouseY;
	UseScreenFonts(p);
	const ScreenLayout l = ScreenLayoutFor(v.screenW, v.screenH);
	const int right = PaintHeaderActions(p, l, {{"Back", kActCancel, true, true}}, mx, my);
	PaintScreenTitle(p, l, "Options", PageTabs(pages, current), right, mx, my);
	const Page &page = pages[current];
	PaintScreenInfo(p, l, page.blurb);

	// A row as wide as the screen would put each control far from its label.
	const int x = l.margin, contentW = std::min(1440, l.w - 2 * l.margin), gap = 32;
	RECT dropAnchor = {};
	std::vector<std::string> dropItems;
	int dropSel = -1;
	const int buttonH = 48, by = l.h - l.margin - buttonH;
	PaintColumns(p, m, page, v, x, l.top, contentW, gap, by - 24 - l.top, x + contentW + 12, dropAnchor, dropItems, dropSel);

	p.Button(x, by, 170, buttonH, "Reset", kActReset, 0, mx, my);
	int bx = l.w - l.margin - 200;
	p.Button(bx, by, 200, buttonH, "OK", kActOk, 0, mx, my, true, true);
	bx -= 12 + 170;
	p.Button(bx, by, 170, buttonH, "Apply", kActApply, 0, mx, my, v.dirty);
	if (v.dirty) {
		p.Diamond(bx - 220, by + buttonH / 2, 5, pal::redText);
		p.Font(3);
		p.Text(bx - 208, by, 196, buttonH, "Changes not applied", pal::redText);
	}

	UseSharedFonts(p);
	PaintDrop(p, v, dropAnchor, dropItems, dropSel, m.comboW, v.screenW, v.screenH);
	PaintColorPicker(p, v, v.screenW, v.screenH);
}
}

void PaintOptionsScreen(Painter &p, const View &v)
{
	const std::vector<Page> pages = BuildPages(v.values);
	const int current = std::max(0, std::min(v.page, static_cast<int>(pages.size()) - 1));
	if (v.fullScreen) {
		PaintFullScreen(p, v, pages, current);
		return;
	}
	const Metrics &m = kWindowMetrics;
	PaintWindowCard(p, kScreenW, kScreenH);
	const Page &page = pages[current];
	PaintWindowHeading(p, kScreenW, "Options", page.blurb, PageTabs(pages, current), 0, v.mouseX, v.mouseY);

	RECT dropAnchor = {};
	std::vector<std::string> dropItems;
	int dropSel = -1;
	const int contentW = kContentRight - kContentX, gap = m.cardGap + 8;
	PaintColumns(p, m, page, v, kContentX, kContentTop, contentW, gap, kFooterTop - 10 - kContentTop, kContentRight + 5, dropAnchor, dropItems, dropSel);

	p.Fill(18, kFooterTop, kScreenW - 36, 1, pal::lineStrong);
	p.Button(24, kFooterButtonsY, 130, kFooterButtonH, "Reset", kActReset, 0, v.mouseX, v.mouseY);
	int bx = kScreenW - 24 - 136;
	p.Button(bx, kFooterButtonsY, 136, kFooterButtonH, "OK", kActOk, 0, v.mouseX, v.mouseY, true, true);
	bx -= 10 + 112;
	p.Button(bx, kFooterButtonsY, 112, kFooterButtonH, "Apply", kActApply, 0, v.mouseX, v.mouseY, v.dirty);
	bx -= 10 + 112;
	p.Button(bx, kFooterButtonsY, 112, kFooterButtonH, "Cancel", kActCancel, 0, v.mouseX, v.mouseY);
	if (v.dirty) {
		p.Diamond(bx - 176, kFooterButtonsY + kFooterButtonH / 2, 4, pal::redText);
		p.Font(3);
		p.Text(bx - 168, kFooterButtonsY, 160, kFooterButtonH, "Changes not applied", pal::redText);
	}

	PaintDrop(p, v, dropAnchor, dropItems, dropSel, m.comboW, kScreenW, kScreenH);
	PaintColorPicker(p, v, kScreenW, kScreenH);
}


uint32_t PickedColor(int part, const RECT &hit, int mx, int my, uint32_t current)
{
	return colorwheel::Pick(part == kActColorValue, hit, mx, my, current);
}

}
