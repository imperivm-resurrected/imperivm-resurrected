// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "core/leaguemaps.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "game/mapinfo.h"
#include "game/mappreview.h"
#include "i18n/tr.h"
#include "net/leagueclient_tourney.h"
#include "ui/fullscreen.h"
#include "ui/leaguemapthumb.h"
#include "ui/leagueui.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {
namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

// The scenarios of a tab of the chooser: the game's (1) or the player's (2), as indexes into the whole list; in a cup of teams, only those
// with a human seat for each of the four players.
std::vector<int> TabRows(const CupScenariosView &v, int tab, TourneyMode mode)
{
	std::vector<int> out;
	for (size_t i = 0; i < v.list.size(); i++) {
		if (v.list[i].game == (tab == 1) && v.list[i].humans >= 2 * TourneySidePlayers(mode)) out.push_back(static_cast<int>(i));
	}
	return out;
}

void Preview(Painter &p, const std::string &path, int x, int y, int size, bool starts = true) { PaintScenarioThumb(p, path, x, y, size, starts); }

// The chooser on a map: its tab, and the random map's type and size or the scenario's file.
void OpenOn(Ui &u, const TourneyMap &m)
{
	u.chooserScroll = 0;
	u.chooserFile.clear();
	const std::vector<std::string> types = LeagueMapTypes();
	u.chooserType = 0;
	for (size_t i = 0; i < types.size(); i++) {
		if (types[i] == (m.Scenario() ? std::string("mediterranean") : m.name)) u.chooserType = static_cast<int>(i);
	}
	const std::vector<std::string> sizes = types.empty() ? std::vector<std::string>() : CupSizesOf(u.cup, types[static_cast<size_t>(u.chooserType)]);
	u.chooserSize = 0;
	for (size_t i = 0; i < sizes.size(); i++) {
		if (sizes[i] == (m.Scenario() ? std::string("medium") : m.size)) u.chooserSize = static_cast<int>(i);
	}
	u.chooserTab = !m.Scenario() ? 0 : m.kind == TourneyMapKind::Game ? 1 : 2;
	g_s.pickSearch.clear();
	if (g_s.focus == kPickSearchField) g_s.focus = kNoField;
	if (m.Scenario()) u.chooserFile = Lower(m.file);
	CupScenariosRescan();
}

}

std::vector<std::string> CupSizesOf(const TourneyCupForm &f, const std::string &type)
{
	return TourneySizesFor(LeagueTourneyMapTypes(), type, f.mode);
}

void OpenMapChooser(Ui &u, int round)
{
	if (round < 0 || round >= static_cast<int>(u.cup.maps.size())) return;
	OpenOn(u, u.cup.maps[static_cast<size_t>(round)]);
	u.chooserRound = round;
	u.chooserPool = false;
}

void OpenPoolChooser(Ui &u, int index)
{
	const std::vector<TourneyMap> &pool = u.cup.seriesPool;
	if (index < 0 || index > static_cast<int>(pool.size()) || index >= kTourneyPoolMax) return;
	TourneyMap m;
	if (index < static_cast<int>(pool.size())) m = pool[static_cast<size_t>(index)];
	else {
		// A new one starts on the first random map not among them yet.
		for (const std::string &type : LeagueMapTypes()) {
			const std::vector<std::string> sizes = CupSizesOf(u.cup, type);
			if (sizes.empty()) continue;
			const std::string size = std::find(sizes.begin(), sizes.end(), "medium") != sizes.end() ? std::string("medium") : sizes.front();
			const TourneyMap candidate = {type, "random", size};
			bool taken = false;
			for (const TourneyMap &x : pool) taken = taken || TourneySameMap(x, candidate);
			if (!taken) {
				m = candidate;
				break;
			}
		}
	}
	OpenOn(u, m);
	u.chooserRound = index;
	u.chooserPool = true;
}

void PaintMapPicture(Painter &p, const TourneyMap &m, int x, int y, int size)
{
	if (!m.Known()) {
		p.Fill(x, y, size, size, pal::bg);
		UseDisplay(p, std::max(16, size / 2));
		const bool raw = p.raw;
		p.raw = true;
		p.Text(x, y, size, size, "?", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = raw;
		p.Frame(x, y, size, size, pal::lineStrong);
		return;
	}
	if (!m.Scenario()) {
		PaintLeagueMapThumb(p, x, y, size, m.name);
		return;
	}
	// Only a copy that is the cup's is shown: another one, or one coming down, would show a different map.
	const CupMapState s = LeagueCupMap(m);
	if (s.status == CupMapStatus::Ready) {
		Preview(p, CupScenarioPath(m.file), x, y, size);
		return;
	}
	p.Fill(x, y, size, size, pal::bg);
	if (s.status == CupMapStatus::Checking || s.status == CupMapStatus::Downloading) p.Spinner(x + size / 2, y + size / 2, std::max(6, size / 8), 3, GetTickCount());
	else p.Diamond(x + size / 2, y + size / 2, std::max(4, size / 12), pal::caption);
	p.Frame(x, y, size, size, pal::lineStrong);
}

TourneyMap ChooserMap(const Ui &u)
{
	TourneyMap m;
	if (u.chooserTab == 0) {
		const std::vector<std::string> types = LeagueMapTypes();
		if (u.chooserType < 0 || u.chooserType >= static_cast<int>(types.size())) return m;
		const std::vector<std::string> sizes = CupSizesOf(u.cup, types[static_cast<size_t>(u.chooserType)]);
		if (sizes.empty()) return m;
		m.name = types[static_cast<size_t>(u.chooserType)];
		m.type = "random";
		m.size = sizes[static_cast<size_t>(std::max(0, std::min(u.chooserSize, static_cast<int>(sizes.size()) - 1)))];
		return m;
	}
	const CupScenariosView v = CupScenarios();
	for (const CupScenario &s : v.list) {
		if (Lower(s.file) != u.chooserFile || s.game != (u.chooserTab == 1)) continue;
		m.kind = s.game ? TourneyMapKind::Game : TourneyMapKind::Upload;
		m.title = s.title;
		m.file = s.file;
		m.hash = s.sha256;
		m.bytes = static_cast<int64_t>(s.bytes);
		m.players = s.humans;
		break;
	}
	return m;
}

std::string CupMapLine(const TourneyMap &m)
{
	if (!m.Scenario()) return Tr("Random map") + " \xC2\xB7 " + MapText(m);
	return m.title + " \xC2\xB7 " + Tr(m.kind == TourneyMapKind::Game ? "scenario of the game" : "uploaded scenario");
}

void PaintMapChooser(Painter &p, const State &st, const Ui &u, PaintOut &out)
{
	const int mx = st.mouseX, my = st.mouseY;
	const int w = std::min(1120, st.viewW - 60), h = std::min(720, st.viewH - 60);
	p.raw = true;
	const std::string title = u.chooserPool ? Tr("Choose a map of the series") : Tr("Choose the map") + " \xC2\xB7 " + CupRoundName(u.cup, u.chooserRound);
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, w, h, title, pal::text);
	const CupScenariosView v = CupScenarios();
	const std::vector<int> game = TabRows(v, 1, u.cup.mode), mine = TabRows(v, 2, u.cup.mode);
	const std::string counts[] = {std::string(), v.loaded ? " (" + std::to_string(game.size()) + ")" : std::string(), v.loaded ? " (" + std::to_string(mine.size()) + ")" : std::string()};
	const std::vector<TabSpec> tabs = {{Tr("Random map"), kTourney, ArgOf(kSubChooserTab, 0), u.chooserTab == 0},
		{Tr("Scenarios of the game") + counts[1], kTourney, ArgOf(kSubChooserTab, 1), u.chooserTab == 1}, {Tr("Own maps") + counts[2], kTourney, ArgOf(kSubChooserTab, 2), u.chooserTab == 2}};
	const int tabsY = d.y + 62, tabW = (d.BodyW() - 12) / 3;
	p.Fill(d.BodyX(), tabsY + 42, d.BodyW(), 1, pal::hairline);
	PaintTabs(p, d.BodyX(), tabsY, tabs, mx, my, tabW, 42, 1);

	const int listX = d.BodyX(), listY = tabsY + 58, listW = std::min(440, d.BodyW() * 42 / 100), listH = d.FooterTop() - 18 - listY;
	const int listYTop = listY, listHTop = listH;
	const int dx = listX + listW + 22, dw = d.x + d.w - d.pad - dx;
	const TourneyMap chosen = ChooserMap(u);
	bool usable = chosen.Known();
	if (u.chooserTab == 0) {
		p.Fill(listX, listY, listW, listH, pal::bg);
		p.Frame(listX, listY, listW, listH, pal::lineStrong);
	}
	PaintCard(p, dx, listY, dw, listH);

	if (u.chooserTab == 0) {
		const std::vector<std::string> types = LeagueMapTypes();
		// The league's types once a frame, not once a row.
		const std::vector<TourneyMapType> leagueTypes = LeagueTourneyMapTypes();
		const int rowH = 46, viewRows = std::max(1, (listH - 4) / rowH), maxScroll = std::max(0, static_cast<int>(types.size()) - viewRows);
		const int scroll = std::max(0, std::min(u.chooserScroll, maxScroll));
		p.Clip(listX + 1, listY + 1, listW - 2, listH - 2);
		for (int r = 0; r < viewRows && scroll + r < static_cast<int>(types.size()); r++) {
			const int i = scroll + r, ry = listY + 2 + r * rowH;
			const bool on = i == u.chooserType, hot = !on && p.Hover(listX + 2, ry, listW - 16, rowH, mx, my);
			if (on) p.Fill(listX + 2, ry, listW - 4, rowH, pal::selected);
			else if (hot) p.Fill(listX + 2, ry, listW - 4, rowH, pal::raised);
			if (on) p.Fill(listX + 2, ry, 3, rowH, pal::red);
			p.Font(0);
			p.Text(listX + 16, ry + 3, listW - 40, 22, LeagueMapText(types[static_cast<size_t>(i)]), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			std::string sizes;
			for (const std::string &s : TourneySizesFor(leagueTypes, types[static_cast<size_t>(i)], u.cup.mode)) sizes += (sizes.empty() ? "" : ", ") + LeagueMapText(s);
			p.Font(3);
			p.Text(listX + 16, ry + 24, listW - 40, 18, sizes, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (!on) p.AddHit(listX + 2, ry, listW - 16, rowH, kTourney, ArgOf(kSubChooserRow, i));
		}
		p.Unclip();
		SetScrollArea(p, out.chooserArea, listX, listY, listW, listH, maxScroll);
		if (maxScroll > 0) PaintScrollbar(p, kBarChooser, listX + listW - 10, listY + 2, listH - 4, {scroll, maxScroll, viewRows}, st.dropBar, mx, my);

		const int pad = 18, size = std::min(220, std::min(dw - 2 * pad, listH - 2 * pad - 150));
		Preview(p, std::string(), dx + pad, listY + pad, size);
		const int tx = dx + pad + size + 18, tw = dx + dw - pad - tx;
		if (chosen.Known()) {
			UseFace(p, BrandFace::UiStrong, 20);
			p.Text(tx, listY + pad, tw, 28, LeagueMapText(chosen.name), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(0);
			p.TextWrapped(tx, listY + pad + 36, tw, size - 36,
				Tr("The game makes a new map from this template for every game, with the size you choose. Nobody has to download anything."), pal::body);
			const std::vector<std::string> sizes = TourneySizesFor(leagueTypes, chosen.name, u.cup.mode);
			const int sy = listY + pad + size + 22;
			PaintCaps(p, dx + pad, sy, dw - 2 * pad, 20, Tr("Size"), pal::caption);
			std::vector<std::string> labels;
			std::vector<int> args;
			int sel = -1;
			for (size_t i = 0; i < sizes.size(); i++) {
				labels.push_back(LeagueMapText(sizes[i]));
				args.push_back(ArgOf(kSubChooserSize, static_cast<int64_t>(i)));
				if (sizes[i] == chosen.size) sel = static_cast<int>(i);
			}
			PaintSegments(p, dx + pad, sy + 28, std::min(dw - 2 * pad, 420), 42, labels, sel, args, mx, my);
			p.Font(3);
			p.TextWrapped(dx + pad, sy + 82, dw - 2 * pad, 44,
				Tr(u.cup.mode == TourneyMode::Teams ? "Only the sizes the template has room for four players in." : "Only the sizes the template has room for two players in."),
				pal::caption);
		}
	}
	else {
		// The same search and order as the room's selector, over the tab's scenarios.
		PaintScenarioToolbar(p, st, listX, listY, listW, 36);
		const int tabRows = static_cast<int>((u.chooserTab == 1 ? game : mine).size());
		const std::vector<int> rows = ScenarioOrder(v.list, u.chooserTab == 1 ? game : mine, st.pickSearch, st.pickSort);
		const int listY = listYTop + 50, listH = listHTop - 50;
		p.Fill(listX, listY, listW, listH, pal::bg);
		p.Frame(listX, listY, listW, listH, pal::lineStrong);
		const int rowH = kScenarioRowH, viewRows = std::max(1, (listH - 4) / rowH), maxScroll = std::max(0, static_cast<int>(rows.size()) - viewRows);
		const int scroll = std::max(0, std::min(u.chooserScroll, maxScroll));
		p.Clip(listX + 1, listY + 1, listW - 2, listH - 2);
		for (int r = 0; r < viewRows && scroll + r < static_cast<int>(rows.size()); r++) {
			const int i = rows[static_cast<size_t>(scroll + r)], ry = listY + 2 + r * rowH;
			const CupScenario &s = v.list[static_cast<size_t>(i)];
			const bool on = Lower(s.file) == u.chooserFile, hot = !on && p.Hover(listX + 2, ry, listW - 16, rowH, mx, my);
			PaintScenarioRow(p, s, listX + 2, ry, listW - 16, rowH, on, hot, std::string(), pal::ok, s.bytes > static_cast<uint64_t>(kTourneyScenarioMaxBytes) && !s.game);
			if (!on) p.AddHit(listX + 2, ry, listW - 16, rowH, kTourney, ArgOf(kSubChooserRow, i));
		}
		if (rows.empty() && tabRows > 0) {
			p.Font(0);
			p.Text(listX + 16, listY + 12, listW - 32, 28, Tr("No map matches the search."), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
		else if (rows.empty()) {
			p.Font(0);
			const bool teams = u.cup.mode == TourneyMode::Teams;
			std::string empty = Tr("Looking for the scenarios...");
			if (v.loaded && u.chooserTab == 1) {
				empty = Tr(teams ? "No scenario of the game for four players or more was found. Check the game's files in Steam."
								 : "No scenario of the game for two players or more was found. Check the game's files in Steam.");
			}
			else if (v.loaded) {
				empty = Tr(teams ? "You have no scenarios of your own for four players or more. Put them in the game's Scenarios folder, or install a content pack of maps."
								 : "You have no scenarios of your own for two players or more. Put them in the game's Scenarios folder, or install a content pack of maps.");
			}
			p.TextWrapped(listX + 16, listY + 16, listW - 32, listH - 32, empty, pal::textDim);
		}
		p.Unclip();
		SetScrollArea(p, out.chooserArea, listX, listY, listW, listH, maxScroll);
		if (maxScroll > 0) PaintScrollbar(p, kBarChooser, listX + listW - 10, listY + 2, listH - 4, {scroll, maxScroll, viewRows}, st.dropBar, mx, my);

		const int pad = 18;
		const CupScenario *s = nullptr;
		for (const CupScenario &x : v.list) {
			if (Lower(x.file) == u.chooserFile && x.game == (u.chooserTab == 1)) s = &x;
		}
		if (!s) {
			p.Font(0);
			std::string text = Tr("Choose a scenario from the list to see it here.");
			if (u.chooserTab == 2) text += "\n\n" + Tr("Own maps are your scenarios that are not the game's own: made with the editor or downloaded. The cup uploads the one you choose, and the players download it when they check in.");
			if (u.chooserTab == 2 && v.fromRules > 0) {
				text += "\n\n" + TrFmt("%s scenarios of content packs that change the rules are not listed: they go with the cup's content.", {std::to_string(v.fromRules)});
			}
			p.TextWrapped(dx + pad, listY + pad, dw - 2 * pad, listH - 2 * pad, text, pal::textDim);
		}
		else {
			const bool large = !s->game && s->bytes > static_cast<uint64_t>(kTourneyScenarioMaxBytes);
			usable = usable && !large;
			std::string note = s->game ? Tr("Every player has it as it is: nothing goes up or down.")
				: large				  ? Tr("It is larger than 16 MB: the league does not keep files that large.")
									  : Tr("It goes up to the league when you publish the cup, and each player downloads it on their own when they sign up or open the cup.");
			PaintScenarioCard(p, *s, dx, listYTop, dw, listHTop, {HumanBytes(s->bytes)}, note, large ? pal::bad : pal::body, ScenarioSeat());
		}
	}
	DialogButtons(p, d, {{Tr("Use this map"), kTourney, ArgOf(kSubChooserOk, 0), true, usable}, {Tr("Cancel"), kTourney, ArgOf(kSubChooserCancel, 0)}}, mx, my);
	p.raw = false;
}

bool CupHasScenarios(const TourneySummary &t)
{
	for (const std::vector<TourneyMap> *list : {&t.maps, &t.seriesPool}) {
		for (const TourneyMap &m : *list) {
			if (m.Scenario()) return true;
		}
	}
	return false;
}

std::vector<TourneyMap> CupAllMaps(const TourneySummary &t)
{
	std::vector<TourneyMap> out = t.maps;
	out.insert(out.end(), t.seriesPool.begin(), t.seriesPool.end());
	return out;
}

std::string CupMapWarning(const TourneyMap &m)
{
	if (!m.Scenario()) return std::string();
	const CupMapState s = LeagueCupMap(m);
	switch (s.status) {
	case CupMapStatus::Missing: return TrFmt("You don't have \"%s\", the game's scenario of this round. Check the game's files in Steam before your match.", {m.title});
	case CupMapStatus::Different:
		if (!s.pack.empty()) return TrFmt("Your copy of \"%s\" is not the game's: the content pack %s changes it. Take it out of your profile for the cup.", {m.title, s.pack});
		return TrFmt("Your copy of \"%s\" is not the game's. Check the game's files in Steam before your match.", {m.title});
	case CupMapStatus::Failed: return TrFmt("The cup's scenario \"%s\" could not be downloaded. Try again from the cup's page.", {m.title});
	case CupMapStatus::Downloading: return TrFmt("Downloading the cup's scenario \"%s\"...", {m.title});
	default: return std::string();
	}
}

void PaintCupMapsState(Painter &p, const State &st, const TourneyDetail &d, int x, int y, int w, int &h)
{
	const int mx = st.mouseX, my = st.mouseY, pad = 22, rowH = 46;
	std::vector<TourneyMap> maps;
	for (const TourneyMap &m : CupAllMaps(d)) {
		if (!m.Scenario()) continue;
		bool seen = false;
		for (const TourneyMap &x2 : maps) seen = seen || TourneySameMap(x2, m);
		if (!seen) maps.push_back(m);
	}
	bool retry = false, ready = true;
	std::vector<CupMapState> states;
	for (const TourneyMap &m : maps) {
		states.push_back(LeagueCupMap(m));
		retry = retry || states.back().status == CupMapStatus::Failed || states.back().status == CupMapStatus::Missing || states.back().status == CupMapStatus::Different;
		ready = ready && states.back().status == CupMapStatus::Ready;
	}
	h = 56 + static_cast<int>(maps.size()) * rowH + (retry ? 52 : 0) + 10;
	PaintGlassCard(p, x, y, w, h);
	p.raw = true;
	PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, Tr("Maps of the cup"), pal::caption);
	p.Font(3);
	p.Text(x + pad, y + 20, w - 2 * pad, 20, ready ? Tr("All ready") : retry ? Tr("Something is missing") : Tr("Getting them ready..."), ready ? pal::ok : retry ? pal::bad : pal::textDim,
		DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	int ry = y + 52;
	for (size_t i = 0; i < maps.size(); i++) {
		const TourneyMap &m = maps[i];
		const CupMapState &s = states[i];
		const bool ok = s.status == CupMapStatus::Ready, bad = s.status == CupMapStatus::Failed || s.status == CupMapStatus::Missing || s.status == CupMapStatus::Different;
		if (ok || bad) PaintCheck(p, x + pad, ry + 4, 18, ok);
		else p.Spinner(x + pad + 9, ry + 13, 8, 3, GetTickCount());
		UseFace(p, BrandFace::UiStrong, 15);
		p.Text(x + pad + 28, ry, w - 2 * pad - 28, 22, m.title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		std::string line;
		switch (s.status) {
		case CupMapStatus::Ready: line = Tr(m.kind == TourneyMapKind::Game ? "The game's, the same as everyone's" : "Downloaded and checked"); break;
		case CupMapStatus::Checking: line = Tr("Checking your copy..."); break;
		case CupMapStatus::Downloading:
			line = TrFmt("Downloading \xC2\xB7 %s", {s.total ? std::to_string(std::min<uint64_t>(100, s.got * 100 / s.total)) + " %" : HumanBytes(s.got)});
			break;
		case CupMapStatus::Missing: line = Tr("You don't have it: check the game's files in Steam"); break;
		case CupMapStatus::Different:
			line = s.pack.empty() ? Tr("Your copy is not the game's: check the game's files in Steam") : TrFmt("The content pack %s changes it", {s.pack});
			break;
		default:
			line = s.reason == "not_found" ? Tr("The league doesn't have it any more: tell the organiser")
				: s.reason == "hash"	   ? Tr("It arrived damaged")
				: s.reason == "save"	   ? Tr("It could not be saved in the Scenarios folder")
				: s.reason == "pack"	   ? TrFmt("The content pack %s brings its own copy", {s.pack})
				: s.reason == "too_large"  ? Tr("It is larger than 16 MB")
										   : Tr("Couldn't download it: check your connection");
			break;
		}
		p.Font(3);
		p.Text(x + pad + 28, ry + 22, w - 2 * pad - 28, 18, line, bad ? pal::bad : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ry += rowH;
	}
	if (retry) PaintSecondButton(p, x + pad, ry + 4, w - 2 * pad, 38, Tr("Try again"), ArgOf(kSubMapsRetry, d.id), true, mx, my);
	p.raw = false;
}

}
