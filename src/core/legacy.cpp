// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/legacy.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

bool Exists(const std::string &path)
{
	return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void RenameIfNeeded(const std::string &from, const std::string &to)
{
	if (Exists(from) && !Exists(to)) MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH);
}

void MergeFolder(const std::string &from, const std::string &to)
{
	if (!Exists(from)) return;
	if (!Exists(to)) {
		MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH);
		return;
	}
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((from + "\\*").c_str(), &fd);
	if (f != INVALID_HANDLE_VALUE) {
		do {
			const std::string n = fd.cFileName;
			if (n == "." || n == "..") continue;
			const std::string a = from + "\\" + n, b = to + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) MergeFolder(a, b);
			else if (!Exists(b)) MoveFileExA(a.c_str(), b.c_str(), MOVEFILE_WRITE_THROUGH);
		} while (FindNextFileA(f, &fd));
		FindClose(f);
	}
	RemoveDirectoryA(from.c_str());
}

void RemoveIfContains(const std::string &path, const char *marker)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;
	std::string text(64 * 1024, '\0');
	DWORD got = 0;
	const BOOL ok = ReadFile(h, &text[0], static_cast<DWORD>(text.size()), &got, nullptr);
	CloseHandle(h);
	text.resize(ok ? got : 0);
	if (text.find(marker) != std::string::npos) DeleteFileA(path.c_str());
}

struct IniRename {
	const char *section, *key, *newSection, *newKey;
};

const IniRename kIniRenames[] = {
	{"Render", "Escalado", "Render", "Scaling"},
	{"Render", "Brillo", "Render", "Brightness"},
	{"Sonido", "VolumenCinematicas", "Sound", "MovieVolume"},
	{"Sonido", "SilenciarCinematicas", "Sound", "MovieMute"},
	{"Sonido", "SilenciarEnSegundoPlano", "Sound", "MuteInBackground"},
	{"Sonido", "MotorModerno", "Sound", "ModernAudio"},
	{"Sonido", "ArreglarVoces", "Sound", "FixVoices"},
	{"Sonido", "AvisosOnline", "Sound", "OnlineAlerts"},
	{"Pantalla", "Modo", "Display", "Mode"},
	{"Pantalla", "VentanaAncho", "Display", "WindowWidth"},
	{"Pantalla", "VentanaAlto", "Display", "WindowHeight"},
	{"Pantalla", "AtraparRaton", "Display", "TrapMouse"},
	{"Pantalla", "VistaAmpliada", "Display", "ExtendedView"},
	{"Pantalla", "MostrarRendimiento", "Display", "ShowPerformance"},
	{"Pantalla", "MostrarRed", "Display", "ShowNetwork"},
	{"Pantalla", "PanelX", "Display", "PanelX"},
	{"Pantalla", "PanelY", "Display", "PanelY"},
	{"Pantalla", "PanelCompacto", "Display", "PanelCompact"},
	{"Pantalla", "PanelOpacidad", "Display", "PanelOpacity"},
	{"Pantalla", "ConscienteDePPP", "Display", "DpiAware"},
	{"Juego", "VelocidadScroll", "Game", "ScrollSpeed"},
	{"Juego", "Autoguardado", "Game", "Autosave"},
	{"Juego", "AutoguardadoMinutos", "Game", "AutosaveMinutes"},
	{"Juego", "AutoguardarAlSalir", "Game", "AutosaveOnExit"},
	{"Juego", "AutoguardadoRanuras", "Game", "AutosaveSlots"},
	{"Juego", "ProtegerCierres", "Game", "CrashFix"},
	{"Juego", "Usar4GB", "Game", "Use4GB"},
	{"Juego", "ScrollSuave", "Game", "SmoothScroll"},
	{"Mods", "Sustituir", "Mods", "Override"},
	{"Mods", "Perfil", "Mods", "Profile"},
	{"Mods", "DesinstalarAlArrancar", "Mods", "UninstallOnStart"},
	{"Mods", "Catalogos", "Mods", "Catalogs"},
	{"Mods", "AvisarActualizaciones", "Mods", "UpdateNotices"},
	{"ModIO", "JuegoId", "ModIO", "GameId"},
	{"GameBanana", "JuegoId", "GameBanana", "GameId"},
	{"Depuracion", "RegistrarArchivos", "Debug", "LogFiles"},
	{"Depuracion", "RegistrarTextosSinTraducir", "Debug", "LogUntranslatedTexts"},
	{"Depuracion", "Volcar", "Debug", "Dump"},
	{"Depuracion", "VolcarAlArrancar", "Debug", "DumpAtStartup"},
	{"Discord", "Presencia", "Discord", "Presence"},
	{"Idioma", "Siguiente", "Language", "Next"},
	{"Mapa", "NieblaVisible", "Map", "FogVisible"},
	{"Mapa", "ActualizarMs", "Map", "RefreshMs"},
	{"Trucos", "PanelY", "Cheats", "PanelY"},
	{"Espectador", "Autoprueba", "Spectator", "SelfTest"},
	{"Espectador", "BarraX", "Spectator", "BarX"},
	{"Espectador", "BarraY", "Spectator", "BarY"},
	{"Online", "PartidaEnCurso", "Online", "GameInProgress"},
	{"Online", "VolverASala", "Online", "ReturnToRoom"},
	{"Sala", "Sitios", "Room", "Sites"},
	{"Sala", "Reglas", "Room", "Rules"},
	{"Actualizaciones", "Modo", "Updates", "Mode"},
	{"Actualizaciones", "Probando", "Updates", "Testing"},
	{"Actualizaciones", "Arranques", "Updates", "Starts"},
	{"Actualizaciones", "Anterior", "Updates", "Previous"},
	{"Actualizaciones", "Quitada", "Updates", "Removed"},
};

struct IniValue {
	const char *section, *key, *from, *to;
};

const IniValue kIniValues[] = {
	{"Display", "Mode", "completa", "fullscreen"},
	{"Display", "Mode", "ventana", "windowed"},
	{"Updates", "Mode", "avisar", "notify"},
	{"Updates", "Mode", "no", "off"},
	{"Mods", "Profile", "Sin perfil", "No profile"},
};

const char kMissing[] = "\x01";

void MigrateIni(const std::string &path)
{
	if (!Exists(path)) return;
	char value[4096];
	for (const IniRename &r : kIniRenames) {
		GetPrivateProfileStringA(r.section, r.key, kMissing, value, sizeof(value), path.c_str());
		if (value[0] == kMissing[0] && !value[1]) continue;
		char probe[8];
		GetPrivateProfileStringA(r.newSection, r.newKey, kMissing, probe, sizeof(probe), path.c_str());
		const bool newExists = !(probe[0] == kMissing[0] && !probe[1]);
		if (!newExists) {
			for (const IniValue &v : kIniValues) {
				if (strcmp(v.section, r.newSection) == 0 && strcmp(v.key, r.newKey) == 0 && _stricmp(v.from, value) == 0) {
					strcpy_s(value, v.to);
					break;
				}
			}
			WritePrivateProfileStringA(r.newSection, r.newKey, value, path.c_str());
		}
		if (strcmp(r.section, r.newSection) != 0 || strcmp(r.key, r.newKey) != 0) WritePrivateProfileStringA(r.section, r.key, nullptr, path.c_str());
	}
	static const char *const kOldSections[] = {"Pantalla", "Sonido", "Juego", "Depuracion", "Idioma", "Mapa", "Trucos", "Espectador", "Sala", "Actualizaciones"};
	for (const char *section : kOldSections) {
		char keys[8];
		if (GetPrivateProfileStringA(section, nullptr, "", keys, sizeof(keys), path.c_str()) == 0) WritePrivateProfileStringA(section, nullptr, nullptr, path.c_str());
	}
}

void MigrateProfilesFolder(const std::string &profiles)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((profiles + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
		const std::string dir = profiles + "\\" + fd.cFileName;
		RenameIfNeeded(dir + "\\partidas.json", dir + "\\games.json");
		for (const std::string &folder : {dir, dir + "\\SaveOnline"}) {
			WIN32_FIND_DATAA f2;
			HANDLE h2 = FindFirstFileA((folder + "\\*.camaras").c_str(), &f2);
			if (h2 == INVALID_HANDLE_VALUE) continue;
			do {
				std::string name = f2.cFileName;
				name.replace(name.size() - 8, 8, ".cameras");
				RenameIfNeeded(folder + "\\" + f2.cFileName, folder + "\\" + name);
			} while (FindNextFileA(h2, &f2));
			FindClose(h2);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

struct ControlId {
	const char *legacy, *current;
};

const ControlId kControlIds[] = {
	{"camara_izquierda", "camera_left"}, {"camara_arriba", "camera_up"}, {"camara_derecha", "camera_right"}, {"camara_abajo", "camera_down"},
	{"centrar", "center"}, {"ultimo_aviso", "last_alert"}, {"cancelar", "cancel"}, {"heroe_siguiente", "next_hero"}, {"grupo_heroe", "hero_group"},
	{"mas_sanos", "keep_healthiest"}, {"mas_heridos", "keep_wounded"}, {"mas_nivel", "keep_highest_level"}, {"menos_nivel", "keep_lowest_level"},
	{"grupo_1", "group_1"}, {"grupo_2", "group_2"}, {"grupo_3", "group_3"}, {"grupo_4", "group_4"}, {"grupo_5", "group_5"},
	{"grupo_6", "group_6"}, {"grupo_7", "group_7"}, {"grupo_8", "group_8"}, {"grupo_9", "group_9"}, {"grupo_0", "group_0"},
	{"ayuda", "help"}, {"guardar", "save"}, {"cargar", "load"}, {"guardado_rapido", "quick_save"}, {"carga_rapida", "quick_load"},
	{"diplomacia", "diplomacy"}, {"notas", "notes"}, {"captura", "screenshot"}, {"marcador", "scoreboard"}, {"marcador_siguiente", "next_scoreboard"},
	{"marcador_reducido", "compact_scoreboard"}, {"barras_vida", "health_bars"}, {"transparentar", "see_through"}, {"panel_info", "info_panel"},
	{"minimapa", "minimap"}, {"panel_rendimiento", "performance_panel"}, {"espectador_pausa", "replay_pause"}, {"espectador_mas_rapido", "replay_faster"},
	{"espectador_mas_lento", "replay_slower"}, {"espectador_atras", "replay_back"}, {"espectador_adelante", "replay_forward"},
	{"espectador_directo", "replay_live"}, {"espectador_interfaz", "replay_hide_ui"}, {"espectador_barra", "replay_collapse_bar"},
	{"trucos_panel", "cheats_panel"},
};

std::vector<std::pair<std::string, std::string>> ReadSection(const char *section, const std::string &path)
{
	std::vector<char> buf(65536);
	std::vector<std::pair<std::string, std::string>> out;
	const DWORD n = GetPrivateProfileSectionA(section, buf.data(), static_cast<DWORD>(buf.size()), path.c_str());
	for (const char *p = buf.data(); n && *p; p += strlen(p) + 1) {
		const char *eq = strchr(p, '=');
		if (!eq || *p == ';') continue;
		out.emplace_back(std::string(p, eq - p), std::string(eq + 1));
	}
	return out;
}

void MigrateControls(const std::string &path)
{
	if (!Exists(path)) return;
	const auto keys = ReadSection("Teclas", path);
	for (const auto &kv : keys) {
		std::string id = kv.first;
		for (const ControlId &c : kControlIds) {
			if (_stricmp(c.legacy, id.c_str()) == 0) {
				id = c.current;
				break;
			}
		}
		WritePrivateProfileStringA("Keys", id.c_str(), kv.second.c_str(), path.c_str());
	}
	WritePrivateProfileStringA("Teclas", nullptr, nullptr, path.c_str());
	for (const auto &kv : ReadSection("Ordenes", path)) WritePrivateProfileStringA("Commands", kv.first.c_str(), kv.second.c_str(), path.c_str());
	WritePrivateProfileStringA("Ordenes", nullptr, nullptr, path.c_str());
	char value[64];
	GetPrivateProfileStringA("General", "Ordenes", kMissing, value, sizeof(value), path.c_str());
	if (!(value[0] == kMissing[0] && !value[1])) {
		WritePrivateProfileStringA("General", "Layout", _stricmp(value, "cuadricula") == 0 ? "grid" : value, path.c_str());
		WritePrivateProfileStringA("General", "Ordenes", nullptr, path.c_str());
	}
	GetPrivateProfileStringA("General", "LetrasEnLaBarra", kMissing, value, sizeof(value), path.c_str());
	if (!(value[0] == kMissing[0] && !value[1])) {
		WritePrivateProfileStringA("General", "LettersOnBar", value, path.c_str());
		WritePrivateProfileStringA("General", "LetrasEnLaBarra", nullptr, path.c_str());
	}
}

}

void LegacyMigrateControls(const std::string &path)
{
	MigrateControls(path);
}

bool LegacyLooksLikeControls(const std::string &text)
{
	return text.find("[Teclas]") != std::string::npos || text.find("[Ordenes]") != std::string::npos;
}

void LegacyMigrate()
{
	const std::string dir = GameDir();
	MigrateIni(dir + "\\imperivm-mod.ini");
	RenameIfNeeded(dir + "\\controles.ini", dir + "\\controls.ini");
	MigrateControls(dir + "\\controls.ini");
	RenameIfNeeded(dir + "\\gbr.exe.sin4gb", dir + "\\gbr.exe.no4gb");
	RenameIfNeeded(dir + "\\winmm.dll.anterior", dir + "\\winmm.dll.previous");
	RenameIfNeeded(dir + "\\winmm.dll.fallida", dir + "\\winmm.dll.failed");
	RenameIfNeeded(dir + "\\winmm.dll.nuevo", dir + "\\winmm.dll.new");
	const std::string mods = dir + "\\mods";
	RenameIfNeeded(mods + "\\_perfiles", mods + "\\_profiles");
	RenameIfNeeded(mods + "\\_profiles\\Sin perfil.json", mods + "\\_profiles\\No profile.json");
	RenameIfNeeded(mods + "\\_descargas", mods + "\\_downloads");
	RenameIfNeeded(mods + "\\_downloads\\respaldo", mods + "\\_downloads\\backup");
	RenameIfNeeded(mods + "\\_downloads\\pendiente", mods + "\\_downloads\\pending");
	RenameIfNeeded(mods + "\\_downloads\\intercambios.txt", mods + "\\_downloads\\swaps.txt");
	RenameIfNeeded(mods + "\\_publicar", mods + "\\_publish");
	RenameIfNeeded(mods + "\\_recibidos.txt", mods + "\\_received.txt");
	MigrateProfilesFolder(dir + "\\Profiles");
	MergeFolder(dir + "\\mod", dir + "\\imperivm-resurrected");
	RemoveIfContains(dir + "\\imperivm-resurrected\\menuini\\mainmenu.ini", "Additional content");
	RemoveIfContains(dir + "\\imperivm-resurrected\\menuini\\gameoptions.ini", "ModWindowCheck");
	RemoveDirectoryA((dir + "\\imperivm-resurrected\\menuini").c_str());
}

const char *LegacyJsonKey(const std::string &key)
{
	static const std::unordered_map<std::string, const char *> kLegacy = {
		{"name", "nombre"},
		{"authors", "autores"},
		{"description", "descripcion"},
		{"redistributable", "redistribuible"},
		{"license", "licencia"},
		{"loadAfter", "cargarDespuesDe"},
		{"loadBefore", "cargarAntesDe"},
		{"tags", "etiquetas"},
		{"category", "categoria"},
		{"kind", "tipo"},
		{"language", "idioma"},
		{"code", "codigo"},
		{"commands", "ordenes"},
		{"sources", "fuentes"},
		{"files", "archivos"},
		{"fingerprint", "huella"},
		{"gameplayFingerprint", "huellaJugabilidad"},
		{"affectsGameplay", "afectaJugabilidad"},
		{"folder", "carpeta"},
		{"autoGenerated", "generadoAutomaticamente"},
		{"profile", "perfil"},
		{"schema", "esquema"},
		{"gameplay", "jugabilidad"},
		{"seconds", "segundos"},
		{"states", "estados"},
		{"rules", "reglas"},
		{"autosave", "autoguardado"},
		{"map", "mapa"},
		{"key", "clave"},
		{"page", "pagina"},
		{"update", "actualizacion"},
		{"requires", "requiere"},
		{"incompatibleWith", "incompatibleCon"},
		{"icon", "icono"},
		{"index", "indice"},
		{"biome", "bioma"},
		{"template", "plantilla"},
		{"internalName", "nombreInterno"},
		{"letter", "letra"},
		{"random", "aleatoria"},
		{"ai", "ia"},
		{"ownAi", "iaPropia"},
		{"classes", "clases"},
		{"values", "valores"},
		{"default", "defecto"},
		{"signature", "firma"},
		{"notes", "notas"},
		{"session", "sesion"},
	};
	const auto it = kLegacy.find(key);
	return it == kLegacy.end() ? nullptr : it->second;
}

std::string LegacyDirective(const std::string &word)
{
	if (word == "principio") return "top";
	if (word == "final") return "bottom";
	if (word == "antes") return "before";
	if (word == "despues") return "after";
	if (word == "reemplazar") return "replace";
	if (word == "fin") return "end";
	return word;
}

std::string LegacyCheatVerb(const std::string &verb)
{
	static const std::unordered_map<std::string, const char *> kVerbs = {
		{"oro", "gold"}, {"comida", "food"}, {"mapa", "map"}, {"poblacion", "maxpop"}, {"habitantes", "population"}, {"curar", "heal"},
		{"paz", "peace"}, {"nivel", "level"}, {"reparar", "repair"}, {"racion", "ration"}, {"enemigos", "kill"}, {"capturar", "capture"},
		{"crear", "spawn"},
	};
	const auto it = kVerbs.find(verb);
	return it == kVerbs.end() ? verb : it->second;
}

unsigned short LegacyKeyVk(const std::string &name)
{
	struct Entry {
		const char *name;
		unsigned short vk;
	};
	static const Entry kNames[] = {
		{"RePag", VK_PRIOR}, {"AvPag", VK_NEXT}, {"ImprPant", VK_SNAPSHOT}, {"Izquierda", VK_LEFT}, {"Arriba", VK_UP}, {"Derecha", VK_RIGHT},
		{"Abajo", VK_DOWN}, {"Coma", VK_OEM_COMMA}, {"Punto", VK_OEM_PERIOD}, {"Menos", VK_OEM_MINUS}, {"Mas", VK_OEM_PLUS},
		{"RatonCentral", VK_MBUTTON}, {"Raton4", VK_XBUTTON1}, {"Raton5", VK_XBUTTON2},
	};
	for (const Entry &e : kNames) {
		if (_stricmp(e.name, name.c_str()) == 0) return e.vk;
	}
	return 0;
}

bool LegacyIsShiftName(const char *name)
{
	return _stricmp(name, "Mayus") == 0 || _stricmp(name, "Mayús") == 0;
}

std::string LegacyProfileName(const std::string &name)
{
	return name == "Sin perfil" ? "No profile" : name;
}

bool LegacyIsYes(const std::string &lowerValue)
{
	return lowerValue == "si" || lowerValue == "sí";
}

int LegacyModKind(const std::string &lowerId)
{
	if (lowerId == "reglas" || lowerId == "jugabilidad") return 0;
	if (lowerId == "contenido" || lowerId == "mapas") return 1;
	if (lowerId == "estetico" || lowerId == "estético") return 2;
	return -1;
}
