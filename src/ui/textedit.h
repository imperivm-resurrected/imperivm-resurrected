// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ui/uikit.h"

namespace ui {

struct EditState {
	int caret = 0;
	int anchor = 0;
	bool dragging = false;
};

inline bool IsCont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
inline int PrevCp(const std::string &t, int i)
{
	if (i <= 0) return 0;
	i--;
	while (i > 0 && IsCont(t[i])) i--;
	return i;
}
inline int NextCp(const std::string &t, int i)
{
	const int n = static_cast<int>(t.size());
	if (i >= n) return n;
	i++;
	while (i < n && IsCont(t[i])) i++;
	return i;
}
inline bool IsWordChar(char c) { return isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80 || c == '_'; }
inline int WordLeft(const std::string &t, int i)
{
	while (i > 0 && !IsWordChar(t[i - 1])) i = PrevCp(t, i);
	while (i > 0 && IsWordChar(t[i - 1])) i = PrevCp(t, i);
	return i;
}
inline int WordRight(const std::string &t, int i)
{
	const int n = static_cast<int>(t.size());
	while (i < n && !IsWordChar(t[i])) i = NextCp(t, i);
	while (i < n && IsWordChar(t[i])) i = NextCp(t, i);
	return i;
}

inline int SelLo(const EditState &e) { return std::min(e.caret, e.anchor); }
inline int SelHi(const EditState &e) { return std::max(e.caret, e.anchor); }
inline bool HasSel(const EditState &e) { return e.caret != e.anchor; }

inline void EditFix(const std::string &t, EditState &e)
{
	const int n = static_cast<int>(t.size());
	e.caret = std::max(0, std::min(e.caret, n));
	e.anchor = std::max(0, std::min(e.anchor, n));
	while (e.caret > 0 && e.caret < n && IsCont(t[e.caret])) e.caret--;
	while (e.anchor > 0 && e.anchor < n && IsCont(t[e.anchor])) e.anchor--;
}
inline void EditToEnd(const std::string &t, EditState &e) { e.caret = e.anchor = static_cast<int>(t.size()); }
inline void EditSelectAll(const std::string &t, EditState &e)
{
	e.anchor = 0;
	e.caret = static_cast<int>(t.size());
}
inline void EditSelectWord(const std::string &t, EditState &e)
{
	EditFix(t, e);
	int lo = e.caret, hi = e.caret;
	while (lo > 0 && IsWordChar(t[lo - 1])) lo = PrevCp(t, lo);
	while (hi < static_cast<int>(t.size()) && IsWordChar(t[hi])) hi = NextCp(t, hi);
	if (lo == hi) EditSelectAll(t, e);
	else {
		e.anchor = lo;
		e.caret = hi;
	}
}
inline std::string EditSelected(const std::string &t, const EditState &e) { return t.substr(SelLo(e), SelHi(e) - SelLo(e)); }
inline bool EditDeleteSel(std::string &t, EditState &e)
{
	if (!HasSel(e)) return false;
	const int lo = SelLo(e), hi = SelHi(e);
	t.erase(lo, hi - lo);
	e.caret = e.anchor = lo;
	return true;
}

inline std::string ClipboardGet()
{
	std::string out;
	if (!OpenClipboard(nullptr)) return out;
	if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
		if (const wchar_t *w = static_cast<const wchar_t *>(GlobalLock(h))) {
			const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			if (n > 1) {
				out.resize(n - 1);
				WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
			}
			GlobalUnlock(h);
		}
	}
	CloseClipboard();
	out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return c == '\r' || c == '\n' || c == '\t'; }), out.end());
	return out;
}
inline void ClipboardSet(const std::string &s)
{
	const std::wstring w = W(s);
	if (!OpenClipboard(nullptr)) return;
	EmptyClipboard();
	if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
		if (void *p = GlobalLock(h)) {
			memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(h);
			SetClipboardData(CF_UNICODETEXT, h);
		}
		else {
			GlobalFree(h);
		}
	}
	CloseClipboard();
}

inline bool EditInsert(std::string &t, EditState &e, std::string ins, size_t limit)
{
	EditFix(t, e);
	const size_t before = t.size();
	EditDeleteSel(t, e);
	const size_t room = limit > t.size() ? limit - t.size() : 0;
	if (ins.size() > room) {
		size_t cut = room;
		while (cut > 0 && cut < ins.size() && IsCont(ins[cut])) cut--;
		ins.resize(cut);
	}
	t.insert(e.caret, ins);
	e.caret += static_cast<int>(ins.size());
	e.anchor = e.caret;
	return t.size() != before || !ins.empty();
}

inline bool EditChar(std::string &t, EditState &e, WPARAM wp, size_t limit)
{
	if (wp < 32 || wp == 127) return false;
	char ansi = static_cast<char>(wp);
	wchar_t w = 0;
	MultiByteToWideChar(CP_ACP, 0, &ansi, 1, &w, 1);
	char utf8[8] = {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, &w, 1, utf8, sizeof(utf8), nullptr, nullptr);
	return EditInsert(t, e, std::string(utf8, n), limit);
}

inline int EditKey(std::string &t, EditState &e, WPARAM vk, size_t limit)
{
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	EditFix(t, e);
	const int n = static_cast<int>(t.size());
	switch (vk) {
	case VK_BACK:
		if (EditDeleteSel(t, e)) return 2;
		if (e.caret > 0) {
			const int from = ctrl ? WordLeft(t, e.caret) : PrevCp(t, e.caret);
			t.erase(from, e.caret - from);
			e.caret = e.anchor = from;
			return 2;
		}
		return 1;
	case VK_DELETE:
		if (EditDeleteSel(t, e)) return 2;
		if (e.caret < n) {
			const int to = ctrl ? WordRight(t, e.caret) : NextCp(t, e.caret);
			t.erase(e.caret, to - e.caret);
			e.anchor = e.caret;
			return 2;
		}
		return 1;
	case VK_LEFT: {
		int to = ctrl ? WordLeft(t, e.caret) : PrevCp(t, e.caret);
		if (!shift && HasSel(e)) to = SelLo(e);
		e.caret = to;
		if (!shift) e.anchor = to;
		return 1;
	}
	case VK_RIGHT: {
		int to = ctrl ? WordRight(t, e.caret) : NextCp(t, e.caret);
		if (!shift && HasSel(e)) to = SelHi(e);
		e.caret = to;
		if (!shift) e.anchor = to;
		return 1;
	}
	case VK_HOME:
		e.caret = 0;
		if (!shift) e.anchor = 0;
		return 1;
	case VK_END:
		e.caret = n;
		if (!shift) e.anchor = n;
		return 1;
	case 'A':
		if (!ctrl) break;
		EditSelectAll(t, e);
		return 1;
	case 'C':
		if (!ctrl) break;
		if (HasSel(e)) ClipboardSet(EditSelected(t, e));
		return 1;
	case 'X':
		if (!ctrl) break;
		if (!HasSel(e)) return 1;
		ClipboardSet(EditSelected(t, e));
		EditDeleteSel(t, e);
		return 2;
	case 'V':
		if (!ctrl) break;
		return EditInsert(t, e, ClipboardGet(), limit) ? 2 : 1;
	}
	return 0;
}

struct FieldGeom {
	RECT r = {};
	int textX = 0;
	int scroll = 0;
	std::vector<int> edges;
	std::vector<int> bytes;
	unsigned serial = 0;
};
inline std::mutex g_fieldMx;
inline std::map<int, FieldGeom> g_fields;
inline unsigned g_fieldSerial = 1;

inline void FieldsBeginFrame()
{
	std::lock_guard<std::mutex> lock(g_fieldMx);
	g_fieldSerial++;
}
inline bool FieldAt(POINT pt, int &action, FieldGeom &out)
{
	std::lock_guard<std::mutex> lock(g_fieldMx);
	for (const auto &kv : g_fields) {
		if (kv.second.serial + 1 >= g_fieldSerial && PtInRect(&kv.second.r, pt)) {
			action = kv.first;
			out = kv.second;
			return true;
		}
	}
	return false;
}
inline bool FieldGet(int action, FieldGeom &out)
{
	std::lock_guard<std::mutex> lock(g_fieldMx);
	const auto it = g_fields.find(action);
	if (it == g_fields.end() || it->second.serial + 1 < g_fieldSerial) return false;
	out = it->second;
	return true;
}
inline int EditIndexAt(const FieldGeom &g, const std::string &t, int mouseX)
{
	if (g.edges.empty() || g.bytes.size() != g.edges.size()) return static_cast<int>(t.size());
	const int rel = mouseX - g.textX + g.scroll;
	size_t best = 0;
	for (size_t i = 1; i < g.edges.size(); i++) {
		if (abs(g.edges[i] - rel) < abs(g.edges[best] - rel)) best = i;
	}
	return std::min(g.bytes[best], static_cast<int>(t.size()));
}

inline void PaintEditField(Painter &p, int x, int y, int w, int h, const std::string &text, const std::string &placeholder,
	bool focused, bool secret, const EditState &es, int action)
{
	p.Fill(x, y, w, h, focused ? pal::raised : pal::bg);
	p.Frame(x, y, w, h, focused ? pal::redText : pal::lineStrong);
	p.Font(0);
	std::vector<int> bytes;
	for (int i = 0;; i = NextCp(text, i)) {
		bytes.push_back(i);
		if (i >= static_cast<int>(text.size())) break;
	}
	std::wstring ws;
	std::vector<size_t> wlen(1, 0);
	for (size_t k = 0; k + 1 < bytes.size(); k++) {
		if (secret) ws += L'*';
		else ws += W(text.substr(bytes[k], bytes[k + 1] - bytes[k]));
		wlen.push_back(ws.size());
	}
	std::vector<int> edges(bytes.size(), 0);
	if (!ws.empty()) {
		std::vector<int> dx(ws.size());
		SIZE sz = {};
		GetTextExtentExPointW(p.dc, ws.c_str(), static_cast<int>(ws.size()), 0x7fffffff, nullptr, dx.data(), &sz);
		for (size_t k = 1; k < edges.size(); k++) edges[k] = wlen[k] ? dx[wlen[k] - 1] : 0;
	}
	const int totalW = edges.back();
	const int px0 = p.S(x + 8), innerW = p.S(w - 16);
	int scroll = 0;
	if (focused) {
		std::lock_guard<std::mutex> lock(g_fieldMx);
		const auto it = g_fields.find(action);
		if (it != g_fields.end()) scroll = it->second.scroll;
	}
	auto indexOf = [&](int byte) {
		const size_t k = std::lower_bound(bytes.begin(), bytes.end(), byte) - bytes.begin();
		return std::min(k, bytes.size() - 1);
	};
	if (focused) {
		const int caretPx = edges[indexOf(es.caret)];
		if (caretPx - scroll > innerW - 2) scroll = caretPx - innerW + 2;
		if (caretPx - scroll < 0) scroll = caretPx;
		scroll = std::max(0, std::min(scroll, std::max(0, totalW - innerW + 2)));
	}
	p.Clip(x + 6, y + 1, w - 12, h - 2);
	const int origin = px0 - scroll;
	if (focused && es.caret != es.anchor) {
		const size_t a = indexOf(SelLo(es)), b = indexOf(SelHi(es));
		RECT sr = {origin + edges[a], p.S(y + 4), origin + edges[b], p.S(y + h - 4)};
		HBRUSH br = CreateSolidBrush(pal::red);
		FillRect(p.dc, &sr, br);
		DeleteObject(br);
	}
	if (text.empty() && !focused) {
		p.Text(x + 8, y, w - 16, h, placeholder, pal::textDim);
	}
	else if (!ws.empty()) {
		SetTextColor(p.dc, pal::text);
		RECT tr = {origin, p.S(y), origin + totalW + p.S(40), p.S(y + h)};
		DrawTextW(p.dc, ws.c_str(), static_cast<int>(ws.size()), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	if (focused && (GetTickCount() / 530) % 2 == 0 && es.caret == es.anchor) {
		RECT cr = {origin + edges[indexOf(es.caret)], p.S(y + 5), origin + edges[indexOf(es.caret)] + std::max(1, p.S(1)), p.S(y + h - 5)};
		HBRUSH br = CreateSolidBrush(pal::body);
		FillRect(p.dc, &cr, br);
		DeleteObject(br);
	}
	p.Unclip();
	{
		std::lock_guard<std::mutex> lock(g_fieldMx);
		FieldGeom &g = g_fields[action];
		g.r = p.R(x, y, w, h);
		OffsetRect(&g.r, p.ox, p.oy);
		g.textX = p.ox + px0;
		g.scroll = scroll;
		g.edges = edges;
		g.bytes = bytes;
		g.serial = g_fieldSerial;
	}
	p.AddHit(x, y, w, h, action);
}

struct FocusItem {
	int action = -1;
	int arg = 0;
};
template <class SkipFn> std::vector<FocusItem> FocusOrder(const std::vector<Hit> &hits, SkipFn skip)
{
	std::vector<FocusItem> out;
	for (const Hit &h : hits) {
		if (skip(h.action)) continue;
		bool dup = false;
		for (const FocusItem &f : out) dup = dup || (f.action == h.action && f.arg == h.arg);
		if (!dup) out.push_back({h.action, h.arg});
	}
	return out;
}
inline FocusItem FocusStep(const std::vector<FocusItem> &order, FocusItem cur, bool back)
{
	if (order.empty()) return {};
	int idx = -1;
	for (size_t i = 0; i < order.size(); i++) {
		if (order[i].action == cur.action && order[i].arg == cur.arg) idx = static_cast<int>(i);
	}
	const int n = static_cast<int>(order.size());
	idx = idx < 0 ? (back ? n - 1 : 0) : (idx + (back ? n - 1 : 1)) % n;
	return order[idx];
}

}
