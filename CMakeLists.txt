# fs1rLib          the engine, no Windows, no host, no GUI. This is what a plugin links.
# fs1r_emu         the test console (WinMM, Windows only), built into bin/.
# test_effects     the effect-block self check.
#
# build.bat is the quick MSVC path; this file is for clang, CI, and the JUCE plugin layer later.
#   cmake -B build/cmake -S . && cmake --build build/cmake --config Release && ctest --test-dir build/cmake
cmake_minimum_required(VERSION 3.16)
# macOS builds are universal, arm64 and x86_64 in one binary, so the same artifact runs on Apple silicon
# and on Intel Macs. The GitHub runner is Apple silicon and a plain build there is arm64 only, which is
# what issue #3 reported: "this application is not supported on this Mac". Both must be set before
# project(), and either can still be overridden on the command line.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(fsvr VERSION 0.4.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Everything meant to be run lands in bin/: the console here, the plugin formats under bin/<Format>/.
set(FS1R_BIN ${CMAKE_CURRENT_SOURCE_DIR}/bin)

add_library(fs1rLib STATIC
    src/fs1r/chips/ymp706.cpp
    src/fs1r/firmware/controllers.cpp
    src/fs1r/firmware/fseq.cpp
    src/fs1r/firmware/midi.cpp
    src/fs1r/firmware/notes.cpp
    src/fs1r/firmware/patch.cpp
    src/fs1r/firmware/rom.cpp
    src/fsvr/device.cpp
    src/fsvr/selftest.cpp)
target_include_directories(fs1rLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The plugin formats are shared modules, and a Linux .so will not link a static library whose objects
# are not position independent. Windows and macOS do not care either way.
set_target_properties(fs1rLib PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
if(MSVC)
  target_compile_options(fs1rLib PRIVATE /W3)
else()
  target_compile_options(fs1rLib PRIVATE -Wall -Wno-unused-function)
endif()

add_executable(test_effects tools/test_effects.cpp)
target_include_directories(test_effects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The renderer behind the calibration loop. The console does this too, but only on Windows, and
# tools/analyze_capture.py needs a render of every capture request to compare a recording against.
add_executable(render_capture tools/render_capture.cpp)
target_link_libraries(render_capture PRIVATE fs1rLib)
set_target_properties(render_capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")

enable_testing()
add_test(NAME effects COMMAND test_effects)

option(FS1R_BUILD_PLUGIN "Build the JUCE plugin (needs the extern/ submodules)" OFF)
if(FS1R_BUILD_PLUGIN)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/extern/JUCE/CMakeLists.txt)
    message(FATAL_ERROR "extern/JUCE is empty; run: git submodule update --init --recursive")
  endif()
  add_subdirectory(extern/JUCE)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/extern/clap-juce-extensions/CMakeLists.txt)
    add_subdirectory(extern/clap-juce-extensions EXCLUDE_FROM_ALL)
  endif()
  add_subdirectory(plugin)
endif()

if(WIN32)
  add_executable(fs1r_emu src/console/main.cpp)
  target_link_libraries(fs1r_emu PRIVATE fs1rLib winmm)
  set_target_properties(fs1r_emu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")
  add_test(NAME engine COMMAND fs1r_emu -selftest)
else()
  message(STATUS "fs1r_emu console needs WinMM; building fs1rLib and test_effects only")
endif()
