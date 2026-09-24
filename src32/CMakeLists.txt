# KeelShim - Windows 7 user-mode UI translation for the Windows 10 kernel
# Copyright (C) 2026 Kevin Dalli <projectkeel@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.28)
project(Keel32 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Keel32 builds with MSVC only so dot-source tools\\env32.ps1 first")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Keel32 must be built as 32-bit so activate tools\\env32.ps1 (got ${CMAKE_SIZEOF_VOID_P}-byte pointers)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /Zi /guard:cf)
add_link_options(/DEBUG /guard:cf)

add_library(keelshim32 SHARED keelshim32/src/main.cpp keelshim32/src/dxgi32.cpp)
add_executable(keelldr32 keelldr32/src/main.cpp)
set_target_properties(keelldr32 PROPERTIES WIN32_EXECUTABLE OFF)
add_executable(keeldxprobe32 WIN32 keeldxprobe32/main.cpp)
target_link_libraries(keeldxprobe32 PRIVATE user32 gdi32 d3d11 dxgi dcomp)
add_executable(keelpaint WIN32 keelpaint/main.cpp keelpaint/keelpaint.rc)
target_link_libraries(keelpaint PRIVATE user32 gdi32 msimg32 comctl32)
add_executable(keelpaintm WIN32 EXCLUDE_FROM_ALL keelpaint/main.cpp keelpaint/keelpaint.rc)
target_link_libraries(keelpaintm PRIVATE user32 gdi32 msimg32 comctl32)
target_link_options(keelpaintm PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTUAC:NO"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/keelpaint/keelpaint.manifest")
