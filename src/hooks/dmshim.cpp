// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "miniaudio.h"
#include "MinHook.h"
#include "core/config.h"
#include "hooks/dmshim.h"
#include "core/log.h"
#include "hooks/gamefiles.h"
#include "addr/media.h"

namespace {
const GUID kClsidLoader = {0xd2ac2892, 0xb39b, 0x11d1, {0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd}};
const GUID kClsidPerformance = {0xd2ac2881, 0xb39b, 0x11d1, {0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd}};
const GUID kGuidNotificationSegment = {0xd2ac2899, 0xb39b, 0x11d1, {0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd}};

const DWORD kSegfSecondary = 0x80;
const DWORD kSegEnd = 1;

ma_engine g_engine;
bool g_engineReady = false;
std::recursive_mutex g_mx;
HANDLE g_wakeDispatcher = nullptr;
HANDLE g_dispatcher = nullptr;
std::atomic<bool> g_quit{false};
DWORD g_t0 = 0;

DWORD NowMs() { return GetTickCount() - g_t0; }

struct FakePath;
struct FakeSegment;
struct FakeState;

struct Voice {
	ma_decoder dec;
	ma_sound snd;
	bool inited = false;
	std::atomic<bool> ended{false};
	std::atomic<bool> stopped{false};
	bool notified = false;
	bool primary = false;
};

#define FN(f) reinterpret_cast<void *>(&f)

struct ComObj {
	void **vtbl;
	LONG refs;
};

HRESULT __stdcall NotImpl(void *) { LogW("audio", "modern audio: call to an unimplemented COM method"); return E_NOTIMPL; }

void **MakeVtbl(int slots)
{
	void **v = new void *[slots];
	for (int i = 0; i < slots; i++) {
		v[i] = reinterpret_cast<void *>(&NotImpl);
	}
	return v;
}

struct FakeSegment : ComObj {
	std::shared_ptr<std::vector<char>> data;
	std::string name;
	DWORD repeats = 0;
	DWORD lengthMs = 1;
};

struct FakeState : ComObj {
	FakeSegment *seg = nullptr;
	FakePath *path = nullptr;
	Voice *voice = nullptr;
	DWORD startMs = 0;
};

struct FakeBuffer : ComObj {
	FakePath *owner;
};
struct FakePath : ComObj {
	ma_sound_group group;
	bool groupInited = false;
	DWORD type = 0;
	FakeBuffer buffer;
};

struct FakePerf : ComObj {
	HANDLE notify = nullptr;
	std::deque<BYTE *> queue;
};
struct FakeLoader : ComObj {};

FakePerf *g_perf = nullptr;
std::vector<FakeState *> g_active;

ULONG AddRefObj(ComObj *o) { return InterlockedIncrement(&o->refs); }

void OnSoundEnd(void *user, ma_sound *)
{
	static_cast<Voice *>(user)->ended = true;
	SetEvent(g_wakeDispatcher);
}

void StopVoice(Voice *v)
{
	if (v && v->inited && !v->stopped && !v->ended) {
		v->stopped = true;
		ma_sound_stop(&v->snd);
	}
}

void DestroyVoice(Voice *v)
{
	if (!v) return;
	if (v->inited) {
		ma_sound_uninit(&v->snd);
		ma_decoder_uninit(&v->dec);
	}
	delete v;
}

bool VoicePlaying(Voice *v)
{
	return v && v->inited && !v->ended && !v->stopped;
}

void **g_vtSegment = nullptr;

HRESULT __stdcall SegQI(FakeSegment *self, REFIID, void **out) { AddRefObj(self); *out = self; return S_OK; }
ULONG __stdcall SegAddRef(FakeSegment *self) { return AddRefObj(self); }
ULONG __stdcall SegRelease(FakeSegment *self)
{
	LONG r = InterlockedDecrement(&self->refs);
	if (r == 0) delete self;
	return r;
}
HRESULT __stdcall SegGetLength(FakeSegment *self, LONG *mt) { *mt = static_cast<LONG>(self->lengthMs); return S_OK; }
HRESULT __stdcall SegSetRepeats(FakeSegment *self, DWORD n) { self->repeats = n; return S_OK; }
HRESULT __stdcall SegGetStartPoint(FakeSegment *, LONG *mt) { *mt = 0; return S_OK; }
HRESULT __stdcall SegDownload(FakeSegment *, IUnknown *) { return S_OK; }
HRESULT __stdcall SegUnload(FakeSegment *, IUnknown *) { return S_OK; }

void **g_vtState = nullptr;

HRESULT __stdcall StQI(FakeState *self, REFIID, void **out) { AddRefObj(self); *out = self; return S_OK; }
ULONG __stdcall StAddRef(FakeState *self) { return AddRefObj(self); }
ULONG __stdcall StRelease(FakeState *self)
{
	LONG r = InterlockedDecrement(&self->refs);
	if (r == 0) {
		StopVoice(self->voice);
		DestroyVoice(self->voice);
		if (self->seg) SegRelease(self->seg);
		delete self;
	}
	return r;
}
HRESULT __stdcall StGetSegment(FakeState *self, FakeSegment **out)
{
	*out = self->seg;
	if (self->seg) AddRefObj(self->seg);
	return self->seg ? S_OK : E_FAIL;
}
HRESULT __stdcall StGetStartTime(FakeState *self, LONG *mt) { *mt = static_cast<LONG>(self->startMs); return S_OK; }

void **g_vtBuffer = nullptr;
ULONG __stdcall PathRelease(FakePath *self);

HRESULT __stdcall BufQI(FakeBuffer *self, REFIID, void **out) { AddRefObj(self->owner); *out = self; return S_OK; }
ULONG __stdcall BufAddRef(FakeBuffer *self) { return AddRefObj(self->owner); }
ULONG __stdcall BufRelease(FakeBuffer *self) { return PathRelease(self->owner); }
HRESULT __stdcall BufSetPan(FakeBuffer *self, LONG pan)
{
	if (pan > 10000) pan = 10000;
	if (pan < -10000) pan = -10000;
	float b = 0.0f;
	if (pan > 0) b = 1.0f - powf(10.0f, -pan / 2000.0f);
	if (pan < 0) b = -(1.0f - powf(10.0f, pan / 2000.0f));
	ma_sound_group_set_pan(&self->owner->group, b);
	return S_OK;
}

void **g_vtPath = nullptr;

HRESULT __stdcall PathQI(FakePath *self, REFIID, void **out) { AddRefObj(self); *out = self; return S_OK; }
ULONG __stdcall PathAddRef(FakePath *self) { return AddRefObj(self); }
ULONG __stdcall PathRelease(FakePath *self)
{
	LONG r = InterlockedDecrement(&self->refs);
	if (r == 0) {
		std::lock_guard<std::recursive_mutex> lock(g_mx);
		for (FakeState *s : g_active) {
			if (s->path == self) {
				StopVoice(s->voice);
				DestroyVoice(s->voice);
				s->voice = nullptr;
				s->path = nullptr;
			}
		}
		if (self->groupInited) ma_sound_group_uninit(&self->group);
		delete self;
	}
	return r;
}
HRESULT __stdcall PathGetObjectInPath(FakePath *self, DWORD, DWORD, DWORD, REFGUID, DWORD, REFGUID, void **out)
{
	AddRefObj(self);
	*out = &self->buffer;
	return S_OK;
}
HRESULT __stdcall PathSetVolume(FakePath *self, LONG mB, DWORD)
{
	if (mB > 2000) mB = 2000;
	if (mB < -9600) mB = -9600;
	ma_sound_group_set_volume(&self->group, powf(10.0f, mB / 2000.0f));
	return S_OK;
}

void **g_vtPerf = nullptr;

void StopAllVoices()
{
	std::lock_guard<std::recursive_mutex> lock(g_mx);
	for (FakeState *s : g_active) StopVoice(s->voice);
}

HRESULT __stdcall PerfFreePMsg(FakePerf *, BYTE *msg);

HRESULT __stdcall PerfQI(FakePerf *self, REFIID, void **out) { AddRefObj(self); *out = self; return S_OK; }
ULONG __stdcall PerfAddRef(FakePerf *self) { return AddRefObj(self); }
ULONG __stdcall PerfRelease(FakePerf *self)
{
	LONG r = InterlockedDecrement(&self->refs);
	if (r == 0) {
		StopAllVoices();
		std::lock_guard<std::recursive_mutex> lock(g_mx);
		for (BYTE *m : self->queue) {
			PerfFreePMsg(self, m);
		}
		self->queue.clear();
		g_perf = nullptr;
		delete self;
	}
	return r;
}

HRESULT __stdcall PerfStop(FakePerf *, FakeSegment *seg, FakeState *st, LONG, DWORD)
{
	std::lock_guard<std::recursive_mutex> lock(g_mx);
	if (!seg && !st) {
		StopAllVoices();
		return S_OK;
	}
	for (FakeState *s : g_active) {
		if ((st && s == st) || (!st && seg && s->seg == seg)) StopVoice(s->voice);
	}
	return S_OK;
}

HRESULT __stdcall PerfIsPlaying(FakePerf *, FakeSegment *seg, FakeState *st)
{
	std::lock_guard<std::recursive_mutex> lock(g_mx);
	if (st) return VoicePlaying(st->voice) ? S_OK : S_FALSE;
	for (FakeState *s : g_active) {
		if (s->seg == seg && VoicePlaying(s->voice)) return S_OK;
	}
	return S_FALSE;
}

HRESULT __stdcall PerfGetTime(FakePerf *, LONGLONG *rt, LONG *mt)
{
	if (rt) *rt = static_cast<LONGLONG>(NowMs()) * 10000;
	if (mt) *mt = static_cast<LONG>(NowMs());
	return S_OK;
}

HRESULT __stdcall PerfFreePMsg(FakePerf *, BYTE *msg)
{
	if (msg) {
		FakeState *st = *reinterpret_cast<FakeState **>(msg + 0x34);
		if (st) StRelease(st);
		delete[] msg;
	}
	return S_OK;
}

HRESULT __stdcall PerfSetNotificationHandle(FakePerf *self, HANDLE h, LONGLONG)
{
	self->notify = h;
	return S_OK;
}

HRESULT __stdcall PerfGetNotificationPMsg(FakePerf *self, BYTE **out)
{
	std::lock_guard<std::recursive_mutex> lock(g_mx);
	if (self->queue.empty()) {
		*out = nullptr;
		return S_FALSE;
	}
	*out = self->queue.front();
	self->queue.pop_front();
	return S_OK;
}

HRESULT __stdcall PerfAddNotificationType(FakePerf *, REFGUID) { return S_OK; }
HRESULT __stdcall PerfCloseDown(FakePerf *) { StopAllVoices(); return S_OK; }

HRESULT __stdcall PerfInitAudio(FakePerf *, void **, void **, HWND, DWORD, DWORD, DWORD, void *)
{
	if (g_engineReady) return S_OK;
	ma_engine_config cfg = ma_engine_config_init();
	if (ma_engine_init(&cfg, &g_engine) != MA_SUCCESS) {
		LogE("audio", "modern audio: could not start miniaudio");
		return E_FAIL;
	}
	g_engineReady = true;
	LogI("audio", "modern audio: miniaudio ready (%u Hz, %u channels)", ma_engine_get_sample_rate(&g_engine),
		ma_engine_get_channels(&g_engine));
	return S_OK;
}

HRESULT __stdcall PerfCreateStandardAudioPath(FakePerf *, DWORD type, DWORD, BOOL, FakePath **out)
{
	if (!g_engineReady) return E_FAIL;
	auto *p = new FakePath();
	p->vtbl = g_vtPath;
	p->refs = 1;
	p->type = type;
	p->buffer.vtbl = g_vtBuffer;
	p->buffer.refs = 0;
	p->buffer.owner = p;
	if (ma_sound_group_init(&g_engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &p->group) != MA_SUCCESS) {
		delete p;
		return E_FAIL;
	}
	p->groupInited = true;
	ma_sound_group_set_pan_mode(&p->group, ma_pan_mode_balance);
	*out = p;
	return S_OK;
}

HRESULT __stdcall PerfPlaySegmentEx(FakePerf *, FakeSegment *seg, void *, void *, DWORD flags, LONGLONG,
	FakeState **outState, void *from, FakePath *path)
{
	if (!seg || !seg->data || !path || !g_engineReady) return E_FAIL;
	std::lock_guard<std::recursive_mutex> lock(g_mx);
	const bool primary = !(flags & kSegfSecondary);

	for (FakeState *s : g_active) {
		if ((from && s->path == from) || (primary && s->voice && s->voice->primary)) StopVoice(s->voice);
	}

	auto *v = new Voice();
	v->primary = primary;
	ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_memory(seg->data->data(), seg->data->size(), &dc, &v->dec) != MA_SUCCESS) {
		delete v;
		return E_FAIL;
	}
	if (ma_sound_init_from_data_source(&g_engine, &v->dec, MA_SOUND_FLAG_NO_SPATIALIZATION, &path->group,
			&v->snd) != MA_SUCCESS) {
		ma_decoder_uninit(&v->dec);
		delete v;
		return E_FAIL;
	}
	v->inited = true;
	ma_sound_set_looping(&v->snd, seg->repeats == 0xFFFFFFFF ? MA_TRUE : MA_FALSE);
	ma_sound_set_end_callback(&v->snd, &OnSoundEnd, v);

	auto *st = new FakeState();
	st->vtbl = g_vtState;
	st->refs = 2;
	st->seg = seg;
	AddRefObj(seg);
	st->path = path;
	st->voice = v;
	st->startMs = NowMs();
	g_active.push_back(st);
	ma_sound_start(&v->snd);
	*outState = st;
	return S_OK;
}

void **g_vtLoader = nullptr;

HRESULT __stdcall LdrQI(FakeLoader *self, REFIID, void **out) { AddRefObj(self); *out = self; return S_OK; }
ULONG __stdcall LdrAddRef(FakeLoader *self) { return AddRefObj(self); }
ULONG __stdcall LdrRelease(FakeLoader *self)
{
	LONG r = InterlockedDecrement(&self->refs);
	if (r == 0) delete self;
	return r;
}

HRESULT __stdcall LdrGetObject(FakeLoader *, BYTE *desc, REFIID, void **out)
{
	*out = nullptr;
	void *stream = *reinterpret_cast<void **>(desc + 0x34c);
	if (!stream) return E_FAIL;
	uintptr_t vt = *reinterpret_cast<uintptr_t *>(stream);
	const char *name = nullptr;
	unsigned mode = 0;
	if (vt == addr::kVtblOggStream) {
		name = static_cast<const char *>(stream) + 0xc;
		mode = 0x41;
	}
	else if (vt == addr::kVtblFileStream) {
		name = static_cast<const char *>(stream) + 0x8;
		mode = 0xc1;
	}
	if (!name) {
		LogW("audio", "modern audio: unknown stream (vtable %08x)", static_cast<unsigned>(vt));
		return E_FAIL;
	}
	auto data = std::make_shared<std::vector<char>>();
	if (!ReadGameFile(name, mode, *data) || data->empty()) {
		LogW("audio", "modern audio: could not read %s", name);
		return E_FAIL;
	}
	ma_decoder dec;
	ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_memory(data->data(), data->size(), &dc, &dec) != MA_SUCCESS) {
		LogW("audio", "modern audio: unsupported format in %s", name);
		return E_FAIL;
	}
	ma_uint64 frames = 0;
	ma_decoder_get_length_in_pcm_frames(&dec, &frames);
	DWORD lenMs = dec.outputSampleRate ? static_cast<DWORD>(frames * 1000 / dec.outputSampleRate) : 0;
	ma_decoder_uninit(&dec);

	auto *seg = new FakeSegment();
	seg->vtbl = g_vtSegment;
	seg->refs = 1;
	seg->data = data;
	seg->name = name;
	seg->lengthMs = lenMs ? lenMs : 1;
	*out = seg;
	return S_OK;
}
HRESULT __stdcall LdrReleaseObjectByUnknown(FakeLoader *, IUnknown *) { return S_OK; }

DWORD WINAPI DispatcherThread(LPVOID)
{
	while (!g_quit) {
		WaitForSingleObject(g_wakeDispatcher, 50);
		std::lock_guard<std::recursive_mutex> lock(g_mx);
		bool signal = false;
		for (size_t i = 0; i < g_active.size();) {
			FakeState *s = g_active[i];
			Voice *v = s->voice;
			if (v && v->ended && !v->notified && g_perf) {
				v->notified = true;
				BYTE *msg = new BYTE[0x60]();
				AddRefObj(s);
				*reinterpret_cast<FakeState **>(msg + 0x34) = s;
				memcpy(msg + 0x38, &kGuidNotificationSegment, sizeof(GUID));
				*reinterpret_cast<DWORD *>(msg + 0x48) = kSegEnd;
				g_perf->queue.push_back(msg);
				signal = true;
			}
			bool done = !v || v->stopped || (v->ended && v->notified);
			if (done && s->refs == 1) {
				g_active.erase(g_active.begin() + i);
				StRelease(s);
				continue;
			}
			i++;
		}
		if (signal && g_perf && g_perf->notify) SetEvent(g_perf->notify);
	}
	return 0;
}

typedef HRESULT(WINAPI *CoCreateInstance_t)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
CoCreateInstance_t g_origCoCreate = nullptr;

void BuildVtables()
{
	g_vtLoader = MakeVtbl(16);
	g_vtLoader[0] = FN(LdrQI); g_vtLoader[1] = FN(LdrAddRef); g_vtLoader[2] = FN(LdrRelease);
	g_vtLoader[0x0c / 4] = FN(LdrGetObject); g_vtLoader[0x34 / 4] = FN(LdrReleaseObjectByUnknown);

	g_vtPerf = MakeVtbl(64);
	g_vtPerf[0] = FN(PerfQI); g_vtPerf[1] = FN(PerfAddRef); g_vtPerf[2] = FN(PerfRelease);
	g_vtPerf[0x14 / 4] = FN(PerfStop); g_vtPerf[0x38 / 4] = FN(PerfIsPlaying); g_vtPerf[0x3c / 4] = FN(PerfGetTime);
	g_vtPerf[0x44 / 4] = FN(PerfFreePMsg); g_vtPerf[0x50 / 4] = FN(PerfSetNotificationHandle);
	g_vtPerf[0x54 / 4] = FN(PerfGetNotificationPMsg); g_vtPerf[0x58 / 4] = FN(PerfAddNotificationType);
	g_vtPerf[0x98 / 4] = FN(PerfCloseDown); g_vtPerf[0xb0 / 4] = FN(PerfInitAudio);
	g_vtPerf[0xb4 / 4] = FN(PerfPlaySegmentEx); g_vtPerf[0xc4 / 4] = FN(PerfCreateStandardAudioPath);

	g_vtSegment = MakeVtbl(40);
	g_vtSegment[0] = FN(SegQI); g_vtSegment[1] = FN(SegAddRef); g_vtSegment[2] = FN(SegRelease);
	g_vtSegment[0x0c / 4] = FN(SegGetLength); g_vtSegment[0x18 / 4] = FN(SegSetRepeats);
	g_vtSegment[0x58 / 4] = FN(SegGetStartPoint); g_vtSegment[0x74 / 4] = FN(SegDownload); g_vtSegment[0x78 / 4] = FN(SegUnload);

	g_vtState = MakeVtbl(12);
	g_vtState[0] = FN(StQI); g_vtState[1] = FN(StAddRef); g_vtState[2] = FN(StRelease);
	g_vtState[0x10 / 4] = FN(StGetSegment); g_vtState[0x14 / 4] = FN(StGetStartTime);

	g_vtPath = MakeVtbl(12);
	g_vtPath[0] = FN(PathQI); g_vtPath[1] = FN(PathAddRef); g_vtPath[2] = FN(PathRelease);
	g_vtPath[0x0c / 4] = FN(PathGetObjectInPath); g_vtPath[0x14 / 4] = FN(PathSetVolume);

	g_vtBuffer = MakeVtbl(24);
	g_vtBuffer[0] = FN(BufQI); g_vtBuffer[1] = FN(BufAddRef); g_vtBuffer[2] = FN(BufRelease);
	g_vtBuffer[0x40 / 4] = FN(BufSetPan);
}

bool g_shimEnabled = false;

bool CreateFake(REFCLSID clsid, LPVOID *out)
{
	if (!g_shimEnabled) return false;
	if (IsEqualGUID(clsid, kClsidLoader)) {
		auto *l = new FakeLoader();
		l->vtbl = g_vtLoader;
		l->refs = 1;
		*out = l;
		LogD("audio", "modern audio: DirectMusic Loader replaced");
		return true;
	}
	if (IsEqualGUID(clsid, kClsidPerformance)) {
		auto *p = new FakePerf();
		p->vtbl = g_vtPerf;
		p->refs = 1;
		g_perf = p;
		*out = p;
		if (!g_dispatcher) {
			g_t0 = GetTickCount();
			g_wakeDispatcher = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			g_dispatcher = CreateThread(nullptr, 0, DispatcherThread, nullptr, 0, nullptr);
		}
		LogD("audio", "modern audio: DirectMusic Performance replaced");
		return true;
	}
	return false;
}

}

bool DmShimCreate(REFCLSID clsid, LPVOID *out)
{
	return CreateFake(clsid, out);
}

void InstallModernAudio(bool knownExe)
{
	if (!knownExe || !GetConfig().modernAudio) {
		LogD("audio", "original DirectMusic%s", knownExe ? "" : " (unknown executable)");
		return;
	}
	BuildVtables();
	g_shimEnabled = true;
	LogI("audio", "modern audio: enabled (miniaudio instead of DirectMusic)");
}

bool DmShimPausePath(void *path, bool pause)
{
	auto *p = static_cast<FakePath *>(path);
	if (!g_shimEnabled || !p || p->vtbl != g_vtPath || !p->groupInited) return false;
	if (pause) ma_sound_group_stop(&p->group);
	else ma_sound_group_start(&p->group);
	return true;
}

namespace {
struct UiTone {
	ma_audio_buffer buf;
	ma_sound snd;
	bool ready = false;
};
UiTone g_uiTones[4];
std::mutex g_uiMx;

}

void UiSoundPlay(int kind)
{
	if (kind < 0 || kind > 3 || !GetConfig().onlineAlerts) return;
	std::lock_guard<std::mutex> lock(g_uiMx);
	if (!g_engineReady) return;
	UiTone &t = g_uiTones[kind];
	if (!t.ready) {
		static const struct {
			float freq[4];
			int notes;
			float noteSecs;
			float volume;
		} kSpec[4] = {{{988.0f, 0, 0, 0}, 1, 0.10f, 0.45f}, {{784.0f, 1047.0f, 1319.0f, 0}, 3, 0.09f, 0.5f}, {{740.0f, 0, 0, 0}, 1, 0.10f, 0.4f},
			// A patron joins: a short fanfare, G C E G.
			{{784.0f, 1047.0f, 1319.0f, 1568.0f}, 4, 0.12f, 0.55f}};
		const auto &s = kSpec[kind];
		const ma_uint32 rate = ma_engine_get_sample_rate(&g_engine);
		const ma_uint32 step = static_cast<ma_uint32>(rate * s.noteSecs), tail = static_cast<ma_uint32>(rate * 0.14f);
		const ma_uint32 total = step * (s.notes - 1) + step + tail;
		std::vector<float> data(static_cast<size_t>(total) * 2, 0.0f);
		const float twoPi = 6.2831853f;
		for (int n = 0; n < s.notes; n++) {
			for (ma_uint32 j = 0; j < step + tail && n * step + j < total; j++) {
				const float sec = j / static_cast<float>(rate);
				const float attack = j < rate / 250 ? j / (rate / 250.0f) : 1.0f;
				const float env = attack * expf(-sec * 16.0f);
				const float v = (sinf(twoPi * s.freq[n] * sec) + 0.3f * sinf(2 * twoPi * s.freq[n] * sec)) * env * 0.4f;
				data[static_cast<size_t>(n * step + j) * 2] += v;
				data[static_cast<size_t>(n * step + j) * 2 + 1] += v;
			}
		}
		ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_f32, 2, total, data.data(), nullptr);
		if (ma_audio_buffer_init_copy(&cfg, &t.buf) != MA_SUCCESS) return;
		if (ma_sound_init_from_data_source(&g_engine, &t.buf, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &t.snd) != MA_SUCCESS) {
			ma_audio_buffer_uninit(&t.buf);
			return;
		}
		ma_sound_set_volume(&t.snd, s.volume);
		t.ready = true;
	}
	ma_sound_stop(&t.snd);
	ma_sound_seek_to_pcm_frame(&t.snd, 0);
	ma_sound_start(&t.snd);
}
