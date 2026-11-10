// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <set>
#include <string>
#include <vector>

// A note of the game (GiveNote), as the objectives screen shows it: its words are the game's, already translated.
struct ObjectiveNote {
	std::string id, title, text;
	// It has a place on the map the player is on, which the game's "View location" centres the view on.
	bool onMap = false;
};

// What the game forgets: the notes it took away during the match, and the ones the player already read on the screen.
class ObjectiveLog {
public:
	void Clear();
	bool Empty() const { return done_.empty() && seen_.empty(); }
	// A note the game removed goes to the newest end, replacing an earlier removal of the same note.
	void Complete(const ObjectiveNote &note);
	void MarkSeen(const std::string &id);
	bool Seen(const std::string &id) const { return seen_.count(id) != 0; }
	// Oldest first.
	const std::vector<ObjectiveNote> &Completed() const { return done_; }
	// The file kept next to a save.
	std::string Encode() const;
	bool Decode(const std::string &data);

private:
	std::vector<ObjectiveNote> done_;
	std::set<std::string> seen_;
};

// The ids the game holds now, in the order it gave them: its list of ids keeps removed ones after ClearNotes and repeats one given
// twice, so only the ones it still holds count, once each; any it holds outside the list go last.
std::vector<std::string> ObjectiveActiveOrder(const std::vector<std::string> &order, const std::vector<std::string> &held);

struct ObjectiveEntry {
	ObjectiveNote note;
	bool done = false;
	// Not selected on the screen yet.
	bool fresh = false;
};

// The notes in progress in the game's order, then the completed ones, newest first; one given again counts only as in progress.
std::vector<ObjectiveEntry> ObjectiveEntries(const std::vector<ObjectiveNote> &active, const ObjectiveLog &log);
int ObjectiveCountInProgress(const std::vector<ObjectiveEntry> &entries);
// Where the screen opens: the first note not read yet, else the first one; -1 when there are none.
int ObjectiveFirstSelection(const std::vector<ObjectiveEntry> &entries);
// The same note after the list changed, wherever it went, or the row nearest to where it was.
int ObjectiveFollow(const std::vector<ObjectiveEntry> &entries, const std::string &id, int previous);
