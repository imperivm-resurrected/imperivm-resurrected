// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"
#include "game/chatlog.h"

#include <string>
#include <vector>

static void TestMasks()
{
	// Player 0 writes; players 1 and 2 are enemies, 3 is an ally.
	const unsigned enemies = (1u << 1) | (1u << 2);
	CHECK_EQ(ChatMask({kTargetAll, -1}, 0, enemies), 0xffffu);
	const unsigned allies = ChatMask({kTargetAllies, -1}, 0, enemies);
	CHECK((allies & 1u) != 0);
	CHECK((allies & (1u << 3)) != 0);
	CHECK((allies & ((1u << 1) | (1u << 2))) == 0);
	CHECK((allies & ~0xffffu) == 0);
	CHECK_EQ(ChatMask({kTargetPlayer, 2}, 0, enemies), (1u << 2) | 1u);
	// An enemy mask that covers everyone but the sender leaves the line to the sender alone.
	CHECK_EQ(ChatMask({kTargetAllies, -1}, 5, 0xffffu & ~(1u << 5)), 1u << 5);

	int to = -2;
	CHECK_EQ(static_cast<int>(ChatScopeOfMask(0xffff, 0, to)), static_cast<int>(kChatAll));
	CHECK_EQ(to, -1);
	CHECK_EQ(static_cast<int>(ChatScopeOfMask((1u << 2) | 1u, 0, to)), static_cast<int>(kChatPrivate));
	CHECK_EQ(to, 2);
	CHECK_EQ(static_cast<int>(ChatScopeOfMask(allies, 0, to)), static_cast<int>(kChatAllies));
	CHECK_EQ(to, -1);
	// Every mask the game builds reads back as what was chosen.
	CHECK_EQ(static_cast<int>(ChatScopeOfMask(ChatMask({kTargetPlayer, 7}, 3, 0), 3, to)), static_cast<int>(kChatPrivate));
	CHECK_EQ(to, 7);
	CHECK_EQ(static_cast<int>(ChatScopeOfMask(ChatMask({kTargetAll, -1}, 3, 0), 3, to)), static_cast<int>(kChatAll));
	// Bits above 16 are not players.
	CHECK_EQ(static_cast<int>(ChatScopeOfMask(0x1ffff, 0, to)), static_cast<int>(kChatAll));
}

static void TestTargets()
{
	const std::vector<ChatTarget> withAllies = ChatTargets(true, {2, 5});
	CHECK_EQ(static_cast<int>(withAllies.size()), 4);
	CHECK_EQ(withAllies[0].kind, static_cast<int>(kTargetAllies));
	CHECK_EQ(withAllies[1].kind, static_cast<int>(kTargetAll));
	CHECK_EQ(withAllies[2].player, 2);
	CHECK_EQ(withAllies[3].player, 5);
	CHECK_EQ(ChatDefaultTarget(withAllies, false), 0);
	CHECK_EQ(ChatDefaultTarget(withAllies, true), 1);

	const std::vector<ChatTarget> alone = ChatTargets(false, {1});
	CHECK_EQ(static_cast<int>(alone.size()), 2);
	CHECK_EQ(alone[0].kind, static_cast<int>(kTargetAll));
	CHECK_EQ(ChatDefaultTarget(alone, false), 0);
	CHECK_EQ(ChatDefaultTarget(alone, true), 0);

	CHECK_EQ(ChatCycle(0, 4, false), 1);
	CHECK_EQ(ChatCycle(3, 4, false), 0);
	CHECK_EQ(ChatCycle(0, 4, true), 3);
	CHECK_EQ(ChatCycle(2, 4, true), 1);
	CHECK_EQ(ChatCycle(0, 1, false), 0);
	CHECK_EQ(ChatCycle(9, 4, false), 0);
	CHECK_EQ(ChatCycle(0, 0, false), 0);

	CHECK_EQ(ChatFindTarget(withAllies, {kTargetPlayer, 5}), 3);
	CHECK_EQ(ChatFindTarget(withAllies, {kTargetPlayer, 6}), -1);
	CHECK_EQ(ChatFindTarget(alone, {kTargetAllies, -1}), -1);
}

static void TestRecall()
{
	ChatRecall r;
	std::string text = "draft";
	CHECK(!r.Older(text));
	CHECK(!r.Newer(text));
	r.Push("hello");
	r.Push("gg");
	r.Push("gg");
	r.Push("");
	CHECK_EQ(static_cast<int>(r.Size()), 2);
	text = "typing";
	CHECK(r.Older(text));
	CHECK_EQ(text, std::string("gg"));
	CHECK(r.Older(text));
	CHECK_EQ(text, std::string("hello"));
	CHECK(!r.Older(text));
	CHECK_EQ(text, std::string("hello"));
	CHECK(r.Newer(text));
	CHECK_EQ(text, std::string("gg"));
	CHECK(r.Newer(text));
	CHECK_EQ(text, std::string("typing"));
	CHECK(!r.Newer(text));
	r.Push("third");
	text.clear();
	CHECK(r.Older(text));
	CHECK_EQ(text, std::string("third"));
}

static void TestFade()
{
	CHECK_EQ(ChatLineAlpha(0), 255);
	CHECK_EQ(ChatLineAlpha(kChatShowMs - kChatFadeMs), 255);
	const int half = ChatLineAlpha(kChatShowMs - kChatFadeMs / 2);
	CHECK(half > 100 && half < 155);
	CHECK(ChatLineAlpha(kChatShowMs - 1) < 5);
	CHECK_EQ(ChatLineAlpha(kChatShowMs), 0);
	CHECK_EQ(ChatLineAlpha(0xffffffffu), 0);
	int last = 256;
	for (uint32_t t = 0; t <= kChatShowMs; t += 100) {
		const int a = ChatLineAlpha(t);
		CHECK(a <= last);
		last = a;
	}

	ChatLog log;
	CHECK(!log.AnyFresh(1000));
	ChatEntry e;
	e.arrivedMs = 1000;
	e.text = "hi";
	log.Add(e);
	CHECK(log.AnyFresh(1000 + kChatShowMs - 1));
	CHECK(!log.AnyFresh(1000 + kChatShowMs));
	const uint32_t serial = log.Serial();
	log.Clear();
	CHECK(log.Lines().empty());
	CHECK(log.Serial() != serial);
	for (size_t i = 0; i < kChatLogSize + 5; i++) log.Add(e);
	CHECK_EQ(static_cast<int>(log.Lines().size()), static_cast<int>(kChatLogSize));
	CHECK(log.Lines().front().serial < log.Lines().back().serial);
}

static void TestText()
{
	CHECK_EQ(ChatCleanText("  hola  "), std::string("hola"));
	CHECK_EQ(ChatCleanText("\t \t"), std::string(""));
	CHECK_EQ(ChatCleanText("a\x01" "b"), std::string("a b"));
	std::string longText(kChatMaxText - 1, 'x');
	longText += "\xc3\xb1\xc3\xb1";
	const std::string cut = ChatCleanText(longText);
	CHECK(cut.size() <= kChatMaxText);
	CHECK_EQ(cut, std::string(kChatMaxText - 1, 'x'));
	CHECK_EQ(ChatCleanText(std::string(400, 'y')).size(), kChatMaxText);

	CHECK_EQ(ChatCommandOf("/gold 500"), std::string("gold 500"));
	CHECK_EQ(ChatCommandOf("/map"), std::string("map"));
	CHECK_EQ(ChatCommandOf("gold"), std::string(""));
	CHECK_EQ(ChatCommandOf("/"), std::string(""));
	CHECK_EQ(ChatCommandOf("// not a command"), std::string(""));
	CHECK_EQ(ChatCommandOf("/ hi"), std::string(""));

	CHECK_EQ(ChatClock(0), std::string("0:00"));
	CHECK_EQ(ChatClock(65999), std::string("1:05"));
	CHECK_EQ(ChatClock(59 * 60000 + 59000), std::string("59:59"));
	CHECK_EQ(ChatClock(3600000 + 2 * 60000 + 3000), std::string("1:02:03"));
}

int main()
{
	TestMasks();
	TestTargets();
	TestRecall();
	TestFade();
	TestText();
	return test::Summary("chatlog");
}
