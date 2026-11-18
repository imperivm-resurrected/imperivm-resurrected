// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ui/textedit.h"
#include "MinHook.h"
#include "core/names.h"
#include "game/mappreview.h"
#include "net/rejoin.h"
#include "net/spectator.h"

#include "ui/fullscreen.h"
#include "ui/leagueui.h"
#include "ui/leaguerewards.h"
#include "ui/mpui_internal.h"
#include "ui/mpui_tourney.h"
#include "ui/patronui.h"
#include "game/gamemodes.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
const int kTableHeadH = 44, kMinRowH = 44, kMaxRowH = 64, kFieldH = 38, kAvatar = 40, kBlockGap = 40;
const COLORREF kAvatarDisc = RGB(0x2e, 0x29, 0x26);

// Where the parts of the room go, in the units of ScreenView: the header of the full screens, whose `top` is where the table or the Game
// tab starts, and the row of cards at the bottom.
struct RoomLayout : ScreenLayout {
	int bottomY = 0, bottomH = 0;
};

RoomLayout LayoutFor(const State &st)
{
	RoomLayout l;
	static_cast<ScreenLayout &>(l) = ScreenLayoutFor(st.viewW, st.viewH);
	l.bottomH = l.roomy ? 264 : 220;
	l.bottomY = l.h - l.margin - l.bottomH;
	return l;
}

COLORREF Mix(COLORREF a, COLORREF b, int percent)
{
	percent = std::max(0, std::min(100, percent));
	return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * percent / 100, GetGValue(a) + (GetGValue(b) - GetGValue(a)) * percent / 100,
		GetBValue(a) + (GetBValue(b) - GetBValue(a)) * percent / 100);
}

COLORREF TeamColor(int team)
{
	static const COLORREF kTeams[kRoomTeams] = {RGB(200, 78, 60), RGB(74, 128, 200), RGB(104, 168, 84), RGB(222, 184, 86)};
	return team > 0 && team <= kRoomTeams ? kTeams[team - 1] : pal::hairline;
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

// The Steam picture cut to a disc. GDI has no smooth clip, so the circle's coverage goes into the alpha of a copy scaled by hand.
bool PaintRoundAvatar(Painter &p, int x, int y, int size, uint64_t id)
{
	const std::shared_ptr<const OnlineAvatar> a = id ? SteamAvatar(id) : nullptr;
	if (!a || a->width <= 0 || a->height <= 0 || a->bgra.size() < static_cast<size_t>(a->width) * a->height) return false;
	const RECT r = p.R(x, y, size, size);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	if (pw <= 0 || ph <= 0) return true;
	const std::vector<uint8_t> mask = CachedShapeMask(InUnitEllipse, pw, ph);
	if (mask.size() != static_cast<size_t>(pw) * ph) return false;
	uint32_t *bits = nullptr;
	HBITMAP bmp = NewBitmap(pw, ph, bits);
	if (!bmp) return false;
	for (int py = 0; py < ph; py++) {
		const int sy0 = py * a->height / ph, sy1 = std::max(sy0 + 1, (py + 1) * a->height / ph);
		for (int px = 0; px < pw; px++) {
			const int sx0 = px * a->width / pw, sx1 = std::max(sx0 + 1, (px + 1) * a->width / pw);
			uint32_t cr = 0, cg = 0, cb = 0, n = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					const uint32_t c = a->bgra[static_cast<size_t>(sy) * a->width + sx];
					cr += (c >> 16) & 255;
					cg += (c >> 8) & 255;
					cb += c & 255;
					n++;
				}
			}
			const uint32_t al = mask[static_cast<size_t>(py) * pw + px];
			bits[static_cast<size_t>(py) * pw + px] = (al << 24) | ((cr / n * al / 255) << 16) | ((cg / n * al / 255) << 8) | (cb / n * al / 255);
		}
	}
	GdiFlush();
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
	DeleteObject(bmp);
	return true;
}

// The first letter of the first two words; anything that is not a letter or a digit splits words ("[RM]Marcus_Rex" gives "RM").
std::string Initials(const std::string &name)
{
	std::string out;
	int letters = 0;
	bool start = true;
	for (size_t i = 0; i < name.size() && letters < 2;) {
		const unsigned char lead = static_cast<unsigned char>(name[i]);
		const size_t n = lead < 0x80 ? 1 : lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		const bool word = lead >= 0x80 || isalnum(lead);
		if (word && start) {
			out += name.substr(i, std::min(n, name.size() - i));
			letters++;
		}
		start = !word;
		i += n;
	}
	return out.empty() ? "?" : out;
}

void PaintMonogram(Painter &p, int x, int y, int size, const std::string &letters)
{
	PaintDisc(p, x + size / 2.0f, y + size / 2.0f, size / 2.0f, kAvatarDisc, kAvatarDisc);
	SelectObject(p.dc, BrandFont(BrandFace::UiStrong, size * 3 / 8, p.s));
	p.TextCaps(x, y, size, size, letters, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
}

// A value in the box of a dropdown. Only an `editable` one has the chevron and opens with `action`; the others keep a quieter box.
void PaintValueBox(Painter &p, const State &st, int x, int y, int w, const std::string &text, COLORREF ink, bool editable, bool open, int action, int arg,
	COLORREF swatch = CLR_INVALID)
{
	const int h = kFieldH;
	const bool hot = editable && p.Hover(x, y, w, h, st.mouseX, st.mouseY);
	if (editable) p.Fill(x, y, w, h, pal::field);
	p.Frame(x, y, w, h, !editable ? pal::hairline : open || hot ? pal::edgeHot : pal::edge);
	int tx = x + 12;
	if (swatch != CLR_INVALID) {
		const int size = 20, sy = y + (h - size) / 2;
		p.Fill(tx, sy, size, size, swatch);
		p.Frame(tx, sy, size, size, pal::bg);
		tx += size + 10;
	}
	const int right = x + w - (editable ? 28 : 8);
	if (!text.empty() && right > tx) {
		p.Font(0);
		p.Text(tx, y, right - tx, h, text, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	if (editable) {
		p.ArrowDown(x + w - 21, y + h / 2 - 3, 10, 5, open || hot ? pal::text : pal::textDim);
		p.AddHit(x, y, w, h, action, arg);
	}
	if (open) g_dropAnchor = {x, y, x + w, y + h};
}

void SlotCombo(Painter &p, const State &st, int x, int y, int w, const std::string &text, COLORREF ink, int kind, int slot, bool editable)
{
	const bool open = st.dropOpen && st.dropKind == kind && st.dropIndex == slot;
	PaintValueBox(p, st, x, y, w, text, ink, editable, open, kGDropOpen, DropArg(kind, slot));
}

// Where each column of the player table starts; the player's column takes what the others leave.
struct Columns {
	int player = 0, playerW = 0, team = 0, teamW = 0, civ = 0, civW = 0, color = 0, colorW = 0, bonus = 0, bonusW = 0, status = 0, statusW = 0,
		kick = 0, kickW = 32;
	bool statusOn = false;
};

// The columns grow from the narrow widths, a table of 1150 that still fits "No team" and the longest civilization, to the wide ones of
// the full-width table at 1080p; the player's column, where an empty slot or an AI keeps its menu, takes the rest. The bonus column is
// always there, a dash while the game has bonuses off, so the table keeps its shape.
Columns TableColumns(int x, int w, bool statusOn)
{
	Columns c;
	c.statusOn = statusOn;
	const float t = std::max(0.0f, std::min(1.0f, (w - 1150) / 550.0f));
	auto width = [t](int narrow, int wide) { return narrow + static_cast<int>((wide - narrow) * t + 0.5f); };
	c.teamW = width(140, 190);
	c.civW = width(170, 230);
	c.colorW = width(64, 76);
	c.bonusW = width(150, 200);
	c.statusW = width(150, 190);
	const int gap = width(12, 16);
	int right = x + w - 16;
	c.kick = right - c.kickW;
	right = c.kick - 8;
	if (statusOn) {
		c.status = right - c.statusW;
		right = c.status - gap;
	}
	c.bonus = right - c.bonusW;
	right = c.bonus - gap;
	c.color = right - c.colorW;
	right = c.color - gap;
	c.civ = right - c.civW;
	right = c.civ - gap;
	c.team = right - c.teamW;
	right = c.team - 24;
	c.player = x + 20;
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
	int humans = 0, readyCount = 0, ai = 0, positions = 0, withoutSlot = 0;
	bool saveReady = true, mapReady = true, allReady = false, canStart = false;
	StartBlock block;
	int hostSlot = -1;
	// The AI shows the ready check too, so the count the player reads takes it in; starting waits for the humans alone.
	std::string ReadyText() const { return TrFmt("%d of %d ready", {std::to_string(readyCount + ai), std::to_string(humans + ai)}); }
};

RoomFacts Facts(const State &st)
{
	const GameRoomSnapshot &g = st.game;
	RoomFacts f;
	f.load = g.online && (g.host ? !st.hostSave.name.empty() : !st.room.info.saveName.empty());
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &s = g.slots[i];
		if (s.exists) f.positions++;
		if (s.used && s.type == 1) f.ai++;
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

// The avatar, the patron's laurel and the name, with `sub` under the name in small type.
void PaintWho(Painter &p, const State &st, int x, int cy, int w, uint64_t who, const std::string &name, const std::string &sub, COLORREF subColor, bool subCaps,
	int profileArg = -1)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int ay = cy - kAvatar / 2;
	// A player who wears a season's frame shows it here too, around a square picture.
	const LeagueFrame frame = who ? PlayerFrameOf(who) : LeagueFrame();
	if (frame.Shown()) PaintFramedAvatar(p, x, ay, kAvatar, who, name, frame);
	else if (!PaintRoundAvatar(p, x, ay, kAvatar, who)) PaintMonogram(p, x, ay, kAvatar, Initials(name));
	int tx = x + kAvatar + 14;
	const PatronInfo patron = who ? PatronOf(who) : PatronInfo();
	const LeagueBadge badge = who ? PlayerBadgeOf(who) : LeagueBadge();
	const int laurel = PatronLaurelSize(patron.tier);
	if (patron.tier != PatronTier::None) tx += laurel + 4;
	const int nameW = std::max(20, x + w - tx);
	// The badge goes after the name, so the name gives up its room.
	const int textW = std::max(20, nameW - LeagueBadgeRoom(badge));
	const int nameY = sub.empty() ? cy - 13 : cy - 24, nameH = sub.empty() ? 26 : 24;
	const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	p.Font(1);
	if (patron.tier != PatronTier::None) {
		const int lx = tx - laurel - 4, ly = cy - laurel / 2;
		DrawPatron(p, lx, ly, laurel, tx, nameY, textW, nameH, name, patron.tier, fmt);
		PatronHotspot(p, lx, ly, laurel, patron, mx, my, kPatronLink);
	}
	else {
		p.Text(tx, nameY, textW, nameH, name, pal::text, fmt);
	}
	DrawBadgeAfterName(p, tx, nameY, textW, nameH, name, badge, mx, my);
	if (who && profileArg >= 0) {
		p.AddHit(x, ay, kAvatar, kAvatar, kProfile, profileArg);
		ProfileHotspot(p, st, tx, nameY, textW, nameH, name, profileArg);
	}
	if (!sub.empty()) {
		p.Font(3);
		if (subCaps) p.TextCaps(tx, cy + 1, nameW, 19, sub, subColor, fmt, 2);
		else p.Text(tx, cy + 1, nameW, 19, sub, subColor, fmt);
	}
}

const DWORD kHoverMs = 120;
const int kCheckR = 12;

void PaintReadyCheck(Painter &p, int cx, int cy, bool ready, bool hot)
{
	const float r = static_cast<float>(kCheckR), fx = static_cast<float>(cx), fy = static_cast<float>(cy);
	if (ready) {
		const COLORREF fill = hot ? pal::body : pal::text;
		PaintDisc(p, fx, fy, r, fill, fill);
		PaintCheckMark(p, fx, fy, 13.0f, 2.6f, pal::bg);
	}
	else {
		PaintRing(p, fx, fy, r - 1.0f, 2.0f, hot ? pal::text : pal::edgeHot);
	}
}

void PaintStatus(Painter &p, const Columns &c, int cy, bool ready, bool hot)
{
	PaintReadyCheck(p, c.status + kCheckR, cy, ready, hot);
	const int tx = c.status + 2 * kCheckR + 10;
	p.Font(3);
	p.Text(tx, cy - 12, c.status + c.statusW - tx, 24, ready ? "Ready" : "Not ready", ready || hot ? pal::text : pal::caption,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintKick(Painter &p, const State &st, int x, int cy, int action, int arg)
{
	const int size = 32;
	const bool hov = p.Hover(x, cy - size / 2, size, size, st.mouseX, st.mouseY);
	PaintCross(p, x + size / 2.0f, static_cast<float>(cy), 11.0f, 2.0f, hov ? pal::redText : pal::disabled);
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

std::string AiLevel(const GameRoomSlot &s)
{
	const int n = static_cast<int>(s.nameItems.size());
	return s.nameSel >= 0 && s.nameSel < n && !s.nameItems[s.nameSel].empty() ? s.nameItems[s.nameSel] : std::string("—");
}

void PaintSlotRow(Painter &p, const State &st, const TableCtx &t, int i, const Arrival &arrival, int rowY, int rowH, int hoverPercent)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const GameRoomSlot &s = g.slots[i];
	const Columns &c = t.c;
	const bool load = t.f.load, moving = t.moving;
	const int cy = rowY + rowH / 2, ctlY = cy - kFieldH / 2;
	// A position the map does not have keeps its row, greyed and still: the table always has its eight.
	const bool available = s.exists || s.used || !arrival.Empty();
	const bool hovered = available && p.Hover(t.x + 1, rowY, t.w - 2, rowH, mx, my);
	const bool human = s.used && (s.local || (g.online && s.type == 3));
	const uint64_t who = !arrival.Empty() ? arrival.id : human ? (s.local ? SteamOwnId() : MemberIdByName(st, s.name)) : 0;

	const bool mine = s.local && s.used;
	if (mine) p.Fill(t.x + 1, rowY, t.w - 2, rowH, pal::selected);
	if (hovered) p.Fill(t.x + 1, rowY, t.w - 2, rowH, Mix(mine ? pal::selected : pal::panel, mine ? pal::edge : pal::selected, hoverPercent));
	if (human) DrawPatronRowGlow(p, t.x + 1, rowY, t.w - 2, rowH, PatronOf(who).tier);
	p.Fill(t.x + 1, rowY, t.w - 2, 1, pal::hairline);
	if (moving && i == st.moveFrom) p.Frame(t.x + 2, rowY + 1, t.w - 4, rowH - 1, pal::redText);

	// The seat buttons end the player's column, or sit in the empty part of a free slot's row. On a player's row they show under the
	// mouse, and always while the host moves a player.
	const bool compact = !s.used && arrival.Empty();
	const int seatW = 92, seatH = 32, seatX = compact ? c.civ : c.player + c.playerW - seatW;
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
			p.Text(seatX, cy - seatH / 2, seatW, seatH, seatLabel, pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		else {
			p.Button(seatX, cy - seatH / 2, seatW, seatH, seatLabel, seatAction, i, mx, my, true, seatStrong, 4);
		}
	};
	const int nameRight = compact ? c.civ - 16 : seatShown ? seatX - 12 : c.player + c.playerW;
	const UINT line = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	auto waiting = [&]() {
		if (!c.statusOn) return;
		p.Font(3);
		p.Text(c.status, cy - 12, c.kick + c.kickW - c.status, 24, "Waiting for a player...", pal::caption, line);
	};

	if (!arrival.Empty()) {
		PaintWho(p, st, c.player, cy, nameRight - c.player, arrival.id, arrival.name, arrival.status, pal::caption, false);
		if (c.statusOn) p.Spinner(c.status + kCheckR, cy, 10, 4, GetTickCount());
		if (g.host) PaintKick(p, st, c.kick, cy, kGKickJoining, i);
		return;
	}

	if (!s.used && s.scenario) {
		PaintMonogram(p, c.player, cy - kAvatar / 2, kAvatar, Tr("AI"));
		p.Font(0);
		p.Text(c.player + kAvatar + 14, cy - 12, std::max(20, c.player + c.playerW - c.player - kAvatar - 14), 24, "Controlled by the scenario", pal::textDim, line);
		const bool named = s.fixedRace < static_cast<int>(s.raceNames.size());
		p.raw = true;
		p.Text(c.civ + 12, ctlY, c.civW - 12, kFieldH, named ? s.raceNames[s.fixedRace] : std::string("\xE2\x80\x94"), pal::textDim, line);
		p.raw = false;
		const uint32_t mapColor = s.scenarioColor;
		p.Fill(c.color + 12, cy - 8, 16, 16, mapColor ? RGB((mapColor >> 16) & 255, (mapColor >> 8) & 255, mapColor & 255) : PlayerColor(g, s.color));
		p.Frame(c.color + 12, cy - 8, 16, 16, pal::edge);
		return;
	}
	if (!s.used) {
		p.Font(3);
		const int textX = c.player, boxW = std::min(260, nameRight - textX);
		if (g.host && !s.local && (s.type == 0 || s.type == 2) && load) {
			const bool saved = i < static_cast<int>(st.savePlayers.size()) && st.savePlayers[i].used() && !st.savePlayers[i].ai();
			const std::string text = s.type == 2 ? std::string("Closed") : saved ? Tr("Slot for ") + st.savePlayers[i].name : std::string("Open slot");
			p.Text(textX, cy - 12, nameRight - textX, 24, text, pal::caption, line);
		}
		else if (!s.exists || s.type == 4) {
			p.Text(textX, cy - 12, nameRight - textX, 24, "This map has no more positions", pal::disabled, line);
		}
		else if (g.host && !s.local && (s.aiAllowed || g.online)) {
			int sel = 0;
			const std::vector<SlotChoice> choices = SlotChoices(g, s, sel);
			SlotCombo(p, st, textX, ctlY, boxW, s.type == 0 && g.online ? std::string("Open slot") : choices[sel].label, pal::caption, kDropSlot, i,
				choices.size() > 1 && !moving && !(g.online && LocalReady(g)));
			if (s.type == 0 && g.online) waiting();
		}
		else if (load && s.type == 0) {
			const std::string owner = SaveSlotNameFromOptions(st.room.info.optionsText, i);
			p.Text(textX, cy - 12, nameRight - textX, 24, owner.empty() ? std::string("Open slot") : Tr("Slot for ") + owner, pal::caption, line);
		}
		else if (g.online && (s.type == 0 || s.type == 2)) {
			PaintValueBox(p, st, textX, ctlY, boxW, s.type == 2 ? "Closed" : "Open slot", pal::caption, false, false, 0, 0);
			if (s.type == 0) waiting();
		}
		else {
			p.Text(textX, cy - 12, nameRight - textX, 24, s.type == 2 ? "Closed" : "Waiting for a player...", pal::caption, line);
		}
		paintSeat();
		return;
	}

	if (s.type == 1) {
		// The AI's level is one of the slot's own choices, next to Open and Closed, as the game keeps them: its menu stays where the empty
		// slot's was, so choosing an AI does not move it across the row.
		int sel = 0;
		const std::vector<SlotChoice> choices = SlotChoices(g, s, sel);
		const bool editable = g.host && !s.local && !load && choices.size() > 1 && !moving && !(g.online && LocalReady(g)) && !RankedRoomLocked(st);
		SlotCombo(p, st, c.player, ctlY, std::min(300, nameRight - c.player), AiLevel(s), pal::text, kDropSlot, i, editable);
	}
	else {
		PaintWho(p, st, c.player, cy, nameRight - c.player, who, SlotLabel(s), i == t.f.hostSlot ? std::string("Host") : std::string(), pal::redText, true,
			kProfileRoomSlot + i);
	}
	paintSeat();

	const bool own = s.local || (g.host && s.type == 1);
	const bool frozen = SlotFrozen(g, s);
	{
		const int team = s.team > 0 && s.team <= kRoomTeams ? s.team : 0;
		const std::string text = team ? TrFmt("Team %d", {std::to_string(team)}) : Tr("No team");
		SlotCombo(p, st, c.team, ctlY, c.teamW, text, team ? pal::text : pal::textDim, kDropTeam, i, !load && own && !frozen && !RankedRoomLocked(st) && !TourneyTeamsLocked(st));
		if (team) p.Fill(c.team + 1, ctlY + kFieldH - 3, c.teamW - 2, 2, TeamColor(team));
	}
	{
		const bool editable = RaceEditable(g, s, load) && !frozen;
		const bool hidden = (g.rules & kRuleBlindPick) && !load && !s.local && !(g.host && s.type == 1);
		const std::string race = hidden ? "Hidden"
			: s.race >= 0 && s.race < static_cast<int>(s.raceNames.size()) ? (RaceSeparator(s.raceNames) && s.race == 9 ? "—" : s.raceNames[s.race])
																			: "?";
		SlotCombo(p, st, c.civ, ctlY, c.civW, race, hidden ? pal::textDim : pal::text, kDropRace, i, editable);
	}
	{
		const bool editable = !load && own && !frozen && !TeamColorsOn(st, g);
		const bool open = st.dropOpen && st.dropKind == kDropColor && st.dropIndex == i;
		PaintValueBox(p, st, c.color, ctlY, c.colorW, std::string(), pal::text, editable, open, kGColor, i, PlayerColor(g, s.color));
	}
	{
		const bool off = BonusesDisabled(g);
		const bool known = s.bonus > 0 && s.bonus < static_cast<int>(g.bonuses.size());
		const std::string bonus = off || s.bonus <= 0 ? "—" : known ? g.bonuses[s.bonus].name : "?";
		const bool editable = !off && !load && g.bonuses.size() > 1 && !GameModeLocksOption(GameModeFor(g.mode.c_str()), 0x10a7) && own && !frozen;
		SlotCombo(p, st, c.bonus, ctlY, c.bonusW, bonus, s.bonus <= 0 ? pal::textDim : pal::text, kDropBonus, i, editable);
	}
	if (c.statusOn) {
		if (s.type == 3 && !s.exists) {
			PaintBadge(p, c.status, cy - 13, std::min(c.statusW, 100), 26, "No slot", pal::red, pal::body);
		}
		else if (s.type == 1) {
			PaintStatus(p, c, cy, true, false);
		}
		else {
			const bool toggles = s.local && (t.f.mapReady || s.ready) && !(st.map.needsPack && !g.host);
			const int hx = c.status - 4, hw = c.statusW + 8;
			const bool hot = toggles && p.Hover(hx, cy - 18, hw, 36, mx, my);
			PaintStatus(p, c, cy, s.ready, hot);
			if (toggles) p.AddHit(hx, cy - 18, hw, 36, kGReady, 0);
		}
	}
	if (g.host && g.online && s.type == 3 && !s.local && !moving && !RankedRoomLocked(st)) PaintKick(p, st, c.kick, cy, kGKick, i);
	// An AI the host added goes the same way: its slot opens again (its level's menu has Open too, but nobody looks for it there).
	if (g.host && s.type == 1 && !load && !moving && !RankedRoomLocked(st) && !TourneyTeamsLocked(st) && !(g.online && LocalReady(g)))
		PaintKick(p, st, c.kick, cy, kGSlotType, i);
}

// The rows are as tall as the mockup's while they fit in `maxH`, and no shorter than the controls in them.
void PaintPlayerTable(Painter &p, const State &st, const RoomFacts &f, int x, int y, int w, int maxH)
{
	const GameRoomSnapshot &g = st.game;
	Arrival arriving[8];
	PlaceArrivals(st, f, arriving);
	// Always the eight positions, those the map lacks greyed: the table keeps its size whatever the map, and shows where there is room.
	const int rows = 8;
	const int rowH = std::max(kMinRowH, std::min(kMaxRowH, (maxH - kTableHeadH) / rows));
	PaintGlassCard(p, x, y, w, kTableHeadH + rows * rowH);
	TableCtx t;
	t.f = f;
	t.x = x;
	t.w = w;
	t.moving = g.host && st.moveFrom >= 0 && st.moveFrom < 8;
	t.c = TableColumns(x, w, g.online);
	const Columns &c = t.c;
	p.Font(4);
	const int hy = y + 2, hh = kTableHeadH - 4;
	const UINT head = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	p.TextCaps(c.player, hy, c.playerW, hh, "Player", pal::caption, head, 2);
	p.TextCaps(c.team, hy, c.teamW, hh, "Team", pal::caption, head, 2);
	p.TextCaps(c.civ, hy, c.civW, hh, "Civilization", pal::caption, head, 2);
	p.TextCaps(c.color, hy, c.colorW + 8, hh, "Color", pal::caption, head, 2);
	p.TextCaps(c.bonus, hy, c.bonusW, hh, "Bonus", pal::caption, head, 2);
	if (c.statusOn) p.TextCaps(c.status, hy, c.statusW, hh, "Status", pal::caption, head, 2);

	int rowY = y + kTableHeadH, hovered = -1;
	for (int i = 0; i < 8; i++) {
		const GameRoomSlot &s = g.slots[i];
		const bool available = s.exists || s.used || !arriving[i].Empty();
		if (available && p.Hover(x + 1, rowY, w - 2, rowH, st.mouseX, st.mouseY)) hovered = i;
		if (hovered == i && g_anim.hoverRow != i) g_anim.hoverAt = GetTickCount();
		const int hoverPercent = static_cast<int>(std::min<DWORD>(kHoverMs, Since(g_anim.hoverAt)) * 100 / kHoverMs);
		PaintSlotRow(p, st, t, i, arriving[i], rowY, rowH, hoverPercent);
		rowY += rowH;
	}
	g_anim.hoverRow = hovered;
}

void PaintRoomHeader(Painter &p, const State &st, const RoomFacts &f, const RoomLayout &l)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const std::string title = !g.online ? Tr("Skirmish") : st.room.id ? st.room.info.name : Tr("Local network game");

	std::vector<GhostButton> buttons;
	if (g.online && st.room.id && !RankedRoomLocked(st)) buttons.push_back({"Invite", kInvite, false, !RoomHasNoSeat(st)});
	buttons.push_back({"Leave", kGCancel, true, true});
	std::string tourneyNote;
	const int buttonsLeft = PaintTourneyRoomStatus(p, st, PaintHeaderActions(p, l, buttons, mx, my), l.margin, tourneyNote);
	PaintScreenTitle(p, l, title, {{"Game", kGTab, 1, st.roomTab == 1, true}, {"Room", kGTab, 0, st.roomTab != 1, true}}, buttonsLeft, mx, my);

	if (f.withoutSlot) {
		PaintScreenInfo(p, l, Tr("The map is for ") + std::to_string(f.positions) + Tr(" players"), pal::bad);
		return;
	}
	std::string info;
	auto add = [&](const std::string &part) {
		if (!part.empty()) info += (info.empty() ? "" : "  \xC2\xB7  ") + part;
	};
	if (g.online) {
		add(st.room.id ? st.room.info.host : f.hostSlot >= 0 ? g.slots[f.hostSlot].name : std::string());
		add(Tr(st.room.id ? "Online on Steam" : "Local network"));
	}
	add(TrFmt("%d of %d players", {std::to_string(f.humans + f.ai), std::to_string(f.positions)}));
	add(tourneyNote);
	if (RankedRoomLocked(st)) {
		ScreenLayout shifted = l;
		const int mark = PaintRankedMark(p, st, l.margin, l.infoY, l.infoH);
		shifted.margin += mark + 16;
		add(Tr("the league sets the options"));
		PaintScreenInfo(p, shifted, info);
		return;
	}
	PaintScreenInfo(p, l, info);
}

}

void PaintMapThumb(Painter &p, const State &st, int x, int y, int size, bool markers)
{
	const GameRoomSnapshot &g = st.game;
	p.Fill(x, y, size, size, pal::bg);
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
			PaintDisc(p, cx, cy, 6.5f, pal::bg, pal::bg);
			const COLORREF c = seated ? PlayerColor(g, g.slots[pl.slot].color) : RGB((pl.color >> 16) & 255, (pl.color >> 8) & 255, pl.color & 255);
			PaintDisc(p, cx, cy, 5.0f, c, c);
		}
	}
	else {
		p.Fill(x + 1, y + 1, size - 2, size - 2, pal::raised);
		p.Diamond(x + size / 2, y + size / 2 - 4, 4, pal::caption);
		if (g.randomMap && size >= 64) {
			p.Font(3);
			p.Text(x + 4, y + size - 24, size - 8, 18, "Random", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	p.Frame(x, y, size, size, pal::lineStrong);
}

// What the player must know before pressing Ready: the saved game, and the scenario on its way.
std::string RoomNotice(const State &st, COLORREF &color)
{
	const GameRoomSnapshot &g = st.game;
	color = pal::textDim;
	if (!g.host && st.map.state == 1) {
		const uint64_t total = std::max<uint64_t>(1, st.map.total);
		color = pal::redText;
		return Tr("Receiving scenario \"") + st.map.name + "\": " + std::to_string(std::min<uint64_t>(100, st.map.got * 100 / total)) + " %";
	}
	if (!g.host && st.map.state == 2) {
		color = pal::bad;
		if (st.map.needsPack) return TrFmt("This room's map comes from \"%s\", which you do not have loaded.", {st.map.packName});
		return Tr("Couldn't receive the scenario: ") + st.map.error;
	}
	const std::string saveName = g.host ? st.hostSave.name : st.room.info.saveName;
	if (!g.online || saveName.empty()) return "";
	color = pal::redText;
	std::string text = Tr("Saved game: ") + saveName;
	if (g.host && !st.saveMapError.empty()) {
		color = pal::bad;
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
		color = pal::bad;
		text += "  ·  " + Tr("Could not receive: ") + st.save.error;
	}
	return text;
}

namespace {
void PaintSummary(Painter &p, const State &st, int cardX, int cardY, int cardW, int cardH)
{
	const bool hot = p.Hover(cardX, cardY, cardW, cardH, st.mouseX, st.mouseY);
	PaintGlassCard(p, cardX, cardY, cardW, cardH);
	if (hot) p.Frame(cardX, cardY, cardW, cardH, pal::edge);
	const int pad = 24, x = cardX + pad, y = cardY + 20, w = cardW - 2 * pad, h = cardH - 20 - pad;
	p.Font(4);
	p.TextCaps(x, y, w, 20, "Game summary", hot ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	COLORREF noticeColor = pal::textDim;
	const std::string notice = RoomNotice(st, noticeColor);
	const int noticeH = notice.empty() ? 0 : 40;
	const int top = y + 34, bottom = y + h - (noticeH ? noticeH + 6 : 0);
	const int thumb = std::max(0, std::min({156, bottom - top, w / 3}));
	if (thumb >= 48) {
		PaintMapThumb(p, st, x, top, thumb, false);
		if (hot) p.Frame(x, top, thumb, thumb, pal::edgeHot);
	}
	const int gx = thumb >= 48 ? x + thumb + 24 : x, gw = x + w - gx;
	struct Line {
		const char *label;
		int id;
		const char *published;
	};
	static const Line kLines[] = {{"Map", 0, nullptr}, {"Mode", 0x1002, "Game type"}, {"Size", 0x01000008, "Map size"}, {"Population", 0x1004, "Population"},
		{"Starting gold", 0x1005, "Starting gold"}, {"Limit", 0x1003, "Limit"}};
	const int columns = gw >= 420 ? 3 : 2, colGap = 20, colW = (gw - (columns - 1) * colGap) / columns, cellH = 52;
	const UINT line = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	int shown = 0;
	for (const Line &l : kLines) {
		const int lx = gx + (shown % columns) * (colW + colGap), ly = top + (shown / columns) * cellH;
		if (ly + cellH - 2 > bottom) break;
		const std::string v = l.published ? RoomOptionText(st, l.id, l.published) : RoomMapLabel(st);
		p.Font(4);
		p.TextCaps(lx, ly, colW, 18, l.label, pal::caption, line, 2);
		p.Font(0);
		p.Text(lx, ly + 20, colW, 26, v.empty() ? std::string("—") : v, pal::text, line);
		shown++;
	}
	if (!notice.empty()) {
		p.Font(3);
		p.Clip(x, y + h - noticeH, w, noticeH);
		p.TextWrapped(x, y + h - noticeH, w, noticeH, notice, noticeColor);
		p.Unclip();
	}
	p.AddHit(cardX, cardY, cardW, cardH, kGTab, 1);
}

// What the room waits for and its one button, in a card at the right of the bottom row: how many are ready, a mark for each player and a
// wide button that says what a click does (be ready, stop being ready, start, stop the countdown).
void PaintReadyBlock(Painter &p, const State &st, const RoomFacts &f, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	const GameRoomSlot &me = g.slots[g.localSlot & 7];
	PaintGlassCard(p, x, y, w, h);
	const int pad = 20, ix = x + pad, iw = w - 2 * pad, btnH = 56, btnY = y + h - pad - btnH;
	const UINT left = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	if (g.online && !g.host && st.map.needsPack) {
		p.Font(4);
		p.TextCaps(ix, y + 20, iw, 20, "Content", pal::caption, left, 2);
		p.Font(3);
		p.TextWrapped(ix, y + 50, iw, std::max(20, btnY - 12 - (y + 50)), "This room plays with content you don't have: it downloads and you come back in.", pal::textDim);
		p.Button(ix, btnY, iw, btnH, "Download and rejoin", kGResync, 0, mx, my, true, true);
		return;
	}
	enum Look { kAsk, kIsReady, kStart, kWait, kCount };
	const int leftMs = CountdownLeftMs();
	Look look = kAsk;
	int action = kGReady;
	if (leftMs >= 0) look = kCount;
	else if (!g.online) {
		look = kStart;
		action = kGStart;
	}
	else if (me.ready) look = kIsReady;
	else if (!f.mapReady) look = kWait;

	p.Font(4);
	p.TextCaps(ix, y + 20, iw, 20, g.online ? "Players ready" : "Game", pal::caption, left, 2);
	int ty = y + 48;
	std::string count;
	if (g.online) count = f.ReadyText();
	else {
		int used = 0, places = 0;
		for (const GameRoomSlot &s : g.slots) {
			used += s.used ? 1 : 0;
			places += s.exists || s.used ? 1 : 0;
		}
		count = TrFmt("%s of %s players", {std::to_string(used), std::to_string(places)});
	}
	p.Font(2);
	p.raw = true;
	p.Text(ix, ty, iw, 36, count, pal::text, left);
	p.raw = false;
	ty += 44;
	if (g.online) {
		// One mark per person in the room, full when they are ready.
		int px = ix;
		for (const GameRoomSlot &s : g.slots) {
			if (!s.used || s.type == 1 || px + 14 > ix + iw) continue;
			if (s.ready) p.Fill(px, ty, 14, 14, pal::ok);
			else p.Frame(px, ty, 14, 14, pal::edgeHot);
			px += 20;
		}
		ty += 26;
	}
	std::string caption;
	COLORREF captionColor = pal::caption;
	switch (look) {
	case kAsk: caption = "The game starts when everyone is ready"; break;
	case kIsReady: caption = g.host && !f.block.label.empty() ? f.block.label : "The game starts when everyone is ready"; break;
	case kCount:
		caption = "Everyone is ready. Click to cancel";
		captionColor = pal::text;
		break;
	default: break;
	}
	if (!caption.empty() && btnY - 12 - ty > 16) {
		p.Font(3);
		p.TextWrapped(ix, ty, iw, btnY - 12 - ty, caption, captionColor);
	}

	const bool enabled = look != kWait;
	const bool hot = enabled && p.Hover(ix, btnY, iw, btnH, mx, my);
	const bool down = enabled && st.mouseDown && st.pressAction == action;
	const int by = btnY + (down ? 1 : 0);
	COLORREF fill = pal::red, ink = pal::onRed;
	std::string label;
	switch (look) {
	case kAsk:
	case kStart:
		fill = down ? pal::redPressed : hot ? Mix(pal::red, pal::redText, 35) : pal::red;
		label = Tr(look == kAsk ? "I'm ready" : "Start the game");
		break;
	case kIsReady:
		// Ready shows green; under the mouse it says what a click does.
		fill = hot ? RGB(0x5a, 0x24, 0x26) : RGB(0x2c, 0x46, 0x24);
		ink = pal::text;
		label = Tr(hot ? "Cancel" : "Ready");
		break;
	case kCount:
		fill = hot ? pal::selected : pal::raised;
		ink = pal::text;
		label = TrFmt("Starts in %s", {std::to_string((leftMs + 999) / 1000)}) + (hot ? "  \xC2\xB7  " + Tr("Cancel") : std::string());
		break;
	case kWait:
		fill = pal::field;
		ink = pal::caption;
		label = Tr(!f.saveReady ? "Waiting for the saved game..." : "Waiting for the scenario...");
		break;
	}
	p.Fill(ix, by, iw, btnH, fill);
	if (look == kCount || look == kWait) p.Frame(ix, by, iw, btnH, pal::edge);
	if (look == kCount) {
		const int done = iw * (5000 - std::max(0, std::min(5000, leftMs))) / 5000;
		p.Fill(ix, by + btnH - 4, done, 4, pal::red);
	}
	p.Font(look == kWait ? 3 : 2);
	const int labelW = look == kWait ? p.TextWidth(label) : p.CapsWidth(label, 1);
	int mark = 0;
	if (look == kIsReady && !hot) mark = 26;
	if (look == kWait) mark = 30;
	const int lx = ix + std::max(0, (iw - labelW - mark) / 2);
	if (look == kIsReady && !hot) {
		const int cy = by + btnH / 2;
		p.Line(lx + 2, cy, lx + 8, cy + 6, pal::ok, 3);
		p.Line(lx + 8, cy + 6, lx + 18, cy - 6, pal::ok, 3);
	}
	if (look == kWait) p.Spinner(lx + 10, by + btnH / 2, 10, 4, GetTickCount());
	p.raw = true;
	if (look == kWait) p.Text(lx + mark, by, iw - (lx + mark - ix), btnH, label, ink, left);
	else p.TextCaps(lx + mark, by, iw - (lx + mark - ix), btnH, label, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	p.raw = false;
	if (enabled) p.AddHit(ix, btnY, iw, btnH, action, 0);
}

void PaintChatBlock(Painter &p, const State &st, int cardX, int cardY, int cardW, int cardH)
{
	PaintGlassCard(p, cardX, cardY, cardW, cardH);
	const int pad = 20, x = cardX + pad, y = cardY + 20, w = cardW - 2 * pad, h = cardH - 20 - pad;
	p.Font(4);
	p.TextCaps(x, y, w, 20, "Chat", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	const int fieldH = 40, sendW = 104, logTop = y + 30;
	PaintChatLog(p, st, x, logTop, w, y + h - fieldH - 10 - logTop);
	PaintField(p, st, x, y + h - fieldH, w - sendW - 10, fieldH, st.chatInput, "Type a message...", st.focus == kChatField, kChatFocus);
	p.Button(x + w - sendW, y + h - fieldH, sendW, fieldH, "Send", kChatSend, 0, st.mouseX, st.mouseY, true, false, 4);
}

void PaintRoomTab(Painter &p, const State &st, const RoomFacts &f, const RoomLayout &l)
{
	const GameRoomSnapshot &g = st.game;
	const int m = l.margin, inner = l.w - 2 * m, by = l.bottomY, bh = l.bottomH, gap = kBlockGap;
	// In a tournament series, the choice of the next game's map goes over the table while it lasts.
	const int pick = PaintTourneyRoomPick(p, st, m, l.top, inner);
	PaintPlayerTable(p, st, f, m, l.top + pick, inner, by - 32 - l.top - pick);
	// The summary, the chat and, at the right edge where the eye ends, the card with what the room waits for and its button; without a
	// chat the summary takes its room.
	const int readyW = std::max(320, std::min(400, inner / 5));
	if (g.online) {
		const int summaryW = (inner - readyW - 2 * gap) * 45 / 100, chatW = inner - readyW - summaryW - 2 * gap;
		PaintSummary(p, st, m, by, summaryW, bh);
		PaintChatBlock(p, st, m + summaryW + gap, by, chatW, bh);
	}
	else {
		PaintSummary(p, st, m, by, inner - readyW - gap, bh);
	}
	PaintReadyBlock(p, st, f, l.w - m - readyW, by, readyW, bh);
}

void PaintRoomDialogs(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const GameRoomSnapshot &g = st.game;
	if (st.dropAsk) {
		const int w = 520, h = 210, x = (st.viewW - w) / 2, y = (st.viewH - h) / 2;
		p.hits->clear();
		p.Fill(x - 2, y - 2, w + 4, h + 4, pal::bg);
		p.Fill(x, y, w, h, pal::raised);
		p.Frame(x, y, w, h, pal::lineStrong);
		p.Font(1);
		p.Text(x + 20, y + 14, w - 40, 26, Tr("This map is for ") + std::to_string(st.dropPositions) + Tr(" players"), pal::redText);
		p.Font(0);
		std::string who;
		for (size_t i = 0; i < st.dropNames.size(); i++) who += (i ? (i + 1 == st.dropNames.size() ? Tr(" and ") : ", ") : "") + st.dropNames[i];
		p.Text(x + 20, y + 48, w - 40, 90,
			(st.dropNames.empty() ? std::string("There are more players than positions.") : Tr("There is no room for ") + who + ".") +
				Tr(" If you use this map, they will leave the room (they can come back in if you switch to a larger map)."),
			pal::text, DT_LEFT | DT_WORDBREAK);
		p.Button(x + 20, y + h - 46, 236, 32, "Remove them and use this map", kDropYes, 0, mx, my, true, false);
		p.Button(x + w - 256, y + h - 46, 236, 32, "Keep the map as it was", kDropNo, 0, mx, my, true, true);
	}
	else if (st.swapAskFrom) {
		const int w = 470, h = 170, x = (st.viewW - w) / 2, y = (st.viewH - h) / 2;
		p.hits->clear();
		p.Fill(x - 2, y - 2, w + 4, h + 4, pal::bg);
		p.Fill(x, y, w, h, pal::raised);
		p.Frame(x, y, w, h, pal::lineStrong);
		p.Font(1);
		p.Text(x + 20, y + 14, w - 40, 26, "Slot swap", pal::redText);
		p.Font(0);
		int theirs = -1;
		for (int i = 0; i < 8; i++) {
			if (g.slots[i].used && g.slots[i].type == 3 && PlayerNameMatches(g.slots[i].name, st.swapAskName)) theirs = i;
		}
		p.Text(x + 20, y + 46, w - 40, 60,
			(theirs >= 0 ? TrFmt("%s wants to swap slots with you (you would move to slot %d).", {st.swapAskName, std::to_string(theirs + 1)})
						 : TrFmt("%s wants to swap slots with you.", {st.swapAskName})),
			pal::text, DT_LEFT | DT_WORDBREAK);
		p.Button(x + w - 300, y + h - 46, 136, 32, "OK", kSwapAccept, 0, mx, my, true, true);
		p.Button(x + w - 156, y + h - 46, 136, 32, "Decline", kSwapDecline, 0, mx, my);
	}
}

void PaintGameRoom(Painter &p, const State &st)
{
	const RoomFacts f = Facts(st);
	const RoomLayout l = LayoutFor(st);
	UseScreenFonts(p);
	PaintRoomHeader(p, st, f, l);
	if (st.roomTab == 1) {
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			g_chat.rectValid = false;
		}
		// The host's slot menus and the seat buttons rely on where the arrivals go, also while the table is hidden.
		Arrival arriving[8];
		PlaceArrivals(st, f, arriving);
		PaintGameTab(p, st, l.margin, l.top, l.w - 2 * l.margin, l.h - l.margin - l.top);
	}
	else {
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			g_optRect = {};
		}
		g_optGeomOut = ListGeom();
		PaintRoomTab(p, st, f, l);
	}
	// The dialogs, menus and the scenario picker over the room keep the shared type their boxes are sized for.
	UseSharedFonts(p);
	PaintRoomDialogs(p, st);
}

void PaintConnecting(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const bool creating = st.autoHost || st.room.owner;
	const bool waiting = !st.connecting && st.room.id && !st.room.info.hosting;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 480, 200, creating ? "Creating the game..." : waiting ? "Waiting for the host..." : "Joining the game...");
	PaintBusyBar(p, d.BodyX(), d.y + 72, d.BodyW(), 8, GetTickCount());
	p.Font(3);
	p.Text(d.BodyX(), d.y + 90, d.BodyW(), 24,
		creating ? "Setting up the game room"
		: waiting ? Tr("You'll join as soon as ") + st.room.info.host + Tr(" creates the game")
							: Tr("Connecting to ") + (st.room.info.host.empty() ? std::string("the host") : st.room.info.host) + Tr(" over the Steam network"),
		pal::textDim);
	DialogButtons(p, d, {{"Cancel", kLeave}}, mx, my, true);
}

void PaintSpectate(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const OnlineLobby &g = st.room.info;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 520, 188, TrFmt("Watching %s's game", {g.host}));
	p.Font(3);
	p.Text(d.BodyX(), d.y + 56, d.BodyW(), 20, g.name, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const int progress = SpectatorProgress();
	if (progress >= 0) PaintProgress(p, d.BodyX(), d.y + 82, d.BodyW(), 14, progress / 100.0f);
	else PaintBusyBar(p, d.BodyX(), d.y + 86, d.BodyW(), 8, GetTickCount());
	const std::string status = SpectatorStatusText();
	p.Font(0);
	p.Text(d.BodyX(), d.y + 106, d.BodyW(), 26, status.empty() ? std::string("Connecting to the host...") : status, pal::redText,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DialogButtons(p, d, {{"Leave", kLeave}}, mx, my, true);
}
}
}

namespace mpui_impl {
void PaintScreen(Painter &p, const State &st)
{
	const bool linkOffer = LinkOfferShown(st);
	p.noHover = st.dropOpen || st.passPrompt || st.syncConfirm || st.syncPhase || st.searching || st.connecting || st.recoverAsk ||
		RejoinClientActive() || RejoinClientFailed() || (st.game.active && st.game.picker) || linkOffer;
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
	UseScreenFonts(p);
	PaintHeader(p, st);
	if (st.view == kViewRanked) PaintRanked(p, st);
	else if (st.view == kViewCreate) PaintCreate(p, st);
	else if (st.view == kViewTournaments) PaintTournaments(p, st);
	else PaintList(p, st);
	// The menus and dialogs over the lobby keep the shared type their boxes are sized for.
	UseSharedFonts(p);
	if (!st.searching && !st.connecting) PaintDropList(p, st);
	if (st.searching) PaintSearch(p, st);
	if (RejoinClientActive() || RejoinClientFailed()) {
		PaintRejoin(p, st);
		return;
	}
	if (st.recoverAsk) {
		PaintRecover(p, st);
		return;
	}
	if (st.passPrompt) PaintPassPrompt(p, st);
	else if (st.syncConfirm) PaintSyncConfirm(p, st);
	else if (st.syncPhase) PaintSyncProgress(p, st);
	else if (linkOffer) PaintLinkOffer(p, st);
	if (st.room.id && st.room.info.spectate && !st.game.active && !st.syncPhase && !st.syncConfirm) {
		PaintSpectate(p, st);
		return;
	}
	if (st.connecting || (st.room.id && !st.game.active && !st.syncPhase && !st.syncConfirm && !st.gameClosedAt)) PaintConnecting(p, st);
}
}
