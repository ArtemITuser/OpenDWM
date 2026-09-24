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

set(_sdkver "$ENV{WindowsSDKVersion}")
string(REGEX REPLACE "[\\/]+$" "" _sdkver "${_sdkver}")
set(_kits "$ENV{WindowsSdkDir}")
string(REGEX REPLACE "[\\/]+$" "" _kits "${_kits}")
set(_kmdf "$ENV{KMDF_VERSION}")

if(NOT EXISTS "${_kits}/Include/${_sdkver}/km/ntddk.h" OR "${_kmdf}" STREQUAL "")
  message(WARNING "keeldrv: WDK km headers or KMDF libs not found; skipping driver")
  return()
endif()
string(REPLACE "." ";" _kmdf_parts "${_kmdf}")
list(GET _kmdf_parts 0 _kmdf_major)
list(GET _kmdf_parts 1 _kmdf_minor)

set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "/Zi /Od /Ob0")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "/Zi /O2 /Ob1 /DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob1 /DNDEBUG")
set(CMAKE_C_STANDARD_LIBRARIES "")
set(CMAKE_EXE_LINKER_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "")
set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "")

add_executable(keeldrv driver.c)
set_target_properties(keeldrv PROPERTIES OUTPUT_NAME keeldrv SUFFIX ".sys" MSVC_RUNTIME_LIBRARY "")

target_compile_options(keeldrv PRIVATE
  /kernel /GS /Gy /Gw /Oi /Zc:wchar_t- /GR- /EHs-c- /Qspectre /guard:cf /W4 /wd4201 /wd4214 /wd4996)
target_compile_definitions(keeldrv PRIVATE
  _WIN64 _AMD64_ AMD64
  KMDF_VERSION_MAJOR=${_kmdf_major} KMDF_VERSION_MINOR=${_kmdf_minor}
  POOL_NX_OPTIN=1 DEPRECATE_DDK_FUNCTIONS=1)
target_include_directories(keeldrv PRIVATE
  "${_kits}/Include/${_sdkver}/km"
  "${_kits}/Include/${_sdkver}/km/crt"
  "${_kits}/Include/${_sdkver}/shared"
  "${_kits}/Include/wdf/kmdf/${_kmdf}")
target_link_directories(keeldrv PRIVATE
  "${_kits}/Lib/${_sdkver}/km/x64"
  "${_kits}/Lib/wdf/kmdf/x64/${_kmdf}")
target_link_options(keeldrv PRIVATE
  /DRIVER /SUBSYSTEM:NATIVE,10.0 /ENTRY:FxDriverEntry /NODEFAULTLIB /INCREMENTAL:NO /RELEASE
  /NXCOMPAT /DYNAMICBASE /MANIFEST:NO /INTEGRITYCHECK /guard:cf
  /MERGE:_TEXT=.text /MERGE:_PAGE=PAGE /SECTION:INIT,d /OPT:REF /OPT:ICF /DEBUG
  /IGNORE:4078 /IGNORE:4254)
target_link_libraries(keeldrv PRIVATE
  ntoskrnl.lib hal.lib wmilib.lib BufferOverflowFastFailK.lib
  WdfLdr.lib WdfDriverEntry.lib ntstrsafe.lib wdmsec.lib)

find_program(STAMPINF stampinf.exe HINTS "${_kits}/bin/${_sdkver}/x64" "${_kits}/bin/${_sdkver}/x86")
configure_file(keeldrv.inf.in "${CMAKE_CURRENT_BINARY_DIR}/keeldrv.inf" COPYONLY)
if(STAMPINF)
  add_custom_command(TARGET keeldrv POST_BUILD
    COMMAND "${STAMPINF}" -f keeldrv.inf -d * -v * -k ${_kmdf} -a amd64
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "stampinf keeldrv.inf")
endif()
