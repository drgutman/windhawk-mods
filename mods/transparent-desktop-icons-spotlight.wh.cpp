// ==WindhawkMod==
// @id              transparent-desktop-icons-spotlight
// @name            Transparent Desktop Icons with Spotlight
// @description     Make desktop icons transparent when idle with an interactive spotlight effect. Hover over the desktop or select icons to reveal them with full clarity and customizable transitions.
// @version         1.0
// @author          drgutman
// @github          https://github.com/drgutman
// @homepage        https://instagram.com/drgutman
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -ldxgi -ld2d1 -ldwrite -ld3d11 -ldcomp -ldwmapi -lgdi32 -lwininet -lpdh -lpowrprof -lshcore -lshlwapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Transparent Desktop Icons with Spotlight

Makes desktop icons semi-transparent when idle, revealing the wallpaper beneath.
When you move your mouse over the desktop, a customizable spotlight effect
illuminates the area around your cursor, revealing icons with full clarity.

As the cursor leaves the desktop or remains idle, the spotlight fades and icons
gradually become transparent again. Selected icons remain opaque with their own 
customizable independent timeouts, blurs, and fade durations.

## Features

* Dedicated render thread for flawless, vsync-matched 60/144+ FPS performance.
* Independent Dual-GPU Blur processing for both the cursor spotlight and selected icons.
* Interactive morphing shapes (square to circle) for both the spotlight and selected areas.
* Persistent selection memory allows deselected icons to gracefully fade away.
* Click-through architecture using WS_DISABLED ensures no interference with native OS interaction.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- idleOpacity: 128
  $name: Idle Opacity
  $description: Opacity of overlay when idle (0 = fully opaque, 255 = fully transparent)
- spotlightRadius: 200
  $name: Spotlight Radius
  $description: Size of the spotlight in pixels
- spotlightRoundness: 100
  $name: Spotlight Roundness (%)
  $description: Shape of the spotlight (0 = square, 100 = circle)
- spotlightBlur: 40
  $name: Spotlight Edge Blur
  $description: Blur radius for soft spotlight edges in pixels (0 = hard edge)
- idleDelay: 2000
  $name: Spotlight Idle Timeout (ms)
  $description: Time in milliseconds before the spotlight begins to fade out
- spotlightFadeDuration: 500
  $name: Spotlight Fade Duration (ms)
  $description: How long it takes for the spotlight to fade in and out
- mouseSmoothing: 10
  $name: Mouse Smoothing
  $description: Smoothing factor for spotlight following (0-20, 0 = no smoothing, 20 = very smooth)
- selectionOpacity: 255
  $name: Selection Opacity
  $description: How opaque icons become when selected (0 = transparent, 255 = fully visible)
- selectionPadding: 12
  $name: Selection Padding
  $description: Extra space around selected icons in pixels
- selectionRoundness: 25
  $name: Selection Roundness (%)
  $description: How rounded the selection box is (0 = sharp square, 100 = pill/circle)
- selectionBlur: 15
  $name: Selection Edge Blur
  $description: Blur radius for the selection reveal box
- selectionTimeout: 0
  $name: Selection Timeout (ms)
  $description: Time to keep the icon visible after deselection
- selectionFadeDuration: 250
  $name: Selection Fade Duration (ms)
  $description: How long the selection area takes to fade out after the timeout
*/
// ==/WindhawkModSettings==

#include <algorithm>
#include <commctrl.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <dxgi1_3.h>
#include <shellscalingapi.h>
#include <windhawk_api.h>
#include <windhawk_utils.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cmath>
#include <vector>

using namespace std::literals;
using Microsoft::WRL::ComPtr;

// Timer IDs
#define TIMER_ID_RECREATE_OVERLAY 4
#define TIMER_ID_STATE_POLL 2

#define WM_APP_CLEANUP (WM_APP + 1)
#define WM_APP_SETTINGS_CHANGED (WM_APP + 2)
#define OVERLAY_WINDOW_CLASS (L"TransparentDesktopSpotlight_" WH_MOD_ID)
#define MESSAGE_WINDOW_CLASS L"TransparentDesktopSpotlight_Message_" WH_MOD_ID

// Direct2D Gaussian Blur CLSID
static const IID kCLSID_D2D1GaussianBlur = {
    0x1feb6d69, 0x2fe6, 0x4ac9,
    {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

////////////////////////////////////////////////////////////////////////////////
// Settings

struct Settings {
    int idleOpacity;
    int spotlightRadius;
    int spotlightRoundness;
    int spotlightBlur;
    int idleDelay;
    int spotlightFadeDuration;
    int mouseSmoothing;
    
    int selectionOpacity;
    int selectionPadding;
    int selectionRoundness;
    int selectionBlur;
    int selectionTimeout;
    int selectionFadeDuration;
} g_settings;

////////////////////////////////////////////////////////////////////////////////
// Global state

// Window handles
HWND g_overlayWnd = nullptr;
HWND g_messageWnd = nullptr;
std::atomic<bool> g_unloading{false};

// Background Render Thread
std::thread g_renderThread;
std::atomic<bool> g_threadStop{false};
std::mutex g_renderMutex;

// DirectX pipeline resources
ComPtr<ID3D11Device> g_d3dDevice;
ComPtr<IDXGIDevice> g_dxgiDevice;
ComPtr<IDXGIFactory2> g_dxgiFactory;
ComPtr<ID2D1Factory1> g_d2dFactory;
ComPtr<ID2D1Device> g_d2dDevice;
ComPtr<IDXGISwapChain1> g_swapChain;
ComPtr<ID2D1DeviceContext> g_dc;
ComPtr<ID2D1Bitmap1> g_targetBitmap;

ComPtr<ID2D1Bitmap1> g_spotlightMaskBitmap; 
ComPtr<ID2D1Bitmap1> g_selectionMaskBitmap; 

ComPtr<IDCompositionDevice> g_compositionDevice;
ComPtr<IDCompositionTarget> g_compositionTarget;
ComPtr<IDCompositionVisual> g_compositionVisual;

// Textures, Brushes, and Filters
ComPtr<ID2D1Bitmap> g_wallpaperBitmap;
ComPtr<ID2D1Effect> g_spotlightBlurEffect;
ComPtr<ID2D1Effect> g_selectionBlurEffect;
ComPtr<ID2D1SolidColorBrush> g_spotlightBrush;
ComPtr<ID2D1SolidColorBrush> g_selectionBrush;

// Animations and states (Render Thread)
D2D1_POINT_2F g_smoothedMousePos = {0.0f, 0.0f};
bool g_isIdle = true;
ULONGLONG g_lastMouseTime = 0;
float g_spotlightFade = 0.0f; 

bool g_hasSelection = false;
ULONGLONG g_lastSelectionTime = 0;
float g_selectionFade = 0.0f;
std::vector<RECT> g_cachedSelectedRects; // Kept alive for fade-out

// UI Thread State polling
std::mutex g_stateMutex;
bool g_isMouseOverDesktop = false;
std::vector<RECT> g_selectedRects;

// DPI scaling factor
float g_dpiScale = 1.0f;

std::atomic<bool> g_initialized{false};

////////////////////////////////////////////////////////////////////////////////
// Forward declarations
void CreateOverlayWindow();
void CreateMessageWindow();
void LoadSettings();
void CaptureWallpaperBitmap();
HWND GetDesktopListView();

////////////////////////////////////////////////////////////////////////////////
// Utility functions

HWND GetDesktopParent() {
    HWND hLV = GetDesktopListView();
    if (hLV) {
        HWND hShellView = GetParent(hLV); 
        if (hShellView) {
            return GetParent(hShellView); 
        }
    }
    return nullptr;
}

float GetMonitorDpiScale(HMONITOR monitor) {
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        return dpiX / 96.0f;
    }
    return 1.0f;
}

HMONITOR GetWorkerWMonitor() {
    HWND hParent = GetDesktopParent();
    if (hParent) {
        return MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
    }
    return MonitorFromPoint({0, 0}, MONITOR_DEFAULTTONEAREST);
}

HWND GetDesktopListView() {
    HWND hShellView = nullptr;
    HWND hProgman = FindWindow(L"Progman", nullptr);
    if (hProgman) {
        hShellView = FindWindowEx(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);
    }

    if (!hShellView) {
        HWND hWorkerW = nullptr;
        while ((hWorkerW = FindWindowEx(nullptr, hWorkerW, L"WorkerW", nullptr)) != nullptr) {
            hShellView = FindWindowEx(hWorkerW, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hShellView) break;
        }
    }

    if (hShellView) {
        return FindWindowEx(hShellView, nullptr, L"SysListView32", nullptr);
    }
    return nullptr;
}

bool IsFolderViewWnd(HWND hWnd) {
    WCHAR buffer[64];
    if (!GetClassName(hWnd, buffer, ARRAYSIZE(buffer)) || _wcsicmp(buffer, L"SysListView32")) return false;
    if (!GetWindowText(hWnd, buffer, ARRAYSIZE(buffer)) || _wcsicmp(buffer, L"FolderView")) return false;
    HWND hParent = GetAncestor(hWnd, GA_PARENT);
    if (!hParent) return false;
    if (!GetClassName(hParent, buffer, ARRAYSIZE(buffer)) || _wcsicmp(buffer, L"SHELLDLL_DefView")) return false;
    return true;
}

HMODULE GetCurrentModuleHandle() {
    HMODULE module;
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"", &module)) return nullptr;
    return module;
}

////////////////////////////////////////////////////////////////////////////////
// UI Thread Polling

void PollDesktopState() {
    // Cache the window handles so we don't spam FindWindow globally!
    static HWND s_hwndLV = nullptr;
    static HWND s_hwndShellView = nullptr;
    static HWND s_hwndWorkerW = nullptr;

    if (!s_hwndLV || !IsWindow(s_hwndLV)) {
        s_hwndLV = GetDesktopListView();
        if (s_hwndLV) {
            s_hwndShellView = GetParent(s_hwndLV);
            s_hwndWorkerW = s_hwndShellView ? GetParent(s_hwndShellView) : nullptr;
        } else {
            return; // Desktop not ready yet
        }
    }

    POINT pt;
    GetCursorPos(&pt);
    HWND hWndAtCursor = WindowFromPoint(pt);

    bool overDesktop = (hWndAtCursor == s_hwndLV || hWndAtCursor == s_hwndShellView || 
                        hWndAtCursor == s_hwndWorkerW || hWndAtCursor == g_overlayWnd);

    std::vector<RECT> selected;
    if (s_hwndLV) {
        int idx = -1;
        while ((idx = (int)SendMessage(s_hwndLV, LVM_GETNEXTITEM, idx, LVNI_SELECTED)) != -1) {
            RECT rcBounds = {};
            rcBounds.left = LVIR_BOUNDS;
            if (SendMessage(s_hwndLV, LVM_GETITEMRECT, idx, (LPARAM)&rcBounds)) {
                POINT ptTL = { rcBounds.left, rcBounds.top };
                POINT ptBR = { rcBounds.right, rcBounds.bottom };
                MapWindowPoints(s_hwndLV, g_overlayWnd, &ptTL, 1);
                MapWindowPoints(s_hwndLV, g_overlayWnd, &ptBR, 1);
                selected.push_back({ptTL.x, ptTL.y, ptBR.x, ptBR.y});
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_isMouseOverDesktop = overDesktop;
    g_selectedRects = std::move(selected);
}

////////////////////////////////////////////////////////////////////////////////
// DirectX initialization

bool InitDirectX() {
    HRESULT hr;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                           nullptr, 0, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) return false;

    hr = g_d3dDevice.As(&g_dxgiDevice);
    if (FAILED(hr)) return false;

    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&g_dxgiFactory));
    if (FAILED(hr)) return false;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&g_d2dFactory));
    if (FAILED(hr)) return false;

    hr = g_d2dFactory->CreateDevice(g_dxgiDevice.Get(), &g_d2dDevice);
    if (FAILED(hr)) return false;

    return true;
}

void UninitDirectX() {
    g_d2dDevice.Reset();
    g_d2dFactory.Reset();
    g_dxgiFactory.Reset();
    g_dxgiDevice.Reset();
    g_d3dDevice.Reset();
}

////////////////////////////////////////////////////////////////////////////////
// Wallpaper capture utilizing GDI PaintDesktop

void CaptureWallpaperBitmap() {
    if (!g_overlayWnd || !g_dc) return;

    RECT rc;
    GetClientRect(g_overlayWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) { ReleaseDC(nullptr, hdcScreen); return; }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; 
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hBmp) {
        DeleteDC(hdcMem); ReleaseDC(nullptr, hdcScreen); return;
    }

    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    POINT ptOrg = {0, 0};
    ClientToScreen(g_overlayWnd, &ptOrg);
    SetWindowOrgEx(hdcMem, ptOrg.x, ptOrg.y, nullptr);

    PaintDesktop(hdcMem);
    GdiFlush();

    BYTE* pixels = static_cast<BYTE*>(pvBits);
    for (int i = 0; i < w * h; i++) pixels[i * 4 + 3] = 255;

    D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_dc->CreateBitmap(D2D1::SizeU(w, h), pvBits, w * 4, bitmapProps, &g_wallpaperBitmap);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void RecreateBrushesAndMask(UINT width, UINT height) {
    if (!g_dc) return;

    D2D1_BITMAP_PROPERTIES1 maskProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, 
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    
    g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &maskProps, &g_spotlightMaskBitmap);
    g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &maskProps, &g_selectionMaskBitmap);

    g_dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0f), &g_selectionBrush);
    g_dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.0f), &g_spotlightBrush);
}

bool CreateSwapChainResources(UINT width, UINT height) {
    HRESULT hr;
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = g_dxgiFactory->CreateSwapChainForComposition(g_dxgiDevice.Get(), &scd, nullptr, &g_swapChain);
    if (FAILED(hr)) return false;

    hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface2> surface;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = g_dc->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProperties, &g_targetBitmap);
    if (FAILED(hr)) return false;

    hr = DCompositionCreateDevice(g_dxgiDevice.Get(), IID_PPV_ARGS(&g_compositionDevice));
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->CreateTargetForHwnd(g_overlayWnd, TRUE, &g_compositionTarget);
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->CreateVisual(&g_compositionVisual);
    if (FAILED(hr)) return false;

    hr = g_compositionVisual->SetContent(g_swapChain.Get());
    if (FAILED(hr)) return false;

    hr = g_compositionTarget->SetRoot(g_compositionVisual.Get());
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->Commit();
    if (FAILED(hr)) return false;

    g_dpiScale = GetMonitorDpiScale(GetWorkerWMonitor());

    g_dc->CreateEffect(kCLSID_D2D1GaussianBlur, &g_spotlightBlurEffect);
    if (g_spotlightBlurEffect) g_spotlightBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);

    g_dc->CreateEffect(kCLSID_D2D1GaussianBlur, &g_selectionBlurEffect);
    if (g_selectionBlurEffect) g_selectionBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);

    RecreateBrushesAndMask(width, height);
    CaptureWallpaperBitmap();

    return true;
}

void ReleaseSwapChainResources() {
    g_selectionBrush.Reset();
    g_spotlightBrush.Reset();
    g_spotlightMaskBitmap.Reset();
    g_selectionMaskBitmap.Reset();
    g_spotlightBlurEffect.Reset();
    g_selectionBlurEffect.Reset();
    g_wallpaperBitmap.Reset();
    g_targetBitmap.Reset();
    g_compositionVisual.Reset();
    g_compositionTarget.Reset();
    g_compositionDevice.Reset();
    g_dc.Reset();
    g_swapChain.Reset();
}

bool ResizeSwapChain(UINT width, UINT height) {
    if (!g_swapChain || !g_dc) return false;

    g_dc->SetTarget(nullptr);
    g_targetBitmap.Reset();
    g_spotlightMaskBitmap.Reset();
    g_selectionMaskBitmap.Reset();

    HRESULT hr = g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface2> surface;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = g_dc->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProperties, &g_targetBitmap);
    if (FAILED(hr)) return false;

    RecreateBrushesAndMask(width, height);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Background Render Thread Functions

void UpdateMouseAndAnimations(float deltaTime) {
    POINT screenPos;
    GetCursorPos(&screenPos);

    static POINT s_lastScreenPos = {0, 0};
    bool mouseMoved = (screenPos.x != s_lastScreenPos.x || screenPos.y != s_lastScreenPos.y);
    s_lastScreenPos = screenPos;

    POINT localPos = screenPos;
    ScreenToClient(g_overlayWnd, &localPos);

    // Delta-time based mouse smoothing
    float lerpFactor = 1.0f;
    if (g_settings.mouseSmoothing > 0) {
        float speed = 30.0f - g_settings.mouseSmoothing; 
        lerpFactor = 1.0f - std::exp(-speed * deltaTime);
    }
    
    g_smoothedMousePos.x += ((float)localPos.x - g_smoothedMousePos.x) * lerpFactor;
    g_smoothedMousePos.y += ((float)localPos.y - g_smoothedMousePos.y) * lerpFactor;

    bool overDesktop = false;
    std::vector<RECT> currentSelectedRects;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        overDesktop = g_isMouseOverDesktop;
        currentSelectedRects = g_selectedRects;
    }

    // --- Spotlight Logic ---
    if (overDesktop && mouseMoved) {
        g_lastMouseTime = GetTickCount64();
        g_isIdle = false;
    }
    ULONGLONG timeSinceLastMove = GetTickCount64() - g_lastMouseTime;
    if (!overDesktop || timeSinceLastMove > (ULONGLONG)g_settings.idleDelay) {
        g_isIdle = true;
    }

    float spotFadeSpeed = (g_settings.spotlightFadeDuration > 0) ? (1.0f / (g_settings.spotlightFadeDuration / 1000.0f)) : 100.0f;
    float targetSpotFade = g_isIdle ? 0.0f : 1.0f;
    
    if (g_spotlightFade < targetSpotFade) g_spotlightFade = (std::min)(g_spotlightFade + spotFadeSpeed * deltaTime, targetSpotFade);
    else if (g_spotlightFade > targetSpotFade) g_spotlightFade = (std::max)(g_spotlightFade - spotFadeSpeed * deltaTime, targetSpotFade);

    // --- Selection Logic ---
    if (!currentSelectedRects.empty()) {
        g_hasSelection = true;
        g_lastSelectionTime = GetTickCount64();
        g_cachedSelectedRects = currentSelectedRects; // Keep them alive for the fade out
    } else {
        ULONGLONG timeSinceSelection = GetTickCount64() - g_lastSelectionTime;
        if (timeSinceSelection > (ULONGLONG)g_settings.selectionTimeout) {
            g_hasSelection = false;
        }
    }

    float selFadeSpeed = (g_settings.selectionFadeDuration > 0) ? (1.0f / (g_settings.selectionFadeDuration / 1000.0f)) : 100.0f;
    float targetSelFade;
    if (g_hasSelection) {
        if (g_settings.selectionFadeDuration > 0 && g_isIdle) {
            targetSelFade = 0.0f;
        } else {
            targetSelFade = 1.0f;
        }
    } else {
        targetSelFade = 0.0f;
    }
    
    if (g_selectionFade < targetSelFade) g_selectionFade = (std::min)(g_selectionFade + selFadeSpeed * deltaTime, targetSelFade);
    else if (g_selectionFade > targetSelFade) g_selectionFade = (std::max)(g_selectionFade - selFadeSpeed * deltaTime, targetSelFade);

    if (!g_hasSelection && g_selectionFade <= 0.0f) {
        g_cachedSelectedRects.clear();
    }
}

void RenderOverlay() {
    if (!g_dc || !g_swapChain || !g_targetBitmap || !g_spotlightMaskBitmap || !g_selectionMaskBitmap) return;

    RECT rc;
    GetClientRect(g_overlayWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;
    if (width == 0 || height == 0) return;

    // 1. Draw Spotlight Mask
    g_dc->SetTarget(g_spotlightMaskBitmap.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (g_spotlightFade > 0.001f && g_spotlightBrush) {
        g_spotlightBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black, g_spotlightFade));
        float radius = (float)g_settings.spotlightRadius * g_dpiScale;
        float cornerRadius = radius * (g_settings.spotlightRoundness / 100.0f);
        
        D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
            D2D1::RectF(g_smoothedMousePos.x - radius, g_smoothedMousePos.y - radius, 
                        g_smoothedMousePos.x + radius, g_smoothedMousePos.y + radius),
            cornerRadius, cornerRadius
        );
        g_dc->FillRoundedRectangle(roundedRect, g_spotlightBrush.Get());
    }
    g_dc->EndDraw();

    // 2. Draw Selection Mask
    g_dc->SetTarget(g_selectionMaskBitmap.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (g_selectionFade > 0.001f && !g_cachedSelectedRects.empty() && g_selectionBrush) {
        // Selection opacity drives the alpha channel of the mask cutout
        float activeAlpha = (g_settings.selectionOpacity / 255.0f) * g_selectionFade;
        g_selectionBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black, activeAlpha));
        
        float pad = (float)g_settings.selectionPadding * g_dpiScale;
        
        for (const auto& rcBounds : g_cachedSelectedRects) {
            float rw = ((rcBounds.right + pad) - (rcBounds.left - pad)) / 2.0f;
            float rh = ((rcBounds.bottom + pad) - (rcBounds.top - pad)) / 2.0f;
            float cornerRadius = (std::min)(rw, rh) * (g_settings.selectionRoundness / 100.0f);

            D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(
                D2D1::RectF((float)rcBounds.left - pad, (float)rcBounds.top - pad,
                            (float)rcBounds.right + pad, (float)rcBounds.bottom + pad),
                cornerRadius, cornerRadius
            );
            g_dc->FillRoundedRectangle(rRect, g_selectionBrush.Get());
        }
    }
    g_dc->EndDraw();

    // 3. Draw Final Composited Screen
    g_dc->SetTarget(g_targetBitmap.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (g_wallpaperBitmap) {
        g_dc->DrawBitmap(g_wallpaperBitmap.Get(),
                         D2D1::RectF(0, 0, (float)width, (float)height),
                         g_settings.idleOpacity / 255.0f);
    }

    // Apply soft-edged masks to punch holes through the overlay
    if (g_spotlightFade > 0.001f && g_spotlightBlurEffect) {
        g_spotlightBlurEffect->SetInput(0, g_spotlightMaskBitmap.Get());
        float blurVal = (std::max)(0.1f, (std::min)((float)g_settings.spotlightBlur * g_dpiScale, 100.0f));
        g_spotlightBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurVal);
        g_dc->DrawImage(g_spotlightBlurEffect.Get(), D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_DESTINATION_OUT);
    }

    if (g_selectionFade > 0.001f && g_selectionBlurEffect) {
        g_selectionBlurEffect->SetInput(0, g_selectionMaskBitmap.Get());
        float blurVal = (std::max)(0.1f, (std::min)((float)g_settings.selectionBlur * g_dpiScale, 100.0f));
        g_selectionBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurVal);
        g_dc->DrawImage(g_selectionBlurEffect.Get(), D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_DESTINATION_OUT);
    }

    g_dc->EndDraw();
    g_swapChain->Present(1, 0); 
}

void RenderLoop() {
    LARGE_INTEGER freq, lastTime, currentTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    while (!g_threadStop) {
        QueryPerformanceCounter(&currentTime);
        float deltaTime = (float)(currentTime.QuadPart - lastTime.QuadPart) / freq.QuadPart;
        lastTime = currentTime;

        if (deltaTime > 0.1f) deltaTime = 0.1f; 

        UpdateMouseAndAnimations(deltaTime);

        {
            std::lock_guard<std::mutex> lock(g_renderMutex);
            if (g_overlayWnd && !g_unloading) {
                RenderOverlay();
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Window procedures

LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER:
            if (!g_unloading && wParam == TIMER_ID_STATE_POLL) {
                PollDesktopState();
            }
            return 0;

        case WM_WINDOWPOSCHANGED: {
            const WINDOWPOS* wp = (const WINDOWPOS*)lParam;
            if (!(wp->flags & SWP_NOSIZE) && !g_unloading) {
                std::lock_guard<std::mutex> lock(g_renderMutex);
                ResizeSwapChain(wp->cx, wp->cy);
                CaptureWallpaperBitmap();
            }
            break;
        }
        case WM_DESTROY:
            g_threadStop = true;
            if (g_renderThread.joinable()) {
                g_renderThread.join();
            }
            
            {
                std::lock_guard<std::mutex> lock(g_renderMutex);
                ReleaseSwapChainResources();
            }
            
            g_overlayWnd = nullptr;
            if (!g_unloading && g_messageWnd) {
                SetTimer(g_messageWnd, TIMER_ID_RECREATE_OVERLAY, 200, nullptr);
            }
            return 0;

        case WM_APP_CLEANUP:
            DestroyWindow(hWnd);
            return 0;

        case WM_APP_SETTINGS_CHANGED: {
            std::lock_guard<std::mutex> lock(g_renderMutex);
            LoadSettings();
            ReleaseSwapChainResources();
            RECT rc;
            GetClientRect(hWnd, &rc);
            CreateSwapChainResources(rc.right - rc.left, rc.bottom - rc.top);
            return 0;
        }
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MessageWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER:
            if (!g_unloading && wParam == TIMER_ID_RECREATE_OVERLAY) {
                KillTimer(hWnd, TIMER_ID_RECREATE_OVERLAY);
                if (g_overlayWnd) { DestroyWindow(g_overlayWnd); }
                CreateOverlayWindow();
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (!g_unloading && (wParam == SPI_SETDESKWALLPAPER || (lParam && wcscmp((LPCWSTR)lParam, L"Desktop") == 0))) {
                if (g_overlayWnd) {
                    std::lock_guard<std::mutex> lock(g_renderMutex);
                    CaptureWallpaperBitmap();
                }
            }
            break;

        case WM_DISPLAYCHANGE:
            if (!g_unloading) {
                SetTimer(hWnd, TIMER_ID_RECREATE_OVERLAY, 500, nullptr);
            }
            break;

        case WM_DESTROY:
            g_messageWnd = nullptr;
            return 0;

        case WM_APP_CLEANUP:
            DestroyWindow(hWnd);
            return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

////////////////////////////////////////////////////////////////////////////////
// Setup

bool RegisterOverlayWindowClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASS wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetCurrentModuleHandle();
    wc.lpszClassName = OVERLAY_WINDOW_CLASS;
    registered = RegisterClass(&wc) != 0;
    return registered;
}

void CreateOverlayWindow() {
    if (g_overlayWnd) return;

    if (!g_initialized) {
        if (!InitDirectX()) return;
        g_initialized = true;
    }

    HWND hParent = GetDesktopParent();
    if (!hParent) return;

    if (!RegisterOverlayWindowClass()) return;

    RECT rc;
    GetWindowRect(hParent, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    g_overlayWnd = CreateWindowEx(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        OVERLAY_WINDOW_CLASS, nullptr, WS_CHILD | WS_VISIBLE | WS_DISABLED, 0, 0, width,
        height, hParent, nullptr, GetCurrentModuleHandle(), nullptr);

    if (!g_overlayWnd) return;

    SetWindowPos(g_overlayWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        if (CreateSwapChainResources(width, height)) {
            g_isIdle = true;
            g_lastMouseTime = GetTickCount64();
            g_spotlightFade = 0.0f;
            g_selectionFade = 0.0f;
            g_hasSelection = false;
            
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(g_overlayWnd, &pt);
            g_smoothedMousePos = { (float)pt.x, (float)pt.y };
            
            g_threadStop = false;
            g_renderThread = std::thread(RenderLoop);
            
            SetTimer(g_overlayWnd, TIMER_ID_STATE_POLL, 50, nullptr); // Dropped to 50ms to guarantee zero UI interference
        }
    }
}

bool RegisterMessageWindowClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASS wc = {};
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = GetCurrentModuleHandle();
    wc.lpszClassName = MESSAGE_WINDOW_CLASS;
    registered = RegisterClass(&wc) != 0;
    return registered;
}

void CreateMessageWindow() {
    if (g_messageWnd) return;
    if (!RegisterMessageWindowClass()) return;
    g_messageWnd = CreateWindowEx(0, MESSAGE_WINDOW_CLASS, nullptr, 0, 0, 0, 0,
                                  0, nullptr, nullptr, GetCurrentModuleHandle(), nullptr);
}

////////////////////////////////////////////////////////////////////////////////
// Hooks

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t CreateWindowExW_Original;

HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, PVOID lpParam) {
    HWND hWnd = CreateWindowExW_Original(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (!hWnd || !IsFolderViewWnd(hWnd)) return hWnd;

    static UINT_PTR s_timer = 0;
    s_timer = SetTimer(nullptr, s_timer, 1000, [](HWND, UINT, UINT_PTR idEvent, DWORD) {
        KillTimer(nullptr, idEvent);
        CreateOverlayWindow();
        CreateMessageWindow();
    });

    return hWnd;
}

////////////////////////////////////////////////////////////////////////////////
// Mod lifecycle

void LoadSettings() {
    g_settings.idleOpacity = std::clamp(Wh_GetIntSetting(L"idleOpacity"), 0, 255);
    g_settings.spotlightRadius = std::max(Wh_GetIntSetting(L"spotlightRadius"), 10);
    g_settings.spotlightRoundness = std::clamp(Wh_GetIntSetting(L"spotlightRoundness"), 0, 100);
    g_settings.spotlightBlur = std::max(Wh_GetIntSetting(L"spotlightBlur"), 0);
    g_settings.idleDelay = std::max(Wh_GetIntSetting(L"idleDelay"), 0);
    g_settings.spotlightFadeDuration = std::max(Wh_GetIntSetting(L"spotlightFadeDuration"), 0);
    g_settings.mouseSmoothing = std::clamp(Wh_GetIntSetting(L"mouseSmoothing"), 0, 20);
    
    g_settings.selectionOpacity = std::clamp(Wh_GetIntSetting(L"selectionOpacity"), 0, 255);
    g_settings.selectionPadding = std::max(Wh_GetIntSetting(L"selectionPadding"), 0);
    g_settings.selectionRoundness = std::clamp(Wh_GetIntSetting(L"selectionRoundness"), 0, 100);
    g_settings.selectionBlur = std::max(Wh_GetIntSetting(L"selectionBlur"), 0);
    g_settings.selectionTimeout = std::max(Wh_GetIntSetting(L"selectionTimeout"), 0);
    g_settings.selectionFadeDuration = std::max(Wh_GetIntSetting(L"selectionFadeDuration"), 0);
}

BOOL Wh_ModInit() {
    LoadSettings();
    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook, (void**)&CreateWindowExW_Original);

    if (GetDesktopListView()) {
        CreateOverlayWindow();
        CreateMessageWindow();
    }
    return TRUE;
}

void Wh_ModUninit() {
    g_unloading = true;
    if (g_overlayWnd) SendMessage(g_overlayWnd, WM_APP_CLEANUP, 0, 0);
    if (g_messageWnd) SendMessage(g_messageWnd, WM_APP_CLEANUP, 0, 0);
    UninitDirectX();
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    if (g_overlayWnd) SendMessage(g_overlayWnd, WM_APP_SETTINGS_CHANGED, 0, 0);
    *bReload = FALSE;
    return TRUE;
}
