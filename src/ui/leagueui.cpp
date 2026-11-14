// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leagueui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <cstdint>
#include <vector>

#include "core/leaguemaps.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leagueemblem.h"

using namespace ui;

namespace {

enum Art { kArtBadge, kArtVexillum, kArtTrophy, kArtEmblem, kArtPhalera };

std::mutex g_cacheMx;

// Premultiplied 32-bit bitmaps for AlphaBlend, one per picture and size; there are only a handful. `variant` is the tier, or the metal of
// a phalera.
HBITMAP Bitmap(Art art, int variant, int size)
{
	std::lock_guard<std::mutex> lock(g_cacheMx);
	static std::map<std::tuple<int, int, int>, HBITMAP> cache;
	const auto key = std::make_tuple(static_cast<int>(art), variant, size);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	const LeagueTier tier = static_cast<LeagueTier>(variant);
	const LeagueMetal metal = static_cast<LeagueMetal>(variant);
	LeagueImage img;
	switch (art) {
	case kArtBadge: img = LeagueBadgePixels(tier, size); break;
	case kArtVexillum: img = LeagueVexillumPixels(tier, size); break;
	case kArtTrophy: img = LeagueTrophyImage(metal, size); break;
	case kArtPhalera: img = LeaguePhaleraImage(metal, size); break;
	default: img = LeagueEmblemImage(tier, size); break;
	}
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = img.w;
	bi.bmiHeader.biHeight = -img.h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = img.w > 0 ? CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
	if (bmp && bits) std::copy(img.px.begin(), img.px.end(), static_cast<uint32_t *>(bits));
	cache[key] = bmp;
	return bmp;
}

void Blit(Painter &p, HBITMAP bmp, int left, int top, int w, int h, int alpha = 255)
{
	if (!bmp || alpha <= 0) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, static_cast<BYTE>(std::min(255, alpha)), AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, left, top, w, h, mem, 0, 0, w, h, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

HBITMAP NewDib(int w, int h, uint32_t *&bits)
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
	bits = static_cast<uint32_t *>(raw);
	return bits ? bmp : nullptr;
}

uint32_t Premultiplied(COLORREF c, uint32_t a)
{
	return (a << 24) | ((GetRValue(c) * a / 255) << 16) | ((GetGValue(c) * a / 255) << 8) | (GetBValue(c) * a / 255);
}

// A per-pixel picture of `size` square from a coverage function of the pixel's centre (relative to the middle, in pixels).
template <class F> void PaintCoverage(Painter &p, int left, int top, int size, COLORREF c, int alpha, F coverage)
{
	if (size <= 0 || alpha <= 0) return;
	uint32_t *bits = nullptr;
	HBITMAP bmp = NewDib(size, size, bits);
	if (!bmp) return;
	const float mid = size / 2.0f;
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			const float a = coverage(x + 0.5f - mid, y + 0.5f - mid);
			bits[static_cast<size_t>(y) * size + x] = Premultiplied(c, static_cast<uint32_t>(255 * std::max(0.0f, std::min(1.0f, a))));
		}
	}
	GdiFlush();
	Blit(p, bmp, left, top, size, size, alpha);
	DeleteObject(bmp);
}

struct Tip {
	bool show = false;
	int x = 0, y = 0;
	LeagueBadge badge;
};
Tip g_tip;

int64_t EpochMs()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	const unsigned long long t = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
	return static_cast<int64_t>((t - 116444736000000000ULL) / 10000ULL);
}

COLORREF Ref(uint32_t rgb)
{
	return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

// The painter's display font at a size of the full screens, for the big code.
struct BigFont {
	Painter &p;
	HFONT saved;
	BigFont(Painter &painter, int size) : p(painter), saved(painter.fonts[5])
	{
		p.fonts[5] = BrandFont(BrandFace::Display, size, p.s);
		p.Font(5);
	}
	~BigFont() { p.fonts[5] = saved; }
	BigFont(const BigFont &) = delete;
	BigFont &operator=(const BigFont &) = delete;
};

void Raw(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, text, c, fmt);
	p.raw = false;
}

int Wrapped(Painter &p, int x, int y, int w, const std::string &text, COLORREF c)
{
	const int h = p.Measure(w, text);
	p.TextWrapped(x, y, w, h + 2, text, c);
	return h;
}

void Busy(Painter &p, int x, int y, int w, const std::string &text)
{
	p.Spinner(x + 11, y + 12, 9, 4, GetTickCount());
	p.Font(3);
	p.Text(x + 32, y, w - 32, 24, text, pal::textDim);
}

// What a badge of the list is called next to its picture.
std::string ChoiceLabel(const LeagueBadgeChoice &c)
{
	if (c.kind == "rank") {
		return c.look.tier != LeagueTier::None ? TrFmt("Current rank: %s", {LeagueRankText(Tr(LeagueTierName(c.look.tier)), c.look.division)})
											   : Tr("Current rank (none yet)");
	}
	if (c.kind == "season") {
		const std::string season = TrFmt("Season %s", {c.id});
		return c.look.tier != LeagueTier::None ? season + ": " + Tr(LeagueTierName(c.look.tier)) : season;
	}
	if (c.kind == "trophy") return c.look.title.empty() ? Tr("Tournament trophy") : c.look.title;
	return Tr("None");
}

// The badge shown to the right of the name, one row per badge of the player's. Returns how tall it came out.
int PaintBadgeChoice(Painter &p, int x, int y, int w, const LeagueMe &me, int action, int mx, int my)
{
	const LeagueRankedView ranked = LeagueRankedState();
	int cy = y;
	PaintSectionLabel(p, x, cy, w, 22, "Badge next to your name");
	cy += 30;
	const std::vector<LeagueBadgeChoice> choices = LeagueBadgeChoices(me);
	const int rowH = 34;
	for (size_t i = 0; i < choices.size(); i++) {
		const LeagueBadgeChoice &c = choices[i];
		const bool hot = !ranked.badgeBusy && !c.chosen && p.Hover(x, cy, w, rowH, mx, my);
		if (c.chosen || hot) p.Fill(x, cy, w, rowH, c.chosen ? pal::selected : pal::raised);
		if (c.chosen) p.Fill(x, cy, 3, rowH, pal::red);
		if (c.look.Shown()) DrawLeagueBadge(p, x + 12, cy + (rowH - kLeagueBadgeSize) / 2, kLeagueBadgeSize, c.look);
		else p.Frame(x + 14, cy + (rowH - 16) / 2, 16, 16, pal::edge);
		p.Font(c.chosen ? 1 : 0);
		Raw(p, x + 44, cy, w - 52, rowH, ChoiceLabel(c), c.chosen ? pal::text : pal::body);
		if (!c.chosen && !ranked.badgeBusy) p.AddHit(x, cy, w, rowH, action, static_cast<int>(i));
		cy += rowH + 2;
	}
	p.Font(3);
	if (ranked.badgeBusy) Busy(p, x, cy + 4, w, "Saving...");
	else if (!ranked.badgeError.empty()) Wrapped(p, x, cy + 4, w, ranked.badgeError, pal::bad);
	else Wrapped(p, x, cy + 4, w, "Others see it in rooms, in the list of games and in the chat.", pal::caption);
	cy += 28;
	return cy - y;
}

std::string Clock(int64_t ms)
{
	const int64_t s = std::max<int64_t>(0, ms / 1000);
	char buf[32];
	sprintf_s(buf, "%lld:%02lld", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
	return buf;
}

}

void DrawLeagueBadge(Painter &p, int x, int y, int size, const LeagueBadge &badge)
{
	if (!badge.Shown()) return;
	const RECT r = p.R(x, y, size, size);
	const int px = std::max(1, static_cast<int>(r.right - r.left));
	const int scale = std::max(1, static_cast<int>(std::lround(px / static_cast<double>(kLeagueBadgeGrid))));
	const Art art = badge.kind == LeagueBadgeKind::Trophy ? kArtTrophy : badge.kind == LeagueBadgeKind::Season ? kArtVexillum : kArtBadge;
	const int side = kLeagueBadgeGrid * scale;
	// The phalera is drawn from its shapes at the size it is shown; the shields are pixels repeated `scale` times.
	const int variant = art == kArtTrophy ? static_cast<int>(badge.metal) : static_cast<int>(badge.tier);
	Blit(p, Bitmap(art, variant, art == kArtTrophy ? side : scale), r.left + (px - side) / 2, r.top + (static_cast<int>(r.bottom - r.top) - side) / 2, side,
		side);
}

void DrawLeaguePhalera(Painter &p, int x, int y, int size, LeagueMetal metal)
{
	const RECT r = p.R(x, y, size, size);
	const int px = std::max(8, static_cast<int>(r.right - r.left));
	Blit(p, Bitmap(px >= 48 ? kArtPhalera : kArtTrophy, static_cast<int>(metal), px), r.left, r.top, px, px);
}

COLORREF LeagueMetalInk(LeagueMetal metal)
{
	return Ref(LeagueMetalColors(metal).text);
}

void DrawLeagueEmblem(Painter &p, int x, int y, int size, LeagueTier tier, int alpha)
{
	const RECT r = p.R(x, y, size, size);
	const int px = std::max(8, static_cast<int>(r.right - r.left));
	Blit(p, Bitmap(kArtEmblem, static_cast<int>(tier), px), r.left, r.top, px, px, alpha);
	if (tier != LeagueTier::None || alpha < 128) return;
	// The unranked shield carries a question mark, as on the website.
	BigFont font(p, std::max(12, size * 56 / 160));
	p.raw = true;
	p.Text(x, y + size * 22 / 160, size, size, "?", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
}

void PaintTint(Painter &p, int x, int y, int w, int h, COLORREF c, int alpha)
{
	if (w <= 0 || h <= 0 || alpha <= 0) return;
	if (alpha >= 255) {
		p.Fill(x, y, w, h, c);
		return;
	}
	static uint32_t *bits = nullptr;
	static HBITMAP dot = NewDib(1, 1, bits);
	if (!dot) return;
	GdiFlush();
	*bits = Premultiplied(c, 255);
	const RECT r = p.R(x, y, w, h);
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, dot);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, static_cast<BYTE>(alpha), 0};
	GdiAlphaBlend(p.dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem, 0, 0, 1, 1, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

void PaintArc(Painter &p, int cx, int cy, int radius, int width, COLORREF c, float fraction)
{
	if (fraction <= 0) return;
	const float outer = (radius + width / 2.0f + 1) * p.s, r = radius * p.s, half = width * p.s / 2;
	const int size = static_cast<int>(std::ceil(outer * 2));
	const float end = std::min(1.0f, fraction) * 6.2831853f;
	PaintCoverage(p, p.S(cx) - size / 2, p.S(cy) - size / 2, size, c, 255, [=](float dx, float dy) {
		float d = half - std::fabs(std::hypot(dx, dy) - r) + 0.5f;
		if (fraction < 1.0f) {
			float a = std::atan2(dx, -dy);
			if (a < 0) a += 6.2831853f;
			d = std::min(d, std::min(a, end - a) * r + 0.5f);
		}
		return d;
	});
}

void PaintRays(Painter &p, int cx, int cy, int radius, COLORREF c, int alpha)
{
	if (alpha <= 0) return;
	const float R = radius * p.s;
	const int size = static_cast<int>(std::ceil(R * 2));
	PaintCoverage(p, p.S(cx) - size / 2, p.S(cy) - size / 2, size, c, alpha, [=](float dx, float dy) {
		const float d = std::hypot(dx, dy);
		if (d >= R) return 0.0f;
		// Eighteen rays, wider towards the edge, that fade out before it.
		const float wave = std::cos(std::atan2(dy, dx) * 18.0f);
		const float ray = std::max(0.0f, std::min(1.0f, (wave - 0.55f) * 4.0f));
		const float fade = 1.0f - d / R;
		return ray * fade * fade * 0.9f;
	});
}

void LeagueChooseBadgeAt(int index)
{
	const LeagueMeView me = LeagueMeState();
	if (!me.loaded) return;
	const std::vector<LeagueBadgeChoice> choices = LeagueBadgeChoices(me.me);
	if (index < 0 || index >= static_cast<int>(choices.size()) || choices[index].chosen) return;
	LeagueBadgeChoose(choices[index].kind, choices[index].id);
}

int LeagueBadgeRoom(const LeagueBadge &badge)
{
	return badge.Shown() ? kLeagueBadgeSize + kLeagueBadgeGap : 0;
}

void DrawBadgeAfterName(Painter &p, int nameX, int y, int nameW, int h, const std::string &name, const LeagueBadge &badge, int mx, int my)
{
	if (!badge.Shown()) return;
	const int textW = std::min(std::max(0, nameW), p.TextWidth(name));
	const int bx = nameX + textW + kLeagueBadgeGap, by = y + (h - kLeagueBadgeSize) / 2;
	DrawLeagueBadge(p, bx, by, kLeagueBadgeSize, badge);
	if (!p.Hover(bx, by, kLeagueBadgeSize, kLeagueBadgeSize, mx, my)) return;
	g_tip.show = true;
	g_tip.x = bx;
	g_tip.y = by + kLeagueBadgeSize + 6;
	g_tip.badge = badge;
}

std::string LeagueRankLabel(LeagueTier tier, int division)
{
	return LeagueRankText(Tr(LeagueTierName(tier)), division);
}

std::string LeagueMapText(const std::string &id)
{
	if (const char *name = LeagueMapName(id)) return Tr(name);
	std::string out = id;
	std::replace(out.begin(), out.end(), '_', ' ');
	if (!out.empty()) out[0] = static_cast<char>(toupper(static_cast<unsigned char>(out[0])));
	return out;
}

COLORREF LeagueTierInk(LeagueTier tier)
{
	return Ref(LeagueTierColors(tier).text);
}

void PaintLeagueBadgeTip(Painter &p, int screenW, int screenH)
{
	if (!g_tip.show) return;
	g_tip.show = false;
	const LeagueBadge &b = g_tip.badge;
	const COLORREF ink = b.kind == LeagueBadgeKind::Trophy ? LeagueMetalInk(b.metal) : LeagueTierInk(b.tier);
	std::string title, line;
	switch (b.kind) {
	case LeagueBadgeKind::Rank:
		title = LeagueRankLabel(b.tier, b.division);
		line = Tr(LeagueTierHasDivisions(b.tier) ? "Ranked 1 vs 1 league" : "Ranked 1 vs 1 league, by place in the leaderboard");
		break;
	case LeagueBadgeKind::Season:
		title = TrFmt("Season %s", {b.id});
		line = b.tier != LeagueTier::None ? TrFmt("Reached %s", {LeagueRankLabel(b.tier, b.division)}) : Tr("Emblem of a past season");
		break;
	default:
		title = Tr(b.metal == LeagueMetal::Silver ? "Silver phalera" : b.metal == LeagueMetal::Bronze ? "Bronze phalera" : b.metal == LeagueMetal::Season ? "Season phalera" : "Gold phalera");
		line = Tr(b.metal == LeagueMetal::Silver ? "Finalist of an official tournament"
				: b.metal == LeagueMetal::Bronze ? "Semifinalist of an official tournament"
				: b.metal == LeagueMetal::Season ? "Champion of a season's final"
												 : "Champion of an official tournament");
		break;
	}
	p.raw = true;
	p.Font(1);
	int w = p.TextWidth(title);
	p.Font(3);
	w = std::max(w, p.TextWidth(line)) + 24;
	const int h = 58;
	const int x = std::max(4, std::min(g_tip.x, screenW - w - 4));
	const int y = g_tip.y + h > screenH - 4 ? std::max(4, g_tip.y - h - 40) : g_tip.y;
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, ink);
	p.Fill(x, y, 3, h, ink);
	p.Font(1);
	p.Text(x + 12, y + 8, w - 20, 20, title, ink, DT_LEFT | DT_TOP | DT_SINGLELINE);
	p.Font(3);
	p.Text(x + 12, y + 31, w - 20, 18, line, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE);
	p.raw = false;
}

int PaintLeagueAccount(Painter &p, int x, int y, int w, const LeagueLinkActions &actions, int mx, int my)
{
	const LeagueLinkView link = LeagueLinkState();
	int cy = y;
	PaintSectionLabel(p, x, cy, w, 22, "League account");
	cy += 34;
	auto error = [&]() {
		if (link.error.empty()) return;
		p.Font(3);
		cy += Wrapped(p, x, cy, w, link.error, pal::bad) + 14;
	};
	switch (link.phase) {
	case LeagueLinkPhase::Starting:
		Busy(p, x, cy, w, "Asking the league for a code...");
		cy += 40;
		break;
	case LeagueLinkPhase::Waiting: {
		p.Font(0);
		cy += Wrapped(p, x, cy, w, "Open this page, sign in with Steam and enter the code:", pal::body) + 8;
		p.Font(1);
		Raw(p, x, cy, w, 26, LeagueUrlDisplay(link.url), pal::text);
		cy += 34;
		{
			BigFont font(p, 72);
			p.raw = true;
			p.TextCaps(x - 2, cy, w, 84, LeagueCodeDisplay(link.code), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 6);
			p.raw = false;
		}
		cy += 92;
		const int64_t left = link.expiresAt ? link.expiresAt - EpochMs() : 0;
		Busy(p, x, cy, w, left > 0 ? TrFmt("Waiting for the website. The code is valid for %s.", {Clock(left)}) : Tr("Waiting for the website..."));
		cy += 44;
		const int openW = std::min(w, 280), cancelW = std::min(160, std::max(0, w - openW - 12));
		p.Button(x, cy, openW, 48, "Open the page", actions.open, 0, mx, my, true, true);
		if (cancelW >= 100) PaintGhostButton(p, x + openW + 12, cy, cancelW, 48, "Cancel", actions.cancel, false, true, mx, my);
		cy += 48;
		break;
	}
	case LeagueLinkPhase::Linked: {
		const LeagueBadge badge = PlayerBadgeOf(SteamOwnId());
		const LeagueMeView me = LeagueMeState();
		const std::string name = !link.name.empty() ? link.name : SteamOnlinePlayerName();
		p.Font(3);
		p.Text(x, cy, w, 22, "Linked as", pal::caption);
		cy += 24;
		p.Font(2);
		p.raw = true;
		const int nameW = w - LeagueBadgeRoom(badge);
		p.Text(x, cy, nameW, 38, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		DrawBadgeAfterName(p, x, cy, nameW, 38, name, badge, mx, my);
		p.raw = false;
		cy += 44;
		const LeagueRanked &rk = me.me.ranked;
		p.Font(0);
		if (!me.loaded) p.Text(x, cy, w, 26, me.loading ? "Reading your rank..." : "Your rank will show here.", pal::caption);
		else if (rk.present && rk.placementLeft > 0) p.Text(x, cy, w, 26, TrFmt("Placement: %s games left", {std::to_string(rk.placementLeft)}), pal::textDim);
		else if (rk.present && rk.tier != LeagueTier::None) {
			p.raw = true;
			p.Text(x, cy, w, 26, LeagueRankLabel(rk.tier, rk.division) + "  \xC2\xB7  " + TrFmt("%s Glory", {std::to_string(rk.glory)}), LeagueTierInk(rk.tier));
			p.raw = false;
		}
		else p.Text(x, cy, w, 26, "Unranked: you have not played ranked yet.", pal::textDim);
		cy += 34;
		p.Font(3);
		cy += Wrapped(p, x, cy, w, "Your online games are reported to the league, which keeps the record of every game.", pal::caption) + 18;
		if (actions.badge && me.loaded) cy += PaintBadgeChoice(p, x, cy, w, me.me, actions.badge, mx, my) + 12;
		error();
		PaintGhostButton(p, x, cy, std::min(w, 200), 44, "Unlink", actions.unlink, false, true, mx, my);
		cy += 44;
		break;
	}
	case LeagueLinkPhase::Unlinking:
		Busy(p, x, cy, w, "Unlinking...");
		cy += 40;
		break;
	default: {
		p.Font(0);
		cy += Wrapped(p, x, cy, w,
				  "Link your Steam account to the Imperivm Resurrected league. From then on every online game you play is recorded, and ranked "
				  "play will use it.",
				  pal::body) +
			16;
		p.Font(3);
		if (link.otherAccount) cy += Wrapped(p, x, cy, w, "This computer was linked by another Steam user. Link it again for yours.", pal::caption) + 12;
		if (!link.steamReady) cy += Wrapped(p, x, cy, w, "Steam is not available. Open Steam to link your account.", pal::warn) + 12;
		error();
		p.Button(x, cy, std::min(w, 280), 48, "Link account", actions.start, 0, mx, my, link.steamReady, true);
		cy += 48;
		break;
	}
	}
	return cy - y;
}
