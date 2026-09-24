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

add_library(keelshim SHARED
  src/dllmain.cpp
  src/hooks.cpp
  src/knowndll.cpp
  src/uxsms.cpp
  src/sessionreg.cpp
  include/keelshim/shim.h
  keelshim.def)
target_include_directories(keelshim PUBLIC include)
target_link_libraries(keelshim PRIVATE keelcommon detours user32 gdi32 advapi32 ole32)
set_target_properties(keelshim PROPERTIES OUTPUT_NAME "keelshim")
