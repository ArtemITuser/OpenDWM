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

#define KEELSHIM_EXPORTS
#include "keelshim/shim.h"

#include <cwctype>

#include "keel/log.h"
#include "keel/version.h"

namespace keelshim {
DWORD InstallHooks(DWORD& installed);
DWORD InstallKnownDllRedirection();
void  SetupDonorSearch();
void  RegisterAsSessionCompositor();
void  InstallRedirTrace();
}

namespace {
KeelShimStatus g_status{sizeof(KeelShimStatus)};

bool ThisIsTheCompositor() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const wchar_t* leaf = wcsrchr(path, L'\\');
    leaf = leaf ? leaf + 1 : path;
    return _wcsicmp(leaf, L"keeldwm.exe") == 0 || _wcsicmp(leaf, L"dwm.exe") == 0;
}

void ReportDisplayGeometry() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    HMODULE shc = GetModuleHandleW(L"shcore.dll");
    if (!u32) return;

    using GetThreadCtxFn = HANDLE (WINAPI*)();
    using GetAwareFromCtxFn = int (WINAPI*)(HANDLE);
    using SetCtxFn = BOOL (WINAPI*)(HANDLE);
    using SetAwarenessFn = HRESULT (WINAPI*)(int);
    using SetDpiAwareFn = BOOL (WINAPI*)();
    auto getCtx   = reinterpret_cast<GetThreadCtxFn>(GetProcAddress(u32, "GetThreadDpiAwarenessContext"));
    auto fromCtx  = reinterpret_cast<GetAwareFromCtxFn>(GetProcAddress(u32, "GetAwarenessFromDpiAwarenessContext"));
    auto setCtx   = reinterpret_cast<SetCtxFn>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
    auto setAware = shc ? reinterpret_cast<SetAwarenessFn>(GetProcAddress(shc, "SetProcessDpiAwareness")) : nullptr;
    auto setLegacy= reinterpret_cast<SetDpiAwareFn>(GetProcAddress(u32, "SetProcessDPIAware"));

    auto awareness = [&]() -> int { return (getCtx && fromCtx) ? fromCtx(getCtx()) : -2; };
    const int before = awareness();

    (void)setCtx; (void)setAware; (void)setLegacy;

    const int cxLogical = GetSystemMetrics(SM_CXSCREEN), cyLogical = GetSystemMetrics(SM_CYSCREEN);
    int cxPhysical = 0, cyPhysical = 0, logPixels = 0;
    if (HDC dc = GetDC(nullptr)) {
        cxPhysical = GetDeviceCaps(dc, DESKTOPHORZRES);
        cyPhysical = GetDeviceCaps(dc, DESKTOPVERTRES);
        logPixels  = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
    }
    KEEL_INFO(L"dpi; awareness=%d (0 unaware, 1 system, 2 per-monitor) logical=%dx%d physical=%dx%d LOGPIXELSX=%d",
              before, cxLogical, cyLogical, cxPhysical, cyPhysical, logPixels);

    if (logPixels && logPixels != 96 && ThisIsTheCompositor()) {
        KEEL_WARN(L"dpi; the COMPOSITOR sees %d DPI not 96 the desktop will be composed "
                  L"x%.2f too large and clipped, gdi32!GetDeviceCaps hook is not in such case"
                  L"in effect (KEEL_NO_DPIFIX=1, or detour failiure see the 'dpi' line above).",
                  logPixels, logPixels / 96.0);
    }
}

void PatchWin7Version() {
    auto peb = reinterpret_cast<BYTE*>(__readgsqword(0x60));
    if (!peb) return;
    DWORD old = 0;
    if (VirtualProtect(peb + 0x118, 0x10, PAGE_READWRITE, &old)) {
        *reinterpret_cast<ULONG*>(peb + 0x118) = 6;
        *reinterpret_cast<ULONG*>(peb + 0x11C) = 1;
        *reinterpret_cast<USHORT*>(peb + 0x120) = 7601;
        *reinterpret_cast<USHORT*>(peb + 0x122) = 0x0100;
        VirtualProtect(peb + 0x118, 0x10, old, &old);
        KEEL_INFO(L"PEB version set as 6.1.7601 SP1");
    } else {
        KEEL_WARN(L"could not set PEB version writable: %lu", GetLastError());
    }
}
}

extern "C" KEELSHIM_API DWORD WINAPI KeelShimInitialize(void) {
    if (g_status.initialized) return ERROR_SUCCESS;
    keel::LogInit(L"keelshim");
    const auto v = keel::GetHostVersion();
    g_status.hostBuild = v.build;
    KEEL_INFO(L"initializing in host build %lu.%lu (pinned=%d)", v.build, v.ubr, keel::HostIsPinned() ? 1 : 0);
    if (!keel::HostIsPinned()) {
        KEEL_ERROR(L"host build %lu is not the pinned build %lu; set KEEL_ALLOW_UNPINNED_HOST=1 to force build (might work)", v.build, keel::kPinnedHostBuild);
        return ERROR_NOT_SUPPORTED;
    }

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
        using NtSetInfoProcFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
        if (auto fn = reinterpret_cast<NtSetInfoProcFn>(GetProcAddress(ntdll, "NtSetInformationProcess"))) {
            ULONG hardErrorMode = 0;
            const ULONG ProcessDefaultHardErrorMode = 12;
            fn(reinterpret_cast<HANDLE>(-1), ProcessDefaultHardErrorMode, &hardErrorMode, sizeof(hardErrorMode));
        }
    }
    PatchWin7Version();
    keelshim::SetupDonorSearch();
    const DWORD kd = keelshim::InstallKnownDllRedirection();
    if (kd != ERROR_SUCCESS) KEEL_WARN(L"KnownDLL redirection not installed: %lu", kd);
    const DWORD err = keelshim::InstallHooks(g_status.hooksInstalled);
    if (err != ERROR_SUCCESS) {
        KEEL_ERROR(L"hook installation failed: %lu", err);
        return err;
    }

    ReportDisplayGeometry();

    {
        wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        for (wchar_t* s = exe; *s; ++s) *s = (wchar_t)towlower(*s);
        if (wcsstr(exe, L"keeldwm.exe") || wcsstr(exe, L"\\dwm.exe")) {
            HMODULE u = LoadLibraryExW(L"C:\\Keel\\rtm\\uxtheme.dll", nullptr, 0);
            wchar_t up[MAX_PATH]{}; if (u) GetModuleFileNameW(u, up, MAX_PATH);
            KEEL_INFO(L"force Win7 uxtheme load -> %p (%s)", u, u ? up : L"FAILED");
        }

        if (wcsstr(exe, L"keeldwm.exe") || wcsstr(exe, L"\\dwm.exe")) keelshim::InstallRedirTrace();

    }
    g_status.initialized = TRUE;
    KEEL_INFO(L"ready with %lu hooks, %lu imports patched", g_status.hooksInstalled, g_status.importsPatched);
    return ERROR_SUCCESS;
}

extern "C" KEELSHIM_API BOOL WINAPI KeelShimGetStatus(KeelShimStatus* status) {
    if (!status || status->size < sizeof(KeelShimStatus)) return FALSE;
    *status = g_status;
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        wchar_t v[8];
        if (GetEnvironmentVariableW(L"KEEL_AUTOINIT", v, 8) > 0 && v[0] == L'1') {
            KeelShimInitialize();
        }
    }
    return TRUE;
}
