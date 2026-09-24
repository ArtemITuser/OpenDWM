/*
 * KeelShim - Windows 7 user-mode UI translation for the Windows 10 kernel
 * Copyright (C) 2026 Kevin Dalli <projectkeel@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <windows.h>

namespace keel {

constexpr DWORD kPinnedHostBuild = 19044;
constexpr DWORD kDonorBuild      = 7601;

struct OsVersion { DWORD major, minor, build, ubr; };

OsVersion GetHostVersion();
bool HostIsPinned();
bool GetFileVersion(const wchar_t* path, DWORD& major, DWORD& minor, DWORD& build, DWORD& rev);

}
