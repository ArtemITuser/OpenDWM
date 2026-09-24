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
#include <shellapi.h>
#include <cwchar>

namespace {

const wchar_t* ArgTail() {
    const wchar_t* p = GetCommandLineW();
    if (!p) return L"";
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') ++p;
        if (*p == L'"') ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    while (*p == L' ' || *p == L'\t') ++p;
    return p;
}

void EnvOr(const wchar_t* name, const wchar_t* def, wchar_t* out, DWORD cch) {
    if (GetEnvironmentVariableW(name, out, cch) == 0 || !out[0]) wcscpy_s(out, cch, def);
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t ldr[MAX_PATH], rtm[MAX_PATH];
    EnvOr(L"KEEL_LDR", L"C:\\Keel\\bin\\keelldr.exe", ldr, MAX_PATH);
    EnvOr(L"KEEL_RTM", L"C:\\Keel\\rtm", rtm, MAX_PATH);

    wchar_t cmd[4096];
    swprintf_s(cmd, L"\"%s\" --donor \"%s\" \"%s\\explorer.exe\"", ldr, rtm, rtm);
    const wchar_t* tail = ArgTail();
    if (*tail) { wcscat_s(cmd, L" "); wcscat_s(cmd, tail); }

    SetEnvironmentVariableW(L"KEEL_SECONDARY", L"1");

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(ldr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        wchar_t msg[4400];
        swprintf_s(msg, L"keelexp err cannot start the Win7 shell through keelldr (error %lu)\n\n%s",
                   GetLastError(), cmd);
        MessageBoxW(nullptr, msg, L"Keel", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
