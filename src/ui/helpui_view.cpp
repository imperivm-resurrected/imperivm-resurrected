// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/helpui_view.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "i18n/tr.h"
#include "ui/keycaps.h"
#include "ui/ornaments.h"
#include "ui/widgets.h"

using namespace ui;

namespace helpui {
namespace {

// No footer: the page runs to the bottom of the frame, and Close sits under the side bar.
constexpr int kMargin = 24, kTop = 92, kPaneBottom = kScreenH - 28, kCloseH = 38, kCloseY = kPaneBottom - kCloseH, kNavBottom = kCloseY - 14;
constexpr int kNavX = 24, kNavW = 268, kSearchY = 96, kSearchH = 32, kNavTop = 140, kDividerX = 306;
constexpr int kPaneX = 322, kPaneW = kScreenW - kMargin - kPaneX;
constexpr int kBarY = 94, kBarH = 28, kTitleY = 128, kTitleH = 46, kRuleY = 176, kViewTop = 192;
constexpr int kScrollW = 5, kContentW = kPaneW - 18;
constexpr int kLineH = 22, kGapLine = 10, kBlockGap = 12, kHeadingGap = 20;
constexpr int kBigW = 52, kBigH = 60, kIconW = 65, kIconH = 76, kSmall = 20, kCardW = 158, kCardH = 124, kCardGap = 12;
constexpr int kLinkRowH = 28, kResultIconW = 40, kResultIconH = 46;
constexpr int kChipH = 50, kChipGap = 10, kChips = 4, kCellH = 46, kKeyW = 220;
constexpr int kGroupH = 32, kItemH = 26, kSectionH = 24, kToggleW = 26;
const COLORREF kPlain = RGB(208, 194, 158), kLink = RGB(226, 186, 104), kLinkHot = RGB(250, 226, 160), kCurrentBg = RGB(60, 43, 22),
			   kHotBg = RGB(38, 30, 20), kCardBg = RGB(25, 19, 13), kCardHot = RGB(46, 35, 22);

const char *const kIconHealth = "UI/help/health ico.bmp", *const kIconSlash = "UI/help/sattack ico.bmp", *const kIconPierce = "UI/help/pattack ico.bmp",
				  *const kIconDefense = "UI/help/defense ico.bmp", *const kIconSpeed = "UI/help/speed ico.bmp", *const kIconSight = "UI/help/sight ico.bmp",
				  *const kIconGold = "UI/help/gold ico.bmp", *const kIconTrain = "UI/help/train ico.bmp";

std::wstring Upper(std::wstring w)
{
	if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	return w;
}

std::string Utf8(const std::wstring &w)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
}

std::string FirstLine(const std::string &s)
{
	std::string l = s.substr(0, s.find('\n'));
	while (!l.empty() && l.back() == ' ') l.pop_back();
	return l;
}

std::string RestLines(const std::string &s)
{
	const size_t nl = s.find('\n');
	return nl == std::string::npos ? std::string() : s.substr(nl + 1);
}

HFONT LinkFont(float s)
{
	static HFONT font = nullptr;
	static float scale = 0;
	if (font && scale == s) return font;
	if (font) DeleteObject(font);
	font = CreateFontW(-static_cast<int>(15 * s + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Georgia");
	scale = s;
	return font;
}

int PixelWidth(Painter &p, const std::wstring &w, int spacing)
{
	SetTextCharacterExtra(p.dc, p.S(spacing));
	SIZE sz = {};
	GetTextExtentPoint32W(p.dc, w.c_str(), static_cast<int>(w.size()), &sz);
	SetTextCharacterExtra(p.dc, 0);
	return static_cast<int>(sz.cx) - (w.empty() ? 0 : p.S(spacing));
}

void SpacedLine(Painter &p, int x, int y, int h, const std::wstring &w, int spacing, COLORREF c)
{
	SetTextCharacterExtra(p.dc, p.S(spacing));
	SetTextColor(p.dc, c);
	RECT r = {p.S(x), p.S(y), p.S(x) + PixelWidth(p, w, spacing) + p.S(spacing) + 4, p.S(y + h)};
	SetTextCharacterExtra(p.dc, p.S(spacing));
	DrawTextW(p.dc, w.c_str(), static_cast<int>(w.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
	SetTextCharacterExtra(p.dc, 0);
}

// Lines of the text in the current font, its own line breaks kept; an empty one stands for the gap between paragraphs.
std::vector<std::wstring> Wrap(Painter &p, const std::string &text, int width)
{
	std::vector<std::wstring> out;
	for (size_t from = 0;;) {
		const size_t nl = text.find('\n', from);
		const std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
		if (line.empty()) out.push_back(std::wstring());
		else {
			for (const std::wstring &l : p.WrapLines(line, 0, width)) out.push_back(l);
		}
		if (nl == std::string::npos) break;
		from = nl + 1;
	}
	return out;
}

int LinesHeight(const std::vector<std::wstring> &lines, int lineH = kLineH)
{
	int h = 0;
	for (const std::wstring &l : lines) h += l.empty() ? kGapLine : lineH;
	return h;
}

void DrawLines(Painter &p, int x, int y, int w, const std::vector<std::wstring> &lines, COLORREF c, bool center, int lineH = kLineH)
{
	for (const std::wstring &l : lines) {
		if (l.empty()) {
			y += kGapLine;
			continue;
		}
		if (center) {
			RECT r = p.R(x, y, w, lineH);
			SetTextColor(p.dc, c);
			DrawTextW(p.dc, l.c_str(), static_cast<int>(l.size()), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		else {
			p.LineW(x, y + (lineH - 18) / 2, w, lineH, l, c);
		}
		y += lineH;
	}
}

constexpr int kCapH = 26, kPlusW = 12;

// The parts of a key written in the help ("Ctrl + Ñ"), each on its cap as the Controls screen draws them.
std::vector<std::string> TextCaps(const std::string &key)
{
	std::vector<std::string> parts;
	std::string cur;
	for (size_t i = 0; i <= key.size(); i++) {
		const bool end = i == key.size();
		const bool plus = !end && key[i] == '+' && i > 0 && i + 1 < key.size() && (key[i - 1] == ' ' || key[i + 1] == ' ');
		if (!end && !plus) {
			cur += key[i];
			continue;
		}
		while (!cur.empty() && cur.back() == ' ') cur.pop_back();
		size_t a = 0;
		while (a < cur.size() && cur[a] == ' ') a++;
		if (a < cur.size()) parts.push_back(cur.substr(a));
		cur.clear();
	}
	return parts;
}

int TextCapsWidth(Painter &p, const std::string &key)
{
	p.Font(4);
	int w = 0;
	const std::vector<std::string> parts = TextCaps(key);
	for (size_t i = 0; i < parts.size(); i++) w += CapWidth(p, parts[i]) + (i ? kPlusW : 0);
	return w;
}

void DrawTextCaps(Painter &p, int x, int y, int h, const std::string &key, int maxW)
{
	const COLORREF edge = RGB(122, 98, 64), fg = RGB(240, 222, 170);
	if (TextCapsWidth(p, key) > maxW) {
		DrawCap(p, x, y, std::min(maxW, CapWidth(p, key)), h, key, 0, edge, fg);
		return;
	}
	const std::vector<std::string> parts = TextCaps(key);
	for (size_t i = 0; i < parts.size(); i++) {
		if (i) {
			p.Text(x, y, kPlusW, h - 2, "+", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			x += kPlusW;
		}
		const int w = CapWidth(p, parts[i]);
		DrawCap(p, x, y, w, h, parts[i], 0, edge, fg);
		x += w;
	}
}

void CenterWrapped(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c)
{
	RECT r = p.R(x, y, w, h);
	const std::wstring ws = W(text);
	SetTextColor(p.dc, c);
	DrawTextW(p.dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
}

bool SmallImage(const std::string &path)
{
	std::string l = path;
	for (char &c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return l.size() >= 7 && l.compare(l.size() - 7, 7, "ico.bmp") == 0;
}

struct Cell {
	std::string label, value, icon;
};

struct Card {
	int target = -1;
	std::string label, sub, image;
};

enum Kind { kKindHeading, kKindText, kKindIconText, kKindLink, kKindGrid, kKindImage, kKindKey, kKindStats, kKindMatchups, kKindResult, kKindNote, kKindLinks, kKindControl };

struct Block {
	Kind kind = kKindText;
	int y = 0, h = 0;
	std::vector<std::wstring> lines, more;
	std::string image;
	bool center = false, tiny = false;
	int target = -1, web = -1, keyW = 0;
	std::vector<Card> cards, strong, weak;
	std::vector<Cell> cells;
	std::wstring key;
	std::string title, context;
	std::vector<ControlKey> keys;
};

struct Page {
	std::wstring title;
	std::vector<Block> blocks;
	int height = 0;
};

struct Builder {
	Painter &p;
	const Content &c;
	const View &v;
	PaintOut &out;
	Page page;
	int y = 4;
	bool lastSpacer = true;

	int Target(const HelpPlace &place)
	{
		out.targets.push_back(place);
		return static_cast<int>(out.targets.size()) - 1;
	}

	void Add(Block b, int gap = kBlockGap)
	{
		y += page.blocks.empty() ? 0 : gap;
		b.y = y;
		y += b.h;
		page.blocks.push_back(std::move(b));
		lastSpacer = false;
	}

	void Spacer()
	{
		if (lastSpacer) return;
		y += 8;
		lastSpacer = true;
	}

	// The picture the listings show for a topic.
	std::string TopicIcon(int topic) const
	{
		const int l = c.index.listing[topic];
		if (l >= 0) {
			for (const HelpEntry &e : c.doc.topics[l].entries) {
				if (e.target == topic && !e.image.empty()) return e.image;
			}
		}
		for (const HelpEntry &e : c.doc.topics[topic].entries) {
			if (!e.image.empty() && !SmallImage(e.image)) return e.image;
		}
		return std::string();
	}

	std::string TopicContext(int topic) const
	{
		const int l = c.index.listing[topic], s = c.index.section[topic];
		return l >= 0 && s >= 0 && s < static_cast<int>(c.index.sections[l].size()) ? c.index.sections[l][s].heading : std::string();
	}

	void Heading(const std::string &text)
	{
		Block b;
		b.kind = kKindHeading;
		p.Font(2);
		b.lines = p.WrapLines(text, 0, kContentW);
		b.h = static_cast<int>(b.lines.size()) * 30 + 8;
		Add(b, kHeadingGap);
	}

	void Text(const std::string &text, bool center, COLORREF = kPlain)
	{
		Block b;
		b.kind = kKindText;
		b.center = center;
		p.Font(0);
		b.lines = Wrap(p, text, kContentW);
		b.h = LinesHeight(b.lines);
		Add(b);
	}

	void Note(const std::string &text)
	{
		Block b;
		b.kind = kKindNote;
		p.Font(3);
		b.lines = Wrap(p, text, kContentW);
		b.h = LinesHeight(b.lines, 18);
		Add(b, 8);
	}

	// A picture beside its text; the first line in bold when it names what the rest describes.
	void IconText(const HelpEntry &e, bool withImage, int target, int web)
	{
		Block b;
		b.kind = target >= 0 || web >= 0 ? kKindLink : withImage ? kKindIconText : kKindText;
		b.image = withImage ? e.image : std::string();
		b.tiny = SmallImage(e.image);
		b.target = target;
		b.web = web;
		const int boxW = b.image.empty() ? 0 : b.tiny ? kSmall : kBigW, boxH = b.image.empty() ? 0 : b.tiny ? kSmall : kBigH;
		const int textW = kContentW - (boxW ? boxW + 14 : 0);
		const std::string first = FirstLine(e.text), rest = RestLines(e.text);
		const bool named = !rest.empty() && first.size() <= 60;
		if (b.kind == kKindLink || named) {
			p.Font(b.kind == kKindLink ? 0 : 1);
			b.lines = p.WrapLines(first, 0, textW);
			p.Font(0);
			if (!rest.empty()) b.more = Wrap(p, rest, textW);
		}
		else {
			p.Font(0);
			b.lines = Wrap(p, e.text, textW);
		}
		b.h = std::max(boxH, LinesHeight(b.lines) + LinesHeight(b.more));
		const bool listed = b.kind == kKindLink && !page.blocks.empty() && page.blocks.back().kind == kKindLink && b.more.empty();
		Add(b, listed ? 6 : kBlockGap);
	}

	void Grid(std::vector<Card> cards)
	{
		Block b;
		b.kind = kKindGrid;
		const int cols = std::max(1, (kContentW + kCardGap) / (kCardW + kCardGap));
		const int rows = (static_cast<int>(cards.size()) + cols - 1) / cols;
		b.cards = std::move(cards);
		b.h = rows * (kCardH + kCardGap) - kCardGap;
		Add(b, kBlockGap + 2);
	}

	void Entries(int topic, int from, int to, const std::string &skipImage)
	{
		const std::vector<HelpEntry> &es = c.doc.topics[topic].entries;
		const bool keys = HelpIsKeyTopic(c.doc, topic);
		int keyW = 0;
		if (keys) {
			p.Font(4);
			for (const HelpEntry &e : es) {
				std::string k, w;
				// A long key goes over its text instead of widening the column for all.
				if (!e.large && e.link.empty() && e.image.empty() && HelpSplitKey(e.text, k, w) && TextCapsWidth(p, k) + 16 <= kKeyW) keyW = std::max(keyW, TextCapsWidth(p, k));
			}
			keyW = std::max(70, keyW + 16);
		}
		const auto simpleLink = [&](const HelpEntry &e) { return e.target >= 0 && e.image.empty() && !e.text.empty() && e.text.find('\n') == std::string::npos; };
		const auto cardable = [&](const HelpEntry &e) { return e.target >= 0 && !e.image.empty() && e.text.find('\n') == std::string::npos && !e.text.empty(); };
		for (int i = from; i < to; i++) {
			const HelpEntry &e = es[i];
			if (e.large && !e.text.empty()) {
				Heading(FirstLine(e.text));
				continue;
			}
			if (cardable(e)) {
				int j = i;
				while (j < to && cardable(es[j])) j++;
				if (j - i >= 3) {
					std::vector<Card> cards;
					for (int k = i; k < j; k++) cards.push_back({Target({es[k].target, -1, std::string()}), FirstLine(es[k].text), std::string(), es[k].image});
					Grid(std::move(cards));
					i = j - 1;
					continue;
				}
			}
			if (simpleLink(e)) {
				int j = i;
				while (j < to && simpleLink(es[j])) j++;
				if (j - i >= 4) {
					Block b;
					b.kind = kKindLinks;
					for (int k = i; k < j; k++) b.cards.push_back({Target({es[k].target, -1, std::string()}), FirstLine(es[k].text), std::string(), std::string()});
					b.h = (static_cast<int>(b.cards.size()) + 1) / 2 * kLinkRowH;
					Add(b);
					i = j - 1;
					continue;
				}
			}
			if (e.target >= 0 || e.web) {
				if (e.text.empty()) continue;
				int web = -1;
				if (e.web) {
					out.web.push_back(e.link);
					web = static_cast<int>(out.web.size()) - 1;
				}
				IconText(e, !e.image.empty(), e.target >= 0 ? Target({e.target, -1, std::string()}) : -1, web);
				continue;
			}
			const bool image = !e.image.empty() && e.image != skipImage;
			if (e.text.empty()) {
				if (image) {
					Block b;
					b.kind = kKindImage;
					b.image = e.image;
					b.tiny = SmallImage(e.image);
					b.h = b.tiny ? kSmall : kIconH;
					Add(b);
				}
				else {
					Spacer();
				}
				continue;
			}
			std::string key, what;
			if (keys && !image && HelpSplitKey(e.text, key, what)) {
				Block b;
				b.kind = kKindKey;
				b.key = W(key);
				const bool stacked = TextCapsWidth(p, key) + 16 > keyW;
				b.keyW = stacked ? 0 : keyW;
				p.Font(0);
				b.lines = p.WrapLines(what, 0, kContentW - b.keyW - 12);
				b.h = std::max(30, LinesHeight(b.lines) + 8) + (stacked ? 30 : 0);
				Add(b, 4);
				continue;
			}
			if (image) IconText(e, true, -1, -1);
			else Text(e.text, e.hcenter);
		}
	}

	std::vector<Card> Chips(const std::vector<HelpCounter> &list, int self)
	{
		std::vector<Card> cards;
		std::set<int> seen;
		for (const HelpCounter &k : list) {
			std::string cls = k.cls;
			for (char &ch : cls) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
			const auto it = c.classTopic.find(cls);
			if (it == c.classTopic.end() || it->second < 0 || it->second == self || !seen.insert(it->second).second) continue;
			cards.push_back({Target({it->second, -1, std::string()}), c.index.titles[it->second], TopicContext(it->second), TopicIcon(it->second)});
			if (static_cast<int>(cards.size()) == kChips) break;
		}
		return cards;
	}

	void Stats(int topic, const HelpUnitStats &s)
	{
		Block b;
		b.kind = kKindStats;
		b.image = TopicIcon(topic);
		auto num = [](int v) { return std::to_string(v); };
		if (s.health >= 0) b.cells.push_back({Tr("Health"), num(s.health), kIconHealth});
		if (s.damage >= 0) {
			std::string kind = s.damageType == kHelpDamageSlash ? Tr("slash") : s.damageType == kHelpDamagePierce ? Tr("pierce") : s.damageType == kHelpDamageSiege ? Tr("siege") : std::string();
			b.cells.push_back({Tr("Damage"), num(s.damage) + (kind.empty() ? "" : "  \xC2\xB7  " + kind), s.damageType == kHelpDamagePierce ? kIconPierce : kIconSlash});
		}
		if (s.armorSlash >= 0) b.cells.push_back({Tr("Slash armour"), num(s.armorSlash), kIconDefense});
		if (s.armorPierce >= 0) b.cells.push_back({Tr("Pierce armour"), num(s.armorPierce), kIconDefense});
		if (s.speed >= 0) b.cells.push_back({Tr("Speed"), num(s.speed), kIconSpeed});
		if (s.sight >= 0) b.cells.push_back({Tr("Sight"), num(s.sight), kIconSight});
		if (HelpUnitIsRanged(s) && s.range > 0) b.cells.push_back({Tr("Range"), num(s.range), kIconPierce});
		if (s.stamina > 0) b.cells.push_back({Tr("Stamina"), num(s.stamina), std::string()});
		if (s.costGold > 0 || s.costFood > 0) {
			std::string cost;
			if (s.costGold > 0) cost = TrFmt("%s gold", {num(s.costGold)});
			if (s.costFood > 0) cost += (cost.empty() ? "" : "  \xC2\xB7  ") + TrFmt("%s food", {num(s.costFood)});
			b.cells.push_back({Tr("Cost"), cost, kIconGold});
		}
		if (s.trainMs > 0) {
			char secs[32];
			sprintf_s(secs, s.trainMs % 1000 ? "%.1f" : "%.0f", s.trainMs / 1000.0);
			b.cells.push_back({Tr("Training time"), TrFmt("%s s", {secs}), kIconTrain});
		}
		for (int at : s.recruitedAt) b.cards.push_back({Target({at, -1, std::string()}), c.index.titles[at], std::string(), std::string()});
		const int rows = (static_cast<int>(b.cells.size()) + 2) / 3;
		b.h = 16 + std::max(kIconH + 4, rows * kCellH) + (b.cards.empty() ? 0 : 34) + 14;
		Add(b);
		const HelpMatchups m = HelpMatchupsFor(c.counters, s.classId);
		Block mb;
		mb.kind = kKindMatchups;
		mb.strong = Chips(m.strong, topic);
		mb.weak = Chips(m.weak, topic);
		if (mb.strong.empty() && mb.weak.empty()) return;
		mb.h = (mb.strong.empty() ? 0 : 24 + kChipH + 8) + (mb.weak.empty() ? 0 : 24 + kChipH + 8) + 20;
		Add(mb, 16);
	}

	// The player's own keys, page by page as the Controls screen has them; the orders of units and buildings stay there.
	void Controls(const std::vector<ControlInfo> &controls)
	{
		Note(Tr("Your keys as they are set in Controls, where the orders of units and buildings are too."));
		for (int page = 0; page < kPageCount; page++) {
			if (ControlsPageIsOrders(page)) continue;
			bool first = true;
			for (const ControlInfo &r : controls) {
				if (r.order || r.page != page) continue;
				if (first) Heading(Tr(ControlsPageName(page)));
				Block b;
				b.kind = kKindControl;
				b.title = Tr(r.name);
				b.context = r.hint.empty() ? std::string() : Tr(r.hint);
				b.keys = KnownKeys({r.key, r.key2});
				b.h = b.context.empty() ? 32 : 46;
				Add(b, first ? kBlockGap : 0);
				first = false;
			}
		}
	}

	void Results()
	{
		const std::vector<HelpHit> none;
		const std::vector<HelpHit> &hits = v.hits ? *v.hits : none;
		if (hits.empty()) {
			Text(TrFmt("Nothing in the help matches \xE2\x80\x9C%s\xE2\x80\x9D.", {v.query}), false);
			return;
		}
		Note(hits.size() == 1 ? Tr("1 result") : TrFmt("%s results", {std::to_string(hits.size())}));
		for (const HelpHit &h : hits) {
			Block b;
			b.kind = kKindResult;
			const HelpPlace place = {h.topic, h.section, std::string()};
			b.target = Target(place);
			b.title = !h.title.empty() ? h.title : h.section >= 0 ? c.index.sections[h.topic][h.section].heading : c.index.titles[h.topic];
			if (h.section < 0 && h.title.empty()) b.image = TopicIcon(h.topic);
			const int textW = kContentW - 24 - (b.image.empty() ? 0 : kResultIconW + 14);
			const std::vector<HelpCrumb> crumbs = HelpCrumbs(c.doc, c.index, place);
			if (!h.title.empty()) {
				b.context = c.index.titles[h.topic] + (h.context.empty() ? "" : "  \xE2\x80\xBA  " + h.context);
			}
			else {
				for (size_t i = 1; i + 1 < crumbs.size(); i++) b.context += (b.context.empty() ? "" : "  \xE2\x80\xBA  ") + crumbs[i].label;
			}
			p.Font(0);
			if (!h.snippet.empty()) {
				b.lines = p.WrapLines(h.snippet, 0, textW);
				if (b.lines.size() > 2) {
					b.lines.resize(2);
					b.lines.back() += L" \x2026";
				}
			}
			b.h = std::max(12 + 24 + (b.context.empty() ? 0 : 18) + static_cast<int>(b.lines.size()) * 21 + 10, b.image.empty() ? 0 : kResultIconH + 16);
			Add(b, 8);
		}
	}

	void Build()
	{
		const HelpPlace &pl = v.place;
		const int n = static_cast<int>(c.doc.topics.size());
		if (!pl.query.empty()) {
			page.title = W(Tr("Search results"));
			Results();
		}
		else if (pl.topic < 0 || pl.topic >= n) {
			page.title = W(Tr("Help"));
			Text(Tr("The game's help could not be read."), false);
		}
		else if (pl.section >= 0 && pl.section < static_cast<int>(c.index.sections[pl.topic].size())) {
			const HelpSection &s = c.index.sections[pl.topic][pl.section];
			page.title = W(s.heading);
			Entries(pl.topic, s.first + 1, s.end, std::string());
		}
		else {
			const std::string &title = c.index.titles[pl.topic];
			page.title = W(pl.topic == c.index.home ? Tr("Contents") : title);
			const std::vector<HelpEntry> &es = c.doc.topics[pl.topic].entries;
			int from = !es.empty() && es[0].large && FirstLine(es[0].text) == title ? 1 : 0;
			std::string skip;
			const auto stats = c.stats.find(pl.topic);
			if (stats != c.stats.end()) {
				Stats(pl.topic, stats->second);
				skip = TopicIcon(pl.topic);
			}
			if (pl.topic == c.index.keys && v.controls && !v.controls->empty()) Controls(*v.controls);
			else Entries(pl.topic, from, static_cast<int>(es.size()), skip);
		}
		page.height = y + 8;
	}
};

// Null while it loads; set and empty when the game has no such picture.
std::shared_ptr<const BattleImage> Image(const View &v, PaintOut &out, const std::string &path, bool &missing)
{
	missing = false;
	if (path.empty()) return nullptr;
	const std::string key = ImageKey(path);
	if (v.images) {
		const auto it = v.images->find(key);
		if (it != v.images->end()) {
			missing = !it->second || it->second->w <= 0;
			return it->second;
		}
	}
	if (std::find(out.wanted.begin(), out.wanted.end(), path) == out.wanted.end()) out.wanted.push_back(path);
	return nullptr;
}

// The picture scaled to w x h, premultiplied for GdiAlphaBlend: the transparent colour of the game's bitmaps shows the card behind.
HBITMAP ScaledBitmap(const std::shared_ptr<const BattleImage> &img, int w, int h)
{
	struct Entry {
		std::shared_ptr<const BattleImage> img;
		HBITMAP bmp;
	};
	static std::map<std::tuple<const BattleImage *, int, int>, Entry> cache;
	const auto key = std::make_tuple(img.get(), w, h);
	const auto it = cache.find(key);
	if (it != cache.end()) return it->second.bmp;
	if (cache.size() > 600) {
		for (auto &kv : cache) DeleteObject(kv.second.bmp);
		cache.clear();
	}
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp || !bits) return nullptr;
	uint32_t *dst = static_cast<uint32_t *>(bits);
	// Each pixel of the box averages the source pixels it covers, alpha included.
	for (int y = 0; y < h; y++) {
		const int sy0 = y * img->h / h, sy1 = std::max(sy0 + 1, (y + 1) * img->h / h);
		for (int x = 0; x < w; x++) {
			const int sx0 = x * img->w / w, sx1 = std::max(sx0 + 1, (x + 1) * img->w / w);
			uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					const uint32_t c = img->bgra[static_cast<size_t>(sy) * img->w + sx], ca = c >> 24;
					a += ca;
					r += (c >> 16 & 255) * ca / 255;
					g += (c >> 8 & 255) * ca / 255;
					b += (c & 255) * ca / 255;
					n++;
				}
			}
			dst[static_cast<size_t>(y) * w + x] = (a / n) << 24 | (r / n) << 16 | (g / n) << 8 | b / n;
		}
	}
	cache[key] = {img, bmp};
	return bmp;
}

// Pictures shrink to their box and never grow; a big one gets a thin frame, and a well while it loads.
void DrawIcon(Painter &p, const View &v, PaintOut &out, const std::string &path, int x, int y, int boxW, int boxH, bool frame)
{
	bool missing = false;
	const std::shared_ptr<const BattleImage> img = Image(v, out, path, missing);
	if (!img || img->w <= 0 || img->h <= 0) {
		if (frame && !missing) {
			p.Fill(x, y, boxW, boxH, pal::well);
			p.Frame(x, y, boxW, boxH, pal::lineDim);
		}
		return;
	}
	const float scale = std::min(1.0f, std::min(boxW / static_cast<float>(img->w), boxH / static_cast<float>(img->h)));
	const int w = std::max(1, static_cast<int>(img->w * scale + 0.5f)), h = std::max(1, static_cast<int>(img->h * scale + 0.5f));
	const int ix = x + (boxW - w) / 2, iy = y + (boxH - h) / 2;
	if (frame) {
		p.Fill(ix, iy, w, h, pal::well);
		p.Frame(ix - 1, iy - 1, w + 2, h + 2, pal::line);
	}
	const RECT r = p.R(ix, iy, w, h);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	HBITMAP bmp = pw > 0 && ph > 0 ? ScaledBitmap(img, pw, ph) : nullptr;
	if (!bmp) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

struct Pane {
	Painter &p;
	const View &v;
	PaintOut &out;
	int scroll = 0;

	int Top(const Block &b) const { return kViewTop + b.y - scroll; }
	bool Visible(int y, int h) const { return y + h > kViewTop && y < kPaneBottom; }
	bool Inside() const
	{
		RECT r = out.content;
		POINT pt = {v.mouseX, v.mouseY};
		return PtInRect(&r, pt) != 0;
	}
	bool Hot(int x, int y, int w, int h) const { return Inside() && p.Hover(x, y, w, h, v.mouseX, v.mouseY); }
	void Hit(int x, int y, int w, int h, int action, int arg)
	{
		const int top = std::max(y, kViewTop), bottom = std::min(y + h, kPaneBottom);
		if (bottom > top) p.AddHit(x, top, w, bottom - top, action, arg);
	}

	void Heading(const Block &b, int x, int y)
	{
		p.Font(2);
		DrawLines(p, x, y, kContentW, b.lines, pal::goldLight, false, 30);
		const int lineY = y + static_cast<int>(b.lines.size()) * 30 + 2;
		p.FillH(x, lineY, kContentW, 1, pal::line, pal::panel);
	}

	void Link(const Block &b, int x, int y)
	{
		const int boxW = b.image.empty() ? 0 : b.tiny ? kSmall : kBigW, boxH = b.image.empty() ? 0 : b.tiny ? kSmall : kBigH;
		const int textX = x + (boxW ? boxW + 14 : 0), textW = kContentW - (textX - x);
		const int textH = LinesHeight(b.lines);
		const bool clickable = b.kind == kKindLink;
		int firstW = 0;
		if (clickable) {
			SelectObject(p.dc, LinkFont(p.s));
			for (const std::wstring &l : b.lines) firstW = std::max(firstW, PixelWidth(p, l, 0));
			firstW = static_cast<int>(firstW / p.s + 1);
		}
		const int rowH = std::max(boxH, textH + LinesHeight(b.more));
		const bool hot = clickable && Hot(x, y, (boxW ? boxW + 14 : 0) + firstW + 6, std::max(boxH, textH));
		if (boxW) DrawIcon(p, v, out, b.image, x, y + (b.tiny ? 1 : 0), boxW, boxH, !b.tiny);
		const int textY = boxW && !b.tiny && b.more.empty() && textH < boxH ? y + (boxH - textH) / 2 : y;
		if (clickable) {
			SelectObject(p.dc, LinkFont(p.s));
			DrawLines(p, textX, textY, textW, b.lines, hot ? kLinkHot : kLink, false);
		}
		else {
			p.Font(b.more.empty() ? 0 : 1);
			DrawLines(p, textX, textY, textW, b.lines, b.more.empty() ? kPlain : kText, false);
		}
		p.Font(0);
		DrawLines(p, textX, textY + textH, textW, b.more, kPlain, false);
		if (clickable) Hit(x, y, (boxW ? boxW + 14 : 0) + firstW + 6, std::max(boxH, textH), b.web >= 0 ? kActWeb : kActGo, b.web >= 0 ? b.web : b.target);
		(void)rowH;
	}

	void Card(const helpui::Card &cd, int x, int y, int w, int h)
	{
		const bool hot = Hot(x, y, w, h);
		p.Fill(x, y, w, h, hot ? kCardHot : kCardBg);
		p.Frame(x, y, w, h, hot ? pal::edgeHot : pal::lineDim);
		DrawIcon(p, v, out, cd.image, x + (w - kIconW) / 2, y + 10, kIconW, kIconH, true);
		p.Font(4);
		CenterWrapped(p, x + 6, y + kIconH + 18, w - 12, h - kIconH - 20, cd.label, hot ? pal::goldLight : kText);
		Hit(x, y, w, h, kActGo, cd.target);
	}

	void Grid(const Block &b, int x, int y)
	{
		const int cols = std::max(1, (kContentW + kCardGap) / (kCardW + kCardGap));
		for (size_t i = 0; i < b.cards.size(); i++) {
			const int cx = x + static_cast<int>(i % cols) * (kCardW + kCardGap), cy = y + static_cast<int>(i / cols) * (kCardH + kCardGap);
			if (Visible(cy, kCardH)) Card(b.cards[i], cx, cy, kCardW, kCardH);
		}
	}

	void Chip(const helpui::Card &cd, int x, int y, int w)
	{
		const bool hot = Hot(x, y, w, kChipH);
		p.Fill(x, y, w, kChipH, hot ? kCardHot : kCardBg);
		p.Frame(x, y, w, kChipH, hot ? pal::edgeHot : pal::lineDim);
		DrawIcon(p, v, out, cd.image, x + 5, y + 4, 36, 42, false);
		p.Font(4);
		p.Text(x + 48, y + 6, w - 54, 20, cd.label, hot ? pal::goldLight : kText);
		p.Font(3);
		p.Text(x + 48, y + 25, w - 54, 18, cd.sub, kDim);
		Hit(x, y, w, kChipH, kActGo, cd.target);
	}

	void Caption(int x, int y, const std::string &text, COLORREF c)
	{
		p.Font(4);
		SpacedLine(p, x, y, 20, Upper(W(text)), 2, c);
	}

	void Matchups(const Block &b, int x, int y)
	{
		const int chipW = (kContentW - (kChips - 1) * kChipGap) / kChips;
		const auto row = [&](const std::vector<helpui::Card> &cards, const std::string &label, COLORREF c) {
			if (cards.empty()) return;
			Caption(x, y, label, c);
			y += 24;
			for (size_t i = 0; i < cards.size(); i++) Chip(cards[i], x + static_cast<int>(i) * (chipW + kChipGap), y, chipW);
			y += kChipH + 8;
		};
		row(b.strong, Tr("Strong against"), kOk);
		row(b.weak, Tr("Weak against"), kBad);
		p.Font(3);
		p.Text(x, y, kContentW, 18, Tr("From the counters the computer players recruit"), RGB(140, 128, 102));
	}

	void Stats(const Block &b, int x, int y)
	{
		p.FillV(x, y, kContentW, std::min(b.h, 60), RGB(40, 30, 18), kCardBg);
		if (b.h > 60) p.Fill(x, y + 60, kContentW, b.h - 60, kCardBg);
		p.Frame(x, y, kContentW, b.h, pal::line);
		PaintFrameCorners(p, x, y, kContentW, b.h, 12);
		DrawIcon(p, v, out, b.image, x + 18, y + 18, kIconW, kIconH, true);
		const int gridX = x + 18 + kIconW + 26, gridW = kContentW - (gridX - x) - 14, cellW = gridW / 3;
		for (size_t i = 0; i < b.cells.size(); i++) {
			const Cell &c = b.cells[i];
			const int cx = gridX + static_cast<int>(i % 3) * cellW, cy = y + 16 + static_cast<int>(i / 3) * kCellH;
			int tx = cx;
			if (!c.icon.empty()) {
				DrawIcon(p, v, out, c.icon, cx, cy + 1, 18, 18, false);
				tx += 24;
			}
			p.Font(3);
			p.Text(tx, cy, cellW - (tx - cx) - 6, 20, c.label, kDim);
			p.Font(1);
			p.Text(tx, cy + 19, cellW - (tx - cx) - 6, 22, c.value, pal::goldLight);
		}
		if (b.cards.empty()) return;
		const int rows = (static_cast<int>(b.cells.size()) + 2) / 3;
		const int ry = y + 16 + std::max(kIconH + 4, rows * kCellH) + 6;
		p.Fill(x + 14, ry, kContentW - 28, 1, pal::lineDim);
		p.Font(3);
		const std::string label = Tr("Recruited at");
		const int lw = p.TextWidth(label);
		p.Text(x + 18, ry + 8, lw + 4, 22, label, kDim);
		int lx = x + 18 + lw + 14;
		SelectObject(p.dc, LinkFont(p.s));
		for (const helpui::Card &cd : b.cards) {
			const int w = static_cast<int>(PixelWidth(p, W(cd.label), 0) / p.s + 1);
			if (lx + w > x + kContentW - 14) break;
			const bool hot = Hot(lx, ry + 8, w, 22);
			SelectObject(p.dc, LinkFont(p.s));
			p.LineW(lx, ry + 10, w + 4, 22, W(cd.label), hot ? kLinkHot : kLink);
			Hit(lx, ry + 8, w, 22, kActGo, cd.target);
			lx += w + 22;
		}
	}

	void Result(const Block &b, int x, int y)
	{
		const bool hot = Hot(x, y, kContentW, b.h);
		if (hot) p.FillH(x, y, kContentW, b.h, kHotBg, pal::panel);
		p.Fill(x, y, 3, b.h, hot ? kGold : pal::lineDim);
		int tx = x + 16;
		if (!b.image.empty()) {
			DrawIcon(p, v, out, b.image, tx, y + 8, kResultIconW, kResultIconH, false);
			tx += kResultIconW + 14;
		}
		const int tw = kContentW - 8 - (tx - x);
		int ty = y + 10;
		if (hot) SelectObject(p.dc, LinkFont(p.s));
		else p.Font(1);
		p.Text(tx, ty, tw, 24, b.title, hot ? kLinkHot : pal::goldLight);
		ty += 24;
		if (!b.context.empty()) {
			p.Font(3);
			p.Text(tx, ty, tw, 18, b.context, kDim);
			ty += 18;
		}
		p.Font(0);
		DrawLines(p, tx, ty, tw, b.lines, kPlain, false, 21);
		Hit(x, y, kContentW, b.h, kActGo, b.target);
	}

	void Links(const Block &b, int x, int y)
	{
		const int colW = (kContentW - 20) / 2, rows = (static_cast<int>(b.cards.size()) + 1) / 2;
		for (size_t i = 0; i < b.cards.size(); i++) {
			const int cx = x + static_cast<int>(i / rows) * (colW + 20), cy = y + static_cast<int>(i % rows) * kLinkRowH;
			const std::wstring label = W(b.cards[i].label);
			SelectObject(p.dc, LinkFont(p.s));
			const int w = std::min(colW - 16, PixelWidthDesign(label));
			const bool hot = Hot(cx, cy, w + 16, kLinkRowH);
			p.Diamond(cx + 4, cy + kLinkRowH / 2, 3, hot ? pal::focus : pal::bronze);
			SelectObject(p.dc, LinkFont(p.s));
			RECT r = p.R(cx + 16, cy, colW - 16, kLinkRowH);
			SetTextColor(p.dc, hot ? kLinkHot : kLink);
			DrawTextW(p.dc, label.c_str(), static_cast<int>(label.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			Hit(cx, cy, w + 16, kLinkRowH, kActGo, b.cards[i].target);
		}
	}

	void Key(const Block &b, int x, int y)
	{
		const bool stacked = b.keyW == 0;
		DrawTextCaps(p, x, y + 1, kCapH, Utf8(b.key), stacked ? kContentW : b.keyW - 8);
		p.Font(0);
		if (stacked) DrawLines(p, x + 12, y + 31, kContentW - 12, b.lines, kPlain, false);
		else DrawLines(p, x + b.keyW + 12, y + 2, kContentW - b.keyW - 12, b.lines, kPlain, false);
	}

	void Control(const Block &b, int x, int y)
	{
		p.Fill(x, y + b.h - 1, kContentW, 1, RGB(44, 34, 23));
		// The caps say Shift or Mayús in the player's language, as on the Controls screen.
		p.raw = false;
		const int keysW = b.keys.empty() ? 0 : KeysWidth(p, b.keys);
		p.raw = true;
		const int nameW = kContentW - std::max(keysW, 90) - 24;
		p.Font(0);
		if (b.context.empty()) {
			p.Text(x + 6, y, nameW, b.h, b.title, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		else {
			p.Text(x + 6, y + 4, nameW, 20, b.title, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(3);
			p.Text(x + 6, y + 24, nameW, 16, b.context, kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if (b.keys.empty()) {
			p.Font(3);
			p.Text(x + kContentW - 96, y, 90, b.h, Tr("No key"), RGB(140, 124, 96), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			return;
		}
		p.raw = false;
		DrawKeys(p, x + kContentW - 6 - keysW, y + (b.h - kCapH) / 2, kCapH, b.keys);
		p.raw = true;
	}

	int PixelWidthDesign(const std::wstring &w) { return static_cast<int>(PixelWidth(p, w, 0) / p.s + 1); }

	void Paint(const Page &page)
	{
		const int x = kPaneX;
		p.Clip(kPaneX - 4, kViewTop, kPaneW + 4, kPaneBottom - kViewTop);
		for (const Block &b : page.blocks) {
			const int y = Top(b);
			if (!Visible(y, b.h)) continue;
			switch (b.kind) {
			case kKindHeading: Heading(b, x, y); break;
			case kKindText:
				p.Font(0);
				DrawLines(p, x, y, kContentW, b.lines, kPlain, b.center);
				break;
			case kKindNote:
				p.Font(3);
				DrawLines(p, x, y, kContentW, b.lines, kDim, false, 18);
				break;
			case kKindIconText:
			case kKindLink: Link(b, x, y); break;
			case kKindGrid: Grid(b, x, y); break;
			case kKindImage: DrawIcon(p, v, out, b.image, x, y, b.tiny ? kSmall : kIconW, b.tiny ? kSmall : kIconH, !b.tiny); break;
			case kKindKey: Key(b, x, y); break;
			case kKindStats: Stats(b, x, y); break;
			case kKindMatchups: Matchups(b, x, y); break;
			case kKindResult: Result(b, x, y); break;
			case kKindLinks: Links(b, x, y); break;
			case kKindControl: Control(b, x, y); break;
			}
		}
		p.Unclip();
		if (page.height > out.viewH) {
			const int trackX = kPaneX + kPaneW - kScrollW, trackH = kPaneBottom - kViewTop;
			const int thumbH = std::max(30, trackH * out.viewH / page.height);
			const int maxScroll = std::max(1, page.height - out.viewH);
			const int thumbY = kViewTop + (trackH - thumbH) * scroll / maxScroll;
			p.Fill(trackX, kViewTop, kScrollW, trackH, pal::well);
			p.Fill(trackX, thumbY, kScrollW, thumbH, pal::thumb);
		}
	}
};

struct NavRow {
	int kind = 0;  // 0 group, 1 item, 2 section
	int g = -1, i = -1, s = -1;
	int y = 0, h = 0;
	std::string label;
	bool current = false, open = false, listing = false;
};

void PaintNav(Painter &p, const Content &c, const View &v, PaintOut &out)
{
	const HelpNavSpot spot = HelpNavFor(c.doc, c.index, v.place);
	std::vector<NavRow> rows;
	int y = 0;
	for (size_t g = 0; g < c.index.groups.size(); g++) {
		const HelpNavGroup &group = c.index.groups[g];
		if (!group.label.empty()) {
			NavRow r;
			r.kind = 0;
			r.y = y + (g ? 8 : 0);
			r.h = kGroupH - 6;
			r.label = group.label;
			rows.push_back(r);
			y = r.y + r.h;
		}
		for (size_t i = 0; i < group.items.size(); i++) {
			const HelpNavItem &item = group.items[i];
			const bool here = spot.group == static_cast<int>(g) && spot.item == static_cast<int>(i);
			NavRow r;
			r.kind = 1;
			r.g = static_cast<int>(g);
			r.i = static_cast<int>(i);
			r.y = y;
			r.h = kItemH;
			r.label = item.label;
			r.listing = !item.sections.empty();
			r.open = r.listing && (here != (v.toggled.count(r.g * 1000 + r.i) != 0));
			r.current = here && spot.section < 0;
			rows.push_back(r);
			y += r.h;
			if (!r.open) continue;
			for (size_t s = 0; s < item.sections.size(); s++) {
				NavRow sr;
				sr.kind = 2;
				sr.g = r.g;
				sr.i = r.i;
				sr.s = static_cast<int>(s);
				sr.y = y;
				sr.h = kSectionH;
				sr.label = item.sections[s];
				sr.current = here && spot.section == static_cast<int>(s);
				rows.push_back(sr);
				y += sr.h;
			}
		}
	}
	const int viewH = kNavBottom - kNavTop;
	out.navH = y;
	out.navViewH = viewH;
	int scroll = std::max(0, std::min(v.navScroll, std::max(0, y - viewH)));
	if (v.revealNav) {
		for (const NavRow &r : rows) {
			if (!r.current) continue;
			if (r.y < scroll) scroll = std::max(0, r.y - 40);
			else if (r.y + r.h > scroll + viewH) scroll = std::min(std::max(0, y - viewH), r.y + r.h - viewH + 40);
		}
	}
	out.navScroll = scroll;
	RECT nav = p.R(kNavX, kNavTop, kNavW, viewH);
	OffsetRect(&nav, p.ox, p.oy);
	out.nav = nav;
	const POINT mouse = {v.mouseX, v.mouseY};
	const bool inside = PtInRect(&nav, mouse) != 0;
	p.Clip(kNavX - 2, kNavTop, kNavW + 4, viewH);
	for (const NavRow &r : rows) {
		const int ry = kNavTop + r.y - scroll;
		if (ry + r.h <= kNavTop || ry >= kNavBottom) continue;
		const int hitTop = std::max(ry, kNavTop), hitH = std::min(ry + r.h, kNavBottom) - hitTop;
		if (r.kind == 0) {
			p.Font(4);
			const std::wstring up = Upper(W(r.label));
			SpacedLine(p, kNavX + 4, ry + 6, 20, up, 2, pal::section);
			const int tw = static_cast<int>(PixelWidth(p, up, 2) / p.s) + 14;
			if (tw < kNavW - 8) p.FillH(kNavX + 4 + tw, ry + 16, kNavW - 8 - tw, 1, pal::line, pal::panel);
			continue;
		}
		const bool hot = inside && !r.current && p.Hover(kNavX, ry, kNavW, r.h, v.mouseX, v.mouseY);
		if (r.current) {
			p.FillH(kNavX, ry, kNavW, r.h, kCurrentBg, pal::panel);
			p.Fill(kNavX, ry + 3, 3, r.h - 6, kGold);
		}
		else if (hot) {
			p.FillH(kNavX, ry, kNavW, r.h, kHotBg, pal::panel);
		}
		const int textX = kNavX + (r.kind == 2 ? 30 : 14), textW = kNavW - (textX - kNavX) - (r.listing ? kToggleW : 6);
		p.Font(r.current ? 1 : 0);
		p.Text(textX, ry, textW, r.h, r.label, r.current ? pal::goldLight : hot ? kText : r.kind == 2 ? RGB(176, 162, 130) : kDim);
		const HelpPlace target = r.kind == 2 ? HelpPlace{c.index.groups[r.g].items[r.i].topic, r.s, std::string()}
											: HelpPlace{c.index.groups[r.g].items[r.i].topic, -1, std::string()};
		out.targets.push_back(target);
		if (hitH > 0) p.AddHit(kNavX, hitTop, kNavW - (r.listing ? kToggleW : 0), hitH, kActGo, static_cast<int>(out.targets.size()) - 1);
		if (r.listing) {
			const int ax = kNavX + kNavW - kToggleW + 8, ay = ry + r.h / 2;
			const bool arrowHot = inside && p.Hover(kNavX + kNavW - kToggleW, ry, kToggleW, r.h, v.mouseX, v.mouseY);
			const COLORREF ac = arrowHot ? pal::focus : kGold;
			if (r.open) p.ArrowDown(ax, ay - 3, 9, 6, ac);
			else p.ArrowRight(ax + 2, ay - 5, 6, 9, ac);
			if (hitH > 0) p.AddHit(kNavX + kNavW - kToggleW, hitTop, kToggleW, hitH, kActToggle, r.g * 1000 + r.i);
		}
	}
	p.Unclip();
	if (y > viewH) {
		const int trackX = kNavX + kNavW + 5, thumbH = std::max(30, viewH * viewH / y);
		const int thumbY = kNavTop + (viewH - thumbH) * scroll / std::max(1, y - viewH);
		p.Fill(trackX, kNavTop, 4, viewH, pal::well);
		p.Fill(trackX, thumbY, 4, thumbH, pal::thumb);
	}
}

void NavButton(Painter &p, int x, int y, bool left, bool enabled, int action, const View &v)
{
	const int w = 34, h = kBarH;
	const bool hot = enabled && p.Hover(x, y, w, h, v.mouseX, v.mouseY);
	const bool down = enabled && p.pressAction == action;
	if (!enabled) p.Fill(x, y, w, h, RGB(30, 24, 17));
	else p.FillV(x, y, w, h, down ? RGB(26, 19, 11) : hot ? RGB(82, 63, 38) : RGB(62, 47, 28), down ? RGB(44, 34, 21) : hot ? RGB(54, 41, 24) : RGB(36, 27, 16));
	p.Frame(x, y, w, h, !enabled ? pal::disabledEdge : hot ? pal::edgeHot : pal::edge);
	const COLORREF c = !enabled ? RGB(96, 84, 64) : hot ? pal::focus : kGold;
	if (left) p.ArrowLeft(x + 12, y + 8, 9, 12, c);
	else p.ArrowRight(x + 13, y + 8, 9, 12, c);
	if (enabled) p.AddHit(x, y, w, h, action, 0);
}

void PaintBar(Painter &p, const Content *c, const View &v, PaintOut &out)
{
	NavButton(p, kPaneX, kBarY, true, v.canBack, kActBack, v);
	NavButton(p, kPaneX + 40, kBarY, false, v.canForward, kActForward, v);
	if (!c) return;
	std::vector<HelpCrumb> crumbs = HelpCrumbs(c->doc, c->index, v.place);
	if (!crumbs.empty()) crumbs[0].label = Tr("Help");
	if (!v.place.query.empty() && crumbs.size() > 1) crumbs.back().label = "\xE2\x80\x9C" + v.place.query + "\xE2\x80\x9D";
	int right = kPaneX + kPaneW;
	if (v.place.query.empty() && v.place.topic >= 0 && v.place.topic == c->index.keys && v.controls && !v.controls->empty()) {
		p.Font(3);
		const std::string label = Tr("Change in Controls");
		const int bw = p.TextWidth(label) + 32;
		right -= bw;
		p.Button(right, kBarY, bw, kBarH, label, kActControls, 0, v.mouseX, v.mouseY, true, false, 3);
		right -= 16;
	}
	p.Font(3);
	const std::string sep = "  \xE2\x80\xBA  ";
	const int x0 = kPaneX + 88, avail = right - x0;
	const int sepW = p.TextWidth(sep);
	auto total = [&]() {
		int t = 0;
		for (size_t i = 0; i < crumbs.size(); i++) t += p.TextWidth(crumbs[i].label) + (i ? sepW : 0);
		return t;
	};
	bool cut = false;
	while (crumbs.size() > 2 && total() > avail) {
		crumbs.erase(crumbs.begin() + 1);
		cut = true;
	}
	if (cut) crumbs.insert(crumbs.begin() + 1, {"\xE2\x80\xA6", HelpPlace{-2, -1, std::string()}});
	int x = x0;
	for (size_t i = 0; i < crumbs.size(); i++) {
		if (i) {
			p.Text(x, kBarY, sepW, kBarH, sep, RGB(120, 100, 66));
			x += sepW;
		}
		const bool last = i + 1 == crumbs.size(), dead = crumbs[i].place.topic == -2;
		const int w = std::min(p.TextWidth(crumbs[i].label), right - x);
		if (w <= 0) break;
		const bool hot = !last && !dead && p.Hover(x, kBarY, w, kBarH, v.mouseX, v.mouseY);
		p.Text(x, kBarY, w + 2, kBarH, crumbs[i].label, last ? kText : hot ? kLinkHot : kDim);
		if (!last && !dead) {
			out.targets.push_back(crumbs[i].place);
			p.AddHit(x, kBarY, w, kBarH, kActGo, static_cast<int>(out.targets.size()) - 1);
		}
		x += w;
	}
}

void PaintTitle(Painter &p, const std::wstring &title)
{
	const std::wstring up = Upper(title);
	p.Font(5);
	if (PixelWidth(p, up, 3) <= p.S(kPaneW)) {
		SpacedLine(p, kPaneX, kTitleY, kTitleH, up, 3, pal::goldLight);
		return;
	}
	p.Font(2);
	if (PixelWidth(p, up, 2) <= p.S(kPaneW)) {
		SpacedLine(p, kPaneX, kTitleY, kTitleH, up, 2, pal::goldLight);
		return;
	}
	RECT r = p.R(kPaneX, kTitleY, kPaneW, kTitleH);
	SetTextColor(p.dc, pal::goldLight);
	DrawTextW(p.dc, title.c_str(), static_cast<int>(title.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void PaintSearch(Painter &p, const View &v)
{
	PaintEditField(p, kNavX, kSearchY, kNavW, kSearchH, v.query, Tr("Search the help"), v.searchFocused, false, v.edit, kActSearch);
	if (v.query.empty()) {
		// A lens: a ring and its handle, solid.
		const int cx = kNavX + kNavW - 22, cy = kSearchY + 13;
		p.Ellipse(cx - 6, cy - 6, 12, 12, kDim);
		p.Ellipse(cx - 4, cy - 4, 8, 8, v.searchFocused ? RGB(30, 24, 16) : pal::well);
		p.Line(cx + 4, cy + 4, cx + 9, cy + 9, kDim, 2);
		return;
	}
	const int bx = kNavX + kNavW - 28;
	const bool hot = p.Hover(bx, kSearchY + 4, 24, kSearchH - 8, v.mouseX, v.mouseY);
	p.Fill(bx, kSearchY + 4, 24, kSearchH - 8, hot ? RGB(70, 54, 32) : RGB(44, 34, 21));
	p.Font(1);
	p.Text(bx, kSearchY + 4, 24, kSearchH - 8, "\xC3\x97", hot ? pal::focus : kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.AddHit(bx, kSearchY + 4, 24, kSearchH - 8, kActClearSearch, 0);
}

void PaintClose(Painter &p, const View &v)
{
	p.FillH(kNavX, kNavBottom + 6, kNavW, 1, pal::line, pal::panel);
	p.Button(kNavX, kCloseY, kNavW, kCloseH, Tr("Close"), kActClose, 0, v.mouseX, v.mouseY);
}

}

void KeyOut(BattleImage &img)
{
	if (img.w <= 0 || img.h <= 0 || img.bgra.empty()) return;
	// The game draws its bitmaps with the colour of their first pixel as the transparent one: green on the portraits, black on the
	// small icons, a lone dot in the corner of the structures.
	const uint32_t key = img.bgra[0] & 0xffffff;
	for (uint32_t &c : img.bgra) {
		if ((c & 0xffffff) == key) c = 0;
	}
}

std::vector<HelpRow> ControlRows(const std::vector<ControlInfo> &controls)
{
	std::vector<HelpRow> rows;
	for (const ControlInfo &r : controls) {
		if (r.order || ControlsPageIsOrders(r.page)) continue;
		std::string keys;
		for (const ControlKey &k : KnownKeys({r.key, r.key2})) {
			std::string label;
			for (const std::string &cap : CapTexts(k)) label += (label.empty() ? "" : " + ") + (cap.empty() ? ControlsKeyLabel({k.vk, 0}) : Tr(cap));
			keys += (keys.empty() ? "" : " / ") + label;
		}
		HelpRow row;
		row.title = Tr(r.name);
		row.context = Tr(ControlsPageName(r.page));
		row.text = keys.empty() ? Tr("No key") : keys;
		if (!r.hint.empty()) row.text += "  \xC2\xB7  " + Tr(r.hint);
		rows.push_back(row);
	}
	return rows;
}

std::string ImageKey(const std::string &path)
{
	std::string k = path;
	for (char &c : k) {
		c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return k;
}

void PaintHelpScreen(Painter &p, const View &v, PaintOut &out)
{
	out = PaintOut();
	// The help's own words are not keys of the mod's translations.
	p.raw = true;
	PaintWindowFrame(p, kScreenW, kScreenH);
	PaintWindowHeader(p, kScreenW, Utf8(Upper(W(Tr("Help")))));
	p.Fill(kDividerX, kTop + 4, 1, kPaneBottom - kTop - 4, pal::lineDim);
	PaintSearch(p, v);
	const Content *c = v.content.get();
	RECT content = p.R(kPaneX, kViewTop, kPaneW, kPaneBottom - kViewTop);
	OffsetRect(&content, p.ox, p.oy);
	out.content = content;
	out.viewH = kPaneBottom - kViewTop;
	if (c) PaintNav(p, *c, v, out);
	PaintBar(p, c, v, out);
	if (c) {
		Builder b{p, *c, v, out, {}};
		b.Build();
		out.contentH = b.page.height;
		PaintTitle(p, b.page.title);
		PaintOrnament(p, kOrnamentRule, kPaneX - 6, kRuleY - 5, kPaneW + 12, 10, kGold);
		Pane pane{p, v, out};
		pane.scroll = std::max(0, std::min(v.scroll, std::max(0, b.page.height - out.viewH)));
		out.scroll = pane.scroll;
		pane.Paint(b.page);
	}
	else {
		PaintTitle(p, W(Tr("Help")));
	}
	PaintClose(p, v);
	p.raw = false;
}

}
