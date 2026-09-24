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

#include <windows.h>
#include <stdio.h>

#include "keel/version.h"

static void Report(FILE* f, const wchar_t* name) {
    fwprintf(f, L"loading %s ...\n", name); fflush(f);
    HMODULE m = LoadLibraryW(name);
    if (!m) { fwprintf(f, L"%-16s LOAD !FAILED! err=%lu\n", name, GetLastError()); fflush(f); return; }
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(m, path, MAX_PATH);
    DWORD a = 0, b = 0, c = 0, d = 0;
    keel::GetFileVersion(path, a, b, c, d);
    const bool donor = (a == 6 && b == 1);
    fwprintf(f, L"%-16s %u.%u.%u.%u  %-7s  %s\n", name, a, b, c, d, donor ? L"[WIN7]" : L"[host]", path);
    fflush(f);
}

int wmain() {
    FILE* f = nullptr;
    _wfopen_s(&f, L"C:\\keel\\r2.txt", L"w, ccs=UTF-8");
    if (!f) return 1;
    setvbuf(f, nullptr, _IONBF, 0);
    const auto v = keel::GetHostVersion();
    fwprintf(f, L"keelr2test on host build %lu.%lu.%lu.%lu\n", v.major, v.minor, v.build, v.ubr);
    fwprintf(f, L"KnownDLL redirection test (bare-name LoadLibrary)\n\n");

    Report(f, L"shlwapi.dll");
    Report(f, L"user32.dll");
    Report(f, L"comctl32.dll");
    Report(f, L"shell32.dll");
    fwprintf(f, L"done\n");
    fclose(f);
    return 0;
}
