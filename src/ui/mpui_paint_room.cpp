// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "game/mappreview.h"
#include "net/rejoin.h"
#include "net/spectator.h"

#include "ui/mpui_internal.h"
#include "ui/patronui.h"
#include "game/gamemodes.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
const int kRoomTabsTop = 78, kRoomTabsH = 32, kRoomTabW = 150, kTableTop = 118, kTableHeadH = 28;
const COLORREF kRowLine = RGB(42, 33, 22);

COLORREF Mix(COLORREF a, COLORREF b, int percent)
{
	percent = std::max(0, std::min(100, percent));
	return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * percent / 100, GetGValue(a) + (GetGValue(b) - GetGValue(a)) * percent / 100,
		GetBValue(a) + (GetBValue(b) - GetBValue(a)) * percent / 100);
}

COLORREF TeamColor(int team)
{
	static const COLORREF kTeams[kRoomTeams] = {RGB(200, 78, 60), RGB(74, 128, 200), RGB(104, 168, 84), RGB(222, 184, 86)};
	return team > 0 && team <= kRoomTeams ? kTeams[team - 1] : pal::lineDim;
}

struct ShapeBuffer {
	HBITMAP bmp = nullptr;
	uint32_t *bits = nullptr;
	int w = 0, h = 0;
};
ShapeBuffer g_shape;

// Shapes that look the same from one frame to the next are drawn once; a room has a few dozen of them.
std::map<std::string, HBITMAP> g_shapeCache;

HBITMAP NewBitmap(int w, int h, uint32_t *&bits)
{
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *raw = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
	if (bmp && !raw) {
		DeleteObject(bmp);
		bmp = nullptr;
	}
	bits = static_cast<uint32_t *>(raw);
	return bmp;
}

// Fills what `inside` covers: a signed distance in design units, positive inside, smoothed over one pixel. A shape with a `name`
// is kept and drawn again from the cache, so `name` must hold everything `inside` depends on, relative to the box.
void PaintShape(Painter &p, int x, int y, int w, int h, COLORREF top, COLORREF bottom, int alpha, const std::function<float(float, float)> &inside,
	const std::string &name = std::string(), int fade = 255)
{
	const RECT r = p.R(x, y, w, h);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	if (pw <= 0 || ph <= 0 || alpha <= 0 || fade <= 0) return;
	alpha = std::min(255, alpha);
	std::string key;
	HBITMAP bmp = nullptr;
	uint32_t *bits = nullptr;
	int stride = 0;
	if (!name.empty()) {
		char tail[112];
		sprintf_s(tail, "|%d|%d|%.3f|%.2f|%.2f|%06x|%06x|%d", pw, ph, p.s, r.left - x * p.s, r.top - y * p.s, static_cast<unsigned>(top),
			static_cast<unsigned>(bottom), alpha);
		key = name + tail;
		const auto it = g_shapeCache.find(key);
		if (it != g_shapeCache.end()) bmp = it->second;
	}
	if (!bmp) {
		if (!key.empty()) {
			if (g_shapeCache.size() >= 256) {
				for (const auto &kv : g_shapeCache) DeleteObject(kv.second);
				g_shapeCache.clear();
			}
			bmp = NewBitmap(pw, ph, bits);
			if (!bmp) return;
			stride = pw;
			g_shapeCache[key] = bmp;
		}
		else {
			if (pw > g_shape.w || ph > g_shape.h) {
				if (g_shape.bmp) DeleteObject(g_shape.bmp);
				g_shape = ShapeBuffer();
				const int bw = std::max(pw, 64), bh = std::max(ph, 64);
				g_shape.bmp = NewBitmap(bw, bh, g_shape.bits);
				if (!g_shape.bmp) return;
				g_shape.w = bw;
				g_shape.h = bh;
			}
			bmp = g_shape.bmp;
			bits = g_shape.bits;
			stride = g_shape.w;
		}
		const float inv = 1.0f / p.s;
		for (int py = 0; py < ph; py++) {
			const COLORREF c = Mix(top, bottom, ph > 1 ? py * 100 / (ph - 1) : 0);
			const uint32_t cr = GetRValue(c), cg = GetGValue(c), cb = GetBValue(c);
			uint32_t *row = bits + static_cast<size_t>(py) * stride;
			const float fy = (r.top + py + 0.5f) * inv;
			for (int px = 0; px < pw; px++) {
				const float d = inside((r.left + px + 0.5f) * inv, fy) * p.s + 0.5f;
				const uint32_t a = d <= 0.0f ? 0 : d >= 1.0f ? static_cast<uint32_t>(alpha) : static_cast<uint32_t>(d * alpha);
				row[px] = (a << 24) | ((cr * a / 255) << 16) | ((cg * a / 255) << 8) | (cb * a / 255);
			}
		}
		GdiFlush();
	}
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, static_cast<BYTE>(std::min(255, fade)), AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

std::string ShapeName(const char *kind, std::initializer_list<float> values)
{
	std::string out = kind;
	char part[24];
	for (float v : values) {
		sprintf_s(part, "|%.2f", v);
		out += part;
	}
	return out;
}

float SegmentDistance(float x, float y, float ax, float ay, float bx, float by)
{
	const float vx = bx - ax, vy = by - ay, wx = x - ax, wy = y - ay;
	const float len = vx * vx + vy * vy;
	const float t = len > 0 ? std::max(0.0f, std::min(1.0f, (wx * vx + wy * vy) / len)) : 0.0f;
	return std::hypot(wx - vx * t, wy - vy * t);
}

void PaintDisc(Painter &p, float cx, float cy, float radius, COLORREF top, COLORREF bottom, int alpha = 255, bool keep = true)
{
	const int x = static_cast<int>(std::floor(cx - radius - 1)), y = static_cast<int>(std::floor(cy - radius - 1));
	const int size = static_cast<int>(std::ceil(radius * 2 + 3));
	PaintShape(p, x, y, size, size, top, bottom, alpha, [=](float px, float py) { return radius - std::hypot(px - cx, py - cy); },
		keep ? ShapeName("disc", {radius, cx - x, cy - y}) : std::string());
}

// A ring of `width` that runs clockwise from the top over `fraction` of the turn.
void PaintRing(Painter &p, float cx, float cy, float radius, float width, COLORREF c, int alpha = 255, float fraction = 1.0f, bool keep = true)
{
	const float outer = radius + width / 2 + 1;
	const int x = static_cast<int>(std::floor(cx - outer)), y = static_cast<int>(std::floor(cy - outer)), size = static_cast<int>(std::ceil(outer * 2 + 1));
	const float end = fraction * 6.2831853f;
	PaintShape(
		p, x, y, size, size, c, c, alpha,
		[=](float px, float py) {
			const float dx = px - cx, dy = py - cy, dist = std::hypot(dx, dy);
			float d = width / 2 - std::fabs(dist - radius);
			if (fraction < 1.0f) {
				float a = std::atan2(dx, -dy);
				if (a < 0) a += 6.2831853f;
				d = std::min(d, std::min(a, end - a) * radius);
			}
			return d;
		},
		keep && fraction >= 1.0f ? ShapeName("ring", {radius, width, cx - x, cy - y}) : std::string());
}

// Light that fades out over `spread` around a circle. `fade` dims all of it at once, so it can breathe without being drawn again.
void PaintHalo(Painter &p, float cx, float cy, float radius, float spread, COLORREF c, int fade)
{
	const float outer = radius + spread;
	const int x = static_cast<int>(std::floor(cx - outer)), y = static_cast<int>(std::floor(cy - outer)), size = static_cast<int>(std::ceil(outer * 2 + 1));
	const float s = p.s;
	PaintShape(
		p, x, y, size, size, c, c, 255,
		[=](float px, float py) {
			const float t = std::max(0.0f, std::min(1.0f, 1.0f - (std::hypot(px - cx, py - cy) - radius) / spread));
			return (t * t - 0.5f) / s;
		},
		ShapeName("halo", {radius, spread, cx - x, cy - y}), fade);
}

void PaintCheckMark(Painter &p, float cx, float cy, float size, float width, COLORREF c, bool keep = true)
{
	const float ax = cx - size * 0.42f, ay = cy + size * 0.02f, bx = cx - size * 0.12f, by = cy + size * 0.30f, ex = cx + size * 0.44f, ey = cy - size * 0.30f;
	const int x = static_cast<int>(std::floor(cx - size)), y = static_cast<int>(std::floor(cy - size)), box = static_cast<int>(std::ceil(size * 2));
	PaintShape(
		p, x, y, box, box, c, c, 255,
		[=](float px, float py) { return width / 2 - std::min(SegmentDistance(px, py, ax, ay, bx, by), SegmentDistance(px, py, bx, by, ex, ey)); },
		keep ? ShapeName("check", {size, width, cx - x, cy - y}) : std::string());
}

void PaintCross(Painter &p, float cx, float cy, float size, float width, COLORREF c)
{
	const float h = size / 2;
	const int x = static_cast<int>(std::floor(cx - size)), y = static_cast<int>(std::floor(cy - size)), box = static_cast<int>(std::ceil(size * 2));
	PaintShape(
		p, x, y, box, box, c, c, 255,
		[=](float px, float py) {
			return width / 2 - std::min(SegmentDistance(px, py, cx - h, cy - h, cx + h, cy + h), SegmentDistance(px, py, cx - h, cy + h, cx + h, cy - h));
		},
		ShapeName("cross", {size, width, cx - x, cy - y}));
}

// Where each column of the player table starts; the player's column takes what the others leave.
struct Columns {
	int player = 0, playerW = 0, team = 0, teamW = 116, civ = 0, civW = 170, color = 0, colorW = 46, bonus = 0, bonusW = 140, ready = 0, readyW = 52,
		kick = 0, kickW = 24;
	bool bonusOn = false, readyOn = false;
};

Columns TableColumns(int x, int w, bool bonusOn, bool readyOn)
{
	Columns c;
	c.bonusOn = bonusOn;
	c.readyOn = readyOn;
	int right = x + w - 10;
	c.kick = right - c.kickW;
	right = c.kick - 8;
	if (readyOn) {
		c.ready = right - c.readyW;
		right = c.ready - 10;
	}
	if (bonusOn) {
		c.bonus = right - c.bonusW;
		right = c.bonus - 12;
	}
	c.color = right - c.colorW;
	right = c.color - 12;
	c.civ = right - c.civW;
	right = c.civ - 12;
	c.team = right - c.teamW;
	right = c.team - 16;
	c.player = x + 12;
	c.playerW = right - c.player;
	return c;
}

struct Arrival {
	std::string name, status;
	uint64_t id = 0;
	bool Empty() const { return name.empty(); }
};

struct RoomFacts {
	bool load = false;
	int humans = 0, readyCount = 0, positions = 0, withoutSlot = 0;
	bool saveReady = true, mapReady = true, allReady = false, canStart = false;
	StartBlock block;
	int hostSlot = -1;
};

RoomFacts Facts(const State &st)
{
	const GameRoomSnapshot &g = st.game;
	RoomFacts f;
	f.load = g.online && (g.host ? !st.hostSave.name.empty() : !st.room.info.saveName.empty());
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &s = g.slots[i];
		if (s.exists) f.positions++;
		if (!s.used || s.type != 3) continue;
		f.humans++;
		if (s.ready) f.readyCount++;
		if (!s.exists) f.withoutSlot++;
	}
	f.saveReady = g.host || st.room.info.saveName.empty() || st.save.ready;
	f.mapReady = st.map.state == 0 && f.saveReady;
	f.allReady = f.humans > 0 && f.readyCount == f.humans;
	if (g.host) {
		f.block = StartBlocker(st, g);
		f.canStart = f.allReady && f.block.toast.empty();
	}
	if (g.online && g.host) {
		f.hostSlot = g.localSlot & 7;
	}
	else if (g.online) {
		for (const OnlineMember &m : st.room.members) {
			if (!m.owner) continue;
			for (int i = 0; i < 8; i++) {
				if (g.slots[i].used && g.slots[i].type == 3 && PlayerNameMatches(g.slots[i].name, m.name)) f.hostSlot = i;
			}
		}
	}
	return f;
}

// Who is on the way into the room, put on the free slots they will most likely take.
void PlaceArrivals(const State &st, const RoomFacts &f, Arrival arriving[8])
{
	const GameRoomSnapshot &g = st.game;
	for (uint64_t &id : g_flow.arrivingIds) id = 0;
	if (!g.online) return;
	std::vector<Arrival> waiting;
	for (const OnlineMember &m : st.room.members) {
		if (m.me) continue;
		bool inGame = false;
		for (const GameRoomSlot &s : g.slots) {
			if (s.used && s.type == 3 && PlayerNameMatches(s.name, m.name)) inGame = true;
		}
		if (inGame) continue;
		waiting.push_back({m.name, m.receiving >= 0 ? TrFmt("Downloading content: %d %%", {std::to_string(m.receiving)}) : Tr("Joining the room..."), m.id});
	}
	for (const OnlineMember &m : st.room.restarting) waiting.push_back({m.name, Tr("Restarting the game with the content..."), m.id});
	auto freeSlot = [&](int i) { return !g.slots[i].used && !g.slots[i].local && g.slots[i].type == 0 && arriving[i].Empty(); };
	auto place = [&](int i, const Arrival &a) {
		arriving[i] = a;
		g_flow.arrivingIds[i] = a.id;
	};
	std::vector<bool> placed(waiting.size(), false);
	if (f.load) {
		for (size_t w = 0; w < waiting.size(); w++) {
			for (size_t j = 0; j < st.savePlayers.size() && j < 8; j++) {
				const MpSavePlayer &sp = st.savePlayers[j];
				if (!sp.used() || sp.ai() || _stricmp(sp.name.c_str(), waiting[w].name.c_str()) != 0) continue;
				if (freeSlot(static_cast<int>(j))) {
					place(static_cast<int>(j), waiting[w]);
					placed[w] = true;
				}
				break;
			}
		}
	}
	size_t next = 0;
	for (int i = 0; i < 8; i++) {
		while (next < waiting.size() && placed[next]) next++;
		if (next >= waiting.size()) break;
		if (g.slots[i].joining && freeSlot(i)) {
			place(i, waiting[next]);
			placed[next] = true;
		}
	}
	next = 0;
	for (int i = 0; i < 8; i++) {
		while (next < waiting.size() && placed[next]) next++;
		if (next >= waiting.size()) break;
		if (freeSlot(i)) {
			place(i, waiting[next]);
			next++;
		}
	}
}

std::string FirstLetter(const std::string &name)
{
	if (name.empty()) return "?";
	const unsigned char lead = static_cast<unsigned char>(name[0]);
	const size_t n = lead < 0x80 ? 1 : lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
	return name.substr(0, std::min(n, name.size()));
}

void PaintMonogram(Painter &p, int x, int y, int size, const std::string &name)
{
	p.FillV(x, y, size, size, RGB(70, 54, 32), RGB(44, 33, 20));
	p.Frame(x, y, size, size, pal::edge);
	p.Font(1);
	p.Text(x, y, size, size, FirstLetter(name), kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// The avatar, the patron's laurel and the name, with `sub` under the name in small type.
void PaintWho(Painter &p, const State &st, int x, int cy, int w, uint64_t who, const std::string &name, const std::string &sub, COLORREF subColor, bool local,
	bool avatar, int profileArg = -1)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int size = 26;
	int tx = x;
	if (avatar) {
		if (!PaintAvatar(p, x, cy - size / 2, size, who)) PaintMonogram(p, x, cy - size / 2, size, name);
		tx += size + 8;
	}
	const PatronInfo patron = who ? PatronOf(who) : PatronInfo();
	const int laurel = PatronLaurelSize(patron.tier);
	if (patron.tier != PatronTier::None) tx += laurel + 4;
	const int nameW = std::max(20, x + w - tx);
	const int nameY = sub.empty() ? cy - 11 : cy - 16, nameH = sub.empty() ? 22 : 18;
	const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	p.Font(local ? 1 : 0);
	if (patron.tier != PatronTier::None) {
		const int lx = tx - laurel - 4, ly = cy - laurel / 2;
		DrawPatron(p, lx, ly, laurel, tx, nameY, nameW, nameH, name, patron.tier, fmt);
		PatronHotspot(p, lx, ly, laurel, patron, mx, my, kPatronLink);
	}
	else {
		p.Text(tx, nameY, nameW, nameH, name, local ? kGold : kText, fmt);
	}
	if (who && profileArg >= 0) {
		if (avatar) p.AddHit(x, cy - size / 2, size, size, kProfile, profileArg);
		ProfileHotspot(p, st, tx, nameY, nameW, nameH, name, profileArg);
	}
	if (!sub.empty()) {
		p.Font(3);
		p.Text(tx, cy + 2, nameW, 14, sub, subColor, fmt);
	}
}

const DWORD kHoverMs = 120;

void PaintReadyCheck(Painter &p, int cx, int cy, bool ready, bool hot)
{
	const float r = 10.5f, fx = static_cast<float>(cx), fy = static_cast<float>(cy);
	if (ready) {
		PaintDisc(p, fx, fy, r, hot ? RGB(150, 206, 120) : RGB(124, 184, 96), hot ? RGB(84, 140, 64) : RGB(64, 116, 50));
		PaintCheckMark(p, fx, fy, 12.0f, 2.6f, RGB(20, 34, 16));
	}
	else {
		PaintRing(p, fx, fy, r - 0.5f, 1.6f, hot ? pal::focus : pal::edge);
	}
}

void PaintKick(Painter &p, const State &st, int x, int cy, int action, int arg)
{
	const int size = 24;
	const bool hov = p.Hover(x, cy - size / 2, size, size, st.mouseX, st.mouseY);
	if (hov) PaintDisc(p, x + size / 2.0f, static_cast<float>(cy), 11.0f, RGB(90, 40, 30), RGB(66, 28, 22));
	PaintCross(p, x + size / 2.0f, static_cast<float>(cy), 9.0f, 2.0f, hov ? RGB(255, 190, 170) : kDim);
	p.AddHit(x, cy - size / 2, size, size, action, arg);
}

struct TableCtx {
	Columns c;
	RoomFacts f;
	int x = 0, w = 0;
	bool moving = false;
};

// The row under the mouse, so its highlight can fade in.
struct RoomAnimState {
	int hoverRow = -1;
	DWORD hoverAt = 0;
};
RoomAnimState g_anim;

DWORD Since(DWORD at)
{
	return at ? GetTickCount() - at : 0xffffffffu;
}

void PaintSlotRow(Painter &p, const State &st, const TableCtx &t, int i, const Arrival &arrival, int rowY, int rowH, int hoverPercent)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const GameRoomSlot &s = g.slots[i];
	const Columns &c = t.c;
	const bool load = t.f.load, moving = t.moving;
	const int cy = rowY + rowH / 2, ctlY = cy - 12;
	const bool hovered = p.Hover(t.x + 1, rowY, t.w - 2, rowH, mx, my);
	const bool human = s.used && (s.local || (g.online && s.type == 3));
	const uint64_t who = !arrival.Empty() ? arrival.id : human ? (s.local ? SteamOwnId() : MemberIdByName(st, s.name)) : 0;

	const bool mine = s.local && s.used;
	if (mine) p.Fill(t.x + 1, rowY, t.w - 2, rowH, RGB(46, 36, 22));
	if (hovered) p.Fill(t.x + 1, rowY, t.w - 2, rowH, Mix(mine ? RGB(46, 36, 22) : RGB(25, 19, 13), mine ? RGB(62, 49, 31) : RGB(42, 33, 22), hoverPercent));
	if (mine) p.Fill(t.x + 1, rowY, 3, rowH, kGold);
	if (human) DrawPatronRowGlow(p, t.x + 1, rowY, t.w - 2, rowH, PatronOf(who).tier);
	p.Fill(t.x + 8, rowY + rowH - 1, t.w - 16, 1, kRowLine);
	if (moving && i == st.moveFrom) p.Frame(t.x + 2, rowY, t.w - 4, rowH, kGold);

	// The seat buttons end the player's column, or sit in the empty part of a free slot's row. On a player's row they show under the
	// mouse, and always while the host moves a player.
	const bool compact = !s.used && arrival.Empty();
	const int seatW = 62, seatX = compact ? c.civ : c.player + c.playerW - seatW;
	std::string seatLabel;
	int seatAction = 0;
	bool seatStrong = false, seatAlways = false;
	if (moving) {
		if (i == st.moveFrom) {
			seatLabel = "Cancel";
			seatAction = kGMove;
			seatAlways = true;
		}
		else if (arrival.Empty() && s.exists && s.type != 4 && s.type != 2) {
			seatLabel = "Here";
			seatAction = kGMoveTo;
			seatStrong = seatAlways = true;
		}
	}
	else if (arrival.Empty() && s.used && s.type == 3) {
		if (g.host) {
			seatLabel = "Move";
			seatAction = kGMove;
		}
		else if (!s.local && g.online) {
			const bool asked = st.swapPendingTo && st.swapPendingName == s.name && GetTickCount() - st.swapPendingTime < 20000;
			seatLabel = asked ? "Asked…" : "Swap";
			seatAction = asked ? -1 : kGSwapAsk;
			seatAlways = asked;
		}
	}
	else if (arrival.Empty() && SeatTakeable(s) && !s.local) {
		const bool canTake = s.used || load || (g.host ? s.aiAllowed : g.online && s.aiAllowed && s.exists);
		if (canTake) {
			seatLabel = "Take";
			seatAction = kGSeat;
			seatAlways = !s.used;
		}
	}
	const bool seatShown = !seatLabel.empty() && (seatAlways || hovered);
	auto paintSeat = [&]() {
		if (!seatShown) return;
		if (seatAction < 0) {
			p.Font(3);
			p.Text(seatX, cy - 11, seatW, 22, seatLabel, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		else {
			p.Button(seatX, cy - 11, seatW, 22, seatLabel, seatAction, i, mx, my, true, seatStrong, 3);
		}
	};
	const int nameRight = compact ? c.civ - 12 : seatShown ? seatX - 8 : c.player + c.playerW;
	const UINT line = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;

	if (!arrival.Empty()) {
		PaintWho(p, st, c.player, cy, nameRight - c.player, arrival.id, arrival.name, arrival.status, kDim, false, true);
		if (c.readyOn) p.Spinner(c.ready + c.readyW / 2, cy, 8, 3, GetTickCount());
		if (g.host) PaintKick(p, st, c.kick, cy, kGKickJoining, i);
		return;
	}

	if (!s.used) {
		p.Font(3);
		const int textX = c.player;
		if (g.host && !s.local && (s.type == 0 || s.type == 2) && load) {
			const bool saved = i < static_cast<int>(st.savePlayers.size()) && st.savePlayers[i].used() && !st.savePlayers[i].ai();
			const std::string text = s.type == 2 ? std::string("Closed") : saved ? Tr("Slot for ") + st.savePlayers[i].name : std::string("Open slot");
			p.Text(textX, cy - 11, nameRight - textX, 22, text, kDim, line);
		}
		else if (!s.exists || s.type == 4) {
			p.Text(textX, cy - 11, nameRight - textX, 22, "This map has no more positions", kDim, line);
		}
		else if (g.host && !s.local && (s.aiAllowed || g.online)) {
			int sel = 0;
			const std::vector<SlotChoice> choices = SlotChoices(g, s, sel);
			const int comboW = 170;
			Combo(p, st, textX, ctlY, comboW, 24, choices[sel].label, kDropSlot, i, choices.size() > 1 && !moving && !(g.online && LocalReady(g)));
			if (s.type == 0 && g.online) {
				p.Font(3);
				p.Text(textX + comboW + 14, cy - 11, nameRight - textX - comboW - 14, 22, "Waiting for a player...", kDim, line);
			}
		}
		else if (load && s.type == 0) {
			const std::string owner = SaveSlotNameFromOptions(st.room.info.optionsText, i);
			p.Text(textX, cy - 11, nameRight - textX, 22, owner.empty() ? std::string("Open slot") : Tr("Slot for ") + owner, kDim, line);
		}
		else {
			p.Text(textX, cy - 11, nameRight - textX, 22, s.type == 2 ? "Closed" : "Waiting for a player...", kDim, line);
		}
		paintSeat();
		return;
	}

	if (s.type == 1 && g.host && !s.local && !load) {
		int sel = 0;
		const std::vector<SlotChoice> choices = SlotChoices(g, s, sel);
		Combo(p, st, c.player, ctlY, std::min(190, nameRight - c.player), 24, choices[sel].label, kDropSlot, i,
			choices.size() > 1 && !moving && !(g.online && LocalReady(g)));
	}
	else if (s.type == 1) {
		p.Font(0);
		p.Text(c.player, cy - 11, nameRight - c.player, 22, SlotLabel(s), kText, line);
	}
	else {
		PaintWho(p, st, c.player, cy, nameRight - c.player, who, SlotLabel(s), i == t.f.hostSlot ? std::string("Host") : std::string(), pal::section, s.local,
			human, kProfileRoomSlot + i);
	}
	paintSeat();

	const bool own = s.local || (g.host && s.type == 1);
	const bool frozen = SlotFrozen(g, s);
	{
		const int team = s.team > 0 && s.team <= kRoomTeams ? s.team : 0;
		const std::string text = team ? TrFmt("Team %d", {std::to_string(team)}) : Tr("No team");
		if (!load && own && !frozen) {
			Combo(p, st, c.team, ctlY, c.teamW, 24, text, kDropTeam, i, true);
		}
		else {
			p.Font(0);
			p.Text(c.team + 8, ctlY, c.teamW - 8, 24, text, team ? kText : kDim, line);
		}
		if (team) p.Fill(c.team + 2, ctlY + 26, c.teamW - 4, 2, TeamColor(team));
	}
	{
		const bool editable = RaceEditable(g, s, load) && !frozen;
		const bool hidden = (g.rules & kRuleBlindPick) && !load && !s.local && !(g.host && s.type == 1);
		const std::string race = hidden ? "Hidden"
			: s.race >= 0 && s.race < static_cast<int>(s.raceNames.size()) ? (RaceSeparator(s.raceNames) && s.race == 9 ? "—" : s.raceNames[s.race])
																			: "?";
		if (editable) {
			Combo(p, st, c.civ, ctlY, c.civW, 24, race, kDropRace, i, true);
		}
		else {
			p.Font(0);
			p.Text(c.civ + 8, ctlY, c.civW - 8, 24, race, hidden ? kDim : kText, line);
		}
	}
	{
		const bool editable = !load && own && !frozen;
		const int sw = 34, sh = 22, sx = c.color + (c.colorW - sw) / 2, sy = cy - sh / 2;
		const bool hot = editable && p.Hover(sx, sy, sw, sh, mx, my);
		p.Fill(sx - 1, sy - 1, sw + 2, sh + 2, RGB(10, 8, 6));
		p.Fill(sx, sy, sw, sh, PlayerColor(g, s.color));
		p.Frame(sx, sy, sw, sh, hot ? pal::focus : editable ? kGold : pal::edge);
		if (editable) {
			p.Fill(sx + sw - 10, sy + sh - 7, 9, 6, RGB(20, 15, 10));
			p.ArrowDown(sx + sw - 9, sy + sh - 6, 7, 4, hot ? pal::focus : pal::goldLight);
			p.AddHit(sx, sy, sw, sh, kGColor, i);
		}
		if (st.dropOpen && st.dropKind == kDropColor && st.dropIndex == i) g_dropAnchor = {sx, sy, sx + sw, sy + sh};
	}
	if (c.bonusOn) {
		const bool known = s.bonus > 0 && s.bonus < static_cast<int>(g.bonuses.size());
		const std::string bonus = s.bonus <= 0 ? "—" : known ? g.bonuses[s.bonus].name : "?";
		if (!load && g.bonuses.size() > 1 && !GameModeLocksOption(GameModeFor(g.mode.c_str()), 0x10a7) && own && !frozen) {
			Combo(p, st, c.bonus, ctlY, c.bonusW, 24, bonus, kDropBonus, i, true);
		}
		else {
			p.Font(0);
			p.Text(c.bonus + 8, ctlY, c.bonusW - 8, 24, bonus, s.bonus <= 0 ? kDim : kText, line);
		}
	}
	if (c.readyOn) {
		const int rx = c.ready + c.readyW / 2;
		if (s.type == 3 && !s.exists) {
			PaintBadge(p, c.ready - 6, cy - 10, c.readyW + 12, 20, "No slot", RGB(90, 36, 30), RGB(255, 200, 190));
		}
		else if (s.type == 1) {
			p.Font(3);
			p.Text(c.ready, cy - 10, c.readyW, 20, "AI", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		else {
			const bool toggles = s.local && (t.f.mapReady || s.ready) && !(st.map.needsPack && !g.host);
			const bool hot = toggles && p.Hover(rx - 13, cy - 13, 26, 26, mx, my);
			PaintReadyCheck(p, rx, cy, s.ready, hot);
			if (toggles) p.AddHit(rx - 13, cy - 13, 26, 26, kGReady, 0);
		}
	}
	if (g.host && g.online && s.type == 3 && !s.local && !moving) PaintKick(p, st, c.kick, cy, kGKick, i);
}

void PaintPlayerTable(Painter &p, const State &st, const RoomFacts &f, int x, int y, int w, int h)
{
	const GameRoomSnapshot &g = st.game;
	PaintCard(p, x, y, w, h);
	TableCtx t;
	t.f = f;
	t.x = x;
	t.w = w;
	t.moving = g.host && st.moveFrom >= 0 && st.moveFrom < 8;
	t.c = TableColumns(x, w, !BonusesDisabled(g), g.online);
	const Columns &c = t.c;
	p.Font(4);
	const int hy = y + 4, hh = kTableHeadH - 6;
	const UINT head = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, center = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
	p.Text(c.player, hy, c.playerW, hh, "Player", pal::section, head);
	p.Text(c.team + 8, hy, c.teamW - 8, hh, "Team", pal::section, head);
	p.Text(c.civ + 8, hy, c.civW - 8, hh, "Civilization", pal::section, head);
	p.Text(c.color - 10, hy, c.colorW + 20, hh, "Color", pal::section, center);
	if (c.bonusOn) p.Text(c.bonus + 8, hy, c.bonusW - 8, hh, "Bonus", pal::section, head);
	if (c.readyOn) p.Text(c.ready - 10, hy, c.readyW + 20, hh, "Ready", pal::section, center);
	p.Fill(x + 8, y + kTableHeadH - 1, w - 16, 1, pal::line);

	Arrival arriving[8];
	PlaceArrivals(st, f, arriving);
	const int playerH = g.online ? 36 : 42, compactH = g.online ? 30 : 38;
	int rowY = y + kTableHeadH + 2, hovered = -1;
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &s = g.slots[i];
		if (!s.exists && !s.used && arriving[i].Empty()) continue;
		const int rowH = s.used || !arriving[i].Empty() ? playerH : compactH;
		if (p.Hover(x + 1, rowY, w - 2, rowH, st.mouseX, st.mouseY)) hovered = i;
		if (hovered == i && g_anim.hoverRow != i) g_anim.hoverAt = GetTickCount();
		const int hoverPercent = static_cast<int>(std::min<DWORD>(kHoverMs, Since(g_anim.hoverAt)) * 100 / kHoverMs);
		PaintSlotRow(p, st, t, i, arriving[i], rowY, rowH, hoverPercent);
		rowY += rowH;
	}
	g_anim.hoverRow = hovered;
}

void PaintRoomHeader(Painter &p, const State &st, const RoomFacts &f)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const std::string title = !g.online ? Tr("Skirmish") : st.room.id ? st.room.info.name : Tr("Local network game");
	const int sideW = 150, areaW = kW - 2 * sideW;
	constexpr int branchW = 58, branchH = 36, gap = 22;
	p.Font(5);
	const int textW = p.TextWidth(title);
	if (textW <= areaW - 2 * (branchW + gap)) {
		p.Text((kW - textW) / 2 - 4, 4, textW + 8, 44, title, pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		PaintOrnament(p, kOrnamentLaurel, (kW - textW) / 2 - gap - branchW, 8, branchW, branchH, pal::goldLight);
		PaintOrnament(p, kOrnamentLaurel, (kW + textW) / 2 + gap, 8, branchW, branchH, pal::goldLight, true);
	}
	else {
		p.Text(sideW, 4, areaW, 44, title, pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	std::string info = RoomMapLabel(st);
	auto add = [&](const std::string &part) {
		if (!part.empty()) info += (info.empty() ? "" : "  ·  ") + part;
	};
	add(RoomOptionText(st, 0x1002, "Game type"));
	if (g.online) add(Tr(st.room.id ? "Online" : "Local network"));
	if (f.positions < 8) add(Tr("Map for ") + std::to_string(f.positions));
	if (g.online) add(TrFmt("%d of %d ready", {std::to_string(f.readyCount), std::to_string(f.humans)}));
	p.Font(3);
	if (f.withoutSlot) {
		p.Text(sideW, 46, areaW, 18, Tr("The map is for ") + std::to_string(f.positions) + Tr(" players"), kBad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else {
		p.Text(sideW, 46, areaW, 18, info, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	PaintOrnament(p, kOrnamentRule, 18, 64, kW - 36, 10, kGold);

	p.Button(kW - 24 - 112, 14, 112, 32, "Leave", kGCancel, 0, mx, my);
	if (g.online && st.room.id) p.Button(24, 14, 112, 32, "Invite", kInvite, 0, mx, my, !RoomHasNoSeat(st));
	PaintTabs(p, (kW - 2 * kRoomTabW - 6) / 2, kRoomTabsTop, {{"Game", kGTab, 1, st.roomTab == 1, true}, {"Room", kGTab, 0, st.roomTab != 1, true}}, mx, my,
		kRoomTabW, kRoomTabsH);
	p.Fill(16, kRoomTabsTop + kRoomTabsH, kW - 32, 1, pal::lineDim);
}

}

void PaintMapThumb(Painter &p, const State &st, int x, int y, int size, bool markers)
{
	const GameRoomSnapshot &g = st.game;
	p.Fill(x, y, size, size, pal::well);
	const std::shared_ptr<const MapPreviewData> d = g.scenario.empty() ? nullptr : MapPreviewGet(g.scenario);
	if (d && !d->failed && d->image.w > 0 && d->image.h > 0) {
		int iw = size, ih = size * d->image.h / d->image.w;
		if (ih > size) {
			iw = size * d->image.w / d->image.h;
			ih = size;
		}
		const int ix = x + (size - iw) / 2, iy = y + (size - ih) / 2;
		p.Image(ix, iy, iw, ih, d->image.bgra.data(), d->image.w, d->image.h);
		for (const MapPlayer &pl : d->info.players) {
			if (!markers || !pl.hasStart || !pl.human || d->info.units <= 0) continue;
			const float cx = ix + static_cast<float>(pl.x) * iw / d->info.units, cy = iy + static_cast<float>(pl.y) * ih / d->info.units;
			const bool seated = pl.slot >= 0 && pl.slot < 8 && g.slots[pl.slot].used;
			PaintDisc(p, cx, cy, 6.5f, RGB(10, 8, 6), RGB(10, 8, 6));
			const COLORREF c = seated ? PlayerColor(g, g.slots[pl.slot].color) : RGB((pl.color >> 16) & 255, (pl.color >> 8) & 255, pl.color & 255);
			PaintDisc(p, cx, cy, 5.0f, c, c);
		}
	}
	else {
		p.FillV(x + 1, y + 1, size - 2, size - 2, RGB(36, 28, 18), RGB(18, 14, 9));
		const int branch = size * 2 / 5;
		PaintOrnament(p, kOrnamentLaurel, x + size / 2 - branch - 2, y + size / 2 - branch / 3 - 4, branch, branch * 2 / 3, pal::lineDim);
		PaintOrnament(p, kOrnamentLaurel, x + size / 2 + 2, y + size / 2 - branch / 3 - 4, branch, branch * 2 / 3, pal::lineDim, true);
		p.Diamond(x + size / 2, y + size / 2 - 4, 4, pal::bronze);
		if (g.randomMap && size >= 64) {
			p.Font(3);
			p.Text(x + 4, y + size - 24, size - 8, 18, "Random", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	p.Frame(x, y, size, size, pal::line);
}

// What the player must know before pressing Ready: the saved game, and the scenario on its way.
std::string RoomNotice(const State &st, COLORREF &color)
{
	const GameRoomSnapshot &g = st.game;
	color = kDim;
	if (!g.host && st.map.state == 1) {
		const uint64_t total = std::max<uint64_t>(1, st.map.total);
		color = kGold;
		return Tr("Receiving scenario \"") + st.map.name + "\": " + std::to_string(std::min<uint64_t>(100, st.map.got * 100 / total)) + " %";
	}
	if (!g.host && st.map.state == 2) {
		color = kBad;
		if (st.map.needsPack) return TrFmt("This room's map comes from \"%s\", which you do not have loaded.", {st.map.packName});
		return Tr("Couldn't receive the scenario: ") + st.map.error;
	}
	const std::string saveName = g.host ? st.hostSave.name : st.room.info.saveName;
	if (!g.online || saveName.empty()) return "";
	color = kGold;
	std::string text = Tr("Saved game: ") + saveName;
	if (g.host && !st.saveMapError.empty()) {
		color = kBad;
		text += "  ·  " + Tr("Map: ") + st.saveMapError;
	}
	else if (g.host && st.saveMapStep < 2) {
		text += "  ·  " + Tr("Choosing the game's map...");
	}
	else if (!g.host && st.save.state == 1) {
		const uint64_t total = std::max<uint64_t>(1, st.save.total);
		text += "  ·  " + Tr("Receiving it from the host: ") + std::to_string(std::min<uint64_t>(100, st.save.got * 100 / total)) + " %";
	}
	else if (!g.host && st.save.state == 2) {
		color = kBad;
		text += "  ·  " + Tr("Could not receive: ") + st.save.error;
	}
	return text;
}

namespace {

void PaintSummary(Painter &p, const State &st, int x, int y, int w, int h)
{
	const bool hot = p.Hover(x, y, w, h, st.mouseX, st.mouseY);
	PaintCard(p, x, y, w, h);
	if (hot) p.Frame(x, y, w, h, pal::edge);
	const int thumb = std::min(128, h - 24);
	PaintMapThumb(p, st, x + 12, y + 12, thumb, false);
	const int tx = x + 12 + thumb + 14, tw = x + w - 14 - tx;
	int ty = y + 10;
	p.Font(1);
	const std::string map = RoomMapLabel(st);
	p.Text(tx, ty, tw, 22, map.empty() ? std::string("—") : map, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	ty += 26;
	struct Line {
		const char *label;
		int id;
		const char *published;
	};
	static const Line kLines[] = {{"Mode", 0x1002, "Game type"}, {"Size", 0x01000008, "Map size"}, {"Population", 0x1004, "Population"},
		{"Starting gold", 0x1005, "Starting gold"}, {"Limit", 0x1003, "Limit"}};
	COLORREF noticeColor = kDim;
	const std::string notice = RoomNotice(st, noticeColor);
	const int noticeH = notice.empty() ? 0 : 36;
	p.Font(3);
	const int columns = tw > 440 ? 2 : 1, colGap = 24, colW = (tw - (columns - 1) * colGap) / columns, top = ty;
	int shown = 0;
	for (const Line &l : kLines) {
		const int lx = tx + (shown % columns) * (colW + colGap), ly = top + (shown / columns) * 19;
		if (ly + 18 > y + h - 8 - noticeH) break;
		const std::string v = RoomOptionText(st, l.id, l.published);
		p.Text(lx, ly, colW, 18, l.label, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Text(lx, ly, colW, 18, v.empty() ? std::string("—") : v, kText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Fill(lx, ly + 18, colW, 1, kRowLine);
		shown++;
	}
	if (!notice.empty()) {
		p.Clip(tx, y + h - 8 - noticeH, tw, noticeH);
		p.TextWrapped(tx, y + h - 8 - noticeH, tw, noticeH, notice, noticeColor);
		p.Unclip();
	}
	p.AddHit(x, y, w, h, kGTab, 1);
}

// The largest of the button fonts that fits the text in the disc.
void FitFont(Painter &p, const char *text, int width)
{
	for (int font : {2, 1, 3}) {
		p.Font(font);
		if (p.TextWidth(text) <= width) return;
	}
}

void PaintReadyBlock(Painter &p, const State &st, const RoomFacts &f, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const GameRoomSlot &me = g.slots[g.localSlot & 7];
	const int radius = std::min(54, (h - (g.online ? 58 : 8)) / 2), cx = x + w / 2, cy = y + radius + 4;
	const int captionY = cy + radius + 10, captionH = y + h - captionY;
	if (g.online && !g.host && st.map.needsPack) {
		p.Button(x + 8, cy - 22, w - 16, 44, "Download and rejoin", kGResync, 0, mx, my, true, true);
		return;
	}
	enum Look { kAsk, kIsReady, kStart, kWait, kCount };
	const int left = CountdownLeftMs();
	Look look = kAsk;
	int action = kGReady;
	if (left >= 0) look = kCount;
	else if (!g.online) {
		look = kStart;
		action = kGStart;
	}
	else if (me.ready) look = kIsReady;
	else if (!f.mapReady) look = kWait;
	const bool enabled = look != kWait;
	const bool hot = enabled && p.Hover(cx - radius, cy - radius, radius * 2, radius * 2, mx, my);
	const bool down = enabled && st.mouseDown && st.pressAction == action;
	const float fx = static_cast<float>(cx), fy = static_cast<float>(cy + (down ? 1 : 0)), fr = static_cast<float>(radius);
	if (look == kAsk || look == kStart) {
		// A slow breath of light around the button while it waits for the click.
		const float pulse = 0.5f + 0.5f * std::sin(GetTickCount() % 2400 * 6.2831853f / 2400.0f);
		PaintHalo(p, fx, fy, fr - 2, 16.0f, pal::goldLight, static_cast<int>((look == kStart ? 90 : 55) + 60 * pulse));
	}
	PaintDisc(p, fx, fy + 3, fr + 1, pal::shadow, pal::shadow, 160);
	PaintDisc(p, fx, fy, fr, pal::goldLight, pal::bronze);
	switch (look) {
	case kAsk:
	case kStart: PaintDisc(p, fx, fy, fr - 4, hot ? RGB(232, 190, 104) : RGB(212, 166, 80), hot ? RGB(150, 104, 40) : RGB(124, 84, 30)); break;
	case kIsReady: PaintDisc(p, fx, fy, fr - 4, hot ? RGB(70, 104, 54) : RGB(56, 88, 44), hot ? RGB(36, 60, 28) : RGB(28, 48, 22)); break;
	case kCount: PaintDisc(p, fx, fy, fr - 4, RGB(48, 37, 22), RGB(26, 20, 12)); break;
	case kWait: PaintDisc(p, fx, fy, fr - 4, RGB(54, 46, 36), RGB(34, 28, 21)); break;
	}
	PaintRing(p, fx, fy, fr - 7.5f, 1.0f, look == kIsReady ? RGB(140, 190, 110) : look == kWait ? pal::disabledEdge : pal::goldLight, 150);
	const COLORREF ink = RGB(36, 24, 10);
	const int textY = cy + (down ? 1 : 0);
	std::string caption;
	COLORREF captionColor = kDim;
	const std::string count = TrFmt("%d of %d ready", {std::to_string(f.readyCount), std::to_string(f.humans)});
	switch (look) {
	case kAsk:
		FitFont(p, "Ready?", radius * 2 - 16);
		p.Text(cx - radius, textY - 16, radius * 2, 32, "Ready?", ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		caption = count;
		break;
	case kStart:
		FitFont(p, "Start", radius * 2 - 16);
		p.Text(cx - radius, textY - 16, radius * 2, 32, "Start", ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		break;
	case kIsReady: {
		PaintCheckMark(p, fx, fy - 9, radius * 0.62f, 5.0f, RGB(170, 226, 140));
		p.Font(1);
		p.Text(cx - radius, textY + 12, radius * 2, 22, "Ready", RGB(200, 236, 180), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		caption = g.host && !f.block.label.empty() ? f.block.label : count;
		break;
	}
	case kCount: {
		const float done = 1.0f - std::max(0, std::min(5000, left)) / 5000.0f;
		PaintRing(p, fx, fy, fr - 9.0f, 4.0f, pal::goldLight, 255, done);
		p.Font(5);
		p.Text(cx - radius, textY - 22, radius * 2, 44, std::to_string((left + 999) / 1000), pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		caption = "Everyone is ready. Click to cancel";
		captionColor = kText;
		break;
	}
	case kWait:
		p.Spinner(cx, cy, 16, 5, GetTickCount());
		caption = !f.saveReady ? "Waiting for the saved game..." : "Waiting for the scenario...";
		break;
	}
	if (enabled) p.AddHit(cx - radius, cy - radius, radius * 2, radius * 2, action, 0);
	if (!g.online) return;
	if (!caption.empty() && captionH > 10) {
		p.Font(3);
		p.Clip(x, captionY, w, captionH);
		const int th = p.Measure(w - 8, caption);
		const RECT r = p.R(x + 4, captionY, w - 8, th + 2);
		SetTextColor(p.dc, captionColor);
		const std::wstring ws = W(Tr(caption));
		RECT box = r;
		DrawTextW(p.dc, ws.c_str(), static_cast<int>(ws.size()), &box, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
		p.Unclip();
	}
}

void PaintChatBlock(Painter &p, const State &st, int x, int y, int w, int h)
{
	PaintCard(p, x, y, w, h);
	p.Font(3);
	PaintChatLog(p, st, x + 8, y + 8, w - 16, h - 16 - 34);
	PaintField(p, st, x + 8, y + h - 8 - 28, w - 16 - 72, 28, st.chatInput, "Type a message...", st.focus == kChatField, kChatFocus);
	p.Button(x + w - 8 - 66, y + h - 8 - 28, 66, 28, "Send", kChatSend, 0, st.mouseX, st.mouseY, true, false, 4);
}

void PaintRoomTab(Painter &p, const State &st, const RoomFacts &f)
{
	const GameRoomSnapshot &g = st.game;
	const int playerH = g.online ? 36 : 42;
	const int tableH = kTableHeadH + 2 + 8 * playerH + 6;
	PaintPlayerTable(p, st, f, 16, kTableTop, kW - 32, tableH);
	const int by = kTableTop + tableH + 10, bh = kH - 16 - by;
	const int sideW = 360, gap = 12;
	const int readyW = kW - 32 - 2 * (sideW + gap);
	if (g.online) {
		PaintChatBlock(p, st, 16, by, sideW, bh);
		PaintReadyBlock(p, st, f, 16 + sideW + gap, by, readyW, bh);
		PaintSummary(p, st, kW - 16 - sideW, by, sideW, bh);
	}
	else {
		PaintSummary(p, st, 16, by, kW - 32 - readyW - gap, bh);
		PaintReadyBlock(p, st, f, kW - 16 - readyW, by, readyW, bh);
	}
}

void PaintRoomDialogs(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	if (st.dropAsk) {
		const int w = 520, h = 210, x = (kW - w) / 2, y = 180;
		p.hits->clear();
		p.Fill(x - 2, y - 2, w + 4, h + 4, RGB(10, 8, 6));
		p.Fill(x, y, w, h, RGB(34, 27, 18));
		p.Frame(x, y, w, h, kGold);
		p.Font(1);
		p.Text(x + 20, y + 14, w - 40, 26, Tr("This map is for ") + std::to_string(st.dropPositions) + Tr(" players"), kGold);
		p.Font(0);
		std::string who;
		for (size_t i = 0; i < st.dropNames.size(); i++) who += (i ? (i + 1 == st.dropNames.size() ? Tr(" and ") : ", ") : "") + st.dropNames[i];
		p.Text(x + 20, y + 48, w - 40, 90,
			(st.dropNames.empty() ? std::string("There are more players than positions.") : Tr("There is no room for ") + who + ".") +
				Tr(" If you use this map, they will leave the room (they can come back in if you switch to a larger map)."),
			kText, DT_LEFT | DT_WORDBREAK);
		p.Button(x + 20, y + h - 46, 236, 32, "Remove them and use this map", kDropYes, 0, mx, my, true, false);
		p.Button(x + w - 256, y + h - 46, 236, 32, "Keep the map as it was", kDropNo, 0, mx, my, true, true);
	}
	else if (st.swapAskFrom) {
		const int w = 470, h = 170, x = (kW - w) / 2, y = 190;
		p.hits->clear();
		p.Fill(x - 2, y - 2, w + 4, h + 4, RGB(10, 8, 6));
		p.Fill(x, y, w, h, RGB(34, 27, 18));
		p.Frame(x, y, w, h, kGold);
		p.Font(1);
		p.Text(x + 20, y + 14, w - 40, 26, "Slot swap", kGold);
		p.Font(0);
		int theirs = -1;
		for (int i = 0; i < 8; i++) {
			if (g.slots[i].used && g.slots[i].type == 3 && PlayerNameMatches(g.slots[i].name, st.swapAskName)) theirs = i;
		}
		p.Text(x + 20, y + 46, w - 40, 60,
			(theirs >= 0 ? TrFmt("%s wants to swap slots with you (you would move to slot %d).", {st.swapAskName, std::to_string(theirs + 1)})
						 : TrFmt("%s wants to swap slots with you.", {st.swapAskName})),
			kText, DT_LEFT | DT_WORDBREAK);
		p.Button(x + w - 300, y + h - 46, 136, 32, "OK", kSwapAccept, 0, mx, my, true, true);
		p.Button(x + w - 156, y + h - 46, 136, 32, "Decline", kSwapDecline, 0, mx, my);
	}
}

void PaintGameRoom(Painter &p, const State &st)
{
	const RoomFacts f = Facts(st);
	PaintRoomHeader(p, st, f);
	if (st.roomTab == 1) {
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			g_chat.rectValid = false;
		}
		// The host's slot menus and the seat buttons rely on where the arrivals go, also while the table is hidden.
		Arrival arriving[8];
		PlaceArrivals(st, f, arriving);
		PaintGameTab(p, st, kTableTop);
	}
	else {
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			g_optRect = {};
		}
		g_optGeomOut = ListGeom();
		PaintRoomTab(p, st, f);
	}
	PaintRoomDialogs(p, st);
}

void PaintConnecting(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const bool creating = st.autoHost || st.room.owner;
	const bool waiting = !st.connecting && st.room.id && !st.room.info.hosting;
	const Dialog d = DialogBegin(p, kW, kH, 480, 200, creating ? "Creating the game..." : waiting ? "Waiting for the host..." : "Joining the game...");
	PaintBusyBar(p, d.BodyX(), d.y + 72, d.BodyW(), 8, GetTickCount());
	p.Font(3);
	p.Text(d.BodyX(), d.y + 90, d.BodyW(), 24,
		creating ? "Setting up the game room"
		: waiting ? Tr("You'll join as soon as ") + st.room.info.host + Tr(" creates the game")
							: Tr("Connecting to ") + (st.room.info.host.empty() ? std::string("the host") : st.room.info.host) + Tr(" over the Steam network"),
		kDim);
	DialogButtons(p, d, {{"Cancel", kLeave}}, mx, my, true);
}

void PaintSpectate(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const OnlineLobby &g = st.room.info;
	const Dialog d = DialogBegin(p, kW, kH, 520, 188, TrFmt("Watching %s's game", {g.host}));
	p.Font(3);
	p.Text(d.BodyX(), d.y + 56, d.BodyW(), 20, g.name, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const int progress = SpectatorProgress();
	if (progress >= 0) PaintProgress(p, d.BodyX(), d.y + 82, d.BodyW(), 14, progress / 100.0f);
	else PaintBusyBar(p, d.BodyX(), d.y + 86, d.BodyW(), 8, GetTickCount());
	const std::string status = SpectatorStatusText();
	p.Font(0);
	p.Text(d.BodyX(), d.y + 106, d.BodyW(), 26, status.empty() ? std::string("Connecting to the host...") : status, kGold,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DialogButtons(p, d, {{"Leave", kLeave}}, mx, my, true);
}
}
}

namespace mpui_impl {
void PaintScreen(Painter &p, const State &st)
{
	PaintWindowFrame(p, kW, kH);
	p.noHover = st.dropOpen || st.passPrompt || st.syncConfirm || st.syncPhase || st.searching || st.connecting || st.recoverAsk ||
		(st.recoverWaitHost && !st.room.id) || RejoinClientActive() || RejoinClientFailed() || (st.game.active && st.game.picker);
	if (st.game.active) {
		PaintGameRoom(p, st);
		if (st.game.picker && !SaveMapPicking(st)) {
			PaintScenarioPicker(p, st);
			PaintDropList(p, st);
			return;
		}
		PaintDropList(p, st);
		return;
	}
	PaintHeader(p, st);
	if (st.view == kViewRanked) PaintRanked(p, st);
	else if (st.view == kViewCreate) PaintCreate(p, st);

	else PaintList(p, st);
	PaintChat(p, st);
	if (!st.searching && !st.connecting) PaintDropList(p, st);
	if (st.searching) PaintSearch(p, st);
	if (RejoinClientActive() || RejoinClientFailed()) {
		PaintRejoin(p, st);
		return;
	}
	if (st.recoverAsk || (st.recoverWaitHost && !st.room.id)) {
		PaintRecover(p, st);
		return;
	}
	if (st.passPrompt) PaintPassPrompt(p, st);
	else if (st.syncConfirm) PaintSyncConfirm(p, st);
	else if (st.syncPhase) PaintSyncProgress(p, st);
	if (st.room.id && st.room.info.spectate && !st.game.active && !st.syncPhase && !st.syncConfirm) {
		PaintSpectate(p, st);
		return;
	}
	if (st.connecting || (st.room.id && !st.game.active && !st.syncPhase && !st.syncConfirm && !st.gameClosedAt)) PaintConnecting(p, st);
}
}
