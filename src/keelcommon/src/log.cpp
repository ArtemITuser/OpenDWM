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

#include "keel/log.h"

#include <stdio.h>
#include <stdarg.h>

namespace keel {
namespace {
const wchar_t* g_component = L"keel";
HANDLE g_file = INVALID_HANDLE_VALUE;
const wchar_t* LevelName(Level l) {
    switch (l) {
        case Level::Trace: return L"T";
        case Level::Info:  return L"I";
        case Level::Warn:  return L"W";
        default:           return L"E";
    }
}
}

void LogInit(const wchar_t* component) {
    g_component = component;
    wchar_t path[MAX_PATH];
    if (GetEnvironmentVariableW(L"KEEL_LOG", path, MAX_PATH) == 0) {

        _snwprintf_s(path, _TRUNCATE, L"C:\\Keel\\keel-%s.log", component);
    }
    g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(Level level, const wchar_t* fmt, ...) {
    wchar_t msg[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);
    wchar_t line[1200];
    _snwprintf_s(line, _TRUNCATE, L"[%s %s pid=%lu tid=%lu] %s\n", g_component, LevelName(level),
                 GetCurrentProcessId(), GetCurrentThreadId(), msg);
    OutputDebugStringW(line);
    if (g_file != INVALID_HANDLE_VALUE) {
        char utf8[2400];
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (n > 1) {
            DWORD written = 0;
            WriteFile(g_file, utf8, static_cast<DWORD>(n - 1), &written, nullptr);
        }
    }
}

}
