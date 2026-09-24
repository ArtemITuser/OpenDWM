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
#include <cwchar>
#include "keel/log.h"

namespace keelshim {

namespace {

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
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
    ULONG MessageId;
    ULONG _pad;
    ULONGLONG ClientViewSize;
} PORT_MESSAGE, *PPORT_MESSAGE;
typedef struct _PORT_VIEW { ULONG Length; HANDLE SectionHandle; ULONG SectionOffset; SIZE_T ViewSize; PVOID ViewBase; PVOID ViewRemoteBase; } PORT_VIEW, *PPORT_VIEW;
typedef struct _REMOTE_PORT_VIEW { ULONG Length; SIZE_T ViewSize; PVOID ViewBase; } REMOTE_PORT_VIEW, *PREMOTE_PORT_VIEW;

typedef NTSTATUS (NTAPI *PFN_NtCreatePort)(PHANDLE, POBJECT_ATTRIBUTES, ULONG, ULONG, ULONG);
typedef NTSTATUS (NTAPI *PFN_NtReplyWaitReceivePort)(HANDLE, PVOID*, PPORT_MESSAGE, PPORT_MESSAGE);
typedef NTSTATUS (NTAPI *PFN_NtAcceptConnectPort)(PHANDLE, PVOID, PPORT_MESSAGE, BOOLEAN, PPORT_VIEW, PREMOTE_PORT_VIEW);
typedef NTSTATUS (NTAPI *PFN_NtCompleteConnectPort)(HANDLE);
typedef NTSTATUS (NTAPI *PFN_NtReplyPort)(HANDLE, PPORT_MESSAGE);
typedef VOID (NTAPI *PFN_RtlInitUnicodeString)(PUNICODE_STRING, PCWSTR);

#define LPC_REQUEST 1
#define LPC_CONNECTION_REQUEST 10

PFN_NtCreatePort pCreate; PFN_NtReplyWaitReceivePort pRWR; PFN_NtAcceptConnectPort pAccept;
PFN_NtCompleteConnectPort pComplete; PFN_NtReplyPort pReply; PFN_RtlInitUnicodeString pInit;
HANDLE g_port = nullptr;
HANDLE g_orphanEvent = nullptr;
bool   g_isPrimary = false;

WCHAR  g_sessionPortName[0x3b] = {};
bool   g_haveSessionPort = false;

void dumphex(const unsigned char* p, unsigned n) {
    wchar_t line[128];
    for (unsigned i = 0; i < n; i += 16) {
        int o = swprintf_s(line, 128, L"    %04x: ", i);
        for (unsigned j = 0; j < 16 && i + j < n && o < 120; ++j)
            o += swprintf_s(line + o, (size_t)(128 - o), L"%02x ", p[i+j]);
        keel::Log(keel::Level::Info, L"uxsms %s", line);
    }
}

DWORD WINAPI ServiceThread(LPVOID) {
    unsigned char rbuf[0x400], sbuf[0x400];
    HANDLE client = nullptr;
    PPORT_MESSAGE pendingReply = nullptr;
    for (;;) {
        PPORT_MESSAGE recv = (PPORT_MESSAGE)rbuf; PVOID ctx = nullptr;
        ZeroMemory(rbuf, sizeof(rbuf));
        NTSTATUS st = pRWR(g_port, &ctx, pendingReply, recv);
        pendingReply = nullptr;
        if (!NT_OK(st)) { KEEL_INFO(L"uxsms; ReplyWaitReceive -> 0x%08X", (unsigned)st); Sleep(100); continue; }
        USHORT type = recv->u2.s2.Type & 0xFF, dl = recv->u1.s1.DataLength;
        KEEL_INFO(L"uxsms; msg type=%u dataLen=%u", type, dl);
        if (dl && dl <= 0x200) dumphex(rbuf + sizeof(PORT_MESSAGE), dl);
        if (type == LPC_CONNECTION_REQUEST) {
            st = pAccept(&client, nullptr, recv, TRUE, nullptr, nullptr);
            KEEL_INFO(L"uxsms; NtAcceptConnectPort -> 0x%08X client=%p", (unsigned)st, client);
            if (NT_OK(st)) { NTSTATUS c = pComplete(client); KEEL_INFO(L"uxsms; NtCompleteConnectPort -> 0x%08X", (unsigned)c); }
        } else if (type == LPC_REQUEST) {

            const ULONG opcode = *(const ULONG*)(rbuf + 0x28);
            PPORT_MESSAGE reply = (PPORT_MESSAGE)sbuf; ZeroMemory(sbuf, sizeof(sbuf));
            *reply = *recv;
            reply->u2.ZeroInit = 0;
            *(ULONG*)(sbuf + 0x28) = opcode;
            if (opcode == 1 && dl >= 0x7e) {
                memcpy(g_sessionPortName, rbuf + 0x30, sizeof(g_sessionPortName));
                g_sessionPortName[0x3a] = 0;
                g_haveSessionPort = true;
                reply->u1.s1.DataLength = 0x18;
                reply->u1.s1.TotalLength = (USHORT)(sizeof(PORT_MESSAGE) + 0x18);
                *(LONG*)(sbuf + 0x2c) = 0;
                *(ULONGLONG*)(sbuf + 0x30) = 0;
                *(HANDLE*)(sbuf + 0x38) = g_orphanEvent;
                KEEL_INFO(L"uxsms; dwm registered session port '%s' (orphan handle=%p)", g_sessionPortName, g_orphanEvent);
            } else if (opcode == 2) {
                reply->u1.s1.DataLength = 0x7e;
                reply->u1.s1.TotalLength = (USHORT)(sizeof(PORT_MESSAGE) + 0x7e);
                if (g_haveSessionPort) {
                    *(LONG*)(sbuf + 0x2c) = 0;
                    memcpy(sbuf + 0x30, g_sessionPortName, sizeof(g_sessionPortName));
                } else {
                    *(LONG*)(sbuf + 0x2c) = (LONG)0x80263001;
                }
                KEEL_INFO(L"uxsms; client pid=%p asked for the session port -> %s", recv->UniqueProcess,
                          g_haveSessionPort ? g_sessionPortName : L"(none registered)");
            } else {
                reply->u1.s1.DataLength = 8;
                reply->u1.s1.TotalLength = (USHORT)(sizeof(PORT_MESSAGE) + 8);
                *(LONG*)(sbuf + 0x2c) = (LONG)0x80004001;
                KEEL_INFO(L"uxsms; opcode 0x%08lX not implemented (as on Win7)", opcode);
            }
            pendingReply = reply;
        }
    }
}

}

bool UxSmsIsPrimary() { return g_isPrimary; }

void StartUxSmsServer() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    pCreate  = (PFN_NtCreatePort)GetProcAddress(nt, "NtCreatePort");
    pRWR     = (PFN_NtReplyWaitReceivePort)GetProcAddress(nt, "NtReplyWaitReceivePort");
    pAccept  = (PFN_NtAcceptConnectPort)GetProcAddress(nt, "NtAcceptConnectPort");
    pComplete= (PFN_NtCompleteConnectPort)GetProcAddress(nt, "NtCompleteConnectPort");
    pReply   = (PFN_NtReplyPort)GetProcAddress(nt, "NtReplyPort");
    pInit    = (PFN_RtlInitUnicodeString)GetProcAddress(nt, "RtlInitUnicodeString");
    if (!pCreate || !pRWR || !pAccept || !pComplete || !pReply) { KEEL_ERROR(L"uxsms; LPC API resolve failed"); return; }

    SECURITY_DESCRIPTOR sd; InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);

    UNICODE_STRING name; pInit(&name, L"\\RPC Control\\UxSmsApiPort");
    OBJECT_ATTRIBUTES oa; ZeroMemory(&oa, sizeof(oa));
    oa.Length = sizeof(oa); oa.ObjectName = &name; oa.SecurityDescriptor = &sd;
    NTSTATUS st = pCreate(&g_port, &oa, 512, 0x148, 0);
    if (!NT_OK(st)) { KEEL_ERROR(L"uxsms; NtCreatePort('\\RPC Control\\UxSmsApiPort') -> 0x%08X", (unsigned)st); return; }
    KEEL_INFO(L"uxsms; created LPC \\RPC Control\\UxSmsApiPort in dwm, handle=%p", g_port);
    g_isPrimary = true;
    g_orphanEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    HANDLE wh = nullptr;
    BOOL rw = RegisterWaitForSingleObject(&wh, g_orphanEvent, [](PVOID, BOOLEAN){}, nullptr, INFINITE, WT_EXECUTEONLYONCE);
    KEEL_INFO(L"uxsms; self-test RWFSO(event=%p) -> %d err=%lu", g_orphanEvent, rw, GetLastError());
    if (rw && wh) UnregisterWaitEx(wh, nullptr);
    CreateThread(nullptr, 0, ServiceThread, nullptr, 0, nullptr);
}

}
