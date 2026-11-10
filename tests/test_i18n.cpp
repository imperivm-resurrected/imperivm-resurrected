// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/legacy.h"
#include "i18n/tr.h"

std::string g_language = "english";

static const std::string &TestLanguage()
{
	return g_language;
}

static std::string Unescape(const std::string &t)
{
	std::string out;
	for (size_t i = 0; i < t.size(); i++) {
		if (t[i] == '\\' && i + 1 < t.size()) {
			const char c = t[++i];
			out += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
		}
		else {
			out += t[i];
		}
	}
	return out;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: test-i18n <translations.tsv>\n");
		return 2;
	}
	TrSetLanguageSource(&TestLanguage);
	std::ifstream in(argv[1], std::ios::binary);
	std::string line;
	int rows = 0;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t a = line.find('\t');
		const size_t b = line.find('\t', a + 1);
		if (a == std::string::npos || b == std::string::npos) continue;
		const std::string en = Unescape(line.substr(0, a)), es = Unescape(line.substr(a + 1, b - a - 1)), it = Unescape(line.substr(b + 1));
		rows++;
		g_language = "english";
		Check(Tr(en) == en, "english " + en);
		g_language = "spanish";
		Check(Tr(en) == (es == "=" ? en : es), "spanish " + en);
		g_language = "italian";
		Check(Tr(en) == (it == "=" ? en : it), "italian " + en);
		g_language = "french";
		Check(Tr(en) == en, "other language " + en);
	}
	g_language = "spanish";
	Check(Tr("Waiting for Ana (scenario)...") == "Esperando a Ana (escenario)...", "pattern es");
	Check(TrFmt("%s: not valid JSON (%s)", {"a.json", "boom"}) == "a.json: no es un JSON válido (boom)", "TrFmt es");
	Check(TrFmt("Could not download %s: %s", {"Greek Mod", Tr("GameBanana sent 0 B instead of the 908.4 MB file. If it was uploaded a moment ago, try "
															 "again in a few minutes.")}) ==
			"No se pudo descargar Greek Mod: GameBanana ha enviado 0 B en lugar del archivo de 908.4 MB. Si se acaba de subir, prueba de nuevo dentro "
			"de unos minutos.",
		"download reason es");
	Check(Tr("the server responded 503") == "el servidor respondió 503" && Tr("network error 12002") == "error de red 12002", "reason patterns es");
	g_language = "italian";
	Check(TrFmt("%s: not valid JSON (%s)", {"a.json", "boom"}) == "a.json: JSON non valido (boom)", "TrFmt it");
	g_language = "english";
	Check(TrFmt("line %s: %s", {"3", "oops"}) == "line 3: oops", "TrFmt en");
	Check(TrSource("Cancelar") == "Cancel" && TrSource("Annulla") == "Cancel", "TrSource exact");
	Check(TrSource("Cancel") == "Cancel" && TrSource("nothing like it, really") == "nothing like it, really" && TrSource("").empty(), "TrSource unchanged");
	Check(TrSource("a.json: no es un JSON válido (boom)") == "a.json: not valid JSON (boom)", "TrSource es pattern");
	Check(TrSource("a.json: JSON non valido (boom)") == "a.json: not valid JSON (boom)", "TrSource it pattern");
	Check(TrSource("Esperando a Ana (escenario)...") == "Waiting for Ana (scenario)...", "TrSource es waiting");
	Check(TrSource("No se pudo descargar Caesar Mod: conexión interrumpida") == "Could not download Caesar Mod: connection interrupted",
		"TrSource es nested reason");
	Check(TrSource("Impossibile scaricare Caesar Mod: il server ha risposto 404") == "Could not download Caesar Mod: the server responded 404",
		"TrSource it nested pattern");
	const std::vector<std::string> all = TrAllLanguages("Autosave %02dh%02d");
	Check(all.size() == 3 && all[1] == "Autoguardado %02dh%02d", "TrAllLanguages");
	Check(std::string(LegacyJsonKey("name")) == "nombre" && LegacyJsonKey("zzz") == nullptr, "LegacyJsonKey");
	Check(LegacyDirective("antes") == "before" && LegacyDirective("top") == "top", "LegacyDirective");
	Check(LegacyKeyVk("Izquierda") == VK_LEFT && LegacyKeyVk("Left") == 0, "LegacyKeyVk");
	printf("%d translation rows\n", rows);
	return test::Summary("i18n");
}
