@echo off
rem Build helper: MSVC x64 from the VS 2022 Professional install (the Community install on E: has a broken vcvarsall).
rem Everything that runs lands in bin\ (objects and the test binaries in build\).
rem   build.bat                      -> bin\fs1r_emu.exe, the console, from src\fs1r_lib.cpp + src\fs1r_console.cpp
rem   build.bat test                 -> builds and runs the self checks (effects, engine, formant, panel)
rem   build.bat plugin               -> the standalone, VST3 and CLAP through CMake into bin\Standalone, bin\VST3, bin\CLAP
rem   build.bat file.cpp [cl args]   -> compiles whatever you pass (paths relative to this folder)
rem CMakeLists.txt builds the same targets for anything that is not MSVC-on-Windows.
pushd "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build mkdir build
if not exist bin mkdir bin
if "%~1"=="" (
  cl /nologo /O2 /EHsc /W3 /std:c++17 src\fs1r_lib.cpp src\fs1r_console.cpp winmm.lib /Fe:bin\fs1r_emu.exe /Fobuild\
) else if "%~1"=="test" (
  cl /nologo /O2 /EHsc /W3 /std:c++17 src\fs1r_lib.cpp src\fs1r_console.cpp winmm.lib /Fe:bin\fs1r_emu.exe /Fobuild\ || goto :done
  cl /nologo /O2 /EHsc /W3 /std:c++17 tools\test_effects.cpp /Fe:build\test_effects.exe /Fobuild\ || goto :done
  build\test_effects.exe || goto :done
  "%~dp0bin\fs1r_emu.exe" -selftest || goto :done
  python "%~dp0tools\check_formant.py" || goto :done
  python "%~dp0tools\check_panel.py"
) else if "%~1"=="plugin" (
  cmake -B build\plugin -S . -DFS1R_BUILD_PLUGIN=ON || goto :done
  cmake --build build\plugin --config Release
) else (
  cl /nologo /O2 /EHsc /W3 /std:c++17 %* /Fobuild\
)
:done
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
