// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/chatlog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

ChatScope ChatScopeOfMask(unsigned mask, int sender, int &privateTo)
{
	privateTo = -1;
	mask &= kChatEveryone;
	if (mask == kChatEveryone) return kChatAll;
	const unsigned others = sender >= 0 && sender < 16 ? mask & ~(1u << sender) : mask;
	int count = 0, only = -1;
	for (int i = 0; i < 16; i++) {
		if (!(others & (1u << i))) continue;
		count++;
		only = i;
	}
	if (count == 1) {
		privateTo = only;
		return kChatPrivate;
	}
	return kChatAllies;
}

unsigned ChatMask(ChatTarget target, int local, unsigned enemies)
{
	const unsigned own = local >= 0 && local < 16 ? 1u << local : 0;
	switch (target.kind) {
	case kTargetAllies: return ((~enemies) & kChatEveryone) | own;
	case kTargetPlayer: return (target.player >= 0 && target.player < 16 ? 1u << target.player : 0) | own;
	default: return kChatEveryone;
	}
}

std::vector<ChatTarget> ChatTargets(bool hasAllies, const std::vector<int> &otherHumans)
{
	std::vector<ChatTarget> out;
	if (hasAllies) out.push_back({kTargetAllies, -1});
	out.push_back({kTargetAll, -1});
	for (int p : otherHumans) out.push_back({kTargetPlayer, p});
	return out;
}

int ChatDefaultTarget(const std::vector<ChatTarget> &targets, bool everyone)
{
	if (everyone) {
		const int all = ChatFindTarget(targets, {kTargetAll, -1});
		if (all >= 0) return all;
	}
	return 0;
}

int ChatCycle(int current, int count, bool back)
{
	if (count <= 0) return 0;
	if (current < 0 || current >= count) return 0;
	return (current + (back ? count - 1 : 1)) % count;
}

int ChatFindTarget(const std::vector<ChatTarget> &targets, ChatTarget wanted)
{
	for (size_t i = 0; i < targets.size(); i++) {
		if (targets[i] == wanted) return static_cast<int>(i);
	}
	return -1;
}

std::string ChatCleanText(const std::string &text)
{
	size_t b = 0, e = text.size();
	while (b < e && (text[b] == ' ' || text[b] == '\t')) b++;
	while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) e--;
	std::string out = text.substr(b, e - b);
	for (char &c : out) {
		if (static_cast<unsigned char>(c) < 0x20) c = ' ';
	}
	if (out.size() > kChatMaxText) {
		size_t cut = kChatMaxText;
		while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80) cut--;
		out.resize(cut);
	}
	return out;
}

bool ChatIsUtf8(const std::string &text)
{
	for (size_t i = 0; i < text.size();) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		size_t n = 0;
		if (c < 0x80) n = 0;
		else if (c >= 0xc2 && c <= 0xdf) n = 1;
		else if (c >= 0xe0 && c <= 0xef) n = 2;
		else if (c >= 0xf0 && c <= 0xf4) n = 3;
		else return false;
		if (i + n >= text.size() && n) return false;
		for (size_t k = 1; k <= n; k++) {
			if ((static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80) return false;
		}
		i += n + 1;
	}
	return true;
}

std::string ChatCommandOf(const std::string &text)
{
	if (text.size() < 2 || text[0] != '/' || text[1] == '/' || text[1] == ' ') return std::string();
	return text.substr(1);
}

std::string ChatClock(uint32_t ms)
{
	const uint32_t s = ms / 1000;
	char buf[24];
	if (s >= 3600) sprintf_s(buf, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
	else sprintf_s(buf, "%u:%02u", s / 60, s % 60);
	return buf;
}

int ChatLineAlpha(uint32_t ageMs)
{
	if (ageMs >= kChatShowMs) return 0;
	if (ageMs <= kChatShowMs - kChatFadeMs) return 255;
	return static_cast<int>((kChatShowMs - ageMs) * 255 / kChatFadeMs);
}

void ChatRecall::Push(const std::string &text)
{
	Reset();
	if (text.empty() || (!sent_.empty() && sent_.back() == text)) return;
	sent_.push_back(text);
	if (sent_.size() > 50) sent_.erase(sent_.begin());
}

bool ChatRecall::Older(std::string &text)
{
	if (sent_.empty()) return false;
	if (pos_ < 0) {
		draft_ = text;
		pos_ = static_cast<int>(sent_.size());
	}
	if (pos_ == 0) return false;
	pos_--;
	text = sent_[pos_];
	return true;
}

bool ChatRecall::Newer(std::string &text)
{
	if (pos_ < 0) return false;
	pos_++;
	if (pos_ >= static_cast<int>(sent_.size())) {
		text = draft_;
		Reset();
		return true;
	}
	text = sent_[pos_];
	return true;
}

void ChatRecall::Reset()
{
	pos_ = -1;
	draft_.clear();
}

void ChatLog::Add(ChatEntry e)
{
	e.serial = next_++;
	lines_.push_back(std::move(e));
	while (lines_.size() > kChatLogSize) lines_.pop_front();
}

void ChatLog::Clear()
{
	lines_.clear();
	next_++;
}

bool ChatLog::AnyFresh(uint32_t now) const
{
	return !lines_.empty() && now - lines_.back().arrivedMs < kChatShowMs;
}

const char *ChatPingPhrase(int type)
{
	switch (type) {
	case kPingAttack: return "Attack here!";
	case kPingDanger: return "Danger!";
	default: return "On my way";
	}
}

int ChatPingTypeOf(const std::string &text, bool location, const std::vector<std::string> (&phrases)[kPingTypes])
{
	if (!location) return -1;
	if (text.empty()) return kPingHere;
	for (int t = 0; t < kPingTypes; t++) {
		for (const std::string &p : phrases[t]) {
			if (!p.empty() && p == text) return t;
		}
	}
	return -1;
}

namespace {
const double kMenuAngles[kPingTypes] = {-90.0, 30.0, 150.0};
}

int ChatPingMenuPick(int dx, int dy, int deadZone)
{
	if (dx * dx + dy * dy <= deadZone * deadZone) return -1;
	const double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) * 180.0 / 3.14159265358979;
	int best = 0;
	double bestGap = 1e9;
	for (int t = 0; t < kPingTypes; t++) {
		double gap = std::fabs(angle - kMenuAngles[t]);
		if (gap > 180) gap = 360 - gap;
		if (gap < bestGap) {
			bestGap = gap;
			best = t;
		}
	}
	return best;
}

void ChatPingMenuOffset(int type, int radius, int &dx, int &dy)
{
	const double a = kMenuAngles[type >= 0 && type < kPingTypes ? type : 0] * 3.14159265358979 / 180.0;
	dx = static_cast<int>(std::lround(std::cos(a) * radius));
	dy = static_cast<int>(std::lround(std::sin(a) * radius));
}

ChatAnchor ChatClampAnchor(ChatAnchor a, int screenW, int width, int minBottom, int maxBottom)
{
	a.left = std::max(0, std::min(a.left, screenW - width));
	if (maxBottom < minBottom) maxBottom = minBottom;
	a.bottom = std::max(minBottom, std::min(a.bottom, maxBottom));
	return a;
}

int ChatToPermille(int value, int size)
{
	if (size <= 0) return 0;
	return std::max(0, std::min(1000, static_cast<int>((static_cast<long long>(value) * 1000 + size / 2) / size)));
}

int ChatFromPermille(int permille, int size) { return static_cast<int>((static_cast<long long>(permille) * size + 500) / 1000); }

void ChatRateLimit::Forget(uint32_t now)
{
	while (!times_.empty() && now - times_.front() >= window_) times_.pop_front();
}

bool ChatRateLimit::Allow(uint32_t now)
{
	Forget(now);
	if (static_cast<int>(times_.size()) >= max_) return false;
	times_.push_back(now);
	return true;
}

uint32_t ChatRateLimit::WaitMs(uint32_t now)
{
	Forget(now);
	if (static_cast<int>(times_.size()) < max_ || times_.empty()) return 0;
	return times_.front() + window_ - now;
}

namespace {
std::string Lower(std::string s)
{
	for (char &c : s) {
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	}
	return s;
}

bool OneOf(const std::string &word, std::initializer_list<const char *> list)
{
	for (const char *w : list) {
		if (word == w) return true;
	}
	return false;
}

// The next word of text from pos; pos ends after the spaces that follow it, and spaced tells whether there were any.
std::string NextWord(const std::string &text, size_t &pos, bool &spaced)
{
	const size_t end = text.find(' ', pos);
	const std::string word = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
	spaced = end != std::string::npos;
	pos = end == std::string::npos ? text.size() : end;
	while (pos < text.size() && text[pos] == ' ') pos++;
	return word;
}
}

ChatSlash ChatParseSlash(const std::string &text)
{
	ChatSlash out;
	if (ChatCommandOf(text).empty()) return out;
	size_t pos = 1;
	bool spaced = false;
	const std::string verb = Lower(NextWord(text, pos, spaced));
	out.complete = spaced;
	if (OneOf(verb, {"help", "ayuda", "aiuto", "?"})) {
		out.kind = kSlashHelp;
	}
	else if (OneOf(verb, {"all", "al", "todos", "tutti"})) {
		out.kind = kSlashTarget;
		out.target = kTargetAll;
	}
	else if (OneOf(verb, {"allies", "a", "aliados", "alleati"})) {
		out.kind = kSlashTarget;
		out.target = kTargetAllies;
	}
	else if (OneOf(verb, {"w", "p"})) {
		out.kind = kSlashWhisper;
		if (spaced) {
			bool nameSpaced = false;
			out.name = NextWord(text, pos, nameSpaced);
			out.complete = nameSpaced;
		}
		else {
			out.complete = false;
		}
	}
	else if (OneOf(verb, {"mute", "silenciar", "silenzia", "mutepings", "silenciarmarcas", "silenziasegnali"})) {
		out.kind = kSlashMute;
		out.marks = OneOf(verb, {"mutepings", "silenciarmarcas", "silenziasegnali"});
		// The rest of the line is the name: names may have spaces.
		out.name = text.substr(pos);
		while (!out.name.empty() && out.name.back() == ' ') out.name.pop_back();
		out.complete = !out.name.empty();
		return out;
	}
	else {
		out.kind = kSlashOther;
		out.verb = verb;
	}
	out.rest = text.substr(pos);
	return out;
}

int ChatMatchName(const std::string &typed, const std::vector<std::string> &names)
{
	if (typed.empty()) return -1;
	const std::string want = Lower(typed);
	int found = -1, count = 0;
	for (size_t i = 0; i < names.size(); i++) {
		const std::string name = Lower(names[i]);
		if (name == want) return static_cast<int>(i);
		if (name.compare(0, want.size(), want) == 0) {
			found = static_cast<int>(i);
			count++;
		}
	}
	return count > 1 ? -2 : found;
}
