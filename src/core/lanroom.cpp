// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/lanroom.h"

#include <algorithm>
#include <cstring>

#include "core/json.h"

namespace {
const char kMagic[] = "IRLAN1\n";
const size_t kMagicLen = sizeof(kMagic) - 1;
// Under a typical network's packet, whatever the names.
const size_t kMaxText = 200;

std::string Cut(const std::string &s, size_t most)
{
	if (s.size() <= most) return s;
	size_t n = most;
	// Not in the middle of a UTF-8 character.
	while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
	return s.substr(0, n);
}
}

std::string LanRoomMapLines(const std::string &options)
{
	std::string out;
	for (size_t a = 0; a < options.size();) {
		size_t b = options.find('\n', a);
		if (b == std::string::npos) b = options.size();
		const std::string line = options.substr(a, b - a);
		if (line.compare(0, 10, "Scenario: ") == 0 || line.compare(0, 10, "Map type: ") == 0) out += (out.empty() ? "" : "\n") + line;
		a = b + 1;
	}
	return Cut(out, kMaxText);
}

std::string LanRoomPacket(const LanRoom &r)
{
	Json j = Json::MakeObject();
	j.Set("name", Json::MakeString(Cut(r.name, kMaxText)));
	j.Set("host", Json::MakeString(Cut(r.host, kMaxText)));
	j.Set("map", Json::MakeString(LanRoomMapLines(r.options)));
	j.Set("ver", Json::MakeString(Cut(r.version, 40)));
	j.Set("lvl", Json::MakeString(Cut(r.rulesLevel, 120)));
	j.Set("build", Json::MakeString(Cut(r.build, 80)));
	j.Set("key", Json::MakeString(Cut(r.gameplayKey, 80)));
	j.Set("players", Json::MakeNumber(r.players));
	j.Set("max", Json::MakeNumber(r.maxPlayers));
	return std::string(kMagic) + j.Dump();
}

bool LanRoomParse(const char *data, int len, LanRoom &out)
{
	if (!data || len <= static_cast<int>(kMagicLen) || memcmp(data, kMagic, kMagicLen) != 0) return false;
	Json j;
	if (!Json::Parse(std::string(data + kMagicLen, static_cast<size_t>(len) - kMagicLen), j) || j.type != Json::Object) return false;
	LanRoom r;
	r.name = Cut(j.GetString("name"), kMaxText);
	r.host = Cut(j.GetString("host"), kMaxText);
	r.options = LanRoomMapLines(j.GetString("map"));
	r.version = j.GetString("ver");
	r.rulesLevel = j.GetString("lvl");
	r.build = j.GetString("build");
	r.gameplayKey = j.GetString("key");
	r.players = std::max(0, std::min(8, j.GetInt("players")));
	r.maxPlayers = std::max(0, std::min(8, j.GetInt("max")));
	if (r.name.empty() || r.maxPlayers < 2) return false;
	out = r;
	return true;
}
