// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

struct OrderItem {
	std::string folder;
	std::string id;
	int kind = -1;
	std::vector<std::string> above;
	std::vector<std::string> below;
	std::vector<std::string> needs;
	std::vector<std::string> author;
};

std::vector<std::string> CanonicalOrder(const std::vector<OrderItem> &items);

enum OrderReasonKind { kOrderByKind = 0, kOrderByName = 1, kOrderNeeds = 2, kOrderNeeded = 3, kOrderAuthor = 4 };

struct OrderReason {
	std::string folder;
	int reason = kOrderByName;
	std::vector<std::string> related;
};

std::vector<OrderReason> ExplainOrder(const std::vector<OrderItem> &items);
