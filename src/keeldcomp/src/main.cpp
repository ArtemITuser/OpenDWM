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

#include <cstdio>
#include <cstdarg>

// generated for each target build by tools/census/gen_dcomp_seam.py since ordinals are build specific
#include "exports.h"

namespace {

constexpr long kUnsupported = 0x887A0004L;

bool Verbose() {
    static int cached = -1;
    if (cached < 0) { wchar_t v[8]{}; cached = (GetEnvironmentVariableW(L"KEELDCOMP_VERBOSE", v, 8) > 0 && v[0] == L'1') ? 1 : 0; }
    return cached == 1;
}

void Log(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* leaf = wcsrchr(exe, L'\\');
    leaf = leaf ? leaf + 1 : exe;
    wchar_t line[800];
    const int n = _snwprintf_s(line, _TRUNCATE, L"[keeldcomp pid=%lu %s] %s\r\n",
                               GetCurrentProcessId(), leaf, msg);
    if (n <= 0) return;
    HANDLE h = CreateFileW(L"C:\\Keel\\native-keeldcomp.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char utf8[1600];
    const int cb = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    if (cb > 0) { DWORD w = 0; WriteFile(h, utf8, (DWORD)cb, &w, nullptr); }
    CloseHandle(h);
}

using NtOpenProcessTokenFn = LONG(NTAPI*)(HANDLE, ACCESS_MASK, PHANDLE);
using NtQueryInfoTokenFn   = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);

// keep the service as pass-through as refusing DirectComposition process wide puts LogonUI in a WerFault loop with no shell
bool RunningAsSystemOrService() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto open  = nt ? (NtOpenProcessTokenFn)GetProcAddress(nt, "NtOpenProcessToken") : nullptr;
    auto query = nt ? (NtQueryInfoTokenFn)GetProcAddress(nt, "NtQueryInformationToken") : nullptr;
    if (!open || !query) return true;

    HANDLE tok = nullptr;
    if (open((HANDLE)-1, TOKEN_QUERY, &tok) < 0 || !tok) return true;
    BYTE buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    ULONG got = 0;
    const LONG st = query(tok, 1 , buf, sizeof(buf), &got);
    CloseHandle(tok);
    if (st < 0) return true;

    const BYTE* sid = (const BYTE*)((TOKEN_USER*)buf)->User.Sid;
    if (!sid) return true;
    if (sid[1] != 1 || sid[7] != 5 ) return false;
    DWORD rid = 0;
    memcpy(&rid, sid + 8, sizeof(rid));
    return rid == 18  || rid == 19  || rid == 20 ;
}

bool Disabled() {
    static int cached = -1;
    if (cached < 0) { wchar_t v[8]{}; cached = (GetEnvironmentVariableW(L"KEEL_NO_DCOMPSEAM", v, 8) > 0 && v[0] == L'1') ? 1 : 0; }
    return cached == 1;
}

bool RefuseHere() {
    static int cached = -1;
    if (cached < 0) {
        const bool service = RunningAsSystemOrService();
        cached = (Disabled() || service) ? 0 : 1;
        if (service) Log(L"service process DirectComposition passed to dcomp10");
        else if (Disabled()) Log(L"KEEL_NO_DCOMPSEAM=1 passed to dcomp10");
    }
    return cached == 1;
}

using CreateDevFn = HRESULT(WINAPI*)(void*, REFIID, void**);

CreateDevFn Real(const char* name) {
    static HMODULE real = nullptr;
    if (!real) {
        real = LoadLibraryExW(L"dcomp10.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!real) { Log(L"dcomp10.dll could not be loaded (err=%lu) failed to pass", GetLastError()); return nullptr; }
    }
    return (CreateDevFn)GetProcAddress(real, name);
}

HRESULT Dispatch(const char* name, void* device, REFIID iid, void** out) {
    if (!RefuseHere()) {
        CreateDevFn fn = Real(name);
        if (fn) return fn(device, iid, out);
        if (out) *out = nullptr;
        return kUnsupported;
    }
    if (out) *out = nullptr;
    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= 8 || Verbose())
        Log(L"%S refused as no existing DirectComposition", name); // win7 doesnt have DirectComposition
    return kUnsupported;
}

}

extern "C" {

HRESULT WINAPI DCompositionCreateDevice(void* dxgiDevice, REFIID iid, void** out) {
    return Dispatch("DCompositionCreateDevice", dxgiDevice, iid, out);
}
HRESULT WINAPI DCompositionCreateDevice2(void* renderingDevice, REFIID iid, void** out) {
    return Dispatch("DCompositionCreateDevice2", renderingDevice, iid, out);
}
HRESULT WINAPI DCompositionCreateDevice3(void* renderingDevice, REFIID iid, void** out) {
    return Dispatch("DCompositionCreateDevice3", renderingDevice, iid, out);
}

}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(mod);
    return TRUE;
}
