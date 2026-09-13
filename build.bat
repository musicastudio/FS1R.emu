@echo off
rem Build helper: MSVC x64 from the VS 2022 Professional install (the Community install on E: has a broken vcvarsall).
rem   build.bat                      -> builds fs1r_emu.exe from src\fs1r_emu.cpp
rem   build.bat file.cpp [cl args]   -> compiles whatever you pass (paths relative to this folder)
pushd "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build mkdir build
if "%~1"=="" (
  cl /nologo /O2 /EHsc /W3 /std:c++17 src\fs1r_emu.cpp winmm.lib /Fe:fs1r_emu.exe /Fobuild\
) else (
  cl /nologo /O2 /EHsc /W3 /std:c++17 %* /Fobuild\
)
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
