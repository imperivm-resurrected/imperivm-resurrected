// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"
#include "mods/civindices.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

void Assignment()
{
	const std::vector<CivIndex> input = {{"zeta"}, {"fixed", 90}, {"alpha"}, {"first", 10}, {"last", 127}, {"beta"}};
	const std::map<std::string, int> expected = {{"alpha", 11}, {"beta", 12}, {"first", 10}, {"fixed", 90}, {"last", 127}, {"zeta", 13}};
	for (size_t shift = 0; shift < input.size(); shift++) {
		std::vector<CivIndex> civs = input;
		std::rotate(civs.begin(), civs.begin() + shift, civs.end());
		if (shift % 2) std::reverse(civs.begin(), civs.end());
		const std::vector<CivIndex> before = civs;
		CHECK(CivIndicesAssign(civs).empty());
		for (size_t i = 0; i < civs.size(); i++) {
			CHECK_EQ(civs[i].id, before[i].id);
			CHECK_EQ(civs[i].index, expected.at(civs[i].id));
		}
	}

	std::vector<CivIndex> civs;
	for (int i = 0; i < 120; i++) civs.push_back({"c" + std::to_string(1000 + i)});
	std::reverse(civs.begin(), civs.end());
	CHECK_EQ(CivIndicesAssign(civs), (std::vector<std::string>{"c1118", "c1119"}));
	for (int i = 0; i < 120; i++) {
		CHECK_EQ(civs[i].id, "c" + std::to_string(1119 - i));
		CHECK_EQ(civs[i].index, i < 2 ? 0 : 129 - i);
	}
	CHECK_EQ(civs[29].index, 100);
	CHECK_EQ(civs[2].index, kCivIndexLast);
	CHECK_EQ(civs.back().index, kCivIndexFirst);

	civs = {{"fixed", 90}};
	for (int i = 0; i < 118; i++) civs.push_back({"c" + std::to_string(1000 + i)});
	CHECK_EQ(CivIndicesAssign(civs), (std::vector<std::string>{"c1117"}));
	CHECK_EQ(civs[0].index, 90);
	CHECK_EQ(civs[81].index, 91);
	CHECK_EQ(civs[117].index, 127);
	CHECK_EQ(civs[118].index, 0);
	civs.clear();
	CHECK(CivIndicesAssign(civs).empty());
}

void SavedMaps()
{
	CivIndicesSet({}, false);
	CHECK(!CivIndicesAutomatic());
	CHECK(CivIndicesCheck(nullptr) == CivIndicesStatus::Compatible);
	Json empty = CivIndicesToJson();
	CHECK(empty.type == Json::Object && empty.obj.empty());
	CHECK(CivIndicesCheck(&empty) == CivIndicesStatus::Compatible);

	std::vector<CivIndex> civs = {{"zeta", 90}, {"alpha", 10}, {"beta", 127}};
	CivIndicesSet(civs, false);
	CHECK(!CivIndicesAutomatic());
	CHECK(CivIndicesCheck(nullptr) == CivIndicesStatus::Compatible);
	Json saved = CivIndicesToJson();
	CHECK_EQ(saved.obj.size(), static_cast<size_t>(3));
	CHECK_EQ(saved.obj[0].first, std::string("alpha"));
	CHECK_EQ(saved.obj[1].first, std::string("beta"));
	CHECK_EQ(saved.obj[2].first, std::string("zeta"));
	CHECK(saved.obj[0].second.type == Json::Number && saved.obj[0].second.num == 10);
	CHECK(saved.obj[1].second.num == 127 && saved.obj[2].second.num == 90);
	CHECK(CivIndicesCheck(&saved) == CivIndicesStatus::Compatible);
	std::reverse(saved.obj.begin(), saved.obj.end());
	CHECK(CivIndicesCheck(&saved) == CivIndicesStatus::Compatible);
	civs[0].index = 91;
	CHECK(CivIndicesCheck(&saved) == CivIndicesStatus::Compatible); // Set copied the caller's table.
	CivIndicesSet(civs, true);
	CHECK(CivIndicesAutomatic());
	CHECK(CivIndicesCheck(nullptr) == CivIndicesStatus::Missing);
	CHECK(CivIndicesCheck(&saved) == CivIndicesStatus::Mismatch);
	CHECK(saved.obj[0].second.num == 90); // Checking never remaps the saved table.

	saved = CivIndicesToJson();
	CHECK(CivIndicesCheck(&saved) == CivIndicesStatus::Compatible);
	Json changed = saved;
	changed.obj.pop_back();
	CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Mismatch);
	CHECK(CivIndicesCheck(&empty) == CivIndicesStatus::Mismatch);
	changed = saved;
	changed.obj[0].first = "other";
	CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Mismatch);
	changed = saved;
	changed.obj.emplace_back("extra", Json::MakeNumber(11));
	CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Mismatch);
	changed = saved;
	std::swap(changed.obj[0].second, changed.obj[1].second);
	CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Mismatch);

	changed = saved;
	changed.obj.emplace_back("alpha", Json::MakeNumber(11));
	CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Invalid);
	changed = saved;
	changed.obj.emplace_back("extra", Json::MakeNumber(10));
	CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Invalid);
	for (const std::string &id : {std::string(), std::string("Alpha"), std::string("1alpha"), std::string("a-b"),
		std::string("a b"), std::string("a/b"), std::string("é"), std::string(25, 'a'), std::string("a\0b", 3)}) {
		changed = saved;
		changed.obj[0].first = id;
		CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Invalid);
	}
	for (const Json &value : {Json(), Json::MakeArray(), Json::MakeObject(), Json::MakeBool(true), Json::MakeBool(false),
		Json::MakeString("10"), Json::MakeNumber(10.5), Json::MakeNumber(0), Json::MakeNumber(9), Json::MakeNumber(128),
		Json::MakeNumber(-10), Json::MakeNumber(std::numeric_limits<double>::infinity()),
		Json::MakeNumber(std::numeric_limits<double>::quiet_NaN())}) {
		changed = saved;
		changed.obj[0].second = value;
		CHECK(CivIndicesCheck(&changed) == CivIndicesStatus::Invalid);
	}
	for (const Json &value : {Json(), Json::MakeArray(), Json::MakeString(""), Json::MakeBool(true), Json::MakeNumber(10)}) {
		CHECK(CivIndicesCheck(&value) == CivIndicesStatus::Invalid);
	}
	CivIndicesSet({{"a_1", 10}, {std::string(24, 'z'), 127}}, false);
	Json validIds = CivIndicesToJson();
	CHECK(CivIndicesCheck(&validIds) == CivIndicesStatus::Compatible);
	CHECK(!CivIndicesAutomatic());
	CHECK(CivIndicesCheck(nullptr) == CivIndicesStatus::Compatible);
	CHECK(CivIndicesCheck(&saved) == CivIndicesStatus::Mismatch);
}

void SavedFileBinding()
{
	CivIndicesSet({{"alpha", 10}}, true);
	const Json saved = CivIndicesToJson();
	const std::string original(64, 'a'), overwritten(64, 'b');
	CHECK(CivIndicesCheckSaved(&saved, original, original) == CivIndicesStatus::Compatible);
	// A failed sidecar overwrite must not authorize a new binary with an old, compatible table.
	CHECK(CivIndicesCheckSaved(&saved, original, overwritten) == CivIndicesStatus::DifferentFile);
	CHECK(CivIndicesCheckSaved(&saved, original, "") == CivIndicesStatus::DifferentFile);
	CHECK(CivIndicesCheckSaved(&saved, "", original) == CivIndicesStatus::Invalid);
	CHECK(CivIndicesCheckSaved(&saved, "short", original) == CivIndicesStatus::Invalid);
	CHECK(CivIndicesCheckSaved(nullptr, "", original) == CivIndicesStatus::Missing);
	CivIndicesSet({}, false);
	CHECK(CivIndicesCheckSaved(nullptr, "", original) == CivIndicesStatus::Compatible);
}

void ConcurrentSnapshots()
{
	CivIndicesSet({{"alpha", 10}, {"beta", 11}}, true);
	std::thread writer([] {
		for (int i = 0; i < 1000; i++) CivIndicesSet({{"alpha", 10 + i % 2}, {"beta", 11 - i % 2}}, i % 2 != 0);
	});
	for (int i = 0; i < 1000; i++) {
		const Json snapshot = CivIndicesToJson();
		CHECK(snapshot.obj.size() == 2 && snapshot.obj[0].first == "alpha" && snapshot.obj[1].first == "beta" &&
			((snapshot.obj[0].second.num == 10 && snapshot.obj[1].second.num == 11) ||
				(snapshot.obj[0].second.num == 11 && snapshot.obj[1].second.num == 10)));
		CHECK(CivIndicesCheck(&snapshot) != CivIndicesStatus::Invalid);
		(void)CivIndicesAutomatic();
	}
	writer.join();
}

}

int main()
{
	Assignment();
	SavedMaps();
	SavedFileBinding();
	ConcurrentSnapshots();
	return test::Summary("civindices");
}
