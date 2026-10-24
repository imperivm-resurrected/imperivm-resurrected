// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/civindices.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_mutex;
std::map<std::string, int> g_civs;
bool g_automatic = false;

bool ValidId(const std::string &id)
{
	if (id.empty() || id.size() > 24 || id[0] < 'a' || id[0] > 'z') return false;
	for (char c : id) {
		if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
	}
	return true;
}

}

std::vector<std::string> CivIndicesAssign(std::vector<CivIndex> &civs)
{
	bool used[kCivIndexLast + 1] = {};
	std::vector<CivIndex *> automatic;
	for (CivIndex &c : civs) {
		if (c.index == 0) automatic.push_back(&c);
		else used[c.index] = true;
	}
	std::sort(automatic.begin(), automatic.end(), [](const CivIndex *a, const CivIndex *b) { return a->id < b->id; });
	std::vector<std::string> overflow;
	int next = kCivIndexFirst;
	for (CivIndex *c : automatic) {
		while (next <= kCivIndexLast && used[next]) next++;
		if (next > kCivIndexLast) overflow.push_back(c->id);
		else c->index = next++;
	}
	return overflow;
}

void CivIndicesSet(const std::vector<CivIndex> &civs, bool automatic)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_civs.clear();
	for (const CivIndex &c : civs) g_civs.emplace(c.id, c.index);
	g_automatic = automatic;
}

bool CivIndicesAutomatic()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_automatic;
}

Json CivIndicesToJson()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Json out = Json::MakeObject();
	for (const auto &c : g_civs) out.obj.emplace_back(c.first, Json::MakeNumber(c.second));
	return out;
}

CivIndicesStatus CivIndicesCheck(const Json *saved)
{
	if (!saved) {
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_automatic ? CivIndicesStatus::Missing : CivIndicesStatus::Compatible;
	}
	if (saved->type != Json::Object) return CivIndicesStatus::Invalid;
	std::map<std::string, int> civs;
	bool used[kCivIndexLast + 1] = {};
	for (const auto &c : saved->obj) {
		const Json &value = c.second;
		if (!ValidId(c.first) || value.type != Json::Number ||
			!(value.num >= kCivIndexFirst && value.num <= kCivIndexLast) || std::floor(value.num) != value.num) {
			return CivIndicesStatus::Invalid;
		}
		const int index = static_cast<int>(value.num);
		if (used[index] || !civs.emplace(c.first, index).second) return CivIndicesStatus::Invalid;
		used[index] = true;
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	return civs == g_civs ? CivIndicesStatus::Compatible : CivIndicesStatus::Mismatch;
}

CivIndicesStatus CivIndicesCheckSaved(const Json *saved, const std::string &savedHash, const std::string &fileHash)
{
	const CivIndicesStatus status = CivIndicesCheck(saved);
	if (status != CivIndicesStatus::Compatible || !saved) return status;
	if (savedHash.size() != 64) return CivIndicesStatus::Invalid;
	return savedHash == fileHash ? CivIndicesStatus::Compatible : CivIndicesStatus::DifferentFile;
}
