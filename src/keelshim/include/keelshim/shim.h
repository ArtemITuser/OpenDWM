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

#ifdef KEELSHIM_EXPORTS
#define KEELSHIM_API __declspec(dllexport)
#else
#define KEELSHIM_API __declspec(dllimport)
#endif

extern "C" {

KEELSHIM_API DWORD WINAPI KeelShimInitialize(void);

struct KeelShimStatus {
    DWORD size;
    DWORD hostBuild;
    DWORD hooksInstalled;
    DWORD importsPatched;
    BOOL  initialized;
};
KEELSHIM_API BOOL WINAPI KeelShimGetStatus(KeelShimStatus* status);

}
