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

static void StubLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    buf[n] = '\r'; buf[n + 1] = '\n';
    HANDLE h = CreateFileA("C:\\Keel\\keelstub.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, (DWORD)(n + 2), &w, NULL);
    CloseHandle(h);
}

typedef LONG(WINAPI* PFN_VOID)(void);
static PFN_VOID g_pfnKernelStartup;
static PFN_VOID g_pfnKernelShutdown;
static PFN_VOID g_pfnSignalStartComplete;
static BOOL     g_resolved;

static void ResolveWin32u(void) {
    if (g_resolved) return;
    g_resolved = TRUE;
    HMODULE w = GetModuleHandleW(L"win32u.dll");
    if (!w) w = LoadLibraryW(L"win32u.dll");
    if (!w) { StubLog("keelstub; win32u.dll NOT AVAILABLE"); return; }
    g_pfnKernelStartup  = (PFN_VOID)GetProcAddress(w, "NtUserDwmKernelStartup");
    g_pfnKernelShutdown = (PFN_VOID)GetProcAddress(w, "NtUserDwmKernelShutdown");
    g_pfnSignalStartComplete = (PFN_VOID)GetProcAddress(w, "NtUserSignalRedirectionStartComplete");
    StubLog("keelstub; win32u=%p KernelStartup=%p KernelShutdown=%p SignalStartComplete=%p",
            w, g_pfnKernelStartup, g_pfnKernelShutdown, g_pfnSignalStartComplete);
}

BOOL __stdcall DwmStartRedirection(DWORD flags) {
    ResolveWin32u();
    LONG st = 0;
    if (g_pfnKernelStartup) {
        SetLastError(0);
        st = g_pfnKernelStartup();
        StubLog("keelstub; DwmStartRedirection(%lu) -> NtUserDwmKernelStartup() = 0x%08lX (lasterr=%lu)",
                flags, (unsigned long)st, GetLastError());

        if (g_pfnSignalStartComplete) {
            SetLastError(0);
            LONG sg = g_pfnSignalStartComplete();
            StubLog("keelstub; NtUserSignalRedirectionStartComplete() = 0x%08lX (lasterr=%lu)",
                    (unsigned long)sg, GetLastError());
        } else {
            StubLog("keelstub; NtUserSignalRedirectionStartComplete unavailable");
        }
    } else {
        StubLog("keelstub; DwmStartRedirection(%lu) with NtUserDwmKernelStartup unavailable", flags);
    }
    SetLastError(0);
    return TRUE;
}

BOOL __stdcall DwmStopRedirection(void) {
    ResolveWin32u();
    if (g_pfnKernelShutdown) {
        SetLastError(0);
        LONG st = g_pfnKernelShutdown();
        StubLog("keelstub; DwmStopRedirection -> NtUserDwmKernelShutdown() = 0x%08lX", (unsigned long)st);
    }
    SetLastError(0);
    return TRUE;
}

BOOL __stdcall CheckDesktopByThreadId(DWORD threadId) {
    (void)threadId;
    return TRUE;
}

BOOL __stdcall UpdateWindowTransform(void) {
    return TRUE;
}

static void SfmLog(const char* name) {
    static LONG count;
    if (InterlockedIncrement(&count) <= 40) StubLog("keelstub; %s called (not yet translated)", name);
}
#define SFM_STUB(fn) long __stdcall fn(void) { SfmLog(#fn); return (long)0x80004001; }
SFM_STUB(SfmDxOpenSwapChain)
SFM_STUB(SfmDxBindSwapChain)
SFM_STUB(SfmDxReleaseSwapChain)
SFM_STUB(SfmDxQuerySwapChainBindingStatus)
SFM_STUB(SfmDxSetSwapChainBindingStatus)
SFM_STUB(SfmDxSetSwapChainStats)
SFM_STUB(SfmDxReportPendingBindingsToDwm)
