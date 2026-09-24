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
#include "keel/log.h"

namespace keelshim {

bool UxSmsIsPrimary();

namespace {

typedef LONG NTSTATUS;
#define NT_OK(s) ((s) >= 0)

typedef struct _UNICODE_STRING { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length; HANDLE RootDirectory; PUNICODE_STRING ObjectName;
    ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
typedef struct _SEC_QOS { ULONG Length; int ImpersonationLevel; BOOLEAN ContextTrackingMode; BOOLEAN EffectiveOnly; } SEC_QOS;
typedef struct _ALPC_PORT_ATTRIBUTES {
    ULONG Flags; SEC_QOS SecurityQos; SIZE_T MaxMessageLength; SIZE_T MemoryBandwidth;
    SIZE_T MaxPoolUsage; SIZE_T MaxSectionSize; SIZE_T MaxViewSize; SIZE_T MaxTotalSectionSize;
    ULONG DupObjectTypes; ULONG Reserved;
} ALPC_PORT_ATTRIBUTES;

typedef NTSTATUS (NTAPI *PFN_NtAlpcCreatePort)(PHANDLE, POBJECT_ATTRIBUTES, ALPC_PORT_ATTRIBUTES*);
typedef NTSTATUS (NTAPI *PFN_NtUserRegisterSessionPort)(HANDLE);
typedef NTSTATUS (NTAPI *PFN_NtUserUnregisterSessionPort)(void);

#define IOCTL_KEEL_DWM_SETOWNER 0x222008
#pragma pack(push,1)
typedef struct { unsigned long long PrevOwner; unsigned long PrevPid; long St; } KEEL_DWM_SETOWNER_RESULT;
#pragma pack(pop)

HANDLE g_regPort = nullptr;

bool NullDwmOwnerViaDriver() {
    HANDLE h = CreateFileW(L"\\\\.\\Keel", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    ULONG pid = 0; KEEL_DWM_SETOWNER_RESULT out{}; DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_KEEL_DWM_SETOWNER, &pid, sizeof(pid), &out, sizeof(out), &ret, nullptr);
    CloseHandle(h);
    if (ok) KEEL_INFO(L"sessionreg; keeldrv nulled g_pepDwm (prev=0x%llX pid=%lu st=0x%08X)",
                      (unsigned long long)out.PrevOwner, out.PrevPid, (unsigned)out.St);
    return ok != FALSE;
}

DWORD WINAPI RegThread(LPVOID) {
    // only the primary compositor registers, a rival instance would take g_pepDwm from it and then crash
    if (!UxSmsIsPrimary()) { KEEL_INFO(L"sessionreg; not the primary compositor so skipping win32k registration"); return 0; }

    LoadLibraryW(L"user32.dll");
    HWND dw = GetDesktopWindow();
    HDC dc = GetDC(nullptr); if (dc) ReleaseDC(nullptr, dc);
    (void)dw;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE win32u = LoadLibraryW(L"win32u.dll");
    auto pCreate     = reinterpret_cast<PFN_NtAlpcCreatePort>(ntdll  ? GetProcAddress(ntdll,  "NtAlpcCreatePort") : nullptr);
    auto pRegister   = reinterpret_cast<PFN_NtUserRegisterSessionPort>(win32u ? GetProcAddress(win32u, "NtUserRegisterSessionPort") : nullptr);
    auto pUnregister = reinterpret_cast<PFN_NtUserUnregisterSessionPort>(win32u ? GetProcAddress(win32u, "NtUserUnregisterSessionPort") : nullptr);
    if (!pCreate || !pRegister) { KEEL_ERROR(L"sessionreg; resolve failed (AlpcCreate=%p Register=%p)", pCreate, pRegister); return 1; }

    OBJECT_ATTRIBUTES oa{}; oa.Length = sizeof(oa);
    ALPC_PORT_ATTRIBUTES pa{}; pa.MaxMessageLength = 0x1000;
    pa.SecurityQos.Length = sizeof(pa.SecurityQos); pa.SecurityQos.ImpersonationLevel = 2 ;
    NTSTATUS cst = pCreate(&g_regPort, &oa, &pa);
    if (!NT_OK(cst) || !g_regPort) { KEEL_ERROR(L"sessionreg; NtAlpcCreatePort -> 0x%08X", (unsigned)cst); return 2; }

    for (int attempt = 0; attempt < 40; ++attempt) {
        NTSTATUS st = (NTSTATUS)0xDEADBEEF;
        __try { st = pRegister(g_regPort); }
        __except (EXCEPTION_EXECUTE_HANDLER) { st = (NTSTATUS)GetExceptionCode(); KEEL_ERROR(L"sessionreg; RegisterSessionPort faulted 0x%08X", (unsigned)st); }
        if (NT_OK(st)) {
            KEEL_INFO(L"sessionreg; NtUserRegisterSessionPort -> 0x00000000 (keeldwm is now the win32k session DWM with port=%p)", g_regPort);
            return 0;
        }
        // a stale owner still holds g_pepDwm, so unregister and then have keeldrv null it
        if ((unsigned)st == 0xC0000038) {
            KEEL_INFO(L"sessionreg; g_pepDwm already set (0xC0000038) so displacing (attempt %d)", attempt);
            if (pUnregister) { NTSTATUS u = pUnregister(); KEEL_INFO(L"sessionreg; NtUserUnregisterSessionPort -> 0x%08X", (unsigned)u); }
            if (attempt >= 2) NullDwmOwnerViaDriver();
            Sleep(250);
            continue;
        }
        KEEL_ERROR(L"sessionreg; NtUserRegisterSessionPort -> 0x%08X (attempt %d)", (unsigned)st, attempt);
        Sleep(250);
    }
    KEEL_ERROR(L"sessionreg; gave up registering as session compositor");
    return 3;
}

}

void RegisterAsSessionCompositor() {
    HANDLE t = CreateThread(nullptr, 0, RegThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

void RegisterAsSessionCompositorOnce() {
    static LONG done = 0;
    if (InterlockedExchange(&done, 1) == 0) RegisterAsSessionCompositor();
}

}
