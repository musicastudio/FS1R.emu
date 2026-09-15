# fs1rLib          the engine, no Windows, no host, no GUI. This is what a plugin links.
# fs1r_emu         the test console (WinMM, Windows only), built into bin/.
# test_effects     the effect-block self check.
#
# build.bat is the quick MSVC path; this file is for clang, CI, and the JUCE plugin layer later.
#   cmake -B build/cmake -S . && cmake --build build/cmake --config Release && ctest --test-dir build/cmake
cmake_minimum_required(VERSION 3.16)
project(fs1r_emu VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Everything meant to be run lands in bin/: the console here, the plugin formats under bin/<Format>/.
set(FS1R_BIN ${CMAKE_CURRENT_SOURCE_DIR}/bin)

add_library(fs1rLib STATIC src/fs1r_lib.cpp)
target_include_directories(fs1rLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(fs1rLib PRIVATE /W3)
else()
  target_compile_options(fs1rLib PRIVATE -Wall -Wno-unused-function)
endif()

add_executable(test_effects tools/test_effects.cpp)
target_include_directories(test_effects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
  add_executable(fs1r_emu src/fs1r_console.cpp)
  target_link_libraries(fs1r_emu PRIVATE fs1rLib winmm)
  set_target_properties(fs1r_emu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")
  add_test(NAME engine COMMAND fs1r_emu -selftest)
else()
  message(STATUS "fs1r_emu console needs WinMM; building fs1rLib and test_effects only")
endif()
