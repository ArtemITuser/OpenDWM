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

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>

#define KEEL_DEVICE_NAME      L"\\Device\\Keel"
#define KEEL_SYMLINK_NAME     L"\\DosDevices\\Keel"
#define KEEL_VERSION_MAJOR    0
#define KEEL_VERSION_MINOR    1

#define IOCTL_KEEL_GET_VERSION CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KEEL_DWM_QUERY   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_KEEL_DWM_SETOWNER CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_KEEL_PATCH_MODULE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_KEEL_DXG_ALLOWVIDPN CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KEEL_DXGPROCESS_RVA_O2      0x58
#define KEEL_DXG_O2_RVA_CHECK1      0xd0
#define KEEL_DXG_O2_RVA_CHECK2      0x130

typedef struct _KEEL_DXG_RESULT {
    ULONGLONG DxgProcess;
    ULONGLONG O2;
    ULONGLONG PrevCheck1;
    ULONGLONG PrevCheck2;
    ULONGLONG StubAddr;
    ULONGLONG ReadBack1;
    UCHAR     Flag15b;
    LONG      Status;
} KEEL_DXG_RESULT, *PKEEL_DXG_RESULT;

typedef struct _KEEL_VERSION_INFO {
    ULONG Major;
    ULONG Minor;
    ULONG HostBuild;
} KEEL_VERSION_INFO, *PKEEL_VERSION_INFO;

#define KEEL_WIN32KBASE_RVA_G_PEPDWM        0x24C690
#define KEEL_WIN32KBASE_RVA_GHSEMDWMSTATE   0x24ABC8

typedef struct _KEEL_DWM_STATE {
    ULONGLONG Win32kBase;
    ULONGLONG GpepDwmAddr;
    ULONGLONG GpepDwmValue;
    ULONG     OwnerPid;
    LONG      ReadStatus;
    ULONG     CallerPid;
    ULONG     CallerSession;
} KEEL_DWM_STATE, *PKEEL_DWM_STATE;

typedef struct _KEEL_DWM_SETOWNER_RESULT {
    ULONGLONG PrevOwner;
    ULONGLONG NewOwner;
    LONG      Status;
} KEEL_DWM_SETOWNER_RESULT, *PKEEL_DWM_SETOWNER_RESULT;

typedef struct _KEEL_PATCH_REQUEST {
    WCHAR ProcessName[64];
    WCHAR ModuleName[64];
    ULONG Rva;
    UCHAR Expect;
    UCHAR New;
} KEEL_PATCH_REQUEST, *PKEEL_PATCH_REQUEST;

typedef struct _KEEL_PATCH_RESULT {
    ULONG     ProcessesMatched;
    ULONG     ModulesFound;
    ULONG     BytesPatched;
    ULONG     AlreadyPatched;
    ULONGLONG LastModuleBase;
    LONG      Status;
} KEEL_PATCH_RESULT, *PKEEL_PATCH_RESULT;

typedef enum _SYSTEM_INFORMATION_CLASS { SystemProcessInformation = 5, SystemModuleInformation = 11 } SYSTEM_INFORMATION_CLASS;
NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
NTKERNELAPI HANDLE NTAPI PsGetProcessId(PEPROCESS);
NTKERNELAPI NTSTATUS NTAPI PsLookupProcessByProcessId(HANDLE, PEPROCESS*);
NTKERNELAPI HANDLE NTAPI PsGetCurrentProcessId(void);
NTKERNELAPI ULONG NTAPI PsGetProcessSessionId(PEPROCESS);

typedef struct _KEEL_KAPC_STATE { UCHAR Opaque[0x30]; } KEEL_KAPC_STATE, *PKEEL_KAPC_STATE;
NTKERNELAPI PVOID NTAPI PsGetProcessPeb(PEPROCESS);
NTKERNELAPI PVOID NTAPI PsGetProcessDxgProcess(PEPROCESS);
NTKERNELAPI VOID NTAPI KeStackAttachProcess(PEPROCESS, PKEEL_KAPC_STATE);
NTKERNELAPI VOID NTAPI KeUnstackDetachProcess(PKEEL_KAPC_STATE);
NTSYSAPI NTSTATUS NTAPI ZwProtectVirtualMemory(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);

typedef struct _KEEL_SPI {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    UCHAR Reserved1[0x30];
    UNICODE_STRING ImageName;
    LONG  BasePriority;
    HANDLE UniqueProcessId;
} KEEL_SPI, *PKEEL_SPI;

typedef struct _KEEL_PEB_LDR { UCHAR Reserved[0x10]; LIST_ENTRY InLoadOrderModuleList; } KEEL_PEB_LDR;
typedef struct _KEEL_PEB { UCHAR Reserved[0x18]; KEEL_PEB_LDR* Ldr; } KEEL_PEB;
typedef struct _KEEL_LDR_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UCHAR pad[4];
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} KEEL_LDR_ENTRY, *PKEEL_LDR_ENTRY;

typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE Section; PVOID MappedBase; PVOID ImageBase; ULONG ImageSize; ULONG Flags;
    USHORT LoadOrderIndex; USHORT InitOrderIndex; USHORT LoadCount; USHORT OffsetToFileName;
    UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, *PRTL_PROCESS_MODULE_INFORMATION;
typedef struct _RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, *PRTL_PROCESS_MODULES;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD KeelEvtDriverUnload;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL KeelEvtIoDeviceControl;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

static ULONG g_HostBuild = 0;

static BOOLEAN KeelStrEqI(PCSTR a, PCSTR b)
{
    for (;; ++a, ++b) {
        CHAR ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (CHAR)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (CHAR)(cb + 32);
        if (ca != cb) return FALSE;
        if (ca == 0) return TRUE;
    }
}

static PVOID KeelFindWin32kBase(void)
{
    ULONG len = 0;
    PVOID base = NULL;
    PRTL_PROCESS_MODULES mods;
    NTSTATUS st = ZwQuerySystemInformation(SystemModuleInformation, &len, 0, &len);
    if (len == 0) return NULL;
    len += 0x2000;
    mods = (PRTL_PROCESS_MODULES)ExAllocatePool2(POOL_FLAG_NON_PAGED, len, 'lssK');
    if (!mods) return NULL;
    st = ZwQuerySystemInformation(SystemModuleInformation, mods, len, &len);
    if (NT_SUCCESS(st)) {
        for (ULONG i = 0; i < mods->NumberOfModules; ++i) {
            PRTL_PROCESS_MODULE_INFORMATION m = &mods->Modules[i];
            PCSTR name = (PCSTR)&m->FullPathName[m->OffsetToFileName];
            if (KeelStrEqI(name, "win32kbase.sys")) { base = m->ImageBase; break; }
        }
    }
    ExFreePoolWithTag(mods, 'lssK');
    return base;
}

static void KeelQueryDwmState(PKEEL_DWM_STATE out)
{
    RtlZeroMemory(out, sizeof(*out));
    out->CallerPid = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    out->CallerSession = PsGetProcessSessionId(PsGetCurrentProcess());
    PUCHAR base = (PUCHAR)KeelFindWin32kBase();
    out->Win32kBase = (ULONGLONG)base;
    if (!base) { out->ReadStatus = (LONG)STATUS_NOT_FOUND; return; }
    PVOID* pGpepDwm = (PVOID*)(base + KEEL_WIN32KBASE_RVA_G_PEPDWM);
    out->GpepDwmAddr = (ULONGLONG)pGpepDwm;
    __try {
        if (!MmIsAddressValid(pGpepDwm)) { out->ReadStatus = (LONG)STATUS_ACCESS_VIOLATION; return; }
        PEPROCESS owner = (PEPROCESS)*pGpepDwm;
        out->GpepDwmValue = (ULONGLONG)owner;
        if (owner && MmIsAddressValid(owner)) {
            out->OwnerPid = (ULONG)(ULONG_PTR)PsGetProcessId(owner);
        }
        out->ReadStatus = (LONG)STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->ReadStatus = (LONG)GetExceptionCode();
    }
}

static void KeelSetDwmOwner(ULONG pid, PKEEL_DWM_SETOWNER_RESULT out)
{
    RtlZeroMemory(out, sizeof(*out));
    PUCHAR base = (PUCHAR)KeelFindWin32kBase();
    if (!base) { out->Status = (LONG)STATUS_NOT_FOUND; return; }
    PVOID* pGpepDwm = (PVOID*)(base + KEEL_WIN32KBASE_RVA_G_PEPDWM);
    PEPROCESS newProc = NULL;
    if (pid != 0) {
        NTSTATUS st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &newProc);
        if (!NT_SUCCESS(st)) { out->Status = (LONG)st; return; }

    }
    __try {
        if (!MmIsAddressValid(pGpepDwm)) { out->Status = (LONG)STATUS_ACCESS_VIOLATION; if (newProc) ObDereferenceObject(newProc); return; }
        out->PrevOwner = (ULONGLONG)*pGpepDwm;
        *pGpepDwm = (PVOID)newProc;
        out->NewOwner = (ULONGLONG)newProc;
        out->Status = (LONG)STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->Status = (LONG)GetExceptionCode();
        if (newProc) ObDereferenceObject(newProc);
    }
}

static LONG KeelReturnOne(void) { return 1; }

static void KeelDxgAllowVidPn(ULONG pid, PKEEL_DXG_RESULT out)
{
    RtlZeroMemory(out, sizeof(*out));
    out->StubAddr = (ULONGLONG)(ULONG_PTR)&KeelReturnOne;
    PEPROCESS proc = NULL;
    NTSTATUS st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &proc);
    if (!NT_SUCCESS(st)) { out->Status = (LONG)st; return; }

    KEEL_KAPC_STATE apc;
    KeStackAttachProcess(proc, &apc);
    __try {
        PUCHAR dxg = (PUCHAR)PsGetProcessDxgProcess(proc);
        out->DxgProcess = (ULONGLONG)(ULONG_PTR)dxg;
        if (!dxg || !MmIsAddressValid(dxg + KEEL_DXGPROCESS_RVA_O2)) { out->Status = (LONG)STATUS_NOT_FOUND; __leave; }

        if (MmIsAddressValid(dxg + 0x15b)) out->Flag15b = dxg[0x15b];
        PUCHAR o2 = *(PUCHAR*)(dxg + KEEL_DXGPROCESS_RVA_O2);
        out->O2 = (ULONGLONG)(ULONG_PTR)o2;
        if (!o2 || !MmIsAddressValid(o2 + KEEL_DXG_O2_RVA_CHECK2)) { out->Status = (LONG)STATUS_INVALID_ADDRESS; __leave; }
        PVOID* pc1 = (PVOID*)(o2 + KEEL_DXG_O2_RVA_CHECK1);
        PVOID* pc2 = (PVOID*)(o2 + KEEL_DXG_O2_RVA_CHECK2);
        out->PrevCheck1 = (ULONGLONG)(ULONG_PTR)*pc1;
        out->PrevCheck2 = (ULONGLONG)(ULONG_PTR)*pc2;
        *pc1 = (PVOID)&KeelReturnOne;
        *pc2 = (PVOID)&KeelReturnOne;
        out->ReadBack1 = (ULONGLONG)(ULONG_PTR)*pc1;
        out->Status = (LONG)STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->Status = (LONG)GetExceptionCode();
    }
    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(proc);
}

static PVOID KeelFindModuleInPeb(PCWSTR moduleName, PULONG sizeOut)
{
    PVOID base = NULL;
    UNICODE_STRING want;
    RtlInitUnicodeString(&want, moduleName);
    __try {
        KEEL_PEB* peb = (KEEL_PEB*)PsGetProcessPeb(IoGetCurrentProcess());
        if (!peb || !peb->Ldr) return NULL;
        LIST_ENTRY* head = &peb->Ldr->InLoadOrderModuleList;
        for (LIST_ENTRY* cur = head->Flink; cur && cur != head; cur = cur->Flink) {
            PKEEL_LDR_ENTRY e = CONTAINING_RECORD(cur, KEEL_LDR_ENTRY, InLoadOrderLinks);
            if (e->BaseDllName.Buffer && e->BaseDllName.Length &&
                RtlEqualUnicodeString(&e->BaseDllName, &want, TRUE)) {
                base = e->DllBase;
                if (sizeOut) *sizeOut = e->SizeOfImage;
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        base = NULL;
    }
    return base;
}

static void KeelPatchOneProcess(PEPROCESS proc, PCWSTR moduleName, ULONG rva, UCHAR expect, UCHAR newByte,
                                PKEEL_PATCH_RESULT res)
{
    KEEL_KAPC_STATE apc;
    KeStackAttachProcess(proc, &apc);
    __try {
        PUCHAR base = (PUCHAR)KeelFindModuleInPeb(moduleName, NULL);
        if (base) {
            res->ModulesFound++;
            res->LastModuleBase = (ULONGLONG)base;
            PUCHAR target = base + rva;
            if (MmIsAddressValid(target)) {
                UCHAR cur = *target;
                if (cur == newByte) {
                    res->AlreadyPatched++;
                    res->Status = (LONG)STATUS_SUCCESS;
                } else if (cur == expect) {
                    PVOID region = target;
                    SIZE_T sz = 1;
                    ULONG oldProt = 0;
                    NTSTATUS st = ZwProtectVirtualMemory(ZwCurrentProcess(), &region, &sz,
                                                         PAGE_EXECUTE_READWRITE, &oldProt);
                    res->Status = (LONG)st;
                    if (NT_SUCCESS(st)) {
                        *target = newByte;
                        ULONG tmp = 0;
                        region = target; sz = 1;
                        ZwProtectVirtualMemory(ZwCurrentProcess(), &region, &sz, oldProt, &tmp);
                        res->BytesPatched++;
                    }
                } else {
                    res->Status = (LONG)STATUS_UNSUCCESSFUL;
                }
            } else {
                res->Status = (LONG)STATUS_ACCESS_VIOLATION;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        res->Status = (LONG)GetExceptionCode();
    }
    KeUnstackDetachProcess(&apc);
}

static void KeelPatchModule(PKEEL_PATCH_REQUEST req, PKEEL_PATCH_RESULT res)
{
    RtlZeroMemory(res, sizeof(*res));

    req->ProcessName[63] = 0; req->ModuleName[63] = 0;
    UNICODE_STRING wantProc;
    RtlInitUnicodeString(&wantProc, req->ProcessName);

    ULONG len = 0;
    ZwQuerySystemInformation(SystemProcessInformation, NULL, 0, &len);
    if (len == 0) { res->Status = (LONG)STATUS_UNSUCCESSFUL; return; }
    len += 0x8000;
    PUCHAR buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, len, 'ispK');
    if (!buf) { res->Status = (LONG)STATUS_INSUFFICIENT_RESOURCES; return; }
    NTSTATUS st = ZwQuerySystemInformation(SystemProcessInformation, buf, len, &len);
    if (!NT_SUCCESS(st)) { ExFreePoolWithTag(buf, 'ispK'); res->Status = (LONG)st; return; }

    for (PUCHAR p = buf;;) {
        PKEEL_SPI spi = (PKEEL_SPI)p;
        if (spi->ImageName.Buffer && spi->ImageName.Length &&
            RtlEqualUnicodeString(&spi->ImageName, &wantProc, TRUE)) {
            res->ProcessesMatched++;
            PEPROCESS proc = NULL;
            if (NT_SUCCESS(PsLookupProcessByProcessId(spi->UniqueProcessId, &proc)) && proc) {
                KeelPatchOneProcess(proc, req->ModuleName, req->Rva, req->Expect, req->New, res);
                ObDereferenceObject(proc);
            }
        }
        if (spi->NextEntryOffset == 0) break;
        p += spi->NextEntryOffset;
    }
    ExFreePoolWithTag(buf, 'ispK');
}

VOID KeelEvtIoDeviceControl(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t OutputBufferLength,
                            _In_ size_t InputBufferLength, _In_ ULONG IoControlCode)
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t written = 0;
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {
    case IOCTL_KEEL_GET_VERSION: {
        PKEEL_VERSION_INFO out = NULL;
        if (OutputBufferLength < sizeof(KEEL_VERSION_INFO)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KEEL_VERSION_INFO), (PVOID*)&out, NULL);
        if (!NT_SUCCESS(status)) break;
        out->Major = KEEL_VERSION_MAJOR;
        out->Minor = KEEL_VERSION_MINOR;
        out->HostBuild = g_HostBuild;
        written = sizeof(KEEL_VERSION_INFO);
        break;
    }
    case IOCTL_KEEL_DWM_QUERY: {
        PKEEL_DWM_STATE out = NULL;
        if (OutputBufferLength < sizeof(KEEL_DWM_STATE)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KEEL_DWM_STATE), (PVOID*)&out, NULL);
        if (!NT_SUCCESS(status)) break;
        KeelQueryDwmState(out);
        written = sizeof(KEEL_DWM_STATE);
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_KEEL_DWM_SETOWNER: {
        ULONG* pin = NULL;
        PKEEL_DWM_SETOWNER_RESULT out = NULL;
        ULONG pid;
        if (InputBufferLength < sizeof(ULONG) || OutputBufferLength < sizeof(KEEL_DWM_SETOWNER_RESULT)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID*)&pin, NULL);
        if (!NT_SUCCESS(status)) break;
        pid = *pin;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KEEL_DWM_SETOWNER_RESULT), (PVOID*)&out, NULL);
        if (!NT_SUCCESS(status)) break;
        KeelSetDwmOwner(pid, out);
        written = sizeof(KEEL_DWM_SETOWNER_RESULT);
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_KEEL_PATCH_MODULE: {
        PKEEL_PATCH_REQUEST in = NULL;
        PKEEL_PATCH_RESULT out = NULL;
        KEEL_PATCH_REQUEST req;
        if (InputBufferLength < sizeof(KEEL_PATCH_REQUEST) || OutputBufferLength < sizeof(KEEL_PATCH_RESULT)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KEEL_PATCH_REQUEST), (PVOID*)&in, NULL);
        if (!NT_SUCCESS(status)) break;
        RtlCopyMemory(&req, in, sizeof(req));
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KEEL_PATCH_RESULT), (PVOID*)&out, NULL);
        if (!NT_SUCCESS(status)) break;
        KeelPatchModule(&req, out);
        written = sizeof(KEEL_PATCH_RESULT);
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_KEEL_DXG_ALLOWVIDPN: {
        ULONG* pin = NULL;
        PKEEL_DXG_RESULT out = NULL;
        ULONG pid;
        if (InputBufferLength < sizeof(ULONG) || OutputBufferLength < sizeof(KEEL_DXG_RESULT)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID*)&pin, NULL);
        if (!NT_SUCCESS(status)) break;
        pid = *pin;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KEEL_DXG_RESULT), (PVOID*)&out, NULL);
        if (!NT_SUCCESS(status)) break;
        KeelDxgAllowVidPn(pid, out);
        written = sizeof(KEEL_DXG_RESULT);
        status = STATUS_SUCCESS;
        break;
    }
    default:
        break;
    }
    WdfRequestCompleteWithInformation(Request, status, written);
}

VOID KeelEvtDriverUnload(_In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "keeldrv: unload\n");
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDFDRIVER driver;
    PWDFDEVICE_INIT deviceInit;
    WDFDEVICE device;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;
    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(KEEL_DEVICE_NAME);
    UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(KEEL_SYMLINK_NAME);
    RTL_OSVERSIONINFOW osv;

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    RtlZeroMemory(&osv, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);
    if (NT_SUCCESS(RtlGetVersion(&osv))) g_HostBuild = osv.dwBuildNumber;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "keeldrv: DriverEntry on build %lu\n", g_HostBuild);

    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    config.DriverInitFlags = WdfDriverInitNonPnpDriver;
    config.EvtDriverUnload = KeelEvtDriverUnload;
    status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, &driver);
    if (!NT_SUCCESS(status)) return status;

    deviceInit = WdfControlDeviceInitAllocate(driver, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    if (deviceInit == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoBuffered);
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) { WdfDeviceInitFree(deviceInit); return status; }

    status = WdfDeviceCreate(&deviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
    if (!NT_SUCCESS(status)) { WdfDeviceInitFree(deviceInit); return status; }

    status = WdfDeviceCreateSymbolicLink(device, &symlinkName);
    if (!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = KeelEvtIoDeviceControl;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) return status;

    WdfControlFinishInitializing(device);
    return STATUS_SUCCESS;
}
