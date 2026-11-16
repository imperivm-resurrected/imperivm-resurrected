// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/logcore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "core/strutil.h"

namespace {

const size_t kLineBuffer = kLogMaxLine + 192;
const size_t kRingSlots = 2048;
const size_t kFloodSlots = 32;
const int kMaxDumps = 8;
const char kLetters[] = "TDIWEF";

using strutil::Lower;

std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
	while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
	return s.substr(a, b - a);
}

size_t FormatMessage(char *out, size_t cap, const char *fmt, va_list args)
{
	int n = vsnprintf(out, cap, fmt, args);
	if (n < 0) n = 0;
	size_t length = static_cast<size_t>(n);
	if (length >= cap) length = cap - 1;
	if (length > kLogMaxLine) {
		length = kLogMaxLine - 3;
		memcpy(out + length, "...", 3);
		length += 3;
	}
	for (size_t i = 0; i < length; i++) {
		if (out[i] == '\n' || out[i] == '\r') out[i] = ' ';
	}
	out[length] = 0;
	return length;
}

}

bool ParseLogLevel(const std::string &name, LogLevel &out)
{
	const std::string n = Lower(Trim(name));
	static const struct {
		const char *name;
		LogLevel level;
	} kNames[] = {{"trace", kLogTrace}, {"debug", kLogDebug}, {"info", kLogInfo}, {"warn", kLogWarn}, {"warning", kLogWarn},
		{"error", kLogError}, {"fatal", kLogFatal}};
	for (const auto &e : kNames) {
		if (n == e.name) {
			out = e.level;
			return true;
		}
	}
	return false;
}

std::map<std::string, LogLevel> ParseLogChannels(const std::string &text)
{
	std::map<std::string, LogLevel> out;
	size_t from = 0;
	while (from <= text.size()) {
		size_t to = text.find(',', from);
		if (to == std::string::npos) to = text.size();
		const std::string item = text.substr(from, to - from);
		const size_t colon = item.find(':');
		LogLevel level;
		if (colon != std::string::npos && ParseLogLevel(item.substr(colon + 1), level)) {
			const std::string channel = Lower(Trim(item.substr(0, colon)));
			if (!channel.empty()) out[channel] = level;
		}
		from = to + 1;
	}
	return out;
}

std::map<std::string, LogLevel> BetaLogChannels()
{
	std::map<std::string, LogLevel> out;
	for (const char *channel : {"steam", "lobby", "room", "match", "net", "rejoin", "spectator", "content-share"}) out[channel] = kLogDebug;
	return out;
}

bool IsBetaVersion(const std::string &version)
{
	return version.compare(0, 2, "0.") == 0;
}

char LogLevelLetter(LogLevel level)
{
	return kLetters[static_cast<int>(level) % 6];
}

LegacyLine ParseLegacyLine(const char *fmt, const char *text)
{
	LegacyLine r;
	r.channel = "log";
	struct {
		const char *word;
		const char *channel;
		LogLevel level;
	} static const kSpecial[] = {{"CRASH", "crash", kLogFatal}, {"HANG", "crash", kLogError}, {"EXCEPTION", "crash", kLogError},
		{"DESYNC", "match", kLogError}};
	for (const auto &s : kSpecial) {
		if (strncmp(fmt, s.word, strlen(s.word)) == 0) {
			r.channel = s.channel;
			r.level = s.level;
			return r;
		}
	}
	if (!isalpha(static_cast<unsigned char>(text[0])) || fmt[0] == '%') return r;
	const size_t literal = strcspn(fmt, "%");
	size_t i = 0;
	while (i < 32 && (isalnum(static_cast<unsigned char>(text[i])) || text[i] == ' ' || text[i] == '+' || text[i] == '.' || text[i] == '-')) i++;
	size_t nameEnd = i;
	while (nameEnd > 0 && text[nameEnd - 1] == ' ') nameEnd--;
	if (nameEnd < 2 || nameEnd > literal) return r;
	size_t skip = 0;
	std::string qualifier;
	if (text[i] == ':') {
		skip = i + 1;
	}
	else if (text[i] == '(' && i > 0 && text[i - 1] == ' ') {
		const char *close = strchr(text + i, ')');
		if (!close || close - (text + i) > 82 || close[1] != ':') return r;
		qualifier.assign(text + i + 1, static_cast<size_t>(close - (text + i + 1)));
		skip = static_cast<size_t>(close - text) + 2;
	}
	else {
		return r;
	}
	if (text[skip] == ' ') skip++;
	std::string channel = Lower(std::string(text, nameEnd));
	for (char &c : channel) {
		if (c == ' ' || c == '+') c = '-';
	}
	r.channel = channel;
	r.skip = skip;
	r.qualifier = qualifier;
	return r;
}

LogCore::LogCore(LogSink *sink, std::function<LogTime()> clock, std::function<unsigned()> threadId, unsigned mainThread)
	: sink_(sink), clock_(std::move(clock)), threadId_(std::move(threadId)), mainThread_(mainThread)
{
	Slot empty = {};
	ring_.assign(kRingSlots, empty);
	flood_.resize(kFloodSlots);
}

void LogCore::Configure(const LogSettings &settings)
{
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	settings_ = settings;
}

LogLevel LogCore::EffectiveLevel(const char *channel) const
{
	auto it = settings_.channels.find(channel);
	return it != settings_.channels.end() ? it->second : settings_.level;
}

bool LogCore::Enabled(LogLevel level, const char *channel)
{
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	return level >= EffectiveLevel(channel);
}

int64_t LogCore::NowMs(const LogTime &t) const
{
	return ((static_cast<int64_t>(t.hour) * 60 + t.minute) * 60 + t.second) * 1000 + t.ms;
}

size_t LogCore::FormatLine(char *out, size_t cap, const LogTime &t, LogLevel level, const char *channel, const char *message, size_t length)
{
	int n = snprintf(out, cap, "[%02d:%02d:%02d.%03d] %c %s", t.hour, t.minute, t.second, t.ms, LogLevelLetter(level), channel);
	if (n < 0) n = 0;
	size_t used = std::min(static_cast<size_t>(n), cap - 1);
	const unsigned tid = threadId_ ? threadId_() : 0;
	if (tid != 0 && tid != mainThread_) {
		const int m = snprintf(out + used, cap - used, "#%u", tid);
		if (m > 0) used = std::min(used + static_cast<size_t>(m), cap - 1);
	}
	if (used + 2 < cap) {
		out[used++] = ':';
		out[used++] = ' ';
	}
	const size_t room = cap - 1 - used;
	const size_t copy = std::min(length, room);
	memcpy(out + used, message, copy);
	used += copy;
	out[used] = 0;
	return used;
}

void LogCore::EmitRaw(const char *data, size_t size)
{
	if (!sink_ || size == 0) return;
	sink_->Write(data, size);
	bytes_ += size;
}

void LogCore::EmitNote(LogLevel level, const char *channel, const char *format, ...)
{
	char message[kLineBuffer];
	va_list args;
	va_start(args, format);
	const size_t length = FormatMessage(message, sizeof(message), format, args);
	va_end(args);
	char line[kLineBuffer + 64];
	size_t n = FormatLine(line, sizeof(line) - 1, clock_(), level, channel, message, length);
	line[n++] = '\n';
	EmitRaw(line, n);
}

void LogCore::FlushRepeatLocked()
{
	if (repeat_ > 0) {
		EmitNote(kLogInfo, "log", "(last message repeated %d times)", repeat_);
		repeat_ = 0;
	}
}

void LogCore::FlushFloodLocked()
{
	for (Flood &f : flood_) {
		if (f.key && f.suppressed > 0) {
			EmitNote(f.level, f.channel, "(%d more like this suppressed)", f.suppressed);
			f.suppressed = 0;
		}
	}
}

void LogCore::DumpLocked(const char *reason, bool skipNewest)
{
	if (dumps_ >= kMaxDumps || ringCount_ == 0) return;
	const size_t start = (ringNext_ + ring_.size() - ringCount_) % ring_.size();
	const size_t last = skipNewest ? ringCount_ - 1 : ringCount_;
	unsigned pending = 0;
	for (size_t k = 0; k < last; k++) {
		if (!ring_[(start + k) % ring_.size()].written) pending++;
	}
	if (pending == 0) return;
	dumps_++;
	EmitNote(kLogInfo, "context", "--- context before this point (%s): %u lines below the log level ---", reason, pending);
	for (size_t k = 0; k < last; k++) {
		Slot &s = ring_[(start + k) % ring_.size()];
		if (s.written) continue;
		EmitRaw(s.text, s.size);
		EmitRaw("\n", 1);
		s.written = true;
	}
	EmitNote(kLogInfo, "context", "--- end of context ---");
}

void LogCore::WriteLocked(LogLevel level, const char *channel, const void *key, const char *message, size_t length)
{
	const LogTime t = clock_();
	counts_[level]++;
	const int day = t.year * 10000 + t.month * 100 + t.day;
	if (lastDay_ != 0 && day != lastDay_) {
		lastDay_ = day;
		EmitNote(kLogInfo, "clock", "date is now %04d-%02d-%02d", t.year, t.month, t.day);
	}
	lastDay_ = day;

	char line[kLineBuffer + 64];
	const size_t n = FormatLine(line, sizeof(line) - 1, t, level, channel, message, length);
	const size_t mine = ringNext_;
	{
		Slot &s = ring_[mine];
		size_t keep = std::min(n, sizeof(s.text) - 1);
		memcpy(s.text, line, keep);
		if (keep < n) memcpy(s.text + keep - 3, "...", 3);
		s.text[keep] = 0;
		s.size = static_cast<uint16_t>(keep);
		s.written = false;
		ringNext_ = (ringNext_ + 1) % ring_.size();
		if (ringCount_ < ring_.size()) ringCount_++;
	}
	if (level < EffectiveLevel(channel)) return;

	if (bytes_ >= settings_.maxFileBytes) {
		if (!capNoticed_) {
			capNoticed_ = true;
			EmitNote(kLogFatal, "log", "the log reached its size limit (%u KB): only fatal errors are written from now on",
				static_cast<unsigned>(settings_.maxFileBytes / 1024));
		}
		return;
	}
	if (bytes_ >= settings_.maxFileBytes / 5 * 4 && level <= kLogDebug) return;

	if (key) {
		const int64_t now = NowMs(t);
		Flood *slot = nullptr;
		for (Flood &f : flood_) {
			if (f.key == key) {
				slot = &f;
				break;
			}
		}
		if (!slot) {
			slot = &flood_[0];
			for (Flood &f : flood_) {
				if (!f.key) {
					slot = &f;
					break;
				}
				if (f.window < slot->window) slot = &f;
			}
			if (slot->key && slot->suppressed > 0) EmitNote(slot->level, slot->channel, "(%d more like this suppressed)", slot->suppressed);
			*slot = Flood();
			slot->key = key;
			slot->window = now;
			strncpy_s(slot->channel, sizeof(slot->channel), channel, _TRUNCATE);
			slot->level = level;
		}
		if (now - slot->window >= 1000 || now < slot->window) {
			if (slot->suppressed > 0) EmitNote(slot->level, slot->channel, "(%d more like this suppressed)", slot->suppressed);
			slot->window = now;
			slot->count = 0;
			slot->suppressed = 0;
		}
		slot->count++;
		if (slot->count > settings_.floodPerSecond) {
			slot->suppressed++;
			ring_[mine].written = true;
			return;
		}
	}

	std::string kind;
	kind.reserve(length + 24);
	kind.push_back(static_cast<char>('0' + level));
	kind += channel;
	kind.push_back('\x01');
	kind.append(message, length);
	if (kind == lastKind_) {
		repeat_++;
		ring_[mine].written = true;
		return;
	}
	FlushRepeatLocked();
	lastKind_ = kind;

	if (level >= kLogError) DumpLocked(level == kLogError ? "error" : "fatal error", true);
	line[n] = '\n';
	EmitRaw(line, n + 1);
	ring_[mine].written = true;
}

void LogCore::WriteFatal(const char *channel, const char *fmt, va_list args)
{
	char message[kLineBuffer];
	const size_t length = FormatMessage(message, sizeof(message), fmt, args);
	const bool locked = mutex_.try_lock_for(std::chrono::milliseconds(250));
	counts_[kLogFatal]++;
	DumpLocked("fatal error", false);
	char line[kLineBuffer + 64];
	size_t n = FormatLine(line, sizeof(line) - 1, clock_(), kLogFatal, channel, message, length);
	line[n++] = '\n';
	EmitRaw(line, n);
	if (locked) mutex_.unlock();
}

void LogCore::Write(LogLevel level, const char *channel, const void *key, const char *fmt, va_list args)
{
	if (level == kLogFatal) {
		WriteFatal(channel, fmt, args);
		return;
	}
	char message[kLineBuffer];
	const size_t length = FormatMessage(message, sizeof(message), fmt, args);
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	WriteLocked(level, channel, key, message, length);
}

void LogCore::WriteLegacy(const char *fmt, va_list args)
{
	char text[kLineBuffer];
	const size_t length = FormatMessage(text, sizeof(text), fmt, args);
	const LegacyLine parsed = ParseLegacyLine(fmt, text);
	std::string message = parsed.qualifier.empty() ? std::string(text + parsed.skip, length - parsed.skip)
												   : "(" + parsed.qualifier + ") " + std::string(text + parsed.skip, length - parsed.skip);
	if (parsed.level == kLogFatal) {
		const bool locked = mutex_.try_lock_for(std::chrono::milliseconds(250));
		counts_[kLogFatal]++;
		DumpLocked("fatal error", false);
		char line[kLineBuffer + 64];
		size_t n = FormatLine(line, sizeof(line) - 1, clock_(), kLogFatal, parsed.channel.c_str(), message.c_str(), message.size());
		line[n++] = '\n';
		EmitRaw(line, n);
		if (locked) mutex_.unlock();
		return;
	}
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	WriteLocked(parsed.level, parsed.channel.c_str(), fmt, message.c_str(), message.size());
}

bool LogCore::Due(const void *key, unsigned intervalMs)
{
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	const int64_t now = NowMs(clock_());
	auto it = every_.find(key);
	if (it == every_.end() || now < it->second || now - it->second >= static_cast<int64_t>(intervalMs)) {
		every_[key] = now;
		return true;
	}
	return false;
}

bool LogCore::FirstTime(const void *key)
{
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	return once_.insert(key).second;
}

void LogCore::DumpContext(const char *reason)
{
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	FlushRepeatLocked();
	DumpLocked(reason, false);
}

void LogCore::Finish(const char *channel, const char *message)
{
	const bool locked = mutex_.try_lock_for(std::chrono::milliseconds(300));
	FlushRepeatLocked();
	FlushFloodLocked();
	if (message && *message) EmitNote(kLogInfo, channel, "%s", message);
	if (locked) mutex_.unlock();
}

size_t LogCore::BytesWritten()
{
	std::lock_guard<std::recursive_timed_mutex> lock(mutex_);
	return bytes_;
}

int LogCore::Count(LogLevel level)
{
	return counts_[level];
}
