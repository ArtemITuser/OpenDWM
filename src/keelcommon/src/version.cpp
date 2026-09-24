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

#include "keel/version.h"

#include <winternl.h>

namespace keel {

OsVersion GetHostVersion() {
    using RtlGetVersionFn = NTSTATUS(NTAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            fn(&vi);
        }
    }
    DWORD ubr = 0;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD size = sizeof(ubr), type = 0;
        RegQueryValueExW(key, L"UBR", nullptr, &type, reinterpret_cast<LPBYTE>(&ubr), &size);
        RegCloseKey(key);
    }
    return {vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber, ubr};
}

bool HostIsPinned() {
    const auto v = GetHostVersion();
    if (v.build == kPinnedHostBuild) return true;
    wchar_t buf[8];
    return GetEnvironmentVariableW(L"KEEL_ALLOW_UNPINNED_HOST", buf, 8) > 0 && buf[0] == L'1';
}

bool GetFileVersion(const wchar_t* path, DWORD& major, DWORD& minor, DWORD& build, DWORD& rev) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) return false;
    auto data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, size));
    if (!data) return false;
    bool ok = false;
    if (GetFileVersionInfoW(path, 0, size, data)) {
        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT len = 0;
        if (VerQueryValueW(data, L"\\", reinterpret_cast<LPVOID*>(&ffi), &len) && ffi) {
            major = HIWORD(ffi->dwFileVersionMS);
            minor = LOWORD(ffi->dwFileVersionMS);
            build = HIWORD(ffi->dwFileVersionLS);
            rev   = LOWORD(ffi->dwFileVersionLS);
            ok = true;
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    return ok;
}

}
