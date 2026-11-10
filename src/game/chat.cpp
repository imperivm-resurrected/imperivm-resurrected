// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"
#include "addr/engine.h"
#include "addr/net.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "game/chat.h"
#include "game/controls.h"
#include "game/zoomview.h"
#include "hooks/hookcount.h"

namespace {

typedef void *(__fastcall *DialogCtor_t)(void *self, void *edx, int sendLocation, int mostDerived);
typedef void(__fastcall *Execute_t)(void *cmd);
typedef void *(__cdecl *Create_t)();
typedef void(__fastcall *Assign_t)(void *str, void *edx, const char *text, unsigned len);
typedef void(__fastcall *SetPlayer_t)(void *cmd, void *edx, void *player);
typedef void(__fastcall *Queue_t)(void *game, void *edx, void *cmd);
typedef void(__fastcall *RunScript_t)(void *game, void *edx, const char *text);
typedef int(__cdecl *IsDemo_t)();
typedef bool(__cdecl *Bool_t)();
typedef void(__cdecl *Free_t)(void *p);

// CVXGame and CVXPlayer fields, as the chat dialog (004e4f00, 004e51e0) and CVXChatMessage::Execute (004e7370) read them.
const unsigned kGameClock = 0x1258, kLocalPlayer = 0x12c8, kPlayers = 0x12cc, kPlayerSize = 800;
const unsigned kPlayerIndex = 8, kPlayerEnemies = 0x10, kPlayerActive = 0x64, kPlayerControl = 0x80, kPlayerName = 0x94, kPlayerNameSize = 0xa4,
			   kPlayerNameCapacity = 0xa8, kPlayerState = 0x290, kPlayerColor = 0x314;
const int kSlots = 8;
// player+0x290: 1 while the player is in the match; a defeat or a surrender changes it (read the same way by gamestats).
const int kStatePlaying = 1;
// CVXChatMessage: +4 sender, +0xc std::string message (buffer +0x10, size +0x20, capacity +0x24), +0x28 location, +0x30 players mask.
const unsigned kCmdPlayer = 4, kCmdText = 0xc, kCmdTextBuf = 0x10, kCmdTextSize = 0x20, kCmdTextCapacity = 0x24, kCmdLocation = 0x28, kCmdMask = 0x30;
const unsigned kSlotSetPlayer = 3, kSlotRunScript = 0xa8 / 4;
// CVXVisibleMgr: the part of the map in view, in the coordinates of the location messages.
const unsigned kViewRect = 0x618;
const unsigned kWindowRect = 0x10, kWindowFlags = 0x28, kWindowVisible = 2;

DialogCtor_t g_origCtor = nullptr;
Execute_t g_origExecute = nullptr;
void *g_origConsolePrintf = nullptr;
// Read by the naked hook: while set, ConsolePrintf prints nothing.
int g_suppressConsole = 0;
bool g_ready = false;
ChatOpenFn g_openHandler = nullptr;

std::mutex g_logLock;
ChatLog g_log;

struct RawPlayer {
	bool exists = false, human = false, playable = false;
	char name[64] = {};
	uint32_t rgb = 0;
};

struct RawMatch {
	bool game = false, editor = false, demo = false, shown = false, severalHumans = false;
	uint32_t clock = 0;
	int local = -1, localState = 0;
	unsigned enemies = 0;
	RawPlayer players[kSlots];
	bool haveView = false, haveVisible = false;
	int view[4] = {}, visible[4] = {};
};

char *Game() { return *reinterpret_cast<char **>(addr::kGame); }

template <class T>
T Field(const char *base, unsigned offset)
{
	return *reinterpret_cast<const T *>(base + offset);
}

void CopyGameString(char *out, size_t size, const char *base, unsigned buf, unsigned length, unsigned capacity)
{
	const char *text = Field<unsigned>(base, capacity) < 0x10 ? base + buf : Field<const char *>(base, buf);
	const size_t n = std::min<size_t>(Field<unsigned>(base, length), size - 1);
	if (text && n) memcpy(out, text, n);
	out[text ? n : 0] = 0;
}

bool ReadClock(const char *game, uint32_t &clock)
{
	__try {
		clock = Field<uint32_t>(game, kGameClock);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

uint32_t Rgb555(unsigned c) { return ((c >> 10 & 0x1f) * 255 / 31) << 16 | ((c >> 5 & 0x1f) * 255 / 31) << 8 | ((c & 0x1f) * 255 / 31); }

bool ReadRaw(RawMatch &m)
{
	__try {
		char *game = Game();
		m.game = game != nullptr;
		if (!game) return true;
		m.editor = *reinterpret_cast<int *>(addr::kEditorMode) != 0;
		m.demo = reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0;
		m.severalHumans = reinterpret_cast<Bool_t>(addr::kMultiHumans)();
		const char *mapView = *reinterpret_cast<const char **>(addr::kMapView);
		m.shown = mapView && (Field<unsigned>(mapView, kWindowFlags) & kWindowVisible);
		if (m.shown) {
			const short *r = reinterpret_cast<const short *>(mapView + kWindowRect);
			m.view[0] = r[0];
			m.view[1] = r[1];
			m.view[2] = r[2] + 1;
			m.view[3] = r[3] + 1;
			m.haveView = m.view[2] > m.view[0] && m.view[3] > m.view[1];
		}
		const char *visible = *reinterpret_cast<const char **>(addr::kVisibleMgr);
		if (visible) {
			for (int i = 0; i < 4; i++) m.visible[i] = Field<int>(visible, kViewRect + i * 4);
			m.haveVisible = m.visible[2] > m.visible[0] && m.visible[3] > m.visible[1];
		}
		m.clock = Field<uint32_t>(game, kGameClock);
		const char *local = Field<const char *>(game, kLocalPlayer);
		if (local) {
			m.local = Field<int>(local, kPlayerIndex);
			m.enemies = Field<unsigned>(local, kPlayerEnemies) & kChatEveryone;
			m.localState = Field<int>(local, kPlayerState);
		}
		for (int i = 0; i < kSlots; i++) {
			const char *p = game + kPlayers + i * kPlayerSize;
			RawPlayer &out = m.players[i];
			CopyGameString(out.name, sizeof(out.name), p, kPlayerName, kPlayerNameSize, kPlayerNameCapacity);
			out.exists = out.name[0] && (Field<int>(p, kPlayerActive) != 0 || Field<int>(p, kPlayerState) != 0);
			const int control = Field<int>(p, kPlayerControl);
			out.human = control == 0;
			// 3 is the neutral player of the map and 4 an unused slot: nobody writes to them.
			out.playable = control != 3 && control != 4;
			out.rgb = Rgb555(Field<unsigned>(p, kPlayerColor));
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

ChatMatch Build(const RawMatch &raw)
{
	ChatMatch m;
	if (!raw.game || raw.editor) return m;
	const bool zoomMap = ZoomViewOpen();
	m.onScreen = raw.shown || zoomMap;
	m.playing = m.onScreen && !raw.demo && raw.local >= 0 && raw.local < 16;
	m.severalHumans = raw.severalHumans;
	m.local = raw.local;
	m.alive = raw.localState == kStatePlaying;
	m.enemies = raw.enemies;
	m.clockMs = raw.clock;
	for (int i = 0; i < kSlots; i++) {
		const RawPlayer &p = raw.players[i];
		if (!p.exists || !p.playable || i == raw.local) continue;
		ChatPlayer cp;
		cp.index = i;
		cp.name = p.name;
		cp.rgb = p.rgb;
		cp.enemy = (raw.enemies & (1u << i)) != 0;
		cp.human = p.human;
		if (!cp.enemy) m.hasAllies = true;
		m.others.push_back(cp);
	}
	m.haveView = raw.haveView;
	m.viewLeft = raw.view[0];
	m.viewTop = raw.view[1];
	m.viewRight = raw.view[2];
	m.viewBottom = raw.view[3];
	m.haveBar = ControlsCmdBarRect(m.barLeft, m.barTop, m.barRight, m.barBottom);
	return m;
}

bool CameraCentre(const RawMatch &raw, int &x, int &y)
{
	if (!raw.haveVisible) return false;
	x = (raw.visible[0] + raw.visible[2]) / 2;
	y = (raw.visible[1] + raw.visible[3]) / 2;
	return true;
}

// As CIngameChatDialog::SendMessage (004e51e0): the factory's object, the text, the sender, the point or -1, the mask, and the
// command queue, which runs it on every computer in the same tick.
bool QueueChat(const std::string &text, unsigned mask, bool withPoint, int x, int y)
{
	__try {
		char *game = Game();
		void *player = game ? Field<void *>(game, kLocalPlayer) : nullptr;
		if (!player) return false;
		char *cmd = static_cast<char *>(reinterpret_cast<Create_t>(addr::kChatMessageCreate)());
		if (!cmd) return false;
		*reinterpret_cast<unsigned short *>(cmd + kCmdMask) = 0;
		reinterpret_cast<SetPlayer_t>((*reinterpret_cast<void ***>(cmd))[kSlotSetPlayer])(cmd, nullptr, player);
		*reinterpret_cast<int *>(cmd + kCmdLocation) = withPoint ? x : -1;
		*reinterpret_cast<int *>(cmd + kCmdLocation + 4) = withPoint ? y : -1;
		reinterpret_cast<Assign_t>(addr::kStringAssign)(cmd + kCmdText, nullptr, text.c_str(), static_cast<unsigned>(text.size()));
		*reinterpret_cast<unsigned short *>(cmd + kCmdMask) = static_cast<unsigned short>(mask);
		reinterpret_cast<Queue_t>(addr::kQueueCommand)(game, nullptr, cmd);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Alone in a match, the dialog also hands the line to the script engine (004e51e0): kept, so the game behaves the same.
void RunAsScript(const std::string &text)
{
	__try {
		char *game = Game();
		if (game) reinterpret_cast<RunScript_t>((*reinterpret_cast<void ***>(game))[kSlotRunScript])(game, nullptr, text.c_str());
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

// The game's std::string (MSVC 2008): allocator, 16-byte buffer or pointer, size, capacity.
struct GameString {
	uint32_t allocator;
	union {
		char buf[16];
		const char *ptr;
	};
	uint32_t size, capacity;
};
static_assert(sizeof(GameString) == 0x1c, "the game's std::string is 28 bytes");
typedef void(__cdecl *PostNotification_t)(const GameString *type, GameString text, int mapX, int mapY);
typedef void(__stdcall *ScreenToMap_t)(unsigned *out, const unsigned *in);
// "location message" is the game's (VXNOTIFICATIONS.XML); the other two are added to that file by the built-in patch
// data/vxnotifications.xml, with the sprite and sound of "unit attacked" and "building attacked".
const char *const kPingNotices[kPingTypes] = {"location message", "mod ping attack", "mod ping danger"};

// Read only by the game, so a text past the 15 characters of the buffer can stay where it is.
GameString GameText(const char *text)
{
	GameString g = {};
	const size_t n = strlen(text);
	if (n < sizeof(g.buf)) {
		memcpy(g.buf, text, n + 1);
		g.capacity = 15;
	}
	else {
		g.ptr = text;
		g.capacity = static_cast<uint32_t>(n);
	}
	g.size = static_cast<uint32_t>(n);
	return g;
}

// CVXChatMessage::Execute (004e7370) raises "location message" (message icon on the map, its sound, the notification and the
// point that Q goes back to) for everyone but the sender. The sender gets it here, and a typed ping gets its own.
// NotifGroup::Notify (0055cad0) puts the type's sprite at the point given, with no unit or building needed.
bool NotifyPing(int type, int x, int y)
{
	__try {
		if (!Game()) return false;
		const unsigned in[2] = {static_cast<unsigned>(x), static_cast<unsigned>(y)};
		unsigned at[2] = {};
		reinterpret_cast<ScreenToMap_t>(addr::kScreenToMap)(at, in);
		if (static_cast<int>(at[0]) < 0 || static_cast<int>(at[1]) < 0 || at[0] >= 4000000 || at[1] >= 4000000) return false;
		const GameString notice = GameText(kPingNotices[type >= 0 && type < kPingTypes ? type : kPingHere]);
		GameString text = {};
		text.capacity = 15;
		reinterpret_cast<PostNotification_t>(addr::kPostNotificationAtPos)(&notice, text, static_cast<int>(at[0]), static_cast<int>(at[1]));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// A notification's map sprite goes when the game clock passes its end (CUINotificationIcon::OnMessage 00559860 compares
// state+0x28, the list node's +0x30, with game+0x1258), and the group drops old states only when another notification of
// its type comes (NotifGroup::Notify 0055cad0). After a defeat or a surrender the clock stops, and so does a pause: the marks
// would stay for good. The mod ends its marks itself: setting the end to 0 lets the game's own animation tick (a window
// message timed by the system clock, CWindow::PostMessage 006321e0) remove the sprite and its notification as usual.
typedef void *(__cdecl *GetGroup_t)(const GameString *type);
const unsigned kGroupList = 4, kNodeEnd = 0x30;

struct Mark {
	char *group = nullptr;
	char *node = nullptr;
	DWORD at = 0;
	int sender = -1;
};
std::vector<Mark> g_marks;

bool NodeAlive(const Mark &m)
{
	__try {
		const char *head = *reinterpret_cast<char **>(m.group + kGroupList);
		int guard = 0;
		for (const char *n = *reinterpret_cast<char **>(const_cast<char *>(head)); n && n != head && guard < 512; n = *reinterpret_cast<char *const *>(n), guard++) {
			if (n == m.node) return true;
		}
		return false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool EndNode(const Mark &m)
{
	if (!NodeAlive(m)) return false;
	__try {
		*reinterpret_cast<uint32_t *>(m.node + kNodeEnd) = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool NewestNode(int type, Mark &m)
{
	__try {
		const GameString notice = GameText(kPingNotices[type >= 0 && type < kPingTypes ? type : kPingHere]);
		m.group = static_cast<char *>(reinterpret_cast<GetGroup_t>(addr::kNotifGetGroup)(&notice));
		if (!m.group) return false;
		char *head = *reinterpret_cast<char **>(m.group + kGroupList);
		m.node = head ? *reinterpret_cast<char **>(head + 4) : nullptr;
		return m.node && m.node != head;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// After a notification of a ping: the newest state of its type is the mark (a nearby one may have been refreshed instead, which
// renews it). A sender keeps at most kPingMarksPerSender marks; the oldest gives way.
void TrackMark(int type, int sender)
{
	Mark m;
	if (!NewestNode(type, m)) return;
	m.at = GetTickCount();
	m.sender = sender;
	g_marks.erase(std::remove_if(g_marks.begin(), g_marks.end(), [&](const Mark &o) { return o.node == m.node || !NodeAlive(o); }), g_marks.end());
	g_marks.push_back(m);
	int count = 0;
	for (const Mark &o : g_marks) count += o.sender == sender ? 1 : 0;
	for (auto it = g_marks.begin(); count > kPingMarksPerSender && it != g_marks.end();) {
		if (it->sender != sender) {
			++it;
			continue;
		}
		EndNode(*it);
		LogD("game", "chat: player %d has more than %d marks on the map: the oldest goes", sender + 1, kPingMarksPerSender);
		it = g_marks.erase(it);
		count--;
	}
}

void ExpireMarks()
{
	const DWORD now = GetTickCount();
	for (auto it = g_marks.begin(); it != g_marks.end();) {
		if (now - it->at < kPingLifeMs) {
			++it;
			continue;
		}
		if (EndNode(*it)) LogD("game", "chat: a mark of player %d ended after %u ms", it->sender + 1, static_cast<unsigned>(now - it->at));
		it = g_marks.erase(it);
	}
}

int OtherHumans(const ChatMatch &m)
{
	int n = 0;
	for (const ChatPlayer &p : m.others) n += p.human ? 1 : 0;
	return n;
}

struct RawChat {
	bool show = false, own = false, location = false;
	int sender = -1, local = -1;
	int at[2] = {-1, -1};
	unsigned mask = 0;
	uint32_t rgb = 0, clock = 0;
	char name[64] = {};
	char text[1024] = {};
	char names[kSlots][64] = {};
};

// What CVXChatMessage::Execute shows: nothing in the editor; in a replay every line; in a match the lines whose mask has the local
// player. A line without text shows only its location.
bool ReadChat(const char *cmd, RawChat &c)
{
	__try {
		const char *game = Game();
		if (!game || *reinterpret_cast<int *>(addr::kEditorMode) != 0) return false;
		const char *sender = Field<const char *>(cmd, kCmdPlayer);
		const char *local = Field<const char *>(game, kLocalPlayer);
		if (!sender || !local) return false;
		c.mask = Field<unsigned short>(cmd, kCmdMask);
		c.local = Field<int>(local, kPlayerIndex);
		c.sender = Field<int>(sender, kPlayerIndex);
		const bool demo = reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0;
		c.own = !demo && sender == local;
		if (!demo && (c.local < 0 || c.local > 15 || !(c.mask & (1u << c.local)))) return false;
		c.at[0] = Field<int>(cmd, kCmdLocation);
		c.at[1] = Field<int>(cmd, kCmdLocation + 4);
		c.location = c.at[0] != -1 && c.at[1] != -1;
		CopyGameString(c.text, sizeof(c.text), cmd, kCmdTextBuf, kCmdTextSize, kCmdTextCapacity);
		if (!c.text[0] && !c.location) return false;
		CopyGameString(c.name, sizeof(c.name), sender, kPlayerName, kPlayerNameSize, kPlayerNameCapacity);
		c.rgb = Rgb555(Field<unsigned>(sender, kPlayerColor));
		c.clock = Field<uint32_t>(game, kGameClock);
		for (int i = 0; i < kSlots; i++) {
			const char *p = game + kPlayers + i * kPlayerSize;
			CopyGameString(c.names[i], sizeof(c.names[i]), p, kPlayerName, kPlayerNameSize, kPlayerNameCapacity);
		}
		c.show = true;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void CallExecute(void *cmd)
{
	g_suppressConsole = 1;
	g_origExecute(cmd);
	g_suppressConsole = 0;
}

ChatEntry EntryOf(const RawChat &c)
{
	ChatEntry e;
	e.arrivedMs = GetTickCount();
	e.clockMs = c.clock;
	int to = -1;
	e.scope = ChatScopeOfMask(c.mask, c.sender, to);
	e.sender = c.sender;
	e.name = c.name;
	e.rgb = c.rgb;
	if (e.scope == kChatPrivate && c.own && to >= 0 && to < kSlots) e.toName = c.names[to];
	e.text = c.text;
	e.location = c.location;
	e.own = c.own;
	std::vector<std::string> phrases[kPingTypes];
	for (int t = 0; t < kPingTypes; t++) phrases[t] = TrAllLanguages(ChatPingPhrase(t));
	e.ping = ChatPingTypeOf(e.text, e.location, phrases);
	return e;
}

// The location goes away for the length of Execute, so the game does not raise its generic notification over the typed one.
void CallExecuteWithoutLocation(void *cmd)
{
	int *at = reinterpret_cast<int *>(static_cast<char *>(cmd) + kCmdLocation);
	const int x = at[0], y = at[1];
	at[0] = at[1] = -1;
	CallExecute(cmd);
	at[0] = x;
	at[1] = y;
}

void __fastcall HookExecute(void *cmd)
{
	RawChat raw;
	if (!g_ready || !g_openHandler || !ReadChat(static_cast<const char *>(cmd), raw) || !raw.show) {
		g_origExecute(cmd);
		return;
	}
	ChatEntry e = EntryOf(raw);
	// The sender raised its own when sending; the game gives the sender none.
	const bool typed = e.ping == kPingAttack || e.ping == kPingDanger;
	if (typed && !raw.own) {
		CallExecuteWithoutLocation(cmd);
		const bool shown = NotifyPing(e.ping, raw.at[0], raw.at[1]);
		if (shown) TrackMark(e.ping, raw.sender);
		LogI("game", "chat: %s ping from player %d at %d,%d, %s", e.ping == kPingAttack ? "attack" : "danger", raw.sender + 1, raw.at[0], raw.at[1],
			shown ? "notified here" : "NOT notified here");
	}
	else {
		CallExecute(cmd);
		// The game raised its own location message for this line: it is a mark too.
		if (e.location && !raw.own) TrackMark(kPingHere, raw.sender);
	}
	LogD("game", "chat: line from player %d (%s%s, %u bytes)", raw.sender + 1, e.scope == kChatAll ? "everyone" : e.scope == kChatPrivate ? "private" : "allies",
		e.location ? ", with a location" : "", static_cast<unsigned>(e.text.size()));
	std::lock_guard<std::mutex> lock(g_logLock);
	g_log.Add(std::move(e));
}

__declspec(naked) void HookConsolePrintf()
{
	__asm {
		cmp g_suppressConsole, 0
		jne skip
		jmp dword ptr [g_origConsolePrintf]
	skip:
		ret
	}
}

void *__fastcall HookCtor(void *self, void *edx, int sendLocation, int mostDerived)
{
	if (g_ready && g_openHandler && self && mostDerived && g_openHandler(sendLocation != 0)) {
		// Both callers only allocate the dialog and forget it: the singleton keeps it. No dialog, so the memory goes back.
		reinterpret_cast<Free_t>(addr::kGameFree)(self);
		return self;
	}
	return g_origCtor(self, edx, sendLocation, mostDerived);
}

bool Create(uintptr_t target, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(target), detour, orig) == MH_OK) return true;
	LogE("game", "chat: could not hook %s (%08x)", what, static_cast<unsigned>(target));
	return false;
}

}

void InstallChat(bool knownExe)
{
	if (!knownExe) return;
	const uintptr_t targets[] = {addr::kConsolePrintf, addr::kChatMessageExecute, addr::kChatDialogCtor};
	bool created[3] = {};
	created[0] = Create(addr::kConsolePrintf, reinterpret_cast<void *>(&HookConsolePrintf), &g_origConsolePrintf, "ConsolePrintf");
	created[1] = created[0] && Create(addr::kChatMessageExecute, reinterpret_cast<void *>(&HookExecute), reinterpret_cast<void **>(&g_origExecute), "CVXChatMessage::Execute");
	created[2] = created[1] && Create(addr::kChatDialogCtor, reinterpret_cast<void *>(&HookCtor), reinterpret_cast<void **>(&g_origCtor), "CIngameChatDialog::CIngameChatDialog");
	if (!created[2]) {
		for (int i = 0; i < 3; i++) {
			if (created[i]) MH_RemoveHook(reinterpret_cast<void *>(targets[i]));
		}
		LogE("game", "chat: the floating chat is off; the game's chat window works as always");
		return;
	}
	bool ok = true;
	for (uintptr_t t : targets) ok = HookEnable(reinterpret_cast<void *>(t)) == MH_OK && ok;
	g_ready = ok;
	LogAt(ok ? kLogInfo : kLogError, "game", "chat: floating chat %s", ok ? "ready" : "NOT available (a hook could not be enabled)");
}

bool ChatFloating() { return g_ready && g_openHandler; }

ChatMatch ChatReadMatch()
{
	RawMatch raw;
	if (!ReadRaw(raw)) return ChatMatch();
	return Build(raw);
}

void ChatSetOpenHandler(ChatOpenFn fn) { g_openHandler = fn; }

bool ChatSend(const std::string &text, ChatTarget target, bool withLocation)
{
	const std::string clean = ChatCleanText(text);
	if (clean.empty() && !withLocation) return false;
	RawMatch raw;
	if (!ReadRaw(raw) || !raw.game || raw.local < 0) return false;
	if (!raw.severalHumans && !clean.empty()) RunAsScript(clean);
	int x = -1, y = -1;
	const bool point = withLocation && raw.localState == kStatePlaying && CameraCentre(raw, x, y);
	if (withLocation && !point) LogI("game", "chat: the location is left out (%s)", raw.localState == kStatePlaying ? "the view could not be read" : "you are out of the match");
	const unsigned mask = ChatMask(target, raw.local, raw.enemies);
	const bool ok = QueueChat(clean, mask, point, x, y);
	const bool shown = ok && point && NotifyPing(kPingHere, x, y);
	if (shown) TrackMark(kPingHere, raw.local);
	LogAt(ok ? kLogDebug : kLogError, "game", "chat: %s %u bytes to mask %04x%s", ok ? "sent" : "could NOT send", static_cast<unsigned>(clean.size()), mask,
		point ? (shown ? " with the camera's location (shown here too)" : " with the camera's location (NOT shown here)") : "");
	return ok;
}

bool ChatPing(int mouseX, int mouseY, bool haveMouse, const char *how, int type, ChatTarget target)
{
	if (type < 0 || type >= kPingTypes) type = kPingHere;
	RawMatch raw;
	if (!ReadRaw(raw) || !raw.game || raw.local < 0) {
		LogW("game", "chat: ping (%s) ignored: no match to read", how);
		return false;
	}
	const ChatMatch m = Build(raw);
	int point[2] = {-1, -1};
	const char *from = "the camera (the mouse is not over the map)";
	if (haveMouse && ZoomViewScreenPoint(mouseX, mouseY, point)) {
		from = "the zoom map";
	}
	else if (haveMouse && m.haveView && raw.haveVisible && mouseX >= m.viewLeft && mouseX < m.viewRight && mouseY >= m.viewTop && mouseY < m.viewBottom &&
		(!m.haveBar || mouseY < m.barTop)) {
		const int spanX = std::max(1, m.viewRight - m.viewLeft), spanY = std::max(1, m.viewBottom - m.viewTop);
		point[0] = raw.visible[0] + (mouseX - m.viewLeft) * (raw.visible[2] - raw.visible[0]) / spanX;
		point[1] = raw.visible[1] + (mouseY - m.viewTop) * (raw.visible[3] - raw.visible[1]) / spanY;
		from = "the map view";
	}
	else if (!CameraCentre(raw, point[0], point[1])) {
		LogW("game", "chat: ping (%s) ignored: the view of the map could not be read", how);
		return false;
	}
	// The line goes through the command queue even with nobody else to read it: it is the sender's own line in the chat.
	// The phrase goes in the sender's language: players without the mod read it next to the game's location message.
	const std::string phrase = ChatCleanText(Tr(ChatPingPhrase(type)));
	const bool ok = QueueChat(phrase, ChatMask(target, raw.local, raw.enemies), true, point[0], point[1]);
	const bool shown = NotifyPing(type, point[0], point[1]);
	if (shown) TrackMark(type, raw.local);
	const int humans = OtherHumans(m);
	static const char *const kTypeNames[kPingTypes] = {"here", "attack", "danger"};
	LogAt(ok && shown ? kLogInfo : kLogError, "game", "chat: %s ping (%s) at %d,%d from %s: %s to %s (%d other humans), %s here", kTypeNames[type], how, point[0], point[1], from,
		ok ? "sent" : "NOT sent", target.kind == kTargetAllies ? "the allies" : target.kind == kTargetPlayer ? "one player" : "everyone", humans,
		shown ? "notified" : "NOT notified");
	return ok;
}

namespace {
typedef void *(__fastcall *WindowAt_t)(void *window, void *edx, const short *point);
const unsigned kSlotDeepestAt = 0x3c / 4;

bool SameWindow(const char *a, const char *b)
{
	if (a == b) return true;
	__try {
		return a == b + 4 + *reinterpret_cast<const int *>(*reinterpret_cast<const char *const *>(b + 4) + 4) ||
			b == a + 4 + *reinterpret_cast<const int *>(*reinterpret_cast<const char *const *>(a + 4) + 4);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// The game's own answer to "which window is here" (CWindow::FindDeepestWindowAt 00632130, slot 15, from the root): the top bar,
// the command bar and every dialog sit over the map view, so a point on them does not reach it.
bool MapViewAt(int x, int y)
{
	__try {
		char *root = *reinterpret_cast<char **>(addr::kUIRoot);
		const char *map = *reinterpret_cast<const char **>(addr::kMapView);
		if (!root || !map) return false;
		const short point[2] = {static_cast<short>(x), static_cast<short>(y)};
		const char *hit = static_cast<const char *>(reinterpret_cast<WindowAt_t>((*reinterpret_cast<void ***>(root))[kSlotDeepestAt])(root, nullptr, point));
		return hit && SameWindow(hit, map);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
}

bool ChatPointOnMap(int x, int y, int *area, const char *&why)
{
	if (ZoomViewOpen()) {
		if (!ZoomViewBitmap(area)) {
			why = "the big map could not be read";
			return false;
		}
		why = "outside the big map's picture";
		return x >= area[0] && x < area[2] && y >= area[1] && y < area[3];
	}
	const ChatMatch m = ChatReadMatch();
	if (!m.haveView) {
		why = "no map view";
		return false;
	}
	area[0] = m.viewLeft;
	area[1] = m.viewTop;
	area[2] = m.viewRight;
	area[3] = m.haveBar ? std::min(m.viewBottom, m.barTop) : m.viewBottom;
	if (x < area[0] || x >= area[2] || y < area[1] || y >= area[3] || !MapViewAt(x, y)) {
		why = "over the game's interface";
		return false;
	}
	// The top bar covers the top of the map view: the map starts where the game finds the view again.
	for (int top = area[1]; top < y; top += 4) {
		if (MapViewAt(x, top)) {
			area[1] = top;
			break;
		}
	}
	return true;
}

void ChatAddLocal(const std::string &text)
{
	ChatEntry e;
	e.arrivedMs = GetTickCount();
	uint32_t clock = 0;
	if (Game()) ReadClock(Game(), clock);
	e.clockMs = clock;
	e.text = text;
	e.local = true;
	std::lock_guard<std::mutex> lock(g_logLock);
	g_log.Add(std::move(e));
}

uint32_t ChatLogSerial()
{
	std::lock_guard<std::mutex> lock(g_logLock);
	return g_log.Serial();
}

std::vector<ChatEntry> ChatLogLines()
{
	std::lock_guard<std::mutex> lock(g_logLock);
	return std::vector<ChatEntry>(g_log.Lines().begin(), g_log.Lines().end());
}

void ChatGameTick()
{
	// A C++ exception thrown through Execute would leave the console silenced.
	g_suppressConsole = 0;
	ExpireMarks();
	static char *lastGame = nullptr;
	static uint32_t lastClock = 0;
	char *game = Game();
	uint32_t clock = 0;
	if (game && !ReadClock(game, clock)) return;
	// Another match, or a saved game loaded into this one: the lines of before do not belong to it.
	const bool other = game != lastGame || clock + 5000 < lastClock;
	lastGame = game;
	lastClock = clock;
	if (!other) return;
	g_marks.clear();
	std::lock_guard<std::mutex> lock(g_logLock);
	g_log.Clear();
}
