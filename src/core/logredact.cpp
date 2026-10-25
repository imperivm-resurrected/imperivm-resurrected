// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/logredact.h"

#include <algorithm>

namespace {

const size_t kMinNameLength = 3;
const char kSteamIdPrefix[] = "7656119";
const size_t kSteamIdLength = 17;
// A player called like the game or a service would erase those words from
// every line, paths included, and the report would be unreadable.
const char *const kNotNames[] = {"imperivm", "resurrected", "steam", "discord", "gamebanana", "windows"};

char Lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
}

bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool IsWord(char c)
{
	return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool MatchesAt(const std::string &text, size_t at, const std::string &lower)
{
	if (at + lower.size() > text.size()) return false;
	for (size_t k = 0; k < lower.size(); k++) {
		if (Lower(text[at + k]) != lower[k]) return false;
	}
	return true;
}

bool Boundary(const std::string &text, size_t end)
{
	return end >= text.size() || !IsWord(text[end]);
}

size_t DigitRun(const std::string &text, size_t from)
{
	size_t j = from;
	while (j < text.size() && IsDigit(text[j])) j++;
	return j - from;
}

bool IsPrivateRange(const std::string &address)
{
	return address.compare(0, 4, "127.") == 0 || address == "0.0.0.0";
}

size_t MatchAddress(const std::string &text, size_t at, std::string &address)
{
	size_t pos = at;
	for (int part = 0; part < 4; part++) {
		const size_t digits = DigitRun(text, pos);
		if (digits < 1 || digits > 3) return 0;
		if (std::stoi(text.substr(pos, digits)) > 255) return 0;
		pos += digits;
		if (part < 3) {
			if (pos >= text.size() || text[pos] != '.') return 0;
			pos++;
		}
	}
	if (pos < text.size() && IsDigit(text[pos])) return 0;
	if (pos + 1 < text.size() && text[pos] == '.' && IsDigit(text[pos + 1])) return 0;
	address = text.substr(at, pos - at);
	return pos - at;
}

}

void LogRedactor::AddName(const std::string &name, const std::string &label)
{
	if (name.size() < kMinNameLength) return;
	std::string lower = name;
	for (char &c : lower) c = Lower(c);
	for (const char *word : kNotNames) {
		if (lower == word) return;
	}
	for (const Known &k : names_) {
		if (k.lower == lower) return;
	}
	Known known;
	known.lower = lower;
	known.label = label.empty() ? "player" + std::to_string(++unnamed_) : label;
	const auto place = std::find_if(names_.begin(), names_.end(), [&](const Known &k) { return k.lower.size() < known.lower.size(); });
	names_.insert(place, known);
}

void LogRedactor::AddUserName(const std::string &name)
{
	if (name.empty()) return;
	std::string lower = name;
	for (char &c : lower) c = Lower(c);
	userPath_ = lower;
	AddName(name, "user");
}

std::string LogRedactor::Apply(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	const size_t n = text.size();
	size_t i = 0;
	while (i < n) {
		const char c = text[i];
		const char before = i ? text[i - 1] : '\0';
		if (IsDigit(c)) {
			const bool insideNumber = IsDigit(before) || (before == '.' && i >= 2 && IsDigit(text[i - 2]));
			if (!insideNumber) {
				const size_t run = DigitRun(text, i);
				if (run == kSteamIdLength && text.compare(i, sizeof(kSteamIdPrefix) - 1, kSteamIdPrefix) == 0) {
					const std::string id = text.substr(i, run);
					const int number = steamIds_.emplace(id, static_cast<int>(steamIds_.size()) + 1).first->second;
					out += "<steamid" + std::to_string(number) + ">";
					replacements_++;
					i += run;
					continue;
				}
				std::string address;
				const size_t length = MatchAddress(text, i, address);
				if (length && !IsPrivateRange(address)) {
					const int number = addresses_.emplace(address, static_cast<int>(addresses_.size()) + 1).first->second;
					out += "<ip" + std::to_string(number) + ">";
					replacements_++;
					i += length;
					continue;
				}
			}
		}
		if ((c == '\\' || c == '/') && !userPath_.empty() && MatchesAt(text, i + 1, "users") && i + 6 < n && (text[i + 6] == '\\' || text[i + 6] == '/') &&
			MatchesAt(text, i + 7, userPath_) && Boundary(text, i + 7 + userPath_.size())) {
			out.append(text, i, 7);
			out += "<user>";
			replacements_++;
			i += 7 + userPath_.size();
			continue;
		}
		if (!IsWord(before)) {
			bool replaced = false;
			for (const Known &k : names_) {
				if (Lower(c) != k.lower[0]) continue;
				if (!MatchesAt(text, i, k.lower) || !Boundary(text, i + k.lower.size())) continue;
				out += "<" + k.label + ">";
				replacements_++;
				i += k.lower.size();
				replaced = true;
				break;
			}
			if (replaced) continue;
		}
		out += c;
		i++;
	}
	return out;
}

std::string RedactIniSecrets(const std::string &ini)
{
	static const char *const kWords[] = {"key", "token", "password", "secret"};
	std::string out;
	out.reserve(ini.size());
	size_t start = 0;
	while (start <= ini.size()) {
		size_t end = ini.find('\n', start);
		const bool last = end == std::string::npos;
		if (last) end = ini.size();
		std::string line = ini.substr(start, end - start);
		const size_t equals = line.find('=');
		if (equals != std::string::npos && line[0] != ';' && line[0] != '[') {
			std::string key = line.substr(0, equals);
			for (char &c : key) c = Lower(c);
			bool secret = false;
			for (const char *word : kWords) secret = secret || key.find(word) != std::string::npos;
			const size_t endOfValue = line.find_last_not_of("\r");
			const bool hasValue = endOfValue != std::string::npos && endOfValue > equals;
			if (secret && hasValue) line = line.substr(0, equals + 1) + "<hidden>" + (line.back() == '\r' ? "\r" : "");
		}
		out += line;
		if (last) break;
		out += '\n';
		start = end + 1;
	}
	return out;
}
