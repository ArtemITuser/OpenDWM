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

namespace {

void Log(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list ap; va_start(ap, fmt); _vsnwprintf_s(msg, _TRUNCATE, fmt, ap); va_end(ap);
    wchar_t line[640];
    const int n = _snwprintf_s(line, _TRUNCATE, L"[keelldr32 pid=%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (n <= 0) return;
    HANDLE h = CreateFileW(L"C:\\Keel\\native-keel32.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char utf8[1280];
    const int cb = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    if (cb > 0) { DWORD w = 0; WriteFile(h, utf8, static_cast<DWORD>(cb), &w, nullptr); }
    CloseHandle(h);
}

bool InjectDll(HANDLE proc, const wchar_t* dllPath) {
    const SIZE_T cb = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, cb, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { Log(L"VirtualAllocEx failed %lu", GetLastError()); return false; }
    if (!WriteProcessMemory(proc, remote, dllPath, cb, nullptr)) {
        Log(L"WriteProcessMemory failed %lu", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return false;
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibrary = k32 ? GetProcAddress(k32, "LoadLibraryW") : nullptr;
    if (!loadLibrary) { Log(L"LoadLibraryW not found"); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); return false; }

    HANDLE th = CreateRemoteThread(proc, nullptr, 0,
                                   reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), remote, 0, nullptr);
    if (!th) { Log(L"CreateRemoteThread failed %lu", GetLastError()); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); return false; }
    WaitForSingleObject(th, 10000);
    DWORD loaded = 0; GetExitCodeThread(th, &loaded);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    if (!loaded) { Log(L"LoadLibraryW in target returned NULL (shim not loaded)"); return false; }
    return true;
}

class IfeoGuard {
public:
    explicit IfeoGuard(const wchar_t* imageName) {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Global\\KeelLdr32IfeoGuard");
        if (mutex_) WaitForSingleObject(mutex_, 15000);

        wchar_t path[512];
        _snwprintf_s(path, _TRUNCATE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\%s",
                     imageName);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_SET_VALUE, &key_) != ERROR_SUCCESS) {
            key_ = nullptr;
            return;
        }
        DWORD type = 0, cb = sizeof(saved_);
        if (RegQueryValueExW(key_, L"Debugger", nullptr, &type, reinterpret_cast<BYTE*>(saved_), &cb) == ERROR_SUCCESS
            && type == REG_SZ) {
            if (RegDeleteValueW(key_, L"Debugger") == ERROR_SUCCESS) removed_ = true;
            else Log(L"could not clear the IFEO Debugger value (%lu) so refusing to launch to avoid recursion", GetLastError());
        }
    }
    ~IfeoGuard() {
        if (key_ && removed_)
            RegSetValueExW(key_, L"Debugger", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(saved_),
                           static_cast<DWORD>((wcslen(saved_) + 1) * sizeof(wchar_t)));
        if (key_) RegCloseKey(key_);
        if (mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); }
    }

    bool WouldRecurse() const { return key_ && !removed_ && saved_[0] != L'\0'; }

private:
    HKEY key_ = nullptr;
    HANDLE mutex_ = nullptr;
    wchar_t saved_[512]{};
    bool removed_ = false;
};

void TargetImageName(const wchar_t* cmdLine, wchar_t* out, size_t cch) {
    wchar_t path[MAX_PATH]{};
    const wchar_t* p = cmdLine;
    size_t i = 0;
    if (*p == L'"') { ++p; while (*p && *p != L'"' && i < MAX_PATH - 1) path[i++] = *p++; }
    else { while (*p && *p != L' ' && i < MAX_PATH - 1) path[i++] = *p++; }
    path[i] = 0;
    const wchar_t* leaf = wcsrchr(path, L'\\');
    wcscpy_s(out, cch, leaf ? leaf + 1 : path);
}

const wchar_t* ChildCommandLine() {
    const wchar_t* cl = GetCommandLineW();

    if (*cl == L'"') { ++cl; while (*cl && *cl != L'"') ++cl; if (*cl == L'"') ++cl; }
    else { while (*cl && *cl != L' ') ++cl; }
    while (*cl == L' ') ++cl;
    return cl;
}

}

int wmain() {
    const wchar_t* child = ChildCommandLine();
    if (!*child) { Log(L"no target on the command line"); return 1; }

    wchar_t shim[MAX_PATH]{};
    GetModuleFileNameW(nullptr, shim, MAX_PATH);
    if (wchar_t* slash = wcsrchr(shim, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - shim), L"keelshim32.dll");
    if (GetFileAttributesW(shim) == INVALID_FILE_ATTRIBUTES) { Log(L"shim missing at %s", shim); return 1; }

    wchar_t cmd[4096];
    wcscpy_s(cmd, child);

    wchar_t image[MAX_PATH]{};
    TargetImageName(child, image, MAX_PATH);
    IfeoGuard guard(image);
    if (guard.WouldRecurse()) { Log(L"IFEO Debugger still set for '%s' so not launching", image); return 1; }

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        Log(L"CreateProcess('%s') failed %lu", cmd, GetLastError());
        return 1;
    }
    const bool ok = InjectDll(pi.hProcess, shim);
    ResumeThread(pi.hThread);
    Log(L"launched pid=%lu injected=%d cmd='%s'", pi.dwProcessId, (int)ok, cmd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
