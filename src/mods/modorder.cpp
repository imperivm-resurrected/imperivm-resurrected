// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/modorder.h"

#include <algorithm>

namespace {

const int kUnknownRank = 3;

std::string Folded(const std::string &s)
{
	std::string out = s;
	for (char &c : out) {
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
	}
	return out;
}

int Rank(const OrderItem &m)
{
	return m.kind >= 0 && m.kind < kUnknownRank ? m.kind : kUnknownRank;
}

bool Names(const std::vector<std::string> &list, const std::string &id)
{
	if (id.empty()) return false;
	const std::string want = Folded(id);
	for (const std::string &x : list) {
		if (Folded(x) == want) return true;
	}
	return false;
}

bool MustBeAbove(const OrderItem &a, const OrderItem &b)
{
	return Names(a.above, b.id) || Names(b.below, a.id);
}

std::vector<size_t> OrderIndices(const std::vector<OrderItem> &items)
{
	const size_t n = items.size();
	std::vector<size_t> base(n);
	for (size_t i = 0; i < n; i++) base[i] = i;
	std::sort(base.begin(), base.end(), [&](size_t a, size_t b) {
		const int ra = Rank(items[a]), rb = Rank(items[b]);
		if (ra != rb) return ra < rb;
		const std::string ia = Folded(items[a].id), ib = Folded(items[b].id);
		if (ia != ib) return ia < ib;
		return Folded(items[a].folder) < Folded(items[b].folder);
	});
	std::vector<size_t> out;
	std::vector<bool> placed(n, false);
	for (size_t step = 0; step < n; step++) {
		size_t best = n;
		for (size_t k = 0; k < n && best == n; k++) {
			const size_t i = base[k];
			if (placed[i]) continue;
			bool blocked = false;
			for (size_t j = 0; j < n && !blocked; j++) {
				if (!placed[j] && j != i && MustBeAbove(items[j], items[i])) blocked = true;
			}
			if (!blocked) best = i;
		}
		if (best == n) {
			for (size_t k = 0; k < n; k++) {
				if (!placed[base[k]]) {
					placed[base[k]] = true;
					out.push_back(base[k]);
				}
			}
			break;
		}
		placed[best] = true;
		out.push_back(best);
	}
	return out;
}

}

std::vector<std::string> CanonicalOrder(const std::vector<OrderItem> &items)
{
	std::vector<std::string> out;
	for (size_t i : OrderIndices(items)) out.push_back(items[i].folder);
	return out;
}
