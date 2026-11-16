// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/helpdialog.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "addr/script.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "core/log.h"
#include "game/cmdcheck.h"
#include "game/gamelayout.h"
#include "game/gamemenu.h"
#include "hooks/gamefiles.h"

namespace {

// gameini/Help/Help.ini: the Close button, whose click CHelpDlg::OnMessage (006bc820) answers by closing the dialog.
const int kIdClose = 0x1011;
const unsigned kFlagVisible = 2;
// CHelpDlg: +0x60 its CHelpDoc, +0x64 the topic shown (1-based, 0 = none).
const int kDlgDoc = 0x60, kDlgTopic = 0x64;
// CHelpDoc: +4/+8 the vector of 0x18-byte topics ({name, parent, next, child, entry, flags}, names as 1-based offsets into the text
// pool), +0x20 the std::string of the pool.
const int kDocTopicsBegin = 4, kDocTopicsEnd = 8, kDocPool = 0x20, kTopicSize = 0x18, kTopicParent = 4;
// CHelpDlg::OnKey (006b90a0) takes the game's key-up message for Escape.
const int kKeyUp = 0x202, kKeyEscape = 0x1b;
// CVXScriptClass (CVXScriptClass::OnXmlStartElement 0059f540, ResolveInheritance 0059c310): +4 id, +0x74 entity, +0xe4 parent,
// +0x1f8 its commands (a list of descriptor indices), +0x6e8 help. The numbers are -15000000 until a class or its parents set them.
const int kClassId = 4, kClassEntity = 0x74, kClassParent = 0xe4, kClassCommands = 0x1f8, kClassHelp = 0x6e8;
const int kClassHealth = 0x294, kClassStamina = 0x298, kClassDamage = 0x2a0, kClassDamageType = 0x2a4, kClassArmorSlash = 0x2ac,
		  kClassArmorPierce = 0x2b0, kClassSpeed = 0x2c8, kClassSight = 0x2d0, kClassRange = 0x2d4;
const int kUnset = -15000000;
// CCmdDesc (CCmdRegistry::OnXmlStartElement 004f2240): +0x60 sclass, +0x1b4 traincommand, +0x1e4 index, +0x1e8 gold, +0x1ec food,
// +0x1f0 population, +0x1f8 execdelay in milliseconds.
const int kDescClass = 0x60, kDescTrain = 0x1b4, kDescIndex = 0x1e4, kDescGold = 0x1e8, kDescFood = 0x1ec, kDescPop = 0x1f0, kDescDelay = 0x1f8;
const size_t kMaxClasses = 20000, kMaxCommands = 20000, kMaxListNodes = 4000;

typedef bool(__fastcall *HasFlags_t)(void *window, void *edx, unsigned flags);
typedef void(__fastcall *OnKey_t)(void *dlg, void *edx, int msg, short key);

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

int Field(const char *cls, int offset)
{
	const int v = *reinterpret_cast<const int *>(cls + offset);
	return v == kUnset ? -1 : v;
}

std::string PoolString(const char *doc, int offset)
{
	if (offset <= 0) return std::string();
	const char *pool = doc + kDocPool;
	const unsigned size = layout::StringSize(pool);
	const char *base = layout::StringChars(pool);
	if (!base || static_cast<unsigned>(offset - 1) >= size) return std::string();
	const char *s = base + offset - 1;
	return std::string(s, strnlen(s, size - (offset - 1)));
}

void CollectClasses(char *node, char *head, std::vector<char *> &out, int depth)
{
	if (!node || node == head || depth > 64 || out.size() >= kMaxClasses) return;
	CollectClasses(*reinterpret_cast<char **>(node), head, out, depth + 1);
	char *cls = *reinterpret_cast<char **>(node + 0x28);
	if (cls && GameStringAt(node + 0xc) == GameStringAt(cls + kClassId)) out.push_back(cls);
	CollectClasses(*reinterpret_cast<char **>(node + 8), head, out, depth + 1);
}

std::vector<char *> GameClasses()
{
	std::vector<char *> classes;
	char *registry = *reinterpret_cast<char **>(addr::kRegistry);
	if (!registry || !*reinterpret_cast<int *>(registry + 0xc)) return classes;
	char *head = *reinterpret_cast<char **>(registry + 4);
	if (head) CollectClasses(*reinterpret_cast<char **>(head + 4), head, classes, 0);
	return classes;
}

bool Offers(const char *cls, int index)
{
	char *head = *reinterpret_cast<char *const *>(cls + kClassCommands);
	if (!head) return false;
	size_t guard = 0;
	for (char *node = *reinterpret_cast<char **>(head); node && node != head && guard < kMaxListNodes; node = *reinterpret_cast<char **>(node), guard++) {
		if (*reinterpret_cast<int *>(node + 8) == index) return true;
	}
	return false;
}

// The first train command of each class, by lower-case class id.
std::map<std::string, const char *> TrainCommands()
{
	std::map<std::string, const char *> out;
	char *registry = *reinterpret_cast<char **>(addr::kCmdRegistry);
	if (!registry) return out;
	char **begin = *reinterpret_cast<char ***>(registry - 0x10);
	char **end = *reinterpret_cast<char ***>(registry - 0xc);
	if (!begin || end < begin || static_cast<size_t>(end - begin) > kMaxCommands) return out;
	for (char **it = begin; it != end; ++it) {
		const char *desc = *it;
		if (!desc || !*reinterpret_cast<const int *>(desc + kDescTrain)) continue;
		const std::string cls = Lower(GameStringAt(desc + kDescClass));
		if (!cls.empty() && !out.count(cls)) out[cls] = desc;
	}
	return out;
}

}

void *HelpDialogOpen()
{
	void *dlg = *reinterpret_cast<void **>(addr::kHelpDlg);
	if (!dlg) return nullptr;
	return reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(dlg, nullptr, kFlagVisible) ? dlg : nullptr;
}

int HelpDialogTopic(void *dlg)
{
	return dlg ? *reinterpret_cast<int *>(static_cast<char *>(dlg) + kDlgTopic) : 0;
}

std::string HelpDialogTopicPath(void *dlg)
{
	const char *doc = dlg ? *reinterpret_cast<char **>(static_cast<char *>(dlg) + kDlgDoc) : nullptr;
	if (!doc) return std::string();
	const char *begin = *reinterpret_cast<const char *const *>(doc + kDocTopicsBegin);
	const char *end = *reinterpret_cast<const char *const *>(doc + kDocTopicsEnd);
	if (!begin || end < begin) return std::string();
	const int count = static_cast<int>((end - begin) / kTopicSize);
	std::string path;
	int topic = HelpDialogTopic(dlg);
	for (int depth = 0; topic >= 1 && topic <= count && depth < 32; depth++) {
		const char *t = begin + (topic - 1) * kTopicSize;
		path = "/" + PoolString(doc, *reinterpret_cast<const int *>(t)) + path;
		topic = *reinterpret_cast<const int *>(t + kTopicParent);
	}
	return path;
}

bool HelpDialogClose(void *dlg)
{
	if (!dlg) return false;
	if (NativeButton(dlg, kIdClose).exists) {
		NativeButtonClick(dlg, kIdClose);
	}
	else {
		// A help.ini without the button: Escape closes the dialog all the same.
		LogW("ui", "the game's help has no Close button; closing it as Escape does");
		reinterpret_cast<OnKey_t>(addr::kHelpDlgOnKey)(dlg, nullptr, kKeyUp, static_cast<short>(kKeyEscape));
	}
	LogI("ui", "the help screen closes");
	return true;
}

bool HelpGameText(std::string &out)
{
	std::vector<char> data;
	if (!ReadGameFile("CurrentLang/help.xml", 1, data)) {
		LogW("ui", "could not read the game's help text");
		return false;
	}
	out.assign(data.begin(), data.end());
	return true;
}

bool HelpCounterText(std::string &out)
{
	std::vector<char> data;
	if (!ReadGameFile("data/CounterUnits.xml", 1, data)) return false;
	out.assign(data.begin(), data.end());
	return true;
}

bool HelpGameImage(const std::string &path, BattleImage &out)
{
	std::vector<char> data;
	// The mode LoadBitmapFile (0062f400) opens pictures with.
	if (path.empty() || !ReadGameFile(path.c_str(), 0x41, data)) return false;
	return DecodeBattleBmp(reinterpret_cast<const unsigned char *>(data.data()), data.size(), out);
}

HelpGameData HelpCollectGameData(const HelpDoc &doc)
{
	HelpGameData out;
	const std::vector<char *> classes = GameClasses();
	if (classes.empty()) {
		LogW("ui", "help: the game has no classes loaded; the unit pages go without their numbers");
		return out;
	}
	std::map<std::string, const char *> byId;
	std::map<int, std::vector<const char *>> byTopic;
	std::map<const char *, int> topicOf;
	for (const char *cls : classes) {
		const std::string id = GameStringAt(cls + kClassId);
		if (id.empty() || GameStringAt(cls + kClassEntity).empty()) continue;
		byId[Lower(id)] = cls;
		const std::string help = GameStringAt(cls + kClassHelp);
		const char *parent = *reinterpret_cast<const char *const *>(cls + kClassParent);
		// A class names its page itself; the ones that inherit it would all claim their parent's.
		if (help.empty() || help == "**Invalid**" || (parent && GameStringAt(parent + kClassHelp) == help)) continue;
		const int topic = HelpResolve(doc, -1, help);
		if (topic < 0) continue;
		byTopic[topic].push_back(cls);
		topicOf[cls] = topic;
		out.classTopic[Lower(id)] = topic;
	}
	// The classes named after a topic, as the help's units are (link="RHastatus" is the class RHastatus).
	for (size_t t = 0; t < doc.topics.size(); t++) {
		const auto it = byId.find(Lower(doc.topics[t].id));
		if (it == byId.end()) continue;
		std::vector<const char *> &list = byTopic[static_cast<int>(t)];
		if (std::find(list.begin(), list.end(), it->second) == list.end()) list.insert(list.begin(), it->second);
		if (!topicOf.count(it->second)) {
			topicOf[it->second] = static_cast<int>(t);
			out.classTopic[it->first] = static_cast<int>(t);
		}
	}
	const std::map<std::string, const char *> train = TrainCommands();
	int units = 0;
	for (const auto &kv : byTopic) {
		const int topic = kv.first;
		const char *cls = nullptr;
		for (const char *c : kv.second) {
			if (Lower(GameStringAt(c + kClassId)) == Lower(doc.topics[topic].id)) cls = c;
		}
		// Several classes on one page (the heroes of each level, the peasants of each civilization) have no one set of numbers.
		if (!cls && kv.second.size() == 1) cls = kv.second[0];
		if (!cls) continue;
		HelpUnitStats s;
		s.classId = GameStringAt(cls + kClassId);
		s.health = Field(cls, kClassHealth);
		s.speed = Field(cls, kClassSpeed);
		if (s.health <= 0 || s.speed <= 0) continue;
		s.stamina = Field(cls, kClassStamina);
		s.damage = Field(cls, kClassDamage);
		s.damageType = Field(cls, kClassDamageType);
		s.armorSlash = Field(cls, kClassArmorSlash);
		s.armorPierce = Field(cls, kClassArmorPierce);
		s.sight = Field(cls, kClassSight);
		s.range = Field(cls, kClassRange);
		const auto cmd = train.find(Lower(s.classId));
		if (cmd != train.end()) {
			const char *desc = cmd->second;
			s.costGold = *reinterpret_cast<const int *>(desc + kDescGold);
			s.costFood = *reinterpret_cast<const int *>(desc + kDescFood);
			s.costPop = *reinterpret_cast<const int *>(desc + kDescPop);
			s.trainMs = *reinterpret_cast<const int *>(desc + kDescDelay);
			const int index = *reinterpret_cast<const int *>(desc + kDescIndex);
			for (const auto &b : topicOf) {
				if (b.second == topic || !Offers(b.first, index)) continue;
				if (std::find(s.recruitedAt.begin(), s.recruitedAt.end(), b.second) == s.recruitedAt.end()) s.recruitedAt.push_back(b.second);
			}
		}
		out.stats[topic] = s;
		units++;
	}
	LogI("ui", "help: %d unit pages with the numbers of their class, from %d classes", units, static_cast<int>(classes.size()));
	return out;
}
