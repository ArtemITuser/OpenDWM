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
#include <dxgi1_3.h>

namespace keelshim32 {

void Log(const wchar_t* fmt, ...);
bool WriteStubRet(BYTE* fn, long hr, BYTE argBytes);
bool EpilogueMatches(const BYTE* fn, BYTE expect, BYTE* found);

namespace {

constexpr long kUnsupported = 0x887A0004L;

constexpr int kSlotCreateSwapChain        = 10;
constexpr int kSlotCreateSwapChainForHwnd = 15;
constexpr int kSlotCreateSwapChainForComp = 24;
constexpr int kSlotPresent = 8, kSlotResizeBuffers = 13, kSlotPresent1 = 22;

constexpr UINT kFlipOnlyFlags = (UINT)DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                                (UINT)DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
constexpr UINT kPresentAllowTearing = 0x00000200;

using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, void**);
using CreateForHwndFn   = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IUnknown*, void**);
using CreateForCompFn   = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
                                                      IUnknown*, void**);
using PresentFn         = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT, UINT);
using Present1Fn        = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn   = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

CreateSwapChainFn g_realCreateSwapChain = nullptr;
CreateForHwndFn   g_realCreateForHwnd   = nullptr;
CreateForCompFn   g_realCreateForComp   = nullptr;
PresentFn         g_realPresent         = nullptr;
Present1Fn        g_realPresent1        = nullptr;
ResizeBuffersFn   g_realResizeBuffers   = nullptr;

bool IsFlip(DXGI_SWAP_EFFECT e) {
    return e == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || e == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

void ToBltModel(DXGI_SWAP_CHAIN_DESC1& d) {
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (d.BufferCount == 0 || d.BufferCount > 16) d.BufferCount = 2;
    d.Flags &= ~kFlipOnlyFlags;
    if (d.Scaling == DXGI_SCALING_NONE) d.Scaling = DXGI_SCALING_STRETCH;
    if (d.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED || d.AlphaMode == DXGI_ALPHA_MODE_STRAIGHT)
        d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
}

volatile LONG g_swapchainPatched = 0;

HRESULT STDMETHODCALLTYPE HookedPresent(IUnknown* self, UINT sync, UINT flags) {
    return g_realPresent(self, sync, flags & ~kPresentAllowTearing);
}
HRESULT STDMETHODCALLTYPE HookedPresent1(IUnknown* self, UINT sync, UINT flags,
                                         const DXGI_PRESENT_PARAMETERS* pp) {
    return g_realPresent1(self, sync, flags & ~kPresentAllowTearing, pp);
}
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IUnknown* self, UINT count, UINT w, UINT h,
                                              DXGI_FORMAT fmt, UINT flags) {
    return g_realResizeBuffers(self, count, w, h, fmt, flags & ~kFlipOnlyFlags);
}

bool PatchSlots(void** vt, int highest, const int* slots, void* const* hooks, void** saves, int n) {
    DWORD old = 0;
    const SIZE_T span = sizeof(void*) * (highest + 1);
    if (!VirtualProtect(vt, span, PAGE_READWRITE, &old)) return false;
    for (int i = 0; i < n; ++i) { saves[i] = vt[slots[i]]; vt[slots[i]] = hooks[i]; }
    VirtualProtect(vt, span, old, &old);
    return true;
}

void PatchSwapChainVtable(void* sc) {
    if (!sc) return;
    if (InterlockedCompareExchange(&g_swapchainPatched, 1, 0) != 0) return;
    void** vt = *reinterpret_cast<void***>(sc);
    const int slots[] = { kSlotPresent, kSlotResizeBuffers, kSlotPresent1 };
    void* const hooks[] = { (void*)&HookedPresent, (void*)&HookedResizeBuffers, (void*)&HookedPresent1 };
    void* saves[3] = {};
    if (!PatchSlots(vt, kSlotPresent1, slots, hooks, saves, 3)) {
        Log(L"dxgi; VirtualProtect on the swap chain vtable failed, err=%lu", GetLastError());
        InterlockedExchange(&g_swapchainPatched, 0);
        return;
    }
    g_realPresent = (PresentFn)saves[0];
    g_realResizeBuffers = (ResizeBuffersFn)saves[1];
    g_realPresent1 = (Present1Fn)saves[2];
    Log(L"dxgi; swap chain vtable %p translated (flip-only present flags stripped)", vt);
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(IUnknown* self, IUnknown* dev,
                                                DXGI_SWAP_CHAIN_DESC* desc, void** out) {
    if (desc && IsFlip(desc->SwapEffect)) {
        DXGI_SWAP_CHAIN_DESC d = *desc;
        d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        if (d.BufferCount == 0 || d.BufferCount > 16) d.BufferCount = 2;
        d.Flags &= ~kFlipOnlyFlags;
        const HRESULT hr = g_realCreateSwapChain(self, dev, &d, out);
        Log(L"dxgi; CreateSwapChain flip->blt hwnd=%p hr=0x%08lX", d.OutputWindow, hr);
        if (SUCCEEDED(hr)) { if (out) PatchSwapChainVtable(*out); return hr; }
        Log(L"dxgi; blt rewrite refused so falling back to the caller's own description");
    }
    return g_realCreateSwapChain(self, dev, desc, out);
}

HRESULT STDMETHODCALLTYPE HookedCreateForHwnd(IUnknown* self, IUnknown* dev, HWND hwnd,
                                              const DXGI_SWAP_CHAIN_DESC1* desc,
                                              const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs,
                                              IUnknown* restrictTo, void** out) {
    if (desc && IsFlip(desc->SwapEffect)) {
        DXGI_SWAP_CHAIN_DESC1 d = *desc;
        ToBltModel(d);
        const HRESULT hr = g_realCreateForHwnd(self, dev, hwnd, &d, fs, restrictTo, out);
        Log(L"dxgi; CreateSwapChainForHwnd flip->blt hwnd=%p %ux%u hr=0x%08lX", hwnd, d.Width, d.Height, hr);
        if (SUCCEEDED(hr)) { if (out) PatchSwapChainVtable(*out); return hr; }
        Log(L"dxgi; blt rewrite refused so falling back to the caller's own description");
    }
    return g_realCreateForHwnd(self, dev, hwnd, desc, fs, restrictTo, out);
}

HRESULT STDMETHODCALLTYPE HookedCreateForComp(IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
                                              IUnknown*, void** out) {
    if (out) *out = nullptr;
    Log(L"dxgi; CreateSwapChainForComposition refused (Win7 DXGI has none)");
    return kUnsupported;
}

using NtOpenProcessTokenFn = LONG(NTAPI*)(HANDLE, ACCESS_MASK, PHANDLE);
using NtQueryInfoTokenFn   = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);

bool RunningAsSystemOrService() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto open  = nt ? (NtOpenProcessTokenFn)GetProcAddress(nt, "NtOpenProcessToken") : nullptr;
    auto query = nt ? (NtQueryInfoTokenFn)GetProcAddress(nt, "NtQueryInformationToken") : nullptr;
    if (!open || !query) return true;

    HANDLE tok = nullptr;
    if (open((HANDLE)-1, TOKEN_QUERY, &tok) < 0 || !tok) return true;
    BYTE buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    ULONG got = 0;
    const LONG st = query(tok, 1 , buf, sizeof(buf), &got);
    CloseHandle(tok);
    if (st < 0) return true;

    const BYTE* sid = (const BYTE*)((TOKEN_USER*)buf)->User.Sid;
    if (!sid) return true;
    if (sid[1] != 1 || sid[7] != 5 ) return false;
    DWORD rid = 0;
    memcpy(&rid, sid + 8, sizeof(rid));
    return rid == 18  || rid == 19  || rid == 20 ;
}

bool Disabled() {
    static int cached = -1;
    if (cached < 0) { wchar_t v[8]{}; cached = (GetEnvironmentVariableW(L"KEEL_NO_DXGIFIX", v, 8) > 0 && v[0] == L'1') ? 1 : 0; }
    return cached == 1;
}
bool StandDown() {
    static int cached = -1;
    if (cached < 0) cached = RunningAsSystemOrService() ? 1 : 0;
    return cached == 1;
}

volatile LONG g_dxgiArmed = 0, g_dcompArmed = 0;

DWORD WINAPI ArmThread(LPVOID) {
    __try {
        HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
        if (!dxgi) return 0;
        using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
        auto create = (CreateFactory1Fn)GetProcAddress(dxgi, "CreateDXGIFactory1");
        if (!create) { Log(L"dxgi; CreateDXGIFactory1 not exported so present path not translated"); return 0; }

        const GUID IID_IDXGIFactory2_ = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
        IUnknown* f2 = nullptr;
        const HRESULT hr = create(IID_IDXGIFactory2_, (void**)&f2);
        if (FAILED(hr) || !f2) { Log(L"dxgi; no IDXGIFactory2 on this host (hr=0x%08lX) so nothing to translate", hr); return 0; }

        void** vt = *reinterpret_cast<void***>(f2);
        const int slots[] = { kSlotCreateSwapChain, kSlotCreateSwapChainForHwnd, kSlotCreateSwapChainForComp };
        void* const hooks[] = { (void*)&HookedCreateSwapChain, (void*)&HookedCreateForHwnd, (void*)&HookedCreateForComp };
        void* saves[3] = {};
        if (PatchSlots(vt, kSlotCreateSwapChainForComp, slots, hooks, saves, 3)) {
            g_realCreateSwapChain = (CreateSwapChainFn)saves[0];
            g_realCreateForHwnd   = (CreateForHwndFn)saves[1];
            g_realCreateForComp   = (CreateForCompFn)saves[2];
            Log(L"dxgi; factory vtable %p translated (flip->blt, no composition swap chains)", vt);
        } else {
            Log(L"dxgi; VirtualProtect on the factory vtable failed, err=%lu", GetLastError());
        }
        f2->Release();
    } __except (EXCEPTION_EXECUTE_HANDLER) {  }
    return 0;
}

}

void InstallDxgiTranslation(HMODULE dxgi) {
    if (!dxgi || Disabled() || StandDown()) return;
    if (InterlockedCompareExchange(&g_dxgiArmed, 1, 0) != 0) return;

    if (HANDLE t = CreateThread(nullptr, 0, ArmThread, nullptr, 0, nullptr)) CloseHandle(t);
    else Log(L"dxgi; CreateThread failed, err=%lu so present path not translated", GetLastError());
}

void InstallDcompRefusal(HMODULE dcomp) {
    if (!dcomp || Disabled() || StandDown()) return;
    if (InterlockedCompareExchange(&g_dcompArmed, 1, 0) != 0) return;
    static const char* const kCreators[] = { "DCompositionCreateDevice", "DCompositionCreateDevice2",
                                             "DCompositionCreateDevice3" };
    for (const char* name : kCreators) {
        auto fn = (BYTE*)GetProcAddress(dcomp, name);
        if (!fn) continue;
        BYTE found = 0;
        if (!EpilogueMatches(fn, 0x0C, &found)) {
            Log(L"dcomp; %S has epilogue ret 0x%02X, expected 0x0C so NOT patching", name, found);
            continue;
        }
        if (WriteStubRet(fn, kUnsupported, 0x0C)) Log(L"dcomp; %S refused (Win7 has no DirectComposition)", name);
        else                                      Log(L"dcomp; %S could not be patched, err=%lu", name, GetLastError());
    }
}

}
