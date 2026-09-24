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
#include <sddl.h>
#include <cstdio>
#pragma comment(lib, "advapi32.lib")

typedef LONG NTSTATUS;
#define NT_OK(s) ((s) >= 0)
typedef struct _UNICODE_STRING { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length; HANDLE RootDirectory; PUNICODE_STRING ObjectName;
    ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
typedef struct _PORT_MESSAGE {
    union { struct { USHORT DataLength; USHORT TotalLength; } s1; ULONG Length; } u1;
    union { struct { USHORT Type; USHORT DataInfoOffset; } s2; ULONG ZeroInit; } u2;
    union { struct { LONG UniqueProcess; LONG UniqueThread; } ClientId; ULONGLONG DoNotUse; };
    ULONG MessageId;
    union { SIZE_T ClientViewSize; ULONG CallbackId; };
} PORT_MESSAGE, *PPORT_MESSAGE;
typedef struct _PORT_VIEW { ULONG Length; HANDLE SectionHandle; ULONG SectionOffset; SIZE_T ViewSize; PVOID ViewBase; PVOID ViewRemoteBase; } PORT_VIEW, *PPORT_VIEW;
typedef struct _REMOTE_PORT_VIEW { ULONG Length; SIZE_T ViewSize; PVOID ViewBase; } REMOTE_PORT_VIEW, *PREMOTE_PORT_VIEW;

typedef struct _KUXQOS { ULONG Length; int ImpersonationLevel; UCHAR ContextTrackingMode; BOOLEAN EffectiveOnly; } KUXQOS;
typedef NTSTATUS (NTAPI *PFN_NtConnectPort)(PHANDLE, PUNICODE_STRING, KUXQOS*, PPORT_VIEW, PREMOTE_PORT_VIEW, PULONG, PVOID, PULONG);
typedef NTSTATUS (NTAPI *PFN_NtCreatePort)(PHANDLE, POBJECT_ATTRIBUTES, ULONG, ULONG, ULONG);
typedef NTSTATUS (NTAPI *PFN_NtReplyWaitReceivePort)(HANDLE, PVOID*, PPORT_MESSAGE, PPORT_MESSAGE);
typedef NTSTATUS (NTAPI *PFN_NtAcceptConnectPort)(PHANDLE, PVOID, PPORT_MESSAGE, BOOLEAN, PPORT_VIEW, PREMOTE_PORT_VIEW);
typedef NTSTATUS (NTAPI *PFN_NtCompleteConnectPort)(HANDLE);
typedef NTSTATUS (NTAPI *PFN_NtReplyPort)(HANDLE, PPORT_MESSAGE);
typedef VOID (NTAPI *PFN_RtlInitUnicodeString)(PUNICODE_STRING, PCWSTR);

#define LPC_REQUEST 1
#define LPC_CONNECTION_REQUEST 10

static PFN_NtCreatePort            pCreatePort;
static PFN_NtReplyWaitReceivePort  pReplyWaitRecv;
static PFN_NtAcceptConnectPort     pAccept;
static PFN_NtCompleteConnectPort   pComplete;
static PFN_NtReplyPort             pReplyPort;
static PFN_RtlInitUnicodeString    pInit;

static void hexdump(const unsigned char* p, unsigned n) {
    for (unsigned i = 0; i < n; i += 16) {
        wprintf(L"    %04x: ", i);
        for (unsigned j = 0; j < 16 && i+j < n; ++j) wprintf(L"%02x ", p[i+j]);
        wprintf(L"\n");
    }
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    DWORD sess0=(DWORD)-1; ProcessIdToSessionId(GetCurrentProcessId(), &sess0);

    if (argc >= 3 && _wcsicmp(argv[1], L"--connect") == 0) {
        auto pInitS = (PFN_RtlInitUnicodeString)GetProcAddress(nt, "RtlInitUnicodeString");
        auto pConn = (PFN_NtConnectPort)GetProcAddress(nt, "NtConnectPort");
        UNICODE_STRING n; pInitS(&n, argv[2]);
        KUXQOS qos; ZeroMemory(&qos, sizeof(qos)); qos.Length = sizeof(qos); qos.ImpersonationLevel = 2; qos.EffectiveOnly = TRUE;
        HANDLE h = nullptr; ULONG maxMsg = 0;
        NTSTATUS st = pConn(&h, &n, &qos, nullptr, nullptr, &maxMsg, nullptr, nullptr);
        wprintf(L"[connect] NtConnectPort('%s') from session %lu -> 0x%08X (handle=%p)\n", argv[2], sess0, (unsigned)st, h);
        return 0;
    }

    if (argc >= 2 && _wcsicmp(argv[1], L"--alias") == 0) {
        typedef NTSTATUS (NTAPI *PFN_NtCreateSymbolicLinkObject)(PHANDLE, ULONG, POBJECT_ATTRIBUTES, PUNICODE_STRING);
        auto pInitS = (PFN_RtlInitUnicodeString)GetProcAddress(nt, "RtlInitUnicodeString");
        auto pLink  = (PFN_NtCreateSymbolicLinkObject)GetProcAddress(nt, "NtCreateSymbolicLinkObject");
        if (!pInitS || !pLink) { wprintf(L"[alias] ntdll entry points missing\n"); return 1; }

        HANDLE tok = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
            TOKEN_PRIVILEGES tp{}; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            LookupPrivilegeValueW(nullptr, L"SeCreatePermanentPrivilege", &tp.Privileges[0].Luid);
            AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
            wprintf(L"[alias] SeCreatePermanentPrivilege enable -> %lu\n", GetLastError());
            CloseHandle(tok);
        }

        PSECURITY_DESCRIPTOR psd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GR;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)",
                                                                  SDDL_REVISION_1, &psd, nullptr)) {
            wprintf(L"[alias] SDDL failed %lu\n", GetLastError()); return 1;
        }
        const wchar_t* linkName   = (argc >= 3) ? argv[2] : L"\\UxSmsApiPort";
        const wchar_t* targetName = (argc >= 4) ? argv[3] : L"\\RPC Control\\UxSmsApiPort";
        UNICODE_STRING ln, tn; pInitS(&ln, linkName); pInitS(&tn, targetName);
        OBJECT_ATTRIBUTES oa; ZeroMemory(&oa, sizeof(oa));
        oa.Length = sizeof(oa); oa.ObjectName = &ln; oa.SecurityDescriptor = psd;
        oa.Attributes = 0x00000010  | 0x00000040 ;
        HANDLE hLink = nullptr;
        NTSTATUS st = pLink(&hLink, 0x000F0001 , &oa, &tn);
        if (st == (NTSTATUS)0xC0000035 ) {
            wprintf(L"[alias] '%s' already exists (session %lu) so left as is\n", linkName, sess0);
            LocalFree(psd); return 0;
        }
        wprintf(L"[alias] NtCreateSymbolicLinkObject('%s' -> '%s') from session %lu -> 0x%08X\n",
                linkName, targetName, sess0, (unsigned)st);
        if (hLink) CloseHandle(hLink);
        LocalFree(psd);
        return NT_OK(st) ? 0 : 3;
    }

    const wchar_t* portName = (argc >= 2) ? argv[1] : L"\\UxSmsApiPort";
    DWORD sess=(DWORD)-1; ProcessIdToSessionId(GetCurrentProcessId(), &sess);
    wprintf(L"keeluxsms (LPC) pid=%lu session=%lu port=%s\n", GetCurrentProcessId(), sess, portName);

    pCreatePort   = (PFN_NtCreatePort)GetProcAddress(nt, "NtCreatePort");
    pReplyWaitRecv= (PFN_NtReplyWaitReceivePort)GetProcAddress(nt, "NtReplyWaitReceivePort");
    pAccept       = (PFN_NtAcceptConnectPort)GetProcAddress(nt, "NtAcceptConnectPort");
    pComplete     = (PFN_NtCompleteConnectPort)GetProcAddress(nt, "NtCompleteConnectPort");
    pReplyPort    = (PFN_NtReplyPort)GetProcAddress(nt, "NtReplyPort");
    pInit         = (PFN_RtlInitUnicodeString)GetProcAddress(nt, "RtlInitUnicodeString");
    if (!pCreatePort || !pReplyWaitRecv || !pAccept || !pComplete) { wprintf(L"[!] resolve failed\n"); return 1; }

    SECURITY_DESCRIPTOR sd; InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);

    UNICODE_STRING name; pInit(&name, portName);
    OBJECT_ATTRIBUTES oa; ZeroMemory(&oa, sizeof(oa));
    oa.Length = sizeof(oa); oa.ObjectName = &name; oa.SecurityDescriptor = &sd;

    HANDLE port = nullptr;
    NTSTATUS st = pCreatePort(&port, &oa, 512, 0x148, 0);
    if (!NT_OK(st)) { wprintf(L"[!] NtCreatePort('%s') -> 0x%08X\n", portName, (unsigned)st); return 2; }
    wprintf(L"[+] created LPC port '%s' handle=%p. Waiting for dwm...\n", portName, port);

    unsigned char buf[0x400];
    HANDLE client = nullptr;
    for (;;) {
        PPORT_MESSAGE msg = (PPORT_MESSAGE)buf; PVOID ctx = nullptr;
        ZeroMemory(buf, sizeof(buf));
        st = pReplyWaitRecv(port, &ctx, nullptr, msg);
        if (!NT_OK(st)) { wprintf(L"[!] ReplyWaitReceive -> 0x%08X\n", (unsigned)st); Sleep(200); continue; }
        USHORT type = msg->u2.s2.Type & 0xFF, dl = msg->u1.s1.DataLength;
        wprintf(L"\n[msg] type=%u dataLen=%u\n", type, dl);
        if (dl && dl <= 0x200) hexdump(buf + sizeof(PORT_MESSAGE), dl);
        if (type == LPC_CONNECTION_REQUEST) {
            st = pAccept(&client, nullptr, msg, TRUE, nullptr, nullptr);
            wprintf(L"[=] NtAcceptConnectPort -> 0x%08X client=%p\n", (unsigned)st, client);
            if (NT_OK(st)) { NTSTATUS c = pComplete(client); wprintf(L"[=] NtCompleteConnectPort -> 0x%08X\n", (unsigned)c); }
        } else if (type == LPC_REQUEST) {

            PPORT_MESSAGE reply = msg;
            reply->u1.s1.DataLength = dl;
            reply->u1.s1.TotalLength = (USHORT)(sizeof(PORT_MESSAGE) + dl);
            ZeroMemory(buf + sizeof(PORT_MESSAGE), dl);
            NTSTATUS r = pReplyPort(client ? client : port, reply);
            wprintf(L"[=] NtReplyPort -> 0x%08X\n", (unsigned)r);
        }
    }
}
