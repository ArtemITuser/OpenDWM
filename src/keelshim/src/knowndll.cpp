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
#include <winternl.h>

#include <algorithm>
#include <string>
#include <vector>

#include <detours.h>

#include "keel/log.h"

#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif

namespace keelshim {

namespace {

using NtOpenSectionFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
NtOpenSectionFn g_realNtOpenSection = nullptr;

std::vector<std::wstring> g_redirect;

std::wstring LowerLeaf(const UNICODE_STRING* name) {
    if (!name || !name->Buffer || name->Length == 0) return L"";
    std::wstring s(name->Buffer, name->Length / sizeof(wchar_t));
    const size_t slash = s.find_last_of(L'\\');
    std::wstring leaf = (slash == std::wstring::npos) ? s : s.substr(slash + 1);
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), towlower);
    return leaf;
}

NTSTATUS NTAPI HookedNtOpenSection(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attrs) {

    if (attrs && attrs->ObjectName) {
        const std::wstring leaf = LowerLeaf(attrs->ObjectName);
        for (const auto& r : g_redirect) {
            if (leaf == r) {
                KEEL_TRACE(L"KnownDLL redirect %s -> donor search", leaf.c_str());
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }
        }
    }
    return g_realNtOpenSection(handle, access, attrs);
}

}

void LoadRedirectSet() {
    g_redirect.clear();
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(L"KEEL_REDIRECT", buf, 1024);
    std::wstring list = (n > 0 && n < 1024) ? buf : L"shell32.dll;shlwapi.dll;uxtheme.dll";
    size_t start = 0;
    while (start < list.size()) {
        size_t sep = list.find(L';', start);
        if (sep == std::wstring::npos) sep = list.size();
        std::wstring item = list.substr(start, sep - start);
        std::transform(item.begin(), item.end(), item.begin(), towlower);
        if (!item.empty()) g_redirect.push_back(item);
        start = sep + 1;
    }
}

DWORD InstallKnownDllRedirection() {
    LoadRedirectSet();
    if (g_redirect.empty()) return ERROR_SUCCESS;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    g_realNtOpenSection = reinterpret_cast<NtOpenSectionFn>(GetProcAddress(ntdll, "NtOpenSection"));
    if (!g_realNtOpenSection) return ERROR_PROC_NOT_FOUND;

    LONG err = DetourTransactionBegin();
    if (err != NO_ERROR) return static_cast<DWORD>(err);
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&g_realNtOpenSection), reinterpret_cast<PVOID>(HookedNtOpenSection));
    err = DetourTransactionCommit();
    if (err != NO_ERROR) return static_cast<DWORD>(err);

    std::wstring names;
    for (const auto& r : g_redirect) { names += r; names += L' '; }
    KEEL_INFO(L"KnownDLL redirection active for %s", names.c_str());
    return ERROR_SUCCESS;
}

void SetupDonorSearch() {
    wchar_t donor[MAX_PATH];
    if (GetEnvironmentVariableW(L"KEEL_DONOR", donor, MAX_PATH) == 0) return;
    SetDllDirectoryW(donor);

    if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll")) {
        // LOAD_LIBRARY_SEARCH_DEFAULT_DIRS ignores SetDllDirectory so applet dependencies resolve Win10 System32 without this
        using AddDllDirFn = void*(WINAPI*)(PCWSTR);
        if (auto add = reinterpret_cast<AddDllDirFn>(GetProcAddress(k32, "AddDllDirectory"))) {
            if (add(donor)) KEEL_INFO(L"donor '%s' added to the LOAD_LIBRARY_SEARCH_USER_DIRS set", donor);
            else            KEEL_WARN(L"AddDllDirectory('%s') failed (%lu)", donor, GetLastError());
        }
    }

    WIN32_FIND_DATAW fd;
    std::wstring pat = std::wstring(donor) + L"\\api-ms-*.dll";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring full = std::wstring(donor) + L"\\" + fd.cFileName;
            if (LoadLibraryExW(full.c_str(), nullptr, 0)) ++n;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (n) KEEL_INFO(L"preloaded %d api-set shim(s) from %s", n, donor);
}

}
