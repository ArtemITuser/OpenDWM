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
#include <uxtheme.h>
#include <vssym32.h>
#include <shlwapi.h>

#include <stdio.h>
#include <string>

#include "keel/log.h"
#include "keel/version.h"

struct ThemeApi {
    HMODULE mod = nullptr;
    std::wstring path;
    std::wstring version;
    bool isDonor = false;

    HTHEME (WINAPI* OpenThemeData)(HWND, LPCWSTR) = nullptr;

    void* themeFile = nullptr;
    void* (*OpenThemeDataFromFile)(void*, HWND, LPCWSTR, int) = nullptr;
    HRESULT (WINAPI* CloseThemeData)(HTHEME) = nullptr;
    HRESULT (WINAPI* DrawThemeBackground)(HTHEME, HDC, int, int, const RECT*, const RECT*) = nullptr;
    HRESULT (WINAPI* DrawThemeText)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, DWORD, const RECT*) = nullptr;
    BOOL (WINAPI* IsThemeActive)() = nullptr;
    BOOL (WINAPI* IsAppThemed)() = nullptr;
    HRESULT (WINAPI* SetWindowTheme)(HWND, LPCWSTR, LPCWSTR) = nullptr;
    void (WINAPI* SetThemeAppProperties)(DWORD) = nullptr;

    bool Bind() {
        if (!mod) return false;
        *(FARPROC*)&OpenThemeData        = GetProcAddress(mod, "OpenThemeData");
        *(FARPROC*)&CloseThemeData       = GetProcAddress(mod, "CloseThemeData");
        *(FARPROC*)&DrawThemeBackground  = GetProcAddress(mod, "DrawThemeBackground");
        *(FARPROC*)&DrawThemeText        = GetProcAddress(mod, "DrawThemeText");
        *(FARPROC*)&IsThemeActive        = GetProcAddress(mod, "IsThemeActive");
        *(FARPROC*)&IsAppThemed          = GetProcAddress(mod, "IsAppThemed");
        *(FARPROC*)&SetWindowTheme       = GetProcAddress(mod, "SetWindowTheme");
        *(FARPROC*)&SetThemeAppProperties= GetProcAddress(mod, "SetThemeAppProperties");
        wchar_t p[MAX_PATH]{};
        GetModuleFileNameW(mod, p, MAX_PATH);
        path = p;
        DWORD a, b, c, d;
        if (keel::GetFileVersion(p, a, b, c, d)) {
            wchar_t v[64];
            _snwprintf_s(v, _TRUNCATE, L"%lu.%lu.%lu.%lu", a, b, c, d);
            version = v;
        }
        return OpenThemeData && DrawThemeBackground;
    }
};

namespace {
ThemeApi g_host;
ThemeApi g_donor;
std::wstring g_donorDir;
std::wstring g_status;
std::wstring g_r3;

constexpr DWORD kRvaThemeLoaderCtor  = 0x1cacc;
constexpr DWORD kRvaThemeLoaderLoad  = 0x1cdf0;
constexpr DWORD kRvaUxFileCtor       = 0xbebc;
constexpr DWORD kRvaUxFileOpenHandle = 0xbf54;
constexpr DWORD kRvaOpenThemeFromFile= 0x39338;
constexpr DWORD kRvaGetThemeDefaults = 0x38970;

void* BuildDonorThemeFile(HMODULE ux, const std::wstring& msstyles) {
    auto at = [ux](DWORD rva) { return reinterpret_cast<BYTE*>(ux) + rva; };
    using CtorT   = void* (*)(void*);
    using LoadT   = long  (*)(void*, void*, void*, const wchar_t*, const wchar_t*, const wchar_t*, void**, int);
    using OpenT   = DWORD (*)(void*, void*, unsigned long, int);
    auto loaderCtor = reinterpret_cast<CtorT>(at(kRvaThemeLoaderCtor));
    auto loadTheme  = reinterpret_cast<LoadT>(at(kRvaThemeLoaderLoad));
    auto fileCtor   = reinterpret_cast<CtorT>(at(kRvaUxFileCtor));
    auto openHandle = reinterpret_cast<OpenT>(at(kRvaUxFileOpenHandle));

    void* loader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x500);
    if (!loader) { g_r3 = L"loader alloc failed"; return nullptr; }
    loaderCtor(loader);

    using DefT = long (*)(const wchar_t*, wchar_t*, int, wchar_t*, int);
    auto getDefaults = reinterpret_cast<DefT>(at(kRvaGetThemeDefaults));
    wchar_t color[128] = L"NormalColor", size[128] = L"NormalSize";
    const long dhr = getDefaults(msstyles.c_str(), color, 128, size, 128);
    {
        wchar_t b[160]; _snwprintf_s(b, _TRUNCATE, L"defaults hr=0x%08lx color='%s' size='%s'; ", dhr, color, size);
        g_r3 = b;
    }
    void* section = nullptr;
    const long hr = loadTheme(loader, nullptr, nullptr, msstyles.c_str(), color, size, &section, 0);
    HeapFree(GetProcessHeap(), 0, loader);
    if (hr < 0 || !section) {
        wchar_t b[128]; _snwprintf_s(b, _TRUNCATE, L"LoadTheme hr=0x%08lx section=%p", hr, section);
        g_r3 += b; return nullptr;
    }
    void* file = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x40);
    if (!file) { g_r3 = L"file alloc failed"; return nullptr; }
    fileCtor(file);
    const DWORD ofh = openHandle(file, section, 4 , 0);
    if (static_cast<long>(ofh) < 0) {
        wchar_t b[96]; _snwprintf_s(b, _TRUNCATE, L"OpenFromHandle hr=0x%08lx", ofh); g_r3 = b;
        HeapFree(GetProcessHeap(), 0, file); return nullptr;
    }
    g_donor.OpenThemeDataFromFile = reinterpret_cast<decltype(g_donor.OpenThemeDataFromFile)>(at(kRvaOpenThemeFromFile));
    g_r3 = L"theme section built from aero.msstyles in-process (no service)";
    return file;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return buf;
}

bool DrawColumn(const ThemeApi& t, HDC dc, RECT col) {
    SetBkMode(dc, TRANSPARENT);
    int x = col.left + 16;
    int y = col.top + 8;
    const int w = (col.right - col.left) - 32;

    auto band = [&](int h) { RECT r{ x, y, x + w, y + h }; y += h + 14; return r; };

    auto openTheme = [&t](HWND hwnd, LPCWSTR cls) -> HTHEME {
        if (t.isDonor && t.OpenThemeDataFromFile && t.themeFile)
            return reinterpret_cast<HTHEME>(t.OpenThemeDataFromFile(t.themeFile, hwnd, cls, 1));
        return t.OpenThemeData ? t.OpenThemeData(hwnd, cls) : nullptr;
    };

    bool any = false;

    if (HTHEME hb = openTheme(nullptr, L"BUTTON")) {
        const int states[4] = { PBS_NORMAL, PBS_HOT, PBS_PRESSED, PBS_DISABLED };
        const wchar_t* names[4] = { L"normal", L"hot", L"pressed", L"disabled" };
        for (int i = 0; i < 4; ++i) {
            RECT r = band(34);
            t.DrawThemeBackground(hb, dc, BP_PUSHBUTTON, states[i], &r, nullptr);
            if (t.DrawThemeText) t.DrawThemeText(hb, dc, BP_PUSHBUTTON, states[i], names[i], -1,
                                                 DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0, &r);
            any = true;
        }

        RECT cb = band(24);
        RECT box{ cb.left, cb.top, cb.left + 20, cb.top + 20 };
        t.DrawThemeBackground(hb, dc, BP_CHECKBOX, CBS_CHECKEDNORMAL, &box, nullptr);
        RECT rb{ cb.left + 90, cb.top, cb.left + 110, cb.top + 20 };
        t.DrawThemeBackground(hb, dc, BP_RADIOBUTTON, RBS_CHECKEDNORMAL, &rb, nullptr);
        t.CloseThemeData(hb);
    }

    if (HTHEME hp = openTheme(nullptr, L"PROGRESS")) {
        RECT r = band(26);
        t.DrawThemeBackground(hp, dc, PP_BAR, 0, &r, nullptr);
        RECT fill = r; InflateRect(&fill, -3, -3); fill.right = fill.left + (int)((fill.right - fill.left) * 0.6);
        t.DrawThemeBackground(hp, dc, PP_CHUNK, 0, &fill, nullptr);
        t.CloseThemeData(hp);
        any = true;
    }

    if (HTHEME hs = openTheme(nullptr, L"SCROLLBAR")) {
        RECT r = band(24);
        RECT up{ r.left, r.top, r.left + 22, r.top + 22 };
        t.DrawThemeBackground(hs, dc, SBP_ARROWBTN, ABS_UPNORMAL, &up, nullptr);
        RECT th{ r.left + 26, r.top, r.left + 120, r.top + 22 };
        t.DrawThemeBackground(hs, dc, SBP_THUMBBTNHORZ, SCRBS_NORMAL, &th, nullptr);
        RECT dn{ r.left + 126, r.top, r.left + 148, r.top + 22 };
        t.DrawThemeBackground(hs, dc, SBP_ARROWBTN, ABS_DOWNNORMAL, &dn, nullptr);
        t.CloseThemeData(hs);
        any = true;
    }
    return any;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

            const int top = 156;
            const int mid = rc.right / 2;
            RECT header{ 8, 6, rc.right - 8, top - 4 };
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, g_status.c_str(), -1, &header, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

            HFONT bold = CreateFontW(20, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, bold);
            RECT lh{ 16, top, mid, top + 26 };
            RECT rh{ mid + 16, top, rc.right, top + 26 };
            DrawTextW(dc, L"Windows 10 host uxtheme", -1, &lh, DT_LEFT);
            DrawTextW(dc, L"Windows 7 donor uxtheme", -1, &rh, DT_LEFT);
            SelectObject(dc, old); DeleteObject(bold);
            MoveToEx(dc, mid, top, nullptr); LineTo(dc, mid, rc.bottom);

            RECT lcol{ 0, top + 28, mid, rc.bottom };
            RECT rcol{ mid, top + 28, rc.right, rc.bottom };
            DrawColumn(g_host, dc, lcol);
            if (!DrawColumn(g_donor, dc, rcol)) {
                RECT r{ mid + 16, top + 40, rc.right - 16, top + 160 };
                const wchar_t* msg =
                    !g_donor.mod
                      ? L"donor uxtheme did not load (see the load line above). Its Win7 API-set "
                        L"dependencies must be shimmed first."
                      : !g_donor.OpenThemeData
                        ? L"donor uxtheme loaded, but exports could not be resolved."
                        : L"donor uxtheme loaded and runs on the Win10 kernel, but OpenThemeData "
                          L"returned no theme. This is research question R3: the Win7 theme section "
                          L"must be built in-process. Real Win7 code executes; the active theme is "
                          L"not yet wired.";
                DrawTextW(dc, msg, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN: if (wp == VK_ESCAPE) DestroyWindow(hwnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    keel::LogInit(L"keeltest");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto hasUx = [](const std::wstring& d) {
        if (d.empty()) return false;
        return GetFileAttributesW((d + L"\\uxtheme.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    };
    std::wstring argDir = (cmd && *cmd) ? cmd : L"";
    while (!argDir.empty() && (argDir.front() == L'"' || argDir.front() == L' ')) argDir.erase(argDir.begin());
    while (!argDir.empty() && (argDir.back() == L'"' || argDir.back() == L' ')) argDir.pop_back();
    wchar_t envDir[MAX_PATH]{}; GetEnvironmentVariableW(L"KEEL_DONOR", envDir, MAX_PATH);
    const std::wstring candidates[] = { argDir, envDir, ExeDir() + L"\\..\\cut3", ExeDir() + L"\\donor\\cut3",
                                        ExeDir() + L"\\cut3", L"C:\\Keel\\cut3" };
    for (const auto& c : candidates) { if (hasUx(c)) { g_donorDir = c; break; } }
    if (g_donorDir.empty()) g_donorDir = argDir.empty() ? std::wstring(L"C:\\Keel\\cut3") : argDir;

    g_host.mod = LoadLibraryW(L"uxtheme.dll");
    g_host.Bind();

    int apisetsLoaded = 0;
    {
        WIN32_FIND_DATAW fd;
        std::wstring pat = g_donorDir + L"\\api-ms-*.dll";
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring full = g_donorDir + L"\\" + fd.cFileName;
                if (LoadLibraryExW(full.c_str(), nullptr, 0)) ++apisetsLoaded;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    std::wstring donorUx = g_donorDir + L"\\uxtheme.dll";
    g_donor.isDonor = true;
    AddDllDirectory(g_donorDir.c_str());
    struct Strat { const wchar_t* label; DWORD flags; };
    const Strat strategies[] = {
        { L"plain",            0 },
        { L"DLL_LOAD_DIR",     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS },
        { L"altered",          LOAD_WITH_ALTERED_SEARCH_PATH },
    };
    std::wstring loadReport;
    DWORD donorErr = 0;
    for (const auto& s : strategies) {
        g_donor.mod = LoadLibraryExW(donorUx.c_str(), nullptr, s.flags);
        if (g_donor.mod) { loadReport += std::wstring(s.label) + L"=OK "; break; }
        donorErr = GetLastError();
        loadReport += std::wstring(s.label) + L"=err" + std::to_wstring(donorErr) + L" ";
    }
    if (g_donor.mod) g_donor.Bind();
    if (g_host.SetThemeAppProperties) g_host.SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS);
    if (g_donor.SetThemeAppProperties) g_donor.SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS);

    if (g_donor.mod && g_donor.version.rfind(L"6.1.", 0) == 0) {
        g_donor.themeFile = BuildDonorThemeFile(g_donor.mod, g_donorDir + L"\\aero.msstyles");
    }

    const auto v = keel::GetHostVersion();

    bool donorIsReallyDonor = g_donor.mod && g_donor.version.rfind(L"6.1.", 0) == 0 &&
                              StrStrIW(g_donor.path.c_str(), L"\\System32\\") == nullptr;
    wchar_t buf[900];
    _snwprintf_s(buf, _TRUNCATE,
        L"Keel M4 first light  -  host build %lu.%lu.%lu.%lu   donor dir: %s\n"
        L"host uxtheme : %s (%s)\n"
        L"donor uxtheme: %s%s\n"
        L"  loaded from : %s\n"
        L"  load: %s (apiset shims preloaded: %d)\n"
        L"  is Win7 copy: %s   OpenThemeData present: %s\n"
        L"  R3: %s",
        v.major, v.minor, v.build, v.ubr, g_donorDir.c_str(),
        g_host.version.empty() ? L"?" : g_host.version.c_str(), g_host.path.c_str(),
        g_donor.version.empty() ? L"(load failed)" : g_donor.version.c_str(),
        g_donor.mod ? L"" : (std::wstring(L" err=") + std::to_wstring(donorErr)).c_str(),
        g_donor.path.empty() ? L"-" : g_donor.path.c_str(),
        loadReport.c_str(), apisetsLoaded,
        donorIsReallyDonor ? L"YES" : L"no (hijacked to System32)",
        g_donor.OpenThemeData ? L"yes" : L"no",
        g_r3.empty() ? L"(not attempted)" : g_r3.c_str());
    g_status = buf;
    KEEL_INFO(L"%s", buf);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"KeelTestWindow";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"keeltest - Windows 10 vs Windows 7 uxtheme",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 560,
                                nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
