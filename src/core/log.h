// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

enum LogLevel { kLogTrace = 0, kLogDebug, kLogInfo, kLogWarn, kLogError, kLogFatal };

void LogInit();
void LogShutdown();

void Log(const char *fmt, ...);

void LogAt(LogLevel level, const char *channel, const char *fmt, ...);
void LogT(const char *channel, const char *fmt, ...);
void LogD(const char *channel, const char *fmt, ...);
void LogI(const char *channel, const char *fmt, ...);
void LogW(const char *channel, const char *fmt, ...);
void LogE(const char *channel, const char *fmt, ...);
void LogF(const char *channel, const char *fmt, ...);

void LogEvery(LogLevel level, const char *channel, unsigned intervalMs, const char *fmt, ...);
void LogOnce(LogLevel level, const char *channel, const char *fmt, ...);

bool LogEnabled(LogLevel level, const char *channel);
void LogSetLevel(LogLevel level);
const char *LogLevelName(LogLevel level);
void LogDumpContext(const char *reason);

const char *LogGameDir();
const char *LogFilePath();
const char *LogPreviousFilePath();
