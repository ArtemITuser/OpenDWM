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
#include <objbase.h>
#include <commctrl.h>
#pragma comment(lib, "ole32.lib")
#include <cstdio>
#include <cwchar>

namespace {

HBITMAP g_bmp = nullptr;
int g_tick = 0;
const COLORREF kA = RGB(60, 160, 60);
const COLORREF kE = RGB(230, 120, 20);
const COLORREF kD = RGB(40, 90, 220);
const COLORREF kMag = RGB(220, 40, 200);

HBITMAP MakeBitmap() {
    HDC dc = GetDC(nullptr);
    HBITMAP b = CreateCompatibleBitmap(dc, 96, 64);
    HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, b);
    RECT r{0, 0, 96, 64}; HBRUSH br = CreateSolidBrush(kMag); FillRect(m, &r, br); DeleteObject(br);
    SelectObject(m, o); DeleteDC(m); ReleaseDC(nullptr, dc);
    return b;
}

bool g_static = false;

void CreateChildren(HWND h) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    CreateWindowExW(0, L"STATIC", L"B: child STATIC text", WS_CHILD | WS_VISIBLE | SS_CENTER,
                    20, 120, 260, 24, h, (HMENU)101, hi, nullptr);
    HWND bm = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_BITMAP,
                              320, 110, 96, 64, h, (HMENU)102, hi, nullptr);
    if (!g_bmp) g_bmp = MakeBitmap();
    SendMessageW(bm, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_bmp);
    if (!g_static) SetTimer(h, 1, 500, nullptr);
}

#pragma comment(lib, "msimg32.lib")
struct Cell { int x; };
HBITMAP MakeDib(int w, int h, COLORREF c, BYTE alpha, void** bits) {
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
    if (b && *bits) {
        DWORD px = (DWORD(alpha) << 24) | (DWORD(GetRValue(c)) << 16) | (DWORD(GetGValue(c)) << 8) | GetBValue(c);
        DWORD* p = static_cast<DWORD*>(*bits); for (int i = 0; i < w * h; ++i) p[i] = px;
    }
    return b;
}
void PaintTransferRow(HDC dc) {
    const int y = 280, w = 90, hgt = 60;
    void* bits = nullptr;

    { HBITMAP b = MakeDib(w, hgt, RGB(240, 220, 40), 255, &bits); HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, b);
      BitBlt(dc, 20, y, w, hgt, m, 0, 0, SRCCOPY); SelectObject(m, o); DeleteDC(m); DeleteObject(b); }

    { BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hgt;
      bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; DWORD* px = new DWORD[w * hgt];
      for (int i = 0; i < w * hgt; ++i) px[i] = 0xFF20D0E0;
      SetDIBitsToDevice(dc, 130, y, w, hgt, 0, 0, 0, hgt, px, &bi, DIB_RGB_COLORS); delete[] px; }

    { BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = -2;
      bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; DWORD px[4] = { 0xFFE02020, 0xFFE02020, 0xFFE02020, 0xFFE02020 };
      StretchDIBits(dc, 240, y, w, hgt, 0, 0, 2, 2, px, &bi, DIB_RGB_COLORS, SRCCOPY); }

    { HBITMAP b = MakeDib(w, hgt, RGB(255, 255, 255), 255, &bits); HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, b);
      BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
      GdiAlphaBlend(dc, 350, y, w, hgt, m, 0, 0, w, hgt, bf); SelectObject(m, o); DeleteDC(m); DeleteObject(b); }

    { TRIVERTEX v[2] = { { 460, y, 0x4000, 0x1000, 0x8000, 0 }, { 460 + w, y + hgt, 0xC000, 0x9000, 0xFF00, 0 } };
      GRADIENT_RECT gr{ 0, 1 }; GdiGradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H); }

    { static HMODULE gp = LoadLibraryW(L"gdiplus.dll");
      using StartupFn = int(WINAPI*)(ULONG_PTR*, const void*, void*);
      using FromHdcFn = int(WINAPI*)(HDC, void**);
      using SolidFn   = int(WINAPI*)(DWORD, void**);
      using FillFn    = int(WINAPI*)(void*, void*, INT, INT, INT, INT);
      using DelGfxFn  = int(WINAPI*)(void*);
      using DelBrFn   = int(WINAPI*)(void*);
      static ULONG_PTR tok = 0;
      if (gp && !tok) { struct { UINT32 v; void* cb; BOOL a, b; } si{ 1, nullptr, FALSE, FALSE }; auto st = (StartupFn)GetProcAddress(gp, "GdiplusStartup"); if (st) st(&tok, &si, nullptr); }
      auto fromHdc = gp ? (FromHdcFn)GetProcAddress(gp, "GdipCreateFromHDC") : nullptr;
      auto solid = gp ? (SolidFn)GetProcAddress(gp, "GdipCreateSolidFill") : nullptr;
      auto fill = gp ? (FillFn)GetProcAddress(gp, "GdipFillRectangleI") : nullptr;
      auto delG = gp ? (DelGfxFn)GetProcAddress(gp, "GdipDeleteGraphics") : nullptr;
      auto delB = gp ? (DelBrFn)GetProcAddress(gp, "GdipDeleteBrush") : nullptr;
      void* g = nullptr; void* br = nullptr;
      if (fromHdc && solid && fill && fromHdc(dc, &g) == 0 && g) {
          solid(0xFF8B4513, &br); if (br) { fill(g, br, 570, y, w, hgt); if (delB) delB(br); }
          if (delG) delG(g);
      } }
}

void PaintAll(HWND h, HDC dc) {
    RECT c; GetClientRect(h, &c);
    RECT a1 = c; a1.bottom -= 40;
    HBRUSH a = CreateSolidBrush(kA); FillRect(dc, &a1, a); DeleteObject(a);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 20, 20, L"A: parent WM_PAINT fill (green). Orange stripe = WM_ERASEBKGND only.", 68);
    RECT d{20, 220, 300, 260}; HBRUSH db = CreateSolidBrush(kD); FillRect(dc, &d, db); DeleteObject(db);
    wchar_t t[64]; swprintf_s(t, L"D: timer repaint %d", g_tick);
    TextOutW(dc, 28, 230, t, (int)wcslen(t));
    TextOutW(dc, 20, 262, L"F BitBlt  G SetDIBits  H StretchDIBits  I AlphaBlend  J Gradient  K GDI+  L GetDC/timer", 84);
    PaintTransferRow(dc);
}
bool Erase(HWND h, HDC dc) {
    RECT c; GetClientRect(h, &c);
    RECT e{0, c.bottom - 40, c.right, c.bottom};
    HBRUSH eb = CreateSolidBrush(kE); FillRect(dc, &e, eb); DeleteObject(eb);
    return true;
}
void Tick(HWND h) {
    ++g_tick;
    RECT d{20, 220, 300, 260};
    InvalidateRect(h, &d, FALSE);

    if (HDC dc = GetDC(h)) {
        RECT r{ 20, 350, 110, 410 }; HBRUSH b = CreateSolidBrush(RGB(0, 150, 150)); FillRect(dc, &r, b); DeleteObject(b);
        wchar_t t[16]; swprintf_s(t, L"L %d", g_tick); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
        TextOutW(dc, 28, 370, t, (int)wcslen(t));
        ReleaseDC(h, dc);
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: CreateChildren(h); return 0;
    case WM_ERASEBKGND: return Erase(h, (HDC)w) ? 1 : 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); PaintAll(h, dc); EndPaint(h, &ps); return 0; }
    case WM_TIMER: Tick(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

INT_PTR CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: CreateChildren(h); return TRUE;
    case WM_ERASEBKGND: Erase(h, (HDC)w); SetWindowLongPtrW(h, DWLP_MSGRESULT, 1); return TRUE;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); PaintAll(h, dc); EndPaint(h, &ps); return TRUE; }
    case WM_TIMER: Tick(h); return TRUE;
    case WM_CLOSE: EndDialog(h, 0); return TRUE;
    }
    return FALSE;
}

#pragma pack(push, 4)
struct DlgTpl { DLGTEMPLATE t; WORD menu, cls; WCHAR title[10]; };
#pragma pack(pop)

}

LRESULT CALLBACK HostChildProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: SetTimer(h, 1, 500, nullptr); return 0;
    case WM_ERASEBKGND: return Erase(h, (HDC)w) ? 1 : 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); PaintAll(h, dc); EndPaint(h, &ps); return 0; }
    case WM_TIMER: Tick(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
LRESULT CALLBACK HostParentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: {
        if (HWND c = GetWindow(h, GW_CHILD)) {
            RECT r; GetClientRect(h, &r);
            SetWindowPos(c, nullptr, 0, 0, r.right, r.bottom, SWP_NOZORDER);
        }
        return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

INT_PTR CALLBACK D7zProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG:
        SetDlgItemTextW(h, 1002, L"C:\\Program Files\\keelpaint");
        SetDlgItemTextW(h, 1004, L"if you can read this, the dialog composited");
        SendDlgItemMessageW(h, 1005, PBM_SETRANGE32, 0, 100);
        SendDlgItemMessageW(h, 1005, PBM_SETPOS, 60, 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDCANCEL || LOWORD(w) == IDOK) { DestroyWindow(h); PostQuitMessage(0); return TRUE; }
        return FALSE;
    case WM_CLOSE: DestroyWindow(h); PostQuitMessage(0); return TRUE;
    }
    (void)l;
    return FALSE;
}

LRESULT CALLBACK CoverProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT c; GetClientRect(h, &c);
        FillRect(dc, &c, (HBRUSH)GetStockObject(WHITE_BRUSH)); EndPaint(h, &ps); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
    const bool dialog = cmd && wcsstr(cmd, L"dialog");

    const bool offscreen = cmd && wcsstr(cmd, L"offscreen");
    const bool covered   = cmd && wcsstr(cmd, L"covered");
    g_static = cmd && wcsstr(cmd, L"static") != nullptr;

    if (cmd && wcsstr(cmd, L"d7z")) {
        const bool noCenter = wcsstr(cmd, L"nocenter") != nullptr;
        const bool useEx    = wcsstr(cmd, L"-ex") != nullptr;
        const bool modal    = wcsstr(cmd, L"modal") != nullptr;

        if (wcsstr(cmd, L"dll")) {
            using SetDefDirsFn = BOOL(WINAPI*)(DWORD);
            auto p = reinterpret_cast<SetDefDirsFn>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories"));
            if (p) p(0x00000800  | 0x00000400 );
        }
        if (wcsstr(cmd, L"com")) CoInitialize(nullptr);
        const int id = noCenter ? 102 : (useEx ? 103 : 101);
        if (modal) { DialogBoxParamW(hi, MAKEINTRESOURCEW(id), nullptr, D7zProc, 0); return 0; }
        HWND h = CreateDialogParamW(hi, MAKEINTRESOURCEW(id), nullptr, D7zProc, 0);
        if (!h) return 1;
        BOOL bRet; MSG msg;
        while ((bRet = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
            if (bRet == -1) return 1;
            if (!IsDialogMessage(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        return 0;
    }
    if (cmd && wcsstr(cmd, L"hosted")) {
        WNDCLASSW pc{}; pc.lpfnWndProc = HostParentProc; pc.hInstance = hi; pc.lpszClassName = L"KeelPaintHost";
        pc.hCursor = LoadCursorW(nullptr, IDC_ARROW); pc.hbrBackground = nullptr; RegisterClassW(&pc);
        WNDCLASSW cc{}; cc.lpfnWndProc = HostChildProc; cc.hInstance = hi; cc.lpszClassName = L"KeelPaintHosted";
        cc.hCursor = pc.hCursor; cc.hbrBackground = nullptr; RegisterClassW(&cc);
        HWND h = CreateWindowExW(0, pc.lpszClassName, L"keelpaint hosted",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                 60, 60, 600, 420, nullptr, nullptr, hi, nullptr);
        RECT r; GetClientRect(h, &r);
        HWND child = CreateWindowExW(0, cc.lpszClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                     0, 0, r.right, r.bottom, h, nullptr, hi, nullptr);
        CreateChildren(child);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return 0;
    }
    if (cmd && wcsstr(cmd, L"busy")) {
        WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"KeelPaintWnd";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
        HWND h = CreateWindowExW(0, wc.lpszClassName, L"keelpaint busy", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 60, 60, 600, 420, nullptr, nullptr, hi, nullptr);
        Sleep(6000);
        InvalidateRect(h, nullptr, TRUE);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return 0;
    }
    if (offscreen || covered) {
        WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"KeelPaintWnd";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
        WNDCLASSW cc{}; cc.lpfnWndProc = CoverProc; cc.hInstance = hi; cc.lpszClassName = L"KeelPaintCover";
        cc.hCursor = wc.hCursor; RegisterClassW(&cc);
        const int x0 = offscreen ? -250 : 60, y0 = offscreen ? -150 : 60;
        HWND h = CreateWindowExW(0, wc.lpszClassName, L"keelpaint", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 x0, y0, 600, 420, nullptr, nullptr, hi, nullptr);
        HWND cover = nullptr;
        if (covered)
            cover = CreateWindowExW(WS_EX_TOPMOST, cc.lpszClassName, L"cover", WS_POPUP | WS_VISIBLE,
                                    40, 40, 320, 240, nullptr, nullptr, hi, nullptr);

        const DWORD until = GetTickCount() + 2000;
        MSG msg;
        while (GetTickCount() < until) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            Sleep(10);
        }
        if (offscreen) SetWindowPos(h, nullptr, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        if (cover) DestroyWindow(cover);
        while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return 0;
    }
    if (dialog) {
        DlgTpl d{};
        d.t.style = DS_MODALFRAME | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
        d.t.cx = 300; d.t.cy = 200;
        d.t.x = 40; d.t.y = 40;
        wcscpy_s(d.title, L"keelpaint");

        struct { DlgTpl h; WORD pt; WCHAR face[14]; } full{ d, 9, L"Segoe UI" };
        full.h.t.style = d.t.style;
        DialogBoxIndirectParamW(hi, reinterpret_cast<LPCDLGTEMPLATEW>(&full), nullptr, DlgProc, 0);
        return 0;
    }
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"KeelPaintWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"keelpaint", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             60, 60, 600, 420, nullptr, nullptr, hi, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    (void)h;
    return 0;
}
