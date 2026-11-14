# Build and run the luaaa tests with CMake / CTest (every platform, including MSVC).
# tests/run.sh is the faster, sanitizer-enabled runner for Linux and macOS;
# this file exists so the same tests can run where bash + pkg-config do not.
#
#   # against a Lua SOURCE tree (built here as a static library):
#   cmake -S tests -B build -DLUA_SRC_DIR=/path/to/lua-5.4.8
#   # or against an installed Lua found by pkg-config:
#   cmake -S tests -B build -DLUA_PKG=lua5.4
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
cmake_minimum_required(VERSION 3.14)
project(luaaa_tests C CXX)

set(LUA_SRC_DIR "" CACHE PATH "Lua source tree (the directory containing src/lua.h) to build and test against")
set(LUA_PKG "lua" CACHE STRING "pkg-config module of an installed Lua (used when LUA_SRC_DIR is empty)")
set(LUAAA_CXX_STANDARD 11 CACHE STRING "C++ standard to compile the tests with (11, 14, 17, 20, 23)")

set(CMAKE_CXX_STANDARD ${LUAAA_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(LUA_SRC_DIR)
    file(GLOB LUA_SOURCES "${LUA_SRC_DIR}/src/*.c")
    list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")
    if(NOT LUA_SOURCES)
        message(FATAL_ERROR "No Lua sources under ${LUA_SRC_DIR}/src")
    endif()
    add_library(lua_under_test STATIC ${LUA_SOURCES})
    target_include_directories(lua_under_test PUBLIC "${LUA_SRC_DIR}/src")
    if(UNIX)
        target_link_libraries(lua_under_test PUBLIC m)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA_UNDER_TEST REQUIRED IMPORTED_TARGET ${LUA_PKG})
    add_library(lua_under_test INTERFACE)
    target_link_libraries(lua_under_test INTERFACE PkgConfig::LUA_UNDER_TEST)
endif()

find_package(Threads REQUIRED)
enable_testing()

file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(${name} PRIVATE lua_under_test Threads::Threads)
    if(MSVC)
        # /utf-8: the tests contain UTF-8 string literals; /Zc:__cplusplus: report the real language level
        target_compile_options(${name} PRIVATE /W3 /utf-8 /Zc:__cplusplus /bigobj)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endforeach()
