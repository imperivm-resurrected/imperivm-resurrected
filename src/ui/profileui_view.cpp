// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/profileui_view.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "i18n/tr.h"
#include "ui/patronui.h"
#include "ui/widgets.h"

using namespace ui;

namespace profileui {
namespace {

constexpr int kTop = 96;
constexpr int kListX = 24, kListW = 330, kListTop = 122, kRowH = 58, kRowGap = 6;
constexpr int kListButtonsY = kListTop + kListRows * (kRowH + kRowGap) + 4, kListButtonH = 34;
constexpr int kRightX = 378, kRightW = kScreenW - 24 - kRightX;
constexpr int kIdentityY = kTop, kIdentityH = 108, kAvatar = 80;
constexpr int kTilesY = kIdentityY + kIdentityH + 12, kTileH = 76, kTileGap = 10;
constexpr int kLowerY = kTilesY + kTileH + 12, kLowerBottom = 612;
constexpr int kSummaryW = 262, kCardPad = 14, kCardHead = 30;
constexpr int kFooterTop = 622, kFooterButtonsY = 632, kFooterButtonH = 36;

const COLORREF kWonColor = RGB(126, 170, 92), kLostColor = RGB(184, 86, 66);
const COLORREF kSelectedTop = RGB(96, 34, 24), kRowBody = RGB(25, 19, 13), kRowHot = RGB(50, 38, 22);
const char kDash[] = "\xE2\x80\x94";

// Names, numbers and texts already in the player's language skip the painter's translation.
void Raw(Painter &p, int x, int y, int w, int h, const std::string &t, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, t, c, fmt);
	p.raw = false;
}

int RawWidth(Painter &p, const std::string &t)
{
	p.raw = true;
	const int w = p.TextWidth(t);
	p.raw = false;
	return w;
}

std::string Grouped(uint64_t n)
{
	std::string s = std::to_string(n);
	// A no-break space: neither a comma nor a dot reads as a decimal point in any of the three languages.
	for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, "\xC2\xA0");
	return s;
}

std::string Percent(int value) { return std::to_string(value) + "\xC2\xA0%"; }

std::string Initials(const std::string &name)
{
	const std::wstring w = W(name);
	std::wstring out;
	bool start = true;
	for (wchar_t c : w) {
		if (c == L' ' || c == L'_' || c == L'-' || c == L'.') {
			start = true;
			continue;
		}
		if (start && out.size() < 2) out += c;
		start = false;
	}
	if (out.empty()) return "?";
	if (out.size() == 1 && w.size() > 1 && w[1] != L' ') out += w[1];
	CharUpperBuffW(&out[0], static_cast<DWORD>(out.size()));
	const int n = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), static_cast<int>(out.size()), nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, out.c_str(), static_cast<int>(out.size()), &s[0], n, nullptr, nullptr);
	return s;
}

std::string GamesText(int games)
{
	if (games == 0) return Tr("No games yet");
	return games == 1 ? Tr("1 game") : TrFmt("%s games", {Grouped(games)});
}

std::string Duration(uint32_t ms)
{
	const uint32_t minutes = ms / 60000;
	char buf[32];
	if (minutes < 60) sprintf_s(buf, "%u min", minutes);
	else sprintf_s(buf, "%u h %02u", minutes / 60, minutes % 60);
	return buf;
}

std::string Date(const RecentRow &r)
{
	char buf[32];
	sprintf_s(buf, "%02d/%02d/%04d", r.day, r.month, r.year);
	return buf;
}

std::string GamesAndWins(int games, int wins)
{
	if (!games) return kDash;
	return TrFmt("%s \xC2\xB7 %s won", {Grouped(games), Percent(PercentOf(wins, games))});
}

void PaintMonogram(Painter &p, int x, int y, int size, const std::string &name, bool strong)
{
	p.FillV(x, y, size, size, strong ? RGB(70, 50, 24) : RGB(44, 33, 20), strong ? RGB(40, 28, 14) : RGB(28, 21, 13));
	p.Frame(x, y, size, size, strong ? kGold : pal::bronze);
	p.Font(size >= 60 ? 5 : 1);
	Raw(p, x, y, size, size, Initials(name), strong ? pal::goldLight : kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintListRow(Painter &p, const Card &c, int index, bool selected, int x, int y, int w, int mx, int my)
{
	const bool hot = !selected && p.Hover(x, y, w, kRowH, mx, my);
	if (selected) {
		p.FillH(x, y, w, kRowH, kSelectedTop, pal::panel);
		p.Frame(x, y, w, kRowH, pal::bronze);
		p.FillV(x, y, 4, kRowH, pal::goldLight, pal::bronze);
	}
	else {
		p.Fill(x, y, w, kRowH, hot ? kRowHot : kRowBody);
		p.Frame(x, y, w, kRowH, pal::lineDim);
	}
	PaintMonogram(p, x + 12, y + 9, 40, c.name, selected);
	int nameW = w - 74;
	if (c.active) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 14;
		PaintBadge(p, x + w - bw - 10, y + 11, bw, 17, "In use", RGB(70, 46, 18), RGB(236, 186, 110));
		nameW -= bw + 8;
	}
	p.Font(1);
	Raw(p, x + 62, y + 9, nameW, 22, c.name, selected ? pal::goldLight : kText);
	p.Font(3);
	const std::string games = GamesText(c.summary.games);
	const std::string line = c.rankName.empty() || !c.readable ? games : c.rankName + "  \xC2\xB7  " + games;
	Raw(p, x + 62, y + 31, w - 74, 18, line, selected ? RGB(200, 180, 136) : kDim);
	p.AddHit(x, y, w, kRowH, kActPick, index);
}

void PaintList(Painter &p, const View &v)
{
	p.Font(4);
	p.Text(kListX, kTop, kListW, 20, "Players", kGold);
	p.Font(3);
	Raw(p, kListX, kTop, kListW, 20, std::to_string(v.cards.size()), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const int count = static_cast<int>(v.cards.size());
	const int maxScroll = std::max(0, count - kListRows);
	const int scroll = std::max(0, std::min(v.scroll, maxScroll));
	const int rowW = maxScroll > 0 ? kListW - 12 : kListW;
	for (int r = 0; r < kListRows && scroll + r < count; r++) {
		const int i = scroll + r;
		PaintListRow(p, v.cards[i], i, i == v.selected, kListX, kListTop + r * (kRowH + kRowGap), rowW, v.mouseX, v.mouseY);
	}
	if (maxScroll > 0) {
		const int trackY = kListTop, trackH = kListRows * (kRowH + kRowGap) - kRowGap;
		const int thumbH = std::max(24, trackH * kListRows / count);
		p.Fill(kListX + kListW - 6, trackY, 6, trackH, pal::track);
		p.Fill(kListX + kListW - 6, trackY + (trackH - thumbH) * scroll / maxScroll, 6, thumbH, pal::thumb);
	}
	const bool one = v.selected >= 0 && v.selected < count;
	const int bw = (kListW - 2 * 9) / 3;
	p.Button(kListX, kListButtonsY, bw, kListButtonH, "New", kActNew, 0, v.mouseX, v.mouseY);
	p.Button(kListX + bw + 9, kListButtonsY, bw, kListButtonH, "Rename", kActRename, 0, v.mouseX, v.mouseY, one);
	p.Button(kListX + 2 * (bw + 9), kListButtonsY, kListW - 2 * (bw + 9), kListButtonH, "Delete", kActDelete, 0, v.mouseX, v.mouseY, one && count > 1);
}

void PaintIdentity(Painter &p, const View &v, const Card &c)
{
	const int x = kRightX, y = kIdentityY, w = kRightW;
	PaintCard(p, x, y, w, kIdentityH);
	const int ax = x + 14, ay = y + (kIdentityH - kAvatar) / 2;
	const bool steam = c.active && v.avatar && !v.avatar->bgra.empty();
	if (steam) {
		p.Image(ax, ay, kAvatar, kAvatar, v.avatar->bgra.data(), v.avatar->width, v.avatar->height);
		p.Frame(ax, ay, kAvatar, kAvatar, kGold);
	}
	else {
		PaintMonogram(p, ax, ay, kAvatar, c.name, true);
	}
	const int tx = ax + kAvatar + 18;
	int right = x + w - 14;
	if (c.active) {
		p.Font(3);
		const int bw = p.TextWidth("In use") + 16;
		PaintBadge(p, right - bw, y + 14, bw, 18, "In use", RGB(70, 46, 18), RGB(236, 186, 110));
		if (!v.steamName.empty()) {
			const std::string steamLine = "Steam \xC2\xB7 " + v.steamName;
			Raw(p, right - 220, y + 36, 220, 18, steamLine, kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		right -= std::max(bw, 0) + 12;
	}
	p.Font(2);
	const PatronTier tier = c.active ? v.patron.tier : PatronTier::None;
	if (tier != PatronTier::None) {
		const int laurel = PatronLaurelSize(tier);
		p.raw = true;
		DrawPatron(p, tx, y + 14 + (30 - laurel) / 2, laurel, tx + laurel + 8, y + 12, right - tx - laurel - 8, 34, c.name, tier,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		PatronHotspot(p, tx, y + 14 + (30 - laurel) / 2, laurel, v.patron, v.mouseX, v.mouseY, kActPatron);
	}
	else {
		Raw(p, tx, y + 12, right - tx, 34, c.name, pal::goldLight);
	}
	if (!c.readable) {
		p.Font(3);
		p.Text(tx, y + 50, x + w - 14 - tx, 20, "Couldn't read this player's statistics.", kBad);
		return;
	}
	const ProfileSummary &s = c.summary;
	p.Font(1);
	Raw(p, tx, y + 48, x + w - 14 - tx, 22, c.rankName.empty() ? std::string(kDash) : c.rankName, kGold);
	if (c.rankName.empty()) return;
	const int barW = std::min(380, x + w - 14 - tx);
	float fraction = 1.0f;
	std::string label;
	if (s.nextRank >= 0) {
		const int span = std::max(1, s.nextPoints - s.rankPoints);
		fraction = static_cast<float>(s.rating - s.rankPoints) / span;
		label = TrFmt("Military rating %s / %s \xC2\xB7 next: %s", {Grouped(static_cast<uint64_t>(std::max<int64_t>(0, s.rating))),
			Grouped(static_cast<uint64_t>(std::max(0, s.nextPoints))), c.nextRankName});
	}
	else {
		label = TrFmt("Military rating %s \xC2\xB7 highest rank", {Grouped(static_cast<uint64_t>(std::max<int64_t>(0, s.rating)))});
	}
	PaintProgress(p, tx, y + 74, barW, 7, fraction);
	p.Frame(tx - 1, y + 73, barW + 2, 9, pal::lineDim);
	p.Font(3);
	Raw(p, tx, y + 84, x + w - 14 - tx, 18, label, RGB(196, 178, 136));
}

void PaintTiles(Painter &p, const Card &c)
{
	const ProfileSummary &s = c.summary;
	const int tileW = (kRightW - 3 * kTileGap) / 4;
	const bool any = c.readable && s.games > 0;
	std::string hours = kDash;
	if (any) {
		const uint64_t h = s.playedMs / 3600000;
		hours = h == 0 ? "<1" : Grouped(h);
	}
	const std::string values[4] = {c.readable ? Grouped(s.games) : std::string(kDash), any ? Percent(PercentOf(s.singleWins + s.multiWins, s.games)) : std::string(kDash),
		hours, any ? Grouped(s.kills) : std::string(kDash)};
	const char *labels[4] = {"Games", "Wins", "Hours played", "Kills"};
	for (int i = 0; i < 4; i++) {
		const int x = kRightX + i * (tileW + kTileGap);
		PaintCard(p, x, kTilesY, tileW, kTileH);
		p.Font(5);
		Raw(p, x, kTilesY + 6, tileW, 42, values[i], pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x + 6, kTilesY + 50, tileW - 12, 18, labels[i], kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

void PaintCardTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	p.Font(4);
	p.Text(x + kCardPad, y + 7, w - kCardPad * 2, 18, title, kGold);
}

void PaintSummary(Painter &p, const Card &c)
{
	const int x = kRightX, y = kLowerY, w = kSummaryW, h = kLowerBottom - kLowerY;
	PaintCard(p, x, y, w, h);
	PaintCardTitle(p, x, y, w, "Summary");
	const ProfileSummary &s = c.summary;
	const bool ok = c.readable;
	const std::string civ = ok && !c.favoriteCiv.empty() ? c.favoriteCiv + " \xC2\xB7 " + Percent(s.favoriteRacePercent) : std::string(kDash);
	const std::string best = ok && !c.bestUnit.empty() ? TrFmt("%s \xC2\xB7 level %s", {c.bestUnit, std::to_string(s.bestLevel)}) : std::string(kDash);
	const std::string army = ok && s.largestArmy ? TrFmt("%s units", {Grouped(s.largestArmy)}) : std::string(kDash);
	const std::pair<const char *, std::string> rows[] = {
		{"Skirmish", ok ? GamesAndWins(s.singleGames, s.singleWins) : std::string(kDash)},
		{"Multiplayer", ok ? GamesAndWins(s.multiGames, s.multiWins) : std::string(kDash)},
		{"Favourite civilization", civ},
		{"Favourite unit", ok && !c.favoriteUnit.empty() ? c.favoriteUnit : std::string(kDash)},
		{"Best unit", best},
		{"Largest army", army},
	};
	const int rowH = 36, inner = w - kCardPad * 2;
	int ry = y + kCardHead;
	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++, ry += rowH) {
		if (i) p.Fill(x + kCardPad, ry, inner, 1, RGB(48, 37, 23));
		p.Font(3);
		p.Text(x + kCardPad, ry + 3, inner, 17, rows[i].first, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(4);
		Raw(p, x + kCardPad, ry + 19, inner, 18, rows[i].second, kText);
	}
	p.Fill(x + kCardPad, ry + 4, inner, 1, pal::lineDim);
	p.Font(3);
	p.TextWrapped(x + kCardPad, ry + 12, inner, y + h - ry - 16, "Adventures, Great Battles and Conquest are not counted by the game.", RGB(140, 126, 98));
}

void PaintChart(Painter &p, const Card &c, int x, int y, int w, int h)
{
	const int n = std::min(static_cast<int>(c.recent.size()), kChartGames);
	p.Font(3);
	p.Text(x, y, w, 16, "Kills per game", kDim);
	const int legendY = y + 4;
	int lx = x + w;
	for (const auto &item : {std::make_pair("Lost", kLostColor), std::make_pair("Won", kWonColor)}) {
		const int tw = p.TextWidth(item.first);
		lx -= tw;
		p.Text(lx, y, tw + 2, 16, item.first, kDim);
		lx -= 14;
		p.Fill(lx, legendY + 2, 9, 9, item.second);
		lx -= 12;
	}
	const int top = y + 22, bottom = y + h;
	uint32_t most = 1;
	for (int i = 0; i < n; i++) most = std::max(most, c.recent[i].kills);
	const int slot = w / kChartGames, barW = std::max(4, slot - 6);
	p.Fill(x, bottom, w, 1, pal::line);
	for (int k = 0; k < n; k++) {
		const RecentRow &r = c.recent[n - 1 - k];
		const int bh = std::max(2, static_cast<int>((bottom - top - 12) * static_cast<uint64_t>(r.kills) / most));
		const int bx = x + k * slot + (slot - barW) / 2;
		p.FillV(bx, bottom - bh, barW, bh, r.won ? kWonColor : kLostColor, r.won ? RGB(70, 104, 50) : RGB(110, 48, 36));
	}
	p.Font(3);
	Raw(p, x, top - 4, 80, 14, Grouped(most), RGB(120, 108, 84));
}

void PaintRecent(Painter &p, const Card &c)
{
	const int x = kRightX + kSummaryW + 12, y = kLowerY, w = kRightW - kSummaryW - 12, h = kLowerBottom - kLowerY;
	PaintCard(p, x, y, w, h);
	PaintCardTitle(p, x, y, w, "Recent games");
	const int x0 = x + kCardPad, inner = w - kCardPad * 2;
	if (c.recent.empty()) {
		p.Font(0);
		p.Text(x0, y + 70, inner, 22, c.readable ? "No games recorded yet." : kDash, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x0 + 20, y + 100, inner - 40, 60, "Finish a skirmish or a multiplayer game and it will show here.", kDim, DT_CENTER | DT_TOP | DT_WORDBREAK);
		return;
	}
	const int dateW = 68, badgeX = x0 + dateW + 4, civX = badgeX + 30, killsW = 40, timeW = 52;
	const int killsX = x0 + inner - killsW, timeX = killsX - timeW - 6, civW = timeX - civX - 6;
	int ry = y + kCardHead;
	p.Font(3);
	const COLORREF head = RGB(130, 116, 90);
	p.Text(x0, ry, dateW, 16, "Date", head);
	p.Text(civX, ry, civW, 16, "Civilization", head);
	p.Text(timeX, ry, timeW, 16, "Duration", head, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.Text(killsX, ry, killsW, 16, "Kills", head, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	ry += 20;
	const int rowH = 28;
	for (int i = 0; i < kRecentRows && i < static_cast<int>(c.recent.size()); i++, ry += rowH) {
		const RecentRow &r = c.recent[i];
		p.Fill(x0, ry, inner, 1, RGB(48, 37, 23));
		p.Font(3);
		Raw(p, x0, ry, dateW, rowH, Date(r), kDim);
		p.Fill(badgeX, ry + 6, 22, rowH - 12, r.won ? RGB(46, 70, 34) : RGB(84, 34, 26));
		p.Font(4);
		p.Text(badgeX, ry + 6, 22, rowH - 12, r.won ? "W" : "L", r.won ? RGB(170, 220, 140) : RGB(240, 150, 128), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		const std::string civ = r.civ.empty() ? std::string(kDash) : r.civ;
		// The mode always shows; a long civilization gives way to it.
		const std::string mode = " \xC2\xB7 " + Tr(r.multi ? "Multiplayer" : "Skirmish");
		p.Font(3);
		const int modeW = std::min(civW / 2, RawWidth(p, mode));
		p.Font(4);
		const int civTextW = std::min(civW - modeW, RawWidth(p, civ));
		Raw(p, civX, ry, civTextW, rowH, civ, kText);
		p.Font(3);
		Raw(p, civX + civTextW, ry, civW - civTextW, rowH, mode, kDim);
		Raw(p, timeX, ry, timeW, rowH, Duration(r.durationMs), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Font(4);
		Raw(p, killsX, ry, killsW, rowH, Grouped(r.kills), kText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	const int chartY = std::max(ry + 10, kLowerBottom - 108);
	PaintChart(p, c, x0, chartY, inner, kLowerBottom - 14 - chartY);
}

void PaintFooter(Painter &p, const View &v)
{
	p.Fill(18, kFooterTop, kScreenW - 36, 1, pal::line);
	const bool one = v.selected >= 0 && v.selected < static_cast<int>(v.cards.size());
	int bx = kScreenW - 24 - 150;
	p.Button(bx, kFooterButtonsY, 150, kFooterButtonH, "Play", kActPlay, 0, v.mouseX, v.mouseY, one, true);
	bx -= 10 + 120;
	p.Button(bx, kFooterButtonsY, 120, kFooterButtonH, "Back", kActBack, 0, v.mouseX, v.mouseY);
	if (one) {
		p.Font(3);
		const std::string hint = v.cards[v.selected].active ? Tr("You are playing as this player.") : TrFmt("You will play as %s.", {v.cards[v.selected].name});
		p.Diamond(kListX + 4, kFooterButtonsY + kFooterButtonH / 2, 4, kGold);
		Raw(p, kListX + 16, kFooterButtonsY, bx - kListX - 28, kFooterButtonH, hint, RGB(186, 170, 134));
	}
}

void PaintModal(Painter &p, const View &v)
{
	if (v.modal == kModalNone) return;
	const int mx = v.mouseX, my = v.mouseY;
	const Card *card = v.selected >= 0 && v.selected < static_cast<int>(v.cards.size()) ? &v.cards[v.selected] : nullptr;
	if (v.modal == kModalDelete) {
		if (!card) return;
		const Dialog d = DialogBegin(p, kScreenW, kScreenH, 560, 204, "Delete player");
		std::string text;
		if (card->saves == 0) text = TrFmt("Delete \"%s\"? Its statistics will be lost.", {card->name});
		else if (card->saves == 1) text = TrFmt("Delete \"%s\"? Its statistics and its saved game will be lost.", {card->name});
		else text = TrFmt("Delete \"%s\"? Its statistics and its %s saved games will be lost.", {card->name, Grouped(card->saves)});
		p.raw = true;
		DialogText(p, d, 64, text + " " + Tr("This cannot be undone."));
		p.raw = false;
		DialogButtons(p, d, {{"Delete", kActModalOk, 0, true}, {"Cancel", kActModalCancel}}, mx, my);
		return;
	}
	const bool create = v.modal == kModalNew;
	const Dialog d = DialogBegin(p, kScreenW, kScreenH, 500, 216, create ? "New player" : "Rename player");
	p.Font(3);
	p.Text(d.BodyX(), d.y + 66, d.BodyW(), 18, "Name", kDim);
	PaintEditField(p, d.BodyX(), d.y + 88, d.BodyW(), 34, v.text, create ? "Name of the new player" : "", true, false, v.edit, kActField);
	DialogButtons(p, d, {{create ? "Create" : "Rename", kActModalOk, 0, true, !NameDraft(v.text).empty()}, {"Cancel", kActModalCancel}}, mx, my);
}

}

std::string NameDraft(const std::string &text)
{
	size_t a = 0, b = text.size();
	while (a < b && text[a] == ' ') a++;
	while (b > a && text[b - 1] == ' ') b--;
	return text.substr(a, b - a);
}

void PaintProfileScreen(Painter &p, const View &v)
{
	p.noHover = v.modal != kModalNone;
	PaintWindowFrame(p, kScreenW, kScreenH);
	PaintWindowHeader(p, kScreenW, "PLAYER");
	PaintList(p, v);
	p.Fill(kRightX - 12, kTop, 1, kLowerBottom - kTop, pal::lineDim);
	if (v.selected >= 0 && v.selected < static_cast<int>(v.cards.size())) {
		const Card &c = v.cards[v.selected];
		PaintIdentity(p, v, c);
		PaintTiles(p, c);
		PaintSummary(p, c);
		PaintRecent(p, c);
	}
	else {
		p.Font(0);
		p.Text(kRightX, kTop + 120, kRightW, 24, v.cards.empty() ? "Create a player to start." : "Choose a player.", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	PaintFooter(p, v);
	PaintPatronTip(p, kScreenW, kScreenH);
	PaintModal(p, v);
}

}
