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
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>

#include <cstdio>

namespace {

constexpr int kW = 300, kH = 210;

void Log(const wchar_t* fmt, ...) {
    wchar_t msg[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);
    wchar_t line[640];
    const int n = _snwprintf_s(line, _TRUNCATE, L"[dxprobe32 pid=%lu] %s\r\n", GetCurrentProcessId(), msg);
    if (n <= 0) return;
    HANDLE h = CreateFileW(L"C:\\Keel\\native-dxprobe32.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char utf8[1280];
    const int cb = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8), nullptr, nullptr);
    if (cb > 0) { DWORD w = 0; WriteFile(h, utf8, (DWORD)cb, &w, nullptr); }
    CloseHandle(h);
}

struct Probe {
    const wchar_t* tag;
    float rgba[4];
    int x, y;
    HWND hwnd = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* dtarget = nullptr;
    IDCompositionVisual* dvisual = nullptr;
    bool gdi = false, alive = false;
};

Probe g_probe[4] = {
    { L"1 GDI   (red)",    {1.0f, 0.15f, 0.15f, 1.0f},  40, 80 },
    { L"2 BLT   (green)",  {0.15f, 0.85f, 0.25f, 1.0f}, 360, 80 },
    { L"3 FLIP  (blue)",   {0.20f, 0.35f, 1.0f, 1.0f},  40, 330 },
    { L"4 DCOMP (yellow)", {1.0f, 0.85f, 0.10f, 1.0f},  360, 330 },
};

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        auto* p = reinterpret_cast<Probe*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        if (p && p->gdi) {
            RECT rc{}; GetClientRect(h, &rc);
            HBRUSH b = CreateSolidBrush(RGB((int)(p->rgba[0] * 255), (int)(p->rgba[1] * 255), (int)(p->rgba[2] * 255)));
            FillRect(dc, &rc, b);
            DeleteObject(b);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

bool MakeDevice(Probe& p, bool forceWarp) {
    const D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    for (int i = forceWarp ? 1 : 0; i < 2; ++i) {
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(nullptr, types[i], nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr, 0, D3D11_SDK_VERSION, &p.dev, &got, &p.ctx);
        if (SUCCEEDED(hr)) {
            Log(L"%s; D3D11 device driver=%s featureLevel=0x%x", p.tag,
                types[i] == D3D_DRIVER_TYPE_HARDWARE ? L"HARDWARE" : L"WARP", (unsigned)got);
            return true;
        }
        Log(L"%s; D3D11CreateDevice(%s) hr=0x%08lX", p.tag,
            types[i] == D3D_DRIVER_TYPE_HARDWARE ? L"HARDWARE" : L"WARP", hr);
    }
    return false;
}

IDXGIFactory2* FactoryFor(Probe& p) {
    IDXGIDevice* dxdev = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f = nullptr;
    if (SUCCEEDED(p.dev->QueryInterface(IID_PPV_ARGS(&dxdev))) && SUCCEEDED(dxdev->GetAdapter(&ad)))
        ad->GetParent(IID_PPV_ARGS(&f));
    if (ad) ad->Release();
    if (dxdev) dxdev->Release();
    return f;
}

bool MakeRtv(Probe& p) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(p.sc->GetBuffer(0, IID_PPV_ARGS(&back)))) { Log(L"%s; GetBuffer failed", p.tag); return false; }
    const HRESULT hr = p.dev->CreateRenderTargetView(back, nullptr, &p.rtv);
    back->Release();
    if (FAILED(hr)) { Log(L"%s; CreateRenderTargetView hr=0x%08lX", p.tag, hr); return false; }
    return true;
}

bool SetupHwndSwapChain(Probe& p, bool flip, bool forceWarp) {
    if (!MakeDevice(p, forceWarp)) return false;
    IDXGIFactory2* f = FactoryFor(p);
    if (!f) { Log(L"%s; no IDXGIFactory2", p.tag); return false; }
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = kW; d.Height = kH;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = flip ? 2 : 1;
    d.SwapEffect = flip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr = f->CreateSwapChainForHwnd(p.dev, p.hwnd, &d, nullptr, nullptr, &p.sc);
    if (FAILED(hr) && flip) {
        Log(L"%s; FLIP_DISCARD hr=0x%08lX, retrying FLIP_SEQUENTIAL", p.tag, hr);
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = f->CreateSwapChainForHwnd(p.dev, p.hwnd, &d, nullptr, nullptr, &p.sc);
    }
    f->Release();
    if (FAILED(hr)) { Log(L"%s; CreateSwapChainForHwnd hr=0x%08lX", p.tag, hr); return false; }
    Log(L"%s; swap chain created (swapEffect=%d bufferCount=%u)", p.tag, (int)d.SwapEffect, d.BufferCount);
    return MakeRtv(p);
}

bool SetupDComp(Probe& p, bool forceWarp) {
    if (!MakeDevice(p, forceWarp)) return false;
    IDXGIFactory2* f = FactoryFor(p);
    if (!f) { Log(L"%s; no IDXGIFactory2", p.tag); return false; }
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = kW; d.Height = kH;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = f->CreateSwapChainForComposition(p.dev, &d, nullptr, &p.sc);
    f->Release();
    if (FAILED(hr)) { Log(L"%s; CreateSwapChainForComposition hr=0x%08lX", p.tag, hr); return false; }
    IDXGIDevice* dxdev = nullptr;
    p.dev->QueryInterface(IID_PPV_ARGS(&dxdev));
    hr = DCompositionCreateDevice(dxdev, IID_PPV_ARGS(&p.dcomp));
    if (dxdev) dxdev->Release();
    if (FAILED(hr)) { Log(L"%s; DCompositionCreateDevice hr=0x%08lX", p.tag, hr); return false; }
    hr = p.dcomp->CreateTargetForHwnd(p.hwnd, TRUE, &p.dtarget);
    if (SUCCEEDED(hr)) hr = p.dcomp->CreateVisual(&p.dvisual);
    if (SUCCEEDED(hr)) hr = p.dvisual->SetContent(p.sc);
    if (SUCCEEDED(hr)) hr = p.dtarget->SetRoot(p.dvisual);
    if (SUCCEEDED(hr)) hr = p.dcomp->Commit();
    if (FAILED(hr)) { Log(L"%s; DComp tree hr=0x%08lX", p.tag, hr); return false; }
    Log(L"%s; DirectComposition tree committed", p.tag);
    return MakeRtv(p);
}

void Frame(Probe& p) {
    if (!p.rtv) return;
    p.ctx->OMSetRenderTargets(1, &p.rtv, nullptr);
    p.ctx->ClearRenderTargetView(p.rtv, p.rgba);
    p.sc->Present(1, 0);
    if (p.dcomp) p.dcomp->Commit();
}

}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    const bool forceWarp = cmd && wcsstr(cmd, L"--warp") != nullptr;
    int seconds = 600;
    if (cmd) if (const wchar_t* s = wcsstr(cmd, L"--seconds")) seconds = _wtoi(s + 9);
    Log(L"keeldxprobe32; forceWarp=%d seconds=%d", forceWarp ? 1 : 0, seconds);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"KeelDxProbe32";
    RegisterClassExW(&wc);

    g_probe[0].gdi = g_probe[0].alive = true;

    for (auto& p : g_probe) {
        RECT rc{ 0, 0, kW, kH };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        p.hwnd = CreateWindowExW(0, L"KeelDxProbe32", p.tag, WS_OVERLAPPEDWINDOW,
                                 p.x, p.y, rc.right - rc.left, rc.bottom - rc.top,
                                 nullptr, nullptr, inst, nullptr);
        if (!p.hwnd) { Log(L"%s; CreateWindow failed %lu", p.tag, GetLastError()); continue; }
        SetWindowLongPtrW(p.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&p));
        ShowWindow(p.hwnd, SW_SHOW);
        UpdateWindow(p.hwnd);
    }

    g_probe[1].alive = SetupHwndSwapChain(g_probe[1], false, forceWarp);
    g_probe[2].alive = SetupHwndSwapChain(g_probe[2], true, forceWarp);
    g_probe[3].alive = SetupDComp(g_probe[3], forceWarp);
    if (!g_probe[3].alive) {
        Log(L"%s; DComp unavailable so falling back to an HWND swap chain as a browser would", g_probe[3].tag);
        if (g_probe[3].sc) { g_probe[3].sc->Release(); g_probe[3].sc = nullptr; }
        if (g_probe[3].dev) { g_probe[3].dev->Release(); g_probe[3].dev = nullptr; }
        if (g_probe[3].ctx) { g_probe[3].ctx->Release(); g_probe[3].ctx = nullptr; }
        g_probe[3].alive = SetupHwndSwapChain(g_probe[3], true, forceWarp);
    }
    for (auto& p : g_probe) Log(L"%s; alive=%d hwnd=%p", p.tag, p.alive ? 1 : 0, p.hwnd);

    const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)seconds * 1000;
    MSG msg;
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        for (auto& p : g_probe) if (p.alive) Frame(p);
        InvalidateRect(g_probe[0].hwnd, nullptr, FALSE);
        Sleep(33);
    }
done:
    Log(L"keeldxprobe32; exiting");
    return 0;
}
