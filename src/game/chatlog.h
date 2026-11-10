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
	// A ping's type (ChatPingType), or -1 for a line that is not one.
	int ping = -1;
	// A line the mod writes for the local player only (help, errors): no name, nothing sent.
	bool local = false;
};


// The chat's place: the bottom-left corner of the panel, kept inside the screen and above the command bar. Saved in thousandths
// of the screen so it survives a change of resolution.
struct ChatAnchor {
	int left = 0, bottom = 0;
};
ChatAnchor ChatClampAnchor(ChatAnchor a, int screenW, int width, int minBottom, int maxBottom);
int ChatToPermille(int value, int size);
int ChatFromPermille(int permille, int size);

// Pings: at most this many in a window, as in other strategy games; each sender keeps at most this many marks on the map, and a
// mark lasts this long even when the game clock stops (paused, or the match over).
constexpr int kPingBurst = 3, kPingMarksPerSender = 3;
constexpr uint32_t kPingWindowMs = 5000, kPingLifeMs = 8000;

class ChatRateLimit {
public:
	ChatRateLimit(int max, uint32_t windowMs) : max_(max), window_(windowMs) {}
	bool Allow(uint32_t now);
	uint32_t WaitMs(uint32_t now);

private:
	void Forget(uint32_t now);
	int max_;
	uint32_t window_;
	std::deque<uint32_t> times_;
};

enum ChatPingType { kPingHere, kPingAttack, kPingDanger, kPingTypes };
// The English phrase a ping of that type carries as its text, translated to the sender's language.
const char *ChatPingPhrase(int type);
// A ping is a chat line with a location whose text is empty (the plain "here") or one of its type's phrases, in any of the
// languages the mod speaks: phrases[type] lists them. -1 for a line that is not a ping.
int ChatPingTypeOf(const std::string &text, bool location, const std::vector<std::string> (&phrases)[kPingTypes]);
// The ping menu: "here" above the press, "attack" down right, "danger" down left. -1 within the dead zone (the default).
int ChatPingMenuPick(int dx, int dy, int deadZone);
void ChatPingMenuOffset(int type, int radius, int &dx, int &dy);

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

// The chat bar's own commands, in the three languages: /help, /all, /allies and /w or /p with a name. They are never sent.
enum ChatSlashKind { kSlashNone, kSlashHelp, kSlashTarget, kSlashWhisper, kSlashOther };
struct ChatSlash {
	int kind = kSlashNone;
	// For kSlashTarget: kTargetAll or kTargetAllies.
	int target = kTargetAll;
	// The command word, lower case, for kSlashOther; the name typed after /w for kSlashWhisper.
	std::string verb, name;
	// The message after the command.
	std::string rest;
	// The command word (and the name of /w) is finished: a space follows it.
	bool complete = false;
};
ChatSlash ChatParseSlash(const std::string &text);
// The player whose name starts with what was typed, without regard to case; an exact name wins over longer ones. -1 if none,
// -2 if several fit.
int ChatMatchName(const std::string &typed, const std::vector<std::string> &names);

// Spaces at both ends go; the text is cut at kChatMaxText bytes without splitting a UTF-8 character.
std::string ChatCleanText(const std::string &text);
// "/w Marcus hi" -> "w Marcus hi"; empty when the line is not a command.
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
