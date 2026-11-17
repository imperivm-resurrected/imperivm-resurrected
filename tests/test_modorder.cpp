// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <algorithm>
#include <string>
#include <vector>

#include "mods/modorder.h"

namespace {

const int kCosmetic = 0, kContent = 1, kRules = 2;

OrderItem Item(const std::string &id, int kind, std::vector<std::string> above = {}, std::vector<std::string> below = {})
{
	OrderItem m;
	m.folder = id + "-folder";
	m.id = id;
	m.kind = kind;
	m.above = above;
	m.below = below;
	return m;
}

std::vector<std::string> Folders(const std::vector<std::string> &ids)
{
	std::vector<std::string> out;
	for (const std::string &id : ids) out.push_back(id + "-folder");
	return out;
}

void ByKind()
{
	const std::vector<OrderItem> items = {Item("rules", kRules), Item("map", kContent), Item("skin", kCosmetic)};
	CHECK_EQ(CanonicalOrder(items), Folders({"skin", "map", "rules"}));
}

void SameResultWhateverTheInput()
{
	std::vector<OrderItem> items = {Item("b-rules", kRules), Item("a-rules", kRules), Item("voices", kCosmetic), Item("c-map", kContent),
		Item("a-map", kContent)};
	const std::vector<std::string> expected = Folders({"voices", "a-map", "c-map", "a-rules", "b-rules"});
	CHECK_EQ(CanonicalOrder(items), expected);
	std::reverse(items.begin(), items.end());
	CHECK_EQ(CanonicalOrder(items), expected);
	std::rotate(items.begin(), items.begin() + 2, items.end());
	CHECK_EQ(CanonicalOrder(items), expected);
}

void IdsIgnoreCase()
{
	const std::vector<OrderItem> items = {Item("Beta", kRules), Item("alpha", kRules)};
	CHECK_EQ(CanonicalOrder(items), Folders({"alpha", "Beta"}));
}

void Dependencies()
{
	CHECK_EQ(CanonicalOrder({Item("addon", kRules, {"base"}), Item("base", kRules)}), Folders({"addon", "base"}));
	CHECK_EQ(CanonicalOrder({Item("a", kRules, {"z"}), Item("z", kRules)}), Folders({"a", "z"}));
	CHECK_EQ(CanonicalOrder({Item("a", kRules), Item("z", kRules, {"A"})}), Folders({"z", "a"}));
	CHECK_EQ(CanonicalOrder({Item("a", kRules, {}, {"z"}), Item("z", kRules)}), Folders({"z", "a"}));
	CHECK_EQ(CanonicalOrder({Item("map", kContent, {}, {"rules"}), Item("rules", kRules)}), Folders({"rules", "map"}));
	CHECK_EQ(CanonicalOrder({Item("skin", kCosmetic), Item("base", kRules, {"skin"})}), Folders({"base", "skin"}));
}

void Chains()
{
	const std::vector<OrderItem> items = {Item("c", kRules, {"b"}), Item("a", kRules), Item("b", kRules, {"a"})};
	CHECK_EQ(CanonicalOrder(items), Folders({"c", "b", "a"}));
}

void Cycles()
{
	const std::vector<OrderItem> items = {Item("x", kRules, {"y"}), Item("y", kRules, {"x"}), Item("skin", kCosmetic)};
	const std::vector<std::string> out = CanonicalOrder(items);
	CHECK_EQ(out.size(), static_cast<size_t>(3));
	CHECK_EQ(out, Folders({"skin", "x", "y"}));
}

void Missing()
{
	OrderItem gone;
	gone.folder = "Old Mod";
	const std::vector<OrderItem> items = {gone, Item("rules", kRules), Item("skin", kCosmetic)};
	CHECK_EQ(CanonicalOrder(items), (std::vector<std::string>{"skin-folder", "rules-folder", "Old Mod"}));
	CHECK(CanonicalOrder({}).empty());
}

void TwoCopiesOfOneMod()
{
	OrderItem a = Item("mod", kRules), b = Item("mod", kRules);
	a.folder = "Mod 1.1";
	b.folder = "Mod 1.0";
	CHECK_EQ(CanonicalOrder({a, b}), (std::vector<std::string>{"Mod 1.0", "Mod 1.1"}));
}

OrderItem Needing(const std::string &id, int kind, std::vector<std::string> needs)
{
	OrderItem m = Item(id, kind, needs);
	m.needs = needs;
	return m;
}

OrderItem ByAuthor(const std::string &id, int kind, std::vector<std::string> after, std::vector<std::string> before = {})
{
	OrderItem m = Item(id, kind, after, before);
	m.author = after;
	m.author.insert(m.author.end(), before.begin(), before.end());
	return m;
}

std::vector<std::string> FoldersOf(const std::vector<OrderReason> &reasons)
{
	std::vector<std::string> out;
	for (const OrderReason &r : reasons) out.push_back(r.folder);
	return out;
}

const OrderReason *Reason(const std::vector<OrderReason> &reasons, const std::string &id)
{
	for (const OrderReason &r : reasons) {
		if (r.folder == id + "-folder") return &r;
	}
	return nullptr;
}

int ReasonOf(const std::vector<OrderReason> &reasons, const std::string &id)
{
	const OrderReason *r = Reason(reasons, id);
	return r ? r->reason : -1;
}

std::vector<std::string> RelatedOf(const std::vector<OrderReason> &reasons, const std::string &id)
{
	const OrderReason *r = Reason(reasons, id);
	return r ? r->related : std::vector<std::string>{"(none)"};
}

void ExplainKindAndName()
{
	const std::vector<OrderItem> items = {Item("b-rules", kRules), Item("a-rules", kRules), Item("voices", kCosmetic), Item("c-map", kContent),
		Item("a-map", kContent)};
	const std::vector<OrderReason> r = ExplainOrder(items);
	CHECK_EQ(FoldersOf(r), CanonicalOrder(items));
	CHECK_EQ(ReasonOf(r, "voices"), static_cast<int>(kOrderByKind));
	CHECK_EQ(ReasonOf(r, "a-map"), static_cast<int>(kOrderByKind));
	CHECK_EQ(ReasonOf(r, "c-map"), static_cast<int>(kOrderByName));
	CHECK_EQ(ReasonOf(r, "a-rules"), static_cast<int>(kOrderByKind));
	CHECK_EQ(ReasonOf(r, "b-rules"), static_cast<int>(kOrderByName));
	for (const OrderReason &x : r) CHECK(x.related.empty());
	CHECK(ExplainOrder({}).empty());
}

void ExplainNeeds()
{
	const std::vector<OrderItem> items = {Item("base", kRules), Needing("addon", kRules, {"BASE"}), Item("skin", kCosmetic)};
	const std::vector<OrderReason> r = ExplainOrder(items);
	CHECK_EQ(FoldersOf(r), Folders({"skin", "addon", "base"}));
	CHECK_EQ(ReasonOf(r, "addon"), static_cast<int>(kOrderNeeds));
	CHECK_EQ(RelatedOf(r, "addon"), Folders({"base"}));
	CHECK_EQ(ReasonOf(r, "base"), static_cast<int>(kOrderNeeded));
	CHECK_EQ(RelatedOf(r, "base"), Folders({"addon"}));
	CHECK_EQ(ReasonOf(r, "skin"), static_cast<int>(kOrderByKind));
}

void ExplainNeedsSeveral()
{
	const std::vector<OrderItem> items = {Item("b", kRules), Needing("patch", kRules, {"b", "a"}), Item("a", kRules), Needing("extra", kRules, {"a"})};
	const std::vector<OrderReason> r = ExplainOrder(items);
	CHECK_EQ(ReasonOf(r, "patch"), static_cast<int>(kOrderNeeds));
	CHECK_EQ(RelatedOf(r, "patch"), Folders({"a", "b"}));
	CHECK_EQ(ReasonOf(r, "a"), static_cast<int>(kOrderNeeded));
	CHECK_EQ(RelatedOf(r, "a"), Folders({"extra", "patch"}));
	CHECK_EQ(RelatedOf(r, "b"), Folders({"patch"}));
}

void ExplainNeedsBeatsTheRest()
{
	OrderItem addon = Needing("addon", kRules, {"base"});
	addon.author = {"other"};
	OrderItem base = Needing("base", kRules, {"core"});
	const std::vector<OrderItem> items = {addon, base, Item("core", kRules), Item("other", kRules)};
	const std::vector<OrderReason> r = ExplainOrder(items);
	CHECK_EQ(ReasonOf(r, "addon"), static_cast<int>(kOrderNeeds));
	CHECK_EQ(RelatedOf(r, "addon"), Folders({"base"}));
	CHECK_EQ(ReasonOf(r, "base"), static_cast<int>(kOrderNeeds));
	CHECK_EQ(RelatedOf(r, "base"), Folders({"core"}));
	CHECK_EQ(ReasonOf(r, "core"), static_cast<int>(kOrderNeeded));
	CHECK_EQ(ReasonOf(r, "other"), static_cast<int>(kOrderAuthor));
	CHECK_EQ(RelatedOf(r, "other"), Folders({"addon"}));
}

void ExplainAuthor()
{
	const std::vector<OrderReason> after = ExplainOrder({ByAuthor("a", kRules, {"z"}), Item("z", kRules), Item("m", kRules)});
	CHECK_EQ(FoldersOf(after), Folders({"a", "m", "z"}));
	CHECK_EQ(ReasonOf(after, "a"), static_cast<int>(kOrderAuthor));
	CHECK_EQ(RelatedOf(after, "a"), Folders({"z"}));
	CHECK_EQ(ReasonOf(after, "z"), static_cast<int>(kOrderAuthor));
	CHECK_EQ(RelatedOf(after, "z"), Folders({"a"}));
	CHECK_EQ(ReasonOf(after, "m"), static_cast<int>(kOrderByName));
	const std::vector<OrderReason> before = ExplainOrder({ByAuthor("a", kRules, {}, {"z"}), Item("z", kRules)});
	CHECK_EQ(FoldersOf(before), Folders({"z", "a"}));
	CHECK_EQ(ReasonOf(before, "a"), static_cast<int>(kOrderAuthor));
	CHECK_EQ(ReasonOf(before, "z"), static_cast<int>(kOrderAuthor));
	const std::vector<OrderReason> both = ExplainOrder({ByAuthor("a", kRules, {"b"}), ByAuthor("b", kRules, {}, {"a"}), ByAuthor("c", kRules, {}, {"a"})});
	CHECK_EQ(RelatedOf(both, "a"), Folders({"b", "c"}));
}

void ExplainIgnoresAbsentMods()
{
	OrderItem addon = Needing("addon", kRules, {"gone"});
	addon.author = {"also-gone"};
	const std::vector<OrderReason> r = ExplainOrder({addon, Item("zz", kRules)});
	CHECK_EQ(ReasonOf(r, "addon"), static_cast<int>(kOrderByKind));
	CHECK_EQ(ReasonOf(r, "zz"), static_cast<int>(kOrderByName));
	CHECK(RelatedOf(r, "addon").empty());
}

void ExplainMissingFolders()
{
	OrderItem gone, other;
	gone.folder = "Old Mod";
	other.folder = "Older Mod";
	const std::vector<OrderReason> r = ExplainOrder({gone, Item("skin", kCosmetic), other});
	CHECK_EQ(FoldersOf(r), (std::vector<std::string>{"skin-folder", "Old Mod", "Older Mod"}));
	CHECK_EQ(r[0].reason, static_cast<int>(kOrderByKind));
	CHECK_EQ(r[1].reason, static_cast<int>(kOrderByKind));
	CHECK_EQ(r[2].reason, static_cast<int>(kOrderByName));
}

void ExplainKindAfterAMovedMod()
{
	const std::vector<OrderItem> items = {ByAuthor("skin", kCosmetic, {}, {"a-rules"}), Item("b-rules", kRules), Item("a-rules", kRules)};
	const std::vector<OrderReason> r = ExplainOrder(items);
	CHECK_EQ(FoldersOf(r), Folders({"a-rules", "skin", "b-rules"}));
	CHECK_EQ(ReasonOf(r, "a-rules"), static_cast<int>(kOrderAuthor));
	CHECK_EQ(ReasonOf(r, "skin"), static_cast<int>(kOrderAuthor));
	CHECK_EQ(ReasonOf(r, "b-rules"), static_cast<int>(kOrderByKind));
}

void ExplainTwoCopies()
{
	OrderItem a = Item("base", kRules), b = Item("base", kRules);
	a.folder = "Base 1.1";
	b.folder = "Base 1.0";
	const std::vector<OrderReason> r = ExplainOrder({a, b, Needing("addon", kRules, {"base"})});
	CHECK_EQ(r.size(), static_cast<size_t>(3));
	if (r.size() != 3) return;
	CHECK_EQ(r[0].folder, std::string("addon-folder"));
	CHECK_EQ(r[0].related, (std::vector<std::string>{"Base 1.0", "Base 1.1"}));
	CHECK_EQ(r[1].reason, static_cast<int>(kOrderNeeded));
	CHECK_EQ(r[2].related, (std::vector<std::string>{"addon-folder"}));
}

}

int main()
{
	ByKind();
	SameResultWhateverTheInput();
	IdsIgnoreCase();
	Dependencies();
	Chains();
	Cycles();
	Missing();
	TwoCopiesOfOneMod();
	ExplainKindAndName();
	ExplainNeeds();
	ExplainNeedsSeveral();
	ExplainNeedsBeatsTheRest();
	ExplainAuthor();
	ExplainIgnoresAbsentMods();
	ExplainMissingFolders();
	ExplainKindAfterAMovedMod();
	ExplainTwoCopies();
	return test::Summary("modorder");
}
