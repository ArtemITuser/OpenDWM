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
project(Keel LANGUAGES C CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Keel builds with MSVC only so dot-source tools\\env.ps1 first")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(KEEL_BUILD_DRIVER "Build keeldrv.sys (needs WDK km headers/libs)" ON)

add_compile_definitions(
  UNICODE _UNICODE
  _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000009
  WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /Zi /guard:cf)
add_link_options(/DEBUG /guard:cf)

if(NOT DEFINED ENV{DETOURS_ROOT})
  message(FATAL_ERROR "DETOURS_ROOT not set (tools\\env.ps1 sets it)")
endif()
add_library(detours STATIC IMPORTED)
set_target_properties(detours PROPERTIES
  IMPORTED_LOCATION "$ENV{DETOURS_ROOT}/lib.X64/detours.lib"
  INTERFACE_INCLUDE_DIRECTORIES "$ENV{DETOURS_ROOT}/include")

add_subdirectory(keelcommon)
add_subdirectory(keelshim)
add_subdirectory(keeltsf)
add_subdirectory(keelldr)
add_subdirectory(keelexp)
add_subdirectory(keeltest)
add_subdirectory(keelbroker)
add_subdirectory(keeluxsms)
add_subdirectory(keelr2test)
add_subdirectory(keeldxprobe)
add_subdirectory(keeldcprobe)
add_subdirectory(keeldcomp)
if(KEEL_BUILD_DRIVER)
  add_subdirectory(keeldrv)
endif()
