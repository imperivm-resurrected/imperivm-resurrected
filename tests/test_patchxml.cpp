// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "mods/patchxml.h"

static const char *kCommands =
	"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
	"<commands>\n"
	"\t<!-- keep this comment -->\n"
	"\t<cmd name=\"trainRHastatus\" method=\"train\" costgold=\"100\" costpop=\"1\" execdelay=\"6000\">\n"
	"\t\t<src obj=\"RBarracks\"/>\n"
	"\t</cmd>\n"
	"\t<cmd name=\"trainRArcher\" key=\"A\" costgold=\"40\"/>\n"
	"\t<cmd name=\"other\" costgold=\"1\"/>\n"
	"</commands>\n";

static bool Contains(const std::string &data, const std::string &part)
{
	if (data.find(part) != std::string::npos) return true;
	std::printf("missing %s in:\n%s\n", test::Show(part).c_str(), data.c_str());
	return false;
}

static bool Lacks(const std::string &data, const std::string &part)
{
	if (data.find(part) == std::string::npos) return true;
	std::printf("unexpected %s in:\n%s\n", test::Show(part).c_str(), data.c_str());
	return false;
}

static bool Xml(std::string &data, const std::string &patch, std::vector<std::string> &errors)
{
	errors.clear();
	return PatchXmlText(data, patch, errors);
}

static void XmlSet()
{
	std::string data = kCommands;
	std::vector<std::string> errors;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='trainRHastatus']\" costgold=\"60\" execdelay=\"3000\" unset=\"costpop\"/></patch>", errors));
	CHECK(errors.empty());
	CHECK(Contains(data, "costgold=\"60\""));
	CHECK(Contains(data, "execdelay=\"3000\""));
	CHECK(Lacks(data, "costpop"));
	CHECK(Contains(data, "<!-- keep this comment -->"));
	CHECK(Contains(data, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>"));
	CHECK(Contains(data, "<cmd name=\"trainRArcher\" key=\"A\" costgold=\"40\"/>"));
	CHECK(Contains(data, "<src obj=\"RBarracks\"/>"));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='trainRArcher']\" rollover=\"Archer\" key=\"Z\"/></patch>", errors));
	CHECK(Contains(data, "rollover=\"Archer\""));
	CHECK(Contains(data, "key=\"Z\""));
	CHECK(Lacks(data, "key=\"A\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd\" extra=\"1\"/></patch>", errors));
	size_t count = 0;
	for (size_t at = data.find("extra=\"1\""); at != std::string::npos; at = data.find("extra=\"1\"", at + 1)) count++;
	CHECK_EQ(count, size_t(3));
}

static void XmlAdd()
{
	std::vector<std::string> errors;
	std::string data = kCommands;
	CHECK(Xml(data, "<patch><add select=\"/commands\"><cmd name=\"added\" costgold=\"5\"/></add></patch>", errors));
	CHECK(errors.empty());
	CHECK(Contains(data, "<cmd name=\"added\" costgold=\"5\"/>"));
	CHECK(data.find("added") > data.find("name=\"other\""));
	CHECK(Contains(data, "</commands>"));

	data = kCommands;
	CHECK(Xml(data, "<patch><add select=\"/commands\" position=\"start\"><cmd name=\"first\"/></add></patch>", errors));
	CHECK(data.find("name=\"first\"") < data.find("name=\"trainRHastatus\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><add select=\"/commands/cmd[@name='trainRArcher']\" position=\"before\"><cmd name=\"b4\"/></add></patch>", errors));
	CHECK(data.find("name=\"b4\"") < data.find("name=\"trainRArcher\""));
	CHECK(data.find("name=\"b4\"") > data.find("name=\"trainRHastatus\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><add select=\"/commands/cmd[@name='trainRArcher']\" position=\"after\"><cmd name=\"af\"/></add></patch>", errors));
	CHECK(data.find("name=\"af\"") > data.find("name=\"trainRArcher\""));
	CHECK(data.find("name=\"af\"") < data.find("name=\"other\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><add select=\"//cmd[@name='trainRHastatus']\"><src obj=\"Second\"/></add></patch>", errors));
	CHECK(data.find("Second") > data.find("RBarracks"));
	CHECK(data.find("Second") < data.find("name=\"trainRArcher\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><add select=\"/commands\"><cmd name=\"one\"/><cmd name=\"two\"/></add></patch>", errors));
	CHECK(data.find("name=\"one\"") < data.find("name=\"two\""));
}

static void XmlRemoveReplace()
{
	std::vector<std::string> errors;
	std::string data = kCommands;
	CHECK(Xml(data, "<patch><remove select=\"//cmd[@name='other']\"/></patch>", errors));
	CHECK(errors.empty());
	CHECK(Lacks(data, "name=\"other\""));
	CHECK(Contains(data, "name=\"trainRArcher\""));
	CHECK(Contains(data, "</commands>"));

	data = kCommands;
	CHECK(Xml(data, "<patch><remove select=\"/commands/cmd[@key]\"/></patch>", errors));
	CHECK(Lacks(data, "trainRArcher"));
	CHECK(Contains(data, "trainRHastatus"));

	data = kCommands;
	CHECK(Xml(data, "<patch><replace select=\"/commands/cmd[@name='trainRHastatus']/src\"><src obj=\"RBarracks2\"/></replace></patch>", errors));
	CHECK(Lacks(data, "obj=\"RBarracks\""));
	CHECK(Contains(data, "obj=\"RBarracks2\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><replace select=\"/commands/cmd[@name='other']\"><cmd name=\"x\"/><cmd name=\"y\"/></replace></patch>", errors));
	CHECK(Lacks(data, "name=\"other\""));
	CHECK(data.find("name=\"x\"") < data.find("name=\"y\""));
}

static void XmlSelectors()
{
	std::vector<std::string> errors;
	std::string data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[2]\" mark=\"two\"/></patch>", errors));
	CHECK(data.find("mark=\"two\"") > data.find("trainRArcher") - 1);
	CHECK(data.find("mark=\"two\"") < data.find("name=\"other\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"//CMD[@NAME='OTHER']\" mark=\"ci\"/></patch>", errors));
	CHECK(Contains(data, "mark=\"ci\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select='//cmd[@name=\"other\"]' mark=\"dq\"/></patch>", errors));
	CHECK(Contains(data, "mark=\"dq\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"//*[@name='trainRArcher']\" mark=\"any\"/></patch>", errors));
	CHECK(Contains(data, "mark=\"any\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@costgold][3]\" mark=\"chain\"/></patch>", errors));
	CHECK(data.find("mark=\"chain\"") > data.find("name=\"other\""));

	data = kCommands;
	CHECK(!Xml(data, "<patch><set select=\"/commands/cmd[@name='x'\" mark=\"1\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));

	data = kCommands;
	CHECK(!Xml(data, "<patch><set mark=\"1\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
}

static void XmlErrors()
{
	std::vector<std::string> errors;
	std::string data = kCommands;
	CHECK(!Xml(data, "<patch><set select=\"/commands/cmd[@name='missing']\" a=\"1\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK_EQ(data, kCommands);

	CHECK(!Xml(data, "<patch><set select=\"/commands/cmd[@name='missing']\" a=\"1\" optional=\"yes\"/></patch>", errors));
	CHECK(errors.empty());
	CHECK(!Xml(data, "<patch><set select=\"/commands/cmd[@name='missing']\" a=\"1\" optional=\"si\"/></patch>", errors));
	CHECK(errors.empty());

	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='missing']\" a=\"1\"/><set select=\"/commands/cmd[@name='other']\" ok=\"1\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK(Contains(data, "ok=\"1\""));

	data = kCommands;
	CHECK(!Xml(data, "<patch><set select=\"/commands\" a=\"1\"></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK_EQ(data, kCommands);

	CHECK(!Xml(data, "just text", errors));
	CHECK_EQ(errors.size(), size_t(1));

	CHECK(!Xml(data, "<patch><explode select=\"/commands\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));

	CHECK(!Xml(data, "<patch><add select=\"/commands\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));

	CHECK(!Xml(data, "<patch><add select=\"/commands\" position=\"middle\"><cmd name=\"q\"/></add></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));

	std::string broken = "<commands><cmd name=\"a\"></commands>";
	CHECK(!Xml(broken, "<patch><set select=\"/commands\" a=\"1\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK_EQ(broken, "<commands><cmd name=\"a\"></commands>");

	CHECK(!Xml(data, "<patch/>", errors));
}

static void XmlFidelity()
{
	std::vector<std::string> errors;
	std::string data = std::string("\xEF\xBB\xBF") + kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='other']\" costgold=\"2\"/></patch>", errors));
	CHECK(data.compare(0, 3, "\xEF\xBB\xBF") == 0);
	CHECK(Contains(data, "costgold=\"2\""));

	data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='other']\" rollover=\"Fish &amp; Chips &lt;1&gt;\"/></patch>", errors));
	CHECK(Contains(data, "rollover=\"Fish &amp; Chips &lt;1&gt;\""));

	data = "<root>\r\n  <a x=\"1\"/>\r\n</root>\r\n";
	CHECK(Xml(data, "<patch><set select=\"/root/a\" x=\"2\"/></patch>", errors));
	CHECK_EQ(data, "<root>\r\n  <a x=\"2\"/>\r\n</root>\r\n");

	data = "<root><a x=\"1\"/><!-- c --><![CDATA[ raw <b> ]]></root>";
	CHECK(Xml(data, "<patch><set select=\"/root/a\" x=\"2\"/></patch>", errors));
	CHECK_EQ(data, "<root><a x=\"2\"/><!-- c --><![CDATA[ raw <b> ]]></root>");

	data = kCommands;
	std::string twice = kCommands;
	const std::string patch = "<patch><set select=\"/commands/cmd[@name='other']\" costgold=\"9\"/></patch>";
	CHECK(Xml(data, patch, errors));
	CHECK(Xml(twice, patch, errors));
	CHECK(Xml(twice, patch, errors));
	CHECK_EQ(data, twice);
}

static void XmlStacking()
{
	std::vector<std::string> errors;
	std::string data = kCommands;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='other']\" costgold=\"2\"/></patch>", errors));
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='other']\" costfood=\"3\"/></patch>", errors));
	CHECK(Xml(data, "<patch><add select=\"/commands\"><cmd name=\"third\"/></add></patch>", errors));
	CHECK(Contains(data, "costgold=\"2\""));
	CHECK(Contains(data, "costfood=\"3\""));
	CHECK(Contains(data, "name=\"third\""));
	CHECK(Contains(data, "name=\"trainRHastatus\""));
}

static const char *kIni =
	"; game constants\r\n"
	"[GamePlay]\r\n"
	"DamageAmplify = 200 ;old comment\r\n"
	"TeachingLevel2 = 5\r\n"
	"\r\n"
	"[GameOptions Objects]\r\n"
	"RSText\r\n"
	"RButton\r\n";

static bool Ini(std::string &data, const std::string &patch, std::vector<std::string> &errors)
{
	errors.clear();
	return PatchIniText(data, patch, errors);
}

static void IniBasics()
{
	std::vector<std::string> errors;
	std::string data = kIni;
	CHECK(Ini(data, "[GamePlay]\nDamageAmplify = 250\n", errors));
	CHECK(errors.empty());
	CHECK(Contains(data, "DamageAmplify = 250\r\n"));
	CHECK(Lacks(data, "old comment"));
	CHECK(Contains(data, "TeachingLevel2 = 5\r\n"));
	CHECK(Contains(data, "; game constants\r\n"));

	data = kIni;
	CHECK(Ini(data, "[gameplay]\ndamageamplify=300\n", errors));
	CHECK(Contains(data, "damageamplify = 300\r\n"));
	CHECK(Lacks(data, "DamageAmplify"));

	data = kIni;
	CHECK(Ini(data, "[GamePlay]\nNewKey = 1\n", errors));
	CHECK(data.find("NewKey = 1") > data.find("TeachingLevel2"));
	CHECK(data.find("NewKey = 1") < data.find("[GameOptions Objects]"));

	data = kIni;
	CHECK(Ini(data, "[Brand New]\nA = 1\nB = 2\n", errors));
	CHECK(Contains(data, "[Brand New]\r\nA = 1\r\n"));
	CHECK(data.find("B = 2") > data.find("A = 1"));
}

static void IniRemoveAdd()
{
	std::vector<std::string> errors;
	std::string data = kIni;
	CHECK(Ini(data, "[GamePlay]\n-TeachingLevel2\n", errors));
	CHECK(errors.empty());
	CHECK(Lacks(data, "TeachingLevel2"));
	CHECK(Contains(data, "DamageAmplify"));

	data = kIni;
	CHECK(Ini(data, "[GameOptions Objects]\n-RSText\n+MyButton\n+RButton\n", errors));
	CHECK(errors.empty());
	CHECK(Lacks(data, "RSText"));
	CHECK(Contains(data, "MyButton\r\n"));
	size_t count = 0;
	for (size_t at = data.find("RButton"); at != std::string::npos; at = data.find("RButton", at + 1)) count++;
	CHECK_EQ(count, size_t(1));

	data = kIni;
	CHECK(!Ini(data, "[GamePlay]\n-Missing\n", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK_EQ(data, kIni);

	data = kIni;
	CHECK(Ini(data, "[Fresh]\n+Line\n", errors));
	CHECK(Contains(data, "[Fresh]\r\nLine\r\n"));
}

static void IniErrorsAndFormat()
{
	std::vector<std::string> errors;
	std::string data = kIni;
	CHECK(!Ini(data, "DamageAmplify = 1\n", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK(!Ini(data, "[GamePlay]\nnot a pair\n", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK(!Ini(data, "; only a comment\n\n", errors));
	CHECK(errors.empty());
	CHECK_EQ(data, kIni);

	data = "[A]\nk = 1\n";
	CHECK(Ini(data, "[A]\nk = 2\n", errors));
	CHECK_EQ(data, "[A]\nk = 2\n");

	data = std::string("\xEF\xBB\xBF") + "[A]\r\nk = 1\r\n";
	CHECK(Ini(data, "[A]\r\nk = 2\r\n", errors));
	CHECK_EQ(data, std::string("\xEF\xBB\xBF") + "[A]\r\nk = 2\r\n");

	data = "[A]\r\nk = 1";
	CHECK(Ini(data, "[A]\nj = 3\n", errors));
	CHECK(Contains(data, "j = 3"));

	data = kIni;
	CHECK(Ini(data, "[GamePlay]\nDamageAmplify = 250 ;trailing\n", errors));
	CHECK(Contains(data, "DamageAmplify = 250 ;trailing\r\n"));
}

static const char *kScript =
	"// GetTacticScript\r\n"
	"int nRace;\r\n"
	"int player;\r\n"
	"nRace = set.GetCentralBuilding.race;\r\n"
	"player = set.player;\r\n"
	"return TS_Default;\r\n";

static bool Script(std::string &data, const std::string &patch, std::vector<std::string> &errors)
{
	errors.clear();
	return PatchScriptText(data, patch, errors);
}

static void ScriptDirectives()
{
	std::vector<std::string> errors;
	std::string data = kScript;
	CHECK(Script(data, "@before nRace = set.GetCentralBuilding.race;\n// MOD\nif (x) return 1;\n\n@after player = set.player;\nLog(1);\n", errors));
	CHECK(errors.empty());
	CHECK_EQ(data,
		"// GetTacticScript\r\n"
		"int nRace;\r\n"
		"int player;\r\n"
		"// MOD\r\n"
		"if (x) return 1;\r\n"
		"nRace = set.GetCentralBuilding.race;\r\n"
		"player = set.player;\r\n"
		"Log(1);\r\n"
		"return TS_Default;\r\n");

	data = kScript;
	CHECK(Script(data, "@replace return TS_Default\nreturn TS_Other;\n", errors));
	CHECK(Contains(data, "return TS_Other;\r\n"));
	CHECK(Lacks(data, "TS_Default"));

	data = kScript;
	CHECK(Script(data, "@top\nint extra;\n@bottom\n// tail\n", errors));
	CHECK(data.find("int extra;") == std::string("// GetTacticScript\r\n").size());
	CHECK(Contains(data, "return TS_Default;\r\n// tail\r\n"));

	data = kScript;
	CHECK(Script(data, "@after int player;\nint a;\n@end\nthis is ignored\n@before return\nint b;\n", errors));
	CHECK(Lacks(data, "this is ignored"));
	CHECK(Contains(data, "int a;"));
	CHECK(Contains(data, "int b;\r\nreturn TS_Default;"));

	data = "int a;\nreturn 1;\n";
	CHECK(Script(data, "@before return\nint z;\n", errors));
	CHECK_EQ(data, "int a;\nint z;\nreturn 1;\n");
}

static void ScriptLegacyAndErrors()
{
	std::vector<std::string> errors;
	std::string data = kScript;
	CHECK(Script(data, "@antes return TS_Default\nint viejo;\n@despues int player;\nint tras;\n", errors));
	CHECK(errors.empty());
	CHECK(Contains(data, "int viejo;\r\nreturn TS_Default;"));
	CHECK(Contains(data, "int player;\r\nint tras;"));

	data = kScript;
	CHECK(!Script(data, "@before no such line\nint z;\n", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK_EQ(data, kScript);

	CHECK(!Script(data, "@explode\nint z;\n", errors));
	CHECK_EQ(errors.size(), size_t(1));

	CHECK(!Script(data, "@before\nint z;\n", errors));
	CHECK_EQ(errors.size(), size_t(1));

	CHECK(!Script(data, "@bottom\n\n\n", errors));
	CHECK_EQ(errors.size(), size_t(1));

	CHECK(Script(data, "@before no such line\nint z;\n@bottom\n// ok\n", errors));
	CHECK_EQ(errors.size(), size_t(1));
	CHECK(Contains(data, "// ok"));
}

static void RemovedByAnotherPatch()
{
	std::vector<std::string> errors;
	std::set<std::string> removed;
	std::string data = kIni;
	CHECK(PatchIniText(data, "[GamePlay]\n-TeachingLevel2\n", errors, &removed));
	CHECK(errors.empty());
	CHECK(!PatchIniText(data, "[gameplay]\n-teachinglevel2\n", errors, &removed));
	CHECK(errors.empty());
	CHECK(Lacks(data, "TeachingLevel2"));
	CHECK(!PatchIniText(data, "[GamePlay]\n-TeachingLevel3\n", errors, &removed));
	CHECK_EQ(errors.size(), size_t(1));
	errors.clear();
	CHECK(!PatchIniText(data, "[GamePlay]\n-TeachingLevel2\n", errors));
	CHECK_EQ(errors.size(), size_t(1));
	errors.clear();
	CHECK(PatchIniText(data, "[GamePlay]\nTeachingLevel2 = 7\n", errors, &removed));
	CHECK(PatchIniText(data, "[GamePlay]\n-TeachingLevel2\n", errors, &removed));
	CHECK(errors.empty());
	CHECK(Lacks(data, "TeachingLevel2"));

}

static void XmlRemovedByAnotherPatch()
{
	std::vector<std::string> errors;
	XmlPatchRun run(kCommands);
	CHECK(run.Ok());
	CHECK(run.Apply("<patch><remove select=\"//cmd[@name='other']\"/></patch>", errors));
	CHECK(errors.empty());
	CHECK(!run.Apply("<patch><remove select='//CMD[@name=\"other\"]'/></patch>", errors));
	CHECK(!run.Apply("<patch><remove select=\"/commands/cmd[3]\"/></patch>", errors));
	CHECK(!run.Apply("<patch><remove select=\"/commands/cmd[@costgold='1']\"/></patch>", errors));
	CHECK(errors.empty());
	CHECK(Lacks(run.Text(), "\"other\""));
	CHECK(!run.Apply("<patch><remove select=\"//cmd[@name='nothere']\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	errors.clear();
	CHECK(!run.Apply("<patch><set select=\"/commands/cmd[@name='other']\" costgold=\"2\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	errors.clear();

	XmlPatchRun nested(kCommands);
	CHECK(nested.Apply("<patch><remove select=\"//cmd[@name='trainRHastatus']\"/></patch>", errors));
	CHECK(!nested.Apply("<patch><remove select=\"//src[@obj='RBarracks']\"/></patch>", errors));
	CHECK(errors.empty());

	XmlPatchRun replaced(kCommands);
	CHECK(replaced.Apply("<patch><replace select=\"//cmd[@name='other']\"><cmd name=\"renamed\"/></replace></patch>", errors));
	CHECK(!replaced.Apply("<patch><remove select=\"//cmd[@name='other']\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	errors.clear();

	XmlPatchRun renamed(kCommands);
	CHECK(renamed.Apply("<patch><set select=\"//cmd[@name='other']\" name=\"moved\"/></patch>", errors));
	CHECK(!renamed.Apply("<patch><remove select=\"//cmd[@name='other']\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
	errors.clear();

	std::string data = kCommands;
	CHECK(PatchXmlText(data, "<patch><remove select=\"//cmd[@name='other']\"/></patch>", errors));
	CHECK(!PatchXmlText(data, "<patch><remove select=\"//cmd[@name='other']\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));

	XmlPatchRun broken("<commands>");
	CHECK(!broken.Ok());
	errors.clear();
	CHECK(!broken.Apply("<patch><remove select=\"//cmd\"/></patch>", errors));
	CHECK_EQ(errors.size(), size_t(1));
}

static void XmlRunMatchesOneByOne()
{
	const std::vector<std::string> patches = {
		"<patch><set select=\"//cmd[@name='other']\" costgold=\"2\" unset=\"key\"/></patch>",
		"<patch><add select=\"/commands\" position=\"start\"><cmd name=\"first\"/></add></patch>",
		"<patch><add select=\"//cmd[@name='trainRArcher']\" position=\"after\"><cmd name=\"afterArcher\"/><cmd name=\"two\"/></add></patch>",
		"<patch><add select=\"//cmd[@name='trainRHastatus']\"><src obj=\"Extra\"/></add></patch>",
		"<patch><replace select=\"//cmd[@name='two']\"><cmd name=\"three\"/></replace></patch>",
		"<patch><remove select=\"//src[@obj='RBarracks']\"/><add select=\"/commands\" position=\"end\"><cmd name=\"last\"/></add></patch>",
		"<patch><set select=\"//nothing\" a=\"1\"/></patch>"};
	std::string one = kCommands;
	std::vector<std::string> errors;
	XmlPatchRun run(kCommands);
	std::vector<std::string> runErrors;
	for (const std::string &p : patches) {
		const bool a = PatchXmlText(one, p, errors);
		const bool b = run.Apply(p, runErrors);
		CHECK_EQ(a, b);
	}
	CHECK_EQ(run.Text(), one);
	CHECK_EQ(runErrors, errors);

	const std::string mixed = "<a>x<b/> <c/>\n<d/></a>\n";
	const std::vector<std::string> steps = {"<patch><remove select=\"/a/b\"/></patch>", "<patch><remove select=\"/a/c\"/></patch>",
		"<patch><add select=\"/a/d\"><e/></add></patch>", "<patch><remove select=\"/a/d/e\"/></patch>"};
	std::string mixedOne = mixed;
	XmlPatchRun mixedRun(mixed);
	for (const std::string &p : steps) {
		CHECK(PatchXmlText(mixedOne, p, errors));
		CHECK(mixedRun.Apply(p, runErrors));
	}
	CHECK_EQ(mixedRun.Text(), mixedOne);
	std::string bom = "\xEF\xBB\xBF" + std::string(kCommands);
	XmlPatchRun withBom(bom);
	CHECK(withBom.Apply(patches[0], errors));
	CHECK_EQ(withBom.Text().compare(0, 3, "\xEF\xBB\xBF"), 0);
}

static std::string NodeOf(const PatchedValue &v)
{
	return v.key.substr(0, v.key.rfind("/@"));
}

static void XmlResolve()
{
	const std::vector<std::vector<PatchedValue>> r = PatchXmlResolve("Data/Commands.xml", kCommands, {},
		{"<patch><set select=\"//cmd[@name='other']\" costgold=\"5\"/></patch>",
			"<patch><set select=\"/commands/cmd[3]\" CostGold=\"6\" unset=\"key\"/></patch>",
			"<patch><set select=\"//cmd\" execdelay=\"1\"/></patch>",
			"<patch><set select=\"//cmd[@name='nothere']\" costgold=\"1\" optional=\"yes\"/></patch>"});
	CHECK_EQ(r.size(), size_t(4));
	if (r.size() != 4) return;
	CHECK_EQ(r[0].size(), size_t(1));
	CHECK_EQ(r[1].size(), size_t(2));
	CHECK_EQ(r[2].size(), size_t(3));
	CHECK(r[3].empty());
	if (r[0].size() != 1 || r[1].size() != 2 || r[2].size() != 3) return;
	CHECK_EQ(r[0][0].key, r[1][0].key);
	CHECK_EQ(r[0][0].file, std::string("data/commands.xml"));
	CHECK_EQ(r[0][0].label, std::string("//cmd[@name='other']/@costgold"));
	CHECK_EQ(r[0][0].value, std::string("5"));
	CHECK_EQ(r[1][0].label, std::string("/commands/cmd[3]/@CostGold"));
	CHECK(r[1][1].removed);
	CHECK_EQ(NodeOf(r[2][2]), NodeOf(r[0][0]));
	CHECK(NodeOf(r[2][0]) != NodeOf(r[0][0]));

	const std::vector<std::vector<PatchedValue>> added = PatchXmlResolve("x.xml", kCommands, {},
		{"<patch><add select=\"/commands\"><cmd name=\"new\"/></add></patch>", "<patch><set select=\"//cmd[@name='new']\" costgold=\"1\"/></patch>",
			"<patch><set select=\"/commands/cmd[4]\" costgold=\"2\"/></patch>"});
	CHECK(added.size() == 3 && added[0].empty() && added[1].size() == 1 && added[2].size() == 1);
	if (added.size() == 3 && added[1].size() == 1 && added[2].size() == 1) CHECK_EQ(added[1][0].key, added[2][0].key);

	const std::vector<std::vector<PatchedValue>> shifted = PatchXmlResolve("x.xml", kCommands, {"<patch><remove select=\"//cmd[@name='trainRArcher']\"/></patch>"},
		{"<patch><set select=\"/commands/cmd[2]\" costgold=\"1\"/></patch>", "<patch><set select=\"//cmd[@name='other']\" costgold=\"2\"/></patch>"});
	CHECK(shifted.size() == 2 && shifted[0].size() == 1 && shifted[1].size() == 1);
	if (shifted.size() == 2 && shifted[0].size() == 1 && shifted[1].size() == 1) CHECK_EQ(shifted[0][0].key, shifted[1][0].key);

	CHECK(PatchXmlResolve("x.xml", "<commands>", {}, {"<patch/>"}).empty());
}

static const PatchedValue *Value(const std::vector<PatchedValue> &values, const std::string &key)
{
	for (const PatchedValue &v : values) {
		if (v.key == key) return &v;
	}
	std::printf("missing value %s\n", test::Show(key).c_str());
	return nullptr;
}

static void XmlValues()
{
	const std::vector<PatchedValue> v = PatchValues("Data/Units.XML",
		"<patch>\n"
		"\t<set select=\"//Unit[@Name='Legionary'][@civ='Rome']\" HP=\"120\" optional=\"yes\" unset=\"armor, speed\"/>\n"
		"\t<add select=\"/units\"><unit name=\"new\"/></add>\n"
		"\t<remove select=\"//unit[@name='old']\"/>\n"
		"\t<replace select=\"//unit[@name='x']\"><unit name=\"x\"/></replace>\n"
		"\t<set select=\"//unit[2]\" hp=\"1\"/>\n"
		"\t<set select=\"//unit[\" hp=\"1\"/>\n"
		"</patch>\n");
	CHECK_EQ(v.size(), size_t(4));
	const PatchedValue *hp = Value(v, "//unit[@civ='rome'][@name='legionary']/@hp");
	CHECK(hp != nullptr);
	if (hp) {
		CHECK_EQ(hp->file, std::string("data/units.xml"));
		CHECK_EQ(hp->label, std::string("//Unit[@Name='Legionary'][@civ='Rome']/@HP"));
		CHECK_EQ(hp->value, std::string("120"));
		CHECK(!hp->removed);
	}
	const PatchedValue *armor = Value(v, "//unit[@civ='rome'][@name='legionary']/@armor");
	CHECK(armor != nullptr && armor->removed);
	CHECK(Value(v, "//unit[@civ='rome'][@name='legionary']/@speed") != nullptr);
	CHECK(Value(v, "//unit[2]/@hp") != nullptr);

	const std::vector<PatchedValue> a = PatchValues("data/units.xml", "<patch><set select=\"unit[@name='x']\" hp=\"1\"/></patch>");
	const std::vector<PatchedValue> b = PatchValues("data/units.xml", "<patch><set select=' /UNIT[ @name = \"X\" ] ' HP=\"2\"/></patch>");
	const std::vector<PatchedValue> deep = PatchValues("data/units.xml", "<patch><set select=\"//unit[@name='x']\" hp=\"1\"/></patch>");
	CHECK_EQ(a.size(), size_t(1));
	CHECK_EQ(b.size(), size_t(1));
	CHECK_EQ(deep.size(), size_t(1));
	if (a.size() == 1 && b.size() == 1 && deep.size() == 1) {
		CHECK_EQ(a[0].key, b[0].key);
		CHECK(a[0].key != deep[0].key);
	}

	CHECK(PatchValues("data/units.xml", "<patch><set select=\"//unit\" hp=\"1\"></patch>").empty());
	CHECK(PatchValues("data/units.xml", "").empty());
	CHECK(PatchValues("scripts/ai.vs", "@top\nint a;\n").empty());
}

static void IniValues()
{
	const std::vector<PatchedValue> v = PatchValues("Options/Game.ini",
		"Before = 1\n"
		"[Game]\n"
		"; comment\n"
		"Speed = 3 \n"
		"-Fog\n"
		"+Extra line\n"
		"NoEquals\n"
		"[OTHER]\n"
		"SPEED=4\n");
	CHECK_EQ(v.size(), size_t(3));
	const PatchedValue *speed = Value(v, "[game]speed");
	CHECK(speed != nullptr);
	if (speed) {
		CHECK_EQ(speed->file, std::string("options/game.ini"));
		CHECK_EQ(speed->label, std::string("[Game] Speed"));
		CHECK_EQ(speed->value, std::string("3"));
		CHECK(!speed->removed);
	}
	const PatchedValue *fog = Value(v, "[game]fog");
	CHECK(fog != nullptr && fog->removed);
	const PatchedValue *other = Value(v, "[other]speed");
	CHECK(other != nullptr && other->value == "4");
	const std::vector<PatchedValue> upper = PatchValues("options/game.ini", "[GAME]\nSPEED = 5\n");
	CHECK(upper.size() == 1 && upper[0].key == "[game]speed");
}

// --- the root of a rules package may add, not change (PatchPolicy::addOnly) ---------------------------------------------------------------

static bool AddOnlyXml(std::string &data, const std::string &patch, PatchPolicy &policy)
{
	std::vector<std::string> errors;
	XmlPatchRun run(data);
	policy.addOnly = true;
	const bool changed = run.Apply(patch, errors, nullptr, &policy);
	if (changed) data = run.Text();
	return changed;
}

static void AddOnlyXmlAppliesWhatAddsAndSkipsWhatChanges()
{
	// adds always apply: a node, an attribute the node lacks, a node before or after a game node
	std::string data = kCommands;
	PatchPolicy p;
	CHECK(AddOnlyXml(data, "<patch><add select=\"/commands\"><cmd name=\"mine\" costgold=\"5\"/></add>"
		"<set select=\"/commands/cmd[@name='other']\" description=\"new\"/>"
		"<add select=\"/commands/cmd[@name='other']\" position=\"after\"><cmd name=\"after\"/></add></patch>", p));
	CHECK_EQ(p.skipped, 0u);
	CHECK(Contains(data, "name=\"mine\"") && Contains(data, "description=\"new\"") && Contains(data, "name=\"after\""));
	// a changed attribute, a removed or replaced node and an unset attribute of the game's are skipped, one count per operation
	data = kCommands;
	PatchPolicy q;
	CHECK(!AddOnlyXml(data, "<patch><set select=\"/commands/cmd[@name='trainRHastatus']\" costgold=\"60\"/>"
		"<set select=\"/commands/cmd[@name='trainRHastatus']\" unset=\"costpop\"/>"
		"<remove select=\"/commands/cmd[@name='other']\"/>"
		"<replace select=\"/commands/cmd[@name='trainRArcher']\"><cmd name=\"x\"/></replace></patch>", q));
	CHECK_EQ(q.skipped, 4u);
	CHECK_EQ(data, std::string(kCommands));
	CHECK_EQ(q.skippedWhat.size(), size_t(4));
}

static void AddOnlyXmlPartialFileAndPartialSet()
{
	// a patch that mixes both: the adds apply, the change does not; a <set> with an added and a changed attribute applies the added one
	std::string data = kCommands;
	PatchPolicy p;
	CHECK(AddOnlyXml(data, "<patch><set select=\"/commands/cmd[@name='trainRHastatus']\" costgold=\"60\" rollover=\"Equip\"/>"
		"<add select=\"/commands\"><cmd name=\"mine\"/></add></patch>", p));
	CHECK_EQ(p.skipped, 1u);
	CHECK(Contains(data, "costgold=\"100\"") && Lacks(data, "costgold=\"60\"") && Contains(data, "rollover=\"Equip\"") && Contains(data, "name=\"mine\""));
	// the same value as the game's changes nothing and is not a change
	data = kCommands;
	PatchPolicy same;
	CHECK(!AddOnlyXml(data, "<patch><set select=\"/commands/cmd[@name='trainRHastatus']\" costgold=\"100\"/></patch>", same));
	CHECK_EQ(same.skipped, 0u);
}

static void AddOnlyXmlOperationsOnWhatThePatchAddedApply()
{
	std::string data = kCommands;
	PatchPolicy p;
	CHECK(AddOnlyXml(data, "<patch><add select=\"/commands\"><cmd name=\"mine\" costgold=\"5\"/></add>"
		"<set select=\"/commands/cmd[@name='mine']\" costgold=\"9\"/>"
		"<add select=\"/commands\"><cmd name=\"gone\"/></add>"
		"<remove select=\"/commands/cmd[@name='gone']\"/>"
		"<replace select=\"/commands/cmd[@name='mine']\"><cmd name=\"mine\" costgold=\"7\"/></replace></patch>", p));
	CHECK_EQ(p.skipped, 0u);
	CHECK(Contains(data, "costgold=\"7\"") && Lacks(data, "name=\"gone\""));
	// a selector over both the game's and its own nodes: only the game's are kept
	data = kCommands;
	PatchPolicy both;
	CHECK(AddOnlyXml(data, "<patch><add select=\"/commands\"><cmd name=\"mine\" costgold=\"5\"/></add>"
		"<set select=\"//cmd\" costgold=\"1\"/></patch>", both));
	CHECK_EQ(both.skipped, 1u);
	CHECK(Contains(data, "name=\"mine\" costgold=\"1\"") && Contains(data, "costgold=\"100\""));
	// without the policy, everything applies as before
	data = kCommands;
	std::vector<std::string> errors;
	CHECK(Xml(data, "<patch><set select=\"/commands/cmd[@name='trainRHastatus']\" costgold=\"60\"/></patch>", errors) && Contains(data, "costgold=\"60\""));
}

static const char *kGameIni = "[GamePlay]\r\nDamageAmplify = 200 ;comment\r\nTelMinDist = 20\r\n\r\n[Vars.All]\r\nAIV_ExcessGold = 10000\r\nBareLine\r\n";

static bool AddOnlyIni(std::string &data, const std::string &patch, PatchPolicy &policy)
{
	std::vector<std::string> errors;
	const std::map<std::string, std::string> keys = PatchIniKeys(data);
	policy.addOnly = true;
	policy.gameIni = &keys;
	return PatchIniText(data, patch, errors, nullptr, &policy);
}

static void AddOnlyIni()
{
	const std::map<std::string, std::string> keys = PatchIniKeys(kGameIni);
	CHECK_EQ(keys.size(), size_t(4));
	CHECK_EQ(keys.at("[gameplay]damageamplify"), "200");
	CHECK_EQ(keys.at("[vars.all]bareline"), "");
	// new keys, new sections and +Line apply; the same value as the game's changes nothing
	std::string data = kGameIni;
	PatchPolicy p;
	CHECK(AddOnlyIni(data, "[GamePlay]\nMyKey = 5\nDamageAmplify = 200\n[Mine]\nA = 1\n[Vars.All]\n+AnotherLine\n", p));
	CHECK_EQ(p.skipped, 0u);
	CHECK(Contains(data, "MyKey = 5") && Contains(data, "[Mine]") && Contains(data, "AnotherLine") && Contains(data, "DamageAmplify = 200 ;comment"));
	// a game key with another value, -Key of a game key (and the Key = v after it, counted once), -Line of a game line: skipped
	data = kGameIni;
	PatchPolicy q;
	CHECK(!AddOnlyIni(data, "[GamePlay]\nDamageAmplify = 250\n[Vars.All]\n-AIV_ExcessGold\nAIV_ExcessGold = 7000\n-BareLine\n", q));
	CHECK_EQ(q.skipped, 3u);
	CHECK_EQ(data, std::string(kGameIni));
	// a key the patch added is its own: it can be changed and removed
	data = kGameIni;
	PatchPolicy r;
	CHECK(AddOnlyIni(data, "[GamePlay]\nMyKey = 5\nMyKey = 6\n-MyKey\nOther = 1\n", r));
	CHECK_EQ(r.skipped, 0u);
	CHECK(Lacks(data, "MyKey") && Contains(data, "Other = 1"));
}

static const char *kAddOnlyScript = "int a;\nif (nRace == ImperialRome)\n\treturn 1;\nreturn 0;\n";

static void AddOnlyScript()
{
	std::string data = kAddOnlyScript;
	std::vector<std::string> errors;
	PatchPolicy p;
	p.addOnly = true;
	CHECK(PatchScriptText(data, "@before return 0;\n// mod\n@after int a;\nint b;\n@bottom\n// end\n", errors, &p));
	CHECK_EQ(p.skipped, 0u);
	data = kAddOnlyScript;
	PatchPolicy q;
	q.addOnly = true;
	CHECK(!PatchScriptText(data, "@replace if (nRace == ImperialRome)\nif (nRace == Greek)\n", errors, &q));
	CHECK_EQ(q.skipped, 1u);
	CHECK_EQ(data, std::string(kAddOnlyScript));
	// the same directives without the policy replace
	PatchPolicy none;
	CHECK(PatchScriptText(data, "@replace if (nRace == ImperialRome)\nif (nRace == Greek)\n", errors, &none));
	CHECK(Contains(data, "nRace == Greek"));
}

static void SkippedOpsOfAPatchForAGameFile()
{
	CHECK_EQ(PatchSkippedOps("data/commands/rbarracks.xml", kCommands, "<patch><add select=\"/commands\"><cmd name=\"n\"/></add></patch>").size(), size_t(0));
	CHECK_EQ(PatchSkippedOps("data/commands/rbarracks.xml", kCommands, "<patch><set select=\"//cmd[@name='other']\" costgold=\"2\"/><remove select=\"//cmd[@name='other']\"/></patch>").size(), size_t(2));
	CHECK_EQ(PatchSkippedOps("data/const.ini", kGameIni, "[GamePlay]\nTelMinDist = 30\nNew = 1\n").size(), size_t(1));
	CHECK_EQ(PatchSkippedOps("data/ai/x.vs", kAddOnlyScript, "@replace return 0;\nreturn 2;\n@top\nint c;\n").size(), size_t(1));
	// a file that is not a text the patcher knows, or a patch that cannot be read, skips nothing
	CHECK_EQ(PatchSkippedOps("data/x.bin", "x", "y").size(), size_t(0));
	CHECK_EQ(PatchSkippedOps("data/commands/rbarracks.xml", kCommands, "<patch><set").size(), size_t(0));
}

int main()
{
	XmlSet();
	XmlAdd();
	XmlRemoveReplace();
	XmlSelectors();
	XmlErrors();
	XmlFidelity();
	XmlStacking();
	IniBasics();
	IniRemoveAdd();
	IniErrorsAndFormat();
	ScriptDirectives();
	ScriptLegacyAndErrors();
	RemovedByAnotherPatch();
	XmlRemovedByAnotherPatch();
	XmlRunMatchesOneByOne();
	XmlResolve();
	XmlValues();
	IniValues();
	AddOnlyXmlAppliesWhatAddsAndSkipsWhatChanges();
	AddOnlyXmlPartialFileAndPartialSet();
	AddOnlyXmlOperationsOnWhatThePatchAddedApply();
	AddOnlyIni();
	AddOnlyScript();
	SkippedOpsOfAPatchForAGameFile();
	return test::Summary("patchxml");
}
