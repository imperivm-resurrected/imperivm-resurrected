// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include "core/perfstats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace perf {

FrameTracker::FrameTracker(size_t capacity) : ring_(capacity ? capacity : 1) {}

void FrameTracker::Reset()
{
	head_ = count_ = 0;
	last_ = -1;
	ClearMedian();
	active_ = false;
	run_ = 0;
	pending_ = false;
	hitches_ = 0;
}

void FrameTracker::Push(const FrameRecord &r)
{
	ring_[head_] = r;
	head_ = (head_ + 1) % ring_.size();
	if (count_ < ring_.size()) count_++;
}

void FrameTracker::PushMedian(float ms)
{
	recent_[recentPos_] = ms;
	recentPos_ = (recentPos_ + 1) % kMedianFrames;
	if (recentCount_ < kMedianFrames) recentCount_++;
	float sorted[kMedianFrames];
	std::copy(recent_, recent_ + recentCount_, sorted);
	std::nth_element(sorted, sorted + recentCount_ / 2, sorted + recentCount_);
	median_ = sorted[recentCount_ / 2];
}

void FrameTracker::ClearMedian()
{
	recentCount_ = recentPos_ = 0;
	median_ = 0;
}

float FrameTracker::MedianMs() const
{
	return median_;
}

float FrameTracker::HitchThresholdMs() const
{
	return std::max(median_ * kHitchRatio, median_ + kHitchExtraMs);
}

void FrameTracker::ResolvePending(float nextMs, bool continuous)
{
	pending_ = false;
	if (!count_) return;
	FrameRecord &slow = ring_[(head_ + ring_.size() - 1) % ring_.size()];
	if (nextMs <= HitchThresholdMs()) {
		// The game went back to its pace: the slow frame was a hitch.
		slow.flags = kFrameCounted | kFrameHitch;
		hitches_++;
		PushMedian(slow.ms);
	}
	else if (continuous) {
		// Still slow: the match got slower, which the FPS and the lows show; the median follows it.
		slow.flags = kFrameCounted;
		PushMedian(slow.ms);
	}
	else {
		active_ = false;
		run_ = 0;
		ClearMedian();
	}
}

bool FrameTracker::AddFrame(double nowMs, bool continuous)
{
	if (last_ < 0) {
		last_ = nowMs;
		return true;
	}
	const double gap = nowMs - last_;
	if (gap < kCoalesceMs) return false;
	last_ = nowMs;
	FrameRecord r;
	r.t = nowMs;
	r.ms = static_cast<float>(gap);
	if (r.ms >= kSuspendMs || (!continuous && r.ms >= kIdleGapMs)) {
		if (pending_) ResolvePending(r.ms, false);
		pending_ = false;
		Push(r);
		active_ = false;
		run_ = 0;
		ClearMedian();
		return true;
	}
	if (pending_) ResolvePending(r.ms, continuous);
	if (!active_) {
		if (r.ms < kActiveMaxMs) {
			run_++;
			PushMedian(r.ms);
		}
		else {
			run_ = 0;
			ClearMedian();
		}
		// In a match the game draws all the time: its frames count from the first one.
		if (continuous || run_ >= kActiveRun) {
			active_ = true;
			r.flags = kFrameCounted;
			if (r.ms >= kActiveMaxMs) PushMedian(r.ms);
		}
		Push(r);
		return true;
	}
	if (recentCount_ >= kWarmupFrames && r.ms > HitchThresholdMs()) {
		Push(r);
		if (continuous) {
			pending_ = true;
			return true;
		}
		// Out of a match the game also draws on demand (the mouse moves, a button lights up): a slow frame there ends the run.
		active_ = false;
		run_ = 0;
		ClearMedian();
		return true;
	}
	r.flags = kFrameCounted;
	Push(r);
	PushMedian(r.ms);
	return true;
}

ScreenState FrameTracker::State(double nowMs, bool continuous) const
{
	if (last_ < 0) return continuous ? ScreenState::kStalled : ScreenState::kIdle;
	const double since = nowMs - last_;
	if (continuous) return since > kStallAfterMs ? ScreenState::kStalled : ScreenState::kActive;
	if (!active_) return ScreenState::kIdle;
	return since > kIdleAfterMs ? ScreenState::kIdle : ScreenState::kActive;
}

void FrameTracker::CopySince(double fromMs, std::vector<FrameRecord> &out) const
{
	out.clear();
	size_t first = count_;
	// Binary search: the ring is in order of time.
	size_t lo = 0, hi = count_;
	while (lo < hi) {
		const size_t mid = (lo + hi) / 2;
		if (At(mid).t > fromMs) hi = mid;
		else lo = mid + 1;
	}
	first = lo;
	for (size_t i = first; i < count_; i++) out.push_back(At(i));
}

float Percentile(std::vector<float> &values, double p)
{
	if (values.empty()) return 0;
	const double n = static_cast<double>(values.size());
	double rank = std::ceil(p / 100.0 * n - 1e-9);
	rank = std::max(1.0, std::min(n, rank));
	const auto nth = values.begin() + static_cast<std::ptrdiff_t>(rank - 1);
	std::nth_element(values.begin(), nth, values.end());
	return *nth;
}

FrameSummary Summarize(const std::vector<FrameRecord> &frames, double fromMs, double toMs, std::vector<float> &scratch)
{
	FrameSummary s;
	scratch.clear();
	double sum = 0, nowSum = 0;
	int nowFrames = 0;
	float minMs = 0, maxMs = 0;
	for (const FrameRecord &r : frames) {
		if (r.t <= fromMs || r.t > toMs || !(r.flags & kFrameCounted)) continue;
		if (scratch.empty() || r.ms < minMs) minMs = r.ms;
		if (scratch.empty() || r.ms > maxMs) maxMs = r.ms;
		scratch.push_back(r.ms);
		sum += r.ms;
		if (r.flags & kFrameHitch) s.hitches++;
		if (r.t > toMs - 1000.0) {
			nowSum += r.ms;
			nowFrames++;
		}
	}
	if (scratch.empty() || sum <= 0) return s;
	s.frames = static_cast<int>(scratch.size());
	s.avgMs = static_cast<float>(sum / s.frames);
	s.avgFps = static_cast<float>(1000.0 * s.frames / sum);
	s.minMs = minMs;
	s.maxMs = maxMs;
	s.activeShare = toMs > fromMs ? static_cast<float>(std::min(1.0, sum / (toMs - fromMs))) : 0;
	s.medianMs = Percentile(scratch, 50);
	s.p99Ms = Percentile(scratch, 99);
	s.p999Ms = Percentile(scratch, 99.9);
	s.low1Fps = s.p99Ms > 0 ? 1000.0f / s.p99Ms : 0;
	s.low01Fps = s.p999Ms > 0 ? 1000.0f / s.p999Ms : 0;
	if (nowFrames && nowSum > 0) {
		s.nowFps = static_cast<float>(1000.0 * nowFrames / nowSum);
		s.nowMs = static_cast<float>(nowSum / nowFrames);
	}
	return s;
}

void BuildColumns(const std::vector<FrameRecord> &frames, const std::vector<PerfEvent> &events, double fromMs, double toMs, int columns,
	GraphColumn *out)
{
	if (columns <= 0) return;
	for (int c = 0; c < columns; c++) out[c] = GraphColumn();
	if (toMs <= fromMs) return;
	const double span = (toMs - fromMs) / columns;
	// Column c holds the times in (from + c span, from + (c + 1) span], as a frame holds the time from the one before to its own.
	auto clamp = [&](double c) { return std::max(0, std::min(columns - 1, static_cast<int>(c))); };
	auto column = [&](double t) { return clamp(std::ceil((t - fromMs) / span) - 1); };
	for (const FrameRecord &r : frames) {
		if (r.t <= fromMs || r.t - r.ms > toMs) continue;
		const int last = column(r.t), first = std::min(last, clamp(std::floor((std::max(fromMs, r.t - r.ms) - fromMs) / span)));
		for (int c = first; c <= last; c++) {
			GraphColumn &g = out[c];
			if (r.flags & kFrameCounted) {
				g.maxMs = std::max(g.maxMs, r.ms);
				g.flags |= kColumnFrames;
				if (r.flags & kFrameHitch) g.flags |= kColumnHitch;
				if (c == last) {
					g.avgMs += r.ms;
					if (g.count < 0xffff) g.count++;
				}
			}
			else {
				g.flags |= kColumnIdle;
			}
		}
	}
	for (int c = 0; c < columns; c++) {
		if (out[c].count) out[c].avgMs /= out[c].count;
		else out[c].avgMs = out[c].maxMs;
	}
	for (const PerfEvent &e : events) {
		if (e.t <= fromMs || e.t > toMs) continue;
		GraphColumn &g = out[column(e.t)];
		g.flags |= kColumnEvent;
		g.event = e.kind;
	}
}

const char *EventName(int kind)
{
	switch (kind) {
	case kEventLoad: return "load";
	case kEventStart: return "match start";
	case kEventEnd: return "match end";
	case kEventDesync: return "desync";
	case kEventRejoin: return "rejoin";
	case kEventCapture: return "capture";
	default: return "";
	}
}

float CpuPercent(uint64_t busy, uint64_t wall, int cores)
{
	if (!wall || cores < 1) return 0;
	const double pct = 100.0 * static_cast<double>(busy) / (static_cast<double>(wall) * cores);
	return static_cast<float>(std::max(0.0, std::min(100.0, pct)));
}

bool CpuMeter::Update(uint64_t wall, uint64_t processTime, uint64_t threadTime, int cores)
{
	if (!have_ || wall < wall_) {
		have_ = true;
		wall_ = wall;
		process_ = processTime;
		thread_ = threadTime;
		return false;
	}
	const uint64_t dw = wall - wall_;
	if (dw < minWindow_) return false;
	process = CpuPercent(processTime >= process_ ? processTime - process_ : 0, dw, cores);
	thread = CpuPercent(threadTime >= thread_ ? threadTime - thread_ : 0, dw, 1);
	wall_ = wall;
	process_ = processTime;
	thread_ = threadTime;
	return true;
}

std::string SummaryLine(const FrameSummary &s)
{
	char text[256];
	sprintf_s(text, "%d frames, %.0f%% of the time drawing: mean %.1f FPS (%.2f ms), median %.2f ms, 1%% low %.1f FPS (p99 %.2f ms), "
		"0.1%% low %.1f FPS (p99.9 %.2f ms), longest %.1f ms, %d hitches", s.frames, s.activeShare * 100.0, s.avgFps, s.avgMs, s.medianMs,
		s.low1Fps, s.p99Ms, s.low01Fps, s.p999Ms, s.maxMs, s.hitches);
	return text;
}

std::string CaptureCsv(const std::vector<FrameRecord> &frames, const std::vector<PerfEvent> &events, double fromMs, double toMs,
	const FrameSummary &s, const std::string &header)
{
	std::string out;
	out.reserve(64 * (frames.size() + 16));
	size_t start = 0;
	while (start < header.size()) {
		size_t end = header.find('\n', start);
		if (end == std::string::npos) end = header.size();
		out += "# " + header.substr(start, end - start) + "\n";
		start = end + 1;
	}
	char line[160];
	sprintf_s(line, "# seconds %.1f, hitch: a frame over max(%.0fx median, median + %.0f ms) followed by one at the usual pace\n",
		(toMs - fromMs) / 1000.0, kHitchRatio, kHitchExtraMs);
	out += line;
	out += "# " + SummaryLine(s) + "\n";
	out += "time_s,frame_ms,counted,hitch,event\n";
	size_t e = 0;
	for (const FrameRecord &r : frames) {
		if (r.t <= fromMs || r.t > toMs) continue;
		for (; e < events.size() && events[e].t <= r.t; e++) {
			if (events[e].t <= fromMs) continue;
			sprintf_s(line, "%.4f,,,,%s\n", (events[e].t - fromMs) / 1000.0, EventName(events[e].kind));
			out += line;
		}
		sprintf_s(line, "%.4f,%.3f,%d,%d,\n", (r.t - fromMs) / 1000.0, r.ms, (r.flags & kFrameCounted) ? 1 : 0, (r.flags & kFrameHitch) ? 1 : 0);
		out += line;
	}
	for (; e < events.size(); e++) {
		if (events[e].t <= fromMs || events[e].t > toMs) continue;
		sprintf_s(line, "%.4f,,,,%s\n", (events[e].t - fromMs) / 1000.0, EventName(events[e].kind));
		out += line;
	}
	return out;
}

namespace {
std::mutex g_eventMx;
const size_t kEventsKept = 64;
PerfEvent g_events[kEventsKept];
size_t g_eventNext = 0, g_eventCount = 0;
}

double QpcMs()
{
	static const double freq = [] {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return static_cast<double>(f.QuadPart);
	}();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart) * 1000.0 / freq;
}

void NoteEvent(int kind)
{
	const double t = QpcMs();
	std::lock_guard<std::mutex> lock(g_eventMx);
	g_events[g_eventNext].t = t;
	g_events[g_eventNext].kind = static_cast<uint8_t>(kind);
	g_eventNext = (g_eventNext + 1) % kEventsKept;
	if (g_eventCount < kEventsKept) g_eventCount++;
}

void EventsSince(double fromMs, std::vector<PerfEvent> &out)
{
	out.clear();
	std::lock_guard<std::mutex> lock(g_eventMx);
	for (size_t i = 0; i < g_eventCount; i++) {
		const PerfEvent &e = g_events[(g_eventNext + kEventsKept - g_eventCount + i) % kEventsKept];
		if (e.t > fromMs) out.push_back(e);
	}
}

}
