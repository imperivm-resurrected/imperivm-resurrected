// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "check.h"

#include <cstdio>
#include <string>

#include "core/json.h"
#include "i18n/tr.h"

static const std::string &TestLanguage()
{
	static const std::string english = "english";
	return english;
}

int main()
{
	TrSetLanguageSource(&TestLanguage);
	Json old;
	std::string err;
	Check(Json::Parse("{\"id\":\"x\",\"nombre\":{\"spanish\":\"Hola\"},\"autores\":[\"a\"],\"tipo\":\"reglas\",\"huellaJugabilidad\":\"abc\"}", old, &err), "parse");
	Check(old.Get("name") != nullptr, "name via nombre");
	Check(old.Get("authors") && old.Get("authors")->arr.size() == 1, "authors via autores");
	Check(old.GetString("kind") == "reglas", "kind via tipo");
	Check(old.GetString("gameplayFingerprint") == "abc", "fingerprint alias");
	old.Set("name", Json::MakeString("New"));
	Check(old.Get("nombre") == nullptr || old.GetString("name") == "New", "set replaces legacy");
	int names = 0;
	for (const auto &kv : old.obj) names += (kv.first == "name" || kv.first == "nombre") ? 1 : 0;
	Check(names == 1, "only one name key after Set");
	Json fresh;
	Check(Json::Parse("{\"name\":\"N\",\"nombre\":\"old\"}", fresh, &err), "parse 2");
	Check(fresh.GetString("name") == "N", "new key wins");
	Json bad;
	Check(!Json::Parse("{\"a\": }", bad, &err) && err.find("position") != std::string::npos, "error has position: " + err);
	Json n;
	Check(Json::Parse("{\"cut\":7.9,\"neg\":-3.5,\"at\":1794662871362,\"text\":\"76561198000000999\",\"id\":12,\"yes\":true,\"no\":false,\"one\":\"1\"}", n, &err), "parse numbers");
	Check(n.GetInt("cut") == 7 && n.GetInt("neg") == -3, "GetInt cuts towards zero");
	Check(n.GetInt("missing", 5) == 5 && n.GetInt("one", 4) == 4, "GetInt default for missing and text");
	Check(n.GetInt64("at") == 1794662871362LL && n.GetInt64("missing", -1) == -1, "GetInt64");
	Check(n.GetIdText("text") == "76561198000000999" && n.GetIdText("id") == "12", "GetIdText of text and number");
	Check(n.GetIdText("yes").empty() && n.GetIdText("missing").empty(), "GetIdText of anything else");
	Check(n.GetFlag("yes") == 1 && n.GetFlag("no") == 0, "GetFlag of a yes or no");
	Check(n.GetFlag("one") == -1 && n.GetFlag("missing") == -1, "GetFlag of anything else");
	return test::Summary("json");
}
