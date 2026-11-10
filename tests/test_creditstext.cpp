// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "core/creditstext.h"

namespace {

const char kBar[] = "==================================================";

std::string Lines(const std::vector<std::string> &lines, const char *eol = "\r\n")
{
	std::string out;
	for (const std::string &l : lines) out += l + eol;
	return out;
}

std::vector<std::string> Names(const char *prefix, int count)
{
	std::vector<std::string> out;
	for (int i = 0; i < count; i++) out.push_back(std::string(prefix) + " " + std::to_string(i + 1));
	return out;
}

std::string Kinds(const CreditsDoc &d)
{
	std::string out;
	for (const CreditsItem &it : d.items) {
		const char *k = "?";
		switch (it.kind) {
		case kCreditsHeading: k = "H"; break;
		case kCreditsSubtitle: k = "S"; break;
		case kCreditsRole: k = "R"; break;
		case kCreditsNote: k = "N"; break;
		case kCreditsNames: k = "L"; break;
		case kCreditsText: k = "T"; break;
		case kCreditsRule: k = "="; break;
		}
		out += k;
	}
	return out;
}

std::vector<std::string> Labels(const CreditsDoc &d)
{
	std::vector<std::string> out;
	for (const CreditsEntry &e : d.index) out.push_back(e.label);
	return out;
}

// The shape of the game's Spanish file, with made-up names.
std::string SpanishSample(int backers)
{
	std::vector<std::string> l = {"\xEF\xBB\xBFImperivm HD / Release 0.1", "2 de julio de 2021", "", "", kBar, "", "", "ImperivmWorld", "Jordi Ejemplo", "", "",
		kBar, "", "", "Haemimont Games", "Ana, Luis & Pedro", "", "", kBar, "", "", "Ilustraci\xC3\xB3n", "Antonio \xE2\x80\x9CKoveck\xE2\x80\x9D", "", "",
		"M\xC3\xBAsica", "Compositor Uno", "", "", "Actores de doblaje"};
	for (const std::string &n : Names("Actor", 11)) l.push_back(n);
	for (const char *s : {"", "", "Productores Asociados ", "(Por orden alfab\xC3\xA9tico de nombre y/o Nick)", "", ""}) l.push_back(s);
	for (const std::string &n : Names("Productor", 19)) l.push_back(n);
	for (const char *s : {"", "", "Gracias de coraz\xC3\xB3n a todos los jugadores que han patrocinado ", "el retorno de la saga en Kickstarter- Marzo / Abril 2021",
			 "(Por orden alfab\xC3\xA9tico de nombre y/o Nick)", "", ""}) {
		l.push_back(s);
	}
	for (const std::string &n : Names("Mecenas", backers)) l.push_back(n);
	for (int i = 0; i < 6; i++) l.push_back("");
	return Lines(l);
}

void SpanishShape()
{
	const CreditsDoc d = ParseCredits(SpanishSample(40));
	CHECK_EQ(Kinds(d), std::string("HS=HL=HL=RLRLRLHNLHNL"));
	CHECK_EQ(d.items[0].lines[0], std::string("Imperivm HD / Release 0.1"));
	CHECK_EQ(d.items[1].lines[0], std::string("2 de julio de 2021"));
	CHECK_EQ(d.items[4].lines[0], std::string("Jordi Ejemplo"));
	CHECK_EQ(d.items[12].lines.size(), static_cast<size_t>(1));
	CHECK_EQ(d.items[14].lines.size(), static_cast<size_t>(11));
	CHECK_EQ(d.items[15].lines[0], std::string("Productores Asociados"));
	CHECK_EQ(d.items[18].lines[0], std::string("Gracias de coraz\xC3\xB3n a todos los jugadores que han patrocinado el retorno de la saga en Kickstarter- Marzo / Abril 2021"));
	CHECK_EQ(d.items[19].kind == kCreditsNote, true);
	CHECK_EQ(d.items[20].lines.size(), static_cast<size_t>(40));
	CHECK_EQ(Labels(d), (std::vector<std::string>{"Imperivm HD / Release 0.1", "ImperivmWorld", "Haemimont Games",
		"Ilustraci\xC3\xB3n \xC2\xB7 M\xC3\xBAsica \xC2\xB7 Actores de doblaje", "Productores Asociados",
		"Gracias de coraz\xC3\xB3n a todos los jugadores que han patrocinado el retorno de la saga en Kickstarter- Marzo / Abril 2021"}));
	CHECK_EQ(static_cast<int>(d.index[3].item), 9);
	CHECK_EQ(static_cast<int>(d.index[4].item), 15);
}

// The English file has no voice actors and ends with a copyright line after the list; LF only and no BOM must read the same.
void EnglishShape()
{
	std::vector<std::string> l = {"Imperivm HD / Release 0.1", "July 2, 2021 ", "", kBar, "", "Haemimont Games", "Gabriel, Bisser & Assen", "", kBar, "",
		"Cover Artist", "Someone", "", "", "Music", "Somebody", "", "", "Associated Producers ", "(In alphabetical order of name and / or Nick) ", "", ""};
	for (const std::string &n : Names("Producer", 13)) l.push_back(n);
	for (int i = 0; i < 9; i++) l.push_back("");
	l.push_back("Copyright (c) 2000-2004 Haemimont Games, Sofia, Bulgaria.");
	const CreditsDoc d = ParseCredits(Lines(l, "\n"));
	CHECK_EQ(Kinds(d), std::string("HS=HL=RLRLHNLT"));
	CHECK_EQ(d.items[1].lines[0], std::string("July 2, 2021"));
	CHECK_EQ(d.items.back().lines[0], std::string("Copyright (c) 2000-2004 Haemimont Games, Sofia, Bulgaria."));
	CHECK_EQ(Labels(d), (std::vector<std::string>{"Imperivm HD / Release 0.1", "Haemimont Games", "Cover Artist \xC2\xB7 Music", "Associated Producers"}));
}

// An Italian file written the same way.
void ItalianShape()
{
	std::vector<std::string> l = {"\xEF\xBB\xBFImperivm HD / Release 0.1", "2 luglio 2021", "", "", kBar, "", "", "Illustrazione", "Qualcuno", "", "", "Musica",
		"Qualcun altro", "", "", "Doppiatori"};
	for (const std::string &n : Names("Voce", 4)) l.push_back(n);
	for (const char *s : {"", "", "Grazie di cuore a tutti i giocatori che hanno sostenuto", "il ritorno della saga su Kickstarter", "(In ordine alfabetico)", "", ""}) {
		l.push_back(s);
	}
	for (const std::string &n : Names("Sostenitore", 30)) l.push_back(n);
	const CreditsDoc d = ParseCredits(Lines(l));
	CHECK_EQ(Kinds(d), std::string("HS=RLRLRLHNL"));
	CHECK_EQ(Labels(d), (std::vector<std::string>{"Imperivm HD / Release 0.1", "Illustrazione \xC2\xB7 Musica \xC2\xB7 Doppiatori",
		"Grazie di cuore a tutti i giocatori che hanno sostenuto il ritorno della saga su Kickstarter"}));
}

// Imperivm I: no bars, roles in capitals with a blank line before their names.
void CapitalRolesWithoutBars()
{
	const CreditsDoc d = ParseCredits(Lines({"HAEMIMONT ", "GAMES", "", "", "", "DISE\xC3\x91O DEL JUEGO", "", "Gabriel", "", "", "PROGRAMADORES", "", "Uno", "Dos",
		"Tres", "", "MANUAL", "", "Cuatro", "", "", "Copyright (c) 2002 Haemimont Games, Sofia, Bulgaria.", "", "", "Translated edition, Copyright (c) 2002 Someone."}));
	CHECK_EQ(Kinds(d), std::string("HRLRLRLTT"));
	CHECK_EQ(d.items[0].lines[0], std::string("HAEMIMONT GAMES"));
	CHECK_EQ(d.items[1].lines[0], std::string("DISE\xC3\x91O DEL JUEGO"));
	CHECK_EQ(d.items[4].lines.size(), static_cast<size_t>(3));
	CHECK_EQ(Labels(d), (std::vector<std::string>{"HAEMIMONT GAMES"}));
}

void MoreThanThreeRolesShortenTheLabel()
{
	const CreditsDoc d = ParseCredits(Lines({"A", "x", "", "B", "y", "", "C", "z", "", "D", "w"}));
	CHECK_EQ(Kinds(d), std::string("RLRLRLRL"));
	CHECK_EQ(Labels(d), (std::vector<std::string>{"A \xC2\xB7 B \xC2\xB7 C \xC2\xB7 \xE2\x80\xA6"}));
}

// Paragraphs and lone lines stay as centred text, and the index still has a line to start from.
void UnknownTextStaysPlain()
{
	const std::string para(120, 'x');
	const CreditsDoc d = ParseCredits(Lines({para, "", "(a note alone)", "", para + " y"}));
	CHECK_EQ(Kinds(d), std::string("TTT"));
	CHECK_EQ(d.index.size(), static_cast<size_t>(1));
	CHECK_EQ(static_cast<int>(d.index[0].item), 0);
}

void EmptyAndBarsOnly()
{
	CHECK_EQ(ParseCredits("").items.size(), static_cast<size_t>(0));
	CHECK_EQ(ParseCredits("\xEF\xBB\xBF\r\n\r\n").items.size(), static_cast<size_t>(0));
	const CreditsDoc d = ParseCredits(Lines({kBar, "", kBar, "Solo", "", kBar, kBar}));
	CHECK_EQ(Kinds(d), std::string("H"));
	CHECK_EQ(d.index.size(), static_cast<size_t>(1));
}

// A dash inside a name is not a bar, and a note among the names stays where it is.
void NamesKeepTheirDashesAndNotes()
{
	const CreditsDoc d = ParseCredits(Lines({"Section", "-gizmo-", "(and friends)", "Another"}));
	CHECK_EQ(Kinds(d), std::string("HLNL"));
	CHECK_EQ(d.items[1].lines[0], std::string("-gizmo-"));
}

void LongListsAreHeadings()
{
	std::vector<std::string> l = {"Intro", "x", "", "Backers"};
	for (const std::string &n : Names("Backer", static_cast<int>(kCreditsColumnsFrom) + 1)) l.push_back(n);
	const CreditsDoc d = ParseCredits(Lines(l));
	CHECK_EQ(Kinds(d), std::string("RLHL"));
	CHECK_EQ(Labels(d), (std::vector<std::string>{"Intro", "Backers"}));
}

}

int main()
{
	SpanishShape();
	EnglishShape();
	ItalianShape();
	CapitalRolesWithoutBars();
	MoreThanThreeRolesShortenTheLabel();
	UnknownTextStaysPlain();
	EmptyAndBarsOnly();
	NamesKeepTheirDashesAndNotes();
	LongListsAreHeadings();
	return test::Summary("creditstext");
}
