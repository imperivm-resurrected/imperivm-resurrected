// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/battlesui.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "game/battlelist.h"
#include "game/greatbattles.h"
#include "mods/mods.h"
#include "mods/savelib.h"
#include "ui/battlesui_view.h"
#include "ui/fullscreen.h"
#include "ui/modui.h"
#include "ui/savesui.h"
#include "ui/widgets.h"

using namespace ui;
using namespace battlesui;

namespace {

const DWORD kReadEveryMs = 100;
// A load that has not taken the screen away by then did not happen: its buttons work again.
const DWORD kLoadingMs = 5000;
const int kWheelLines = 3;

struct State {
	View view;
	std::vector<Hit> hits;
	PaintOut geom;
	ListState listDrag, descDrag;
	bool mouseDown = false;
	DWORD lastRead = 0;
	DWORD loadingAt = 0;
	bool active = false;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

// The adventure games on disk, read when the screen opens and again when the load screen closes (a game may have been deleted there),
// and the picture of the newest one. Only the game thread touches them.
struct SavesCache {
	bool rescan = true;
	std::vector<SaveRec> recs;
	std::string thumbOf;
	std::shared_ptr<const BattleImage> thumb;
	// The diary of each save already read, by its path, with the save's time: a save written again is read again.
	std::map<std::string, std::pair<uint64_t, std::shared_ptr<const std::vector<ObjectiveNote>>>> diaries;
	// The content packs of the Imperivm I and II campaigns, from the mods' scan, and their pictures: read again when the scan changes
	// or the content screen, opened from here, closes.
	bool packsRead = false;
	uint64_t packsSerial = 0;
	BattleCampaignPack packs[kBattlesTabCount];
	std::shared_ptr<const BattleImage> packCovers[kBattlesTabCount];
};
SavesCache g_saves;
bool g_savesWasOpen = false, g_modsWasOpen = false;

std::string TimeText(uint64_t ft)
{
	FILETIME f = {static_cast<DWORD>(ft), static_cast<DWORD>(ft >> 32)}, l;
	SYSTEMTIME s;
	if (!ft || !FileTimeToLocalFileTime(&f, &l) || !FileTimeToSystemTime(&l, &s)) return "";
	char buf[48];
	sprintf_s(buf, "%02d/%02d/%04d %02d:%02d", s.wDay, s.wMonth, s.wYear, s.wHour, s.wMinute);
	return buf;
}

std::string PlayedText(int seconds)
{
	if (seconds < 0) return "";
	const int minutes = seconds / 60;
	char buf[32];
	sprintf_s(buf, "%d:%02d h", minutes / 60, minutes % 60);
	return buf;
}

int TabOf(int list, const std::string &path)
{
	if (list != kBattlesOthers) return kBattlesTabGreat;
	switch (BattleGameOf(path)) {
	case kBattleGameImperivm1: return kBattlesTabImperivm1;
	case kBattleGameImperivm2: return kBattlesTabImperivm2;
	default: return kBattlesTabCustom;
	}
}

std::shared_ptr<const BattleImage> ThumbOf(const SaveRec &rec)
{
	if (!rec.hasThumb) return nullptr;
	auto img = std::make_shared<BattleImage>();
	if (!SaveLibLoadThumb(rec, img->bgra, img->w, img->h) || img->w <= 0 || img->h <= 0 ||
		img->bgra.size() < static_cast<size_t>(img->w) * static_cast<size_t>(img->h)) {
		return nullptr;
	}
	return img;
}

// The completed objectives kept next to a save (<save>.objectives), newest first; null when there are none or the file cannot be read.
std::shared_ptr<const std::vector<ObjectiveNote>> DiaryOf(const SaveRec &rec)
{
	const auto known = g_saves.diaries.find(rec.vfsPath);
	if (known != g_saves.diaries.end() && known->second.first == rec.time) return known->second.second;
	auto &cached = g_saves.diaries[rec.vfsPath];
	cached = {rec.time, nullptr};
	const DWORD kMaxBytes = 4u << 20;
	HANDLE f = CreateFileA((rec.diskPath + ".objectives").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) return nullptr;
	std::string data;
	LARGE_INTEGER size = {};
	DWORD got = 0;
	if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(kMaxBytes)) {
		data.resize(static_cast<size_t>(size.QuadPart));
		if (!ReadFile(f, &data[0], static_cast<DWORD>(data.size()), &got, nullptr)) got = 0;
		data.resize(got);
	}
	CloseHandle(f);
	ObjectiveLog log;
	if (data.empty() || !log.Decode(data)) {
		if (!data.empty()) LogW("ui", "adventure screen: the objectives of \"%s\" could not be read", rec.vfsPath.c_str());
		return nullptr;
	}
	if (log.Completed().empty()) return nullptr;
	auto notes = std::make_shared<std::vector<ObjectiveNote>>(log.Completed().rbegin(), log.Completed().rend());
	cached.second = notes;
	return notes;
}

// The picture next to the adventure of a pack, read from the mods folder: an inactive pack is not in the game's files.
std::shared_ptr<const BattleImage> PackPicture(const BattleCampaignPack &pack)
{
	std::string rel = BattlePicturePath(pack.adventure);
	if (!pack.installed || rel.empty()) return nullptr;
	std::replace(rel.begin(), rel.end(), '/', '\\');
	const std::string file = ModsFolderPath() + "\\" + pack.folder + "\\" + rel;
	const DWORD kMaxBytes = 32u << 20;
	HANDLE f = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) return nullptr;
	std::vector<unsigned char> data;
	LARGE_INTEGER size = {};
	DWORD got = 0;
	if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(kMaxBytes)) {
		data.resize(static_cast<size_t>(size.QuadPart));
		if (!ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &got, nullptr)) got = 0;
		data.resize(got);
	}
	CloseHandle(f);
	auto img = std::make_shared<BattleImage>();
	if (DecodeBattleBmp(data.data(), data.size(), *img)) return img;
	LogW("ui", "adventure screen: the picture of the pack %s could not be read", pack.folder.c_str());
	return nullptr;
}

// Whether the campaigns of Imperivm I and II are in a content pack, and whether the game mounted it: a pack left out of the profile
// in use keeps its adventures out of the game's list.
void ReadPacks(View &v)
{
	const uint64_t serial = ModsListSerial();
	if (!g_saves.packsRead || g_saves.packsSerial != serial) {
		g_saves.packsRead = true;
		g_saves.packsSerial = serial;
		const std::vector<std::string> mounted = ModsMountedProfile().mods;
		std::vector<BattlePackFiles> packs;
		for (const ModInfo &m : ModsList()) {
			BattlePackFiles pack;
			pack.folder = m.folder;
			pack.active = std::find(mounted.begin(), mounted.end(), m.folder) != mounted.end();
			for (const std::string &path : m.paths) {
				if (path.size() > 5 && _stricmp(path.c_str() + path.size() - 5, ".bfhp") == 0) pack.files.push_back(path);
			}
			if (!pack.files.empty()) packs.push_back(std::move(pack));
		}
		for (int tab : {kBattlesTabImperivm1, kBattlesTabImperivm2}) {
			const BattleCampaignPack found = BattleCampaignPackOf(packs, tab == kBattlesTabImperivm1 ? kBattleGameImperivm1 : kBattleGameImperivm2);
			if (found.folder != g_saves.packs[tab].folder || found.adventure != g_saves.packs[tab].adventure) g_saves.packCovers[tab] = PackPicture(found);
			g_saves.packs[tab] = found;
			if (found.installed && !found.active) LogI("ui", "adventure screen: the pack %s has the campaign of tab %d but is not active", found.folder.c_str(), tab);
		}
	}
	std::copy(std::begin(g_saves.packs), std::end(g_saves.packs), std::begin(v.packs));
	std::copy(std::begin(g_saves.packCovers), std::end(g_saves.packCovers), std::begin(v.packCovers));
}

// Which adventure of the screen each game belongs to (by the title the game puts at the end of its name), the newest one of each, and
// the newest of all. Conquests have their own games, which the overview does not show.
void ReadSaves(const BattlesSnapshot &s, View &v)
{
	v.hasLatest = false;
	v.latest = SaveInfo();
	v.lastByRow.clear();
	for (int &n : v.savedInTab) n = 0;
	if (!s.open || s.tab == kBattlesTabConquests) return;
	if (g_saves.rescan) {
		g_saves.rescan = false;
		g_saves.recs.clear();
		g_saves.diaries.clear();
		for (SaveRec &r : SaveLibScan()) {
			if (r.kind == kSaveAdventure && !r.online) g_saves.recs.push_back(std::move(r));
		}
	}
	std::vector<std::string> titles;
	std::vector<std::pair<int, int>> where;
	for (int list = 0; list < kBattlesListCount; list++) {
		for (size_t i = 0; i < s.lists[list].size(); i++) {
			titles.push_back(s.lists[list][i].title);
			where.push_back({list, static_cast<int>(i)});
		}
	}
	std::map<int, uint64_t> newest;
	const SaveRec *latest = nullptr;
	for (const SaveRec &r : g_saves.recs) {
		std::string name;
		const int at = BattleSaveAdventure(r.baseName, titles, &name);
		if (at < 0) continue;
		const int list = where[at].first, index = where[at].second, key = RowArg(list, index);
		const auto seen = newest.find(key);
		if (seen != newest.end() && seen->second >= r.time) continue;
		newest[key] = r.time;
		SaveInfo info;
		info.vfsPath = r.vfsPath;
		info.name = name;
		info.title = s.lists[list][index].title;
		info.when = TimeText(r.time);
		info.played = PlayedText(r.playSeconds);
		info.list = list;
		info.index = index;
		info.tab = TabOf(list, s.lists[list][index].path);
		info.diary = DiaryOf(r);
		v.lastByRow[key] = info;
		if (!latest || r.time > latest->time) {
			latest = &r;
			v.latest = info;
			v.hasLatest = true;
		}
	}
	for (const auto &kv : v.lastByRow) {
		if (kv.second.tab >= 0 && kv.second.tab < kBattlesTabCount) v.savedInTab[kv.second.tab]++;
	}
	if (!latest) return;
	if (g_saves.thumbOf != latest->vfsPath) {
		g_saves.thumbOf = latest->vfsPath;
		g_saves.thumb = ThumbOf(*latest);
	}
	v.latest.picture = g_saves.thumb ? g_saves.thumb : BattlesAdventureImage(s.lists[v.latest.list][v.latest.index].path);
}

void Refresh()
{
	BattlesSnapshot snap = BattlesRead();
	View saves;
	ReadSaves(snap, saves);
	if (snap.open) ReadPacks(saves);
	std::lock_guard<std::mutex> lock(g_lock);
	View &v = g_s.view;
	if (!g_s.active) LogD("ui", "the new adventure screen opens");
	if (snap.tab != v.snap.tab) v.listScroll = 0;
	if (snap.tab != v.snap.tab || snap.selList != v.snap.selList || snap.selIndex != v.snap.selIndex) {
		v.descScroll = 0;
		v.diary = false;
	}
	v.snap = std::move(snap);
	v.hasLatest = saves.hasLatest;
	v.latest = std::move(saves.latest);
	v.lastByRow = std::move(saves.lastByRow);
	std::copy(std::begin(saves.savedInTab), std::end(saves.savedInTab), std::begin(v.savedInTab));
	if (snap.open) {
		std::copy(std::begin(saves.packs), std::end(saves.packs), std::begin(v.packs));
		std::copy(std::begin(saves.packCovers), std::end(saves.packCovers), std::begin(v.packCovers));
	}
	if (v.loading && GetTickCount() - g_s.loadingAt > kLoadingMs) v.loading = false;
	g_s.active = true;
	g_s.lastRead = GetTickCount();
}

struct Command {
	int action = kActNone;
	int arg = 0;
	bool start = false;
};

// The newest game of all (0) or of the chosen adventure (1); empty when there is none.
std::string ContinuePath(int which)
{
	std::lock_guard<std::mutex> lock(g_lock);
	const View &v = g_s.view;
	if (v.loading || v.snap.starting) return "";
	if (which == 0) return v.hasLatest ? v.latest.vfsPath : "";
	const auto it = v.lastByRow.find(RowArg(v.snap.selList, v.snap.selIndex));
	return v.snap.selList >= 0 && it != v.lastByRow.end() ? it->second.vfsPath : "";
}

void Execute(const Command &cmd)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.view.loading) return;
	}
	switch (cmd.action) {
	case kActOverview: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.overview = true;
		return;
	}
	case kActOpenTab: {
		BattlesSetTab(cmd.arg);
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.overview = false;
		g_s.view.listScroll = g_s.view.descScroll = 0;
		break;
	}
	case kActContinue: {
		const std::string path = ContinuePath(cmd.arg);
		if (path.empty()) return;
		LogI("ui", "adventure screen: continuing \"%s\"", path.c_str());
		SavesUiLoadSave(path);
		// The load screen closes once the game is loading; it stays open when its checks stopped the load.
		if (!SavesUiIsOpen()) {
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.view.loading = true;
			g_s.loadingAt = GetTickCount();
		}
		return;
	}
	case kActAllSaves: SavesUiOpen(); return;
	case kActContent: ModUiOpen(); return;
	case kActPage: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.diary = cmd.arg == 1;
		g_s.view.descScroll = 0;
		return;
	}
	case kActTab: BattlesSetTab(cmd.arg); break;
	case kActRow: {
		const int list = cmd.arg >> 16, index = cmd.arg & 0xffff;
		bool same = false;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			same = g_s.view.snap.selList == list && g_s.view.snap.selIndex == index;
		}
		if (!same) BattlesSelect(list, index);
		if (cmd.start) BattlesStart();
		break;
	}
	case kActDifficulty: BattlesSetDifficulty(cmd.arg); break;
	case kActStart: BattlesStart(); break;
	case kActIntro: BattlesPlayIntro(); break;
	case kActBack: BattlesBack(); break;
	default: return;
	}
	Refresh();
}

// Up and down walk the rows of the tab as they are drawn, through both groups of the great battles.
void MoveSelection(View &v, int step, Command &cmd)
{
	const BattlesSnapshot &s = v.snap;
	std::vector<std::pair<int, int>> order;
	for (int list : s.tab == kBattlesTabGreat ? std::vector<int>{kBattlesVictories, kBattlesDefeats} : std::vector<int>{kBattlesOthers}) {
		for (size_t i = 0; i < s.lists[list].size(); i++) {
			if (BattlesInTab(s.tab, list, s.lists[list][i].path)) order.push_back({list, static_cast<int>(i)});
		}
	}
	if (order.empty()) return;
	int at = -1;
	for (size_t i = 0; i < order.size(); i++) {
		if (order[i].first == s.selList && order[i].second == s.selIndex) at = static_cast<int>(i);
	}
	const int next = at < 0 ? 0 : std::max(0, std::min(static_cast<int>(order.size()) - 1, at + step));
	if (next == at) return;
	cmd.action = kActRow;
	cmd.arg = RowArg(order[next].first, order[next].second);
	const int top = RowTop(s, order[next].first, order[next].second);
	const int visible = g_s.geom.list.visible;
	if (top >= 0 && visible > 0) {
		if (top - 30 < v.listScroll) v.listScroll = std::max(0, top - (next == 0 || order[next].second == 0 ? 44 : 8));
		else if (top + kListRowH > v.listScroll + visible) v.listScroll = top + kListRowH - visible + 8;
	}
}

// Everything that touches the game runs without the lock: its screen can call back into the mod.
bool Handle(UINT msg, WPARAM wp, LPARAM lp, Command &cmd)
{
	std::lock_guard<std::mutex> lock(g_lock);
	View &v = g_s.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (ListMove(g_s.listDrag, g_s.geom.list, at)) v.listScroll = g_s.listDrag.scroll;
		if (ListMove(g_s.descDrag, g_s.geom.desc, at)) v.descScroll = g_s.descDrag.scroll;
		// The game moves its cursor with these.
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseDown = true;
		g_s.listDrag.scroll = v.listScroll;
		g_s.descDrag.scroll = v.descScroll;
		if (ListDown(g_s.listDrag, g_s.geom.list, at)) {
			v.listScroll = g_s.listDrag.scroll;
			return true;
		}
		if (ListDown(g_s.descDrag, g_s.geom.desc, at)) {
			v.descScroll = g_s.descDrag.scroll;
			return true;
		}
		int arg = 0;
		cmd.action = HitActionAt(g_s.hits, at, arg, kActNone);
		cmd.arg = arg;
		cmd.start = msg == WM_LBUTTONDBLCLK && cmd.action == kActRow;
		return true;
	}
	case WM_LBUTTONUP:
		g_s.mouseDown = false;
		ListUp(g_s.listDrag);
		ListUp(g_s.descDrag);
		return true;
	case WM_MOUSEWHEEL: {
		// The wheel's point is on the screen: the last position of the mouse in the frame is the one that counts.
		const POINT mouse = {v.mouseX, v.mouseY};
		const int delta = GET_WHEEL_DELTA_WPARAM(wp);
		ListState st;
		st.scroll = v.descScroll;
		if (ListWheel(st, g_s.geom.desc, mouse, delta, kWheelLines)) v.descScroll = st.scroll;
		st.scroll = v.listScroll;
		if (ListWheel(st, g_s.geom.list, mouse, delta, kListRowH)) v.listScroll = st.scroll;
		return true;
	}
	case WM_KEYDOWN:
		// On the overview Escape leaves and Enter continues the newest game; in a campaign Escape goes back to the overview.
		if (ShowsOverview(v)) {
			if (wp == VK_ESCAPE) cmd.action = kActBack;
			else if (wp == VK_RETURN && v.hasLatest) cmd.action = kActContinue;
			return true;
		}
		if (wp == VK_ESCAPE) cmd.action = v.snap.tab == kBattlesTabConquests ? kActBack : kActOverview;
		else if (wp == VK_RETURN) cmd.action = kActStart;
		else if (wp == VK_UP || wp == VK_DOWN) MoveSelection(v, wp == VK_UP ? -1 : 1, cmd);
		return true;
	case WM_KEYUP:
	case WM_CHAR:
		return true;
	default:
		return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	}
}

}

void BattlesUiGameTick()
{
	static bool told = false;
	if (!told) {
		told = true;
		BattlesOnDescription([] {
			if (BattlesScreenWanted() && !BattlesIntroPlaying()) Refresh();
		});
	}
	// The screen comes back as it was after the intro.
	if (BattlesIntroPlaying()) return;
	if (!BattlesScreenWanted()) {
		g_saves = SavesCache();
		g_savesWasOpen = false;
		g_modsWasOpen = false;
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.active) g_s = State();
		return;
	}
	// A game deleted or renamed on the load screen, opened from here, must not stay on the overview.
	const bool savesOpen = SavesUiNeedsDraw();
	if (g_savesWasOpen && !savesOpen) g_saves.rescan = true;
	g_savesWasOpen = savesOpen;
	// A pack may have been turned on or off on the content screen, opened from here.
	const bool modsOpen = ModUiIsOpen();
	if (g_modsWasOpen && !modsOpen) g_saves.packsRead = false;
	g_modsWasOpen = modsOpen;
	bool due = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		due = !g_s.active || GetTickCount() - g_s.lastRead >= kReadEveryMs;
	}
	if (due) Refresh();
}

bool BattlesUiNeedsDraw()
{
	if (!BattlesScreenWanted()) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.active;
}

void BattlesUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.active) return;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	const float s = ScreenScale(w, h);
	const int screenW = ScreenWidth(w, h);
	// The canvas can fall a pixel short of the frame, or past it on a narrow one: the page colour fills what it leaves.
	std::fill(buf, buf + static_cast<size_t>(w) * h, 0xff000000u | PixelOf(pal::bg));
	PanelRender r(g_canvas, buf, w, h);
	if (!BeginFullScreen(r, ScreenView{s, screenW, kScreenH})) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOut out;
	PaintBattlesScreen(r.p, screenW, ScreenViewFor(w, h), view, out);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	g_s.geom = out;
}

bool BattlesUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	// The load screen, opened from here, is drawn over this one and comes after it in the window's handlers: it gets the input.
	if (!BattlesScreenWanted() || BattlesIntroPlaying() || ModUiIsOpen() || SavesUiNeedsDraw()) return false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.active) return false;
	}
	if (!BattlesDialogOpen()) return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	Command cmd;
	const bool consumed = Handle(msg, wp, lp, cmd);
	if (cmd.action != kActNone) Execute(cmd);
	return consumed;
}
