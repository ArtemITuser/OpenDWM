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

namespace keelshim32 {
void InstallDxgiTranslation(HMODULE dxgi);
void InstallDcompRefusal(HMODULE dcomp);
}

namespace keelshim32 {

bool Verbose() {
    static int cached = -1;
    if (cached < 0) { wchar_t v[8]{}; cached = (GetEnvironmentVariableW(L"KEEL32_VERBOSE", v, 8) > 0 && v[0] == L'1') ? 1 : 0; }
    return cached == 1;
}
void Log(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);

    wchar_t line[640];
    const int n = _snwprintf_s(line, _TRUNCATE, L"[keelshim32 pid=%lu tid=%lu] %s\r\n",
                               GetCurrentProcessId(), GetCurrentThreadId(), msg);
    if (n <= 0) return;

    HANDLE h = CreateFileW(L"C:\\Keel\\native-keel32.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char utf8[1280];
    const int cb = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    if (cb > 0) { DWORD written = 0; WriteFile(h, utf8, static_cast<DWORD>(cb), &written, nullptr); }
    CloseHandle(h);
}

// no Detours in the x86 tree, and an x86 stub's "ret N" must come from the PDB epilogue, not a guess
bool WriteStubRet(BYTE* fn, long hr, BYTE argBytes) {
    BYTE stub[8];
    stub[0] = 0xB8;
    *reinterpret_cast<long*>(stub + 1) = hr;
    stub[5] = 0xC2;
    stub[6] = argBytes;
    stub[7] = 0x00;

    DWORD old = 0;
    if (!VirtualProtect(fn, sizeof(stub), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(fn, stub, sizeof(stub));
    VirtualProtect(fn, sizeof(stub), old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, sizeof(stub));
    return true;
}

bool EpilogueMatches(const BYTE* fn, BYTE expectArgBytes, BYTE* foundArgBytes) {
    for (int i = 0; i < 0x80; ++i) {
        if (fn[i] == 0xC2) {
            *foundArgBytes = fn[i + 1];
            return fn[i + 1] == expectArgBytes && fn[i + 2] == 0x00;
        }
        if (fn[i] == 0xC3) { *foundArgBytes = 0; return false; }
    }
    *foundArgBytes = 0xFF;
    return false;
}
}

namespace {

using keelshim32::Log;
using keelshim32::Verbose;
using keelshim32::WriteStubRet;
using keelshim32::EpilogueMatches;

constexpr long kENotImpl = 0x80004001L;

struct StubTarget {
    const char* name;
    BYTE argBytes;
};

constexpr StubTarget kTargets[] = {
    { "TextInputHostCreate", 0x0C },
    { "TsfOneCreate",        0x10 },
};

bool WriteStub(BYTE* fn, BYTE argBytes) { return WriteStubRet(fn, kENotImpl, argBytes); }

volatile LONG g_patched = 0;

void PatchTextInputFramework(HMODULE mod) {
    if (!mod) return;
    if (InterlockedCompareExchange(&g_patched, 1, 0) != 0) return;

    for (const StubTarget& t : kTargets) {
        auto fn = reinterpret_cast<BYTE*>(GetProcAddress(mod, t.name));
        if (!fn) { Log(L"%S not exported so skipped", t.name); continue; }

        BYTE found = 0;
        if (!EpilogueMatches(fn, t.argBytes, &found)) {
            Log(L"%S at %p epilogue is ret 0x%02X, expected ret 0x%02X so NOT patching",
                t.name, fn, found, t.argBytes);
            continue;
        }
        if (WriteStub(fn, t.argBytes)) Log(L"%S at %p stubbed -> E_NOTIMPL (ret 0x%02X)", t.name, fn, t.argBytes);
        else                           Log(L"%S at %p VirtualProtect failed, err=%lu", t.name, fn, GetLastError());
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
            keelshim32::InstallDxgiTranslation(static_cast<HMODULE>(data->DllBase));
        else if (_wcsicmp(name, L"dcomp.dll") == 0)
            keelshim32::InstallDcompRefusal(static_cast<HMODULE>(data->DllBase));
    } __except (EXCEPTION_EXECUTE_HANDLER) {  }
}

void Install() {
    if (Verbose()) {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
        Log(L"attached to '%s'", path);
    }

    keelshim32::InstallDxgiTranslation(GetModuleHandleW(L"dxgi.dll"));
    keelshim32::InstallDcompRefusal(GetModuleHandleW(L"dcomp.dll"));

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
