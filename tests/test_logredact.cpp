// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "core/logredact.h"

namespace {

void SteamIds()
{
	LogRedactor r;
	CHECK_EQ(r.Apply("room of 76561198012345678 and 76561198000000001, again 76561198012345678"),
		std::string("room of <steamid1> and <steamid2>, again <steamid1>"));
	CHECK_EQ(r.Apply("id=76561198012345678."), std::string("id=<steamid1>."));
	CHECK_EQ(r.Apply("a 765611980123456789 b"), std::string("a 765611980123456789 b"));
	CHECK_EQ(r.Apply("a 7656119801234567 b"), std::string("a 7656119801234567 b"));
	CHECK_EQ(r.Apply("a 12345678901234567 b"), std::string("a 12345678901234567 b"));
	CHECK_EQ(r.Apply("hash 176561198012345678"), std::string("hash 176561198012345678"));
	CHECK_EQ(r.Replacements(), static_cast<size_t>(4));
}

void Addresses()
{
	LogRedactor r;
	CHECK_EQ(r.Apply("Join: 192.168.1.33:27015 and 8.8.4.4."), std::string("Join: <ip1>:27015 and <ip2>."));
	CHECK_EQ(r.Apply("again 192.168.1.33"), std::string("again <ip1>"));
	CHECK_EQ(r.Apply("virtual 127.77.0.2, local 127.0.0.1, none 0.0.0.0"), std::string("virtual 127.77.0.2, local 127.0.0.1, none 0.0.0.0"));
	CHECK_EQ(r.Apply("version 0.31.57 build 1.2.3"), std::string("version 0.31.57 build 1.2.3"));
	CHECK_EQ(r.Apply("driver 32.0.15.6094 and 10.0.26200.1"), std::string("driver 32.0.15.6094 and 10.0.26200.1"));
	CHECK_EQ(r.Apply("not 256.1.1.1 nor 1.2.3.4.5"), std::string("not 256.1.1.1 nor 1.2.3.4.5"));
	CHECK_EQ(r.Apply("[01:02:03.456] I x: 10.0.0.5"), std::string("[01:02:03.456] I x: <ip3>"));
	CHECK_EQ(r.Apply("folder THIONG-01.23.29-192.168.1.33\\desync.txt"), std::string("folder THIONG-01.23.29-<ip1>\\desync.txt"));
}

void Names()
{
	LogRedactor r;
	r.AddName("THIONG", "you");
	r.AddName("Maria");
	r.AddName("ab");
	CHECK_EQ(r.Apply("steam: online ready as THIONG"), std::string("steam: online ready as <you>"));
	CHECK_EQ(r.Apply("room of maria, MARIA's army and thiong-01.02.03"), std::string("room of <player1>, <player1>'s army and <you>-01.02.03"));
	CHECK_EQ(r.Apply("Mariana and xTHIONG and THIONG_2 stay"), std::string("Mariana and xTHIONG and THIONG_2 stay"));
	CHECK_EQ(r.Apply("ab stays"), std::string("ab stays"));
	r.AddName("Maria");
	CHECK_EQ(r.Apply("Maria"), std::string("<player1>"));
}

void GameWords()
{
	LogRedactor r;
	r.AddName("Imperivm");
	r.AddName("STEAM");
	r.AddName("Maria");
	CHECK_EQ(r.Apply("Imperivm Resurrected 1.0.1, Logs\\imperivm\\ and steam: room of Imperivm with Maria"),
		std::string("Imperivm Resurrected 1.0.1, Logs\\imperivm\\ and steam: room of Imperivm with <player1>"));
}

void LongestNameFirst()
{
	LogRedactor r;
	r.AddName("Sam");
	r.AddName("Sam Houston", "boss");
	CHECK_EQ(r.Apply("by Sam Houston and Sam"), std::string("by <boss> and <player1>"));
}

void NonAscii()
{
	LogRedactor r;
	r.AddName("Rom\xc3\xa1n", "roman");
	CHECK_EQ(r.Apply("player Rom\xc3\xa1n joined"), std::string("player <roman> joined"));
	CHECK_EQ(r.Apply("player ROM\xc3\xa1N joined"), std::string("player <roman> joined"));
	CHECK_EQ(r.Apply("player Rom\xc3\x81n joined"), std::string("player Rom\xc3\x81n joined"));
}

void UserName()
{
	LogRedactor r;
	r.AddUserName("jdoe");
	CHECK_EQ(r.Apply("C:\\Users\\jdoe\\Desktop\\x.txt and c:/users/JDOE/y"), std::string("C:\\Users\\<user>\\Desktop\\x.txt and c:/users/<user>/y"));
	CHECK_EQ(r.Apply("C:\\Users\\other\\Desktop"), std::string("C:\\Users\\other\\Desktop"));
	CHECK_EQ(r.Apply("jdoe owns it"), std::string("<user> owns it"));

	LogRedactor shortName;
	shortName.AddUserName("jo");
	CHECK_EQ(shortName.Apply("C:\\Users\\jo\\Documents, jo said"), std::string("C:\\Users\\<user>\\Documents, jo said"));
	CHECK_EQ(shortName.Apply("C:\\Users\\joe\\Documents"), std::string("C:\\Users\\joe\\Documents"));

	LogRedactor spaced;
	spaced.AddUserName("John Smith");
	CHECK_EQ(spaced.Apply("C:\\Users\\John Smith\\AppData"), std::string("C:\\Users\\<user>\\AppData"));
}

void Mixed()
{
	LogRedactor r;
	r.AddName("THIONG", "you");
	r.AddUserName("jdoe");
	const std::string line = "[12:00:00.000] I steam: online ready as THIONG (76561198012345678), from 83.45.12.9, in C:\\Users\\jdoe\\x";
	CHECK_EQ(r.Apply(line), std::string("[12:00:00.000] I steam: online ready as <you> (<steamid1>), from <ip1>, in C:\\Users\\<user>\\x"));
	CHECK_EQ(r.Apply(""), std::string());
	CHECK_EQ(r.Apply("nothing to hide\nsecond line\n"), std::string("nothing to hide\nsecond line\n"));
}

void IniSecrets()
{
	CHECK_EQ(RedactIniSecrets("[A]\r\nApiKey=abc123\r\nName=x\r\nAccessToken = zzz\r\n; key=comment\r\nPASSWORD=hunter2\r\nEmptyKey=\r\n"),
		std::string("[A]\r\nApiKey=<hidden>\r\nName=x\r\nAccessToken =<hidden>\r\n; key=comment\r\nPASSWORD=<hidden>\r\nEmptyKey=\r\n"));
	CHECK_EQ(RedactIniSecrets("Secret=1"), std::string("Secret=<hidden>"));
	CHECK_EQ(RedactIniSecrets(""), std::string());
	CHECK_EQ(RedactIniSecrets("\n\n"), std::string("\n\n"));
	CHECK_EQ(RedactIniSecrets("Mode=fullscreen\nVSync=1\n"), std::string("Mode=fullscreen\nVSync=1\n"));
}

void Big()
{
	LogRedactor r;
	r.AddName("THIONG", "you");
	std::string text;
	for (int i = 0; i < 200000; i++) text += "[12:00:00.000] D net: slot " + std::to_string(i % 8) + " tick " + std::to_string(i) + " THIONG\n";
	const std::string out = r.Apply(text);
	CHECK(out.size() < text.size());
	CHECK_EQ(r.Replacements(), static_cast<size_t>(200000));
}

}

int main()
{
	SteamIds();
	Addresses();
	Names();
	GameWords();
	LongestNameFirst();
	NonAscii();
	UserName();
	Mixed();
	IniSecrets();
	Big();
	return test::Summary("logredact");
}
