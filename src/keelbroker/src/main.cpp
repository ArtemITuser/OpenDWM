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
#include <cstdint>

typedef LONG NTSTATUS;
#define NT_OK(s) ((s) >= 0)

typedef struct _UNICODE_STRING { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length; HANDLE RootDirectory; PUNICODE_STRING ObjectName;
    ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _SEC_QOS {
    ULONG Length; int ImpersonationLevel; BOOLEAN ContextTrackingMode; BOOLEAN EffectiveOnly;
} SEC_QOS;

typedef struct _ALPC_PORT_ATTRIBUTES {
    ULONG Flags;
    SEC_QOS SecurityQos;
    SIZE_T MaxMessageLength;
    SIZE_T MemoryBandwidth;
    SIZE_T MaxPoolUsage;
    SIZE_T MaxSectionSize;
    SIZE_T MaxViewSize;
    SIZE_T MaxTotalSectionSize;
    ULONG DupObjectTypes;
    ULONG Reserved;
} ALPC_PORT_ATTRIBUTES;

typedef NTSTATUS (NTAPI *PFN_NtAlpcCreatePort)(PHANDLE, POBJECT_ATTRIBUTES, ALPC_PORT_ATTRIBUTES*);
typedef NTSTATUS (NTAPI *PFN_NtUserRegisterSessionPort)(HANDLE);
typedef NTSTATUS (NTAPI *PFN_NtUserUnregisterSessionPort)(void);

#define IOCTL_KEEL_DWM_QUERY 0x222004
#pragma pack(push,1)
typedef struct { unsigned long long base, addr, val; unsigned long pid; long st;
                 unsigned long callerPid, callerSession; } KEEL_DWM_STATE;
#pragma pack(pop)
static bool keel_read_gpepdwm(KEEL_DWM_STATE* out)
{
    HANDLE h = CreateFileW(L"\\\\.\\Keel", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { wprintf(L"[!] open \\\\.\\Keel failed err=%lu\n", GetLastError()); return false; }
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_KEEL_DWM_QUERY, nullptr, 0, out, sizeof(*out), &ret, nullptr);
    CloseHandle(h);
    if (!ok) { wprintf(L"[!] DWM_QUERY failed err=%lu\n", GetLastError()); return false; }
    return true;
}

static PFN_NtAlpcCreatePort            pNtAlpcCreatePort;
static PFN_NtUserRegisterSessionPort   pRegister;
static PFN_NtUserUnregisterSessionPort pUnregister;

static void report_station()
{

    wchar_t name[256];
    DWORD n = 0;
    HWINSTA ws = GetProcessWindowStation();
    if (ws && GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &n)) wprintf(L"[i] window station = %s\n", name);
    HDESK dk = GetThreadDesktop(GetCurrentThreadId());
    n = 0;
    if (dk && GetUserObjectInformationW(dk, UOI_NAME, name, sizeof(name), &n)) wprintf(L"[i] desktop        = %s\n", name);
}

static void connect_win32k()
{
    HMODULE u32 = LoadLibraryW(L"user32.dll");
    (void)u32;
    HWND d = GetDesktopWindow();
    HDC dc = GetDC(nullptr); if (dc) ReleaseDC(nullptr, dc);
    wprintf(L"[+] win32k connected to desktop hwnd=%p\n", d);
    report_station();
}

static bool resolve()
{
    HMODULE ntdll  = GetModuleHandleW(L"ntdll.dll");
    HMODULE win32u = LoadLibraryW(L"win32u.dll");
    if (!ntdll || !win32u) { wprintf(L"[!] module load failiure\n"); return false; }
    pNtAlpcCreatePort = (PFN_NtAlpcCreatePort)GetProcAddress(ntdll, "NtAlpcCreatePort");
    pRegister   = (PFN_NtUserRegisterSessionPort)  GetProcAddress(win32u, "NtUserRegisterSessionPort");
    pUnregister = (PFN_NtUserUnregisterSessionPort)GetProcAddress(win32u, "NtUserUnregisterSessionPort");
    if (!pNtAlpcCreatePort || !pRegister || !pUnregister) {
        wprintf(L"[!] resolve failed AlpcCreate=%p Register=%p Unregister=%p\n",
                pNtAlpcCreatePort, pRegister, pUnregister);
        return false;
    }
    return true;
}

static HANDLE create_alpc_port()
{
    HANDLE port = nullptr;
    OBJECT_ATTRIBUTES oa; ZeroMemory(&oa, sizeof(oa)); oa.Length = sizeof(oa);
    ALPC_PORT_ATTRIBUTES pa; ZeroMemory(&pa, sizeof(pa));
    pa.MaxMessageLength = 0x1000;
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = 2 ;
    NTSTATUS st = pNtAlpcCreatePort(&port, &oa, &pa);
    if (!NT_OK(st)) { wprintf(L"[!] NtAlpcCreatePort -> 0x%08X\n", (unsigned)st); return nullptr; }
    wprintf(L"[+] ALPC port handle = %p\n", port);
    return port;
}

static NTSTATUS safe_register(HANDLE port, DWORD* excOut)
{
    NTSTATUS st = (NTSTATUS)0xDEADBEEF;
    *excOut = 0;
    __try { st = pRegister(port); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *excOut = GetExceptionCode(); }
    return st;
}

static const wchar_t* status_note(NTSTATUS st)
{
    switch ((unsigned)st) {
        case 0x00000000: return L"STATUS_SUCCESS (this process is now the registered compositor)";
        case 0xC0000038: return L"STATUS_DEVICE_ALREADY_ATTACHED (another process already owns g_pepDwm)";
        case 0xC0000022: return L"STATUS_ACCESS_DENIED (security identifier or session check failed)";
        case 0xC000000D: return L"STATUS_INVALID_PARAMETER";
        default:         return L"(view ntstatus.h)";
    }
}

static int do_register(bool hold)
{
    HANDLE port = create_alpc_port();
    if (!port) return 2;
    DWORD exc = 0;
    NTSTATUS st = safe_register(port, &exc);
    if (exc) { wprintf(L"[!] register faulted with exception 0x%08X\n", exc); CloseHandle(port); return 4; }
    wprintf(L"[=] NtUserRegisterSessionPort -> 0x%08X  %s\n", (unsigned)st, status_note(st));
    if (!NT_OK(st)) { CloseHandle(port); return 3; }
    wprintf(L"[+] registered. pid=%lu\n", GetCurrentProcessId());
    if (hold) {
        wprintf(L"[.] holding the port open (Ctrl-C to release) so win32k now treats this pid as the DWM.\n");
        for (;;) Sleep(1000);
    }
    return 0;
}

static int do_unregister()
{
    NTSTATUS st = pUnregister();
    wprintf(L"[=] NtUserUnregisterSessionPort -> 0x%08X\n", (unsigned)st);
    return NT_OK(st) ? 0 : 3;
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const wchar_t* mode = (argc >= 2) ? argv[1] : L"query";
    DWORD sess = (DWORD)-1; ProcessIdToSessionId(GetCurrentProcessId(), &sess);
    wprintf(L"keelbroker as compositor registration broker (pid=%lu, session=%lu, mode=%s)\n",
            GetCurrentProcessId(), sess, mode);
    if (!resolve()) return 1;
    connect_win32k();

    if (_wcsicmp(mode, L"regcheck") == 0) {

        KEEL_DWM_STATE s; ZeroMemory(&s, sizeof(s));
        if (keel_read_gpepdwm(&s))
            wprintf(L"[k] BEFORE keeldrv g_pepDwm val=0x%llX ownerPid=%lu (readSt=0x%08X) [keeldrv ran as pid=%lu session=%lu]\n",
                    s.val, s.pid, (unsigned)s.st, s.callerPid, s.callerSession);
        HANDLE port = create_alpc_port();
        if (!port) return 2;
        DWORD exc = 0;
        NTSTATUS st = safe_register(port, &exc);
        if (exc) { wprintf(L"[!] register faulted 0x%08X\n", exc); return 4; }
        wprintf(L"[=] NtUserRegisterSessionPort -> 0x%08X  %s\n", (unsigned)st, status_note(st));
        ZeroMemory(&s, sizeof(s));
        if (keel_read_gpepdwm(&s))
            wprintf(L"[k] AFTER  keeldrv g_pepDwm val=0x%llX ownerPid=%lu (readSt=0x%08X) [keeldrv ran as pid=%lu session=%lu]  (my pid=%lu session=%lu)\n",
                    s.val, s.pid, (unsigned)s.st, s.callerPid, s.callerSession, GetCurrentProcessId(), sess);
        if (NT_OK(st)) { wprintf(L"[.] holding registration keeldrv should view my pid. Ctrl-C to release.\n");
                         for (;;) Sleep(1000); }
        CloseHandle(port);
        return 0;
    }
    if (_wcsicmp(mode, L"info") == 0) {

        wprintf(L"[i] info syscalls have resolved and thread connected to win32k with no attempted registration.\n");
        return 0;
    }
    if (_wcsicmp(mode, L"query") == 0) {

        HANDLE port = create_alpc_port();
        if (!port) return 2;
        DWORD exc = 0;
        NTSTATUS st = safe_register(port, &exc);
        if (exc) wprintf(L"[!] probe faulted with exception 0x%08X\n", exc);
        else {
            wprintf(L"[=] probe NtUserRegisterSessionPort -> 0x%08X  %s\n", (unsigned)st, status_note(st));
            if (NT_OK(st)) { wprintf(L"[i] unexpectedly registered so releasing.\n"); pUnregister(); }
        }
        CloseHandle(port);
        return 0;
    }
    if (_wcsicmp(mode, L"register")   == 0) return do_register(true);
    if (_wcsicmp(mode, L"register1")  == 0) return do_register(false);
    if (_wcsicmp(mode, L"unregister") == 0) return do_unregister();
    if (_wcsicmp(mode, L"cycle")      == 0) { do_unregister(); return do_register(true); }

    wprintf(L"keelbroker [query|register|register1|unregister|cycle]\n");
    return 1;
}
