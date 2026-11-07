// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <mutex>
#include <string>
#include <vector>

#include "game/controls.h"
#include "ui/controlsui.h"
#include "ui/keycaps.h"
#include "core/log.h"
#include "ui/widgets.h"
#include "i18n/tr.h"

using namespace ui;

namespace {

const int kW = 880, kH = 580;
const int kNavX = 16, kNavW = 182, kTop = 72;
const int kListX = 212, kListW = kW - kListX - 16;
const int kFooterH = 60;
const int kRowH = 34, kHintRowH = 46, kHeaderH = 30, kNavItemH = 30, kNavLabelH = 22, kColsH = 24;
const int kCellW = 132, kCellGap = 8, kResetW = 26;
const int kBannerH = 132;
const COLORREF kSpecial = RGB(255, 196, 84);
const DWORD kUndoMs = 15000;

enum Action {
	kNone = 0, kClose, kNav, kCell, kRowReset, kSearchField, kClearSearch, kSwap, kAssignAnyway,
	kCancelPending, kModeLetter, kModePosition, kUndo, kViewList, kViewKeys, kKbKey, kKbLayer, kKbGo,
	kBarLetters, kCopyConfig, kPasteConfig, kBoxMode, kResetAll, kResetAllYes,
};
const int kSearchAction = 7301;

struct Ref {
	int row = -1, slot = 0;
};

struct State {
	bool open = false;
	std::vector<ControlInfo> rows;
	std::vector<Ref> clash;
	bool grid = false;
	bool barLetters = true;
	int boxMode = 0;
	int page = kPageCamera;
	ListState list;
	ListGeom listGeom;
	std::string search;
	EditState edit;
	bool searchFocus = false;
	Ref capture;
	Ref pend;
	bool pendReset = false;
	bool confirmResetAll = false;
	ControlKey pendKey;
	std::vector<Ref> pendWith;
	int view = 0;
	int layer = 0;
	int kbSel = -1;
	ListState kbList;
	ListGeom kbGeom;
	int kbScrollKey = -1;
	RECT kbBox = {};
	int flashRow = -1;
	DWORD flashAt = 0;
	std::string undo;
	DWORD undoAt = 0;
	std::string status;
	bool statusBad = false;
	DWORD statusAt = 0;
	int mx = -1, my = -1;
	int tabAction = -1, tabArg = 0;
	bool escArmed = false, eatChar = false;
	std::vector<Hit> hits;
	RECT panel = {};
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

bool Asking(const State &st)
{
	return st.pend.row >= 0 || st.confirmResetAll;
}

ControlKey KeyOf(const ControlInfo &r, int slot)
{
	return slot ? r.key2 : r.key;
}

bool IsDefault(const ControlInfo &r)
{
	return r.key == r.def && !r.key2.vk;
}

bool ClashKeys(const ControlInfo &a, ControlKey ka, const ControlInfo &b, ControlKey kb, bool grid)
{
	if (!ka.vk || ka.vk != kb.vk) return false;
	if (grid && (a.order || b.order)) return false;
	if (IsDefault(a) && IsDefault(b)) return false;
	if (a.context == -1 || b.context == -1) return ka == kb;
	if (!a.order && !b.order) return ka.mods == kb.mods;
	if (a.order && b.order) return a.context == b.context && (ka.mods == kb.mods || !ka.mods || !kb.mods);
	const bool aGeneral = !a.order;
	const ControlInfo &g = aGeneral ? a : b;
	const ControlKey kg = aGeneral ? ka : kb, ko = aGeneral ? kb : ka;
	if (g.withOrders && kg == g.def) return false;
	return kg.mods == 0 || kg.mods == ko.mods;
}

std::vector<Ref> ClashesFor(const State &st, int row, int slot, ControlKey k)
{
	std::vector<Ref> out;
	ControlInfo probe = st.rows[row];
	(slot ? probe.key2 : probe.key) = k;
	for (int i = 0; i < static_cast<int>(st.rows.size()); i++) {
		if (i == row) continue;
		for (int s = 0; s < 2; s++) {
			if (ClashKeys(probe, k, st.rows[i], KeyOf(st.rows[i], s), st.grid)) out.push_back({i, s});
		}
	}
	return out;
}

void Refresh(State &st)
{
	st.rows = ControlsList();
	st.grid = ControlsGridMode();
	st.barLetters = ControlsBarLetters();
	st.boxMode = ControlsZoomBoxMode();
	st.clash.assign(st.rows.size() * 2, Ref());
	for (size_t i = 0; i < st.rows.size(); i++) {
		for (int s = 0; s < 2; s++) {
			const ControlKey k = KeyOf(st.rows[i], s);
			if (!k.vk) continue;
			for (size_t j = 0; j < st.rows.size() && st.clash[i * 2 + s].row < 0; j++) {
				if (i == j) continue;
				for (int t = 0; t < 2; t++) {
					if (ClashKeys(st.rows[i], k, st.rows[j], KeyOf(st.rows[j], t), st.grid)) {
						st.clash[i * 2 + s] = {static_cast<int>(j), t};
						break;
					}
				}
			}
		}
	}
}

void SetStatus(State &st, const std::string &text, bool bad)
{
	st.status = text;
	st.statusBad = bad;
	st.statusAt = GetTickCount();
}

std::string KeyText(ControlKey k)
{
	return k.vk ? ControlsKeyLabel(k) : "no key";
}

std::string Where(const ControlInfo &r)
{
	return r.section.empty() ? ControlsPageName(r.page) : r.section;
}

void SetRowKey(const ControlInfo &r, int slot, ControlKey k)
{
	if (r.order && !r.ids.empty()) {
		for (const std::string &id : r.ids) ControlsSetKey(id, true, slot, k);
	}
	else {
		ControlsSetKey(r.id, r.order, slot, k);
	}
}

void ResetRow(const ControlInfo &r)
{
	if (r.order && !r.ids.empty()) {
		for (const std::string &id : r.ids) ControlsResetRow(id, true);
	}
	else {
		ControlsResetRow(r.id, r.order);
	}
}

void BeginChange(State &st)
{
	st.undo = ControlsSnapshot();
	st.undoAt = GetTickCount();
}

void Commit(State &st, Ref at, ControlKey k)
{
	const ControlInfo r = st.rows[at.row];
	BeginChange(st);
	SetRowKey(r, at.slot, k);
	if (r.order && at.slot == 0 && r.key2.vk && k.vk && r.key2.mods != k.mods) SetRowKey(r, 1, ControlKey());
	Refresh(st);
	SetStatus(st, r.name + (at.slot ? Tr(" (alternative): ") : ": ") + KeyText(k), false);
}

void TryAssign(State &st, Ref at, ControlKey k)
{
	st.capture = Ref();
	if (at.row < 0 || at.row >= static_cast<int>(st.rows.size())) return;
	const ControlInfo &r = st.rows[at.row];
	std::string why;
	if (k.vk && !ControlsKeyAllowed(r.rule, k, why)) {
		SetStatus(st, Tr("You can't use ") + ControlsKeyLabel(k) + ": " + why + ".", true);
		return;
	}
	if (k.vk && k == KeyOf(r, 1 - at.slot)) {
		SetStatus(st, ControlsKeyLabel(k) + Tr(" is already the other key for this action."), true);
		return;
	}
	if (r.order && at.slot == 1 && k.vk && r.key.vk && k.mods != r.key.mods) {
		SetStatus(st, Tr("A command's alternative key uses Ctrl just like the main one (") + ControlsKeyLabel(r.key) + ").", true);
		return;
	}
	if (k == KeyOf(r, at.slot)) return;
	std::vector<Ref> with = ClashesFor(st, at.row, at.slot, k);
	if (!with.empty()) {
		st.pend = at;
		st.pendKey = k;
		st.pendWith = with;
		return;
	}
	const std::string name = r.name;
	const bool general = !r.order;
	Commit(st, at, k);
	if (st.grid && general && k.vk && !k.mods) {
		const std::string grid = ControlsGridKeys();
		const size_t pos = grid.find(static_cast<char>(k.vk));
		if (k.vk < 128 && pos != std::string::npos) {
			SetStatus(st, Tr("Heads up: with commands by position, ") + ControlsKeyLabel(k) + Tr(" was button ") + std::to_string(pos + 1) +
				Tr(" on the bar; now it does «") + name + "».", true);
		}
	}
}

bool CanSwap(const State &st)
{
	if (st.pend.row < 0 || st.pendWith.size() != 1) return false;
	const ControlKey old = KeyOf(st.rows[st.pend.row], st.pend.slot);
	const ControlInfo &o = st.rows[st.pendWith[0].row];
	std::string why;
	if (!old.vk || !ControlsKeyAllowed(o.rule, old, why) || old == KeyOf(o, 1 - st.pendWith[0].slot)) return false;
	if (o.order && st.pendWith[0].slot == 1 && o.key.vk && old.mods != o.key.mods) return false;
	return true;
}

std::string ClipboardText()
{
	std::string out;
	if (!OpenClipboard(nullptr)) return out;
	if (HANDLE hData = GetClipboardData(CF_UNICODETEXT)) {
		if (const wchar_t *w = static_cast<const wchar_t *>(GlobalLock(hData))) {
			const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			if (n > 1) {
				out.resize(n - 1);
				WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
			}
			GlobalUnlock(hData);
		}
	}
	CloseClipboard();
	std::string clean;
	size_t from = 0;
	while (from <= out.size()) {
		size_t to = out.find('\n', from);
		if (to == std::string::npos) to = out.size();
		std::string line = out.substr(from, to - from);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.rfind("```", 0) != 0) clean += line + "\r\n";
		from = to + 1;
	}
	return clean;
}

std::wstring Fold(const std::string &s)
{
	std::wstring w = W(s);
	if (!w.empty()) CharLowerBuffW(&w[0], static_cast<DWORD>(w.size()));
	for (wchar_t &c : w) {
		switch (c) {
		case L'á': c = L'a'; break;
		case L'é': c = L'e'; break;
		case L'í': c = L'i'; break;
		case L'ó': c = L'o'; break;
		case L'ú': case L'ü': c = L'u'; break;
		}
	}
	return w;
}

struct Item {
	bool header;
	int row;
	std::string label;
};

std::vector<Item> BuildView(const State &st)
{
	std::vector<Item> view;
	std::string lastHeader;
	auto header = [&](const std::string &h) {
		if (h != lastHeader) view.push_back({true, -1, h});
		lastHeader = h;
	};
	if (!st.search.empty()) {
		const std::wstring q = Fold(st.search);
		for (int i = 0; i < static_cast<int>(st.rows.size()); i++) {
			const ControlInfo &r = st.rows[i];
			const std::wstring haystack = Fold(r.name + " | " + r.hint + " | " + r.section + " | " + ControlsPageName(r.page) + " | " +
				ControlsKeyLabel(r.key) + " | " + ControlsKeyLabel(r.key2));
			if (haystack.find(q) == std::wstring::npos) continue;
			header(r.section.empty() ? ControlsPageName(r.page) : std::string(ControlsPageName(r.page)) + " · " + r.section);
			view.push_back({false, i, ""});
		}
		return view;
	}
	for (int i = 0; i < static_cast<int>(st.rows.size()); i++) {
		const ControlInfo &r = st.rows[i];
		if (r.page != st.page) continue;
		if (!r.section.empty()) header(r.section);
		view.push_back({false, i, ""});
	}
	return view;
}

bool LongName(const ControlInfo &r)
{
	int chars = 0;
	for (unsigned char c : r.name) chars += (c & 0xC0) != 0x80;
	return chars > 40;
}

int RowHeight(const ControlInfo &r)
{
	return r.hint.empty() && !LongName(r) ? kRowH : kHintRowH;
}

int ViewHeight(const State &st, const std::vector<Item> &view)
{
	int h = 0;
	for (const Item &it : view) h += it.header ? kHeaderH : RowHeight(st.rows[it.row]);
	return h;
}

int ChangedOnPage(const State &st, int page)
{
	int n = 0;
	for (const ControlInfo &r : st.rows) {
		if (r.page == page && !IsDefault(r)) n++;
	}
	return n;
}

void ResetGlyph(Painter &p, int cx, int cy, COLORREF c)
{
	const double kPi = 3.14159265358979;
	const float r = 6.5f * p.s, x = cx * p.s, y = cy * p.s;
	const double a0 = 100 * kPi / 180, a1 = 20 * kPi / 180;
	HPEN pen = CreatePen(PS_SOLID, std::max(1, p.S(2)), c);
	HGDIOBJ op = SelectObject(p.dc, pen), ob = SelectObject(p.dc, GetStockObject(NULL_BRUSH));
	SetArcDirection(p.dc, AD_COUNTERCLOCKWISE);
	const int ri = static_cast<int>(r + 0.5f), xi = static_cast<int>(x + 0.5f), yi = static_cast<int>(y + 0.5f);
	Arc(p.dc, xi - ri, yi - ri, xi + ri, yi + ri, xi + static_cast<int>(r * cos(a0)), yi - static_cast<int>(r * sin(a0)),
		xi + static_cast<int>(r * cos(a1)), yi - static_cast<int>(r * sin(a1)));
	SelectObject(p.dc, op);
	SelectObject(p.dc, ob);
	DeleteObject(pen);
	const float ex = x + r * static_cast<float>(cos(a1)), ey = y - r * static_cast<float>(sin(a1));
	const float tx = -static_cast<float>(sin(a1)), ty = -static_cast<float>(cos(a1));
	const float nx = static_cast<float>(cos(a1)), ny = -static_cast<float>(sin(a1));
	const float len = 4.0f * p.s, half = 3.5f * p.s;
	POINT pts[3] = {{static_cast<LONG>(ex + tx * len), static_cast<LONG>(ey + ty * len)},
		{static_cast<LONG>(ex + nx * half), static_cast<LONG>(ey + ny * half)},
		{static_cast<LONG>(ex - nx * half), static_cast<LONG>(ey - ny * half)}};
	HBRUSH b = CreateSolidBrush(c);
	HPEN pen2 = CreatePen(PS_SOLID, 1, c);
	op = SelectObject(p.dc, pen2);
	ob = SelectObject(p.dc, b);
	Polygon(p.dc, pts, 3);
	SelectObject(p.dc, op);
	SelectObject(p.dc, ob);
	DeleteObject(b);
	DeleteObject(pen2);
}

HFONT KeyFont(float s);

bool PageShown(const State &st, int page)
{
	if (page != kPageModOrders) return true;
	for (const ControlInfo &r : st.rows) {
		if (r.page == page) return true;
	}
	return false;
}

void DrawNav(Painter &p, const State &st, int mx, int my)
{
	int y = kTop;
	for (int page = 0; page < kPageCount; page++) {
		if (!PageShown(st, page)) continue;
		if (page == kPageCamera || page == kPageUnits) {
			if (page == kPageUnits) y += 10;
			p.Font(4);
			p.Text(kNavX + 10, y, kNavW - 20, kNavLabelH, page == kPageCamera ? "GENERAL" : "COMMANDS", RGB(150, 126, 84));
			y += kNavLabelH;
		}
		const bool sel = st.search.empty() && st.page == page;
		const bool hov = p.Hover(kNavX, y, kNavW, kNavItemH, mx, my);
		if (sel) {
			p.Fill(kNavX, y, kNavW, kNavItemH, pal::rowHover);
			p.Fill(kNavX, y, 3, kNavItemH, kGold);
		}
		else if (hov) {
			p.Fill(kNavX, y, kNavW, kNavItemH, pal::fieldOpen);
		}
		p.Font(sel ? 1 : 0);
		p.Text(kNavX + 14, y, kNavW - 50, kNavItemH, ControlsPageName(page), sel ? pal::focus : kText);
		const int changed = ChangedOnPage(st, page);
		if (changed) {
			p.Font(4);
			p.Text(kNavX + kNavW - 44, y, 34, kNavItemH, std::to_string(changed), kSpecial, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
		p.AddHit(kNavX, y, kNavW, kNavItemH, kNav, page);
		y += kNavItemH;
	}
}

struct Columns {
	int name, cell0, cell1, reset, right;
};

Columns Cols()
{
	Columns c;
	c.right = kListX + kListW - 18;
	c.reset = c.right - kResetW;
	c.cell1 = c.reset - 6 - kCellW;
	c.cell0 = c.cell1 - kCellGap - kCellW;
	c.name = kListX + 10;
	return c;
}

RECT g_captureCell = {};

unsigned char HeldMods()
{
	unsigned char m = 0;
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= kKeyCtrl;
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m |= kKeyShift;
	if (GetAsyncKeyState(VK_MENU) & 0x8000) m |= kKeyAlt;
	return m;
}

void DrawRow(Painter &p, const State &st, int row, int y, int listTop, int listBottom, int mx, int my)
{
	const ControlInfo &r = st.rows[row];
	const int rowH = RowHeight(r);
	if (y + rowH <= listTop || y >= listBottom) return;
	const Columns c = Cols();
	const bool off = st.grid && r.order;
	const bool active = !Asking(st);
	const bool inList = active && my >= p.oy + p.S(listTop) && my < p.oy + p.S(listBottom);
	const bool hov = inList && p.Hover(kListX, y, kListW - 12, rowH, mx, my);
	if (hov) p.Fill(kListX + 2, y, kListW - 16, rowH, RGB(40, 32, 22));
	if (st.capture.row == row) {
		p.Fill(kListX + 2, y, kListW - 16, rowH, RGB(48, 37, 22));
		p.Fill(kListX + 2, y, 3, rowH, kGold);
	}
	if (st.flashRow == row && GetTickCount() - st.flashAt < 1600) {
		p.Fill(kListX + 2, y, kListW - 16, rowH, RGB(84, 64, 30));
		p.Fill(kListX + 2, y, 3, rowH, kGold);
	}
	p.Fill(c.name, y + rowH - 1, c.right - c.name, 1, RGB(44, 34, 23));
	p.Font(0);
	const int nameMax = c.cell0 - c.name - 10;
	if (r.hint.empty() && LongName(r)) {
		p.TextWrapped(c.name, y + 3, nameMax, rowH - 4, r.name, off ? kDim : kText);
	}
	else if (r.hint.empty()) {
		p.Text(c.name, y, nameMax, rowH, r.name, off ? kDim : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	else {
		p.Text(c.name, y + 5, nameMax, 20, r.name, off ? kDim : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(c.name, y + 25, nameMax, 16, r.hint, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	auto hit = [&](int hx, int hy, int hw, int hh, int action, int arg) {
		const int top = std::max(hy, listTop), bottom = std::min(hy + hh, listBottom);
		if (bottom > top && inList) p.AddHit(hx, top, hw, bottom - top, action, arg);
	};
	if (off) {
		p.Font(3);
		p.Text(c.cell0, y, kCellW, rowH, "By position", RGB(130, 116, 92), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const float beat = 0.5f + 0.5f * static_cast<float>(sin((GetTickCount() % 1400) / 1400.0 * 6.2831853));
	auto mix = [](COLORREF a, COLORREF b, float t) {
		return RGB(static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
			static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
	};
	for (int slot = 0; slot < 2; slot++) {
		const int ch = kRowH - 4, cx = slot ? c.cell1 : c.cell0, cy = y + (rowH - ch) / 2;
		const ControlKey k = KeyOf(r, slot);
		const bool capturing = st.capture.row == row && st.capture.slot == slot;
		const bool chov = inList && p.Hover(cx, cy, kCellW, ch, mx, my);
		const bool clash = st.clash[row * 2 + slot].row >= 0;
		if (capturing) {
			const COLORREF edge = mix(RGB(150, 112, 50), RGB(255, 228, 150), beat);
			p.RoundRect(cx, cy, kCellW, ch, 8, edge);
			p.RoundRect(cx + 2, cy + 2, kCellW - 4, ch - 4, 6, RGB(56, 42, 22));
			const unsigned char held = HeldMods();
			if (held) {
				std::vector<std::string> caps;
				if (held & kKeyCtrl) caps.push_back("Ctrl");
				if (held & kKeyShift) caps.push_back("Shift");
				if (held & kKeyAlt) caps.push_back("Alt");
				p.Font(4);
				int total = 0;
				for (const std::string &t : caps) total += CapWidth(p, t) + 12;
				total += p.TextWidth("…");
				int kx = cx + (kCellW - total) / 2;
				for (const std::string &t : caps) {
					const int cw = CapWidth(p, t);
					DrawCap(p, kx, cy + 3, cw, ch - 6, t, 0, pal::edgeStrong, kSpecial);
					kx += cw;
					p.Text(kx, cy, 12, ch, "+", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
					kx += 12;
				}
				p.Text(kx, cy, 20, ch, "…", pal::focus, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
			else {
				p.Font(4);
				p.Text(cx, cy, kCellW, ch, "Press a key", mix(RGB(220, 196, 140), RGB(255, 240, 190), beat), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
			if (cy + ch > listTop && cy < listBottom) g_captureCell = {cx, cy, cx + kCellW, cy + ch};
		}
		else if (chov) {
			if (k.vk) {
				DrawKey(p, cx, cy - 1, kCellW, ch, k, clash ? kBad : RGB(255, 222, 140), clash ? kBad : RGB(255, 240, 196), RGB(84, 66, 40));
			}
			else {
				p.Font(4);
				p.Text(cx, cy, kCellW, ch, slot ? "+ Add" : "+ Set key", RGB(255, 226, 150), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
		}
		else if (!k.vk) {
			p.Font(3);
			p.Text(cx, cy, kCellW, ch, slot ? "—" : "No key", slot ? RGB(96, 84, 64) : RGB(140, 124, 96), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		else {
			const bool custom = slot == 1 || k != r.def;
			DrawKey(p, cx, cy, kCellW, ch, k, clash ? kBad : custom ? pal::edgeStrong : RGB(122, 98, 64),
				clash ? kBad : custom ? kSpecial : kText);
		}
		hit(cx, cy, kCellW, ch, kCell, row * 2 + slot);
	}
	if (!IsDefault(r)) {
		const int ry = y + (rowH - 26) / 2;
		const bool rh = inList && p.Hover(c.reset, ry, kResetW, 26, mx, my);
		if (rh) p.RoundRect(c.reset, ry, kResetW, 26, 6, RGB(60, 47, 32));
		ResetGlyph(p, c.reset + kResetW / 2, ry + 13, rh ? pal::focus : RGB(176, 140, 80));
		hit(c.reset, ry, kResetW, 26, kRowReset, row);
	}
}

struct KbKey {
	float x, y, w, h;
	unsigned short scan;
	unsigned short vk;
	const char *label;
	bool modifier;
	float x2 = 0, y2 = 0, w2 = 0, h2 = 0;
};

bool AnsiLayout()
{
	return HIWORD(reinterpret_cast<UINT_PTR>(ControlsLayout())) == 0x0409;
}

const std::vector<KbKey> &Keyboard()
{
	static std::vector<KbKey> k;
	static bool builtAnsi = false;
	const bool ansi = AnsiLayout();
	if (!k.empty() && builtAnsi == ansi) return k;
	k.clear();
	builtAnsi = ansi;
	auto add = [&](float x, float y, float w, float h, unsigned short scan, unsigned short vk, const char *label = nullptr, bool mod = false) {
		k.push_back({x, y, w, h, scan, vk, label, mod});
	};
	add(0, 0, 1, 1, 0, VK_ESCAPE);
	for (int i = 0; i < 12; i++) add(2 + i + (i >= 4 ? 0.5f : 0) + (i >= 8 ? 0.5f : 0), 0, 1, 1, 0, static_cast<unsigned short>(VK_F1 + i));
	add(15.5f, 0, 1, 1, 0, VK_SNAPSHOT, "Prt\nScr");
	add(16.5f, 0, 1, 1, 0, VK_SCROLL, "Scroll\nLock", true);
	add(17.5f, 0, 1, 1, 0, VK_PAUSE, "Pause");
	add(19, 0, 4.0f / 3, 1, 0, VK_MBUTTON, "Wheel");
	add(19 + 4.0f / 3, 0, 4.0f / 3, 1, 0, VK_XBUTTON1, "Mouse\n4");
	add(19 + 8.0f / 3, 0, 4.0f / 3, 1, 0, VK_XBUTTON2, "Mouse\n5");
	add(0, 1.5f, 1, 1, 0x29, 0);
	for (int i = 0; i < 12; i++) add(1.0f + i, 1.5f, 1, 1, static_cast<unsigned short>(0x02 + i), 0);
	add(13, 1.5f, 2, 1, 0, VK_BACK, "Backspace");
	add(15.5f, 1.5f, 1, 1, 0, VK_INSERT, "Insert");
	add(16.5f, 1.5f, 1, 1, 0, VK_HOME, "Home");
	add(17.5f, 1.5f, 1, 1, 0, VK_PRIOR, "Page\nUp");
	add(19, 1.5f, 1, 1, 0, VK_NUMLOCK, "Num\nLock", true);
	add(20, 1.5f, 1, 1, 0, VK_DIVIDE, "/");
	add(21, 1.5f, 1, 1, 0, VK_MULTIPLY, "*");
	add(22, 1.5f, 1, 1, 0, VK_SUBTRACT, "-");
	add(0, 2.5f, 1.5f, 1, 0, VK_TAB, "Tab");
	for (int i = 0; i < 12; i++) add(1.5f + i, 2.5f, 1, 1, static_cast<unsigned short>(0x10 + i), 0);
	if (ansi) {
		add(13.5f, 2.5f, 1.5f, 1, 0x2B, 0);
	}
	else {
		add(13.5f, 2.5f, 1.5f, 1, 0, VK_RETURN, "Enter");
		k.back().x2 = 13.75f;
		k.back().y2 = 3.5f;
		k.back().w2 = 1.25f;
		k.back().h2 = 1;
	}
	add(15.5f, 2.5f, 1, 1, 0, VK_DELETE, "Del");
	add(16.5f, 2.5f, 1, 1, 0, VK_END, "End");
	add(17.5f, 2.5f, 1, 1, 0, VK_NEXT, "Page\nDown");
	for (int i = 0; i < 3; i++) add(19.0f + i, 2.5f, 1, 1, 0, static_cast<unsigned short>(VK_NUMPAD7 + i));
	add(22, 2.5f, 1, 2, 0, VK_ADD, "+");
	add(0, 3.5f, 1.75f, 1, 0, VK_CAPITAL, "Caps\nLock", true);
	for (int i = 0; i < 11; i++) add(1.75f + i, 3.5f, 1, 1, static_cast<unsigned short>(0x1E + i), 0);
	if (ansi) add(12.75f, 3.5f, 2.25f, 1, 0, VK_RETURN, "Enter");
	else add(12.75f, 3.5f, 1, 1, 0x2B, 0);
	for (int i = 0; i < 3; i++) add(19.0f + i, 3.5f, 1, 1, 0, static_cast<unsigned short>(VK_NUMPAD4 + i));
	if (ansi) {
		add(0, 4.5f, 2.25f, 1, 0, VK_SHIFT, "Shift", true);
	}
	else {
		add(0, 4.5f, 1.25f, 1, 0, VK_SHIFT, "Shift", true);
		add(1.25f, 4.5f, 1, 1, 0x56, 0);
	}
	for (int i = 0; i < 10; i++) add(2.25f + i, 4.5f, 1, 1, static_cast<unsigned short>(0x2C + i), 0);
	add(12.25f, 4.5f, 2.75f, 1, 0, VK_SHIFT, "Shift", true);
	add(16.5f, 4.5f, 1, 1, 0, VK_UP);
	for (int i = 0; i < 3; i++) add(19.0f + i, 4.5f, 1, 1, 0, static_cast<unsigned short>(VK_NUMPAD1 + i));
	add(22, 4.5f, 1, 2, 0, VK_RETURN, "Enter");
	add(0, 5.5f, 1.25f, 1, 0, VK_CONTROL, "Ctrl", true);
	add(1.25f, 5.5f, 1.25f, 1, 0, VK_LWIN, "Win", true);
	add(2.5f, 5.5f, 1.25f, 1, 0, VK_MENU, "Alt", true);
	add(3.75f, 5.5f, 6.25f, 1, 0, VK_SPACE, "Space");
	add(10, 5.5f, 1.25f, 1, 0, VK_RMENU, "AltGr", true);
	add(11.25f, 5.5f, 1.25f, 1, 0, VK_RWIN, "Win", true);
	add(12.5f, 5.5f, 1.25f, 1, 0, VK_APPS, "Menu", true);
	add(13.75f, 5.5f, 1.25f, 1, 0, VK_CONTROL, "Ctrl", true);
	add(15.5f, 5.5f, 1, 1, 0, VK_LEFT);
	add(16.5f, 5.5f, 1, 1, 0, VK_DOWN);
	add(17.5f, 5.5f, 1, 1, 0, VK_RIGHT);
	add(19, 5.5f, 2, 1, 0, VK_NUMPAD0);
	add(21, 5.5f, 1, 1, 0, VK_DECIMAL, ".");
	return k;
}

unsigned short KbVk(const KbKey &key)
{
	if (key.vk) return key.vk;
	return static_cast<unsigned short>(MapVirtualKeyExW(key.scan, MAPVK_VSC_TO_VK_EX, ControlsLayout()));
}

std::string KbLabel(const KbKey &key)
{
	if (key.label) return key.label;
	const unsigned short vk = KbVk(key);
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return std::string(1, static_cast<char>('0' + vk - VK_NUMPAD0));
	return IsArrow(vk) ? "" : ControlsKeyLabel({vk, 0});
}

struct KeyUse {
	std::vector<int> general, orders;
	bool clash = false;
	int gridPos = -1;
	bool Used() const { return !general.empty() || !orders.empty() || gridPos >= 0; }
};

KeyUse UseOf(const State &st, unsigned short vk, unsigned char mods)
{
	KeyUse u;
	if (!vk) return u;
	for (int i = 0; i < static_cast<int>(st.rows.size()); i++) {
		const ControlInfo &r = st.rows[i];
		if (r.order && st.grid) continue;
		for (int slot = 0; slot < 2; slot++) {
			const ControlKey k = KeyOf(r, slot);
			if (k.vk != vk || k.mods != mods) continue;
			std::vector<int> &list = r.order ? u.orders : u.general;
			if (list.empty() || list.back() != i) list.push_back(i);
			if (st.clash[i * 2 + slot].row >= 0) u.clash = true;
		}
	}
	if (st.grid && mods == 0 && vk >= 'A' && vk <= 'Z') {
		const std::string keys = ControlsGridKeys();
		const size_t at = keys.find(static_cast<char>(vk));
		if (at != std::string::npos) u.gridPos = static_cast<int>(at);
	}
	return u;
}

HFONT KeyFont(float s)
{
	static HFONT font = nullptr;
	static float scale = 0;
	if (font && scale == s) return font;
	if (font) DeleteObject(font);
	font = CreateFontW(-static_cast<int>(10 * s + 0.5f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	scale = s;
	return font;
}

struct KbOut {
	RECT box = {};
	int key = -1;
	ListGeom geom;
};

void DrawKeyboardView(Painter &p, const State &st, int mx, int my, int areaBottom, KbOut &out)
{
	const int x0 = 16, w0 = kW - 32, y0 = kTop;
	const char *layers[] = {"No modifier", "Ctrl", "Shift", "Alt"};
	const int layerMods[] = {0, kKeyCtrl, kKeyShift, kKeyAlt};
	const int lw[] = {150, 70, 80, 70};
	int lx = x0 + w0 - 10 - (150 + 70 + 80 + 70);
	for (int i = 0; i < 4; i++) {
		p.Button(lx, y0, lw[i], 28, layers[i], kKbLayer, layerMods[i], mx, my, true, st.layer == layerMods[i], 4);
		lx += lw[i];
	}
	struct Swatch {
		COLORREF face, edge;
		const char *text;
	};
	const Swatch kLegend[] = {{RGB(118, 86, 36), pal::edgeStrong, "Action"}, {RGB(62, 70, 44), RGB(120, 136, 84), "Commands"},
		{RGB(44, 35, 24), RGB(90, 72, 50), "Free"}, {RGB(44, 35, 24), kBad, "Clash"}};
	int gx = x0 + 10;
	p.Font(3);
	for (const Swatch &sw : kLegend) {
		p.RoundRect(gx, y0 + 7, 16, 14, 4, sw.edge);
		p.RoundRect(gx + 1, y0 + 8, 14, 12, 3, sw.face);
		p.Text(gx + 22, y0, 80, 28, sw.text, kDim);
		gx += 22 + p.TextWidth(sw.text) + 18;
	}

	const std::vector<KbKey> &keys = Keyboard();
	const int u = 36;
	const int kx = x0 + (w0 - 23 * u) / 2, ky = y0 + 42;
	int hover = -1;
	for (int i = 0; i < static_cast<int>(keys.size()); i++) {
		const KbKey &k = keys[i];
		const int x = kx + static_cast<int>(k.x * u), y = ky + static_cast<int>(k.y * u);
		const int w = static_cast<int>(k.w * u) - 4, h = static_cast<int>(k.h * u) - 4;
		const bool hov = !k.modifier && (p.Hover(x, y, w, h, mx, my) ||
			(k.w2 > 0 && p.Hover(kx + static_cast<int>(k.x2 * u), ky + static_cast<int>(k.y2 * u), static_cast<int>(k.w2 * u) - 4, static_cast<int>(k.h2 * u) - 4, mx, my)));
		if (hov) hover = i;
		const unsigned short vk = KbVk(k);
		const KeyUse use = k.modifier ? KeyUse() : UseOf(st, vk, static_cast<unsigned char>(st.layer));
		bool other = false;
		for (int m : layerMods) {
			if (st.layer == 0 && m != 0 && !k.modifier && UseOf(st, vk, static_cast<unsigned char>(m)).Used()) other = true;
		}
		COLORREF face = RGB(44, 35, 24), edge = RGB(90, 72, 50), fg = RGB(150, 136, 110);
		if (k.modifier) {
			face = pal::dialog;
			edge = RGB(60, 48, 34);
			fg = RGB(104, 92, 72);
		}
		else if (!use.general.empty()) {
			face = RGB(118, 86, 36);
			edge = pal::edgeStrong;
			fg = pal::focus;
		}
		else if (!use.orders.empty() || use.gridPos >= 0) {
			face = RGB(62, 70, 44);
			edge = RGB(120, 136, 84);
			fg = RGB(226, 232, 190);
		}
		if (use.clash) edge = kBad;
		const bool sel = st.kbSel == i;
		if (sel) edge = pal::focus;
		if (hov && !sel) face = RGB(GetRValue(face) + 22, GetGValue(face) + 18, GetBValue(face) + 12);
		if (sel) {
			p.RoundRect(x - 3, y - 3, w + 6, h + 6, 9, kGold);
			p.RoundRect(x - 1, y - 1, w + 2, h + 2, 7, pal::panel);
		}
		if (k.w2 > 0) {
			const int x2 = kx + static_cast<int>(k.x2 * u), y2 = ky + static_cast<int>(k.y2 * u);
			const int w2 = static_cast<int>(k.w2 * u) - 4, h2 = static_cast<int>(k.h2 * u) - 4;
			p.RoundRect(x, y + 2, w, h, 6, RGB(12, 9, 6));
			p.RoundRect(x2, y2 + 2, w2, h2, 6, RGB(12, 9, 6));
			p.RoundRect(x, y, w, h, 6, edge);
			p.RoundRect(x2, y2 - 8, w2, h2 + 8, 6, edge);
			p.RoundRect(x + 1, y + 1, w - 2, h - 2, 5, face);
			p.RoundRect(x2 + 1, y2 - 7, w2 - 2, h2 + 6, 5, face);
			p.Fill(x2 + 1, y + h - 8, w2 - 2, 10, face);
		}
		else {
			p.RoundRect(x, y + 2, w, h, 6, RGB(12, 9, 6));
			p.RoundRect(x, y, w, h, 6, edge);
			p.RoundRect(x + 1, y + 1, w - 2, h - 2, 5, face);
		}
		const std::string label = KbLabel(k);
		if (label.empty() && IsArrow(vk)) {
			ArrowGlyph(p, x + w / 2, y + h / 2, vk, fg);
		}
		else if (label.find('\n') != std::string::npos) {
			SelectObject(p.dc, KeyFont(p.s));
			const size_t nl = label.find('\n');
			p.Text(x, y + 3, w, h / 2 - 2, label.substr(0, nl), fg, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
			p.Text(x, y + h / 2 - 1, w, h / 2 - 2, label.substr(nl + 1), fg, DT_CENTER | DT_TOP | DT_SINGLELINE);
		}
		else {
			if (label.size() <= 2) p.Font(4);
			else SelectObject(p.dc, KeyFont(p.s));
			p.Text(x, y, w, h, label, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if (other) p.Ellipse(x + w - 9, y + 4, 5, 5, kSpecial);
		if (!k.modifier) {
			p.AddHit(x, y, w, h, kKbKey, i);
			if (k.w2 > 0) p.AddHit(kx + static_cast<int>(k.x2 * u), ky + static_cast<int>(k.y2 * u), static_cast<int>(k.w2 * u) - 4, static_cast<int>(k.h2 * u) - 4, kKbKey, i);
		}
	}

	const int bx = x0 + 10, bw = w0 - 20, dy = ky + static_cast<int>(6.5f * u) + 8, dh = areaBottom - dy;
	p.Fill(bx, dy, bw, dh, RGB(34, 27, 18));
	p.Frame(bx, dy, bw, dh, RGB(80, 62, 38));
	static int lastHover = -1;
	const bool inBox = p.Hover(bx, dy, bw, dh, mx, my);
	if (hover >= 0) lastHover = hover;
	int shown = st.kbSel >= 0 ? st.kbSel : hover >= 0 ? hover : inBox && lastHover >= 0 ? lastHover : -1;
	if (shown >= static_cast<int>(keys.size())) shown = -1;
	out.box = {p.ox + p.S(bx), p.oy + p.S(dy), p.ox + p.S(bx + bw), p.oy + p.S(dy + dh)};
	out.key = shown;
	const int tx = bx + 16, tw = bw - 32;
	if (shown < 0) {
		p.Font(3);
		p.TextWrapped(tx, dy + 14, tw, dh - 20,
			"Hover over a key to see what it does, or click it to pin it. The dot in the corner means it also "
			"does something with Ctrl, Shift or Alt. Click any of its uses to jump to it in the list.", kDim);
		return;
	}
	const KbKey &k = keys[shown];
	const unsigned short vk = KbVk(k);
	const KeyUse use = UseOf(st, vk, static_cast<unsigned char>(st.layer));
	p.Font(1);
	const std::string title = ControlsKeyLabel({vk, static_cast<unsigned char>(st.layer)});
	p.Text(tx, dy + 8, tw, 26, title, kGold);
	const int titleW = p.TextWidth(title);
	std::string summary;
	if (!use.general.empty()) summary += std::to_string(use.general.size()) + (use.general.size() == 1 ? Tr(" action") : Tr(" actions"));
	if (!use.orders.empty()) summary += std::string(summary.empty() ? "" : "  ·  ") + std::to_string(use.orders.size()) + (use.orders.size() == 1 ? Tr(" command") : Tr(" commands"));
	if (use.gridPos >= 0) summary += std::string(summary.empty() ? "" : "  ·  ") + Tr("button ") + std::to_string(use.gridPos + 1) + Tr(" on the bar");
	p.Font(3);
	p.Text(tx + titleW + 14, dy + 8, tw - titleW - 14, 26, summary, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (!use.Used()) {
		p.Text(tx, dy + 40, tw, 20, "Free", kDim);
		return;
	}
	struct Item {
		std::string name, where;
		int row;
		COLORREF color;
	};
	std::vector<Item> items;
	for (int row : use.general) {
		const ControlInfo &r = st.rows[row];
		const bool clash = st.clash[row * 2].row >= 0 || st.clash[row * 2 + 1].row >= 0;
		items.push_back({r.name, ControlsPageName(r.page), row, clash ? kBad : kText});
	}
	for (int row : use.orders) {
		const ControlInfo &r = st.rows[row];
		items.push_back({r.name, r.section.empty() ? ControlsPageName(r.page) : r.section, row, RGB(226, 232, 190)});
	}
	const int top = dy + 40, viewH = dy + dh - 8 - top, itemH = 26, colGap = 20;
	const int colW = (tw - 14 - colGap) / 2;
	const int rows = static_cast<int>((items.size() + 1) / 2), contentH = rows * itemH;
	const int maxScroll = std::max(0, contentH - viewH);
	const int scroll = st.kbScrollKey == shown ? std::max(0, std::min(st.kbList.scroll, maxScroll)) : 0;
	p.Clip(tx - 6, top, tw + 12, viewH);
	for (size_t i = 0; i < items.size(); i++) {
		const Item &it = items[i];
		const int ix = tx + static_cast<int>(i % 2) * (colW + colGap), iy = top + static_cast<int>(i / 2) * itemH - scroll;
		if (iy + itemH <= top || iy >= top + viewH) continue;
		const bool hov = p.Hover(ix - 6, std::max(iy, top), colW + 12, std::min(iy + itemH, top + viewH) - std::max(iy, top), mx, my);
		if (hov) p.Fill(ix - 6, iy, colW + 12, itemH, pal::rowHover);
		p.Font(0);
		const int nameW = std::min(colW - 40, p.TextWidth(it.name));
		p.Text(ix, iy, colW - 40 > 0 ? nameW : colW, itemH, it.name, hov ? pal::focus : it.color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(ix + nameW + 10, iy + 1, colW - nameW - 10, itemH, it.where, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		const int ht = std::max(iy, top), hb = std::min(iy + itemH, top + viewH);
		if (hb > ht) p.AddHit(ix - 6, ht, colW + 12, hb - ht, kKbGo, it.row);
	}
	p.Unclip();
	ListState bar = st.kbList;
	bar.scroll = scroll;
	ScrollbarPaint(p, bx + bw - 12, top, viewH, contentH, viewH, bar, out.geom, mx, my);
}

void GoToRow(State &st, int row)
{
	if (row < 0 || row >= static_cast<int>(st.rows.size())) return;
	st.view = 0;
	st.search.clear();
	st.edit = EditState();
	st.page = st.rows[row].page;
	int y = 0;
	for (const Item &it : BuildView(st)) {
		if (!it.header && it.row == row) break;
		y += it.header ? kHeaderH : RowHeight(st.rows[it.row]);
	}
	st.list.scroll = std::max(0, y - 80);
	st.flashRow = row;
	st.flashAt = GetTickCount();
}

}

void ControlsUiOpen()
{
	ControlsNoteLayout();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s = State();
	g_s.open = true;
	Refresh(g_s);
	LogD("controls", "screen open (%d rows)", static_cast<int>(g_s.rows.size()));
}

bool ControlsUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.open;
}

void ControlsUiDraw(uint32_t *buf, int w, int h)
{
	State st;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.open) return;
		st = g_s;
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kW, kH)) return;
	r.Focus(st.tabAction, st.tabArg);
	Painter &p = r.p;
	std::vector<Hit> &hits = r.hits;
	const int ox = r.ox, oy = r.oy, pw = r.pw, ph = r.ph;
	const float s = r.s;
	const int mx = st.mx, my = st.my;

	PaintWindowFrame(p, kW, kH);
	PaintTitle(p, 24, 20, 300, "Controls");
	const int sx = kW - 24 - 280;
	p.Button(sx - 12 - 180, 22, 90, 30, "List", kViewList, 0, mx, my, true, st.view == 0, 4);
	p.Button(sx - 12 - 90, 22, 90, 30, "Keyboard", kViewKeys, 0, mx, my, true, st.view == 1, 4);
	PaintEditField(p, sx, 22, 280, 30, st.search, "Search action or key", st.searchFocus, false, st.edit, kSearchAction);
	if (!st.search.empty()) {
		const bool hov = p.Hover(sx + 280 - 28, 22, 28, 30, mx, my);
		p.Font(1);
		p.Text(sx + 280 - 28, 22, 28, 30, "×", hov ? pal::focus : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.AddHit(sx + 280 - 28, 22, 28, 30, kClearSearch);
	}
	p.Fill(16, kTop - 10, kW - 32, 1, pal::lineDim);

	g_captureCell = {};
	const bool keyboard = st.view == 1;
	int maxScroll = 0;
	ListGeom listGeom;
	const int listBottom = kH - kFooterH - 8;
	int listTop = kTop;
	KbOut kb;
	if (keyboard) DrawKeyboardView(p, st, mx, my, listBottom, kb);
	if (!keyboard) {
	DrawNav(p, st, mx, my);
	p.Fill(kListX - 8, kTop, 1, listBottom - kTop, RGB(56, 43, 28));

	const bool ordersBanner = st.search.empty() && ControlsPageIsOrders(st.page);
	const bool boxBanner = st.search.empty() && st.page == kPageMod;
	const bool banner = ordersBanner || boxBanner;
	if (boxBanner) {
		const int bx = kListX + 8, bw = kListW - 16, by = kTop, bh = kBannerH - 10;
		p.Fill(bx, by, bw, bh, RGB(36, 28, 19));
		p.Frame(bx, by, bw, bh, pal::divider);
		p.Font(1);
		p.Text(bx + 14, by + 8, bw - 28, 28, "Box selection on the big map (M)", kText);
		static const char *const kBoxLabels[kZoomBoxModes] = {"Ctrl + drag", "Alt + drag", "Shift + drag", "Off"};
		const int segW = 120, segX = bx + 14;
		for (int i = 0; i < kZoomBoxModes; i++) {
			p.Button(segX + i * segW, by + 46, segW, 28, kBoxLabels[i], kBoxMode, i, mx, my, true, st.boxMode == i, 4);
		}
		p.Font(3);
		p.TextWrapped(bx + 14, by + 82, bw - 28, 34,
			"Hold the key and drag over the big map to select the troops and heroes inside the box; hold the other one of Shift and Ctrl to add to your "
			"selection. Without the key, a click takes you to that place, as in the original game.",
			kDim);
	}
	if (ordersBanner) {
		const int bx = kListX + 8, bw = kListW - 16, by = kTop, bh = kBannerH - 10;
		p.Fill(bx, by, bw, bh, RGB(36, 28, 19));
		p.Frame(bx, by, bw, bh, pal::divider);
		p.Font(1);
		p.Text(bx + 14, by + 8, 220, 28, "Command keys", kText);
		const int seg1 = 110, seg2 = 190, segX = bx + bw - 14 - seg1 - seg2;
		p.Button(segX, by + 8, seg1, 28, "By letter", kModeLetter, 0, mx, my, true, !st.grid, 4);
		p.Button(segX + seg1, by + 8, seg2, 28, "By position on the bar", kModePosition, 0, mx, my, true, st.grid, 4);
		const int bs = 36, gap = 8, exW = 5 * bs + 4 * gap, exX = bx + bw - 18 - exW, exY = by + 50;
		const char *letters = st.grid ? "QWERT" : "SMAPD";
		for (int i = 0; i < 5; i++) {
			const int x = exX + i * (bs + gap);
			p.RoundRect(x, exY, bs, bs, 6, RGB(96, 76, 46));
			p.RoundRect(x + 1, exY + 1, bs - 2, bs - 2, 5, RGB(58, 46, 30));
			p.Fill(x + 10, exY + 14, bs - 18, bs - 20, RGB(84, 68, 44));
			p.Fill(x + 2, exY + 2, 15, 15, pal::edgeStrong);
			p.Fill(x + 3, exY + 3, 13, 13, RGB(24, 18, 12));
			SelectObject(p.dc, KeyFont(p.s));
			p.Text(x + 3, exY + 3, 13, 13, std::string(1, letters[i]), RGB(255, 232, 160), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		p.Font(3);
		if (st.grid) {
			for (int i = 0; i < 5; i++) {
				p.Text(exX + i * (bs + gap), exY + bs + 4, bs, 18, std::to_string(i + 1) + ".º", RGB(140, 124, 96), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
		}
		const int tx = bx + 14, tw = exX - 20 - tx;
		p.TextWrapped(tx, by + 46, tw, 42,
			st.grid ? "The key follows the button's position: Q for the first, W for the second… The same for any unit or building."
					: "Each command has its own letter (M move, A attack…): the game's or the one you set here. They change depending on the unit or building.",
			kDim);
		const int tgY = by + bh - 32, tgW = 240;
		const bool tgHov = p.Hover(tx, tgY, tgW, 22, mx, my);
		const int sw = Painter::kToggleW, swY = tgY + (22 - Painter::kToggleH) / 2;
		p.Toggle(tx, swY, st.barLetters, true, tgHov);
		p.Text(tx + sw + 8, tgY, tgW - sw - 8, 22, "Show the letter on each button", tgHov ? pal::focus : st.barLetters ? kText : kDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.AddHit(tx, tgY, tgW, 22, kBarLetters);
	}

	const Columns cols = Cols();
	const int colsY = kTop + (banner ? kBannerH : 0);
	p.Font(4);
	p.Text(cols.name, colsY, 200, kColsH, "Action", RGB(150, 126, 84));
	p.Text(cols.cell0, colsY, kCellW, kColsH, "Key", RGB(150, 126, 84), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Text(cols.cell1, colsY, kCellW, kColsH, "Alternative", RGB(150, 126, 84), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	const std::vector<Item> view = BuildView(st);
	listTop = colsY + kColsH;
	const int listH = listBottom - listTop;
	const int contentH = ViewHeight(st, view);
	maxScroll = std::max(0, contentH - listH);
	const int scroll = std::max(0, std::min(st.list.scroll, maxScroll));
	p.Clip(kListX, listTop, kListW, listH);
	int y = listTop - scroll;
	for (const Item &it : view) {
		if (it.header) {
			if (y + kHeaderH > listTop && y < listBottom) {
				p.Font(4);
				p.Text(kListX + 10, y + 6, kListW - 40, kHeaderH - 6, it.label, kGold);
				p.Fill(kListX + 10, y + kHeaderH - 2, kListW - 28, 1, pal::divider);
			}
			y += kHeaderH;
		}
		else {
			DrawRow(p, st, it.row, y, listTop, listBottom, mx, my);
			y += RowHeight(st.rows[it.row]);
		}
	}
	if (view.empty()) {
		p.Font(0);
		p.Text(kListX + 10, listTop + 20, kListW - 20, 30,
			st.search.empty() ? std::string("The game's commands could not be read (see the log in Logs\\imperivm).") : Tr("Nothing matches «") + st.search + "».", kDim);
	}
	p.Unclip();
	{
		ListState bar = st.list;
		bar.scroll = scroll;
		ScrollbarPaint(p, kListX + kListW - 8, listTop, listH, contentH, listH, bar, listGeom, mx, my);
	}

	if (st.pend.row >= 0 && st.pend.row < static_cast<int>(st.rows.size()) && !st.pendWith.empty()) {
		const ControlInfo &r = st.rows[st.pend.row];
		const ControlInfo &o = st.rows[st.pendWith[0].row];
		const int cx = kListX + 8, cw = kListW - 24, chh = 78, cyy = listBottom - chh - 8;
		p.Fill(cx + 4, cyy + 4, cw, chh, RGB(10, 8, 5));
		p.Fill(cx, cyy, cw, chh, RGB(40, 31, 20));
		p.Frame(cx, cyy, cw, chh, kGold);
		std::string text = ControlsKeyLabel(st.pendKey) + Tr(" already belongs to «") + o.name + "» (" + Where(o) + ")";
		if (st.pendWith.size() > 1) text += Tr(" and ") + std::to_string(st.pendWith.size() - 1) + Tr(" more");
		text += ".";
		p.Font(0);
		p.Text(cx + 14, cyy + 8, cw - 28, 24, text, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(cx + 14, cyy + 30, cw - 28, 18, std::string("For «") + r.name + "»" + (st.pend.slot ? Tr(" as an alternative.") : "."), kDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		int bx = cx + cw - 14;
		const int by = cyy + chh - 36, bh = 28;
		bx -= 100;
		p.Button(bx, by, 100, bh, "Cancel", kCancelPending, 0, mx, my, true, false, 4);
		bx -= 158;
		p.Button(bx, by, 150, bh, "Use on both", kAssignAnyway, 0, mx, my, true, false, 4);
		if (CanSwap(st)) {
			bx -= 158;
			p.Button(bx, by, 150, bh, "Swap keys", kSwap, 0, mx, my, true, true, 4);
		}
	}

	}

	const int fy = kH - kFooterH;
	p.Fill(16, fy + 2, kW - 32, 1, pal::lineDim);
	p.Button(24, fy + 12, 96, 34, "Export", kCopyConfig, 0, mx, my, true, false, 4);
	p.Button(24 + 96 + 8, fy + 12, 96, 34, "Import", kPasteConfig, 0, mx, my, true, false, 4);
	p.Button(24 + (96 + 8) * 2, fy + 12, 110, 34, "Reset all", kResetAll, 0, mx, my, true, false, 4);
	p.Button(kW - 24 - 130, fy + 12, 130, 34, "Close", kClose, 0, mx, my, true, true);
	const bool canUndo = !st.undo.empty() && GetTickCount() - st.undoAt < kUndoMs;
	const int undoX = kW - 24 - 130 - 12 - 110;
	if (canUndo) p.Button(undoX, fy + 12, 110, 34, "Undo", kUndo, 0, mx, my, true, false, 4);
	std::string status;
	COLORREF statusColor = kText;
	if (st.capture.row < 0 && !Asking(st)) {
		const POINT pt = {mx, my};
		for (const Hit &hh : hits) {
			if (!PtInRect(&hh.r, pt)) continue;
			if (hh.action == kCell && hh.arg >= 0 && hh.arg < static_cast<int>(st.clash.size()) && st.clash[hh.arg].row >= 0) {
				const ControlInfo &o = st.rows[st.clash[hh.arg].row];
				status = Tr("Clashes with «") + o.name + "» (" + Where(o) + Tr("): only one of the two will work.");
				statusColor = kBad;
			}
			else if (hh.action == kCell && hh.arg >= 0 && hh.arg / 2 < static_cast<int>(st.rows.size()) && KeyOf(st.rows[hh.arg / 2], hh.arg % 2).vk) {
				status = "Click: change  ·  Right-click: remove";
			}
			else if (hh.action == kCopyConfig) {
				status = "Copies your controls as text so you can pass them to another player.";
			}
			else if (hh.action == kPasteConfig) {
				status = "Loads the controls you copied from another player.";
			}
			else if (hh.action == kResetAll) {
				status = "Puts every key back as in the original game.";
			}
			else if (hh.action == kRowReset && hh.arg >= 0 && hh.arg < static_cast<int>(st.rows.size())) {
				status = Tr("Back to the game's key: ") + KeyText(st.rows[hh.arg].def) + ".";
			}
		}
	}
	if (st.confirmResetAll) {
		const int cw = 560, chh = 78, cx = (kW - cw) / 2, cyy = fy - chh - 8;
		p.Fill(cx + 4, cyy + 4, cw, chh, RGB(10, 8, 5));
		p.Fill(cx, cyy, cw, chh, RGB(40, 31, 20));
		p.Frame(cx, cyy, cw, chh, kGold);
		p.Font(0);
		p.Text(cx + 14, cyy + 8, cw - 28, 24, "Put every key back as in the original game?", kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(cx + 14, cyy + 30, cw - 28, 18, "Your keys and command modes go back to the game's. You can undo it for a few seconds.", kDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		int bx = cx + cw - 14;
		const int by = cyy + chh - 36, bh = 28;
		bx -= 100;
		p.Button(bx, by, 100, bh, "Cancel", kCancelPending, 0, mx, my, true, false, 4);
		bx -= 158;
		p.Button(bx, by, 150, bh, "Reset all", kResetAllYes, 0, mx, my, true, true, 4);
	}

	if (!st.status.empty() && GetTickCount() - st.statusAt < 5000) {
		p.Font(3);
		const int maxW = 600;
		const int tw = std::min(maxW - 40, p.TextWidth(st.status));
		const int th = p.Measure(tw, st.status);
		const int bw = tw + 40, bh = th + 20, bx = (kW - bw) / 2, by = fy - 8 - 12 - bh;
		p.Fill(bx, by, bw, bh, RGB(26, 21, 15));
		p.Frame(bx, by, bw, bh, st.statusBad ? kBad : pal::edge);
		p.TextWrapped(bx + 20, by + 10, tw + 4, th + 2, st.status, st.statusBad ? RGB(255, 190, 170) : kText);
	}
	{
		static int lastAction = -1, lastArg = 0;
		static DWORD since = 0;
		int action = -1, arg = 0;
		const POINT pt = {mx, my};
		for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
			if (PtInRect(&it->r, pt)) {
				action = it->action;
				arg = it->arg;
				break;
			}
		}
		if (action != lastAction || arg != lastArg) {
			lastAction = action;
			lastArg = arg;
			since = GetTickCount();
		}
		if (GetTickCount() - since < 500) status.clear();
	}
	if (!status.empty()) {
		p.Font(3);
		const int tw = std::min(300, p.TextWidth(status));
		const int th = p.Measure(tw, status);
		const int bw = tw + 20, bh = th + 12;
		int bx = static_cast<int>((mx - ox) / s) + 14, by = static_cast<int>((my - oy) / s) + 20;
		bx = std::min(bx, kW - bw - 8);
		if (by + bh > kH - 8) by = static_cast<int>((my - oy) / s) - bh - 8;
		p.Fill(bx, by, bw, bh, RGB(26, 21, 15));
		p.Frame(bx, by, bw, bh, statusColor == kBad ? kBad : pal::edge);
		p.TextWrapped(bx + 10, by + 6, tw + 4, th + 2, status, statusColor == kBad ? RGB(255, 190, 170) : kText);
	}
	if (st.capture.row >= 0 && g_captureCell.right > g_captureCell.left) {
		const std::string tip = "Key or mouse button  ·  Esc cancels";
		p.Font(3);
		const int bw = p.TextWidth(tip) + 24, bh = 26;
		const int right = g_captureCell.right;
		const int bx = std::min(right, kW - 12) - bw;
		const bool below = g_captureCell.bottom + 8 + bh < kH - kFooterH - 8;
		const int by = below ? g_captureCell.bottom + 8 : g_captureCell.top - 8 - bh;
		p.Fill(bx, by, bw, bh, RGB(26, 21, 15));
		p.Frame(bx, by, bw, bh, pal::edge);
		p.Text(bx, by, bw, bh, tip, pal::focus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(hits);
	g_s.panel = {ox, oy, ox + pw, oy + ph};
	g_s.listGeom = listGeom;
	if (g_s.list.scroll > maxScroll) g_s.list.scroll = maxScroll;
	g_s.kbBox = kb.box;
	g_s.kbGeom = kb.geom;
	if (g_s.kbScrollKey != kb.key) {
		g_s.kbScrollKey = kb.key;
		g_s.kbList.scroll = 0;
	}
}

namespace {

int HitAt(const State &st, POINT pt, int &arg)
{
	for (auto it = st.hits.rbegin(); it != st.hits.rend(); ++it) {
		if (PtInRect(&it->r, pt)) {
			arg = it->arg;
			return it->action;
		}
	}
	arg = 0;
	return kNone;
}

unsigned char Mods()
{
	unsigned char m = 0;
	if (GetKeyState(VK_CONTROL) & 0x8000) m |= kKeyCtrl;
	if (GetKeyState(VK_SHIFT) & 0x8000) m |= kKeyShift;
	if (GetKeyState(VK_MENU) & 0x8000) m |= kKeyAlt;
	return m;
}

bool IsModifier(WPARAM vk)
{
	return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
		vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

Ref CellRef(int arg)
{
	return {arg / 2, arg % 2};
}

void SetGrid(State &st, bool on)
{
	if (on == st.grid) return;
	BeginChange(st);
	const std::string moved = ControlsSetGridMode(on);
	st.capture = Ref();
	Refresh(st);
	if (!on) {
		SetStatus(st, "Commands by letter.", false);
		return;
	}
	std::string text = "Commands by position: Q is the bar's first button.";
	if (!moved.empty()) {
		std::string m = moved;
		m[0] = static_cast<char>(toupper(static_cast<unsigned char>(m[0])));
		text += " " + m + ".";
	}
	const std::string keys = ControlsGridKeys();
	for (const ControlInfo &r : st.rows) {
		if (r.order || r.context != 0) continue;
		for (int s = 0; s < 2; s++) {
			const ControlKey k = KeyOf(r, s);
			if (!k.vk || k.mods || keys.find(static_cast<char>(k.vk)) == std::string::npos) continue;
			if (r.withOrders && k == r.def) continue;
			text += Tr(" Heads up: the ") + ControlsKeyLabel(k) + Tr(" is «") + r.name + "».";
			SetStatus(st, text, false);
			return;
		}
	}
	SetStatus(st, text, false);
}

void Activate(State &st, int action, int arg, POINT pt)
{
	if (action == kSearchAction) action = kSearchField;
	if (action == kCopyConfig || action == kPasteConfig) {
		if (Asking(st)) return;
		if (action == kCopyConfig) {
			const std::string text = ControlsExport();
			if (text.empty()) {
				SetStatus(st, "You have the game's keys: there's nothing to export.", false);
			}
			else {
				ClipboardSet(text);
				SetStatus(st, "Controls exported: they're copied, ready to paste in Discord.", false);
			}
		}
		else {
			const std::string text = ClipboardText();
			const std::string undo = ControlsSnapshot();
			if (!ControlsImport(text)) {
				SetStatus(st, "To import, first copy the controls someone sent you.", true);
			}
			else {
				st.undo = undo;
				st.undoAt = GetTickCount();
				Refresh(st);
				SetStatus(st, "Controls imported. If you don't like them, Undo.", false);
			}
		}
		return;
	}
	if (st.pend.row >= 0 && action != kSwap && action != kAssignAnyway && action != kCancelPending) return;
	if (st.confirmResetAll && action != kResetAllYes && action != kCancelPending) return;
	if (action != kSearchField) st.searchFocus = false;
	const bool sameCell = action == kCell && CellRef(arg).row == st.capture.row && CellRef(arg).slot == st.capture.slot;
	if (st.capture.row >= 0 && !sameCell) st.capture = Ref();
	switch (action) {
	case kClose:
		st.open = false;
		break;
	case kNav:
		st.page = arg;
		st.list.scroll = 0;
		st.search.clear();
		st.edit = EditState();
		break;
	case kCell:
		st.capture = sameCell ? Ref() : CellRef(arg);
		st.status.clear();
		break;
	case kRowReset:
		if (arg >= 0 && arg < static_cast<int>(st.rows.size())) {
			const ControlInfo r = st.rows[arg];
			std::vector<Ref> with = ClashesFor(st, arg, 0, r.def);
			if (!with.empty() && r.def.vk) {
				st.pend = {arg, 0};
				st.pendKey = r.def;
				st.pendWith = with;
				st.pendReset = true;
				break;
			}
			BeginChange(st);
			ResetRow(r);
			Refresh(st);
			SetStatus(st, r.name + ": " + KeyText(r.def) + Tr(", as in the game."), false);
		}
		break;
	case kSearchField: {
		st.view = 0;
		st.searchFocus = true;
		FieldGeom geom;
		if (FieldGet(kSearchAction, geom)) st.edit.caret = st.edit.anchor = EditIndexAt(geom, st.search, pt.x);
		else EditToEnd(st.search, st.edit);
		break;
	}
	case kClearSearch:
		st.search.clear();
		st.edit = EditState();
		st.list.scroll = 0;
		break;
	case kSwap: {
		const Ref at = st.pend, other = st.pendWith[0];
		const ControlKey old = KeyOf(st.rows[at.row], at.slot), k = st.pendKey;
		const ControlInfo r = st.rows[at.row], o = st.rows[other.row];
		const bool reset = st.pendReset;
		st.pend = Ref();
		st.pendReset = false;
		BeginChange(st);
		SetRowKey(o, other.slot, old);
		if (o.order && other.slot == 0 && o.key2.vk && old.vk && o.key2.mods != old.mods) SetRowKey(o, 1, ControlKey());
		if (reset) ResetRow(r);
		else {
			SetRowKey(r, at.slot, k);
			if (r.order && at.slot == 0 && r.key2.vk && k.vk && r.key2.mods != k.mods) SetRowKey(r, 1, ControlKey());
		}
		Refresh(st);
		SetStatus(st, r.name + ": " + KeyText(k) + "  ·  " + o.name + ": " + KeyText(old), false);
		break;
	}
	case kAssignAnyway: {
		const Ref at = st.pend;
		st.pend = Ref();
		if (st.pendReset) {
			st.pendReset = false;
			const ControlInfo r = st.rows[at.row];
			BeginChange(st);
			ResetRow(r);
			Refresh(st);
			SetStatus(st, r.name + ": " + KeyText(r.def) + Tr(", as in the game."), false);
			break;
		}
		Commit(st, at, st.pendKey);
		break;
	}
	case kCancelPending:
		st.pend = Ref();
		st.pendReset = false;
		st.confirmResetAll = false;
		break;
	case kResetAll:
		st.confirmResetAll = true;
		break;
	case kResetAllYes:
		st.confirmResetAll = false;
		BeginChange(st);
		ControlsResetAll();
		Refresh(st);
		SetStatus(st, "Every key is back as in the original game. If you want yours back, Undo.", false);
		break;
	case kModeLetter:
	case kModePosition:
		SetGrid(st, action == kModePosition);
		break;
	case kViewList:
		st.view = 0;
		break;
	case kViewKeys:
		st.view = 1;
		st.capture = Ref();
		break;
	case kKbLayer:
		st.layer = arg;
		break;
	case kKbKey:
		st.kbSel = st.kbSel == arg ? -1 : arg;
		break;
	case kKbGo:
		GoToRow(st, arg);
		break;
	case kBoxMode:
		ControlsSetZoomBoxMode(arg);
		Refresh(st);
		SetStatus(st, st.boxMode == kZoomBoxOff ? "Box selection on the big map is off." : "Box selection on the big map changed.", false);
		break;
	case kBarLetters:
		ControlsSetBarLetters(!st.barLetters);
		Refresh(st);
		SetStatus(st, st.barLetters ? "The command bar will show each button's key." : "The command bar no longer shows keys.", false);
		break;
	case kUndo:
		if (!st.undo.empty()) {
			ControlsRestore(st.undo);
			st.undo.clear();
			Refresh(st);
			SetStatus(st, "Change undone.", false);
		}
		break;
	default:
		break;
	}
}

void Click(State &st, POINT pt)
{
	int arg = 0;
	const int action = HitAt(st, pt, arg);
	Activate(st, action, arg, pt);
}

bool TabAllowed(const State &st, int a)
{
	if (a == kNone) return false;
	if (st.pend.row >= 0) return a == kSwap || a == kAssignAnyway || a == kCancelPending;
	if (st.confirmResetAll) return a == kResetAllYes || a == kCancelPending;
	return true;
}

void TabStep(State &st, bool back)
{
	FocusItem cur = {st.tabAction, st.tabArg};
	if (st.searchFocus) cur = {kSearchAction, 0};
	const FocusItem next = FocusStep(FocusOrder(st.hits, [&](int a) { return !TabAllowed(st, a); }), cur, back);
	if (next.action < 0) return;
	st.searchFocus = false;
	st.tabAction = next.action;
	st.tabArg = next.arg;
	if (next.action == kSearchAction) {
		st.searchFocus = true;
		st.tabAction = -1;
		EditSelectAll(st.search, st.edit);
	}
}

void MoveRow(State &st, int delta)
{
	const bool cell = st.tabAction == kCell;
	if (!cell && st.tabAction != kRowReset) return;
	const int row = cell ? st.tabArg / 2 : st.tabArg, slot = cell ? st.tabArg % 2 : 0;
	std::vector<std::pair<int, int>> rows;
	int y = 0;
	for (const Item &it : BuildView(st)) {
		if (it.header) {
			y += kHeaderH;
			continue;
		}
		rows.push_back({it.row, y});
		y += RowHeight(st.rows[it.row]);
	}
	int at = -1;
	for (size_t i = 0; i < rows.size(); i++) {
		if (rows[i].first == row) at = static_cast<int>(i);
	}
	if (at < 0) return;
	const int to = std::max(0, std::min(static_cast<int>(rows.size()) - 1, at + delta));
	const int newRow = rows[to].first, top = rows[to].second, height = RowHeight(st.rows[newRow]);
	st.tabArg = cell ? newRow * 2 + slot : newRow;
	if (top < st.list.scroll) st.list.scroll = top;
	else if (top + height > st.list.scroll + st.listGeom.visible) st.list.scroll = top + height - st.listGeom.visible;
	st.list.scroll = std::max(0, std::min(st.list.scroll, st.listGeom.maxScroll));
}

void KeyActivate(State &st, bool space)
{
	for (const Hit &h : st.hits) {
		if (h.action != st.tabAction || h.arg != st.tabArg) continue;
		const POINT center = {(h.r.left + h.r.right) / 2, (h.r.top + h.r.bottom) / 2};
		const int action = st.tabAction, arg = st.tabArg;
		Activate(st, action, arg, center);
		if (space) st.eatChar = true;
		return;
	}
}

}

bool ControlsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_INPUTLANGCHANGE) ControlsNoteLayout();
	std::lock_guard<std::mutex> lock(g_lock);
	State &st = g_s;
	if (!st.open) return false;
	switch (msg) {
	case WM_MOUSEMOVE:
		st.mx = static_cast<short>(LOWORD(lp));
		st.my = static_cast<short>(HIWORD(lp));
		{
			const POINT at = {st.mx, st.my};
			ListMove(st.list, st.listGeom, at);
			ListMove(st.kbList, st.kbGeom, at);
		}
		if (st.edit.dragging && st.searchFocus) {
			FieldGeom geom;
			if (FieldGet(kSearchAction, geom)) st.edit.caret = EditIndexAt(geom, st.search, st.mx);
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		{
			const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
			st.tabAction = -1;
			if (!Asking(st) && st.capture.row < 0 &&
				(st.view == 1 ? ListDown(st.kbList, st.kbGeom, at) : ListDown(st.list, st.listGeom, at))) {
				return true;
			}
			int hitArg = 0;
			const bool onSearch = HitAt(st, at, hitArg) == kSearchAction && !Asking(st);
			Click(st, at);
			if (onSearch && st.searchFocus) {
				if (msg == WM_LBUTTONDBLCLK) EditSelectWord(st.search, st.edit);
				else st.edit.dragging = true;
			}
		}
		return true;
	case WM_LBUTTONUP:
		st.edit.dragging = false;
		ListUp(st.list);
		ListUp(st.kbList);
		return true;
	case WM_RBUTTONDOWN: {
		int arg = 0;
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		if (!Asking(st) && HitAt(st, pt, arg) == kCell) TryAssign(st, CellRef(arg), ControlKey());
		return true;
	}
	case WM_MOUSEWHEEL:
		if (st.view == 1) {
			st.kbList.scroll -= GET_WHEEL_DELTA_WPARAM(wp) * 52 / WHEEL_DELTA;
			st.kbList.scroll = std::max(0, std::min(st.kbList.scroll, st.kbGeom.maxScroll));
			return true;
		}
		if (!Asking(st)) {
			st.list.scroll -= GET_WHEEL_DELTA_WPARAM(wp) * 64 / WHEEL_DELTA;
			st.list.scroll = std::max(0, std::min(st.list.scroll, st.listGeom.maxScroll));
		}
		return true;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (st.capture.row >= 0) {
			if ((lp & (1 << 30)) || IsModifier(wp) || wp == VK_CAPITAL || wp == VK_NUMLOCK) return true;
			if (wp == VK_ESCAPE) {
				st.capture = Ref();
				return true;
			}
			TryAssign(st, st.capture, {static_cast<unsigned short>(wp), Mods()});
			st.eatChar = true;
			return true;
		}
		if (wp == VK_ESCAPE) {
			st.escArmed = true;
			return true;
		}
		if (wp == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
			TabStep(st, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
			return true;
		}
		if (Asking(st)) {
			if (st.tabAction >= 0 && !(lp & (1 << 30)) && (wp == VK_RETURN || wp == VK_SPACE)) KeyActivate(st, wp == VK_SPACE);
			return true;
		}
		if (st.searchFocus) {
			if (wp == VK_RETURN) st.searchFocus = false;
			else if (EditKey(st.search, st.edit, wp, 60) == 2) st.list.scroll = 0;
			return true;
		}
		if (st.tabAction >= 0) {
			if (wp == VK_RETURN || wp == VK_SPACE) {
				if (!(lp & (1 << 30))) KeyActivate(st, wp == VK_SPACE);
			}
			else if (wp == VK_UP || wp == VK_DOWN) MoveRow(st, wp == VK_UP ? -1 : 1);
			else if (st.tabAction == kCell && (wp == VK_DELETE || wp == VK_BACK)) TryAssign(st, CellRef(st.tabArg), ControlKey());
		}
		return true;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (wp == VK_SNAPSHOT && st.capture.row >= 0) {
			TryAssign(st, st.capture, {VK_SNAPSHOT, Mods()});
			return true;
		}
		if (wp == VK_ESCAPE && st.escArmed) {
			st.escArmed = false;
			if (st.view == 1 && st.kbSel >= 0) st.kbSel = -1;
			else if (st.pend.row >= 0) {
				st.pend = Ref();
				st.pendReset = false;
			}
			else if (st.confirmResetAll) st.confirmResetAll = false;
			else if (!st.search.empty()) {
				st.search.clear();
				st.edit = EditState();
				st.list.scroll = 0;
			}
			else st.open = false;
		}
		return true;
	case WM_CHAR:
		if (st.eatChar) {
			st.eatChar = false;
			return true;
		}
		if (st.capture.row >= 0 || Asking(st)) return true;
		if (!st.searchFocus && wp >= 32 && wp != 127) {
			st.view = 0;
			st.searchFocus = true;
			EditToEnd(st.search, st.edit);
		}
		if (st.searchFocus && EditChar(st.search, st.edit, wp, 60)) st.list.scroll = 0;
		return true;
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
		if (st.capture.row >= 0) {
			const unsigned short vk = msg == WM_MBUTTONDOWN ? VK_MBUTTON : HIWORD(wp) == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1;
			TryAssign(st, st.capture, {vk, Mods()});
		}
		return true;
	case WM_SYSCHAR:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
		return true;
	default:
		return false;
	}
}
