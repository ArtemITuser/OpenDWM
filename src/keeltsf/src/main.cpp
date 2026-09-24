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

namespace keeltsf {
void InstallDxgiTranslation(HMODULE dxgi);
void InstallDcompRefusal(HMODULE dcomp);

bool Verbose() {
    static int cached = -1;
    if (cached < 0) { wchar_t v[8]{}; cached = (GetEnvironmentVariableW(L"KEEL64_VERBOSE", v, 8) > 0 && v[0] == L'1') ? 1 : 0; }
    return cached == 1;
}
void Log(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);

    wchar_t line[640];
    const int n = _snwprintf_s(line, _TRUNCATE, L"[keeltsf pid=%lu tid=%lu] %s\r\n",
                               GetCurrentProcessId(), GetCurrentThreadId(), msg);
    if (n <= 0) return;

    HANDLE h = CreateFileW(L"C:\\Keel\\native-keel64.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char utf8[1280];
    const int cb = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    if (cb > 0) { DWORD written = 0; WriteFile(h, utf8, static_cast<DWORD>(cb), &written, nullptr); }
    CloseHandle(h);
}

bool WriteStubHr(BYTE* fn, long hr) {
    BYTE stub[6];
    stub[0] = 0xB8;
    *reinterpret_cast<long*>(stub + 1) = hr;
    stub[5] = 0xC3;

    DWORD old = 0;
    if (!VirtualProtect(fn, sizeof(stub), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(fn, stub, sizeof(stub));
    VirtualProtect(fn, sizeof(stub), old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, sizeof(stub));
    return true;
}
}

namespace {

using keeltsf::Log;
using keeltsf::Verbose;
using keeltsf::WriteStubHr;

constexpr long kENotImpl = 0x80004001L;

constexpr const char* kTargets[] = { "TextInputHostCreate", "TsfOneCreate" };

bool WriteStub(BYTE* fn) { return WriteStubHr(fn, kENotImpl); }

volatile LONG g_patched = 0;

void PatchTextInputFramework(HMODULE mod) {
    if (!mod) return;
    if (InterlockedCompareExchange(&g_patched, 1, 0) != 0) return;

    if (GetModuleHandleW(L"keelshim.dll")) {
        if (Verbose()) Log(L"keelshim.dll present so leaving the TSF stub to it");
        return;
    }

    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(mod, p, MAX_PATH);
    const wchar_t* leaf = wcsrchr(p, L'\\');
    leaf = leaf ? leaf + 1 : p;
    if (_wcsicmp(leaf, L"textinputframework.dll") != 0) return;

    for (const char* name : kTargets) {
        auto fn = reinterpret_cast<BYTE*>(GetProcAddress(mod, name));
        if (!fn) { Log(L"%S not exported so skipped", name); continue; }
        if (WriteStub(fn)) Log(L"%S at %p stubbed -> E_NOTIMPL", name, fn);
        else               Log(L"%S at %p VirtualProtect failed, err=%lu", name, fn, GetLastError());
    }
}

struct UNICODE_STR { USHORT Length, MaximumLength; PWSTR Buffer; };
struct LDR_DLL_NOTIFICATION_DATA {
    ULONG Flags;
    const UNICODE_STR* FullDllName;
    const UNICODE_STR* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
};
using LdrRegisterFn = LONG(NTAPI*)(ULONG, void(CALLBACK*)(ULONG, const LDR_DLL_NOTIFICATION_DATA*, PVOID), PVOID, PVOID*);

void CALLBACK DllNotification(ULONG reason, const LDR_DLL_NOTIFICATION_DATA* data, PVOID) {
    __try {
        if (reason != 1  || !data || !data->BaseDllName) return;
        const UNICODE_STR* n = data->BaseDllName;
        if (!n->Buffer || n->Length == 0) return;
        wchar_t name[64]{};
        const USHORT chars = n->Length / sizeof(wchar_t) < 63 ? n->Length / sizeof(wchar_t) : 63;
        memcpy(name, n->Buffer, chars * sizeof(wchar_t));
        if (_wcsicmp(name, L"textinputframework.dll") == 0)
            PatchTextInputFramework(static_cast<HMODULE>(data->DllBase));

        else if (_wcsicmp(name, L"dxgi.dll") == 0)
            keeltsf::InstallDxgiTranslation(static_cast<HMODULE>(data->DllBase));
        else if (_wcsicmp(name, L"dcomp.dll") == 0)
            keeltsf::InstallDcompRefusal(static_cast<HMODULE>(data->DllBase));
    } __except (EXCEPTION_EXECUTE_HANDLER) {  }
}

void Install() {
    if (Verbose()) {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
        Log(L"attached to '%s'", path);
    }

    keeltsf::InstallDxgiTranslation(GetModuleHandleW(L"dxgi.dll"));
    keeltsf::InstallDcompRefusal(GetModuleHandleW(L"dcomp.dll"));

    if (HMODULE already = GetModuleHandleW(L"textinputframework.dll")) {
        PatchTextInputFramework(already);

    }

    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto reg = nt ? reinterpret_cast<LdrRegisterFn>(GetProcAddress(nt, "LdrRegisterDllNotification")) : nullptr;
    if (!reg) { Log(L"LdrRegisterDllNotification unavailable so TSF stub not armed"); return; }
    PVOID cookie = nullptr;
    const LONG st = reg(0, DllNotification, nullptr, &cookie);
    if (st != 0) Log(L"LdrRegisterDllNotification failed 0x%08lX", st);
}

}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        __try { Install(); } __except (EXCEPTION_EXECUTE_HANDLER) {  }
    }
    return TRUE;
}
