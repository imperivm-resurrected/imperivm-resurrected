// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdio>
#include <map>
#include <string>

#include "hooks/builtinfixes.h"
#include "hooks/builtin_data.h"
#include "core/log.h"
#include "core/winpath.h"

namespace {

const char kCacheDir[] = "imperivm-resurrected/_cache/builtin/";

const char kHeroAttackIndependentPath[] = "data/subai/hero_attack_independent.vs";
const char kHeroAttackIndependent[] =
	"// void, Obj me, Obj u\n"
	"\n"
	"Hero this;\n"
	"Unit un;\n"
	"int i;\n"
	"\n"
	"this = me.AsHero;\n"
	"if (!.IsValid)\n"
	"\treturn;\n"
	"\n"
	".army.ClearDead;\n"
	"for (i = 0; i < .army.count; i += 1) {\n"
	"\tun = .army[i].AsUnit;\n"
	"\tif (un.IsValid)\n"
	"\t\tun.SetCommand(\"attack_independent\", u);\n"
	"}\n"
	"\n"
	".AddCommand(true, \"attack_independent_unit\", u);\n";

const char kHeroClassPath[] = "data/classes/hero.sc.xml";
const char kHeroClassPatch[] =
	"<patch>\n"
	"  <add select=\"/class\">\n"
	"    <method sig=\"attack_independent\" vs=\"data/subai/hero_attack_independent.vs\" verify=\"data/subai/attack_independent_verify.vs\"/>\n"
	"    <method sig=\"attack_independent_unit\" vs=\"data/subai/unit_capture.vs\"/>\n"
	"  </add>\n"
	"</patch>\n";

bool g_enabled = false;
bool g_texts = false;

using winpath::GameDir;

void MakeDirs(std::string path)
{
	for (size_t i = 0; i < path.size(); i++) {
		if (path[i] == '/') path[i] = '\\';
	}
	for (size_t i = 3; i < path.size(); i++) {
		if (path[i] != '\\') continue;
		path[i] = 0;
		CreateDirectoryA(path.c_str(), nullptr);
		path[i] = '\\';
	}
}

bool WriteIfDifferent(const std::string &file, const char *text)
{
	std::string disk = file;
	for (char &c : disk) {
		if (c == '/') c = '\\';
	}
	const size_t size = strlen(text);
	FILE *in = nullptr;
	if (fopen_s(&in, disk.c_str(), "rb") == 0 && in) {
		std::string have(size + 1, '\0');
		const size_t got = fread(&have[0], 1, size + 1, in);
		fclose(in);
		if (got == size && memcmp(have.data(), text, size) == 0) return true;
	}
	MakeDirs(disk);
	FILE *out = nullptr;
	if (fopen_s(&out, disk.c_str(), "wb") != 0 || !out) return false;
	const bool ok = fwrite(text, 1, size, out) == size;
	fclose(out);
	return ok;
}

}

void BuiltinFixesInstall(bool knownExe)
{
	if (!knownExe) return;
	g_texts = true;
	const std::string target = GameDir() + "\\" + kCacheDir + kHeroAttackIndependentPath;
	g_enabled = WriteIfDifferent(target, kHeroAttackIndependent);
	LogI("game", "fixes: hero attack on an independent stronghold with its army: %s", g_enabled ? "ready" : "Could NOT prepare the script");
}

bool BuiltinFilePath(const std::string &rel, std::string &diskRel)
{
	if (!g_enabled || rel != kHeroAttackIndependentPath) return false;
	diskRel = std::string(kCacheDir) + rel;
	return true;
}

std::vector<BuiltinPatch> BuiltinPatches()
{
	std::vector<BuiltinPatch> out;
	if (g_enabled) out.push_back({kHeroClassPath, kHeroClassPatch, ""});
	if (g_texts) {
		int count = 0;
		const BuiltinText *texts = BuiltinPatchTexts(count);
		for (int i = 0; i < count; i++) out.push_back({texts[i].path, texts[i].text, texts[i].gameFileSha256 ? texts[i].gameFileSha256 : ""});
	}
	return out;
}
