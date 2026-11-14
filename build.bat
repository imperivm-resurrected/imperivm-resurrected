@echo off
rem Builds the mod as build\winmm.dll (x86). Usage: build.bat
rem You need: Visual Studio Build Tools 2022 with the C++ compiler and the Steamworks SDK (download it from Valve; it is not in this repository).
rem Put the SDK path in the STEAMWORKS_SDK variable (the folder that contains public\steam\steam_api.h).
setlocal
if "%STEAMWORKS_SDK%"=="" (
  echo Set STEAMWORKS_SDK to the Steamworks SDK folder, for example: set STEAMWORKS_SDK=C:\steamworks_sdk
  exit /b 1
)
if not exist "%STEAMWORKS_SDK%\public\steam\steam_api.h" (
  echo Cannot find %STEAMWORKS_SDK%\public\steam\steam_api.h
  exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
where cl >nul 2>nul || (echo The x86 C++ compiler was not found & exit /b 1)
cd /d "%~dp0"
if not exist build mkdir build
del /q build\*.obj build\*.res 2>nul
set MH=third_party\minhook
set MA=third_party\miniaudio
set MZ=third_party\miniz
set LA=third_party\libarchive
set XZ=third_party\xz
set BZ=third_party\bzip2
set XL=%XZ%\src\liblzma

cl /nologo /c /O2 /MT /W3 /DNDEBUG /I%MH%\include /Fobuild\ %MH%\src\buffer.c %MH%\src\hook.c %MH%\src\trampoline.c %MH%\src\hde\hde32.c || exit /b 1
cl /nologo /c /O2 /MT /W0 /DNDEBUG /I%MA% /Fobuild\ src\hooks\miniaudio_impl.c || exit /b 1
cl /nologo /c /O2 /MT /W0 /DNDEBUG /I%MZ% /Fobuild\ %MZ%\miniz.c || exit /b 1
cl /nologo /c /O2 /MT /W0 /DNDEBUG /DHAVE_CONFIG_H /DLZMA_API_STATIC /I%LA% /I%LA%\libarchive /I%XL%\api /I%BZ% /I%MZ% /Fobuild\ %LA%\libarchive\*.c || exit /b 1
cl /nologo /c /O2 /MT /W0 /DNDEBUG /DHAVE_CONFIG_H /DLZMA_API_STATIC /I%XZ% /I%XL%\api /I%XL%\common /I%XL%\lz /I%XL%\lzma /I%XL%\rangecoder /I%XL%\delta /I%XL%\simple /I%XZ%\src\common /Fobuild\ %XL%\common\*.c %XL%\lz\*.c %XL%\lzma\*.c %XL%\delta\*.c %XL%\simple\*.c || exit /b 1
cl /nologo /c /O2 /MT /W0 /DNDEBUG /I%BZ% /Fobuild\ %BZ%\*.c || exit /b 1
rc /nologo /Isrc /fo build\winmm.res src\app\winmm.rc || exit /b 1
cl /nologo /c /O2 /MT /W3 /EHsc /std:c++17 /utf-8 /DNDEBUG /Isrc /I%MA% /I%MZ% /I%LA%\libarchive /I%MH%\include /I"%STEAMWORKS_SDK%\public" /Fobuild\ src\core\*.cpp src\i18n\*.cpp src\app\*.cpp src\hooks\*.cpp src\game\*.cpp src\mods\*.cpp src\net\*.cpp src\ui\*.cpp || exit /b 1
link /nologo /DLL /MAP:build\winmm.map /OUT:build\winmm.dll /DEF:src\app\winmm.def build\*.obj build\winmm.res d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib kernel32.lib ole32.lib bcrypt.lib shell32.lib ws2_32.lib winhttp.lib comdlg32.lib advapi32.lib crypt32.lib windowscodecs.lib strmiids.lib || exit /b 1
echo OK: build\winmm.dll
