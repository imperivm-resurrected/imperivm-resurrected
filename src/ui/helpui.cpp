// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/helpui.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/helpdoc.h"
#include "core/log.h"
#include "game/helpdialog.h"
#include "ui/helpui_view.h"
#include "ui/modui.h"
#include "ui/widgets.h"

using namespace ui;
using namespace helpui;

namespace {

const DWORD kCloseTimeoutMs = 2000;
const int kWheelStep = 66, kKeyStep = 44, kImagesPerTick = 32;
const size_t kQueryLimit = 60;

struct State {
	void *dlg = nullptr;
	int nativeTopic = 0;
	View view;
	HelpHistory history;
	// The scroll of each place of the history, to come back to it.
	std::map<int, int> scrolls;
	std::vector<Hit> hits;
	PaintOut out;
	std::vector<std::string> wanted;
	bool mouseDown = false, escArmed = false;
	DWORD closedAt = 0;
};

// What stays from one opening to the next, as the game keeps its own history: F1 comes back to the last page.
struct Kept {
	size_t textHash = 0;
	bool read = false;
	std::shared_ptr<const Content> content;
	std::shared_ptr<const ImageMap> images;
	HelpHistory history;
	int nativeTopic = -1;
};

std::mutex g_lock;
State g_s;
Kept g_kept;
Canvas g_canvas;
// The click that closes the screen must not reach the map when the button comes up.
bool g_swallowUp = false;

std::shared_ptr<const Content> Build(const std::string &text)
{
	auto c = std::make_shared<Content>();
	c->doc = ParseHelpXml(text);
	c->index = BuildHelpIndex(c->doc);
	HelpGameData data = HelpCollectGameData(c->doc);
	c->stats = std::move(data.stats);
	c->classTopic = std::move(data.classTopic);
	std::string counters;
	if (HelpCounterText(counters)) c->counters = ParseCounterUnits(counters);
	size_t entries = 0;
	for (const HelpTopic &t : c->doc.topics) entries += t.entries.size();
	LogI("ui", "help: %d topics, %d entries, %d groups in the side bar, %d units in the counters table", static_cast<int>(c->doc.topics.size()),
		static_cast<int>(entries), static_cast<int>(c->index.groups.size()), static_cast<int>(c->counters.of.size()));
	return c;
}

HelpPlace PlaceOfPath(const Content &c, const std::string &path)
{
	const int topic = path.empty() ? -1 : HelpResolve(c.doc, -1, path);
	return {topic >= 0 ? topic : c.index.home, -1, std::string()};
}

// The view follows the history: the place, the buttons, and the search of a search page.
void Apply(State &st)
{
	View &v = st.view;
	const HelpPlace *cur = st.history.Current();
	v.place = cur ? *cur : HelpPlace{v.content ? v.content->index.home : -1, -1, std::string()};
	v.canBack = st.history.CanBack();
	v.canForward = st.history.CanForward();
	const auto it = st.scrolls.find(st.history.at);
	v.scroll = it != st.scrolls.end() ? it->second : 0;
	v.revealNav = true;
	if (!v.place.query.empty() && v.content) {
		if (v.query != v.place.query || !v.hits) {
			v.query = v.place.query;
			EditToEnd(v.query, v.edit);
			v.hits = std::make_shared<const std::vector<HelpHit>>(HelpSearch(v.content->doc, v.content->index, v.query));
		}
	}
}

void KeepScroll(State &st)
{
	if (st.history.at >= 0) st.scrolls[st.history.at] = st.view.scroll;
}

void Go(State &st, const HelpPlace &place)
{
	if (!st.view.content || (place.query.empty() && (place.topic < 0 || place.topic >= static_cast<int>(st.view.content->doc.topics.size())))) return;
	KeepScroll(st);
	st.history.Go(place);
	for (auto it = st.scrolls.begin(); it != st.scrolls.end();) it = it->first >= st.history.at ? st.scrolls.erase(it) : std::next(it);
	Apply(st);
}

void Back(State &st)
{
	KeepScroll(st);
	if (st.history.Back()) Apply(st);
}

void Forward(State &st)
{
	KeepScroll(st);
	if (st.history.Forward()) Apply(st);
}

std::string Trimmed(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && s[a] == ' ') a++;
	while (b > a && s[b - 1] == ' ') b--;
	return s.substr(a, b - a);
}

void QueryChanged(State &st)
{
	View &v = st.view;
	if (!v.content) return;
	const std::string q = Trimmed(v.query);
	const HelpPlace *cur = st.history.Current();
	const bool onSearch = cur && !cur->query.empty();
	if (q.empty()) {
		v.hits.reset();
		if (onSearch) {
			if (st.history.CanBack()) Back(st);
			else {
				st.history.Replace({v.content->index.home, -1, std::string()});
				Apply(st);
			}
		}
		return;
	}
	v.hits = std::make_shared<const std::vector<HelpHit>>(HelpSearch(v.content->doc, v.content->index, q));
	const HelpPlace place = {-1, -1, q};
	if (onSearch) {
		st.history.Replace(place);
		v.place = place;
		v.scroll = 0;
	}
	else {
		Go(st, place);
	}
}

void ClearSearch(State &st)
{
	st.view.query.clear();
	st.view.edit = EditState();
	QueryChanged(st);
}

int HitAt(const std::vector<Hit> &hits, POINT at, int &arg)
{
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		return it->action;
	}
	return kActNone;
}

int MaxScroll(const State &st) { return std::max(0, st.out.contentH - st.out.viewH); }

void ScrollBy(State &st, int delta)
{
	st.view.scroll = std::max(0, std::min(MaxScroll(st), st.view.scroll + delta));
}

void NavScrollBy(State &st, int delta)
{
	st.view.navScroll = std::max(0, std::min(std::max(0, st.out.navH - st.out.navViewH), st.view.navScroll + delta));
}

struct After {
	bool close = false;
	std::string web;
};

void Click(State &st, int action, int arg, After &after)
{
	View &v = st.view;
	switch (action) {
	case kActGo:
		if (arg >= 0 && arg < static_cast<int>(st.out.targets.size())) Go(st, st.out.targets[arg]);
		break;
	case kActBack: Back(st); break;
	case kActForward: Forward(st); break;
	case kActClose: after.close = true; break;
	case kActClearSearch: ClearSearch(st); break;
	case kActToggle:
		if (!v.toggled.erase(arg)) v.toggled.insert(arg);
		break;
	case kActWeb:
		if (arg >= 0 && arg < static_cast<int>(st.out.web.size())) after.web = st.out.web[arg];
		break;
	default: break;
	}
}

bool Key(State &st, WPARAM wp)
{
	View &v = st.view;
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	if (v.searchFocused) {
		switch (wp) {
		case VK_ESCAPE:
			if (!v.query.empty()) ClearSearch(st);
			else v.searchFocused = false;
			return true;
		case VK_RETURN:
			if (v.hits && !v.hits->empty()) {
				Go(st, {(*v.hits)[0].topic, (*v.hits)[0].section, std::string()});
				v.searchFocused = false;
			}
			return true;
		case VK_TAB:
		case VK_DOWN:
			v.searchFocused = false;
			return true;
		default:
			if (EditKey(v.query, v.edit, wp, kQueryLimit) == 2) QueryChanged(st);
			return true;
		}
	}
	switch (wp) {
	case VK_ESCAPE: st.escArmed = true; break;
	case VK_BACK: Back(st); break;
	case VK_BROWSER_BACK: Back(st); break;
	case VK_BROWSER_FORWARD: Forward(st); break;
	case VK_UP: ScrollBy(st, -kKeyStep); break;
	case VK_DOWN: ScrollBy(st, kKeyStep); break;
	case VK_PRIOR: ScrollBy(st, -st.out.viewH * 4 / 5); break;
	case VK_NEXT:
	case VK_SPACE: ScrollBy(st, st.out.viewH * 4 / 5); break;
	case VK_HOME: v.scroll = 0; break;
	case VK_END: v.scroll = MaxScroll(st); break;
	case 'F':
		if (ctrl) {
			v.searchFocused = true;
			EditSelectAll(v.query, v.edit);
		}
		break;
	default: break;
	}
	return true;
}

bool Handle(UINT msg, WPARAM wp, LPARAM lp, After &after)
{
	std::lock_guard<std::mutex> lock(g_lock);
	State &st = g_s;
	View &v = st.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		// The game moves its cursor with these.
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		st.mouseDown = true;
		g_swallowUp = true;
		v.mouseX = at.x;
		v.mouseY = at.y;
		int fieldAction = 0;
		FieldGeom geom;
		if (FieldAt(at, fieldAction, geom) && fieldAction == kActSearch) {
			int arg = 0;
			if (HitAt(st.hits, at, arg) == kActClearSearch) {
				ClearSearch(st);
				return true;
			}
			v.searchFocused = true;
			v.edit.caret = v.edit.anchor = EditIndexAt(geom, v.query, at.x);
			if (msg == WM_LBUTTONDBLCLK) EditSelectWord(v.query, v.edit);
			return true;
		}
		v.searchFocused = false;
		int arg = 0;
		const int action = HitAt(st.hits, at, arg);
		if (action != kActNone) Click(st, action, arg, after);
		return true;
	}
	case WM_LBUTTONUP:
		st.mouseDown = false;
		return true;
	case WM_XBUTTONDOWN:
		if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) Back(st);
		else if (GET_XBUTTON_WPARAM(wp) == XBUTTON2) Forward(st);
		return true;
	case WM_MOUSEWHEEL: {
		// The wheel's point is on the screen: the last position of the mouse in the frame is the one that counts.
		const POINT mouse = {v.mouseX, v.mouseY};
		const int delta = -GET_WHEEL_DELTA_WPARAM(wp) * kWheelStep / WHEEL_DELTA;
		if (PtInRect(&st.out.nav, mouse)) NavScrollBy(st, delta);
		else ScrollBy(st, delta);
		return true;
	}
	case WM_KEYDOWN:
		// F1 stays the game's, which shows the help it already has open.
		if (wp == VK_F1) return false;
		return Key(st, wp);
	case WM_KEYUP:
		if (wp == VK_F1) return false;
		// On the release, as the game does: closing on the press would leave it the release, and it would open its menu.
		if (wp == VK_ESCAPE && st.escArmed) {
			st.escArmed = false;
			after.close = true;
		}
		return true;
	case WM_CHAR:
		if (wp < 32 || wp == 127) return true;
		if (!v.searchFocused) {
			if (wp == ' ') return true;
			v.searchFocused = true;
			EditToEnd(v.query, v.edit);
		}
		if (EditChar(v.query, v.edit, wp, kQueryLimit)) QueryChanged(st);
		return true;
	default:
		return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	}
}

void OpenWeb(const std::string &url)
{
	LogI("ui", "help: opening a web link of the help");
	const std::wstring w = W(url);
	ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Close(void *dlg)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.closedAt = GetTickCount();
	}
	if (HelpDialogClose(dlg)) return;
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.closedAt = 0;
}

void LoadImages(void *dlg)
{
	std::vector<std::string> wanted;
	std::shared_ptr<const ImageMap> images;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.dlg != dlg || g_s.wanted.empty()) return;
		wanted.swap(g_s.wanted);
		images = g_s.view.images;
	}
	auto next = std::make_shared<ImageMap>(images ? *images : ImageMap());
	int loaded = 0;
	std::vector<std::string> later;
	for (const std::string &path : wanted) {
		const std::string key = ImageKey(path);
		if (next->count(key)) continue;
		if (loaded >= kImagesPerTick) {
			later.push_back(path);
			continue;
		}
		auto img = std::make_shared<BattleImage>();
		if (HelpGameImage(path, *img)) {
			KeyOut(*img);
			(*next)[key] = img;
		}
		else (*next)[key] = nullptr;
		loaded++;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.dlg != dlg) return;
	g_s.view.images = next;
	g_s.wanted.insert(g_s.wanted.end(), later.begin(), later.end());
}

void Remember(const State &st)
{
	g_kept.history = st.history;
	g_kept.nativeTopic = st.nativeTopic;
	g_kept.images = st.view.images;
}

}

void HelpUiGameTick()
{
	void *dlg = HelpDialogOpen();
	bool fresh = false;
	int seen = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!dlg) {
			if (g_s.dlg) {
				Remember(g_s);
				g_s = State();
			}
			return;
		}
		fresh = g_s.dlg != dlg;
		seen = g_s.nativeTopic;
	}
	const int topic = HelpDialogTopic(dlg);
	if (fresh) {
		std::string text;
		const bool read = HelpGameText(text);
		const size_t hash = std::hash<std::string>()(text);
		if (!g_kept.content || g_kept.textHash != hash || g_kept.read != read) {
			g_kept = Kept();
			g_kept.textHash = hash;
			g_kept.read = read;
			g_kept.content = Build(read ? text : std::string());
		}
		const std::shared_ptr<const Content> content = g_kept.content;
		const std::string path = HelpDialogTopicPath(dlg);
		std::lock_guard<std::mutex> lock(g_lock);
		g_s = State();
		g_s.dlg = dlg;
		g_s.nativeTopic = topic;
		g_s.view.content = content;
		g_s.view.images = g_kept.images;
		g_s.history = g_kept.history;
		// The same topic as when it closed: the player pressed F1 again and comes back to the page they were reading.
		if (!g_s.history.Current() || topic != g_kept.nativeTopic) g_s.history.Go(PlaceOfPath(*content, path));
		Apply(g_s);
		LogI("ui", "the help screen opens at %s", path.empty() ? "its contents" : path.c_str());
		return;
	}
	if (topic != seen) {
		// The game opened another topic while it was open: the help of a unit from its icon.
		const std::string path = HelpDialogTopicPath(dlg);
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.dlg == dlg && g_s.view.content) {
			g_s.nativeTopic = topic;
			Go(g_s, PlaceOfPath(*g_s.view.content, path));
		}
	}
	LoadImages(dlg);
}

// On the drawing thread: the game tick says whether the game's dialog is open, which is not read from here.
bool HelpUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.dlg) return false;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void HelpUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg) return;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kScreenW, kScreenH, 40)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOut out;
	PaintHelpScreen(r.p, view, out);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	for (const std::string &path : out.wanted) {
		if (std::find(g_s.wanted.begin(), g_s.wanted.end(), path) == g_s.wanted.end()) g_s.wanted.push_back(path);
	}
	if (view.revealNav && g_s.view.revealNav && g_s.view.place == view.place) {
		g_s.view.revealNav = false;
		g_s.view.navScroll = out.navScroll;
	}
	g_s.out = std::move(out);
}

bool HelpUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		return true;
	}
	void *dlg = HelpDialogOpen();
	if (!dlg || ModUiIsOpen()) return false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.dlg != dlg) return false;
		if (g_s.closedAt) return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEMOVE) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	}
	After after;
	const bool consumed = Handle(msg, wp, lp, after);
	if (!after.web.empty()) OpenWeb(after.web);
	if (after.close) Close(dlg);
	return consumed;
}
