// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool b = false;
	double num = 0;
	std::string str;
	std::vector<Json> arr;
	std::vector<std::pair<std::string, Json>> obj;

	static Json MakeString(const std::string &s) { Json j; j.type = String; j.str = s; return j; }
	static Json MakeNumber(double n) { Json j; j.type = Number; j.num = n; return j; }
	static Json MakeBool(bool v) { Json j; j.type = Bool; j.b = v; return j; }
	static Json MakeArray() { Json j; j.type = Array; return j; }
	static Json MakeObject() { Json j; j.type = Object; return j; }

	const Json *Get(const std::string &key) const;
	std::string GetString(const std::string &key, const std::string &def = "") const;
	double GetNumber(const std::string &key, double def = 0) const;
	bool GetBool(const std::string &key, bool def = false) const;
	// A number cut to a whole one; `def` when the key is missing or not a number.
	int GetInt(const std::string &key, int def = 0) const;
	int64_t GetInt64(const std::string &key, int64_t def = 0) const;
	// An id that may come as text or as a number, as text; empty for anything else.
	std::string GetIdText(const std::string &key) const;
	// 1 or 0 for a yes or no; -1 when the key is missing or is not one.
	int GetFlag(const std::string &key) const;
	Json &Set(const std::string &key, const Json &value);

	static bool Parse(const std::string &text, Json &out, std::string *error = nullptr);
	std::string Dump(int indent = 0) const;
};

bool ReadJsonFile(const std::string &path, Json &out, std::string *error = nullptr);
bool WriteJsonFile(const std::string &path, const Json &value);
