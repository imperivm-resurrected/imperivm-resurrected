// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/discord.h"
#include "net/patrons.h"
#include "core/guard.h"
#include "core/log.h"
#include "ui/modui.h"
#include "ui/patronui.h"
#include "ui/report.h"
#include "ui/support.h"
#include "ui/textedit.h"
#include "ui/uikit.h"
#include "ui/widgets.h"

using namespace ui;

namespace {

const char kPaypal[] = "https://paypal.me/imperivmresurrected";
const char kBtc[] = "bc1q58tsfh2h6yaqt7zlpqu08ykjrg8vf40wme0lzn";
const char kGitHub[] = "https://github.com/THIONG";
const char kLinkedIn[] = "https://www.linkedin.com/in/saulrodriguezg/";
const char kDiscordUser[] = "thiong";

enum Action { kNone = 0, kClose, kOpenLogs, kMakeReport, kWholeRoll, kTabSupport, kTabRoll, kCopyBase = 100, kOpenBase = 200 };

struct Row {
	std::string label, value;
	bool open = false;
	std::string copy;
};

std::mutex g_lock;
bool g_open = false;
DWORD g_cornerTick = 0;
RECT g_heart = {};
RECT g_panel = {};
int g_mx = -1, g_my = -1;
bool g_swallowUp = false, g_escArmed = false;
std::vector<Hit> g_hits;
int g_tabAction = -1;
// 0 the ways to support, 1 the honour roll.
int g_tab = 0;
int g_rollScroll = 0, g_rollScrollMax = 0;

void TabStep(bool back)
{
	const FocusItem next = FocusStep(FocusOrder(g_hits, [](int a) { return a == kNone; }), {g_tabAction, 0}, back);
	if (next.action >= 0) g_tabAction = next.action;
}
int g_copied = -1;
DWORD g_copiedAt = 0;
Canvas g_panelCanvas;

void BuildRows(std::vector<Row> &donate, std::vector<Row> &contact)
{
	if (kPaypal[0]) {
		const std::string url = kPaypal;
		const size_t scheme = url.find("://");
		const std::string shown = scheme == std::string::npos ? url : url.substr(scheme + 3);
		donate.push_back({"PayPal", shown, true, url});
	}
	donate.push_back({"Bitcoin address", kBtc, false, kBtc});
	contact.push_back({"GitHub", "github.com/THIONG", true, kGitHub});
	contact.push_back({"LinkedIn", "linkedin.com/in/saulrodriguezg", true, kLinkedIn});
	contact.push_back({"Discord", kDiscordUser, false, kDiscordUser});
}

const int kPanelW = 640, kRowH = 58, kSectionH = 36, kProblemRows = 2;
const int kTabRowY = 64, kContentY = 112, kPodiumMiniH = 78, kPodiumH = 168, kRollRowH = 38;

int PanelHeight(const std::vector<Row> &donate, const std::vector<Row> &contact)
{
	int h = kContentY + 44 + kPodiumMiniH + 16;
	for (const std::vector<Row> *rows : {&donate, &contact}) h += kSectionH + kRowH * static_cast<int>(rows->size());
	return h + kSectionH + kRowH * kProblemRows + 76;
}

std::string Euros(double amount)
{
	char text[32];
	if (amount == static_cast<double>(static_cast<long long>(amount))) {
		sprintf_s(text, "%lld \xE2\x82\xAC", static_cast<long long>(amount));
		return text;
	}
	const long long cents = static_cast<long long>(amount * 100 + 0.5);
	char units[24], hundredths[8];
	sprintf_s(units, "%lld", cents / 100);
	sprintf_s(hundredths, "%02lld", cents % 100);
	// The decimal mark of the game's language, as the website writes it.
	return TrFmt("%s.%s \xE2\x82\xAC", {units, hundredths});
}

void OpenLink(const std::string &url)
{
	SafeThread([url] {
		const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		SHELLEXECUTEINFOA sei = {};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		sei.lpVerb = "open";
		sei.lpFile = url.c_str();
		sei.nShow = SW_SHOWNORMAL;
		ShellExecuteExA(&sei);
		if (SUCCEEDED(co)) CoUninitialize();
	}).detach();
}

struct PointF {
	float x, y;
};

const float kHeartPath[][6] = {
	{5.4645f, 3, 3.9216f, 3.5f, 2.75f, 4.7f},
	{0.4069f, 7.1f, 0.4712f, 10.8f, 2.75f, 13.2f},
	{-1, -1, -1, -1, 12, 23},
	{-1, -1, -1, -1, 21.25f, 13.2f},
	{23.529f, 10.8f, 23.593f, 7.1f, 21.25f, 4.7f},
	{18.907f, 2.4f, 15.093f, 2.4f, 12.75f, 4.7f},
	{-1, -1, -1, -1, 12, 5.5f},
	{-1, -1, -1, -1, 11.25f, 4.7f},
	{10.0785f, 3.5f, 8.5355f, 3, 7, 3},
};

std::vector<PointF> HeartPolygon()
{
	std::vector<PointF> poly;
	PointF cur = {7.0f, 3.0f};
	poly.push_back(cur);
	for (const auto &seg : kHeartPath) {
		const PointF end = {seg[4], seg[5]};
		if (seg[0] < 0) {
			poly.push_back(end);
		}
		else {
			const PointF c1 = {seg[0], seg[1]}, c2 = {seg[2], seg[3]};
			for (int i = 1; i <= 16; i++) {
				const float t = i / 16.0f, u = 1 - t;
				poly.push_back({u * u * u * cur.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * end.x,
					u * u * u * cur.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * end.y});
			}
		}
		cur = end;
	}
	for (PointF &p : poly) p.y -= 0.7f;
	return poly;
}

const std::vector<uint8_t> &HeartMask(int size)
{
	static std::vector<uint8_t> mask;
	static int built = 0;
	if (built == size) return mask;
	built = size;
	mask.assign(static_cast<size_t>(size) * size, 0);
	const std::vector<PointF> poly = HeartPolygon();
	const float scale = 24.0f / size;
	for (int py = 0; py < size; py++) {
		for (int px = 0; px < size; px++) {
			int hit = 0;
			for (int sy = 0; sy < 4; sy++) {
				for (int sx = 0; sx < 4; sx++) {
					const float x = (px + (sx + 0.5f) / 4.0f) * scale, y = (py + (sy + 0.5f) / 4.0f) * scale;
					bool inside = false;
					for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
						if ((poly[i].y > y) != (poly[j].y > y) && x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) inside = !inside;
					}
					if (inside) hit++;
				}
			}
			mask[static_cast<size_t>(py) * size + px] = static_cast<uint8_t>(hit * 255 / 16);
		}
	}
	return mask;
}

void DrawHeart(uint32_t *buf, int w, int h, int left, int top, int size, uint32_t color, int alphaMax, int dx, int dy)
{
	const std::vector<uint8_t> &mask = HeartMask(size);
	for (int py = 0; py < size; py++) {
		for (int px = 0; px < size; px++) {
			const unsigned a = mask[static_cast<size_t>(py) * size + px] * alphaMax / 255;
			if (!a) continue;
			const int x = left + px + dx, y = top + py + dy;
			if (x < 0 || x >= w || y < 0 || y >= h) continue;
			uint32_t &dst = buf[static_cast<size_t>(y) * w + x];
			const uint32_t rb = ((color & 0xff00ff) * a + (dst & 0xff00ff) * (255 - a)) >> 8;
			const uint32_t g = ((color & 0x00ff00) * a + (dst & 0x00ff00) * (255 - a)) >> 8;
			dst = 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
		}
	}
}

}

void SupportDrawCorner(uint32_t *buf, int w, int h, const RECT &d)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_cornerTick = GetTickCount();
	const int size = d.right - d.left;
	const int gap = std::max(8, size / 4);
	g_heart = {d.left, d.top - gap - size, d.right, d.top - gap};
	const bool hover = !g_open && g_mx >= g_heart.left && g_mx < g_heart.right && g_my >= g_heart.top && g_my < g_heart.bottom;
	const int shadow = std::max(1, size / 24);
	DrawHeart(buf, w, h, g_heart.left, g_heart.top, size, 0x000000u, 130, shadow, shadow);
	DrawHeart(buf, w, h, g_heart.left, g_heart.top, size, hover ? 0xE74C3Cu : 0xFFFFFFu, hover ? 255 : 235, 0, 0);
	if (hover) ModUiDrawHoverCard(buf, w, h, "Support the project", RGB(231, 76, 60), g_heart.right + size / 3, (g_heart.top + g_heart.bottom) / 2);
}

namespace {

// One card of the podium: laurel, place, name with its light and amount.
void PodiumCard(Painter &p, int x, int y, int w, int h, const PatronRollEntry &e, int laurel, bool big)
{
	const PatronColors c = PatronColorsOf(e.tier);
	p.Fill(x, y, w, h, PatronBlend(RGB(24, 19, 13), c.laurel, 12));
	p.Fill(x, y, w, 3, c.laurel);
	const UINT center = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	int cy = y + (big ? 14 : 8);
	p.Font(big ? 2 : 1);
	const std::string place = PatronRoman(e.place);
	if (big) {
		DrawPatron(p, x + (w - laurel) / 2, cy, laurel, x, cy + laurel + 2, w, 30, place, e.tier, center);
		cy += laurel + 34;
	}
	else {
		const int lx = x + 10;
		DrawPatron(p, lx, y + (h - laurel) / 2, laurel, lx + laurel + 8, y + 6, w - laurel - 26, 26, e.name, e.tier,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(lx + laurel + 8, y + h - 34, w - laurel - 26, 22, place + (e.hideAmount ? std::string() : "  \xC2\xB7  " + Euros(e.amount)), kDim,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	p.Font(1);
	DrawPatron(p, x, cy, 0, x + 8, cy, w - 16, 26, e.name, e.tier, center);
	cy += 28;
	if (!e.hideAmount) {
		p.Font(3);
		p.Text(x, cy, w, 22, Euros(e.amount), kText, center);
	}
}

}

void SupportDrawPanel(uint32_t *buf, int w, int h)
{
	int tab, scroll;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_open) return;
		tab = g_tab;
		scroll = g_rollScroll;
	}
	std::vector<Row> donate, contact;
	BuildRows(donate, contact);
	const int H = PanelHeight(donate, contact);
	PanelRender r(g_panelCanvas, buf, w, h);
	if (!r.Begin(kPanelW, H)) return;
	r.Focus(g_tabAction, 0);
	Painter &p = r.p;
	const int ox = r.ox, oy = r.oy, pw = r.pw, ph = r.ph;
	int mx, my, copied;
	DWORD copiedAt;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		mx = g_mx;
		my = g_my;
		copied = g_copied;
		copiedAt = g_copiedAt;
	}
	if (copied >= 0 && GetTickCount() - copiedAt > 2000) copied = -1;
	PaintWindowFrame(p, kPanelW, H);
	const int x = 28, w2 = kPanelW - 56;
	int y = 20;
	PaintTitle(p, x, y, w2, "Support the project");

	PatronsRollRefresh();
	int state = 0;
	const std::vector<PatronRollEntry> roll = PatronsRoll(state);

	// Tabs
	{
		int tx = x;
		const char *const labels[2] = {"Support", "Honour roll"};
		for (int i = 0; i < 2; i++) {
			p.Font(1);
			const int tw = p.TextWidth(labels[i]) + 28;
			const bool active = tab == i, hov = !active && p.Hover(tx, kTabRowY, tw, 34, mx, my);
			p.Text(tx, kTabRowY, tw, 34, labels[i], active ? kGold : hov ? kText : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (active) p.Fill(tx + 6, kTabRowY + 32, tw - 12, 3, kGold);
			p.AddHit(tx, kTabRowY, tw, 34, i == 0 ? kTabSupport : kTabRoll, 0);
			tx += tw + 8;
		}
		p.Fill(x, kTabRowY + 35, w2, 1, pal::lineDim);
	}
	y = kContentY;

	if (tab == 0) {
		p.Font(3);
		p.TextWrapped(x, y, w2, 40, "Imperivm Resurrected is free. If you like it, you can help keep it going.", kDim);
		y += 44;
		// The top three as a teaser: a click goes to the whole roll.
		if (!roll.empty()) {
			// As on the podium: the first in the middle and a little bigger.
			const int gap = 10, side = (w2 - gap * 2) * 31 / 100, middle = w2 - gap * 2 - side * 2;
			const int order[3] = {1, 0, 2};
			for (int slot = 0; slot < 3; slot++) {
				const int i = order[slot];
				if (i >= static_cast<int>(roll.size())) continue;
				const int cx = slot == 0 ? x : slot == 1 ? x + side + gap : x + side + gap + middle + gap;
				const int cw = slot == 1 ? middle : side, top = slot == 1 ? 0 : 8;
				PodiumCard(p, cx, y + top, cw, kPodiumMiniH - 8 - top, roll[i], slot == 1 ? 30 : 24, false);
				p.AddHit(cx, y + top, cw, kPodiumMiniH - 8 - top, kTabRoll, 0);
			}
		}
		else {
			p.Font(3);
			p.Text(x, y, w2, kPodiumMiniH - 8, state == 0 ? "Loading..." : "No patrons yet. Be the first!", kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
		y += kPodiumMiniH + 16;
		int index = 0;
		auto header = [&](const char *title) {
			p.Font(1);
			p.Text(x, y + 4, w2, 22, title, kText);
			p.Fill(x, y + 28, w2, 1, pal::lineDim);
			y += kSectionH;
		};
		auto section = [&](const char *title, const std::vector<Row> &rows) {
			header(title);
			for (const Row &r : rows) {
				p.Font(3);
				p.Text(x, y, 200, 18, r.label, kDim);
				p.Font(0);
				p.Text(x, y + 20, w2 - 130, 24, r.value, kText);
				const bool done = copied == index;
				p.Button(x + w2 - 110, y + 6, 110, 32, r.open ? "Open link" : done ? "Copied" : "Copy", (r.open ? kOpenBase : kCopyBase) + index, 0, mx, my,
					true, done);
				y += kRowH;
				index++;
			}
		};
		section("Donations", donate);
		section("Contact", contact);
		p.Font(1);
		int bw = 110;
		for (const char *label : {"Open folder", "Create report", "Creating..."}) bw = std::max(bw, p.TextWidth(label) + 32);
		auto problemRow = [&](const char *label, const char *value, const char *button, int action, bool enabled) {
			p.Font(3);
			p.Text(x, y, 200, 18, label, kDim);
			p.Font(0);
			p.Text(x, y + 20, w2 - bw - 20, 24, value, kText);
			p.Button(x + w2 - bw, y + 6, bw, 32, button, action, 0, mx, my, enabled);
			y += kRowH;
		};
		header("Report a problem");
		problemRow("Logs folder", "Logs\\imperivm", "Open folder", kOpenLogs, true);
		const bool busy = ReportBusy();
		problemRow("Problem report", "Zip with logs and system info", busy ? "Creating..." : "Create report", kMakeReport, !busy);
	}
	else {
		int maxScroll = 0;
		if (roll.empty()) {
			p.Font(3);
			p.TextWrapped(x, y + 10, w2, 60, state == 0 ? "Loading..." : "No patrons yet. Be the first!", kDim);
		}
		else {
			// The podium as on the website: first in the middle and highest, second on the left, third on the right.
			const int gap = 12, cw = (w2 - gap * 2) / 3;
			const int order[3] = {1, 0, 2}, lift[3] = {18, 0, 30};
			for (int slot = 0; slot < 3; slot++) {
				const int i = order[slot];
				if (i >= static_cast<int>(roll.size())) continue;
				PodiumCard(p, x + slot * (cw + gap), y + lift[slot], cw, kPodiumH - lift[slot], roll[i], i == 0 ? 54 : 44, true);
			}
			y += kPodiumH + 14;
			// The rest in a list that scrolls with the wheel.
			const int listH = H - 76 - y, rows = static_cast<int>(roll.size()) - 3;
			maxScroll = std::max(0, rows * kRollRowH - listH);
			scroll = std::max(0, std::min(scroll, maxScroll));
			p.Clip(x, y, w2, listH);
			const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
			for (int i = 3; i < static_cast<int>(roll.size()); i++) {
				const int ry = y + (i - 3) * kRollRowH - scroll;
				if (ry + kRollRowH < y || ry > y + listH) continue;
				const PatronRollEntry &e = roll[i];
				if (i % 2) p.Fill(x, ry, w2, kRollRowH, RGB(30, 24, 17));
				p.Font(1);
				p.Text(x + 6, ry, 52, kRollRowH, PatronRoman(e.place), kDim, fmt);
				const std::string amount = e.hideAmount ? std::string() : Euros(e.amount);
				p.Font(3);
				const int amountW = amount.empty() ? 0 : p.TextWidth(amount) + 12;
				p.Font(0);
				DrawPatron(p, x + 60, ry + (kRollRowH - 22) / 2, 22, x + 90, ry, w2 - 90 - amountW - (maxScroll ? 12 : 0), kRollRowH, e.name, e.tier, fmt);
				if (!amount.empty()) {
					p.Font(3);
					p.Text(x + w2 - amountW - (maxScroll ? 12 : 0), ry, amountW, kRollRowH, amount, kText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
				}
			}
			p.Unclip();
			if (maxScroll > 0) {
				const int trackH = listH, thumbH = std::max(30, trackH * listH / (listH + maxScroll));
				const int thumbY = y + (trackH - thumbH) * scroll / maxScroll;
				p.Fill(x + w2 - 5, y, 4, trackH, pal::lineDim);
				p.Fill(x + w2 - 5, thumbY, 4, thumbH, kGold);
			}
			else if (rows <= 0) {
				p.Font(3);
				p.Text(x, y, w2, 30, "Only three patrons so far: there is room for yours.", kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
		}
		p.Button(x, H - 60, 220, 38, "See it on the website", kWholeRoll, 0, mx, my, true, false);
		std::lock_guard<std::mutex> lock(g_lock);
		g_rollScroll = scroll;
		g_rollScrollMax = maxScroll;
	}
	p.Button(kPanelW - 28 - 140, H - 60, 140, 38, "Close", kClose, 0, mx, my, true, true);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_hits = std::move(r.hits);
	g_panel = {ox, oy, ox + pw, oy + ph};
}

bool SupportPanelOpen()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_open;
}

bool SupportHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	std::unique_lock<std::mutex> lock(g_lock);
	if (msg == WM_MOUSEMOVE) {
		g_mx = static_cast<short>(LOWORD(lp));
		g_my = static_cast<short>(HIWORD(lp));
	}
	if (!g_open) {
		if (GetTickCount() - g_cornerTick >= 300) return false;
		if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
			const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
			if (PtInRect(&g_heart, pt)) {
				g_open = true;
				g_tab = 0;
				g_rollScroll = 0;
				g_tabAction = -1;
				g_swallowUp = true;
				g_copied = -1;
				return true;
			}
		}
		if (msg == WM_LBUTTONUP && g_swallowUp) {
			g_swallowUp = false;
			return true;
		}
		return false;
	}
	auto perform = [&](int action) {
		if (action == kClose) {
			g_open = false;
			g_swallowUp = true;
			return;
		}
		if (action == kOpenLogs) {
			ReportOpenLogsFolder();
			return;
		}
		if (action == kMakeReport) {
			ReportStart();
			return;
		}
		if (action == kTabSupport || action == kTabRoll) {
			g_tab = action == kTabRoll ? 1 : 0;
			g_rollScroll = 0;
			return;
		}
		if (action == kWholeRoll) {
			OpenLink(Tr("https://imperivmresurrected.com/en/supporters.html"));
			return;
		}
		if (action >= kCopyBase) {
			const bool open = action >= kOpenBase;
			const int index = action - (open ? kOpenBase : kCopyBase);
			std::vector<Row> donate, contact;
			BuildRows(donate, contact);
			std::vector<Row> all = donate;
			all.insert(all.end(), contact.begin(), contact.end());
			if (index >= 0 && index < static_cast<int>(all.size())) {
				const Row &r = all[index];
				if (open) {
					OpenLink(r.copy);
				}
				else {
					ClipboardSet(r.copy);
					g_copied = index;
					g_copiedAt = GetTickCount();
				}
			}
		}
	};
	switch (msg) {
	case WM_MOUSEMOVE:
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		g_tabAction = -1;
		int action = kNone;
		for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it) {
			if (PtInRect(&it->r, pt)) {
				action = it->action;
				break;
			}
		}
		if (action == kNone && !PtInRect(&g_panel, pt)) action = kClose;
		perform(action);
		return true;
	}
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) g_escArmed = true;
		else if (wp == VK_TAB) TabStep((GetKeyState(VK_SHIFT) & 0x8000) != 0);
		else if (g_tabAction >= 0 && !(lp & (1 << 30)) && (wp == VK_RETURN || wp == VK_SPACE)) perform(g_tabAction);
		return true;
	case WM_KEYUP:
		if (wp == VK_ESCAPE && g_escArmed) {
			g_escArmed = false;
			g_open = false;
		}
		return true;
	case WM_LBUTTONUP:
		g_swallowUp = false;
		return true;
	case WM_CHAR:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEWHEEL:
		if (g_tab == 1) {
			g_rollScroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * kRollRowH * 2;
			g_rollScroll = std::max(0, std::min(g_rollScroll, g_rollScrollMax));
		}
		return true;
		return true;
	default:
		return false;
	}
}
