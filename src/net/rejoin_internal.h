// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <utility>
#include <cstdint>
#include "MinHook.h"
#include "net/rejoin.h"
#include "net/sharemsg.h"

namespace rejoin_impl {

const unsigned kGameThreadManagers[2] = {0x1294, 0x1298};

const char kLogDir[] = "logs/imperivm/rejoin";

const char kDumpHost[] = "logs/imperivm/rejoin/host.txt";

const char kDumpBackBefore[] = "logs/imperivm/rejoin/return-before.txt";

const char kDumpBackAfter[] = "logs/imperivm/rejoin/return.txt";

const char kTraceFile[] = "logs/imperivm/rejoin/threads.txt";

const char kAiHistFile[] = "logs/imperivm/rejoin/ai-history.txt";

const char kObjectsHost[] = "logs/imperivm/rejoin/objects-host.bin";

const char kObjectsBack[] = "logs/imperivm/rejoin/objects-return.bin";

const unsigned kSlotIsPaused = 22;

const unsigned kSlotSetPaused = 16;

enum : uint8_t { kRjReq = 20, kRjAccept, kRjDeny, kRjState, kRjFile, kRjFileEnd, kRjReady, kRjGo, kRjCancel, kRjWait, kRjThreads, kRjDump, kRjObjects, kRjRawUnused, kRjReport, kRjQueries, kRjAi, kRjSquads, kRjModVars, kRjHandles };
static_assert(kRejoinMessages.Has(kRjReq) && kRejoinMessages.Has(kRjHandles), "rejoin messages out of their range");

enum : uint8_t { kRjReload = kReloadMessages.first, kRjReloadFile, kRjReloadEnd, kRjReloadAck, kRjReloadGo };
static_assert(kReloadMessages.Has(kRjReloadGo), "reload messages out of their range");

const char kMagicPrefix[] = "#RJ:";

const char kOwnerRedirect[] = "#host:";

const char kDenyOut[] = "#out:";

const char kDenyBusy[] = "#busy:";

const char kSaveName[] = "Autosave rejoin";

const char kBackSave[] = "SaveOnline/rejoin.snr";

const char kBackSettings[] = "SaveOnline/rejoin.usr";

typedef void *(__fastcall *AdvanceTick_t)(void *t);

typedef void(__fastcall *AddCommand_t)(void *pump, void *edx, int *cmd);

typedef void(__stdcall *Desync_t)(unsigned tick, unsigned hash, unsigned compared);

typedef void(__fastcall *ThisOnly_t)(void *self);

typedef void *(__fastcall *SendPacket_t)(void *gate, void *edx, unsigned mask, unsigned tick, int *desc);

typedef void *(__cdecl *Create_t)();

typedef void(__fastcall *Exec_t)(void *cmd);

typedef void(__fastcall *ThisPtr_t)(void *self, void *edx, void *arg);

typedef void(__fastcall *ThisInt_t)(void *self, void *edx, int arg);

typedef void *(__fastcall *Assign_t)(void *self, void *edx, const char *text, unsigned len);

typedef void *(__cdecl *OperatorNew_t)(unsigned size);

typedef int(__cdecl *IsDemo_t)();

typedef int(__fastcall *IntThis_t)(void *self);

typedef void *(__fastcall *LoadObject_t)(void *stream, void *edx, void *into, int a, int b);

typedef int(__fastcall *StreamOpen_t)(void *stream, void *edx, const char *path);

typedef void *(__fastcall *IsKindOf_t)(void *obj, void *edx, const char *name);

#pragma pack(push, 1)
struct WireState {
	uint32_t magic;
	int32_t tick;
	uint32_t hash58;
	int32_t tick70;
	int32_t pause90;
	int32_t timer2c;
	int32_t gameEnd;
	uint32_t sync[8];
	uint32_t mask;
	int32_t hostIndex;
	int32_t slot;
	uint32_t blob[8];
	uint32_t settingsSize;
	uint32_t saveSize;
	uint32_t multi[13];
	uint32_t rng[2];
};
#pragma pack(pop)

const uint32_t kWireMagic = 0x334e4a52;

struct Msg {
	uint64_t peer;
	uint8_t type;
	std::string data;
};

enum Role { kNoRole, kHost, kBack, kOther };

enum HostPhase { kHWaitJoin = 1, kHWaitFreeze, kHWaitSave, kHWaitReady };

enum BackPhase { kBWaitAccept = 1, kBWaitData, kBWaitStart, kBWaitLoad, kBWaitGo, kBFailed };

extern std::mutex g_mx;
extern std::deque<Msg> g_queue;
struct RejoinBlobs {
	WireState wire = {};
	std::string wireRaw;
	std::string save;
	std::string threads;
	std::string hostDump;
	std::string objects;
	std::string ai;
	std::string squads;
	std::string modVars;
	std::string handles;
	std::string queries;
};
extern RejoinBlobs g_blobs;
void *Game();
char *Transport();
void **Vt(void *obj);
unsigned Now();
char *Record(char *t, int peer, int tick);
std::string DiskPath(const std::string &vfs);
bool ReadFileAll(const std::string &path, std::string &out);
bool WriteFileAll(const std::string &path, const std::string &data);
void SetStatus(const std::string &text, bool failed = false);
void RaiseQuitTimeouts();
void Unfreeze();
void ResetRole();
std::vector<uint64_t> OtherMembers();
bool InOnlineGame();
int SlotOfNumber(int k);
void EnsureLogDir();
bool DumpGameState(const char *vfsPath);
#pragma pack(push, 1)
struct ThreadRec {
	uint8_t mgr;
	uint16_t bucket;
	uint16_t handle;
	uint32_t id;
	uint32_t due;
	uint32_t vt;
	uint32_t p2, p3;
	uint32_t tag;
};
#pragma pack(pop)

typedef void *(__fastcall *StartThread_t)(void *owner, void *edx, uint32_t id, uint32_t due);

typedef void(__fastcall *DeleteThread_t)(void *owner, void *edx, uint32_t id);

char *ThreadManager(int m);
struct TraceRec {
	int tick;
	char kind;
	uint16_t h;
	uint32_t t, vt, id, due, from, hash;
};

const size_t kRingSize = 1 << 21;

const int kTraceTicksWritten = 40;

uint32_t ThreadsHash();
void TraceStart(int);
void TracePush(const TraceRec &r);
bool Tracing(int &tick);
bool WriteTrace(int from);
struct TraceState {
	std::vector<TraceRec> ring;
	size_t ringNext = 0;
	size_t ringCount = 0;
	bool active = false;
	int lastTick = -1;
};
extern TraceState g_trace;
void *__fastcall HookStartThread(void *owner, void *edx, uint32_t id, uint32_t due);
typedef void *(__fastcall *RescheduleThread_t)(void *owner, void *edx, uint32_t *thread, uint32_t due);

typedef void *(__fastcall *StopThread_t)(void *owner, void *edx, uint32_t id);

void *__fastcall HookRescheduleThread(void *owner, void *edx, uint32_t *thread, uint32_t due);
void *__fastcall HookStopThread(void *owner, void *edx, uint32_t id);
typedef void(__fastcall *DeleteThreadHook_t)(void *owner, void *edx, uint32_t id);
void __fastcall HookDeleteThread(void *owner, void *edx, uint32_t id);
typedef int(__fastcall *SetThreadTime_t)(void *owner, void *edx, uint32_t id, uint32_t due);
int __fastcall HookSetThreadTime(void *owner, void *edx, uint32_t id, uint32_t due);
typedef int(__fastcall *RandomRange_t)(void *rng, void *edx, int lo, int hi);

int __fastcall HookRandomRange(void *rng, void *edx, int lo, int hi);
typedef void(__fastcall *AiUpdate_t)(void *exec);

typedef int(__cdecl *AiAddOrder_t)(void *exec, short prio, short type, unsigned short a, char *b, unsigned char flag);

bool AiBusy(char *exec);
uint32_t AiOrdersHash(char *exec, uint32_t &count, uint32_t &active);
void WriteAiHistory();
void __fastcall HookAiUpdate(void *execv);
int __cdecl HookAiAddOrder(void *execv, short prio, short type, unsigned short a, char *b, unsigned char flag);
const uint32_t kMaxNatives = 0x1000;

const uint32_t kThunkSize = 10;

const uint32_t kNativeDepth = 1024;

struct NativeState {
	uint32_t orig[kMaxNatives];
	uint8_t *thunks = nullptr;
};
extern NativeState g_natives;
void PatchNatives();
std::string NativeName(uint32_t index);
template <class Fn> void ForEachThread(char *mgr, Fn fn)
{
	const uint32_t n = *reinterpret_cast<uint32_t *>(mgr + 0x18);
	char *vec = *reinterpret_cast<char **>(mgr + 0xc);
	if (!vec || n == 0 || n > 100000) return;
	for (uint32_t b = 0; b < n; b++) {
		char *head = *reinterpret_cast<char **>(vec + b * 0xc + 4);
		if (!head) continue;
		int guard = 0;
		for (char *node = *reinterpret_cast<char **>(head); node && node != head && guard < 100000; node = *reinterpret_cast<char **>(node), guard++) {
			fn(b, node, reinterpret_cast<uint32_t *>(*reinterpret_cast<uint32_t *>(node + 8)));
		}
	}
}

std::string CollectThreads();
void FixThreads();
void FixCommandIds();
const unsigned kObjFirst = 0x24;

const unsigned kObjLast = 0x6c;

const unsigned kShipLoadFields = 0x234;

const unsigned kObjFields = (kObjLast - kObjFirst) / 4 + 1;

std::string CollectObjects(const std::string &dump);
void DumpObjectsBinary(const std::string &dump, const char *file);
std::string CollectHandles();
void ApplyHandles();
void FixObjects();
bool SafeCopy(void *dst, const void *src, size_t n);
void WriteAiState(FILE *f);
std::string CollectAi();
void ApplyAi();
std::string CollectSquads();
void ApplySquads();
std::string CollectQueries();
void ApplyQueries();
void LogDumpDiff(const std::string &host, const std::string &mine);
std::string BuildState(const std::string &saveData);
bool LoadSettings(const std::string &vfsPath, int slot);
enum ReloadPhase { kRNone, kRWaitData, kRWaitSlot, kRWaitGo, kRWaitAcks, kRQuit, kRStart, kRLoading };
struct ReloadState {
	int phase = kRNone;
	uint64_t from = 0;
	std::string returning;
	uint64_t returningId = 0;
	std::vector<uint64_t> peers;
	std::vector<uint64_t> waiting;
	DWORD goneSince = 0;
	std::string wireRaw;
	WireState wire = {};
	std::string save;
	std::string cams;
	bool mine = false;
	bool quitAsked = false;
	unsigned rules = 0;
	DWORD deadline = 0;
};
extern ReloadState g_reload;
void ReloadBroadcast(const std::string &save, const std::string &cams, const std::string &returning, uint64_t returningId);
void ReloadOnMessage(const Msg &m);
bool ReloadOnCancel(const Msg &m);
void ReloadTick();
void ReloadCancel(const std::string &why, bool tellHost);
void ReloadDrop();
void HostCancel(const std::string &why);
void HostOnRequest(uint64_t peer, int number);
void HostTick();
void ApplyGo(int slot);
void BackFail(const std::string &why);
struct RejoinSession {
	int role = kNoRole;
	int phase = 0;
	uint64_t peer = 0;
	std::string peerName;
	std::vector<std::pair<uint64_t, std::string>> candidates;
	DWORD ignoreFreezeUntil = 0;
	int slot = -1;
	int freezeTick = -1;
	bool frozen = false;
	DWORD deadline = 0;
	DWORD saveAskedAt = 0;
	std::string savePath;
	uint64_t lobby = 0;
	bool saveDone = false;
	bool pendingLoad = false;
	bool filterHost = false;
	bool firstAdvance = false;
	uint8_t restoreBits = 0;
	std::string status;
	DWORD lastRequest = 0;
	int joinTries = 0;
	DWORD joinRetryAt = 0;
	bool startCrumb = false;
};
extern RejoinSession g_session;
bool AskNext(const std::string &why);
void BackTick();
void FillRing(char *t, int myIndex);
void SendOwnTickData(char *t);
void *__fastcall HookAdvanceTick(void *tp);
void __fastcall HookAddCommand(void *pump, void *edx, int *cmd);
void __stdcall HookDesync(unsigned tick, unsigned hash, unsigned compared);
void __fastcall HookMsgExecute(void *cmd);
typedef void(__fastcall *SetPosition_t)(void *self, void *edx, uint32_t *pt, int a, int b);

void __fastcall HookSetPosition(void *self, void *edx, uint32_t *pt, int a, int b);
typedef void(__fastcall *RemoveFromMap_t)(void *self);
void __fastcall HookRemoveFromMap(void *self);
const int kMovesTicksWritten = 40;
const char kMovesFile[] = "logs/imperivm/rejoin/moves.txt";
uint32_t PositionsHash();
const uint16_t kFirstLocalHandle = 40000;
bool KeepLocalOutOfPositionsHash(void *obj, uint32_t before);
uint32_t *ThreadsChecksum(void *owner);
bool KeepLocalOutOfThreadsHash(void *owner, uint32_t *checksum, uint32_t before);
void TimerPush(char kind, void *owner, uint32_t id, uint32_t due, uint32_t before, void *from);
void MovePush(char kind, void *obj, uint32_t before, void *from);
bool WriteMoves(int from);
typedef void(__cdecl *TraceTileLine_t)(int, int, int, int, void *);
typedef void(__cdecl *SmoothPath_t)(int, void *, int);

typedef int(__fastcall *TileLineVisit_t)(void *self, void *edx, int x, int y);

struct HookOriginals {
	AdvanceTick_t advance = nullptr;
	AddCommand_t addCommand = nullptr;
	Desync_t desync = nullptr;
	Exec_t msgExec = nullptr;
	StartThread_t startThread = nullptr;
	RescheduleThread_t reschedule = nullptr;
	StopThread_t stopThread = nullptr;
	DeleteThreadHook_t deleteThread = nullptr;
	SetThreadTime_t setThreadTime = nullptr;
	RandomRange_t randomRange = nullptr;
	AiUpdate_t aiUpdate = nullptr;
	AiAddOrder_t aiAddOrder = nullptr;
	SetPosition_t setPosition = nullptr;
	RemoveFromMap_t removeFromMap = nullptr;
	TraceTileLine_t traceTileLine = nullptr;
	SmoothPath_t smoothPath = nullptr;
	TileLineVisit_t tileLineVisit = nullptr;
};
extern HookOriginals g_orig;
void __cdecl HookTraceTileLine(int x1, int y1, int x2, int y2, void *visitor);
void __cdecl HookSmoothPath(int path, void *out, int flag);
int __fastcall HookTileLineVisit(void *self, void *edx, int x, int y);
bool HookAt(uintptr_t addr, void *detour, void **orig);

}
