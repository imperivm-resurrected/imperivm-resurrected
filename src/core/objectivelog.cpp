// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/objectivelog.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace {

const char kHeader[] = "imperivm-objectives 1";
const size_t kMaxEntries = 2000, kMaxField = 1 << 16;

std::string Escape(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (const char c : s) {
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '\t': out += "\\t"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		default: out += c; break;
		}
	}
	return out;
}

bool Unescape(const std::string &s, std::string &out)
{
	out.clear();
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '\\') {
			out += s[i];
			continue;
		}
		if (++i >= s.size()) return false;
		switch (s[i]) {
		case '\\': out += '\\'; break;
		case 't': out += '\t'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		default: return false;
		}
	}
	return out.size() <= kMaxField;
}

std::vector<std::string> Split(const std::string &line)
{
	std::vector<std::string> parts;
	size_t from = 0;
	for (;;) {
		const size_t tab = line.find('\t', from);
		parts.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
		if (tab == std::string::npos) return parts;
		from = tab + 1;
	}
}

}

void ObjectiveLog::Clear()
{
	done_.clear();
	seen_.clear();
}

void ObjectiveLog::Complete(const ObjectiveNote &note)
{
	if (note.id.empty()) return;
	done_.erase(std::remove_if(done_.begin(), done_.end(), [&](const ObjectiveNote &n) { return n.id == note.id; }), done_.end());
	ObjectiveNote kept = note;
	kept.onMap = false;
	done_.push_back(kept);
	if (done_.size() > kMaxEntries) done_.erase(done_.begin());
}

void ObjectiveLog::MarkSeen(const std::string &id)
{
	if (!id.empty() && seen_.size() < kMaxEntries) seen_.insert(id);
}

std::string ObjectiveLog::Encode() const
{
	std::string out = kHeader;
	out += '\n';
	for (const std::string &id : seen_) out += "seen\t" + Escape(id) + "\n";
	for (const ObjectiveNote &n : done_) out += "done\t" + Escape(n.id) + "\t" + Escape(n.title) + "\t" + Escape(n.text) + "\n";
	return out;
}

bool ObjectiveLog::Decode(const std::string &data)
{
	Clear();
	std::vector<std::string> lines;
	size_t from = 0;
	while (from < data.size()) {
		size_t nl = data.find('\n', from);
		if (nl == std::string::npos) nl = data.size();
		std::string line = data.substr(from, nl - from);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
		from = nl + 1;
	}
	if (lines.empty() || lines[0] != kHeader) return false;
	ObjectiveLog read;
	for (size_t i = 1; i < lines.size(); i++) {
		if (lines[i].empty()) continue;
		const std::vector<std::string> parts = Split(lines[i]);
		if (parts[0] == "seen") {
			std::string id;
			if (parts.size() != 2 || !Unescape(parts[1], id)) return false;
			read.MarkSeen(id);
		}
		else if (parts[0] == "done") {
			ObjectiveNote n;
			if (parts.size() != 4 || !Unescape(parts[1], n.id) || !Unescape(parts[2], n.title) || !Unescape(parts[3], n.text)) return false;
			read.Complete(n);
		}
		// Lines of a later version are left alone.
	}
	*this = read;
	return true;
}

std::vector<std::string> ObjectiveActiveOrder(const std::vector<std::string> &order, const std::vector<std::string> &held)
{
	const std::set<std::string> holds(held.begin(), held.end());
	std::set<std::string> placed;
	std::vector<std::string> out;
	for (const std::string &id : order) {
		if (holds.count(id) && placed.insert(id).second) out.push_back(id);
	}
	for (const std::string &id : held) {
		if (placed.insert(id).second) out.push_back(id);
	}
	return out;
}

std::vector<ObjectiveEntry> ObjectiveEntries(const std::vector<ObjectiveNote> &active, const ObjectiveLog &log)
{
	std::vector<ObjectiveEntry> out;
	std::set<std::string> live;
	for (const ObjectiveNote &n : active) {
		if (!live.insert(n.id).second) continue;
		ObjectiveEntry e;
		e.note = n;
		e.fresh = !log.Seen(n.id);
		out.push_back(e);
	}
	const std::vector<ObjectiveNote> &done = log.Completed();
	for (auto it = done.rbegin(); it != done.rend(); ++it) {
		if (live.count(it->id)) continue;
		ObjectiveEntry e;
		e.note = *it;
		e.done = true;
		out.push_back(e);
	}
	return out;
}

int ObjectiveCountInProgress(const std::vector<ObjectiveEntry> &entries)
{
	return static_cast<int>(std::count_if(entries.begin(), entries.end(), [](const ObjectiveEntry &e) { return !e.done; }));
}

int ObjectiveFirstSelection(const std::vector<ObjectiveEntry> &entries)
{
	for (size_t i = 0; i < entries.size(); i++) {
		if (!entries[i].done && entries[i].fresh) return static_cast<int>(i);
	}
	return entries.empty() ? -1 : 0;
}

int ObjectiveFollow(const std::vector<ObjectiveEntry> &entries, const std::string &id, int previous)
{
	if (entries.empty()) return -1;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].note.id == id) return static_cast<int>(i);
	}
	return std::max(0, std::min(previous, static_cast<int>(entries.size()) - 1));
}
