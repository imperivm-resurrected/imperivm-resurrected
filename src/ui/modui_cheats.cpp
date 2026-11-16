// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <mutex>
#include "game/cheats.h"
#include "game/controls.h"

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
CheatsPanelState g_ch;
}

namespace modui_impl {
namespace {
float g_chThumbH = 0;
int g_chBarGrab = 0;
const int kChSteps[7][8] = {{100, 250, 500, 1000, 2500, 5000, 10000, 25000}, {100, 250, 500, 1000, 2500, 5000, 10000, 25000},
	{10, 25, 50, 100, 250, 500, 1000, 0}, {10, 25, 50, 100, 250, 500, 1000, 5000}, {100, 250, 500, 1000, 2500, 5000, 10000, 25000},
	{5, 10, 12, 20, 30, 50, 100, 200}, {1, 5, 10, 20, 30, 50, 75, 100}};
const int kChMax[7] = {100000, 100000, 1000, 10000, 100000, 999, kCheatsMaxSpawn};
int g_chVal[7] = {1000, 1000, 50, 100, 1000, 12, 1};
}
}

namespace modui_impl {
namespace {
std::string g_chEditText;
bool g_chSpawn = false;
}
}

namespace modui_impl {
namespace {
std::string g_chSearch;

const size_t kChSearchLimit = 20;
}
}

namespace modui_impl {
namespace {
bool g_chEatUp[256] = {};
float g_chViewH = 0;
}
}

namespace modui_impl {
namespace {
std::string ChIniPath()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char *slash = strrchr(path, '\\')) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	return path;
}

void ChLoadPos()
{
	if (g_ch.pos > -1.5f) return;
	char text[32] = {};
	GetPrivateProfileStringA("Cheats", "PanelY", "-1", text, sizeof(text), ChIniPath().c_str());
	g_ch.pos = std::max(-1.0f, std::min(1.0f, static_cast<float>(atof(text))));
}
}
}

namespace modui_impl {
void ChSavePos()
{
	char text[32];
	snprintf(text, sizeof(text), "%.4f", g_ch.pos);
	WritePrivateProfileStringA("Cheats", "PanelY", text, ChIniPath().c_str());
}
}

namespace modui_impl {
namespace {
int ChStepCount(int r)
{
	int n = 0;
	while (n < 8 && kChSteps[r][n]) n++;
	return n;
}

void ChStep(int r, int dir)
{
	const int n = ChStepCount(r);
	int v;
	if (dir > 0) {
		v = kChMax[r];
		for (int i = n - 1; i >= 0; i--) {
			if (kChSteps[r][i] > g_chVal[r]) v = kChSteps[r][i];
		}
	}
	else {
		v = 1;
		for (int i = 0; i < n; i++) {
			if (kChSteps[r][i] < g_chVal[r]) v = kChSteps[r][i];
		}
	}
	g_chVal[r] = std::max(1, std::min(kChMax[r], v));
}
}
}

namespace modui_impl {
bool ChIsValue(int verb) { return verb <= -30 && verb >= -36; }
bool ChEditing() { return g_ch.edit >= 0 || g_ch.searchEdit; }
std::string &ChText() { return g_ch.searchEdit ? g_chSearch : g_chEditText; }
}

namespace modui_impl {
namespace {
size_t ChLimit() { return g_ch.searchEdit ? kChSearchLimit : std::to_string(kChMax[std::max(0, g_ch.edit)]).size(); }
}
}

namespace modui_impl {
int ChSlot() { return g_ch.searchEdit ? 7 : g_ch.edit; }
}

namespace modui_impl {
namespace {
void ChCommitText()
{
	if (g_ch.edit < 0 || g_chEditText.empty()) return;
	g_chVal[g_ch.edit] = std::max(1, std::min(kChMax[g_ch.edit], atoi(g_chEditText.c_str())));
}

void ChStopEdit(bool keep)
{
	if (keep) ChCommitText();
	g_ch.edit = -1;
	g_chEditText.clear();
	g_ch.searchEdit = false;
	g_ch.es = EditState();
}
}
}

namespace modui_impl {
// The panel is drawn straight on the frame, without a Painter, but its bar drags and pages as PaintScrollbar's.
bool CheatsBarPress(POINT pt)
{
	if (g_ch.track.right <= g_ch.track.left || g_ch.maxScroll <= 0 || !PtInRect(&g_ch.track, pt)) return false;
	const int trackLen = g_ch.track.bottom - g_ch.track.top, thumbLen = static_cast<int>(g_chThumbH);
	const int thumbTop = g_ch.track.top + static_cast<int>((trackLen - thumbLen) * (g_ch.scroll / g_ch.maxScroll));
	if (pt.y >= thumbTop && pt.y < thumbTop + thumbLen) {
		g_ch.drag = true;
		g_chBarGrab = pt.y - thumbTop;
	}
	else {
		const ScrollSpan span = {static_cast<int>(g_ch.scroll + 0.5f), static_cast<int>(g_ch.maxScroll + 0.5f), static_cast<int>(g_chViewH)};
		g_ch.scroll = static_cast<float>(ScrollPaged(span, pt.y < thumbTop ? -1 : 1));
	}
	return true;
}

void CheatsScrollToMouse(int mouseY)
{
	const int trackLen = g_ch.track.bottom - g_ch.track.top;
	g_ch.scroll = static_cast<float>(ScrollAtThumb(static_cast<int>(g_ch.maxScroll + 0.5f), trackLen, static_cast<int>(g_chThumbH), mouseY - g_chBarGrab - g_ch.track.top));
}
}

namespace modui_impl {
namespace {
void ChScrollToFocus()
{
	for (int i = 0; i < g_ch.btnCount; i++) {
		if (g_ch.btn[i].verb != g_ch.focus) continue;
		const float top = g_ch.btn[i].offY, bottom = top + g_ch.btn[i].offH, margin = 8;
		if (top - margin < g_ch.scroll) g_ch.scroll = std::max(0.0f, top - margin);
		else if (bottom + margin > g_ch.scroll + g_chViewH) g_ch.scroll = std::max(0.0f, std::min(g_ch.maxScroll, bottom + margin - g_chViewH));
		return;
	}
}
}
}

namespace modui_impl {
void ChSetFocus(int verb)
{
	const bool same = (ChIsValue(verb) && g_ch.edit == -30 - verb) || (verb == -52 && g_ch.searchEdit);
	if (!same) {
		ChStopEdit(true);
		if (ChIsValue(verb)) {
			g_ch.edit = -30 - verb;
			g_chEditText = std::to_string(g_chVal[g_ch.edit]);
			EditSelectAll(g_chEditText, g_ch.es);
		}
		else if (verb == -52) {
			g_ch.searchEdit = true;
			EditSelectAll(g_chSearch, g_ch.es);
		}
	}
	g_ch.focus = verb;
	ChScrollToFocus();
}

void ChClearFocus()
{
	ChStopEdit(true);
	g_ch.focus = kChNoFocus;
}
}

namespace modui_impl {
namespace {
std::vector<FocusItem> ChOrder()
{
	std::vector<FocusItem> out;
	for (int i = 0; i < g_ch.btnCount; i++) {
		const int v = g_ch.btn[i].verb;
		if (v <= -10 && v >= -29) continue;
		out.push_back({v, 0});
	}
	return out;
}

void ChMove(int steps)
{
	const std::vector<FocusItem> order = ChOrder();
	if (order.empty()) return;
	FocusItem cur = {g_ch.focus, 0};
	FocusItem next = cur;
	for (int i = 0; i < std::abs(steps); i++) next = FocusStep(order, next, steps < 0);
	if (next.action != cur.action) ChSetFocus(next.action);
}

void ChTab(bool back)
{
	const std::vector<FocusItem> order = ChOrder();
	if (order.empty()) return;
	const FocusItem next = FocusStep(order, {g_ch.focus, 0}, back);
	ChSetFocus(next.action);
}
}
}

namespace modui_impl {
void ChActivate(int v, int amount, bool fromKey)
{
	if (v <= -100) {
		ChStopEdit(true);
		const int unit = -100 - v, count = g_chVal[6];
		g_lock.unlock();
		CheatsSpawn(unit, count);
		g_lock.lock();
	}
	else if (v == -50 || v == -51) {
		ChStopEdit(true);
		g_chSpawn = v == -50;
		g_ch.scroll = 0;
		g_ch.focus = kChNoFocus;
		if (fromKey) {
			g_ch.focus = v == -50 ? -52 : -50;
			if (v == -50) {
				g_ch.searchEdit = true;
				EditSelectAll(g_chSearch, g_ch.es);
			}
		}
	}
	else if (v == -52) {
		ChSetFocus(-52);
	}
	else if (ChIsValue(v)) {
		ChSetFocus(v);
	}
	else {
		ChStopEdit(true);
		if (v <= -20) ChStep(-20 - v, 1);
		else if (v <= -10) ChStep(-10 - v, -1);
		else {
			g_lock.unlock();
			CheatsRun(v, amount);
			g_lock.lock();
		}
	}
}

bool ChHandleKey(UINT msg, WPARAM wp, LPARAM lp)
{
	const bool editing = ChEditing();
	const bool focused = g_ch.focus != kChNoFocus;
	if (msg == WM_KEYUP) {
		if (wp < 256 && g_chEatUp[wp]) {
			g_chEatUp[wp] = false;
			return true;
		}
		return false;
	}
	if (msg == WM_CHAR) {
		if (!editing) return focused && (wp == 9 || wp == 13 || wp == 27 || wp == 32);
		if (wp >= 32 && wp != 127 && (g_ch.edit < 0 || (wp >= '0' && wp <= '9'))) {
			if (EditChar(ChText(), g_ch.es, wp, ChLimit()) && g_ch.searchEdit) g_ch.scroll = 0;
		}
		return true;
	}
	if (msg != WM_KEYDOWN) return false;
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool repeat = (lp & (1 << 30)) != 0;
	auto eat = [&]() {
		if (wp < 256) g_chEatUp[wp] = true;
		return true;
	};
	if (wp == VK_TAB && !ctrl) {
		const POINT hover = {g_hover.x, g_hover.y};
		const bool armed = CheatsPanelConsumeKeyOpen();
		if (!editing && !focused && !armed && !PtInRect(&g_ch.panel, hover)) return false;
		ChTab(shift);
		return eat();
	}
	if (!editing && !focused) return false;
	if (editing) {
		if (ctrl && ControlsModKeyPressed(kModKeyCheats, static_cast<unsigned>(wp))) return false;
		const bool value = g_ch.edit >= 0;
		if (wp == VK_ESCAPE) {
			if (!value && !g_chSearch.empty()) {
				g_chSearch.clear();
				g_ch.es = EditState();
				g_ch.scroll = 0;
			}
			else {
				ChStopEdit(false);
				g_ch.focus = kChNoFocus;
			}
			return eat();
		}
		if (wp == VK_RETURN) {
			if (value) {
				ChCommitText();
				g_chEditText = std::to_string(g_chVal[g_ch.edit]);
				EditSelectAll(g_chEditText, g_ch.es);
			}
			else {
				ChTab(false);
			}
			return eat();
		}
		if (wp == VK_UP || wp == VK_DOWN) {
			if (value) {
				ChCommitText();
				ChStep(g_ch.edit, wp == VK_UP ? 1 : -1);
				g_chEditText = std::to_string(g_chVal[g_ch.edit]);
				EditSelectAll(g_chEditText, g_ch.es);
			}
			else {
				ChTab(wp == VK_UP);
			}
			return eat();
		}
		if (value && ctrl && wp == 'V') {
			std::string digits;
			for (const char c : ClipboardGet()) {
				if (c >= '0' && c <= '9') digits += c;
			}
			EditInsert(g_chEditText, g_ch.es, digits, ChLimit());
			return eat();
		}
		if (EditKey(ChText(), g_ch.es, wp, ChLimit()) == 2 && g_ch.searchEdit) g_ch.scroll = 0;
		return eat();
	}
	if (wp == VK_ESCAPE) {
		g_ch.focus = kChNoFocus;
		return eat();
	}
	if (wp == VK_RETURN || wp == VK_SPACE) {
		if (!repeat) {
			for (int i = 0; i < g_ch.btnCount; i++) {
				if (g_ch.btn[i].verb != g_ch.focus) continue;
				ChActivate(g_ch.focus, g_ch.btn[i].amount, true);
				break;
			}
		}
		return eat();
	}
	if (wp == VK_UP || wp == VK_DOWN) {
		ChTab(wp == VK_UP);
		return eat();
	}
	if (wp == VK_PRIOR || wp == VK_NEXT) {
		ChMove(wp == VK_PRIOR ? -8 : 8);
		return eat();
	}
	if (wp == VK_HOME || wp == VK_END) {
		const std::vector<FocusItem> order = ChOrder();
		if (!order.empty()) ChSetFocus(wp == VK_HOME ? order.front().action : order.back().action);
		return eat();
	}
	return false;
}

void DrawCheatsPanel(uint32_t *frame, int frameW, int H, float s)
{
	uint32_t *buf = frame;
	int W = frameW;
	if (!CheatsPanelAvailable()) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_ch.shown = false;
		g_ch.drag = false;
		ChStopEdit(false);
		g_ch.focus = kChNoFocus;
		return;
	}
	const bool open = CheatsPanelOpen();
	POINT mouse;
	float scroll;
	bool spawn, searchEdit;
	std::string search, editText;
	int focus, editField;
	EditState es;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!open) {
			ChStopEdit(false);
			g_ch.focus = kChNoFocus;
		}
		mouse = {g_hover.x, g_hover.y};
		scroll = g_ch.scroll;
		spawn = g_chSpawn;
		search = g_chSearch;
		editText = g_chEditText;
		editField = g_ch.edit;
		searchEdit = g_ch.searchEdit;
		focus = g_ch.focus;
		es = g_ch.es;
	}
	EnsureFonts(s);
	const DWORD now = GetTickCount();
	const COLORREF panel = pal::panel, frameOuter = pal::hairline, frameInner = pal::lineStrong;
	const COLORREF btn = pal::button, btnHot = pal::buttonHover, strong = pal::red, strongHot = pal::redPressed;
	const COLORREF btnFrame = pal::edgeHot, label = pal::body;
	const float line = std::max(1.0f, s), rad = 2.0f * s;

	struct Item {
		int kind;
		const char *text;
		int verb;
		int res;
		bool hero;
	};
	static const Item mainItems[] = {
		{1, "Gold", 0, 0},
		{1, "Food", 1, 1},
		{1, "Population", 15, 3},
		{1, "Max population", 3, 2},
		{1, "Level", 10, 5},
		{2, "Heal troops", 4, 0},
		{2, "Feed troops", 12, 0},
		{2, "Repair building", 11, 0},
		{2, "Reveal the whole map", 2, 0},
		{2, "Research technologies", 9, 0},
		{2, "Eliminate unit", 13, 0},
		{2, "Capture settlement", 14, 0},
		{2, "Spawn troops...", -50, 0},
	};
	std::vector<Item> items;
	if (!spawn) {
		items.assign(mainItems, mainItems + sizeof(mainItems) / sizeof(mainItems[0]));
	}
	else {
		items.push_back({2, "Back", -51, 0, false});
		items.push_back({1, "Amount", -60, 6, false});
		items.push_back({4, "Search", -52, 0, false});
		auto lower = [](std::string t) {
			for (char &c : t) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			return t;
		};
		const std::string q = lower(search);
		int lastGroup = -1;
		for (int i = 0; i < CheatsUnitCount(); i++) {
			if (!q.empty() && lower(CheatsUnitName(i)).find(q) == std::string::npos && lower(CheatsGroupName(CheatsUnitGroup(i))).find(q) == std::string::npos) continue;
			if (CheatsUnitGroup(i) != lastGroup) {
				lastGroup = CheatsUnitGroup(i);
				items.push_back({0, CheatsGroupName(lastGroup), 0, 0, false});
			}
			items.push_back({3, CheatsUnitName(i), -100 - i, 0, CheatsUnitIsHero(i)});
		}
		if (lastGroup < 0) items.push_back({0, CheatsUnitCount() ? "No results" : "The game has not loaded its troops yet", 0, 0, false});
	}
	const int nItems = static_cast<int>(items.size());
	const float panelW = 300 * s, pad = 12 * s, rowH = 30 * s, unitH = 24 * s, secH = 26 * s, headH = 36 * s;
	auto heightOf = [&](const Item &it) { return it.kind == 0 ? secH : it.kind == 3 ? unitH + 2 * s : rowH + 4 * s; };
	float contentH = 8 * s;
	for (const Item &it : items) contentH += heightOf(it);
	contentH += 8 * s;
	ChLoadPos();
	const float tabW = 18 * s, tabH = 60 * s;
	const float yMin = 84.0f, yMax = static_cast<float>(H) - 58.0f;
	const float full = headH + contentH + 4 * s;
	const float panelH = std::min(full, yMax - yMin - 8.0f);
	const float top = yMin + (yMax - yMin - panelH) / 2;
	const float tabY = (yMin + yMax) / 2 - tabH / 2;
	const float viewTop = top + headH, viewH = panelH - headH - 4 * s;
	const float maxScroll = std::max(0.0f, contentH - viewH);
	scroll = std::max(0.0f, std::min(scroll, maxScroll));
	const float mx = static_cast<float>(mouse.x), my = static_cast<float>(mouse.y);

	RectF tab{open ? panelW : 0.0f, tabY, tabW, tabH};
	const bool hTab = tab.Has(mx, my);
	FillRound(buf, W, H, tab.x, tab.y, tab.w, tab.h, rad, hTab ? strongHot : strong, 1.0f);
	{
		const float cx = tab.x + tab.w / 2, cy = tab.y + tab.h / 2, wd = 9 * s;
		if (open) FillTriangle(buf, W, H, cx + wd / 2, cy - wd, cx + wd / 2, cy + wd, cx - wd / 2, cy, label, 1.0f);
		else FillTriangle(buf, W, H, cx - wd / 2, cy - wd, cx - wd / 2, cy + wd, cx + wd / 2, cy, label, 1.0f);
	}

	int count = 0;
	static CheatBtn buttons[kChMaxBtn];
	RECT track = {};
	float thumbH = 0;
	auto toRect = [](const RectF &r) { return RECT{static_cast<LONG>(r.x), static_cast<LONG>(r.y), static_cast<LONG>(r.x + r.w), static_cast<LONG>(r.y + r.h)}; };
	static FieldGeom cGeom[8];
	static bool cGeomOk[8];
	if (open) {
		static std::vector<uint32_t> cache;
		static std::string cacheSig;
		static CheatBtn cButtons[kChMaxBtn];
		static int cCount = 0;
		static RECT cTrack = {};
		static float cThumb = 0;
		const int cacheW = static_cast<int>(panelW) + 2;
		const bool mouseOver = mouse.x >= 0 && mouse.x < cacheW && mouse.y >= static_cast<int>(top) && mouse.y < static_cast<int>(top + panelH);
		char sigBuf[320];
		snprintf(sigBuf, sizeof(sigBuf), "%d|%d|%d|%.2f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%.2f|%d|%d|%d|%d|%d", static_cast<int>(top), static_cast<int>(panelH), cacheW, scroll, spawn ? 1 : 0,
			editField, searchEdit ? 1 : 0, (editField >= 0 || searchEdit) ? static_cast<int>((now / 500) % 2) : 0, g_chVal[0], g_chVal[1], g_chVal[2], g_chVal[3], g_chVal[5], g_chVal[6],
			mouseOver ? mouse.x : -1, mouseOver ? mouse.y : -1, s, g_ch.drag ? 1 : 0, H, focus, es.caret, es.anchor);
		const std::string sig = std::string(sigBuf) + "|" + editText + "|" + search;
		if (sig != cacheSig || cache.empty()) {
		for (bool &ok : cGeomOk) ok = false;
		cache.assign(static_cast<size_t>(cacheW) * H, 0xff000000u | (30u << 16) | (24u << 8) | 17u);
		uint32_t *buf = cache.data();
		const int W = cacheW;
		FillRound(buf, W, H, 0, top, panelW, panelH, rad, panel, 0.97f);
		StrokeRound(buf, W, H, 0, top, panelW, panelH, rad, line, frameOuter, 1.0f);
		std::wstring title = ::W(Tr("Cheats"));
		CharUpperBuffW(&title[0], static_cast<DWORD>(title.size()));
		DrawLabel(buf, W, H, title, pad + 2 * s, top + 4 * s, panelW - 2 * pad, 30 * s, DT_LEFT, pal::text, 1.0f, BrandFont(BrandFace::Display, 26, s));
		FillRound(buf, W, H, pad, top + headH - 2 * s, panelW - 2 * pad, line, 0, frameOuter, 1.0f);

		const bool bar = maxScroll > 0;
		const float contentW = panelW - 2 * pad - (bar ? 12 * s : 0);
		const RECT view = {0, static_cast<LONG>(viewTop), static_cast<LONG>(panelW), static_cast<LONG>(viewTop + viewH)};
		g_clip = {view.left, view.top, view.right, view.bottom};
		auto visible = [&](const RectF &r) {
			RECT out = {}, rr = toRect(r);
			IntersectRect(&out, &rr, &view);
			return out;
		};
		auto button = [&](const RectF &r, bool isStrong, bool hot) {
			FillRound(buf, W, H, r.x, r.y, r.w, r.h, rad, isStrong ? (hot ? strongHot : strong) : (hot ? btnHot : btn), 1.0f);
		};
		const bool mouseIn = mx >= 0 && mx < panelW && my >= viewTop && my < viewTop + viewH;
		auto add = [&](int verb, int amount, const RECT &rect, const RectF &row) {
			if (count < kChMaxBtn) buttons[count++] = {verb, amount, rect, row.y - viewTop + scroll, row.h};
		};
		auto ring = [&](const RectF &r) {
			StrokeRound(buf, W, H, r.x - 1.5f * s, r.y - 1.5f * s, r.w + 3 * s, r.h + 3 * s, rad, 2 * line, pal::text, 1.0f);
		};
		auto fieldText = [&](const RectF &box, const std::string &text, bool editing, bool center, int slot) {
			EditState e = es;
			EditFix(text, e);
			std::vector<int> bytes, edges(1, 0);
			bytes.push_back(0);
			{
				std::wstring ws;
				std::vector<size_t> wlen(1, 0);
				for (int i = 0; i < static_cast<int>(text.size());) {
					const int nx = NextCp(text, i);
					ws += ::W(text.substr(i, nx - i));
					wlen.push_back(ws.size());
					bytes.push_back(nx);
					i = nx;
				}
				edges.assign(bytes.size(), 0);
				if (!ws.empty() && g_spec.text.Ensure(400, 64)) {
					SelectObject(g_spec.text.dc, g_fonts[3]);
					std::vector<int> dx(ws.size());
					SIZE sz = {};
					GetTextExtentExPointW(g_spec.text.dc, ws.c_str(), static_cast<int>(ws.size()), 0x7fffffff, nullptr, dx.data(), &sz);
					for (size_t k = 1; k < edges.size(); k++) edges[k] = wlen[k] ? dx[wlen[k] - 1] : 0;
				}
			}
			auto indexOf = [&](int byte) {
				const size_t k = std::lower_bound(bytes.begin(), bytes.end(), byte) - bytes.begin();
				return std::min(k, bytes.size() - 1);
			};
			const float padX = 8 * s, innerW = box.w - 2 * padX, totalW = static_cast<float>(edges.back());
			const float origin = center && totalW <= innerW ? box.x + (box.w - totalW) / 2 : box.x + padX;
			float scrollPx = 0;
			if (editing && totalW > innerW) scrollPx = std::max(0.0f, std::min(totalW - innerW + 2, static_cast<float>(edges[indexOf(e.caret)]) - innerW + 2));
			const float textX = origin - scrollPx;
			const ClipBox saved = g_clip;
			g_clip.x0 = std::max(g_clip.x0, static_cast<int>(box.x + 4 * s));
			g_clip.x1 = std::min(g_clip.x1, static_cast<int>(box.x + box.w - 4 * s));
			if (editing && HasSel(e)) {
				const float a = textX + edges[indexOf(SelLo(e))], b = textX + edges[indexOf(SelHi(e))];
				FillRound(buf, W, H, a, box.y + 4 * s, b - a, box.h - 8 * s, 0, pal::red, 1.0f);
			}
			if (!text.empty()) DrawLabel(buf, W, H, ::W(text), textX, box.y, totalW + 16 * s, box.h, DT_LEFT, label, 1.0f);
			if (editing && !HasSel(e) && (now / 500) % 2 == 0)
				FillRound(buf, W, H, textX + edges[indexOf(e.caret)], box.y + 5 * s, std::max(1.0f, s), box.h - 10 * s, 0, label, 1.0f);
			g_clip = saved;
			cGeom[slot].textX = static_cast<int>(textX);
			cGeom[slot].scroll = 0;
			cGeom[slot].edges = edges;
			cGeom[slot].bytes = bytes;
			cGeomOk[slot] = true;
		};
		float y = viewTop - scroll + 8 * s;
		for (int n = 0; n < nItems; n++) {
			const Item &it = items[n];
			if (it.kind == 0) {
				std::wstring t = ::W(Tr(it.text));
				CharUpperBuffW(&t[0], static_cast<DWORD>(t.size()));
				DrawLabel(buf, W, H, t, pad + 2 * s, y + 4 * s, contentW, 20 * s, DT_LEFT, pal::textDim, 1.0f);
				FillRound(buf, W, H, pad, y + secH - 3 * s, contentW, line, 0, frameInner, 0.7f);
				y += secH;
				continue;
			}
			RectF row{pad, y, contentW, it.kind == 3 ? unitH : rowH};
			if (it.kind == 2) {
				const bool hot = mouseIn && row.Has(mx, my);
				button(row, false, hot);
				DrawLabel(buf, W, H, ::W(Tr(it.text)), row.x + 6 * s, row.y, row.w - 12 * s, row.h, DT_CENTER | DT_END_ELLIPSIS, label, 1.0f);
				if (focus == it.verb) ring(row);
				add(it.verb, 0, visible(row), row);
			}
			else if (it.kind == 3) {
				const bool hot = mouseIn && row.Has(mx, my);
				button(row, false, hot);
				DrawLabel(buf, W, H, ::W(it.text), row.x + 8 * s, row.y, row.w - 16 * s, row.h, DT_LEFT | DT_END_ELLIPSIS, it.hero ? pal::redText : label, 1.0f);
				if (focus == it.verb) ring(row);
				add(it.verb, 0, visible(row), row);
			}
			else if (it.kind == 4) {
				const bool editing = searchEdit, hot = mouseIn && row.Has(mx, my);
				FillRound(buf, W, H, row.x, row.y, row.w, row.h, rad, pal::bg, 1.0f);
				StrokeRound(buf, W, H, row.x, row.y, row.w, row.h, rad, line, editing ? pal::redText : hot ? btnFrame : frameInner, 1.0f);
				if (search.empty() && !editing) DrawLabel(buf, W, H, ::W(Tr("Search...")), row.x + 8 * s, row.y, row.w - 16 * s, row.h, DT_LEFT, pal::disabled, 1.0f);
				fieldText(row, search, editing, false, 7);
				add(it.verb, 0, visible(row), row);
			}
			else {
				const int r = it.res;
				const bool noGive = it.verb < 0;
				const float giveW = noGive ? 0.0f : 50 * s, arrowW = 22 * s, valW = 62 * s, gap = noGive ? 0.0f : 5 * s;
				RectF give{row.x + row.w - giveW, row.y, giveW, row.h};
				RectF inc{give.x - gap - arrowW, row.y, arrowW, row.h};
				RectF val{inc.x - valW, row.y, valW, row.h};
				RectF dec{val.x - arrowW, row.y, arrowW, row.h};
				DrawLabel(buf, W, H, ::W(Tr(it.text)), row.x + 2 * s, row.y, dec.x - row.x - 6 * s, row.h, DT_LEFT | DT_END_ELLIPSIS, label, 1.0f);
				const int cur = g_chVal[r];
				const bool canDec = cur > 1, canInc = cur < kChMax[r];
				const bool hDec = mouseIn && dec.Has(mx, my) && canDec, hInc = mouseIn && inc.Has(mx, my) && canInc;
				const bool editing = editField == r, hVal = mouseIn && val.Has(mx, my);
				FillRound(buf, W, H, val.x, val.y, val.w, val.h, rad, pal::bg, 1.0f);
				StrokeRound(buf, W, H, val.x, val.y, val.w, val.h, rad, line, editing ? pal::redText : hVal ? btnFrame : frameInner, 1.0f);
				fieldText(val, editing ? editText : std::to_string(cur), editing, true, r);
				button(dec, false, hDec);
				button(inc, false, hInc);
				const COLORREF dim = pal::disabled;
				const float cyc = row.y + row.h / 2, wd = 9 * s;
				FillTriangle(buf, W, H, dec.x + dec.w / 2 + wd / 3, cyc - wd / 2, dec.x + dec.w / 2 + wd / 3, cyc + wd / 2, dec.x + dec.w / 2 - wd / 2, cyc, canDec ? label : dim, 1.0f);
				FillTriangle(buf, W, H, inc.x + inc.w / 2 - wd / 3, cyc - wd / 2, inc.x + inc.w / 2 - wd / 3, cyc + wd / 2, inc.x + inc.w / 2 + wd / 2, cyc, canInc ? label : dim, 1.0f);
				if (!noGive) {
					const bool hGive = mouseIn && give.Has(mx, my);
					button(give, true, hGive);
					DrawLabel(buf, W, H, ::W(Tr("Give")), give.x, give.y, give.w, give.h, DT_CENTER, label, 1.0f);
				}
				if (!noGive && focus == it.verb) ring(give);
				add(-10 - r, 0, visible(dec), row);
				add(-20 - r, 0, visible(inc), row);
				add(-30 - r, 0, visible(val), row);
				if (!noGive) add(it.verb, cur, visible(give), row);
			}
			y += heightOf(it);
		}
		g_clip = ClipBox();
		if (bar) {
			const float bx = panelW - pad - 4 * s, bw = 6 * s;
			thumbH = std::max(28 * s, viewH * viewH / contentH);
			const float thumbY = viewTop + (viewH - thumbH) * (scroll / maxScroll);
			const bool grab = g_ch.drag || (mx >= bx - 6 * s && mx < bx + bw + 6 * s && my >= viewTop && my < viewTop + viewH);
			FillRound(buf, W, H, bx, viewTop, bw, viewH, 0, pal::bg, 1.0f);
			FillRound(buf, W, H, bx, thumbY, bw, thumbH, 0, grab ? pal::edgeHot : pal::red, 1.0f);
			track = {static_cast<LONG>(bx - 6 * s), static_cast<LONG>(viewTop), static_cast<LONG>(bx + bw + 6 * s), static_cast<LONG>(viewTop + viewH)};
		}
		cCount = count;
		std::copy(buttons, buttons + count, cButtons);
		cTrack = track;
		cThumb = thumbH;
		cacheSig = sig;
		}
		for (int y = std::max(0, static_cast<int>(top)); y < std::min(H, static_cast<int>(top + panelH) + 1); y++)
			memcpy(frame + static_cast<size_t>(y) * frameW, cache.data() + static_cast<size_t>(y) * cacheW, static_cast<size_t>(std::min(cacheW, frameW)) * 4);
		count = cCount;
		std::copy(cButtons, cButtons + count, buttons);
		track = cTrack;
		thumbH = cThumb;
	}
	if (hTab) ModUiDrawHoverCard(buf, W, H, open ? "Hide cheats" : "Cheats", pal::redText, static_cast<int>(tab.x + tab.w + 8 * s), static_cast<int>(tab.y + tab.h / 2), KnownKeys({ControlsModKey(kModKeyCheats)}));
	std::lock_guard<std::mutex> lock(g_lock);
	g_ch.tab = toRect(tab);
	g_ch.panel = open ? RECT{0, static_cast<LONG>(top), static_cast<LONG>(panelW), static_cast<LONG>(top + panelH)} : RECT{};
	g_ch.track = track;
	g_chThumbH = thumbH;
	g_ch.maxScroll = maxScroll;
	g_ch.scroll = std::max(0.0f, std::min(g_ch.scroll, maxScroll));
	g_ch.winH = H;
	g_ch.btnCount = count;
	for (int i = 0; i < count; i++) g_ch.btn[i] = buttons[i];
	g_chViewH = viewH;
	for (int i = 0; i < 8; i++) {
		g_ch.geomOk[i] = open && cGeomOk[i];
		if (g_ch.geomOk[i]) g_ch.geom[i] = cGeom[i];
	}
	g_ch.shown = true;
}
}

bool ModUiCoversPoint(int x, int y)
{
	if (!CheatsPanelAvailable()) return false;
	const POINT at = {x, y};
	std::lock_guard<std::mutex> lock(g_lock);
	return PtInRect(&g_ch.tab, at) || PtInRect(&g_ch.panel, at);
}
