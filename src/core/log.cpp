// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <share.h>
#include <string>

#include "core/log.h"
#include "core/logcore.h"
#include "core/logfiles.h"
#include "core/sysinfo.h"
#include "core/version.h"

namespace {

class FileSink : public LogSink {
public:
	bool Open(const std::string &path)
	{
		file_ = _fsopen(path.c_str(), "w", _SH_DENYWR);
		return file_ != nullptr;
	}
	void Write(const char *data, size_t size) override
	{
		if (!file_) return;
		fwrite(data, 1, size, file_);
		fflush(file_);
	}
	void Close()
	{
		if (file_) fclose(file_);
		file_ = nullptr;
	}

private:
	FILE *file_ = nullptr;
};

LogTime Now()
{
	SYSTEMTIME s;
	GetLocalTime(&s);
	LogTime t;
	t.year = s.wYear;
	t.month = s.wMonth;
	t.day = s.wDay;
	t.hour = s.wHour;
	t.minute = s.wMinute;
	t.second = s.wSecond;
	t.ms = s.wMilliseconds;
	return t;
}

FileSink g_sink;
LogCore *g_core = nullptr;
LogPaths g_paths;
std::string g_current;
std::string g_previous;
bool g_ownsLock = false;
DWORD g_started = 0;
volatile LONG g_shut = 0;
LogSettings g_settings;

std::string GameDirectory()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

std::string UtcOffsetText()
{
	TIME_ZONE_INFORMATION tz;
	const DWORD kind = GetTimeZoneInformation(&tz);
	const LONG bias = tz.Bias + (kind == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias);
	char text[16];
	sprintf_s(text, "UTC%c%02ld:%02ld", bias <= 0 ? '+' : '-', labs(bias) / 60, labs(bias) % 60);
	return text;
}

LogSettings ReadSettings(const std::string &gameDir)
{
	LogSettings settings;
	if (IsBetaVersion(ModVersionText())) settings.channels = BetaLogChannels();
	const std::string ini = gameDir + "\\imperivm-mod.ini";
	char text[512] = {};
	GetPrivateProfileStringA("Debug", "LogLevel", "info", text, sizeof(text), ini.c_str());
	ParseLogLevel(text, settings.level);
	GetPrivateProfileStringA("Debug", "LogChannels", "", text, sizeof(text), ini.c_str());
	for (const auto &c : ParseLogChannels(text)) settings.channels[c.first] = c.second;
	return settings;
}

void WriteHeader(const LogFilesState &state, const LogSettings &settings)
{
	SYSTEMTIME s;
	GetLocalTime(&s);
	LogI("session", "Imperivm Resurrected %s, pid %lu", ModVersionText(), GetCurrentProcessId());
	LogI("session", "build %s", BuildStampText(BuildStamp()).c_str());
	LogI("session", "started %04d-%02d-%02d %02d:%02d:%02d (%s)", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, s.wSecond,
		UtcOffsetText().c_str());
	if (state.otherInstance) LogW("session", "another instance of the game is running: this log is %s", g_current.c_str());
	else if (state.previousUnclean) LogW("session", "the previous session DID NOT END CLEANLY; its log is kept as Logs\\imperivm\\imperivm-mod.crashed.log");
	else if (!state.previousLog.empty()) LogI("session", "the previous session ended normally");
	else LogI("session", "no previous session on record");
	if (!state.gameCrashReport.empty()) LogW("session", "the game wrote its own crash report: Logs\\%s", state.gameCrashReport.c_str());
	LogI("session", "system: %s", DescribeWindows().c_str());
	LogI("session", "system: %s; %s; language %s", DescribeCpu().c_str(), DescribeMemory().c_str(), DescribeLanguage().c_str());
	for (const std::string &monitor : DescribeMonitors()) LogI("display", "%s", monitor.c_str());
	std::string channels;
	for (const auto &c : settings.channels) channels += (channels.empty() ? "" : ", ") + c.first + ":" + LogLevelName(c.second);
	LogI("session", "log level %s%s%s; the last %d sessions are kept in Logs\\imperivm\\", LogLevelName(settings.level), channels.empty() ? "" : " with ",
		channels.c_str(), kLogFilesKept);
}

}

void LogInit()
{
	g_started = GetTickCount();
	g_paths = MakeLogPaths(GameDirectory());
	const LogFilesState state = PrepareLogFiles(g_paths);
	g_current = state.otherInstance ? g_paths.ForProcess(GetCurrentProcessId()) : g_paths.current;
	g_previous = state.previousLog;
	if (!g_sink.Open(g_current)) return;
	g_core = new LogCore(&g_sink, Now, [] { return static_cast<unsigned>(GetCurrentThreadId()); }, GetCurrentThreadId());
	g_settings = ReadSettings(g_paths.gameDir);
	const LogSettings &settings = g_settings;
	g_core->Configure(settings);
	if (!state.otherInstance) g_ownsLock = WriteSessionLock(g_paths);
	WriteHeader(state, settings);
}

void LogShutdown()
{
	if (!g_core || InterlockedExchange(&g_shut, 1)) return;
	char summary[128];
	sprintf_s(summary, "ended cleanly after %lu s; %d warnings, %d errors", (GetTickCount() - g_started) / 1000, g_core->Count(kLogWarn),
		g_core->Count(kLogError) + g_core->Count(kLogFatal));
	g_core->Finish("session", summary);
	g_sink.Close();
	if (g_ownsLock) RemoveSessionLock(g_paths);
}

void Log(const char *fmt, ...)
{
	if (!g_core) return;
	va_list args;
	va_start(args, fmt);
	g_core->WriteLegacy(fmt, args);
	va_end(args);
}

#define IMPERIVM_LOG_AT(name, level)                     \
	void name(const char *channel, const char *fmt, ...) \
	{                                                    \
		if (!g_core) return;                             \
		va_list args;                                    \
		va_start(args, fmt);                             \
		g_core->Write(level, channel, fmt, fmt, args);   \
		va_end(args);                                    \
	}

void LogAt(LogLevel level, const char *channel, const char *fmt, ...)
{
	if (!g_core) return;
	va_list args;
	va_start(args, fmt);
	g_core->Write(level, channel, fmt, fmt, args);
	va_end(args);
}

IMPERIVM_LOG_AT(LogT, kLogTrace)
IMPERIVM_LOG_AT(LogD, kLogDebug)
IMPERIVM_LOG_AT(LogI, kLogInfo)
IMPERIVM_LOG_AT(LogW, kLogWarn)
IMPERIVM_LOG_AT(LogE, kLogError)
IMPERIVM_LOG_AT(LogF, kLogFatal)

void LogEvery(LogLevel level, const char *channel, unsigned intervalMs, const char *fmt, ...)
{
	if (!g_core || !g_core->Due(fmt, intervalMs)) return;
	va_list args;
	va_start(args, fmt);
	g_core->Write(level, channel, fmt, fmt, args);
	va_end(args);
}

void LogOnce(LogLevel level, const char *channel, const char *fmt, ...)
{
	if (!g_core || !g_core->FirstTime(fmt)) return;
	va_list args;
	va_start(args, fmt);
	g_core->Write(level, channel, fmt, fmt, args);
	va_end(args);
}

bool LogEnabled(LogLevel level, const char *channel)
{
	return g_core && g_core->Enabled(level, channel);
}

const char *LogLevelName(LogLevel level)
{
	static const char *const kNames[] = {"trace", "debug", "info", "warn", "error", "fatal"};
	return kNames[level];
}

void LogSetLevel(LogLevel level)
{
	if (!g_core || level == g_settings.level) return;
	g_settings.level = level;
	g_core->Configure(g_settings);
	LogI("session", "log level %s", LogLevelName(level));
}

void LogDumpContext(const char *reason)
{
	if (g_core) g_core->DumpContext(reason);
}

const char *LogGameDir()
{
	return g_paths.gameDir.c_str();
}

const char *LogFilePath()
{
	return g_current.c_str();
}

const char *LogPreviousFilePath()
{
	return g_previous.c_str();
}
