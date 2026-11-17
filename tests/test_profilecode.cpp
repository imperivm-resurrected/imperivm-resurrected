// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>
#include <vector>

#include "core/base64.h"
#include "i18n/tr.h"
#include "mods/profilecode.h"

namespace {

const std::string &TestLanguage()
{
	static const std::string english = "english";
	return english;
}

ModsSharedMod Shared(const std::string &id, const std::string &name, const std::string &version, uint64_t gbId)
{
	ModsSharedMod m;
	m.id = id;
	m.name = name;
	m.version = version;
	m.gbId = gbId;
	return m;
}

ModsSharedProfile Sample()
{
	ModsSharedProfile sp;
	sp.name = "Campa\xc3\xb1" "a \"hard\" / v2";
	sp.mods.push_back(Shared("hd-voices", "HD Voices", "1.2.0", 512345));
	sp.mods.push_back(Shared("local-skin", "My skin, with commas", "", 0));
	sp.mods.push_back(Shared("big-id", "Big", "0.1", 1ull << 60));
	sp.choices.push_back({"local-skin", "hd-voices"});
	sp.files.push_back({"gameres/interface/menu.tga", "hd-voices"});
	sp.files.push_back({"currentlang/voices/hero/01.ogg", "local-skin"});
	return sp;
}

int Problem(const std::string &text)
{
	ModsSharedProfile out;
	int problem = -1;
	const bool ok = DecodeProfileCode(text, out, &problem);
	CHECK_EQ(ok, problem == kProfileCodeOk);
	if (!ok) CHECK(out.mods.empty() && out.name.empty() && out.choices.empty() && out.files.empty());
	return problem;
}

std::string Code(const std::string &json) { return "IMPV1-" + Base64UrlEncode(json); }

void RoundTrip()
{
	const ModsSharedProfile sp = Sample();
	const std::string code = EncodeProfileCode(sp);
	CHECK_EQ(code.compare(0, 6, "IMPV1-"), 0);
	CHECK(code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", 6) == std::string::npos);
	CHECK(code.size() < 480);
	ModsSharedProfile out;
	int problem = -1;
	CHECK(DecodeProfileCode(code, out, &problem));
	CHECK_EQ(problem, static_cast<int>(kProfileCodeOk));
	CHECK_EQ(out.name, sp.name);
	CHECK_EQ(out.mods.size(), sp.mods.size());
	for (size_t i = 0; i < out.mods.size() && i < sp.mods.size(); i++) {
		CHECK_EQ(out.mods[i].id, sp.mods[i].id);
		CHECK_EQ(out.mods[i].name, sp.mods[i].name);
		CHECK_EQ(out.mods[i].version, sp.mods[i].version);
		CHECK_EQ(out.mods[i].gbId, sp.mods[i].gbId);
		CHECK(!out.mods[i].installed);
		CHECK(out.mods[i].folder.empty());
	}
	CHECK_EQ(out.choices.size(), static_cast<size_t>(1));
	if (out.choices.size() == 1) {
		CHECK_EQ(out.choices[0].use, std::string("local-skin"));
		CHECK_EQ(out.choices[0].over, std::string("hd-voices"));
	}
	CHECK_EQ(out.files.size(), static_cast<size_t>(2));
	if (out.files.size() == 2) {
		CHECK_EQ(out.files[0].path, std::string("gameres/interface/menu.tga"));
		CHECK_EQ(out.files[0].use, std::string("hd-voices"));
		CHECK_EQ(out.files[1].path, std::string("currentlang/voices/hero/01.ogg"));
		CHECK_EQ(out.files[1].use, std::string("local-skin"));
	}
	CHECK_EQ(EncodeProfileCode(out), code);
}

void EmptyProfile()
{
	ModsSharedProfile sp;
	const std::string code = EncodeProfileCode(sp);
	ModsSharedProfile out;
	out.name = "old";
	CHECK(DecodeProfileCode(code, out));
	CHECK(out.name.empty());
	CHECK(out.mods.empty());
}

void SkipsWhatCannotBeRead()
{
	ModsSharedProfile sp;
	sp.name = "x";
	sp.mods.push_back(Shared("", "No id", "1", 0));
	sp.mods.push_back(Shared("ok", "Ok", "1", 0));
	sp.choices.push_back({"", "ok"});
	sp.files.push_back({"gameres/menu.tga", ""});
	ModsSharedProfile out;
	CHECK(DecodeProfileCode(EncodeProfileCode(sp), out));
	CHECK_EQ(out.mods.size(), static_cast<size_t>(1));
	CHECK(out.choices.empty());
	CHECK(out.files.empty());
}

void AroundTheCode()
{
	const std::string code = EncodeProfileCode(Sample());
	const std::string lower = "impv1-" + code.substr(6);
	for (const std::string &text : {"  " + code + "\r\n", "\t\"" + code + "\" ", "'" + code + "'", "`" + code + "`", "\xe2\x80\x9c" + code + "\xe2\x80\x9d",
			 "\xc2\xab" + code + "\xc2\xbb", "My profile: " + code + ". Have fun!", "(" + code + ")", lower}) {
		ModsSharedProfile out;
		CHECK(DecodeProfileCode(text, out));
		CHECK_EQ(out.mods.size(), static_cast<size_t>(3));
	}
}

void Garbage()
{
	for (const std::string &text : {std::string(""), std::string("   "), std::string("hello"), std::string("IMPV"), std::string("IMPV-abc"),
			 std::string("IMPVx1-abc"), std::string("IMPV0-") + EncodeProfileCode(Sample()).substr(6), std::string("xIMPV1-") + EncodeProfileCode(Sample()).substr(6),
			 std::string(70000, 'a') + EncodeProfileCode(Sample())}) {
		CHECK_EQ(Problem(text), static_cast<int>(kProfileCodeNotACode));
	}
	for (const std::string &text : {std::string("IMPV1-"), std::string("IMPV1-!!!!"), std::string("IMPV1-a"), Code("not json"), Code("[1,2]"), Code("{}"),
			 Code("{\"n\":\"x\"}"), Code("{\"n\":5,\"m\":[]}"), Code("{\"m\":{}}"), Code("{\"m\":[[]]}"), Code("{\"m\":[[\"\"]]}"),
			 Code("{\"m\":[[\"a\",1]]}"), Code("{\"m\":[[\"a\",\"b\",\"c\",\"12x\"]]}"), Code("{\"m\":[[\"a\",\"b\",\"c\",\"1\",\"e\"]]}"),
			 Code("{\"m\":[\"a\"]}"), Code("{\"m\":[],\"c\":[[\"a\"]]}"), Code("{\"m\":[],\"c\":{}}"), Code("{\"m\":[],\"f\":[[\"a\",\"\"]]}"),
			 Code("{\"m\":[],\"f\":[[\"a\",2]]}"), Code("{\"m\":[[\"a\",\"b\",\"c\",-4]]}"), Code("{\"m\":[[\"a\",\"b\",\"c\",1.5]]}")}) {
		CHECK_EQ(Problem(text), static_cast<int>(kProfileCodeDamaged));
	}
}

void Truncated()
{
	const std::string code = EncodeProfileCode(Sample());
	for (size_t len = 6; len < code.size(); len++) {
		CHECK_EQ(Problem(code.substr(0, len)), static_cast<int>(kProfileCodeDamaged));
	}
	CHECK_EQ(Problem(code.substr(0, code.size() / 2) + " " + code.substr(code.size() / 2)), static_cast<int>(kProfileCodeDamaged));
}

void Newer()
{
	const std::string body = EncodeProfileCode(Sample()).substr(6);
	CHECK_EQ(Problem("IMPV2-" + body), static_cast<int>(kProfileCodeNewer));
	CHECK_EQ(Problem("impv12-abc"), static_cast<int>(kProfileCodeNewer));
	CHECK_EQ(Problem("IMPV99999999999999999999-abc"), static_cast<int>(kProfileCodeNewer));
}

void GameBananaIds()
{
	ModsSharedProfile out;
	CHECK(DecodeProfileCode(Code("{\"n\":\"x\",\"m\":[[\"a\",\"A\",\"1\",4567],[\"b\",\"B\",\"2\",\"18446744073709551615\"],[\"c\"]]}"), out));
	CHECK_EQ(out.mods.size(), static_cast<size_t>(3));
	if (out.mods.size() == 3) {
		CHECK_EQ(out.mods[0].gbId, 4567ull);
		CHECK_EQ(out.mods[1].gbId, 18446744073709551615ull);
		CHECK_EQ(out.mods[2].gbId, 0ull);
		CHECK(out.mods[2].name.empty());
		CHECK(out.mods[2].version.empty());
	}
}

}

int main()
{
	TrSetLanguageSource(&TestLanguage);
	RoundTrip();
	EmptyProfile();
	SkipsWhatCannotBeRead();
	AroundTheCode();
	Garbage();
	Truncated();
	Newer();
	GameBananaIds();
	return test::Summary("profilecode");
}
