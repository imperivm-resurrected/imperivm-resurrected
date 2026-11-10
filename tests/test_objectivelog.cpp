// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "core/objectivelog.h"

static ObjectiveNote Note(const std::string &id, const std::string &title = "", const std::string &text = "", bool onMap = false)
{
	ObjectiveNote n;
	n.id = id;
	n.title = title.empty() ? id : title;
	n.text = text;
	n.onMap = onMap;
	return n;
}

static std::vector<std::string> Ids(const std::vector<ObjectiveEntry> &entries)
{
	std::vector<std::string> out;
	for (const ObjectiveEntry &e : entries) out.push_back((e.done ? "done:" : "") + e.note.id + (e.fresh ? "*" : ""));
	return out;
}

static void ActiveOrderFollowsTheGame()
{
	// ClearNotes empties what the game holds but not its list of ids, and a note given again is listed twice.
	const std::vector<std::string> order = {"a", "b", "c", "b", "d"};
	CHECK_EQ(ObjectiveActiveOrder(order, {"d", "b"}), (std::vector<std::string>{"b", "d"}));
	CHECK_EQ(ObjectiveActiveOrder(order, {}), std::vector<std::string>());
	CHECK_EQ(ObjectiveActiveOrder({}, {"x", "y"}), (std::vector<std::string>{"x", "y"}));
	CHECK_EQ(ObjectiveActiveOrder({"a"}, {"z", "a"}), (std::vector<std::string>{"a", "z"}));
}

static void GroupsInProgressThenCompletedNewestFirst()
{
	ObjectiveLog log;
	log.Complete(Note("old"));
	log.Complete(Note("older-then-again"));
	log.Complete(Note("newest"));
	log.MarkSeen("b");
	const std::vector<ObjectiveEntry> entries = ObjectiveEntries({Note("a"), Note("b")}, log);
	CHECK_EQ(Ids(entries), (std::vector<std::string>{"a*", "b", "done:newest", "done:older-then-again", "done:old"}));
	CHECK_EQ(ObjectiveCountInProgress(entries), 2);
}

static void NoteGivenAgainIsOnlyInProgress()
{
	ObjectiveLog log;
	log.Complete(Note("hold"));
	log.Complete(Note("capture"));
	const std::vector<ObjectiveEntry> entries = ObjectiveEntries({Note("hold")}, log);
	CHECK_EQ(Ids(entries), (std::vector<std::string>{"hold*", "done:capture"}));
}

static void RemovedTwiceKeepsTheLastWords()
{
	ObjectiveLog log;
	log.Complete(Note("a", "First", "one"));
	log.Complete(Note("b"));
	log.Complete(Note("a", "Second", "two", true));
	CHECK_EQ(static_cast<int>(log.Completed().size()), 2);
	CHECK_EQ(log.Completed().back().id, std::string("a"));
	CHECK_EQ(log.Completed().back().title, std::string("Second"));
	// A completed note has nothing to show on the map any more.
	CHECK(!log.Completed().back().onMap);
	log.Complete(Note(""));
	CHECK_EQ(static_cast<int>(log.Completed().size()), 2);
}

static void SeenAndClear()
{
	ObjectiveLog log;
	CHECK(log.Empty());
	CHECK(!log.Seen("a"));
	log.MarkSeen("a");
	log.MarkSeen("");
	CHECK(log.Seen("a"));
	CHECK(!log.Seen(""));
	CHECK(!log.Empty());
	log.Clear();
	CHECK(log.Empty());
	CHECK(!log.Seen("a"));
}

static void DuplicateActiveNotesShowOnce()
{
	ObjectiveLog log;
	CHECK_EQ(Ids(ObjectiveEntries({Note("a"), Note("a"), Note("b")}, log)), (std::vector<std::string>{"a*", "b*"}));
}

static void OpensOnTheFirstUnreadNote()
{
	ObjectiveLog log;
	log.MarkSeen("a");
	CHECK_EQ(ObjectiveFirstSelection(ObjectiveEntries({Note("a"), Note("b")}, log)), 1);
	log.MarkSeen("b");
	CHECK_EQ(ObjectiveFirstSelection(ObjectiveEntries({Note("a"), Note("b")}, log)), 0);
	ObjectiveLog onlyDone;
	onlyDone.Complete(Note("x"));
	CHECK_EQ(ObjectiveFirstSelection(ObjectiveEntries({}, onlyDone)), 0);
	CHECK_EQ(ObjectiveFirstSelection(ObjectiveEntries({}, ObjectiveLog())), -1);
}

static void SelectionFollowsTheNote()
{
	ObjectiveLog log;
	std::vector<ObjectiveEntry> entries = ObjectiveEntries({Note("a"), Note("b"), Note("c")}, log);
	CHECK_EQ(ObjectiveFollow(entries, "b", 1), 1);
	// "b" is removed while the screen is open: it moves to the completed ones and the selection goes with it.
	log.Complete(Note("b"));
	entries = ObjectiveEntries({Note("a"), Note("c")}, log);
	CHECK_EQ(ObjectiveFollow(entries, "b", 1), 2);
	CHECK_EQ(ObjectiveFollow(entries, "gone", 7), 2);
	CHECK_EQ(ObjectiveFollow(entries, "gone", -3), 0);
	CHECK_EQ(ObjectiveFollow({}, "a", 0), -1);
}

static void SavedLogComesBack()
{
	ObjectiveLog log;
	log.MarkSeen("Julius Caesar");
	log.MarkSeen("tab\there");
	log.Complete(Note("Capture Alesia", "Captura Alesia", "Captura todos los foros\nde la fortaleza.\tFin \\ ya"));
	log.Complete(Note("Hold the siege", "Mantén el sitio", ""));
	const std::string data = log.Encode();
	ObjectiveLog back;
	CHECK(back.Decode(data));
	CHECK(back.Seen("Julius Caesar"));
	CHECK(back.Seen("tab\there"));
	CHECK_EQ(static_cast<int>(back.Completed().size()), 2);
	CHECK_EQ(back.Completed()[0].id, std::string("Capture Alesia"));
	CHECK_EQ(back.Completed()[0].text, std::string("Captura todos los foros\nde la fortaleza.\tFin \\ ya"));
	CHECK_EQ(back.Completed()[1].title, std::string("Mantén el sitio"));
	CHECK_EQ(back.Encode(), data);
	// Written on Windows by hand, and with a line of a later version.
	std::string crlf;
	for (const char c : data) {
		if (c == '\n') crlf += "\r\n";
		else crlf += c;
	}
	CHECK(back.Decode(crlf + "later\tsomething\r\n"));
	CHECK_EQ(static_cast<int>(back.Completed().size()), 2);
}

static void DamagedLogIsRefused()
{
	ObjectiveLog log;
	log.MarkSeen("a");
	CHECK(!log.Decode(""));
	CHECK(log.Empty());
	CHECK(!log.Decode("something else\nseen\ta\n"));
	CHECK(!log.Decode("imperivm-objectives 1\ndone\tonly-two\tfields\n"));
	CHECK(!log.Decode("imperivm-objectives 1\nseen\tbad\\escape\n"));
	CHECK(!log.Decode("imperivm-objectives 1\nseen\tends with\\"));
	CHECK(log.Empty());
	CHECK(log.Decode("imperivm-objectives 1\n"));
	CHECK(log.Empty());
	CHECK(ObjectiveLog().Encode() == "imperivm-objectives 1\n");
}

int main()
{
	ActiveOrderFollowsTheGame();
	GroupsInProgressThenCompletedNewestFirst();
	NoteGivenAgainIsOnlyInProgress();
	RemovedTwiceKeepsTheLastWords();
	SeenAndClear();
	DuplicateActiveNotesShowOnce();
	OpensOnTheFirstUnreadNote();
	SelectionFollowsTheNote();
	SavedLogComesBack();
	DamagedLogIsRefused();
	return test::Summary("objectivelog");
}
