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

// just a test harness.

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d3d9.h>
#include <gl/GL.h>

#include <stdio.h>

#include "keel/log.h"

namespace {

constexpr int kW = 300, kH = 210;

struct Probe {
    const wchar_t* tag;
    float          rgba[4];
    int            x, y;
    HWND           hwnd = nullptr;
    ID3D11Device*  dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* dtarget = nullptr;
    IDCompositionVisual* dvisual = nullptr;

    HDC            gldc = nullptr;
    HGLRC          glrc = nullptr;
    IDirect3D9*    d3d9 = nullptr;
    IDirect3DDevice9* dev9 = nullptr;
    bool           gdi = false;
    bool           alive = false;
};

Probe g_probe[6] = {
    { L"1 GDI   (red)",     {1.0f, 0.15f, 0.15f, 1.0f},  20,  60 },
    { L"2 BLT   (green)",   {0.15f, 0.85f, 0.25f, 1.0f}, 350, 60 },
    { L"3 FLIP  (blue)",    {0.20f, 0.35f, 1.0f, 1.0f},  680, 60 },
    { L"4 DCOMP (yellow)",  {1.0f, 0.85f, 0.10f, 1.0f},  20,  330 },
    { L"5 GL    (cyan)",    {0.15f, 0.85f, 0.95f, 1.0f}, 350, 330 },
    { L"6 D3D9  (magenta)", {0.95f, 0.25f, 0.85f, 1.0f}, 680, 330 },
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
    const int first = forceWarp ? 1 : 0;
    for (int i = first; i < 2; ++i) {
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(nullptr, types[i], nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr, 0, D3D11_SDK_VERSION, &p.dev, &got, &p.ctx);
        if (SUCCEEDED(hr)) {
            KEEL_INFO(L"%s: D3D11 device driver=%s featureLevel=0x%x", p.tag,
                      types[i] == D3D_DRIVER_TYPE_HARDWARE ? L"HARDWARE" : L"WARP", (unsigned)got);
            return true;
        }
        KEEL_WARN(L"%s: D3D11CreateDevice(%s) hr=0x%08lX", p.tag,
                  types[i] == D3D_DRIVER_TYPE_HARDWARE ? L"HARDWARE" : L"WARP", hr);
    }
    return false;
}

IDXGIFactory2* FactoryFor(Probe& p) {
    IDXGIDevice* dxdev = nullptr; IDXGIAdapter* adapter = nullptr; IDXGIFactory2* factory = nullptr;
    if (SUCCEEDED(p.dev->QueryInterface(IID_PPV_ARGS(&dxdev))) && SUCCEEDED(dxdev->GetAdapter(&adapter)))
        adapter->GetParent(IID_PPV_ARGS(&factory));
    if (adapter) adapter->Release();
    if (dxdev) dxdev->Release();
    return factory;
}

bool MakeRtv(Probe& p) {
    ID3D11Texture2D* back = nullptr;
    HRESULT hr = p.sc->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr)) { KEEL_WARN(L"%s: GetBuffer hr=0x%08lX", p.tag, hr); return false; }
    hr = p.dev->CreateRenderTargetView(back, nullptr, &p.rtv);
    back->Release();
    if (FAILED(hr)) { KEEL_WARN(L"%s: CreateRenderTargetView hr=0x%08lX", p.tag, hr); return false; }
    return true;
}

bool SetupHwndSwapChain(Probe& p, bool flip, bool forceWarp) {
    if (!MakeDevice(p, forceWarp)) return false;
    IDXGIFactory2* factory = FactoryFor(p);
    if (!factory) { KEEL_WARN(L"%s: no IDXGIFactory2", p.tag); return false; }

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = kW; d.Height = kH;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = flip ? 2 : 1;
    d.SwapEffect = flip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr = factory->CreateSwapChainForHwnd(p.dev, p.hwnd, &d, nullptr, nullptr, &p.sc);
    if (FAILED(hr) && flip) {
        KEEL_WARN(L"%s: FLIP_DISCARD hr=0x%08lX, retrying FLIP_SEQUENTIAL", p.tag, hr);
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChainForHwnd(p.dev, p.hwnd, &d, nullptr, nullptr, &p.sc);
    }
    factory->Release();
    if (FAILED(hr)) { KEEL_WARN(L"%s: CreateSwapChainForHwnd hr=0x%08lX", p.tag, hr); return false; }
    KEEL_INFO(L"%s: swap chain created (swapEffect=%d bufferCount=%u)", p.tag, (int)d.SwapEffect, d.BufferCount);
    return MakeRtv(p);
}

bool SetupDComp(Probe& p, bool forceWarp) {
    if (!MakeDevice(p, forceWarp)) return false;
    IDXGIFactory2* factory = FactoryFor(p);
    if (!factory) { KEEL_WARN(L"%s: no IDXGIFactory2", p.tag); return false; }

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = kW; d.Height = kH;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = factory->CreateSwapChainForComposition(p.dev, &d, nullptr, &p.sc);
    factory->Release();
    if (FAILED(hr)) { KEEL_WARN(L"%s: CreateSwapChainForComposition hr=0x%08lX", p.tag, hr); return false; }

    IDXGIDevice* dxdev = nullptr;
    p.dev->QueryInterface(IID_PPV_ARGS(&dxdev));
    hr = DCompositionCreateDevice(dxdev, IID_PPV_ARGS(&p.dcomp));
    if (dxdev) dxdev->Release();
    if (FAILED(hr)) { KEEL_WARN(L"%s: DCompositionCreateDevice hr=0x%08lX", p.tag, hr); return false; }
    hr = p.dcomp->CreateTargetForHwnd(p.hwnd, TRUE, &p.dtarget);
    if (FAILED(hr)) { KEEL_WARN(L"%s: CreateTargetForHwnd hr=0x%08lX", p.tag, hr); return false; }
    hr = p.dcomp->CreateVisual(&p.dvisual);
    if (SUCCEEDED(hr)) hr = p.dvisual->SetContent(p.sc);
    if (SUCCEEDED(hr)) hr = p.dtarget->SetRoot(p.dvisual);
    if (SUCCEEDED(hr)) hr = p.dcomp->Commit();
    if (FAILED(hr)) { KEEL_WARN(L"%s: DComp tree hr=0x%08lX", p.tag, hr); return false; }
    KEEL_INFO(L"%s: DirectComposition tree committed", p.tag);
    return MakeRtv(p);
}

volatile LONG g_frame = 0;

float Wave() {
    const LONG n = g_frame;
    const float t = (float)((n / 2) % 100) / 100.0f;
    return 0.35f + 0.65f * (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f);
}

bool SetupGL(Probe& p) {
    p.gldc = GetDC(p.hwnd);
    if (!p.gldc) { KEEL_WARN(L"%s: GetDC failed", p.tag); return false; }
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    const int fmt = ChoosePixelFormat(p.gldc, &pfd);
    if (!fmt || !SetPixelFormat(p.gldc, fmt, &pfd)) { KEEL_WARN(L"%s: pixel format failed %lu", p.tag, GetLastError()); return false; }
    p.glrc = wglCreateContext(p.gldc);
    if (!p.glrc) { KEEL_WARN(L"%s: wglCreateContext failed %lu", p.tag, GetLastError()); return false; }
    if (!wglMakeCurrent(p.gldc, p.glrc)) { KEEL_WARN(L"%s: wglMakeCurrent failed", p.tag); return false; }

    const char* ven = (const char*)glGetString(GL_VENDOR);
    const char* ren = (const char*)glGetString(GL_RENDERER);
    KEEL_INFO(L"%s: OpenGL vendor='%S' renderer='%S' (GDI Generic == software fallback)", p.tag,
              ven ? ven : "?", ren ? ren : "?");
    wglMakeCurrent(nullptr, nullptr);
    return true;
}

bool SetupD3D9(Probe& p) {
    p.d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!p.d3d9) { KEEL_WARN(L"%s: Direct3DCreate9 failed", p.tag); return false; }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = p.hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = p.d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, p.hwnd,
                                      D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &p.dev9);
    if (FAILED(hr)) {
        KEEL_WARN(L"%s: D3D9 HAL device hr=0x%08lX, retrying REF", p.tag, hr);
        hr = p.d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, p.hwnd,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &p.dev9);
    }
    if (FAILED(hr)) { KEEL_WARN(L"%s: CreateDevice hr=0x%08lX", p.tag, hr); return false; }
    D3DADAPTER_IDENTIFIER9 id{};
    if (SUCCEEDED(p.d3d9->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id)))
        KEEL_INFO(L"%s: D3D9 adapter '%S' driver '%S'", p.tag, id.Description, id.Driver);
    return true;
}

void FrameGL(Probe& p) {
    if (!p.glrc || !wglMakeCurrent(p.gldc, p.glrc)) return;
    const float k = Wave();
    glClearColor(p.rgba[0] * k, p.rgba[1] * k, p.rgba[2] * k, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers(p.gldc);
    wglMakeCurrent(nullptr, nullptr);
}

void FrameD3D9(Probe& p) {
    if (!p.dev9) return;
    const float k = Wave();
    const D3DCOLOR c = D3DCOLOR_XRGB((int)(p.rgba[0] * k * 255), (int)(p.rgba[1] * k * 255), (int)(p.rgba[2] * k * 255));
    const HRESULT hrClear = p.dev9->Clear(0, nullptr, D3DCLEAR_TARGET, c, 1.0f, 0);
    const HRESULT hrPresent = p.dev9->Present(nullptr, nullptr, nullptr, nullptr);
    static HRESULT lastClear = 1, lastPresent = 1;
    static LONG okPresents = 0;
    if (SUCCEEDED(hrPresent)) ++okPresents;
    if (hrClear != lastClear || hrPresent != lastPresent) {
        KEEL_INFO(L"%s: Clear=0x%08lX Present=0x%08lX (changed at frame %ld)", p.tag, hrClear, hrPresent, g_frame);
        lastClear = hrClear; lastPresent = hrPresent;
    }
    if ((g_frame % 60) == 0)
        KEEL_INFO(L"%s: %ld successful Presents so far, colour now 0x%06lX", p.tag, okPresents, (unsigned long)c);
}

void Frame(Probe& p) {
    if (!p.rtv) return;

    const LONG n = g_frame;
    const float t = (float)((n / 2) % 100) / 100.0f;
    (void)n; (void)t;
    const float k = Wave();
    const float c[4] = { p.rgba[0] * k, p.rgba[1] * k, p.rgba[2] * k, 1.0f };
    p.ctx->OMSetRenderTargets(1, &p.rtv, nullptr);
    p.ctx->ClearRenderTargetView(p.rtv, c);
    p.sc->Present(1, 0);
    if (p.dcomp) p.dcomp->Commit();
}

}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    keel::LogInit(L"dxprobe");
    const bool forceWarp = cmd && wcsstr(cmd, L"--warp") != nullptr;
    int seconds = 600;
    if (cmd) if (const wchar_t* s = wcsstr(cmd, L"--seconds")) seconds = _wtoi(s + 9);
    KEEL_INFO(L"keeldxprobe: forceWarp=%d seconds=%d", forceWarp ? 1 : 0, seconds);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"KeelDxProbe";
    RegisterClassExW(&wc);

    g_probe[0].gdi = g_probe[0].alive = true;

    for (auto& p : g_probe) {
        RECT rc{0, 0, kW, kH};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        p.hwnd = CreateWindowExW(0, L"KeelDxProbe", p.tag, WS_OVERLAPPEDWINDOW,
                                 p.x, p.y, rc.right - rc.left, rc.bottom - rc.top,
                                 nullptr, nullptr, inst, nullptr);
        if (!p.hwnd) { KEEL_ERROR(L"%s: CreateWindow failed %lu", p.tag, GetLastError()); continue; }
        SetWindowLongPtrW(p.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&p));
        ShowWindow(p.hwnd, SW_SHOW);
        UpdateWindow(p.hwnd);
    }

    g_probe[1].alive = SetupHwndSwapChain(g_probe[1], false, forceWarp);
    g_probe[2].alive = SetupHwndSwapChain(g_probe[2], true, forceWarp);
    g_probe[3].alive = SetupDComp(g_probe[3], forceWarp);
    if (!g_probe[3].alive) {

        KEEL_INFO(L"%s: DComp unavailable; falling back to an HWND swap chain, as a browser would", g_probe[3].tag);
        if (g_probe[3].sc) { g_probe[3].sc->Release(); g_probe[3].sc = nullptr; }
        if (g_probe[3].dev) { g_probe[3].dev->Release(); g_probe[3].dev = nullptr; }
        if (g_probe[3].ctx) { g_probe[3].ctx->Release(); g_probe[3].ctx = nullptr; }
        g_probe[3].alive = SetupHwndSwapChain(g_probe[3], true, forceWarp);
    }
    g_probe[4].alive = SetupGL(g_probe[4]);
    g_probe[5].alive = SetupD3D9(g_probe[5]);
    for (auto& p : g_probe) KEEL_INFO(L"%s: alive=%d hwnd=%p", p.tag, p.alive ? 1 : 0, p.hwnd);

    const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)seconds * 1000;
    MSG msg;
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        InterlockedIncrement(&g_frame);
        for (int i = 1; i <= 3; ++i) if (g_probe[i].alive) Frame(g_probe[i]);
        if (g_probe[4].alive) FrameGL(g_probe[4]);
        if (g_probe[5].alive) FrameD3D9(g_probe[5]);
        InvalidateRect(g_probe[0].hwnd, nullptr, FALSE);

        if ((g_frame % 60) == 0) KEEL_INFO(L"frame %ld presented to all live windows", g_frame);
        Sleep(33);
    }
done:
    KEEL_INFO(L"keeldxprobe: exiting");
    return 0;
}
