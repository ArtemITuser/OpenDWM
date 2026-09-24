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
#include <shlwapi.h>

#include <string>
#include <vector>

#include "inject.h"
#include "keel/log.h"
#include "keel/version.h"

namespace {

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return buf;
}

int Usage() {
    fwprintf(stderr, L"keelldr [--donor <dir>] [--shim <dll>] [--wait] <exe> [args...]\n");
    return 2;
}

}

int wmain(int argc, wchar_t** argv) {

    if (HWND con = GetConsoleWindow()) ShowWindow(con, SW_HIDE);
    keel::LogInit(L"keelldr");
    std::wstring donor = ExeDir() + L"\\donor\\cut3";
    std::wstring shim = ExeDir() + L"\\keelshim.dll";
    std::wstring ifeoTarget;
    std::wstring fallback;
    bool wait = false;
    int i = 1;
    for (; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--donor" && i + 1 < argc) donor = argv[++i];
        else if (a == L"--shim" && i + 1 < argc) shim = argv[++i];
        else if (a == L"--ifeo-target" && i + 1 < argc) ifeoTarget = argv[++i];
        else if (a == L"--fallback" && i + 1 < argc) fallback = argv[++i];
        else if (a == L"--wait") wait = true;
        else if (a.rfind(L"--", 0) == 0) return Usage();
        else break;
    }

    if (!ifeoTarget.empty()) {
        if (i < argc) ++i;

        static std::vector<wchar_t*> rebuilt;
        rebuilt.clear();
        rebuilt.push_back(argv[0]);
        rebuilt.push_back(const_cast<wchar_t*>(ifeoTarget.c_str()));
        for (int k = i; k < argc; ++k) rebuilt.push_back(argv[k]);
        argv = rebuilt.data();
        argc = (int)rebuilt.size();
        i = 1;
    }
    if (i >= argc) return Usage();

    const auto v = keel::GetHostVersion();
    KEEL_INFO(L"host %lu.%lu.%lu.%lu pinned=%d donor=%s", v.major, v.minor, v.build, v.ubr, keel::HostIsPinned() ? 1 : 0, donor.c_str());
    if (!keel::HostIsPinned()) {
        fwprintf(stderr, L"keelldr: host build %lu is not the pinned build %lu (set KEEL_ALLOW_UNPINNED_HOST=1 to override)\n", v.build, keel::kPinnedHostBuild);
        return 3;
    }

    std::wstring exe = argv[i];
    if (PathIsRelativeW(exe.c_str())) exe = donor + L"\\" + exe;
    std::wstring cmdline = L"\"" + exe + L"\"";
    for (int k = i + 1; k < argc; ++k) { cmdline += L" "; cmdline += argv[k]; }

    SetEnvironmentVariableW(L"KEEL_DONOR", donor.c_str());
    SetEnvironmentVariableW(L"KEEL_AUTOINIT", L"1");
    wchar_t path[32767];
    const DWORD n = GetEnvironmentVariableW(L"PATH", path, 32767);
    std::wstring newPath = donor + L";" + (n ? path : L"");
    SetEnvironmentVariableW(L"PATH", newPath.c_str());

    PROCESS_INFORMATION pi{};
    const DWORD err = keelldr::LaunchWithShim(exe.c_str(), cmdline.c_str(), donor.c_str(), shim.c_str(), pi);
    if (err) {
        fwprintf(stderr, L"keelldr: launch failed: %lu\n", err);
        return 1;
    }
    DWORD code = 0;
    if (wait || !fallback.empty()) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        KEEL_INFO(L"primary pid %lu exited with 0x%lX", pi.dwProcessId, code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!fallback.empty()) {
        for (;;) {
            std::wstring fcmd = L"\"" + fallback + L"\"";
            STARTUPINFOW si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION fpi{};

            if (CreateProcessW(fallback.c_str(), &fcmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &fpi)) {
                KEEL_INFO(L"fallback dwm launched pid %lu", fpi.dwProcessId);
                WaitForSingleObject(fpi.hProcess, INFINITE);
                GetExitCodeProcess(fpi.hProcess, &code);
                KEEL_INFO(L"fallback dwm pid %lu exited with 0x%lX; relaunching", fpi.dwProcessId, code);
                CloseHandle(fpi.hThread); CloseHandle(fpi.hProcess);
            } else {
                KEEL_INFO(L"fallback launch failed: %lu", GetLastError());
            }
            Sleep(500);
        }
    }
    return static_cast<int>(code);
}
