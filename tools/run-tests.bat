@echo off
rem Runs everything that checks the code without the game: the source checks (Node), a syntax-only compile of every file, and the unit tests (CMake + ctest).
rem No Steamworks SDK is needed (files that include its headers are only checked when STEAMWORKS_SDK or ..\tools\steamworks_sdk exist).
rem Run it from a Visual Studio x86 Developer Command Prompt, or from any prompt if Visual Studio is installed. Usage: tools\run-tests.bat
setlocal enabledelayedexpansion
cd /d "%~dp0.."
where cl >nul 2>nul && set HAVE_CL=1
if not defined HAVE_CL if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" echo Visual Studio was not found: run this from a Developer Command Prompt& exit /b 1
if not defined HAVE_CL pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not defined HAVE_CL .\vswhere.exe -latest -products * -property installationPath > "%TEMP%\imperivm-vsdir.txt"
if not defined HAVE_CL popd
if not defined HAVE_CL set /p VSDIR=<"%TEMP%\imperivm-vsdir.txt"
if not defined HAVE_CL call "!VSDIR!\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if not defined HAVE_CL set "PATH=!VSDIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!VSDIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;!PATH!"
set OUT=build\tests
node tools\gen-tr.js || exit /b 1
node tools\gen-builtin.js || exit /b 1
node tools\check-addresses.js || exit /b 1
node tools\check-layering.js || exit /b 1
node tools\check-conventions.js || exit /b 1
node tools\check-includes.js || exit /b 1
node tools\check-logs.js || exit /b 1
if exist sdk node tools\check-sdk.js || exit /b 1
if exist sdk node tools\check-examples.js || exit /b 1
node tools\imperivm1\selfcheck.js || exit /b 1
node tools\syntax-check.js || exit /b 1
cmake -S . -B %OUT% -G Ninja -DCMAKE_BUILD_TYPE=Release -DIMPERIVM_BUILD_DLL=OFF >nul || exit /b 1
cmake --build %OUT% || exit /b 1
ctest --test-dir %OUT% --output-on-failure || exit /b 1
echo All tests passed
