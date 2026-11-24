// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The numbers of the performance panel, without the game: frame times, the hitches and the idle screens, percentiles, CPU shares,
// the graph's columns and the capture's file. All times are in milliseconds of one monotonic clock.
namespace perf {

// In a match, a frame is a hitch when it takes longer than both twice the median of the frames before it and the median plus this,
// and the next one is back under that line: at 60 FPS (16.7 ms) a hitch is a frame over 41.7 ms, at 30 FPS (33.3 ms) one over 66.7 ms.
// A run of slow frames is the match getting slower (the FPS and the lows show it), and only its last one is a hitch.
constexpr float kHitchRatio = 2.0f;
constexpr float kHitchExtraMs = 25.0f;
// The median is taken over the last frames, and hitches are only judged once it has enough of them.
constexpr int kMedianFrames = 64;
constexpr int kWarmupFrames = 12;
// Copies of the game's image closer together than this are parts of one frame (the game can send its image in several pieces).
constexpr double kCoalesceMs = 1.5;
// A gap this long is not a frame: the game was minimised, loading or held by a dialog.
constexpr float kSuspendMs = 5000.0f;
// Out of a match the game only draws when something changes, so a slow frame there is an idle screen and never a hitch. It is drawing
// continuously after this many frames in a row each shorter than kActiveMaxMs, and idle again after a frame over the hitch threshold
// (or of kIdleGapMs or more before the median has enough frames).
constexpr int kActiveRun = 8;
constexpr float kActiveMaxMs = 100.0f;
constexpr float kIdleGapMs = 250.0f;
// With no frame for this long the screen counts as idle (out of a match) or stalled (in one).
constexpr double kIdleAfterMs = 1000.0;
constexpr double kStallAfterMs = 2000.0;

enum FrameFlags : uint8_t {
	// Part of the statistics: the game was drawing continuously.
	kFrameCounted = 1,
	kFrameHitch = 2,
};

struct FrameRecord {
	// When the frame arrived, and how long it took since the one before.
	double t = 0;
	float ms = 0;
	uint8_t flags = 0;
};

enum class ScreenState { kIdle, kActive, kStalled };

class FrameTracker {
public:
	explicit FrameTracker(size_t capacity = 32768);
	// An image of the game arrived at `nowMs`. `continuous` says that the game is expected to draw all the time (a running match):
	// then a slow frame is always a hitch, never an idle screen. False when the image belongs to the frame before (kCoalesceMs).
	bool AddFrame(double nowMs, bool continuous);
	ScreenState State(double nowMs, bool continuous) const;
	float MedianMs() const;
	float HitchThresholdMs() const;
	size_t Size() const { return count_; }
	// i from the oldest kept.
	const FrameRecord &At(size_t i) const { return ring_[(head_ + ring_.size() - count_ + i) % ring_.size()]; }
	// The frames that arrived after `fromMs`, oldest first, into `out` (cleared first; its capacity is reused).
	void CopySince(double fromMs, std::vector<FrameRecord> &out) const;
	double LastFrameMs() const { return last_; }
	uint64_t TotalHitches() const { return hitches_; }
	void Reset();

private:
	void Push(const FrameRecord &r);
	void PushMedian(float ms);
	void ClearMedian();
	void ResolvePending(float nextMs, bool continuous);

	std::vector<FrameRecord> ring_;
	size_t head_ = 0, count_ = 0;
	double last_ = -1;
	float recent_[kMedianFrames] = {};
	int recentCount_ = 0, recentPos_ = 0;
	float median_ = 0;
	bool active_ = false;
	int run_ = 0;
	// The last frame kept was slow: the next one says whether it was a hitch or the screen went idle.
	bool pending_ = false;
	uint64_t hitches_ = 0;
};

// The nearest-rank percentile (p from 0 to 100) of `values`, which it reorders; 0 when there are none.
float Percentile(std::vector<float> &values, double p);

struct FrameSummary {
	int frames = 0;
	int hitches = 0;
	// The share of the window the game was drawing continuously (0 to 1).
	float activeShare = 0;
	float avgFps = 0, avgMs = 0, medianMs = 0, minMs = 0, maxMs = 0;
	// The 1% and 0.1% lows: the FPS of the 99th and 99.9th percentile frame times (1000 / p99 and 1000 / p99.9).
	float p99Ms = 0, p999Ms = 0, low1Fps = 0, low01Fps = 0;
	// The last second only.
	float nowFps = 0, nowMs = 0;
};

// The counted frames of `frames` (oldest first) that arrived between fromMs and toMs. `scratch` is reused between calls.
FrameSummary Summarize(const std::vector<FrameRecord> &frames, double fromMs, double toMs, std::vector<float> &scratch);

enum ColumnFlags : uint8_t {
	kColumnFrames = 1,
	kColumnHitch = 2,
	// Only frames of an idle screen, or none while the game sent nothing.
	kColumnIdle = 4,
	kColumnEvent = 8,
};
// A long frame fills every column it lasted through, so a one-second freeze is as wide as a second of the graph.
struct GraphColumn {
	float maxMs = 0;
	float avgMs = 0;
	uint16_t count = 0;
	uint8_t flags = 0;
	uint8_t event = 0;
};

enum EventKind : uint8_t { kEventNone = 0, kEventLoad, kEventStart, kEventEnd, kEventDesync, kEventRejoin, kEventCapture };
struct PerfEvent {
	double t = 0;
	uint8_t kind = kEventNone;
};
// The event's name in the capture's file.
const char *EventName(int kind);

// `columns` columns over fromMs..toMs, the newest on the right; `out` must hold `columns` entries.
void BuildColumns(const std::vector<FrameRecord> &frames, const std::vector<PerfEvent> &events, double fromMs, double toMs, int columns,
	GraphColumn *out);

// The share of `cores` cores (0 to 100) busy for `busy` out of `wall`, in any one unit.
float CpuPercent(uint64_t busy, uint64_t wall, int cores);

// Rolling CPU shares, computed over at least `minWindow` of wall time so the 15.6 ms granularity of Windows' times stays small.
class CpuMeter {
public:
	explicit CpuMeter(uint64_t minWindow) : minWindow_(minWindow) {}
	// Totals since any fixed start, in one unit. True when the shares were computed again.
	bool Update(uint64_t wall, uint64_t process, uint64_t thread, int cores);
	float process = -1, thread = -1;

private:
	uint64_t minWindow_;
	bool have_ = false;
	uint64_t wall_ = 0, process_ = 0, thread_ = 0;
};

// The capture's file: `header` and a summary in lines that start with '#', then one line per frame, with '.' decimals.
std::string CaptureCsv(const std::vector<FrameRecord> &frames, const std::vector<PerfEvent> &events, double fromMs, double toMs,
	const FrameSummary &s, const std::string &header);
// The summary of a capture in one line for the log.
std::string SummaryLine(const FrameSummary &s);

// Marks for the graph from any thread: a load, a match that starts or ends, a desync, a rejoin. Times from QueryPerformanceCounter.
void NoteEvent(int kind);
// The events after `fromMs` (QpcMs clock), oldest first, into `out`.
void EventsSince(double fromMs, std::vector<PerfEvent> &out);
// QueryPerformanceCounter in milliseconds.
double QpcMs();

}
