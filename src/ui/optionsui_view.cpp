// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/optionsui_view.h"

#include <algorithm>

#include "core/logcore.h"
#include "core/version.h"
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

COLORREF RowText(const Row &r) { return r.enabled || r.kind == kRowToggleSlider ? kText : kDim; }

constexpr int kNavX = 12, kNavW = 208, kNavH = 56, kNavTop = 92, kNavRight = kNavX + kNavW, kNavIcon = 30;
constexpr int kContentX = 240, kContentRight = kScreenW - 18, kContentTop = 132;
constexpr int kFooterTop = 622, kFooterButtonsY = 632, kFooterButtonH = 36;
constexpr int kRowH = 40, kCardHead = 30, kCardGap = 12, kCardPad = 14, kComboW = 180, kSliderW = 150, kValueW = 44;

void PaintNavItem(Painter &p, int y, const std::string &title, const std::string &subtitle, Ornament icon, bool active, int action, int arg, int mx, int my)
{
	const bool hot = !active && p.Hover(kNavX, y, kNavW, kNavH, mx, my);
	if (active) {
		p.FillH(kNavX, y, kNavW, kNavH, RGB(96, 34, 24), pal::panel);
		p.FillV(kNavX, y, 4, kNavH, pal::goldLight, pal::bronze);
	}
	else if (hot) {
		p.FillH(kNavX, y, kNavW, kNavH, RGB(50, 38, 22), pal::panel);
	}
	const COLORREF iconColor = active ? pal::goldLight : hot ? kGold : pal::bronze;
	PaintOrnament(p, icon, kNavX + 15, y + (kNavH - kNavIcon) / 2, kNavIcon, kNavIcon, iconColor);
	p.Font(1);
	p.Text(kNavX + 56, y + 9, kNavW - 62, 20, title, active ? pal::goldLight : hot ? kText : RGB(214, 200, 164));
	p.Font(3);
	p.Text(kNavX + 56, y + 29, kNavW - 62, 16, subtitle, active ? RGB(190, 172, 132) : kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.AddHit(kNavX, y, kNavW, kNavH, action, arg);
}

void PaintRowLabel(Painter &p, const Row &r, int x, int y, int w)
{
	const bool hint = !r.hint.empty();
	p.Font(0);
	const int labelY = hint ? y + 2 : y + 8;
	p.Text(x, labelY, w, 22, r.label, RowText(r), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (r.restart) {
		const int used = std::min(w - 62, p.TextWidth(r.label));
		PaintBadge(p, x + used + 8, labelY + 3, 54, 16, "Restarts", RGB(70, 46, 18), RGB(236, 186, 110));
	}
	if (hint) {
		p.Font(3);
		p.Text(x, y + 22, w, 16, r.hint, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintSliderControl(Painter &p, const Row &r, const View &v, int x, int y, int w)
{
	const float f = r.max > r.min ? static_cast<float>(r.value - r.min) / (r.max - r.min) : 0.0f;
	const bool hot = r.enabled && (v.dragId == r.id || p.Hover(x - kSliderMargin, y + 4, w + kSliderMargin * 2, kRowH - 8, v.mouseX, v.mouseY));
	PaintSlider(p, x, y + 10, w, 20, f, r.enabled, hot);
	p.Font(3);
	p.Text(x + w + 8, y + 10, kValueW - 8, 20, std::to_string(r.shown >= 0 ? r.shown : r.value) + " %", r.enabled ? pal::goldLight : kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	if (r.enabled) p.AddHit(x - kSliderMargin, y + 4, w + kSliderMargin * 2, kRowH - 8, kActSlider, r.id);
}

void PaintRow(Painter &p, const Row &r, const View &v, int x, int y, int w, RECT &dropAnchor, std::vector<std::string> &dropItems, int &dropSel)
{
	const int right = x + w;
	switch (r.kind) {
	case kRowToggle: {
		const int tx = right - kBigToggleW;
		const bool hot = p.Hover(tx, y + 9, kBigToggleW, kBigToggleH, v.mouseX, v.mouseY);
		PaintRowLabel(p, r, x, y, w - kBigToggleW - 12);
		PaintBigToggle(p, tx, y + 9, r.on, r.enabled, hot);
		if (r.enabled) p.AddHit(tx, y + 6, kBigToggleW, kBigToggleH + 6, kActToggle, r.id);
		break;
	}
	case kRowSlider: {
		const int sx = right - kValueW - kSliderW;
		PaintRowLabel(p, r, x, y, sx - x - kSliderMargin * 2);
		PaintSliderControl(p, r, v, sx, y, kSliderW);
		break;
	}
	case kRowToggleSlider: {
		const int sx = right - kValueW - kSliderW;
		const int tx = sx - kSliderMargin * 2 - kBigToggleW;
		const bool hot = p.Hover(tx, y + 9, kBigToggleW, kBigToggleH, v.mouseX, v.mouseY);
		PaintRowLabel(p, r, x, y, tx - x - 8);
		PaintBigToggle(p, tx, y + 9, r.on, true, hot);
		p.AddHit(tx, y + 6, kBigToggleW, kBigToggleH + 6, kActToggle, r.toggleId);
		PaintSliderControl(p, r, v, sx, y, kSliderW);
		break;
	}
	case kRowCombo: {
		const int cx = right - kComboW;
		PaintRowLabel(p, r, x, y, w - kComboW - 10);
		const std::string text = r.selected >= 0 && r.selected < static_cast<int>(r.items.size()) ? r.items[r.selected] : std::string("—");
		PaintCombo(p, cx, y + 6, kComboW, 28, text, v.dropId == r.id, r.enabled, v.mouseX, v.mouseY, kActCombo, r.id, v.dropId == r.id ? &dropAnchor : nullptr);
		if (v.dropId == r.id) {
			dropItems = r.items;
			dropSel = r.selected;
		}
		break;
	}
	case kRowSegment: {
		const int segW = 128, sx = right - segW * static_cast<int>(r.items.size());
		PaintRowLabel(p, r, x, y, sx - x - 8);
		for (size_t i = 0; i < r.items.size(); i++) {
			const int bx = sx + static_cast<int>(i) * segW;
			const bool on = static_cast<int>(i) == r.selected;
			const bool hot = !on && p.Hover(bx, y + 6, segW, 28, v.mouseX, v.mouseY);
			if (on) p.FillV(bx, y + 6, segW, 28, pal::goldLight, kGold);
			else p.Fill(bx, y + 6, segW, 28, hot ? pal::fieldHover : pal::well);
			p.Frame(bx, y + 6, segW, 28, on ? pal::goldLight : pal::edge);
			p.Font(0);
			p.Text(bx, y + 6, segW, 28, r.items[i], on ? RGB(34, 22, 8) : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (!on) p.AddHit(bx, y + 6, segW, 28, kActSegment, r.id << 4 | static_cast<int>(i));
		}
		break;
	}
	case kRowAction: {
		const int bw = 130, bx = right - bw;
		PaintRowLabel(p, r, x, y, bx - x - 8);
		p.Button(bx, y + 6, bw, 28, r.action, kActRow, r.id, v.mouseX, v.mouseY, r.enabled, false, 0);
		break;
	}
	case kRowInfo: {
		p.Font(0);
		const int textW = std::min(w / 2, p.TextWidth(r.text) + 4);
		int tx = right - textW;
		p.Text(tx, y + 8, textW, 22, r.text, pal::goldLight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		if (!r.badge.empty()) {
			tx -= 8 + 54;
			PaintBadge(p, tx, y + 11, 54, 16, r.badge, RGB(70, 46, 18), RGB(236, 186, 110));
		}
		PaintRowLabel(p, r, x, y, tx - x - 8);
		break;
	}
	}
}

int PaintCard(Painter &p, const Card &c, const View &v, int x, int y, int w, RECT &dropAnchor, std::vector<std::string> &dropItems, int &dropSel)
{
	const int h = kCardHead + static_cast<int>(c.rows.size()) * kRowH + 4;
	PaintCard(p, x, y, w, h);
	p.Font(4);
	p.Text(x + kCardPad, y + 7, w - kCardPad * 2, 18, c.title, kGold);
	int ry = y + kCardHead;
	for (size_t i = 0; i < c.rows.size(); i++, ry += kRowH) {
		if (i) p.Fill(x + kCardPad, ry, w - kCardPad * 2, 1, RGB(48, 37, 23));
		PaintRow(p, c.rows[i], v, x + kCardPad, ry, w - kCardPad * 2, dropAnchor, dropItems, dropSel);
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
		page.subtitle = "Image and panels";
		page.blurb = "How the game looks on your monitor.";
		page.icon = kOrnamentScreen;
		Row mode;
		mode.label = "Mode";
		mode.hint = "Alt+Enter switches";
		mode.kind = kRowSegment;
		mode.id = kWindowed;
		mode.items = {"Full screen", "Window"};
		mode.selected = Check(v, kWindowed) ? 1 : 0;
		const bool windowed = Check(v, kWindowed);
		page.left.push_back({"Size and mode", {Combo(v, "Monitor", kMonitor), mode, Combo(v, "Resolution", kResolution), Combo(v, "Window size", kWindowSize, "", false, windowed),
			Toggle(v, "Extended view", kExtendedView, "Adds larger sizes that show more of the map"), Toggle(v, "Trap the mouse", kTrapMouse, "The cursor stays inside the game window")}});
		page.right.push_back({"Image", {Combo(v, "Image scaling", kImageScaling, "How it is enlarged"), Slider(v, "Brightness", kBrightness, 80, 130), Toggle(v, "Vertical sync", kVSync, "Avoids tearing in the image"),
			Combo(v, "FPS limit", kFpsLimit)}});
		Row compact = Toggle(v, "Compact panel", kPanelCompact), opacity = Slider(v, "Panel opacity", kPanelOpacity, 30, 100);
		compact.enabled = opacity.enabled = Check(v, kShowPerf) || Check(v, kShowNet);
		page.right.push_back({"Performance panel", {Toggle(v, "Show FPS and memory", kShowPerf), Toggle(v, "Network statistics", kShowNet), compact, opacity}});
		pages.push_back(page);
	}
	{
		Page page;
		page.title = "Sound";
		page.subtitle = "Volume and alerts";
		page.blurb = "Turn each kind of sound on or off and set its volume.";
		page.icon = kOrnamentSound;
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
		page.subtitle = "Pace and saves";
		page.blurb = "How you play and how your games are saved.";
		page.icon = kOrnamentSwords;
		Row speed = Slider(v, "Game speed", kGameSpeed, 1, 100);
		speed.shown = GameSpeedPercent(speed.value);
		page.left.push_back({"Pace", {speed, Slider(v, "Scroll speed", kScrollSpeed, 1, 100), Toggle(v, "Smooth camera", kSmoothCamera),
			Toggle(v, "Pause in the background", kPauseInBackground, "Single player, when you switch to another window")}});
		Row bars = Combo(v, "Health bars", kHealthBars, "When a match starts");
		bars.items = {"No bars", "All units", "My units", "Enemy units"};
		Row heroes = Toggle(v, "Heroes only", kHealthBarHeroes);
		heroes.enabled = bars.selected > 0;
		page.left.push_back({"Interface", {Combo(v, "Language", kLanguage, "", true), bars, heroes}});
		const bool autosave = Check(v, kAutosave);
		page.right.push_back({"Autosave", {Toggle(v, "Autosave", kAutosave), Combo(v, "Every", kAutosaveEvery, "", false, autosave), Combo(v, "Copies", kAutosaveCopies, "", false, autosave),
			Toggle(v, "Save when leaving the game", kSaveOnExit)}});
		pages.push_back(page);
	}
	{
		Page page;
		page.title = "System";
		page.subtitle = "Updates and logs";
		page.blurb = "Updates of the mod, and what to do when something fails.";
		page.icon = kOrnamentGear;
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

void PaintOptionsScreen(Painter &p, const View &v)
{
	PaintWindowFrame(p, kScreenW, kScreenH);
	PaintWindowHeader(p, kScreenW, "OPTIONS");

	const std::vector<Page> pages = BuildPages(v.values);
	const int current = std::max(0, std::min(v.page, static_cast<int>(pages.size()) - 1));
	int navY = kNavTop;
	for (size_t i = 0; i < pages.size(); i++, navY += kNavH + 3) {
		const Page &page = pages[i];
		PaintNavItem(p, navY, page.title, page.subtitle, page.icon, static_cast<int>(i) == current, kActPage, static_cast<int>(i), v.mouseX, v.mouseY);
	}
	PaintNavItem(p, navY, "Controls", "Keys and shortcuts", kOrnamentKeys, false, kActControls, 0, v.mouseX, v.mouseY);
	p.Fill(kNavRight, kNavTop - 2, 1, kFooterTop - kNavTop - 2, pal::lineDim);

	const Page &page = pages[current];
	p.Font(2);
	const int titleW = p.TextWidth(page.title);
	p.Text(kContentX, kNavTop, titleW + 4, 30, page.title, pal::goldLight);
	p.Font(3);
	p.Text(kContentX + titleW + 14, kNavTop + 7, kContentRight - kContentX - titleW - 14, 20, page.blurb, RGB(186, 170, 134), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	RECT dropAnchor = {};
	std::vector<std::string> dropItems;
	int dropSel = -1;
	const int contentW = kContentRight - kContentX, leftW = page.leftW, rightX = kContentX + leftW + kCardGap + 8, rightW = contentW - leftW - kCardGap - 8;
	int y = kContentTop;
	for (const Card &c : page.left) y += PaintCard(p, c, v, kContentX, y, leftW, dropAnchor, dropItems, dropSel) + kCardGap;
	y = kContentTop;
	for (const Card &c : page.right) y += PaintCard(p, c, v, rightX, y, rightW, dropAnchor, dropItems, dropSel) + kCardGap;

	p.Fill(18, kFooterTop, kScreenW - 36, 1, pal::line);
	p.Button(24, kFooterButtonsY, 130, kFooterButtonH, "Reset", kActReset, 0, v.mouseX, v.mouseY);
	int bx = kScreenW - 24 - 136;
	p.Button(bx, kFooterButtonsY, 136, kFooterButtonH, "OK", kActOk, 0, v.mouseX, v.mouseY, true, true);
	bx -= 10 + 112;
	p.Button(bx, kFooterButtonsY, 112, kFooterButtonH, "Apply", kActApply, 0, v.mouseX, v.mouseY, v.dirty);
	bx -= 10 + 112;
	p.Button(bx, kFooterButtonsY, 112, kFooterButtonH, "Cancel", kActCancel, 0, v.mouseX, v.mouseY);
	if (v.dirty) {
		p.Diamond(bx - 176, kFooterButtonsY + kFooterButtonH / 2, 4, RGB(236, 186, 110));
		p.Font(3);
		p.Text(bx - 168, kFooterButtonsY, 160, kFooterButtonH, "Changes not applied", RGB(236, 186, 110));
	}

	if (v.dropId && !dropItems.empty()) {
		PaintDropMenu(p, dropAnchor, ToDropItems(dropItems), dropSel, v.dropScroll, kScreenW, kScreenH, kActPick, kActDropClose, v.mouseX, v.mouseY, kComboW);
	}
}

}
