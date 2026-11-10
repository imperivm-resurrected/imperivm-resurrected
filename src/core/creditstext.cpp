// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/creditstext.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

typedef std::vector<std::string> Group;

// A subtitle is a short line with a number: the date or the release under the first heading.
const size_t kSubtitleMax = 40;
// A lone line longer than this closes a section instead of opening one (a copyright line).
const size_t kHeadingMax = 48;
// Lines longer than this belong to a paragraph, which is drawn as it is.
const size_t kProseFrom = 90;
const size_t kTitleLinesMax = 3, kLabelRoles = 3;

std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
	return s.substr(a, b - a);
}

size_t Length(const std::string &s)
{
	size_t n = 0;
	for (unsigned char c : s) n += (c & 0xc0) != 0x80;
	return n;
}

std::vector<uint32_t> CodePoints(const std::string &s)
{
	std::vector<uint32_t> out;
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
		uint32_t cp = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0f : extra == 1 ? c & 0x1f : c;
		i++;
		for (; extra > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80; extra--, i++) cp = cp << 6 | (static_cast<unsigned char>(s[i]) & 0x3f);
		out.push_back(cp);
	}
	return out;
}

bool IsBar(const std::string &line)
{
	if (line.size() < 5) return false;
	const char c = line[0];
	if (c != '=' && c != '-' && c != '_' && c != '*' && c != '~') return false;
	for (char x : line) {
		if (x != c) return false;
	}
	return true;
}

bool IsNote(const std::string &line) { return line.size() >= 2 && line.front() == '(' && line.back() == ')'; }

bool IsSubtitle(const std::string &line)
{
	if (IsNote(line) || Length(line) > kSubtitleMax) return false;
	for (char c : line) {
		if (c >= '0' && c <= '9') return true;
	}
	return false;
}

// Written in capitals, as the roles of the older games are.
bool IsCapitals(const std::string &line)
{
	int letters = 0;
	for (uint32_t cp : CodePoints(line)) {
		const bool lower = (cp >= 'a' && cp <= 'z') || (cp >= 0xdf && cp <= 0xff && cp != 0xf7);
		const bool upper = (cp >= 'A' && cp <= 'Z') || (cp >= 0xc0 && cp <= 0xde && cp != 0xd7);
		if (lower) return false;
		letters += upper;
	}
	return letters >= 2;
}

bool IsProse(const Group &g)
{
	for (const std::string &l : g) {
		if (Length(l) > kProseFrom) return true;
	}
	return false;
}

size_t TitleLines(const Group &g)
{
	size_t n = 0;
	while (n < g.size() && !IsNote(g[n])) n++;
	return n;
}

bool AllNotesFrom(const Group &g, size_t from)
{
	for (size_t i = from; i < g.size(); i++) {
		if (!IsNote(g[i])) return false;
	}
	return true;
}

// A title that cannot be a name: it has a note under it, or it is written in capitals.
bool StrongTitle(const Group &g)
{
	const size_t n = TitleLines(g);
	if (n == 0 || n > kTitleLinesMax || !AllNotesFrom(g, n)) return false;
	if (n < g.size()) return true;
	for (size_t i = 0; i < n; i++) {
		if (!IsCapitals(g[i])) return false;
	}
	return true;
}

// The group holds only a title (and its notes): its names, if it has any, are the next group.
bool TitleOnly(const Group &g) { return StrongTitle(g) || (g.size() == 1 && !IsNote(g[0])); }

std::string JoinTitle(const Group &g)
{
	std::string out;
	for (size_t i = 0, n = TitleLines(g); i < n; i++) out += (out.empty() ? "" : " ") + g[i];
	return out;
}

size_t NameCount(const Group &g, size_t from)
{
	size_t n = 0;
	for (size_t i = from; i < g.size(); i++) n += !IsNote(g[i]);
	return n;
}

struct Builder {
	CreditsDoc &doc;
	// The roles that open a section before any heading: they name its line of the index together.
	std::vector<std::string> roles;
	size_t rolesAt = 0;
	// After a heading the roles belong to its line.
	bool headed = false;

	void Add(CreditsKind kind, const std::vector<std::string> &lines) { doc.items.push_back({kind, lines}); }

	void Heading(const std::string &title)
	{
		FlushRoles();
		headed = true;
		doc.index.push_back({title, doc.items.size()});
		Add(kCreditsHeading, {title});
	}

	void Role(const std::string &title)
	{
		if (!headed) {
			if (roles.empty()) rolesAt = doc.items.size();
			roles.push_back(title);
		}
		Add(kCreditsRole, {title});
	}

	void FlushRoles()
	{
		if (roles.empty()) return;
		std::string label;
		for (size_t i = 0; i < roles.size() && i < kLabelRoles; i++) label += (i ? " \xC2\xB7 " : "") + roles[i];
		if (roles.size() > kLabelRoles) label += " \xC2\xB7 \xE2\x80\xA6";
		doc.index.push_back({label, rolesAt});
		roles.clear();
	}

	// Names, with any note among them kept where it is.
	void Body(const Group &g, size_t from)
	{
		std::vector<std::string> names;
		for (size_t i = from; i < g.size(); i++) {
			if (!IsNote(g[i])) {
				names.push_back(g[i]);
				continue;
			}
			if (!names.empty()) Add(kCreditsNames, names);
			names.clear();
			Add(kCreditsNote, {g[i]});
		}
		if (!names.empty()) Add(kCreditsNames, names);
	}

	void Text(const Group &g)
	{
		for (const std::string &l : g) Add(kCreditsText, {l});
	}

	// The title with its notes, then its names: a long list or one with a note gets a line of its own in the index.
	void Titled(const std::string &title, const Group &head, size_t notesFrom, const Group *names, size_t namesFrom)
	{
		const bool notes = notesFrom < head.size();
		const size_t count = names ? NameCount(*names, namesFrom) : 0;
		if (notes || count > kCreditsColumnsFrom) Heading(title);
		else Role(title);
		for (size_t i = notesFrom; i < head.size(); i++) Add(kCreditsNote, {head[i]});
		if (names) Body(*names, namesFrom);
	}

	void SingleGroup(const Group &g)
	{
		if (IsProse(g) || IsNote(g[0]) || (g.size() == 1 && Length(g[0]) > kHeadingMax)) {
			Text(g);
			return;
		}
		Heading(g[0]);
		size_t i = 1;
		if (i < g.size() && IsSubtitle(g[i])) Add(kCreditsSubtitle, {g[i++]});
		Body(g, i);
	}

	void Block(const std::vector<Group> &groups)
	{
		if (groups.size() == 1) {
			SingleGroup(groups[0]);
			return;
		}
		for (size_t gi = 0; gi < groups.size(); gi++) {
			const Group &g = groups[gi];
			const Group *next = gi + 1 < groups.size() ? &groups[gi + 1] : nullptr;
			// A long line in small letters is a closing line (a copyright), not the title of what follows.
			const bool longLine = !StrongTitle(g) && Length(g[0]) > kHeadingMax;
			if (IsProse(g) || IsNote(g[0]) || longLine) {
				Text(g);
				continue;
			}
			if (TitleOnly(g)) {
				const std::string title = JoinTitle(g);
				if (next && !IsProse(*next) && !StrongTitle(*next) && !IsNote((*next)[0])) {
					Titled(title, g, TitleLines(g), next, 0);
					gi++;
				}
				else if (StrongTitle(g) || (Length(title) <= kHeadingMax && next)) {
					Heading(title);
					for (size_t i = TitleLines(g); i < g.size(); i++) Add(kCreditsNote, {g[i]});
				}
				else {
					Text(g);
				}
				continue;
			}
			size_t notesEnd = 1;
			while (notesEnd < g.size() && IsNote(g[notesEnd])) notesEnd++;
			Group head(g.begin(), g.begin() + notesEnd);
			Titled(g[0], head, 1, &g, notesEnd);
		}
		FlushRoles();
	}
};

}

CreditsDoc ParseCredits(const std::string &text)
{
	std::vector<std::vector<Group>> blocks(1);
	Group group;
	const auto closeGroup = [&]() {
		if (!group.empty()) blocks.back().push_back(group);
		group.clear();
	};
	size_t start = text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		const std::string line = Trim(text.substr(start, end - start));
		start = end + 1;
		if (IsBar(line)) {
			closeGroup();
			if (!blocks.back().empty()) blocks.emplace_back();
		}
		else if (line.empty()) {
			closeGroup();
		}
		else {
			group.push_back(line);
		}
	}
	closeGroup();

	CreditsDoc doc;
	Builder b{doc};
	for (const std::vector<Group> &block : blocks) {
		if (block.empty()) continue;
		if (!doc.items.empty()) b.Add(kCreditsRule, {});
		b.headed = false;
		b.Block(block);
		b.FlushRoles();
	}
	if (doc.index.empty() && !doc.items.empty() && !doc.items[0].lines.empty()) doc.index.push_back({doc.items[0].lines[0], 0});
	return doc;
}
