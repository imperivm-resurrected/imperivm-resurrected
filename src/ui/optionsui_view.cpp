// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/optionsui_view.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/logcore.h"
#include "core/version.h"
#include "ui/fullscreen.h"
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

COLORREF RowText(const Row &r) { return r.enabled || r.kind == kRowToggleSlider ? pal::text : pal::textDim; }

constexpr int kContentX = 24, kContentRight = kScreenW - 18, kContentTop = 100;
constexpr int kFooterTop = 622, kFooterButtonsY = 632, kFooterButtonH = 36;

// The sizes of the cards and their rows: the window's, and the larger ones of the full screen.
struct Metrics {
	int rowH, cardHead, cardPad, cardGap, comboW, controlH, sliderW, valueW, segW, actionW;
	// Where the label goes in its row, alone or over its hint, and the hint under it.
	int labelY, labelH, hintLabelY, hintY, hintH;
	int badgeW, badgeH, titleY, titleH;
	bool screen;
};
constexpr Metrics kWindowMetrics = {40, 30, 14, 12, 180, 28, 150, 44, 128, 130, 8, 22, 2, 22, 16, 54, 16, 7, 18, false};
constexpr Metrics kScreenMetrics = {56, 46, 24, 24, 260, 38, 240, 64, 160, 170, 16, 24, 6, 30, 20, 76, 22, 16, 22, true};
// The pages' columns were drawn for this width: a page's leftW is the left one's part of it, and the window and the screen scale it to theirs.
constexpr int kColumnsW = 822;

void PaintRowLabel(Painter &p, const Metrics &m, const Row &r, int x, int y, int w)
{
	const bool hint = !r.hint.empty();
	p.Font(0);
	const int labelY = y + (hint ? m.hintLabelY : m.labelY);
	p.Text(x, labelY, w, m.labelH, r.label, RowText(r), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (r.restart) {
		const int used = std::min(w - m.badgeW - 8, p.TextWidth(r.label));
		PaintBadge(p, x + used + 8, labelY + (m.labelH - m.badgeH) / 2, m.badgeW, m.badgeH, "Restarts", pal::hairline, pal::redText);
	}
	if (hint) {
		p.Font(3);
		p.Text(x, y + m.hintY, w, m.hintH, r.hint, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
	case kRowInfo: {
		p.Font(0);
		const int textW = std::min(w / 2, p.TextWidth(r.text) + 4);
		int tx = right - textW;
		p.Text(tx, y + m.labelY, textW, m.labelH, r.text, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		if (!r.badge.empty()) {
			tx -= 8 + m.badgeW;
			PaintBadge(p, tx, y + m.labelY + (m.labelH - m.badgeH) / 2, m.badgeW, m.badgeH, r.badge, pal::hairline, pal::redText);
		}
		PaintRowLabel(p, m, r, x, y, tx - x - 8);
		break;
	}
	}
}

int PaintCard(Painter &p, const Metrics &m, const Card &c, const View &v, int x, int y, int w, RECT &dropAnchor, std::vector<std::string> &dropItems,
	int &dropSel)
{
	const int h = m.cardHead + static_cast<int>(c.rows.size()) * m.rowH + 4;
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
	for (size_t i = 0; i < c.rows.size(); i++, ry += m.rowH) {
		if (i) p.Fill(x + m.cardPad, ry, w - m.cardPad * 2, 1, pal::hairline);
		PaintRow(p, m, c.rows[i], v, x + m.cardPad, ry, w - m.cardPad * 2, dropAnchor, dropItems, dropSel);
	}
	return h;
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
		Row bars = Combo(v, "Health bars", kHealthBars, "When a match starts");
		bars.items = {"No bars", "All units", "My units", "Enemy units"};
		Row heroes = Toggle(v, "Heroes only", kHealthBarHeroes);
		heroes.enabled = bars.selected > 0;
		Row chat = ActionRow("Chat position", kResetChatPlace, "Reset", "Back above the command bar");
		chat.enabled = !(v.disabled.count(kResetChatPlace) && v.disabled.at(kResetChatPlace));
		page.left.push_back({"Interface", {Combo(v, "Language", kLanguage, "", true), bars, heroes,
			Toggle(v, "Ally and enemy colors", kAllyColors, "You blue, allies yellow, enemies red, only on your screen; a key in Controls switches it during the game"), chat}});
		const bool autosave = Check(v, kAutosave);
		page.right.push_back({"Autosave", {Toggle(v, "Autosave", kAutosave), Combo(v, "Every", kAutosaveEvery, "", false, autosave), Combo(v, "Copies", kAutosaveCopies, "", false, autosave),
			Toggle(v, "Save when leaving the game", kSaveOnExit)}});
		page.right.push_back({"Multiplayer", {Toggle(v, "Show badges in the chat", kChatBadges, "The rank or emblem after each name"),
			Toggle(v, "Sound when a match is found", kMatchFoundSound, "Ranked: also with the game in the background")}});
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
	const int leftW = (contentW - gap) * page.leftW / kColumnsW, rightX = x + leftW + gap, rightW = contentW - leftW - gap;
	RECT dropAnchor = {};
	std::vector<std::string> dropItems;
	int dropSel = -1;
	int y = l.top;
	for (const Card &c : page.left) y += PaintCard(p, m, c, v, x, y, leftW, dropAnchor, dropItems, dropSel) + m.cardGap;
	y = l.top;
	for (const Card &c : page.right) y += PaintCard(p, m, c, v, rightX, y, rightW, dropAnchor, dropItems, dropSel) + m.cardGap;

	const int buttonH = 48, by = l.h - l.margin - buttonH;
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
	const int leftW = (contentW - gap) * page.leftW / kColumnsW, rightX = kContentX + leftW + gap, rightW = contentW - leftW - gap;
	int y = kContentTop;
	for (const Card &c : page.left) y += PaintCard(p, m, c, v, kContentX, y, leftW, dropAnchor, dropItems, dropSel) + m.cardGap;
	y = kContentTop;
	for (const Card &c : page.right) y += PaintCard(p, m, c, v, rightX, y, rightW, dropAnchor, dropItems, dropSel) + m.cardGap;

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
}

}
