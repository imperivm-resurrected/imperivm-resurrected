// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "MinHook.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "core/log.h"
#include "game/greatbattles.h"
#include "hooks/gamefiles.h"
#include "hooks/hookcount.h"
#include "hooks/media.h"

namespace {

const unsigned kMsgClick = 0x12345604, kMsgSelected = 0xe;
// Posted to the screen so that the cutscene plays from its own message, where the game runs its other modal loops.
const unsigned kMsgPlayIntro = 0x49524901;
const int kIdStart = 0x1005, kIdStartConquest = 0x1009, kIdBack = 0x1006, kIdDifficulty = 0x1003, kIdDescription = 0x1012;
const int kListIds[kBattlesListCount] = {0x1001, 0x1002, 0x1083};
const unsigned kFlagVisible = 0x2;
const unsigned kMenuSize = 0x70;
const unsigned kReadMode = 1;
const DWORD kSwitchTimeoutMs = 2000;

typedef int(__fastcall *OnShow_t)(void *dlg);
typedef unsigned(__fastcall *OnMessage_t)(void *dlg, void *edx, int *msg);
typedef void(__fastcall *Dtor_t)(void *dlg);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef int(__fastcall *ComboGetSelected_t)(void *combo);
typedef void(__fastcall *ComboSetSelected_t)(void *combo, void *edx, int index, int notify);
typedef void(__fastcall *SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int delay);
typedef void(__fastcall *SelectItem_t)(void *list, void *edx, unsigned index, int select, int notify, int redraw);
typedef void(__fastcall *SelectExclusive_t)(void *list, void *edx, unsigned index, int notify, int redraw);
typedef int(__fastcall *HasFlags_t)(void *window, void *edx, unsigned flags);
typedef unsigned(__fastcall *GetText_t)(void *text, void *edx, char *buf, unsigned size);
typedef void(__fastcall *UpdateModeControls_t)(void *dlg, void *edx, int greatBattles);
typedef void *(__fastcall *MenuCtor_t)(void *self, void *edx, int conquests, int mostDerived);
typedef void(__fastcall *SwitchMenu_t)(void *frame, void *edx, void *menu);
typedef void(__fastcall *PostMessage_t)(void *self, void *edx, void *target, unsigned msg, int a, int b, int delay);
typedef void *(__cdecl *OperatorNew_t)(unsigned size);
typedef int(__cdecl *PlayMovieFile_t)(const char *path);
typedef void *(__fastcall *GetFocus_t)(void *desktop);
typedef void(__fastcall *SetFocus_t)(void *desktop, void *edx, void *window);
typedef int(__fastcall *GetId_t)(void *ctrl);

OnShow_t g_origOnShow = nullptr;
OnMessage_t g_origOnMessage = nullptr;
Dtor_t g_origDtor = nullptr;

std::atomic<void *> g_dlg{nullptr};
std::atomic<DWORD> g_switchAt{0};
std::atomic<int> g_switchTab{kBattlesTabGreat};
std::atomic<bool> g_playing{false};
bool g_pendingCustom = false;
void *g_startedOn = nullptr;
std::string g_introFile;
// The game writes the description when a list tells it about a new choice: until then it still shows the last one.
int g_descList = -1, g_descIndex = -1;
std::map<std::string, std::shared_ptr<const BattleImage>> g_images;
std::map<std::string, std::string> g_intros;

bool Switching()
{
	const DWORD at = g_switchAt;
	return at && GetTickCount() - at < kSwitchTimeoutMs;
}

void *Child(void *dlg, int id, uintptr_t className)
{
	if (!dlg) return nullptr;
	void **vt = *reinterpret_cast<void ***>(dlg);
	return reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(className));
}

void *List(void *dlg, int list) { return list >= 0 && list < kBattlesListCount ? Child(dlg, kListIds[list], addr::kClassNameCUISList) : nullptr; }

unsigned Count(void *list) { return list ? *reinterpret_cast<unsigned *>(static_cast<char *>(list) + 0x58) : 0; }

// The item the game starts: CUIItemList keeps it at +0x6c, out of range when there is none.
int Current(void *list)
{
	if (!list) return -1;
	const unsigned at = *reinterpret_cast<unsigned *>(static_cast<char *>(list) + 0x6c);
	return at < Count(list) ? static_cast<int>(at) : -1;
}

std::vector<BattlesEntry> Entries(void *list)
{
	std::vector<BattlesEntry> out;
	const char *items = list ? *reinterpret_cast<char *const *>(static_cast<char *>(list) + 0x94) : nullptr;
	for (unsigned i = 0, n = Count(list); items && i < n && i < 512; i++) {
		// An AdventureListItem of 0x14 bytes: the title, two flags, the path without its extension and the picture.
		const char *title = *reinterpret_cast<const char *const *>(items + i * 0x14);
		const char *path = *reinterpret_cast<const char *const *>(items + i * 0x14 + 0xc);
		out.push_back({title ? title : "", path ? path : ""});
	}
	return out;
}

int DialogTab(void *dlg)
{
	const char *d = static_cast<const char *>(dlg);
	if (*reinterpret_cast<const int *>(d + 0x64)) return kBattlesTabConquests;
	return *reinterpret_cast<const int *>(d + 0x60) ? kBattlesTabGreat : kBattlesTabCustom;
}

void Click(void *dlg, int id)
{
	char *b = static_cast<char *>(Child(dlg, id, addr::kClassNameCUIButton));
	if (!b) return;
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(b))[0x6c / 4])(b, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgClick,
		*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
}

void Deselect(void *list)
{
	if (list && Current(list) >= 0) reinterpret_cast<SelectItem_t>((*reinterpret_cast<void ***>(list))[0x88 / 4])(list, nullptr, 0xffffffffu, 0, 0, 0);
}

// The list tells the screen, which then shows the description and forgets the other list's choice, as after a click.
void Select(void *dlg, int list, int index)
{
	void *l = List(dlg, list);
	if (!l || index < 0 || static_cast<unsigned>(index) >= Count(l)) return;
	if (list == kBattlesVictories) Deselect(List(dlg, kBattlesDefeats));
	if (list == kBattlesDefeats) Deselect(List(dlg, kBattlesVictories));
	reinterpret_cast<SelectExclusive_t>(addr::kListSelectExclusive)(l, nullptr, static_cast<unsigned>(index), 0, 0);
	reinterpret_cast<SelectItem_t>((*reinterpret_cast<void ***>(l))[0x88 / 4])(l, nullptr, static_cast<unsigned>(index), 1, 1, 1);
}

void SelectionOf(void *dlg, int tab, int &list, int &index)
{
	list = index = -1;
	const int candidates[2] = {tab == kBattlesTabGreat ? kBattlesVictories : kBattlesOthers, tab == kBattlesTabGreat ? kBattlesDefeats : -1};
	for (int c : candidates) {
		const int at = c >= 0 ? Current(List(dlg, c)) : -1;
		if (at >= 0) {
			list = c;
			index = at;
			return;
		}
	}
}

// Shows the current choice again, or the first adventure when there is none, so that the description matches the tab.
void Reselect(void *dlg)
{
	const int tab = DialogTab(dlg);
	int list = -1, index = -1;
	SelectionOf(dlg, tab, list, index);
	if (list < 0) {
		const int candidates[2] = {tab == kBattlesTabGreat ? kBattlesVictories : kBattlesOthers, tab == kBattlesTabGreat ? kBattlesDefeats : -1};
		for (int c : candidates) {
			if (c >= 0 && Count(List(dlg, c))) {
				list = c;
				index = 0;
				break;
			}
		}
	}
	if (list >= 0) Select(dlg, list, index);
}

std::string GameDir()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char *slash = strrchr(path, '\\')) slash[1] = 0;
	return path;
}

std::string IntroFile(const std::string &adventurePath)
{
	const auto it = g_intros.find(adventurePath);
	if (it != g_intros.end()) return it->second;
	std::string found;
	const std::string pattern = BattleIntroPattern(adventurePath);
	if (!pattern.empty()) {
		WIN32_FIND_DATAA fd = {};
		HANDLE h = FindFirstFileA((GameDir() + "Movies\\" + pattern).c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) {
			found = fd.cFileName;
			FindClose(h);
		}
	}
	g_intros[adventurePath] = found;
	return found;
}

std::shared_ptr<const BattleImage> Image(const std::string &file)
{
	const auto it = g_images.find(file);
	if (it != g_images.end()) return it->second;
	std::shared_ptr<BattleImage> img;
	std::vector<char> bytes;
	if (ReadGameFile(file.c_str(), kReadMode, bytes) && !bytes.empty()) {
		img = std::make_shared<BattleImage>();
		if (!DecodeBattleBmp(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), *img)) {
			LogW("ui", "the picture %s could not be read", file.c_str());
			img.reset();
		}
	}
	g_images[file] = img;
	return img;
}

std::vector<std::string> ComboItems(void *combo)
{
	std::vector<std::string> out;
	char *list = combo ? *reinterpret_cast<char **>(static_cast<char *>(combo) + 0x64) : nullptr;
	if (!list) return out;
	const unsigned count = *reinterpret_cast<unsigned *>(list + 0x58);
	const char *items = *reinterpret_cast<const char **>(list + 0x94);
	for (unsigned i = 0; items && i < count && i < 16; i++) {
		const char *text = *reinterpret_cast<const char *const *>(items + i * 0x14);
		out.push_back(text ? text : "");
	}
	return out;
}

void SwitchTo(bool conquests, int tab)
{
	void *frame = *reinterpret_cast<void **>(addr::kMenuFrame);
	void *menu = frame ? reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(kMenuSize) : nullptr;
	if (!menu) return;
	reinterpret_cast<MenuCtor_t>(addr::kPreAdventureCtor)(menu, nullptr, conquests ? 1 : 0, 1);
	g_pendingCustom = tab == kBattlesTabCustom;
	g_switchTab = tab;
	g_switchAt = GetTickCount();
	reinterpret_cast<SwitchMenu_t>(addr::kMenuSwitchMenu)(frame, nullptr, menu);
	LogI("ui", "adventure screen: switching to the %s", conquests ? "conquests" : "adventures");
}

void PlayIntro(void *dlg)
{
	const std::string path = "movies\\" + g_introFile;
	void *root = *reinterpret_cast<void **>(addr::kUIRoot);
	void **rootVt = root ? *reinterpret_cast<void ***>(root) : nullptr;
	void *focus = rootVt ? reinterpret_cast<GetFocus_t>(rootVt[0x74 / 4])(root) : nullptr;
	LogI("ui", "playing the intro %s", path.c_str());
	g_playing = true;
	MediaPauseMusic(true);
	const int ok = reinterpret_cast<PlayMovieFile_t>(addr::kPlayMovieFile)(path.c_str());
	MediaPauseMusic(false);
	g_playing = false;
	// As the game does after a cutscene in a battle: the movie window had the focus and is gone.
	if (rootVt) reinterpret_cast<SetFocus_t>(rootVt[0x78 / 4])(root, nullptr, focus ? focus : dlg);
	if (!ok) LogW("ui", "the intro %s could not be played", path.c_str());
}

int __fastcall HookOnShow(void *dlg)
{
	const int r = g_origOnShow(dlg);
	if (r) return r;
	g_dlg = dlg;
	g_switchAt = 0;
	g_descList = g_descIndex = -1;
	g_startedOn = nullptr;
	if (g_pendingCustom && DialogTab(dlg) == kBattlesTabGreat) reinterpret_cast<UpdateModeControls_t>(addr::kPreAdventureUpdateModeControls)(dlg, nullptr, 0);
	g_pendingCustom = false;
	Reselect(dlg);
	LogI("ui", "adventure screen open (tab %d)", DialogTab(dlg));
	return r;
}

unsigned __fastcall HookOnMessage(void *dlg, void *edx, int *msg)
{
	if (static_cast<unsigned>(msg[0x10 / 4]) == kMsgPlayIntro && reinterpret_cast<void *>(msg[0xc / 4]) == dlg) {
		if (!g_playing && !g_introFile.empty()) PlayIntro(dlg);
		return 0;
	}
	const unsigned r = g_origOnMessage(dlg, edx, msg);
	void *sender = reinterpret_cast<void *>(msg[0xc / 4]);
	if (static_cast<unsigned>(msg[0x10 / 4]) == kMsgSelected && sender) {
		const int id = reinterpret_cast<GetId_t>((*reinterpret_cast<void ***>(sender))[0x1c / 4])(sender);
		// The game also tells it when it empties the other list's choice, and then leaves the text as it was.
		for (int i = 0; i < kBattlesListCount; i++) {
			if (id != kListIds[i] || Current(sender) < 0) continue;
			g_descList = i;
			g_descIndex = Current(sender);
		}
	}
	return r;
}

void __fastcall HookDtor(void *dlg)
{
	void *expected = dlg;
	g_dlg.compare_exchange_strong(expected, nullptr);
	if (g_startedOn == dlg) g_startedOn = nullptr;
	g_origDtor(dlg);
}

bool HookAt(uintptr_t at, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(at), detour, orig) == MH_OK && HookEnable(reinterpret_cast<void *>(at)) == MH_OK) return true;
	LogE("ui", "could not hook %s", what);
	return false;
}

}

void InstallGreatBattlesHooks(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookAt(addr::kPreAdventureOnShow, reinterpret_cast<void *>(&HookOnShow), reinterpret_cast<void **>(&g_origOnShow), "CVXUIPreAdventureMenu::OnShow") &&
		HookAt(addr::kPreAdventureOnMessage, reinterpret_cast<void *>(&HookOnMessage), reinterpret_cast<void **>(&g_origOnMessage), "CVXUIPreAdventureMenu::OnMessage") &&
		HookAt(addr::kPreAdventureDtor, reinterpret_cast<void *>(&HookDtor), reinterpret_cast<void **>(&g_origDtor), "CVXUIPreAdventureMenu destructor");
	LogAt(ok ? kLogInfo : kLogError, "ui", "adventure screen %s", ok ? "connected to the mod's" : "not connected");
}

void *BattlesDialogOpen()
{
	return g_playing ? nullptr : g_dlg.load();
}

bool BattlesScreenWanted()
{
	if (g_playing) return false;
	return g_dlg || Switching();
}

bool BattlesIntroPlaying()
{
	return g_playing;
}

BattlesSnapshot BattlesRead()
{
	BattlesSnapshot s;
	void *dlg = g_dlg;
	if (!dlg || g_playing) {
		s.switching = BattlesScreenWanted();
		s.tab = g_switchTab;
		return s;
	}
	s.open = true;
	s.starting = g_startedOn == dlg;
	s.tab = DialogTab(dlg);
	for (int i = 0; i < kBattlesListCount; i++) s.lists[i] = Entries(List(dlg, i));
	SelectionOf(dlg, s.tab, s.selList, s.selIndex);
	void *combo = Child(dlg, kIdDifficulty, addr::kClassNameCUIComboBox);
	if (combo) {
		s.difficulties = ComboItems(combo);
		s.difficulty = reinterpret_cast<ComboGetSelected_t>(addr::kComboGetSelected)(combo);
		s.difficultyShown = reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(combo, nullptr, kFlagVisible) != 0 && !s.difficulties.empty();
	}
	const std::string fallback = s.tab == kBattlesTabConquests ? "CurrentLang/ConquestDefault.bmp" : "CurrentLang/AdventureDefault.BMP";
	if (s.selList >= 0) {
		const BattlesEntry &e = s.lists[s.selList][s.selIndex];
		void *text = Child(dlg, kIdDescription, addr::kClassNameCUITextW);
		static char buf[16384];
		buf[0] = 0;
		const bool current = g_descList == s.selList && g_descIndex == s.selIndex;
		if (current && text && reinterpret_cast<GetText_t>(addr::kGetTextUtf8)(text, nullptr, buf, sizeof(buf) - 1) == 0) s.description = BattleDescriptionText(buf);
		s.intro = s.tab == kBattlesTabGreat && !IntroFile(e.path).empty();
		s.image = Image(e.path + ".bmp");
	}
	if (!s.image) s.image = Image(fallback);
	return s;
}

void BattlesSelect(int list, int index)
{
	void *dlg = BattlesDialogOpen();
	if (dlg && g_startedOn != dlg) Select(dlg, list, index);
}

void BattlesSetDifficulty(int index)
{
	void *dlg = BattlesDialogOpen();
	void *combo = dlg && g_startedOn != dlg ? Child(dlg, kIdDifficulty, addr::kClassNameCUIComboBox) : nullptr;
	if (combo) reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(combo, nullptr, index, 1);
}

void BattlesSetTab(int tab)
{
	void *dlg = BattlesDialogOpen();
	if (!dlg || g_startedOn == dlg || Switching()) return;
	const int now = DialogTab(dlg);
	if (tab == now) return;
	if (tab == kBattlesTabConquests || now == kBattlesTabConquests) {
		SwitchTo(tab == kBattlesTabConquests, tab);
		return;
	}
	// What the screen's own Custom and Great Battles buttons do, without waiting for their message.
	reinterpret_cast<UpdateModeControls_t>(addr::kPreAdventureUpdateModeControls)(dlg, nullptr, tab == kBattlesTabGreat ? 1 : 0);
	Reselect(dlg);
}

void BattlesStart()
{
	void *dlg = BattlesDialogOpen();
	if (!dlg || g_startedOn == dlg || Switching()) return;
	int list = -1, index = -1;
	SelectionOf(dlg, DialogTab(dlg), list, index);
	if (list < 0) return;
	g_startedOn = dlg;
	LogI("ui", "starting the adventure %s", Entries(List(dlg, list))[index].path.c_str());
	Click(dlg, Child(dlg, kIdStart, addr::kClassNameCUIButton) ? kIdStart : kIdStartConquest);
}

void BattlesBack()
{
	void *dlg = BattlesDialogOpen();
	if (dlg && g_startedOn != dlg) Click(dlg, kIdBack);
}

void BattlesPlayIntro()
{
	void *dlg = BattlesDialogOpen();
	if (!dlg || g_startedOn == dlg || DialogTab(dlg) != kBattlesTabGreat) return;
	int list = -1, index = -1;
	SelectionOf(dlg, kBattlesTabGreat, list, index);
	if (list < 0) return;
	g_introFile = IntroFile(Entries(List(dlg, list))[index].path);
	if (g_introFile.empty()) return;
	reinterpret_cast<PostMessage_t>(addr::kWindowPostMessage)(dlg, nullptr, dlg, kMsgPlayIntro, 0, 0, 0);
}
