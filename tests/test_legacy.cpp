// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "check.h"

#include <cstdio>
#include <string>

#include "core/legacy.h"

static std::string Dir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

static void Touch(const std::string &path, const std::string &text = "x")
{
	std::string dir = path.substr(0, path.find_last_of('\\'));
	for (size_t i = 3; i < dir.size(); i++) {
		if (dir[i] == '\\') CreateDirectoryA(dir.substr(0, i).c_str(), nullptr);
	}
	CreateDirectoryA(dir.c_str(), nullptr);
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "wb") == 0 && f) {
		fwrite(text.data(), 1, text.size(), f);
		fclose(f);
	}
}

static bool Exists(const std::string &path)
{
	return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::string Ini(const std::string &path, const char *section, const char *key)
{
	char buf[512];
	GetPrivateProfileStringA(section, key, "<none>", buf, sizeof(buf), path.c_str());
	return buf;
}

int main()
{
	const std::string d = Dir();
	for (const char *f : {"imperivm-mod.ini", "controles.ini", "controls.ini", "gbr.exe.sin4gb", "gbr.exe.no4gb", "winmm.dll.anterior", "winmm.dll.previous"}) {
		DeleteFileA((d + "\\" + f).c_str());
	}
	for (const char *sub : {"mods", "Profiles", "mod", "imperivm-resurrected"}) test::RemoveTree(d + "\\" + sub);
	Touch(d + "\\mod\\civs\\own.json", "mine");
	Touch(d + "\\mod\\keep.txt", "old");
	Touch(d + "\\mod\\discord.png", "old");
	Touch(d + "\\imperivm-resurrected\\discord.png", "new");
	Touch(d + "\\imperivm-resurrected\\civs\\shipped.json", "shipped");
	const std::string ini = d + "\\imperivm-mod.ini";
	Touch(ini, "[Render]\r\nEscalado=suave\r\nBrillo=120\r\nEnabled=1\r\n[Pantalla]\r\nModo=ventana\r\nVentanaAncho=1280\r\n[Juego]\r\nAutoguardadoMinutos=15\r\n[Mods]\r\nPerfil=Sin perfil\r\n[Actualizaciones]\r\nModo=no\r\n[Online]\r\nPartidaEnCurso=abc\r\n[Game]\r\nHealthBars=2\r\n");
	Touch(d + "\\controles.ini", "[General]\r\nOrdenes=cuadricula\r\nLetrasEnLaBarra=0\r\n[Teclas]\r\nguardado_rapido=Ctrl+S\r\nmenu=F10\r\n[Ordenes]\r\nM=K\r\n");
	Touch(d + "\\mods\\_perfiles\\Sin perfil.json", "{}");
	Touch(d + "\\mods\\_descargas\\respaldo\\a.txt");
	Touch(d + "\\mods\\_recibidos.txt");
	Touch(d + "\\Profiles\\Foo\\partidas.json", "{}");
	Touch(d + "\\Profiles\\Foo\\save-1.camaras");
	Touch(d + "\\gbr.exe.sin4gb");
	Touch(d + "\\winmm.dll.anterior");
	LegacyMigrate();
	LegacyMigrate();
	Check(Ini(ini, "Render", "Scaling") == "suave", "scaling key moved");
	Check(Ini(ini, "Render", "Brightness") == "120", "brightness");
	Check(Ini(ini, "Render", "Escalado") == "<none>", "old key removed");
	Check(Ini(ini, "Display", "Mode") == "windowed", "mode value");
	Check(Ini(ini, "Display", "WindowWidth") == "1280", "window width");
	Check(Ini(ini, "Game", "AutosaveMinutes") == "15", "autosave minutes");
	Check(Ini(ini, "Mods", "Profile") == "No profile", "profile value");
	Check(Ini(ini, "Updates", "Mode") == "off", "updates mode");
	Check(Ini(ini, "Online", "GameInProgress") == "abc", "online key");
	Check(Ini(ini, "Pantalla", "Modo") == "<none>", "old section removed");
	Check(Ini(ini, "Game", "HealthBarOwners") == "1" && Ini(ini, "Game", "HealthBarHeroesOnly") == "1", "health bars of heroes: every owner, heroes only");
	Check(Ini(ini, "Game", "HealthBars") == "<none>", "old health bars key removed");
	Check(Exists(d + "\\controls.ini") && !Exists(d + "\\controles.ini"), "controls file renamed");
	Check(Ini(d + "\\controls.ini", "Keys", "quick_save") == "Ctrl+S", "control id");
	Check(Ini(d + "\\controls.ini", "Keys", "menu") == "F10", "control id kept");
	Check(Ini(d + "\\controls.ini", "Commands", "M") == "K", "commands section");
	Check(Ini(d + "\\controls.ini", "General", "Layout") == "grid", "layout");
	Check(Ini(d + "\\controls.ini", "General", "LettersOnBar") == "0", "letters");
	Check(Exists(d + "\\mods\\_profiles\\No profile.json"), "profile file");
	Check(Exists(d + "\\mods\\_downloads\\backup\\a.txt"), "downloads backup");
	Check(Exists(d + "\\mods\\_received.txt"), "received");
	Check(Exists(d + "\\Profiles\\Foo\\games.json") && Exists(d + "\\Profiles\\Foo\\save-1.cameras"), "profile files");
	Check(Exists(d + "\\gbr.exe.no4gb") && Exists(d + "\\winmm.dll.previous"), "renamed exe files");
	Check(Exists(d + "\\imperivm-resurrected\\civs\\own.json") && Exists(d + "\\imperivm-resurrected\\keep.txt"), "files of the old mod folder merged into the new one");
	Check(Exists(d + "\\imperivm-resurrected\\civs\\shipped.json"), "files already in the new folder kept");
	Check(!Exists(d + "\\mod\\civs") && !Exists(d + "\\mod\\keep.txt"), "merged files left the old folder");
	Check(Exists(d + "\\mod\\discord.png"), "a file that already exists in the new folder is not overwritten");
	Touch(ini, "[Game]\r\nHealthBars=3\r\n");
	LegacyMigrate();
	Check(Ini(ini, "Game", "HealthBarOwners") == "2" && Ini(ini, "Game", "HealthBarHeroesOnly") == "0", "health bars of your units");
	Touch(ini, "[Game]\r\nHealthBars=1\r\nHealthBarOwners=3\r\n");
	LegacyMigrate();
	Check(Ini(ini, "Game", "HealthBarOwners") == "3" && Ini(ini, "Game", "HealthBars") == "<none>", "the new key wins over the old one");
	return test::Summary("legacy");
}
