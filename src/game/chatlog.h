// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// The game's CVXChatMessage carries a mask of 16 player bits: every bit set is a player that shows the line.
constexpr unsigned kChatEveryone = 0xffff;
constexpr size_t kChatMaxText = 250;
// A line stays this long after it arrives, and fades out during the last part of it.
constexpr uint32_t kChatShowMs = 10000, kChatFadeMs = 1500;
constexpr size_t kChatLogSize = 100, kChatHistoryLines = 30;

enum ChatScope { kChatAll, kChatAllies, kChatPrivate };

struct ChatEntry {
	uint32_t serial = 0;
	uint32_t arrivedMs = 0;
	uint32_t clockMs = 0;
	int scope = kChatAll;
	int sender = -1;
	std::string name;
	uint32_t rgb = 0xe8d8a0;
	// The player a private line went to, for the lines the local player sent.
	std::string toName;
	std::string text;
	bool location = false;
	bool own = false;
};

// What a received mask means: everyone, one other player, or the sender's side.
ChatScope ChatScopeOfMask(unsigned mask, int sender, int &privateTo);

enum ChatTargetKind { kTargetAllies, kTargetAll, kTargetPlayer };
struct ChatTarget {
	int kind = kTargetAll;
	int player = -1;
	bool operator==(const ChatTarget &o) const { return kind == o.kind && player == o.player; }
};

// The mask the game's chat dialog builds: "allies" is everyone who is not an enemy, and the sender always reads their own line.
unsigned ChatMask(ChatTarget target, int local, unsigned enemies);
std::vector<ChatTarget> ChatTargets(bool hasAllies, const std::vector<int> &otherHumans);
int ChatDefaultTarget(const std::vector<ChatTarget> &targets, bool everyone);
int ChatCycle(int current, int count, bool back);
int ChatFindTarget(const std::vector<ChatTarget> &targets, ChatTarget wanted);

// Spaces at both ends go; the text is cut at kChatMaxText bytes without splitting a UTF-8 character.
std::string ChatCleanText(const std::string &text);
// "/gold 500" -> "gold 500"; empty when the line is not a command.
std::string ChatCommandOf(const std::string &text);
std::string ChatClock(uint32_t ms);
int ChatLineAlpha(uint32_t ageMs);

// The messages the player sent, walked with Up and Down like a shell: the text being typed comes back at the bottom.
class ChatRecall {
public:
	void Push(const std::string &text);
	bool Older(std::string &text);
	bool Newer(std::string &text);
	void Reset();
	size_t Size() const { return sent_.size(); }

private:
	std::vector<std::string> sent_;
	int pos_ = -1;
	std::string draft_;
};

class ChatLog {
public:
	void Add(ChatEntry e);
	void Clear();
	uint32_t Serial() const { return next_ - 1; }
	const std::deque<ChatEntry> &Lines() const { return lines_; }
	bool AnyFresh(uint32_t now) const;

private:
	std::deque<ChatEntry> lines_;
	uint32_t next_ = 1;
};
