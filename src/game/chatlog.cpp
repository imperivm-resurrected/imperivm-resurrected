// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/chatlog.h"

#include <cstdio>
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
