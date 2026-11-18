// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <cstdint>
#include "core/transferrate.h"
#include "mods/downloads.h"
#include "ui/fullscreen.h"
#include "ui/widgets.h"
#include "ui/keycaps.h"
#include "ui/modui.h"
#include "ui/richtext.h"
#include "mods/modorder.h"
#include "mods/mods.h"

namespace modui_impl {
using namespace ui;

// The size the toasts, cards and restart questions are scaled for; the content screen itself fills the whole screen (PanelLayout).
const int kDesignW = 1000;

const int kDesignH = 640;

const int kRowH = 40;

// The rows of the installed list and of the load order: a name over a line of detail.
const int kModRowH = 54;

// The band that names a group of the load order (cosmetic, maps and campaigns, gameplay, disabled) above its first row.
const int kGroupH = 30;

// The height of the fields and buttons in the bars of the content screen, and of its row of actions at the bottom.
const int kBarH = 40;

const int kFootH = 44;

// The tabs of the content screen.
enum Tab { kTabListInstalled = 0, kTabListOrder = 1, kTabListExplore = 2 };

// The tabs of the card beside the installed list.
enum CardTab { kCardDetails = 0, kCardConflicts = 1, kCardFiles = 2 };

const char kOriginalName[] = "Original";

const char kNoProfileName[] = "No profile";

enum Action {
	kNone, kDelProfile, kToggle,
	kOpenMod, kApply, kClose, kConfirmYes, kConfirmNo, kConfirmAlt,
	kTabInstalled, kPickArchive, kUpdateMod,
	kDropOpen, kDropClose, kDropPick, kSaveAs, kFocusName, kNameOk, kNameCancel, kFocusSearch,
	kTabExplore, kIoFocusSearch, kIoPrev, kIoNext, kIoOpen, kIoBack, kIoInstall, kIoPage, kIoRetry, kIoRefresh, kOpenSource,
	kIoShotOpen, kIoShotClose, kIoShotStep,
	kRichLink, kShowProblems, kUpdateAll,
	kIoFilterOpen, kIoFilterClose, kIoFilterToggle, kIoFilterClear,
	kUseFiles,
	kRowClick, kMark, kStateFilter, kCatChip, kCatPanel, kCatClear, kBulkOn, kBulkOff, kBulkUninstall, kClearMarks, kSetActive, kForget, kReinstall,
	kTabOrder, kCardTab, kUseFile, kDisableOther, kClearChoices, kCopyCode, kPasteCode, kFocusImport, kImportCode, kImportClear,
};

// kUseFile packs which shared pair, which file of the card and whose copy into one number.
inline int UseFileArg(int overlap, int file, bool mine) { return (overlap * 4096 + file) * 2 + (mine ? 1 : 0); }

enum Field { kNoField, kSearchField, kNameField, kIoSearchField, kImportField };

enum Drop { kDropNone, kDropProfile, kDropMenu, kDropSort, kDropIoSort, kDropVersions, kDropClashFiles, kDropModMenu, kDropRowMenu };

const int kIoFilterTypes = 0;
const int kIoFilterPlayers = 1;
const int kIoFilterLanguages = 2;
const int kIoFilterGroupStride = 64;

const char *const kIoSortNames[] = {"Popular", "Most liked", "Recent", "Recently updated"};

const int kIoPerPage = 8;

std::string IoTagsText(const GbMod &m);
enum NameMode { kNameNone, kNameSaveAs, kNameRename, kNameNew };

enum Confirm { kConfirmRestart, kConfirmUninstall, kConfirmDelete, kConfirmNotice, kConfirmAction };

// Priority is the load order: the active packs as they load, then the rest by name.
const char *const kSortNames[] = {"Priority", "Name", "Active first", "Recently installed", "Size"};
const int kSortCount = 5;
enum Sort { kSortPriority = 0, kSortName, kSortActiveFirst, kSortRecent, kSortSize };
enum StateFilter { kStateAll, kStateActive, kStateDisabled, kStateConflicts, kStateProblems, kStateCount };

// What a pack shares with the others of the profile: the files it wins and loses, the pairs and values it shares, and whether
// sharing them is a problem (gameplay files that the order cannot settle).
struct ClashSummary {
	int wins = 0, loses = 0, pairs = 0, values = 0;
	bool problem = false;
	std::string problemWith;
};

struct Row {
	std::string folder;
	bool active = false;
};

struct State {
	bool open = false;
	bool confirm = false;
	int confirmKind = kConfirmRestart;
	bool confirmOnly = false;
	std::vector<std::string> confirmItems;
	std::string confirmItemsTitle;
	ListState confirmList;
	ListGeom confirmGeom;
	ModProfile profile;
	std::vector<std::string> profileNames;
	std::vector<ModInfo> mods;
	std::vector<Row> rows;
	std::map<std::string, std::vector<std::string>> usedBy;
	std::vector<std::string> pendingUninstall;
	std::shared_ptr<const std::map<std::string, ModIssues>> issues = std::make_shared<const std::map<std::string, ModIssues>>();
	std::vector<std::string> problems;
	int selected = 0;
	ListState list;
	ListGeom listGeom;
	int dropScroll = 0;
	ScrollDrag dropBar;
	bool mouseDown = false;
	bool busy = false;
	int mouseX = -1, mouseY = -1;
	std::vector<Hit> hits;
	std::string toast;
	DWORD toastUntil = 0;
	DWORD toastLen = 6000;
	bool toastErr = false;
	DWORD lastRefresh = 0;
	bool restartNeeded = false;

	int drop = kDropNone;
	int nameMode = kNameNone;
	std::string nameText;
	std::string searchText;
	int stateFilter = kStateAll;
	CatalogFilter catFilter;
	bool catOpen = false;
	int sort = 0;
	std::vector<std::string> marked;
	std::vector<std::string> uninstallTargets;
	int anchor = -1;
	int menuRow = -1;
	int menuX = 0, menuY = 0;
	uint64_t listSerial = 0;

	int tab = kTabListInstalled;
	int cardTab = kCardDetails;
	// The active packs as they load, with why each one is where it is.
	std::vector<ModOrderEntry> order;
	ListState orderList;
	ListGeom orderGeom;
	// The code being imported, and the profile it made while the packs it lacks download.
	std::string importText;
	std::string importProfile;
	std::vector<uint64_t> importWant, importLoading;
	int focus = kNoField;
	DownloadStatus dl;
	bool catalogRequested = false;

	std::string ioSearch;
	DWORD ioSearchDue = 0;
	EditState edit;
	int tabAction = -1, tabArg = 0;
	int ioSort = 0, ioPage = 0;
	CatalogFilter ioFilter;
	bool ioFilterOpen = false;
	int ioDetail = -1;
	int ioShot = -1;
	bool ioRequested = false;
	// Refresh keeps the cards on screen while the same page loads again; any other search shows the shape of the cards.
	bool ioRefreshing = false;
	GbResults io;
	GbMod verMod;
	bool verHas = false;
	uint64_t dlFinished = 0;

	std::string restartReason;
	int restartLargeAddress = -1;
	std::string noticeTitle, noticeText;
	std::string actionYes;
	std::function<void()> action;
	// A second answer beside yes (ModUiChoose); Esc and the third button only close.
	std::string actionAlt;
	std::function<void()> alt;
	// A question that takes `alt` by itself at this time (GetTickCount), and on Esc; 0 for the others.
	unsigned confirmDeadline = 0;
	// The size of the frame being painted, in its own design units: the whole screen while the content screen is open.
	int viewW = kDesignW, viewH = kDesignH;
};

// Where the parts of the content screen go, in the units of ScreenView: under the header of the full screens, the list and the detail of
// the Installed tab or the grid of Browse content, down to the row of actions at the bottom.
struct PanelLayout : ScreenLayout {
	int x = 0, inner = 0;
	// The tools over the list and the card, and where the list and the card start: they reach the foot of the screen, which only
	// Browse content keeps for its pages.
	int toolsY = 0, cardTop = 0;
	int footY = 0, cardBottom = 0;
	int listW = 0, detailX = 0, detailW = 0;
};
PanelLayout PanelLayoutFor(const State &st);

extern std::mutex g_lock;
extern State g_s;
extern ListGeom g_listGeomOut;
extern ListGeom g_orderGeomOut;
extern std::atomic<int> g_installedRows;
extern std::atomic<int> g_orderRows;
struct RichPos {
	int y, h, indent;
};
// What the blocks' places were measured for: measuring every block on every frame is slow, so they are measured again only when one
// of these, or the text, changes.
struct RichMeasure {
	int width = -1, baseFont = -1;
	float scale = 0;
	bool raw = false;
	HFONT fonts[3] = {};
	bool operator==(const RichMeasure &o) const
	{
		return width == o.width && baseFont == o.baseFont && scale == o.scale && raw == o.raw && fonts[0] == o.fonts[0] && fonts[1] == o.fonts[1] &&
			fonts[2] == o.fonts[2];
	}
};
struct RichTextState {
	std::mutex mx;
	std::string src;
	std::vector<RichBlock> blocks;
	std::vector<RichPos> pos;
	int total = 0;
	RichMeasure measured;
	int view = 0;
	ListState list;
	ListGeom geom;
	RECT rect = {};
	bool rectValid = false;
	DWORD rectTick = 0;
};
extern RichTextState g_rich;
const COLORREF kLink = RGB(130, 182, 238);

const ModInfo *FindModIn(const std::vector<ModInfo> &mods, const std::string &folder);
const ModInfo *FindMod(const std::string &folder);
std::string OverlapText(const ModOverlap &ov);
const Row *SelectedRow(const State &st);
const ModIssues *SelectedIssues(const State &st);
bool Pending(const State &st, const std::string &folder);
std::vector<int> VisibleRows(const State &st);
std::vector<int> VisibleRows(const State &st, int state, const CatalogFilter &filter);
// How many rows each state filter would show under the search and `filter`, counted in one pass.
void CountStates(const State &st, const CatalogFilter &filter, int counts[kStateCount]);
CatalogFacts ModFactsOf(const ModInfo &m);
uint32_t ModCategoriesOf(const ModInfo &m);
int ModMainCategory(const ModInfo &m);
bool RowHasProblems(const State &st, const Row &r);
ClashSummary ClashSummaryOf(const State &st, const std::string &folder);
// Where the pack loads (position 1 is the first and wins), or null when it does not load.
const ModOrderEntry *OrderEntryOf(const State &st, const std::string &folder);
// Why the pack is where it is in the load order, as a sentence.
std::string OrderReasonText(const State &st, const ModOrderEntry &e);
// The name of a group of the load order, 0-2 by kind and 3 for what does not load.
const char *OrderGroupName(int group);
void ImportTick();
void PaintOrderTab(Painter &p, const State &st);
// A bolt `h` tall at (x, y), the mark of the files two packs share.
void PaintBolt(Painter &p, int x, int y, int h, COLORREF c);
// A padlock `h` tall at (x, y), for what the load order decides and cannot be chosen.
void PaintPadlock(Painter &p, int x, int y, int h, COLORREF c);
// One half of a choice of two: the one in use has the lighter fill and the red rule under it, and only the other can be clicked.
void PaintSeg(Painter &p, int x, int y, int w, int h, const std::string &label, bool on, bool enabled, int action, int arg, int mx, int my);
// What a pack shares, in the Conflicts column of the installed list and of the load order.
void PaintClashCell(Painter &p, const State &st, const Row &r, int x, int y, int w, int h);
// The band that names a group of the load order and counts its packs.
void PaintGroupBand(Painter &p, int x, int y, int w, int group, int count);
void SetTipText(const std::string &text, int x, int y);
bool IsMarked(const State &st, const std::string &folder);
std::vector<std::string> Targets(const State &st);
CatalogCounts InstalledCounts(const State &st);
void SetActive(const std::vector<std::string> &folders, bool on);
void AskUninstall(const std::vector<std::string> &folders);
std::vector<std::string> RowMenuItems(const State &st);
void RunRowMenu(int index);
void BuildRows();
void BuildUsage();
void Reload();
int FieldOfAction(int action);
int ActionOfField(int field);
std::string &FieldRef(int field);
void FocusField(int field);
void SetStatus(const std::string &s);
bool FindUpdateFor(const State &st, const ModInfo &m, CatalogEntry &out);
void SwitchProfile(const std::string name);
void CommitProfile();
void SelectFolder(const std::string &folder);
void FinishName();
void StartName(int mode);
std::vector<std::string> DropItems(const State &st);
void IoSearch();
void OpenWeb(const std::string &url);
void OpenFolderAsync(const std::string &folder);
void PickDrop(int index);
void DoAction(int action, int arg);
struct DropListState {
	int rows = 0;
	int count = 0;
	RECT anchor = {};
};
extern DropListState g_drop;
int DropCurrent(const State &st);
void PaintFacetPanel(Painter &p, const PanelLayout &l, const CatalogFilter &f, const CatalogCounts &counts, int toggleAction, int clearAction, int closeAction, int mx, int my);
void PaintFilterPanel(Painter &p, const State &st);
void PaintInstalledFilterPanel(Painter &p, const State &st);
void PaintPanel(Painter &p, const State &st);
void PaintInstalled(Painter &p, const State &st);
void PaintField(Painter &p, const State &st, int x, int y, int w, int h, const std::string &text, const std::string &placeholder,
	bool focused, bool secret, int action, int mx, int my);
void PaintFooter(Painter &p, const State &st);
const int kConfirmW = 560;

const int kConfirmH = 210;

const int kConfirmListRows = 6;

const int kConfirmRowH = 22;

extern ListGeom g_confirmGeomOut;
void ConfirmSize(const State &st, int &cw, int &ch);
void PaintConfirm(Painter &p, const State &st);
extern Canvas g_canvas;
extern Canvas g_toastCanvas;
int UpdatesAvailable();
void ResetUpdatesCache();
struct CardState {
	RECT close = {};
	bool shown = false;
	bool swallowUp = false;
	Canvas canvas;
	TransferRate rate;
};
extern CardState g_card;
struct Card {
	std::string text;
	std::string sub;
	float progress = -1;
	COLORREF accent = pal::redText;
	bool close = false;
	bool corner = false;
	int fixedW = 0;
	int atX = -1, atCenterY = 0;
	std::vector<ControlKey> keys;
};

RECT DrawCard(Canvas &canvas, uint32_t *buf, int w, int h, const Card &c, float s, int alpha);
struct HoverState {
	int x = -1;
	int y = -1;
};
extern HoverState g_hover;
struct DiscordIconState {
	Canvas canvas;
	std::vector<uint32_t> base;
	int baseW = 0;
	int baseH = 0;
	std::vector<uint32_t> scaled;
	int scaledSize = 0;
	bool loaded = false;
	RECT rect = {};
	bool shown = false;
	bool swallowUp = false;
};
extern DiscordIconState g_icon;
struct SpectatorBarState {
	RECT btn[9] = {};
	RECT bar = {};
	bool barShown = false;
	bool swallowUp = false;
	bool drag = false;
	bool move = false;
	int moveDX = 0;
	int moveDY = 0;
	int winW = 0;
	int winH = 0;
	double dragFrac = 0;
	Canvas text;
};
extern SpectatorBarState g_spec;
struct ClipBox {
	int x0 = 0, y0 = 0, x1 = 1 << 30, y1 = 1 << 30;
};

extern ClipBox g_clip;
void FillRound(uint32_t *buf, int W, int H, float x, float y, float w, float h, float r, COLORREF c, float a);
void StrokeRound(uint32_t *buf, int W, int H, float x, float y, float w, float h, float r, float thick, COLORREF c, float a);
void FillDisc(uint32_t *buf, int W, int H, float cx, float cy, float r, COLORREF c, float a);
void FillTriangle(uint32_t *buf, int W, int H, float ax, float ay, float bx, float by, float cx, float cy, COLORREF c, float a);
struct ShapePoint {
	float x, y;
};
void FillShape(uint32_t *buf, int W, int H, const std::vector<std::vector<ShapePoint>> &shape, float x, float y, float scale, COLORREF c, float a);
void DrawLabel(uint32_t *buf, int W, int H, const std::wstring &t, float fx, float fy, float fw, float fh, UINT align, COLORREF col, float a, HFONT font = nullptr);
std::wstring ClockText(unsigned ms);
struct RectF {
	float x = 0, y = 0, w = 0, h = 0;
	bool Has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

void DrawSpectatorBar(uint32_t *buf, int W, int H, float s, int topY);
void DrawWatchersBadge(uint32_t *buf, int W, int H);
struct CheatBtn {
	int verb;
	int amount;
	RECT rect;
	float offY;
	float offH;
};

const int kChMaxBtn = 512;

const int kChNoFocus = 1 << 20;

struct CheatsPanelState {
	CheatBtn btn[kChMaxBtn] = {};
	int btnCount = 0;
	RECT tab = {};
	RECT panel = {};
	RECT track = {};
	bool shown = false;
	bool swallowUp = false;
	bool drag = false;
	float scroll = 0;
	float maxScroll = 0;
	int edit = -1;
	bool searchEdit = false;
	int focus = kChNoFocus;
	EditState es;
	FieldGeom geom[8];
	bool geomOk[8] = {};
	float pos = -2;
	int move = 0;
	int moveY0 = 0;
	int grab = 0;
	int winH = 1;
	bool moved = false;
};
extern CheatsPanelState g_ch;
void ChSavePos();
bool ChIsValue(int verb);
bool ChEditing();
std::string &ChText();
int ChSlot();
bool CheatsBarPress(POINT pt);
void CheatsScrollToMouse(int mouseY);
void ChSetFocus(int verb);
void ChClearFocus();
void ChActivate(int v, int amount, bool fromKey);
bool ChHandleKey(UINT msg, WPARAM wp, LPARAM lp);
void DrawCheatsPanel(uint32_t *frame, int frameW, int H, float s);
RECT DiscordIconRect(int w, int h);
void DrawDiscordCorner(uint32_t *buf, int w, int h);
void DrawVersionCorner(uint32_t *buf, int w, int h);

}
