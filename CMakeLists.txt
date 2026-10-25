cmake_minimum_required(VERSION 3.20)
project(imperivm_resurrected LANGUAGES C CXX)

# Two things can be built, each with its own option:
#   IMPERIVM_BUILD_DLL    build\winmm.dll (x86), the same output as build.bat. Needs MinHook, miniaudio and the Steamworks SDK.
#   IMPERIVM_BUILD_TESTS  the unit tests (tests\*.cpp), run with ctest. Of those libraries they only need miniz.
#
#   cmake -S . -B build\cmake -G Ninja -DCMAKE_BUILD_TYPE=Release     (from an x86 Developer Command Prompt)
#   cmake -S . -B build\cmake -A Win32                                (Visual Studio generators)
#   cmake -S . -B build\tests -A Win32 -DIMPERIVM_BUILD_DLL=OFF        (tests only)
#   cmake --build build\cmake --config Release
#   ctest --test-dir build\cmake -C Release --output-on-failure
# Use a short build path: very long paths break MSVC's /showIncludes handling in CMake's compiler checks.
#
# Dependencies of the DLL, by default in the layout of the private workspace (..\tools) or in third_party\ (public repository):
#   MINHOOK_DIR, MINIAUDIO_DIR, STEAMWORKS_SDK (folder that contains public\steam\steam_api.h)
# miniz (MINIZ_DIR) is used by the DLL and by the tests of the zip code.
# libarchive (LIBARCHIVE_DIR, reduced to its RAR, RAR5 and 7z readers), the LZMA decoders of xz (XZ_DIR) and bzip2 (BZIP2_DIR) are used by the DLL and by the tests of the archive code.

option(IMPERIVM_BUILD_DLL "Build winmm.dll (needs the Steamworks SDK)" ON)
option(IMPERIVM_BUILD_TESTS "Build the unit tests" ON)

if(NOT MSVC)
	message(FATAL_ERROR "The mod targets MSVC (the game is a 32-bit Windows executable).")
endif()
if(IMPERIVM_BUILD_DLL AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "The mod must be built for x86. Use an x86 toolchain (vcvars32) or -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")
	set(_default_third_party "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
else()
	set(_default_third_party "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
endif()
set(MINIZ_DIR "${_default_third_party}/miniz" CACHE PATH "miniz source folder (miniz.c and miniz.h)")
if(NOT EXISTS "${MINIZ_DIR}/miniz.h")
	message(FATAL_ERROR "Missing dependency: ${MINIZ_DIR}/miniz.h")
endif()

set(LIBARCHIVE_DIR "${_default_third_party}/libarchive" CACHE PATH "libarchive source folder (config.h and libarchive\\*.c)")
if(NOT EXISTS "${LIBARCHIVE_DIR}/libarchive/archive.h")
	message(FATAL_ERROR "Missing dependency: ${LIBARCHIVE_DIR}/libarchive/archive.h")
endif()
set(XZ_DIR "${_default_third_party}/xz" CACHE PATH "xz source folder (config.h and src\\liblzma, reduced to the decoders)")
if(NOT EXISTS "${XZ_DIR}/src/liblzma/api/lzma.h")
	message(FATAL_ERROR "Missing dependency: ${XZ_DIR}/src/liblzma/api/lzma.h")
endif()
set(BZIP2_DIR "${_default_third_party}/bzip2" CACHE PATH "bzip2 source folder (the decompression files of libbz2)")
if(NOT EXISTS "${BZIP2_DIR}/bzlib.h")
	message(FATAL_ERROR "Missing dependency: ${BZIP2_DIR}/bzlib.h")
endif()
file(GLOB BZIP2_SOURCES CONFIGURE_DEPENDS "${BZIP2_DIR}/*.c")
set_source_files_properties(${BZIP2_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
# Both libraries have their own config.h, so each one only sees its own folder. Its zlib.h is miniz.
file(GLOB LIBARCHIVE_SOURCES CONFIGURE_DEPENDS "${LIBARCHIVE_DIR}/libarchive/*.c")
set_source_files_properties(${LIBARCHIVE_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0" COMPILE_DEFINITIONS "HAVE_CONFIG_H;LZMA_API_STATIC"
	INCLUDE_DIRECTORIES "${LIBARCHIVE_DIR};${LIBARCHIVE_DIR}/libarchive;${XZ_DIR}/src/liblzma/api;${BZIP2_DIR};${MINIZ_DIR}")
file(GLOB XZ_SOURCES CONFIGURE_DEPENDS "${XZ_DIR}/src/liblzma/*/*.c")
set(XZ_INCLUDES "${XZ_DIR}")
foreach(_part api common lz lzma rangecoder delta simple)
	list(APPEND XZ_INCLUDES "${XZ_DIR}/src/liblzma/${_part}")
endforeach()
list(APPEND XZ_INCLUDES "${XZ_DIR}/src/common")
set_source_files_properties(${XZ_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0" COMPILE_DEFINITIONS "HAVE_CONFIG_H;LZMA_API_STATIC" INCLUDE_DIRECTORIES "${XZ_INCLUDES}")

if(IMPERIVM_BUILD_DLL)
	set(MINHOOK_DIR "${_default_third_party}/minhook" CACHE PATH "MinHook source tree")
	set(MINIAUDIO_DIR "${_default_third_party}/miniaudio" CACHE PATH "miniaudio source tree")
	set(STEAMWORKS_SDK "${_default_third_party}/steamworks_sdk" CACHE PATH "Steamworks SDK folder (provided by Valve)")

	foreach(_required "${MINHOOK_DIR}/include/MinHook.h" "${MINIAUDIO_DIR}/miniaudio.h" "${STEAMWORKS_SDK}/public/steam/steam_api.h")
		if(NOT EXISTS "${_required}")
			message(FATAL_ERROR "Missing dependency: ${_required}")
		endif()
	endforeach()

	enable_language(RC)
	file(GLOB_RECURSE MOD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

	set(MINHOOK_SOURCES
		"${MINHOOK_DIR}/src/buffer.c"
		"${MINHOOK_DIR}/src/hook.c"
		"${MINHOOK_DIR}/src/trampoline.c"
		"${MINHOOK_DIR}/src/hde/hde32.c")
	set(MINIAUDIO_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/src/hooks/miniaudio_impl.c")
	set(MINIZ_SOURCE "${MINIZ_DIR}/miniz.c")

	add_library(winmm SHARED ${MOD_SOURCES} ${MINHOOK_SOURCES} ${MINIAUDIO_IMPL} ${MINIZ_SOURCE} ${LIBARCHIVE_SOURCES} ${XZ_SOURCES} ${BZIP2_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/app/winmm.def" "${CMAKE_CURRENT_SOURCE_DIR}/src/app/winmm.rc")
	set_target_properties(winmm PROPERTIES PREFIX "" OUTPUT_NAME "winmm")

	set_source_files_properties(${MINIAUDIO_IMPL} ${MINIZ_SOURCE} PROPERTIES COMPILE_OPTIONS "/W0")
	set_source_files_properties(${MOD_SOURCES} PROPERTIES COMPILE_OPTIONS "/W3;/EHsc;/utf-8")

	target_include_directories(winmm PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${MINHOOK_DIR}/include"
		"${MINIAUDIO_DIR}"
		"${MINIZ_DIR}"
		"${LIBARCHIVE_DIR}/libarchive"
		"${STEAMWORKS_SDK}/public")
	target_compile_definitions(winmm PRIVATE $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
	target_compile_options(winmm PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2>)
	target_link_libraries(winmm PRIVATE
		d3d11 dxgi d3dcompiler user32 gdi32 kernel32 ole32 bcrypt shell32 ws2_32 winhttp comdlg32 advapi32 windowscodecs strmiids)
endif()

if(IMPERIVM_BUILD_TESTS)
	enable_testing()

	function(imperivm_test name)
		add_executable(${name} tests/${name}.cpp ${ARGN})
		target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/tests" "${MINIZ_DIR}")
		target_compile_options(${name} PRIVATE /W3 /EHsc /utf-8)
		target_link_libraries(${name} PRIVATE user32 advapi32 bcrypt)
		string(REGEX REPLACE "^test_" "" short_name ${name})
		add_test(NAME ${short_name} COMMAND ${name} ${IMPERIVM_TEST_ARGS})
	endfunction()

	set(TR_SOURCES src/i18n/tr.cpp src/i18n/tr_data.cpp src/core/legacy.cpp tests/log_stub.cpp)
	set(HASH_SOURCES src/core/sha256.cpp)
	set(ZIP_SOURCES src/core/zipfile.cpp "${MINIZ_DIR}/miniz.c")
	set(ARCHIVE_SOURCES src/core/archivefile.cpp ${ZIP_SOURCES} ${LIBARCHIVE_SOURCES} ${XZ_SOURCES} ${BZIP2_SOURCES})
	set_source_files_properties("${MINIZ_DIR}/miniz.c" PROPERTIES COMPILE_OPTIONS "/W0")

	imperivm_test(test_legacy src/core/legacy.cpp)
	imperivm_test(test_json src/core/json.cpp ${TR_SOURCES})
	imperivm_test(test_pathsafe)
	imperivm_test(test_versions src/core/versioncmp.cpp)
	imperivm_test(test_modrules src/mods/modrules.cpp ${HASH_SOURCES})
	imperivm_test(test_fingerprint src/mods/fingerprint.cpp src/mods/modrules.cpp ${HASH_SOURCES})
	imperivm_test(test_options src/mods/modoption_bits.cpp)
	imperivm_test(test_pak src/mods/pak.cpp)
	imperivm_test(test_modlist src/net/modlist.cpp)
	imperivm_test(test_patchxml src/mods/patchxml.cpp ${TR_SOURCES})
	imperivm_test(test_log src/core/logcore.cpp)
	imperivm_test(test_logfiles src/core/logfiles.cpp)
	imperivm_test(test_logredact src/core/logredact.cpp)
	imperivm_test(test_modorder src/mods/modorder.cpp)
	imperivm_test(test_modclash src/mods/modclash.cpp src/mods/modrules.cpp src/mods/patchxml.cpp ${HASH_SOURCES} ${TR_SOURCES})
	imperivm_test(test_catalogfilter src/mods/catalogfilter.cpp)
	imperivm_test(test_looselayout src/mods/looselayout.cpp)
	imperivm_test(test_leftovers src/mods/leftovers.cpp)
	imperivm_test(test_gbref src/mods/gbref.cpp src/core/json.cpp ${TR_SOURCES})
	imperivm_test(test_transferrate)
	imperivm_test(test_serverpick src/mods/serverpick.cpp src/core/json.cpp ${TR_SOURCES})
	imperivm_test(test_roomcontent src/net/roomcontent.cpp)
	imperivm_test(test_scenvalue src/net/scenvalue.cpp)
	imperivm_test(test_mappack src/mods/mappack.cpp)
	imperivm_test(test_seasondecors src/game/seasondecors.cpp)
	imperivm_test(test_rletail src/mods/rletail.cpp)
	imperivm_test(test_hidefiles src/mods/hidefiles.cpp)
	imperivm_test(test_civroom src/game/civroom.cpp)
	imperivm_test(test_techplan src/game/techplan.cpp)
	imperivm_test(test_unitcatalog src/game/unitcatalog.cpp)
	imperivm_test(test_hpfs src/core/hpfs.cpp)
	imperivm_test(test_mapinfo src/game/mapinfo.cpp src/core/hpfs.cpp)
	imperivm_test(test_bonuses src/game/bonuses.cpp)
	imperivm_test(test_gbcache src/mods/gbcache.cpp)
	imperivm_test(test_menuart src/mods/menuart.cpp)
	imperivm_test(test_respick src/hooks/respick.cpp)
	imperivm_test(test_builtin src/hooks/builtin_data.cpp src/mods/patchxml.cpp ${TR_SOURCES})
	imperivm_test(test_logreport src/core/logreport.cpp src/core/logfiles.cpp src/core/logredact.cpp ${ZIP_SOURCES} tests/log_stub.cpp)
	imperivm_test(test_zip ${ZIP_SOURCES})
	set(IMPERIVM_TEST_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rar")
	imperivm_test(test_rar ${ARCHIVE_SOURCES})
	target_include_directories(test_rar PRIVATE "${LIBARCHIVE_DIR}/libarchive")
	set(IMPERIVM_TEST_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/7z")
	imperivm_test(test_7z ${ARCHIVE_SOURCES})
	target_include_directories(test_7z PRIVATE "${LIBARCHIVE_DIR}/libarchive")
	unset(IMPERIVM_TEST_ARGS)

	find_program(NODE_EXE node)
	if(NODE_EXE)
		set(TSV "${CMAKE_CURRENT_BINARY_DIR}/translations.tsv")
		add_custom_command(OUTPUT "${TSV}"
			COMMAND "${NODE_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/export-translations-tsv.js" "${TSV}"
			DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/i18n/translations.json" "${CMAKE_CURRENT_SOURCE_DIR}/tools/export-translations-tsv.js")
		add_custom_target(translations_tsv ALL DEPENDS "${TSV}")
		set(IMPERIVM_TEST_ARGS "${TSV}")
		imperivm_test(test_i18n ${TR_SOURCES})
		unset(IMPERIVM_TEST_ARGS)
		add_dependencies(test_i18n translations_tsv)
	else()
		message(WARNING "node was not found: the translation test is skipped")
	endif()
endif()
