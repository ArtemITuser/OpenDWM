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

#include "inject.h"

#include <detours.h>
#include <string>

#include "keel/log.h"

namespace keelldr {

DWORD LaunchWithShim(const wchar_t* exePath, const wchar_t* commandLine, const wchar_t* workingDir,
                     const wchar_t* dllPath, PROCESS_INFORMATION& pi) {

    char dllA[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, 0, dllPath, -1, dllA, MAX_PATH, nullptr, nullptr)) return GetLastError();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring cmd = commandLine ? commandLine : L"";
    ZeroMemory(&pi, sizeof(pi));

    const BOOL ok = DetourCreateProcessWithDllExW(
        exePath, cmd.empty() ? nullptr : cmd.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_DEFAULT_ERROR_MODE, nullptr, workingDir, &si, &pi, dllA, nullptr);
    if (!ok) {
        const DWORD err = GetLastError();
        KEEL_ERROR(L"DetourCreateProcessWithDllEx(%s) failed: %lu", exePath, err);
        return err;
    }
    KEEL_INFO(L"launched pid %lu with %s", pi.dwProcessId, dllPath);
    ResumeThread(pi.hThread);
    return ERROR_SUCCESS;
}

}
