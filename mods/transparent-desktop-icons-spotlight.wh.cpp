// ==WindhawkMod==
// @id              transparent-desktop-icons-spotlight
// @name            "Ab Tenebris" - Transparent Desktop Icons with Spotlight and Dynamic Shadows
// @description     Make desktop icons transparent when idle with an interactive spotlight effect, plus the Ab Tenebris control panel: tray icon and native settings window. Hover over the desktop or select icons to reveal them.
// @version         2.10.3
// @author          drgutman
// @github          https://github.com/drgutman
// @homepage        https://instagram.com/drgutman
// @include         explorer.exe
// @include         windhawk.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -ldxgi -ld2d1 -ld3d11 -ldcomp -lgdi32 -lshcore -lwindowscodecs -lole32 -luuid -lshell32 -ladvapi32 -lcomdlg32 -luxtheme -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*

# AB TENEBRIS - Transparent Desktop Icons with Spotlight and Dynamic Shadows

## SETTINGS
All settings live in the **Ab Tenebris settings window** — open it from the
tray icon this mod adds to the taskbar (left-click the icon, or right-click
it and choose Settings). The Windhawk settings page is intentionally empty.


Ab tenebris - latin for "out of the darkness"

Makes desktop icons semi-transparent when idle, revealing the wallpaper beneath.
When you move your mouse over the desktop, a customizable spotlight effect
illuminates the area around your cursor, revealing icons with full clarity.

As the cursor leaves the desktop or remains idle, the spotlight fades and icons
gradually become transparent again. Selected icons remain opaque with their own
customizable independent timeouts, blurs, and fade durations.


# !!! WARNING !!!
**The Shadows feature will eat your videocard and your cat**

It won't but! be very careful with how much you increase the Shadow quality/fps.

Depending on:
 - how many icons you have on the desktop
 - what's their base resolution
 - how unique they are (if they're a bunch of folders with the default windows icon and no previews, you'll be ok)
 - how much blur and tail softness you add
 - how big your spotlight is
 - what fps you set

everything adds up very quickly to the videocard stress.

Open up task manager and **Keep an eye on it when you make changes**
And don't come back to me crying if you set it on fire.

**Demonstration**: in this case the selected icons have a timeout (set it to 0 to keep them active)

![Demonstration:](https://i.imgur.com/7cTTHTN.gif)




*/
// ==/WindhawkModReadme==


#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define _WIN32_IE 0x0A00

#include <algorithm>
#include <cstdarg>
#include <exception>
#include <commctrl.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <d3d11_4.h> // ID3D11Multithread (missing from the bundled d3d11.h)
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_3.h>
#include <shellscalingapi.h>
#include <windhawk_api.h>
#include <windhawk_utils.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <cmath>
#include <vector>
#include <chrono>
#include <random>
#include <wincodec.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <string>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
// Control-panel section needs (merged from ab_tenebris_control.cpp):
#include <commdlg.h>
#include <uxtheme.h>
#include <dwmapi.h>

// ===== MinGW fallback definitions for missing shell interfaces =====
#ifndef SWC_DESKTOP
#define SWC_DESKTOP 8
#endif
#ifndef SWFO_NEEDDISPATCH
#define SWFO_NEEDDISPATCH 1
#endif

#ifndef __IShellWindows_INTERFACE_DEFINED__
#define __IShellWindows_INTERFACE_DEFINED__
#if defined(__cplusplus) && !defined(CINTERFACE)
struct IShellWindows : public IDispatch {
    STDMETHOD(FindWindowSW)(VARIANT* pvarloc, VARIANT* pvarlocRoot, LONG swCls, LONG* phwnd, LONG swfwOptions, IDispatch** ppdispOut) = 0;
};
#else
typedef struct {
    IDispatchVtbl IDispatchVtbl;
    HRESULT (STDMETHODCALLTYPE *FindWindowSW)(IShellWindows* This, VARIANT* pvarloc, VARIANT* pvarlocRoot, LONG swCls, LONG* phwnd, LONG swfwOptions, IDispatch** ppdispOut);
} IShellWindowsVtbl;
struct IShellWindows { const IShellWindowsVtbl* lpVtbl; };
#endif
#endif

#ifndef CLSID_ShellWindows
DEFINE_GUID(CLSID_ShellWindows, 0x9BA05972, 0xF6A8, 0x11CF, 0xA4, 0x42, 0x00, 0xA0, 0xC9, 0x0A, 0x8F, 0x39);
#endif
// Register IShellWindows with MinGW's __uuidof / __mingw_uuidof for IID_PPV_ARGS
#ifdef __MINGW32__
template<>
inline const GUID& __mingw_uuidof<IShellWindows>() {
    static const GUID guid = {0x85CB6900, 0x4D95, 0x11CF, {0x96, 0x0C, 0x00, 0x80, 0xC7, 0xF4, 0xEE, 0x85}};
    return guid;
}
#endif

#ifndef SID_STopLevelBrowser
DEFINE_GUID(SID_STopLevelBrowser, 0x00021490, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#endif
// =====

using namespace std::literals;
using Microsoft::WRL::ComPtr;

// 64x64 Blue Noise Texture (Luminance)
static const uint8_t BLUE_NOISE_64x64[4096] = {
    0xAF, 0xDF, 0x35, 0x98, 0xB6, 0x2B, 0x79, 0xD3, 0x62, 0xA9, 0x91, 0x37, 0xA1, 0x01, 0xD5, 0xA8,
    0xF8, 0x25, 0x0B, 0xA5, 0xD8, 0x2A, 0x72, 0xEC, 0x3D, 0xB6, 0x4C, 0x74, 0xF2, 0x5B, 0x88, 0xD6,
    0x99, 0xC6, 0x20, 0xE5, 0x5A, 0xF7, 0x05, 0xC6, 0xE3, 0x6D, 0xB6, 0x5B, 0xE6, 0x68, 0xA5, 0xB5,
    0x41, 0xC8, 0xAA, 0xE0, 0xBC, 0x3D, 0xC7, 0x17, 0x33, 0x5F, 0x46, 0x76, 0xD2, 0x38, 0xBA, 0x06,
    0x72, 0xA1, 0x57, 0x19, 0x46, 0xE5, 0x8E, 0x21, 0xB9, 0x0B, 0x1D, 0x7E, 0xC7, 0xEB, 0x31, 0x79,
    0x4F, 0xE3, 0xC2, 0x83, 0x46, 0xB9, 0x98, 0x65, 0x23, 0x8E, 0xD4, 0x06, 0xC3, 0x1B, 0xB9, 0x6F,
    0xFF, 0x2E, 0x7D, 0xD4, 0x9D, 0x28, 0x4B, 0x94, 0x37, 0x87, 0x08, 0x2A, 0xCC, 0x36, 0x15, 0x7F,
    0x26, 0xEF, 0x51, 0x1C, 0x7D, 0x5A, 0xE7, 0xA8, 0x93, 0xEE, 0xC9, 0x15, 0x8A, 0x51, 0x63, 0xFA,
    0x14, 0x40, 0xF5, 0xCF, 0x83, 0xA6, 0xFB, 0x3C, 0x73, 0xEF, 0xE0, 0x67, 0xB4, 0x58, 0x17, 0x88,
    0x69, 0xB3, 0x36, 0x61, 0x19, 0xE7, 0x0E, 0xAE, 0xE1, 0x15, 0x9F, 0x64, 0x41, 0x94, 0x37, 0x0D,
    0x49, 0x62, 0xB5, 0x90, 0x3A, 0xBD, 0x77, 0xEB, 0x21, 0xAE, 0xF6, 0x4B, 0x9B, 0xBE, 0x55, 0xDA,
    0x62, 0x91, 0x35, 0x6B, 0x98, 0x0A, 0x27, 0x4C, 0x6C, 0x08, 0xDF, 0x2A, 0xA5, 0xEA, 0x97, 0x2E,
    0xC7, 0x8B, 0xB8, 0x6C, 0x07, 0x62, 0x12, 0x59, 0x99, 0x4C, 0xCA, 0x3F, 0x25, 0x97, 0xF5, 0x42,
    0xCF, 0x04, 0x91, 0xF3, 0xC7, 0x54, 0x89, 0x38, 0x7B, 0xC9, 0x2E, 0xF8, 0x7D, 0xCD, 0xAA, 0xEC,
    0x85, 0xE3, 0x00, 0x52, 0x1A, 0xDE, 0x66, 0xA2, 0x55, 0xC2, 0x7C, 0xDC, 0x1A, 0x8B, 0xF7, 0x00,
    0xA0, 0xE4, 0x0E, 0xC1, 0xD6, 0xB1, 0xFA, 0x8B, 0xC2, 0x40, 0x7C, 0x5A, 0xBD, 0x20, 0xD8, 0x7D,
    0xE4, 0x4D, 0x24, 0xEA, 0x33, 0xC5, 0xB3, 0xDC, 0x2A, 0xA4, 0x89, 0x06, 0xD6, 0x70, 0xAC, 0xBD,
    0xE6, 0x21, 0x9E, 0x70, 0x2D, 0xA2, 0xD1, 0xF6, 0x49, 0x59, 0xB3, 0xE6, 0x50, 0x21, 0xDA, 0x59,
    0x14, 0xA0, 0xCB, 0x6D, 0xB0, 0xF1, 0x09, 0xCD, 0x14, 0x3F, 0x60, 0xA7, 0x6C, 0x3D, 0x76, 0xD0,
    0x47, 0xAC, 0x84, 0xF2, 0x43, 0x60, 0x76, 0xDB, 0x1F, 0xB4, 0x9D, 0xF7, 0x6F, 0x49, 0x00, 0xA9,
    0x5C, 0x0C, 0x9D, 0x76, 0xD8, 0x93, 0x47, 0xF3, 0x7C, 0x18, 0xBE, 0xFE, 0x50, 0x7F, 0x0C, 0x2E,
    0x5E, 0x4C, 0x7D, 0xD7, 0x41, 0x07, 0x6A, 0x1A, 0xA9, 0x71, 0x02, 0x8A, 0x0F, 0x9C, 0x74, 0x2C,
    0xBF, 0x40, 0xFA, 0x30, 0x88, 0x43, 0x7F, 0x2D, 0xFE, 0x8F, 0xE5, 0x0E, 0x25, 0xED, 0xB3, 0x30,
    0x20, 0x70, 0x54, 0x2A, 0x12, 0xA3, 0x36, 0x02, 0x55, 0xEF, 0x2F, 0x0E, 0x87, 0xCA, 0x34, 0x69,
    0xBF, 0xF0, 0x3B, 0xAF, 0x53, 0x1F, 0x86, 0x02, 0xD0, 0x6D, 0x31, 0x62, 0x9D, 0x3A, 0xDE, 0x8F,
    0xA5, 0xF0, 0x10, 0xB0, 0xFF, 0xBC, 0x83, 0xEA, 0x99, 0x27, 0xDC, 0xBD, 0x3B, 0x68, 0xF3, 0xB2,
    0x8F, 0x78, 0x22, 0xD5, 0xA8, 0x59, 0xDA, 0x9A, 0xB7, 0x73, 0x33, 0xD3, 0xC4, 0x9D, 0x5D, 0x90,
    0xC8, 0xFE, 0xB8, 0xD0, 0x95, 0xEA, 0x86, 0xCD, 0xA8, 0x66, 0x92, 0xD3, 0x3D, 0xAE, 0xFF, 0x92,
    0x29, 0xD3, 0x83, 0x16, 0xFC, 0x69, 0xA9, 0x3A, 0x5B, 0xE9, 0xAD, 0xDB, 0x1D, 0xC6, 0xF8, 0x17,
    0x6B, 0xC1, 0x39, 0x58, 0x94, 0x25, 0x5F, 0x35, 0xC2, 0x44, 0xFC, 0x5F, 0xA4, 0xD4, 0x49, 0x18,
    0xDF, 0x64, 0x51, 0x96, 0x13, 0xC1, 0x20, 0x64, 0x4C, 0x02, 0xB0, 0x53, 0x80, 0x44, 0x08, 0xE6,
    0x15, 0x63, 0x3C, 0x7A, 0x1C, 0x4A, 0xBF, 0x6E, 0x28, 0xE0, 0x4D, 0x1A, 0xE8, 0x56, 0x79, 0x1B,
    0x42, 0xB6, 0x61, 0x99, 0xC3, 0x2E, 0xE7, 0xBA, 0x97, 0x43, 0x0D, 0x8E, 0xB8, 0x59, 0x76, 0x45,
    0xD0, 0x87, 0x1F, 0xDD, 0x75, 0x47, 0xE3, 0x0A, 0xD6, 0x91, 0x7F, 0x1C, 0x2F, 0xC8, 0x83, 0x09,
    0x38, 0xC6, 0xE7, 0x06, 0xF5, 0x6E, 0x3B, 0xEE, 0xC9, 0x88, 0xF4, 0x97, 0x1C, 0xFA, 0xBB, 0x4D,
    0x82, 0x9E, 0x05, 0xE0, 0xB1, 0x59, 0xFB, 0x14, 0x41, 0x82, 0xC4, 0x72, 0xB8, 0x09, 0xA0, 0xDF,
    0x51, 0x70, 0x03, 0xE3, 0x49, 0x0D, 0x73, 0xCC, 0x23, 0xF4, 0x72, 0x4C, 0x2A, 0x84, 0x00, 0xB2,
    0x31, 0xEC, 0x9C, 0x05, 0xCB, 0xA7, 0x89, 0xB2, 0x69, 0x10, 0x51, 0xAF, 0xEF, 0x93, 0x55, 0xF7,
    0x9B, 0xAC, 0x7D, 0x45, 0xB7, 0x85, 0xA0, 0xDF, 0x18, 0x29, 0x5F, 0xD9, 0x38, 0x79, 0x6A, 0xA9,
    0xD4, 0x31, 0xEF, 0x8B, 0x69, 0x2C, 0xD7, 0x9C, 0xB5, 0x08, 0xF2, 0x96, 0x24, 0x65, 0xCE, 0x86,
    0xA8, 0xF8, 0x36, 0x7E, 0xD4, 0xA2, 0x8B, 0x4E, 0x13, 0x80, 0xA4, 0xE1, 0xCE, 0xF2, 0xA3, 0xE4,
    0x61, 0x50, 0xB7, 0x6A, 0xF6, 0x15, 0x53, 0xED, 0x3D, 0xCE, 0x74, 0xE0, 0x04, 0x6D, 0x23, 0xBA,
    0x71, 0x2B, 0x1B, 0x5D, 0xD0, 0x32, 0x0C, 0x79, 0xAC, 0x40, 0x6F, 0xA4, 0x0D, 0xCA, 0x27, 0xE2,
    0x1B, 0x56, 0xC4, 0x44, 0xA5, 0x0B, 0x8F, 0x78, 0xE5, 0x60, 0x35, 0xAA, 0x47, 0xF6, 0x30, 0x14,
    0x8E, 0xC8, 0x23, 0xB9, 0x5C, 0x1D, 0xF9, 0x63, 0xD7, 0xB2, 0x35, 0x0A, 0x66, 0x3E, 0x20, 0x92,
    0x12, 0x80, 0x27, 0x3F, 0xC0, 0x32, 0x7B, 0x23, 0xA1, 0xBA, 0x2B, 0x9B, 0x3A, 0xC4, 0x60, 0xE9,
    0x40, 0xD7, 0xFB, 0x92, 0xA6, 0xEB, 0x4A, 0x58, 0x93, 0xC4, 0xE6, 0xB9, 0x51, 0xEC, 0x8A, 0x3F,
    0x94, 0xB7, 0x75, 0x22, 0xF6, 0xCB, 0x39, 0x4F, 0x21, 0xCD, 0x8C, 0x57, 0xD9, 0x7A, 0xBC, 0xE7,
    0x0D, 0x67, 0x96, 0xF1, 0x3F, 0xAE, 0x31, 0xC2, 0x93, 0x56, 0xFA, 0xC0, 0x9A, 0x52, 0xC5, 0x71,
    0xD6, 0xA7, 0xF1, 0x8F, 0x5C, 0xD8, 0x95, 0xFD, 0x62, 0x14, 0xE8, 0x81, 0x48, 0xD5, 0x8C, 0x0B,
    0xA1, 0x84, 0x53, 0x02, 0x6B, 0x25, 0xBC, 0xD4, 0xFA, 0x04, 0x20, 0x82, 0x32, 0x9C, 0x5E, 0x01,
    0xFB, 0x66, 0x0E, 0xDC, 0x80, 0x5E, 0xBD, 0xA2, 0x6D, 0xFD, 0x13, 0xC0, 0x04, 0x9A, 0x3E, 0x5A,
    0x2C, 0xDC, 0x4E, 0x76, 0x0A, 0x85, 0xEB, 0x02, 0x41, 0x26, 0x6E, 0x1B, 0x7B, 0xDC, 0x2F, 0xFE,
    0x46, 0x08, 0xCE, 0x74, 0x19, 0xA4, 0x02, 0x45, 0xC2, 0x8B, 0x57, 0xF5, 0xA6, 0x1A, 0xB4, 0x4F,
    0x14, 0xCB, 0x36, 0xC0, 0xE2, 0x82, 0x13, 0x63, 0x30, 0x8C, 0x48, 0x68, 0xF3, 0x15, 0xC3, 0xB0,
    0x7B, 0xD3, 0x37, 0xAC, 0x49, 0x15, 0xE9, 0x01, 0xDA, 0x45, 0x2F, 0x81, 0xEF, 0x1F, 0x6F, 0xB3,
    0x80, 0xA4, 0x19, 0xE6, 0xCD, 0x9E, 0x67, 0x7A, 0xE0, 0xA1, 0xEA, 0x8D, 0xB6, 0x04, 0x86, 0xAD,
    0x58, 0xBB, 0x36, 0xE8, 0x4E, 0xB4, 0xE4, 0x71, 0xD1, 0x38, 0x0A, 0x68, 0x26, 0x75, 0xFF, 0xDE,
    0x67, 0xAE, 0xF0, 0x76, 0x42, 0xB1, 0x9B, 0xEE, 0x76, 0xB5, 0xDF, 0xCF, 0xA8, 0x73, 0xDA, 0x46,
    0x2B, 0x9F, 0x52, 0xF0, 0x99, 0x87, 0x32, 0xAF, 0x79, 0x92, 0xB3, 0x66, 0xA6, 0x4D, 0xCD, 0xFB,
    0x42, 0xC4, 0x34, 0xB4, 0x58, 0x2A, 0x49, 0xB8, 0xC8, 0x11, 0x5F, 0x4A, 0x3A, 0xEE, 0x64, 0x18,
    0x9E, 0x6B, 0x21, 0x89, 0xC8, 0x60, 0x2A, 0x83, 0x1D, 0xAC, 0xDD, 0xB7, 0xCC, 0x98, 0x35, 0x7E,
    0x29, 0x95, 0x1E, 0x5F, 0x10, 0xD1, 0x52, 0x3C, 0x19, 0x9E, 0x57, 0x0B, 0x3C, 0x22, 0x55, 0x86,
    0xE8, 0x18, 0xC0, 0x70, 0x26, 0xD0, 0x64, 0xF5, 0x54, 0x19, 0xC5, 0xE7, 0x38, 0xDA, 0x91, 0x07,
    0xEB, 0x99, 0x69, 0x8A, 0x10, 0xDA, 0xF4, 0x1E, 0x88, 0x31, 0xD5, 0xA9, 0xCF, 0x27, 0xC1, 0xE1,
    0x3F, 0xF6, 0x7D, 0x99, 0x0A, 0x3D, 0xF9, 0x9D, 0x4C, 0xF2, 0x91, 0x53, 0x41, 0x00, 0x5B, 0xC2,
    0x46, 0xE4, 0x8A, 0xA4, 0xFC, 0x2C, 0x90, 0xDC, 0xC3, 0x27, 0xFE, 0x7E, 0x90, 0xB8, 0xF6, 0x0C,
    0xCC, 0x62, 0x8E, 0x05, 0xE2, 0xB8, 0x1C, 0x40, 0x9A, 0xD5, 0x0B, 0x5B, 0x25, 0x77, 0x15, 0x61,
    0x54, 0x23, 0xD0, 0xFF, 0x74, 0x3B, 0x94, 0xA5, 0x53, 0x71, 0xFC, 0x0C, 0x98, 0x54, 0x78, 0x8F,
    0x0F, 0xD4, 0x2C, 0xEB, 0xAD, 0xDA, 0x13, 0xBE, 0x6C, 0x2F, 0x7A, 0x18, 0xEC, 0xDB, 0xA9, 0xF3,
    0x0D, 0xB7, 0x33, 0x4C, 0xBB, 0x7C, 0x0A, 0x6B, 0xAB, 0x45, 0x66, 0xC9, 0x2F, 0xE3, 0x9A, 0x6D,
    0xA9, 0x33, 0xFF, 0x42, 0x58, 0x76, 0xA7, 0xE5, 0x85, 0x2C, 0x72, 0xFA, 0x9E, 0x86, 0xBF, 0xAB,
    0xE0, 0x7B, 0x00, 0x4A, 0xAC, 0xBF, 0x62, 0x05, 0xE8, 0x42, 0xB5, 0x7E, 0x67, 0x1C, 0xF4, 0x47,
    0xB1, 0xC3, 0x5A, 0x49, 0x67, 0x78, 0x8E, 0x56, 0xE9, 0x06, 0xA3, 0xC7, 0x89, 0x6B, 0x22, 0x84,
    0x73, 0xD5, 0x65, 0x04, 0xDF, 0xC8, 0x59, 0xF4, 0x85, 0x15, 0xE7, 0xAF, 0x06, 0x5F, 0x40, 0x1B,
    0x4D, 0x7D, 0xB0, 0xC6, 0x95, 0x37, 0x08, 0xC9, 0x4E, 0xBB, 0x3B, 0xAD, 0xCD, 0x45, 0xF5, 0x30,
    0xB7, 0x3E, 0x8F, 0xED, 0x13, 0x28, 0xCC, 0x7A, 0xD8, 0x19, 0x90, 0x2D, 0xBC, 0xD9, 0xA0, 0x30,
    0x71, 0x07, 0xA3, 0x1D, 0xBB, 0x34, 0xCB, 0x24, 0xD4, 0xB3, 0x3E, 0x5E, 0x2C, 0xBA, 0x4B, 0x97,
    0x19, 0x55, 0xEC, 0x9A, 0x6F, 0x39, 0x23, 0xA1, 0xD5, 0x35, 0x97, 0x50, 0x74, 0xD7, 0xBD, 0x8C,
    0xD0, 0xEA, 0x23, 0x10, 0xD9, 0x81, 0xF9, 0x6B, 0x20, 0xEE, 0x8F, 0x02, 0x52, 0x1D, 0x6D, 0x0B,
    0xD5, 0x65, 0xC7, 0xA0, 0x5A, 0x85, 0xF9, 0x35, 0xB0, 0x5E, 0xC7, 0xF0, 0x3D, 0x01, 0x81, 0xCD,
    0x60, 0xFB, 0x8A, 0xDD, 0xF1, 0x01, 0xA8, 0x45, 0x85, 0x66, 0xFA, 0xD9, 0x10, 0x9F, 0xD2, 0xFC,
    0x3D, 0xCA, 0xA7, 0x1C, 0x87, 0xB1, 0xEB, 0x4B, 0x00, 0xBF, 0x80, 0x21, 0xF1, 0xA3, 0x27, 0xF8,
    0x02, 0x72, 0x9E, 0x66, 0x4A, 0xBD, 0x17, 0x5C, 0xA0, 0xD7, 0x7C, 0x63, 0xDC, 0xEA, 0x93, 0xA3,
    0x84, 0x1A, 0xE5, 0x33, 0x6F, 0xDE, 0x97, 0x4F, 0x22, 0xA0, 0x0A, 0x4C, 0xE2, 0xA9, 0x52, 0xE6,
    0x23, 0x98, 0x38, 0x50, 0x80, 0x95, 0x5D, 0xE6, 0x0D, 0x99, 0x1E, 0x7E, 0xEE, 0x57, 0x35, 0x0A,
    0xB3, 0x7B, 0x2D, 0xF6, 0x43, 0x11, 0x94, 0x76, 0x64, 0xF9, 0x5B, 0xCD, 0x10, 0x49, 0x83, 0x36,
    0xB2, 0x54, 0xE3, 0x2F, 0xF2, 0xA4, 0x8B, 0x2A, 0xB5, 0x43, 0x13, 0x31, 0xB2, 0xC3, 0x3A, 0x57,
    0xFB, 0x2A, 0x4F, 0xB4, 0x18, 0x43, 0x07, 0xBC, 0x6A, 0xF5, 0x87, 0x75, 0x65, 0x8F, 0x17, 0xB2,
    0x43, 0xC8, 0x75, 0x11, 0xD1, 0x29, 0xFF, 0x72, 0xC1, 0x36, 0xAD, 0x48, 0x8D, 0xC2, 0x78, 0x68,
    0xE5, 0x90, 0x4F, 0xC3, 0x61, 0xE1, 0xB9, 0xCF, 0x29, 0xA8, 0x3C, 0xB4, 0x8F, 0x6B, 0xE0, 0x5E,
    0xC3, 0x3F, 0x91, 0x14, 0xCD, 0x73, 0x3A, 0xE2, 0xCA, 0x6F, 0xFE, 0x9C, 0x80, 0x25, 0x73, 0x0F,
    0xB0, 0x98, 0x78, 0xC1, 0xF1, 0xA6, 0xD3, 0x7C, 0xE5, 0x39, 0xCB, 0x26, 0xD2, 0x34, 0xF8, 0x6E,
    0x0E, 0xEC, 0xA6, 0xBA, 0x68, 0x3D, 0xB1, 0x16, 0xDF, 0x54, 0xCF, 0x70, 0x04, 0x26, 0xDD, 0xA3,
    0x5C, 0x22, 0x05, 0xD9, 0x82, 0x30, 0x55, 0x1B, 0x8A, 0xDE, 0x09, 0xE9, 0x2E, 0xC7, 0x18, 0x98,
    0x0C, 0xD7, 0x7B, 0xAD, 0x5A, 0x03, 0xED, 0x50, 0x0C, 0x93, 0x59, 0xE4, 0x07, 0x4D, 0xF1, 0xC9,
    0x42, 0xDD, 0x03, 0x89, 0x60, 0x26, 0x8E, 0x11, 0x57, 0xAA, 0x95, 0x0F, 0xB9, 0x58, 0x9D, 0xC1,
    0x85, 0x2F, 0x55, 0xE2, 0x1F, 0x9C, 0x4E, 0x7D, 0x8F, 0x2B, 0xF6, 0xA2, 0xE9, 0xB7, 0x43, 0x14,
    0xF8, 0xBC, 0x9C, 0x6A, 0xAB, 0x0E, 0xFF, 0x9F, 0x47, 0x6C, 0x7A, 0x9B, 0x55, 0xFD, 0x77, 0xA7,
    0xE9, 0x65, 0x25, 0xFC, 0x45, 0x97, 0xBB, 0x7B, 0xAE, 0x1D, 0x36, 0xC0, 0xAA, 0xD9, 0x8B, 0x5F,
    0x1E, 0x6C, 0xEA, 0x37, 0xCA, 0x4B, 0xFD, 0x2F, 0xC2, 0x1C, 0x48, 0xEA, 0x7E, 0xDF, 0x1E, 0x4A,
    0xD9, 0x64, 0x06, 0x8C, 0xF3, 0xCA, 0xD9, 0x08, 0xBB, 0x6A, 0x19, 0x3A, 0x61, 0x95, 0xCD, 0x33,
    0x86, 0x48, 0xD1, 0x3A, 0xE8, 0x78, 0xC6, 0x38, 0xED, 0xBE, 0x16, 0xD8, 0x41, 0xB0, 0x22, 0x4B,
    0x34, 0xB7, 0x89, 0xC6, 0x1B, 0xDD, 0x65, 0x26, 0xD5, 0xF5, 0x88, 0x46, 0x67, 0x17, 0xA2, 0x32,
    0xD2, 0xAA, 0x57, 0x0E, 0xA0, 0x73, 0xB3, 0x64, 0xDD, 0x72, 0xF8, 0x60, 0x3F, 0xA5, 0x03, 0x78,
    0x94, 0xFC, 0xB4, 0x46, 0x76, 0x32, 0x60, 0xA8, 0x45, 0xE3, 0xC6, 0x86, 0x0F, 0x50, 0x7E, 0xAD,
    0xEB, 0x72, 0x17, 0x8D, 0x25, 0x5C, 0x93, 0x03, 0xAC, 0x51, 0x27, 0x88, 0x65, 0x0E, 0xD2, 0x82,
    0xF3, 0x05, 0x51, 0x6D, 0xA5, 0x31, 0x86, 0x41, 0x9F, 0x5E, 0x76, 0xCF, 0x2B, 0xE8, 0x7A, 0xBE,
    0x49, 0x92, 0x82, 0xF6, 0xDB, 0x17, 0x97, 0x3C, 0x85, 0x9F, 0x0B, 0xB4, 0x8C, 0x2D, 0xF0, 0xCC,
    0x25, 0x3B, 0xA2, 0x15, 0xC0, 0x0D, 0x84, 0xF9, 0x23, 0x98, 0x59, 0xB0, 0xDB, 0xFE, 0x69, 0x01,
    0x24, 0xE0, 0x53, 0xB6, 0xF3, 0x42, 0xDA, 0x68, 0x81, 0xD0, 0xF5, 0xA2, 0xC2, 0xE5, 0x93, 0x5B,
    0xBF, 0x9F, 0x3A, 0xEC, 0xCD, 0x09, 0xF9, 0xC2, 0x14, 0xEA, 0x00, 0xB8, 0x91, 0x54, 0x08, 0xFD,
    0x12, 0x2A, 0xB7, 0x3E, 0x22, 0xBF, 0xED, 0x02, 0xCB, 0x50, 0x24, 0xD6, 0xC5, 0x6B, 0x52, 0xB8,
    0x5E, 0xD3, 0x6F, 0xDF, 0x57, 0x94, 0xE6, 0x3B, 0x72, 0x03, 0xF2, 0x78, 0x1E, 0x2D, 0xBB, 0x9B,
    0x5D, 0xC4, 0xA6, 0x07, 0xCC, 0x9E, 0x15, 0xBA, 0x1F, 0x34, 0x5D, 0x08, 0x3A, 0x70, 0x2D, 0x1B,
    0x74, 0xE0, 0x12, 0x93, 0x7C, 0x59, 0xB2, 0x71, 0x4F, 0x34, 0xA7, 0x22, 0xF4, 0x3C, 0x9C, 0x6F,
    0xC5, 0x5E, 0xD5, 0x6A, 0x4C, 0x7E, 0x5A, 0x2B, 0xE3, 0xAC, 0x7B, 0x38, 0x16, 0xE2, 0x80, 0x0B,
    0xAB, 0x1A, 0x86, 0xEE, 0x2C, 0xAF, 0xC9, 0x4F, 0xB9, 0xD5, 0x31, 0x48, 0xA5, 0xCF, 0x40, 0xF1,
    0x89, 0x38, 0x7C, 0x67, 0x2F, 0x85, 0x56, 0xF9, 0xE2, 0x77, 0x92, 0xEB, 0xB6, 0x50, 0xF8, 0xC9,
    0x43, 0xAD, 0x61, 0x28, 0x49, 0xE7, 0x21, 0x8C, 0xE1, 0x96, 0xC6, 0x6B, 0xD7, 0x84, 0xAF, 0xDF,
    0x7D, 0xEF, 0x04, 0x9D, 0xAE, 0xCF, 0x91, 0x6C, 0x42, 0xF1, 0x95, 0x5C, 0xFF, 0x9C, 0x3E, 0x90,
    0xF7, 0x34, 0x4D, 0x9D, 0x68, 0x1E, 0x7B, 0x10, 0xA1, 0x8D, 0x66, 0xC4, 0x91, 0x09, 0x70, 0x4E,
    0xD3, 0x0F, 0xFB, 0x47, 0xE7, 0xB2, 0x71, 0x3D, 0xA7, 0x4B, 0xCC, 0x19, 0x84, 0xA9, 0x99, 0x01,
    0xD8, 0x8A, 0xFE, 0xBC, 0xD1, 0xA8, 0x3B, 0x0E, 0xD3, 0x5C, 0x7C, 0x48, 0x0F, 0x5A, 0x30, 0x1F,
    0xA6, 0x39, 0x8D, 0xE8, 0x30, 0x14, 0xFC, 0xA4, 0x1D, 0xBB, 0x06, 0x71, 0xAF, 0x4B, 0x12, 0xBE,
    0xD6, 0x75, 0xC8, 0x00, 0x43, 0xD9, 0xFD, 0x5E, 0x29, 0xEE, 0x13, 0x53, 0xE9, 0x7E, 0xE2, 0x19,
    0xA3, 0xB3, 0x21, 0x90, 0xD1, 0x0D, 0x28, 0xC6, 0x05, 0x9A, 0x2B, 0x6C, 0x42, 0xE0, 0x24, 0x7D,
    0x55, 0x33, 0x16, 0x6E, 0x07, 0x9A, 0x65, 0xBB, 0xF3, 0x2A, 0x18, 0xED, 0xB4, 0xCB, 0xF7, 0x4D,
    0xBE, 0x6E, 0x1C, 0x56, 0xDA, 0x78, 0x0A, 0xC5, 0x85, 0x54, 0x32, 0xDC, 0xC2, 0x29, 0xEC, 0x67,
    0x56, 0x24, 0xA7, 0xF2, 0xB9, 0x89, 0x35, 0xCF, 0xAA, 0x81, 0xD8, 0x39, 0xB0, 0x27, 0x96, 0x58,
    0x31, 0xDC, 0x75, 0x5E, 0x99, 0x4F, 0xEF, 0x8C, 0x61, 0xDC, 0xFE, 0xBC, 0x0F, 0x5F, 0xEF, 0x6A,
    0xB8, 0x9E, 0xE4, 0x82, 0x2D, 0xF6, 0x79, 0x4C, 0x86, 0xAB, 0x40, 0x9E, 0x8A, 0x05, 0x67, 0x93,
    0x11, 0xFA, 0xC8, 0x44, 0xB2, 0x64, 0x37, 0x49, 0xE0, 0x68, 0xCE, 0x8D, 0x1B, 0x7B, 0xA5, 0x88,
    0x09, 0xDF, 0x92, 0x5C, 0x0F, 0x71, 0x99, 0x4A, 0x06, 0x6D, 0xBD, 0x1D, 0x5F, 0xFC, 0xBA, 0xC9,
    0x69, 0xF2, 0x3F, 0xBE, 0x1B, 0xAB, 0xD6, 0x7A, 0x13, 0xB0, 0x53, 0x87, 0xD2, 0x31, 0xC3, 0x0C,
    0xD4, 0x3C, 0x4E, 0xC4, 0x5A, 0xDD, 0x1B, 0xCA, 0x04, 0x6F, 0xDD, 0xC2, 0x25, 0x77, 0xE5, 0x41,
    0xD3, 0x9C, 0x7A, 0x27, 0x88, 0xED, 0x96, 0xAC, 0xF7, 0x25, 0xA0, 0x41, 0xF5, 0x5E, 0xE3, 0x31,
    0x46, 0xB3, 0x7F, 0x3A, 0xE4, 0xC5, 0x1A, 0xB5, 0xDE, 0x40, 0xF5, 0x9B, 0x78, 0x44, 0x04, 0x86,
    0x14, 0x9F, 0x81, 0x01, 0xF6, 0x37, 0x6B, 0x45, 0xC1, 0x21, 0x39, 0x72, 0xA3, 0x94, 0x4B, 0xAD,
    0x8E, 0x1A, 0xEC, 0xA5, 0x90, 0x42, 0xAE, 0x31, 0x98, 0xFF, 0x61, 0x38, 0x50, 0xD8, 0xAA, 0x2B,
    0xB5, 0x08, 0x60, 0xE4, 0xBE, 0x19, 0xD1, 0x59, 0x00, 0x7F, 0x12, 0xB6, 0x51, 0x0C, 0xC7, 0x97,
    0xCE, 0xFC, 0x17, 0x6A, 0x2C, 0x52, 0xF7, 0x7A, 0x5B, 0x2E, 0x8D, 0x0F, 0xD2, 0xA9, 0x35, 0xED,
    0x4D, 0x29, 0xC4, 0x55, 0xDB, 0x85, 0xA3, 0x2D, 0xE0, 0x93, 0xE7, 0x03, 0xF4, 0x1D, 0x7B, 0xFB,
    0x28, 0x75, 0x63, 0x03, 0x24, 0xD3, 0x6A, 0xE9, 0x56, 0xB6, 0x11, 0x92, 0xF1, 0x19, 0x83, 0x57,
    0xF4, 0x8E, 0x4D, 0xA6, 0x0D, 0x40, 0x73, 0x2D, 0xC0, 0x91, 0xE7, 0xD4, 0x75, 0xAC, 0x3C, 0x6C,
    0x20, 0x51, 0xBD, 0xA1, 0xD6, 0xAC, 0x90, 0x24, 0xE8, 0xA5, 0xCB, 0x4E, 0x68, 0xE0, 0x8F, 0x72,
    0xD0, 0xAF, 0xE6, 0x65, 0x23, 0xB7, 0x09, 0xFA, 0x5A, 0x7F, 0x48, 0xB7, 0x5D, 0xDA, 0x3E, 0x58,
    0xE1, 0xCD, 0xB3, 0xF9, 0x7E, 0xBA, 0x0D, 0x89, 0x45, 0x21, 0xD2, 0x73, 0xA4, 0xBC, 0x65, 0x36,
    0x71, 0x20, 0xDC, 0x34, 0xFE, 0x83, 0xA1, 0xF1, 0x6A, 0x4B, 0x35, 0x62, 0x29, 0x87, 0xF8, 0x06,
    0x7A, 0xE6, 0x8C, 0x03, 0xEE, 0x3D, 0x11, 0x64, 0xC1, 0x01, 0x82, 0xEB, 0x28, 0x1A, 0xBF, 0x5D,
    0x09, 0x95, 0x3B, 0x78, 0x8F, 0x4A, 0xCF, 0x98, 0x18, 0xCA, 0xA7, 0x69, 0x2D, 0xC4, 0x9D, 0x06,
    0x87, 0x32, 0x47, 0x9A, 0x53, 0x39, 0xF2, 0xA1, 0xC8, 0x81, 0xE5, 0x2E, 0x47, 0x01, 0xCF, 0xE7,
    0x41, 0x99, 0xC9, 0x67, 0xB6, 0x50, 0xD7, 0x1E, 0xB1, 0xCB, 0xA7, 0xEE, 0x16, 0xC0, 0x9D, 0xD8,
    0xAF, 0x60, 0x28, 0x48, 0x73, 0x84, 0xCA, 0x9E, 0x49, 0x71, 0x3B, 0xAE, 0x59, 0x9C, 0xF7, 0x41,
    0xDD, 0x18, 0xFF, 0xAA, 0x12, 0xEC, 0x62, 0x73, 0x37, 0xF2, 0x0E, 0x8D, 0xEE, 0x15, 0x70, 0xBB,
    0xA8, 0x10, 0x6D, 0xC3, 0x13, 0xDE, 0x75, 0x2C, 0x65, 0x07, 0xAC, 0x5C, 0xFB, 0x8D, 0x9F, 0x15,
    0x80, 0xAE, 0x04, 0x77, 0x28, 0x93, 0x5C, 0x0B, 0x3C, 0x78, 0x05, 0x94, 0xDE, 0x47, 0x58, 0x2E,
    0x41, 0xC5, 0x9B, 0xD0, 0x5A, 0xB8, 0x32, 0xFE, 0xDB, 0x1D, 0xBA, 0xF3, 0x0B, 0x7C, 0x2F, 0xB6,
    0x84, 0x6D, 0x53, 0x32, 0xD7, 0xC0, 0x28, 0xAE, 0x51, 0xBD, 0x25, 0x43, 0xD5, 0x82, 0x4D, 0xE6,
    0x60, 0xF2, 0xD6, 0x28, 0x8E, 0x5D, 0xAA, 0x17, 0xD8, 0x91, 0x3C, 0xC5, 0x7B, 0x24, 0x51, 0xC2,
    0x2E, 0x58, 0xF1, 0xD1, 0x13, 0xEB, 0xC3, 0xE1, 0x89, 0xFC, 0x54, 0x24, 0x7D, 0x6A, 0xF2, 0x1A,
    0x83, 0xEA, 0x11, 0xF6, 0x1F, 0xE1, 0x0A, 0x53, 0x8B, 0x2B, 0x95, 0xD6, 0x6C, 0xC7, 0x50, 0xA1,
    0x22, 0xCC, 0xBB, 0x9B, 0x05, 0x7E, 0x41, 0xE4, 0x89, 0xD9, 0x77, 0x9B, 0xB0, 0x35, 0xCC, 0x24,
    0x92, 0x3A, 0xB1, 0x80, 0xEB, 0xCA, 0x4A, 0xFC, 0xBD, 0x52, 0xEE, 0x0E, 0xB4, 0x6E, 0xD7, 0xF7,
    0x64, 0xB9, 0x47, 0x8B, 0x38, 0xA7, 0x44, 0x6E, 0x9D, 0x2E, 0xD2, 0xB9, 0xA3, 0x0D, 0xCE, 0xBC,
    0x92, 0x65, 0x3A, 0xA8, 0x7F, 0x96, 0x67, 0xAB, 0x78, 0xE7, 0x5F, 0x44, 0x14, 0x8C, 0xE5, 0x02,
    0x63, 0xF3, 0x44, 0x8A, 0x5C, 0xF8, 0xA3, 0x13, 0x68, 0x00, 0xFD, 0x55, 0x63, 0x0B, 0xF5, 0x79,
    0x18, 0x55, 0x9F, 0x0A, 0x40, 0x1F, 0x9B, 0x33, 0x7F, 0x6B, 0x99, 0x1C, 0xDF, 0x34, 0xA9, 0x10,
    0xE3, 0x1B, 0x9D, 0xDB, 0x67, 0x7E, 0x23, 0xB4, 0x11, 0x62, 0xE8, 0x42, 0x8B, 0x37, 0xAB, 0x75,
    0x4C, 0x09, 0xB5, 0x6E, 0x2B, 0x43, 0xD3, 0x16, 0xB6, 0x05, 0xCC, 0xA6, 0x33, 0xFC, 0xAD, 0xD3,
    0x79, 0x2D, 0xE3, 0x1B, 0x6F, 0xC9, 0x22, 0x94, 0xCE, 0x31, 0xB6, 0x1D, 0xE2, 0x8F, 0xA4, 0x46,
    0xC5, 0xFE, 0x69, 0xE2, 0x74, 0xB5, 0x63, 0x01, 0xE3, 0x26, 0x43, 0xCB, 0x60, 0x4B, 0x82, 0x94,
    0x74, 0xC8, 0x29, 0x52, 0xFA, 0x05, 0xCE, 0xF3, 0x4F, 0xC1, 0x73, 0x1C, 0xF6, 0x5F, 0xE2, 0x26,
    0xFF, 0xDC, 0xC9, 0x53, 0xE5, 0xBF, 0xF9, 0x4E, 0x37, 0xED, 0x81, 0x55, 0x75, 0x1D, 0x3C, 0x4B,
    0x96, 0x0F, 0xA4, 0xB2, 0xDA, 0x39, 0x56, 0xF1, 0x48, 0x82, 0xA2, 0x3E, 0xC0, 0x6E, 0x29, 0xB3,
    0xD8, 0x03, 0x30, 0xBF, 0x4F, 0xF4, 0x89, 0xCE, 0xA5, 0xB6, 0xF9, 0x89, 0xA2, 0xED, 0x03, 0x3F,
    0xFE, 0x86, 0x0B, 0xAA, 0xBA, 0x90, 0x5B, 0x30, 0x84, 0xA9, 0x08, 0x96, 0xD5, 0x51, 0x03, 0x9B,
    0x34, 0x89, 0x17, 0x9F, 0x00, 0x87, 0x1E, 0xA3, 0x6D, 0x99, 0x23, 0xC2, 0xDF, 0x92, 0xBE, 0x5E,
    0xED, 0xC5, 0x67, 0x4E, 0x07, 0x7C, 0xBA, 0xAB, 0x61, 0xE9, 0x74, 0xD2, 0x06, 0xF0, 0x51, 0x81,
    0x60, 0x97, 0x87, 0xA6, 0x16, 0xD6, 0x39, 0x12, 0x70, 0x58, 0x08, 0x77, 0x2A, 0xBC, 0xD3, 0xAF,
    0x33, 0x5D, 0xE8, 0x42, 0x73, 0x17, 0xEA, 0x9B, 0xDF, 0x3D, 0xCB, 0x2B, 0xB4, 0x82, 0x6F, 0xC6,
    0xB1, 0x5B, 0x7B, 0xEC, 0x3B, 0x75, 0x5E, 0xC7, 0xD9, 0x40, 0xF6, 0x0F, 0xB0, 0x69, 0x07, 0x80,
    0x23, 0x34, 0x85, 0xFD, 0x91, 0xE4, 0x2D, 0x0C, 0xC4, 0x15, 0x26, 0x5B, 0x8B, 0xDB, 0x38, 0x1C,
    0xEB, 0x3F, 0xCA, 0x24, 0x5C, 0x79, 0x95, 0xEF, 0x4C, 0xC1, 0x3B, 0xE8, 0x14, 0x69, 0x55, 0x20,
    0x6F, 0xBE, 0x96, 0xD8, 0x35, 0xC4, 0x68, 0x49, 0x1E, 0x7A, 0xFB, 0x69, 0x46, 0xF0, 0x13, 0x3E,
    0xD7, 0x21, 0x47, 0xD0, 0xBA, 0x93, 0xF3, 0x29, 0x07, 0x8D, 0x62, 0x4C, 0x2C, 0xD2, 0xF7, 0x9E,
    0xE1, 0xB6, 0xD1, 0x40, 0x20, 0xA0, 0x71, 0x43, 0x8C, 0x9A, 0xF9, 0x4A, 0xA9, 0x11, 0x9C, 0xBB,
    0x72, 0x0E, 0xDC, 0xF9, 0x47, 0xB2, 0x2B, 0xDE, 0x1D, 0x90, 0xAB, 0xD8, 0x98, 0x44, 0xF0, 0x9E,
    0xCE, 0x4C, 0x0F, 0x80, 0x24, 0xA2, 0xD3, 0x00, 0xB0, 0x8E, 0x5A, 0x0D, 0xA2, 0xC0, 0x91, 0xE6,
    0x64, 0xA6, 0xFA, 0x6B, 0x30, 0x11, 0x54, 0xAA, 0x7C, 0xB7, 0xE1, 0xA1, 0x87, 0x39, 0x54, 0x17,
    0x47, 0x74, 0x5B, 0x12, 0xC0, 0x63, 0xF3, 0xCE, 0xE1, 0x38, 0xB4, 0x7B, 0xC8, 0x64, 0xFC, 0xCE,
    0x56, 0xAA, 0x7E, 0x6A, 0x05, 0xC7, 0xA1, 0x62, 0x83, 0xCC, 0x2F, 0x5D, 0x7E, 0xC5, 0x8A, 0x08,
    0x2D, 0xF6, 0x63, 0xB4, 0xF1, 0x56, 0x77, 0xF7, 0xBB, 0x27, 0xDA, 0xE9, 0x34, 0x1E, 0x54, 0x7B,
    0x2C, 0x09, 0x98, 0x1A, 0xB1, 0xDB, 0xE8, 0x45, 0xD0, 0x33, 0x16, 0x77, 0xEF, 0xBA, 0x6E, 0xC9,
    0xA8, 0x01, 0x98, 0xEA, 0xAE, 0x4F, 0x83, 0x1A, 0x58, 0x6A, 0x07, 0xDE, 0x1F, 0x30, 0x86, 0x47,
    0x27, 0x93, 0x35, 0xB9, 0x8C, 0xE4, 0x40, 0x0E, 0xFF, 0x6D, 0x0A, 0xF4, 0x1E, 0x37, 0xB1, 0xDD,
    0xA5, 0x8F, 0x1C, 0xE1, 0x45, 0x8A, 0x14, 0x39, 0x61, 0x4E, 0xC6, 0x85, 0x75, 0xD1, 0xAD, 0xF5,
    0xBC, 0x8A, 0xCA, 0x4E, 0x81, 0x61, 0x9C, 0x0C, 0x6B, 0xFE, 0x5B, 0xC7, 0x09, 0x20, 0xE8, 0x8D,
    0x2F, 0xFA, 0x7C, 0x25, 0xDC, 0x33, 0x04, 0xAA, 0xBC, 0x29, 0xEC, 0x92, 0x41, 0x70, 0xB1, 0x01,
    0xE2, 0xF4, 0x14, 0x4E, 0xEC, 0x20, 0x74, 0xAE, 0x52, 0xBB, 0x46, 0xA2, 0xE1, 0x65, 0x13, 0x77,
    0x5B, 0x3B, 0xC6, 0x6E, 0x09, 0xAB, 0xC8, 0xE4, 0x94, 0xA6, 0x17, 0x40, 0x9A, 0x62, 0x02, 0x4A,
    0x3A, 0xE3, 0x73, 0xEF, 0x36, 0xC5, 0x22, 0x88, 0xBE, 0x94, 0x26, 0x48, 0xAD, 0x99, 0x3F, 0x60,
    0xB5, 0xD4, 0x69, 0x3C, 0xC6, 0x90, 0xFB, 0x77, 0xD8, 0x85, 0x4E, 0xA4, 0xBE, 0xF0, 0xD4, 0x5E,
    0xA1, 0xC3, 0x66, 0xD1, 0x9D, 0x5B, 0x32, 0xD7, 0x96, 0x26, 0xD1, 0x88, 0x52, 0xB9, 0xFC, 0x48,
    0x04, 0xED, 0xAF, 0x97, 0x33, 0xD6, 0x28, 0x7F, 0x6C, 0xEF, 0x07, 0xB5, 0xFE, 0x28, 0xDD, 0x9F,
    0x12, 0x5D, 0x25, 0xA9, 0x03, 0x6F, 0xF7, 0x3D, 0x51, 0xA5, 0xEA, 0xD5, 0x66, 0x7D, 0xDE, 0x0C,
    0x4D, 0x1A, 0x88, 0x56, 0xA4, 0x6C, 0x46, 0x20, 0x9E, 0x3A, 0xCF, 0x16, 0x57, 0x0C, 0x8E, 0x23,
    0x77, 0x3C, 0x84, 0x2C, 0x08, 0x7C, 0xC2, 0xF2, 0x12, 0x7A, 0xEE, 0x00, 0x71, 0x29, 0xC7, 0x83,
    0xD4, 0x27, 0x79, 0x50, 0xFF, 0x5D, 0xA0, 0x4B, 0x1C, 0x34, 0xD7, 0x70, 0x56, 0x8E, 0xC4, 0x6C,
    0x84, 0xB6, 0xD6, 0x44, 0x8F, 0xE0, 0xB2, 0xD1, 0x18, 0x76, 0x01, 0x35, 0x8B, 0xF8, 0x28, 0xCB,
    0x9B, 0xF4, 0xBD, 0x08, 0xEC, 0x12, 0xCB, 0x5D, 0xF0, 0x0B, 0x66, 0xFF, 0x7F, 0x36, 0xE5, 0x4A,
    0xAE, 0x1A, 0xDD, 0xB3, 0xFB, 0xA7, 0x8E, 0x3E, 0x67, 0xB4, 0x36, 0xA8, 0x93, 0x3F, 0xE8, 0x9A,
    0x63, 0xBF, 0xDE, 0x0E, 0x85, 0xB8, 0x04, 0xF4, 0xBD, 0xCB, 0x83, 0x47, 0xE6, 0x0D, 0x32, 0xF0,
    0xCD, 0x1B, 0xFD, 0x56, 0xA0, 0x13, 0x5E, 0x2E, 0x84, 0xE2, 0xC3, 0x59, 0xB3, 0x10, 0x53, 0x76,
    0xA8, 0x43, 0xDB, 0x30, 0x95, 0xB2, 0xE1, 0x2B, 0x8C, 0xAF, 0x74, 0xC3, 0x95, 0xB5, 0x64, 0xCA,
    0xF9, 0x57, 0x92, 0x46, 0x6C, 0x0F, 0x53, 0xE4, 0x1A, 0x4C, 0xDE, 0x5F, 0xCF, 0x0E, 0xAD, 0x17,
    0x36, 0x8B, 0x46, 0x20, 0x6A, 0xE3, 0x3E, 0x75, 0x98, 0x5F, 0xAA, 0x21, 0xA1, 0xBB, 0x7D, 0x50,
    0x3F, 0x96, 0x78, 0x30, 0xC1, 0x7D, 0x4A, 0xF0, 0xAB, 0x68, 0x44, 0x1E, 0x9E, 0xED, 0xC0, 0x34,
    0x68, 0x82, 0x22, 0x61, 0x75, 0x3E, 0x7F, 0xBD, 0x4A, 0xDD, 0x30, 0x1B, 0x44, 0x27, 0x9D, 0x11,
    0x81, 0x05, 0xC5, 0xE9, 0x36, 0xD4, 0x27, 0xC8, 0x82, 0x9C, 0xBF, 0x21, 0xF9, 0x7D, 0x6D, 0x54,
    0xB9, 0xA4, 0xF8, 0xCC, 0xAC, 0x8E, 0xD2, 0x54, 0x2C, 0x10, 0xFA, 0x3B, 0xD2, 0x16, 0x66, 0xA8,
    0x04, 0xB1, 0x68, 0x0F, 0xEA, 0xD7, 0x21, 0x9A, 0x0B, 0x90, 0xFC, 0xD0, 0x6F, 0x3E, 0x92, 0xD6,
    0x04, 0xEB, 0xAD, 0xCE, 0xFD, 0x53, 0x19, 0x9A, 0x00, 0x57, 0xA2, 0xE7, 0xD1, 0xEF, 0x6E, 0xD9,
    0x32, 0xA5, 0x74, 0x1F, 0xB8, 0x99, 0x5D, 0xAC, 0xFD, 0x6C, 0x0A, 0x8A, 0x2E, 0x49, 0xD9, 0xF2,
    0x2B, 0x70, 0x11, 0x58, 0x39, 0x26, 0x16, 0xEB, 0xB1, 0xDD, 0x8C, 0x77, 0x58, 0x94, 0xF7, 0xDB,
    0x2A, 0xED, 0xD0, 0x8B, 0x3D, 0xB5, 0x63, 0xCB, 0x39, 0xBC, 0x2A, 0x80, 0x12, 0xE4, 0x24, 0x5A,
    0xB7, 0x18, 0x4D, 0x8A, 0x0D, 0xC3, 0xE8, 0x6B, 0xF8, 0xC9, 0x86, 0x61, 0x06, 0x4F, 0x8B, 0xBC,
    0x41, 0xF4, 0x63, 0x4D, 0x87, 0xF0, 0x77, 0x03, 0x31, 0x42, 0xE9, 0x57, 0xC7, 0xB3, 0x95, 0x1C,
    0xE6, 0xC6, 0x97, 0x7C, 0xEF, 0xA2, 0xC1, 0x81, 0x67, 0x4A, 0x02, 0xC1, 0xE9, 0x24, 0x45, 0x88,
    0xBD, 0x5B, 0x1F, 0x4E, 0xA7, 0x06, 0x74, 0xF5, 0x52, 0xDC, 0x5D, 0xA3, 0x4B, 0xAD, 0x87, 0xFB,
    0x7A, 0x97, 0xE2, 0x39, 0xA1, 0x26, 0xAA, 0x34, 0x79, 0x22, 0x3D, 0xBA, 0x76, 0xAB, 0x17, 0x5C,
    0x23, 0xCC, 0x97, 0x0A, 0xDB, 0x3F, 0x19, 0xD0, 0x90, 0xB7, 0xD7, 0x73, 0x9E, 0x3B, 0x02, 0x81,
    0x60, 0x42, 0x06, 0xD6, 0x65, 0x48, 0x09, 0x96, 0x37, 0xCA, 0xA6, 0x2E, 0x6E, 0xB5, 0x09, 0x79,
    0x37, 0x9D, 0x70, 0xFA, 0xDB, 0x95, 0x28, 0x88, 0x16, 0x7A, 0x03, 0xB9, 0xD5, 0x64, 0x09, 0xC9,
    0x44, 0x2B, 0xC0, 0x71, 0x63, 0xD8, 0x45, 0x8D, 0xD1, 0xB0, 0x13, 0xF3, 0x94, 0x36, 0xFA, 0xE0,
    0x7A, 0xB1, 0xE7, 0x2E, 0xA9, 0xC0, 0x69, 0xF5, 0x4E, 0xA4, 0x1D, 0x11, 0xEE, 0x66, 0xDE, 0x51,
    0x89, 0xA0, 0xF3, 0x2E, 0xBA, 0xE2, 0x57, 0xFD, 0x79, 0x1D, 0xF2, 0x4F, 0x9F, 0xD9, 0x62, 0xCC,
    0xE7, 0x16, 0xC7, 0x81, 0x31, 0x59, 0xE6, 0xC6, 0xB0, 0x3E, 0xF7, 0x8C, 0x2C, 0xF0, 0x37, 0x6E,
    0xA6, 0x56, 0xF7, 0x02, 0x81, 0xF0, 0x5A, 0x0A, 0xE5, 0x65, 0x4A, 0xDB, 0x2A, 0xCF, 0xA0, 0x46,
    0x01, 0x8D, 0x52, 0x12, 0x7F, 0x58, 0x9B, 0x28, 0x7C, 0x62, 0x37, 0x86, 0xAC, 0x26, 0xCE, 0xB7,
    0x32, 0x18, 0xB1, 0x74, 0x1F, 0x88, 0xCF, 0x29, 0xB5, 0xDE, 0x5F, 0x8A, 0x3C, 0x12, 0xFF, 0x96,
    0x4B, 0xAB, 0x05, 0x44, 0xBF, 0x10, 0x6C, 0x48, 0x9B, 0xE3, 0x70, 0x1E, 0x55, 0xBE, 0x9C, 0x15,
    0xDF, 0x1F, 0xAF, 0xD3, 0x19, 0xB8, 0x97, 0x1E, 0xBF, 0x9F, 0x88, 0x70, 0x59, 0x10, 0x80, 0x66,
    0xC5, 0x38, 0x6D, 0xFB, 0xCE, 0x39, 0xDE, 0x10, 0xBE, 0xE5, 0xC9, 0xF8, 0x4B, 0x79, 0x0C, 0xFE,
    0x6E, 0xD4, 0x4F, 0x91, 0x3C, 0xAB, 0x11, 0x6D, 0x43, 0x9A, 0x08, 0xD2, 0xAF, 0x7F, 0x20, 0x56,
    0x2D, 0x87, 0xEE, 0x66, 0xB2, 0x9F, 0xF1, 0x1F, 0x2F, 0x61, 0xC8, 0x95, 0x45, 0xE9, 0x7F, 0xCD,
    0x90, 0x5F, 0x32, 0x8C, 0x4D, 0x3B, 0x72, 0xFD, 0x52, 0x33, 0x03, 0xC4, 0xE9, 0xAE, 0xBE, 0x1F,
    0xEE, 0xDA, 0xA3, 0xB8, 0x21, 0x91, 0xED, 0xAE, 0x42, 0x8D, 0x06, 0x59, 0x98, 0xC1, 0x3F, 0xA6,
    0x5C, 0xEB, 0x0D, 0xC3, 0xF8, 0x5D, 0x9F, 0xEA, 0x84, 0x18, 0xBF, 0x72, 0x29, 0xEF, 0xC4, 0xB8,
    0x6F, 0xDD, 0xCD, 0x1C, 0x38, 0x8D, 0xD2, 0x7E, 0xA7, 0x0C, 0xD9, 0x15, 0xB2, 0x69, 0x0B, 0x3D,
    0xF4, 0x77, 0xBD, 0xEC, 0xA2, 0xCC, 0x2C, 0xDA, 0x7F, 0xA7, 0xF4, 0x22, 0x3E, 0x8D, 0x4F, 0x2F,
    0x96, 0x5C, 0x16, 0x48, 0x76, 0x64, 0x02, 0x52, 0x71, 0x22, 0xDA, 0x2F, 0x6A, 0xE1, 0x1E, 0x92,
    0xCA, 0x25, 0x80, 0x68, 0xDB, 0x01, 0x4D, 0xC5, 0x32, 0xF9, 0x54, 0xE4, 0x48, 0x63, 0x8E, 0x10,
    0x3E, 0x9A, 0x5A, 0x78, 0xFB, 0x50, 0x00, 0xC1, 0x57, 0xFD, 0x83, 0x33, 0xA3, 0xE1, 0x27, 0x50,
    0xAB, 0x07, 0x44, 0x6B, 0x11, 0x84, 0xAD, 0x61, 0x16, 0x44, 0xD2, 0x96, 0x79, 0x0C, 0xFD, 0xD5,
    0x72, 0x07, 0x87, 0xF5, 0x2D, 0xA0, 0xD4, 0x82, 0xFD, 0xB9, 0x9C, 0xAF, 0x0F, 0xF1, 0x83, 0x49,
};

constexpr uint32_t CACHE_VERSION = 207; // [CORE-CENTER v2.8.3] layer offsetX/Y semantics changed (anchor -> layerPad): old saved layers would restore with corner-pinned growth

////////////////////////////////////////////////////////////////////////////////
// Timer IDs
#define TIMER_ID_STATE_POLL         2   // Heartbeat: revalidate desktop window handles (1s)
#define TIMER_ID_RECREATE_OVERLAY   4
#define TIMER_ID_WALLPAPER_UPDATE   5
#define TIMER_ID_SNAP_RETRY         6   // Catch asynchronous snap-to-grid animations
#define TIMER_ID_FOCUS_RETRY        7   // Catch asynchronous label expansion on focus change
#define TIMER_ID_EXPLORER_SETTLE    10  // Once-a-second check for Explorer settling, then one resync
#define TIMER_ID_COLLAPSE_POLL      11  // Fast poll while a selection-collapse hold is active
#define TIMER_ID_ZOOM_SETTLE        12  // [SLITHER v2.5.2] One-shot re-poll after a zoom change

// WM_APP_TRIGGER_REFRESH wParam tags: how deep the refresh must go.
// Wallpaper-only churn (Colors/Wallpapers keys) must NOT wipe icon caches.
#define REFRESH_TAG_FULL        0   // Theme/icon-affecting change: full cache wipe + deep pass
#define REFRESH_TAG_WALLPAPER   1   // Wallpaper/background change: re-read wallpaper style only

// Absolute deadline (GetTickCount64) the shared snapshot-retry timer is armed
// for, so a late-arming site can't postpone an earlier pending retry.
std::atomic<ULONGLONG> g_snapRetryDeadline{0};

// How long a deselected icon keeps its selection-sized mask cutout before
// falling back to the silhouette, when Explorer never reports a label collapse
// (e.g. short labels that don't change size).
#define COLLAPSE_HOLD_MS            100

////////////////////////////////////////////////////////////////////////////////
// Custom messages

UINT WM_APP_CLEANUP = 0;
UINT WM_APP_DEVICE_LOST = 0;
UINT WM_APP_TRIGGER_REFRESH = 0;
UINT WM_APP_SELECTION_UPDATE = 0;
UINT WM_APP_DEFERRED_INIT = 0;
UINT WM_APP_FOCUSSTORM = 0;     // render thread -> UI thread: focus-toggle hack (drag release)

// Boot-sequence file log. DebugView's boot capture is unreliable for
// user-mode OutputDebugString from early-boot processes, so the [BOOT]
// timeline (init -> bootstrap -> deferred init -> first render -> any
// external destroy/recreate) is ALSO appended to boot_log.txt in the mod
// storage dir, timestamped with ms-since-boot. A handful of one-shot writes
// per boot; the file accumulates across boots as a history.
static void BootLog(const wchar_t* fmt, ...) {
    wchar_t buf[256];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    Wh_Log(L"%s", buf);

    WCHAR storagePath[MAX_PATH];
    if (Wh_GetModStoragePath(storagePath, MAX_PATH) <= 0) return;
    std::wstring filePath = std::wstring(storagePath) + L"\\boot_log.txt";
    HANDLE hFile = CreateFileW(filePath.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER liEnd = {};
    SetFilePointerEx(hFile, liEnd, nullptr, FILE_END);
    wchar_t line[320];
    int len = swprintf_s(line, L"%llu\t[pid %u]\t%s\r\n", GetTickCount64(), GetCurrentProcessId(), buf);
    if (len > 0) {
        DWORD written = 0;
        WriteFile(hFile, line, (DWORD)(len * sizeof(wchar_t)), &written, nullptr);
    }
    CloseHandle(hFile);
}

// Same append-to-file pattern as BootLog but for runtime bug-hunt diagnostics
// ([GHOST]/[SHADOW]/[MASK] sites) — DbgView isn't always running, so the AI
// can read these straight off disk during a repro.
static void DiagLog(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    Wh_Log(L"%s", buf);

    WCHAR storagePath[MAX_PATH];
    if (Wh_GetModStoragePath(storagePath, MAX_PATH) <= 0) return;
    std::wstring filePath = std::wstring(storagePath) + L"\\diag_log.txt";
    HANDLE hFile = CreateFileW(filePath.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER liEnd = {};
    SetFilePointerEx(hFile, liEnd, nullptr, FILE_END);
    wchar_t line[576];
    int len = swprintf_s(line, L"%llu\t[pid %u]\t%s\r\n", GetTickCount64(), GetCurrentProcessId(), buf);
    if (len > 0) {
        DWORD written = 0;
        WriteFile(hFile, line, (DWORD)(len * sizeof(wchar_t)), &written, nullptr);
    }
    CloseHandle(hFile);
}

////////////////////////////////////////////////////////////////////////////////
// Window / subclass identifiers

#define OVERLAY_WINDOW_CLASS  (L"TransparentDesktopSpotlight_" WH_MOD_ID)
#define MESSAGE_WINDOW_CLASS  (L"TransparentDesktopSpotlight_Message_" WH_MOD_ID)

#define SUBCLASS_ID_LV  1   // SysListView32
#define SUBCLASS_ID_SV  2   // SHELLDLL_DefView

////////////////////////////////////////////////////////////////////////////////
// Settings

struct Settings {
    bool enableShadows;
    bool enableSpotlight;
    bool enableSelectionGlow;

    int idleOpacity;
    bool overlayClipToIcons;
    int spotlightRadius;

    int spotlightRoundness;
    int spotlightBlur;
    int idleDelay;
    int spotlightFadeInDuration;
    int spotlightFadeOutDuration;
    int mouseSmoothing;

    int selectionOpacity;
    int selectionPadding;
    int selectionRoundness;
    int selectionBlur;
    int selectionTimeout;
    int selectionFadeInDuration;
    int selectionFadeOutDuration;

    bool toggleOnDoubleClick;
    bool overlayEnabled;
    int toggleFadeInDuration;
    int toggleFadeOutDuration;

    int shadowLength;
    int shadowPasses;
    int shadowBaseAlpha;
    int shadowBlur;
    int shadowTailSoftness;
    int shadowTailOpacity;
    int shadowTailScale;
    int shadowOffset;
    bool shadowGlobalTracking;
    int shadowFadeInDuration;
    int shadowFadeOutDuration;

    int targetFps;
    int cacheMode;
    bool customArrowEnabled;
    int customArrowScale;
    int customArrowOffsetX;
    int customArrowOffsetY;

    // Overlay tint ("out of the darkness" veil). tintColor.a is 1.0; the
    // strength slider scales it at render time.
    bool tintEnabled;
    int tintStrength;
    D2D1_COLOR_F tintColor;

    // 🦇 Easter egg. While enabled it forces roundness 100% / blur 20 and
    // projects the bat silhouette under the cursor.
    bool batSignalEnabled;
    int batStyle;
    int batLogoBlur;   // -1 = Auto (logo = spotlightBlur/6); >= 0 drives BOTH the spotlight edge blur and the logo (logo = value/6)

} g_settings;

// Flag indicating whether a custom shortcut arrow (e.g. Winaero "29") is in use
std::atomic<bool> g_isCustomShortcutArrow{false};


// Icon Snapshot & Light Math
struct IconSnapshot {
    int imageIndex;
    // LV item index at staging time (raw.index). Transient: never persisted,
    // never copied at publish. The identity tiebreaker for same-named icons
    // (see oldSnapshotsMulti in ProcessSnapshotData).
    int lvIndex = -1;
    RECT iconRect;
    RECT boundsRect;
    RECT labelRect;
    int state;
    int overlayIndex;
    bool isDragged = false;
    bool isPendingDrop = false;
    bool isFolder;
    bool isThumbnail;
    bool hasCustomIcon;
    bool isFolderWithSubfolders;
    bool isPendingThumbnail = false;
    int thumbnailRetryCount = 0;
    bool wasGeneric = false;          // True if previous extraction looked generic
    bool wasEverNonGeneric = false;   // True if item ever had a non-generic icon
    std::wstring filePath; // <--- Stores the HDD path for aspect ratio extraction
    std::wstring displayName; // <--- Stores the text label for the Fast Path optimization
    std::wstring customIconPath;
    int customIconIndex = 0;
    uint64_t cacheKey;     // <--- Unique key for the cache (hash of path for thumbs)
    // [F1] arrow-generation counter at combo build time. Bumped on any change
    // that alters the baked-in arrow without changing a combo hash (arrow
    // settings, custom-arrow registry flip). Manifest restore leaves it 0,
    // which deliberately rebuilds all combos once on boot (pre-skip behavior).
    uint32_t arrowGen = 0;
    uint64_t comboHash_c = 0; // Unified pipeline hash for collapsed state
    uint64_t comboHash_e = 0; // Unified pipeline hash for extended state
    RECT combinedRect = {0, 0, 0, 0}; // Unified bounding box of icon + label
    RECT preDragBoundsRect = {0, 0, 0, 0}; // boundsRect when the current drag started (transient, not persisted)
    // --- Collapse hold (transient, not persisted) ---
    // While true, the mask keeps the selection-sized RECT cutout so Windows'
    // lingering selection rectangle is never partially covered during the
    // selected -> deselected transition.
    // --- TRANSIENT FIELDS (not persisted to the disk manifest) ---
    // WARNING: ProcessSnapshotData stages new snapshots over hundreds of ms and
    // then publishes them wholesale. Any transient field copied from a stale
    // staging entry can RESURRECT state that the poll already cleared (this
    // happened with pendingCollapse). If you add a transient field here, it
    // must be re-validated at publish time in ProcessSnapshotData.
    bool pendingCollapse = false;
    ULONGLONG pendingCollapseStart = 0;
    RECT heldCollapseRect = {0, 0, 0, 0}; // boundsRect frozen at selection loss
    // --- COMPOSITE GEOMETRY STAMPS (persisted in the disk manifest) ---
    // The label dims each combo hash was BUILT with. The poll compares these
    // against the live labelRect and invalidates + rebuilds a composite that
    // was baked from stale geometry (e.g. a deep pass that captured a
    // collapsed rect while the state said expanded — the boot-restore race
    // that produced truncated "expanded" silhouettes).
    RECT comboRect_c = {0, 0, 0, 0};
    RECT comboRect_e = {0, 0, 0, 0};
    ULONGLONG lastComboRebuildReq = 0; // poll-side debounce, transient
};

// How long (ms) an icon keeps full-rect cookie-cut coverage after losing its
// highlight (deselection / focus loss). Explorer keeps its own selection
// highlight box painted for a frame or two after the state flags flip;
// keeping the overlay painted over the whole frozen selection box during
// that window stops the box from flashing through the icon-shaped mask.




////////////////////////////////////////////////////////////////////////////////
// --- NEW PIPELINE STRUCTS (Phase 0 & 1) ---

// Represents a raw 1-byte alpha silhouette
struct SilhouetteData {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> alphaData;
};

struct SavedShadowLayer {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> alphaData;
};

struct CompositeData {
    SilhouetteData pc1;      // Icon + Overlay
    SilhouetteData pc2_c;    // pc1 + Collapsed Label
    SilhouetteData pc2_e;    // pc1 + Extended Label
    std::vector<SavedShadowLayer> shadows_c; // Pre-baked shadow layers for collapsed state
    std::vector<SavedShadowLayer> shadows_e; // Pre-baked shadow layers for extended state
    // [F1/P1.1] Arrow generation the entry was baked with. NOT serialized:
    // disk-loaded entries carry 0 (unknown) and are never trusted by the
    // disk-hit skip; RAM entries written this session carry the real gen.
    uint32_t arrowGen = 0;
};


inline uint64_t GenerateComboHash(uint64_t baseIconHash, int overlayIndex, UINT labelW, UINT labelH, uint64_t shadowBakeHash, uint64_t labelTextHash, UINT scalePermille = 1000) {
    uint64_t hash = baseIconHash;
    hash ^= (uint64_t)overlayIndex + 0x9e3779b97f4a7c15ULL;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)labelW + 0x517cc1b727220a95ULL;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)labelH + 0x6c62272e07bb0142ULL;
    hash *= 1099511628211ULL;
    hash ^= labelTextHash + 0xd6e8feb86659fd93ULL;
    hash *= 1099511628211ULL;
    hash ^= shadowBakeHash + 0x8527a89c1e4b3d5fULL;
    hash *= 1099511628211ULL;
    // [MULTIMON v2.9.4] Explorer draws desktop icons at PER-MONITOR DPI scale
    // (measured: 94px icons @125% primary vs 75px @100% secondary — ratio
    // 96/120). The combo content is baked at the item's render scale, so the
    // scale must be part of the address. Usually redundant (label dims
    // already differ per monitor) but required for no-label items where
    // monitor-1 and monitor-2 geometry would otherwise collide.
    hash ^= (uint64_t)scalePermille + 0x0d94d6a8f7c3b2e1ULL;
    hash *= 1099511628211ULL;
    return hash;
}

////////////////////////////////////////////////////////////////////////////////
// Global state

// Window handles
HWND g_overlayWnd   = nullptr;
HWND g_messageWnd   = nullptr;
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_startupGracePeriod{true};
bool g_isRecreating = false;
std::atomic<bool> g_selectionUpdatePending{false};

// Cached overlay dimensions (written under g_renderMutex, read in render thread)
UINT g_overlayWidth  = 0;
UINT g_overlayHeight = 0;

// Subclassed desktop windows
HWND g_subclassedLV = nullptr;   // SysListView32
HWND g_subclassedSV = nullptr;   // SHELLDLL_DefView
HWND g_subclassedHost = nullptr;

static bool g_trackingMouseLV = false;
static bool g_trackingMouseSV = false;

// Background render thread
// SafeThread: destroying a joinable std::thread calls std::terminate -> abort
// (exception 0x40000015 in libc++.whl = the boot-time explorer crash).
// Explorer's transient processes (folder hosts) exit WITHOUT Wh_ModUninit
// running, so static thread holders get destroyed at DLL detach holding
// finished-but-never-joined threads. This holder detaches on destruction
// instead; explicit joins still happen in Wh_ModUninit before reset().
struct SafeThread {
    std::unique_ptr<std::thread> t;
    explicit operator bool() const { return t != nullptr; }
    std::thread* operator->() { return t.get(); }
    bool joinable() const { return t && t->joinable(); }
    void join() { if (t && t->joinable()) t->join(); }
    void reset() { if (t) { if (t->joinable()) t->join(); t.reset(); } }
    void reset(std::thread* p) { t.reset(p); }
    ~SafeThread() { if (t && t->joinable()) t->detach(); }
};

SafeThread g_renderThread;
std::atomic<bool>   g_threadStop{false};
// ----------------------------------------------------------------------------
// LOCK HIERARCHY (read this before touching any locking code!)
//
// Rule 1: NEVER nest two of these mutexes in a different order than listed.
// Rule 2: NEVER take the same non-recursive mutex twice on one thread
//         (this caused a real Explorer deadlock once - see QuickSnapshotStatePoll).
//
// Declared order (outermost -> innermost when nesting is unavoidable):
//   g_stateMutex          - cached HWNDs, editing/drag UI flags
//   g_snapshotMutex       - g_iconSnapshots (the single source of truth)
//   g_renderMutex         - g_settings + master render state
//   g_cacheMutex          - g_iconCache (GPU icon bitmaps)
//   g_comboShadowCacheMutex / g_hashCacheMutex / g_uniqueShapesMutex /
//   g_overlayCacheMutex   - asset caches; these are leaf locks: never take
//                           another mutex while holding one of these
//   g_resourceDcMutex     - shared D2D device context (leaf)
//   g_snapshotRequestMutex, g_watchedFoldersMutex, g_underlayMutex,
//   disk-cache mutexes    - independent domains, never nested with the above
//
// Known nesting actually used today:
//   ProcessSnapshotData:  g_snapshotMutex -> g_cacheMutex (asset lookups)
//   RenderUnderlayLoop:   g_comboShadowCacheMutex held across DrawBitmap calls
//                         (leaf usage only - do not add lock acquisitions inside)
// ----------------------------------------------------------------------------
std::mutex          g_renderMutex;
std::atomic<bool>   g_forceRender{true};
std::atomic<bool>   g_underlayForceRender{true};

HANDLE              g_renderEvent = nullptr;
HANDLE              g_hUnloadEvent = nullptr;

SafeThread       g_regThread;
HANDLE       g_hRegStopEvent = nullptr;

// Ab Tenebris config-file watcher (settings.cfg; see the config layer above
// LoadSettings). The settings window (tool mod) writes the file, we re-read.
SafeThread       g_cfgThread;
HANDLE       g_hCfgStopEvent = nullptr;

HHOOK        g_bootstrapHook = nullptr; // Thread-local bootstrap hook

// DirectX pipeline resources
ComPtr<ID3D11Device>         g_d3dDevice;
ComPtr<IDXGIDevice>          g_dxgiDevice;
ComPtr<IDXGIFactory2>        g_dxgiFactory;
ComPtr<ID2D1Factory1>        g_d2dFactory;
ComPtr<ID2D1Device>          g_d2dDevice;
ComPtr<IDXGISwapChain1>      g_swapChain;
ComPtr<ID2D1DeviceContext>   g_dc;
ComPtr<ID2D1Bitmap1>         g_targetBitmap;
ComPtr<ID2D1Bitmap1>         g_intermediateTarget;
ComPtr<ID2D1Bitmap1>         g_spotlightMaskBitmap;
ComPtr<ID2D1Bitmap1>         g_selectionMaskBitmap;
ComPtr<ID2D1Bitmap1>         g_bakedSpotlight;
ComPtr<ID2D1BitmapBrush>     g_spotlightOpacityBrush;
ComPtr<ID2D1BitmapBrush>     g_overlayClipBrush; // For clipping the overlay to the shared mask
ComPtr<ID2D1SolidColorBrush> g_tintBrush;        // "Out of the darkness" full-screen tint veil
ComPtr<ID2D1Bitmap1>         g_batSignalBitmap;  // 🦇 baked silhouette, drawn under the cursor
uint32_t                     g_batBakeKey = 0xFFFFFFFF; // style+blur fingerprint of the current bake
ComPtr<ID2D1Effect>          g_selectionStampEffect;
std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap1>> g_bakedSelectionGlows;
// Last-use stamps for LRU eviction of the glow cache (render thread only)
std::unordered_map<uint64_t, ULONGLONG> g_bakedSelectionGlowUse;
ComPtr<ID2D1Effect>          g_spotlightStampEffect;
float                        g_bakedSpotlightHalfW = 0.0f; // bitmap half-extents, for centering the stamp on the cursor
float                        g_bakedSpotlightHalfH = 0.0f;
// 🦇 Oval logo (style 1): the spotlight is baked as a horizontal ellipse.
// Aspect = horizontal/vertical ratio (the logo's bounding ellipse is 2:1);
// Scale = fraction of the spotlight radius the light covers per axis
// (0.465 ≈ the logo's bounding ellipse + ~8% breathing room).
static constexpr float kBatSpotOvalAspect = 2.0f;
static constexpr float kBatSpotOvalScale = 0.465f;

ComPtr<IDCompositionDevice>  g_compositionDevice;
ComPtr<IDCompositionTarget>  g_compositionTarget;
ComPtr<IDCompositionVisual>  g_compositionVisual;
// [TINT-SPLIT v2.9.26] Two composition visuals on the overlay window:
// bottom = wallpaper replica (opaque at the mask glyphs), top = tint veil.
// DWM then blends the tint over BOTH the replica glyphs and the real
// desktop in ONE composition pass / one color space. Under HDR DWM
// composites in linear FP16 scRGB while our 8-bit D2D draws blend in gamma
// space — the single-surface tint was mixed by two transfer functions (the
// miscolored/lighter cutouts, direction color-pair dependent, Doc
// field-proven). v2.9.24's attempt failed on IDCompositionVisual::AddVisual
// NULL-reference semantics (MSDN: insertAbove=TRUE with NULL reference =
// BELOW all siblings) — the explicit referenceVisual form removes the trap.
ComPtr<IDXGISwapChain1>      g_tintSwapChain;
ComPtr<ID2D1DeviceContext>   g_tintDc;
ComPtr<ID2D1Bitmap1>         g_tintTargetBitmap;
ComPtr<IDCompositionVisual>  g_rootVisual;
ComPtr<IDCompositionVisual>  g_tintCompVisual;
ComPtr<IDCompositionDevice>  g_underlayCompDevice;

// Textures, brushes, and effects
ComPtr<ID2D1Bitmap>     g_wallpaperBitmap;      // Original wallpaper image (full size)
ComPtr<ID2D1Effect>     g_spotlightBlurEffect;
ComPtr<ID2D1Effect>     g_selectionBlurEffect;
ComPtr<ID2D1SolidColorBrush> g_spotlightBrush;
ComPtr<ID2D1SolidColorBrush> g_selectionBrush;
ComPtr<ID2D1BitmapBrush>     g_tileBrush;        // For tiled wallpaper

ComPtr<ID2D1Bitmap1> g_noiseBitmap;
ComPtr<ID2D1Effect> g_tileEffect;
ComPtr<ID2D1Effect> g_noiseTransformEffect;

ComPtr<ID2D1Effect> g_invertEffect;
ComPtr<ID2D1Effect> g_maskMultiplyEffect;
ComPtr<ID2D1Effect> g_noiseMultiplyEffect;
ComPtr<ID2D1Effect> g_spotlightNoiseComposite;
ComPtr<ID2D1Effect> g_selInvertEffect;
ComPtr<ID2D1Effect> g_selMaskMultiplyEffect;
ComPtr<ID2D1Effect> g_selNoiseMultiplyEffect;
ComPtr<ID2D1Effect> g_selNoiseComposite;


int g_noiseRotation = 0;
float g_noiseOffsetX = 0.0f;
float g_noiseOffsetY = 0.0f;

// Wallpaper state (protected by g_renderMutex)
bool                       g_hasWallpaper = false;          // True if a wallpaper image is set (not solid color)
D2D1_COLOR_F               g_desktopBgColor = {0.0f, 0.0f, 0.0f, 1.0f};
DESKTOP_WALLPAPER_POSITION g_wallpaperPosition = DWPOS_CENTER; // Enum replaces int style and bool tile
std::wstring               g_currentWallpaperPath;           // Path of currently loaded wallpaper (for change detection)
// [MULTIMON v2.9.4] Per-monitor wallpaper tiles (defined next to the builder
// further down; the overlay render consumes the table directly).
struct MonitorTile { RECT rect; };
static std::vector<MonitorTile> g_monitorTiles; // guarded by g_renderMutex
FILETIME                   g_lastWallpaperWriteTime = {0, 0}; // Track file modifications!

// WIC factory
ComPtr<IWICImagingFactory> g_wicFactory;
ComPtr<IWICImagingFactory> g_workerWicFactory;

// Generic icon baselines for fallback detection
SilhouetteData g_genericFileBaseline;
// [PIXEL-GENERIC v2.1.10] Windows serves SEVERAL different generic-document
// renderings depending on the path it resolved (association-based doc,
// SIID_DOCNOASSOC stock doc, classic shell32 doc). ImageList churn can serve
// any of them, so placeholder detection compares against all variants.
std::vector<SilhouetteData> g_genericFileBaselines;
SilhouetteData g_genericFolderBaseline;
bool g_baselinesReady = false;
int g_baselineTargetSize = 0;

// Cached image list info (UI thread writes, worker/render threads read)
std::atomic<HIMAGELIST> g_cachedImageList{nullptr};
std::atomic<int> g_cachedIconSize{48};
std::atomic<int> g_lastSnapshotIconSize{48};
// [P1.2] Zoom changed: the LEAVING zoom's shape keys must reach
// icon_shapes.bin before the manifest switches to the new zoom (the
// debounced 30s save misses quick A->B legs). The [ZOOM] marker sets this;
// the next pass end honors it with a throttle-bypassing save. Cheap flag
// here, file I/O on the worker at pass end — never on the zoom T0 path.
std::atomic<bool> g_shapeSaveForced{false};
// [STAGE A v2.4.0] Zoom fast path: the [ZOOM] marker stores the new icon
// size here and wakes the snapshot worker. The worker restores the target
// zoom's cached manifest + silhouettes BEFORE the (slow) deep pass runs, so
// known zooms display near-instantly; the pass behind it validates. 0 = no
// request. Coalesced by design: only the latest stored zoom runs.
std::atomic<int> g_stageARequestZoom{0};
// [SLITHER v2.5.3] Tick of the last [ZOOM] event. The publish guard uses it
// to know a pass is racing Explorer's layout settle; PASS-END uses it to
// re-arm the settle re-poll when a pass finishes after the 800ms re-poll
// already fired (the late republish would otherwise reinstate the slither).
std::atomic<ULONGLONG> g_lastZoomTick{0};
// [STAGE A v2.4.1] The zoom being LEFT (prevSize at the marker). The worker
// snapshots the still-live manifest for it and queues its cache-file save
// BEFORE the geometry swap — cache files are per zoom level, and a leg that
// ends before any pass-end save ran leaves its file missing (measured:
// "no cached manifest" on both legs of a 46<->41 cycle whose composites
// were fully alive in the RAM mirror).
std::atomic<int> g_stageALeavingZoom{0};

// [STAGE A v2.4.0] Defined further down (worker section); the [ZOOM] marker
// wakes the worker directly so the cached-state restore never waits for the
// poll cadence (measured 31ms..921ms ZOOM->PASS-START spread).
extern HANDLE g_snapshotWorkerEvent;

inline void UpdateCachedImageList(HWND hLV) {
    if (!hLV) return;
    HIMAGELIST hIL = ListView_GetImageList(hLV, LVSIL_NORMAL);
    g_cachedImageList.store(hIL, std::memory_order_relaxed);
    if (hIL) {
        int cx = 0, cy = 0;
        ImageList_GetIconSize(hIL, &cx, &cy);
        if (cx > 0) {
            // [PERF-AUDIT v2.2.6] T0 marker for the zoom pipeline timeline.
            int prevSize = g_cachedIconSize.exchange(cx, std::memory_order_relaxed);
            if (prevSize != cx) {
                DiagLog(L"[ZOOM] icon size %d -> %d (pipeline T0)", prevSize, cx);
                g_lastZoomTick.store(GetTickCount64(), std::memory_order_relaxed); // [SLITHER v2.5.3]
                g_shapeSaveForced.store(true, std::memory_order_relaxed); // [P1.2]
                // [STAGE A v2.4.0] request the cached-state restore and wake
                // the worker directly — display must not wait for the poll
                // cadence to queue the deep pass (measured 31ms..921ms).
                g_stageARequestZoom.store(cx, std::memory_order_relaxed);
                g_stageALeavingZoom.store(prevSize, std::memory_order_relaxed);
                if (g_snapshotWorkerEvent) SetEvent(g_snapshotWorkerEvent);
                // [SLITHER v2.5.2] The deep pass now runs ~50ms after T0
                // (the pacing fix made it fast enough to beat Explorer's
                // layout settle) and captures rects ~2px above the settled
                // positions — the sliver at icon bottoms. While idle NOTHING
                // re-measures (the 1s heartbeat doesn't run the poll), so
                // the slither persisted until the user touched focus. One
                // re-poll after the settle window heals it autonomously.
                // SetTimer resets on rapid zooms — last zoom wins. (Not
                // ArmSnapRetry: its earliest-deadline rule would let a
                // shorter pending retry swallow this re-measure.)
                if (g_overlayWnd && IsWindow(g_overlayWnd)) {
                    SetTimer(g_overlayWnd, TIMER_ID_ZOOM_SETTLE, 800, nullptr);
                }
            }
        }
    }
}

// Animation and render state (render thread, under g_renderMutex)
D2D1_POINT_2F g_smoothedMousePos  = {0.0f, 0.0f};
std::atomic<bool> g_isIdle         = true;
ULONGLONG     g_lastMouseTime      = 0;

std::atomic<float> g_spotlightFade{0.0f};

ULONGLONG         g_lastSelectionTime = 0;
float             g_selectionFade     = 0.0f;
std::vector<RECT> g_cachedSelectedRects;

// "Show desktop icons" disabled: Explorer hides the desktop's SysListView32.
// No visible icons -> no icon geometry may be shadowed or cut out, but the
// overlay itself (tint, bat signal, spotlight punch) stays fully active.
std::atomic<bool> g_desktopIconsHidden{false};

// Cross-thread signals (render thread reads, UI thread writes)
std::atomic<bool> g_clickOnDesktop{false};
std::atomic<bool> g_selectionJustChanged{false};
std::atomic<bool> g_isMouseOverDesktop{false};
std::atomic<bool> g_isDesktopActive{true};
std::atomic<bool> g_isDisplayOn{true};
HPOWERNOTIFY      g_hPowerNotify = nullptr;

#ifndef GUID_CONSOLE_DISPLAY_STATE
DEFINE_GUID(GUID_CONSOLE_DISPLAY_STATE, 0x6FE69556, 0x704A, 0x47A0, 0x8F, 0x24, 0xC2, 0x8D, 0x93, 0x6F, 0xDA, 0x47);
#endif

// UI thread state (under g_stateMutex)
std::mutex        g_stateMutex;
HWND              g_cachedHwndLV          = nullptr;
HWND              g_cachedHwndShellView   = nullptr;
HWND              g_cachedHwndDesktopHost = nullptr;
std::vector<RECT> g_selectedRects;
bool              g_isEditing             = false;
// [EDIT-SHADOW-OFF v2.7.3] LV index of the item whose label is being edited,
// -1 when not editing. Atomic: the underlay thread reads it per frame. While
// set, the UNDERLAY SKIPS DRAWING that icon's shadow entirely (Doc ruling:
// the label cutout is glyph-shaped for the text AT CAPTURE TIME and never
// tracks keystrokes, so the shadow would show a stale word under a live
// editbox). No combo rebuild, no pass, no keypress monitoring — the skip is
// per-frame, and the shadow returns the moment the index clears.
std::atomic<int>  g_editingLvIndex{-1};

// DPI scaling factor
std::atomic<float> g_dpiScale{1.0f};

// Initialization state
std::atomic<bool> g_initialized_desktop{false};
std::atomic<bool> g_initialized{false};

// Cold-start retry tracking
std::atomic<int>       g_initAttempts{0};
std::atomic<ULONGLONG> g_lastInitFailure{0};

// Overlay toggle
std::atomic<bool> g_overlayBypassed{false};
float g_masterFade = 1.0f;

float g_shadowFade = 0.0f;

////////////////////////////////////////////////////////////
// --- UNDERLAY GLOBALS ---
#define UNDERLAY_WINDOW_CLASS (L"TransparentDesktopUnderlay_" WH_MOD_ID)
HWND g_underlayWnd = nullptr;
ComPtr<IDXGISwapChain1> g_underlaySwapChain;
// Dedicated D2D context for shadow baking (snapshot worker thread). Keeps
// disk-cache uploads, GPU bakes and Map() readbacks off the underlay render
// context so icon changes can't stall a frame.
ComPtr<ID2D1DeviceContext> g_bakeDc;
ComPtr<ID2D1DeviceContext> g_underlayDc;
ComPtr<ID2D1DeviceContext> g_resourceDc;
ComPtr<ID2D1Bitmap1> g_underlayTargetBitmap;
ComPtr<ID2D1Bitmap1> g_stagingTexA;
const UINT STAGING_TEX_SIZE = 1024;

// Ping-Pong Shared Masks for Underlay -> Overlay synchronization
ComPtr<ID2D1Bitmap1> g_sharedMask[2];
ComPtr<ID2D1Bitmap1> g_maskScratchBitmap;
ComPtr<ID2D1Effect>  g_maskBlurEffect;
ComPtr<ID2D1Effect>  g_maskRampEffect;
std::atomic<int> g_activeSharedMaskIdx{0};
int g_underlayWriteMaskIdx = 1;
std::atomic<bool> g_sharedMaskReady{false};

ComPtr<IDCompositionTarget> g_underlayCompTarget;
ComPtr<IDCompositionVisual> g_underlayCompVisual;
SafeThread g_underlayRenderThread;
std::atomic<bool> g_underlayThreadStop{false};
HANDLE g_underlayRenderEvent = nullptr;
std::mutex g_underlayMutex;
UINT g_underlayWidth = 0, g_underlayHeight = 0;
std::atomic<bool> g_isLeftButtonDown{false};
std::atomic<bool> g_isMarqueeDrag{false};
std::wstring g_editingOldText;

// Icon Asset Cache
std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> g_iconCache;
std::mutex g_cacheMutex;
std::mutex g_resourceDcMutex;
ComPtr<ID2D1Effect> g_underlayBlurEffect;
ComPtr<ID2D1Bitmap1> g_whiteBitmap; // 512x512 solid white bitmap for cookie-cutting selected bounds


////////////////////////////////////////////////////////////////////////////////
// --- DISK CACHE MANAGER ---

#pragma pack(push, 1)
struct CacheFileHeader {
    uint32_t magic;          // 0x53484144 ("SHAD")
    uint32_t version;        // Mod version
    float dpiScale;
    int iconZoomLevel;
    uint64_t themeFontHash;
    uint32_t entryCount;
    uint32_t manifestCount;
    uint64_t manifestOffset;
};

struct CacheFileEntryMeta {
    uint64_t comboHash;
    uint64_t profileHash;
    uint64_t fileOffset;
    uint32_t dataSize;
};

struct DiskManifestHeader {
    int imageIndex;
    int overlayIndex;
    RECT iconRect;
    RECT boundsRect;
    RECT labelRect;
    RECT combinedRect;
    RECT comboLabelRect_c;
    RECT comboLabelRect_e;
    uint64_t cacheKey;
    uint64_t comboHash_c;
    uint64_t comboHash_e;
    uint8_t isFolder;
    uint8_t isThumbnail;
    uint8_t hasCustomIcon;
    uint32_t nameLen; // Number of wchar_t characters
};
#pragma pack(pop)

// Forward declarations so the disk-cache loader/saver can access globals defined lower down
extern std::vector<IconSnapshot> g_iconSnapshots;
extern std::mutex g_snapshotMutex;
extern std::atomic<uint64_t> g_snapshotGeneration;
extern HANDLE g_underlayRenderEvent;
extern HANDLE g_renderEvent;

class DiskCacheManager {
public:
    DiskCacheManager() {
        m_cacheFilePath = L"shadow_cache_v2.bin";
        m_tempFilePath = L"shadow_cache.tmp";
        m_cacheMode = 1;
    }

    ~DiskCacheManager() {
        JoinSaveThread();
    }

    void SetCacheMode(int mode) { m_cacheMode = mode; }

    // Force the next SaveToDisk to write even if nothing changed since the last
    // save attempt (needed after switching into a disk-caching mode, because
    // saves attempted in mode 0 clear the dirty flag without writing).
    void MarkDirty() { MarkDataChanged(); }

    // Evict every profile except the preferred one. Used when entering
    // single-profile mode so the existing cache file can be reused as-is
    // instead of being deleted and rebuilt from scratch. The preferred hash
    // is the caller's CURRENT state; falls back to the most recently used
    // profile when the preferred one isn't cached (e.g. settings just changed
    // but nothing has re-baked yet).
    void TrimToProfile(uint64_t preferredProfile) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_profileLastUsed.empty()) return;

        if (m_profileLastUsed.find(preferredProfile) == m_profileLastUsed.end()) {
            ULONGLONG newest = 0;
            for (const auto& [prof, lastUsed] : m_profileLastUsed) {
                if (lastUsed > newest) {
                    newest = lastUsed;
                    preferredProfile = prof;
                }
            }
        }

        for (auto it = m_profileToCombos.begin(); it != m_profileToCombos.end(); ) {
            if (it->first != preferredProfile) {
                for (uint64_t oldCombo : it->second) {
                    m_memoryCache.erase(oldCombo);
                    m_comboToProfile.erase(oldCombo);
                }
                m_profileLastUsed.erase(it->first);
                it = m_profileToCombos.erase(it);
            } else {
                ++it;
            }
        }
        MarkDataChanged();
        Wh_Log(L"[DISKCACHE] Trimmed to profile 0x%llX (Entries: %zu)",
               (unsigned long long)preferredProfile, m_memoryCache.size());
    }

    std::wstring GetCacheFilePath() const { return m_cacheFilePath; }

    // [STAGE A v2.4.2] Per-zoom cache files: shadow_cache_v2.z<zoom>.bin.
    // The old single-file design carried the zoom in its HEADER and
    // OpenValidCacheFile DELETED the whole cache on a zoom mismatch — any
    // cross-zoom load detonated it (Stage A was the first such caller and
    // nuked the cache on every zoom change, measured 2026-11-06). One file
    // per zoom level; the header zoom check stays as a per-file sanity.
    std::wstring CacheFilePathFor(int zoom) const {
        std::wstring base = m_cacheFilePath;
        size_t slash = base.find_last_of(L"\\/");
        std::wstring dir, name;
        if (slash == std::wstring::npos) {
            name = base;
        } else {
            dir = base.substr(0, slash + 1);
            name = base.substr(slash + 1);
        }
        size_t dot = name.rfind(L'.');
        std::wstring stem = (dot == std::wstring::npos) ? name : name.substr(0, dot);
        std::wstring ext = (dot == std::wstring::npos) ? L"" : name.substr(dot);
        return dir + stem + L".z" + std::to_wstring(zoom) + ext;
    }

    // Delete every per-zoom cache file (settings cache clear, mode changes).
    void DeleteAllZoomCacheFiles() {
        std::wstring base = m_cacheFilePath;
        size_t slash = base.find_last_of(L"\\/");
        std::wstring dir, name;
        if (slash == std::wstring::npos) {
            name = base;
        } else {
            dir = base.substr(0, slash + 1);
            name = base.substr(slash + 1);
        }
        size_t dot = name.rfind(L'.');
        std::wstring stem = (dot == std::wstring::npos) ? name : name.substr(0, dot);
        std::wstring ext = (dot == std::wstring::npos) ? L"" : name.substr(dot);
        std::wstring pattern = dir + stem + L".z*" + ext;
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                DeleteFileW((dir + fd.cFileName).c_str());
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    void Init() {
        WCHAR pathBuffer[MAX_PATH];
        if (Wh_GetModStoragePath(pathBuffer, MAX_PATH) > 0) {
            CreateDirectoryW(pathBuffer, nullptr);
            m_cacheFilePath = std::wstring(pathBuffer) + L"\\shadow_cache_v2.bin";
            m_tempFilePath = std::wstring(pathBuffer) + L"\\shadow_cache.tmp";
        }
        if (!m_saveWakeEvent) {
            m_saveWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
    }

    void CleanupOrphans() {
        DeleteFileW(m_tempFilePath.c_str());
        // [STAGE A v2.4.2] the legacy single-file cache is orphaned by the
        // per-zoom scheme; remove it once so its entries re-bake per zoom.
        DeleteFileW(m_cacheFilePath.c_str());
    }

    void LoadManifestFromDisk(float dpi, int zoom, uint64_t themeFontHash);
    std::vector<IconSnapshot> LoadManifestOnly(float dpi, int zoom, uint64_t themeFontHash);
    // [STAGEA-DIAG v2.4.1] diagnostics access to the private file opener.
    HANDLE DebugOpenCacheFile(float dpi, int zoom, uint64_t themeFontHash, CacheFileHeader& header);
    void LoadCompositesFromDisk(float dpi, int zoom, uint64_t themeFontHash);
    void MergeCompositesFromDisk(float dpi, int zoom, uint64_t themeFontHash);
    void SaveToDisk(float dpi, int zoom, uint64_t themeFontHash, std::vector<IconSnapshot> manifest);
    void QueueSave(float dpi, int zoom, uint64_t themeFontHash, std::vector<IconSnapshot> manifest);
    void JoinSaveThread();
    void SaveWorkerProc();

    bool GetComposite(uint64_t comboHash, CompositeData& outData) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_memoryCache.find(comboHash);
        if (it != m_memoryCache.end()) {
            outData = it->second;
            auto profIt = m_comboToProfile.find(comboHash);
            if (profIt != m_comboToProfile.end()) {
                m_profileLastUsed[profIt->second] = GetTickCount64();
            }
            return true;
        }
        return false;
    }

    void SetComposite(uint64_t comboHash, const CompositeData& data, uint64_t profileHash) {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Always mark dirty: geometry equality doesn't imply pixel equality
        // (e.g. a re-extracted thumbnail with identical dimensions), so any
        // cheaper "changed" heuristic risks persisting stale pixels forever.
        // Saves are debounced by QueueSave, so this costs at most one disk
        // write per snapshot burst.
        m_memoryCache[comboHash] = data;
        m_comboToProfile[comboHash] = profileHash;
        m_profileToCombos[profileHash].insert(comboHash);
        m_profileLastUsed[profileHash] = GetTickCount64();
        MarkDataChanged();

        // Profile-Based LRU Eviction (Keep max 8 profiles in Hoarder Mode, 1 in Single Profile)
        size_t maxProfiles = (m_cacheMode == 2) ? 8 : 1;
        if (m_cacheMode > 0 && m_profileToCombos.size() > maxProfiles) {
            uint64_t oldestProfile = 0;
            ULONGLONG oldestTime = 0xFFFFFFFFFFFFFFFFULL;
            bool foundOldest = false;

            for (const auto& [prof, lastUsed] : m_profileLastUsed) {
                if (prof != profileHash && lastUsed < oldestTime) {
                    oldestTime = lastUsed;
                    oldestProfile = prof;
                    foundOldest = true;
                }
            }

            if (foundOldest) {
                auto comboSetIt = m_profileToCombos.find(oldestProfile);
                if (comboSetIt != m_profileToCombos.end()) {
                    for (uint64_t oldCombo : comboSetIt->second) {
                        m_memoryCache.erase(oldCombo);
                        m_comboToProfile.erase(oldCombo);
                    }
                    m_profileToCombos.erase(comboSetIt);
                }
                m_profileLastUsed.erase(oldestProfile);
                MarkDataChanged();
                Wh_Log(L"[DISKCACHE] LRU Evicted oldest profile 0x%llX (Profiles: %zu/%zu, Entries: %zu)",
                       (unsigned long long)oldestProfile, m_profileToCombos.size(), maxProfiles, m_memoryCache.size());
            }
        }
    }

    bool IsDirty() const { return m_isDirty.load(std::memory_order_relaxed); }

    void ClearMemoryCache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_memoryCache.clear();
        m_comboToProfile.clear();
        m_profileToCombos.clear();
        m_profileLastUsed.clear();
    }

private:
    HANDLE OpenValidCacheFile(float dpi, int zoom, uint64_t themeFontHash, CacheFileHeader& header);
    std::unordered_map<uint64_t, CompositeData> m_memoryCache;
    std::unordered_map<uint64_t, uint64_t> m_comboToProfile;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_profileToCombos;
    std::unordered_map<uint64_t, ULONGLONG> m_profileLastUsed;
    std::mutex m_mutex;
    std::atomic<bool> m_isDirty{false};
    std::wstring m_cacheFilePath;
    std::wstring m_tempFilePath;
    std::thread m_saveThread;          // persistent save worker (lazy-started)
    std::mutex m_threadMutex;
    HANDLE m_saveWakeEvent = nullptr;  // auto-reset wake event
    bool m_saveStop = false;
    uint64_t m_saveRequestGen = 0;     // bumped per queued request
    uint64_t m_saveConsumedGen = 0;    // last generation consumed by the worker
    float m_pendDpi = 0.0f;
    int m_pendZoom = 0;
    uint64_t m_pendThemeFontHash = 0;
    std::vector<IconSnapshot> m_pendManifest;
    std::atomic<int> m_cacheMode{1};
    // Incremented on every data mutation; lets SaveToDisk detect writes that
    // landed DURING a save (whose dirty flag would otherwise be swallowed).
    std::atomic<uint64_t> m_dataGeneration{0};

    void MarkDataChanged() {
        m_dataGeneration.fetch_add(1, std::memory_order_relaxed);
        m_isDirty.store(true, std::memory_order_relaxed);
    }
};

DiskCacheManager g_diskCache;

static bool WriteExact(HANDLE hFile, LPCVOID data, DWORD size) {
    DWORD written = 0;
    return WriteFile(hFile, data, size, &written, nullptr) && written == size;
}

static bool WriteSilhouette(HANDLE hFile, const SilhouetteData& sil) {
    if (!WriteExact(hFile, &sil.width, sizeof(UINT))) return false;
    if (!WriteExact(hFile, &sil.height, sizeof(UINT))) return false;
    if (sil.width > 0 && sil.height > 0 && !sil.alphaData.empty()) {
        if (!WriteExact(hFile, sil.alphaData.data(), (DWORD)sil.alphaData.size())) return false;
    }
    return true;
}

static bool WriteShadowLayers(HANDLE hFile, const std::vector<SavedShadowLayer>& layers) {
    uint32_t count = (uint32_t)layers.size();
    if (!WriteExact(hFile, &count, sizeof(uint32_t))) return false;
    for (const auto& layer : layers) {
        if (!WriteExact(hFile, &layer.offsetX, sizeof(float))) return false;
        if (!WriteExact(hFile, &layer.offsetY, sizeof(float))) return false;
        if (!WriteExact(hFile, &layer.width, sizeof(UINT))) return false;
        if (!WriteExact(hFile, &layer.height, sizeof(UINT))) return false;
        uint32_t dataSize = (uint32_t)layer.alphaData.size();
        if (!WriteExact(hFile, &dataSize, sizeof(uint32_t))) return false;
        if (dataSize > 0) {
            if (!WriteExact(hFile, layer.alphaData.data(), dataSize)) return false;
        }
    }
    return true;
}

static bool ReadShadowLayers(HANDLE hFile, std::vector<SavedShadowLayer>& layers) {
    DWORD read;
    uint32_t count = 0;
    if (!ReadFile(hFile, &count, sizeof(uint32_t), &read, nullptr) || read != sizeof(uint32_t)) return false;
    if (count > 1000) return false; // Sanity check
    layers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!ReadFile(hFile, &layers[i].offsetX, sizeof(float), &read, nullptr) || read != sizeof(float)) return false;
        if (!ReadFile(hFile, &layers[i].offsetY, sizeof(float), &read, nullptr) || read != sizeof(float)) return false;
        if (!ReadFile(hFile, &layers[i].width, sizeof(UINT), &read, nullptr) || read != sizeof(UINT)) return false;
        if (!ReadFile(hFile, &layers[i].height, sizeof(UINT), &read, nullptr) || read != sizeof(UINT)) return false;
        uint32_t dataSize = 0;
        if (!ReadFile(hFile, &dataSize, sizeof(uint32_t), &read, nullptr) || read != sizeof(uint32_t)) return false;
        if (dataSize > 10 * 1024 * 1024) return false; // Sanity check
        if (dataSize > 0) {
            layers[i].alphaData.resize(dataSize);
            if (!ReadFile(hFile, layers[i].alphaData.data(), dataSize, &read, nullptr) || read != dataSize) return false;
        }
    }
    return true;
}

static bool ReadSilhouette(HANDLE hFile, SilhouetteData& sil) {
    DWORD read;
    if (!ReadFile(hFile, &sil.width, sizeof(UINT), &read, nullptr) || read != sizeof(UINT)) return false;
    if (!ReadFile(hFile, &sil.height, sizeof(UINT), &read, nullptr) || read != sizeof(UINT)) return false;
    if (sil.width > 2048 || sil.height > 2048) return false; // Hard clamp against corrupt header offsets
    if (sil.width > 0 && sil.height > 0) {
        size_t size = (size_t)sil.width * sil.height;
        if (size > 4 * 1024 * 1024) return false; // 4MB max for a single silhouette
        sil.alphaData.resize(size);
        if (!ReadFile(hFile, sil.alphaData.data(), (DWORD)size, &read, nullptr) || read != size) return false;
    } else {
        sil.alphaData.clear();
    }
    return true;
}

// Opens the cache file and validates its header against the current
// environment (dpi/zoom/theme). Returns an open handle + header on success,
// nullptr on any failure (a mismatched header deletes the stale file).
HANDLE DiskCacheManager::OpenValidCacheFile(float dpi, int zoom, uint64_t themeFontHash, CacheFileHeader& header) {
    if (m_cacheMode == 0) {
        Wh_Log(L"[DISKCACHE] Load skipped: cacheMode is 0");
        return nullptr;
    }
    // [STAGE A v2.4.2] one file per zoom level.
    std::wstring zoomFilePath = CacheFilePathFor(zoom);
    HANDLE hFile = CreateFileW(zoomFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        Wh_Log(L"[DISKCACHE] No cache file found at \"%s\" (err=%lu)", zoomFilePath.c_str(), GetLastError());
        return nullptr;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &header, sizeof(header), &bytesRead, nullptr) || bytesRead != sizeof(header)) {
        Wh_Log(L"[DISKCACHE] Header read failed (got %lu of %zu bytes)", bytesRead, sizeof(header));
        CloseHandle(hFile);
        return nullptr;
    }

    if (header.magic != 0x53484144 || header.version != CACHE_VERSION ||
        std::fabs(header.dpiScale - dpi) > 0.01f ||
        header.themeFontHash != themeFontHash) {
        // [STAGE A v2.4.2] dpi/font/version mismatch = stale for this zoom:
        // delete ONLY this zoom's file. The old code deleted the shared
        // single file here, taking every other zoom's cache with it.
        Wh_Log(L"[DISKCACHE] Header mismatch, deleting zoom cache: magic=%s version=%u/%u dpi=%.3f/%.3f font=%llu/%llu",
               (header.magic == 0x53484144) ? L"ok" : L"BAD",
               header.version, CACHE_VERSION,
               header.dpiScale, dpi,
               (unsigned long long)header.themeFontHash, (unsigned long long)themeFontHash);
        CloseHandle(hFile);
        DeleteFileW(zoomFilePath.c_str());
        return nullptr;
    }
    if (header.iconZoomLevel != zoom) {
        // Belongs to another zoom (renamed leftover): never delete — the
        // other zoom's file is named for its own level. Just decline.
        Wh_Log(L"[DISKCACHE] Zoom mismatch in per-zoom file: %d/%d", header.iconZoomLevel, zoom);
        CloseHandle(hFile);
        return nullptr;
    }

    if (header.entryCount == 0 || header.entryCount > 100000) {
        Wh_Log(L"[DISKCACHE] entryCount out of range: %u", header.entryCount);
        CloseHandle(hFile);
        return nullptr;
    }

    return hFile;
}

// SYNCHRONOUS, UI thread, boot-time: reads ONLY the layout manifest so the
// underlay can draw the icon layout on Frame 1. The heavy composite blobs are
// decompressed later by the snapshot worker (LoadCompositesFromDisk), so boot
// never blocks on disk I/O under g_renderMutex.
// [STAGE A v2.4.0] Read the per-zoom cache file's layout manifest
// (geometry + combo hashes + identity), no GPU work, no publish. Shared by
// the boot Frame-1 bootstrap and the Stage A zoom fast path.
std::vector<IconSnapshot> DiskCacheManager::LoadManifestOnly(float dpi, int zoom, uint64_t themeFontHash) {
    std::vector<IconSnapshot> loadedSnapshots;
    CacheFileHeader header = {};
    HANDLE hFile = OpenValidCacheFile(dpi, zoom, themeFontHash, header);
    if (!hFile) return loadedSnapshots;

    // Read Manifest if present to enable Frame-1 Instant Launch
    if (header.manifestCount > 0 && header.manifestOffset > 0 && header.manifestCount <= 10000) {
        LARGE_INTEGER liManifest;
        liManifest.QuadPart = header.manifestOffset;
        if (SetFilePointerEx(hFile, liManifest, nullptr, FILE_BEGIN)) {
            loadedSnapshots.reserve(header.manifestCount);
            for (uint32_t i = 0; i < header.manifestCount; ++i) {
                DiskManifestHeader dmh = {};
                DWORD bytesRead = 0;
                if (!ReadFile(hFile, &dmh, sizeof(dmh), &bytesRead, nullptr) || bytesRead != sizeof(dmh)) break;

                IconSnapshot snap = {};
                snap.imageIndex = dmh.imageIndex;
                snap.overlayIndex = dmh.overlayIndex;
                snap.iconRect = dmh.iconRect;
                snap.boundsRect = dmh.boundsRect;
                snap.labelRect = dmh.labelRect;
                snap.combinedRect = dmh.combinedRect;
                snap.comboRect_c = dmh.comboLabelRect_c;
                snap.comboRect_e = dmh.comboLabelRect_e;
                snap.cacheKey = dmh.cacheKey;
                snap.comboHash_c = dmh.comboHash_c;
                snap.comboHash_e = dmh.comboHash_e;
                snap.isFolder = (dmh.isFolder != 0);
                snap.isThumbnail = (dmh.isThumbnail != 0);
                snap.hasCustomIcon = (dmh.hasCustomIcon != 0);
                snap.state = 0;
                snap.isDragged = false;

                if (dmh.nameLen > 0 && dmh.nameLen < 1024) {
                    snap.displayName.resize(dmh.nameLen);
                    if (!ReadFile(hFile, &snap.displayName[0], (DWORD)(dmh.nameLen * sizeof(wchar_t)), &bytesRead, nullptr) || bytesRead != dmh.nameLen * sizeof(wchar_t)) {
                        break;
                    }
                }
                loadedSnapshots.push_back(std::move(snap));
            }
        }
    }

    CloseHandle(hFile);
    return loadedSnapshots;
}

HANDLE DiskCacheManager::DebugOpenCacheFile(float dpi, int zoom, uint64_t themeFontHash, CacheFileHeader& header) {
    return OpenValidCacheFile(dpi, zoom, themeFontHash, header);
}

void DiskCacheManager::LoadManifestFromDisk(float dpi, int zoom, uint64_t themeFontHash) {
    std::vector<IconSnapshot> loadedSnapshots = LoadManifestOnly(dpi, zoom, themeFontHash);

    // FRAME-1 BOOTSTRAP: Pre-populate g_iconSnapshots so the Underlay draws immediately
    if (!loadedSnapshots.empty()) {
        {
            std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
            g_iconSnapshots = std::move(loadedSnapshots);
            g_snapshotGeneration.fetch_add(1, std::memory_order_release);
        }
        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
        if (g_renderEvent) SetEvent(g_renderEvent);
        Wh_Log(L"[DISKCACHE] Warm Layout Manifest applied -> %zu icons ready on Frame 1", g_iconSnapshots.size());
    }
}

// SNAPSHOT WORKER thread: decompresses every composite blob into the memory
// cache. Runs before the worker's first deep pass, so GetComposite hits from
// the first processed request onward.
void DiskCacheManager::LoadCompositesFromDisk(float dpi, int zoom, uint64_t themeFontHash) {
    ULONGLONG t0 = GetTickCount64();
    CacheFileHeader header = {};
    HANDLE hFile = OpenValidCacheFile(dpi, zoom, themeFontHash, header);
    if (!hFile) return;

    std::vector<CacheFileEntryMeta> metas(header.entryCount);
    DWORD metaSize = header.entryCount * sizeof(CacheFileEntryMeta);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, metas.data(), metaSize, &bytesRead, nullptr) || bytesRead != metaSize) {
        Wh_Log(L"[DISKCACHE] Metadata table read failed (got %lu of %lu bytes)", bytesRead, metaSize);
        CloseHandle(hFile);
        return;
    }

    std::unordered_map<uint64_t, CompositeData> loadedCache;
    std::unordered_map<uint64_t, uint64_t> loadedComboToProfile;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> loadedProfileToCombos;
    std::unordered_map<uint64_t, ULONGLONG> loadedProfileLastUsed;

    loadedCache.reserve(header.entryCount);
    uint32_t failedEntries = 0;

    for (const auto& meta : metas) {
        LARGE_INTEGER liOffset;
        liOffset.QuadPart = meta.fileOffset;
        if (!SetFilePointerEx(hFile, liOffset, nullptr, FILE_BEGIN)) { failedEntries++; continue; }

        CompositeData comp;
        if (!ReadSilhouette(hFile, comp.pc1)) { failedEntries++; continue; }
        if (!ReadSilhouette(hFile, comp.pc2_c)) { failedEntries++; continue; }
        if (!ReadSilhouette(hFile, comp.pc2_e)) { failedEntries++; continue; }
        if (!ReadShadowLayers(hFile, comp.shadows_c)) { failedEntries++; continue; }
        if (!ReadShadowLayers(hFile, comp.shadows_e)) { failedEntries++; continue; }

        loadedCache[meta.comboHash] = std::move(comp);
        loadedComboToProfile[meta.comboHash] = meta.profileHash;
        loadedProfileToCombos[meta.profileHash].insert(meta.comboHash);
        loadedProfileLastUsed[meta.profileHash] = GetTickCount64();
    }

    CloseHandle(hFile);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_memoryCache = std::move(loadedCache);
        m_comboToProfile = std::move(loadedComboToProfile);
        m_profileToCombos = std::move(loadedProfileToCombos);
        m_profileLastUsed = std::move(loadedProfileLastUsed);
    }

    if (failedEntries > 0) {
        Wh_Log(L"[DISKCACHE] Loaded %zu/%u composite entries (%u failed) in %llums", m_memoryCache.size(), header.entryCount, failedEntries, GetTickCount64() - t0);
    } else {
        Wh_Log(L"[DISKCACHE] Loaded %zu/%u composite entries in %llums", m_memoryCache.size(), header.entryCount, GetTickCount64() - t0);
    }
}

// [STAGE A v2.4.3] Merge-mode composite load for the zoom fast path: read
// the zoom file's entries into the RAM mirror WITHOUT replacing it —
// session-built entries keep their arrowGen stamps, disk entries only fill
// holes. Boot keeps the replace-semantics loader above.
void DiskCacheManager::MergeCompositesFromDisk(float dpi, int zoom, uint64_t themeFontHash) {
    ULONGLONG t0 = GetTickCount64();
    CacheFileHeader header = {};
    HANDLE hFile = OpenValidCacheFile(dpi, zoom, themeFontHash, header);
    if (!hFile) return;

    std::vector<CacheFileEntryMeta> metas(header.entryCount);
    DWORD metaSize = header.entryCount * sizeof(CacheFileEntryMeta);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, metas.data(), metaSize, &bytesRead, nullptr) || bytesRead != metaSize) {
        CloseHandle(hFile);
        return;
    }

    size_t added = 0, skipped = 0;
    for (const auto& meta : metas) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_memoryCache.find(meta.comboHash) != m_memoryCache.end()) { ++skipped; continue; }
        }
        LARGE_INTEGER liOffset;
        liOffset.QuadPart = meta.fileOffset;
        if (!SetFilePointerEx(hFile, liOffset, nullptr, FILE_BEGIN)) continue;

        CompositeData comp;
        if (!ReadSilhouette(hFile, comp.pc1)) continue;
        if (!ReadSilhouette(hFile, comp.pc2_c)) continue;
        if (!ReadSilhouette(hFile, comp.pc2_e)) continue;
        if (!ReadShadowLayers(hFile, comp.shadows_c)) continue;
        if (!ReadShadowLayers(hFile, comp.shadows_e)) continue;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_memoryCache.find(meta.comboHash) == m_memoryCache.end()) {
            m_memoryCache[meta.comboHash] = std::move(comp);
            m_comboToProfile[meta.comboHash] = meta.profileHash;
            m_profileToCombos[meta.profileHash].insert(meta.comboHash);
            m_profileLastUsed[meta.profileHash] = GetTickCount64();
            ++added;
        }
    }
    CloseHandle(hFile);
    Wh_Log(L"[DISKCACHE] Merged zoom %d composites: +%zu (skipped %zu) in %llums", zoom, added, skipped, GetTickCount64() - t0);
}

void DiskCacheManager::SaveToDisk(float dpi, int zoom, uint64_t themeFontHash, std::vector<IconSnapshot> manifest) {
    if (m_cacheMode == 0) {
        m_isDirty.store(false, std::memory_order_relaxed);
        return;
    }
    if (!m_isDirty.load(std::memory_order_relaxed)) return;

    std::unordered_map<uint64_t, CompositeData> snapshot;
    std::unordered_map<uint64_t, uint64_t> comboToProfileSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_memoryCache;
        comboToProfileSnapshot = m_comboToProfile;
    }

    // Remember the data generation this save is based on. If anything mutates
    // the cache while we write, clearing the dirty flag below would silently
    // swallow the change - so only clear when the generation still matches.
    const uint64_t startGeneration = m_dataGeneration.load(std::memory_order_relaxed);

    if (snapshot.empty()) {
        if (m_dataGeneration.load(std::memory_order_relaxed) == startGeneration) {
            m_isDirty.store(false, std::memory_order_relaxed);
        }
        return;
    }

    // [STAGE A v2.4.2] per-zoom target file + matching temp.
    const std::wstring zoomFilePath = CacheFilePathFor(zoom);
    const std::wstring zoomTempPath = zoomFilePath + L".tmp";
    HANDLE hFile = CreateFileW(zoomTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return; // Dirty stays set: next pass retries

    CacheFileHeader header;
    header.magic = 0x53484144;
    header.version = CACHE_VERSION;
    header.dpiScale = dpi;
    header.iconZoomLevel = zoom;
    header.themeFontHash = themeFontHash;
    header.entryCount = (uint32_t)snapshot.size();

    bool writeOk = WriteExact(hFile, &header, sizeof(header));

    DWORD metaSize = header.entryCount * sizeof(CacheFileEntryMeta);
    std::vector<CacheFileEntryMeta> metas(header.entryCount);

    LARGE_INTEGER liMetaSize;
    liMetaSize.QuadPart = metaSize;
    SetFilePointerEx(hFile, liMetaSize, nullptr, FILE_CURRENT);

    int idx = 0;
    for (const auto& [hash, comp] : snapshot) {
        LARGE_INTEGER curPos;
        curPos.QuadPart = 0;
        SetFilePointerEx(hFile, curPos, &curPos, FILE_CURRENT);

        metas[idx].comboHash = hash;
        auto profIt = comboToProfileSnapshot.find(hash);
        metas[idx].profileHash = (profIt != comboToProfileSnapshot.end()) ? profIt->second : 0;
        metas[idx].fileOffset = (uint64_t)curPos.QuadPart;

        if (writeOk) {
            writeOk = WriteSilhouette(hFile, comp.pc1)
                   && WriteSilhouette(hFile, comp.pc2_c)
                   && WriteSilhouette(hFile, comp.pc2_e)
                   && WriteShadowLayers(hFile, comp.shadows_c)
                   && WriteShadowLayers(hFile, comp.shadows_e);
        }

        LARGE_INTEGER endPos;
        endPos.QuadPart = 0;
        SetFilePointerEx(hFile, endPos, &endPos, FILE_CURRENT);
        metas[idx].dataSize = (uint32_t)(endPos.QuadPart - curPos.QuadPart);
        idx++;
    }

    // --- MANIFEST WRITING (Using decoupled, lock-free copy) ---
    LARGE_INTEGER liManifestPos;
    liManifestPos.QuadPart = 0;
    SetFilePointerEx(hFile, liManifestPos, &liManifestPos, FILE_CURRENT);

    uint64_t manifestOffset = (uint64_t)liManifestPos.QuadPart;
    uint32_t manifestCount = (uint32_t)manifest.size();

    if (manifestCount > 0) {
        for (const auto& snap : manifest) {
            DiskManifestHeader dmh = {};
            dmh.imageIndex = snap.imageIndex;
            dmh.overlayIndex = snap.overlayIndex;
            dmh.iconRect = snap.iconRect;
            dmh.boundsRect = snap.boundsRect;
            dmh.labelRect = snap.labelRect;
            dmh.combinedRect = snap.combinedRect;
            dmh.comboLabelRect_c = snap.comboRect_c;
            dmh.comboLabelRect_e = snap.comboRect_e;
            dmh.cacheKey = snap.cacheKey;
            dmh.comboHash_c = snap.comboHash_c;
            dmh.comboHash_e = snap.comboHash_e;
            dmh.isFolder = snap.isFolder ? 1 : 0;
            dmh.isThumbnail = snap.isThumbnail ? 1 : 0;
            dmh.hasCustomIcon = snap.hasCustomIcon ? 1 : 0;
            dmh.nameLen = (uint32_t)snap.displayName.size();

            if (writeOk) writeOk = WriteExact(hFile, &dmh, sizeof(dmh));
            if (dmh.nameLen > 0 && writeOk) {
                writeOk = WriteExact(hFile, snap.displayName.data(), (DWORD)(dmh.nameLen * sizeof(wchar_t)));
            }
        }
    }

    // Write Metadata Table
    LARGE_INTEGER liMetaOffset;
    liMetaOffset.QuadPart = sizeof(header);
    SetFilePointerEx(hFile, liMetaOffset, nullptr, FILE_BEGIN);
    if (writeOk) writeOk = WriteExact(hFile, metas.data(), metaSize);

    // Rewrite Header with final Manifest offset and count
    header.manifestCount = manifestCount;
    header.manifestOffset = (manifestCount > 0) ? manifestOffset : 0;

    LARGE_INTEGER liHeaderStart;
    liHeaderStart.QuadPart = 0;
    SetFilePointerEx(hFile, liHeaderStart, nullptr, FILE_BEGIN);
    if (writeOk) writeOk = WriteExact(hFile, &header, sizeof(header));

    CloseHandle(hFile);

    if (!writeOk) {
        // Failed/partial save: discard the temp file and keep dirty set so a
        // later pass retries. The previous cache file remains intact.
        Wh_Log(L"[DISKCACHE] Save FAILED (write error), keeping old cache");
        DeleteFileW(zoomTempPath.c_str());
        return;
    }

    // Atomic rename can transiently fail while AV/Defender holds the temp file
    // open for scanning — retry with a short backoff before giving up.
    bool moved = false;
    for (int attempt = 0; attempt < 3 && !moved; ++attempt) {
        if (attempt > 0) Sleep(25);
        moved = MoveFileExW(zoomTempPath.c_str(), zoomFilePath.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (!moved) {
            Wh_Log(L"[DISKCACHE] MoveFileExW attempt %d/3 failed (err=%lu)", attempt + 1, GetLastError());
        }
    }
    if (!moved) {
        Wh_Log(L"[DISKCACHE] MoveFileExW failed after retries, keeping old cache");
        DeleteFileW(zoomTempPath.c_str());
        return;
    }

    Wh_Log(L"[DISKCACHE] Saved %u entries & %u manifest items to \"%s\"", header.entryCount, manifestCount, zoomFilePath.c_str());
    if (m_dataGeneration.load(std::memory_order_relaxed) == startGeneration) {
        m_isDirty.store(false, std::memory_order_relaxed);
    }
}

void DiskCacheManager::QueueSave(float dpi, int zoom, uint64_t themeFontHash, std::vector<IconSnapshot> manifest) {
    // Hand the request to the persistent save worker and return immediately:
    // the old spawn-join-spawn design blocked the caller for the full duration
    // of the previous save whenever saves came in faster than they completed.
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (!m_saveWakeEvent) return;
        if (!m_saveThread.joinable()) {
            m_saveThread = std::thread([this]() { SaveWorkerProc(); });
        }
        m_pendDpi = dpi;
        m_pendZoom = zoom;
        m_pendThemeFontHash = themeFontHash;
        m_pendManifest = std::move(manifest);
        m_saveRequestGen++;
    }
    SetEvent(m_saveWakeEvent);
}

void DiskCacheManager::SaveWorkerProc() {
    while (true) {
        WaitForSingleObject(m_saveWakeEvent, INFINITE);
        float dpi;
        int zoom;
        uint64_t themeFontHash;
        std::vector<IconSnapshot> manifest;
        bool haveWork = false;
        bool stop = false;
        {
            std::lock_guard<std::mutex> lock(m_threadMutex);
            stop = m_saveStop;
            if (m_saveConsumedGen != m_saveRequestGen) {
                dpi = m_pendDpi;
                zoom = m_pendZoom;
                themeFontHash = m_pendThemeFontHash;
                manifest = std::move(m_pendManifest);
                m_pendManifest.clear();
                m_pendManifest.shrink_to_fit();
                m_saveConsumedGen = m_saveRequestGen;
                haveWork = true;
            }
        }
        // Saves never overlap (single worker). A request that arrived DURING
        // this save already set the wake event and is consumed on the next
        // loop iteration, so bursts coalesce into back-to-back saves.
        if (haveWork) {
            SaveToDisk(dpi, zoom, themeFontHash, std::move(manifest));
        }
        if (stop) break;
    }
}

void DiskCacheManager::JoinSaveThread() {
    HANDLE ev = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (!m_saveThread.joinable()) return;
        m_saveStop = true;
        ev = m_saveWakeEvent;
    }
    if (ev) SetEvent(ev);
    m_saveThread.join();
    std::lock_guard<std::mutex> lock(m_threadMutex);
    m_saveStop = false;
}

// --- SNAPSHOT WORKER GLOBALS ---
struct RawListItem {
    int index;
    int imageIndex;
    UINT state;
    int overlayIndex;
    std::wstring text;
    std::wstring parsingName; // Canonical Shell Parsing Name (e.g. ::{GUID} or C:\Path)
    RECT iconRect;
    RECT boundsRect;
    RECT labelRect;
};

std::mutex g_snapshotRequestMutex;
std::vector<RawListItem> g_snapshotRequestItems;
std::wstring g_snapshotUserDesktop;
std::wstring g_snapshotPublicDesktop;

// [F1 SKIP GUARD] Bumped whenever an input that affects the BAKED-IN pc1
// arrow can change without changing any combo hash (arrow settings change,
// custom-arrow registry flip). Snapshots record the generation at build
// time; the combo skip requires a match. Starts at 1 so manifest-restored
// snapshots (gen 0) rebuild once on boot — the pre-skip behavior anyway.
std::atomic<uint32_t> g_arrowGen{1};
// [P1.1] Set by LoadSettings when a baked-pixel input (shadow params or
// arrow scale/offsets) actually changed. Wh_ModSettingsChanged consumes it:
// only THEN does g_arrowGen bump and stand down the skip guard. A settings
// load that changed nothing relevant must NOT invalidate the whole
// composite mirror (measured 2026-11-05: every unconditional bump forced a
// full-leg combo rebuild, zoom revisit 797ms -> 1188ms).
std::atomic<bool> g_bakedInputsChanged{false};

// [F2 GLYPH CACHE] Bumped on every captured-font change (QueueFullSnapshot);
// RenderGlyphSilhouette's cache keys include it, so a new font invalidates
// the whole cache. Defined here because the font-capture site runs earlier.
std::atomic<uint32_t> g_labelFontGen{1};
// Label font captured on the UI thread (WM_GETFONT of the desktop ListView)
// so the worker can render letter-shaped label silhouettes without ever
// messaging Explorer's windows cross-thread.
// The ListView's live HFONT captured on the UI thread. GDI fonts are
// process-global, so the worker can select this directly — recreating from a
// LOGFONT in the worker maps heights through a different DPI awareness and
// yields slightly different glyph metrics than Explorer's own text.
LOGFONTW g_snapshotLabelFont = {};
HFONT g_snapshotLabelHFont = nullptr;
bool g_snapshotRequestPending = false;
HANDLE g_snapshotWorkerEvent = nullptr;
SafeThread g_snapshotWorkerThread;
// [MULTIMON v2.9.7] Wake/topology watchdog (defined after BuildMonitorTiles):
// heals stale overlay/underlay union sizes. Called from the 1s heartbeat
// (RevalidateDesktopWindows) and the WM_DISPLAYCHANGE handler.
void RepairDesktopCoverage();
// Retired label fonts: the UI thread stops owning a font the moment it
// captures a new one, but an in-flight worker pass may still hold the old
// HFONT. Deleting it immediately is a use-after-free race; parking it here
// defers the delete until the worker picks up its next request, at which
// point every earlier pass has completed. Guarded by g_snapshotRequestMutex.
std::vector<HFONT> g_retiredLabelFonts;

void SnapshotWorkerThreadProc();
void PreflightShadowAssets(const Settings& localSettings);
void ProcessSnapshotData(const std::vector<RawListItem>& rawItems, const std::wstring& userDesktop, const std::wstring& publicDesktop,
                         HFONT labelFont, const LOGFONTW& labelLogfont);
void ProcessSnapshotData(const std::vector<RawListItem>& rawItems, const std::wstring& userDesktop, const std::wstring& publicDesktop,
                         HFONT labelFont, const LOGFONTW& labelLogfont, bool mergeIntoExisting);
void QueueFullSnapshot();
void QueueFastPathPass(int itemIndex); // per-icon priority pass (click latency fix)

// --- FOLDER WATCHER GLOBALS ---
std::vector<std::wstring> g_watchedFolderPaths;
std::mutex g_watchedFoldersMutex;
HANDLE g_hFolderWatchUpdateEvent = nullptr;
SafeThread g_folderWatcherThread;
void FolderWatcherThreadProc();

std::vector<IconSnapshot> g_iconSnapshots;
std::mutex g_snapshotMutex;
std::atomic<uint64_t> g_snapshotGeneration{0};

struct ShadowLayer {
    ComPtr<ID2D1Bitmap> bmp;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> cpuAlpha; // 1-byte alpha array for disk cache serialization
};

struct ShadowCache {
    ComPtr<ID2D1Bitmap1> sourceBmp; // Master 1:1 composite bitmap (Icon + Arrow + Label)
    std::vector<ShadowLayer> layers;
};

// New Unified Combo Shadow Cache
std::unordered_map<uint64_t, ShadowCache> g_comboShadowCache;
std::mutex g_comboShadowCacheMutex;

// [VRAM v2.4.4 — Doc ruling 2026-11-05: "keep them, screw that upload trail
// delay"] Combo hashes of the MOST RECENTLY LEFT zoom level: the SILPERSIST
// sweep keeps their GPU shadow layers instead of stripping (at least the
// last level survives for instant shadow restore on accidental/return
// zooms). Replaced on every zoom change — bounded at ~1 level (~80MB);
// wider retention (10-60s window, user setting) queued for the settings
// work on the windhawk2/standalone migration.
std::unordered_set<uint64_t> g_layersKeepHashes;
std::mutex g_layersKeepHashesMutex;

// [SILFIRST v2.4.0] Publish a combo's silhouette GPU bitmap from CPU alpha,
// if the cache entry has no sourceBmp yet. Shared by the build branch
// (freshly rendered pc2), the P1.1 disk-hit skip (cached pc2) and the
// Stage A zoom restore (mirror/file pc2) — one silhouette-first rule, one
// implementation. Silhouette-only entries are upgraded in place later.
static void PublishSilhouetteFromPc2(uint64_t comboHash, const SilhouetteData& pc2) {
    if (!g_bakeDc || comboHash == 0) return;
    if (pc2.width == 0 || pc2.height == 0 ||
        pc2.alphaData.size() != (size_t)pc2.width * pc2.height) return;
    bool needSilPub = false;
    {
        std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
        auto entryIt = g_comboShadowCache.find(comboHash);
        needSilPub = (entryIt == g_comboShadowCache.end() || !entryIt->second.sourceBmp);
    }
    if (!needSilPub) return;
    std::vector<BYTE> silBgra(pc2.alphaData.size() * 4);
    for (UINT i = 0; i < pc2.alphaData.size(); ++i) {
        silBgra[i * 4 + 0] = 0;
        silBgra[i * 4 + 1] = 0;
        silBgra[i * 4 + 2] = 0;
        silBgra[i * 4 + 3] = pc2.alphaData[i];
    }
    ComPtr<ID2D1Bitmap1> silBmp;
    D2D1_BITMAP_PROPERTIES1 silProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_bakeDc->CreateBitmap(D2D1::SizeU(pc2.width, pc2.height), silBgra.data(), pc2.width * 4, &silProps, &silBmp);
    if (silBmp) {
        std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
        auto& entry = g_comboShadowCache[comboHash];
        if (!entry.sourceBmp) entry.sourceBmp = silBmp;
    }
}

// Maps Windows imageIndex -> Silhouette Hash (lazy-loaded)
std::unordered_map<uint64_t, uint64_t> g_iconHashCache;
std::mutex g_hashCacheMutex;
// Persisted icon shapes: cacheKey -> shape hash, loaded from icon_shapes.bin
// at boot. Lets cold boots skip shell extraction for icons whose cacheKey
// (file identity + times + icon location + dpi/zoom) is unchanged. Save-side
// filters exclude generic placeholders / pending thumbnails, so an index hit
// is always a verified non-placeholder shape.
std::unordered_map<uint64_t, uint64_t> g_persistedShapeIndex;
std::mutex g_persistedShapeIndexMutex;

// Stores the raw alpha data of unique shapes for CPU-side Tail-to-Front comparison
struct IconShapeSignature {
    uint64_t hash;
    UINT width;
    UINT height;
    std::vector<uint8_t> alphaData;
    UINT thumbW = 0;
    UINT thumbH = 0;
};
std::vector<IconShapeSignature> g_uniqueShapes;
std::mutex g_uniqueShapesMutex;

// Maps the Unique Hash directly to the shared GPU texture (BGRA, not A8 - effects require 4 channels)
std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> g_shapeToBitmap;

// Guards the CPU overlay silhouettes (shortcut-arrow alpha arrays consumed
// by the composite pipeline). The old GPU g_overlayCache was write-only and
// was removed in the ponytail audit.
std::mutex g_overlayCacheMutex;

// CPU-side raw alpha arrays for overlays (used for compositing p-c_1)
std::unordered_map<int, SilhouetteData> g_cpuOverlaySilhouettes;

std::atomic<bool> g_snapshotUpdatePending{false};
std::atomic<int> g_draggedItemIndex{-1};
// True only while an actual drag is in progress (threshold passed / LVN_BEGINDRAG).
// g_draggedItemIndex merely records the last pressed item and must not be used
// to infer dragging.
std::atomic<bool> g_isDraggingItem{false};


////////////////////////////////////////////////////////////////////////////////
// Forward declarations

void     CreateOverlayWindow();
void     CreateUnderlayWindow();
void     CreateMessageWindow();
void     LoadSettings();
void     RefreshWallpaperAndStyle();   // New unified loader
HWND     GetDesktopListView();
void     UpdateSelectionRects();
void     RevalidateDesktopWindows();
bool     ApplyDesktopSubclasses(HWND hLV);
void     RemoveDesktopSubclasses();
void     BuildGenericBaselines();
void     ExtractIconAlphaBaseline(HICON hIcon, int targetSize, SilhouetteData& outBaseline);

extern UINT WM_APP_SNAPSHOT_UPDATE; // We'll initialize this in Wh_ModInit
UINT WM_APP_SNAPSHOT_UPDATE = 0;

void ArmSnapRetry(UINT delayMs);
bool UpdateDesktopIconsHiddenState(HWND hLV);
void RegistryWatcherThread();
void FolderWatcherThreadProc();
void ConfigWatcherThreadProc();
void SnapshotWorkerThreadProc();
static void StartAuxThreads();

bool InitWIC();
////////////////////////////////////////////////////////////////////////////////
// Utility functions

// Direct2D Gaussian Blur CLSID
static const IID kCLSID_D2D1GaussianBlur = {
    0x1feb6d69, 0x2fe6, 0x4ac9,
    {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

// Check an EndDraw result. Device-level failures must not be silently ignored:
// bakes would produce garbage/null assets and renders would no-op until the
// next Present happened to notice. Posts WM_APP_DEVICE_LOST so recovery starts
// immediately.
static bool CheckEndDraw(HRESULT hr, LPCWSTR where) {
    if (SUCCEEDED(hr)) return true;
    Wh_Log(L"[D2D] EndDraw failed in %s: 0x%08lX", where, hr);
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        if (g_messageWnd) PostMessageW(g_messageWnd, WM_APP_DEVICE_LOST, 0, 0);
    }
    return false;
}


// Helper to extract and map ListView item rectangles
bool GetListViewItemRects(HWND hLV, HWND hOverlay, int idx, RECT& outIcon, RECT& outBounds, RECT& outLabel) {
    // Abort if the overlay isn't fully created yet to prevent MapWindowPoints from falling back to screen coordinates
    if (!hOverlay || !IsWindow(hOverlay)) return false;

    RECT rcIcon = { LVIR_ICON, 0, 0, 0 };
    RECT rcLabel = { LVIR_LABEL, 0, 0, 0 };
    RECT rcBounds = { LVIR_BOUNDS, 0, 0, 0 };

    bool gotIcon = SendMessage(hLV, LVM_GETITEMRECT, idx, (LPARAM)&rcIcon) != 0;
    bool gotLabel = SendMessage(hLV, LVM_GETITEMRECT, idx, (LPARAM)&rcLabel) != 0;
    bool gotBounds = SendMessage(hLV, LVM_GETITEMRECT, idx, (LPARAM)&rcBounds) != 0;

    if (!gotIcon && !gotLabel && !gotBounds) return false;

    RECT finalRect = {};
    if (gotIcon && gotLabel) UnionRect(&finalRect, &rcIcon, &rcLabel);
    else if (gotIcon) finalRect = rcIcon;
    else if (gotLabel) finalRect = rcLabel;
    else if (gotBounds) finalRect = rcBounds;

    POINT ptTL_icon = {rcIcon.left, rcIcon.top};
    POINT ptBR_icon = {rcIcon.right, rcIcon.bottom};
    MapWindowPoints(hLV, hOverlay, &ptTL_icon, 1);
    MapWindowPoints(hLV, hOverlay, &ptBR_icon, 1);
    outIcon = {ptTL_icon.x, ptTL_icon.y, ptBR_icon.x, ptBR_icon.y};

    POINT ptTL_bounds = {finalRect.left, finalRect.top};
    POINT ptBR_bounds = {finalRect.right, finalRect.bottom};
    MapWindowPoints(hLV, hOverlay, &ptTL_bounds, 1);
    MapWindowPoints(hLV, hOverlay, &ptBR_bounds, 1);
    outBounds = {ptTL_bounds.x, ptTL_bounds.y, ptBR_bounds.x, ptBR_bounds.y};

    if (gotLabel) {
        POINT ptTL_label = {rcLabel.left, rcLabel.top};
        POINT ptBR_label = {rcLabel.right, rcLabel.bottom};
        MapWindowPoints(hLV, hOverlay, &ptTL_label, 1);
        MapWindowPoints(hLV, hOverlay, &ptBR_label, 1);

        // Letter-shaped labels: use Explorer's raw label rect untouched.
        // (The old +2px Y shift / +2px width padding existed to loosen the
        // solid rectangle silhouette; glyph silhouettes are dilated instead.)
        int top = ptTL_label.y;
        int bottom = ptBR_label.y;
        int left = ptTL_label.x;
        int right = ptBR_label.x;

        outLabel = {left, top, right, bottom};
    } else {
        outLabel = {0, 0, 0, 0};
    }

    return true;
}

float CalculateLerpFactor(int smoothingSetting, float deltaTime) {
    if (smoothingSetting <= 0) return 1.0f;
    float speed = 30.0f - (float)smoothingSetting;
    return 1.0f - std::exp(-speed * deltaTime);
}

// Helper for smooth fade animations
float AnimateFade(float current, float target, int fadeInDuration, int fadeOutDuration, float deltaTime) {
    if (target > 0.5f) {
        if (fadeInDuration <= 0) return 1.0f;
        float speed = 1.0f / (fadeInDuration / 1000.0f);
        return (std::min)(current + speed * deltaTime, 1.0f);
    } else {
        if (fadeOutDuration <= 0) return 0.0f;
        float speed = 1.0f / (fadeOutDuration / 1000.0f);
        return (std::max)(current - speed * deltaTime, 0.0f);
    }
}

inline D2D1_MATRIX_5X4_F CreateFadeMatrix(float alpha) {
    return D2D1::Matrix5x4F(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alpha,
        0.0f, 0.0f, 0.0f, 0.0f
    );
}

inline void WireSensitivityMaskGraph(
    ID2D1Image* sourceImage,
    ID2D1Effect* invertEffect,
    ID2D1Effect* maskMultiplyEffect,
    ID2D1Effect* noiseMultiplyEffect,
    ID2D1Effect* noiseCompositeEffect,
    ID2D1Effect* stampEffect,
    ID2D1Effect* sharedNoiseTransformEffect,
    const D2D1_VECTOR_4F& compositeCoeffs)
{
    invertEffect->SetInput(0, sourceImage);
    maskMultiplyEffect->SetInput(0, sourceImage);

    ComPtr<ID2D1Image> invertOutput;
    invertEffect->GetOutput(&invertOutput);
    maskMultiplyEffect->SetInput(1, invertOutput.Get());

    ComPtr<ID2D1Image> maskOutput;
    maskMultiplyEffect->GetOutput(&maskOutput);

    noiseMultiplyEffect->SetInput(0, maskOutput.Get());

    ComPtr<ID2D1Image> transformOutput;
    sharedNoiseTransformEffect->GetOutput(&transformOutput);
    noiseMultiplyEffect->SetInput(1, transformOutput.Get());

    noiseCompositeEffect->SetInput(0, sourceImage);
    noiseCompositeEffect->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, compositeCoeffs);

    ComPtr<ID2D1Image> modulatedNoiseOutput;
    noiseMultiplyEffect->GetOutput(&modulatedNoiseOutput);
    noiseCompositeEffect->SetInput(1, modulatedNoiseOutput.Get());

    ComPtr<ID2D1Image> compositeOutput;
    noiseCompositeEffect->GetOutput(&compositeOutput);
    stampEffect->SetInput(0, compositeOutput.Get());
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

struct LocalTimerHandle {
    HANDLE hTimer = nullptr;
    LocalTimerHandle() {
        hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    ~LocalTimerHandle() {
        if (hTimer) {
            CloseHandle(hTimer);
            hTimer = nullptr;
        }
    }
};

inline void CapFramerate(std::chrono::steady_clock::time_point& nextFrameTime, int targetFps = 60) {
    auto now = std::chrono::steady_clock::now();
    if (now < nextFrameTime) {
        auto remainingMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(nextFrameTime - now).count();
        if (remainingMicroseconds > 200) {
            thread_local LocalTimerHandle tl_timer;
            if (tl_timer.hTimer) {
                LARGE_INTEGER liDueTime;
                // Negative value indicates relative time in 100-nanosecond units
                liDueTime.QuadPart = -(remainingMicroseconds * 10);
                SetWaitableTimer(tl_timer.hTimer, &liDueTime, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(tl_timer.hTimer, INFINITE);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(remainingMicroseconds));
            }
        }
    } else {
        nextFrameTime = now;
    }
    nextFrameTime += std::chrono::microseconds(1000000 / targetFps);
}

float UpdateShadowFade(const Settings& settings, float currentFade, bool isVisible, float deltaTime) {
    float target = isVisible ? 1.0f : 0.0f;
    return AnimateFade(currentFade, target, settings.shadowFadeInDuration, settings.shadowFadeOutDuration, deltaTime);
}

HWND GetDesktopListView() {
    HWND hShellView = nullptr;
    HWND hProgman = FindWindow(L"Progman", nullptr);
    if (hProgman)
        hShellView = FindWindowEx(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);

    if (!hShellView) {
        HWND hWorkerW = nullptr;
        while ((hWorkerW = FindWindowEx(nullptr, hWorkerW, L"WorkerW", nullptr)) != nullptr) {
            hShellView = FindWindowEx(hWorkerW, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hShellView) break;
        }
    }

    if (hShellView)
        return FindWindowEx(hShellView, nullptr, L"SysListView32", nullptr);

    return nullptr;
}

HWND GetDesktopParent() {
    HWND hLV = GetDesktopListView();
    if (hLV) {
        HWND hShellView = GetParent(hLV);
        if (hShellView)
            return GetParent(hShellView);
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////
// Reusable SAD helper (Sum of Absolute Differences)
// Calculates the 4-bucket penalty between two 1-byte alpha arrays.
// Returns early if the penalty exceeds maxAllowedPenalty to save CPU cycles.
uint32_t CalculateSADPenalty(const uint8_t* dataA, const uint8_t* dataB, size_t size, uint32_t maxAllowedPenalty) {
    // 1. memcmp shortcut for 100% identical shapes
    if (memcmp(dataA, dataB, size) == 0) {
        return 0;
    }

    uint32_t totalPenalty = 0;
    const uint8_t* ptrA = dataA;
    const uint8_t* ptrB = dataB;
    size_t numChunks = size / 8;

    // 2. Process 8 pixels at a time using 64-bit integers
    for (size_t i = 0; i < numChunks; ++i) {
        uint64_t valA, valB;
        // memcpy is the standard, safe way to read 8 bytes without triggering compiler alignment warnings
        memcpy(&valA, ptrA, sizeof(uint64_t));
        memcpy(&valB, ptrB, sizeof(uint64_t));

        if (valA != valB) {
            // Mismatch in this 8-byte block, drop down to byte-level math
            for (int j = 0; j < 8; ++j) {
                int diff = std::abs((int)ptrA[j] - (int)ptrB[j]);
                if (diff <= 15) { /* 0 points */ }
                else if (diff <= 64) totalPenalty += 1;
                else if (diff <= 128) totalPenalty += 5;
                else totalPenalty += 20;

                if (totalPenalty > maxAllowedPenalty) return totalPenalty;
            }
        }
        ptrA += 8;
        ptrB += 8;
    }

    // 3. Handle any remaining bytes (if size is not a perfect multiple of 8)
    size_t remainder = size % 8;
    for (size_t i = 0; i < remainder; ++i) {
        int diff = std::abs((int)ptrA[i] - (int)ptrB[i]);
        if (diff <= 15) { /* 0 points */ }
        else if (diff <= 64) totalPenalty += 1;
        else if (diff <= 128) totalPenalty += 5;
        else totalPenalty += 20;

        if (totalPenalty > maxAllowedPenalty) return totalPenalty;
    }

    return totalPenalty;
}

// Compare two shadow layer alpha arrays for similarity (CPU-side, no GPU stalls).
// Both sides already carry 1-byte cpuAlpha arrays (kept for disk cache serialization),
// so the weighted similarity test runs entirely in RAM. No staging textures, no Map(),
// no GPU pipeline stall. Tolerance and weighting match the old GPU version exactly.
bool CompareAlphaLayersTTF(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, UINT w, UINT h) {
    size_t n = (size_t)w * h;
    if (a.size() < n || b.size() < n) return false;

    // Fast path: byte-identical layers (the common case for shared shapes)
    if (memcmp(a.data(), b.data(), n) == 0) return true;

    uint64_t totalWeightedDiff = 0;
    uint64_t totalPixels = (uint64_t)w * h;
    const uint64_t TOLERANCE_MULTIPLIER = 500;
    uint64_t maxAllowedDiff = totalPixels * TOLERANCE_MULTIPLIER;

    for (size_t i = 0; i < n; ++i) {
        uint8_t alphaA = a[i];
        uint8_t alphaB = b[i];

        int diff = std::abs((int)alphaA - (int)alphaB);
        if (diff == 0) continue;

        int maxAlpha = std::max((int)alphaA, (int)alphaB);
        uint64_t weight = 100 + (uint64_t)((255 - maxAlpha) * 100 / 255);

        totalWeightedDiff += (uint64_t)diff * weight;
        if (totalWeightedDiff > maxAllowedDiff) return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// UI thread — selection rect computation

void UpdateSelectionRects() {
    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    if (!hLV || !IsWindow(hLV)) return;
    // Hidden LV = "Show desktop icons" off: nothing may be selected on screen
    if (!IsWindowVisible(hLV)) return;

    bool isEditing = false;
    HWND hEdit = FindWindowEx(hLV, nullptr, L"Edit", nullptr);
    if (!hEdit) hEdit = FindWindowEx(hLV, nullptr, L"WC_EDIT", nullptr);
    if (hEdit && IsWindowVisible(hEdit))
        isEditing = true;

    static std::vector<RECT> s_rects;
    s_rects.clear();

    HWND overlay = g_overlayWnd;
    int count = (int)SendMessage(hLV, LVM_GETSELECTEDCOUNT, 0, 0);

    // If count is 0, the user has deselected everything.
    // We intentionally ignore LVNI_FOCUSED here because Windows keeps a "ghost"
    // focus anchor for keyboard navigation, but it's not actually selected.
    if (count > 0 && overlay) {
        // 1. Track all selected items
        int idx = -1;
        while ((idx = (int)SendMessage(hLV, LVM_GETNEXTITEM, idx, LVNI_SELECTED)) != -1) {
            RECT dummyIcon, finalRect, dummyLabel;
            if (!GetListViewItemRects(hLV, overlay, idx, dummyIcon, finalRect, dummyLabel)) {
                continue;
            }
            s_rects.push_back(finalRect);
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_selectedRects = s_rects;
        g_isEditing     = isEditing;
    }

    g_selectionJustChanged.store(true, std::memory_order_relaxed);
    // Wake underlay so g_sharedMask flips in lockstep with the overlay
    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
    if (g_renderEvent) SetEvent(g_renderEvent);
}

////////////////////////////////////////////////////////////////////////////////
// UI thread — desktop window heartbeat (1s)

void RevalidateDesktopWindows() {

    if (g_unloading) return;

    HWND hLV = GetDesktopListView();

    if (!hLV || !IsWindow(hLV)) {
        RemoveDesktopSubclasses();
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_cachedHwndLV          = nullptr;
        g_cachedHwndShellView   = nullptr;
        g_cachedHwndDesktopHost = nullptr;
        return;
    }

    // Detect "Show desktop icons" toggles even when no selection events fire
    // (this heartbeat is the only periodic caller when the desktop is idle).
    UpdateDesktopIconsHiddenState(hLV);

    // [MULTIMON v2.9.7] Wake/topology watchdog: monitor power cycles and
    // desktop-host recreation can leave the overlay/underlay at a stale
    // union size with NO broadcast to heal them (no mode change -> no
    // WM_DISPLAYCHANGE; parent growing doesn't resize children -> no
    // WM_WINDOWPOSCHANGED). Field case: right quarter of the primary bare
    // after display sleep (log: tiles built with origin=(0,0) at wake).
    RepairDesktopCoverage();

    if (hLV == g_subclassedLV) return;

    RemoveDesktopSubclasses();
    ApplyDesktopSubclasses(hLV);
}


int GetCurrentIconZoom() {
    return g_cachedIconSize.load(std::memory_order_relaxed);
}

// Helper to query custom Shell Icons (Winaero Tweaker / Registry Shell Icons)
// Safely read a string-typed registry value into a fixed WCHAR buffer.
// Rejects non-string types (REG_BINARY, REG_DWORD, ...): their raw bytes would
// otherwise be interpreted as a possibly unterminated string and over-read the
// buffer. Accepts REG_SZ and REG_EXPAND_SZ (icon locations often use env vars).
static bool QuerySzValue(HKEY hKey, const wchar_t* valName, WCHAR* buf, DWORD cchBuf) {
    DWORD type = 0;
    DWORD size = cchBuf * sizeof(WCHAR);
    if (RegQueryValueExW(hKey, valName, nullptr, &type, (LPBYTE)buf, &size) != ERROR_SUCCESS) return false;
    if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
    buf[cchBuf - 1] = L'\0';
    return true;
}

bool GetCustomShellIcon(const wchar_t* indexStr, std::wstring& outPath, int& outIndex) {
    WCHAR iconPath[MAX_PATH] = {0};
    HKEY hKey;
    bool found = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (QuerySzValue(hKey, indexStr, iconPath, MAX_PATH) && iconPath[0] != 0) found = true;
        RegCloseKey(hKey);
    }
    if (!found && RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (QuerySzValue(hKey, indexStr, iconPath, MAX_PATH) && iconPath[0] != 0) found = true;
        RegCloseKey(hKey);
    }
    if (!found) return false;

    std::wstring pathStr = iconPath;
    int idx = 0;
    size_t comma = pathStr.find_last_of(L',');
    if (comma != std::wstring::npos) {
        try {
            idx = std::stoi(pathStr.substr(comma + 1));
        } catch (...) {
            idx = 0;
        }
        pathStr = pathStr.substr(0, comma);
    }
    WCHAR expandedPath[MAX_PATH] = {0};
    ExpandEnvironmentStringsW(pathStr.c_str(), expandedPath, MAX_PATH);
    outPath = (expandedPath[0] != 0) ? expandedPath : pathStr;
    outIndex = idx;
    return true;
}

// Helper to query Desktop CLSID DefaultIcon (This PC, Recycle Bin, Network, Control Panel)
bool GetClsidDefaultIcon(const wchar_t* clsidStr, std::wstring& outPath, int& outIndex, bool isRecycleBinFull = false) {
    std::wstring cleanClsid = clsidStr;
    if (cleanClsid.rfind(L"::", 0) == 0) {
        cleanClsid = cleanClsid.substr(2);
    }
    size_t closeBrace = cleanClsid.find(L'}');
    if (closeBrace != std::wstring::npos) {
        cleanClsid = cleanClsid.substr(0, closeBrace + 1);
    }

    WCHAR iconPath[MAX_PATH] = {0};
    bool found = false;
    HKEY hClsid = nullptr;

    // Reads the DefaultIcon from an open key, honoring the Recycle Bin's
    // full/empty sub-values. Shared by all three lookup locations below.
    auto readDefaultIcon = [&](HKEY hKey) -> bool {
        if (_wcsicmp(cleanClsid.c_str(), L"{645FF040-5081-101B-9F08-00AA002F954E}") == 0) {
            const wchar_t* valName = isRecycleBinFull ? L"full" : L"empty";
            if (QuerySzValue(hKey, valName, iconPath, MAX_PATH) && iconPath[0] != 0) return true;
        }
        return QuerySzValue(hKey, nullptr, iconPath, MAX_PATH) && iconPath[0] != 0;
    };

    // 1. Check HKCU Explorer override (custom theme / user customization in desk.cpl ,5)
    std::wstring explorerKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID\\" + cleanClsid + L"\\DefaultIcon";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, explorerKey.c_str(), 0, KEY_READ, &hClsid) == ERROR_SUCCESS) {
        found = readDefaultIcon(hClsid);
        RegCloseKey(hClsid);
    }

    // 2. Check HKCU\Software\Classes\CLSID (User class association)
    if (!found) {
        std::wstring userClassKey = L"Software\\Classes\\CLSID\\" + cleanClsid + L"\\DefaultIcon";
        if (RegOpenKeyExW(HKEY_CURRENT_USER, userClassKey.c_str(), 0, KEY_READ, &hClsid) == ERROR_SUCCESS) {
            found = readDefaultIcon(hClsid);
            RegCloseKey(hClsid);
        }
    }

    // 3. Fallback to HKEY_CLASSES_ROOT\CLSID (System default Windows theme icons in imageres.dll!)
    if (!found) {
        std::wstring rootClassKey = L"CLSID\\" + cleanClsid + L"\\DefaultIcon";
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, rootClassKey.c_str(), 0, KEY_READ, &hClsid) == ERROR_SUCCESS) {
            found = readDefaultIcon(hClsid);
            RegCloseKey(hClsid);
        }
    }

    if (!found) return false;

    std::wstring pathStr = iconPath;
    int idx = 0;
    size_t comma = pathStr.find_last_of(L',');
    if (comma != std::wstring::npos) {
        try {
            idx = std::stoi(pathStr.substr(comma + 1));
        } catch (...) {
            idx = 0;
        }
        pathStr = pathStr.substr(0, comma);
    }
    WCHAR expPath[MAX_PATH] = {0};
    ExpandEnvironmentStringsW(pathStr.c_str(), expPath, MAX_PATH);
    outPath = (expPath[0] != 0) ? expPath : pathStr;
    outIndex = idx;
    return true;
}

// Retrieve the active Windows Stock Document (blank white sheet) system ImageList index
int GetGenericDocSysIconIndex() {
    SHSTOCKICONINFO sii = { sizeof(sii) };
    if (SUCCEEDED(SHGetStockIconInfo(SIID_DOCNOASSOC, SHGSI_SYSICONINDEX, &sii))) {
        return sii.iSysImageIndex;
    }
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(L".____dummy_unassociated", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
        return sfi.iIcon;
    }
    return -1;
}

uint64_t GetEnvironmentHash() {
    uint64_t hash = 14695981039346656037ULL;

    // 1. Hash System Font
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        for (wchar_t c : ncm.lfMessageFont.lfFaceName) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        hash ^= ncm.lfMessageFont.lfHeight;
        hash *= 1099511628211ULL;
    }

    // 2. Hash Theme (Light/Dark mode & Active Theme File)
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1;
        DWORD size = sizeof(val);
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&val, &size);
        hash ^= val;
        hash *= 1099511628211ULL;

        val = 1;
        RegQueryValueExW(hKey, L"SystemUsesLightTheme", nullptr, nullptr, (LPBYTE)&val, &size);
        hash ^= val;
        hash *= 1099511628211ULL;
        RegCloseKey(hKey);
    }

    // Hash active theme file name (detects theme swaps)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR themeBuf[MAX_PATH] = {0};
        DWORD size = sizeof(themeBuf);
        if (RegQueryValueExW(hKey, L"CurrentTheme", nullptr, nullptr, (LPBYTE)themeBuf, &size) == ERROR_SUCCESS) {
            for (wchar_t c : themeBuf) {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
        }
        RegCloseKey(hKey);
    }

    // 3. Hash Global Text Scale (Accessibility "Make text bigger" slider)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD textScale = 100;
        DWORD size = sizeof(textScale);
        RegQueryValueExW(hKey, L"TextScaleFactor", nullptr, nullptr, (LPBYTE)&textScale, &size);
        hash ^= textScale;
        hash *= 1099511628211ULL;
        RegCloseKey(hKey);
    }

    // 4. Hash Custom Shell Icons (Shortcut Arrows / UAC Shields / Folders)
    auto hashShellIcon = [&](HKEY rootKey, const wchar_t* valueName) {
        HKEY hShellIcons;
        if (RegOpenKeyExW(rootKey, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons", 0, KEY_READ, &hShellIcons) == ERROR_SUCCESS) {
            WCHAR iconPath[MAX_PATH] = {0};
            DWORD size = sizeof(iconPath);
            DWORD type = 0;
            if (RegQueryValueExW(hShellIcons, valueName, nullptr, &type, (LPBYTE)iconPath, &size) == ERROR_SUCCESS && type == REG_SZ) {
                for (wchar_t c : iconPath) {
                    hash ^= c;
                    hash *= 1099511628211ULL;
                }
            }
            RegCloseKey(hShellIcons);
        }
    };
    hashShellIcon(HKEY_CURRENT_USER, L"3");  // Default Closed Folder (Winaero)
    hashShellIcon(HKEY_LOCAL_MACHINE, L"3");
    hashShellIcon(HKEY_CURRENT_USER, L"4");  // Default Open Folder
    hashShellIcon(HKEY_LOCAL_MACHINE, L"4");
    hashShellIcon(HKEY_CURRENT_USER, L"29"); // Shortcut Arrow
    hashShellIcon(HKEY_LOCAL_MACHINE, L"29");
    hashShellIcon(HKEY_CURRENT_USER, L"30"); // UAC Shield
    hashShellIcon(HKEY_LOCAL_MACHINE, L"30");

    // 5. Hash Desktop Special CLSID Icons (This PC, Recycle Bin, Network, User Files)
    auto hashClsidIcon = [&](const wchar_t* clsidStr) {
        std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID\\" + std::wstring(clsidStr) + L"\\DefaultIcon";
        HKEY hClsid;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &hClsid) == ERROR_SUCCESS) {
            WCHAR iconPath[MAX_PATH] = {0};
            DWORD size = sizeof(iconPath);
            if (RegQueryValueExW(hClsid, nullptr, nullptr, nullptr, (LPBYTE)iconPath, &size) == ERROR_SUCCESS) {
                for (wchar_t c : iconPath) {
                    hash ^= c;
                    hash *= 1099511628211ULL;
                }
            }
            // Also hash "full" / "empty" states for Recycle Bin
            size = sizeof(iconPath);
            if (RegQueryValueExW(hClsid, L"empty", nullptr, nullptr, (LPBYTE)iconPath, &size) == ERROR_SUCCESS) {
                for (wchar_t c : iconPath) { hash ^= c; hash *= 1099511628211ULL; }
            }
            size = sizeof(iconPath);
            if (RegQueryValueExW(hClsid, L"full", nullptr, nullptr, (LPBYTE)iconPath, &size) == ERROR_SUCCESS) {
                for (wchar_t c : iconPath) { hash ^= c; hash *= 1099511628211ULL; }
            }
            RegCloseKey(hClsid);
        }
    };

    hashClsidIcon(L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}"); // This PC / Computer
    hashClsidIcon(L"{645FF040-5081-101B-9F08-00AA002F954E}"); // Recycle Bin
    hashClsidIcon(L"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}"); // Network
    hashClsidIcon(L"{59031a47-3f72-44a7-89c5-5595fe6b30ee}"); // User Files

    return hash;
}

// Bump whenever the label/shadow RENDERING math changes (not just settings):
// baked composites are keyed by the bake hash, so renderer tweaks otherwise
// keep serving stale disk-cached silhouettes.
constexpr uint32_t LABEL_RENDERER_VERSION = 31;

uint64_t GetShadowBakeHash(const Settings& s) {
    uint64_t hash = 14695981039346656037ULL;
    hash ^= (uint64_t)s.shadowPasses;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)s.shadowBlur;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)s.shadowTailSoftness;
    hash *= 1099511628211ULL;
    // shadowLength/shadowOffset determine the baked layer count via
    // totalMaxReach in BakeUnifiedShadows, so they must invalidate the profile.
    hash ^= (uint64_t)s.shadowTailScale;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)s.shadowLength;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)s.shadowOffset;
    hash *= 1099511628211ULL;
    hash ^= (uint64_t)LABEL_RENDERER_VERSION;
    hash *= 1099511628211ULL;
    return hash;
}

// Thread-safe settings snapshot for readers NOT already holding g_renderMutex
// (UI thread, worker threads). The render loop and bake functions read g_settings
// under the lock; everyone else must go through this.
static Settings SnapshotSettings() {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    return g_settings;
}

uint64_t GetShadowBakeHash() {
    Settings s;
    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        s = g_settings;
    }
    return GetShadowBakeHash(s);
}

// [MULTIMON v2.9.4, CORRECTED v2.9.5] Explorer draws desktop items at
// PER-MONITOR DPI scale. Measured coherently on the field rig (125% primary
// + 100% portrait secondary): LVIR_LABEL rects 20px vs 16px tall (the label
// text itself scales), LVIR_ICON cells 94x50 vs 75x41, baselines/shapes
// extract at image-list size (46x46), image-list cx cycles 41..51. So the
// anchor is the MONITOR's DPI ratio — NOT rect-vs-imagelist (those rects are
// per-monitor-scaled CELLS, ~2x the glyph at the current zoom): glyph =
// imagelist cx on the system-DPI monitor, cx * monitorDpi/systemDpi
// elsewhere. The primary is the system-DPI reference (g_dpiScale = its
// dpi/96), so scale = monitorDpi / primaryDpi; 1.0 on the primary keeps the
// byte-identical legacy path.
static float GetItemMonitorScale(HWND hOverlay, const RECT& localRect) {
    if (!hOverlay || !IsWindow(hOverlay)) return 1.0f;
    if (localRect.right <= localRect.left || localRect.bottom <= localRect.top) return 1.0f;
    POINT c = { (localRect.left + localRect.right) / 2, (localRect.top + localRect.bottom) / 2 };
    if (!ClientToScreen(hOverlay, &c)) return 1.0f;
    HMONITOR hMon = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dx, &dy))) return 1.0f;
    float primaryDpi = g_dpiScale.load(std::memory_order_relaxed) * 96.0f;
    if (primaryDpi <= 0.0f) primaryDpi = 96.0f;
    float s = (float)dx / primaryDpi;
    if (s <= 0.3f || s > 2.0f) return 1.0f;           // degenerate: legacy path
    if (std::fabs(s - 1.0f) < 0.02f) return 1.0f;     // primary-DPI monitor: byte-identical legacy path
    return s;
}

// Box-filter downsample of a silhouette alpha array (area average — clean
// edges for the 1.25->0.8 scale factors mixed-DPI setups produce).
static void BoxDownsampleAlpha(const SilhouetteData& src, int dstW, int dstH, std::vector<uint8_t>& dst) {
    dst.assign((size_t)((dstW > 0 ? dstW : 1)) * (dstH > 0 ? dstH : 1), 0);
    int sw = (int)src.width, sh = (int)src.height;
    if (sw <= 0 || sh <= 0 || dstW <= 0 || dstH <= 0 ||
        src.alphaData.size() != (size_t)sw * (size_t)sh) return;
    for (int dy = 0; dy < dstH; ++dy) {
        int y0 = (int)((int64_t)dy * sh / dstH);
        int y1 = (int)((int64_t)(dy + 1) * sh / dstH);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh) y1 = sh;
        for (int dx = 0; dx < dstW; ++dx) {
            int x0 = (int)((int64_t)dx * sw / dstW);
            int x1 = (int)((int64_t)(dx + 1) * sw / dstW);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw) x1 = sw;
            UINT acc = 0, n = 0;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* row = &src.alphaData[(size_t)y * sw];
                for (int x = x0; x < x1; ++x) { acc += row[x]; ++n; }
            }
            dst[(size_t)dy * dstW + dx] = (uint8_t)((acc + n / 2) / n);
        }
    }
}

float GetMonitorDpiScale(HMONITOR monitor) {
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
        return dpiX / 96.0f;
    return 1.0f;
}

HMONITOR GetWorkerWMonitor() {
    HWND hParent = GetDesktopParent();
    if (hParent)
        return MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
    return MonitorFromPoint({0, 0}, MONITOR_DEFAULTTONEAREST);
}

// [MULTIMON v2.9.4] Settings px are tuned against the PRIMARY monitor (Doc's
// physical-px law). GetWorkerWMonitor resolves by largest intersection — a
// big enough secondary monitor would silently flip g_dpiScale to the wrong
// scale. Pin to the primary explicitly.
HMONITOR GetPrimaryMonitor() {
    POINT ptZero = {0, 0};
    return MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
}

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
inline HMODULE GetCurrentModuleHandle() {
    return (HMODULE)&__ImageBase;
}

// Reads TranscodedImageCache binary value for the guaranteed active image path (fixes Slideshows)

std::wstring ReadTranscodedImageCache() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return L"";
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hKey, L"TranscodedImageCache", nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_BINARY) {
        RegCloseKey(hKey); return L"";
    }

    // SANITY CHECK: Prevent std::bad_alloc crash if registry is corrupted with massive size
    if (size == 0 || size > 1024 * 1024) {
        RegCloseKey(hKey); return L"";
    }

    std::vector<BYTE> buffer(size);
    RegQueryValueExW(hKey, L"TranscodedImageCache", nullptr, nullptr, buffer.data(), &size);
    RegCloseKey(hKey);

    if (size <= 24) return L""; // Skip 24-byte header
    const wchar_t* pathStart = reinterpret_cast<const wchar_t*>(buffer.data() + 24);
    size_t maxChars = (size - 24) / sizeof(wchar_t);
    std::wstring path(pathStart, wcsnlen(pathStart, maxChars));

    // SANITY CHECK: Ensure it looks like a valid path (Drive letter or UNC)
    if (path.length() < 3) return L"";
    if (path[1] == L':' && path[2] == L'\\') return path; // C:\...
    if (path[0] == L'\\' && path[1] == L'\\') return path; // \\server\...
    return L"";
}

// Reads exact background color directly from the Shell API (fixes Solid Color swaps)
// Falls back to the legacy registry key if the COM API fails.
// THREADING CONTRACT: Relies on COM but does NOT initialize it.
// MUST only be called from the Explorer UI thread, which is already STA.
void ReadBackgroundColor(D2D1_COLOR_F& color) {
    bool comSuccess = false;
    ComPtr<IDesktopWallpaper> pDesktopWallpaper;

    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pDesktopWallpaper)))) {
        COLORREF bgColor = 0;
        if (SUCCEEDED(pDesktopWallpaper->GetBackgroundColor(&bgColor))) {
            color = D2D1::ColorF(
                GetRValue(bgColor) / 255.0f,
                GetGValue(bgColor) / 255.0f,
                GetBValue(bgColor) / 255.0f,
                1.0f
            );
            comSuccess = true;
        }
    }

    if (!comSuccess) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            WCHAR rgb[32] = {};
            DWORD size = sizeof(rgb);
            if (RegQueryValueExW(hKey, L"Background", nullptr, nullptr, (LPBYTE)rgb, &size) == ERROR_SUCCESS) {
                int r = 0, g = 0, b = 0;
                if (swscanf_s(rgb, L"%d %d %d", &r, &g, &b) == 3)
                    color = D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
            }
            RegCloseKey(hKey);
        }
    }
}

// Load wallpaper image from given path into a Direct2D bitmap.
// Returns true on success, false on failure (file missing, corrupt, etc.)
bool LoadWallpaperBitmap(const std::wstring& path, ComPtr<ID2D1Bitmap>& outBitmap) {
    if (!g_dc || path.empty()) return false;
    if (!g_wicFactory && !InitWIC()) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    // We use GENERIC_READ. If Windows is actively saving the file, this will fail with ERROR_SHARING_VIOLATION.
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = g_wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    ComPtr<ID2D1Bitmap> bitmap;
    hr = g_dc->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap);
    if (FAILED(hr)) return false;

    outBitmap = bitmap;
    return true;
}

// Unified wallpaper and style refresh.
// Uses the modern IDesktopWallpaper API to guarantee 100% accurate reads
// Bypasses registry cache files entirely.
// Must be called while holding g_renderMutex.
void RefreshWallpaperAndStyle() {
    if (!g_dc) return;
    // THREADING CONTRACT: Must be called while holding g_renderMutex AND on the
    // Explorer UI thread (STA). CoCreateInstance for IDesktopWallpaper requires STA.
    // The UI thread is already initialized by Explorer, so we just ride on its state.
    // Do not add call sites from the render thread or any non-UI thread.
    DWORD bgType = 0;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(bgType);
        RegQueryValueExW(hKey, L"BackgroundType", nullptr, nullptr, (LPBYTE)&bgType, &size);
        RegCloseKey(hKey);
    }

    // 1. Force a direct color read (bypasses COM cache)
    D2D1_COLOR_F newColor = {0.0f, 0.0f, 0.0f, 1.0f};
    ReadBackgroundColor(newColor);
    bool colorChanged = (newColor.r != g_desktopBgColor.r || newColor.g != g_desktopBgColor.g || newColor.b != g_desktopBgColor.b);
    g_desktopBgColor = newColor;

    if (bgType == 1) {
        g_hasWallpaper = false;
        g_wallpaperBitmap.Reset();
        g_tileBrush.Reset();
        g_currentWallpaperPath.clear();
        g_lastWallpaperWriteTime = {0, 0};

        if (colorChanged) g_forceRender = true;
    } else {
        // 2. Force a direct image path read (bypasses COM cache for Slideshows)
        std::wstring newPath = ReadTranscodedImageCache();

        // Fallback if cache is completely empty
        if (newPath.empty()) {
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                WCHAR pathBuf[MAX_PATH] = {};
                DWORD size = sizeof(pathBuf);
                if (RegQueryValueExW(hKey, L"Wallpaper", nullptr, nullptr, (LPBYTE)pathBuf, &size) == ERROR_SUCCESS) {
                    newPath = pathBuf;
                }
                RegCloseKey(hKey);
            }
        }

        // 3. We STILL use COM just for the Fit position (Center, Stretch, etc) because that works well
        DESKTOP_WALLPAPER_POSITION pos = DWPOS_CENTER;
        ComPtr<IDesktopWallpaper> pDesktopWallpaper;
        if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pDesktopWallpaper)))) {
            pDesktopWallpaper->GetPosition(&pos);
        }

        // 4. File Timestamp Check
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        FILETIME newWriteTime = {0, 0};
        if (GetFileAttributesExW(newPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
            newWriteTime = fileInfo.ftLastWriteTime;
        }

        bool pathChanged = (newPath != g_currentWallpaperPath);
        bool timeChanged = (CompareFileTime(&newWriteTime, &g_lastWallpaperWriteTime) != 0);
        bool posChanged = (pos != g_wallpaperPosition);

        g_wallpaperPosition = pos;

        if (pathChanged || timeChanged || posChanged) {
            ComPtr<ID2D1Bitmap> newBitmap;
            if (!newPath.empty() && LoadWallpaperBitmap(newPath, newBitmap)) {
                g_wallpaperBitmap = newBitmap;
                g_hasWallpaper = true;
                g_currentWallpaperPath = newPath;
                g_lastWallpaperWriteTime = newWriteTime;
                // [MULTIMON v2.9.6] Wallpaper probe: the replica draws THIS
                // path/position combo; compare against what the desktop
                // shows when a fit looks wrong. Runs only on actual reloads.
                {
                    D2D1_SIZE_U wp = newBitmap ? newBitmap->GetPixelSize() : D2D1::SizeU(0, 0);
                    DiagLog(L"[WALLPAPER] pos=%d img=%ux%u path='%s'",
                            (int)g_wallpaperPosition, wp.width, wp.height, newPath.c_str());
                }
            } else {
                if (g_messageWnd) SetTimer(g_messageWnd, TIMER_ID_WALLPAPER_UPDATE, 300, nullptr);
            }

            g_tileBrush.Reset();
            if (g_hasWallpaper && g_wallpaperPosition == DWPOS_TILE && g_wallpaperBitmap) {
                D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(
                    D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                g_dc->CreateBitmapBrush(g_wallpaperBitmap.Get(), brushProps, &g_tileBrush);
            }
        } else {
        }
    }

    g_forceRender = true;
    if (g_renderEvent) SetEvent(g_renderEvent);
}

////////////////////////////////////////////////////////////////////////////////
// Desktop subclassing

LRESULT CALLBACK ListViewSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK ShellViewSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

bool ApplyDesktopSubclasses(HWND hLV) {
    if (g_unloading) return false;
    if (!hLV || !IsWindow(hLV)) return false;

    HWND hSV   = GetParent(hLV);
    HWND hHost = hSV ? GetParent(hSV) : nullptr;
    if (!hSV) return false;

    if (!SetWindowSubclass(hLV, ListViewSubclassProc, SUBCLASS_ID_LV, 0))
        return false;

    if (!SetWindowSubclass(hSV, ShellViewSubclassProc, SUBCLASS_ID_SV, 0)) {
        RemoveWindowSubclass(hLV, ListViewSubclassProc, SUBCLASS_ID_LV);
        return false;
    }

    g_subclassedLV = hLV;
    g_subclassedSV = hSV;

    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_cachedHwndLV          = hLV;
    g_cachedHwndShellView   = hSV;
    g_cachedHwndDesktopHost = hHost;

    return true;
}

void RemoveDesktopSubclasses() {
    if (g_subclassedSV) {
        RemoveWindowSubclass(g_subclassedSV, ShellViewSubclassProc, SUBCLASS_ID_SV);
        g_subclassedSV = nullptr;
        g_trackingMouseSV = false;
    }
    if (g_subclassedLV) {
        RemoveWindowSubclass(g_subclassedLV, ListViewSubclassProc, SUBCLASS_ID_LV);
        g_subclassedLV = nullptr;
        g_trackingMouseLV = false;
    }
}

static void HandleDesktopMouseMove(HWND hWnd) {
    bool wasOver = g_isMouseOverDesktop.exchange(true, std::memory_order_relaxed);
    bool wasIdle = g_isIdle.exchange(false, std::memory_order_relaxed);

    // Only ask the kernel to track the mouse if we aren't already tracking it!
    bool needsTracking = false;
    if (hWnd == g_subclassedLV && !g_trackingMouseLV) {
        g_trackingMouseLV = true;
        needsTracking = true;
    } else if (hWnd == g_subclassedSV && !g_trackingMouseSV) {
        g_trackingMouseSV = true;
        needsTracking = true;
    }

    if (needsTracking) {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
    }

    // DECOUPLE THREADS: Only wake the render threads on state transitions!
    // This prevents the UI thread from spamming the kernel and stalling.
    if (!wasOver || wasIdle) {
        if (g_renderEvent) SetEvent(g_renderEvent);
        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
    }
}

static void HandleDesktopMouseLeave(HWND hWnd) {
    // Reset the flag so we can track it again next time it enters
    if (hWnd == g_subclassedLV) g_trackingMouseLV = false;
    else if (hWnd == g_subclassedSV) g_trackingMouseSV = false;

    POINT pt; GetCursorPos(&pt);
    HWND hUnder = WindowFromPoint(pt);
    HWND hLV = nullptr, hSV = nullptr, hHost = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV; hSV = g_cachedHwndShellView; hHost = g_cachedHwndDesktopHost;
    }
    bool stillOver = (hUnder == hLV || hUnder == hSV || hUnder == hHost || hUnder == g_overlayWnd || hUnder == g_underlayWnd);
    if (!stillOver && hUnder && hLV) {
        if (GetAncestor(hUnder, GA_PARENT) == hLV) stillOver = true;
    }
    if (!stillOver) {
        g_isMouseOverDesktop.store(false, std::memory_order_relaxed);
        if (g_renderEvent) SetEvent(g_renderEvent);
        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent); // WAKE UP UNDERLAY IMMEDIATELY
    }
}

inline void QueueSelectionUpdate() {
    if (g_overlayWnd) {
        bool expected = false;
        if (g_selectionUpdatePending.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
            PostMessageW(g_overlayWnd, WM_APP_SELECTION_UPDATE, 0, 0);
        }
    }
}

inline void QueueSnapshotUpdate(bool forceAllowDrag = false) {
    if (g_overlayWnd) {
        if (!forceAllowDrag && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) return;

        bool wasPending = g_snapshotUpdatePending.exchange(true, std::memory_order_relaxed);
        if (!wasPending || forceAllowDrag) {
            DiagLog(L"[TRIG-QUEUE] snapshot update queued (pending was %d)", wasPending ? 1 : 0);
            PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
        }
    }
}

// Arm the shared snapshot-retry timer without postponing an earlier deadline.
// Many unrelated sites share TIMER_ID_SNAP_RETRY (drag settle, theme changes,
// icon retry ladder, image-list rebuild); a plain SetTimer would let a
// late-arming long delay push back a retry that was about to fire.
void ArmSnapRetry(UINT delayMs) {
    HWND hOverlay = g_overlayWnd;
    if (!hOverlay || !IsWindow(hOverlay)) return;
    ULONGLONG now = GetTickCount64();
    ULONGLONG due = now + delayMs;
    ULONGLONG prev = g_snapRetryDeadline.load(std::memory_order_acquire);
    if (prev != 0 && (LONGLONG)(prev - now) > 0 && (LONGLONG)(due - prev) > 0) {
        return; // An earlier deadline is already armed and still pending
    }
    g_snapRetryDeadline.store(due, std::memory_order_release);
    SetTimer(hOverlay, TIMER_ID_SNAP_RETRY, delayMs, nullptr);
}

LRESULT CALLBACK ListViewSubclassProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uId, DWORD_PTR dwRef);
LRESULT CALLBACK ListViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR uId, DWORD_PTR dwRef) {
    try {
        return ListViewSubclassProcInner(hWnd, uMsg, wParam, lParam, uId, dwRef);
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] ListViewSubclassProc exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] ListViewSubclassProc unknown exception");
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ListViewSubclassProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR uId, DWORD_PTR dwRef) {
    if (!g_unloading) {
        switch (uMsg) {

        case LVM_SETIMAGELIST:
        {
            LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            UpdateCachedImageList(hWnd);
            BuildGenericBaselines();
            return res;
        }
        case LVM_SETITEMPOSITION:
        case LVM_SETITEMPOSITION32:
        case LVM_ARRANGE:
        {
            LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);

            // Immediately query the new position from Explorer in one atomic hand-off
            int movedIdx = (uMsg == LVM_SETITEMPOSITION || uMsg == LVM_SETITEMPOSITION32) ? (int)wParam : -1;
            if (movedIdx >= 0 && g_overlayWnd) {
                RECT newIcon = {}, newBounds = {}, newLabel = {};
                if (GetListViewItemRects(hWnd, g_overlayWnd, movedIdx, newIcon, newBounds, newLabel)) {
                    std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                    if ((size_t)movedIdx < g_iconSnapshots.size()) {
                        auto& snap = g_iconSnapshots[movedIdx];
                        snap.iconRect = newIcon;
                        snap.boundsRect = newBounds;
                        snap.labelRect = newLabel;
                        snap.isDragged = false;
                        snap.isPendingDrop = false;
                        snap.preDragBoundsRect = {0, 0, 0, 0};

                        RECT combined = newIcon;
                        if ((newLabel.right - newLabel.left) > 0 && (newLabel.bottom - newLabel.top) > 0) {
                            UnionRect(&combined, &newIcon, &newLabel);
                        }
                        snap.combinedRect = combined;
                        g_snapshotGeneration.fetch_add(1, std::memory_order_release);
                    }
                }
            } else if (uMsg == LVM_ARRANGE) {
                std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                for (auto& snap : g_iconSnapshots) {
                    snap.isDragged = false;
                    snap.isPendingDrop = false;
                    snap.preDragBoundsRect = {0, 0, 0, 0};
                }
                g_snapshotGeneration.fetch_add(1, std::memory_order_release);
            }

            if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
            QueueSelectionUpdate();
            QueueSnapshotUpdate(true);
            return res;
        }

            case WM_MOUSEMOVE: {
                static POINT s_dragAnchor = {0, 0};
                static bool s_dragStarted = false;

                if (g_isLeftButtonDown.load(std::memory_order_relaxed)) {
                    short mx = (short)LOWORD(lParam);
                    short my = (short)HIWORD(lParam);
                    int dx = std::abs((int)mx - (int)s_dragAnchor.x);
                    int dy = std::abs((int)my - (int)s_dragAnchor.y);
                    int dragIdx = g_draggedItemIndex.load(std::memory_order_relaxed);
                    // Never treat a press on empty desktop space as an item drag
                    // (that's a marquee selection, not a drag).
                    if (!s_dragStarted && dragIdx >= 0 &&
                        (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG))) {
                        s_dragStarted = true;
                        {
                            std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                            for (size_t i = 0; i < g_iconSnapshots.size(); ++i) {
                                if ((int)i == dragIdx || (g_iconSnapshots[i].state & LVIS_SELECTED)) {
                                    g_iconSnapshots[i].isDragged = true;
                                    g_iconSnapshots[i].preDragBoundsRect = g_iconSnapshots[i].boundsRect;
                                }
                            }
                            g_snapshotGeneration.fetch_add(1, std::memory_order_release);
                        }
                        g_isDraggingItem.store(true, std::memory_order_relaxed);
                        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                    }
                } else {
                    s_dragAnchor = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
                    s_dragStarted = false;
                }
                HandleDesktopMouseMove(hWnd);
                break;
            }
            case WM_MOUSELEAVE:
                HandleDesktopMouseLeave(hWnd);
                break;
            case WM_MOUSEWHEEL:
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    UpdateCachedImageList(hWnd);
                    ArmSnapRetry(300);
                }
                break;

            case WM_SETFOCUS:
                g_isDesktopActive.store(true, std::memory_order_relaxed);
                QueueSelectionUpdate();
                QueueSnapshotUpdate(true);
                if (g_overlayWnd) {
                    SetTimer(g_overlayWnd, TIMER_ID_FOCUS_RETRY, 50, nullptr);
                    SetTimer(g_overlayWnd, TIMER_ID_FOCUS_RETRY + 1, 150, nullptr);
                    SetTimer(g_overlayWnd, TIMER_ID_FOCUS_RETRY + 2, 300, nullptr);
                }
                break;
            case WM_KILLFOCUS:
                g_isDesktopActive.store(false, std::memory_order_relaxed);
                QueueSelectionUpdate();
                QueueSnapshotUpdate(true);
                if (g_overlayWnd) {
                    SetTimer(g_overlayWnd, TIMER_ID_FOCUS_RETRY, 50, nullptr);
                    SetTimer(g_overlayWnd, TIMER_ID_FOCUS_RETRY + 1, 150, nullptr);
                    SetTimer(g_overlayWnd, TIMER_ID_FOCUS_RETRY + 2, 300, nullptr);
                }
                break;

            case WM_KEYDOWN:
            case WM_CHAR:
                QueueSelectionUpdate();
                g_selectionJustChanged.store(true, std::memory_order_relaxed);
                if (g_renderEvent) SetEvent(g_renderEvent);
                break;

            case WM_LBUTTONDBLCLK:
                {
                    bool suppressSpotlight = false;
                    if (SnapshotSettings().toggleOnDoubleClick) {
                        LVHITTESTINFO hti = {};
                        hti.pt.x = (short)LOWORD(lParam);
                        hti.pt.y = (short)HIWORD(lParam);
                        ListView_HitTest(hWnd, &hti);
                        if (hti.flags & LVHT_NOWHERE) {
                            bool nowBypassed = !g_overlayBypassed.load(std::memory_order_relaxed);
                            g_overlayBypassed.store(nowBypassed, std::memory_order_relaxed);
                        if (g_renderEvent) SetEvent(g_renderEvent);
                        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                        suppressSpotlight = nowBypassed;
                    }
                }
                if (!suppressSpotlight) {
                    g_clickOnDesktop.store(true, std::memory_order_relaxed);
                    if (g_renderEvent) SetEvent(g_renderEvent);
                    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                }
            }
            break;

            case WM_MBUTTONDOWN:
                g_clickOnDesktop.store(true, std::memory_order_relaxed);
                if (g_renderEvent) SetEvent(g_renderEvent);
                if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                break;
            case WM_LBUTTONDOWN:
                g_isLeftButtonDown.store(true, std::memory_order_relaxed);
                [[fallthrough]];
            case WM_RBUTTONDOWN: {
                LVHITTESTINFO hti = {};
                hti.pt.x = (short)LOWORD(lParam);
                hti.pt.y = (short)HIWORD(lParam);
                ListView_HitTest(hWnd, &hti);
                if ((hti.flags & LVHT_ONITEMICON) || (hti.flags & LVHT_ONITEMLABEL)) {
                    g_draggedItemIndex.store(hti.iItem, std::memory_order_relaxed);
                } else {
                    g_draggedItemIndex.store(-1, std::memory_order_relaxed);
                }
                g_clickOnDesktop.store(true, std::memory_order_relaxed);
                if (g_renderEvent) SetEvent(g_renderEvent);
                if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                break;
            }

            case WM_LBUTTONUP:
                g_isLeftButtonDown.store(false, std::memory_order_relaxed);
                [[fallthrough]];
            case WM_RBUTTONUP: {
                {
                    std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                    for (auto& snap : g_iconSnapshots) {
                        if (snap.isDragged) {
                            snap.isDragged = false;
                            // Only suppress the old spot when the icon ACTUALLY moved
                            // during the drag. A plain select-click must never flag a
                            // pending drop, otherwise the icon loses its overlay and
                            // shadow until it moves again.
                            bool actuallyMoved = memcmp(&snap.boundsRect, &snap.preDragBoundsRect, sizeof(RECT)) != 0;
                            snap.isPendingDrop = actuallyMoved;
                            snap.preDragBoundsRect = {0, 0, 0, 0};
                        }
                    }
                    g_snapshotGeneration.fetch_add(1, std::memory_order_release);
                }
                g_isDraggingItem.store(false, std::memory_order_relaxed);
                if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                QueueSelectionUpdate();
                QueueSnapshotUpdate(true);
                ArmSnapRetry(100);
                break;
            }
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellViewSubclassProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef);
LRESULT CALLBACK ShellViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    try {
        return ShellViewSubclassProcInner(hWnd, uMsg, wParam, lParam, uId, dwRef);
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] ShellViewSubclassProc exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] ShellViewSubclassProc unknown exception");
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ShellViewSubclassProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
        if (!g_unloading) {
            switch (uMsg) {
                case WM_MOUSEMOVE:
                    HandleDesktopMouseMove(hWnd);
                    break;
                case WM_MOUSELEAVE:
                    HandleDesktopMouseLeave(hWnd);
                    break;
            }

        // Forward critical desktop redraw broadcasts directly to our monitor.
        // [VD-FIX 2026-11-01] VD switches broadcast a STRINGLESS WM_SETTINGCHANGE
        // (lParam empty — Windows names no area; measured: uMsg=26 lParamStr=''
        // twice per switch). Forwarding it wiped ALL icon + shadow caches twice
        // per switch (white-rect fallbacks + ~1s of shadow re-uploads = the
        // visible full reload). Only real theme strings earn the wipe;
        // everything else is logged and ignored. WM_THEMECHANGED /
        // WM_DISPLAYCHANGE keep forwarding (rare, genuinely theme/display-level).
        if (uMsg == WM_SETTINGCHANGE || uMsg == WM_THEMECHANGED || uMsg == WM_DISPLAYCHANGE) {
            const wchar_t* fwdArea = (uMsg == WM_SETTINGCHANGE && lParam)
                                     ? (const wchar_t*)lParam : L"";
            bool fwdIsTheme = (uMsg != WM_SETTINGCHANGE) ||
                              _wcsicmp(fwdArea, L"WindowsThemeElement") == 0 ||
                              _wcsicmp(fwdArea, L"ImmersiveColorSet") == 0;
            // [VD-DIAG] name the exact broadcast riding the desktop chain
            DiagLog(L"[REFRESH-FWD] uMsg=%u lParamStr='%s' -> %s",
                    uMsg, fwdArea,
                    fwdIsTheme ? L"TRIGGER_REFRESH(FULL)" : L"ignored");

            if (fwdIsTheme) {
                if (g_messageWnd) {
                    PostMessageW(g_messageWnd, WM_APP_TRIGGER_REFRESH, 0, 0);
                }
                // DPI or theme changes can alter icon sizes/visuals, force a deep snapshot pass
                // Increased to 500ms to let the Shell completely rebuild the HIMAGELIST
                ArmSnapRetry(500);
            }
        }

        if (uMsg == WM_NOTIFY) {
            NMHDR* hdr = (NMHDR*)lParam;
            if (hdr && hdr->hwndFrom == g_subclassedLV) {

                // Helper to safely queue a Deep Pass, deferring it if the user is mid-drag
                auto QueueDeepPass = [&]() {
                    QueueSnapshotUpdate();
                };

                switch (hdr->code) {
                    case LVN_BEGINDRAG:
                    case LVN_BEGINRDRAG:
                    {
                        NMLISTVIEW* pnmlv = (NMLISTVIEW*)lParam;
                        int dragItem = pnmlv ? pnmlv->iItem : -1;
                        if (dragItem >= 0) {
                            g_draggedItemIndex.store(dragItem, std::memory_order_relaxed);
                        }
                        {
                            std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                            for (size_t i = 0; i < g_iconSnapshots.size(); ++i) {
                                if ((int)i == dragItem || (g_iconSnapshots[i].state & LVIS_SELECTED)) {
                                    g_iconSnapshots[i].isDragged = true;
                                    g_iconSnapshots[i].preDragBoundsRect = g_iconSnapshots[i].boundsRect;
                                }
                            }
                            g_snapshotGeneration.fetch_add(1, std::memory_order_release);
                        }
                        g_isDraggingItem.store(true, std::memory_order_relaxed);
                        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                        break;
                    }
                    case LVN_INSERTITEM:
                    case LVN_DELETEITEM:
                        QueueDeepPass();
                        break;
                 case LVN_ITEMCHANGED:
                     {
                        NMLISTVIEW* pnmlv = (NMLISTVIEW*)lParam;

                        // 1. Always update selection geometry (Lightweight)
                        QueueSelectionUpdate();
                        // Explicitly wake the render thread so it processes the new geometry
                        g_selectionJustChanged.store(true, std::memory_order_relaxed);

                        if (g_renderEvent) SetEvent(g_renderEvent);

                        // 1b. PER-ICON FAST PATH: an item just BECAME selected.
                        // Queue the priority single-icon pass immediately so the
                        // expanded-label composite + shadow assets are ready
                        // before the user can deselect into ghost — instead of
                        // riding the full-pass convoy.
                        if (pnmlv && (pnmlv->uChanged & LVIF_STATE) &&
                            (pnmlv->uNewState & LVIS_SELECTED) &&
                            !(pnmlv->uOldState & LVIS_SELECTED)) {
                            QueueFastPathPass(pnmlv->iItem);
                        }

                        // 2. Check if the actual ICON IMAGE changed (Heavy)
                        bool imageChanged = false;
                        if (pnmlv && (pnmlv->uChanged & LVIF_IMAGE)) {
                            imageChanged = true;
                        }

                        if (imageChanged) {
                            QueueDeepPass();
                        }

                        break;
                        }
                    case LVN_BEGINLABELEDIT:
                        {
                            std::lock_guard<std::mutex> lock(g_stateMutex);
                            g_isEditing = true;
                            // Store the current label text before the user starts editing
                            {
                                HWND hLV = g_cachedHwndLV;
                                NMLVDISPINFOW* pnmdi = (NMLVDISPINFOW*)lParam;
                                if (hLV && pnmdi) {
                                    wchar_t buf[MAX_PATH] = {};
                                    ListView_GetItemText(hLV, pnmdi->item.iItem, 0, buf, MAX_PATH);
                                    g_editingOldText = buf;
                                }
                            }
                        }
                        // [EDIT-SHADOW-OFF v2.7.3] Name the edited item so the
                        // underlay skips its shadow while the label is being
                        // typed. NO QueueFullSnapshot here — nothing needs
                        // rebuilding; the skip is per-frame.
                        {
                            NMLVDISPINFOW* pnmdi = (NMLVDISPINFOW*)lParam;
                            if (pnmdi)
                                g_editingLvIndex.store(pnmdi->item.iItem, std::memory_order_relaxed);
                        }
                        g_selectionJustChanged.store(true, std::memory_order_relaxed);
                        if (g_renderEvent) SetEvent(g_renderEvent);
                        break;

                    case LVN_ENDLABELEDIT:
                    {
                        std::lock_guard<std::mutex> lock(g_stateMutex);
                        g_isEditing = false;

                        // Check if the label text actually changed
                        bool textChanged = false;
                        NMLVDISPINFOW* pnmdi = (NMLVDISPINFOW*)lParam;

                        // If pszText is not null, the user accepted the edit.
                        // If it is null, the user pressed ESC and canceled.
                        if (pnmdi && pnmdi->item.pszText) {
                            std::wstring newText = pnmdi->item.pszText;
                            if (newText != g_editingOldText) {
                                textChanged = true;
                            }
                        }
                        g_editingOldText.clear();

                        // [EDIT-SHADOW-OFF v2.7.3] Clear the skip on every exit
                        // path (accept, ESC-cancel, no-change). Atomic store
                        // only — NO QueueFullSnapshot (the v2.7.0 faint came
                        // from calling it under this lock). Shadow redraws on
                        // the next frame, nothing to rebuild.
                        g_editingLvIndex.store(-1, std::memory_order_relaxed);

                        if (!textChanged) {
                            // Label didn't change (or edit was canceled), no need to re-bake anything
                            break;
                        }

                        g_selectionJustChanged.store(true, std::memory_order_relaxed);
                        if (g_renderEvent) SetEvent(g_renderEvent);

                        // Update snapshot when finishing a rename
                        QueueDeepPass();
                    }
                    break;
                }
            }
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

////////////////////////////////////////////////////////////////////////////////
// DirectX & WIC initialization

bool InitDirectX() {
    HRESULT hr;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                           nullptr, 0, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, nullptr);
    if (FAILED(hr)) return false;

    // Three device contexts (overlay, underlay, worker) share this D3D device
    // from different threads; without the driver-level lock they can race.
    ComPtr<ID3D11Multithread> d3dMt;
    if (SUCCEEDED(g_d3dDevice.As(&d3dMt))) {
        d3dMt->SetMultithreadProtected(TRUE);
    }

    hr = g_d3dDevice.As(&g_dxgiDevice);
    if (FAILED(hr)) return false;

    // Reduce flip-queue latency from the default 3 frames to 1 so the spotlight
    // tracks the cursor more tightly on high refresh rate monitors.
    ComPtr<IDXGIDevice1> dxgiDevice1;
    if (SUCCEEDED(g_dxgiDevice.As(&dxgiDevice1))) {
        dxgiDevice1->SetMaximumFrameLatency(1);
    }

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

bool InitWIC() {
    if (g_wicFactory) return true;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wicFactory));
    return SUCCEEDED(hr);
}

////////////////////////////////////////////////////////////////////////////////
// Swap chain and D2D resources

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

ComPtr<ID2D1Bitmap1> BakeGlowMask(
    UINT bmpW, UINT bmpH,
    const D2D1_RECT_F& shapeRect,
    float cornerRadius,
    float blurVal,
    DXGI_FORMAT pixelFormat,
    ID2D1Effect* blurEffect,
    bool asEllipse = false)
{
    if (!g_dc || bmpW == 0 || bmpH == 0) return nullptr;

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(pixelFormat, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> bakedGlow;
    HRESULT hr = g_dc->CreateBitmap(D2D1::SizeU(bmpW, bmpH), nullptr, 0, &props, bakedGlow.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return nullptr;

    ComPtr<ID2D1Bitmap1> tempShape;
    hr = g_dc->CreateBitmap(D2D1::SizeU(bmpW, bmpH), nullptr, 0, &props, tempShape.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return nullptr;

    ComPtr<ID2D1Image> oldTarget;
    g_dc->GetTarget(oldTarget.ReleaseAndGetAddressOf());

    // Draw pure white shape (Alpha 1.0) on transparent background
    g_dc->SetTarget(tempShape.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    ComPtr<ID2D1SolidColorBrush> whiteBrush;
    g_dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteBrush);

    if (asEllipse) {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(
            D2D1::Point2F((shapeRect.left + shapeRect.right) / 2.0f,
                          (shapeRect.top + shapeRect.bottom) / 2.0f),
            (shapeRect.right - shapeRect.left) / 2.0f,
            (shapeRect.bottom - shapeRect.top) / 2.0f);
        g_dc->FillEllipse(ellipse, whiteBrush.Get());
    } else {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(shapeRect, cornerRadius, cornerRadius);
        g_dc->FillRoundedRectangle(rr, whiteBrush.Get());
    }
    if (!CheckEndDraw(g_dc->EndDraw(), L"BakeGlowMask.shape")) return nullptr;

    // Blur onto Baked Bitmap
    g_dc->SetTarget(bakedGlow.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (blurEffect) {
        blurEffect->SetInput(0, tempShape.Get());
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurVal);
        blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
        g_dc->DrawImage(blurEffect, D2D1_INTERPOLATION_MODE_LINEAR);
    } else {
        g_dc->DrawBitmap(tempShape.Get());
    }
    if (!CheckEndDraw(g_dc->EndDraw(), L"BakeGlowMask.blur")) return nullptr;

    g_dc->SetTarget(oldTarget.Get());
    return bakedGlow;
}

// Selection glow edge blur (gaussian sigma). Shared by the bake and the draw
// pass so the draw-time positioning padding can never drift from the baked
// blur. Clamped BELOW the selection padding: a sigma at/above the padding
// smears the glow across the icon itself and the selection reads as invisible.
static float ComputeSelectionBlurVal(float selectionPad) {
    float maxBlur = selectionPad; // was selectionPad * 10 — blur could reach 10x the padding
    float blurVal = (g_settings.selectionBlur / 100.0f) * maxBlur;
    return (std::max)(0.1f, (std::min)(blurVal, selectionPad * 0.9f));
}

ComPtr<ID2D1Bitmap1> BakeSelectionGlow(UINT width, UINT height) {
    if (!g_dc || width == 0 || height == 0) return nullptr;

    float cornerRadius = (std::min)(width, height) / 2.0f * (g_settings.selectionRoundness / 100.0f);
    float selectionPad = (float)g_settings.selectionPadding * g_dpiScale;
    float blurVal = ComputeSelectionBlurVal(selectionPad);

    float blurPadding = blurVal * 3.0f;
    UINT bmpW = (UINT)std::ceil(width + blurPadding * 2.0f);
    UINT bmpH = (UINT)std::ceil(height + blurPadding * 2.0f);
    if (bmpW == 0) bmpW = 1;
    if (bmpH == 0) bmpH = 1;

    D2D1_RECT_F shapeRect = D2D1::RectF(blurPadding, blurPadding, blurPadding + width, blurPadding + height);

    return BakeGlowMask(bmpW, bmpH, shapeRect, cornerRadius, blurVal,
                        DXGI_FORMAT_B8G8R8A8_UNORM, g_selectionBlurEffect.Get());
}

void BakeSpotlight() {
    g_bakedSpotlight.Reset();
    g_spotlightOpacityBrush.Reset();
    g_overlayClipBrush.Reset();
    if (!g_dc) return;

    float radius = (float)g_settings.spotlightRadius * g_dpiScale;
    float cornerRadius = radius * (g_settings.spotlightRoundness / 100.0f);
    float blurVal = (std::max)(0.1f, (std::min)((float)g_settings.spotlightBlur * g_dpiScale, 250.0f));

    // 🦇 Oval logo (style 1): bake the spotlight as a horizontal ellipse with
    // the logo's 2:1 aspect, scaled down to hug the logo (see kBatSpotOval*).
    float ovalAspect = 1.0f;
    float ovalScale = 1.0f;
    bool asEllipse = false;
    if (g_settings.batSignalEnabled && g_settings.batStyle == 1) {
        ovalAspect = kBatSpotOvalAspect;
        ovalScale = kBatSpotOvalScale;
        asEllipse = true;
    }

    float padding = blurVal * 3.0f;
    float shapeW = radius * 2.0f * ovalAspect * ovalScale;
    float shapeH = radius * 2.0f * ovalScale;
    // The bitmap is exactly shape + padding on ALL sides, so placing the shape
    // at (padding, padding) centers it for ANY aspect/scale — the blur spread
    // can never shift the light off the cursor.
    UINT bmpW = (UINT)std::ceil(shapeW + padding * 2.0f);
    UINT bmpH = (UINT)std::ceil(shapeH + padding * 2.0f);
    if (bmpW == 0) bmpW = 1;
    if (bmpH == 0) bmpH = 1;

    D2D1_RECT_F shapeRect = D2D1::RectF(padding, padding, padding + shapeW, padding + shapeH);

    g_bakedSpotlight = BakeGlowMask(bmpW, bmpH, shapeRect, cornerRadius, blurVal,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT, g_spotlightBlurEffect.Get(),
                                    asEllipse);
    if (!g_bakedSpotlight) return;

    // Create the opacity brush from the baked mask
    D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(
        D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    g_dc->CreateBitmapBrush(g_bakedSpotlight.Get(), brushProps, &g_spotlightOpacityBrush);

    g_bakedSpotlightHalfW = shapeW / 2.0f + padding;
    g_bakedSpotlightHalfH = shapeH / 2.0f + padding;

    // WIRE THE SPOTLIGHT GRAPH ONCE (Massive CPU/GPU spike fix)
    if (g_invertEffect && g_maskMultiplyEffect && g_noiseMultiplyEffect && g_spotlightNoiseComposite && g_spotlightStampEffect) {
        float spotStrength = 0.5f; // Tamed to 3% for FP16
        D2D1_VECTOR_4F spotAddCoeffs = {0.0f, 1.0f, spotStrength, 0.0f};

        WireSensitivityMaskGraph(
            g_bakedSpotlight.Get(),
            g_invertEffect.Get(),
            g_maskMultiplyEffect.Get(),
            g_noiseMultiplyEffect.Get(),
            g_spotlightNoiseComposite.Get(),
            g_spotlightStampEffect.Get(),
            g_noiseTransformEffect.Get(),
            spotAddCoeffs
        );
    }
}


bool CreateSwapChainResources(UINT width, UINT height) {
    HRESULT hr;
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width        = width;
    scd.Height       = height;
    scd.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount  = 2;
    scd.Scaling      = DXGI_SCALING_STRETCH;
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode    = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = g_dxgiFactory->CreateSwapChainForComposition(g_dxgiDevice.Get(), &scd, nullptr, &g_swapChain);
    if (FAILED(hr)) return false;

    hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface2> surface;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
    bitmapProperties.pixelFormat.alphaMode  = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.pixelFormat.format     = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.bitmapOptions          = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = g_dc->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProperties, &g_targetBitmap);
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 fp16Props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &fp16Props, &g_intermediateTarget);
    if (FAILED(hr)) {
        Wh_Log(L"Failed to create FP16 intermediate target: %x", hr);
        return false;
    }

    // Create Ping-Pong Shared Masks (Screen-sized BGRA)
    D2D1_BITMAP_PROPERTIES1 sharedMaskProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &sharedMaskProps, &g_sharedMask[0]);
    g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &sharedMaskProps, &g_sharedMask[1]);
    g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &sharedMaskProps, &g_maskScratchBitmap);
    g_activeSharedMaskIdx.store(0, std::memory_order_release);
    g_underlayWriteMaskIdx = 1;

    hr = DCompositionCreateDevice(g_dxgiDevice.Get(), IID_PPV_ARGS(&g_compositionDevice));
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->CreateTargetForHwnd(g_overlayWnd, TRUE, &g_compositionTarget);
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->CreateVisual(&g_compositionVisual);
    if (FAILED(hr)) return false;

    hr = g_compositionVisual->SetContent(g_swapChain.Get());
    if (FAILED(hr)) return false;

    // [TINT-SPLIT v2.9.26] Second surface: tint-only. Z-ORDER via the
    // EXPLICIT reference form (MSDN receipt from the v2.9.24 field failure):
    // replica as first child (NULL reference, only child so order moot), then
    // tint added ABOVE the replica by name. Never use the NULL-reference
    // convention here again — insertAbove=TRUE + NULL = BELOW all siblings.
    hr = g_dxgiFactory->CreateSwapChainForComposition(g_dxgiDevice.Get(), &scd, nullptr, &g_tintSwapChain);
    if (FAILED(hr)) return false;

    hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_tintDc);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface2> tintSurface;
    hr = g_tintSwapChain->GetBuffer(0, IID_PPV_ARGS(&tintSurface));
    if (FAILED(hr)) return false;

    hr = g_tintDc->CreateBitmapFromDxgiSurface(tintSurface.Get(), bitmapProperties, &g_tintTargetBitmap);
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->CreateVisual(&g_rootVisual);
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->CreateVisual(&g_tintCompVisual);
    if (FAILED(hr)) return false;

    hr = g_tintCompVisual->SetContent(g_tintSwapChain.Get());
    if (FAILED(hr)) return false;

    hr = g_rootVisual->AddVisual(g_compositionVisual.Get(), TRUE, nullptr); // replica: sole child
    if (FAILED(hr)) return false;

    hr = g_rootVisual->AddVisual(g_tintCompVisual.Get(), TRUE, g_compositionVisual.Get()); // tint ABOVE replica, by name
    if (FAILED(hr)) return false;

    hr = g_compositionTarget->SetRoot(g_rootVisual.Get());
    if (FAILED(hr)) return false;

    hr = g_compositionDevice->Commit();
    if (FAILED(hr)) return false;

    g_dpiScale = GetMonitorDpiScale(GetPrimaryMonitor());

    // Frame-1 bootstrap: load ONLY the layout manifest here (UI thread). The
    // heavy composite blobs load on the snapshot worker (LoadCompositesFromDisk).
    g_diskCache.LoadManifestFromDisk(g_dpiScale, GetCurrentIconZoom(), GetEnvironmentHash());

    g_dc->CreateEffect(kCLSID_D2D1GaussianBlur, &g_spotlightBlurEffect);
    if (g_spotlightBlurEffect)
        g_spotlightBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);

    g_dc->CreateEffect(kCLSID_D2D1GaussianBlur, &g_selectionBlurEffect);
    if (g_selectionBlurEffect)
        g_selectionBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    g_dc->CreateEffect(CLSID_D2D1ColorMatrix, &g_spotlightStampEffect);
    g_dc->CreateEffect(CLSID_D2D1ColorMatrix, &g_selectionStampEffect);


    // --- BLUE NOISE DITHERING PIPELINE ---
    // 1. Create the 32-bit BGRA Noise Bitmap (Effects pipeline hates single-channel A8_UNORM)
    std::vector<BYTE> bgraNoise(64 * 64 * 4);
    for (int i = 0; i < 4096; ++i) {
        bgraNoise[i * 4 + 0] = BLUE_NOISE_64x64[i]; // B
        bgraNoise[i * 4 + 1] = BLUE_NOISE_64x64[i]; // G
        bgraNoise[i * 4 + 2] = BLUE_NOISE_64x64[i]; // R
        bgraNoise[i * 4 + 3] = BLUE_NOISE_64x64[i]; // A
    }

    D2D1_BITMAP_PROPERTIES1 noiseProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    g_dc->CreateBitmap(D2D1::SizeU(64, 64), bgraNoise.data(), 64 * 4, &noiseProps, &g_noiseBitmap);

    // 2. Tile Effect (Repeats the 64x64 image infinitely)
    g_dc->CreateEffect(CLSID_D2D1Tile, &g_tileEffect);
    g_tileEffect->SetInput(0, g_noiseBitmap.Get());
    D2D1_VECTOR_4F tileVec = {0.0f, 0.0f, 64.0f, 64.0f}; // Must be VECTOR_4F for D2D1_TILE_PROP_RECT
    g_tileEffect->SetValue(D2D1_TILE_PROP_RECT, tileVec);

    // 3. Affine Transform Effect (Jitters and Rotates the tiled field)
    g_dc->CreateEffect(CLSID_D2D12DAffineTransform, &g_noiseTransformEffect);
    ComPtr<ID2D1Image> tileOutput;
    g_tileEffect->GetOutput(&tileOutput); // Safely get output image!
    g_noiseTransformEffect->SetInput(0, tileOutput.Get());
    g_noiseTransformEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_INTERPOLATION_MODE, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    // --- SENSITIVITY MASK GRAPH (The "Donut" Mask) ---
    // 1. Invert Effect (Creates 1 - Alpha)
    g_dc->CreateEffect(CLSID_D2D1ColorMatrix, &g_invertEffect);
    D2D1_MATRIX_5X4_F invertMatrix = D2D1::Matrix5x4F(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, -1.0f, // Multiply Alpha by -1
        0.0f, 0.0f, 0.0f, 1.0f   // Add 1.0 to Alpha
    );
    g_invertEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, invertMatrix);

    // 2. SPOTLIGHT Mask Multiply (Locked to 1.0f to prevent margin blowout)
    g_dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_maskMultiplyEffect);
    D2D1_VECTOR_4F spotMultCoeffs = {2.0f, 0.0f, 0.0f, 0.0f};
    g_maskMultiplyEffect->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, spotMultCoeffs);

    // 3. SPOTLIGHT Noise Multiply
    g_dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_noiseMultiplyEffect);
    g_noiseMultiplyEffect->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, spotMultCoeffs);

    // --- SELECTION GLOW NOISE GRAPH ---
    g_dc->CreateEffect(CLSID_D2D1ColorMatrix, &g_selInvertEffect);
    g_selInvertEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, invertMatrix);

    g_dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_selMaskMultiplyEffect);
    D2D1_VECTOR_4F selMultCoeffs = {1.0f, 0.0f, 0.0f, 0.0f}; // Baseline 1.0 for selection
    g_selMaskMultiplyEffect->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, selMultCoeffs);

    g_dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_selNoiseMultiplyEffect);
    g_selNoiseMultiplyEffect->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, selMultCoeffs);

    g_dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_selNoiseComposite);
    g_selNoiseComposite->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT, TRUE);

    // 4. Final Add (Spotlight + Strength * ModulatedNoise)
    g_dc->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_spotlightNoiseComposite);
    g_spotlightNoiseComposite->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT, TRUE);

    BakeSpotlight();
    RecreateBrushesAndMask(width, height);

    RefreshWallpaperAndStyle();  // initial load

    return true;
}

void ReleaseSwapChainResources() {
    g_tileBrush.Reset();
    g_selectionBrush.Reset();
    g_spotlightBrush.Reset();
    g_spotlightMaskBitmap.Reset();
    g_selectionMaskBitmap.Reset();
    g_spotlightBlurEffect.Reset();
    g_selectionBlurEffect.Reset();

    g_bakedSpotlight.Reset();
    g_spotlightOpacityBrush.Reset();
    g_overlayClipBrush.Reset();
    g_tintBrush.Reset();
    g_batSignalBitmap.Reset();
    g_spotlightStampEffect.Reset();

    g_noiseBitmap.Reset();
    g_tileEffect.Reset();
    g_noiseTransformEffect.Reset();

    g_invertEffect.Reset();
    g_maskMultiplyEffect.Reset();
    g_noiseMultiplyEffect.Reset();
    g_spotlightNoiseComposite.Reset();
    g_selInvertEffect.Reset();
    g_selMaskMultiplyEffect.Reset();
    g_selNoiseMultiplyEffect.Reset();
    g_selNoiseComposite.Reset();

    g_bakedSelectionGlows.clear();
    g_bakedSelectionGlowUse.clear();
    g_selectionStampEffect.Reset();

    g_sharedMask[0].Reset();
    g_sharedMask[1].Reset();
    g_maskScratchBitmap.Reset();
    g_maskBlurEffect.Reset();
    g_maskRampEffect.Reset();
    g_sharedMaskReady.store(false, std::memory_order_release);

    g_hasWallpaper = false;
    g_currentWallpaperPath.clear();
    g_lastWallpaperWriteTime = {0, 0};

    g_wallpaperBitmap.Reset();
    g_targetBitmap.Reset();
    g_intermediateTarget.Reset();
    g_tintTargetBitmap.Reset();
    g_tintDc.Reset();
    g_tintSwapChain.Reset();
    g_tintCompVisual.Reset();
    g_rootVisual.Reset();
    g_compositionVisual.Reset();
    g_compositionTarget.Reset();
    g_compositionDevice.Reset();
    g_dc.Reset();
    g_swapChain.Reset();
}

void ReleaseAllDirectXResources() {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    std::lock_guard<std::mutex> underlayLock(g_underlayMutex);

    // 1. Clear CPU/GPU asset maps
    {
        std::lock_guard<std::mutex> cLock(g_cacheMutex);
        g_iconCache.clear();
    }
    {
        std::lock_guard<std::mutex> csLock(g_comboShadowCacheMutex);
        g_comboShadowCache.clear();
    }
    {
        std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
        g_iconHashCache.clear();
    }
    {
        std::lock_guard<std::mutex> uLock(g_uniqueShapesMutex);
        g_uniqueShapes.clear();
        g_shapeToBitmap.clear();
    }
    {
        std::lock_guard<std::mutex> oLock(g_overlayCacheMutex);
        g_cpuOverlaySilhouettes.clear();
    }
    g_bakedSelectionGlows.clear();
    g_bakedSelectionGlowUse.clear();

    // 2. Release Overlay Brushes, Bitmaps & Effects
    ReleaseSwapChainResources();

    // 3. Release Underlay Bitmaps & Effects
    g_underlayTargetBitmap.Reset();
    g_stagingTexA.Reset();
    g_maskBlurEffect.Reset();
    g_maskRampEffect.Reset();
    g_underlayBlurEffect.Reset();
    g_whiteBitmap.Reset();

    // 4. Detach DirectComposition Trees cleanly
    if (g_compositionTarget) g_compositionTarget->SetRoot(nullptr);
    if (g_underlayCompTarget) g_underlayCompTarget->SetRoot(nullptr);
    if (g_compositionDevice) g_compositionDevice->Commit();
    if (g_underlayCompDevice) g_underlayCompDevice->Commit();

    g_compositionVisual.Reset();
    g_compositionTarget.Reset();
    g_compositionDevice.Reset();
    g_swapChain.Reset();

    g_tintCompVisual.Reset();
    g_rootVisual.Reset();
    g_tintSwapChain.Reset();
    g_tintTargetBitmap.Reset();
    g_tintDc.Reset();

    g_underlayCompVisual.Reset();
    g_underlayCompTarget.Reset();
    g_underlayCompDevice.Reset();
    g_underlaySwapChain.Reset();

    // 5. Release Device Contexts
    g_dc.Reset();
    g_tintDc.Reset();
    g_underlayDc.Reset();
    g_bakeDc.Reset();
    {
        std::lock_guard<std::mutex> rLock(g_resourceDcMutex);
        g_resourceDc.Reset();
    }

    // 6. Release Root Devices & Factories
    UninitDirectX();
    g_wicFactory.Reset();
    g_workerWicFactory.Reset();
}

bool ResizeSwapChain(UINT width, UINT height) {
    if (!g_swapChain || !g_dc) return false;

    g_dc->SetTarget(nullptr);
    g_targetBitmap.Reset();
    g_intermediateTarget.Reset();
    g_spotlightMaskBitmap.Reset();
    g_selectionMaskBitmap.Reset();

    HRESULT hr = g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface2> surface;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
    bitmapProperties.pixelFormat.alphaMode  = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.pixelFormat.format     = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.bitmapOptions          = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = g_dc->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProperties, &g_targetBitmap);
    if (FAILED(hr)) return false;

    // [TINT-SPLIT v2.9.26] resize the tint surface in lockstep
    if (g_tintSwapChain && g_tintDc) {
        g_tintDc->SetTarget(nullptr);
        g_tintTargetBitmap.Reset();
        HRESULT hrT = g_tintSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (SUCCEEDED(hrT)) {
            ComPtr<IDXGISurface2> tintSurface;
            hrT = g_tintSwapChain->GetBuffer(0, IID_PPV_ARGS(&tintSurface));
            if (SUCCEEDED(hrT)) {
                g_tintDc->CreateBitmapFromDxgiSurface(tintSurface.Get(), bitmapProperties, &g_tintTargetBitmap);
            }
        }
    }

    D2D1_BITMAP_PROPERTIES1 fp16Props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &fp16Props, &g_intermediateTarget);
    if (FAILED(hr)) {
        Wh_Log(L"Failed to create FP16 intermediate target on resize: %x", hr);
        return false;
    }

    // Rebuild the shared masks by SWAPPING fresh bitmaps into the globals
    // rather than resetting them in place: the underlay thread copies these
    // ComPtrs mid-frame, and any in-flight copy keeps an old bitmap alive
    // until it finishes drawing. Readers gate on g_sharedMaskReady, which we
    // clear until the underlay rebuilds the masks at the new size.
    ComPtr<ID2D1Bitmap1> newMask0, newMask1, newScratch;
    D2D1_BITMAP_PROPERTIES1 sharedMaskProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hrMask0 = g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &sharedMaskProps, &newMask0);
    HRESULT hrMask1 = g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &sharedMaskProps, &newMask1);
    HRESULT hrScratch = g_dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &sharedMaskProps, &newScratch);
    if (SUCCEEDED(hrMask0) && SUCCEEDED(hrMask1) && SUCCEEDED(hrScratch)) {
        g_sharedMaskReady.store(false, std::memory_order_release);
        g_activeSharedMaskIdx.store(0, std::memory_order_release);
        g_underlayWriteMaskIdx = 1;
        g_maskScratchBitmap = newScratch;
        g_sharedMask[1] = newMask1;
        g_sharedMask[0] = newMask0;
    } else {
        // Keep the old (wrong-size but valid) bitmaps rather than leaving nulls.
        Wh_Log(L"Failed to recreate shared masks on resize: %x/%x/%x", hrMask0, hrMask1, hrScratch);
    }

    RecreateBrushesAndMask(width, height);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Helper: compute source and target rectangles for wallpaper drawing
// refSize must be in LOGICAL coordinates (same space as render target / window)

void ComputeWallpaperRects(const D2D1_SIZE_F& imgSize,
                           const D2D1_SIZE_F& refSize,
                           DESKTOP_WALLPAPER_POSITION position,
                           D2D1_RECT_F& sourceRect,
                           D2D1_RECT_F& targetRect) {
    // Default: full source, full target
    sourceRect = D2D1::RectF(0, 0, imgSize.width, imgSize.height);
    targetRect = D2D1::RectF(0, 0, refSize.width, refSize.height);

    switch (position) {
        case DWPOS_CENTER: {
            // Center without scaling
            float left = (refSize.width - imgSize.width) / 2.0f;
            float top  = (refSize.height - imgSize.height) / 2.0f;
            targetRect = D2D1::RectF(left, top, left + imgSize.width, top + imgSize.height);
            break;
        }
        case DWPOS_FIT: {
            // Scale to fit entirely within screen (letterbox)
            float scale = std::min(refSize.width / imgSize.width, refSize.height / imgSize.height);
            float drawW = imgSize.width * scale;
            float drawH = imgSize.height * scale;
            float left = (refSize.width - drawW) / 2.0f;
            float top  = (refSize.height - drawH) / 2.0f;
            targetRect = D2D1::RectF(left, top, left + drawW, top + drawH);
            break;
        }
        case DWPOS_FILL: {
            float scaleX = refSize.width / imgSize.width;
            float scaleY = refSize.height / imgSize.height;
            float scale = std::max(scaleX, scaleY);

            float srcW = refSize.width / scale;
            float srcH = refSize.height / scale;

            // 1. Default mathematical crop (divided by 2)
            float srcX = (imgSize.width - srcW) / 2.0f;
            float srcY = (imgSize.height - srcH) / 2.0f;

            // 2. THE UNIVERSAL WINDOWS QUIRK
            // Windows Shell hardcodes a /3 divisor for the centering crop,
            // completely ignoring standard mathematical centering (/2).
            // This applies at ALL DPI scales, including 100%.
            float screenAR = refSize.height / refSize.width;
            float correctCropH = imgSize.height - (imgSize.width * screenAR);
            float correctCropW = imgSize.width - (imgSize.height / screenAR);

            if (scaleX > scaleY) {
                srcY = correctCropH / 3.0f;
            } else if (scaleY > scaleX) {
                srcX = correctCropW / 3.0f;
            }

            // 3. RESIZE THEN CROP (The Painter's Pipeline)
            // Map the FULL source image to an oversized target rect.
            float drawW = imgSize.width * scale;
            float drawH = imgSize.height * scale;

            // Project the source offset into target coordinates
            float targetLeft = -(srcX * scale);
            float targetTop  = -(srcY * scale);

            sourceRect = D2D1::RectF(0, 0, imgSize.width, imgSize.height);
            targetRect = D2D1::RectF(targetLeft, targetTop, targetLeft + drawW, targetTop + drawH);
            break;
        }
        case DWPOS_STRETCH:
        default: {
            // Stretch to exactly fill (no aspect ratio preservation)
            // sourceRect = full image, targetRect = full screen (already set)
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Background render thread

void UpdateMouseAndAnimations(float deltaTime) {

    if (g_clickOnDesktop.exchange(false, std::memory_order_relaxed)) {
        g_isIdle       = false;
        g_lastMouseTime = GetTickCount64();
    }

    POINT screenPos;
    GetCursorPos(&screenPos);

    bool mouseMoved = false;
    {
        static POINT s_lastScreenPos = {0, 0};
        mouseMoved = (screenPos.x != s_lastScreenPos.x || screenPos.y != s_lastScreenPos.y);
        s_lastScreenPos = screenPos;
    }

    POINT localPos = screenPos;
    if (g_overlayWnd && IsWindow(g_overlayWnd)) {
        ScreenToClient(g_overlayWnd, &localPos);
    }

    float lerpFactor = CalculateLerpFactor(g_settings.mouseSmoothing, deltaTime);

    bool overDesktop = g_isMouseOverDesktop.load(std::memory_order_relaxed);
    bool isEditing   = false;
    std::vector<RECT> currentSelectedRects;
    // [MENU-HOLD v2.8.1] Cursor over the DESKTOP's own right-click menu must
    // count as "on the desktop" — the menu is a separate #32768 popup window,
    // so overDesktop went false the moment the menu opened and the spotlight
    // faded out while the user browsed menu items. Identity-gated: the menu
    // window must belong to the desktop ListView's thread (the shell view's
    // TrackPopupMenu), so right-click menus of other apps never hold the
    // light. Raw GetCursorPos — menu loops swallow WM_MOUSEMOVE.
    bool overMenu = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        currentSelectedRects = g_selectedRects;
        isEditing            = g_isEditing;
        if (!overDesktop && g_cachedHwndLV && IsWindow(g_cachedHwndLV)) {
            HWND hMenu = FindWindowW(L"#32768", nullptr);
            if (hMenu && IsWindowVisible(hMenu)) {
                DWORD menuThread = GetWindowThreadProcessId(hMenu, nullptr);
                DWORD lvThread   = GetWindowThreadProcessId(g_cachedHwndLV, nullptr);
                if (menuThread == lvThread) {
                    RECT mr;
                    if (GetWindowRect(hMenu, &mr) && PtInRect(&mr, screenPos))
                        overMenu = true;
                }
            }
        }
    }

    {
        static bool s_wasOverDesktop = false;
        if ((overDesktop || overMenu) && !s_wasOverDesktop) {
            g_smoothedMousePos = {(float)localPos.x, (float)localPos.y};
            g_lastMouseTime = GetTickCount64();
            g_isIdle        = false;
        } else if ((overDesktop || overMenu) && mouseMoved) {
            g_lastMouseTime = GetTickCount64();
            g_isIdle        = false;
        }
        s_wasOverDesktop = (overDesktop || overMenu);
    }

    // OLE drags stop WM_MOUSEMOVE delivery (and fire WM_MOUSELEAVE at drag
    // start), so overDesktop/idle go stale mid-drag and the selection glow
    // decayed at the OLD position while the user was still dragging. Raw
    // cursor movement (GetCursorPos needs no messages) during an icon drag
    // counts as activity.
    bool dragActive = g_isDraggingItem.load(std::memory_order_relaxed) ||
                      (g_draggedItemIndex.load(std::memory_order_relaxed) != -1);
    if (dragActive && mouseMoved) {
        g_lastMouseTime = GetTickCount64();
        g_isIdle        = false;
    }

    // [FOCUS-STORM HACK — Doc 2026-10-24] The only trigger ever proven to make
    // DWM recompose the desktop (so our lone drag-end frame surfaces) is a
    // focus change — the "first drag after refocus always works" storm. So the
    // instant a REAL drag's button is released (threshold passed — plain
    // select-clicks must not storm), replay the storm: toggle foreground to
    // our own overlay window and straight back to the desktop, same handler,
    // no timer, no third app. Runs on the UI thread via WM_APP_FOCUSSTORM.
    {
        bool draggingReal = g_isDraggingItem.load(std::memory_order_relaxed);
        bool leftDownNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        static bool s_stormArmed = false;
        if (draggingReal && leftDownNow) {
            s_stormArmed = true;
        } else if (s_stormArmed && (!leftDownNow || !dragActive)) {
            s_stormArmed = false;
            if (g_messageWnd && !g_unloading)
                PostMessageW(g_messageWnd, WM_APP_FOCUSSTORM, 0, 0);
        }
    }

    g_smoothedMousePos.x += ((float)localPos.x - g_smoothedMousePos.x) * lerpFactor;
    g_smoothedMousePos.y += ((float)localPos.y - g_smoothedMousePos.y) * lerpFactor;

    ULONGLONG timeSinceLastMove = GetTickCount64() - g_lastMouseTime;
    // [MENU-HOLD v2.8.1] overMenu keeps the light alive while the cursor is
    // on the desktop's context menu; the idle DELAY still applies normally,
    // so a menu left untouched for the idle timeout fades like the desktop.
    if ((!overDesktop && !overMenu) ||
        (g_settings.idleDelay > 0 && timeSinceLastMove > (ULONGLONG)g_settings.idleDelay))
        g_isIdle = true;

    bool isBypassed = g_overlayBypassed.load(std::memory_order_relaxed);
    bool batActive = g_settings.batSignalEnabled;
    float targetSpotFade = (batActive && isBypassed) ? 1.0f : ((g_isIdle || !g_settings.enableSpotlight || isBypassed) ? 0.0f : 1.0f);

    float currentSpotFade = g_spotlightFade.load(std::memory_order_relaxed);
    currentSpotFade = AnimateFade(currentSpotFade, targetSpotFade, g_settings.spotlightFadeInDuration, g_settings.spotlightFadeOutDuration, deltaTime);
    g_spotlightFade.store(currentSpotFade, std::memory_order_relaxed);

    bool selChanged = g_selectionJustChanged.exchange(false, std::memory_order_relaxed);

    if (!currentSelectedRects.empty()) {
        g_cachedSelectedRects = currentSelectedRects;
        if (selChanged || !g_isIdle) {
            g_lastSelectionTime = GetTickCount64();
        }

        // LAZY BAKE: Safely bake the selection glows for ALL unique sizes BEFORE BeginDraw() starts!
        for (const auto& rc : g_cachedSelectedRects) {
            UINT targetW = (UINT)(rc.right - rc.left) + (UINT)(g_settings.selectionPadding * g_dpiScale * 2);
            UINT targetH = (UINT)(rc.bottom - rc.top) + (UINT)(g_settings.selectionPadding * g_dpiScale * 2);
            uint64_t key = ((uint64_t)targetW << 32) | targetH;

            if (g_bakedSelectionGlows.find(key) == g_bakedSelectionGlows.end()) {
                ComPtr<ID2D1Bitmap1> baked = BakeSelectionGlow(targetW, targetH);
                if (baked) {
                    g_bakedSelectionGlows[key] = baked;
                    g_bakedSelectionGlowUse[key] = GetTickCount64();
                }
            } else {
                g_bakedSelectionGlowUse[key] = GetTickCount64();
            }
        }

        // Prevent infinite cache growth from DPI changes or window resizing.
        // LRU eviction instead of a wholesale clear: clearing everything forced
        // every glow to re-bake next frame (visible GPU spike).
        if (g_bakedSelectionGlows.size() > 100) {
            // Sort once by last-use and evict the tail in bulk (the previous
            // loop re-scanned the whole map for every single eviction).
            std::vector<std::pair<uint64_t, ULONGLONG>> entries;
            entries.reserve(g_bakedSelectionGlowUse.size());
            for (const auto& entry : g_bakedSelectionGlowUse) {
                entries.emplace_back(entry.first, entry.second);
            }
            std::sort(entries.begin(), entries.end(),
                      [](const std::pair<uint64_t, ULONGLONG>& a, const std::pair<uint64_t, ULONGLONG>& b) {
                          return a.second < b.second;
                      });
            size_t toRemove = entries.size() - 75;
            for (size_t i = 0; i < toRemove; ++i) {
                g_bakedSelectionGlows.erase(entries[i].first);
                g_bakedSelectionGlowUse.erase(entries[i].first);
            }
        }
    }

    // 2. Calculate target fade based on the REAL current selection state, not the cached geometry
    float targetSelFade = 0.0f;

    // We use GetAsyncKeyState because OLE Drag-Drop can swallow WM_LBUTTONUP, leaving our state stuck!
    bool isLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // Force clear the dragged index if the button is physically up
    if (!isLeftDown && g_draggedItemIndex.load(std::memory_order_relaxed) != -1) {
        g_draggedItemIndex.store(-1, std::memory_order_relaxed);
    }

    if (!g_settings.enableSelectionGlow) {
        targetSelFade = 0.0f;
    } else if (!currentSelectedRects.empty()) {
        if (isEditing) {
            targetSelFade       = 1.0f;
            g_lastSelectionTime = GetTickCount64();
        } else if (g_settings.selectionTimeout <= 0) {
            targetSelFade = 1.0f;
        } else {
            ULONGLONG timeSinceInteraction = GetTickCount64() - g_lastSelectionTime;
            targetSelFade = (timeSinceInteraction <= (ULONGLONG)g_settings.selectionTimeout) ? 1.0f : 0.0f;
        }
    }

    // 3. Animate the fade in/out
    g_selectionFade = AnimateFade(g_selectionFade, targetSelFade, g_settings.selectionFadeInDuration, g_settings.selectionFadeOutDuration, deltaTime);

    // bool isBypassed = g_overlayBypassed.load(std::memory_order_relaxed);
    bool effectiveGlobalTracking = g_settings.shadowGlobalTracking && isBypassed;
    bool maskGateOk = (!g_settings.overlayClipToIcons || g_sharedMaskReady.load(std::memory_order_acquire));
    bool shadowsVisible = g_settings.enableShadows && (overDesktop || effectiveGlobalTracking) && maskGateOk;
    g_shadowFade = UpdateShadowFade(g_settings, g_shadowFade, shadowsVisible, deltaTime);

    // Master overlay toggle fade (independent of spotlight/selection)
    float targetMasterFade =
        (g_overlayBypassed.load(std::memory_order_relaxed) ||
         !g_settings.overlayEnabled)
            ? 0.0f
            : 1.0f;


    g_masterFade = AnimateFade(g_masterFade, targetMasterFade, g_settings.toggleFadeInDuration, g_settings.toggleFadeOutDuration, deltaTime);

    // 4. Clean up the cached geometry ONLY after the fade-out animation is completely finished
    if (currentSelectedRects.empty() && g_selectionFade <= 0.0f && targetSelFade < 0.5f) {
        g_cachedSelectedRects.clear();
    }

    // NOISE ANIMATION: Jump and rotate ONLY when moving or fading
    static float s_lastSpotlightFadeForNoise = -1.0f;
    static ULONGLONG s_lastNoiseUpdate = 0;

    float currentSpotFadeForNoise = g_spotlightFade.load(std::memory_order_relaxed);
    bool isSpotlightFading = (std::abs(currentSpotFadeForNoise - s_lastSpotlightFadeForNoise) > 0.001f);

    if (mouseMoved || isSpotlightFading) {
        ULONGLONG now = GetTickCount64();
        if (now - s_lastNoiseUpdate > 40) { // Jump every 40ms
            // Thread-local RNG to avoid MSVC's thread-unsafe global rand() state
            thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 63);

            g_noiseOffsetX = (float)dist(rng);
            g_noiseOffsetY = (float)dist(rng);
            g_noiseRotation = (g_noiseRotation + 1) % 4;
            s_lastNoiseUpdate = now;
        }
    }
    s_lastSpotlightFadeForNoise = currentSpotFadeForNoise;
}

// ---------------------------------------------------------------------------
// 🦇 BAT SIGNAL
// Bakes the bat silhouette ("Batman Begins" angular style) into a 512x256
// bitmap once; RenderOverlay draws it scaled under the cursor. Coordinates are
// normalized: x in [-1,1], y in [-0.7,0.7]. Iterate the point table to tweak
// the shape - wings, ears and scallops are all explicit vertices.
// ---------------------------------------------------------------------------
static const D2D1_POINT_2F kBatOutline[] = {
    {   0.101f,  -0.644f }, {   0.082f,  -0.602f }, {   0.067f,  -0.557f }, {   0.049f,  -0.521f },
    {   0.013f,  -0.521f }, {  -0.022f,  -0.521f }, {  -0.055f,  -0.535f }, {  -0.072f,  -0.578f },
    {  -0.089f,  -0.622f }, {  -0.108f,  -0.663f }, {  -0.124f,  -0.659f }, {  -0.125f,  -0.609f },
    {  -0.128f,  -0.559f }, {  -0.131f,  -0.510f }, {  -0.137f,  -0.461f }, {  -0.144f,  -0.412f },
    {  -0.154f,  -0.364f }, {  -0.167f,  -0.318f }, {  -0.184f,  -0.275f }, {  -0.207f,  -0.237f },
    {  -0.235f,  -0.206f }, {  -0.267f,  -0.185f }, {  -0.302f,  -0.175f }, {  -0.338f,  -0.176f },
    {  -0.372f,  -0.188f }, {  -0.404f,  -0.209f }, {  -0.432f,  -0.239f }, {  -0.454f,  -0.278f },
    {  -0.470f,  -0.323f }, {  -0.478f,  -0.371f }, {  -0.480f,  -0.421f }, {  -0.475f,  -0.470f },
    {  -0.465f,  -0.518f }, {  -0.451f,  -0.564f }, {  -0.437f,  -0.609f }, {  -0.468f,  -0.602f },
    {  -0.501f,  -0.586f }, {  -0.535f,  -0.569f }, {  -0.568f,  -0.552f }, {  -0.601f,  -0.533f },
    {  -0.634f,  -0.514f }, {  -0.666f,  -0.493f }, {  -0.698f,  -0.470f }, {  -0.729f,  -0.446f },
    {  -0.759f,  -0.421f }, {  -0.789f,  -0.393f }, {  -0.818f,  -0.364f }, {  -0.845f,  -0.332f },
    {  -0.871f,  -0.299f }, {  -0.896f,  -0.263f }, {  -0.919f,  -0.225f }, {  -0.939f,  -0.184f },
    {  -0.957f,  -0.141f }, {  -0.973f,  -0.096f }, {  -0.985f,  -0.050f }, {  -0.995f,  -0.002f },
    {  -1.000f,   0.047f }, {  -0.999f,   0.097f }, {  -0.995f,   0.146f }, {  -0.987f,   0.195f },
    {  -0.976f,   0.242f }, {  -0.961f,   0.287f }, {  -0.944f,   0.331f }, {  -0.924f,   0.372f },
    {  -0.902f,   0.412f }, {  -0.879f,   0.449f }, {  -0.853f,   0.484f }, {  -0.827f,   0.517f },
    {  -0.799f,   0.547f }, {  -0.769f,   0.575f }, {  -0.738f,   0.599f }, {  -0.706f,   0.620f },
    {  -0.673f,   0.638f }, {  -0.638f,   0.652f }, {  -0.604f,   0.662f }, {  -0.606f,   0.632f },
    {  -0.622f,   0.587f }, {  -0.635f,   0.541f }, {  -0.644f,   0.492f }, {  -0.647f,   0.443f },
    {  -0.638f,   0.395f }, {  -0.613f,   0.361f }, {  -0.580f,   0.343f }, {  -0.544f,   0.339f },
    {  -0.509f,   0.343f }, {  -0.474f,   0.354f }, {  -0.441f,   0.370f }, {  -0.408f,   0.391f },
    {  -0.379f,   0.419f }, {  -0.356f,   0.457f }, {  -0.337f,   0.432f }, {  -0.312f,   0.396f },
    {  -0.285f,   0.365f }, {  -0.251f,   0.352f }, {  -0.215f,   0.353f }, {  -0.182f,   0.368f },
    {  -0.151f,   0.394f }, {  -0.124f,   0.426f }, {  -0.099f,   0.461f }, {  -0.076f,   0.499f },
    {  -0.055f,   0.540f }, {  -0.037f,   0.582f }, {  -0.021f,   0.627f }, {  -0.008f,   0.673f },
    {   0.004f,   0.685f }, {   0.019f,   0.639f }, {   0.035f,   0.595f }, {   0.053f,   0.552f },
    {   0.072f,   0.510f }, {   0.094f,   0.471f }, {   0.118f,   0.435f }, {   0.145f,   0.402f },
    {   0.175f,   0.374f }, {   0.207f,   0.355f }, {   0.242f,   0.347f }, {   0.277f,   0.355f },
    {   0.308f,   0.379f }, {   0.332f,   0.416f }, {   0.347f,   0.461f }, {   0.373f,   0.444f },
    {   0.399f,   0.411f }, {   0.429f,   0.383f }, {   0.461f,   0.361f }, {   0.494f,   0.346f },
    {   0.530f,   0.340f }, {   0.565f,   0.343f }, {   0.599f,   0.357f }, {   0.629f,   0.382f },
    {   0.646f,   0.426f }, {   0.648f,   0.475f }, {   0.641f,   0.524f }, {   0.629f,   0.570f },
    {   0.612f,   0.614f }, {   0.592f,   0.656f }, {   0.611f,   0.664f }, {   0.645f,   0.652f },
    {   0.679f,   0.636f }, {   0.712f,   0.617f }, {   0.743f,   0.594f }, {   0.774f,   0.568f },
    {   0.803f,   0.539f }, {   0.831f,   0.508f }, {   0.857f,   0.475f }, {   0.882f,   0.440f },
    {   0.906f,   0.403f }, {   0.928f,   0.363f }, {   0.948f,   0.322f }, {   0.965f,   0.279f },
    {   0.980f,   0.234f }, {   0.992f,   0.187f }, {   0.999f,   0.138f }, {   1.000f,   0.089f },
    {   0.999f,   0.039f }, {   0.996f,  -0.011f }, {   0.991f,  -0.060f }, {   0.978f,  -0.107f },
    {   0.962f,  -0.151f }, {   0.943f,  -0.193f }, {   0.921f,  -0.232f }, {   0.897f,  -0.269f },
    {   0.872f,  -0.303f }, {   0.845f,  -0.336f }, {   0.817f,  -0.366f }, {   0.788f,  -0.395f },
    {   0.758f,  -0.423f }, {   0.728f,  -0.449f }, {   0.697f,  -0.473f }, {   0.665f,  -0.496f },
    {   0.633f,  -0.517f }, {   0.600f,  -0.537f }, {   0.568f,  -0.556f }, {   0.534f,  -0.574f },
    {   0.501f,  -0.591f }, {   0.467f,  -0.606f }, {   0.433f,  -0.618f }, {   0.444f,  -0.582f },
    {   0.461f,  -0.539f }, {   0.473f,  -0.492f }, {   0.479f,  -0.443f }, {   0.480f,  -0.393f },
    {   0.473f,  -0.344f }, {   0.459f,  -0.299f }, {   0.440f,  -0.257f }, {   0.415f,  -0.221f },
    {   0.386f,  -0.193f }, {   0.353f,  -0.175f }, {   0.317f,  -0.173f }, {   0.282f,  -0.176f },
    {   0.249f,  -0.195f }, {   0.221f,  -0.225f }, {   0.198f,  -0.263f }, {   0.179f,  -0.305f },
    {   0.164f,  -0.350f }, {   0.152f,  -0.397f }, {   0.143f,  -0.445f }, {   0.136f,  -0.494f },
    {   0.131f,  -0.543f }, {   0.127f,  -0.592f }, {   0.125f,  -0.642f }, {   0.121f,  -0.685f },
    {   0.101f,  -0.644f },
};

static const D2D1_POINT_2F kBatOutlineOval[] = {
    {  -0.301f,  -0.648f }, {  -0.352f,  -0.641f }, {  -0.403f,  -0.629f },  {  -0.452f,  -0.615f },
    {  -0.501f,  -0.597f }, {  -0.549f,  -0.576f }, {  -0.595f,  -0.552f },  {  -0.639f,  -0.525f },
    {  -0.682f,  -0.496f }, {  -0.724f,  -0.465f }, {  -0.763f,  -0.430f },  {  -0.800f,  -0.394f },
    {  -0.835f,  -0.356f }, {  -0.868f,  -0.316f }, {  -0.898f,  -0.274f },  {  -0.925f,  -0.229f },
    {  -0.949f,  -0.183f }, {  -0.968f,  -0.135f }, {  -0.984f,  -0.086f },  {  -0.995f,  -0.035f },
    {  -1.000f,   0.017f }, {  -1.000f,   0.069f }, {  -0.994f,   0.120f },  {  -0.982f,   0.171f },
    {  -0.964f,   0.219f }, {  -0.941f,   0.266f }, {  -0.914f,   0.310f },  {  -0.882f,   0.351f },
    {  -0.846f,   0.389f }, {  -0.808f,   0.424f }, {  -0.768f,   0.456f },  {  -0.725f,   0.486f },
    {  -0.681f,   0.514f }, {  -0.636f,   0.539f }, {  -0.590f,   0.563f },  {  -0.542f,   0.584f },
    {  -0.494f,   0.604f }, {  -0.446f,   0.623f }, {  -0.397f,   0.639f },  {  -0.432f,   0.601f },
    {  -0.467f,   0.563f }, {  -0.501f,   0.524f }, {  -0.533f,   0.483f },  {  -0.565f,   0.442f },
    {  -0.595f,   0.400f }, {  -0.623f,   0.356f }, {  -0.648f,   0.311f },  {  -0.661f,   0.261f },
    {  -0.648f,   0.212f }, {  -0.604f,   0.184f }, {  -0.553f,   0.179f },  {  -0.502f,   0.189f },
    {  -0.454f,   0.208f }, {  -0.409f,   0.235f }, {  -0.368f,   0.266f },  {  -0.329f,   0.301f },
    {  -0.293f,   0.337f }, {  -0.258f,   0.376f }, {  -0.225f,   0.416f },  {  -0.232f,   0.382f },
    {  -0.240f,   0.331f }, {  -0.233f,   0.280f }, {  -0.206f,   0.237f },  {  -0.157f,   0.227f },
    {  -0.114f,   0.255f }, {  -0.084f,   0.297f }, {  -0.062f,   0.344f },  {  -0.045f,   0.393f },
    {  -0.032f,   0.443f }, {  -0.022f,   0.494f }, {  -0.014f,   0.545f },  {  -0.008f,   0.597f },
    {  -0.003f,   0.649f }, {   0.004f,   0.633f }, {   0.009f,   0.581f },  {   0.016f,   0.529f },
    {   0.024f,   0.478f }, {   0.035f,   0.428f }, {   0.049f,   0.378f },  {   0.067f,   0.329f },
    {   0.091f,   0.283f }, {   0.125f,   0.244f }, {   0.172f,   0.225f },  {   0.216f,   0.248f },
    {   0.236f,   0.296f }, {   0.237f,   0.347f }, {   0.225f,   0.398f },  {   0.233f,   0.405f },
    {   0.266f,   0.366f }, {   0.302f,   0.327f }, {   0.339f,   0.291f },  {   0.378f,   0.257f },
    {   0.420f,   0.227f }, {   0.466f,   0.202f }, {   0.515f,   0.184f },  {   0.566f,   0.179f },
    {   0.616f,   0.190f }, {   0.655f,   0.224f }, {   0.659f,   0.275f },  {   0.641f,   0.323f },
    {   0.615f,   0.368f }, {   0.587f,   0.411f }, {   0.556f,   0.453f },  {   0.524f,   0.494f },
    {   0.491f,   0.534f }, {   0.457f,   0.573f }, {   0.421f,   0.611f },  {   0.410f,   0.635f },
    {   0.459f,   0.618f }, {   0.507f,   0.598f }, {   0.555f,   0.578f },  {   0.602f,   0.556f },
    {   0.648f,   0.532f }, {   0.693f,   0.506f }, {   0.737f,   0.478f },  {   0.779f,   0.448f },
    {   0.819f,   0.414f }, {   0.856f,   0.378f }, {   0.890f,   0.339f },  {   0.921f,   0.298f },
    {   0.948f,   0.253f }, {   0.969f,   0.206f }, {   0.985f,   0.157f },  {   0.996f,   0.106f },
    {   1.000f,   0.054f }, {   0.999f,   0.002f }, {   0.992f,  -0.049f },  {   0.979f,  -0.100f },
    {   0.963f,  -0.149f }, {   0.941f,  -0.196f }, {   0.917f,  -0.242f },  {   0.889f,  -0.286f },
    {   0.859f,  -0.328f }, {   0.825f,  -0.367f }, {   0.789f,  -0.405f },  {   0.751f,  -0.440f },
    {   0.711f,  -0.474f }, {   0.670f,  -0.505f }, {   0.626f,  -0.533f },  {   0.581f,  -0.559f },
    {   0.535f,  -0.582f }, {   0.487f,  -0.602f }, {   0.438f,  -0.619f },  {   0.388f,  -0.633f },
    {   0.337f,  -0.643f }, {   0.285f,  -0.649f }, {   0.273f,  -0.638f },  {   0.318f,  -0.613f },
    {   0.362f,  -0.585f }, {   0.404f,  -0.554f }, {   0.443f,  -0.520f },  {   0.476f,  -0.480f },
    {   0.500f,  -0.434f }, {   0.507f,  -0.383f }, {   0.494f,  -0.333f },  {   0.463f,  -0.292f },
    {   0.422f,  -0.260f }, {   0.376f,  -0.237f }, {   0.326f,  -0.222f },  {   0.275f,  -0.215f },
    {   0.223f,  -0.217f }, {   0.174f,  -0.233f }, {   0.136f,  -0.268f },  {   0.123f,  -0.318f },
    {   0.117f,  -0.369f }, {   0.112f,  -0.421f }, {   0.106f,  -0.473f },  {   0.101f,  -0.524f },
    {   0.096f,  -0.576f }, {   0.090f,  -0.627f }, {   0.075f,  -0.615f },  {   0.058f,  -0.565f },
    {   0.018f,  -0.548f }, {  -0.034f,  -0.548f }, {  -0.063f,  -0.580f },  {  -0.080f,  -0.629f },
    {  -0.093f,  -0.613f }, {  -0.098f,  -0.562f }, {  -0.103f,  -0.510f },  {  -0.108f,  -0.458f },
    {  -0.113f,  -0.407f }, {  -0.119f,  -0.355f }, {  -0.125f,  -0.304f },  {  -0.145f,  -0.257f },
    {  -0.187f,  -0.227f }, {  -0.238f,  -0.216f }, {  -0.289f,  -0.216f },  {  -0.341f,  -0.225f },
    {  -0.389f,  -0.242f }, {  -0.435f,  -0.268f }, {  -0.473f,  -0.302f },  {  -0.500f,  -0.346f },
    {  -0.508f,  -0.397f }, {  -0.495f,  -0.447f }, {  -0.468f,  -0.491f },  {  -0.433f,  -0.530f },
    {  -0.393f,  -0.563f }, {  -0.351f,  -0.593f }, {  -0.307f,  -0.620f },  {  -0.261f,  -0.645f },
    {  -0.301f,  -0.648f },
};
static const D2D1_POINT_2F kBatOutlineNolan[] = {
    {  -1.000f,  -0.359f }, {  -0.974f,  -0.351f }, {  -0.948f,  -0.342f }, {  -0.923f,  -0.331f },
    {  -0.898f,  -0.319f }, {  -0.873f,  -0.306f }, {  -0.850f,  -0.292f }, {  -0.828f,  -0.276f },
    {  -0.807f,  -0.259f }, {  -0.787f,  -0.240f }, {  -0.769f,  -0.219f }, {  -0.752f,  -0.197f },
    {  -0.738f,  -0.173f }, {  -0.726f,  -0.149f }, {  -0.716f,  -0.123f }, {  -0.709f,  -0.097f },
    {  -0.704f,  -0.070f }, {  -0.702f,  -0.042f }, {  -0.702f,  -0.015f }, {  -0.704f,   0.012f },
    {  -0.709f,   0.040f }, {  -0.715f,   0.067f }, {  -0.705f,   0.076f }, {  -0.678f,   0.069f },
    {  -0.652f,   0.063f }, {  -0.625f,   0.058f }, {  -0.598f,   0.053f }, {  -0.570f,   0.049f },
    {  -0.543f,   0.047f }, {  -0.516f,   0.045f }, {  -0.488f,   0.045f }, {  -0.461f,   0.045f },
    {  -0.433f,   0.046f }, {  -0.406f,   0.048f }, {  -0.379f,   0.052f }, {  -0.352f,   0.056f },
    {  -0.325f,   0.062f }, {  -0.298f,   0.069f }, {  -0.272f,   0.077f }, {  -0.246f,   0.086f },
    {  -0.221f,   0.097f }, {  -0.196f,   0.109f }, {  -0.172f,   0.123f }, {  -0.149f,   0.137f },
    {  -0.127f,   0.154f }, {  -0.106f,   0.172f }, {  -0.087f,   0.191f }, {  -0.069f,   0.212f },
    {  -0.052f,   0.234f }, {  -0.038f,   0.257f }, {  -0.025f,   0.281f }, {  -0.014f,   0.307f },
    {  -0.005f,   0.332f }, {   0.002f,   0.359f }, {   0.009f,   0.338f }, {   0.018f,   0.312f },
    {   0.028f,   0.287f }, {   0.040f,   0.262f }, {   0.055f,   0.239f }, {   0.071f,   0.216f },
    {   0.088f,   0.195f }, {   0.108f,   0.176f }, {   0.128f,   0.157f }, {   0.150f,   0.141f },
    {   0.173f,   0.126f }, {   0.196f,   0.112f }, {   0.221f,   0.099f }, {   0.246f,   0.089f },
    {   0.272f,   0.079f }, {   0.298f,   0.070f }, {   0.325f,   0.063f }, {   0.352f,   0.058f },
    {   0.379f,   0.053f }, {   0.406f,   0.049f }, {   0.433f,   0.047f }, {   0.460f,   0.045f },
    {   0.488f,   0.045f }, {   0.516f,   0.045f }, {   0.543f,   0.047f }, {   0.570f,   0.049f },
    {   0.597f,   0.052f }, {   0.625f,   0.057f }, {   0.652f,   0.062f }, {   0.678f,   0.068f },
    {   0.705f,   0.074f }, {   0.721f,   0.072f }, {   0.715f,   0.045f }, {   0.711f,   0.018f },
    {   0.708f,  -0.009f }, {   0.708f,  -0.037f }, {   0.709f,  -0.064f }, {   0.714f,  -0.091f },
    {   0.720f,  -0.118f }, {   0.729f,  -0.144f }, {   0.741f,  -0.168f }, {   0.755f,  -0.192f },
    {   0.771f,  -0.215f }, {   0.788f,  -0.236f }, {   0.808f,  -0.255f }, {   0.829f,  -0.272f },
    {   0.851f,  -0.289f }, {   0.874f,  -0.304f }, {   0.898f,  -0.317f }, {   0.923f,  -0.329f },
    {   0.948f,  -0.340f }, {   0.974f,  -0.349f }, {   1.000f,  -0.357f }, {   0.984f,  -0.359f },
    {   0.956f,  -0.359f }, {   0.929f,  -0.359f }, {   0.902f,  -0.359f }, {   0.874f,  -0.359f },
    {   0.847f,  -0.359f }, {   0.819f,  -0.359f }, {   0.792f,  -0.359f }, {   0.764f,  -0.359f },
    {   0.737f,  -0.359f }, {   0.709f,  -0.359f }, {   0.682f,  -0.359f }, {   0.654f,  -0.359f },
    {   0.627f,  -0.359f }, {   0.599f,  -0.359f }, {   0.572f,  -0.359f }, {   0.545f,  -0.359f },
    {   0.517f,  -0.359f }, {   0.490f,  -0.359f }, {   0.462f,  -0.359f }, {   0.435f,  -0.359f },
    {   0.407f,  -0.359f }, {   0.380f,  -0.359f }, {   0.352f,  -0.359f }, {   0.325f,  -0.359f },
    {   0.312f,  -0.341f }, {   0.305f,  -0.314f }, {   0.295f,  -0.289f }, {   0.282f,  -0.264f },
    {   0.267f,  -0.242f }, {   0.248f,  -0.222f }, {   0.226f,  -0.205f }, {   0.202f,  -0.192f },
    {   0.176f,  -0.183f }, {   0.149f,  -0.176f }, {   0.122f,  -0.173f }, {   0.095f,  -0.171f },
    {   0.080f,  -0.185f }, {   0.077f,  -0.212f }, {   0.074f,  -0.239f }, {   0.071f,  -0.267f },
    {   0.069f,  -0.294f }, {   0.066f,  -0.321f }, {   0.056f,  -0.309f }, {   0.044f,  -0.284f },
    {   0.032f,  -0.260f }, {   0.006f,  -0.265f }, {  -0.021f,  -0.260f }, {  -0.036f,  -0.279f },
    {  -0.048f,  -0.304f }, {  -0.060f,  -0.327f }, {  -0.063f,  -0.300f }, {  -0.065f,  -0.272f },
    {  -0.068f,  -0.245f }, {  -0.071f,  -0.218f }, {  -0.074f,  -0.190f }, {  -0.083f,  -0.171f },
    {  -0.111f,  -0.172f }, {  -0.138f,  -0.175f }, {  -0.165f,  -0.181f }, {  -0.191f,  -0.190f },
    {  -0.216f,  -0.202f }, {  -0.238f,  -0.218f }, {  -0.257f,  -0.237f }, {  -0.274f,  -0.259f },
    {  -0.287f,  -0.283f }, {  -0.297f,  -0.309f }, {  -0.305f,  -0.335f }, {  -0.313f,  -0.359f },
    {  -0.341f,  -0.359f }, {  -0.369f,  -0.359f }, {  -0.396f,  -0.359f }, {  -0.423f,  -0.359f },
    {  -0.451f,  -0.359f }, {  -0.478f,  -0.359f }, {  -0.506f,  -0.359f }, {  -0.533f,  -0.359f },
    {  -0.561f,  -0.359f }, {  -0.588f,  -0.359f }, {  -0.615f,  -0.359f }, {  -0.643f,  -0.359f },
    {  -0.671f,  -0.359f }, {  -0.698f,  -0.359f }, {  -0.725f,  -0.359f }, {  -0.753f,  -0.359f },
    {  -0.780f,  -0.359f }, {  -0.808f,  -0.359f }, {  -0.835f,  -0.359f }, {  -0.863f,  -0.359f },
    {  -0.890f,  -0.359f }, {  -0.918f,  -0.359f }, {  -0.945f,  -0.359f }, {  -0.972f,  -0.359f },
    {  -1.000f,  -0.359f },
};

static ComPtr<ID2D1Bitmap1> BakeBatSignal(int styleIdx, float blurDev) {
    if (!g_dc || !g_d2dFactory) { Wh_Log(L"[BAT] bake fail: no dc/factory"); return nullptr; }

    // Style -> silhouette table
    const D2D1_POINT_2F* pts = kBatOutline;
    size_t count = ARRAYSIZE(kBatOutline);
    if (styleIdx == 1)      { pts = kBatOutlineOval;  count = ARRAYSIZE(kBatOutlineOval);  }
    else if (styleIdx == 2) { pts = kBatOutlineNolan; count = ARRAYSIZE(kBatOutlineNolan); }

    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(g_d2dFactory->CreatePathGeometry(&geo))) { Wh_Log(L"[BAT] bake fail: CreatePathGeometry"); return nullptr; }

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) { Wh_Log(L"[BAT] bake fail: sink Open"); return nullptr; }

    constexpr float SX = 512.0f, SY = 256.0f;
    // Inset the geometry by 3x the blur radius so the gaussian's spread has
    // room inside the bitmap instead of getting clipped at the edges.
    const float pad = (std::max)(2.0f, blurDev * 3.0f);
    const float iw = SX - 2.0f * pad;
    const float ih = SY - 2.0f * pad;
    // Per-style vertical nudge (normalized units, + = lower on screen)
    static const float kBatNudgeY[3] = { 0.0f, 0.0f, 0.12f };
    const float nudgeY = kBatNudgeY[(std::min)((std::max)(styleIdx, 0), 2)];
    // Per-style logo scale: the 🦇🦇🦇 logo is naturally half as tall as the
    // others (wide, short design) — +20% height so it doesn't read as weak.
    static const float kBatScaleY[3] = { 1.0f, 1.0f, 1.2f };
    const float scaleY = kBatScaleY[(std::min)((std::max)(styleIdx, 0), 2)];
    auto toBmp = [&](const D2D1_POINT_2F& p) -> D2D1_POINT_2F {
        return D2D1::Point2F(pad + (p.x + 1.0f) * 0.5f * iw,
                             pad + (p.y * scaleY + 0.7f + nudgeY) / 1.4f * ih);
    };

    sink->BeginFigure(toBmp(pts[0]), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < count; ++i) {
        sink->AddLine(toBmp(pts[i]));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) { Wh_Log(L"[BAT] bake fail: sink Close"); return nullptr; }

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        // TARGET is REQUIRED for SetTarget() to accept this bitmap; without it
        // the switch silently fails and the bake draws into the void.
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    // Pass 1: sharp silhouette into a scratch target
    ComPtr<ID2D1Bitmap1> scratch;
    if (FAILED(g_dc->CreateBitmap(D2D1::SizeU(512, 256), nullptr, 0, &props, &scratch))) { Wh_Log(L"[BAT] bake fail: CreateBitmap"); return nullptr; }

    ComPtr<ID2D1SolidColorBrush> black;
    if (FAILED(g_dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &black))) { Wh_Log(L"[BAT] bake fail: brush"); return nullptr; }

    ID2D1Image* oldTarget = nullptr;
    g_dc->GetTarget(&oldTarget);
    // Self-contained bake: own BeginDraw/EndDraw session, so the caller can
    // invoke this BEFORE its own frame. Drawing outside a session is a silent
    // no-op — that's exactly what blanked the signal when this bake ran
    // without a session of its own. SetTarget switches inside the session are
    // legal, and no nested BeginDraw happens either way.
    g_dc->SetTarget(scratch.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_dc->FillGeometry(geo.Get(), black.Get(), nullptr);

    // Pass 2: soften into the final bitmap ("projected light" edge). Blur is
    // half of the user's spotlight edge blur so it tracks their taste.
    ComPtr<ID2D1Bitmap1> bmp;
    if (SUCCEEDED(g_dc->CreateBitmap(D2D1::SizeU(512, 256), nullptr, 0, &props, &bmp))) {
        ComPtr<ID2D1Effect> blur;
        // blurDev < 0.5 skips the gaussian entirely: the "None" logo blur level
        // must be truly crisp, not minimally soft.
        bool useBlur = blurDev >= 0.5f && SUCCEEDED(g_dc->CreateEffect(kCLSID_D2D1GaussianBlur, &blur));
        if (useBlur) {
            blur->SetInput(0, scratch.Get());
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, (std::max)(0.5f, blurDev));
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
            g_dc->SetTarget(bmp.Get());
            g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            g_dc->DrawImage(blur.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
        } else {
            g_dc->SetTarget(bmp.Get());
            g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            g_dc->DrawImage(scratch.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
        }
    }

    g_dc->Flush(); // ensure both passes land before anything samples this bitmap
    CheckEndDraw(g_dc->EndDraw(), L"BatBake");
    g_dc->SetTarget(oldTarget);
    if (oldTarget) oldTarget->Release(); // GetTarget AddRef'd it
    Wh_Log(L"[BAT] baked OK style=%d blur=%.1f", styleIdx, blurDev);

    // If the final bitmap couldn't be created, fall back to the sharp
    // silhouette from pass 1 rather than nothing.
    return bmp ? bmp : scratch;
}

void RenderOverlay() {
    if (!g_dc || !g_targetBitmap || !g_intermediateTarget) return;
    UINT width  = g_overlayWidth;
    UINT height = g_overlayHeight;
    if (width == 0 || height == 0) return;

    // Bake the bat signal BEFORE BeginDraw: BakeBatSignal switches the D2D
    // target and flushes internally — legal mid-session, but fragile, and
    // there's no reason to do it inside our own draw pass.
    if (g_settings.batSignalEnabled && g_settings.tintStrength > 0) {
        // batLogoBlur: -1 = Auto (follow the spotlight edge blur), else an
        // edge-blur equivalent chosen by the user — the 1/6 logo link is kept.
        float effectiveEdgeBlur = (g_settings.batLogoBlur >= 0)
                                      ? (float)g_settings.batLogoBlur
                                      : (float)g_settings.spotlightBlur;
        float batBlurDev = effectiveEdgeBlur * (1.0f / 6.0f) * g_dpiScale;
        uint32_t batKey = (uint32_t)g_settings.batStyle |
                          ((uint32_t)(int)(batBlurDev * 4.0f) << 8);
        if (!g_batSignalBitmap || g_batBakeKey != batKey) {
            g_batSignalBitmap = BakeBatSignal(g_settings.batStyle, batBlurDev);
            g_batBakeKey = batKey;
        }
    }

    // 1. COMPOSITE INTO FP16 INTERMEDIATE TARGET
    g_dc->SetTarget(g_intermediateTarget.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    float idleAlpha = (g_settings.idleOpacity / 255.0f) * g_masterFade;

    // PREPARE LAYER: Apply idleOpacity uniformly
    D2D1_LAYER_PARAMETERS layerParams;
    layerParams.contentBounds = D2D1::InfiniteRect();
    layerParams.geometricMask = nullptr;
    layerParams.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    layerParams.maskTransform = D2D1::Matrix3x2F::Identity();
    layerParams.opacity = idleAlpha;
    layerParams.opacityBrush = nullptr;
    layerParams.layerOptions = D2D1_LAYER_OPTIONS_NONE;

    // --- CLIP TO ICONS (Shared Mask) ---
    if (g_settings.overlayClipToIcons && !g_desktopIconsHidden.load(std::memory_order_relaxed) &&
        g_sharedMaskReady.load(std::memory_order_acquire)) {
        int readIdx = g_activeSharedMaskIdx.load(std::memory_order_acquire);
        if (g_sharedMask[readIdx]) {
            if (!g_overlayClipBrush) {
                D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(
                    D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                g_dc->CreateBitmapBrush(g_sharedMask[readIdx].Get(), brushProps, &g_overlayClipBrush);
            } else {
                g_overlayClipBrush->SetBitmap(g_sharedMask[readIdx].Get());
            }
            layerParams.opacityBrush = g_overlayClipBrush.Get();
        }
    }

    g_dc->PushLayer(&layerParams, nullptr);

    g_dc->Clear(g_desktopBgColor);

    if (g_hasWallpaper && g_wallpaperBitmap) {
        if (g_wallpaperPosition == DWPOS_TILE && g_tileBrush) {
            g_dc->FillRectangle(D2D1::RectF(0, 0, (float)width, (float)height), g_tileBrush.Get());
        } else if (g_wallpaperPosition == DWPOS_SPAN && g_overlayWnd) {
            float virtW = (float)GetSystemMetrics(SM_CXVIRTUALSCREEN);
            float virtH = (float)GetSystemMetrics(SM_CYVIRTUALSCREEN);
            POINT pt = {0, 0};
            ClientToScreen(g_overlayWnd, &pt);
            float offsetX = (float)(pt.x - GetSystemMetrics(SM_XVIRTUALSCREEN));
            float offsetY = (float)(pt.y - GetSystemMetrics(SM_YVIRTUALSCREEN));
            // [MULTIMON v2.9.4] GetPixelSize (physical px), not GetSize()
            // (DIPs — bitmap-dpi-dependent, inflated 1.33x for 72-dpi jpgs);
            // the per-monitor tile path below already used pixel dims.
            D2D1_SIZE_U spanPx = g_wallpaperBitmap->GetPixelSize();
            D2D1_SIZE_F imgSize = D2D1::SizeF((float)spanPx.width, (float)spanPx.height);
            // [MULTIMON v2.9.12] SPAN SOLVED by Doc's landmark experiment
            // (three images, exact-union/4K/square): Shell cover-fits the
            // union (aspect preserved) but crops with the /3 QUIRK — the
            // same Shell /3 divisor our FILL path already carries: top gets
            // 1/3 of the vertical overflow, bottom 2/3. Doc's measured
            // deltas (102px @2767.5 drawH, 460px @4920 drawH, 0 @exact)
            // match -overflow/3 three for three, the square case exactly.
            // Center-crop hypotheses were all wrong; the quirk generalizes
            // across wallpaper positions. Horizontal overflow: same /3
            // treatment (measured cases were width-driven, so this axis is
            // inference — probe + Doc's eyes will verdict it).
            float scale = std::max(virtW / imgSize.width, virtH / imgSize.height);
            float drawW = imgSize.width * scale;
            float drawH = imgSize.height * scale;
            float left = -((drawW - virtW) / 3.0f);
            float top  = -((drawH - virtH) / 3.0f);
            D2D1_RECT_F targetRect = D2D1::RectF(left - offsetX, top - offsetY, left - offsetX + drawW, top - offsetY + drawH);
            // [MULTIMON v2.9.8] SPAN probe (one-shot per wallpaper draw session):
            // every input of the anchor math. Doc field case: replica sits
            // HIGHER than Shell's span, same scale — anchor is the variable.
            // 458px delta = our top-crop misanchored; 116px = monitor-2 offset
            // leak; something else = DPI-scaled virtual-screen metrics.
            {
                static ULONGLONG s_lastSpanProbe = 0;
                ULONGLONG now = GetTickCount64();
                if (now - s_lastSpanProbe > 15000) {
                    s_lastSpanProbe = now;
                    UINT sysDpi = GetDpiForWindow(g_overlayWnd);
                    DiagLog(L"[SPAN] virt=%gx%g smXY=(%ld,%ld) overlayScreen=(%ld,%ld) off=(%g,%g) "
                            L"img=%ux%u draw=%gx%g left/top=(%g,%g) overlayDpi=%u rect=[%g %g %g %g]",
                            virtW, virtH,
                            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                            pt.x, pt.y, offsetX, offsetY,
                            (unsigned)spanPx.width, (unsigned)spanPx.height,
                            drawW, drawH, left, top, sysDpi,
                            targetRect.left, targetRect.top, targetRect.right, targetRect.bottom);
                }
            }
            g_dc->DrawBitmap(g_wallpaperBitmap.Get(), targetRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            D2D1_SIZE_U pixelSize = g_wallpaperBitmap->GetPixelSize();
            D2D1_SIZE_F imgSize = D2D1::SizeF((float)pixelSize.width, (float)pixelSize.height);
            float dpiX = 96.0f, dpiY = 96.0f;
            g_wallpaperBitmap->GetDpi(&dpiX, &dpiY);
            // [MULTIMON v2.9.4] One tile PER MONITOR, each fit/cropped against
            // its own dimensions exactly like the desktop renders it. The old
            // single fit against the whole union rect matched NEITHER screen
            // on mixed-DPI/aspect setups (measured: 125% landscape primary +
            // 100% portrait secondary — both wrong).
            if (!g_monitorTiles.empty()) {
                for (const auto& tile : g_monitorTiles) {
                    float tw = (float)(tile.rect.right - tile.rect.left);
                    float th = (float)(tile.rect.bottom - tile.rect.top);
                    if (tw <= 0.0f || th <= 0.0f) continue;
                    g_dc->PushAxisAlignedClip(
                        D2D1::RectF((float)tile.rect.left, (float)tile.rect.top,
                                    (float)tile.rect.right, (float)tile.rect.bottom),
                        D2D1_ANTIALIAS_MODE_ALIASED);
                    D2D1_RECT_F sourceRect, targetRect;
                    ComputeWallpaperRects(imgSize, D2D1::SizeF(tw, th), g_wallpaperPosition, sourceRect, targetRect);
                    D2D1_RECT_F sourceRectDips = D2D1::RectF(
                        sourceRect.left   * (96.0f / dpiX),
                        sourceRect.top    * (96.0f / dpiY),
                        sourceRect.right  * (96.0f / dpiX),
                        sourceRect.bottom * (96.0f / dpiY)
                    );
                    targetRect.left   += (float)tile.rect.left;
                    targetRect.top    += (float)tile.rect.top;
                    targetRect.right  += (float)tile.rect.left;
                    targetRect.bottom += (float)tile.rect.top;
                    g_dc->DrawBitmap(g_wallpaperBitmap.Get(), targetRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, sourceRectDips);
                    g_dc->PopAxisAlignedClip();
                }
            } else {
                D2D1_SIZE_F refSize = D2D1::SizeF((float)width, (float)height);
                D2D1_RECT_F sourceRect, targetRect;
                ComputeWallpaperRects(imgSize, refSize, g_wallpaperPosition, sourceRect, targetRect);
                D2D1_RECT_F sourceRectDips = D2D1::RectF(
                    sourceRect.left   * (96.0f / dpiX),
                    sourceRect.top    * (96.0f / dpiY),
                    sourceRect.right  * (96.0f / dpiX),
                    sourceRect.bottom * (96.0f / dpiY)
                );
                g_dc->DrawBitmap(g_wallpaperBitmap.Get(), targetRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, sourceRectDips);
            }
        }
    }

    g_dc->PopLayer();

    // [TINT-SPLIT v2.9.26] The tint veil and bat ink live on the tint surface
    // (pass 3 below). This surface carries only the wallpaper replica
    // (opaque at the mask glyphs) and the punches that erase it.

    // SHOW: Apply spotlight and selection masks (destination out)
    // The Bat-Signal keeps this section alive even when the overlay is toggled
    // off, so it can punch through the persistent tint veil.
    bool batActive = g_settings.batSignalEnabled;
    bool isBypassed = g_overlayBypassed.load(std::memory_order_relaxed);
    if (g_masterFade > 0.001f || (batActive && isBypassed)) {

        // Update Noise Transform Matrix ONCE per frame so both spotlight and selection share the animated field
        D2D1_MATRIX_3X2_F mat = D2D1::Matrix3x2F::Identity();
        mat = mat * D2D1::Matrix3x2F::Rotation((float)g_noiseRotation * 90.0f, D2D1::Point2F(32.0f, 32.0f));
        mat = mat * D2D1::Matrix3x2F::Translation(g_noiseOffsetX, g_noiseOffsetY);
        if (g_noiseTransformEffect) {
            g_noiseTransformEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, mat);
        }

        // While the overlay is toggled off with the signal active, the spotlight
        // punch burns at full strength so the tint keeps its lit circle.
        float spotFade = g_spotlightFade.load(std::memory_order_relaxed);
        if (batActive && isBypassed) spotFade = 1.0f;

        if (spotFade > 0.001f && g_bakedSpotlight && g_spotlightStampEffect) {
            // The graph is already wired in BakeSpotlight(). Just update the fade and draw!
            g_spotlightStampEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, CreateFadeMatrix(spotFade));
            float destX = std::round(g_smoothedMousePos.x - g_bakedSpotlightHalfW);
            float destY = std::round(g_smoothedMousePos.y - g_bakedSpotlightHalfH);
            D2D1_POINT_2F offset = D2D1::Point2F(destX, destY);

            g_dc->DrawImage(g_spotlightStampEffect.Get(), &offset, nullptr,
                            D2D1_INTERPOLATION_MODE_LINEAR,
                            D2D1_COMPOSITE_MODE_DESTINATION_OUT);
        }

        if (g_selectionFade > 0.001f && !g_cachedSelectedRects.empty()) {
            if (g_selectionStampEffect && g_selNoiseComposite) {

                float activeAlpha = (g_settings.selectionOpacity / 255.0f) * g_selectionFade;
                g_selectionStampEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, CreateFadeMatrix(activeAlpha));

                float pad = (float)g_settings.selectionPadding * g_dpiScale;
                float blurVal = ComputeSelectionBlurVal(pad);
                float blurPadding = blurVal * 3.0f;

                float strength = 1.0f;
                D2D1_VECTOR_4F selAddCoeffs = {0.0f, 1.0f, strength, 0.0f};

                for (const auto& rcBounds : g_cachedSelectedRects) {
                    UINT targetW = (UINT)(rcBounds.right - rcBounds.left) + (UINT)(g_settings.selectionPadding * g_dpiScale * 2);
                    UINT targetH = (UINT)(rcBounds.bottom - rcBounds.top) + (UINT)(g_settings.selectionPadding * g_dpiScale * 2);
                    uint64_t key = ((uint64_t)targetW << 32) | targetH;

                    ComPtr<ID2D1Bitmap1> bakedGlow;
                    auto it = g_bakedSelectionGlows.find(key);
                    if (it != g_bakedSelectionGlows.end() && it->second) {
                        bakedGlow = it->second;
                        g_bakedSelectionGlowUse[key] = GetTickCount64();
                    } else {
                        bakedGlow = BakeSelectionGlow(targetW, targetH);
                        if (bakedGlow) {
                            g_bakedSelectionGlows[key] = bakedGlow;
                            g_bakedSelectionGlowUse[key] = GetTickCount64();
                        }
                    }

                    if (!bakedGlow) continue;

                    // Rewire unconditionally every frame (same convention as
                    // BakeSpotlight). The effects are destroyed and recreated by
                    // ReleaseSwapChainResources/CreateSwapChainResources on device
                    // loss and WorkerW rebuilds, so a "wired once" flag would leave
                    // the graph silently broken after any recreation.
                    WireSensitivityMaskGraph(
                        bakedGlow.Get(),
                        g_selInvertEffect.Get(),
                        g_selMaskMultiplyEffect.Get(),
                        g_selNoiseMultiplyEffect.Get(),
                        g_selNoiseComposite.Get(),
                        g_selectionStampEffect.Get(),
                        g_noiseTransformEffect.Get(),
                        selAddCoeffs
                    );

                    float destX = std::round((float)rcBounds.left - pad - blurPadding);
                    float destY = std::round((float)rcBounds.top - pad - blurPadding);
                    D2D1_POINT_2F offset = D2D1::Point2F(destX, destY);

                    g_dc->DrawImage(g_selectionStampEffect.Get(), &offset, nullptr,
                                    D2D1_INTERPOLATION_MODE_LINEAR,
                                    D2D1_COMPOSITE_MODE_DESTINATION_OUT);

                }
            }
        }
    }


    CheckEndDraw(g_dc->EndDraw(), L"RenderOverlay.pass1");

    // 2. BLIT FP16 RESULT TO 8-BIT SWAPCHAIN
    g_dc->SetTarget(g_targetBitmap.Get());
    g_dc->BeginDraw();
    g_dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    // DrawImage handles FP16 -> 8-bit format conversion much more reliably than DrawBitmap
    g_dc->DrawImage(g_intermediateTarget.Get(),
                    D2D1::Point2F(0, 0),
                    D2D1::RectF(0, 0, (float)width, (float)height),
                    D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                    D2D1_COMPOSITE_MODE_SOURCE_OVER);
    CheckEndDraw(g_dc->EndDraw(), L"RenderOverlay.pass2");

    // [TINT-SPLIT v2.9.27] Present the replica surface HERE, immediately
    // before the tint surface presents at the end of pass 3 — back-to-back
    // presents in one call land in the same compositor batch. Previously the
    // replica presented back in RenderLoopInner, microseconds AFTER the tint
    // surface had already presented; mid-fade that skew showed as one
    // surface's quantization step landing without the other's (Doc's
    // spotlight fade-out blink, gray-on-gray maximizing it — the same
    // visibility profile as the old 256-level bug).
    if (g_swapChain) {
        HRESULT hrA = g_swapChain->Present(0, 0);
        if (hrA == DXGI_ERROR_DEVICE_REMOVED || hrA == DXGI_ERROR_DEVICE_RESET) {
            if (g_messageWnd) PostMessage(g_messageWnd, WM_APP_DEVICE_LOST, 0, 0);
        }
    }

    // 3. [TINT-SPLIT v2.9.26] TINT SURFACE — veil + bat ink + the same punches,
    // on the TOP composition visual. DWM blends this over the replica in its
    // own composition pass and color space — the same blend it applies to the
    // real desktop everywhere else. One blend owner: the whole point.
    if (g_tintDc && g_tintTargetBitmap) {
        g_tintDc->SetTarget(g_tintTargetBitmap.Get());
        g_tintDc->BeginDraw();
        g_tintDc->Clear(D2D1::ColorF(0, 0, 0, 0));

        // --- "OUT OF THE DARKNESS" TINT VEIL --- (moved verbatim from pass 1)
        if ((g_settings.tintEnabled || g_settings.batSignalEnabled) && g_settings.tintStrength > 0) {
            if (!g_tintBrush) {
                g_tintDc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &g_tintBrush);
            }
            if (g_tintBrush) {
                D2D1_COLOR_F c = g_settings.tintColor;
                // Follow the overlay's toggle fade for symmetric fade-in/out...
                float tintFade = g_masterFade;
                // ...unless the Bat-Signal is on: the signal needs its darkness
                // even while the overlay is toggled off.
                if (g_settings.batSignalEnabled) tintFade = 1.0f;
                c.a = (g_settings.tintStrength / 100.0f) * tintFade;
                g_tintBrush->SetColor(c);
                g_tintDc->FillRectangle(D2D1::RectF(0.0f, 0.0f, (float)width, (float)height), g_tintBrush.Get());
            }
        }

        bool batActive2 = g_settings.batSignalEnabled;
        bool isBypassed2 = g_overlayBypassed.load(std::memory_order_relaxed);
        if (g_masterFade > 0.001f || (batActive2 && isBypassed2)) {

            // Noise transform: same field as pass 1 (shared effect object).
            D2D1_MATRIX_3X2_F mat2 = D2D1::Matrix3x2F::Identity();
            mat2 = mat2 * D2D1::Matrix3x2F::Rotation((float)g_noiseRotation * 90.0f, D2D1::Point2F(32.0f, 32.0f));
            mat2 = mat2 * D2D1::Matrix3x2F::Translation(g_noiseOffsetX, g_noiseOffsetY);
            if (g_noiseTransformEffect) {
                g_noiseTransformEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, mat2);
            }

            float spotFade2 = g_spotlightFade.load(std::memory_order_relaxed);
            if (batActive2 && isBypassed2) spotFade2 = 1.0f;

            if (spotFade2 > 0.001f && g_bakedSpotlight && g_spotlightStampEffect) {
                g_spotlightStampEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, CreateFadeMatrix(spotFade2));
                float destX2 = std::round(g_smoothedMousePos.x - g_bakedSpotlightHalfW);
                float destY2 = std::round(g_smoothedMousePos.y - g_bakedSpotlightHalfH);
                D2D1_POINT_2F offset2 = D2D1::Point2F(destX2, destY2);

                g_tintDc->DrawImage(g_spotlightStampEffect.Get(), &offset2, nullptr,
                                    D2D1_INTERPOLATION_MODE_LINEAR,
                                    D2D1_COMPOSITE_MODE_DESTINATION_OUT);
            }

            // 🦇 PROJECT THE SIGNAL (bat ink is part of the darkness system,
            // not a punch — lives on the tint surface above the veil).
            float batFade2 = isBypassed2 ? 1.0f : g_spotlightFade.load(std::memory_order_relaxed);
            if (batActive2 && (g_spotlightFade.load(std::memory_order_relaxed) > 0.001f || isBypassed2)) {
                if (g_batSignalBitmap) {
                    float batR = (float)g_settings.spotlightRadius * g_dpiScale;
                    float batW = batR * 1.7f;
                    float batH = batW * 0.5f;
                    float bmx = g_smoothedMousePos.x;
                    float bmy = g_smoothedMousePos.y;
                    D2D1_RECT_F batDst = D2D1::RectF(bmx - batW * 0.5f, bmy - batH * 0.5f,
                                                     bmx + batW * 0.5f, bmy + batH * 0.5f);
                    float batOpacity = (g_settings.tintStrength / 100.0f) * batFade2;
                    static int s_batDrawLogCounter2 = 0;
                    if ((s_batDrawLogCounter2++ % 120) == 0) {
                        Wh_Log(L"[BAT] draw: bmp=%p opacity=%.2f w=%.0f h=%.0f at (%.0f,%.0f)",
                               (void*)g_batSignalBitmap.Get(), batOpacity, batW, batH, bmx, bmy);
                    }
                    if (batOpacity > 0.001f) {
                        g_tintDc->DrawBitmap(g_batSignalBitmap.Get(), &batDst, batOpacity,
                                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    }
                }
            }

            if (g_selectionFade > 0.001f && !g_cachedSelectedRects.empty()) {
                if (g_selectionStampEffect && g_selNoiseComposite) {

                    float activeAlpha = (g_settings.selectionOpacity / 255.0f) * g_selectionFade;
                    g_selectionStampEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, CreateFadeMatrix(activeAlpha));

                    float pad = (float)g_settings.selectionPadding * g_dpiScale;
                    float blurVal = ComputeSelectionBlurVal(pad);
                    float blurPadding = blurVal * 3.0f;

                    float strength = 1.0f;
                    D2D1_VECTOR_4F selAddCoeffs = {0.0f, 1.0f, strength, 0.0f};

                    for (const auto& rcBounds : g_cachedSelectedRects) {
                        UINT targetW = (UINT)(rcBounds.right - rcBounds.left) + (UINT)(g_settings.selectionPadding * g_dpiScale * 2);
                        UINT targetH = (UINT)(rcBounds.bottom - rcBounds.top) + (UINT)(g_settings.selectionPadding * g_dpiScale * 2);
                        uint64_t key = ((uint64_t)targetW << 32) | targetH;

                        ComPtr<ID2D1Bitmap1> bakedGlow;
                        auto it = g_bakedSelectionGlows.find(key);
                        if (it != g_bakedSelectionGlows.end() && it->second) {
                            bakedGlow = it->second;
                            g_bakedSelectionGlowUse[key] = GetTickCount64();
                        } else {
                            bakedGlow = BakeSelectionGlow(targetW, targetH);
                            if (bakedGlow) {
                                g_bakedSelectionGlows[key] = bakedGlow;
                                g_bakedSelectionGlowUse[key] = GetTickCount64();
                            }
                        }

                        if (!bakedGlow) continue;

                        WireSensitivityMaskGraph(
                            bakedGlow.Get(),
                            g_selInvertEffect.Get(),
                            g_selMaskMultiplyEffect.Get(),
                            g_selNoiseMultiplyEffect.Get(),
                            g_selNoiseComposite.Get(),
                            g_selectionStampEffect.Get(),
                            g_noiseTransformEffect.Get(),
                            selAddCoeffs
                        );

                        float destX = std::round((float)rcBounds.left - pad - blurPadding);
                        float destY = std::round((float)rcBounds.top - pad - blurPadding);
                        D2D1_POINT_2F offset = D2D1::Point2F(destX, destY);

                        g_tintDc->DrawImage(g_selectionStampEffect.Get(), &offset, nullptr,
                                            D2D1_INTERPOLATION_MODE_LINEAR,
                                            D2D1_COMPOSITE_MODE_DESTINATION_OUT);

                    }
                }
            }
        }

        CheckEndDraw(g_tintDc->EndDraw(), L"RenderOverlay.tint");
        if (g_tintSwapChain) {
            HRESULT hrTint = g_tintSwapChain->Present(0, 0);
            if (hrTint == DXGI_ERROR_DEVICE_REMOVED || hrTint == DXGI_ERROR_DEVICE_RESET) {
                if (g_messageWnd) PostMessage(g_messageWnd, WM_APP_DEVICE_LOST, 0, 0);
            }
        }
    }
}

// --- CRASH GUARDS -----------------------------------------------------------
// The Event Viewer verdict for the boot-time explorer crashes was
// 'libc++.whl' + exception 0x40000015 = an uncaught C++ exception reached
// std::terminate -> abort. These wrappers make that structurally impossible
// for this mod: every thread proc / window proc entry point catches,
// logs, and either self-heals (render loops trigger a device-lost recreate)
// or degrades gracefully instead of killing explorer.

void RenderLoopInner();
void RenderLoop() {
    try {
        RenderLoopInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] RenderLoop exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] RenderLoop unknown exception");
    }
    // The render thread is dead — force the recreate path to rebuild it.
    if (g_messageWnd && !g_unloading) PostMessageW(g_messageWnd, WM_APP_DEVICE_LOST, 0, 0);
}

void RenderLoopInner() {
    BootLog(L"[BOOT] RenderLoop started (first frame imminent)");
    LARGE_INTEGER freq, lastTime, currentTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    float s_lastRenderedSpotFade  = -1.0f;
    float s_lastRenderedSelFade   = -1.0f;
    float s_lastRenderedMasterFade = 1.0f;

    D2D1_POINT_2F s_lastRenderedMousePos = {-1.0f, -1.0f};
    std::vector<RECT> s_lastRenderedSelectedRects; // Track geometry changes

    // Initialize framerate cap timer fresh for this specific thread lifetime
    auto nextOverlayFrameTime = std::chrono::steady_clock::now();

    while (!g_threadStop) {
        if (!g_isDisplayOn.load(std::memory_order_relaxed)) {
            WaitForSingleObject(g_renderEvent, INFINITE);
            QueryPerformanceCounter(&lastTime);
            continue;
        }

        QueryPerformanceCounter(&currentTime);
        float deltaTime = (float)(currentTime.QuadPart - lastTime.QuadPart) / freq.QuadPart;
        lastTime = currentTime;

        if (deltaTime > 0.1f) deltaTime = 0.016f;

        bool frameRendered = false;
        ComPtr<IDXGISwapChain1> currentSwapChain;
        bool isFullyIdle = false;
        int targetFps = 60;

        {
            std::lock_guard<std::mutex> lock(g_renderMutex);
            targetFps = g_settings.targetFps;
            if (g_overlayWnd && !g_unloading) {
                UpdateMouseAndAnimations(deltaTime);

                // Check if the actual selection rectangles changed
                bool selectionChanged =
                    g_cachedSelectedRects.size() != s_lastRenderedSelectedRects.size() ||
                    (!g_cachedSelectedRects.empty() &&
                     memcmp(g_cachedSelectedRects.data(), s_lastRenderedSelectedRects.data(),
                            g_cachedSelectedRects.size() * sizeof(RECT)) != 0);

                bool mouseMovedSignificantly =
                    (std::abs(g_smoothedMousePos.x - s_lastRenderedMousePos.x) > 0.1f ||
                    (std::abs(g_smoothedMousePos.y - s_lastRenderedMousePos.y) > 0.1f));


                // 🦇 While the signal is active the overlay stays alive even when
                // the overlay itself is toggled off (tint + bat keep rendering).
                bool batActive = g_settings.batSignalEnabled;

                bool stateChanged =
                    selectionChanged ||
                    (std::abs(g_masterFade - s_lastRenderedMasterFade) > 0.001f) ||
                    (std::abs(g_spotlightFade.load(std::memory_order_relaxed) - s_lastRenderedSpotFade) > 0.001f) ||
                    (std::abs(g_selectionFade - s_lastRenderedSelFade) > 0.001f) ||
                    (mouseMovedSignificantly && g_spotlightFade > 0.001f) ||
                    (batActive && mouseMovedSignificantly) ||
                    g_forceRender;

                bool isBypassed = g_overlayBypassed.load(std::memory_order_relaxed);
                    float targetMaster = isBypassed ? 0.0f : 1.0f;
                    float idleAlpha = (g_settings.idleOpacity / 255.0f) * g_masterFade;

                // Determine if we actually need to draw pixels to the screen
                // If bypassed, completely shut down the overlay renders to prevent GPU spikes (unless Bat-Signal keeps the tint active)!
                bool needsOverlayDraw = ((g_spotlightFade > 0.001f && (!isBypassed || batActive)) ||
                                        (g_selectionFade > 0.001f && (!isBypassed || batActive)) ||
                                        idleAlpha > 0.001f ||
                                        g_settings.overlayClipToIcons ||
                                        (batActive && mouseMovedSignificantly) ||
                                        std::abs(g_masterFade - targetMaster) > 0.001f);

                bool shadowFading = (g_shadowFade > 0.001f && g_shadowFade < 0.999f);

                isFullyIdle = (g_spotlightFade <= 0.001f &&
                            g_selectionFade <= 0.001f &&
                            g_isIdle &&
                            std::abs(g_masterFade - targetMaster) <= 0.001f &&
                            !shadowFading &&
                            !batActive);

                if (g_swapChain && g_targetBitmap) {

                    // Clear the force flag unconditionally — it was set to force
                    // this exact frame to evaluate. If stateChanged is false,
                    // we still consumed the signal, so clear it to avoid spinning.
                    g_forceRender = false;

                    // ONLY render and Present if we actually have visible elements
                    if (stateChanged && needsOverlayDraw) {
                        RenderOverlay();

                        s_lastRenderedMasterFade = g_masterFade;
                        s_lastRenderedSpotFade   = g_spotlightFade.load(std::memory_order_relaxed);
                        s_lastRenderedSelFade   = g_selectionFade;
                        s_lastRenderedSelectedRects = g_cachedSelectedRects;

                        // ALWAYS track the mouse position so we don't get stuck
                        s_lastRenderedMousePos = g_smoothedMousePos;

                        currentSwapChain        = g_swapChain;
                        frameRendered           = true;
                    }

                    // ALWAYS track the mouse position so we don't get stuck
                    s_lastRenderedMousePos = g_smoothedMousePos;

                    // If the mouse moved, or shadows are fading, we MUST signal the underlay
                    if (mouseMovedSignificantly || (stateChanged && needsOverlayDraw) || shadowFading) {
                        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                    }
                }
            }
        }

        if (frameRendered && currentSwapChain) {
            // [TINT-SPLIT v2.9.27] The replica swapchain now presents inside
            // RenderOverlay, back-to-back with the tint surface (see pass 2).
            // This block keeps only the framerate cap.

            // PROPER FRAMERATE CAP FOR OVERLAY
            // Prevents the thread from spinning at 1000fps and choking the GPU when Present(0,0) is used.
            // Using sleep_until avoids the Windows 15.6ms Sleep() timer resolution trap and arithmetic drift.
            CapFramerate(nextOverlayFrameTime, targetFps);

        } else {
            DWORD sleepTime = isFullyIdle ? INFINITE : 10;
            DWORD waitRes = WaitForSingleObject(g_renderEvent, sleepTime);
            // FIX: If we were in a deep sleep and just woke up, reset the clock
            // so we don't inject a 100ms delta-time spike into the fade animations!
            if (sleepTime == INFINITE && waitRes == WAIT_OBJECT_0) {
                QueryPerformanceCounter(&lastTime);
            }
        }
    }
}
// RAII helper: destroys an owned HICON when it leaves scope. Icons obtained
// from shared resources (ImageList) are assigned with owned=false.
struct UniqueHICON {
    HICON h = nullptr;
    bool owned = false;
    UniqueHICON() = default;
    ~UniqueHICON() { reset(); }
    UniqueHICON(const UniqueHICON&) = delete;
    UniqueHICON& operator=(const UniqueHICON&) = delete;
    void assign(HICON icon, bool isOwned) { reset(); h = icon; owned = isOwned; }
    void reset() { if (owned && h) DestroyIcon(h); h = nullptr; owned = false; }
    HICON get() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

// Shared media-extension tables. Image extensions only matter for thumbnail
// aspect probing; containers (video/audio) also gate the animated-WebP check.
static const wchar_t* const kImageExts[] = {
    L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".webp"
};
static const wchar_t* const kMediaContainerExts[] = {
    L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".flv",
    L".webm", L".m4v", L".mpg", L".mpeg", L".3gp", L".ts",
    L".flac", L".mp3", L".m4a", L".aac", L".ogg", L".wma", L".opus", L".alac", L".aiff", L".wav"
};

bool CacheIconBitmap(HWND hLV, int imageIndex, uint64_t cacheKey, bool isFolder, bool isThumbnail, bool hasCustomIcon, bool isFolderWithSubfolders, const std::wstring& filePath, std::unordered_map<int, uint64_t>& passIndexMap, bool forceReextract = false, bool* outIsFallback = nullptr, const std::wstring& customIconPath = L"", int customIconIndex = 0) {
    if (outIsFallback) *outIsFallback = false;

    // Stock-placeholder check FIRST - before the cache lookup. Items whose
    // placeholder pixels were cached under an unchanged cacheKey must still be
    // flagged here, otherwise the early return below hides them from the
    // retry ladder forever.
    if (outIsFallback) {
        int genericDocIdx = GetGenericDocSysIconIndex();
        if (genericDocIdx >= 0 && imageIndex == genericDocIdx) {
            *outIsFallback = true;
        }
    }

    // Check if we already have it
    if (!forceReextract) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (g_iconCache.find(cacheKey) != g_iconCache.end()) {

            return true;
        }
    }

    // PRE-FLIGHT CHECK: Bypass heavy extraction for identical generic icons in the same pass
    // ONLY do this if we don't have a stable file path (e.g. standard generic icons)
    if (!isFolder && !isThumbnail && filePath.empty() && customIconPath.empty()) {

        auto preFlightIt = passIndexMap.find(imageIndex);

        if (preFlightIt != passIndexMap.end()) {
            uint64_t knownHash = preFlightIt->second;
            ComPtr<ID2D1Bitmap> sharedBmp;
            {
                std::lock_guard<std::mutex> lock(g_uniqueShapesMutex);
                auto shapeIt = g_shapeToBitmap.find(knownHash);
                if (shapeIt != g_shapeToBitmap.end()) {
                    sharedBmp = shapeIt->second;
                }
            }

            if (sharedBmp) {
                // Free ride! Link the unique cacheKey to the shared GPU texture and hash.
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_iconCache[cacheKey] = sharedBmp;
                std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
                g_iconHashCache[cacheKey] = knownHash;

                // [STAKEOUT] The blank-doc suspect: this bypass is the one path
                // that can cache GENERIC pixels while leaving wasGeneric=false
                // (shared shape from an earlier same-index icon). Log every
                // ride with the baseline state so a stuck icon's history shows
                // whether it boarded here.
                DiagLog(L"[STAKEOUT-FR] free-ride imgIdx=%d key=%llu baselinesReady=%d fallbackFlagged=%d",
                        imageIndex, (unsigned long long)cacheKey,
                        g_baselinesReady ? 1 : 0,
                        (outIsFallback && *outIsFallback) ? 1 : 0);

                // Do not bypass placeholder detection: flag for retry if this shared shape is generic
                if (outIsFallback && g_baselinesReady) {
                    *outIsFallback = true;
                }
                return true;
            }
        }
    }

    if (!g_workerWicFactory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                         CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_workerWicFactory));
    }
    if (!g_resourceDc || !g_workerWicFactory) return false;


    int targetSize = g_cachedIconSize.load(std::memory_order_relaxed);

    float thumbAspectW = 1.0f, thumbAspectH = 1.0f;
    bool isNonImageMedia = false;
    bool hasRealThumbnail = false;

    if (isThumbnail && !filePath.empty()) {
        size_t dotPos = filePath.find_last_of(L'.');
        if (dotPos != std::wstring::npos) {
            std::wstring ext = filePath.substr(dotPos);
            for (const auto& mExt : kMediaContainerExts) {
                if (_wcsicmp(ext.c_str(), mExt) == 0) {
                    isNonImageMedia = true;
                    break;
                }
            }

            // SPECIAL CASE: WebP can be static or animated.
            if (!isNonImageMedia && _wcsicmp(ext.c_str(), L".webp") == 0) {
                HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    uint8_t header[21] = {0};
                    DWORD bytesRead = 0;
                    if (ReadFile(hFile, header, sizeof(header), &bytesRead, nullptr) && bytesRead >= 21) {
                        if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' &&
                            header[8] == 'W' && header[9] == 'E' && header[10] == 'B' && header[11] == 'P' &&
                            header[12] == 'V' && header[13] == 'P' && header[14] == '8' && header[15] == 'X') {
                            if (header[20] & 0x02) {
                                isNonImageMedia = true;
                            }
                        }
                    }
                    CloseHandle(hFile);
                }
            }
        }

        // 1. Query Windows Shell for genuine thumbnail preview (videos, photos, and audio album art)
        ComPtr<IShellItem> shellItem;
        if (SUCCEEDED(SHCreateItemFromParsingName(filePath.c_str(), nullptr, IID_PPV_ARGS(&shellItem)))) {
            ComPtr<IShellItemImageFactory> factory;
            if (SUCCEEDED(shellItem->QueryInterface(IID_PPV_ARGS(&factory)))) {
                SIZE size = { (LONG)targetSize, (LONG)targetSize };
                HBITMAP hBitmap = nullptr;
                HRESULT hrThumb = factory->GetImage(size, (SIIGBF)0x18, &hBitmap);
                if (SUCCEEDED(hrThumb) && hBitmap) {
                    BITMAP bm = {};
                    if (GetObject(hBitmap, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
                        hasRealThumbnail = true;
                        thumbAspectW = (float)bm.bmWidth;
                        thumbAspectH = (float)bm.bmHeight;
                    }
                    DeleteObject(hBitmap);
                }
            }
        }

        // 2. For image files (e.g. photos), fallback to WIC header read if Shell hasn't cached it yet
        if (!hasRealThumbnail && !isNonImageMedia) {
            ComPtr<IWICBitmapDecoder> decoder;
            if (SUCCEEDED(g_workerWicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) {
                ComPtr<IWICBitmapFrameDecode> frame;
                if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                    UINT iw = 0, ih = 0;
                    if (SUCCEEDED(frame->GetSize(&iw, &ih)) && iw > 0 && ih > 0) {
                        hasRealThumbnail = true;
                        thumbAspectW = (float)iw;
                        thumbAspectH = (float)ih;
                    }
                }
            }
        }

        // If no genuine thumbnail/album art exists, treat this item as a standard icon!
        // This preserves custom file association icons (e.g. FileTypesMan, Foobar, VLC, etc.)
        isThumbnail = hasRealThumbnail;
    }

    // RAII baton: the icon is destroyed automatically when it leaves scope
    // unless ownership was never taken (shared ImageList icons etc.). Makes
    // every early return in the extraction ladder leak-proof.
    UniqueHICON baton;

    // 1. Direct extraction for items with explicit custom icon paths (Customized Folders via desktop.ini or Shortcuts)
    if (hasCustomIcon && !customIconPath.empty()) {
        HICON hIconLarge = nullptr;
        HICON hIconSmall = nullptr;
        UINT nIconSize = MAKELONG(targetSize, targetSize);
        HRESULT hrExtract = SHDefExtractIconW(customIconPath.c_str(), customIconIndex, 0, &hIconLarge, &hIconSmall, nIconSize);
        if (SUCCEEDED(hrExtract) && hIconLarge) {
            baton.assign(hIconLarge, true);
            if (hIconSmall) DestroyIcon(hIconSmall);
        }
    }

    // 2. Stuffed Folders without custom desktop.ini: ALWAYS use default Windows yellow folder (imageres.dll, 3)
    // so paper sheets / file thumbnails sticking out don't distort the clean silhouette.
    if (!baton && isFolder && !hasCustomIcon && isFolderWithSubfolders) {
        HICON hIconLarge = nullptr;
        HICON hIconSmall = nullptr;

        WCHAR sysDir[MAX_PATH];
        GetSystemDirectoryW(sysDir, MAX_PATH);
        std::wstring imagePath = std::wstring(sysDir) + L"\\imageres.dll";
        std::wstring shellPath = std::wstring(sysDir) + L"\\shell32.dll";

        UINT nIconSize = MAKELONG(targetSize, targetSize);
        HRESULT hrExtract = SHDefExtractIconW(imagePath.c_str(), 3, 0, &hIconLarge, &hIconSmall, nIconSize);
        if (FAILED(hrExtract) || !hIconLarge) {
            hrExtract = SHDefExtractIconW(shellPath.c_str(), 3, 0, &hIconLarge, &hIconSmall, nIconSize);
        }

        if (SUCCEEDED(hrExtract) && hIconLarge) {
            baton.assign(hIconLarge, true);
            if (hIconSmall) DestroyIcon(hIconSmall);
        }
    }

    // 3. Empty Folders without custom desktop.ini: check for Winaero / Registry Shell Icons "3"
    // (If not set, falls through to HIMAGELIST to extract the exact native empty folder icon!)
    if (!baton && isFolder && !hasCustomIcon && !isFolderWithSubfolders) {
        std::wstring customFolderFile;
        int customFolderIdx = 0;
        if (GetCustomShellIcon(L"3", customFolderFile, customFolderIdx)) {
            HICON hIconLarge = nullptr;
            HICON hIconSmall = nullptr;
            UINT nIconSize = MAKELONG(targetSize, targetSize);
            HRESULT hrExtract = SHDefExtractIconW(customFolderFile.c_str(), customFolderIdx, 0, &hIconLarge, &hIconSmall, nIconSize);
            if (SUCCEEDED(hrExtract) && hIconLarge) {
                baton.assign(hIconLarge, true);
                if (hIconSmall) DestroyIcon(hIconSmall);
            }
        }
    }

    // 4. Extraction from desktop ListView HIMAGELIST (exact native desktop zoom size: 48px, 64px, 96px, etc.)
    if (!baton) {
        HIMAGELIST hCachedIL = g_cachedImageList.load(std::memory_order_relaxed);
        if (!hCachedIL && hLV) {
            hCachedIL = ListView_GetImageList(hLV, LVSIL_NORMAL);
        }
        if (hCachedIL && imageIndex >= 0) {
            HICON hFromList = ImageList_GetIcon(hCachedIL, imageIndex, ILD_TRANSPARENT);
            if (hFromList) baton.assign(hFromList, true);
        }
    }

    // 5. Fallback direct extraction from target file if ImageList hasn't resolved yet
    if (!baton && !filePath.empty()) {
        HICON hIconLarge = nullptr;
        HICON hIconSmall = nullptr;
        UINT nIconSize = MAKELONG(targetSize, targetSize);
        if (SUCCEEDED(SHDefExtractIconW(filePath.c_str(), 0, 0, &hIconLarge, &hIconSmall, nIconSize)) && hIconLarge) {
            baton.assign(hIconLarge, true);
            if (hIconSmall) DestroyIcon(hIconSmall);
        }
    }

    if (!baton) {
        // Schedule auto-retry so newly installed/dropped icons resolve the moment Explorer finishes loading them
        ArmSnapRetry(300);
        return false;
    }

    ComPtr<IWICBitmap> wicBitmap;
    HRESULT hr = g_workerWicFactory->CreateBitmapFromHICON(baton.get(), &wicBitmap);
    // baton destructor destroys the icon - no manual cleanup needed

    if (FAILED(hr)) {
        ArmSnapRetry(300);
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = g_workerWicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(wicBitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);

    if (FAILED(hr)) {
        ArmSnapRetry(300);
        return false;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);

    ComPtr <ID2D1Bitmap > d2dBitmap;
    uint64_t matchedHash = cacheKey;

    if (w > 0 && h > 0) {
        std::vector<BYTE> tempBgra(w * h * 4);
        if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, (UINT)tempBgra.size(), tempBgra.data()))) {

            // ==========================================================
            // CPU SILHOUETTE PROCESSOR (1-Byte Alpha Crunching)
            // ==========================================================
            std::vector<uint8_t> alphaOnly(w * h);
            UINT tW = 0, tH = 0;

            if (isThumbnail && !isFolder) {
                // Universal Ceiling Math: Always rounds up to prevent the 1-pixel missing row/column
                auto MulDivCeil = [](UINT n, UINT num, UINT den) -> UINT {
                    return (UINT)((((unsigned long long)n * num) + den - 1) / den);
                };

                if (thumbAspectW * h > thumbAspectH * w) {
                    // Landscape: width is locked, height gets the ceiling pixel
                    tW = w;
                    tH = MulDivCeil(w, (UINT)thumbAspectH, (UINT)thumbAspectW);
                } else {
                    // Portrait/Square: height is locked, width (the side) gets the ceiling pixel
                    tH = h;
                    tW = MulDivCeil(h, (UINT)thumbAspectW, (UINT)thumbAspectH);
                }

                // Safety clamps
                if (tW > w) tW = w;
                if (tH > h) tH = h;

                UINT minX = (w - tW) / 2;
                UINT minY = h - tH;

                // Attempt to decode the real alpha channel from the source file
                bool extractedAlpha = false;
                ComPtr<IWICBitmapDecoder> thumbDecoder;
                if (SUCCEEDED(g_workerWicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &thumbDecoder))) {
                    ComPtr<IWICBitmapFrameDecode> thumbFrame;
                    if (SUCCEEDED(thumbDecoder->GetFrame(0, &thumbFrame))) {
                        ComPtr<IWICBitmapScaler> scaler;
                        if (SUCCEEDED(g_workerWicFactory->CreateBitmapScaler(&scaler))) {
                            if (SUCCEEDED(scaler->Initialize(thumbFrame.Get(), tW, tH, WICBitmapInterpolationModeLinear))) {
                                ComPtr<IWICFormatConverter> thumbConverter;
                                if (SUCCEEDED(g_workerWicFactory->CreateFormatConverter(&thumbConverter))) {
                                    if (SUCCEEDED(thumbConverter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))) {
                                        std::vector<BYTE> thumbPixels(tW * tH * 4);
                                        if (SUCCEEDED(thumbConverter->CopyPixels(nullptr, tW * 4, (UINT)thumbPixels.size(), thumbPixels.data()))) {
                                            for (UINT y = 0; y < tH; ++y) {
                                                for (UINT x = 0; x < tW; ++x) {
                                                    UINT destX = minX + x;
                                                    UINT destY = minY + y;
                                                    if (destX < w && destY < h) {
                                                        uint8_t a = thumbPixels[(y * tW + x) * 4 + 3];
                                                        alphaOnly[destY * w + destX] = (a >= 8) ? a : 0;
                                                    }
                                                }
                                            }
                                            extractedAlpha = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Standard solid card rectangle (used for videos and opaque photos)
                if (!extractedAlpha) {
                    for (UINT y = 0; y < tH; ++y) {
                        for (UINT x = 0; x < tW; ++x) {
                            UINT destX = minX + x;
                            UINT destY = minY + y;
                            if (destX < w && destY < h) {
                                alphaOnly[destY * w + destX] = 255;
                            }
                        }
                    }
                }

            } else {
                // Solid alpha sealing for standard icons: prevents bright pixels from shining through the overlay
                for (UINT i = 0; i < w * h; ++i) {
                    uint8_t a = tempBgra[i * 4 + 3];
                    // Clean threshold: discard faint compression noise (<16), seal real icon pixels to 255
                    alphaOnly[i] = (a >= 16) ? 255 : 0;
                }
            }

            // --- DIRECT STOCK PLACEHOLDER DETECTION (zero pixel math, instant metadata check) ---
            // No class exclusions: a thumbnail-less video or an unresolved .lnk
            // landing on the stock document icon IS a placeholder, and excluding
            // those classes left them permanently stuck with it (the roach).
            if (outIsFallback) {
                int genericDocIdx = GetGenericDocSysIconIndex();
                if (genericDocIdx >= 0 && imageIndex == genericDocIdx) {
                    *outIsFallback = true;
                }
            }

            // [PIXEL-GENERIC 2026-11-03] Index-based detection cannot survive
            // ImageList churn. Stakeout evidence (zCode update repro): Explorer
            // repopulates list slots during installs and serves GENERIC pixels
            // at NON-generic indexes (icon indexes jumped 19->24->19 across the
            // update while placeholder pixels were cached as good under fresh
            // stable keys — fallback=0, no retries, stuck until a forced
            // rebuild). So: byte-compare the extracted alpha against the
            // generic baselines (both thresholded identically, 0/255). Pixels
            // identical to the placeholder = placeholder, whatever the index.
            // [v2.1.13] Lazy baseline top-up from the LV's OWN ImageList: the
            // generic-doc icon as the desktop itself serves it — same list,
            // same size, same rendering pipeline as the churn pixels, so the
            // byte-compare below is apples-to-apples by construction. Runs
            // whenever the eager BuildGenericBaselines attempt came up short
            // (assoc failure at reload timing, or the list only became
            // available later). Builds once; ignores repeats.
            if (outIsFallback) {
                // Top-up when NO file variant exists at the CURRENT extraction
                // size: variants built from icon files come out at their
                // native frames (40x40/51x51 measured) while ImageList
                // extractions come out at the desktop's zoom size (46x46
                // measured) — only a same-size variant can byte-match.
                bool haveMatchingSize = false;
                {
                    // [PHASE 3 v2.4.5] Pool threads read this vector too now;
                    // the top-up path below pushes under this same mutex.
                    std::lock_guard<std::mutex> bLock(g_uniqueShapesMutex);
                    for (const auto& e : g_genericFileBaselines)
                        if (e.width == targetSize && e.height == targetSize) { haveMatchingSize = true; break; }
                }
                if (!haveMatchingSize) {
                HIMAGELIST hIL = g_cachedImageList.load(std::memory_order_relaxed);
                if (!hIL && hLV) hIL = ListView_GetImageList(hLV, LVSIL_NORMAL);
                int gIdx = GetGenericDocSysIconIndex();
                HICON hGen = (hIL && gIdx >= 0) ? ImageList_GetIcon(hIL, gIdx, ILD_TRANSPARENT) : nullptr;
                static int s_topUpLogged = 0;
                if (s_topUpLogged < 3) {
                    DiagLog(L"[STAKEOUT] top-up attempt: hIL=%p gIdx=%d hGen=%p",
                            (void*)hIL, gIdx, (void*)hGen);
                    s_topUpLogged++;
                }
                if (hGen) {
                        SilhouetteData v;
                        ExtractIconAlphaBaseline(hGen, targetSize, v);
                        DestroyIcon(hGen);
                        if (v.width > 0) {
                            bool already = false;
                            std::lock_guard<std::mutex> uLock(g_uniqueShapesMutex);
                            for (const auto& e : g_genericFileBaselines) {
                                if (e.width == v.width && e.height == v.height &&
                                    e.alphaData == v.alphaData) { already = true; break; }
                            }
                            if (!already) {
                                g_genericFileBaselines.push_back(std::move(v));
                                DiagLog(L"[STAKEOUT] baseline top-up from ImageList: %ux%u (idx %d)",
                                        v.width, v.height, gIdx);
                            }
                            if (g_genericFolderBaseline.width > 0)
                                g_baselinesReady = true;
                        }
                    }
                }
            }

            if (outIsFallback && !*outIsFallback && g_baselinesReady) {
                // [PHASE 3 v2.4.5] Baseline vectors are shared with the pool
                // threads now (top-up pushes under this mutex); the unlocked
                // reads below were single-thread-worker-safe, no longer.
                std::lock_guard<std::mutex> cmpLock(g_uniqueShapesMutex);
                auto matchesBaseline = [&](const SilhouetteData& b) {
                    return b.width == w && b.height == h &&
                           b.alphaData.size() == alphaOnly.size() &&
                           memcmp(b.alphaData.data(), alphaOnly.data(), alphaOnly.size()) == 0;
                };
                bool pixelGeneric = false;
                for (const auto& b : g_genericFileBaselines) {
                    if (matchesBaseline(b)) { pixelGeneric = true; break; }
                }
                // [FLASH-FIX v2.9.22] A genuine uncustomized FOLDER's icon IS
                // the generic folder baseline — matching it is not a
                // placeholder. Flagging it set wasGeneric permanently ->
                // forceReextract EVERY pass -> the 8433 erase wiped the
                // folder combo's RAM silhouettes each pass -> the mask
                // white-rected during the worker's busy window (Doc's
                // selected-folders flash, 200x receipts; force-re generic=1
                // folder=1 + br=D every pass). Exempt real folders from the
                // folder-baseline match; files matching it stay flagged
                // (that remains a poison signal — the blank-doc family).
                bool isPlainFolder = isFolder && !hasCustomIcon;
                if (!pixelGeneric && !isPlainFolder && matchesBaseline(g_genericFolderBaseline)) pixelGeneric = true;
                // [v2.1.10] Thumbnails keep alpha gradations, so byte-compare
                // against the 0/255 baselines can never match — compare a
                // thresholded copy instead.
                if (!pixelGeneric && isThumbnail && !isFolder) {
                    std::vector<uint8_t> thr(alphaOnly.size());
                    for (size_t i = 0; i < alphaOnly.size(); ++i)
                        thr[i] = (alphaOnly[i] >= 16) ? 255 : 0;
                    auto matchesThr = [&](const SilhouetteData& b) {
                        return b.width == w && b.height == h &&
                               b.alphaData.size() == thr.size() &&
                               memcmp(b.alphaData.data(), thr.data(), thr.size()) == 0;
                    };
                    for (const auto& b : g_genericFileBaselines)
                        if (matchesThr(b)) { pixelGeneric = true; break; }
                    if (!pixelGeneric && matchesThr(g_genericFolderBaseline)) pixelGeneric = true;
                }
                if (pixelGeneric) {
                    *outIsFallback = true;
                    DiagLog(L"[STAKEOUT] PIXEL-GENERIC hit: imgIdx=%d key=%llu (index check missed, pixels == baseline)",
                            imageIndex, (unsigned long long)cacheKey);
                } else {
                    // [v2.1.10] Probe: log the extracted dims + alpha hash for
                    // the first extractions of the session. The baseline
                    // variants built at reload came out 40x40 / 51x51 — if
                    // ImageList extractions are a different size entirely,
                    // byte-compare can never fire and the baselines must be
                    // rebuilt from the SAME source (the LV's own ImageList).
                    static int s_probeLogged = 0;
                    if (s_probeLogged < 6) {
                        uint64_t exHash = 14695981039346656037ULL;
                        for (uint8_t a : alphaOnly) { exHash ^= a; exHash *= 1099511628211ULL; }
                        DiagLog(L"[STAKEOUT] extracted %ux%u hash=%llu imgIdx=%d (baselines: %d file variants @ %ux%u)",
                                w, h, (unsigned long long)exHash, imageIndex,
                                (int)g_genericFileBaselines.size(),
                                g_genericFileBaselines.empty() ? 0 : g_genericFileBaselines[0].width);
                        s_probeLogged++;
                    }
                }
            }

            // 4-Bucket Hash Comparison (running lightning fast on 1-byte arrays)
            // SHAPE DEDUPLICATION
            {
                ULONGLONG tSad0 = GetTickCount64(); // [PERF-AUDIT v2.2.6]
                std::lock_guard<std::mutex> lock(g_uniqueShapesMutex);

                if (isThumbnail && !isFolder) {
                    // THUMBNAIL FAST-PATH: Exact dimension match!
                    // Just compare the two numbers (width and height of the rectangle).
                    for (const auto& shape : g_uniqueShapes) {
                        if (shape.thumbW == tW && shape.thumbH == tH) {
                            matchedHash = shape.hash;
                            auto bmpIt = g_shapeToBitmap.find(matchedHash);
                            if (bmpIt != g_shapeToBitmap.end()) d2dBitmap = bmpIt->second; // REUSE GPU ASSET!
                            break;
                        }
                    }
                } else {
                    // STANDARD ICONS: 4-Bucket Fuzzy SAD Comparison
                    // Tightened tolerance: Only allow a 10% deviation.
                    // Prevents outline icons with different text (e.g. "py" vs "txt") from being merged.
                    uint32_t maxAllowedPenalty = (w * h) / 10;

                    for (const auto & shape : g_uniqueShapes) {
                        if (shape.width != w || shape.height != h) continue;

                        uint32_t totalPenalty = CalculateSADPenalty(shape.alphaData.data(), alphaOnly.data(), w * h, maxAllowedPenalty);

                    if (totalPenalty <= maxAllowedPenalty) {
                        matchedHash = shape.hash;
                        auto bmpIt = g_shapeToBitmap.find(matchedHash);
                        if (bmpIt != g_shapeToBitmap.end()) d2dBitmap = bmpIt->second; // REUSE GPU ASSET!
                            break;
                        }
                    }
                }


                // If FAIL: Upload brand new unique 8-bit texture to GPU
                if (d2dBitmap == nullptr) {
                    // We still need a hash to use as the dictionary key for the map
                    uint64_t h_hash = 14695981039346656037ULL; // FNV-1a
                    for (UINT i = 0; i < w * h; ++i) {
                        h_hash ^= alphaOnly[i];
                        h_hash *= 1099511628211ULL;
                    }
                    matchedHash = h_hash;

                    // Direct2D explicitly supports native 8-bit Alpha format! (75% less VRAM)
                    std::vector<BYTE> bgraData(w * h * 4);
                    for (UINT i = 0; i < w * h; ++i) {
                        bgraData[i * 4 + 0] = 0; // B
                        bgraData[i * 4 + 1] = 0; // G
                        bgraData[i * 4 + 2] = 0; // R
                        bgraData[i * 4 + 3] = alphaOnly[i]; // A
                    }

                    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                        D2D1_BITMAP_OPTIONS_NONE,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
                    );

                    {
                        ComPtr <ID2D1Bitmap1> tempBitmap1;
                        std::lock_guard<std::mutex> dcLock(g_resourceDcMutex);
                        hr = g_resourceDc->CreateBitmap(D2D1::SizeU(w, h), bgraData.data(), w * 4,  &props,  &tempBitmap1);
                        d2dBitmap = tempBitmap1;
                    }

                    // Save for future sharing (passing tW and tH for thumbnails!)
                    g_uniqueShapes.push_back({matchedHash, w, h, std::move(alphaOnly), tW, tH});
                    g_shapeToBitmap[matchedHash] = d2dBitmap;

                }
                // [PERF-AUDIT v2.2.6] SAD scan cost vs the whole shape store.
                ULONGLONG sadMs = GetTickCount64() - tSad0;
                if (sadMs > 0) DiagLog(L"[EXTRACT-SAD] %llums imgIdx=%d matched=%d", (unsigned long long)sadMs, imageIndex, matchedHash != cacheKey ? 1 : 0);
            }
        }
    }

    // Save to the per-pass map so the next identical generic icon gets the free ride (exclude placeholder/virtual index <= 0)
    if (!isFolder && !isThumbnail && filePath.empty() && customIconPath.empty() && imageIndex > 0) {
        passIndexMap[imageIndex] = matchedHash;
    }

    // CRITICAL: Never cache NULL bitmaps. If the Shell returned an empty icon or D2D failed,
    // we must return false so the caller knows to retry on the next pass instead of permanently hiding the icon.
    if (!d2dBitmap) return false;
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_iconCache[cacheKey] = d2dBitmap;
    {
        std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
        g_iconHashCache[cacheKey] = matchedHash;
    }
    return true;
}

inline bool IsIconExpanded(const IconSnapshot& snap);

// Tracks the "Show desktop icons" setting (Explorer implements it by hiding
// the desktop's SysListView32, so hLV's visibility IS the setting's state).
// On hide: purge all cached icon geometry so nothing can shadow or cut out
// invisible icons; on re-show: force a deep pass to rebuild everything.
// Returns true when icon-geometry work must be skipped. UI thread only.
bool UpdateDesktopIconsHiddenState(HWND hLV) {
    bool iconsHidden = !IsWindowVisible(hLV);
    bool wasHidden = g_desktopIconsHidden.exchange(iconsHidden, std::memory_order_relaxed);
    if (iconsHidden) {
        if (!wasHidden) {
            {
                std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                g_iconSnapshots.clear();
                g_snapshotGeneration.fetch_add(1, std::memory_order_release);
            }
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                g_selectedRects.clear();
            }
            g_selectionJustChanged.store(true, std::memory_order_relaxed);
            g_forceRender.store(true, std::memory_order_relaxed);
            g_underlayForceRender.store(true, std::memory_order_relaxed);
            if (g_overlayWnd) KillTimer(g_overlayWnd, TIMER_ID_COLLAPSE_POLL);
            Wh_Log(L"[ICONS] Show desktop icons disabled -> geometry purged, shadows/cut-outs gated off");
        }
        return true;
    }
    if (wasHidden) {
        Wh_Log(L"[ICONS] Show desktop icons re-enabled -> forcing full snapshot");
        QueueSnapshotUpdate();
    }
    return false;
}

void QuickSnapshotStatePoll() {
    // Do not poll geometry if the overlay is currently being recreated
    if (!g_overlayWnd || !IsWindow(g_overlayWnd)) return;

    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    if (!hLV || !IsWindow(hLV) || g_unloading) return;

    // "Show desktop icons" off: purge/skip icon-geometry work (also detects
    // the re-show transition). The heartbeat calls this too, so the toggle is
    // detected even with no selection events flowing.
    if (UpdateDesktopIconsHiddenState(hLV)) return;

    UpdateCachedImageList(hLV);

    int count = (int)SendMessage(hLV, LVM_GETITEMCOUNT, 0, 0);
    {
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        if (count != (int)g_iconSnapshots.size()) {
            DiagLog(L"[TRIG-COUNT] poll: %d items != %zu published -> full pass",
                    count, g_iconSnapshots.size());
            if (g_overlayWnd) {
                g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
            }
            return;
        }
    }

    int draggedIdx = g_draggedItemIndex.load(std::memory_order_relaxed);
    bool isLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    // SELF-HEAL: WM_LBUTTONUP can be swallowed (capture changes, OLE drag), so
    // g_isDraggingItem must never outlive the physical button state.
    if (!isLeftDown && g_isDraggingItem.exchange(false, std::memory_order_relaxed)) {
        g_underlayForceRender = true;
    }
    HWND hFocus = GetFocus();
    bool isMarqueeDrag = isLeftDown && (draggedIdx == -1) && (hFocus == hLV);

    bool isEditing = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        isEditing = g_isEditing;
    }
    g_isMarqueeDrag.store(isMarqueeDrag, std::memory_order_relaxed);

    // Read item states and geometry outside the lock
    struct PollItemData {
        UINT state;
        int imageIndex;
        std::wstring name;
        RECT iconRect;
        RECT boundsRect;
        RECT labelRect;
        bool valid;
    };
    std::vector<PollItemData> polled(count);

    for (int i = 0; i < count; ++i) {
        WCHAR diagIdentText[MAX_PATH] = {};
        LVITEMW lvi = {};
        lvi.mask = LVIF_IMAGE | LVIF_STATE | LVIF_TEXT;
        lvi.stateMask = LVIS_SELECTED | LVIS_DROPHILITED | LVIS_FOCUSED;
        lvi.iItem = i;
        lvi.pszText = diagIdentText;
        lvi.cchTextMax = MAX_PATH;
        SendMessage(hLV, LVM_GETITEM, 0, (LPARAM)&lvi);

        polled[i].state = lvi.state;
        polled[i].imageIndex = lvi.iImage;
        polled[i].name = diagIdentText;
        polled[i].valid = GetListViewItemRects(hLV, g_overlayWnd, i, polled[i].iconRect, polled[i].boundsRect, polled[i].labelRect);
    }

    std::unique_lock<std::mutex> snapLock(g_snapshotMutex);
    if ((size_t)count != g_iconSnapshots.size()) return;

    bool collapseStateChanged = false;
    bool anyPending = false;
    // [SLITHER-PROBE v2.5.1] Poll-side capture. Collected under snapLock,
    // flushed after unlock (DiagLog does file I/O — never inside the lock).
    std::vector<std::wstring> slitherFixLogs;
    for (size_t i = 0; i < g_iconSnapshots.size(); ++i) {
        bool diagIdentImageMismatch = (polled[i].imageIndex != g_iconSnapshots[i].imageIndex);
        bool diagIdentNameMismatch = (!polled[i].name.empty() && g_iconSnapshots[i].displayName != polled[i].name);
        if (diagIdentImageMismatch || diagIdentNameMismatch) {
            DiagLog(L"[TRIG-IDENT] poll idx %zu: img %dvs%d nameMismatch=%d -> full pass",
                    i, polled[i].imageIndex, g_iconSnapshots[i].imageIndex,
                    diagIdentNameMismatch ? 1 : 0);
            if (g_overlayWnd) {
                g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
            }
            return;
        }

        IconSnapshot& snap = g_iconSnapshots[i];
        UINT oldPollState = snap.state;
        snap.state = polled[i].state;

        // --- GHOST FOCUS FIX + COMPOSITE GEOMETRY VALIDATION ---
        // The deep pass builds whichever composite matches the state at
        // capture time, using the geometry captured at that same instant. If
        // geometry and state drifted apart between capture and build (boot
        // restore race: state said expanded, label rect was still collapsed),
        // the composite holds the WRONG silhouette — that was the truncated
        // "expanded" ghost-selected bug. The poll is the authority: compare
        // each combo's stamped build-geometry against the live rect and
        // invalidate + rebuild any composite baked from stale geometry.
        // CAUTION: right after a deselect the LV transiently reports the
        // COLLAPSED rect while the painted ghost label is still expanded — a
        // single bad sample must not destroy a correct expanded composite.
        // Every mismatch is therefore re-measured once, and only a FRESH read
        // that still disagrees with the stamped geometry invalidates.
        {
            bool needComboRebuild = false;
            auto liveRectDisagrees = [&](const RECT& built) -> bool {
                RECT reIcon, reBounds, reLabel;
                if (!GetListViewItemRects(hLV, g_overlayWnd, (int)i, reIcon, reBounds, reLabel)) return false;
                int rw = reLabel.right - reLabel.left;
                int rh = reLabel.bottom - reLabel.top;
                return (rw != built.right - built.left) || (rh != built.bottom - built.top);
            };
            if (IsIconExpanded(snap)) {
                if (snap.comboHash_e == 0) {
                    // Expanded (incl. ghost focus) but no expanded composite yet.
                    needComboRebuild = true;
                } else {
                    int bw = snap.comboRect_e.right - snap.comboRect_e.left;
                    int bh = snap.comboRect_e.bottom - snap.comboRect_e.top;
                    int lw = snap.labelRect.right - snap.labelRect.left;
                    int lh = snap.labelRect.bottom - snap.labelRect.top;
                    if (bw > 0 && bh > 0 && (bw != lw || bh != lh) &&
                        liveRectDisagrees(snap.comboRect_e)) {
                        DiagLog(L"[GHOST] '%s': expanded combo baked at %dx%d, live label rect %dx%d (re-measured) - invalidated for rebuild",
                                polled[i].name.empty() ? L"?" : polled[i].name.c_str(), bw, bh, lw, lh);
                        snap.comboHash_e = 0; // stale geometry: force rebuild
                        needComboRebuild = true;
                    }
                }
            } else if (snap.comboHash_c != 0) {
                // Collapsed composite: only distrust it when the live rect is
                // SMALLER than what it was built with (label shrank below its
                // build geometry). Live being larger is just the expanded
                // state, not staleness.
                int bw = snap.comboRect_c.right - snap.comboRect_c.left;
                int bh = snap.comboRect_c.bottom - snap.comboRect_c.top;
                int lw = snap.labelRect.right - snap.labelRect.left;
                int lh = snap.labelRect.bottom - snap.labelRect.top;
                if (bw > 0 && bh > 0 && (lw < bw || lh < bh) &&
                    liveRectDisagrees(snap.comboRect_c)) {
                    snap.comboHash_c = 0; // stale geometry: force rebuild
                    needComboRebuild = true;
                }
            }
            if (needComboRebuild) {
                ULONGLONG nowReq = GetTickCount64();
                if (nowReq - snap.lastComboRebuildReq > 500) {
                    snap.lastComboRebuildReq = nowReq;
                    DiagLog(L"[TRIG-COMBO] poll idx %zu '%s': combo geometry invalidation -> full pass",
                            i, polled[i].name.empty() ? L"?" : polled[i].name.c_str());
                    if (g_overlayWnd && !g_snapshotUpdatePending.load(std::memory_order_relaxed)) {
                        g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                        PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
                    }
                }
            }
        }

        // --- COLLAPSE HOLD management ---
        if (snap.state & LVIS_SELECTED) {
            snap.pendingCollapse = false;
        } else if ((oldPollState & LVIS_SELECTED) && !snap.pendingCollapse && !IsIconExpanded(snap)) {
            // Selection lost AND the label will collapse (no sticky focus keeping it
            // expanded): hold the selection-sized RECT cutout until Explorer collapses
            // the label (polled dims shrink) or the safety timeout fires.
            //
            // Ghost-focus (selection lost, focus kept) is deliberately excluded:
            // the label stays expanded, so there is nothing to wait for and holding
            // would only delay the shadow by the full timeout.
            snap.pendingCollapse = true;
            snap.pendingCollapseStart = GetTickCount64();
            snap.heldCollapseRect = snap.boundsRect;
            collapseStateChanged = true;
        } else if (snap.pendingCollapse) {
            bool collapsed = false;
            if (polled[i].valid) {
                int newW = polled[i].labelRect.right - polled[i].labelRect.left;
                int newH = polled[i].labelRect.bottom - polled[i].labelRect.top;
                int heldW = snap.heldCollapseRect.right - snap.heldCollapseRect.left;
                int heldH = snap.heldCollapseRect.bottom - snap.heldCollapseRect.top;
                collapsed = (newW > 0 && newH > 0) && (newW < heldW || newH < heldH);
            }
            if (collapsed || (GetTickCount64() - snap.pendingCollapseStart > COLLAPSE_HOLD_MS)) {
                snap.pendingCollapse = false;
                collapseStateChanged = true;
            }
        }
        if (snap.pendingCollapse) anyPending = true;

        bool positionMoved = false;
        if (polled[i].valid && (snap.iconRect.left != polled[i].iconRect.left || snap.iconRect.top != polled[i].iconRect.top)) {
            positionMoved = true;
        }

        if (positionMoved) {
            snap.isPendingDrop = false;
            snap.isDragged = false;
            snap.preDragBoundsRect = {0, 0, 0, 0};
        } else if (snap.isPendingDrop &&
                   memcmp(&snap.boundsRect, &snap.preDragBoundsRect, sizeof(RECT)) != 0) {
            // Drop committed: the icon now sits somewhere other than its pre-drag spot
            snap.isPendingDrop = false;
            snap.preDragBoundsRect = {0, 0, 0, 0};
        } else if (snap.isDragged && !isLeftDown && !g_isDraggingItem.load(std::memory_order_relaxed)) {
            // SELF-HEAL: stale drag flag with no active drag. WM_LBUTTONUP is not
            // guaranteed to reach our subclass (e.g. capture changes mid-gesture),
            // so a dragged flag must never outlive the button press.
            snap.isDragged = false;
        } else if ((int)i == draggedIdx && isLeftDown && g_isDraggingItem.load(std::memory_order_relaxed)) {
            // Only a REAL drag marks the item as dragged. g_draggedItemIndex is set
            // on every plain button-down over an icon and must not be used here.
            snap.isDragged = true;
        }

        RECT newLabelRect = {0, 0, 0, 0};
        if ((int)i == draggedIdx || (!isMarqueeDrag && !isEditing)) {
            if (polled[i].valid) {
                snap.iconRect = polled[i].iconRect;
                snap.boundsRect = polled[i].boundsRect;
                newLabelRect = polled[i].labelRect;
            } else {
                newLabelRect = snap.labelRect;
            }
        } else {
            newLabelRect = snap.labelRect;
        }

        int oldW = snap.labelRect.right - snap.labelRect.left;
        int oldH = snap.labelRect.bottom - snap.labelRect.top;
        int newW = newLabelRect.right - newLabelRect.left;
        int newH = newLabelRect.bottom - newLabelRect.top;

        bool sizeChanged = (oldW != newW) || (oldH != newH);
        bool moved = (snap.labelRect.left != newLabelRect.left) || (snap.labelRect.top != newLabelRect.top);
        bool isLarger = (newW > oldW) || (newH > oldH);
        bool isSmaller = (newW < oldW) || (newH < oldH);

        bool shouldShrink = false;
        if (newW > 0) {
            bool desktopIsActive = g_isDesktopActive.load(std::memory_order_relaxed);
            bool itemIsHighlighted = (snap.state & (LVIS_SELECTED | LVIS_FOCUSED)) != 0;
            if (!desktopIsActive || !itemIsHighlighted) {
                shouldShrink = true;
            }
        }

        bool updateRect = (oldW == 0) || isLarger || (isSmaller && shouldShrink) || (moved && !isSmaller);
        if (updateRect) {
            snap.labelRect = newLabelRect;
            if (sizeChanged || oldW == 0) {
                DiagLog(L"[TRIG-LABEL] poll idx %zu '%s': label %dx%d -> %dx%d -> full pass",
                        i, polled[i].name.empty() ? L"?" : polled[i].name.c_str(),
                        oldW, oldH, newW, newH);
                if (!g_snapshotUpdatePending.load(std::memory_order_relaxed)) {
                    if (g_overlayWnd) {
                        g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                        PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
                    }
                }
            }
        }

        // ALWAYS keep the unified bounding box in sync with the icon and label rects.
        // This ensures shadows track perfectly during fast drags before the deep worker thread catches up.
        RECT combinedRect = snap.iconRect;
        int cLabelW = snap.labelRect.right - snap.labelRect.left;
        int cLabelH = snap.labelRect.bottom - snap.labelRect.top;
        if (cLabelW > 0 && cLabelH > 0) {
            UnionRect(&combinedRect, &snap.iconRect, &snap.labelRect);
        }
        // [SLITHER-PROBE v2.5.1] The poll re-correcting a rect the pass
        // published is the slither's "focus change re-corrects" event. Log
        // only SMALL corrections (<=2px per edge, something nonzero) — the
        // slither signature. Drags/zoom relayouts move by much more and
        // stay silent. Bounded: first 40 per session.
        {
            static int s_slitherFixLogged = 0;
            int dL = combinedRect.left - snap.combinedRect.left;
            int dT = combinedRect.top - snap.combinedRect.top;
            int dR = combinedRect.right - snap.combinedRect.right;
            int dB = combinedRect.bottom - snap.combinedRect.bottom;
            bool anyDiff = (dL || dT || dR || dB);
            bool small = (dL >= -2 && dL <= 2) && (dT >= -2 && dT <= 2) &&
                         (dR >= -2 && dR <= 2) && (dB >= -2 && dB <= 2);
            if (anyDiff && small && s_slitherFixLogged < 40) {
                ++s_slitherFixLogged;
                wchar_t line[512];
                swprintf_s(line,
                    L"[SLITHER-FIX] poll re-correct '%s' idx %zu: (%ld,%ld,%ld,%ld)->(%ld,%ld,%ld,%ld) d=(%d,%d,%d,%d) label=%dx%d",
                    snap.displayName.empty() ? L"?" : snap.displayName.c_str(), i,
                    snap.combinedRect.left, snap.combinedRect.top, snap.combinedRect.right, snap.combinedRect.bottom,
                    combinedRect.left, combinedRect.top, combinedRect.right, combinedRect.bottom,
                    dL, dT, dR, dB, cLabelW, cLabelH);
                slitherFixLogs.push_back(line);
            }
        }
        snap.combinedRect = combinedRect;
    }
    g_snapshotGeneration.fetch_add(1, std::memory_order_release);
    snapLock.unlock();
    for (const auto& l : slitherFixLogs) DiagLog(L"%s", l.c_str()); // [SLITHER-PROBE]

    // A hold starting/ending changes what the mask must stamp without changing
    // any snapshot geometry, so nudge the render loop into rebuilding the mask.
    if (collapseStateChanged) {
        g_forceRender.store(true, std::memory_order_relaxed);
        g_underlayForceRender.store(true, std::memory_order_relaxed);
    }

    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);

    // Fast poll while any collapse hold is pending, so the label-collapse signal
    // isn't delayed by the slow heartbeat. (Runs on the UI thread.)
    if (g_overlayWnd && IsWindow(g_overlayWnd)) {
        if (anyPending) {
            SetTimer(g_overlayWnd, TIMER_ID_COLLAPSE_POLL, 16, nullptr);
        } else {
            KillTimer(g_overlayWnd, TIMER_ID_COLLAPSE_POLL);
        }
    }
}

// Capture ONE desktop item's RawListItem (LV read + parsing name + rects).
// UI thread only (documented rule: the worker never messages hLV). Shared by
// the full pass and the per-icon fast path.
static void CaptureRawListItem(HWND hLV, IFolderView2* pFolderView2, int i, RawListItem& item) {
    item.index = i;

    LVITEMW lvi = {};
    lvi.mask = LVIF_IMAGE | LVIF_STATE | LVIF_TEXT;
    lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED | LVIS_DROPHILITED | LVIS_OVERLAYMASK;
    lvi.iItem = i;
    WCHAR itemName[MAX_PATH] = {0};
    lvi.pszText = itemName;
    lvi.cchTextMax = MAX_PATH;

    SendMessage(hLV, LVM_GETITEM, 0, (LPARAM)&lvi);
    item.imageIndex = lvi.iImage;
    item.state = lvi.state;
    item.overlayIndex = (lvi.state & LVIS_OVERLAYMASK) >> 8;
    item.text = itemName;

    // Retrieve canonical parsing name directly from Desktop Shell View
    if (pFolderView2) {
        ComPtr<IShellItem> shellItem;
        if (SUCCEEDED(pFolderView2->GetItem(i, IID_PPV_ARGS(&shellItem))) && shellItem) {
            PWSTR pszParsing = nullptr;
            if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pszParsing)) && pszParsing) {
                item.parsingName = pszParsing;
                CoTaskMemFree(pszParsing);
            }
        }
    }

    RECT tempIcon, tempBounds, tempLabel;
    if (GetListViewItemRects(hLV, g_overlayWnd, i, tempIcon, tempBounds, tempLabel)) {
        item.iconRect = tempIcon;
        item.boundsRect = tempBounds;
        item.labelRect = tempLabel;
    } else {
        // Fallback to the last known good snapshot to prevent 0-sized cache keys
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        if (i < (int)g_iconSnapshots.size()) {
            item.iconRect = g_iconSnapshots[i].iconRect;
            item.boundsRect = g_iconSnapshots[i].boundsRect;
            item.labelRect = g_iconSnapshots[i].labelRect;
        } else {
            item.iconRect = {0, 0, 0, 0};
            item.boundsRect = {0, 0, 0, 0};
            item.labelRect = {0, 0, 0, 0};
        }
    }
}

// --- PER-ICON FAST PATH (rebake-latency fix) ------------------------------
// A click used to ride full deep passes (175 items, 0.5-2.3 s each, 2+ per
// click per the [PASS] diag). The fast path is a priority slot the worker
// drains BEFORE the normal queue: one item, captured at selection time,
// processed + preflighted in a few ms — so the expanded-label composite and
// its shadow assets exist while the selection glow is still fading in.
struct FastPathRequest {
    std::vector<RawListItem> items;
    std::wstring userDesktop;
    std::wstring publicDesktop;
    HFONT labelFont = nullptr;
    LOGFONTW labelLogfont = {};
};
std::mutex g_fastPathMutex;
FastPathRequest g_fastPathRequest;

void QueueFastPathPass(int itemIndex) {
    if (!g_overlayWnd || !IsWindow(g_overlayWnd)) return;
    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    if (!hLV || !IsWindow(hLV) || g_unloading) return;
    if (!IsWindowVisible(hLV)) return;

    HFONT font = nullptr;
    LOGFONTW lf = {};
    {
        std::lock_guard<std::mutex> lock(g_snapshotRequestMutex);
        font = g_snapshotLabelHFont;
        lf = g_snapshotLabelFont;
    }
    if (!font) return; // no boot pass yet — the normal pass covers us

    // IFolderView2 for the canonical parsing name (same acquisition as the
    // full pass; once per click, cheap).
    ComPtr<IFolderView2> pFolderView2;
    {
        ComPtr<IShellWindows> shellWindows;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows)))) {
            VARIANT vEmpty = {};
            vEmpty.vt = VT_EMPTY;
            long hwndDesktop = 0;
            ComPtr<IDispatch> disp;
            if (SUCCEEDED(shellWindows->FindWindowSW(&vEmpty, &vEmpty, SWC_DESKTOP, &hwndDesktop, SWFO_NEEDDISPATCH, &disp)) && disp) {
                ComPtr<IServiceProvider> sp;
                if (SUCCEEDED(disp.As(&sp))) {
                    ComPtr<IShellBrowser> sb;
                    if (SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb)))) {
                        ComPtr<IShellView> sv;
                        if (SUCCEEDED(sb->QueryActiveShellView(&sv))) {
                            sv.As(&pFolderView2);
                        }
                    }
                }
            }
        }
    }

    FastPathRequest req;
    req.labelFont = font;
    req.labelLogfont = lf;
    RawListItem item = {};
    CaptureRawListItem(hLV, pFolderView2.Get(), itemIndex, item);
    req.items.push_back(std::move(item));

    PWSTR pUserDesktop = nullptr;
    PWSTR pPublicDesktop = nullptr;
    SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pUserDesktop);
    SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, NULL, &pPublicDesktop);
    req.userDesktop = pUserDesktop ? pUserDesktop : L"";
    req.publicDesktop = pPublicDesktop ? pPublicDesktop : L"";
    if (pUserDesktop) CoTaskMemFree(pUserDesktop);
    if (pPublicDesktop) CoTaskMemFree(pPublicDesktop);

    {
        std::lock_guard<std::mutex> lock(g_fastPathMutex);
        // Replace, don't queue: only the most recent click matters — an older
        // single-icon request would be stale by the time it runs.
        g_fastPathRequest = std::move(req);
    }
    if (g_snapshotWorkerEvent) SetEvent(g_snapshotWorkerEvent);
}

void QueueFullSnapshot() {
    // Do not run a deep pass if the overlay is currently being recreated
    if (!g_overlayWnd || !IsWindow(g_overlayWnd)) return;

    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    if (!hLV || !IsWindow(hLV) || g_unloading) return;
    // Hidden LV = "Show desktop icons" off: no deep passes while icons are hidden
    if (!IsWindowVisible(hLV)) return;

    // [STAMPEDE DEDUPE] One click used to queue 2-3 full passes (click +
    // focus-retry timers), each blocking the UI thread 100-450 ms to capture
    // 175 icons and processing 0.5-2.3 s serialized. Coalesce anything within
    // 250 ms of the last queued pass: the fast path already covered the
    // clicked icon, and the 1 s state poll re-detects any drift a skipped
    // pass would have caught.
    {
        static std::atomic<ULONGLONG> s_lastFullQueueTick{0};
        ULONGLONG nowTick = GetTickCount64();
        ULONGLONG lastTick = s_lastFullQueueTick.exchange(nowTick, std::memory_order_relaxed);
        if (lastTick != 0 && nowTick - lastTick < 250) {
            // [PACING v2.5.1] The dedupe used to DROP the request silently,
            // trusting "the 1s poll re-detects" — which deferred zoom passes
            // by up to a full heartbeat (the 31ms-vs-921ms PASS-START spread)
            // and could eat a zoom leg entirely (log proof 2026-11-06: the
            // 46->51 zoom got no PASS-START at all). Re-arm instead of drop:
            // the retry fires past the dedupe window and re-queues exactly
            // once. Anti-stampede property intact (still one pass per
            // 250ms window), the dropped-request hole is closed.
            ArmSnapRetry(260);
            DiagLog(L"[PACING] queue coalesced within 250ms window -> retry armed");
            return;
        }
    }

    UpdateCachedImageList(hLV);

    // STABILITY CHECK: Ensure the Shell has finished rebuilding the ImageList
    // During Ctrl+Scroll or DPI changes, the ImageList can temporarily become nullptr or empty.
    // If we try to extract icons now, extraction fails and silhouettes disappear.
    HIMAGELIST hImageList = ListView_GetImageList(hLV, LVSIL_NORMAL);
    if (!hImageList || ImageList_GetImageCount(hImageList) == 0) {
        // ImageList is mid-rebuild. Reschedule the timer and wait.
        ArmSnapRetry(300);
        return;
    }

    int count = (int)SendMessage(hLV, LVM_GETITEMCOUNT, 0, 0);
    std::vector<RawListItem> rawItems;
    rawItems.reserve(count);

    // Capture the ListView's current font for label silhouette rendering.
    // Must happen here on the UI thread; the worker never touches hLV.
    // Keep the HFONT itself (not a LOGFONT copy): selecting Explorer's real
    // font guarantees identical glyph metrics.
    static HFONT s_prevCaptured = nullptr; // owned copy of the LV font, UI-thread created
    static LOGFONTW s_prevCapturedLf = {};
    HFONT hLvFont = (HFONT)SendMessageW(hLV, WM_GETFONT, 0, 0);
    if (hLvFont) {
        if (hLvFont != s_prevCaptured) {
            // RETIRE (don't delete) the old font: an in-flight worker pass may
            // still hold it. The worker purges the graveyard on its next pickup.
            if (s_prevCaptured) {
                std::lock_guard<std::mutex> lock(g_snapshotRequestMutex);
                g_retiredLabelFonts.push_back(s_prevCaptured);
            }
            // Own a copy independent of the LV's font lifecycle
            LOGFONTW lf = {};
            if (GetObjectW(hLvFont, sizeof(LOGFONTW), &lf) == sizeof(LOGFONTW)) {
                // Resolve GDI font substitution from the registry. The
                // Winaero-style system font swap maps "Segoe UI" -> <custom>
                // via HKLM\...\FontSubstitutes (and blanks the Segoe UI font
                // files), so the LV font's LOGFONT still SAYS "Segoe UI" while
                // GDI actually draws the substituted font. Empirically
                // GetTextFace also reports the REQUESTED name in this scenario,
                // so the FontSubstitutes key is the authoritative source.
                // DWrite ignores FontSubstitutes entirely — without this
                // resolution it renders the silhouettes in the wrong font.
                WCHAR face[LF_FACESIZE];
                wcsncpy_s(face, lf.lfFaceName, _TRUNCATE);
                for (int hop = 0; hop < 4; ++hop) {
                    WCHAR resolved[LF_FACESIZE] = {};
                    DWORD cbResolved = sizeof(resolved);
                    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
                                     face, RRF_RT_REG_SZ, nullptr, resolved, &cbResolved) != ERROR_SUCCESS ||
                        !resolved[0] || _wcsicmp(resolved, face) == 0) {
                        break;
                    }
                    wcsncpy_s(face, resolved, _TRUNCATE);
                }
                if (_wcsicmp(face, lf.lfFaceName) != 0) {
                    Wh_Log(L"[LABEL] FontSubstitutes: '%s' -> '%s'", lf.lfFaceName, face);
                    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
                }
                s_prevCaptured = CreateFontIndirectW(&lf); // created on UI thread = correct DPI mapping
                s_prevCapturedLf = lf;
                // [F2] New captured font: every cached glyph alpha is stale.
                g_labelFontGen.fetch_add(1, std::memory_order_relaxed);
            } else {
                s_prevCaptured = nullptr;
            }
        }
    }

    // Query Desktop IFolderView2 to get canonical IShellItem parsing names
    ComPtr<IFolderView2> pFolderView2;
    {
        ComPtr<IShellWindows> shellWindows;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows)))) {
            VARIANT vEmpty = {};
            vEmpty.vt = VT_EMPTY;
            long hwndDesktop = 0;
            ComPtr<IDispatch> disp;
            if (SUCCEEDED(shellWindows->FindWindowSW(&vEmpty, &vEmpty, SWC_DESKTOP, &hwndDesktop, SWFO_NEEDDISPATCH, &disp)) && disp) {
                ComPtr<IServiceProvider> sp;
                if (SUCCEEDED(disp.As(&sp))) {
                    ComPtr<IShellBrowser> sb;
                    if (SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb)))) {
                        ComPtr<IShellView> sv;
                        if (SUCCEEDED(sb->QueryActiveShellView(&sv))) {
                            sv.As(&pFolderView2);
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < count; ++i) {
        RawListItem item = {};
        CaptureRawListItem(hLV, pFolderView2.Get(), i, item);
        rawItems.push_back(item);
    }

    PWSTR pUserDesktop = nullptr;
    PWSTR pPublicDesktop = nullptr;
    SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pUserDesktop);
    SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, NULL, &pPublicDesktop);

    std::wstring userDesktop = pUserDesktop ? pUserDesktop : L"";
    std::wstring publicDesktop = pPublicDesktop ? pPublicDesktop : L"";

    if (pUserDesktop) CoTaskMemFree(pUserDesktop);
    if (pPublicDesktop) CoTaskMemFree(pPublicDesktop);

    {
        std::lock_guard<std::mutex> lock(g_snapshotRequestMutex);
        g_snapshotRequestItems = std::move(rawItems);
        g_snapshotUserDesktop = userDesktop;
        g_snapshotPublicDesktop = publicDesktop;
        g_snapshotLabelHFont = s_prevCaptured;
        g_snapshotLabelFont = s_prevCapturedLf;
        g_snapshotRequestPending = true;
    }

    if (g_snapshotWorkerEvent) SetEvent(g_snapshotWorkerEvent);
}

void CacheOverlayBitmap(int overlayIndex) {
    if (overlayIndex <= 0) return;

    // Worker thread: never read g_settings directly, take a consistent snapshot
    const Settings settings = SnapshotSettings();

    {
        std::lock_guard<std::mutex> lock(g_overlayCacheMutex);
        if (g_cpuOverlaySilhouettes.find(overlayIndex) != g_cpuOverlaySilhouettes.end()) return;
    }

    if (!g_workerWicFactory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                         CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_workerWicFactory));
    }
    if (!g_resourceDc || !g_workerWicFactory) return;

    // 1. Detect the exact desktop icon size so we extract the correct resolution from the .ico pack
    int targetSize = 48;
    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    if (hLV) {
        HIMAGELIST hImageList = ListView_GetImageList(hLV, LVSIL_NORMAL);
        if (hImageList) {
            int cx = 0, cy = 0;
            ImageList_GetIconSize(hImageList, &cx, &cy);
            if (cx > 0) targetSize = cx;
        }
    }

    // The shortcut arrow is an overlay, so it should be smaller than the base icon.
    // Scaling it down to ~7% forces SHDefExtractIconW to pick the smaller, correct layer from the .ico pack.
    // 1. Check for Custom Shell Icon (Winaero Tweaker / Registry "29")
    WCHAR iconPath[MAX_PATH] = {0};
    DWORD size = sizeof(iconPath);
    HKEY hKey;
    bool found = false;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"29", nullptr, nullptr, (LPBYTE)iconPath, &size) == ERROR_SUCCESS && iconPath[0] != 0) found = true;
        RegCloseKey(hKey);
    }
    if (!found && RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        size = sizeof(iconPath);
        if (RegQueryValueExW(hKey, L"29", nullptr, nullptr, (LPBYTE)iconPath, &size) == ERROR_SUCCESS && iconPath[0] != 0) found = true;
        RegCloseKey(hKey);
    }

    if (!settings.customArrowEnabled) found = false;
    g_isCustomShortcutArrow.store(found, std::memory_order_relaxed);

    // [F1] The custom-arrow flip changes pc1's baked arrow with no combo-hash
    // input changing — stand the skip guard down for this pass.
    {
        static bool s_prevCustomArrow = false;
        if (s_prevCustomArrow != found) {
            s_prevCustomArrow = found;
            g_arrowGen.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Custom arrow uses user-defined scale; Windows Default arrow extracts at full canvas size (100%)
    int overlaySize = targetSize;
    if (found) {
        float scale = (float)settings.customArrowScale / 100.0f;
        overlaySize = (int)std::ceil(targetSize * scale);
    }
    if (overlaySize < 16) overlaySize = 16;

    UINT nIconSize = MAKELONG(overlaySize, overlaySize);

    UniqueHICON baton;
    HICON hIconLarge = nullptr;
    HICON hIconSmall = nullptr;

    if (found) {
        std::wstring pathStr = iconPath;
        int index = 0;
        size_t comma = pathStr.find_last_of(L',');
        if (comma != std::wstring::npos) {
            try {
                index = std::stoi(pathStr.substr(comma + 1));
            } catch (...) {
                index = 0;
            }
            pathStr = pathStr.substr(0, comma);
        }
        WCHAR expandedPath[MAX_PATH];
        ExpandEnvironmentStringsW(pathStr.c_str(), expandedPath, MAX_PATH);

        // SHDefExtractIconW natively handles multi-resolution .ico packs and scales them perfectly
        HRESULT hrExtract = SHDefExtractIconW(expandedPath, index, 0, &hIconLarge, &hIconSmall, nIconSize);
        if (SUCCEEDED(hrExtract) && hIconLarge) {
            baton.assign(hIconLarge, true);
            if (hIconSmall) DestroyIcon(hIconSmall);
        }
    }

    // 2. Default Fallback: Official Windows SIID_LINK Stock Shortcut Arrow
    if (!baton) {
        #ifndef SIID_LINK
        #define SIID_LINK 29
        #endif
        #ifndef SHGSI_ICONLOCATION
        #define SHGSI_ICONLOCATION 0x00000000
        #endif

        SHSTOCKICONINFO sii = { sizeof(sii) };
        if (SUCCEEDED(SHGetStockIconInfo((SHSTOCKICONID)SIID_LINK, SHGSI_ICONLOCATION, &sii))) {
            HRESULT hrExtract = SHDefExtractIconW(sii.szPath, sii.iIcon, 0, &hIconLarge, &hIconSmall, nIconSize);
            if (SUCCEEDED(hrExtract) && hIconLarge) {
                baton.assign(hIconLarge, true);
                if (hIconSmall) DestroyIcon(hIconSmall);
            }
        }
    }

    if (!baton) return;

    ComPtr<IWICBitmap> wicBitmap;
    if (FAILED(g_workerWicFactory->CreateBitmapFromHICON(baton.get(), &wicBitmap))) {
        return;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(g_workerWicFactory->CreateFormatConverter(&converter))) return;
    if (FAILED(converter->Initialize(wicBitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))) return;

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) return;

    std::vector<BYTE> tempBgra(w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)tempBgra.size(), tempBgra.data()))) return;

    std::vector<uint8_t> alphaOnly(w * h);
    for (UINT i = 0; i < w * h; ++i) {
        uint8_t a = tempBgra[i * 4 + 3];
        // Clean noise filter: discard faint compression noise (<16), preserve smooth anti-aliasing
        alphaOnly[i] = (a >= 32) ? a : 0;
    }

    // Save the raw CPU alpha array for the compositing pipeline. No GPU copy
    // is cached here: nothing drew from it (the composite pipeline consumes
    // the CPU alpha), so the bitmap upload was removed in the ponytail audit.
    SilhouetteData cpuSil;
    cpuSil.width = w;
    cpuSil.height = h;
    cpuSil.alphaData = alphaOnly;
    {
        std::lock_guard<std::mutex> lock(g_overlayCacheMutex);
        g_cpuOverlaySilhouettes[overlayIndex] = cpuSil;
    }
}

void ExtractIconAlphaBaseline(HICON hIcon, int targetSize, SilhouetteData& outBaseline) {
    if (!g_wicFactory) InitWIC();
    if (!g_wicFactory) return;

    ComPtr<IWICBitmap> wicBitmap;
    if (FAILED(g_wicFactory->CreateBitmapFromHICON(hIcon, &wicBitmap))) return;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(g_wicFactory->CreateFormatConverter(&converter))) return;
    if (FAILED(converter->Initialize(wicBitmap.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))) return;

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) return;

    std::vector<BYTE> tempBgra(w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)tempBgra.size(), tempBgra.data()))) return;

    outBaseline.width = w;
    outBaseline.height = h;
    outBaseline.alphaData.resize(w * h);

    // Threshold synced 1:1 with CacheIconBitmap for 100% memcmp match
    for (UINT i = 0; i < w * h; ++i) {
        uint8_t a = tempBgra[i * 4 + 3];
        outBaseline.alphaData[i] = (a >= 16) ? 255 : 0;
    }
}

void BuildGenericBaselines() {
    int targetSize = GetCurrentIconZoom();
    if (g_baselinesReady && g_baselineTargetSize == targetSize) return;

    g_baselinesReady = false;
    g_genericFileBaseline = {};
    g_genericFileBaselines.clear();
    g_genericFolderBaseline = {};

    if (!g_wicFactory) InitWIC();
    if (!g_wicFactory) return;

    // Generic file icon via SHGetFileInfo with dummy extension
    SHFILEINFOW sfi = {};
    if (SHGetFileInfo(L".____xdummy", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                      SHGFI_ICONLOCATION | SHGFI_USEFILEATTRIBUTES)) {
        UINT nIconSize = MAKELONG(targetSize, targetSize);
        HICON hIconLarge = nullptr, hIconSmall = nullptr;
        if (SUCCEEDED(SHDefExtractIconW(sfi.szDisplayName, sfi.iIcon, 0, &hIconLarge, &hIconSmall, nIconSize))) {
            ExtractIconAlphaBaseline(hIconLarge, targetSize, g_genericFileBaseline);
            if (hIconLarge) DestroyIcon(hIconLarge);
            if (hIconSmall) DestroyIcon(hIconSmall);
        }
    }
    g_genericFileBaselines.push_back(g_genericFileBaseline);

    // [v2.1.10] Variant 2: SIID_DOCNOASSOC stock doc (what the ImageList may
    // hold during churn, even when the association path resolves elsewhere).
    {
        SHSTOCKICONINFO sii = { sizeof(sii) };
        if (SUCCEEDED(SHGetStockIconInfo(SIID_DOCNOASSOC, SHGSI_ICON, &sii)) && sii.hIcon) {
            SilhouetteData v;
            ExtractIconAlphaBaseline(sii.hIcon, targetSize, v);
            DestroyIcon(sii.hIcon);
            g_genericFileBaselines.push_back(std::move(v));
        }
    }
    // [v2.1.10] Variant 3: the classic shell32 blank doc (Win10-era rendering,
    // still what several churn paths serve on Win11).
    {
        WCHAR sysDir2[MAX_PATH];
        GetSystemDirectoryW(sysDir2, MAX_PATH);
        UINT nIconSize2 = MAKELONG(targetSize, targetSize);
        HICON hLarge2 = nullptr, hSmall2 = nullptr;
        if (SUCCEEDED(SHDefExtractIconW((std::wstring(sysDir2) + L"\\shell32.dll").c_str(), 1, 0,
                                        &hLarge2, &hSmall2, nIconSize2)) && hLarge2) {
            SilhouetteData v;
            ExtractIconAlphaBaseline(hLarge2, targetSize, v);
            if (hSmall2) DestroyIcon(hSmall2);
            DestroyIcon(hLarge2);
            g_genericFileBaselines.push_back(std::move(v));
        }
    }

    // Ground truth for the blank-doc hunt: every variant's dims + alpha hash,
    // so a near-miss in the field can be compared against exactly what we hold.
    for (const auto& v : g_genericFileBaselines) {
        if (v.width == 0) continue;
        uint64_t h = 14695981039346656037ULL;
        for (uint8_t a : v.alphaData) { h ^= a; h *= 1099511628211ULL; }
        DiagLog(L"[STAKEOUT] baseline variant %ux%u hash=%llu",
                v.width, v.height, (unsigned long long)h);
    }

    // Generic folder icon via imageres.dll index 3 (same as CacheIconBitmap)
    WCHAR sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring imagePath = std::wstring(sysDir) + L"\\imageres.dll";
    UINT nIconSize = MAKELONG(targetSize, targetSize);
    HICON hIconLarge = nullptr, hIconSmall = nullptr;
    if (SUCCEEDED(SHDefExtractIconW(imagePath.c_str(), 3, 0, &hIconLarge, &hIconSmall, nIconSize))) {
        ExtractIconAlphaBaseline(hIconLarge, targetSize, g_genericFolderBaseline);
        if (hIconLarge) DestroyIcon(hIconLarge);
        if (hIconSmall) DestroyIcon(hIconSmall);
    }

    g_baselineTargetSize = targetSize;
    // [v2.1.13] Ready = at least one file variant AND the folder baseline.
    // The assoc variant can fail at reload timing (shell association engine
    // not warm: assocDims=0x0 measured); the lazy ImageList top-up in
    // CacheIconBitmap fills the gap from the LV's own list.
    bool anyFileVariant = false;
    for (const auto& v : g_genericFileBaselines) if (v.width > 0) anyFileVariant = true;
    g_baselinesReady = (anyFileVariant && g_genericFolderBaseline.width > 0);
    {
        int builtCount = 0;
        for (const auto& v : g_genericFileBaselines) if (v.width > 0) builtCount++;
        DiagLog(L"[STAKEOUT] baselines built: targetSize=%d ready=%d fileVariants=%d/%d assocDims=%ux%u folderDims=%ux%u",
                targetSize, g_baselinesReady ? 1 : 0, builtCount,
                (int)g_genericFileBaselines.size(),
                g_genericFileBaseline.width, g_genericFileBaseline.height,
                g_genericFolderBaseline.width, g_genericFolderBaseline.height);
    }
}


////////////////////////////////////////////////////////////////////////////////
// --- CPU SILHOUETTE COMPOSITING HELPERS ---

// ============================================================================
// Letter-shaped label silhouettes
// Render the label text with the desktop ListView's own font into an alpha
// coverage buffer using the same layout inputs Explorer uses (text, font,
// rect). DrawTextW's word-wrap/ellipsis mirrors Explorer's one-row vs two-row
// and truncation behavior closely; the shadow blur forgives small divergence.
// ============================================================================

// Expand coverage by 1px (4-neighbor max filter) so the overlay punch-through
// mask always fully covers Explorer's own anti-aliased glyph edges even if our
// layout diverges by a pixel.
static void DilateAlpha(std::vector<uint8_t>& a, int w, int h) {
    std::vector<uint8_t> src = a;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t m = src[y * w + x];
            if (x > 0) m = (std::max)(m, src[y * w + x - 1]);
            if (x < w - 1) m = (std::max)(m, src[y * w + x + 1]);
            if (y > 0) m = (std::max)(m, src[(y - 1) * w + x]);
            if (y < h - 1) m = (std::max)(m, src[(y + 1) * w + x]);
            a[y * w + x] = m;
        }
    }
}

// DWrite factory — Explorer renders desktop labels with DirectWrite, so we
// measure/shape through the same engine. GDI metrics round advances
// differently per string, which is why pixel-tuning could never converge.
static ComPtr<IDWriteFactory> g_dwriteFactory;
// Guards factory creation. Today only the snapshot worker calls in, but the
// mutex makes the lazy init safe if a second caller ever appears.
static std::mutex g_dwriteFactoryMutex;

// WindHawk's lld link ignores #pragma comment(lib); load dwrite.dll dynamically.
static bool EnsureDWriteFactory() {
    std::lock_guard<std::mutex> lock(g_dwriteFactoryMutex);
    if (g_dwriteFactory) return true;
    using PFN_DWriteCreateFactory = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    static PFN_DWriteCreateFactory pfn = nullptr;
    if (!pfn) {
        HMODULE hDWrite = LoadLibraryW(L"dwrite.dll");
        if (!hDWrite) {
            Wh_Log(L"[LABEL] dwrite.dll not found");
            return false;
        }
        pfn = (PFN_DWriteCreateFactory)GetProcAddress(hDWrite, "DWriteCreateFactory");
        if (!pfn) {
            Wh_Log(L"[LABEL] DWriteCreateFactory export missing");
            return false;
        }
    }
    HRESULT hrF = pfn(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown**>(g_dwriteFactory.GetAddressOf()));
    if (FAILED(hrF)) {
        Wh_Log(L"[LABEL] DWriteCreateFactory failed 0x%08lX", hrF);
        return false;
    }
    return true;
}

// DirectWrite path: word wrap + character-granularity U+2026 trimming natively,
// rendered through the bake context. Returns false if unavailable.
// `truncated`: true = collapsed label (ellipsis truncation, Explorer's
// DrawText DT_END_ELLIPSIS behavior); false = expanded label (whole name,
// wrap-only, overlong words char-wrap mid-word onto the next row, NO ellipsis
// anywhere — Explorer grows the rect to fit the full name).
static bool RenderGlyphSilhouetteD2D(const std::wstring& text, const LOGFONTW& lf, int w, int h, std::vector<uint8_t>& alphaOut, bool truncated) {
    if (!g_bakeDc || g_unloading.load(std::memory_order_relaxed)) return false;
    if (!EnsureDWriteFactory()) return false;

    // GDI probe FIRST: Explorer draws labels through GDI, so the metrics that
    // matter (em size, row advance) must come from GDI's realization of the
    // font — those track the real text for ANY font, including
    // Winaero-substituted ones where DWrite's own line metrics diverge from
    // what's actually on screen.
    HDC hdcTmp = CreateCompatibleDC(nullptr);
    HFONT hTmpFont = CreateFontIndirectW(&lf);
    HGDIOBJ oldFont = nullptr;
    TEXTMETRICW tm = {};
    bool haveTm = false;
    if (hdcTmp && hTmpFont) {
        oldFont = SelectObject(hdcTmp, hTmpFont);
        haveTm = (GetTextMetricsW(hdcTmp, &tm) != 0);
    }

    // The em size GDI ACTUALLY realized = tmHeight - tmInternalLeading (valid
    // for either sign of lfHeight). Trusting lfHeight alone assumes the font
    // mapper honored the request exactly — substitution + rounding can make
    // the realized em differ, and DWrite then renders visibly smaller/larger
    // than the real text. Deriving from the realized metrics self-calibrates.
    float fontSize = haveTm ? (float)(tm.tmHeight - tm.tmInternalLeading)
                   : (float)((lf.lfHeight < 0) ? -lf.lfHeight : lf.lfHeight);
    if (fontSize <= 0 || text.empty()) {
        if (oldFont) SelectObject(hdcTmp, oldFont);
        if (hTmpFont) DeleteObject(hTmpFont);
        if (hdcTmp) DeleteDC(hdcTmp);
        return false;
    }

    // One-shot diagnostics: dump the captured font + realized metrics so any
    // remaining size mismatch can be pinpointed against the real label.
    static LOGFONTW s_lastLoggedLf = {};
    static int s_fontLogCount = 0;
    if (s_fontLogCount < 20 && memcmp(&s_lastLoggedLf, &lf, sizeof(lf)) != 0) {
        s_lastLoggedLf = lf;
        s_fontLogCount++;
        Wh_Log(L"[LABEL] font face='%s' lfHeight=%d lfWidth=%d weight=%d | "
               L"tmHeight=%d intLead=%d extLead=%d aveW=%d | fontSize=%.2f canvas=%dx%d",
               lf.lfFaceName, lf.lfHeight, lf.lfWidth, lf.lfWeight,
               tm.tmHeight, tm.tmInternalLeading, tm.tmExternalLeading,
               tm.tmAveCharWidth, fontSize, w, h);
    }

    DWRITE_FONT_STYLE style = lf.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = g_dwriteFactory->CreateTextFormat(lf.lfFaceName, nullptr,
        (DWRITE_FONT_WEIGHT)lf.lfWeight, style, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &format);
    if (FAILED(hr)) {
        if (oldFont) SelectObject(hdcTmp, oldFont);
        if (hTmpFont) DeleteObject(hTmpFont);
        if (hdcTmp) DeleteDC(hdcTmp);
        return false;
    }

    // We drive line breaking ourselves (DWrite's breaker splits after hyphens
    // and never emergency-splits unbreakable tokens — neither matches
    // Explorer). DWrite is used purely for glyph rendering.
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // CENTER centers every line horizontally within the canvas, matching
    // Explorer's label rendering.
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Row advance: GDI's tmHeight + tmExternalLeading. The label rects
    // Explorer reports are the text block inflated by 1px top + 1px bottom:
    // measured on the same desktop, a 2-line label rect is 38px tall for BOTH
    // Segoe UI and a substituted font (2 x 18 + 2), and a single-line rect is
    // 20 (18 + 2) — font-independent padding, GDI-dependent advance. The
    // vertical centering below then reproduces Explorer's 1px top offset
    // automatically. (DWrite's own line spacing is a different number per
    // font and drifts the rows; that was tried and reverted.)
    int lineH = haveTm ? (tm.tmHeight + tm.tmExternalLeading) : 0;
    if (lineH <= 0) lineH = (int)std::lround(fontSize * 1.33f);
    int maxLines = (std::max)(1, (h + lineH / 2) / lineH);

    // --- GDI-BASED WORD WRAP & TRUNCATION ---
    std::vector<std::wstring> lines;

    // (hdcTmp / hTmpFont already created above for the metrics probe)

    // comctl32 uses InflateRect(-2,0) before drawing.
    // If this wraps too early/late, adjust this constant (w-2, w, etc).
    int availW = w - 4;
    if (availW < 10) availW = 10;

    // Clean text (remove trailing ellipsis/periods)
    std::wstring cleanText = text;
    while (!cleanText.empty() && (cleanText.back() == L'\u2026' || cleanText.back() == L'.')) {
        cleanText.pop_back();
    }

    if (hdcTmp) {
        // Explorer's label wrapper = DrawTextW(DT_WORDBREAK | DT_END_ELLIPSIS):
        //   - ONLY spaces are break points (hyphens, periods, digits are not —
        //     measured against Explorer: "2026-08-30qweq-qwe-asd" and
        //     "Screensh.ot2026-.08-31" each stay one unbreakable token).
        //   - Each line fills greedily with whole words.
        //   - A word wider than the whole line is emergency-broken at
        //     character granularity WITH an ellipsis, and the next line starts
        //     at the NEXT word, not the remainder ("Antigravity IDE" renders
        //     "Antigra…" / "IDE", never "Antigravit" / "y IDE").
        //   - Every line truncates independently, each with its own ellipsis.
        // GDI measurement keeps wrap/truncation points aligned with what
        // comctl32 actually draws. DT_NOPREFIX: '&' is a literal character in
        // listview labels — without it DrawTextW strips it as an accelerator
        // prefix and under-measures ("_milk&honey" measured as "_milkhoney").
        auto fitsOnLine = [&](const std::wstring& s) {
            RECT rc = {0, 0, availW, 1000};
            DrawTextW(hdcTmp, s.c_str(), (int)s.length(), &rc,
                      DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            return rc.right <= availW;
        };
        // Longest truncated form of `s` that fits one line, decided by GDI's
        // own DT_END_ELLIPSIS machinery (DT_MODIFYSTRING) — the exact engine
        // Explorer truncates with. Hand-measuring prefix+"…" disagrees by a
        // character on boundary cases ("Everything" -> "Everyt…" vs Win's
        // "Everyth...").
        auto ellipsize = [&](const std::wstring& s) {
            std::vector<wchar_t> buf(s.begin(), s.end());
            buf.resize(buf.size() + 4, L'\0');
            RECT rc = {0, 0, availW, 1000};
            DrawTextW(hdcTmp, buf.data(), (int)s.length(), &rc,
                      DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | DT_MODIFYSTRING | DT_END_ELLIPSIS);
            return std::wstring(buf.data());
        };
        std::vector<std::wstring> tokens;
        {
            std::wstring tok;
            for (wchar_t c : cleanText) {
                if (c == L' ') {
                    if (!tok.empty()) tokens.push_back(std::move(tok));
                    tok.clear();
                } else {
                    tok += c;
                }
            }
            if (!tok.empty()) tokens.push_back(std::move(tok));
        }
        size_t ti = 0;
        while (ti < tokens.size() && (int)lines.size() < maxLines) {
            bool lastLine = ((int)lines.size() + 1) >= maxLines;
            std::wstring cur = tokens[ti++];
            if (!truncated) {
                // --- EXPANDED MODE: whole name, wrap-only, no ellipsis ---
                if (!fitsOnLine(cur)) {
                    // Word wider than the line: char-fill each row and
                    // CONTINUE mid-word on the next (Explorer's expanded
                    // label keeps the entire name visible).
                    size_t pos = 0;
                    while (pos < cur.length() && (int)lines.size() < maxLines) {
                        size_t fit = 0;
                        for (size_t k = 1; pos + k <= cur.length(); ++k) {
                            if (!fitsOnLine(cur.substr(pos, k))) break;
                            fit = k;
                        }
                        if (fit == 0) fit = 1;
                        lines.push_back(cur.substr(pos, fit));
                        pos += fit;
                    }
                    continue;
                }
                // Greedy fill: add whole words while they fit.
                while (ti < tokens.size()) {
                    std::wstring cand = cur + L' ' + tokens[ti];
                    if (!fitsOnLine(cand)) break;
                    cur = std::move(cand);
                    ++ti;
                }
                lines.push_back(std::move(cur));
                continue;
            }
            if (!fitsOnLine(cur)) {
                // Whole word wider than the line: emergency break with
                // ellipsis; the word is consumed entirely.
                lines.push_back(ellipsize(cur));
                continue;
            }
            // Greedy fill: add whole words while they fit.
            while (ti < tokens.size()) {
                std::wstring cand = cur + L' ' + tokens[ti];
                if (!fitsOnLine(cand)) break;
                cur = std::move(cand);
                ++ti;
            }
            if (lastLine && ti < tokens.size()) {
                // Last allowed line: the remaining words stay on it, joined,
                // truncated with an ellipsis.
                std::wstring restLine = cur;
                for (; ti < tokens.size(); ++ti) restLine += L' ' + tokens[ti];
                if (!fitsOnLine(restLine)) restLine = ellipsize(restLine);
                lines.push_back(std::move(restLine));
                break;
            }
            lines.push_back(std::move(cur));
        }
    }

    if (oldFont) SelectObject(hdcTmp, oldFont);
    if (hTmpFont) DeleteObject(hTmpFont);
    if (hdcTmp) DeleteDC(hdcTmp);


    if (lines.empty()) lines.push_back(text.substr(0, 1));

    if (lines.size() > 1) {
        static std::atomic<int> s_wrapLogCount{0};
        if (s_wrapLogCount.fetch_add(1) < 300) {
            for (size_t i = 0; i < lines.size(); ++i)
                Wh_Log(L"[LABEL] wrap text='%s' line%zu='%s' maxLines=%d h=%d lineH=%d",
                       text.c_str(), i, lines[i].c_str(), maxLines, h, lineH);
        }
    }

    // Join pre-broken lines; NO_WRAP layout renders them verbatim.
    std::wstring joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) joined += L"\r\n";
        joined += lines[i];
    }

    // GDI-compatible layout: snaps glyph advances to integer pixels, exactly
    // like the GDI measurement above (and like Explorer's own label drawing).
    // The plain CreateTextLayout uses ideal sub-pixel advances, which render
    // slightly wider than measured on some fonts — the centered line then
    // overflows the canvas on BOTH sides and clips the first/last glyph
    // ("_milk&ho..." lost its underscore on the left).
    ComPtr<IDWriteTextLayout> layout;
    hr = g_dwriteFactory->CreateGdiCompatibleTextLayout(
        joined.c_str(), (UINT32)joined.length(), format.Get(),
        (float)w, (float)h, 1.0f /* pixelsPerDip: bake DC is 96 DPI */,
        nullptr, FALSE /* useGdiNatural: integer advances, like GDI aliased text */,
        &layout);
    if (FAILED(hr)) {
        hr = g_dwriteFactory->CreateTextLayout(joined.c_str(), (UINT32)joined.length(), format.Get(),
                                               (float)w, (float)h, &layout);
    }
    if (FAILED(hr)) return false;

    ComPtr<ID2D1Image> oldTarget;
    g_bakeDc->GetTarget(oldTarget.ReleaseAndGetAddressOf());

    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> bmp;
    g_bakeDc->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), nullptr, 0, &targetProps, bmp.ReleaseAndGetAddressOf());
    if (!bmp) {
        g_bakeDc->SetTarget(oldTarget.Get());
        return false;
    }

    g_bakeDc->SetTarget(bmp.Get());
    g_bakeDc->BeginDraw();
    g_bakeDc->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        ComPtr<ID2D1SolidColorBrush> brush;
        g_bakeDc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brush);
        if (brush) {
            // Vertical centering of the line block within the rect, like Explorer
            float blockH = (float)(lines.size() * lineH);
            float yOff = ((float)h - blockH) / 2.0f;
            if (yOff < 0) yOff = 0;
            g_bakeDc->DrawTextLayout(D2D1::Point2F(0, yOff), layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }
    if (!CheckEndDraw(g_bakeDc->EndDraw(), L"LabelGlyph.d2d")) {
        g_bakeDc->SetTarget(oldTarget.Get());
        return false;
    }
    g_bakeDc->SetTarget(oldTarget.Get());

    // Read back coverage through the staging texture (premultiplied white:
    // alpha channel == coverage)
    alphaOut.assign((size_t)w * h, 0);
    ComPtr<ID2D1Bitmap1> stg;
    if (w <= (int)STAGING_TEX_SIZE && h <= (int)STAGING_TEX_SIZE && g_stagingTexA) {
        stg = g_stagingTexA;
    } else {
        D2D1_BITMAP_PROPERTIES1 sp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        g_bakeDc->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), nullptr, 0, &sp, &stg);
    }
    bool ok = false;
    if (stg) {
        D2D1_POINT_2U pt = D2D1::Point2U(0, 0);
        D2D1_RECT_U rc = D2D1::RectU(0, 0, (UINT32)w, (UINT32)h);
        if (SUCCEEDED(stg->CopyFromBitmap(&pt, bmp.Get(), &rc))) {
            D2D1_MAPPED_RECT map;
            if (SUCCEEDED(stg->Map(D2D1_MAP_OPTIONS_READ, &map))) {
                for (int yy = 0; yy < h; ++yy)
                    for (int xx = 0; xx < w; ++xx)
                        alphaOut[yy * w + xx] = map.bits[yy * map.pitch + xx * 4 + 3];
                stg->Unmap();
                DilateAlpha(alphaOut, w, h);
                ok = true;
            }
        }
    }
    if (ok) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            Wh_Log(L"[LABEL] DWrite path ACTIVE, dpiScale=%f, fontSize=%f, canvas=%dx%d",
                   g_dpiScale.load(std::memory_order_relaxed), fontSize, w, h);
        }
    }
    return ok;
}

// [F2 GLYPH CACHE] RenderGlyphSilhouette is expensive (GDI metrics probe +
// O(n^2) wrap probes + DWrite layout/render/readback) and used to re-run for
// EVERY icon on EVERY deep pass. Cache the rendered alpha per
// (text, dims, truncation, font generation); the generation counter lives
// next to g_arrowGen. Entries are pure CPU alpha (a few KB each), bounded
// by a lazy clear.
struct GlyphCacheKey {
    uint64_t textHash;
    int w;
    int h;
    int fontHeight; // [MULTIMON v2.9.4] LOGFONT height: per-monitor font sizes (20px primary vs 16px secondary) must never share an entry
    uint32_t fontGen;
    bool truncated;
    bool operator==(const GlyphCacheKey& o) const {
        return textHash == o.textHash && w == o.w && h == o.h &&
               fontHeight == o.fontHeight &&
               fontGen == o.fontGen && truncated == o.truncated;
    }
};
struct GlyphCacheKeyHash {
    size_t operator()(const GlyphCacheKey& k) const {
        uint64_t h = 14695981039346656037ULL;
        h ^= k.textHash;            h *= 1099511628211ULL;
        h ^= (uint64_t)(uint32_t)k.w; h *= 1099511628211ULL;
        h ^= (uint64_t)(uint32_t)k.h; h *= 1099511628211ULL;
        h ^= (uint64_t)(int64_t)k.fontHeight; h *= 1099511628211ULL;
        h ^= (uint64_t)k.fontGen;   h *= 1099511628211ULL;
        h ^= (uint64_t)k.truncated; h *= 1099511628211ULL;
        return (size_t)h;
    }
};
static std::mutex s_glyphCacheMutex;
static std::unordered_map<GlyphCacheKey, std::vector<uint8_t>, GlyphCacheKeyHash> s_glyphCache;

static bool RenderGlyphSilhouette(const std::wstring& text, HFONT hFont, const LOGFONTW& lf, int w, int h, std::vector<uint8_t>& alphaOut, bool truncated) {
    if (text.empty() || !hFont || w <= 0 || h <= 0 || w > 512 || h > 512) return false;

    // [F2] Exact-reuse cache: same text, dims, truncation mode and font
    // generation => byte-identical alpha. Wrong predictions simply never hit.
    GlyphCacheKey key{};
    key.textHash = 14695981039346656037ULL;
    for (wchar_t c : text) {
        key.textHash ^= (uint64_t)c;
        key.textHash *= 1099511628211ULL;
    }
    key.w = w;
    key.h = h;
    key.fontHeight = lf.lfHeight; // [MULTIMON v2.9.4]
    key.fontGen = g_labelFontGen.load(std::memory_order_relaxed);
    key.truncated = truncated;
    {
        std::lock_guard<std::mutex> lock(s_glyphCacheMutex);
        auto it = s_glyphCache.find(key);
        if (it != s_glyphCache.end()) {
            alphaOut = it->second;
            return true;
        }
    }

    // DirectWrite only — same text engine Explorer uses; GDI metrics round
    // advances differently per string and can never fully converge. (The old
    // GDI DIB fallback renderer was removed in the ponytail audit: dwrite.dll
    // ships on every Win10+ target, and a D2D failure degrades to the solid
    // rectangle label via the composite path's letterShaped check.)
    // hFont stays as the "a real LV font was captured" validity gate.
    bool ok = RenderGlyphSilhouetteD2D(text, lf, w, h, alphaOut, truncated);
    if (ok) {
        std::lock_guard<std::mutex> lock(s_glyphCacheMutex);
        if (s_glyphCache.size() > 1024) s_glyphCache.clear(); // lazy bound
        s_glyphCache[key] = alphaOut;
    }
    return ok;
}

// ============================================================================
// Single source of truth for the expanded/collapsed visual state of an icon.
// An icon is "expanded" (long label shown) only while the desktop window is
// ACTIVE and the item is selected or focused. When the desktop deactivates,
// Explorer collapses EVERY label — even items that keep LVIS_SELECTED — so
// nothing counts as expanded while it's inactive. Everywhere else in the mod,
// use these two helpers instead of re-deriving the predicate.
// ============================================================================
inline bool IsIconExpanded(const IconSnapshot& snap) {
    bool desktopActive = g_isDesktopActive.load(std::memory_order_relaxed);
    if (!desktopActive) return false;
    return (snap.state & (LVIS_SELECTED | LVIS_FOCUSED)) != 0;
}

// Picks the combo hash matching the item's current visual state,
// falling back to the other state's hash if the preferred one is missing.
inline uint64_t PickComboHash(const IconSnapshot& snap) {
    bool expanded = IsIconExpanded(snap);
    uint64_t targetHash = expanded ? snap.comboHash_e : snap.comboHash_c;
    if (targetHash == 0) {
        targetHash = expanded ? snap.comboHash_c : snap.comboHash_e;
    }
    return targetHash;
}

// --- PERSISTED ICON SHAPES (cold-boot extraction skip) -----------------------
// Saves the shape store + a cacheKey->shape index for every icon that is not
// a generic placeholder / pending thumbnail. At cold boot the index lets
// ProcessSnapshotData adopt the persisted shape instead of re-extracting the
// icon from the shell — the cacheKey already validates file identity, times,
// icon location, DPI and zoom, so an index hit means the shape is current.
static std::wstring IconShapesFilePath() {
    WCHAR pathBuffer[MAX_PATH];
    if (Wh_GetModStoragePath(pathBuffer, MAX_PATH) <= 0) return L"";
    return std::wstring(pathBuffer) + L"\\icon_shapes.bin";
}

struct IconShapesHeader { uint32_t magic; uint32_t version; uint32_t shapeCount; uint32_t indexCount; };
struct IconShapeRecord { uint64_t hash; uint32_t w, h, thumbW, thumbH, alphaLen; };

// Raw file parse shared by LoadIconShapes (boot) and SaveIconShapes
// (cumulative merge). Returns false if the file is absent/corrupt.
struct PersistedShapeBlob { uint64_t hash; uint32_t w, h, thumbW, thumbH; std::vector<uint8_t> alpha; };
static bool ReadIconShapeFile(std::vector<PersistedShapeBlob>& outShapes,
                              std::unordered_map<uint64_t, uint64_t>& outIndex) {
    std::wstring filePath = IconShapesFilePath();
    if (filePath.empty()) return false;
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    IconShapesHeader header = {};
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &header, sizeof(header), &bytesRead, nullptr) ||
        bytesRead != sizeof(header) || header.magic != 0x49435348 || header.version != 1 ||
        header.shapeCount > 10000 || header.indexCount > 100000) {
        CloseHandle(hFile);
        return false;
    }

    for (uint32_t i = 0; i < header.shapeCount; ++i) {
        IconShapeRecord rec = {};
        if (!ReadFile(hFile, &rec, sizeof(rec), &bytesRead, nullptr) || bytesRead != sizeof(rec)) break;
        if (rec.w == 0 || rec.h == 0 || rec.alphaLen != (uint32_t)rec.w * rec.h) break;
        std::vector<uint8_t> alpha(rec.alphaLen);
        if (!ReadFile(hFile, alpha.data(), rec.alphaLen, &bytesRead, nullptr) ||
            bytesRead != rec.alphaLen) break;
        outShapes.push_back({rec.hash, rec.w, rec.h, rec.thumbW, rec.thumbH, std::move(alpha)});
    }
    for (uint32_t i = 0; i < header.indexCount; ++i) {
        uint64_t pair[2] = {};
        if (!ReadFile(hFile, pair, sizeof(pair), &bytesRead, nullptr) || bytesRead != sizeof(pair)) break;
        if (pair[0] != 0 && pair[1] != 0) outIndex[pair[0]] = pair[1];
    }
    CloseHandle(hFile);
    return true;
}

static void LoadIconShapes() {
    std::vector<PersistedShapeBlob> fileShapes;
    std::unordered_map<uint64_t, uint64_t> loadedIndex;
    if (!ReadIconShapeFile(fileShapes, loadedIndex)) return;

    {
        std::lock_guard<std::mutex> uLock(g_uniqueShapesMutex);
        for (auto& blob : fileShapes) {
            // Skip shapes already known (the dedup store may have been warmed).
            bool exists = false;
            for (const auto& shape : g_uniqueShapes) {
                if (shape.hash == blob.hash) { exists = true; break; }
            }
            if (exists) continue;
            g_uniqueShapes.push_back({blob.hash, blob.w, blob.h, std::move(blob.alpha), blob.thumbW, blob.thumbH});
        }
    }

    {
        std::lock_guard<std::mutex> pLock(g_persistedShapeIndexMutex);
        g_persistedShapeIndex = std::move(loadedIndex);
    }
    Wh_Log(L"[SHAPES] Loaded %zu persisted shapes, %zu index entries",
           g_uniqueShapes.size(), g_persistedShapeIndex.size());
}

// [POISON-GUARD v2.2.7] A shape matching a generic-doc baseline IS the
// placeholder itself. The pixel tripwire guards EXTRACTION, but the
// persisted-shape adoption path (below) trusted icon_shapes.bin blindly —
// and the file had been poisoned once (boot 2026-11-05: 54 index keys
// pointed at the placeholder shape), so every boot re-adopted the blank-doc
// silhouette and nothing could fire: adoption never extracts pixels. Both
// ends now compare content against g_genericFileBaselines.
static bool IsGenericDocShape(const std::vector<uint8_t>& alpha, UINT w, UINT h) {
    for (const auto& b : g_genericFileBaselines) {
        if (b.width != w || b.height != h ||
            b.alphaData.size() != alpha.size()) continue;
        // [POISON-GUARD v2.2.15] The persisted poison is NOT byte-identical
        // to today's baseline: measured 45/2601 flipped boundary pixels
        // (1.7%) — a slightly different rendering of the same doc page (the
        // same drift that let it slip past the pixel tripwire originally).
        // Measured separation (v2.2.13/14 diags, SAD + threshold probes):
        // poison = 45 flips (SAD 900), nearest legit shape = 80 flips (SAD
        // 1600), real icons 260+ flips. Boundary placed at 2.5% of pixels.
        // False positives are cheap (one honest re-extraction); false
        // negatives resurrect the blank-doc loop.
        size_t flips = 0;
        for (size_t i = 0; i < alpha.size(); ++i)
            if (alpha[i] != b.alphaData[i]) ++flips;
        if (flips * 40 <= alpha.size()) return true;   // <= 2.5% flipped
    }
    return false;
}

static void SaveIconShapes(const std::vector<IconSnapshot>& manifest) {
    std::wstring filePath = IconShapesFilePath();
    if (filePath.empty()) return;

    // 1. Build the index from the manifest, collecting the used shape hashes.
    //    Generic placeholders / pending thumbnails are excluded so they
    //    re-extract next boot exactly as today.
    std::unordered_map<uint64_t, uint64_t> index;   // cacheKey -> shapeHash
    std::unordered_set<uint64_t> usedShapes;
    {
        std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
        for (const auto& snap : manifest) {
            if (snap.cacheKey == 0 || snap.wasGeneric || snap.isPendingThumbnail ||
                snap.thumbnailRetryCount > 0) continue;
            auto it = g_iconHashCache.find(snap.cacheKey);
            if (it == g_iconHashCache.end() || it->second == 0) continue;
            index[snap.cacheKey] = it->second;
            usedShapes.insert(it->second);
        }
    }
    if (index.empty()) return;

    // 1b. [ZOOMFIX v2.2.2] CUMULATIVE merge: the old file holds every
    // previously-visited zoom level's keys; replacing it on every save (old
    // behavior) left only the current zoom in the index, so revisiting a
    // zoom could never adopt its persisted shape and had to re-extract —
    // whose pixels can differ from the original bake (ImageList churn),
    // rekeying every combo hash and defeating the multi-zoom disk cache
    // (proven 2026-11-03). Fresh entries win over stale ones per key.
    std::vector<PersistedShapeBlob> oldFileShapes;
    std::unordered_map<uint64_t, uint64_t> oldFileIndex;
    bool hadOldFile = ReadIconShapeFile(oldFileShapes, oldFileIndex);
    if (hadOldFile) {
        for (const auto& pair : oldFileIndex) {
            if (index.find(pair.first) == index.end()) {
                index[pair.first] = pair.second;
                usedShapes.insert(pair.second);
            }
        }
    }

    // 2. Snapshot the used shapes (CPU alpha; GPU assets are rebuilt on adopt).
    struct ShapeBlob {
        uint64_t hash; uint32_t w, h, thumbW, thumbH; std::vector<uint8_t> alpha;
    };
    std::vector<ShapeBlob> shapes;
    {
        std::lock_guard<std::mutex> uLock(g_uniqueShapesMutex);
        for (const auto& shape : g_uniqueShapes) {
            if (usedShapes.find(shape.hash) == usedShapes.end()) continue;
            shapes.push_back({shape.hash, shape.width, shape.height,
                              shape.thumbW, shape.thumbH, shape.alphaData});
        }
    }
    if (hadOldFile) {
        // Shapes evicted from the dedup store (per-zoom GC) but still
        // referenced by merged index entries: keep their alpha from the file.
        std::unordered_set<uint64_t> haveShape;
        for (const auto& s : shapes) haveShape.insert(s.hash);
        for (auto& blob : oldFileShapes) {
            if (usedShapes.find(blob.hash) != usedShapes.end() &&
                haveShape.find(blob.hash) == haveShape.end()) {
                shapes.push_back({blob.hash, blob.w, blob.h, blob.thumbW, blob.thumbH, std::move(blob.alpha)});
                haveShape.insert(blob.hash);
            }
        }
        // [POISON-ORPHAN v2.6.3] A merged index entry whose shape bytes exist
        // in NEITHER g_uniqueShapes NOR oldFileShapes is adoption-impossible:
        // nothing can ever read its shape, so it is a zombie that still gets
        // written to disk (the 2026-11-06 forensic: REJECT-POISONED fired on
        // a file that parsed clean — orphans survived door 2 because it only
        // tests shapes present in the save snapshot). Drop them here.
        size_t orphansErased = 0;
        for (auto it = index.begin(); it != index.end(); ) {
            if (haveShape.count(it->second) == 0) { it = index.erase(it); ++orphansErased; }
            else ++it;
        }
        if (orphansErased > 0)
            DiagLog(L"[POISON-ORPHAN] save: dropped %zu adoption-impossible index key(s)", orphansErased);
    }
    // [POISON-GUARD v2.3.0] Drop every index entry whose shape is the
    // generic-doc placeholder itself (fresh entries AND cumulative-merge
    // imports) — one bad write must not be immortal. The file self-heals
    // on this save; affected keys re-extract once and re-persist honestly.
    {
        std::unordered_set<uint64_t> poisonedShapes;
        for (const auto& s : shapes)
            if (IsGenericDocShape(s.alpha, s.w, s.h)) poisonedShapes.insert(s.hash);
        if (!poisonedShapes.empty()) {
            size_t erasedKeys = 0;
            for (auto it = index.begin(); it != index.end(); ) {
                if (poisonedShapes.count(it->second)) { it = index.erase(it); ++erasedKeys; }
                else ++it;
            }
            shapes.erase(std::remove_if(shapes.begin(), shapes.end(),
                                        [&](const ShapeBlob& s) {
                                            return poisonedShapes.count(s.hash) != 0;
                                        }),
                         shapes.end());
            DiagLog(L"[POISON-GUARD] save: dropped %zu poisoned shape(s) / %zu index keys",
                    poisonedShapes.size(), erasedKeys);
        }
    }
    if (shapes.empty()) return;

    // 3. Write temp + atomic rename (same pattern as the composite cache).
    std::wstring tempPath = filePath + L".tmp";
    HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    IconShapesHeader header = { 0x49435348, 1, (uint32_t)shapes.size(), (uint32_t)index.size() };
    DWORD written = 0;
    bool ok = WriteFile(hFile, &header, sizeof(header), &written, nullptr);

    for (const auto& s : shapes) {
        if (!ok) break;
        IconShapeRecord rec = { s.hash, s.w, s.h, s.thumbW, s.thumbH, (uint32_t)s.alpha.size() };
        ok = WriteFile(hFile, &rec, sizeof(rec), &written, nullptr);
        if (ok) ok = WriteFile(hFile, s.alpha.data(), (DWORD)s.alpha.size(), &written, nullptr);
    }
    for (const auto& pair : index) {
        if (!ok) break;
        uint64_t pairBuf[2] = { pair.first, pair.second };
        ok = WriteFile(hFile, pairBuf, sizeof(pairBuf), &written, nullptr);
    }
    CloseHandle(hFile);

    if (ok) {
        MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING);
        // Keep the in-RAM index in sync so within-session zoom revisits can
        // adopt without waiting for the next boot.
        std::lock_guard<std::mutex> pLock(g_persistedShapeIndexMutex);
        g_persistedShapeIndex = std::move(index);
    } else {
        DeleteFileW(tempPath.c_str());
    }
}

void ProcessSnapshotData(const std::vector<RawListItem>& rawItems, const std::wstring& userDesktop, const std::wstring& publicDesktop,
                         HFONT labelFont, const LOGFONTW& labelLogfont) {
    ProcessSnapshotData(rawItems, userDesktop, publicDesktop, labelFont, labelLogfont, false);
}

void ProcessSnapshotData(const std::vector<RawListItem>& rawItems, const std::wstring& userDesktop, const std::wstring& publicDesktop,
                         HFONT labelFont, const LOGFONTW& labelLogfont, bool mergeIntoExisting) {
    // "Show desktop icons" off: nothing to process, and publishing stale
    // geometry would let the renderers cut holes for invisible icons.
    if (g_desktopIconsHidden.load(std::memory_order_relaxed)) return;
    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    if (!hLV || !IsWindow(hLV) || g_unloading) return;

    Settings localSettings = SnapshotSettings();

    ULONGLONG tSnapshotStart = GetTickCount64();

    // Scan Desktop folders into memory ONCE to eliminate thousands of GetFileAttributes disk syscalls
    std::unordered_map<std::wstring, std::wstring> desktopFileMap;
    // Attributes + last-write time captured by the same scan, keyed by
    // LOWERCASED FULL PATH so same-named items in the two desktop folders
    // can't shadow each other.
    std::unordered_map<std::wstring, WIN32_FILE_ATTRIBUTE_DATA> desktopFileMeta;
    auto scanDesktopFolder = [&](const std::wstring& folderPath) {
        if (folderPath.empty()) return;
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((folderPath + L"\\*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == L'.') continue;
                std::wstring name = fd.cFileName;
                std::wstring fullPath = folderPath + L"\\" + name;

                // Map full name
                std::wstring nameLower = name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
                desktopFileMap[nameLower] = fullPath;

                // Capture metadata for exact-path lookups
                std::wstring pathLower = fullPath;
                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::towlower);
                WIN32_FILE_ATTRIBUTE_DATA& meta = desktopFileMeta[pathLower];
                meta.dwFileAttributes = fd.dwFileAttributes;
                meta.ftLastWriteTime = fd.ftLastWriteTime;

                // Map base name without extension (for Windows hidden extension mode)
                size_t dotPos = nameLower.find_last_of(L'.');
                if (dotPos != std::wstring::npos) {
                    std::wstring baseLower = nameLower.substr(0, dotPos);
                    desktopFileMap[baseLower] = fullPath;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    };
    scanDesktopFolder(userDesktop);
    scanDesktopFolder(publicDesktop);

    // Attribute/time lookups served from the scan snapshot; automatically
    // falls back to the real syscall for paths outside the scanned folders.
    auto lookupMeta = [&](const std::wstring& fullPath, WIN32_FILE_ATTRIBUTE_DATA& out) -> bool {
        std::wstring pathLower = fullPath;
        std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::towlower);
        auto itMeta = desktopFileMeta.find(pathLower);
        if (itMeta == desktopFileMeta.end()) return false;
        out = itMeta->second;
        return true;
    };
    auto fastGetAttributes = [&](const std::wstring& fullPath) -> DWORD {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (lookupMeta(fullPath, data)) return data.dwFileAttributes;
        return GetFileAttributesW(fullPath.c_str());
    };
    auto fastGetFileTime = [&](const std::wstring& fullPath, FILETIME* outFt) -> bool {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (lookupMeta(fullPath, data)) {
            *outFt = data.ftLastWriteTime;
            return true;
        }
        WIN32_FILE_ATTRIBUTE_DATA sysData = {};
        if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &sysData)) {
            *outFt = sysData.ftLastWriteTime;
            return true;
        }
        return false;
    };

    std::vector<IconSnapshot> newSnapshots;
    std::vector<std::wstring> localWatchedPaths;
    std::unordered_map<int, uint64_t> passIndexMap;

    // [MERGE MODE — fast path] The fast path hands us ONE raw item. A full
    // rebuild would publish a 1-icon desktop (the v2.1.2 reset bug), so in
    // merge mode newSnapshots starts as a copy of the live list and the loop
    // below REPLACES the matching slot instead of appending. The publish at
    // the end then swaps in the full list with just that one entry refreshed.
    // All the per-item work (hashes, silhouette, disk cache) is identical.
    bool mergeBail = false; // identity mismatch -> fall back to a full pass
    if (mergeIntoExisting) {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        newSnapshots = g_iconSnapshots;
    }
    if (mergeIntoExisting && newSnapshots.empty()) {
        mergeBail = true; // nothing live yet — the full pass must run first
    }

    bool iconSizeChanged = false;
    int currentIconSize = g_cachedIconSize.load(std::memory_order_relaxed);
    if (currentIconSize != g_lastSnapshotIconSize.load(std::memory_order_relaxed)) {
        iconSizeChanged = true;
        g_lastSnapshotIconSize.store(currentIconSize, std::memory_order_relaxed);
    }

    int countSlow = 0, countFast = 0;
    int countDiskSkip = 0; // [P1.1] combos carried from cache instead of re-rendered

    // [PERF-AUDIT v2.2.6] pass wall-clock + per-icon phase timing.
    ULONGLONG passStartTick = GetTickCount64();
    DiagLog(L"[PASS-START] items=%zu merge=%d", rawItems.size(), mergeIntoExisting ? 1 : 0);

    newSnapshots.reserve(rawItems.size());

    // [PHASE 3 v2.4.5] Loop split (ZOOM_CACHE_REDESIGN.md §12): the per-icon
    // body divides into DECISIONS (identity resolution, file/lnk resolution,
    // cacheKey salt chain, force-reextract eviction, persisted-shape
    // adoption) and EXECUTION (extraction, combo build). The decide loop
    // below records everything the execute stage needs in one PassDecision
    // per raw item — including the old-state fields the retry ladder reads,
    // because the execute loop no longer resolves oldSnapPtr. decisions[i]
    // corresponds to rawItems[i] one-to-one (the merge-bail return is the
    // only early exit, and it fires before any decision is recorded).
    struct PassDecision {
        IconSnapshot snap;
        int mergeSlot = -1;          // merge mode: newSnapshots slot to replace
        bool fastPath = false;
        bool forceReextract = false; // DECIDED value — pool tasks must carry it, never guess it
        bool adoptedFromDisk = false;
        bool hasOld = false;         // an old snapshot resolved for this item
        bool oldWasGeneric = false;
        bool oldWasEverNonGeneric = false;
        uint32_t oldArrowGen = 0;    // F1 skip guard's old-state input
        int oldRetryCount = 0;
    };
    std::vector<PassDecision> decisions;
    decisions.reserve(rawItems.size());

    uint64_t dpiSalt = (uint64_t)(g_dpiScale * 1000.0f);

    // Preserve previously computed combo hashes so we don't lose the other state's hash
    std::unordered_map<uint64_t, uint64_t> oldComboHashes_c;
    std::unordered_map<uint64_t, uint64_t> oldComboHashes_e;
    std::unordered_map<uint64_t, RECT> oldComboRects_c;
    std::unordered_map<uint64_t, RECT> oldComboRects_e;
    std::unordered_map<std::wstring, IconSnapshot> oldSnapshotsByName;

    // [NAMECOLL] Diagnostics (v2.1.16): identity keys that appeared more than
    // once in the maps below — same display name + same lnk/item class means
    // two visible items share one map slot, and every lookup for either twin
    // resolves to the same (last-iterated) entry. Logging only — the maps'
    // semantics are unchanged.
    std::unordered_set<std::wstring> collidedNameKeys;
    // [NAMECOLL FIX v2.1.17] All candidates for collided keys, so the lookup
    // can tell same-named twins apart instead of silently keeping only the
    // last-iterated one. Populated only for keys that actually collide.
    std::unordered_map<std::wstring, std::vector<IconSnapshot>> oldSnapshotsMulti;
    std::vector<std::wstring> nameCollLogs;   // formatted under lock, flushed after it

    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);

        for (const auto& oldSnap : g_iconSnapshots) {
                if (oldSnap.comboHash_c != 0) { oldComboHashes_c[oldSnap.cacheKey] = oldSnap.comboHash_c; oldComboRects_c[oldSnap.cacheKey] = oldSnap.comboRect_c; }
                if (oldSnap.comboHash_e != 0) { oldComboHashes_e[oldSnap.cacheKey] = oldSnap.comboHash_e; oldComboRects_e[oldSnap.cacheKey] = oldSnap.comboRect_e; }
            if (!oldSnap.displayName.empty()) {
                // Key old snapshots by display name AND shortcut status so duplicate names don't collide
                std::wstring oldKey = oldSnap.displayName + (oldSnap.overlayIndex > 0 ? L":lnk" : L":item");
                auto nameCollIt = oldSnapshotsByName.find(oldKey);
                if (nameCollIt != oldSnapshotsByName.end()) {
                    collidedNameKeys.insert(oldKey);
                    auto multiIt = oldSnapshotsMulti.find(oldKey);
                    if (multiIt == oldSnapshotsMulti.end())
                        oldSnapshotsMulti.emplace(oldKey, std::vector<IconSnapshot>{ nameCollIt->second });
                    oldSnapshotsMulti[oldKey].push_back(oldSnap);
                    wchar_t collLine[512];
                    swprintf_s(collLine, L"[NAMECOLL] OLD-MAP key '%s' x2: kept pos(%ld,%ld) state=%u | dropped pos(%ld,%ld) state=%u",
                               oldKey.c_str(),
                               nameCollIt->second.iconRect.left, nameCollIt->second.iconRect.top,
                               nameCollIt->second.state,
                               oldSnap.iconRect.left, oldSnap.iconRect.top, oldSnap.state);
                    nameCollLogs.push_back(collLine);
                }
                oldSnapshotsByName[oldKey] = oldSnap;
            }
        }
    }
    for (const auto& collLine : nameCollLogs) DiagLog(L"%s", collLine.c_str());

    for (const auto& raw : rawItems) {
        if (g_unloading.load(std::memory_order_relaxed)) return;

        // [MERGE MODE] Identity check against the live slot we're replacing:
        // if the desktop changed since the snapshot list was built (different
        // item at this index), don't merge — queue a full pass instead.
        if (mergeIntoExisting) {
            if (raw.index < 0 || raw.index >= (int)newSnapshots.size()) {
                mergeBail = true;
            } else {
                const IconSnapshot& live = newSnapshots[raw.index];
                bool sameName = (live.displayName == raw.text);
                bool sameOverlay = (live.overlayIndex == raw.overlayIndex);
                if (!sameName || !sameOverlay) mergeBail = true;
            }
            if (mergeBail) {
                DiagLog(L"[FAST] merge bail (identity mismatch at %d) -> full pass", raw.index);
                if (g_overlayWnd && !g_snapshotUpdatePending.load(std::memory_order_relaxed)) {
                    g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                    PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
                }
                return;
            }
        }

        IconSnapshot snap = {};
        snap.imageIndex = raw.imageIndex;
        snap.lvIndex = raw.index;
        snap.state = raw.state;
        snap.overlayIndex = raw.overlayIndex;
        // Only mark as dragged during a REAL drag. g_draggedItemIndex records the
        // last pressed item (set on every plain select-click) and must never be
        // used on its own here, otherwise deep passes flag the selected icon as
        // dragged and it loses its overlay/shadow until the next rebuild.
        snap.isDragged = g_isDraggingItem.load(std::memory_order_relaxed) &&
                         (raw.index == g_draggedItemIndex.load(std::memory_order_relaxed));
        snap.iconRect = raw.iconRect;
        snap.boundsRect = raw.boundsRect;
        snap.displayName = raw.text;

        snap.isFolder = false;
        snap.isThumbnail = false;
        snap.hasCustomIcon = false;

        snap.labelRect = raw.labelRect;

        // [PHASE 3 v2.4.5] Decision outputs, hoisted so the decision-record
        // block below (outside the slow branch) can read them. The fast path
        // leaves both false — it never extracts.
        bool forceReextract = false;
        bool adoptedFromDisk = false;

        std::wstring currentLookupKey = raw.text + (raw.overlayIndex > 0 ? L":lnk" : L":item");
        // [NAMECOLL FIX v2.1.17] Identity resolution. For unique keys this is
        // exactly the old map lookup (same entry as before). For collided keys
        // (same display name + same lnk/item class — e.g. the same shortcut in
        // both the user and the public desktop), the map slot alone cannot tell
        // the twins apart, so resolve against ALL candidates of that key:
        // exact LV index match first, then position-nearest (LV order shifts on
        // add/remove; on-screen positions are what Explorer keeps stable), then
        // first as last resort.
        const IconSnapshot* oldSnapPtr = nullptr;
        {
            auto mapIt = oldSnapshotsByName.find(currentLookupKey);
            if (mapIt != oldSnapshotsByName.end()) {
                if (!collidedNameKeys.count(currentLookupKey)) {
                    oldSnapPtr = &mapIt->second;
                } else {
                    auto multiIt = oldSnapshotsMulti.find(currentLookupKey);
                    if (multiIt != oldSnapshotsMulti.end() && !multiIt->second.empty()) {
                        int resolvedHow = 0; // 1=exact index, 2=nearest pos, 3=first
                        for (const auto& cand : multiIt->second) {
                            if (cand.lvIndex >= 0 && cand.lvIndex == raw.index) {
                                oldSnapPtr = &cand; resolvedHow = 1; break;
                            }
                        }
                        if (!oldSnapPtr) {
                            long bestDist = 0;
                            const IconSnapshot* best = nullptr;
                            for (const auto& cand : multiIt->second) {
                                long dx = (long)cand.iconRect.left - (long)raw.iconRect.left;
                                long dy = (long)cand.iconRect.top - (long)raw.iconRect.top;
                                long dist = dx * dx + dy * dy;
                                if (!best || dist < bestDist) { bestDist = dist; best = &cand; }
                            }
                            oldSnapPtr = best; resolvedHow = 2;
                        }
                        if (!oldSnapPtr) { oldSnapPtr = &multiIt->second[0]; resolvedHow = 3; }
                        DiagLog(L"[NAMECOLL] RESOLVE '%s' idx=%d -> old lvIdx=%d pos(%ld,%ld) how=%s",
                                raw.text.c_str(), raw.index, oldSnapPtr->lvIndex,
                                oldSnapPtr->iconRect.left, oldSnapPtr->iconRect.top,
                                resolvedHow == 1 ? L"exact-index" :
                                resolvedHow == 2 ? L"nearest-pos" : L"first");
                    }
                    if (!oldSnapPtr) oldSnapPtr = &mapIt->second;
                }
            }
        }

        // --- COLLAPSE HOLD: carry across deep passes / engage on transition ---
        if (snap.state & LVIS_SELECTED) {
            snap.pendingCollapse = false;
        } else if (oldSnapPtr && (oldSnapPtr->state & LVIS_SELECTED) && !IsIconExpanded(snap)) {
            // Selection lost between passes AND label will collapse: hold the
            // selection-sized cutout. Ghost-focus excluded, same as the poll.
            snap.pendingCollapse = true;
            snap.pendingCollapseStart = GetTickCount64();
            snap.heldCollapseRect = snap.boundsRect;
            if (collidedNameKeys.count(currentLookupKey)) {
                DiagLog(L"[NAMECOLL] HOLD-ENGAGE '%s' this idx=%d pos(%ld,%ld) via old idx=%d pos(%ld,%ld)%s",
                        raw.text.c_str(), raw.index, raw.iconRect.left, raw.iconRect.top,
                        oldSnapPtr->lvIndex, oldSnapPtr->iconRect.left, oldSnapPtr->iconRect.top,
                        (oldSnapPtr->lvIndex != raw.index) ? L"  <== WRONG TWIN" : L"");
            }
        } else if (oldSnapPtr) {
            snap.pendingCollapse = oldSnapPtr->pendingCollapse;
            snap.pendingCollapseStart = oldSnapPtr->pendingCollapseStart;
            snap.heldCollapseRect = oldSnapPtr->heldCollapseRect;
            if (snap.pendingCollapse &&
                (GetTickCount64() - snap.pendingCollapseStart > COLLAPSE_HOLD_MS)) {
                snap.pendingCollapse = false;
            }
        }

        bool fastPath = false;

        if (!raw.text.empty() && oldSnapPtr && oldSnapPtr->imageIndex == raw.imageIndex) {
            const IconSnapshot& oldSnap = *oldSnapPtr;
            // Check if label rect size changed (collapsed <-> expanded transition)
            int oldLabelW = oldSnap.labelRect.right - oldSnap.labelRect.left;
            int oldLabelH = oldSnap.labelRect.bottom - oldSnap.labelRect.top;
            int newLabelW = raw.labelRect.right - raw.labelRect.left;
            int newLabelH = raw.labelRect.bottom - raw.labelRect.top;
            bool labelSizeChanged = (oldLabelW != newLabelW) || (oldLabelH != newLabelH);

            bool rectUnchanged = (oldSnap.iconRect.right - oldSnap.iconRect.left) == (raw.iconRect.right - raw.iconRect.left) &&
                                (oldSnap.iconRect.bottom - oldSnap.iconRect.top) == (raw.iconRect.bottom - raw.iconRect.top) &&
                                !labelSizeChanged;

            bool textureExists = false;
            if (rectUnchanged && !iconSizeChanged) {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                textureExists = (g_iconCache.find(oldSnap.cacheKey) != g_iconCache.end());
            }

            // DISABLE FAST PATH FOR FOLDERS, THUMBNAILS, SHORTCUTS, AND GENERIC PLACEHOLDERS:
            // Generic placeholders must NEVER enter the permanent fast path until resolved!
            if (!iconSizeChanged && rectUnchanged && textureExists &&
                !oldSnap.isFolder && !oldSnap.isThumbnail && oldSnap.overlayIndex == 0 &&
                !oldSnap.hasCustomIcon && !oldSnap.wasGeneric && !oldSnap.isPendingThumbnail) {
                snap.isFolder = oldSnap.isFolder;
                snap.isThumbnail = oldSnap.isThumbnail;
                snap.hasCustomIcon = oldSnap.hasCustomIcon;
                snap.isFolderWithSubfolders = oldSnap.isFolderWithSubfolders;
                snap.filePath = oldSnap.filePath;
                snap.cacheKey = oldSnap.cacheKey;
                snap.wasGeneric = oldSnap.wasGeneric;
                snap.wasEverNonGeneric = oldSnap.wasEverNonGeneric;
                fastPath = true;
                if (collidedNameKeys.count(currentLookupKey)) {
                    DiagLog(L"[NAMECOLL] FASTPATH '%s' this idx=%d pos(%ld,%ld) adopts old idx=%d pos(%ld,%ld) key=%llu%s",
                            raw.text.c_str(), raw.index, raw.iconRect.left, raw.iconRect.top,
                            oldSnap.lvIndex, oldSnap.iconRect.left, oldSnap.iconRect.top,
                            (unsigned long long)oldSnap.cacheKey,
                            (oldSnap.lvIndex != raw.index) ? L"  <== WRONG TWIN" : L"");
                }
                if (oldSnap.thumbnailRetryCount >= 6) {
                    Wh_Log(L"[GENERIC] FAST-PATH on retry-exhausted item '%s'", raw.text.c_str());
                }
            }
        }

        if (!fastPath) {
            countSlow++;
            // --- SLOW PATH (Heavy Disk I/O & COM Calls) ---
            std::wstring itemName = raw.text;
            std::wstring fullPathUser, fullPathPublic;
            DWORD attribUser = INVALID_FILE_ATTRIBUTES;
            DWORD attribPublic = INVALID_FILE_ATTRIBUTES;

            std::wstring resolvedLnkPath;
            std::wstring resolvedLnkTarget;
            std::wstring resolvedLnkIconLocation;

            if (!itemName.empty()) {
             fullPathUser = userDesktop + L"\\" + itemName;
             fullPathPublic = publicDesktop + L"\\" + itemName;
             attribUser = fastGetAttributes(fullPathUser);
             attribPublic = fastGetAttributes(fullPathPublic);

                bool isDir = false;
                    std::wstring validPath = L"";
                    if (attribUser != INVALID_FILE_ATTRIBUTES && (attribUser & FILE_ATTRIBUTE_DIRECTORY)) {
                        isDir = true;
                        validPath = fullPathUser;
                    } else if (attribPublic != INVALID_FILE_ATTRIBUTES && (attribPublic & FILE_ATTRIBUTE_DIRECTORY)) {
                        isDir = true;
                        validPath = fullPathPublic;
                    }

                // Name conflict fix: If a folder and a shortcut have the exact same display name,
                // and this item has a shortcut overlay, prefer treating it as the shortcut.
                if (isDir && snap.overlayIndex > 0) {
                    std::wstring testLnkUser = fullPathUser + L".lnk";
                    std::wstring testLnkPublic = fullPathPublic + L".lnk";
                    if (fastGetAttributes(testLnkUser) != INVALID_FILE_ATTRIBUTES ||
                        fastGetAttributes(testLnkPublic) != INVALID_FILE_ATTRIBUTES) {
                        isDir = false;
                        validPath.clear();
                    }
                }

                if (!isDir) {
                    std::wstring lnkPath = L"";
                    size_t lenName = itemName.length();
                    if (lenName >= 4 && _wcsicmp(itemName.c_str() + lenName - 4, L".lnk") == 0) {
                        if (attribUser != INVALID_FILE_ATTRIBUTES) lnkPath = fullPathUser;
                        else if (attribPublic != INVALID_FILE_ATTRIBUTES) lnkPath = fullPathPublic;
                    } else if (raw.overlayIndex > 0) {
                        // ONLY search for .lnk if Windows actually marked this item as a shortcut
                        std::wstring testPathUser = fullPathUser + L".lnk";
                        std::wstring testPathPublic = fullPathPublic + L".lnk";
                        if (fastGetAttributes(testPathUser) != INVALID_FILE_ATTRIBUTES) lnkPath = testPathUser;
                        else if (fastGetAttributes(testPathPublic) != INVALID_FILE_ATTRIBUTES) lnkPath = testPathPublic;
                    }
                    if (!lnkPath.empty()) {
                        resolvedLnkPath = lnkPath;
                        ComPtr<IShellLinkW> psl;
                        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl)))) {
                            ComPtr<IPersistFile> ppf;
                            if (SUCCEEDED(psl.As(&ppf))) {
                                if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                                    WCHAR targetPath[MAX_PATH] = {0};
                                    if (SUCCEEDED(psl->GetPath(targetPath, MAX_PATH, nullptr, SLGP_UNCPRIORITY))) {
                                        resolvedLnkTarget = targetPath;
                                        DWORD targetAttrib = GetFileAttributesW(targetPath);
                                        if (targetAttrib != INVALID_FILE_ATTRIBUTES && (targetAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
                                            isDir = true;
                                            validPath = targetPath;
                                        }
                                    }

                                    WCHAR iconLocBuf[MAX_PATH] = {0};
                                    int iconIdxBuf = 0;
                                    if (SUCCEEDED(psl->GetIconLocation(iconLocBuf, MAX_PATH, &iconIdxBuf))) {
                                        resolvedLnkIconLocation = iconLocBuf;
                                        if (iconLocBuf[0] != 0) {
                                            WCHAR expLoc[MAX_PATH] = {0};
                                            ExpandEnvironmentStringsW(iconLocBuf, expLoc, MAX_PATH);
                                            snap.customIconPath = (expLoc[0] != 0) ? expLoc : iconLocBuf;
                                            snap.customIconIndex = iconIdxBuf;
                                            snap.hasCustomIcon = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (isDir) {
                    snap.isFolder = true;
                    // If this is a shortcut pointing to a folder, and the shortcut had its own custom icon, keep it!
                    if (!snap.hasCustomIcon) {
                        std::wstring iniPath = validPath + L"\\desktop.ini";
                        DWORD iniAttrib = fastGetAttributes(iniPath);
                        if (iniAttrib != INVALID_FILE_ATTRIBUTES && !(iniAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
                            WCHAR iconResBuf[MAX_PATH] = {0};
                            GetPrivateProfileStringW(L".ShellClassInfo", L"IconResource", L"", iconResBuf, MAX_PATH, iniPath.c_str());
                            if (iconResBuf[0] != 0) {
                                std::wstring resStr = iconResBuf;
                                int iconIdx = 0;
                                size_t comma = resStr.find_last_of(L',');
                                if (comma != std::wstring::npos) {
                                    try {
                                        iconIdx = std::stoi(resStr.substr(comma + 1));
                                    } catch (...) {
                                        iconIdx = 0;
                                    }
                                    resStr = resStr.substr(0, comma);
                                }
                                WCHAR expPath[MAX_PATH] = {0};
                                ExpandEnvironmentStringsW(resStr.c_str(), expPath, MAX_PATH);
                                snap.customIconPath = (expPath[0] != 0) ? expPath : resStr;
                                snap.customIconIndex = iconIdx;
                                snap.hasCustomIcon = true;
                            } else {
                                WCHAR iconFileBuf[MAX_PATH] = {0};
                                GetPrivateProfileStringW(L".ShellClassInfo", L"IconFile", L"", iconFileBuf, MAX_PATH, iniPath.c_str());
                                if (iconFileBuf[0] != 0) {
                                    int iconIdx = GetPrivateProfileIntW(L".ShellClassInfo", L"IconIndex", 0, iniPath.c_str());
                                    WCHAR expPath[MAX_PATH] = {0};
                                    ExpandEnvironmentStringsW(iconFileBuf, expPath, MAX_PATH);
                                    snap.customIconPath = (expPath[0] != 0) ? expPath : iconFileBuf;
                                    snap.customIconIndex = iconIdx;
                                    snap.hasCustomIcon = true;
                                }
                            }
                        }
                    }
                    if (!snap.hasCustomIcon) {
                        if (!validPath.empty()) localWatchedPaths.push_back(validPath);
                        bool hasSubfolders = false;
                        bool hasFiles = false;
                        WIN32_FIND_DATAW findData;
                        HANDLE hFind = FindFirstFileW((validPath + L"\\*").c_str(), &findData);
                        if (hFind != INVALID_HANDLE_VALUE) {
                            do {
                                std::wstring fileName = findData.cFileName;
                                if (fileName == L"." || fileName == L".." || fileName == L"desktop.ini") continue;
                                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) hasSubfolders = true;
                                else { hasFiles = true; break; }
                            } while (FindNextFileW(hFind, &findData));
                            FindClose(hFind);
                        }
                        // Non-empty folder (contains either files or subfolders) -> Stuffed Folder
                        snap.isFolderWithSubfolders = (hasSubfolders || hasFiles);
                    }
                } else {
                    auto isMediaExt = [&](const std::wstring& path) -> bool {
                        size_t dotPos = path.find_last_of(L'.');
                        if (dotPos == std::wstring::npos) return false;
                        std::wstring ext = path.substr(dotPos);
                        for (const auto& mExt : kImageExts) {
                            if (_wcsicmp(ext.c_str(), mExt) == 0) return true;
                        }
                        for (const auto& mExt : kMediaContainerExts) {
                            if (_wcsicmp(ext.c_str(), mExt) == 0) return true;
                        }
                        return false;
                    };

                    std::wstring validFilePath = L"";

                    // 1. Check shortcut target
                    if (!resolvedLnkTarget.empty()) {
                        validFilePath = resolvedLnkTarget;
                        if (isMediaExt(resolvedLnkTarget)) {
                            snap.isThumbnail = true;
                        }
                    }

                    // 2. Direct Canonical Shell Parsing Name resolution for real files on disk
                    if (validFilePath.empty() && !raw.parsingName.empty() && raw.parsingName.rfind(L"::{", 0) != 0) {
                        validFilePath = raw.parsingName;
                        if (isMediaExt(raw.parsingName)) {
                            snap.isThumbnail = true;
                        }
                    }

                    // 3. Fallback in-memory map lookup
                    if (validFilePath.empty()) {
                        std::wstring nameLower = itemName;
                        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
                        auto itFound = desktopFileMap.find(nameLower);
                        if (itFound != desktopFileMap.end()) {
                            validFilePath = itFound->second;
                            if (isMediaExt(itFound->second)) {
                                snap.isThumbnail = true;
                            }
                        }
                    }
                    snap.filePath = validFilePath;

                    // 4. Direct Resolution for Virtual Desktop CLSIDs via Canonical Parsing Name (::{GUID})
                    // Works regardless of what the icon is named ("PC", "Lightning McQueen", "Acest PC")
                    if (!isDir && !raw.parsingName.empty() && raw.parsingName.rfind(L"::{", 0) == 0) {
                        bool isRb = (raw.parsingName.find(L"645FF040-5081-101B-9F08-00AA002F954E") != std::wstring::npos);
                        bool isFull = false;
                        if (isRb) {
                            SHQUERYRBINFO rbi = { sizeof(rbi) };
                            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &rbi))) {
                                isFull = (rbi.i64NumItems > 0);
                            }
                        }
                        std::wstring clsidIconPath;
                        int clsidIconIdx = 0;
                        if (GetClsidDefaultIcon(raw.parsingName.c_str(), clsidIconPath, clsidIconIdx, isFull)) {
                            snap.customIconPath = clsidIconPath;
                            snap.customIconIndex = clsidIconIdx;
                            snap.hasCustomIcon = true;
                        }
                    }
                }
            }
            uint64_t cacheKey = 14695981039346656037ULL;
            std::wstring anchorPath;
            if (!resolvedLnkPath.empty()) {
                anchorPath = resolvedLnkPath;
            } else if (attribUser != INVALID_FILE_ATTRIBUTES) {
                anchorPath = fullPathUser;
            } else if (attribPublic != INVALID_FILE_ATTRIBUTES) {
                anchorPath = fullPathPublic;
            } else if (snap.isThumbnail && !snap.filePath.empty()) {
                anchorPath = snap.filePath;
            } else {
                anchorPath = raw.text;
            }

            for (wchar_t c : anchorPath) {
                cacheKey ^= c;
                cacheKey *= 1099511628211ULL;
            }

            // SALT WITH FILE TIMESTAMP
            FILETIME anchorFt = {};
            bool gotAnchorData = false;
            if (!anchorPath.empty()) {
                gotAnchorData = fastGetFileTime(anchorPath, &anchorFt);
            }
            if (gotAnchorData) {
                cacheKey ^= (uint64_t)anchorFt.dwLowDateTime;
                cacheKey *= 1099511628211ULL;
                cacheKey ^= (uint64_t)anchorFt.dwHighDateTime;
                cacheKey *= 1099511628211ULL;

                // If it's a folder, also hash the desktop.ini timestamp if it exists
                if (snap.isFolder) {
                    std::wstring iniPath = anchorPath + L"\\desktop.ini";
                    FILETIME iniFt = {};
                    if (fastGetFileTime(iniPath, &iniFt)) {
                        cacheKey ^= (uint64_t)iniFt.dwLowDateTime;
                        cacheKey *= 1099511628211ULL;
                        cacheKey ^= (uint64_t)iniFt.dwHighDateTime;
                        cacheKey *= 1099511628211ULL;
                    }
                }
            }

            // Salt with shortcut target path & target write time if present
            if (!resolvedLnkTarget.empty()) {
                for (wchar_t c : resolvedLnkTarget) {
                    cacheKey ^= c;
                    cacheKey *= 1099511628211ULL;
                }
                FILETIME targetFt = {};
                if (fastGetFileTime(resolvedLnkTarget, &targetFt)) {
                    cacheKey ^= (uint64_t)targetFt.dwLowDateTime;
                    cacheKey *= 1099511628211ULL;
                    cacheKey ^= (uint64_t)targetFt.dwHighDateTime;
                    cacheKey *= 1099511628211ULL;
                }
            }

            // Salt with custom icon location, index & file write time if present (Folder or Shortcut)
            if (snap.hasCustomIcon && !snap.customIconPath.empty()) {
                for (wchar_t c : snap.customIconPath) {
                    cacheKey ^= c;
                    cacheKey *= 1099511628211ULL;
                }
                cacheKey ^= (uint64_t)snap.customIconIndex;
                cacheKey *= 1099511628211ULL;

                FILETIME iconLocFt = {};
                if (fastGetFileTime(snap.customIconPath, &iconLocFt)) {
                    cacheKey ^= (uint64_t)iconLocFt.dwLowDateTime;
                    cacheKey *= 1099511628211ULL;
                    cacheKey ^= (uint64_t)iconLocFt.dwHighDateTime;
                    cacheKey *= 1099511628211ULL;
                }
            }

            // Always salt with imageIndex so image-index changes refresh the cache
            cacheKey ^= (uint64_t)snap.imageIndex;
            cacheKey *= 1099511628211ULL;
            cacheKey ^= dpiSalt;
            cacheKey *= 1099511628211ULL;
            int rectW = snap.iconRect.right - snap.iconRect.left;
            int rectH = snap.iconRect.bottom - snap.iconRect.top;
            uint64_t rectSalt = ((uint64_t)rectW << 16) | (uint64_t)rectH;
            cacheKey ^= rectSalt;
            cacheKey *= 1099511628211ULL;
            if (snap.isFolder && !snap.hasCustomIcon) {
                uint64_t stateSalt = snap.isFolderWithSubfolders ? 0x5354554646454421ULL : 0x5354414E44415244ULL;
                cacheKey ^= stateSalt;
                cacheKey *= 1099511628211ULL;
            }

        bool wasPendingThumbnail = (oldSnapPtr &&
                                    oldSnapPtr->isPendingThumbnail &&
                                    oldSnapPtr->thumbnailRetryCount < 6);

        // Force fresh re-extraction if the item was previously holding a generic placeholder!
        // [PHASE 3 v2.4.5] forceReextract hoisted above the fast-path check.
        forceReextract = (oldSnapPtr &&
                            (oldSnapPtr->cacheKey != cacheKey ||
                            oldSnapPtr->imageIndex != snap.imageIndex ||
                            oldSnapPtr->wasGeneric ||
                            wasPendingThumbnail));
        // [FLASH-PROBE v2.9.21] forceReextract erases the old combo's RAM
        // entries (8433) on EVERY cause — key change was only one suspect.
        // Log every fire with its cause bits; persistent folder re-extraction
        // via the pixel-generic detector is the current suspect (folders only).
        if (forceReextract) {
            if (oldSnapPtr) {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_iconCache.erase(oldSnapPtr->cacheKey);
                std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
                g_iconHashCache.erase(oldSnapPtr->cacheKey);
                std::lock_guard<std::mutex> comboLock(g_comboShadowCacheMutex);
                if (oldSnapPtr->comboHash_c != 0) g_comboShadowCache.erase(oldSnapPtr->comboHash_c);
                if (oldSnapPtr->comboHash_e != 0) g_comboShadowCache.erase(oldSnapPtr->comboHash_e);
            }
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_iconCache.erase(cacheKey);
        }

        snap.cacheKey = cacheKey;

        // --- PERSISTED SHAPE ADOPTION (cold-boot extraction skip) ---
        // The persisted index is key-validated (file identity + times + icon
        // location + dpi/zoom), and the save side excludes generic placeholders
        // / pending thumbnails, so an index hit means the shape is trustworthy
        // without re-extracting from the shell. forceReextract (key change,
        // image change, wasGeneric, pending retry) keeps its authority here.
        // [PHASE 3 v2.4.5] adoptedFromDisk hoisted above the fast-path check.
        // [ZOOMFIX v2.2.2] Adoption used to be skipped whenever forceReextract
        // was set — and a zoom change ALWAYS sets it (the rect salt changes the
        // key), forcing a fresh extraction whose pixels can differ from the
        // original bake (ImageList churn at the zoom switch). That rekeys every
        // combo hash at the revisited zoom and defeats the multi-zoom disk
        // cache: same cacheKey + same label dims, different combo hash
        // (proven in diag_log 2026-11-03). The persisted index is its own
        // validator — entries exist only for validated non-generic shapes
        // under the exact key (file identity + times + icon location + dpi/
        // zoom + rect) — so consult it unless the PREVIOUS state was a
        // placeholder/pending-thumbnail suspect. Identity changes still fail
        // closed: file edits, timestamp bumps and imageIndex changes alter the
        // key itself, and a changed key simply has no index entry.
        bool adoptionBlocked = (oldSnapPtr && (oldSnapPtr->wasGeneric || wasPendingThumbnail));
        if (!adoptionBlocked) {
            uint64_t persistedShapeHash = 0;
            {
                std::lock_guard<std::mutex> pLock(g_persistedShapeIndexMutex);
                auto pit = g_persistedShapeIndex.find(cacheKey);
                if (pit != g_persistedShapeIndex.end()) persistedShapeHash = pit->second;
            }
            if (persistedShapeHash != 0) {
                std::lock_guard<std::mutex> uLock(g_uniqueShapesMutex);
                for (const auto& shape : g_uniqueShapes) {
                    if (shape.hash != persistedShapeHash) continue;
                    // [POISON-GUARD v2.2.7] Never adopt a shape that IS the
                    // generic-doc placeholder — fall through to extraction,
                    // where the pixel tripwire + retry ladder keep authority.
                    if (IsGenericDocShape(shape.alphaData, shape.width, shape.height)) {
                        DiagLog(L"[STAKEOUT] REJECT-POISONED key=%llu shape=%llu (generic-doc silhouette in persisted index)",
                                (unsigned long long)cacheKey, (unsigned long long)persistedShapeHash);
                        break;
                    }
                    ComPtr<ID2D1Bitmap> bmp;
                    auto bmpIt = g_shapeToBitmap.find(shape.hash);
                    if (bmpIt != g_shapeToBitmap.end()) bmp = bmpIt->second;
                    if (!bmp) {
                        // Rebuild the shared GPU asset from the persisted alpha.
                        std::vector<BYTE> bgra(shape.alphaData.size() * 4);
                        for (size_t i = 0; i < shape.alphaData.size(); ++i) bgra[i * 4 + 3] = shape.alphaData[i];
                        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                        std::lock_guard<std::mutex> dcLock(g_resourceDcMutex);
                        if (SUCCEEDED(g_resourceDc->CreateBitmap(D2D1::SizeU(shape.width, shape.height), bgra.data(), shape.width * 4, &props, &bmp))) {
                            g_shapeToBitmap[shape.hash] = bmp;
                        }
                    }
                    if (bmp) {
                        {
                            std::lock_guard<std::mutex> cLock(g_cacheMutex);
                            g_iconCache[cacheKey] = bmp;
                        }
                        {
                            std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
                            g_iconHashCache[cacheKey] = persistedShapeHash;
                        }
                        adoptedFromDisk = true;
                        DiagLog(L"[STAKEOUT] adopted persisted shape for key=%llu (shape %llu)",
                                (unsigned long long)cacheKey, (unsigned long long)persistedShapeHash);
                    }
                    break;
                }
            }
        }

        // [PHASE 3 v2.4.5] Extraction moved to the execute stage below
        // (extraction pool + serial finish). Everything from here to the
        // end of the slow branch is pure decision/recording.

        // [PHASE 3 v2.4.5] The STAKEOUT trail, generic tracking, retry ladder
        // and overlay cache fill moved to the execute stage — they need the
        // extraction result (isFallback), which now comes from the pool.

        }
        if (fastPath) {
            countFast++;
            // --- FAST PATH (Instant Geometry Update) ---
            if (snap.overlayIndex > 0) {
                CacheOverlayBitmap(snap.overlayIndex);
            }
            // Preserve the watched folder paths so the Folder Watcher doesn't lose track of them
            if (snap.isFolder && !snap.hasCustomIcon && !snap.filePath.empty()) {
                localWatchedPaths.push_back(snap.filePath);
            }
        }

        // [PHASE 3 v2.4.5] Decide stage complete for this item: record it.
        {
            PassDecision dec;
            dec.snap = std::move(snap);
            dec.mergeSlot = mergeIntoExisting ? raw.index : -1;
            dec.fastPath = fastPath;
            dec.forceReextract = forceReextract;
            dec.adoptedFromDisk = adoptedFromDisk;
            dec.hasOld = (oldSnapPtr != nullptr);
            if (oldSnapPtr) {
                dec.oldWasGeneric = oldSnapPtr->wasGeneric;
                dec.oldWasEverNonGeneric = oldSnapPtr->wasEverNonGeneric;
                dec.oldArrowGen = oldSnapPtr->arrowGen;
                dec.oldRetryCount = oldSnapPtr->thumbnailRetryCount;
            }
            decisions.push_back(std::move(dec));
        }
    }

    DiagLog(L"[PASS-DECIDE] %zu decisions in %llu ms", decisions.size(),
            GetTickCount64() - passStartTick);

    // ======================================================================
    // [PHASE 3 v2.4.5] EXECUTE STAGE — extraction pool + serial finish.
    // The decide stage above ran the whole per-icon decision chain
    // serially. What remains is per-icon EXECUTION: extraction (poolable —
    // leaf-mutex writes only, hLV touched solely behind the
    // g_cachedImageList atomic) and the combo build (stays serial: GPU
    // publishes + RAM mirror writes + publish-order stability).
    // §12 rule: pool tasks carry the DECIDED forceReextract, never a guess
    // — a force=true icon extracted with force=false would poison the
    // cache with pixels the decision chain explicitly rejected.
    // ======================================================================
    struct ExtrResult {
        bool ran = false;        // a pool thread completed this item
        bool isFallback = false;
        ULONGLONG extrMs = 0;
    };
    std::vector<ExtrResult> extrResults(decisions.size());
    std::vector<size_t> poolTasks;
    poolTasks.reserve(decisions.size());
    for (size_t di = 0; di < decisions.size(); ++di) {
        if (!decisions[di].fastPath && !decisions[di].adoptedFromDisk)
            poolTasks.push_back(di);
    }

    // Pool gate: pool threads pass hLV=nullptr (documented rule: pool
    // threads never touch the LV). CacheIconBitmap only needs hLV when the
    // g_cachedImageList atomic is NOT serving — so if the atomic is null,
    // extraction stays on this worker thread, byte-identical to pre-2.4.5.
    const unsigned int kExtractionPoolThreads = 3; // Doc ruling 2026-11-05: 2-4
    bool poolOk = !poolTasks.empty() &&
                  (g_cachedImageList.load(std::memory_order_relaxed) != nullptr) &&
                  !g_unloading.load(std::memory_order_relaxed);

    if (poolOk) {
        ULONGLONG tPool0 = GetTickCount64();
        // Eager WIC factory init BEFORE the pool: CacheIconBitmap lazy-inits
        // it with no lock — N pool threads would race CoCreateInstance on
        // the first pooled pass and leak all but one factory. Serial here
        // makes the race structurally dead.
        if (!g_workerWicFactory) {
            CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_workerWicFactory));
        }

        std::atomic<size_t> nextTask(0);
        std::vector<std::unordered_map<int, uint64_t>> tlMaps(kExtractionPoolThreads);
        std::vector<std::thread> pool;
        unsigned int nThreads = (unsigned int)std::min<size_t>(kExtractionPoolThreads, poolTasks.size());
        pool.reserve(nThreads);
        for (unsigned int slot = 0; slot < nThreads; ++slot) {
            pool.emplace_back([&, slot]() {
                try {
                    // MTA per pool thread — the worker's own apartment model.
                    bool comHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
                    for (;;) {
                        size_t ti = nextTask.fetch_add(1, std::memory_order_relaxed);
                        if (ti >= poolTasks.size()) break;
                        PassDecision& dec = decisions[poolTasks[ti]];
                        IconSnapshot& snap = dec.snap;
                        ULONGLONG tExtr0 = GetTickCount64();
                        bool fb = false;
                        CacheIconBitmap(nullptr, snap.imageIndex, snap.cacheKey,
                                        snap.isFolder, snap.isThumbnail, snap.hasCustomIcon,
                                        snap.isFolderWithSubfolders, snap.filePath,
                                        tlMaps[slot], dec.forceReextract, &fb,
                                        snap.customIconPath, snap.customIconIndex);
                        ExtrResult& r = extrResults[poolTasks[ti]];
                        r.ran = true;
                        r.isFallback = fb;
                        r.extrMs = GetTickCount64() - tExtr0;
                    }
                    if (comHere) CoUninitialize();
                } catch (const std::exception& e) {
                    Wh_Log(L"[CRASH-GUARD] extraction pool exception: %hs", e.what());
                } catch (...) {
                    Wh_Log(L"[CRASH-GUARD] extraction pool unknown exception");
                }
            });
        }
        for (auto& t : pool) t.join();
        // Thread-local free-ride maps merge post-join (§12: imageIndex is
        // unique per icon in a pass, so per-thread maps merge cleanly).
        for (auto& tl : tlMaps)
            for (const auto& entry : tl)
                passIndexMap[entry.first] = entry.second;
        DiagLog(L"[PASS-POOL] %zu pooled extractions on %u threads in %llu ms", poolTasks.size(), nThreads, GetTickCount64() - tPool0);
    }

    // SERIAL FINISH — decision order preserved for the publish (the
    // index-aligned stale-state guard trusts the staged order). Extraction
    // results come from the pool when it ran; the direct CacheIconBitmap
    // call below is the fallback for anything the pool did not complete.
    for (size_t di = 0; di < decisions.size(); ++di) {
        if (g_unloading.load(std::memory_order_relaxed)) return;

        PassDecision& dec = decisions[di];
        const RawListItem& raw = rawItems[di];
        IconSnapshot& snap = dec.snap;
        bool fastPath = dec.fastPath;

        // [PERF-AUDIT v2.2.6] per-icon phase timers (combo runs below).
        ULONGLONG extrMs = 0, tCombo0 = 0;

        if (!fastPath) {
            bool isFallback = false;
            if (!dec.adoptedFromDisk) {
                if (poolOk && extrResults[di].ran) {
                    isFallback = extrResults[di].isFallback;
                    extrMs = extrResults[di].extrMs;
                } else {
                    ULONGLONG tExtr0 = GetTickCount64();
                    CacheIconBitmap(hLV, snap.imageIndex, snap.cacheKey, snap.isFolder, snap.isThumbnail, snap.hasCustomIcon, snap.isFolderWithSubfolders, snap.filePath, passIndexMap, dec.forceReextract, &isFallback, snap.customIconPath, snap.customIconIndex);
                    extrMs = GetTickCount64() - tExtr0;
                }
            }

            // [STAKEOUT] Blank-doc hunt: the full decision trail for every
            // slow-path icon. Fired only on the slow path (fast-path icons are
            // known-good), so normal operation stays quiet and an install/update
            // writes the whole story: what the shell served, what we flagged,
            // what we adopted, what we forced.
            {
                int stakeoutGenericIdx = GetGenericDocSysIconIndex();
                DiagLog(L"[STAKEOUT] '%s' imgIdx=%d genericIdx=%d fallback=%d adopt=%d forced=%d br=%d key=%llu path='%s'",
                        raw.text.c_str(), snap.imageIndex, stakeoutGenericIdx,
                        isFallback ? 1 : 0, dec.adoptedFromDisk ? 1 : 0,
                        dec.forceReextract ? 1 : 0,
                        g_baselinesReady ? 1 : 0,
                        (unsigned long long)snap.cacheKey, snap.filePath.c_str());
            }

            // Update progressive generic placeholder tracking
            snap.wasGeneric = isFallback;
            if (!isFallback) {
                snap.wasEverNonGeneric = true;
            }
            if (isFallback || (dec.hasOld && dec.oldWasGeneric)) {
                Wh_Log(L"[GENERIC] fallback=%d '%s' imgIdx=%d key=0x%llX",
                       isFallback ? 1 : 0, raw.text.c_str(), snap.imageIndex,
                       (unsigned long long)snap.cacheKey);
            }

            // --- 6-STEP PROGRESSIVE SELF-HEALING LADDER ---
            // 200ms -> 500ms -> 1s -> 2s -> 4s -> 8s (Total ~16s coverage for slow machines)
            static const UINT s_retryDelays[] = { 200, 500, 1000, 2000, 4000, 8000 };
            const int kMaxRetrySteps = (int)(sizeof(s_retryDelays) / sizeof(s_retryDelays[0]));

            int prevRetries = dec.hasOld ? dec.oldRetryCount : 0;
            bool inGracePeriod = (!dec.hasOld) || g_startupGracePeriod.load(std::memory_order_relaxed);
            bool shouldRetry = isFallback && ((dec.hasOld && dec.oldWasEverNonGeneric) || inGracePeriod);

            if (shouldRetry && prevRetries < kMaxRetrySteps) {
                snap.isPendingThumbnail = true;
                snap.thumbnailRetryCount = prevRetries + 1;
                UINT delayMs = s_retryDelays[prevRetries];
                Wh_Log(L"[GENERIC] retry #%d in %ums '%s'", prevRetries + 1, delayMs, raw.text.c_str());
                DiagLog(L"[STAKEOUT] retry #%d in %ums '%s'", prevRetries + 1, delayMs, raw.text.c_str());
                ArmSnapRetry(delayMs);
            } else {
                if (shouldRetry && prevRetries >= kMaxRetrySteps) {
                    Wh_Log(L"[GENERIC] RETRY EXHAUSTED '%s' (still fallback)", raw.text.c_str());
                    DiagLog(L"[STAKEOUT] RETRY EXHAUSTED '%s' (still fallback)", raw.text.c_str());
                }
                snap.isPendingThumbnail = false;
                snap.thumbnailRetryCount = (prevRetries >= kMaxRetrySteps) ? kMaxRetrySteps : 0;
            }

            if (snap.overlayIndex > 0) {
                CacheOverlayBitmap(snap.overlayIndex);
            }
        }

        snap.comboHash_c = oldComboHashes_c[snap.cacheKey];
        snap.comboHash_e = oldComboHashes_e[snap.cacheKey];
        snap.comboRect_c = oldComboRects_c[snap.cacheKey];
        snap.comboRect_e = oldComboRects_e[snap.cacheKey];

        // --- NEW PIPELINE: COMPOSITE p-c_1 AND p-c_2_c ---
        tCombo0 = GetTickCount64();
     uint64_t baseShapeHash = 0;
     {
         std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
         auto it = g_iconHashCache.find(snap.cacheKey);
         if (it != g_iconHashCache.end()) baseShapeHash = it->second;
     }


        SilhouetteData baseSil;
        bool gotBaseSil = false;
        if (baseShapeHash != 0) {
            std::lock_guard<std::mutex> uLock(g_uniqueShapesMutex);
            for (const auto& shape : g_uniqueShapes) {
                if (shape.hash == baseShapeHash) {
                    baseSil.width = shape.width;
                    baseSil.height = shape.height;
                    baseSil.alphaData = shape.alphaData;
                    gotBaseSil = true;
                    break;
                }
            }
        }
        if (!gotBaseSil) {
            static int s_noSilLogged = 0;
            if (s_noSilLogged++ < 10)
                Wh_Log(L"[SNAPSHOT] no silhouette for '%s' (baseShapeHash=%llu, cacheKey=%llu) - combo not updated this pass",
                       raw.text.c_str(), (unsigned long long)baseShapeHash, (unsigned long long)snap.cacheKey);
        }

        // [F1 SKIP GUARD] If the composite for the CURRENT state was built
        // with exactly the inputs a rebuild would use (shape, overlay, label
        // dims, text, shadow profile, arrow generation), the rebuild would
        // produce identical bytes — skip the pc1 copy, pc2 blits, label
        // render, and cache writes entirely. Before this guard every icon on
        // every pass re-did all of it (164k [COMBO] rebuilds in one diag
        // session; ~15-30 ms each was the deep-pass floor).
        bool skipComboRebuild = false;
        if (gotBaseSil && baseShapeHash != 0) {
            int skipLabelW = snap.labelRect.right - snap.labelRect.left;
            int skipLabelH = snap.labelRect.bottom - snap.labelRect.top;
            UINT skipComboLabelW = (skipLabelW > 0) ? (UINT)skipLabelW : 0;
            UINT skipComboLabelH = (skipLabelH > 0) ? (UINT)skipLabelH : 0;
            RECT skipCombined = snap.iconRect;
            if (skipComboLabelW > 0 && skipComboLabelH > 0) {
                UnionRect(&skipCombined, &snap.iconRect, &snap.labelRect);
            }
            snap.combinedRect = skipCombined;

            bool skipExpanded = IsIconExpanded(snap);
            uint64_t skipProfile = GetShadowBakeHash(localSettings);
            uint64_t skipTextHash = 14695981039346656037ULL;
            for (wchar_t c : raw.text) {
                skipTextHash ^= (uint64_t)c;
                skipTextHash *= 1099511628211ULL;
            }
            UINT skipScalePermille = (UINT)std::lround(GetItemMonitorScale(g_overlayWnd, snap.iconRect) * 1000.0f); // [MULTIMON v2.9.4]
            uint64_t skipCandidate = GenerateComboHash(baseShapeHash, snap.overlayIndex,
                                                       skipComboLabelW, skipComboLabelH,
                                                       skipProfile, skipTextHash, skipScalePermille);
            uint64_t skipStored = skipExpanded ? snap.comboHash_e : snap.comboHash_c;
            // [PHASE 3 v2.4.5] oldSnapPtr no longer exists here — the F1
            // guard reads the decision-carried old state instead.
            if (dec.hasOld && skipStored != 0 && skipCandidate == skipStored &&
                dec.oldArrowGen == g_arrowGen.load(std::memory_order_relaxed)) {
                // [FLASH-FIX v2.9.18] F1 assumed the RAM entry survives as long
                // as the stored hash matches. It doesn't: the SILPERSIST budget
                // sweep erases inactive silhouette-only entries past 1200 (183
                // items x 2 states x visited zoom levels overflows it), and a
                // later F1 skip then publishes hash-bearing snapshots against a
                // RAM hole — the mask white-rects until preflight repopulates
                // (~750ms window = Doc's selected-folders flash; 200x
                // [FLASH] white-rect ramEntry=0 diskHas=1 receipts). Verify RAM
                // presence; a hole falls through to the disk-hit branch, which
                // republishes the silhouette from the mirror on this same pass.
                bool ramHasBmp = false;
                {
                    std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
                    auto rit = g_comboShadowCache.find(skipCandidate);
                    ramHasBmp = (rit != g_comboShadowCache.end() && rit->second.sourceBmp != nullptr);
                }
                if (ramHasBmp) {
                    skipComboRebuild = true;
                    snap.arrowGen = dec.oldArrowGen;
                }
            }
            // [DISK-HIT SKIP P1.1] The F1 guard needs session-previous state
            // (oldSnapPtr + a stored combo hash) — exactly what a zoom revisit
            // does NOT have (fresh snapshots, comboHash 0 under the new zoom's
            // keys), which is why revisit legs re-rendered every combo. Let
            // the RAM composite mirror prove the content instead: same hash
            // inputs + a populated pc2 for the current state + an entry baked
            // by the CURRENT arrow generation. Disk-loaded entries carry
            // arrowGen=0 (unknown) and conservatively rebuild.
            if (!skipComboRebuild && skipCandidate != 0) {
                CompositeData cached;
                if (g_diskCache.GetComposite(skipCandidate, cached)) {
                    const SilhouetteData& curPc2 = skipExpanded ? cached.pc2_e : cached.pc2_c;
                    bool pc2ok = curPc2.width > 0 && curPc2.height > 0 &&
                                 curPc2.alphaData.size() == (size_t)curPc2.width * curPc2.height;
                    bool genOk = cached.arrowGen != 0 &&
                                 cached.arrowGen == g_arrowGen.load(std::memory_order_relaxed);
                    if (pc2ok && genOk) {
                        skipComboRebuild = true;
                        snap.arrowGen = cached.arrowGen;
                        if (skipExpanded) {
                            snap.comboHash_e = skipCandidate;
                            snap.comboRect_e = snap.labelRect; // geometry the cached composite was built from
                        } else {
                            snap.comboHash_c = skipCandidate;
                            snap.comboRect_c = snap.labelRect;
                        }
                        ++countDiskSkip;
                        static int s_diskSkipLogged = 0;
                        if (s_diskSkipLogged++ < 8)
                            DiagLog(L"[COMBO] '%s': carried %c composite from cache (disk-hit skip)",
                                    raw.text.c_str(), skipExpanded ? L'E' : L'C');
                        // [SILFIRST on skip, v2.3.4] The build branch's SILFIRST
                        // publish doesn't run on a carried combo — and the GC
                        // may have evicted this hash's sourceBmp since the
                        // composite was last live. Without it the mask has
                        // nothing to stamp: shadows render first and the
                        // overlays sit on rectangles until the pre-sweep
                        // rebuild (the regression Doc saw on-screen). Publish
                        // the silhouette from the CACHED pc2 the same way the
                        // build branch publishes its fresh one.
                        PublishSilhouetteFromPc2(skipCandidate, curPc2);
                    }
                }
            }
        }

        if (skipComboRebuild) {
            // Combo bytes unchanged; hashes, rects and combinedRect are
            // already in place on snap. Nothing to do.
        } else if (gotBaseSil) {
            // 1. Calculate unified bounding box
            int labelW = snap.labelRect.right - snap.labelRect.left;
            int labelH = snap.labelRect.bottom - snap.labelRect.top;

            UINT comboLabelW = (labelW > 0) ? labelW : 0;
            UINT comboLabelH = (labelH > 0) ? labelH : 0;

            RECT combinedRect = snap.iconRect;
            if (comboLabelW > 0 && comboLabelH > 0) {
                UnionRect(&combinedRect, &snap.iconRect, &snap.labelRect);
            }
            snap.combinedRect = combinedRect;

            int totalW = combinedRect.right - combinedRect.left;
            int totalH = combinedRect.bottom - combinedRect.top;
            if (totalW <= 0) totalW = 1;
            if (totalH <= 0) totalH = 1;

            // 2. Base Icon Sizing (1:1 Exact Native Dimensions without artificial cell stretching)
            int cellW = snap.iconRect.right - snap.iconRect.left;
            int cellH = snap.iconRect.bottom - snap.iconRect.top;
            if (cellW <= 0) cellW = (int)baseSil.width;
            if (cellH <= 0) cellH = (int)baseSil.height;

            // [MULTIMON v2.9.4] Explorer draws this item at PER-MONITOR DPI
            // scale (glyph = imagelist cx * monitorDpi/primaryDpi). Bake the
            // composite at the item's real render scale — otherwise the
            // silhouette is an oversize glyph on secondary monitors (the
            // monitor-2 bug). itemScale == 1.0 on the primary keeps the
            // byte-identical legacy 1:1 path.
            float itemScale = GetItemMonitorScale(g_overlayWnd, combinedRect);
            int drawW = (int)std::lround((float)baseSil.width * itemScale);
            int drawH = (int)std::lround((float)baseSil.height * itemScale);
            if (drawW <= 0) drawW = 1;
            if (drawH <= 0) drawH = 1;

            // Center the 1:1 icon inside Explorer's cell slot
            int iconOffsetX = (snap.iconRect.left - combinedRect.left) + (cellW - drawW) / 2;
            int iconOffsetY = (snap.iconRect.top - combinedRect.top) + (cellH - drawH) / 2;

            // 3. Build pc1 (Base Icon + Overlay Arrow)
            SilhouetteData pc1;
            pc1.width = (UINT)drawW;
            pc1.height = (UINT)drawH;
            if (itemScale == 1.0f) {
                // Direct 1:1 copy of native alpha bytes (zero scaling distortion)
                pc1.alphaData = baseSil.alphaData;
            } else {
                // [MULTIMON v2.9.4] Per-monitor render scale: area-average
                // downsample so the glyph matches the physically smaller icon.
                BoxDownsampleAlpha(baseSil, drawW, drawH, pc1.alphaData);
            }

            // Blit Shortcut Overlay Arrow into pc1
            if (snap.overlayIndex > 0) {
                SilhouetteData ovSil;
                bool hasOv = false;
                bool isCustom = g_isCustomShortcutArrow.load(std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(g_overlayCacheMutex);
                    auto ovIt = g_cpuOverlaySilhouettes.find(snap.overlayIndex);
                    if (ovIt != g_cpuOverlaySilhouettes.end()) {
                        ovSil = ovIt->second;
                        hasOv = true;
                    }
                }
                if (hasOv && ovSil.width > 0 && ovSil.height > 0) {
                    float ovScale = (baseSil.width > 0) ? ((float)drawW / (float)baseSil.width) : 1.0f;
                    int scaledOvW = (int)std::round((float)ovSil.width * ovScale);
                    int scaledOvH = (int)std::round((float)ovSil.height * ovScale);

                    int ovX = (int)std::round(1.0f * g_dpiScale);
                    int ovY = 0;

                    if (isCustom) {
                        ovX += (int)std::round((float)localSettings.customArrowOffsetX * g_dpiScale * ovScale);
                        ovY += drawH - scaledOvH + (int)std::round((float)localSettings.customArrowOffsetY * g_dpiScale * ovScale);
                    } else {
                        float stepSize = 48.0f * g_dpiScale;
                        if ((float)drawW < 36.0f * g_dpiScale) {
                            stepSize = 24.0f * g_dpiScale;
                        } else if ((float)drawW < 72.0f * g_dpiScale) {
                            stepSize = 38.0f * g_dpiScale;
                        } else {
                            stepSize = std::round((12.0f * g_dpiScale) + (0.36f * (float)drawW) + 1.6f);
                        }
                        scaledOvW = (int)std::round(stepSize);
                        scaledOvH = (int)std::round(stepSize);
                        ovY += drawH - scaledOvH - (int)std::round(1.0f * g_dpiScale);
                    }

                    for (int y = 0; y < scaledOvH; ++y) {
                        int destY = ovY + y;
                        if (destY < 0 || destY >= drawH) continue;
                        int srcY = (scaledOvH > 0) ? (y * (int)ovSil.height / scaledOvH) : 0;
                        if (srcY < 0) srcY = 0;
                        if (srcY >= (int)ovSil.height) srcY = (int)ovSil.height - 1;

                        for (int x = 0; x < scaledOvW; ++x) {
                            int destX = ovX + x;
                            if (destX < 0 || destX >= drawW) continue;
                            int srcX = (scaledOvW > 0) ? (x * (int)ovSil.width / scaledOvW) : 0;
                            if (srcX < 0) srcX = 0;
                            if (srcX >= (int)ovSil.width) srcX = (int)ovSil.width - 1;

                            uint8_t a = ovSil.alphaData[srcY * ovSil.width + srcX];
                            uint8_t dstA = pc1.alphaData[destY * drawW + destX];
                            if (a > dstA) pc1.alphaData[destY * drawW + destX] = a;
                        }
                    }
                }
            }

            // 4. Build pc2_c (pc1 + Collapsed Label) at size totalW x totalH
            SilhouetteData pc2_c;
            pc2_c.width = totalW;
            pc2_c.height = totalH;
            pc2_c.alphaData.assign(totalW * totalH, 0);

            // Blit pc1 into pc2_c at (iconOffsetX, iconOffsetY) without 1px drift
            for (int y = 0; y < drawH; ++y) {
                int destY = iconOffsetY + y;
                if (destY < 0 || destY >= totalH) continue;
                for (int x = 0; x < drawW; ++x) {
                    int destX = iconOffsetX + x;
                    if (destX < 0 || destX >= totalW) continue;
                    pc2_c.alphaData[destY * totalW + destX] = pc1.alphaData[y * drawW + x];
                }
            }

            // Check if the label is visually expanded (Selected, OR Focused
            // while desktop is active). Needed BEFORE the label blit: the two
            // states render text differently (see truncated flag below).
            bool isExpanded = IsIconExpanded(snap);

            // Blit Label Text into pc2_c — letter-shaped when we can render
            // the text with Explorer's own font, solid rectangle otherwise.
            // Collapsed labels use Explorer's DrawText(DT_WORDBREAK |
            // DT_END_ELLIPSIS) truncation; EXPANDED labels never ellipsize —
            // expanding exists to show the whole name (the rect grows to fit
            // it), overlong words just char-wrap mid-word onto the next row.
            if (comboLabelW > 0 && comboLabelH > 0) {
                int labelOffsetX = snap.labelRect.left - combinedRect.left;
                int labelOffsetY = snap.labelRect.top - combinedRect.top;
                std::vector<uint8_t> labelAlpha;
                // [MULTIMON v2.9.4] Scale the LOGFONT to the item's per-
                // monitor render size — the D2D label renderer self-calibrates
                // from GDI-realized metrics of the passed LOGFONT, so a scaled
                // lfHeight flows through em size and row advance correctly.
                // (20px-tall primary metrics squeezed into a 16px secondary
                // rect used to top-clip the glyphs.)
                LOGFONTW scaledLabelLf = labelLogfont;
                if (itemScale != 1.0f) {
                    scaledLabelLf.lfHeight = (LONG)std::lround((float)labelLogfont.lfHeight * itemScale);
                    if (scaledLabelLf.lfHeight == labelLogfont.lfHeight)
                        scaledLabelLf.lfHeight += (labelLogfont.lfHeight < 0) ? -1 : 1;
                    if (labelLogfont.lfWidth != 0)
                        scaledLabelLf.lfWidth = (LONG)std::lround((float)labelLogfont.lfWidth * itemScale);
                }
                bool letterShaped = labelFont &&
                    RenderGlyphSilhouette(raw.text, labelFont, scaledLabelLf, (int)comboLabelW, (int)comboLabelH, labelAlpha, !isExpanded);
                // [FLASH-PROBE v2.9.14] A failed glyph render stamps the label
                // as a SOLID RECTANGLE in the silhouette — the only remaining
                // un-instrumented rectangle source after the collapse-hold
                // and white-rect paths logged nothing during Doc's folder
                // flash repro. This probe arbitrates.
                if (!letterShaped) {
                    static std::unordered_map<uint64_t, int> s_glyphFailLogged;
                    if (s_glyphFailLogged[snap.cacheKey]++ < 8) {
                        DiagLog(L"[FLASH] glyph-rect '%s' font=%d scale=%.2f label=%ux%u expanded=%d",
                                raw.text.c_str(), labelFont ? 1 : 0,
                                itemScale, comboLabelW, comboLabelH, isExpanded ? 1 : 0);
                    }
                }
                for (int y = 0; y < (int)comboLabelH; ++y) {
                    int destY = labelOffsetY + y;
                    if (destY < 0 || destY >= totalH) continue;
                    for (int x = 0; x < (int)comboLabelW; ++x) {
                        int destX = labelOffsetX + x;
                        if (destX < 0 || destX >= totalW) continue;
                        pc2_c.alphaData[destY * totalW + destX] = letterShaped ? labelAlpha[y * comboLabelW + x] : 255;
                    }
                }
            }

            // 3. Generate Combo Hash & Store State-Specific Composite
            uint64_t currentProfile = GetShadowBakeHash(localSettings);
            // Letter-shaped labels make the composite text-dependent, so the
            // combo must distinguish same-size labels with different text.
            uint64_t labelTextHash = 14695981039346656037ULL;
            for (wchar_t c : raw.text) {
                labelTextHash ^= (uint64_t)c;
                labelTextHash *= 1099511628211ULL;
            }
            UINT itemScalePermille = (UINT)std::lround(itemScale * 1000.0f); // [MULTIMON v2.9.4]
            uint64_t currentComboHash = GenerateComboHash(baseShapeHash, snap.overlayIndex, comboLabelW, comboLabelH, currentProfile, labelTextHash, itemScalePermille);

            CompositeData compData;
            // Fetch existing data so we don't wipe the other state's composite if it's already cached
            bool fetchedExisting = g_diskCache.GetComposite(currentComboHash, compData);
            // [ZOOMDIAG v2.2.1] What the build saw. If an entry that a prior
            // [SAVEBACK] filled with layers reads back silhouette-only
            // (fetched=1, c_lay=0), the RAM mirror lost layers in between.
            DiagLog(L"[COMBOSAVE] '%s' hash=%llu fetched=%d c_lay=%u e_lay=%u",
                    raw.text.c_str(), (unsigned long long)currentComboHash,
                    fetchedExisting ? 1 : 0,
                    (unsigned)compData.shadows_c.size(), (unsigned)compData.shadows_e.size());
            compData.pc1 = pc1;
            snap.arrowGen = g_arrowGen.load(std::memory_order_relaxed); // [F1]
            compData.arrowGen = snap.arrowGen; // [P1.1] stamp gen for disk-hit skip trust
            if (isExpanded) {
                snap.comboHash_e = currentComboHash;
                snap.comboRect_e = snap.labelRect; // geometry the e-composite was built from
                DiagLog(L"[COMBO] '%s': built E composite at %dx%d (state expanded)",
                        raw.text.c_str(), (int)comboLabelW, (int)comboLabelH);
                compData.pc2_e = pc2_c; // pc2_c holds the unified silhouette for the current state
                g_diskCache.SetComposite(snap.comboHash_e, compData, currentProfile);
            } else {
                snap.comboHash_c = currentComboHash;
                snap.comboRect_c = snap.labelRect; // geometry the c-composite was built from
                DiagLog(L"[COMBO] '%s': built C composite at %dx%d (state collapsed)",
                        raw.text.c_str(), (int)comboLabelW, (int)comboLabelH);
                compData.pc2_c = pc2_c;
                g_diskCache.SetComposite(snap.comboHash_c, compData, currentProfile);
            }

            // [SILFIRST v2.2.5] Publish the silhouette GPU bitmap the moment
            // the combo exists — during the pass, not at preflight/bake. The
            // mask stamps sourceBmp out of the combo cache, so every icon's
            // overlay cutout lands as soon as its composite is built (first
            // visit to a zoom level / cold boot), instead of waiting for the
            // whole bake pipeline to reach it. [v2.4.0] via shared helper.
            PublishSilhouetteFromPc2(currentComboHash, pc2_c);
        }

        // [PERF-AUDIT v2.2.6] per-icon slow-path phases on the pass timeline.
        DiagLog(L"[PASSTICK] '%s' extr=%llums combo=%llums t+%llums fast=%d",
                raw.text.c_str(), extrMs,
                tCombo0 ? (GetTickCount64() - tCombo0) : 0,
                GetTickCount64() - passStartTick,
                fastPath ? 1 : 0);

        if (mergeIntoExisting) {
            newSnapshots[dec.mergeSlot] = std::move(snap);
        } else {
            newSnapshots.push_back(std::move(snap));
        }

    }

    bool anyLiveHold = false;
    size_t publishedCount = 0;
    std::vector<std::wstring> publishCollLogs;   // [NAMECOLL] flushed after the lock scope
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        // STALE-STATE GUARD (v2.1.17, index-aligned): the staged `state` was
        // captured on the UI thread when the deep pass STARTED; the poll keeps
        // writing fresher state while staging runs. Publishing the stale copy
        // resurrects finished selections (mask stamps a selection rectangle,
        // shadow vanishes) until the next pass. The poll re-reads the LV on
        // every selection change, so the live entry's state is always at least
        // as fresh.
        // The old name-keyed lookup ("displayName:lnk/item" -> one live entry)
        // merged SAME-NAMED icons into one slot: every publish copied one
        // twin's state onto the other (the YTDownloader shadow flicker,
        // proven in diag_log 2026-11-03). The live list is index-aligned with
        // the staged list by construction (both enumerate the LV in order —
        // the same spine the poll already trusts), so copy live[i] ->
        // staged[i] and VERIFY identity (name + lnk class) before trusting
        // the slot. A mid-pass reorder fails the name check: nothing is
        // copied (safe) and the poll's own per-index identity check forces a
        // full re-pass, so worst case is one pass of staged-start state.
        if (!g_iconSnapshots.empty()) {
            constexpr UINT kPollStateMask = LVIS_SELECTED | LVIS_FOCUSED | LVIS_DROPHILITED;
            const size_t liveCount = g_iconSnapshots.size();
            // [NAMECOLL] Same-name twins are legitimate as long as each staged
            // slot validates against its OWN live slot. Count duplicated
            // identity keys so their aligned copies stay visible in the log
            // (proof the fix runs); unique names stay silent.
            std::unordered_map<std::wstring, int> nameDups;
            for (const auto& live : g_iconSnapshots) {
                if (!live.displayName.empty())
                    nameDups[live.displayName + (live.overlayIndex > 0 ? L":lnk" : L":item")]++;
            }
            for (size_t stagedIdx = 0; stagedIdx < newSnapshots.size(); ++stagedIdx) {
                if (stagedIdx >= liveCount) break; // staged grew (new items): no live counterpart yet
                auto& s = newSnapshots[stagedIdx];
                if (s.displayName.empty()) continue;
                const IconSnapshot& live = g_iconSnapshots[stagedIdx];
                if (s.displayName != live.displayName ||
                    (s.overlayIndex > 0) != (live.overlayIndex > 0)) {
                    continue; // reordered mid-pass: copy nothing, mismatch detector re-passes
                }
                s.state = (s.state & ~kPollStateMask) | (live.state & kPollStateMask);
                // Transient selection state: the poll is the authority (it
                // engages/expires holds and self-heals drag flags every pass).
                s.pendingCollapse = live.pendingCollapse;
                s.pendingCollapseStart = live.pendingCollapseStart;
                s.heldCollapseRect = live.heldCollapseRect;
                s.isDragged = live.isDragged;
                s.isPendingDrop = live.isPendingDrop;
                s.preDragBoundsRect = live.preDragBoundsRect;
                auto dupIt = nameDups.find(s.displayName + (s.overlayIndex > 0 ? L":lnk" : L":item"));
                if (dupIt != nameDups.end() && dupIt->second > 1) {
                    wchar_t collLine[512];
                    swprintf_s(collLine, L"[NAMECOLL] PUBLISH-ALIGNED '%s' idx %zu state=%u (twin x%d, own slot)",
                               s.displayName.c_str(), stagedIdx, s.state, dupIt->second);
                    publishCollLogs.push_back(collLine);
                }
                // [SLITHER-GUARD v2.5.3] Geometry arbitration on tiny deltas.
                // The probe (v2.5.1) proved the deep pass — now fast enough
                // to beat Explorer's post-zoom layout settle — captures
                // rects ~2px above the settled positions and republishes
                // them OVER Stage A's correct manifest geometry (the
                // "correct first, then slither" sequence). Rule: when the
                // staged rect agrees with live to within 2px on every edge
                // at the SAME size, the live value wins — it is either the
                // settled Stage A manifest or the poll's fresher re-measure
                // (poll authority, invariant 1, now extended to geometry).
                // Big deltas (real zoom relayout, real moves) still take the
                // staged measurement untouched.
                {
                    int dL = s.combinedRect.left - live.combinedRect.left;
                    int dT = s.combinedRect.top - live.combinedRect.top;
                    int dR = s.combinedRect.right - live.combinedRect.right;
                    int dB = s.combinedRect.bottom - live.combinedRect.bottom;
                    bool anyDiff = (dL || dT || dR || dB);
                    bool small = (dL >= -2 && dL <= 2) && (dT >= -2 && dT <= 2) &&
                                 (dR >= -2 && dR <= 2) && (dB >= -2 && dB <= 2);
                    if (anyDiff && small) {
                        static int s_slitherGuardLogged = 0;
                        if (s_slitherGuardLogged < 40) {
                            ++s_slitherGuardLogged;
                            wchar_t line[512];
                            swprintf_s(line,
                                L"[SLITHER-GUARD] live wins '%s' idx %zu: (%ld,%ld,%ld,%ld) over staged (%ld,%ld,%ld,%ld) d=(%d,%d,%d,%d)",
                                s.displayName.c_str(), stagedIdx,
                                live.combinedRect.left, live.combinedRect.top, live.combinedRect.right, live.combinedRect.bottom,
                                s.combinedRect.left, s.combinedRect.top, s.combinedRect.right, s.combinedRect.bottom,
                                dL, dT, dR, dB);
                            publishCollLogs.push_back(line);
                        }
                        s.iconRect = live.iconRect;
                        s.boundsRect = live.boundsRect;
                        s.labelRect = live.labelRect;
                        s.combinedRect = live.combinedRect;
                    }
                }
            }
        }
        // The deep pass stages items over hundreds of ms; a hold that was live when
        // an item was staged may have already expired (or been cleared by the poll)
        // by publish time. Re-validate so a stale copy can't resurrect the hold.
        ULONGLONG nowPublish = GetTickCount64();
        for (auto& s : newSnapshots) {
            if (s.pendingCollapse) {
                if (nowPublish - s.pendingCollapseStart > COLLAPSE_HOLD_MS) {
                    s.pendingCollapse = false;
                } else {
                    anyLiveHold = true;
                }
            }
        }
        publishedCount = newSnapshots.size(); // log after the move reads a moved-from vector
        g_iconSnapshots = std::move(newSnapshots);
        g_snapshotGeneration.fetch_add(1, std::memory_order_release);
    }
    for (const auto& collLine : publishCollLogs) DiagLog(L"%s", collLine.c_str());
    // [PERF-AUDIT v2.2.6] pass wall clock. [P1.3] printed newSnapshots.size()
    // AFTER the move (always 0) — publishCount is captured pre-move at 8423.
    DiagLog(L"[PASS-END] %zu published in %llu ms (slow=%d fast=%d diskSkip=%d)",
            publishedCount, GetTickCount64() - passStartTick, countSlow, countFast, countDiskSkip);

    // [SLITHER v2.5.3] A post-zoom pass can outlast the 800ms settle re-poll
    // (passes run 469ms-3.6s) and republish mid-settle geometry AFTER the
    // re-poll already healed — with nothing behind it (Doc's "sometimes it
    // doesn't heal until I select"). If this pass follows a zoom, re-arm
    // one settle re-poll so the last writer is always followed by a
    // settled re-measure.
    {
        ULONGLONG sinceZoom = GetTickCount64() - g_lastZoomTick.load(std::memory_order_relaxed);
        if (sinceZoom < 3000 && g_overlayWnd && IsWindow(g_overlayWnd)) {
            SetTimer(g_overlayWnd, TIMER_ID_ZOOM_SETTLE, 400, nullptr);
        }
    }

    // A hold survived publishing, but this thread may run while the fast collapse
    // poll timer is dead (e.g. the poll ended an earlier hold). Nudge the UI thread
    // so QuickSnapshotStatePoll re-runs and re-arms the timer.
    if (anyLiveHold && g_overlayWnd && IsWindow(g_overlayWnd)) {
        PostMessageW(g_overlayWnd, WM_APP_SELECTION_UPDATE, 0, 0);
    }

    // --- GARBAGE COLLECTION: Mark and Sweep ---
    // 1. MARK: Collect all cache keys and shape hashes currently visible on the desktop
    std::unordered_set<uint64_t> activeCacheKeys;
    std::unordered_set<uint64_t> activeShapeHashes;

    std::unordered_set<uint64_t> activeComboHashes;

    std::unordered_map<uint64_t, uint64_t> localHashCache;
    {
        std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
        localHashCache = g_iconHashCache;
    }

    // Snapshot the published list under the lock: LoadManifestFromDisk can
    // replace g_iconSnapshots concurrently, and iterating it unlocked is a
    // data race.
    std::vector<IconSnapshot> gcSnapshotCopy;
    {
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        gcSnapshotCopy = g_iconSnapshots;
    }

    // We use the snapshot copy because it holds the exact list of what the Windows Shell is currently displaying
    for (const auto& snap : gcSnapshotCopy) {
        activeCacheKeys.insert(snap.cacheKey);

        auto it = localHashCache.find(snap.cacheKey);
        if (it != localHashCache.end()) {
            activeShapeHashes.insert(it->second);
        }

        if (snap.comboHash_c != 0) activeComboHashes.insert(snap.comboHash_c);
        if (snap.comboHash_e != 0) activeComboHashes.insert(snap.comboHash_e);
    }

    // 2. SWEEP: Evict orphaned GPU/CPU assets to prevent VRAM/RAM leaks
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        for (auto it = g_iconCache.begin(); it != g_iconCache.end(); ) {
            if (activeCacheKeys.find(it->first) == activeCacheKeys.end()) {
                it = g_iconCache.erase(it); // Frees VRAM
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_hashCacheMutex);
        for (auto it = g_iconHashCache.begin(); it != g_iconHashCache.end(); ) {
            if (activeCacheKeys.find(it->first) == activeCacheKeys.end()) {
                it = g_iconHashCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_comboShadowCacheMutex);
        // [SILPERSIST v2.2.3] Inactive entries lose their shadow LAYERS (the
        // expensive GPU part, re-uploaded on revisit) but KEEP sourceBmp —
        // the mask stamps it, so a zoom revisit shows true silhouettes
        // instantly instead of a white-rect wave while preflight re-uploads.
        // Hard cap on silhouette-only entries (dead zooms/label variants)
        // keeps the map bounded; realistic worst case ~7 zoom levels.
        size_t silOnlyBudget = 1200;
        for (auto it = g_comboShadowCache.begin(); it != g_comboShadowCache.end(); ) {
            if (activeComboHashes.find(it->first) == activeComboHashes.end()) {
                // [SILPERSIST v2.2.4] Retain silhouette-only entries within
                // budget (each retained entry consumes budget, whether it was
                // just stripped or kept from an earlier sweep — the previous
                // logic erased already-stripped entries on the very next
                // sweep, destroying the persistence it was meant to provide).
                if (silOnlyBudget > 0) {
                    // [VRAM v2.4.4] hashes of the most recently left zoom keep
                    // their GPU layers — the re-upload trail on a return zoom
                    // dies (Doc ruling). Only hashes OUTSIDE the keep set are
                    // stripped to silhouette-only.
                    bool keepLayers = false;
                    {
                        std::lock_guard<std::mutex> keepLock(g_layersKeepHashesMutex);
                        keepLayers = g_layersKeepHashes.count(it->first) != 0;
                    }
                    if (!keepLayers && !it->second.layers.empty()) it->second.layers.clear();
                    --silOnlyBudget;
                    ++it;
                } else {
                    it = g_comboShadowCache.erase(it);
                }
            } else {
                ++it;
            }
        }
    }

    {
        // [ZOOMFIX v2.2.2] Shapes referenced by the persisted index survive
        // the sweep: other zoom levels' shapes are not "active" right now but
        // are exactly what a zoom revisit adopts (stable shape hash -> stable
        // combo hash -> disk cache hit). Bounded by visited zoom levels.
        std::unordered_set<uint64_t> persistedShapeHashes;
        {
            std::lock_guard<std::mutex> pLock(g_persistedShapeIndexMutex);
            for (const auto& pair : g_persistedShapeIndex) persistedShapeHashes.insert(pair.second);
        }
        std::lock_guard<std::mutex> lock(g_uniqueShapesMutex);
        for (auto it = g_uniqueShapes.begin(); it != g_uniqueShapes.end(); ) {
            if (activeShapeHashes.find(it->hash) == activeShapeHashes.end() &&
                persistedShapeHashes.find(it->hash) == persistedShapeHashes.end()) {
                it = g_uniqueShapes.erase(it); // Frees CPU RAM (alpha arrays)
            } else {
                ++it;
            }
        }
        for (auto it = g_shapeToBitmap.begin(); it != g_shapeToBitmap.end(); ) {
            if (activeShapeHashes.find(it->first) == activeShapeHashes.end() &&
                persistedShapeHashes.find(it->first) == persistedShapeHashes.end()) {
                it = g_shapeToBitmap.erase(it); // Frees shared 8-bit GPU texture
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_watchedFoldersMutex);
        g_watchedFolderPaths = std::move(localWatchedPaths);
    }

    Wh_Log(L"[SNAPSHOT] Finished %zu items in %llums (Slow Path: %d, Fast Path: %d)",
           publishedCount, GetTickCount64() - tSnapshotStart, countSlow, countFast);

    if (g_hFolderWatchUpdateEvent) SetEvent(g_hFolderWatchUpdateEvent);
    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);

    float dpi = g_dpiScale;
    int zoom = GetCurrentIconZoom();
    uint64_t envHash = GetEnvironmentHash();
    std::vector<IconSnapshot> manifestCopy;
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        manifestCopy = g_iconSnapshots;
    }

    // Persist icon shapes (throttled to one write per 30s) so cold boots
    // can skip shell extraction for unchanged icons. Independent of the
    // composite dirty flag — shapes and composites change on different cues.
    static ULONGLONG s_lastShapeSave = 0;
    ULONGLONG nowShapeSave = GetTickCount64();
    // [P1.2] A zoom change forces the next pass-end save past the throttle:
    // each visited zoom's keys reach the file at its own arrival pass, so a
    // quick A->B leg can no longer orphan A's shapes before adoption.
    bool forceShapeSave = g_shapeSaveForced.exchange(false, std::memory_order_relaxed);
    if (forceShapeSave || nowShapeSave - s_lastShapeSave > 30000) {
        s_lastShapeSave = nowShapeSave;
        SaveIconShapes(manifestCopy);
    }

    // Save new composites to disk asynchronously (Phase 1 completion)
    if (g_diskCache.IsDirty()) {
        g_diskCache.QueueSave(dpi, zoom, envHash, std::move(manifestCopy));
    }
}

void SnapshotWorkerThreadProcInner();
void SnapshotWorkerThreadProc() {
    try {
        SnapshotWorkerThreadProcInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] SnapshotWorker exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] SnapshotWorker unknown exception");
    }
}

// [STAGE A v2.4.0] Zoom fast path: restore the target zoom's cached layout
// + silhouettes so display flips near-instantly, then let the deep pass
// validate behind it. Runs on the worker (serialized with passes), BEFORE
// any queued full pass. Invariants: publish goes through the poll-authority
// index-aligned guard (no side door); silhouette-first (silhouettes stamp
// before the geometry swap wakes the renderers); first visit (no cache
// file) falls through to the deep pass unchanged.
static void RunStageARestore(int zoom) {
    if (g_unloading.load(std::memory_order_relaxed) || !g_bakeDc) return;
    ULONGLONG t0 = GetTickCount64();
    float dpi = g_dpiScale.load(std::memory_order_relaxed);
    // [STAGE A v2.4.1] Persist the LEAVING zoom's layout before anything
    // swaps: its manifest is still live right now, and its cache file may
    // not exist yet (files are only written for the zoom current at save
    // time). Runs even when this restore is superseded — the leaving zoom
    // of an aborted leg is exactly the intermediate zoom that would
    // otherwise never reach disk.
    int leavingZoom = g_stageALeavingZoom.exchange(0, std::memory_order_relaxed);
    if (leavingZoom > 0) {
        std::vector<IconSnapshot> leaving;
        {
            std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
            leaving = g_iconSnapshots;
        }
        if (!leaving.empty()) {
            // [VRAM v2.4.4] the leaving zoom's combos keep their GPU layers
            // through the next sweep(s) — shadows restore instantly on return.
            {
                std::unordered_set<uint64_t> keep;
                for (const auto& s : leaving) {
                    if (s.comboHash_c) keep.insert(s.comboHash_c);
                    if (s.comboHash_e) keep.insert(s.comboHash_e);
                }
                std::lock_guard<std::mutex> keepLock(g_layersKeepHashesMutex);
                g_layersKeepHashes = std::move(keep);
            }
            size_t leavingCount = leaving.size();
            g_diskCache.QueueSave(dpi, leavingZoom, GetEnvironmentHash(), std::move(leaving));
            DiagLog(L"[STAGEA] leaving zoom %d: %zu snapshots queued for cache save", leavingZoom, leavingCount);
        }
    }
    if (GetCurrentIconZoom() != zoom) return; // superseded by a newer zoom
    std::vector<IconSnapshot> manifest = g_diskCache.LoadManifestOnly(dpi, zoom, GetEnvironmentHash());
    if (manifest.empty()) {
        // [STAGEA-DIAG] name the miss: file absent vs file present with an
        // empty manifest section (different fixes).
        CacheFileHeader dbgHeader = {};
        HANDLE dbgFile = g_diskCache.DebugOpenCacheFile(dpi, zoom, GetEnvironmentHash(), dbgHeader);
        if (dbgFile) {
            CloseHandle(dbgFile);
            DiagLog(L"[STAGEA] zoom %d: cache file EXISTS but manifestCount=%u (save never wrote layout)", zoom, dbgHeader.manifestCount);
        } else {
            DiagLog(L"[STAGEA] zoom %d: no cache file (first visit - deep pass owns it)", zoom);
        }
        return;
    }

    // Silhouette-first: stamp sourceBmp for every restored combo hash we can
    // prove, BEFORE the geometry swap wakes the mask. RAM mirror first; the
    // zoom file fills holes via merge-load (keeps session entries intact).
    int silPublished = 0;
    std::vector<uint64_t> missingHashes;
    auto publishSil = [&](uint64_t h, bool preferE) -> bool {
        if (h == 0) return true;
        {
            std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
            auto it = g_comboShadowCache.find(h);
            if (it != g_comboShadowCache.end() && it->second.sourceBmp) return true;
        }
        CompositeData cd;
        if (!g_diskCache.GetComposite(h, cd)) return false;
        const SilhouetteData& pc2 = (preferE && !cd.pc2_e.alphaData.empty()) ? cd.pc2_e : cd.pc2_c;
        PublishSilhouetteFromPc2(h, pc2);
        return true;
    };
    for (const auto& snap : manifest) {
        const uint64_t hashes[2] = { snap.comboHash_c, snap.comboHash_e };
        for (int hi = 0; hi < 2; ++hi) {
            uint64_t h = hashes[hi];
            if (h == 0) continue;
            if (!publishSil(h, hi == 1)) missingHashes.push_back(h);
            else ++silPublished;
        }
    }
    if (!missingHashes.empty()) {
        // [STAGE A v2.4.3] mirror missed: pull the zoom file's composites in
        // (merge, not replace), then retry the holes.
        g_diskCache.MergeCompositesFromDisk(dpi, zoom, GetEnvironmentHash());
        size_t recovered = 0;
        for (uint64_t h : missingHashes) {
            if (publishSil(h, false)) { ++silPublished; ++recovered; }
        }
        DiagLog(L"[STAGEA] zoom %d: mirror missed %zu, file merge recovered %zu",
                zoom, missingHashes.size(), recovered);
    }

    const size_t restoredCount = manifest.size();
    // Geometry swap through the SAME poll-authority rules as the deep pass
    // publish (index-aligned stale-state guard, v2.1.17/v2.2.0): copy live
    // selection state onto staged slots only when identity (name + overlay
    // class) verifies; a count mismatch or reorder leaves staged state at 0
    // (safe: deselected flash at worst, the pass re-corrects).
    {
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        if (!g_iconSnapshots.empty()) {
            const size_t liveCount = g_iconSnapshots.size();
            for (size_t i = 0; i < manifest.size() && i < liveCount; ++i) {
                const auto& live = g_iconSnapshots[i];
                auto& staged = manifest[i];
                if (staged.displayName == live.displayName &&
                    (staged.overlayIndex > 0) == (live.overlayIndex > 0)) {
                    staged.state = live.state;
                }
            }
        }
        g_iconSnapshots = std::move(manifest);
        g_snapshotGeneration.fetch_add(1, std::memory_order_release);
    }
    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
    if (g_renderEvent) SetEvent(g_renderEvent);
    DiagLog(L"[STAGEA] zoom %d: %zu snapshots restored, %d silhouettes in %llu ms",
            zoom, restoredCount, silPublished, GetTickCount64() - t0);
}

void SnapshotWorkerThreadProcInner() {
    // MTA by recorded review decision (see to-do 📌): STA shell extension
    // handlers called from MTA get hosted/marshaled by COM — the failure mode
    // is latency, not explorer crashes. Revisit only on a real hang report.

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (SUCCEEDED(hrCom)) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                         CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_workerWicFactory));
    }

    // Composite blobs load HERE (worker thread) rather than on the UI thread
    // during CreateSwapChainResources — boot no longer decompresses the whole
    // shadow cache under g_renderMutex on explorer's main thread. Runs before
    // the first request is consumed, so the first deep pass already hits.
    g_diskCache.LoadCompositesFromDisk(g_dpiScale.load(std::memory_order_relaxed),
                                       GetCurrentIconZoom(), GetEnvironmentHash());

    // [STAGE A v2.4.4 — boot amendment] The Frame-1 bootstrap restored the
    // boot zoom's layout, but its mask stamped white rects because nothing
    // had published silhouettes yet. The composites are in the mirror now:
    // stamp sourceBmp for every restored combo and wake the mask, so boot
    // shows true silhouettes BEFORE the first deep pass finishes.
    {
        std::vector<IconSnapshot> bootManifest;
        {
            std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
            bootManifest = g_iconSnapshots;
        }
        int bootSil = 0;
        for (const auto& snap : bootManifest) {
            const uint64_t hashes[2] = { snap.comboHash_c, snap.comboHash_e };
            for (int hi = 0; hi < 2; ++hi) {
                uint64_t h = hashes[hi];
                if (h == 0) continue;
                {
                    std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
                    auto it = g_comboShadowCache.find(h);
                    if (it != g_comboShadowCache.end() && it->second.sourceBmp) continue;
                }
                CompositeData cd;
                if (g_diskCache.GetComposite(h, cd)) {
                    const SilhouetteData& pc2 = (hi == 1 && !cd.pc2_e.alphaData.empty()) ? cd.pc2_e : cd.pc2_c;
                    PublishSilhouetteFromPc2(h, pc2);
                    ++bootSil;
                }
            }
        }
        if (bootSil > 0) {
            DiagLog(L"[STAGEA] boot: %d silhouettes published from mirror for Frame-1 layout", bootSil);
            if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
            if (g_renderEvent) SetEvent(g_renderEvent);
        }
    }

    while (!g_unloading) {
        WaitForSingleObject(g_snapshotWorkerEvent, INFINITE);
        if (g_unloading) break;

        while (true) {
            // FAST PATH first: drain the priority single-icon slot before any
            // queued full pass, so a click's expanded composite + shadow assets
            // land in ms instead of riding the full-pass convoy.
            {
                FastPathRequest fastReq;
                bool haveFast = false;
                {
                    std::lock_guard<std::mutex> lock(g_fastPathMutex);
                    if (!g_fastPathRequest.items.empty()) {
                        fastReq = std::move(g_fastPathRequest);
                        g_fastPathRequest = FastPathRequest{};
                        haveFast = true;
                    }
                }
                if (haveFast) {
                    try {
                        ProcessSnapshotData(fastReq.items, fastReq.userDesktop,
                                            fastReq.publicDesktop,
                                            fastReq.labelFont, fastReq.labelLogfont,
                                            /*mergeIntoExisting=*/true);
                    } catch (...) {
                        Wh_Log(L"[SNAPSHOT] Caught unexpected exception in fast path");
                    }
                    if (!g_unloading && g_bakeDc) {
                        try {
                            PreflightShadowAssets(SnapshotSettings());
                        } catch (...) {
                            Wh_Log(L"[SNAPSHOT] Caught unexpected exception in fast-path preflight");
                        }
                    }
                    continue; // re-check the fast slot before touching the queue
                }
            }

            // [STAGE A v2.4.0] Zoom fast path: restore the cached state for a
            // known zoom BEFORE any queued deep pass. After it, re-check the
            // fast slot (a click may have raced the restore).
            {
                int stageA = g_stageARequestZoom.exchange(0, std::memory_order_relaxed);
                if (stageA != 0) {
                    try {
                        RunStageARestore(stageA);
                    } catch (...) {
                        Wh_Log(L"[SNAPSHOT] Caught unexpected exception in Stage A restore");
                    }
                    continue;
                }
            }

            std::vector<RawListItem> items;
            std::wstring userDesktop, publicDesktop;
            HFONT labelFont = nullptr;
            LOGFONTW labelLogfont = {};

            {
                std::lock_guard<std::mutex> lock(g_snapshotRequestMutex);
                if (!g_snapshotRequestPending) break;
                items = std::move(g_snapshotRequestItems);
                userDesktop = g_snapshotUserDesktop;
                publicDesktop = g_snapshotPublicDesktop;
                labelFont = g_snapshotLabelHFont;
                labelLogfont = g_snapshotLabelFont;
                g_snapshotRequestPending = false;
                // All earlier passes are complete by now (requests are
                // processed sequentially), so no retired font can still be in
                // use — safe to delete the graveyard here.
                for (HFONT oldFont : g_retiredLabelFonts) {
                    DeleteObject(oldFont);
                }
                g_retiredLabelFonts.clear();
            }
            try {
                ProcessSnapshotData(items, userDesktop, publicDesktop, labelFont, labelLogfont);
            } catch (...) {
                Wh_Log(L"[SNAPSHOT] Caught unexpected exception in worker thread");
            }
        }

        // Shadow pre-flight runs here, on the worker's own D2D context, so the
        // underlay render loop never stalls on disk loads/uploads/readbacks.
        if (!g_unloading && g_bakeDc) {
            try {
                PreflightShadowAssets(SnapshotSettings());
            } catch (...) {
                Wh_Log(L"[SNAPSHOT] Caught unexpected exception in shadow pre-flight");
            }
        }
    }

    if (SUCCEEDED(hrCom)) CoUninitialize();
}

void BakeUnifiedShadows(const Settings& settings, uint64_t comboHash, const SilhouetteData& silhouette, float itemScale) {
    if (!g_bakeDc || silhouette.width == 0 || silhouette.height == 0) return;
    if (silhouette.alphaData.size() != silhouette.width * silhouette.height) return;

    D2D1_SIZE_U origSize = D2D1::SizeU(silhouette.width, silhouette.height);

    // 1. Upload the unified CPU alpha array to a temporary GPU bitmap
    std::vector<BYTE> bgraData(origSize.width * origSize.height * 4);
    for (UINT i = 0; i < origSize.width * origSize.height; ++i) {
        bgraData[i * 4 + 0] = 0;
        bgraData[i * 4 + 1] = 0;
        bgraData[i * 4 + 2] = 0;
        bgraData[i * 4 + 3] = silhouette.alphaData[i];
    }

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> sourceBmp;
    g_bakeDc->CreateBitmap(origSize, bgraData.data(), origSize.width * 4, &props, &sourceBmp);
    if (!sourceBmp) return;

    // 1b. [SILPERSIST v2.2.3] Layers-aware existence check + EARLY silhouette
    // publish: the mask stamps sourceBmp out of this cache, so the entry goes
    // in BEFORE the (slow) layer bake — icons get their true silhouette cutout
    // immediately and shadows fill in when the layers land. Fully-baked
    // entries still short-circuit; silhouette-only entries (GC strip or an
    // interrupted bake) are adopted and upgraded in place at the end, which
    // also makes a dead bake self-healing on the next preflight.
    {
        std::lock_guard<std::mutex> lock(g_comboShadowCacheMutex);
        auto it = g_comboShadowCache.find(comboHash);
        if (it != g_comboShadowCache.end()) {
            if (!it->second.layers.empty()) return; // fully baked already
            if (!it->second.sourceBmp) it->second.sourceBmp = sourceBmp;
        } else {
            ShadowCache silOnly;
            silOnly.sourceBmp = sourceBmp;
            g_comboShadowCache[comboHash] = std::move(silOnly);
        }
    }

    // --- BLUR & BAKE LOGIC ---
    // [MULTIMON v2.9.6] The silhouette canvas (pc2) is in MONITOR-LOCAL
    // units (it derives from the live per-monitor-scaled cell), so the
    // blur sigma must shrink with it on secondary monitors (D2 ruling:
    // proportionate shadows). v2.9.5 scaled the LAYER DRAW instead, which
    // shrank the whole layer canvas a second time — Doc field-caught
    // "shadows smaller than the icon" on the 100% secondary. Sigma scales
    // here, at the bake; the draw anchors layers natively.
    float sharpBlur = (std::min)((float)settings.shadowBlur * g_dpiScale, 250.0f) * itemScale;
    float tailRate = settings.shadowTailSoftness / 100.0f;
    float tailAddition = std::max(20.0f, sharpBlur * 9.0f);
    float softBlur = sharpBlur + (tailRate * tailAddition);

    float maxPadding = softBlur * 3.0f + 2.0f;
    UINT masterW = origSize.width + (UINT)std::ceil(maxPadding * 2.0f);
    UINT masterH = origSize.height + (UINT)std::ceil(maxPadding * 2.0f);
    D2D1_SIZE_U masterSize = D2D1::SizeU(masterW, masterH);

    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> tempSilhouette;
    ComPtr<ID2D1Image> oldTarget;
    g_bakeDc->GetTarget(oldTarget.ReleaseAndGetAddressOf());

    g_bakeDc->CreateBitmap(masterSize, nullptr, 0, &targetProps, tempSilhouette.ReleaseAndGetAddressOf());
    if (!tempSilhouette) {
        g_bakeDc->SetTarget(oldTarget.Get());
        return;
    }

    g_bakeDc->SetTarget(tempSilhouette.Get());
    g_bakeDc->BeginDraw();
    g_bakeDc->Clear(D2D1::ColorF(0, 0, 0, 0));

    D2D1_MATRIX_3X2_F oldTransform;
    g_bakeDc->GetTransform(&oldTransform);
    g_bakeDc->SetTransform(D2D1::Matrix3x2F::Translation(maxPadding, maxPadding));
    g_bakeDc->DrawBitmap(sourceBmp.Get());
    g_bakeDc->SetTransform(oldTransform);
    if (!CheckEndDraw(g_bakeDc->EndDraw(), L"BakeUnifiedShadows.master")) return;

    float maxSoftBlur = sharpBlur + (tailRate * std::max(20.0f, sharpBlur * 9.0f));
    float totalMaxReach = ((float)settings.shadowLength + (float)settings.shadowOffset) * g_dpiScale + maxSoftBlur;

    int passes = settings.shadowPasses;
    int optimalPasses = (std::max)(1, (int)std::ceil(totalMaxReach / 2.0f));
    if (passes > optimalPasses) passes = optimalPasses;

    ShadowCache cache;
    cache.sourceBmp = sourceBmp; // Save master composite bitmap for overlay mask stamping
    cache.layers.reserve(passes);

    for (int p = 1; p <= passes; ++p) {
        float stepFraction = (float)p / passes;
        float currentBlur = sharpBlur + (softBlur - sharpBlur) * stepFraction;
        float safeBlur = (std::max)(0.1f, currentBlur);

        float layerPad = currentBlur * 3.0f + 2.0f;

        // Tail scale falloff: slider -100..100 maps exponentially so the tail
        // layer reaches 4^(v/100) (0 -> 1x unchanged, -100 -> 0.25x, +100 ->
        // 4x; log-symmetric and never zero). Each layer p interpolates
        // geometrically from 1x at the root to the target scale at the tail.
        // Blur is deliberately independent — only the silhouette is resized,
        // BEFORE the blur pass, so the blur never resamples a scaled result.
        int tailSlider = settings.shadowTailScale;
        float tailMaxScale = std::pow(4.0f, tailSlider / 100.0f);
        float layerScale = (tailSlider == 0) ? 1.0f : std::pow(tailMaxScale, stepFraction);

        UINT contentW = (UINT)std::max(1.0f, std::floor(origSize.width * layerScale + 0.5f));
        UINT contentH = (UINT)std::max(1.0f, std::floor(origSize.height * layerScale + 0.5f));

        // The scaled silhouette stays centered on the icon, so the anchor
        // (where the render loop expects the silhouette core inside this
        // bitmap) shifts by half the size delta.
        float anchorX = layerPad - (float)((INT)contentW - (INT)origSize.width) * 0.5f;
        float anchorY = layerPad - (float)((INT)contentH - (INT)origSize.height) * 0.5f;

        UINT layerW = contentW + (UINT)std::ceil(layerPad * 2.0f);
        UINT layerH = contentH + (UINT)std::ceil(layerPad * 2.0f);
        D2D1_SIZE_U layerSize = D2D1::SizeU(layerW, layerH);

        ComPtr<ID2D1Bitmap1> layerBmp;
        g_bakeDc->CreateBitmap(layerSize, nullptr, 0, &targetProps, layerBmp.ReleaseAndGetAddressOf());

        if (layerBmp) {
            // Pick what gets blurred: the pristine master silhouette, or a
            // per-layer pre-scaled copy (resize before blur, no artifacts).
            ComPtr<ID2D1Bitmap1> blurInput = tempSilhouette;
            float inputOffsetX = layerPad - maxPadding;
            float inputOffsetY = layerPad - maxPadding;

            if (layerScale != 1.0f) {
                const float kEdgePad = 2.0f;
                UINT scaledW = contentW + (UINT)(kEdgePad * 2.0f);
                UINT scaledH = contentH + (UINT)(kEdgePad * 2.0f);
                ComPtr<ID2D1Bitmap1> scaledBmp;
                g_bakeDc->CreateBitmap(D2D1::SizeU(scaledW, scaledH), nullptr, 0, &targetProps, scaledBmp.ReleaseAndGetAddressOf());
                if (scaledBmp) {
                    g_bakeDc->SetTarget(scaledBmp.Get());
                    g_bakeDc->BeginDraw();
                    g_bakeDc->Clear(D2D1::ColorF(0, 0, 0, 0));
                    // Sample the untouched silhouette out of the master
                    // composite and scale it to this layer's content size.
                    g_bakeDc->DrawBitmap(tempSilhouette.Get(),
                        D2D1::RectF(kEdgePad, kEdgePad, kEdgePad + contentW, kEdgePad + contentH),
                        1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                        D2D1::RectF(maxPadding, maxPadding,
                                    maxPadding + origSize.width, maxPadding + origSize.height));
                    if (!CheckEndDraw(g_bakeDc->EndDraw(), L"BakeUnifiedShadows.scale")) return;
                    blurInput = scaledBmp;
                    // Place the scaled copy so its silhouette core lands on
                    // the anchor; kEdgePad accounts for the sampling border.
                    inputOffsetX = anchorX - kEdgePad;
                    inputOffsetY = anchorY - kEdgePad;
                }
                // If the scaled copy failed to allocate, fall through with the
                // unscaled input rather than dropping the layer entirely.
            }

            g_bakeDc->SetTarget(layerBmp.Get());
            g_bakeDc->BeginDraw();
            g_bakeDc->Clear(D2D1::ColorF(0, 0, 0, 0));
            if (g_underlayBlurEffect) {
                g_underlayBlurEffect->SetInput(0, blurInput.Get());
                g_underlayBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, safeBlur);
                g_underlayBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);

                g_bakeDc->SetTransform(D2D1::Matrix3x2F::Translation(inputOffsetX, inputOffsetY));
                g_bakeDc->DrawImage(g_underlayBlurEffect.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
                g_bakeDc->SetTransform(D2D1::Matrix3x2F::Identity());
            }
            if (!CheckEndDraw(g_bakeDc->EndDraw(), L"BakeUnifiedShadows.layer")) return;

            // Extract alpha to CPU for disk cache serialization
            std::vector<uint8_t> cpuAlpha(layerW * layerH);
            ComPtr<ID2D1Bitmap1> stg;
            if (layerW <= STAGING_TEX_SIZE && layerH <= STAGING_TEX_SIZE && g_stagingTexA) {
                stg = g_stagingTexA;
            } else {
                D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                g_bakeDc->CreateBitmap(D2D1::SizeU(layerW, layerH), nullptr, 0, &stagingProps, &stg);
            }
            if (stg) {
                D2D1_POINT_2U destPt = D2D1::Point2U(0, 0);
                D2D1_RECT_U srcRect = D2D1::RectU(0, 0, layerW, layerH);
                if (SUCCEEDED(stg->CopyFromBitmap(&destPt, layerBmp.Get(), &srcRect))) {
                    D2D1_MAPPED_RECT map;
                    if (SUCCEEDED(stg->Map(D2D1_MAP_OPTIONS_READ, &map))) {
                        for (UINT y = 0; y < layerH; ++y) {
                            for (UINT x = 0; x < layerW; ++x) {
                                cpuAlpha[y * layerW + x] = map.bits[y * map.pitch + x * 4 + 3];
                            }
                        }
                        stg->Unmap();
                    }
                }
            }

            ShadowLayer sl;
            sl.bmp = layerBmp;
            sl.width = layerW;
            sl.height = layerH;
            // [CORE-CENTER GROWTH v2.8.3] The render loop places the layer so
            // the stored offset lands at combinedRect.topLeft + stepOffset —
            // a fixed point for ALL layers. Storing the anchor (core TOP-LEFT
            // inside the bitmap) pinned every scaled layer's growth to that
            // corner: with tail scale up, bigger cores grew right+down only,
            // faking a bottom-right drift regardless of light direction (Doc
            // field report). Storing plain layerPad instead makes the CORE'S
            // CENTER the fixed point: growth is symmetric around the
            // silhouette, and only stepOffset carries direction. At
            // tailScale=0 anchor==layerPad, so the default look is unchanged.
            sl.offsetX = layerPad;
            sl.offsetY = layerPad;
            sl.cpuAlpha = std::move(cpuAlpha);
            cache.layers.push_back(sl);
        }
    }

    // --- TAIL-TO-FRONT (TtF) LAYER SHARING (CPU-side, no GPU readbacks) ---
    // Compare cpuAlpha arrays directly instead of doing staging-texture Map()
    // readbacks. Both sides already carry the 1-byte arrays (kept for disk
    // cache serialization), so the comparison is pure RAM. The comparison is
    // bounded CPU work with early exit, so doing it under the mutex is fine;
    // the garbage collector takes the same mutex, which also keeps every
    // cpuAlpha vector and bmp ComPtr alive while we look at them.
    {
        std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);

        for (const auto& [existingKey, existingCache] : g_comboShadowCache) {
            if (existingKey == comboHash) continue;
            if (existingCache.layers.size() != cache.layers.size()) continue;

            bool fullyMatched = true;
            for (int p = (int)cache.layers.size() - 1; p >= 0; --p) {
                ShadowLayer& newLayer = cache.layers[p];
                const ShadowLayer& existingLayer = existingCache.layers[p];

                if (newLayer.width != existingLayer.width || newLayer.height != existingLayer.height) {
                    fullyMatched = false;
                    break;
                }
                if (newLayer.bmp.Get() == existingLayer.bmp.Get()) continue;

                // Pure CPU comparison — no staging textures, no pipeline stall
                if (CompareAlphaLayersTTF(newLayer.cpuAlpha, existingLayer.cpuAlpha,
                                          newLayer.width, newLayer.height)) {
                    newLayer.bmp = existingLayer.bmp;
                    newLayer.cpuAlpha = existingLayer.cpuAlpha; // Keep serialization consistent with the shared bmp
                } else {
                    fullyMatched = false;
                    break;
                }
            }

            if (fullyMatched) {
                break; // Found a match, stop checking other caches
            }
        }

        // Insert our optimized cache into the map (same lock scope)
        g_comboShadowCache[comboHash] = std::move(cache);
    }
}


// Warm flag for the render-loop missing-assets diagnostics: cold start
// legitimately has no assets until the first pre-flight has run.
std::atomic<bool> g_shadowDiagWarm{false};

// Ensure every published icon has GPU shadow assets: RAM hit -> nothing,
// disk cache -> upload the saved layers, else bake fresh via
// BakeUnifiedShadows. Runs on the SNAPSHOT WORKER thread using g_bakeDc so
// the underlay render loop never stalls mid-frame on disk loads, bitmap
// uploads or staging-texture Map() readbacks.
void PreflightShadowAssets(const Settings& localSettings) {
    bool isEditing = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        isEditing = g_isEditing;
    }
    if (g_isMarqueeDrag.load(std::memory_order_relaxed) || isEditing) return;

    std::vector<IconSnapshot> snapshots;
    {
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        snapshots = g_iconSnapshots;
    }

    int countRAM = 0, countDisk = 0, countGPU = 0;
    ULONGLONG tBakeStart = GetTickCount64();
    bool assetsAdded = false;

    // RADIAL BAKE ORDER (Doc's wave-killer, variant 2 — radial pop): sort bake
    // targets closest-to-mouse first. Preflight runs on the worker within ~ms
    // of the [ZOOM] event, so cursor position here is effectively the T0
    // capture. The existing progressive display then draws the cold wave as
    // an expanding circle from the cursor instead of the snapshot vector's
    // arbitrary order. Total bake time unchanged (GPU-bound serial) — only
    // ARRIVAL ORDER moves. Stable sort: equal distances keep snapshot order.
    {
        POINT mouse;
        if (GetCursorPos(&mouse)) {
            auto centerDist2 = [&mouse](const IconSnapshot& s) -> long long {
                // Widest live rect wins; empty rects fall through to the next.
                const RECT* r = &s.combinedRect;
                if (r->right <= r->left || r->bottom <= r->top) r = &s.boundsRect;
                if (r->right <= r->left || r->bottom <= r->top) r = &s.iconRect;
                if (r->right <= r->left || r->bottom <= r->top) return LLONG_MAX;
                long long cx = ((long long)r->left + r->right) / 2;
                long long cy = ((long long)r->top + r->bottom) / 2;
                long long dx = cx - mouse.x;
                long long dy = cy - mouse.y;
                return dx * dx + dy * dy;
            };
            std::stable_sort(snapshots.begin(), snapshots.end(),
                             [&centerDist2](const IconSnapshot& a, const IconSnapshot& b) {
                                 return centerDist2(a) < centerDist2(b);
                             });
            if (!snapshots.empty()) {
                long long dFirst = centerDist2(snapshots.front());
                long long dLast = centerDist2(snapshots.back());
                Wh_Log(L"[RADIAL] mouse=(%ld,%ld) sorted %zu targets, nearest ~%.0fpx, farthest ~%.0fpx",
                       mouse.x, mouse.y, snapshots.size(),
                       sqrt((double)dFirst),
                       dLast == LLONG_MAX ? -1.0 : sqrt((double)dLast));
            }
        } else {
            Wh_Log(L"[RADIAL] GetCursorPos failed - keeping snapshot order");
        }
    }

    // BOTH states are ensured per icon, not just the active one. Preflight
    // used to bake only PickComboHash(snap): an icon that changed state
    // between its composite publishing and the preflight pass never got GPU
    // assets for the other state, and NOTHING re-triggered the bake — ghost
    // select then drew the truncated collapsed silhouette (mask alt-fallback)
    // with no shadow until a re-select happened to run another batch.
    // Expanded is ensured FIRST: a fresh selection needs it ready before the
    // user can deselect into ghost.
    // [SILFIRST v2.2.5] Preflight runs in TWO sweeps so every silhouette is
    // in the GPU combo cache before ANY shadow-layer work starts. The mask
    // stamps sourceBmp out of this cache, so sweep 1 (cheap ~30KB uploads,
    // ~150ms for the whole desktop) completes the overlay cutouts for all
    // icons; sweep 2 (the expensive 12-layer uploads, ~600-1000ms) then fills
    // shadows in. Old single-sweep interleaved them per icon: icon N+1's
    // silhouette waited for icon N's layers — the visible left-to-right wave.
    auto ensureSilhouetteAssets = [&](const IconSnapshot& snap, uint64_t targetHash, bool isTargetExpanded) -> bool {
        if (targetHash == 0) return true;
        {
            std::lock_guard<std::mutex> lock(g_comboShadowCacheMutex);
            auto entryIt = g_comboShadowCache.find(targetHash);
            if (entryIt != g_comboShadowCache.end() && entryIt->second.sourceBmp) return true;
        }
        CompositeData compData;
        if (!g_diskCache.GetComposite(targetHash, compData)) {
            static std::unordered_map<uint64_t,int> s_noCompLogged;
            if (s_noCompLogged[targetHash]++ < 3)
                Wh_Log(L"[PRE] no composite for combo %llu (c=%llu e=%llu) expanded=%d '%s'",
                       (unsigned long long)targetHash, (unsigned long long)snap.comboHash_c,
                       (unsigned long long)snap.comboHash_e, (int)IsIconExpanded(snap),
                       snap.displayName.c_str());
            DiagLog(L"[PRE] REQUEUE: no composite for combo %llu '%s' -> full pass",
                    (unsigned long long)targetHash, snap.displayName.c_str());
            if (g_overlayWnd && !g_snapshotUpdatePending.load(std::memory_order_relaxed)) {
                g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
            }
            return false;
        }
        const auto& sil = isTargetExpanded ? compData.pc2_e : compData.pc2_c;
        if (sil.width == 0 || sil.height == 0) {
            static std::unordered_map<uint64_t,int> s_silEmptyLogged;
            if (s_silEmptyLogged[targetHash]++ < 3)
                Wh_Log(L"[PRE] combo %llu has empty silhouette (c=%llu e=%llu) '%s'",
                       (unsigned long long)targetHash, (unsigned long long)snap.comboHash_c,
                       (unsigned long long)snap.comboHash_e, snap.displayName.c_str());
            DiagLog(L"[PRE] REQUEUE: empty silhouette combo %llu '%s' -> full pass",
                    (unsigned long long)targetHash, snap.displayName.c_str());
            if (g_overlayWnd && !g_snapshotUpdatePending.load(std::memory_order_relaxed)) {
                g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
            }
            return false;
        }
        // [ZOOMDIAG v2.2.1] What preflight read from the disk mirror.
        DiagLog(L"[PREFETCH] '%s' hash=%llu sil=%ux%u lay_c=%u lay_e=%u",
                snap.displayName.c_str(), (unsigned long long)targetHash,
                sil.width, sil.height,
                (unsigned)compData.shadows_c.size(), (unsigned)compData.shadows_e.size());
        if (sil.alphaData.empty()) return false;
        ComPtr<ID2D1Bitmap1> silBmp;
        {
            std::vector<BYTE> silBgra(sil.width * sil.height * 4);
            for (UINT i = 0; i < sil.width * sil.height; ++i) {
                silBgra[i * 4 + 0] = 0;
                silBgra[i * 4 + 1] = 0;
                silBgra[i * 4 + 2] = 0;
                silBgra[i * 4 + 3] = sil.alphaData[i];
            }
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            g_bakeDc->CreateBitmap(D2D1::SizeU(sil.width, sil.height), silBgra.data(), sil.width * 4, &props, &silBmp);
        }
        if (silBmp) {
            std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
            auto& entry = g_comboShadowCache[targetHash];
            if (!entry.sourceBmp) entry.sourceBmp = silBmp;
        }
        return true;
    };

    auto ensureLayerAssets = [&](const IconSnapshot& snap, uint64_t targetHash, bool isTargetExpanded) {
        if (targetHash == 0) return;

        bool hasFull = false;
        {
            std::lock_guard<std::mutex> lock(g_comboShadowCacheMutex);
            auto entryIt = g_comboShadowCache.find(targetHash);
            // [SILPERSIST v2.2.3] "RAM hit" means LAYERS present.
            hasFull = (entryIt != g_comboShadowCache.end() && !entryIt->second.layers.empty());
        }
        if (hasFull) {
            countRAM++;
            return;
        }
        CompositeData compData;
        if (!g_diskCache.GetComposite(targetHash, compData)) return; // REQUEUE already fired in sweep 1
        const auto& savedLayers = isTargetExpanded ? compData.shadows_e : compData.shadows_c;
        const auto& sil = isTargetExpanded ? compData.pc2_e : compData.pc2_c;
        if (savedLayers.empty()) {
            countGPU++;
            assetsAdded = true;
            BakeUnifiedShadows(localSettings, targetHash, sil,
                               GetItemMonitorScale(g_overlayWnd, snap.combinedRect)); // [MULTIMON v2.9.6]
            return;
        }
        countDisk++;
        assetsAdded = true;
        ShadowCache cache;
        cache.layers.reserve(savedLayers.size());
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

        for (const auto& saved : savedLayers) {
            ShadowLayer sl;
            sl.offsetX = saved.offsetX;
            sl.offsetY = saved.offsetY;
            sl.width = saved.width;
            sl.height = saved.height;
            sl.cpuAlpha = saved.alphaData;
            std::vector<BYTE> bgraData(saved.width * saved.height * 4);
            for (UINT i = 0; i < saved.width * saved.height; ++i) {
                bgraData[i * 4 + 0] = 0;
                bgraData[i * 4 + 1] = 0;
                bgraData[i * 4 + 2] = 0;
                bgraData[i * 4 + 3] = saved.alphaData[i];
            }
            ComPtr<ID2D1Bitmap1> tempBmp;
            g_bakeDc->CreateBitmap(D2D1::SizeU(saved.width, saved.height), bgraData.data(), saved.width * 4, &props, &tempBmp);
            sl.bmp = tempBmp;
            cache.layers.push_back(std::move(sl));
        }
        {
            std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
            auto& entry = g_comboShadowCache[targetHash];
            if (entry.layers.empty()) entry.layers = std::move(cache.layers);
        }
        auto& targetSaved = isTargetExpanded ? compData.shadows_e : compData.shadows_c;
        if (targetSaved.empty()) {
            std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
            auto it = g_comboShadowCache.find(targetHash);
            if (it != g_comboShadowCache.end() && !it->second.layers.empty()) {
                for (const auto& sl : it->second.layers) {
                    SavedShadowLayer saved;
                    saved.offsetX = sl.offsetX;
                    saved.offsetY = sl.offsetY;
                    saved.width = sl.width;
                    saved.height = sl.height;
                    saved.alphaData = sl.cpuAlpha;
                    targetSaved.push_back(std::move(saved));
                }
                uint64_t currentProfile = GetShadowBakeHash(localSettings); // Lock-free!
                g_diskCache.SetComposite(targetHash, compData, currentProfile);
                // [ZOOMDIAG v2.2.1] proof the upgrade to full landed.
                DiagLog(L"[SAVEBACK] hash=%llu layers=%u sil=%ux%u",
                        (unsigned long long)targetHash, (unsigned)targetSaved.size(),
                        (isTargetExpanded ? compData.pc2_e.width : compData.pc2_c.width),
                        (isTargetExpanded ? compData.pc2_e.height : compData.pc2_c.height));
            }
        }
    };

    // Sweep 1: silhouettes for EVERY icon first — the mask completes here.
    ULONGLONG tSweep1 = GetTickCount64(); // [PERF-AUDIT v2.2.6]
    for (const auto& snap : snapshots) {
        if (snap.isDragged) continue;
        ensureSilhouetteAssets(snap, snap.comboHash_e, true);
        ensureSilhouetteAssets(snap, snap.comboHash_c, false);
    }
    ULONGLONG sweep1Ms = GetTickCount64() - tSweep1;
    // Sweep 2: shadow layers (upload or bake) — shadows trail the cutouts.
    ULONGLONG tSweep2 = GetTickCount64();
    for (const auto& snap : snapshots) {
        if (snap.isDragged) continue;
        ensureLayerAssets(snap, snap.comboHash_e, true);
        ensureLayerAssets(snap, snap.comboHash_c, false);
    }
    ULONGLONG sweep2Ms = GetTickCount64() - tSweep2;
    // [PERF-AUDIT v2.2.6] sweep split for the zoom timeline.
    DiagLog(L"[PRE-SWEEPS] sil %llu ms, layers %llu ms (ram=%d disk=%d gpu=%d)",
            (unsigned long long)sweep1Ms, (unsigned long long)sweep2Ms,
            countRAM, countDisk, countGPU);

    Wh_Log(L"[UNDERLAY] Shadow Pre-flight in %llums -> RAM Hits: %d, Disk Cache Hits: %d, GPU Bakes: %d",
            GetTickCount64() - tBakeStart, countRAM, countDisk, countGPU);
    DiagLog(L"[PRE] preflight done in %lu ms (ram=%d disk=%d gpu=%d)",
            (unsigned long)(GetTickCount64() - tBakeStart), countRAM, countDisk, countGPU);
    // Warm the render-loop missing-assets diagnostics: before this point the
    // cache is legitimately empty on cold start.
    g_shadowDiagWarm.store(true, std::memory_order_release);

    // New assets appeared: wake the render loops so shadows pop in immediately
    // instead of waiting for the next natural frame.
    if (assetsAdded) {
        g_forceRender = true;
        g_underlayForceRender = true;
        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
        if (g_renderEvent) SetEvent(g_renderEvent);
    }
}

void RenderUnderlayLoopInner();
void RenderUnderlayLoop() {
    try {
        RenderUnderlayLoopInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] RenderUnderlayLoop exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] RenderUnderlayLoop unknown exception");
    }
    if (g_messageWnd && !g_unloading) PostMessageW(g_messageWnd, WM_APP_DEVICE_LOST, 0, 0);
}

void RenderUnderlayLoopInner() {
    // State tracking for idle detection
    int lastRawMx = -1, lastRawMy = -1;
    std::vector<IconSnapshot> snapshots;
    uint64_t localSnapGen = 0;
    bool lastOverDesktop = false;      // Track boundary crossing
    float localShadowFade = 0.0f;      // Thread-local fade to avoid cross-thread reads
    float lastSpotFade = -1.0f;        // Track spotlight fade state

    LARGE_INTEGER freq, lastTime, currentTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    // Initialize state fresh for this specific thread lifetime
    D2D1_POINT_2F s_underlaySmoothedPos = {0, 0};
    bool s_underlayPosInitialized = false;
    auto nextFrameTime = std::chrono::steady_clock::now();

    while (!g_underlayThreadStop) {
        if (!g_isDisplayOn.load(std::memory_order_relaxed)) {
            WaitForSingleObject(g_underlayRenderEvent, INFINITE);
            QueryPerformanceCounter(&lastTime);
            continue;
        }

        QueryPerformanceCounter(&currentTime);
        float deltaTime = (float)(currentTime.QuadPart - lastTime.QuadPart) / freq.QuadPart;
        lastTime = currentTime;
        // FIX: Treat massive sleep spikes as a single 16ms frame
        if (deltaTime > 0.1f) deltaTime = 0.016f;

        // --- LOCAL SNAPSHOT FOR THREAD SAFETY ---
        Settings localSettings = SnapshotSettings();

        ComPtr<ID2D1DeviceContext> dc;
        ComPtr<ID2D1Bitmap1> target;
        ComPtr<IDXGISwapChain1> swapChain;
        HWND hwnd;

        {
        std::lock_guard<std::mutex> stateLock(g_underlayMutex);
        dc = g_underlayDc;
        target = g_underlayTargetBitmap;
        swapChain = g_underlaySwapChain;
        hwnd = g_underlayWnd;

        // --- AUTOMATIC THREAD-SAFE RESIZE ---
        if (target && swapChain && g_underlayWidth > 0 && g_underlayHeight > 0) {
            D2D1_SIZE_U size = target->GetPixelSize();
            if (size.width != g_underlayWidth || size.height != g_underlayHeight) {

                // Release the old buffers before resizing
                dc->SetTarget(nullptr);
                target.Reset();
                g_underlayTargetBitmap.Reset();

                if (SUCCEEDED(swapChain->ResizeBuffers(0, g_underlayWidth, g_underlayHeight, DXGI_FORMAT_UNKNOWN, 0))) {
                    ComPtr<IDXGISurface2> surface;
                    if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) {
                        D2D1_BITMAP_PROPERTIES1 bp = {};
                        bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                        bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                        bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                        g_underlayDc->CreateBitmapFromDxgiSurface(surface.Get(), bp, &g_underlayTargetBitmap);
                        target = g_underlayTargetBitmap; // Update local reference
                    }
                }
            }
        }
    }

    if (!dc || !target || !hwnd) {
        Sleep(10);
        continue;
    }

    // Read RAW mouse pos for idle detection and local smoothing
    POINT rawMouse;
    GetCursorPos(&rawMouse);
    bool mouseMoved = (rawMouse.x != lastRawMx || rawMouse.y != lastRawMy);
    lastRawMx = rawMouse.x;
    lastRawMy = rawMouse.y;

    bool overDesktop = g_isMouseOverDesktop.load(std::memory_order_relaxed);
    bool boundaryCrossed = (overDesktop != lastOverDesktop);
    lastOverDesktop = overDesktop;

    bool isBypassed = g_overlayBypassed.load(std::memory_order_relaxed);
    bool effectiveGlobalTracking = localSettings.shadowGlobalTracking && isBypassed;
    // "Show desktop icons" off: no icon shadows, no icon cut-outs
    bool iconsHidden = g_desktopIconsHidden.load(std::memory_order_relaxed);
    bool underlayMaskGateOk = !iconsHidden &&
        (!localSettings.overlayClipToIcons || g_sharedMaskReady.load(std::memory_order_acquire));
    bool shadowsShouldBeVisible = localSettings.enableShadows && !iconsHidden &&
                                  (overDesktop || effectiveGlobalTracking) && underlayMaskGateOk;
    localShadowFade = UpdateShadowFade(localSettings, localShadowFade, shadowsShouldBeVisible, deltaTime);

    // 🎬 Toggle fade: shadows follow the overlay's toggle animation instead of
    // popping. presence 1 = overlay on (normal cull rules), 0 = bypassed
    // (shadows everywhere). Uses the Toggle transparency durations.
    static float s_togglePresence = 1.0f;
    {
        float toggleTarget = isBypassed ? 0.0f : 1.0f;
        if (s_togglePresence != toggleTarget) {
            int dur = isBypassed ? localSettings.toggleFadeOutDuration
                                 : localSettings.toggleFadeInDuration;
            float rate = (dur > 0) ? (1000.0f / (float)dur) : 1000.0f;
            float step = rate * deltaTime;
            s_togglePresence += (toggleTarget > s_togglePresence) ? step : -step;
            if (s_togglePresence < 0.0f) s_togglePresence = 0.0f;
            if (s_togglePresence > 1.0f) s_togglePresence = 1.0f;
        }
    }
    bool toggleAnimating = (isBypassed ? s_togglePresence > 0.0f : s_togglePresence < 1.0f);


    POINT localRaw = rawMouse;
    ScreenToClient(g_underlayWnd, &localRaw);

    bool justSnappedShadow = (!s_underlayPosInitialized || (boundaryCrossed && overDesktop && !effectiveGlobalTracking));

    if (justSnappedShadow) {
            // SNAP the shadow instantly to the cursor when re-entering the desktop!
            s_underlaySmoothedPos = {(float)localRaw.x, (float)localRaw.y};
            s_underlayPosInitialized = true;
    } else {
        float lerpFactor = CalculateLerpFactor(localSettings.mouseSmoothing, deltaTime);
        s_underlaySmoothedPos.x += ((float)localRaw.x - s_underlaySmoothedPos.x) * lerpFactor;
        s_underlaySmoothedPos.y += ((float)localRaw.y - s_underlaySmoothedPos.y) * lerpFactor;
    }

    // 1. Use locally smoothed mouse pos for drawing
    float mx = s_underlaySmoothedPos.x;
    float my = s_underlaySmoothedPos.y;

    float currentSpotFade = g_spotlightFade.load(std::memory_order_relaxed);
    bool spotFading = (std::abs(currentSpotFade - lastSpotFade) > 0.001f);
    lastSpotFade = currentSpotFade;

    // Grab a safe copy of the snapshot state ONLY if the generation changed
    uint64_t currentGen = g_snapshotGeneration.load(std::memory_order_acquire);
    bool iconsChanged = false;
    if (currentGen != localSnapGen) {
        ULONGLONG tCopy0 = GetTickCount64();
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        snapshots = g_iconSnapshots;
        localSnapGen = currentGen;
        iconsChanged = true;
        // [TEARDOWN v2.6.1] Measure the perceived teardown latency: the
        // frame that FIRST draws the post-zoom snapshot list. Old shadows
        // die exactly here — T+X after [ZOOM] IS the removal delay. Also
        // times the vector copy itself and catches slow copies on
        // non-zoom publishes (bounded, session-capped).
        ULONGLONG copyMs = GetTickCount64() - tCopy0;
        ULONGLONG zTick = g_lastZoomTick.load(std::memory_order_relaxed);
        ULONGLONG sinceZoom = zTick ? (GetTickCount64() - zTick) : 0;
        static int s_teardownLogged = 0;
        if (s_teardownLogged < 24) {
            if (zTick && sinceZoom < 5000) {
                ++s_teardownLogged;
                DiagLog(L"[TEARDOWN] post-zoom swap frame: T+%llums after [ZOOM], copy %llums, %zu snaps",
                        sinceZoom, copyMs, snapshots.size());
            } else if (copyMs > 3) {
                ++s_teardownLogged;
                DiagLog(L"[TEARDOWN] slow snapshot copy %llums (%zu snaps)", copyMs, snapshots.size());
            }
        }
    }

    bool forceRender = g_underlayForceRender.exchange(false, std::memory_order_relaxed);

    // 3. THE INFINITE GPU FURNACE FIX (Boundary-aware + Local Fade Polling)
    bool shadowsVisible = (localShadowFade > 0.001f);
    bool isFadingIn = shadowsShouldBeVisible && (localShadowFade < 0.999f);
    bool isFadingOut = !shadowsShouldBeVisible && (localShadowFade > 0.001f);
    bool isTransitioning = isFadingIn || isFadingOut;

    static bool s_maskRebuildPending = true;
    if (iconsChanged || forceRender) {
        s_maskRebuildPending = true;
    }

    bool maskNeeded = (localSettings.enableShadows || localSettings.overlayClipToIcons) && !iconsHidden;
    bool needsMaskRebuild = s_maskRebuildPending && maskNeeded;
    bool needsShadowRender = (shadowsVisible || isTransitioning) && (mouseMoved || isTransitioning || boundaryCrossed || forceRender || iconsChanged || spotFading || toggleAnimating);

    if (!needsShadowRender && !needsMaskRebuild) {
    bool keepAwake = isTransitioning || toggleAnimating || (localSettings.enableShadows && effectiveGlobalTracking);            DWORD sleepTime = keepAwake ? 16 : INFINITE;
        DWORD waitRes = WaitForSingleObject(g_underlayRenderEvent, sleepTime);
        // FIX: Prevent shadow opacity from jumping 50% on the first frame
        if (sleepTime == INFINITE && waitRes == WAIT_OBJECT_0) {
            QueryPerformanceCounter(&lastTime);
        }
        continue;
    }

    // 4. Lock GPU ONLY while drawing (Removed g_dxMutex for parallel rendering!)
    // --- THE TWO-PASS RENDER LOOP ---
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));

    shadowsVisible = (localSettings.enableShadows && localShadowFade > 0.001f);

    if (shadowsVisible || maskNeeded) {
        float radius = (float)localSettings.spotlightRadius * g_dpiScale;
        float maxShadowLength = (float)localSettings.shadowLength * g_dpiScale;

        // Hard cull only at full overlay presence; during the toggle animation
        // the edge fade below handles the transition pop-free.
        bool applyCull = localSettings.enableSpotlight && s_togglePresence > 0.999f;
        bool spotlightCull = localSettings.enableSpotlight; // geometry exists during the animation too
        float shadowCullRadius = 0.0f;

        if (spotlightCull) {
            float spotlightBlur = (std::max)(0.1f, (std::min)((float)localSettings.spotlightBlur * g_dpiScale, 250.0f));
            // 🦇 Oval bat style: the light is 2:1, so cull on the horizontal reach
            // or shadows would pop while still inside the lit ellipse.
            float spotAspect = (localSettings.batSignalEnabled && localSettings.batStyle == 1) ? kBatSpotOvalAspect : 1.0f;
            float spotlightReach = radius * spotAspect + (spotlightBlur * 3.0f);
            // Room for a shadow to reach full length before the boundary.
            shadowCullRadius = spotlightReach + spotlightBlur + maxShadowLength;
        }

        // --- PASS 1: DRAW UNIFIED SHADOWS (NEW PIPELINE) ---
        if (shadowsVisible) {
            // [TEARDOWN v2.6.1] Contention probe: this leaf mutex is held
            // for the ENTIRE draw loop, and the post-zoom worker hammers
            // it (177 SILFIRST publishes + preflight + GC). A stall here
            // delays the teardown frame — the number tells us whether the
            // perceived slow removal is lock racing or just frame cadence.
            ULONGLONG tShadowLock0 = GetTickCount64();
            std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
            ULONGLONG shadowLockStallMs = GetTickCount64() - tShadowLock0;
            if (shadowLockStallMs > 5) {
                DiagLog(L"[TEARDOWN] comboCache mutex stalled %llums before draw pass", shadowLockStallMs);
            }

            for (const auto & snap : snapshots) {
                if (snap.isDragged || snap.isPendingDrop) continue; // Suppress shadow at old spot during move
                // [EDIT-SHADOW-OFF v2.7.3] While this item's label is being
                // edited, no shadow at all: the glyph-shaped cutout was built
                // from the pre-edit text and never tracks keystrokes. Skips
                // regardless of LV state quirks during edit mode.
                if (snap.lvIndex >= 0 && snap.lvIndex == g_editingLvIndex.load(std::memory_order_relaxed)) continue;
                // Explorer keeps LVIS_SELECTED after the desktop deactivates, but
                // the label collapses and no active selection box is drawn —
                // shadow it like a normal collapsed icon there.
                if ((snap.state & LVIS_SELECTED) && g_isDesktopActive.load(std::memory_order_relaxed)) continue; // Shadows hidden for actively selected icons
                if (snap.pendingCollapse) continue; // Label not collapsed yet: silhouette would mismatch Explorer's expanded label
                // If deselected but still focused (sticky label) AND desktop is active, draw the extended shadow
                uint64_t targetHash = PickComboHash(snap);
                if (targetHash == 0) continue;

                auto it = g_comboShadowCache.find(targetHash);
                if (it == g_comboShadowCache.end()) {
                    // DIAGNOSTIC: skip the cold-start flood — only log once the
                    // first pre-flight has run (assets had a chance to exist).
                    static std::unordered_map<uint64_t, int> s_missingLogged;
                    if (g_shadowDiagWarm.load(std::memory_order_relaxed)) {
                        if (s_missingLogged[targetHash]++ < 3) {
                            DiagLog(L"[SHADOW] no assets for combo %llu (c=%llu e=%llu) expanded=%d '%s'",
                                   (unsigned long long)targetHash,
                                   (unsigned long long)snap.comboHash_c,
                                   (unsigned long long)snap.comboHash_e,
                                   (int)IsIconExpanded(snap),
                                   snap.displayName.empty() ? L"?" : snap.displayName.c_str());
                        }
                    }
                    continue;
                }
                const ShadowCache& cache = it->second;
                // [MULTIMON v2.9.4] Per-monitor render scale (D2 ruling:
                // shadows stay PROPORTIONATE on secondary monitors — blur was
                // baked primary-physical and is scaled down with the layer
                // bitmaps below; length cap and stretch offset scale here).
                float itemScale = GetItemMonitorScale(g_overlayWnd, snap.combinedRect);

                float ix = snap.combinedRect.left + (snap.combinedRect.right - snap.combinedRect.left) / 2.0f;
                float iy = snap.combinedRect.top + (snap.combinedRect.bottom - snap.combinedRect.top) / 2.0f;
                float distX = ix - mx;
                float distY = iy - my;
                float dist = std::sqrt(distX * distX + distY * distY);

                if (applyCull && dist > shadowCullRadius) continue;

                float dynamicShadowLength = std::min(dist * 0.2f, maxShadowLength * itemScale);
                float edgeFade = 1.0f;
                if (spotlightCull) {
                    // Fade completes at 40% of the cull radius; the rest is
                    // safety margin, so the hard cutoff below is always applied
                    // to a fully transparent shadow and can never pop.
                    float fadeEnd = shadowCullRadius * 0.8f;
                    edgeFade = 1.0f - (dist / fadeEnd);
                    if (edgeFade < 0.0f) edgeFade = 0.0f;
                }

                float baseAlpha = (localSettings.shadowBaseAlpha / 255.0f) * localShadowFade;
                // Blend cull fade (overlay on) toward full shadows (overlay off)
                // following the toggle animation, so both directions glide.
                float shadowMul = edgeFade * s_togglePresence + (1.0f - s_togglePresence);
                baseAlpha *= shadowMul;
                if (applyCull) {
                    bool idleOnDesktop = g_isIdle && overDesktop;
                    if (idleOnDesktop) {
                        baseAlpha *= currentSpotFade;
                    }
                }

                if (baseAlpha < 0.001f) continue;

                if (!cache.layers.empty()) {
                    int actualPasses = (int)cache.layers.size();
                    // [LENGTH-OFFSET DECOUPLE v2.8.2 — TEST] The offset used to
                    // be clamped to dynamicShadowLength, welding the two
                    // settings together: offset past the length silently did
                    // nothing. Now the offset answers only to shadowOffset ×
                    // (dist/800) — independent of shadowLength. Offset > length
                    // = the tail STARTS away from the icon (detached-shadow
                    // regime); Doc judges whether independence looks better.
                    float dynamicOffset = (float)localSettings.shadowOffset * (dist / 800.0f) * itemScale;

                    for (int p = 1; p <= actualPasses; ++p) {
                        float stretchFraction = (float)p / actualPasses;
                        float alphaFraction = (float)(p - 1) / actualPasses;
                        float stepOffsetX = 0.0f, stepOffsetY = 0.0f;
                        if (dist > 0.1f) {
                            float dirX = distX / dist;
                            float dirY = distY / dist;
                            float totalStretch = dynamicOffset + (dynamicShadowLength * stretchFraction);
                            stepOffsetX = dirX * totalStretch;
                            stepOffsetY = dirY * totalStretch;
                        }
                        float tailOpacity = localSettings.shadowTailOpacity / 100.0f;
                        float stepAlpha = baseAlpha * (1.0f - alphaFraction * (1.0f - tailOpacity));

                        // Skip threshold scales with layer count: N stacked layers
                        // each at the threshold would sum to a visible remnant that
                        // pops when they all vanish together. Keeping the SUMMED
                        // contribution below half an LSB makes the cutoff seamless.
                        float skipAlpha = 0.5f / 255.0f / (float)actualPasses;
                        if (stepAlpha < skipAlpha) continue;

                        const ShadowLayer& sl = cache.layers[p - 1];
                        if (!sl.bmp) continue;

                        // Anchor directly to the unified bounding box (icon + label)
                        // [MULTIMON v2.9.6] Layers anchor NATIVE: the baked
                        // canvas is already monitor-local (pc2 derives from
                        // the live per-monitor cell) and the blur sigma is
                        // now scaled at bake time. Only the settings-derived
                        // stretch (stepOffset above) and the length cap stay
                        // itemScale-scaled.
                        D2D1_POINT_2F targetOffset = D2D1::Point2F(
                            (float)snap.combinedRect.left + stepOffsetX - sl.offsetX,
                            (float)snap.combinedRect.top + stepOffsetY - sl.offsetY
                        );

                        D2D1_RECT_F destRect = D2D1::RectF(
                            targetOffset.x, targetOffset.y,
                            targetOffset.x + (float)sl.width, targetOffset.y + (float)sl.height
                        );

                        dc->DrawBitmap(sl.bmp.Get(), destRect, stepAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    }
                }
            }
        }


        // --- PASS 2: BUILD SHARED MASK (Only when geometry changes!) ---
        bool allAssetsReady = true;
        if (needsMaskRebuild) {
            {
                std::lock_guard<std::mutex> cacheLock(g_cacheMutex);
                for (const auto& snap : snapshots) {
                    if (snap.state & LVIS_SELECTED) continue;
                    if (g_iconCache.find(snap.cacheKey) == g_iconCache.end()) {
                        allAssetsReady = false;
                        break;
                    }
                }
            }
        }


        if (needsMaskRebuild) {
            int writeIdx = g_underlayWriteMaskIdx;
            ComPtr<ID2D1Bitmap1> maskTarget = g_sharedMask[writeIdx];
            if (maskTarget && g_maskScratchBitmap) {
                ULONGLONG tMask0 = GetTickCount64(); // [PERF-AUDIT v2.2.6]
                // 1. Draw all elements into the scratch mask using the single master composite texture!
            dc->SetTarget(g_maskScratchBitmap.Get());
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            for (const auto& snap : snapshots) {
                if (snap.isDragged || snap.isPendingDrop) continue; // Never stamp overlay or mask at the old spot

                // Explorer keeps LVIS_SELECTED after the desktop deactivates,
                // but the label collapses and no active selection box is drawn
                // — stamp the silhouette there, not a collapsed-sized RECT.
                bool isSelected = (snap.state & LVIS_SELECTED) != 0 &&
                                  g_isDesktopActive.load(std::memory_order_relaxed);
                // [FLASH-FIX v2.9.15] The collapse-hold RECT exists to cover
                // Explorer's still-visible SELECTION BOX during a live label
                // collapse — that box only exists while the desktop is
                // ACTIVE. Punching it while inactive (focus moved to a
                // window) left bare RECTANGLE cutouts on the 7 selected
                // folders until the hold released — the flash Doc caught
                // ([FLASH] rect-stamp hold=1 sel=0 receipts). Inactive =
                // silhouette path, exactly as the comment below always said.
                bool holdPunch = snap.pendingCollapse &&
                                 g_isDesktopActive.load(std::memory_order_relaxed);
                if (isSelected || holdPunch) {
                    if (g_whiteBitmap) {
                        // During a collapse hold we keep punching the selection-sized
                        // RECT so Explorer's lingering selection box stays uncovered.
                        const RECT& cutRect = snap.pendingCollapse ? snap.heldCollapseRect : snap.boundsRect;
                        float w = (float)(cutRect.right - cutRect.left);
                        float h = (float)(cutRect.bottom - cutRect.top);
                        float x = (float)cutRect.left;
                        float y = (float)cutRect.top;
                        if (w > 512.0f) w = 512.0f;
                        if (h > 512.0f) h = 512.0f;
                        D2D1_POINT_2F offset = D2D1::Point2F(x, y);
                        D2D1_RECT_F srcRect = D2D1::RectF(0, 0, w, h);
                        dc->DrawImage(g_whiteBitmap.Get(), &offset, &srcRect, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
                    }
                } else {
                        uint64_t targetHash = PickComboHash(snap);

                        ComPtr<ID2D1Bitmap1> masterBmp;

                        {
                            std::lock_guard<std::mutex> shadowLock(g_comboShadowCacheMutex);
                            auto it = g_comboShadowCache.find(targetHash);
                            if (it != g_comboShadowCache.end()) {
                                masterBmp = it->second.sourceBmp;
                            }
                            if (!masterBmp) {
                                // The preferred state's silhouette isn't baked yet
                                // (freshly computed combo hash after a label-rect
                                // wobble, post-recreate cache, preflight still in
                                // flight). Stamp the OTHER state's silhouette —
                                // right shape family, no white-rect blink — and
                                // keep the plain RECT only for brand-new icons
                                // where neither half exists.
                                uint64_t altHash = (targetHash == snap.comboHash_e) ? snap.comboHash_c
                                                                                    : snap.comboHash_e;
                                if (altHash != 0) {
                                    auto altIt = g_comboShadowCache.find(altHash);
                                    if (altIt != g_comboShadowCache.end()) {
                                        masterBmp = altIt->second.sourceBmp;
                                        static std::unordered_map<uint64_t, int> s_altUsedLogged;
                                        if (s_altUsedLogged[targetHash]++ < 5) {
                                            DiagLog(L"[MASK] alt-state silhouette for combo %llu (alt %llu, c=%llu e=%llu) '%s'",
                                                   (unsigned long long)targetHash,
                                                   (unsigned long long)altHash,
                                                   (unsigned long long)snap.comboHash_c,
                                                   (unsigned long long)snap.comboHash_e,
                                                   snap.displayName.empty() ? L"?" : snap.displayName.c_str());
                                        }
                                    }
                                }
                            }
                        }

                        if (masterBmp) {
                            D2D1_POINT_2F destPt = D2D1::Point2F(
                                (float)snap.combinedRect.left,
                                (float)snap.combinedRect.top
                            );
                            dc->DrawImage(masterBmp.Get(), &destPt, nullptr, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
                        } else if (g_whiteBitmap) {
                            // Last-resort cutout (brand-new icon, neither state baked).
                            // If this fires for an OLD icon, the [PRE]/[DISKCACHE] logs
                            // explain why the composite never landed.
                            static std::unordered_map<uint64_t, int> s_rectFallbackLogged;
                            if (s_rectFallbackLogged[targetHash]++ < 3) {
                                DiagLog(L"[MASK] white-rect fallback: no silhouette for combo %llu (c=%llu e=%llu) '%s'",
                                       (unsigned long long)targetHash,
                                       (unsigned long long)snap.comboHash_c,
                                       (unsigned long long)snap.comboHash_e,
                                       snap.displayName.empty() ? L"?" : snap.displayName.c_str());
                            }
                            float w = (float)(snap.boundsRect.right - snap.boundsRect.left);
                            float h = (float)(snap.boundsRect.bottom - snap.boundsRect.top);
                            if (w > 0 && h > 0) {
                                if (w > 512.0f) w = 512.0f;
                                if (h > 512.0f) h = 512.0f;
                                D2D1_POINT_2F offset = D2D1::Point2F((float)snap.boundsRect.left, (float)snap.boundsRect.top);
                                D2D1_RECT_F srcRect = D2D1::RectF(0, 0, w, h);
                                dc->DrawImage(g_whiteBitmap.Get(), &offset, &srcRect, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
                            }
                        }
                    }
                }

                // 2. Step 4: Pipe scratch mask through Micro-Blur (radial expansion) -> Soft Alpha Ramp -> final maskTarget
                dc->SetTarget(maskTarget.Get());
                dc->Clear(D2D1::ColorF(0, 0, 0, 0));

                if (g_maskBlurEffect && g_maskRampEffect) {
                    g_maskBlurEffect->SetInput(0, g_maskScratchBitmap.Get());
                    g_maskRampEffect->SetInputEffect(0, g_maskBlurEffect.Get());
                    dc->DrawImage(g_maskRampEffect.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
                } else {
                    dc->DrawBitmap(g_maskScratchBitmap.Get());
                }

                // CRITICAL: Flush the D2D command queue before flipping the buffer.
                dc->Flush();

                dc->SetTarget(target.Get());
                // [PERF-AUDIT v2.2.6] mask rebuild cost on the render thread.
                DiagLog(L"[MASKBUILD] mask rebuilt in %llu ms (%zu snapshots)",
                        (unsigned long long)(GetTickCount64() - tMask0), snapshots.size());

                // Flip the ping-pong buffer ONLY when we actually rebuilt it
                g_activeSharedMaskIdx.store(writeIdx, std::memory_order_release);
                g_underlayWriteMaskIdx = 1 - writeIdx;
                g_sharedMaskReady.store(true, std::memory_order_release);
                if (allAssetsReady) {
                    s_maskRebuildPending = false; // Successfully built all assets!
                }
                g_forceRender.store(true, std::memory_order_relaxed);
                if (g_renderEvent) SetEvent(g_renderEvent);

                if (g_startupGracePeriod.load(std::memory_order_relaxed)) {
                    g_startupGracePeriod.store(false, std::memory_order_release);
                }
            }
        }

        // Punch holes in the shadows using the CURRENT active mask (Safe from GPU race conditions!)
        if (shadowsVisible && maskNeeded) {
            int readIdx = g_activeSharedMaskIdx.load(std::memory_order_acquire);
            ComPtr<ID2D1Bitmap1> currentMask = g_sharedMask[readIdx];
            if (currentMask) {
                dc->DrawImage(currentMask.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_DESTINATION_OUT);
            }
        }

    } // End of shadowsVisible || maskNeeded check

    CheckEndDraw(dc->EndDraw(), L"RenderUnderlayLoop");
    HRESULT hrPresent = swapChain->Present(0, 0);
    if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET) {
        if (g_messageWnd) PostMessage(g_messageWnd, WM_APP_DEVICE_LOST, 0, 0);
    }

    // 4. PROPER FRAMERATE CAP
    // Using sleep_until avoids the Windows 15.6ms Sleep() timer resolution trap and arithmetic drift.
    CapFramerate(nextFrameTime, localSettings.targetFps);

    }
}


////////////////////////////////////////////////////////////////////////////////
// Window procedures

// The HWND our overlay is currently parented to — compared at external-destroy
// time to distinguish "host torn down" from "our window killed directly".
static HWND g_overlayParentHwnd = nullptr;

// [MULTIMON v2.9.4] Monitor-tile builder. The Shell renders the wallpaper
// INDEPENDENTLY per monitor (each screen fits/crops the image against its
// own dimensions — a 16:9 primary letterboxes differently than a 9:16
// portrait). One union-wide fit matches neither screen, so the overlay
// draws one tile per monitor, clipped. The tile vector itself is declared
// with the wallpaper globals above; same g_renderMutex guard.
// Runs on paths that already hold g_renderMutex, or lock it themselves
// (WM_DISPLAYCHANGE).

// Rebuild the tile table from the current display topology. Runs on paths
// that already hold g_renderMutex, or lock it themselves (WM_DISPLAYCHANGE).
// [MULTIMON v2.9.7] HARDENED after the display-sleep field case: a topology
// churn can invalidate the cached parent hwnd (host recreation) or race the
// origin probe — publishing tiles in SCREEN coords (origin=(0,0) fallback)
// shifted the whole replica one monitor-width left, leaving the primary's
// right quarter bare (log-receipt: tiles "origin=(0,0) (0,0)-(3840,2160)
// (-1080,116)-(0,2036)" at wake). On ANY probe failure we now KEEP the last
// good table instead of publishing garbage.
static void BuildMonitorTiles() {
    std::vector<MonitorTile> tiles;
    POINT origin = {0, 0};
    bool originValid = false;
    HWND hParent = (g_overlayParentHwnd && IsWindow(g_overlayParentHwnd)) ? g_overlayParentHwnd
                                                                          : GetDesktopParent();
    if (hParent && IsWindow(hParent)) {
        RECT rc = {};
        if (GetClientRect(hParent, &rc) && ClientToScreen(hParent, &origin)) {
            // origin = screen position of parent-client (0,0); children sit at
            // parent-client (0,0), so overlay-local = screen - origin.
            originValid = true;
        }
    }
    if (!originValid) {
        DiagLog(L"[MULTIMON] tiles probe FAILED (parent=%p alive=%d) - keeping last table",
                (void*)hParent, (hParent && IsWindow(hParent)) ? 1 : 0);
        return;
    }
    struct Ctx { POINT origin; std::vector<MonitorTile>* tiles; } ctx = { origin, &tiles };
    auto cb = [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
        Ctx* c = (Ctx*)lParam;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMonitor, &mi) && mi.rcMonitor.right > mi.rcMonitor.left && mi.rcMonitor.bottom > mi.rcMonitor.top) {
            MonitorTile t;
            t.rect.left   = mi.rcMonitor.left   - c->origin.x;
            t.rect.top    = mi.rcMonitor.top    - c->origin.y;
            t.rect.right  = mi.rcMonitor.right  - c->origin.x;
            t.rect.bottom = mi.rcMonitor.bottom - c->origin.y;
            c->tiles->push_back(t);
        }
        return TRUE;
    };
    EnumDisplayMonitors(nullptr, nullptr, cb, (LPARAM)&ctx);
    g_monitorTiles = std::move(tiles);
    Wh_Log(L"[MULTIMON] tiles built: %zu (origin %ld,%ld)", g_monitorTiles.size(), origin.x, origin.y);
    {
        std::wstring tileStr;
        for (const auto& t : g_monitorTiles) {
            wchar_t buf[96];
            swprintf_s(buf, L"(%ld,%ld)-(%ld,%ld) ", t.rect.left, t.rect.top, t.rect.right, t.rect.bottom);
            tileStr += buf;
        }
        DiagLog(L"[MULTIMON] tiles: %zu origin=(%ld,%ld) %s",
                 g_monitorTiles.size(), origin.x, origin.y, tileStr.c_str());
    }
}

// [MULTIMON v2.9.7] See forward declaration. The SetWindowPos calls fire the
// windows' own WM_WINDOWPOSCHANGED handlers, which do the heavy lifting
// (swapchain resize, dpi refresh, wallpaper refresh, tile rebuild, underlay
// force-render) — this function only decides WHEN and anchors position.
void RepairDesktopCoverage() {
    if (g_unloading.load(std::memory_order_relaxed)) return;
    if (!g_overlayWnd || !IsWindow(g_overlayWnd) || !g_underlayWnd || !IsWindow(g_underlayWnd)) return;
    HWND hParent = (g_overlayParentHwnd && IsWindow(g_overlayParentHwnd)) ? g_overlayParentHwnd
                                                                          : GetDesktopParent();
    if (!hParent || !IsWindow(hParent)) return;
    RECT rcParent = {};
    if (!GetClientRect(hParent, &rcParent)) return;
    int newWidth = rcParent.right - rcParent.left;
    int newHeight = rcParent.bottom - rcParent.top;
    if (newWidth <= 0 || newHeight <= 0) return;
    RECT rcCur = {};
    if (!GetClientRect(g_overlayWnd, &rcCur)) return;
    int curW = rcCur.right - rcCur.left;
    int curH = rcCur.bottom - rcCur.top;
    if (curW == newWidth && curH == newHeight) return; // healthy: no-op
    DiagLog(L"[MULTIMON] coverage repair: windows %dx%d -> parent %dx%d",
            curW, curH, newWidth, newHeight);
    SetWindowPos(g_overlayWnd, nullptr, 0, 0, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_underlayWnd, nullptr, 0, 0, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    HWND hLV = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        hLV = g_cachedHwndLV;
    }
    UpdateCachedImageList(hLV);
}

LRESULT CALLBACK OverlayWndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    try {
        return OverlayWndProcInner(hWnd, uMsg, wParam, lParam);
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] OverlayWndProc exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] OverlayWndProc unknown exception");
    }
    return 0;
}

LRESULT CALLBACK OverlayWndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_APP_SELECTION_UPDATE) {
        if (!g_unloading) {
            g_selectionUpdatePending.store(false, std::memory_order_relaxed);
            UpdateSelectionRects();
            QuickSnapshotStatePoll();
        }
        return 0;
    }

 if (uMsg == WM_APP_SNAPSHOT_UPDATE) {

     if (!g_unloading && g_snapshotUpdatePending.exchange(false)) {
         QueueFullSnapshot();
     }
     return 0;
 }

    switch (uMsg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_TIMER:
            if (!g_unloading && wParam == TIMER_ID_STATE_POLL) RevalidateDesktopWindows();
            if (!g_unloading && wParam == TIMER_ID_COLLAPSE_POLL) QuickSnapshotStatePoll();
            if (!g_unloading && wParam == TIMER_ID_ZOOM_SETTLE) {
                // [SLITHER v2.5.2] Post-zoom settle re-measure: the poll
                // re-reads live rects and re-syncs any that moved under it
                // (the probe proved the pass publishes ~2px-high rects when
                // it beats Explorer's settle). One shot.
                KillTimer(hWnd, TIMER_ID_ZOOM_SETTLE);
                DiagLog(L"[SLITHER] zoom settle re-poll fired");
                QuickSnapshotStatePoll();
            }
            if (!g_unloading && wParam >= TIMER_ID_FOCUS_RETRY && wParam <= TIMER_ID_FOCUS_RETRY + 2) {
                KillTimer(hWnd, wParam);
                g_selectionUpdatePending.store(true, std::memory_order_relaxed);
                PostMessageW(hWnd, WM_APP_SELECTION_UPDATE, 0, 0);
            }
        if (!g_unloading && wParam == TIMER_ID_SNAP_RETRY) {
             KillTimer(hWnd, TIMER_ID_SNAP_RETRY);
             g_snapRetryDeadline.store(0, std::memory_order_release);
             Wh_Log(L"[TIMER] SNAP_RETRY fired -> triggering snapshot update");
             g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
             PostMessageW(hWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
             // The drag also moved the SELECTION GLOW: g_selectedRects is only
             // refreshed by WM_APP_SELECTION_UPDATE, and a pure icon move
             // never changes the selection — so the glow sat at the pre-drag
             // spot until focus changed. Re-read the rects after the snap.
             g_selectionUpdatePending.store(true, std::memory_order_relaxed);
             PostMessageW(hWnd, WM_APP_SELECTION_UPDATE, 0, 0);
         }
            return 0;

        case WM_WINDOWPOSCHANGED: {
            const WINDOWPOS* wp = (const WINDOWPOS*)lParam;
            if (!(wp->flags & SWP_NOSIZE) && !g_unloading) {
                std::lock_guard<std::mutex> lock(g_renderMutex);

                // Only hit the heavy DirectX resize if the dimensions ACTUALLY changed
                if (g_overlayWidth != (UINT)wp->cx || g_overlayHeight != (UINT)wp->cy) {
                    g_overlayWidth  = (UINT)wp->cx;
                    g_overlayHeight = (UINT)wp->cy;

                    g_dpiScale = GetMonitorDpiScale(GetPrimaryMonitor());
                    BuildMonitorTiles(); // [MULTIMON v2.9.4]

                    ResizeSwapChain(wp->cx, wp->cy);
                    RefreshWallpaperAndStyle();
                    g_forceRender = true;
                    if (g_renderEvent) SetEvent(g_renderEvent);
                }
            }
            break;
        }

        case WM_DESTROY:
            // Signaling alone is NOT synchronization. If this destroy was
            // triggered externally (Explorer tearing down its WorkerW host from
            // under us) rather than by our own recreate path, the render thread
            // could be mid-frame against resources we are about to release.
            // Join it first; internal paths (recreate timer, unload) have
            // already joined, so this is a no-op for them.
            if (!g_isRecreating && !g_unloading) {
                // EXTERNAL destroy: our parent host was torn down under us.
                // The overlay vanishes and the 200ms recreate below brings it
                // back — this IS the boot blink if it fires during startup.
                HWND hParentCheck = GetDesktopParent();
                BootLog(L"[BOOT] Overlay WM_DESTROY EXTERNAL [BLINK] curParentAlive=%d ourParentStillAlive=%d (ourParent=%p)",
                        hParentCheck ? 1 : 0,
                        (g_overlayParentHwnd && IsWindow(g_overlayParentHwnd)) ? 1 : 0,
                        (void*)g_overlayParentHwnd);
            }
            g_threadStop = true;
            if (g_renderEvent) SetEvent(g_renderEvent);
            if (g_renderThread && g_renderThread->joinable()) {
                g_renderThread->join();
                g_renderThread.reset();
            }
            {
                std::lock_guard<std::mutex> lock(g_renderMutex);
                ReleaseSwapChainResources();
            }
            g_overlayWnd = nullptr;

            if (!g_unloading && g_messageWnd && !g_isRecreating)
                SetTimer(g_messageWnd, TIMER_ID_RECREATE_OVERLAY, 200, nullptr);
            return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MessageWndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MessageWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    try {
        return MessageWndProcInner(hWnd, uMsg, wParam, lParam);
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] MessageWndProc exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] MessageWndProc unknown exception");
    }
    return 0;
}

LRESULT CALLBACK MessageWndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_APP_FOCUSSTORM) {
        // [FOCUS-STORM HACK] Fire the focus storm on the UI thread. Intermediate
        // target = our own overlay window (topmost, click-transparent). If the
        // overlay can't take focus (WS_EX_NOACTIVATE policies vary), fall back
        // to the taskbar. Then hand focus back to the desktop ListView.
        HWND hLV = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            hLV = g_cachedHwndLV;
        }
        if (hLV && IsWindow(hLV) && !g_unloading) {
            HWND prev = GetForegroundWindow();
            BOOL toOverlay = SetForegroundWindow(g_overlayWnd);
            if (!toOverlay || GetForegroundWindow() != g_overlayWnd) {
                HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
                toOverlay = tray ? SetForegroundWindow(tray) : FALSE;
            }
            BOOL back = SetForegroundWindow(hLV);
            DiagLog(L"[STORM] focus toggle: prev=%p mid=%d back=%d fgNow=%p",
                    (void*)prev, (int)toOverlay, (int)back,
                    (void*)GetForegroundWindow());
        }
        return 0;
    }
    if (uMsg == WM_APP_DEFERRED_INIT) {
        if (!g_unloading) {
            ULONGLONG t0 = GetTickCount64();
            BootLog(L"[BOOT] DeferredInit: begin (UI thread)");
            // Query ListView and cache the icon size BEFORE loading the disk cache
            HWND hLV = GetDesktopListView();
            UpdateCachedImageList(hLV);

            CreateOverlayWindow();
            BootLog(L"[BOOT] DeferredInit: overlay ready (+%llu ms)", GetTickCount64() - t0);
            CreateUnderlayWindow();
            BootLog(L"[BOOT] DeferredInit: underlay ready (+%llu ms)", GetTickCount64() - t0);

            {
                HWND hLV = nullptr;
                { std::lock_guard<std::mutex> lock(g_stateMutex); hLV = g_cachedHwndLV; }
                UpdateCachedImageList(hLV);
            }

            // Guarantee one full snapshot pass happens with g_resourceDc and
            // g_workerWicFactory confirmed ready, so the worker thread's first
            // pass can't silently bail on icons before resources exist.
            if (g_overlayWnd) {
                g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
            }

            BuildGenericBaselines();
            BootLog(L"[BOOT] DeferredInit: baselines ready (+%llu ms)", GetTickCount64() - t0);

            // Load persisted icon shapes BEFORE the first deep pass so unchanged
            // icons skip shell extraction entirely.
            LoadIconShapes();

            // Aux threads (registry/folder watchers, snapshot worker) start
            // HERE, only in the desktop-owner process. Wh_ModInit used to spawn
            // them in EVERY explorer.exe instance the mod injects into.
            StartAuxThreads();

            // Separately, check once a second whether Explorer's desktop host has
            // stopped changing. Once it has, do a single reconciliation pass to make
            // sure the subclassing/mask/settings all match the now-final state -
            // not a repeated "is it stable yet" gate, just a one-time correctness check.
            SetTimer(hWnd, TIMER_ID_EXPLORER_SETTLE, 1000, nullptr);
        }
        return 0;
    }
    if (uMsg == WM_TIMER && wParam == TIMER_ID_EXPLORER_SETTLE) {
        static HWND s_lastSeenParent = nullptr;
        static int  s_settleHits = 0;

        HWND hParent = GetDesktopParent();
        if (hParent && hParent == s_lastSeenParent) {
            s_settleHits++;
        } else {
            s_lastSeenParent = hParent;
            s_settleHits = hParent ? 1 : 0;
        }

        if (s_settleHits >= 2) {
            KillTimer(hWnd, TIMER_ID_EXPLORER_SETTLE);
            BootLog(L"[BOOT] Explorer Settled (2s) -> final snapshot pass");
            Wh_Log(L"[TIMER] Explorer Settled (2s) -> triggering final snapshot pass");
            if (!g_unloading && g_overlayWnd) {
                RevalidateDesktopWindows();
                UpdateSelectionRects();
                QueueSnapshotUpdate(true);
                g_forceRender = true;
                g_underlayForceRender = true;
            }
            // Ensures grace period always clears even if shadows and clip-to-icons are both OFF
            g_startupGracePeriod.store(false, std::memory_order_release);
        }
        return 0;
    }
    if (uMsg == WM_APP_DEVICE_LOST) {
        if (!g_unloading && !g_isRecreating) SetTimer(hWnd, TIMER_ID_RECREATE_OVERLAY, 100, nullptr);
        return 0;
    }

    // [VD-DIAG] name every broadcast/refresh arriving at the message window
    if (uMsg == WM_SETTINGCHANGE || uMsg == WM_THEMECHANGED ||
        uMsg == WM_APP_TRIGGER_REFRESH || uMsg == WM_DISPLAYCHANGE) {
        DiagLog(L"[MSGWND] uMsg=%u wParam=%llu lParamStr='%s'",
                uMsg, (unsigned long long)wParam,
                (uMsg == WM_SETTINGCHANGE && lParam) ? (const wchar_t*)lParam : L"");
    }
    // [VD-FIX 2026-11-01] WM_SETTINGCHANGE arriving here directly is rare (the
    // desktop-chain forwarder is the usual path), but the same stringless VD
    // broadcast could hit this top-level window too. Same rule as the
    // forwarder: only real theme strings earn the heavy wipe; everything else
    // is ignored (its string is already logged by the [MSGWND] station above).
    if (uMsg == WM_SETTINGCHANGE) {
        const wchar_t* area = lParam ? (const wchar_t*)lParam : L"";
        bool isTheme = _wcsicmp(area, L"WindowsThemeElement") == 0 ||
                       _wcsicmp(area, L"ImmersiveColorSet") == 0;
        if (!isTheme) return 0;
    }
 if (uMsg == WM_APP_TRIGGER_REFRESH || uMsg == WM_THEMECHANGED || uMsg == WM_SETTINGCHANGE) {
     if (!g_unloading && g_overlayWnd) {

        // Wallpaper-only change: just re-read the wallpaper/background style.
        // Icon caches and silhouettes are unaffected — wiping them here turned
        // every wallpaper switch into a full re-extract + re-bake storm.
        if (uMsg == WM_APP_TRIGGER_REFRESH && wParam == REFRESH_TAG_WALLPAPER) {
            KillTimer(hWnd, TIMER_ID_WALLPAPER_UPDATE);
            SetTimer(hWnd, TIMER_ID_WALLPAPER_UPDATE, 200, nullptr);
            g_forceRender = true;
            g_underlayForceRender = true;
            if (g_renderEvent) SetEvent(g_renderEvent);
            if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
            return 0;
        }

            // Re-arm grace window so progressive icon resolution runs for all items
            g_startupGracePeriod.store(true, std::memory_order_release);

            // [VD-DIAG] mark that the heavy wipe actually ran, and why
            DiagLog(L"[WIPE] heavy cache wipe: uMsg=%u wParam=%llu",
                    uMsg, (unsigned long long)wParam);

            // Wipe icon and shadow caches so in-place theme changes (This PC, Recycle Bin) re-extract
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_iconCache.clear();
            }
            {
                std::lock_guard<std::mutex> hLock(g_hashCacheMutex);
                g_iconHashCache.clear();
            }
            {
                std::lock_guard<std::mutex> comboLock(g_comboShadowCacheMutex);
                g_comboShadowCache.clear();
            }
            {
                std::lock_guard<std::mutex> lock(g_overlayCacheMutex);
                g_cpuOverlaySilhouettes.clear();
            }

            // Rebuild generic baselines in case theme/system font changed
            BuildGenericBaselines();

            KillTimer(hWnd, TIMER_ID_WALLPAPER_UPDATE);
            SetTimer(hWnd, TIMER_ID_WALLPAPER_UPDATE, 200, nullptr);

            // Force a deep snapshot pass to rebuild all silhouettes with the new theme icons
            g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
            PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
        }
        return 0;
    }

    if (uMsg == WM_POWERBROADCAST && wParam == PBT_POWERSETTINGCHANGE && lParam) {
        const POWERBROADCAST_SETTING* pbs = (const POWERBROADCAST_SETTING*)lParam;
        if (IsEqualGUID(pbs->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && pbs->DataLength >= sizeof(DWORD)) {
            DWORD displayState = *(const DWORD*)pbs->Data;
            bool isNowOn = (displayState != 0); // 0 = off, 1 = on, 2 = dimmed
            bool wasOn = g_isDisplayOn.exchange(isNowOn, std::memory_order_relaxed);
            if (isNowOn && !wasOn) {
                g_forceRender = true;
                g_underlayForceRender = true;
                if (g_renderEvent) SetEvent(g_renderEvent);
                if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
            }
        }
        return TRUE;
    }

    if (uMsg == WM_APP_CLEANUP) {
        if (g_hPowerNotify) {
            UnregisterPowerSettingNotification(g_hPowerNotify);
            g_hPowerNotify = nullptr;
        }

        // Remove subclasses on creating UI thread
        RemoveDesktopSubclasses();

        // Kill all active timers
        KillTimer(hWnd, TIMER_ID_EXPLORER_SETTLE);
        KillTimer(hWnd, TIMER_ID_RECREATE_OVERLAY);
        KillTimer(hWnd, TIMER_ID_WALLPAPER_UPDATE);

        // Destroy child windows on their creating thread
        if (g_overlayWnd && IsWindow(g_overlayWnd)) {
            KillTimer(g_overlayWnd, TIMER_ID_STATE_POLL);
            KillTimer(g_overlayWnd, TIMER_ID_SNAP_RETRY);
            DestroyWindow(g_overlayWnd);
            g_overlayWnd = nullptr;
        }
        if (g_underlayWnd && IsWindow(g_underlayWnd)) {
            DestroyWindow(g_underlayWnd);
            g_underlayWnd = nullptr;
        }

        DestroyWindow(hWnd);
        g_messageWnd = nullptr;
        return 0;
    }

    switch (uMsg) {

        case WM_TIMER:
            if (!g_unloading) {
                if (wParam == TIMER_ID_RECREATE_OVERLAY) {
                    KillTimer(hWnd, TIMER_ID_RECREATE_OVERLAY);
                    g_isRecreating = true;

                    bool skipDestroy = g_startupGracePeriod.load(std::memory_order_relaxed);
                    BootLog(L"[BOOT] Recreate: skipDestroy=%d overlay=%p underlay=%p",
                            skipDestroy, (void*)g_overlayWnd, (void*)g_underlayWnd);

                    // Stop the render threads BEFORE destroying the windows or releasing resources
                    g_threadStop = true;
                    g_underlayThreadStop = true;
                    if (g_renderEvent) SetEvent(g_renderEvent);
                    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
                    if (g_renderThread && g_renderThread->joinable()) {
                        g_renderThread->join();
                        g_renderThread.reset();
                    }
                    if (g_underlayRenderThread && g_underlayRenderThread->joinable()) {
                        g_underlayRenderThread->join();
                        g_underlayRenderThread.reset();
                    }

                    if (!skipDestroy) {
                        if (g_underlayWnd) DestroyWindow(g_underlayWnd);
                        if (g_overlayWnd) DestroyWindow(g_overlayWnd);
                    } else {
                        // Grace period: Just tear down the GPU resources to prevent desktop blinks
                        std::lock_guard<std::mutex> lock(g_renderMutex);
                        ReleaseSwapChainResources();

                        std::lock_guard<std::mutex> underlayLock(g_underlayMutex);
                        g_underlayTargetBitmap.Reset();
                        g_underlayDc.Reset();
                        g_bakeDc.Reset();
                        g_underlaySwapChain.Reset();
                        g_underlayCompVisual.Reset();
                        g_underlayCompTarget.Reset();
                        g_underlayCompDevice.Reset();
                        g_stagingTexA.Reset();
                        g_underlayBlurEffect.Reset();
                        g_whiteBitmap.Reset();
                    }

                    CreateOverlayWindow();
                    CreateUnderlayWindow();
                    g_isRecreating = false;
                } else if (wParam == TIMER_ID_WALLPAPER_UPDATE) {
                    KillTimer(hWnd, TIMER_ID_WALLPAPER_UPDATE);
                    if (g_overlayWnd) {
                        std::lock_guard<std::mutex> lock(g_renderMutex);
                        RefreshWallpaperAndStyle();
                    }
                }
            }
            return 0;

        case WM_DISPLAYCHANGE:
            BootLog(L"[BOOT] WM_DISPLAYCHANGE (wparam=%llu)", (unsigned long long)wParam);
            // [MULTIMON v2.9.7] Same repair as the heartbeat watchdog — one
            // code path for both broadcast-driven and polled healing.
            if (!g_unloading && g_overlayWnd && g_underlayWnd) {
                {
                    std::lock_guard<std::mutex> lock(g_renderMutex);
                    BuildMonitorTiles(); // topology may have changed even at the same union size
                }
                RepairDesktopCoverage();
            }
            break;

        case WM_DESTROY:
            g_messageWnd = nullptr;
            return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK UnderlayWndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK UnderlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    try {
        return UnderlayWndProcInner(hWnd, uMsg, wParam, lParam);
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] UnderlayWndProc exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] UnderlayWndProc unknown exception");
    }
    return 0;
}

LRESULT CALLBACK UnderlayWndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCHITTEST) return HTTRANSPARENT;

    if (uMsg == WM_WINDOWPOSCHANGED) {
        const WINDOWPOS* wp = (const WINDOWPOS*)lParam;
        if (!(wp->flags & SWP_NOSIZE) && !g_unloading) {
            std::lock_guard<std::mutex> lock(g_underlayMutex);

            // Only update and wake the thread if dimensions actually changed!
            if (g_underlayWidth != (UINT)wp->cx || g_underlayHeight != (UINT)wp->cy) {
                g_underlayWidth = (UINT)wp->cx;
                g_underlayHeight = (UINT)wp->cy;

                g_underlayForceRender = true;
                if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
            }
        }
        return 0;
    }
    if (uMsg == WM_DESTROY) {
        // Same reasoning as the overlay's WM_DESTROY: signal AND join. Signaling
        // alone is not synchronization — an external destroy (Explorer tearing
        // down its WorkerW host) could catch the render thread mid-frame against
        // the COM resources reset below. Internal paths (recreate timer, unload)
        // have already joined by the time they destroy the window, so this is a
        // no-op for them.
        g_underlayThreadStop = true;
        if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
        if (g_underlayRenderThread && g_underlayRenderThread->joinable()) {
            g_underlayRenderThread->join();
            g_underlayRenderThread.reset();
        }
        {
            std::lock_guard <std::mutex> underlayLock(g_underlayMutex);
            g_underlayTargetBitmap.Reset();
            g_underlayDc.Reset();
            g_bakeDc.Reset();

            // DO NOT reset g_resourceDc here! It is actively used by the Snapshot Worker thread.
            // Resetting it here causes a race condition where CreateBitmap fails and silhouettes disappear.
            g_underlaySwapChain.Reset();

            g_underlayCompVisual.Reset();
            g_underlayCompTarget.Reset();
            g_underlayCompDevice.Reset();

            g_stagingTexA.Reset();

            // FIX: Lock mutexes before clearing to prevent race conditions
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_iconCache.clear();
            }

            {
                std::lock_guard <std::mutex > lock(g_comboShadowCacheMutex);
                g_comboShadowCache.clear();
            }

            {
                std::lock_guard<std::mutex> lock(g_hashCacheMutex);
                g_iconHashCache.clear();
            }
            {
                std::lock_guard<std::mutex> lock(g_uniqueShapesMutex);
                g_uniqueShapes.clear();
                g_shapeToBitmap.clear(); // FIX: Prevent COM leak / crash on unload
            }
            {
                std::lock_guard<std::mutex> lock(g_overlayCacheMutex);
                g_cpuOverlaySilhouettes.clear(); // plain CPU data, no COM held here
            }

            g_underlayBlurEffect.Reset();
            g_whiteBitmap.Reset();
        }
        g_underlayWnd = nullptr;
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////
// Setup

HWND CreateDesktopOverlayWindow(LPCWSTR className, WNDPROC wndProc, int* outWidth, int* outHeight) {
    HWND hParent = GetDesktopParent();
    if (!hParent) return nullptr;

    WNDCLASS wc = {};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = GetCurrentModuleHandle();
    wc.lpszClassName = className;
    if (RegisterClass(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }

    // Parent client area is the true coordinate canvas for all desktop monitors
    RECT rcParent = {};
    GetClientRect(hParent, &rcParent);
    int width = rcParent.right - rcParent.left;
    int height = rcParent.bottom - rcParent.top;
    if (width <= 0 || height <= 0) {
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;

    return CreateWindowEx(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        className, nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, width, height,
        hParent, nullptr, GetCurrentModuleHandle(), nullptr);
}


void CreateOverlayWindow() {
    if (g_overlayWnd && g_swapChain) {
        return;
    }
    auto scheduleRetry = [&]() {
        if (g_messageWnd && !g_unloading)
            SetTimer(g_messageWnd, TIMER_ID_RECREATE_OVERLAY, 500, nullptr);
    };
    if (!g_initialized) {
        if (!InitDirectX()) { Wh_Log(L"CreateOverlayWindow: InitDirectX failed"); scheduleRetry(); return; }
        g_initialized = true;
    } else {
        HRESULT hr = g_d3dDevice ? g_d3dDevice->GetDeviceRemovedReason() : E_FAIL;
        if (FAILED(hr)) {
            UninitDirectX();
            g_initialized = false;
            if (!InitDirectX()) { Wh_Log(L"CreateOverlayWindow: InitDirectX retry failed"); scheduleRetry(); return; }
            g_initialized = true;
        }
    }

    if (!g_overlayWnd) {
        int width = 0, height = 0;
        g_overlayWnd = CreateDesktopOverlayWindow(OVERLAY_WINDOW_CLASS, OverlayWndProc, &width, &height);
        if (!g_overlayWnd) {
            Wh_Log(L"CreateOverlayWindow: CreateDesktopOverlayWindow failed, error=%lu", GetLastError());
            scheduleRetry();
            return;
        }
        std::lock_guard<std::mutex> lock(g_renderMutex);
        g_overlayWidth  = (UINT)width;
        g_overlayHeight = (UINT)height;
        g_overlayParentHwnd = GetParent(g_overlayWnd);
        BuildMonitorTiles(); // [MULTIMON v2.9.4] initial tile table
    }

    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        if (CreateSwapChainResources(g_overlayWidth, g_overlayHeight)) {
            g_isIdle        = true;
            g_lastMouseTime = GetTickCount64();
            g_spotlightFade = 0.0f;
            g_selectionFade = 0.0f;
            g_shadowFade    = 0.0f;
            g_forceRender   = true;

            POINT pt; GetCursorPos(&pt);
            ScreenToClient(g_overlayWnd, &pt);
            g_smoothedMousePos = {(float)pt.x, (float)pt.y};

            if (!g_renderEvent)
                g_renderEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

            g_threadStop   = false;
            g_renderThread.reset(new std::thread([]() {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                RenderLoop();
            }));

            SetTimer(g_overlayWnd, TIMER_ID_STATE_POLL, 1000, nullptr);
        } else {
            // DO NOT DESTROY WINDOW HERE! Just schedule retry.
            // The window stays alive, preventing desktop blinks.
            scheduleRetry();
            return;
        }
    }

    HWND hLV = GetDesktopListView();
    if (hLV) ApplyDesktopSubclasses(hLV);

    UpdateSelectionRects();
}

void CreateUnderlayWindow() {
    if (g_underlayWnd && g_underlaySwapChain) {
        return;
    }

    if (!g_underlayWnd) {
        int width = 0, height = 0;
        g_underlayWnd = CreateDesktopOverlayWindow(UNDERLAY_WINDOW_CLASS, UnderlayWndProc, &width, &height);
        if (!g_underlayWnd) {
            return;
        }
        g_underlayForceRender = true; // Ensure mask is populated on first frame
        // THE Z-ORDER ANCHOR
        // 1. Push Underlay to absolute top (places it above the icons)
        SetWindowPos(g_underlayWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // 2. Push Overlay to absolute top (takes #1 spot, pushes Underlay to #2)
        SetWindowPos(g_overlayWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        std::lock_guard<std::mutex> lock(g_underlayMutex);
        g_underlayWidth = (UINT)width;
        g_underlayHeight = (UINT)height;
    }

    UINT width = 0, height = 0;
    {
        std::lock_guard<std::mutex> lock(g_underlayMutex);
        width = g_underlayWidth;
        height = g_underlayHeight;
    }

    // Initialize DirectX for Underlay (Shares the main D3D Device/Factory)
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = width; scd.Height = height;

    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    HRESULT hr = g_dxgiFactory->CreateSwapChainForComposition(g_dxgiDevice.Get(), &scd, nullptr, &g_underlaySwapChain);
    if (FAILED(hr) || !g_underlaySwapChain) {
        Wh_Log(L"[Underlay] Failed to create SwapChain: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }

    hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_underlayDc);
    if (FAILED(hr) || !g_underlayDc) {
        Wh_Log(L"[Underlay] Failed to create D2D Device Context: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }

    // Bake context for the snapshot worker (same device, separate context so
    // it never fights the render loop over targets).
    hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_bakeDc);
    if (FAILED(hr) || !g_bakeDc) {
        Wh_Log(L"[Underlay] Failed to create Bake D2D Device Context: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }

    if (!g_resourceDc) {
        hr = g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_resourceDc);
        if (FAILED(hr) || !g_resourceDc) {
            Wh_Log(L"[Underlay] Failed to create Resource D2D Context: 0x%08lX", hr);
            DestroyWindow(g_underlayWnd);
            g_underlayWnd = nullptr;
            return;
        }
    }

    // Pre-allocate staging textures to prevent CPU_READ allocation stalls in the TtF loop
    D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    g_underlayDc->CreateBitmap(D2D1::SizeU(STAGING_TEX_SIZE, STAGING_TEX_SIZE), nullptr, 0, &stagingProps, &g_stagingTexA);

    ComPtr <IDXGISurface2> surface;

    hr = g_underlaySwapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr) || !surface) {
        Wh_Log(L"[Underlay] Failed to get SwapChain Buffer: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }

    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = g_underlayDc->CreateBitmapFromDxgiSurface(surface.Get(), bp, &g_underlayTargetBitmap);
    if (FAILED(hr) || !g_underlayTargetBitmap) {
        Wh_Log(L"[Underlay] Failed to create Target Bitmap from Surface: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }

    // Initialize DirectComposition for Underlay using the GLOBAL pointer
    hr = DCompositionCreateDevice(g_dxgiDevice.Get(), IID_PPV_ARGS(&g_underlayCompDevice));
    if (FAILED(hr) || !g_underlayCompDevice) {
        Wh_Log(L"[Underlay] DComp Device failed: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }
    hr = g_underlayCompDevice->CreateTargetForHwnd(g_underlayWnd, TRUE, &g_underlayCompTarget);
    if (FAILED(hr) || !g_underlayCompTarget) {
        Wh_Log(L"[Underlay] DComp Target failed: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }
    hr = g_underlayCompDevice->CreateVisual(&g_underlayCompVisual);
    if (FAILED(hr) || !g_underlayCompVisual) {
        Wh_Log(L"[Underlay] DComp Visual failed: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }
    hr = g_underlayCompVisual->SetContent(g_underlaySwapChain.Get());
    if (FAILED(hr)) {
        Wh_Log(L"[Underlay] DComp SetContent failed: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }
    hr = g_underlayCompTarget->SetRoot(g_underlayCompVisual.Get());
    if (FAILED(hr)) {
        Wh_Log(L"[Underlay] DComp SetRoot failed: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }
    hr = g_underlayCompDevice->Commit();
    if (FAILED(hr)) {
        Wh_Log(L"[Underlay] DComp Commit failed: 0x%08lX", hr);
        DestroyWindow(g_underlayWnd);
        g_underlayWnd = nullptr;
        return;
    }

    if (!g_underlayRenderEvent)
        g_underlayRenderEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_underlayThreadStop = false;

    // Initialize Underlay Blur
    hr = g_underlayDc->CreateEffect(kCLSID_D2D1GaussianBlur, &g_underlayBlurEffect);
    if (g_underlayBlurEffect) {
        g_underlayBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.0f); // 4px blur radius
        g_underlayBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    }

    // Initialize Micro-Gaussian Blur (Step 4a: soft, radial sub-pixel expansion inside & outside)
    hr = g_underlayDc->CreateEffect(kCLSID_D2D1GaussianBlur, &g_maskBlurEffect);
    if (g_maskBlurEffect) {
        g_maskBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 0.65f * g_dpiScale);
        g_maskBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    }

    // Initialize Soft Alpha Ramp Effect (Step 4b: solid interior seal + anti-aliased subpixel perimeter)
    hr = g_underlayDc->CreateEffect(CLSID_D2D1ColorMatrix, &g_maskRampEffect);
    if (g_maskRampEffect) {
        D2D1_MATRIX_5X4_F rampMatrix = D2D1::Matrix5x4F(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 4.5f, // Solid interior seal (locks all icon body pixels to 100% opacity)
            0.0f, 0.0f, 0.0f, 0.0f
        );
        g_maskRampEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, rampMatrix);
        g_maskRampEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);
    }


    // Create 512x512 White Bitmap for Cookie Cutting selected bounds
    // 512 is the hard limit because desktop icon + text bounding boxes rarely exceed 256x256 + padding.
    std::vector<UINT8> whitePixels(512 * 512 * 4, 255);
    D2D1_BITMAP_PROPERTIES1 whiteProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    g_underlayDc->CreateBitmap(D2D1::SizeU(512, 512), whitePixels.data(), 512 * 4, &whiteProps, g_whiteBitmap.ReleaseAndGetAddressOf());

    // Construction phase is single-threaded: start the render loop only after
    // every resource it touches (dc, effects, white bitmap) exists.
    g_underlayRenderThread.reset(new std::thread([]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        RenderUnderlayLoop();
    }));

    // Trigger the initial snapshot!
    if (g_overlayWnd) {
        g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
        PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
    }
}

bool RegisterMessageWindowClass() {
    WNDCLASS wc = {};
    wc.lpfnWndProc   = MessageWndProc;
    wc.hInstance     = GetCurrentModuleHandle();
    wc.lpszClassName = MESSAGE_WINDOW_CLASS;
    if (RegisterClass(&wc) == 0)
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return true;
}

void CreateMessageWindow() {
    if (g_messageWnd) return;
    if (!RegisterMessageWindowClass()) return;

    g_messageWnd = CreateWindowEx(0, MESSAGE_WINDOW_CLASS, nullptr, 0, 0, 0, 0, 0,
                                  nullptr, nullptr, GetCurrentModuleHandle(), nullptr);
    if (g_messageWnd) {
        g_hPowerNotify = RegisterPowerSettingNotification(g_messageWnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
}

////////////////////////////////////////////////////////////////////////////////


LRESULT CALLBACK BootstrapHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && !g_initialized_desktop.load()) {
        g_initialized_desktop.store(true);

        // Unhook immediately to eliminate ALL overhead
        HHOOK hookToUnhook = g_bootstrapHook;
        g_bootstrapHook = nullptr;
        if (hookToUnhook) {
            UnhookWindowsHookEx(hookToUnhook);
        }

        // Safely create the message window on the correct UI thread
        CreateMessageWindow();
        if (g_messageWnd) {
            // Hand off the heavy lifting to the message pump to avoid blocking the hook
            PostMessageW(g_messageWnd, WM_APP_DEFERRED_INIT, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void BootstrapThreadInner();
void BootstrapThread() {
    try {
        BootstrapThreadInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] BootstrapThread exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] BootstrapThread unknown exception");
    }
}

void BootstrapThreadInner() {
    BootLog(L"[BOOT] BootstrapThread started");
    while (!g_unloading && !g_initialized_desktop.load()) {
        HWND hLV = GetDesktopListView();
        if (hLV && IsWindow(hLV)) {
            // Only the process that actually OWNS the desktop LV may proceed.
            // @include explorer.exe matches EVERY explorer.exe process (the
            // main desktop host + folder-window hosts spawned later), and each
            // instance used to install a cross-process WH_GETMESSAGE hook on
            // the LV thread it doesn't own. The hook fires in the LV owner's
            // address space (setting the OWNER's init flag), so the other
            // instances never see their own flag set and retry the
            // hook/unhook cycle on the desktop's main thread every ~7s,
            // forever — prime suspect for the boot-time Windhawk panic.
            DWORD lvPid = 0;
            GetWindowThreadProcessId(hLV, &lvPid);
            if (lvPid != GetCurrentProcessId()) {
                BootLog(L"[BOOT] Not the desktop owner process -> staying dormant");
                return;
            }
            BootLog(L"[BOOT] Bootstrap: desktop LV found (owner)");
            DWORD tid = GetWindowThreadProcessId(hLV, nullptr);
            if (tid != 0) {
                // Install hook ONLY on the Explorer UI thread
                g_bootstrapHook = SetWindowsHookExW(WH_GETMESSAGE, BootstrapHookProc, GetCurrentModuleHandle(), tid);
                if (g_bootstrapHook) {
                    // Wake up the thread's message pump to trigger the hook
                    PostThreadMessageW(tid, WM_NULL, 0, 0);

                    // Wait to see if it succeeds (wake instantly on unload)
                    for (int i = 0; i < 10 && !g_initialized_desktop.load(); ++i) {
                        if (WaitForSingleObject(g_hUnloadEvent, 500) == WAIT_OBJECT_0) break;
                    }
                    if (g_initialized_desktop.load()) {
                        break;
                    }
                    // Init failed this round: unhook before retrying, otherwise the
                    // stale hook stays installed on Explorer's UI thread while the
                    // next iteration overwrites g_bootstrapHook (leak).
                    if (g_bootstrapHook) {
                        UnhookWindowsHookEx(g_bootstrapHook);
                        g_bootstrapHook = nullptr;
                    }
                } else {
                }
            }
        }
        // Wait 2 seconds, but wake up INSTANTLY if the unload event is signaled.
        if (WaitForSingleObject(g_hUnloadEvent, 2000) == WAIT_OBJECT_0) {
            break;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////
// Mod lifecycle

// "Out of the darkness" tint presets. Names must match the $options list in
// the WindhawkModSettings block. Values are 0xRRGGBB.
struct TintPresetEntry { const wchar_t* name; uint32_t rgb; };
static const TintPresetEntry kTintPresets[] = {
    { L"Black",      0x000000 },
    { L"Slate",      0x16191E },
    { L"Midnight",   0x051529 },
    { L"Wine",       0x1A0F2B },
    { L"Nightshade", 0x220c29 },
    { L"Deep_Red",   0x290606 },
    { L"Umber",      0x26160f },
    { L"Sepia",      0x2b2009 },
    { L"Forest",     0x15200C },
    { L"Abyss",      0x042024 },

};



static bool ParseHexColor(const wchar_t* text, D2D1_COLOR_F& out) {
    if (!text) return false;
    while (*text == L' ' || *text == L'#') ++text;
    if (wcslen(text) != 6) return false;
    wchar_t buf[3] = {0,0,0};
    auto byteAt = [&](int i) -> int {
        buf[0] = text[i*2]; buf[1] = text[i*2+1];
        return (int)wcstoul(buf, nullptr, 16);
    };
    int r = byteAt(0), g = byteAt(1), b = byteAt(2);
    if (r < 0 || g < 0 || b < 0) return false;
    out = D2D1::ColorF((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);
    return true;
}

static void ResolveTintColor(PCWSTR presetName, PCWSTR customHex, D2D1_COLOR_F& out) {
    for (const auto& p : kTintPresets) {
        if (wcscmp(presetName, p.name) == 0) {
            out = D2D1::ColorF(p.rgb, 1.0f); // ColorF(UINT32) expects 0xRRGGBB
            return;
        }
    }
    // Unknown or "Custom": fall back to the hex field, then to Midnight.
    if (ParseHexColor(customHex, out)) return;
    out = D2D1::ColorF(0x0D / 255.0f, 0x1B / 255.0f, 0x2A / 255.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////
// Ab Tenebris config file (the windhawk2-era settings store)
//
// The settings window (tool mod) OWNS this file; the engine only reads it,
// plus a one-time self-import when it doesn't exist yet (writes the current
// Windhawk settings values out — the import tool is the fallback loader).
// Keys mirror the Windhawk settings keys exactly. Missing keys inside the
// file fall back to the built-in defaults table (kCfgDefaults below — the
// bootstrap the removed Windhawk settings block used to provide), so the
// engine still boots sane if the settings window never wrote the file.

static const wchar_t kAbTenebrisDirName[] = L"AbTenebris";
static const wchar_t kAbTenebrisCfgName[] = L"settings.cfg";

struct CfgKeyEntry { PCWSTR key; bool isString; };
// Order here = file layout order. isString keys are stored verbatim. The
// legacy import path below still reads them through Wh_Get*Setting (kept for
// users upgrading from a build that still had a settings block).
static const CfgKeyEntry kCfgKeys[] = {
    {L"Shadows.enableShadows", false},
    {L"Shadows.shadowLength", false},
    {L"Shadows.shadowPasses", false},
    {L"Shadows.shadowBaseAlpha", false},
    {L"Shadows.shadowBlur", false},
    {L"Shadows.shadowTailSoftness", false},
    {L"Shadows.shadowTailOpacity", false},
    {L"Shadows.shadowTailScale", false},
    {L"Shadows.shadowOffset", false},
    {L"Shadows.shadowGlobalTracking", false},
    {L"Shadows.shadowFadeInDuration", false},
    {L"Shadows.shadowFadeOutDuration", false},
    {L"Spotlight.enableSpotlight", false},
    {L"Spotlight.idleOpacity", false},
    {L"Spotlight.overlayClipToIcons", false},
    {L"Spotlight.spotlightRadius", false},
    {L"Spotlight.spotlightRoundness", false},
    {L"Spotlight.spotlightBlur", false},
    {L"Spotlight.idleDelay", false},
    {L"Spotlight.spotlightFadeInDuration", false},
    {L"Spotlight.spotlightFadeOutDuration", false},
    {L"Spotlight.mouseSmoothing", false},
    {L"Tint.tintEnabled", false},
    {L"Tint.tintStrength", false},
    {L"Tint.tintPreset", true},
    {L"Tint.tintColorHex", true},
    {L"Selection.enableSelectionGlow", false},
    {L"Selection.selectionOpacity", false},
    {L"Selection.selectionPadding", false},
    {L"Selection.selectionRoundness", false},
    {L"Selection.selectionBlur", false},
    {L"Selection.selectionTimeout", false},
    {L"Selection.selectionFadeInDuration", false},
    {L"Selection.selectionFadeOutDuration", false},
    {L"Toggle.toggleOnDoubleClick", false},
    {L"Toggle.toggleFadeInDuration", false},
    {L"Toggle.toggleFadeOutDuration", false},
    {L"General.targetFps", false},
    {L"General.cacheMode", false},
    {L"General.customArrowScale", false},
    {L"General.customArrowOffsetX", false},
    {L"General.customArrowOffsetY", false},
    {L"batSignalEnabled", false},
    {L"batSignalStyle", true},
    {L"batLogoBlur", true},   // "auto" or a number — string on purpose
};

// Built-in defaults — the bootstrap the Windhawk settings block used to
// provide. Used when settings.cfg is missing or a key is absent from it, and
// to seed a fresh install's first settings.cfg. Values mirror the old YAML.
struct CfgDefault { PCWSTR key; int i; PCWSTR s; };
static const CfgDefault kCfgDefaults[] = {
    {L"Shadows.enableShadows", 1, nullptr},
    {L"Shadows.shadowLength", 160, nullptr},
    {L"Shadows.shadowPasses", 6, nullptr},
    {L"Shadows.shadowBaseAlpha", 140, nullptr},
    {L"Shadows.shadowBlur", 2, nullptr},
    {L"Shadows.shadowTailSoftness", 50, nullptr},
    {L"Shadows.shadowTailOpacity", 0, nullptr},
    {L"Shadows.shadowTailScale", 0, nullptr},
    {L"Shadows.shadowOffset", 10, nullptr},
    {L"Shadows.shadowGlobalTracking", 0, nullptr},
    {L"Shadows.shadowFadeInDuration", 200, nullptr},
    {L"Shadows.shadowFadeOutDuration", 500, nullptr},
    {L"Spotlight.enableSpotlight", 1, nullptr},
    {L"Spotlight.idleOpacity", 225, nullptr},
    {L"Spotlight.overlayClipToIcons", 0, nullptr},
    {L"Spotlight.spotlightRadius", 360, nullptr},
    {L"Spotlight.spotlightRoundness", 70, nullptr},
    {L"Spotlight.spotlightBlur", 40, nullptr},
    {L"Spotlight.idleDelay", 3000, nullptr},
    {L"Spotlight.spotlightFadeInDuration", 100, nullptr},
    {L"Spotlight.spotlightFadeOutDuration", 2000, nullptr},
    {L"Spotlight.mouseSmoothing", 0, nullptr},
    {L"Tint.tintEnabled", 1, nullptr},
    {L"Tint.tintStrength", 25, nullptr},
    {L"Tint.tintPreset", 0, L"Midnight"},
    {L"Tint.tintColorHex", 0, L"#0D1B2A"},
    {L"Selection.enableSelectionGlow", 1, nullptr},
    {L"Selection.selectionOpacity", 255, nullptr},
    {L"Selection.selectionPadding", 60, nullptr},
    {L"Selection.selectionRoundness", 10, nullptr},
    {L"Selection.selectionBlur", 50, nullptr},
    {L"Selection.selectionTimeout", 2000, nullptr},
    {L"Selection.selectionFadeInDuration", 250, nullptr},
    {L"Selection.selectionFadeOutDuration", 4000, nullptr},
    {L"Toggle.toggleOnDoubleClick", 1, nullptr},
    {L"Toggle.overlayEnabled", 1, nullptr},
    {L"Toggle.toggleFadeInDuration", 400, nullptr},
    {L"Toggle.toggleFadeOutDuration", 400, nullptr},
    {L"General.targetFps", 60, nullptr},
    {L"General.cacheMode", 1, nullptr},
    {L"General.customArrowEnabled", 1, nullptr},
    {L"General.customArrowScale", 75, nullptr},
    {L"General.customArrowOffsetX", 0, nullptr},
    {L"General.customArrowOffsetY", 0, nullptr},
    {L"batSignalEnabled", 0, nullptr},
    {L"batSignalStyle", 0, L"🦧"},
    {L"batLogoBlur", 0, L"auto"},
};
static const CfgDefault* FindCfgDefault(PCWSTR key) {
    for (const auto& d : kCfgDefaults) {
        if (wcscmp(d.key, key) == 0) return &d;
    }
    return nullptr;
}
static int CfgIntDefault(PCWSTR key) {
    const CfgDefault* d = FindCfgDefault(key);
    return d ? d->i : 0;
}
static std::wstring CfgStrDefault(PCWSTR key) {
    const CfgDefault* d = FindCfgDefault(key);
    return d ? d->s : std::wstring();
}

static std::wstring GetAbTenebrisConfigDir() {
    std::wstring dir;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        dir = std::wstring(local) + L"\\" + kAbTenebrisDirName;
        CoTaskMemFree(local);
    }
    return dir;
}

static std::wstring GetAbTenebrisConfigPath() {
    std::wstring dir = GetAbTenebrisConfigDir();
    if (dir.empty()) return dir;
    return dir + L"\\" + kAbTenebrisCfgName;
}

static void TrimWstring(std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) { s.clear(); return; }
    size_t e = s.find_last_not_of(L" \t\r\n");
    s = s.substr(b, e - b + 1);
}

// Reads key=value lines (UTF-16 LE with BOM, or UTF-8). Returns false only
// when the file couldn't be opened. An empty/unparseable file yields an
// empty map — every key then falls back to the Windhawk block.
static bool ReadAbTenebrisConfig(const std::wstring& path,
                                 std::unordered_map<std::wstring, std::wstring>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::wstring content;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 1024 * 1024) {
        std::vector<char> bytes((size_t)size.QuadPart);
        DWORD got = 0;
        if (ReadFile(h, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got >= 2) {
            const unsigned char* p = (const unsigned char*)bytes.data();
            if (p[0] == 0xFF && p[1] == 0xFE) {
                content.assign((const wchar_t*)(p + 2), (got - 2) / 2);
            } else {
                int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)p, got, nullptr, 0);
                if (wlen > 0) {
                    std::wstring w(wlen, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, (const char*)p, got, &w[0], wlen);
                    content = std::move(w);
                }
            }
        }
    }
    CloseHandle(h);

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        size_t end = (eol == std::wstring::npos) ? content.size() : eol;
        std::wstring line = content.substr(pos, end - pos);
        pos = (eol == std::wstring::npos) ? content.size() : eol + 1;
        TrimWstring(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        TrimWstring(key);
        TrimWstring(val);
        if (!key.empty()) out[std::move(key)] = std::move(val);
    }
    return true;
}

// Atomic write per house style: temp file + MoveFileExW (AV keeps temp files
// busy; the move retries are cheap and the watcher's LastWriteTime compare
// ignores intermediate states anyway).
static bool WriteAbTenebrisConfig(const std::unordered_map<std::wstring, std::wstring>& kv) {
    std::wstring dir = GetAbTenebrisConfigDir();
    if (dir.empty()) return false;
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring tmp = dir + L"\\settings.cfg.tmp";
    std::wstring final = dir + L"\\" + kAbTenebrisCfgName;

    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    WORD bom = 0xFEFF;
    DWORD wrote = 0;
    WriteFile(h, &bom, sizeof(bom), &wrote, nullptr);

    std::wstring body;
    for (const auto& e : kCfgKeys) {
        auto it = kv.find(e.key);
        if (it == kv.end()) continue;
        body += e.key;
        body += L'=';
        body += it->second;
        body += L"\r\n";
    }
    BOOL ok = WriteFile(h, body.data(), (DWORD)(body.size() * sizeof(wchar_t)), &wrote, nullptr);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// One-time import: snapshot the current Windhawk BIOS values into the file.
static bool ImportAbTenebrisConfigFromWindhawk() {
    std::unordered_map<std::wstring, std::wstring> kv;
    wchar_t buf[32];
    bool blockHasValues = false;
    for (const auto& e : kCfgKeys) {
        if (e.isString) {
            PCWSTR s = Wh_GetStringSetting(e.key);
            kv[e.key] = (s && *s) ? s : L"";
            Wh_FreeStringSetting(s);
        } else {
            int v = Wh_GetIntSetting(e.key);
            swprintf(buf, 32, L"%d", v);
            kv[e.key] = buf;
            if (v != 0) blockHasValues = true;
        }
        if (e.isString && !kv[e.key].empty()) blockHasValues = true;
    }
    if (!blockHasValues) {
        // No Windhawk settings block anymore (it was removed when the control
        // panel merged in). Seed the file with the built-in defaults instead
        // so a fresh install boots with the classic configuration.
        kv.clear();
        for (const auto& d : kCfgDefaults) {
            if (d.s) {
                kv[d.key] = d.s;
            } else {
                swprintf(buf, 32, L"%d", d.i);
                kv[d.key] = buf;
            }
        }
    }
    bool ok = WriteAbTenebrisConfig(kv);
    Wh_Log(L"[CFG] self-import %hs", ok ? L"written" : L"FAILED");
    return ok;
}

// --- config-file watcher ---------------------------------------------------
// Watches %LOCALAPPDATA%\AbTenebris for settings.cfg changes (the settings
// window writes it) and re-runs the settings pipeline. Same lifecycle as the
// registry watcher: started from StartAuxThreads (desktop-owner gated),
// stopped via g_hCfgStopEvent + g_unloading.

void ApplySettingsChanges(); // forward: defined after LoadSettings (non-static, matches decl)

void ConfigWatcherThreadProcInner() {
    auto queryWrite = [&]() -> ULONGLONG {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        std::wstring path = GetAbTenebrisConfigPath();
        if (path.empty() ||
            !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
        return ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) |
                (ULONGLONG)fa.ftLastWriteTime.dwLowDateTime;
    };
    ULONGLONG lastSeen = queryWrite();

    HANDLE hChange = INVALID_HANDLE_VALUE;
    while (!g_unloading) {
        if (hChange == INVALID_HANDLE_VALUE) {
            std::wstring dir = GetAbTenebrisConfigDir();
            if (!dir.empty()) {
                hChange = FindFirstChangeNotificationW(
                    dir.c_str(), FALSE,
                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_SIZE);
            }
            if (hChange == INVALID_HANDLE_VALUE) {
                // Directory may not exist yet (tool mod not installed); retry.
                if (WaitForSingleObject(g_hCfgStopEvent, 5000) == WAIT_OBJECT_0) break;
                continue;
            }
        }

        HANDLE waits[2] = { hChange, g_hCfgStopEvent };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 1 || g_unloading) break;
        if (w != WAIT_OBJECT_0) break;

        // Debounce: temp+rename writes fire several notifications. Drain the
        // burst (each signal needs FindNext to rearm), then apply once.
        for (int i = 0; i < 20 && !g_unloading; ++i) {
            if (WaitForSingleObject(hChange, 150) != WAIT_OBJECT_0) break;
            FindNextChangeNotification(hChange);
        }
        FindNextChangeNotification(hChange);

        ULONGLONG now = queryWrite();
        if (now != lastSeen && !g_unloading) {
            lastSeen = now;
            DiagLog(L"[CFG] settings.cfg changed -> reload");
            ApplySettingsChanges();
        }
    }
    if (hChange != INVALID_HANDLE_VALUE) FindCloseChangeNotification(hChange);
}

void ConfigWatcherThreadProc() {
    try {
        ConfigWatcherThreadProcInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] ConfigWatcher exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] ConfigWatcher unknown exception");
    }
}

void LoadSettings() {
    // Ab Tenebris config file first; per-key fallback to the Windhawk block
    // (bootstrap). Missing file = one-time self-import from Windhawk values.
    std::wstring cfgPath = GetAbTenebrisConfigPath();
    std::unordered_map<std::wstring, std::wstring> cfg;
    if (cfgPath.empty() || !ReadAbTenebrisConfig(cfgPath, cfg) || cfg.empty()) {
        ImportAbTenebrisConfigFromWindhawk();
        if (!cfgPath.empty()) ReadAbTenebrisConfig(cfgPath, cfg);
    }
    auto cfgInt = [&](PCWSTR key) -> int {
        auto it = cfg.find(key);
        return (it != cfg.end()) ? _wtoi(it->second.c_str()) : CfgIntDefault(key);
    };
    auto cfgIntDef = [&](PCWSTR key, int def) -> int {
        auto it = cfg.find(key);
        return (it != cfg.end()) ? _wtoi(it->second.c_str()) : def;
    };
    auto cfgStr = [&](PCWSTR key) -> std::wstring {
        auto it = cfg.find(key);
        if (it != cfg.end()) return it->second;
        std::wstring d = CfgStrDefault(key);
        return d;
    };

    Settings s;
    s.enableShadows            = cfgInt(L"Shadows.enableShadows") != 0;
    s.enableSpotlight          = cfgInt(L"Spotlight.enableSpotlight") != 0;
    s.enableSelectionGlow      = cfgInt(L"Selection.enableSelectionGlow") != 0;
    s.idleOpacity              = std::clamp(cfgInt(L"Spotlight.idleOpacity"), 0, 255);
    s.overlayClipToIcons       = cfgInt(L"Spotlight.overlayClipToIcons") != 0;
    s.spotlightRadius          = std::max(cfgInt(L"Spotlight.spotlightRadius"), 10);
    s.spotlightRoundness       = std::clamp(cfgInt(L"Spotlight.spotlightRoundness"), 0, 100);
    s.spotlightBlur            = std::max(cfgInt(L"Spotlight.spotlightBlur"), 0);
    s.idleDelay                = std::max(cfgInt(L"Spotlight.idleDelay"), 0);
    s.spotlightFadeInDuration  = std::max(cfgInt(L"Spotlight.spotlightFadeInDuration"), 0);
    s.spotlightFadeOutDuration = std::max(cfgInt(L"Spotlight.spotlightFadeOutDuration"), 0);
    s.mouseSmoothing           = std::clamp(cfgInt(L"Spotlight.mouseSmoothing"), 0, 20);
    s.selectionOpacity         = std::clamp(cfgInt(L"Selection.selectionOpacity"), 0, 255);
    s.selectionPadding         = std::max(cfgInt(L"Selection.selectionPadding"), 0);
    s.selectionRoundness       = std::clamp(cfgInt(L"Selection.selectionRoundness"), 0, 100);
    s.selectionBlur            = std::clamp(cfgInt(L"Selection.selectionBlur"), 0, 100);
    s.selectionTimeout         = std::max(cfgInt(L"Selection.selectionTimeout"), 0);
    s.selectionFadeInDuration  = std::max(cfgInt(L"Selection.selectionFadeInDuration"), 0);
    s.selectionFadeOutDuration = std::max(cfgInt(L"Selection.selectionFadeOutDuration"), 0);
    s.toggleOnDoubleClick      = cfgInt(L"Toggle.toggleOnDoubleClick") != 0;
    s.overlayEnabled           = cfgIntDef(L"Toggle.overlayEnabled", 1) != 0;
    s.toggleFadeInDuration     = std::max(cfgInt(L"Toggle.toggleFadeInDuration"), 0);
    s.toggleFadeOutDuration    = std::max(cfgInt(L"Toggle.toggleFadeOutDuration"), 0);
    s.shadowLength             = std::max(cfgInt(L"Shadows.shadowLength"), 0);
    s.shadowPasses             = std::clamp(cfgInt(L"Shadows.shadowPasses"), 1, 64);
    s.shadowBaseAlpha          = std::clamp(cfgInt(L"Shadows.shadowBaseAlpha"), 0, 255);
    s.shadowBlur               = std::max(cfgInt(L"Shadows.shadowBlur"), 0);
    s.shadowTailSoftness       = std::clamp(cfgInt(L"Shadows.shadowTailSoftness"), 0, 100);
    s.shadowTailOpacity        = std::clamp(cfgInt(L"Shadows.shadowTailOpacity"), 0, 100);
    s.shadowTailScale         = std::clamp(cfgInt(L"Shadows.shadowTailScale"), -100, 100);
    s.shadowOffset             = std::clamp(cfgInt(L"Shadows.shadowOffset"), 0, 500);
    s.shadowGlobalTracking     = cfgInt(L"Shadows.shadowGlobalTracking") != 0;
    s.shadowFadeInDuration     = std::max(cfgInt(L"Shadows.shadowFadeInDuration"), 0);
    s.shadowFadeOutDuration    = std::max(cfgInt(L"Shadows.shadowFadeOutDuration"), 0);
    s.targetFps                = std::clamp(cfgInt(L"General.targetFps"), 15, 360);
    s.cacheMode                = std::clamp(cfgInt(L"General.cacheMode"), 0, 2);
    s.customArrowEnabled       = cfgIntDef(L"General.customArrowEnabled", 1) != 0;
    s.customArrowScale         = std::clamp(cfgInt(L"General.customArrowScale"), 10, 200);
    s.customArrowOffsetX       = std::clamp(cfgInt(L"General.customArrowOffsetX"), -200, 200);
    s.customArrowOffsetY       = std::clamp(cfgInt(L"General.customArrowOffsetY"), -200, 200);

    // Overlay tint
    s.tintEnabled              = cfgInt(L"Tint.tintEnabled") != 0;
    s.tintStrength             = std::clamp(cfgInt(L"Tint.tintStrength"), 0, 100);
    std::wstring tintPreset    = cfgStr(L"Tint.tintPreset");
    std::wstring tintHex       = cfgStr(L"Tint.tintColorHex");
    ResolveTintColor(tintPreset.c_str(), tintHex.c_str(), s.tintColor);

    // 🦇 The signal demands a perfectly round projection. Edge blur is NOT
    // forced: the user's blur slider drives both the spotlight edge and the
    // bat silhouette (which uses half of it), keeping them visually linked.
    s.batSignalEnabled = cfgInt(L"batSignalEnabled") != 0;
    std::wstring batStyleStr = cfgStr(L"batSignalStyle");
    s.batStyle = batStyleStr == L"🦇🦇" ? 1 :
                 batStyleStr == L"🦇🦇🦇" ? 2 : 0;
    std::wstring batLogoBlurStr = cfgStr(L"batLogoBlur");
    s.batLogoBlur = batLogoBlurStr == L"auto" ? -1 : _wtoi(batLogoBlurStr.c_str());
    if (s.batSignalEnabled) {
        s.spotlightRoundness = 100;
        // An explicit logo blur drives the spotlight edge blur too (the logo
        // stays 1/6 of it, keeping the spotlight-blurrier-than-logo link).
        // "Auto" keeps the user's spotlight blur and derives the logo from it.
        if (s.batLogoBlur >= 0) {
            s.spotlightBlur = s.batLogoBlur;
        }
    }

    static bool s_isFirstSettingsLoad = true;
    bool shadowBakeChanged = !s_isFirstSettingsLoad && (s.shadowPasses != g_settings.shadowPasses ||
                              s.shadowBlur != g_settings.shadowBlur ||
                              s.shadowTailSoftness != g_settings.shadowTailSoftness ||
                              s.shadowLength != g_settings.shadowLength ||
                              s.shadowOffset != g_settings.shadowOffset ||
                              s.shadowTailScale != g_settings.shadowTailScale);
    bool cacheModeChanged = !s_isFirstSettingsLoad && (s.cacheMode != g_settings.cacheMode);
    bool arrowSettingsChanged = !s_isFirstSettingsLoad && (s.customArrowScale != g_settings.customArrowScale ||
                                 s.customArrowOffsetX != g_settings.customArrowOffsetX ||
                                 s.customArrowOffsetY != g_settings.customArrowOffsetY);
    s_isFirstSettingsLoad = false;
    // [P1.1] Tell Wh_ModSettingsChanged whether the skip guard must stand
    // down. First load is suppressed: at mod boot the mirror is empty
    // anyway, and an install-time settings reload changes nothing.
    g_bakedInputsChanged.store(shadowBakeChanged || arrowSettingsChanged,
                               std::memory_order_relaxed);

    // 1. Update settings under g_renderMutex and release immediately (prevents deadlocks!)
    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        g_settings    = s;
        g_forceRender = true;
        BakeSpotlight();
    }

    g_diskCache.SetCacheMode(s.cacheMode);

    // 2. Clear caches independently without holding g_renderMutex
    // NOTE: cacheModeChanged alone must NOT invalidate baked shadows — the
    // composites are identical no matter where they get persisted, so wiping
    // g_comboShadowCache would just cause a pointless re-bake flicker.
    // (The combo cache itself is cleared once below, under
    //  shadowBakeChanged || arrowSettingsChanged.)

    if (cacheModeChanged) {
        if (s.cacheMode == 0) {
            g_diskCache.ClearMemoryCache();
            g_diskCache.DeleteAllZoomCacheFiles(); // [STAGE A v2.4.2] all zoom files
        } else {
            // Entering single/multi mode: reuse whatever is already in memory.
            // Single-profile keeps only the CURRENT state's profile (computed
            // from the new settings) so the existing file stays valid instead
            // of being deleted and rebuilt.
            if (s.cacheMode == 1) {
                g_diskCache.TrimToProfile(GetShadowBakeHash(s));
            }
            // Mark dirty explicitly: a stale-clear flag (saves attempted in
            // mode 0 reset it without writing) would otherwise suppress the
            // .bin until the next unrelated icon change.
            g_diskCache.MarkDirty();
            float dpi = g_dpiScale;
            int zoom = GetCurrentIconZoom();
            uint64_t envHash = GetEnvironmentHash();
            std::vector<IconSnapshot> manifestCopy;
            {
                std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
                manifestCopy = g_iconSnapshots;
            }
            g_diskCache.QueueSave(dpi, zoom, envHash, std::move(manifestCopy));
        }
    } else if (shadowBakeChanged) {
        if (s.cacheMode == 1) {
            g_diskCache.ClearMemoryCache();
            g_diskCache.DeleteAllZoomCacheFiles(); // [STAGE A v2.4.2] all zoom files
        }
    }

    if (arrowSettingsChanged) {
        std::lock_guard<std::mutex> lock(g_overlayCacheMutex);
        g_cpuOverlaySilhouettes.clear();
    }

    if (shadowBakeChanged || arrowSettingsChanged) {
        {
            std::lock_guard<std::mutex> comboLock(g_comboShadowCacheMutex);
            g_comboShadowCache.clear();
        }
        if (g_overlayWnd) {
            g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
            PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
        }
    }

    g_underlayForceRender = true;
    {
        // Render thread owns/reads g_bakedSelectionGlows under g_renderMutex;
        // clearing it here without the lock raced the render loop.
        std::lock_guard<std::mutex> lock(g_renderMutex);
        g_bakedSelectionGlows.clear();
        g_bakedSelectionGlowUse.clear();
    }
    if (g_renderEvent) SetEvent(g_renderEvent);
    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);
}

// Define the Thread Agnostic flag just in case the compiler's older Windows SDK is missing it
#ifndef REG_NOTIFY_THREAD_AGNOSTIC
#define REG_NOTIFY_THREAD_AGNOSTIC 0x10000000
#endif

void RegistryWatcherThreadInner();
void RegistryWatcherThread() {
    try {
        RegistryWatcherThreadInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] RegistryWatcher exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] RegistryWatcher unknown exception");
    }
}

void RegistryWatcherThreadInner() {
    HKEY hKeyDesktop = nullptr;
    HKEY hKeyColors = nullptr;
    HKEY hKeyWallpapers = nullptr;
    HKEY hKeyDWM = nullptr;
    HKEY hKeyThemes = nullptr;
    HKEY hKeyClsid = nullptr;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_NOTIFY, &hKeyDesktop) != ERROR_SUCCESS) hKeyDesktop = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, KEY_NOTIFY, &hKeyColors) != ERROR_SUCCESS) hKeyColors = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", 0, KEY_NOTIFY, &hKeyWallpapers) != ERROR_SUCCESS) hKeyWallpapers = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_NOTIFY, &hKeyDWM) != ERROR_SUCCESS) hKeyDWM = nullptr;
    // Watch full Themes branch (catches theme swaps & Personalize)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes", 0, KEY_NOTIFY, &hKeyThemes) != ERROR_SUCCESS) hKeyThemes = nullptr;
    // Watch Desktop Special CLSIDs (catches Desktop Icon Settings changes)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID", 0, KEY_NOTIFY, &hKeyClsid) != ERROR_SUCCESS) hKeyClsid = nullptr;

    HANDLE hEvents[7];
    hEvents[0] = g_hRegStopEvent;
    hEvents[1] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hEvents[2] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hEvents[3] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hEvents[4] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hEvents[5] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hEvents[6] = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    bool eventsValid = true;
    for (int i = 1; i < 7; ++i) {
        if (!hEvents[i]) {
            eventsValid = false;
            break;
        }
    }
    if (!eventsValid) {
        for (int i = 1; i < 7; ++i) {
            if (hEvents[i]) CloseHandle(hEvents[i]);
        }
        // Keys were opened before event creation could fail; don't leak them.
        if (hKeyDesktop) RegCloseKey(hKeyDesktop);
        if (hKeyColors) RegCloseKey(hKeyColors);
        if (hKeyWallpapers) RegCloseKey(hKeyWallpapers);
        if (hKeyDWM) RegCloseKey(hKeyDWM);
        if (hKeyThemes) RegCloseKey(hKeyThemes);
        if (hKeyClsid) RegCloseKey(hKeyClsid);
        return;
    }

    DWORD notifyFlags = REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC;

    while (!g_unloading) {
        if (hKeyDesktop) RegNotifyChangeKeyValue(hKeyDesktop, FALSE, notifyFlags, hEvents[1], TRUE);
        if (hKeyColors) RegNotifyChangeKeyValue(hKeyColors, FALSE, notifyFlags, hEvents[2], TRUE);
        if (hKeyWallpapers) RegNotifyChangeKeyValue(hKeyWallpapers, FALSE, notifyFlags, hEvents[3], TRUE);
        if (hKeyDWM) RegNotifyChangeKeyValue(hKeyDWM, FALSE, notifyFlags, hEvents[4], TRUE);
        if (hKeyThemes) RegNotifyChangeKeyValue(hKeyThemes, TRUE, notifyFlags, hEvents[5], TRUE);
        if (hKeyClsid) RegNotifyChangeKeyValue(hKeyClsid, TRUE, notifyFlags, hEvents[6], TRUE);

        DWORD res = WaitForMultipleObjects(7, hEvents, FALSE, INFINITE);

        if (res == WAIT_OBJECT_0 || g_unloading) {
            break;
        }
     if (res >= WAIT_OBJECT_0 + 1 && res <= WAIT_OBJECT_0 + 6) {

         // Scope the refresh: Colors/Wallpapers only affect the background,
         // so don't pay a full icon-cache wipe + deep pass for them.
         WPARAM refreshTag = (res == WAIT_OBJECT_0 + 2 || res == WAIT_OBJECT_0 + 3)
                             ? REFRESH_TAG_WALLPAPER : REFRESH_TAG_FULL;

         // [VD-DIAG] name the registry key that fired
         DiagLog(L"[REGWATCH] key event %d -> tag %u",
                 (int)(res - WAIT_OBJECT_0), (unsigned)refreshTag);

         if (g_messageWnd) {
             PostMessageW(g_messageWnd, WM_APP_TRIGGER_REFRESH, refreshTag, 0);
         }
         // Coalesce registry notification bursts (theme changes fire several
         // keys at once); each burst otherwise costs a full cache rebuild.
         Sleep(250);
     }
    }

    if (hKeyDesktop) RegCloseKey(hKeyDesktop);
    if (hKeyColors) RegCloseKey(hKeyColors);
    if (hKeyWallpapers) RegCloseKey(hKeyWallpapers);
    if (hKeyDWM) RegCloseKey(hKeyDWM);
    if (hKeyThemes) RegCloseKey(hKeyThemes);
    if (hKeyClsid) RegCloseKey(hKeyClsid);

    for (int i=1; i<7; i++) CloseHandle(hEvents[i]);
}


void FolderWatcherThreadProcInner();
void FolderWatcherThreadProc() {
    try {
        FolderWatcherThreadProcInner();
    } catch (const std::exception& e) {
        Wh_Log(L"[CRASH-GUARD] FolderWatcher exception: %hs", e.what());
    } catch (...) {
        Wh_Log(L"[CRASH-GUARD] FolderWatcher unknown exception");
    }
}

void FolderWatcherThreadProcInner() {
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    PWSTR pUserDesktop = nullptr;
    PWSTR pPublicDesktop = nullptr;
    SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pUserDesktop);
    SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, NULL, &pPublicDesktop);

    HANDLE hUserChange = INVALID_HANDLE_VALUE;
    HANDLE hPublicChange = INVALID_HANDLE_VALUE;

    if (pUserDesktop) {
        hUserChange = FindFirstChangeNotificationW(
            pUserDesktop,
            FALSE, // Non-recursive on desktop root: only top-level changes trigger snapshots
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
        );
        CoTaskMemFree(pUserDesktop);
    }

    if (pPublicDesktop) {
        hPublicChange = FindFirstChangeNotificationW(
            pPublicDesktop,
            FALSE, // Non-recursive on desktop root
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
        );
        CoTaskMemFree(pPublicDesktop);
    }

    std::vector<HANDLE> hEvents;
    if (g_hUnloadEvent) hEvents.push_back(g_hUnloadEvent);
    if (hUserChange != INVALID_HANDLE_VALUE) hEvents.push_back(hUserChange);
    if (hPublicChange != INVALID_HANDLE_VALUE) hEvents.push_back(hPublicChange);

    while (!g_unloading && !hEvents.empty()) {
        DWORD res = WaitForMultipleObjects((DWORD)hEvents.size(), hEvents.data(), FALSE, 500);

        if (res == WAIT_OBJECT_0 || g_unloading) {
            break;
        }

        if (res > WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + hEvents.size()) {
            DWORD idx = res - WAIT_OBJECT_0;
            if (g_overlayWnd) {
                if (g_isLeftButtonDown.load(std::memory_order_relaxed)) {
                    DiagLog(L"[TRIG-FOLDER] desktop folder change (deferred, button down)");
                    g_snapshotUpdatePending.store(true, std::memory_order_relaxed);
                } else {
                    bool expectedSnap = false;
                    if (g_snapshotUpdatePending.compare_exchange_strong(expectedSnap, true)) {
                        DiagLog(L"[TRIG-FOLDER] desktop folder change -> full pass");
                        PostMessageW(g_overlayWnd, WM_APP_SNAPSHOT_UPDATE, 0, 0);
                    }
                }
            }
            FindNextChangeNotification(hEvents[idx]);
        }
    }

    if (hUserChange != INVALID_HANDLE_VALUE) FindCloseChangeNotification(hUserChange);
    if (hPublicChange != INVALID_HANDLE_VALUE) FindCloseChangeNotification(hPublicChange);

    if (SUCCEEDED(hrCom)) CoUninitialize();
}

SafeThread g_bootstrapThread;

static BOOL Engine_ModInit() {
    ULONGLONG bootT0 = GetTickCount64();
    BootLog(L"[BOOT] ===== new boot =====");
    BootLog(L"[BOOT] Wh_ModInit enter");

    // Log std::terminate with the active exception before the runtime aborts
    // (the libc++.whl 0x40000015 crash signature). Process-global: if another
    // mod ever installs its own handler, ours is replaced — acceptable.
    std::set_terminate([]() {
        BootLog(L"[TERMINATE] std::terminate called!");
        if (std::current_exception()) {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                BootLog(L"[TERMINATE] exception: %hs", e.what());
            } catch (...) {
                BootLog(L"[TERMINATE] non-standard exception");
            }
        } else {
            BootLog(L"[TERMINATE] no active exception (abort/assert path)");
        }
        std::abort();
    });
    // Reset all lifecycle and thread flags to allow clean on/off toggles
    g_unloading.store(false, std::memory_order_release);
    g_initialized_desktop.store(false, std::memory_order_release);
    g_initialized.store(false, std::memory_order_release);
    g_threadStop.store(false, std::memory_order_release);
    g_underlayThreadStop.store(false, std::memory_order_release);
    g_startupGracePeriod.store(true, std::memory_order_release);
    g_sharedMaskReady.store(false, std::memory_order_release);
    g_snapshotUpdatePending.store(false, std::memory_order_release);
    g_selectionUpdatePending.store(false, std::memory_order_release);
    g_overlayBypassed.store(false, std::memory_order_relaxed);
    g_forceRender.store(true, std::memory_order_relaxed);
    g_underlayForceRender.store(true, std::memory_order_relaxed);

    // Initialize the disk cache paths now that the Windhawk API is fully ready
    g_diskCache.Init();

    WM_APP_CLEANUP = RegisterWindowMessageW(L"TransparentDesktopSpotlight_Cleanup");
    WM_APP_DEVICE_LOST = RegisterWindowMessageW(L"TransparentDesktopSpotlight_DeviceLost");
    WM_APP_TRIGGER_REFRESH = RegisterWindowMessageW(L"TransparentDesktopSpotlight_Refresh");
    WM_APP_SELECTION_UPDATE = RegisterWindowMessageW(L"TransparentDesktopSpotlight_SelUpdate");
    WM_APP_SNAPSHOT_UPDATE = RegisterWindowMessageW(L"TransparentDesktopSpotlight_SnapUpdate");
    WM_APP_DEFERRED_INIT = RegisterWindowMessageW(L"TransparentDesktopSpotlight_DeferredInit");
    WM_APP_FOCUSSTORM = RegisterWindowMessageW(L"TransparentDesktopSpotlight_FocusStorm");

    g_hUnloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    LoadSettings();


    // Clean up orphaned temp cache files from previous crashes
    g_diskCache.CleanupOrphans();

    // Start the lightweight bootstrap thread
    g_bootstrapThread.reset(new std::thread(BootstrapThread));

    g_hRegStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hCfgStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hFolderWatchUpdateEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_snapshotWorkerEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // NOTE: the aux worker threads (registry watcher, folder watcher, snapshot
    // worker) are NOT started here — @include explorer.exe matches every
    // explorer.exe process, and only the desktop owner may run workers. They
    // are started from DeferredInit via StartAuxThreads().
    BootLog(L"[BOOT] Wh_ModInit exit (+%llu ms)", GetTickCount64() - bootT0);
    return TRUE;
}

static void StartAuxThreads() {
    if (!g_regThread) {
        g_regThread.reset(new std::thread(RegistryWatcherThread));
    }
    if (!g_folderWatcherThread) {
        g_folderWatcherThread.reset(new std::thread(FolderWatcherThreadProc));
    }
    if (!g_snapshotWorkerThread) {
        g_snapshotWorkerThread.reset(new std::thread(SnapshotWorkerThreadProc));
    }
    if (!g_cfgThread) {
        g_cfgThread.reset(new std::thread(ConfigWatcherThreadProc));
    }
}


static void Engine_ModUninit() {
    // 1. Raise the flag. All WndProcs and Loops will immediately drop incoming work.
    g_unloading = true;

    // 2. UNHOOK BOOTSTRAP (If it somehow didn't unhook itself)
    if (g_bootstrapHook) {
        UnhookWindowsHookEx(g_bootstrapHook);
        g_bootstrapHook = nullptr; // Fixed the "null ptr" typo from the file!
    }

    // 3. WAKE UP & STOP BACKGROUND HELPERS
    if (g_hUnloadEvent) SetEvent(g_hUnloadEvent);
    if (g_hRegStopEvent) SetEvent(g_hRegStopEvent);

    if (g_bootstrapThread) {
        if (g_bootstrapThread->joinable()) g_bootstrapThread->join();
        g_bootstrapThread.reset();
    }
    if (g_regThread) {
        if (g_regThread->joinable()) g_regThread->join();
        g_regThread.reset();
    }

    // Stop + join the config-file watcher (settings.cfg)
    if (g_hCfgStopEvent) SetEvent(g_hCfgStopEvent);
    g_cfgThread.reset();

    // Wake up the folder watcher so it breaks out of the wait loop
    if (g_hFolderWatchUpdateEvent) SetEvent(g_hFolderWatchUpdateEvent);


    if (g_snapshotWorkerEvent) SetEvent(g_snapshotWorkerEvent);
    if (g_snapshotWorkerThread) {
        if (g_snapshotWorkerThread->joinable()) g_snapshotWorkerThread->join();
        g_snapshotWorkerThread.reset();
    }

    // Worker is dead: nothing can hold the retired or current label fonts.
    // (s_prevCaptured is a function-local static in QueueFullSnapshot and is
    // intentionally left to process teardown — one HFONT at unload.)
    {
        std::lock_guard<std::mutex> lock(g_snapshotRequestMutex);
        for (HFONT oldFont : g_retiredLabelFonts) DeleteObject(oldFont);
        g_retiredLabelFonts.clear();
    }
    if (g_snapshotLabelHFont) {
        DeleteObject(g_snapshotLabelHFont);
        g_snapshotLabelHFont = nullptr;
    }
    // Release the DWrite factory explicitly: static destruction during
    // DLL_PROCESS_DETACH is the wrong place for COM object teardown.
    g_dwriteFactory.Reset();

    // Wait for any pending disk cache saves to finish before the DLL unmaps.
    // First flush anything still dirty so the last state is never lost.
    std::vector<IconSnapshot> manifestCopy;
    {
        std::lock_guard<std::mutex> snapLock(g_snapshotMutex);
        manifestCopy = g_iconSnapshots;
    }
    SaveIconShapes(manifestCopy);  // final shape persistence before unload
    if (g_diskCache.IsDirty()) {
        float dpi = g_dpiScale;
        int zoom = GetCurrentIconZoom();
        uint64_t envHash = GetEnvironmentHash();
        g_diskCache.QueueSave(dpi, zoom, envHash, std::move(manifestCopy));
    }
    g_diskCache.JoinSaveThread();

    // Safe to reset g_resourceDc now that the snapshot worker is dead

    g_resourceDc.Reset();

    if (g_folderWatcherThread) {
        if (g_folderWatcherThread->joinable()) g_folderWatcherThread->join();
        g_folderWatcherThread.reset();

    }

    // 4. STOP THE RENDER LOOPS
    // We do this BEFORE destroying windows so the UI thread isn't deadlocked by a render mutex.
    g_threadStop = true;
    g_underlayThreadStop = true;
    if (g_renderEvent) SetEvent(g_renderEvent);
    if (g_underlayRenderEvent) SetEvent(g_underlayRenderEvent);

    if (g_renderThread && g_renderThread->joinable()) {
        g_renderThread->join();
        g_renderThread.reset();
    }
    if (g_underlayRenderThread && g_underlayRenderThread->joinable()) {
        g_underlayRenderThread->join();
        g_underlayRenderThread.reset();
    }

    // 5. SAFELY DESTROY WINDOWS & REMOVE SUBCLASSES ON THE UI THREAD
    if (g_messageWnd && IsWindow(g_messageWnd)) {
        DWORD_PTR res = 0;
        SendMessageTimeoutW(g_messageWnd, WM_APP_CLEANUP, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, 2000, &res);
    }

    // Safety fallback: if UI thread was hung, ensure subclasses are stripped.
    // RemoveWindowSubclass MUST run on the window's owning thread, so never call
    // it directly from this thread — retry the marshaled cleanup instead.
    if (g_subclassedLV || g_subclassedSV) {
        DWORD_PTR res = 0;
        if (g_messageWnd && IsWindow(g_messageWnd)) {
            SendMessageTimeoutW(g_messageWnd, WM_APP_CLEANUP, 0, 0,
                                SMTO_NORMAL | SMTO_ABORTIFHUNG, 2000, &res);
        }
        // Last resort: only strip inline if this thread happens to own the windows
        // (otherwise leaving them is safer than racing the subclass chain).
        HWND hStill = g_subclassedLV ? g_subclassedLV : g_subclassedSV;
        if (hStill && IsWindow(hStill) &&
            GetWindowThreadProcessId(hStill, nullptr) == GetCurrentThreadId()) {
            RemoveDesktopSubclasses();
        }
    }

    // Unregister classes so Windows forgets our function pointers.
    // Only safe once every window of ours is actually destroyed — if any window
    // survived (hung UI thread), unregistering would fail silently and leave a
    // wndproc pointing into an unloading DLL.
    if (!IsWindow(g_overlayWnd) && !IsWindow(g_underlayWnd) && !IsWindow(g_messageWnd)) {
        UnregisterClass(OVERLAY_WINDOW_CLASS, GetCurrentModuleHandle());
        UnregisterClass(UNDERLAY_WINDOW_CLASS, GetCurrentModuleHandle());
        UnregisterClass(MESSAGE_WINDOW_CLASS, GetCurrentModuleHandle());
    }

    // 6. THE EXECUTIONER: Hierarchically release all GPU & COM resources
    ReleaseAllDirectXResources();

    // 7. CLEANUP EVENT HANDLES
    if (g_renderEvent) { CloseHandle(g_renderEvent); g_renderEvent = nullptr; }
    if (g_underlayRenderEvent) { CloseHandle(g_underlayRenderEvent); g_underlayRenderEvent = nullptr; }
    if (g_hUnloadEvent) { CloseHandle(g_hUnloadEvent); g_hUnloadEvent = nullptr; }
    if (g_hRegStopEvent) { CloseHandle(g_hRegStopEvent); g_hRegStopEvent = nullptr; }
    if (g_hCfgStopEvent) { CloseHandle(g_hCfgStopEvent); g_hCfgStopEvent = nullptr; }
    if (g_hFolderWatchUpdateEvent) { CloseHandle(g_hFolderWatchUpdateEvent); g_hFolderWatchUpdateEvent = nullptr; }
    if (g_snapshotWorkerEvent) { CloseHandle(g_snapshotWorkerEvent); g_snapshotWorkerEvent = nullptr; }
}

// Shared apply path for BOTH settings sources: Windhawk's settings-changed
// callback and the config-file watcher (the settings window's writes).
void ApplySettingsChanges() {
    LoadSettings();
    // [F1/P1.1] Bump the arrow generation ONLY when a baked-pixel input
    // actually changed (LoadSettings reports via g_bakedInputsChanged). The
    // old unconditional bump invalidated the composite mirror on every
    // settings load — including no-op reloads fired by mod installs — and
    // every zoom revisit leg paid a full combo rebuild for it.
    if (g_bakedInputsChanged.exchange(false, std::memory_order_relaxed)) {
        g_arrowGen.fetch_add(1, std::memory_order_relaxed);
    }
}

static void Engine_ModSettingsChanged() {
    ApplySettingsChanges();
}

////////////////////////////////////////////////////////////////////////////////
// ============ AB TENEBRIS CONTROL PANEL (merged from ab_tenebris_control.cpp v0.2.44) ============
// Three-way process branch (see merged Wh_ModInit at end of file):
//   explorer.exe            -> engine (Engine_ModInit above)
//   windhawk.exe            -> launcher: spawns + watches the tool host process
//   windhawk.exe -tool-mod  -> tool host: tray icon + settings window (below)
// Config contract unchanged: control panel writes %LOCALAPPDATA%\AbTenebris\settings.cfg,
// engine's ConfigWatcherThreadProc reloads it. Windhawk [Settings] block above stays
// bootstrap/fallback only (one-time import when the cfg file is missing).
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Constants

static const wchar_t kWindowClassName[] = L"AbTenebrisControl_SettingsWnd";
static const wchar_t kMessageClassName[] = L"AbTenebrisControl_MsgWnd";
static const wchar_t kWindowTitle[] = L"Ab Tenebris — Settings";
static const UINT WMAPP_TRAYICON = WM_APP + 1;
static HICON g_trayIcon = nullptr;  // loaded via LoadTrayIcon (system icon)
static const UINT WMAPP_SCREENSHOT = WM_APP + 11;  // lParam = 0; saves snap.png
static const UINT WMAPP_SWITCHSECTION = WM_APP + 12;  // lParam = section index

static const wchar_t kCfgDirName[] = L"AbTenebris";
static const wchar_t kCfgFileName[] = L"settings.cfg";
static const wchar_t kProfilesDirName[] = L"profiles";

// Layout (dialog units avoided; raw px, scaled by system DPI at create time)
static const int kSidebarW = 200;
static const int kBottomBarH = 48;

////////////////////////////////////////////////////////////////////////////////
// Tool-mod process plumbing (desktop-window-cards-v2 recipe)

static bool g_isToolModProcessLauncher;
static bool g_isCtrlToolProcess;
static HANDLE g_toolModProcessMutex;
// Set by Wh_ModUninit (launcher instance) so the long-lived watchdog thread
// exits before the mod DLL unmaps — the old 3-attempt watchdog self-expired
// after ~40s, which left the tray with no lifeguard for the rest of the day.
static HANDLE g_watchdogStop = nullptr;

static HWND g_settingsWnd = nullptr;
static HWND g_msgWnd = nullptr;
static NOTIFYICONDATAW g_nid = {};
static std::atomic<bool> g_trayAdded{false};
static std::atomic<bool> g_ctrlUnloading{false};
static DWORD g_uiThreadId = 0;

////////////////////////////////////////////////////////////////////////////////
// Colors / fonts

static const COLORREF kColBg = RGB(32, 32, 32);       // #202020 window base
static const COLORREF kColSidebar = RGB(32, 32, 32);  // unified: no two-tone slab
static const COLORREF kColInputBg = RGB(40, 40, 40);  // #282828 tertiary
static const COLORREF kColAccent = RGB(68, 138, 255); // the ONE accent
static const COLORREF kColText = RGB(255, 255, 255);  // primary
static const COLORREF kColTextSec = RGB(196, 196, 196);  // white @ 77%
static const COLORREF kColDim = RGB(94, 94, 94);      // white @ 37%

static HFONT g_font = nullptr;
static HFONT g_fontBold = nullptr;
static HFONT g_fontEmoji = nullptr;
static HBRUSH g_brushBg = nullptr;
static HBRUSH g_brushSidebar = nullptr;
static HBRUSH g_brushSidebarHover = nullptr;
static HBRUSH g_brushInput = nullptr;
static HBRUSH g_brushAccent = nullptr;
static HBRUSH g_brushStroke = nullptr;
static HBRUSH g_brushHover = nullptr;
static HBRUSH g_brushThumb = nullptr;

// Dark-mode theming: kills light scrollbar/combo/trackbar artifacts on the
// dark background (stable since Win10 1809).
static void Darkize(HWND h) {
    if (h) SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
}

static HFONT g_fontMdl2 = nullptr;  // Segoe MDL2 Assets glyphs (system font)

static HFONT MakePtFont(int pt, bool bold, const wchar_t* face = L"Segoe UI") {
    HDC hdc = GetDC(nullptr);
    int px = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

////////////////////////////////////////////////////////////////////////////////
// Config file (same format the engine reads: key=value, UTF-16 LE, BOM)

struct CtrlCfgKeyEntry {
    const wchar_t* key;
    bool isString;
};

// Mirrors the engine's kCtrlCfgKeys (order = file layout).
static const CtrlCfgKeyEntry kCtrlCfgKeys[] = {
    {L"Shadows.enableShadows", false},
    {L"Shadows.shadowLength", false},
    {L"Shadows.shadowPasses", false},
    {L"Shadows.shadowBaseAlpha", false},
    {L"Shadows.shadowBlur", false},
    {L"Shadows.shadowTailSoftness", false},
    {L"Shadows.shadowTailOpacity", false},
    {L"Shadows.shadowTailScale", false},
    {L"Shadows.shadowOffset", false},
    {L"Shadows.shadowGlobalTracking", false},
    {L"Shadows.shadowFadeInDuration", false},
    {L"Shadows.shadowFadeOutDuration", false},
    {L"Spotlight.enableSpotlight", false},
    {L"Spotlight.idleOpacity", false},
    {L"Spotlight.overlayClipToIcons", false},
    {L"Spotlight.spotlightRadius", false},
    {L"Spotlight.spotlightRoundness", false},
    {L"Spotlight.spotlightBlur", false},
    {L"Spotlight.idleDelay", false},
    {L"Spotlight.spotlightFadeInDuration", false},
    {L"Spotlight.spotlightFadeOutDuration", false},
    {L"Spotlight.mouseSmoothing", false},
    {L"Tint.tintEnabled", false},
    {L"Tint.tintStrength", false},
    {L"Tint.tintPreset", true},
    {L"Tint.tintColorHex", true},
    {L"Selection.enableSelectionGlow", false},
    {L"Selection.selectionOpacity", false},
    {L"Selection.selectionPadding", false},
    {L"Selection.selectionRoundness", false},
    {L"Selection.selectionBlur", false},
    {L"Selection.selectionTimeout", false},
    {L"Selection.selectionFadeInDuration", false},
    {L"Selection.selectionFadeOutDuration", false},
    {L"Toggle.overlayEnabled", false},
    {L"Toggle.toggleOnDoubleClick", false},
    {L"Toggle.toggleFadeInDuration", false},
    {L"Toggle.toggleFadeOutDuration", false},
    {L"General.targetFps", false},
    {L"General.cacheMode", false},
    {L"General.customArrowEnabled", false},
    {L"General.customArrowScale", false},
    {L"General.customArrowOffsetX", false},
    {L"General.customArrowOffsetY", false},
    {L"batSignalEnabled", false},
    {L"batSignalStyle", true},
    {L"batLogoBlur", true},
};

static std::wstring GetCfgDir() {
    std::wstring dir;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                       &local))) {
        dir = std::wstring(local) + L"\\" + kCfgDirName;
        CoTaskMemFree(local);
    }
    return dir;
}

static std::wstring GetCfgPath() {
    std::wstring dir = GetCfgDir();
    return dir.empty() ? dir : dir + L"\\" + kCfgFileName;
}

static void TrimW(std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) {
        s.clear();
        return;
    }
    size_t e = s.find_last_not_of(L" \t\r\n");
    s = s.substr(b, e - b + 1);
}

static bool ReadConfigFile(const std::wstring& path,
                           std::unordered_map<std::wstring, std::wstring>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::wstring content;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 &&
        size.QuadPart < 1024 * 1024) {
        std::vector<char> bytes((size_t)size.QuadPart);
        DWORD got = 0;
        if (ReadFile(h, bytes.data(), (DWORD)bytes.size(), &got, nullptr) &&
            got >= 2) {
            const unsigned char* p = (const unsigned char*)bytes.data();
            if (p[0] == 0xFF && p[1] == 0xFE) {
                content.assign((const wchar_t*)(p + 2), (got - 2) / 2);
            } else {
                int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)p, got,
                                               nullptr, 0);
                if (wlen > 0) {
                    std::wstring w(wlen, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, (const char*)p, got, &w[0],
                                        wlen);
                    content = std::move(w);
                }
            }
        }
    }
    CloseHandle(h);

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        size_t end = (eol == std::wstring::npos) ? content.size() : eol;
        std::wstring line = content.substr(pos, end - pos);
        pos = (eol == std::wstring::npos) ? content.size() : eol + 1;
        TrimW(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        TrimW(key);
        TrimW(val);
        if (!key.empty()) out[std::move(key)] = std::move(val);
    }
    return true;
}

static bool WriteConfigMap(
    const std::unordered_map<std::wstring, std::wstring>& kv) {
    std::wstring dir = GetCfgDir();
    if (dir.empty()) return false;
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring tmp = dir + L"\\settings.cfg.tmp";
    std::wstring finalPath = dir + L"\\" + kCfgFileName;

    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    WORD bom = 0xFEFF;
    DWORD wrote = 0;
    WriteFile(h, &bom, sizeof(bom), &wrote, nullptr);

    std::wstring body;
    for (const auto& e : kCtrlCfgKeys) {
        auto it = kv.find(e.key);
        if (it == kv.end()) continue;
        body += e.key;
        body += L'=';
        body += it->second;
        body += L"\r\n";
    }
    BOOL ok =
        WriteFile(h, body.data(), (DWORD)(body.size() * sizeof(wchar_t)),
                  &wrote, nullptr);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), finalPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
}

////////////////////////////////////////////////////////////////////////////////
// Control spec table (drives pane construction + read/write)

enum class Kind { Chk, Slider, Combo, Color };

struct CtlSpec {
    int section;
    Kind kind;
    const wchar_t* key;
    const wchar_t* label;
    int minV;
    int maxV;
    const wchar_t* const* options;       // for Combo (stored values)
    int optionCount;
    const wchar_t* const* displayNames;  // optional user-facing combo names
};

// Sections mirror the ENGINE MOD's own settings groups — no invented names.
// Sections: General first (Doc's order). The Toggle-transparency rows live
// inside General now. Labels carry NO section-name prefixes (the tab IS the
// context — "Shadow Length" inside a Shadows tab is redundant).
static const int SEC_GENERAL = 0;
static const int SEC_OVERLAY = 1;
static const int SEC_SPOTLIGHT = 2;
static const int SEC_SHADOWS = 3;
static const int SEC_SELECTION = 4;
static const int SEC_DIAGNOSTICS = 5;
static const int SEC_COUNT = 6;
// Pane slot only (not a sidebar section): the profiles UI lives at the
// bottom of the General tab.
static const int SEC_PROFILES = 6;

// Sidebar rows that carry a live enable-checkbox (click = toggle + write).
struct SidebarToggle {
    int section;
    const wchar_t* key;
};
static const SidebarToggle kSidebarToggles[] = {
    {SEC_SHADOWS, L"Shadows.enableShadows"},
    {SEC_SPOTLIGHT, L"Spotlight.enableSpotlight"},
    {SEC_OVERLAY, L"Tint.tintEnabled"},
    {SEC_SELECTION, L"Selection.enableSelectionGlow"},
};

// Mirror of the ENGINE's kCtrlTintPresets table (mod_014) - swatch preview
// only; the engine remains the single source of truth.
struct TintPresetColor {
    const wchar_t* name;
    COLORREF rgb;
};
static const TintPresetColor kTintPresetColors[] = {
    {L"Black", RGB(0x00, 0x00, 0x00)},
    {L"Slate", RGB(0x16, 0x19, 0x1E)},
    {L"Midnight", RGB(0x05, 0x15, 0x29)},
    {L"Wine", RGB(0x1A, 0x0F, 0x2B)},
    {L"Nightshade", RGB(0x22, 0x0C, 0x29)},
    {L"Deep_Red", RGB(0x29, 0x06, 0x06)},
    {L"Umber", RGB(0x26, 0x16, 0x0F)},
    {L"Sepia", RGB(0x2B, 0x20, 0x09)},
    {L"Forest", RGB(0x15, 0x20, 0x0C)},
    {L"Abyss", RGB(0x04, 0x20, 0x24)},
};

// The picked custom color (written to Tint.tintColorHex by the picker; no
// dedicated UI row anymore).
static std::wstring g_customHex;

static const wchar_t* const kCtrlTintPresets[] = {
    L"Black", L"Slate", L"Midnight", L"Wine", L"Nightshade",
    L"Deep_Red", L"Umber", L"Sepia", L"Forest", L"Abyss", L"Custom"};
static const wchar_t* const kBatStyles[] = {L"🦇", L"🦇🦇", L"🦇🦇🦇"};
static const wchar_t* const kBatBlurs[] = {L"auto", L"0", L"10", L"20", L"30",
                                           L"40", L"60"};
static const wchar_t* const kFps[] = {L"30", L"60", L"144"};
static const wchar_t* const kCacheModes[] = {L"0", L"1", L"2"};
static const wchar_t* const kCacheModeDisplay[] = {
    L"No Cache (RAM only)", L"Single Profile", L"Multi-Profile Hoarder"};

#define CHKS(s, k, lbl) {s, Kind::Chk, k, lbl, 0, 0, nullptr, 0}
#define SLD(s, k, lbl, mn, mx) {s, Kind::Slider, k, lbl, mn, mx, nullptr, 0}
#define CBOS(s, k, lbl, opts) {s, Kind::Combo, k, lbl, 0, 0, opts, ARRAYSIZE(opts), nullptr}
#define CBOSD(s, k, lbl, opts, disp) {s, Kind::Combo, k, lbl, 0, 0, opts, ARRAYSIZE(opts), disp}
#define COLR(s, k, lbl) {s, Kind::Color, k, lbl, 0, 0, nullptr, 0}

static const CtlSpec kSpecs[] = {
    // General (engine "General" + 🦇)
    CBOS(SEC_GENERAL, L"General.targetFps", L"Target Framerate", kFps),
    CBOSD(SEC_GENERAL, L"General.cacheMode", L"Disk Cache Mode", kCacheModes,
          kCacheModeDisplay),
    // Overlay (Doc 2026-11-08: dim veil + toggle/tint/arrow groups; the
    // sidebar checkbox toggles veil AND tint. Group titles are the enable
    // checkboxes and live in kGroups, not here.)
    SLD(SEC_OVERLAY, L"Spotlight.idleOpacity", L"Overlay Opacity", 0, 255),
    CHKS(SEC_OVERLAY, L"Spotlight.overlayClipToIcons",
         L"Clip Overlay to Icons"),
    SLD(SEC_OVERLAY, L"Toggle.toggleFadeInDuration",
        L"Fade In Duration (ms)", 0, 3000),
    SLD(SEC_OVERLAY, L"Toggle.toggleFadeOutDuration",
        L"Fade Out Duration (ms)", 0, 3000),
    SLD(SEC_OVERLAY, L"Tint.tintStrength", L"Strength (%)", 0, 100),
    // One Color row drives everything (Doc 2026-11-12): the preset combo
    // selects, the swatch previews the EFFECTIVE color, the picker button
    // writes Tint.tintColorHex and flips the combo to Custom (engine
    // contract: unknown preset name = use the hex key).
    CBOS(SEC_OVERLAY, L"Tint.tintPreset", L"Color", kCtrlTintPresets),
    SLD(SEC_OVERLAY, L"General.customArrowScale", L"Scale (%)", 10, 200),
    SLD(SEC_OVERLAY, L"General.customArrowOffsetX", L"Offset X (px)", -200,
        200),
    SLD(SEC_OVERLAY, L"General.customArrowOffsetY", L"Offset Y (px)", -200,
        200),
    // Shadows (labels de-prefixed: the tab is the context; Enable lives in
    // the sidebar)
    SLD(SEC_SHADOWS, L"Shadows.shadowLength", L"Length (px)", 0, 1000),
    SLD(SEC_SHADOWS, L"Shadows.shadowPasses", L"Quality", 1, 32),
    SLD(SEC_SHADOWS, L"Shadows.shadowBaseAlpha", L"Opacity", 0, 255),
    SLD(SEC_SHADOWS, L"Shadows.shadowBlur", L"Blur (px)", 0, 50),
    SLD(SEC_SHADOWS, L"Shadows.shadowTailSoftness", L"Tail Softness (%)", 0,
        100),
    SLD(SEC_SHADOWS, L"Shadows.shadowTailOpacity", L"Tail Opacity (%)", 0, 100),
    SLD(SEC_SHADOWS, L"Shadows.shadowTailScale", L"Tail Scale", -100, 100),
    SLD(SEC_SHADOWS, L"Shadows.shadowOffset", L"Offset (px)", 0, 500),
    SLD(SEC_SHADOWS, L"Shadows.shadowFadeInDuration",
        L"Fade In Duration (ms)", 0, 5000),
    SLD(SEC_SHADOWS, L"Shadows.shadowFadeOutDuration",
        L"Fade Out Duration (ms)", 0, 5000),
    // Spotlight (Enable lives in the sidebar; veil rows moved to Overlay)
    SLD(SEC_SPOTLIGHT, L"Spotlight.spotlightRadius", L"Radius (px)", 50, 1500),
    SLD(SEC_SPOTLIGHT, L"Spotlight.spotlightRoundness", L"Roundness (%)", 0,
        100),
    SLD(SEC_SPOTLIGHT, L"Spotlight.spotlightBlur", L"Edge Blur (px)", 0, 500),
    SLD(SEC_SPOTLIGHT, L"Spotlight.idleDelay", L"Idle Timeout (ms)", 0, 10000),
    SLD(SEC_SPOTLIGHT, L"Spotlight.spotlightFadeInDuration",
        L"Fade In Duration (ms)", 0, 3000),
    SLD(SEC_SPOTLIGHT, L"Spotlight.spotlightFadeOutDuration",
        L"Fade Out Duration (ms)", 0, 5000),
    SLD(SEC_SPOTLIGHT, L"Spotlight.mouseSmoothing", L"Mouse Smoothing", 0, 20),
    CBOS(SEC_SPOTLIGHT, L"batSignalStyle", L"🦇 style", kBatStyles),
    CBOS(SEC_SPOTLIGHT, L"batLogoBlur", L"🦇 blur", kBatBlurs),
    // Selection (Enable lives in the sidebar)
    SLD(SEC_SELECTION, L"Selection.selectionOpacity", L"Opacity", 0, 255),
    SLD(SEC_SELECTION, L"Selection.selectionPadding", L"Padding (px)", 0, 500),
    SLD(SEC_SELECTION, L"Selection.selectionRoundness", L"Roundness (%)", 0,
        100),
    SLD(SEC_SELECTION, L"Selection.selectionBlur", L"Edge Blur (%)", 0, 100),
    SLD(SEC_SELECTION, L"Selection.selectionTimeout",
        L"Timeout (ms)", 0, 10000),
    SLD(SEC_SELECTION, L"Selection.selectionFadeInDuration",
        L"Fade In Duration (ms)", 0, 3000),
    SLD(SEC_SELECTION, L"Selection.selectionFadeOutDuration",
        L"Fade Out Duration (ms)", 0, 5000),
};

static const wchar_t* kSectionNames[SEC_COUNT] = {
    L"General", L"Overlay", L"Spotlight", L"Shadows", L"Selection",
    L"Diagnostics"};

////////////////////////////////////////////////////////////////////////////////
// Window state

struct Row {
    const CtlSpec* spec;
    HWND label = nullptr;   // row label static
    HWND ctl = nullptr;     // checkbox / trackbar / combobox / edit
    HWND value = nullptr;   // value edit (sliders)
    HWND spin = nullptr;    // up-down spinner buddied to value (sliders)
    HWND swatch = nullptr;  // color swatch button (color rows)
    HWND pick = nullptr;    // "..." button (color rows)
    int lastY = 0;          // pane-relative y from the last layout pass
};

static std::vector<Row> g_rows;
static HWND g_panes[SEC_COUNT + 1] = {};  // +1: profiles pane slot
static HWND g_sidebar = nullptr;
// Sidebar enable-checkbox state for keys that have NO row anymore (Enable
// switches left the tabs; the sidebar checkbox is the only control).
static std::unordered_map<std::wstring, bool> g_toggleState;
// Forward decls (group layout + rowless-key plumbing run earlier in file).
static Row* FindRowByKey(const wchar_t* key);
static bool RowlessOn(const wchar_t* key);
static void ApplyGroupEnableStates();
static HWND g_btnApply = nullptr;
static HWND g_btnSave = nullptr;
static HWND g_btnCancel = nullptr;
static HWND g_chkPreview = nullptr;
static HWND g_profilesEmpty = nullptr;
static int g_curSection = 0;
static bool g_dirty = false;
static bool g_loading = false;
static bool g_resizing = false;  // inside a WM_ENTERSIZEMOVE drag loop
static bool g_syncing = false;  // suppress EN_CHANGE loops during programmatic
                                // slider<->edit sync
static UINT_PTR g_previewTimer = 0;
static std::unordered_map<std::wstring, std::wstring> g_initialSnapshot;

static const int ID_BTN_APPLY = 1001;
static const int ID_BTN_SAVE = 1002;
static const int ID_BTN_CANCEL = 1003;
static const int ID_CHK_PREVIEW = 1004;
static const int ID_COLOR_PICK = 1005;
static const int ID_COLOR_SWATCH = 1006;
static const int ID_TINT_PICK = 1007;
static const int ID_PROFILE_LIST = 1101;
static const int ID_PROFILE_NAME = 1102;
static const int ID_PROFILE_SAVE = 1103;
static const int ID_PROFILE_APPLY = 1104;
static const int ID_PROFILE_DELETE = 1105;
static const int ID_DIAG_OPENFOLDER = 1201;
static const int ID_SIDEBAR = 1300;
static const UINT WM_APP_RELAYOUT = WM_APP + 10;
static const int TIMER_PREVIEW = 1;

// Sidebar hover state (item index, -1 = none), fed by the sidebar subclass.
static int g_sidebarHover = -1;

// Visual grouping: 1px frame windows drawn BEHIND a row range, with an
// optional CHECKBOX title sitting on the top border (the Win32 answer to
// PySide6's QGroupBox — the native themed groupbox doesn't fit the dark
// square language). Unchecking the title disables the member rows.
struct GroupSpec {
    int section;
    const wchar_t* titleKey;    // enable key (row-less, lives in the map)
    const wchar_t* titleText;   // checkbox label
    const wchar_t* const* memberKeys;
    int memberCount;
};
static const wchar_t* const kFadeMembers[] = {
    L"Toggle.toggleFadeInDuration", L"Toggle.toggleFadeOutDuration"};
static const wchar_t* const kTintMembers[] = {
    L"Tint.tintStrength", L"Tint.tintPreset"};
static const wchar_t* const kBatMembers[] = {
    L"batSignalStyle", L"batLogoBlur"};
static const wchar_t* const kShadowFadeMembers[] = {
    L"Shadows.shadowFadeInDuration", L"Shadows.shadowFadeOutDuration"};
static const wchar_t* const kArrowMembers[] = {
    L"General.customArrowScale", L"General.customArrowOffsetX",
    L"General.customArrowOffsetY"};
static const GroupSpec kGroups[] = {
    {SEC_OVERLAY, L"Toggle.toggleOnDoubleClick", L"Toggle on Double-Click",
     kFadeMembers, 2},
    {SEC_OVERLAY, L"Tint.tintEnabled", L"Tint", kTintMembers, 2},
    {SEC_OVERLAY, L"General.customArrowEnabled", L"Custom Shortcut Arrow:",
     kArrowMembers, 3},
    {SEC_SPOTLIGHT, L"batSignalEnabled", L"🦇 Bat Signal",
     kBatMembers, 2},
    {SEC_SHADOWS, L"Shadows.shadowGlobalTracking", L"Global Tracking",
     kShadowFadeMembers, 2},
};
static HWND g_groupWnds[ARRAYSIZE(kGroups)] = {};
static HWND g_groupTitle[ARRAYSIZE(kGroups)] = {};

// Group frame window: paints pane bg + a 1px stroke frame. Created before
// the rows so it sits BEHIND them; positioned by LayoutRows to wrap the
// row range (it scrolls with the rows).
static LRESULT CALLBACK GroupProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            // Click-through: the frame must never eat clicks meant for the
            // title checkbox or the rows (it overlapped the checkbox's
            // lower half).
            return HTTRANSPARENT;
        case WM_ERASEBKGND: {
            // Opaque fill: without it, vacated pixels from other sections
            // ghost through the group interiors (field-proven). The title
            // checkbox is kept clean by geometry + forced repaint below.
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_brushBg);
            return 1;
        }
        case WM_PAINT: {
            // 1px frame; the top line leaves a GAP under the title checkbox
            // (GWLP_USERDATA = gap width; gap starts at 10px raw).
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int dpi = GetDpiForWindow(hwnd);
            int gapL = MulDiv(10, dpi, 96);
            int gapR = gapL + (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(58, 58, 58));
            HPEN old = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, gapR, 0, nullptr);
            LineTo(hdc, rc.right - 1, 0);
            LineTo(hdc, rc.right - 1, rc.bottom - 1);
            LineTo(hdc, 0, rc.bottom - 1);
            LineTo(hdc, 0, 0);
            LineTo(hdc, gapL, 0);
            SelectObject(hdc, old);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}



static void RegisterGroupClass() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = GroupProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"AbTenebrisControl_Group";
    RegisterClassW(&wc);
}

////////////////////////////////////////////////////////////////////////////////
// Wheel-reversal subclass for NATIVE scrollable controls (sidebar listbox,
// profiles list): Doc wants the reversed wheel direction on ALL scrollbars,
// and native controls' built-in mapping can't be set — so flip the delta
// sign and let the control do the rest. dwRefData=1 marks the SIDEBAR,
// which additionally tracks a hover item (subtle bg change on hover).
static LRESULT CALLBACK WheelReverseProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR,
                                         DWORD_PTR dwRefData) {
    if (msg == WM_MOUSEWHEEL) {
        short delta = (short)HIWORD(wParam);
        if (delta != SHRT_MIN) delta = (short)-delta;  // flip (guard SHRT_MIN)
        wParam = (wParam & 0xFFFF0000) | (WORD)delta;
    } else if (dwRefData == 1 && msg == WM_MOUSEMOVE) {
        int itemH = (int)SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0);
        if (itemH > 0) {
            int count = (int)SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
            int idx = (short)HIWORD(lParam) / itemH;  // GET_Y_LPARAM needs windowsx.h
            if (idx < 0 || idx >= count) idx = -1;
            if (idx != g_sidebarHover) {
                RECT rc;
                if (g_sidebarHover >= 0 &&
                    SendMessageW(hwnd, LB_GETITEMRECT, g_sidebarHover,
                                 (LPARAM)&rc))
                    InvalidateRect(hwnd, &rc, FALSE);
                g_sidebarHover = idx;
                if (idx >= 0 &&
                    SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&rc))
                    InvalidateRect(hwnd, &rc, FALSE);
            }
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
    } else if (dwRefData == 1 && msg == WM_MOUSELEAVE) {
        RECT rc;
        if (g_sidebarHover >= 0 &&
            SendMessageW(hwnd, LB_GETITEMRECT, g_sidebarHover, (LPARAM)&rc))
            InvalidateRect(hwnd, &rc, FALSE);
        g_sidebarHover = -1;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void ReverseWheel(HWND hwnd, bool sidebar = false) {
    SetWindowSubclass(hwnd, WheelReverseProc, 1, sidebar ? 1 : 0);
}

// Trackbars: WM_MOUSEWHEEL goes to the FOCUS window — after a drag the
// trackbar itself has focus, so the NATIVE control handled the wheel and
// it never reached PaneProc (this is why every pane-side "fix" so far
// changed nothing Doc could feel). Route the wheel to the parent pane and
// eat the native handling completely.
static LRESULT CALLBACK TrackbarWheelProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEWHEEL) {
        // Route via the TOP window: WM_MOUSEWHEEL goes to the focus
        // window, so a trackbar focused in pane A ate wheels meant for
        // pane B (field-proven). The root routes purely by cursor.
        SendMessageW(GetAncestor(hwnd, GA_ROOT), WM_MOUSEWHEEL, wParam,
                     lParam);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void RouteTrackbarWheel(HWND hwnd) {
    SetWindowSubclass(hwnd, TrackbarWheelProc, 2, 0);
}

////////////////////////////////////////////////////////////////////////////////
// Responsive layout: reposition every row's controls. All coordinates are
// PANE-RELATIVE (children live inside the pane window, which itself sits at
// content.left — never add content.left to child positions).
static void LayoutRows(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int dpi = GetDpiForWindow(hwnd);
    int barTop = rc.bottom - MulDiv(kBottomBarH, dpi, 96);
    int margin = MulDiv(12, dpi, 96);
    int labelW = MulDiv(210, dpi, 96);
    int valueW = MulDiv(64, dpi, 96);
    int rowH = MulDiv(36, dpi, 96);
    RECT content = {MulDiv(kSidebarW + 6, dpi, 96),
                    MulDiv(6, dpi, 96),
                    rc.right - MulDiv(12, dpi, 96), barTop};
    int paneW = content.right - content.left;
    int ctlW = paneW - 2 * margin - labelW - valueW - MulDiv(8, dpi, 96);
    int comboW = MulDiv(220, dpi, 96);
    int editW = MulDiv(110, dpi, 96);
    int pickW = MulDiv(70, dpi, 96);

    int idx[SEC_COUNT] = {};
    int extras[SEC_COUNT] = {};  // group spacing accumulated, per section
    bool afterGroup[SEC_COUNT] = {};
    // Group vertical rhythm (raw px): gap above a group's title, the title
    // checkbox band, gap between title and first member row, gap below a
    // group's frame before the next row.
    const int grpBefore = MulDiv(10, dpi, 96);
    const int grpTitle = MulDiv(20, dpi, 96);
    const int grpTitleGap = MulDiv(6, dpi, 96);
    const int grpAfter = MulDiv(8, dpi, 96);
    for (auto& row : g_rows) {
        int s = row.spec->section;
        int i = idx[s]++;
        HWND pane = g_panes[s];
        if (!pane) continue;
        int scroll = (int)GetWindowLongPtrW(pane, GWLP_USERDATA);
        if (afterGroup[s]) {
            extras[s] += grpAfter;
            afterGroup[s] = false;
        }
        for (const GroupSpec& g : kGroups) {
            if (g.section != s) continue;
            if (wcscmp(g.memberKeys[0], row.spec->key) == 0)
                extras[s] += grpBefore + grpTitle + grpTitleGap;
            if (wcscmp(g.memberKeys[g.memberCount - 1], row.spec->key) == 0)
                afterGroup[s] = true;
        }
        int y = margin + i * rowH + extras[s] - scroll;
        row.lastY = y;

        int ctlH = MulDiv(row.spec->kind == Kind::Slider ? 28 : 24, dpi, 96);
        int ctlY = y + (rowH - ctlH) / 2;
        if (row.label) {
            // Rows living inside a titled group get 12px of label padding
            // (Doc: the frame already indents them visually).
            bool inGroup = false;
            for (const GroupSpec& g : kGroups)
                for (int mk = 0; mk < g.memberCount && !inGroup; mk++)
                    if (wcscmp(g.memberKeys[mk], row.spec->key) == 0)
                        inGroup = true;
            MoveWindow(row.label, margin + (inGroup ? MulDiv(12, dpi, 96) : 0),
                       y, labelW, rowH, !g_resizing);
        }
        // Slider column caps at 560px — precision dies on an endless track.
        int sliderW = ctlW;
        if (sliderW > MulDiv(560, dpi, 96)) sliderW = MulDiv(560, dpi, 96);
        // One right-edge grid: value boxes, combos and hex fields all END on
        // the same line (the value column's right edge).
        int valueRight = margin + labelW + sliderW + MulDiv(8, dpi, 96) +
                         valueW;
        // Checkboxes span the whole row (own text); others sit in the ctl column.
        int ctlX = margin + (row.spec->kind == Kind::Chk ? 0 : labelW);
        int ctlWid =
            row.spec->kind == Kind::Slider
                ? sliderW
                : (row.spec->kind == Kind::Combo
                       ? comboW
                       : (row.spec->kind == Kind::Color
                              ? editW
                              : (row.spec->kind == Kind::Chk
                                     ? labelW + ctlW - MulDiv(20, dpi, 96)
                                     : ctlW)));
        if (row.spec->kind == Kind::Combo)
            ctlX = valueRight - ctlWid;
        else if (row.spec->kind == Kind::Color)
            ctlX = valueRight - pickW - MulDiv(8, dpi, 96) - editW;
        MoveWindow(row.ctl, ctlX, ctlY, ctlWid, ctlH, !g_resizing);
        if (row.value) {
            // Slider rows: the spinner occupies the editbox's last 16px —
            // the pair stays flush to the value column's right edge.
            // (UDS_ALIGNRIGHT auto-docking didn't track our MoveWindow
            // layout, so the spin is placed by hand.)
            int spinW = row.spin ? MulDiv(16, dpi, 96) : 0;
            MoveWindow(row.value, valueRight - valueW, ctlY,
                       valueW - spinW, MulDiv(24, dpi, 96), !g_resizing);
            if (row.spin)
                MoveWindow(row.spin, valueRight - spinW, ctlY, spinW,
                           MulDiv(24, dpi, 96), !g_resizing);
        }
        if (wcscmp(row.spec->key, L"Tint.tintPreset") == 0) {
            // The Color row: combo LEFT (slider column), then swatch (42),
            // then the picker-icon button.
            // +8raw = 10phys nudge (trackbar channel inset): Doc-px are
            // PHYSICAL at 125%, so MulDiv(x,dpi,96) renders x*1.25.
            int comboW2 = MulDiv(200, dpi, 96);  // 200raw = 250phys
            int cbx = margin + labelW + MulDiv(8, dpi, 96);
            MoveWindow(row.ctl, cbx, ctlY, comboW2, ctlH, !g_resizing);
            MoveWindow(row.swatch, cbx + comboW2 + MulDiv(8, dpi, 96), ctlY,
                       MulDiv(50, dpi, 96), ctlH, !g_resizing);
            MoveWindow(row.pick, cbx + comboW2 + MulDiv(8, dpi, 96) +
                                     MulDiv(50, dpi, 96) +
                                     MulDiv(6, dpi, 96),
                       ctlY, ctlH, ctlH, !g_resizing);
        } else {
            if (row.swatch)
                MoveWindow(row.swatch, valueRight - pickW - MulDiv(28, dpi, 96),
                           ctlY, MulDiv(24, dpi, 96), ctlH, !g_resizing);
            if (row.pick)
                MoveWindow(row.pick, valueRight - pickW, ctlY, pickW, ctlH,
                           !g_resizing);
        }
    }

    // Per-pane scrollbars: enable when the section's rows overflow the pane.
    for (int s = 0; s < SEC_COUNT; s++) {
        HWND pane = g_panes[s];
        if (!pane) continue;
        int n = 0;
        for (const Row& r : g_rows)
            if (r.spec->section == s) n++;
        RECT prc;
        GetClientRect(pane, &prc);
        int contentH = margin * 2 + n * rowH + extras[s];
        // The trailing bottom margin is padding, not content — it must never
        // count as scrollable range (used to grant 1-8px phantom wheel
        // scroll on sections whose rows all fit).
        int usableH = contentH - margin;
        SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMax = usableH;
        si.nPage = (UINT)(prc.bottom > 0 ? prc.bottom : 1);
        si.nPos = (int)GetWindowLongPtrW(pane, GWLP_USERDATA);
        if (usableH > prc.bottom) {
            SetScrollInfo(pane, SB_VERT, &si, !g_resizing);
            ShowScrollBar(pane, SB_VERT, !g_resizing);
        } else {
            SetWindowLongPtrW(pane, GWLP_USERDATA, 0);
            // Also WIPE the scroll range: ShowScrollBar(FALSE) alone leaves
            // stale si behind (measured: nMax 525 vs nPage 518 on hidden
            // panes), and any later si read then trusts a phantom range.
            SCROLLINFO wipe = {sizeof(wipe), SIF_RANGE | SIF_PAGE};
            wipe.nPage = (UINT)(prc.bottom > 0 ? prc.bottom : 1);
            SetScrollInfo(pane, SB_VERT, &wipe, FALSE);
            ShowScrollBar(pane, SB_VERT, FALSE);
        }
        // Erase stale pixels from every reposition — INCLUDING children.
        // WS_CLIPCHILDREN excludes child rects from the parent's erase, so
        // rows vacated by a scroll keep ghost pixels unless RDW_ALLCHILDREN
        // forces them in. During live resize we skip the sync repaint (it
        // made resizing feel like it was "running math"); the full repaint
        // happens once on WM_EXITSIZEMOVE.
        if (!g_resizing) {
            RedrawWindow(pane, nullptr, nullptr,
                         RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN |
                             RDW_UPDATENOW);
        }
        // Group frames wrap their member rows (pane-relative; they ride
        // the same scroll offset); the title checkbox sits ON the top
        // border and the frame leaves a gap beneath it.
        for (int gi = 0; gi < (int)ARRAYSIZE(kGroups); gi++) {
            if (kGroups[gi].section != s || !g_groupWnds[gi]) continue;
            Row* rFirst =
                FindRowByKey(kGroups[gi].memberKeys[0]);
            Row* rLast = FindRowByKey(
                kGroups[gi].memberKeys[kGroups[gi].memberCount - 1]);
            if (!rFirst || !rLast) continue;
            int pad = MulDiv(4, dpi, 96);
            int titleH = MulDiv(20, dpi, 96);
            // Top border runs through the middle of the title band, which
            // sits grpTitleGap above the first member row.
            int yTop = rFirst->lastY - MulDiv(6, dpi, 96) - titleH / 2;
            int yBot = rLast->lastY + rowH + pad;
            int fx = margin - MulDiv(8, dpi, 96);
            MoveWindow(g_groupWnds[gi], fx, yTop,
                       paneW - 2 * margin + MulDiv(16, dpi, 96), yBot - yTop,
                       TRUE);
            HDC tdc = GetDC(g_groupTitle[gi]);
            HFONT of = (HFONT)SelectObject(tdc, g_font);
            SIZE tsz = {};
            GetTextExtentPoint32W(tdc, kGroups[gi].titleText,
                                  (int)wcslen(kGroups[gi].titleText), &tsz);
            SelectObject(tdc, of);
            ReleaseDC(g_groupTitle[gi], tdc);
            int cbW = tsz.cx + MulDiv(30, dpi, 96);
            int cbH = titleH;
            MoveWindow(g_groupTitle[gi], fx + MulDiv(10, dpi, 96),
                       yTop - cbH / 2, cbW, cbH, TRUE);
            SetWindowLongPtrW(g_groupWnds[gi], GWLP_USERDATA,
                              tsz.cx + MulDiv(18, dpi, 96));
        }
    }
}

static std::wstring GetProfileDir() {
    std::wstring dir = GetCfgDir();
    return dir.empty() ? dir : dir + L"\\" + kProfilesDirName;
}

// Full window relayout: sidebar, bottom bar, panes, rows. Runs on WM_SIZE
// (outside a drag) and once on WM_EXITSIZEMOVE.
static void RelayoutAll(HWND hwnd) {
    int dpi = GetDpiForWindow(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int barTop = rc.bottom - MulDiv(kBottomBarH, dpi, 96);
    SetWindowPos(g_sidebar, nullptr, 0, 0, MulDiv(kSidebarW, dpi, 96), barTop,
                 SWP_NOZORDER | SWP_NOMOVE);
    // Bottom bar, right-aligned: [Preview] [Apply] [Save] [Close],
    // 12px between everything (Doc's layout).
    int gap = MulDiv(12, dpi, 96);
    int btnH = MulDiv(32, dpi, 96);
    int by = barTop + (MulDiv(kBottomBarH, dpi, 96) - btnH) / 2;
    int bx = rc.right - MulDiv(12, dpi, 96);
    HWND order[3] = {g_btnCancel, g_btnSave, g_btnApply};
    for (HWND btn : order) {
        RECT br;
        GetWindowRect(btn, &br);
        int w = br.right - br.left;
        bx -= w;
        SetWindowPos(btn, nullptr, bx, by, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        bx -= gap;
    }
    RECT pr;
    GetWindowRect(g_chkPreview, &pr);
    int pw = pr.right - pr.left;
    SetWindowPos(g_chkPreview, nullptr, bx - pw + gap,
                 by + (btnH - MulDiv(22, dpi, 96)) / 2, 0, 0,
                 SWP_NOZORDER | SWP_NOSIZE);
    RECT content = {MulDiv(kSidebarW + 6, dpi, 96),
                    MulDiv(6, dpi, 96),
                    rc.right - MulDiv(10, dpi, 96), barTop};
    for (int s = 0; s < SEC_COUNT; s++) {
        if (g_panes[s])
            MoveWindow(g_panes[s], content.left, content.top,
                       content.right - content.left,
                       content.bottom - content.top, TRUE);
    }
    LayoutRows(hwnd);
}

// File logger (Wh_Log from tool mods doesn't reach OutputDebugString; this
// gives the same diagnose-before-fix loop the engine mod has).
static void CtlLog(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list a;
    va_start(a, fmt);
    vswprintf(buf, 512, fmt, a);
    va_end(a);
    std::wstring dir = GetCfgDir();
    if (dir.empty()) return;
    CreateDirectoryW(dir.c_str(), nullptr);
    HANDLE h = CreateFileW((dir + L"\\control_log.txt").c_str(),
                           FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    wchar_t line[600];
    swprintf_s(line, L"%u %s\r\n", GetTickCount(), buf);
    DWORD wrote;
    WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &wrote, nullptr);
    CloseHandle(h);
}

////////////////////////////////////////////////////////////////////////////////
// Value <-> control plumbing

static std::wstring GetCtlValueText(const Row& row) {
    wchar_t buf[64] = {};
    switch (row.spec->kind) {
        case Kind::Chk:
            return SendMessageW(row.ctl, BM_GETCHECK, 0, 0) == BST_CHECKED
                       ? L"1"
                       : L"0";
        case Kind::Slider: {
            int v = (int)SendMessageW(row.ctl, TBM_GETPOS, 0, 0);
            swprintf_s(buf, L"%d", v);
            return buf;
        }
        case Kind::Combo: {
            int sel = (int)SendMessageW(row.ctl, CB_GETCURSEL, 0, 0);
            if (sel < 0) return L"";
            return row.spec->options[sel];
        }
        case Kind::Color: {
            wchar_t hex[32] = {};
            GetWindowTextW(row.ctl, hex, 32);
            return hex;
        }
    }
    return L"";
}

static void SetCtlFromValue(Row& row, const std::wstring& val) {
    switch (row.spec->kind) {
        case Kind::Chk:
            SendMessageW(row.ctl, BM_SETCHECK,
                         (val == L"1" || _wtoi(val.c_str()) != 0) ? BST_CHECKED
                                                                  : BST_UNCHECKED,
                         0);
            break;
        case Kind::Slider: {
            int v = _wtoi(val.c_str());
            if (v < row.spec->minV) v = row.spec->minV;
            if (v > row.spec->maxV) v = row.spec->maxV;
            CtlLog(L"[SET] %s v=%d editHwnd=0x%p", row.spec->key, v,
                   (void*)row.value);
            g_syncing = true;
            SendMessageW(row.ctl, TBM_SETPOS, TRUE, (LPARAM)v);
            if (row.value) {
                wchar_t buf[32];
                swprintf_s(buf, L"%d", v);
                BOOL ok = SetWindowTextW(row.value, buf);
                CtlLog(L"[SET] edit text '%s' ok=%d", buf, (int)ok);
            }
            g_syncing = false;
            break;
        }
        case Kind::Combo: {
            int sel = -1;
            for (int i = 0; i < row.spec->optionCount; i++) {
                if (val == row.spec->options[i]) {
                    sel = i;
                    break;
                }
            }
            if (sel < 0) sel = 0;
            SendMessageW(row.ctl, CB_SETCURSEL, sel, 0);
            break;
        }
        case Kind::Color:
            SetWindowTextW(row.ctl, val.c_str());
            break;
    }
}

static void CollectAllValues(std::unordered_map<std::wstring, std::wstring>& out,
                             const std::unordered_map<std::wstring, std::wstring>* fillMissing) {
    for (auto& row : g_rows) {
        out[row.spec->key] = GetCtlValueText(row);
    }
    // Row-less enable keys (sidebar toggles + group titles) come from the
    // state map.
    for (const SidebarToggle& t : kSidebarToggles) {
        if (out.find(t.key) == out.end())
            out[t.key] = RowlessOn(t.key) ? L"1" : L"0";
    }
    for (const GroupSpec& g : kGroups) {
        if (out.find(g.titleKey) == out.end())
            out[g.titleKey] = RowlessOn(g.titleKey) ? L"1" : L"0";
    }
    if (!g_customHex.empty())
        out[L"Tint.tintColorHex"] = g_customHex;  // picker's write path
    // Keys not shown in the UI survive from the given snapshot.
    if (fillMissing) {
        for (const auto& e : *fillMissing) {
            if (out.find(e.first) == out.end()) out[e.first] = e.second;
        }
    }
}

static void WriteConfigFromUI();  // defined below
static Row* FindRowByKey(const wchar_t* key) {
    for (auto& row : g_rows)
        if (wcscmp(row.spec->key, key) == 0) return &row;
    return nullptr;
}

// Row-less enable keys (sidebar toggles + group-title checkboxes) live in
// g_toggleState; one setter keeps every UI surface bound to the key in sync.
static bool RowlessOn(const wchar_t* key) {
    auto it = g_toggleState.find(key);
    return it != g_toggleState.end() && it->second;
}

static void ApplyGroupEnableStates() {
    for (int gi = 0; gi < (int)ARRAYSIZE(kGroups); gi++) {
        bool on = RowlessOn(kGroups[gi].titleKey);
        for (int m = 0; m < kGroups[gi].memberCount; m++) {
            Row* r = FindRowByKey(kGroups[gi].memberKeys[m]);
            if (!r) continue;
            EnableWindow(r->ctl, on);
            if (r->value) EnableWindow(r->value, on);
            if (r->spin) EnableWindow(r->spin, on);
            if (r->label) EnableWindow(r->label, on);
        }
    }
}

static void SetRowlessKey(const wchar_t* key, bool on) {
    g_toggleState[key] = on;
    for (int gi = 0; gi < (int)ARRAYSIZE(kGroups); gi++) {
        if (wcscmp(kGroups[gi].titleKey, key) == 0 && g_groupTitle[gi])
            SendMessageW(g_groupTitle[gi], BM_SETCHECK,
                         on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    ApplyGroupEnableStates();
    if (g_sidebar) InvalidateRect(g_sidebar, nullptr, TRUE);
}

// Sidebar checkbox toggle: flip one enable-key, write the file, refresh the
// row control + sidebar. Not routed through the dirty state — it IS a save.
// Keys with a row use the row control; row-less keys live in g_toggleState.
static void ToggleSidebarEnable(const wchar_t* key) {
    Row* row = FindRowByKey(key);
    if (row) {
        bool on = SendMessageW(row->ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SetCtlFromValue(*row, on ? L"0" : L"1");
    } else {
        SetRowlessKey(key, !RowlessOn(key));
    }
    WriteConfigFromUI();
    InvalidateRect(g_sidebar, nullptr, TRUE);
}

// Checked state of a sidebar toggle: row control if the key has one,
// g_toggleState otherwise.
static bool SidebarToggleOn(const SidebarToggle& t) {
    Row* row = FindRowByKey(t.key);
    if (row) return SendMessageW(row->ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
    auto it = g_toggleState.find(t.key);
    return it != g_toggleState.end() && it->second;
}

static void CtrlMarkDirty() {
    if (g_loading) return;
    if (!g_dirty) {
        g_dirty = true;
        Wh_Log(L"[CTRL] dirty -> enabling Apply/Save");
        EnableWindow(g_btnApply, TRUE);
        EnableWindow(g_btnSave, TRUE);
        SetWindowTextW(g_btnCancel, L"Cancel");
    }
}

static void WriteConfigFromUI() {
    std::unordered_map<std::wstring, std::wstring> kv;
    CollectAllValues(kv, &g_initialSnapshot);
    if (WriteConfigMap(kv)) {
        Wh_Log(L"[CTRL] config written (%zu keys)", kv.size());
    } else {
        Wh_Log(L"[CTRL] config WRITE FAILED");
    }
}

static void ApplyClearedDirty() {
    g_dirty = false;
    EnableWindow(g_btnApply, FALSE);
    EnableWindow(g_btnSave, FALSE);
    SetWindowTextW(g_btnCancel, L"Close");
}

////////////////////////////////////////////////////////////////////////////////
// Pane building

static void OnControlChanged();  // defined after the window state section

// Panes are real windows (not STATICs) whose job is forwarding child
// notifications (WM_COMMAND / WM_HSCROLL / ...) up to the settings window —
// notifications go to the immediate parent, and rows are pane children —
// plus vertical scrolling when a section's rows overflow the pane.
static LRESULT CALLBACK PaneProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
        case WM_HSCROLL:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_NOTIFY:
            return SendMessageW(GetParent(hwnd), msg, wParam, lParam);
        case WM_VSCROLL: {
            SCROLLINFO si = {sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int old = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: si.nPos -= 40; break;
                case SB_LINEDOWN: si.nPos += 40; break;
                case SB_PAGEUP: si.nPos -= (int)si.nPage; break;
                case SB_PAGEDOWN: si.nPos += (int)si.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
            }
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);  // read back the clamped pos
            if (si.nPos != old) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, si.nPos);
                SendMessageW(GetParent(hwnd), WM_APP_RELAYOUT, 0, 0);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            // Wheel over a value spinbox adjusts THAT value; otherwise the
            // pane scrolls (only if it actually overflows). Direction:
            // REVERSED per Doc — wheel down = content up.
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            // Geometry hit-test, NOT ChildWindowFromPoint: a group frame's
            // rect CONTAINS its member rows, so ChildWindowFromPoint could
            // hand back the frame and silently kill the wheel (field-proven:
            // only the ungrouped "Overlay Opacity" row ever matched).
            POINT pt;
            GetCursorPos(&pt);
            for (auto& row : g_rows) {
                if (!row.ctl || GetParent(row.ctl) != hwnd) continue;
                if (!IsWindowEnabled(row.ctl)) continue;
                bool hit = false;
                for (HWND t : {row.value, row.spin, row.ctl}) {
                    if (!t) continue;
                    RECT rc;
                    GetWindowRect(t, &rc);
                    if (PtInRect(&rc, pt)) {
                        hit = true;
                        break;
                    }
                }
                if (!hit) continue;
                if (row.spec->kind == Kind::Slider) {
                    int v = (int)SendMessageW(row.ctl, TBM_GETPOS, 0, 0);
                    // Doc's mapping: wheel DOWN = value DOWN. Step =
                    // ~1% of the slider's range (min 1).
                    int range = row.spec->maxV - row.spec->minV;
                    int step = range > 100 ? (range + 50) / 100 : 1;
                    v += (delta > 0 ? step : -step);
                    if (v < row.spec->minV) v = row.spec->minV;
                    if (v > row.spec->maxV) v = row.spec->maxV;
                    SetCtlFromValue(row, std::to_wstring(v));
                    OnControlChanged();
                    CtlLog(L"[WHEEL] slider '%s' delta=%d -> v=%d",
                           row.spec->key, delta, v);
                    return 0;
                }
                break;  // over a non-slider control: fall through to scroll
            }
            // No phantom scrolling: only act when the scrollbar genuinely
            // has a range (stale/off-by-rounding si used to let panes whose
            // content fits wheel-scroll a few pixels with no bar visible).
            SCROLLINFO wsi = {sizeof(wsi), SIF_ALL};
            if (GetScrollInfo(hwnd, SB_VERT, &wsi) &&
                wsi.nMax - wsi.nMin + 1 > (int)wsi.nPage) {
                // Doc's mapping: wheel DOWN = content moves UP.
                SendMessageW(hwnd, WM_VSCROLL,
                             delta > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
                CtlLog(L"[WHEEL] pane scroll delta=%d", delta);
            }
            return 0;
        }
        case WM_ERASEBKGND: {
            // Panes MUST erase: without this every scroll/relayout leaves
            // ghost rows painted over stale pixels.
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_brushBg);
            // Section frame (Doc: see where each tab zone starts/ends).
            // The profiles pane docks under General's rows, so it draws
            // only sides + bottom and continues General's frame.
            if (hwnd == g_panes[SEC_PROFILES]) {
                RECT l = {rc.left, rc.top, rc.left + 1, rc.bottom};
                RECT r = {rc.right - 1, rc.top, rc.right, rc.bottom};
                RECT b = {rc.left, rc.bottom - 1, rc.right, rc.bottom};
                FillRect(hdc, &l, g_brushStroke);
                FillRect(hdc, &r, g_brushStroke);
                FillRect(hdc, &b, g_brushStroke);
            } else {
                FrameRect(hdc, &rc, g_brushStroke);
            }
            return 1;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterPaneClass() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = PaneProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"AbTenebrisControl_Pane";
    RegisterClassW(&wc);
}

static void BuildSectionPanes(HWND parent, const RECT& content) {
    RegisterPaneClass();
    int dpi = GetDpiForWindow(parent);
    int rowH = MulDiv(36, dpi, 96);
    int labelW = MulDiv(210, dpi, 96);
    int margin = MulDiv(12, dpi, 96);
    HINSTANCE hInst = GetModuleHandle(nullptr);

    g_rows.clear();
    for (int s = 0; s < SEC_COUNT; s++) g_panes[s] = nullptr;

    for (const CtlSpec& spec : kSpecs) {
        if (!g_panes[spec.section]) {
            g_panes[spec.section] = CreateWindowExW(
                0, L"AbTenebrisControl_Pane", nullptr,
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                content.left, content.top, content.right - content.left,
                content.bottom - content.top, parent, nullptr, hInst, nullptr);
            // Group frames + their title checkboxes: created BEFORE the
            // rows so the frames sit behind them (checkboxes must be after
            // their frame, which they are — same loop).
            RegisterGroupClass();
            for (int gi = 0; gi < (int)ARRAYSIZE(kGroups); gi++) {
                if (kGroups[gi].section != spec.section) continue;
                g_groupWnds[gi] = CreateWindowExW(
                    0, L"AbTenebrisControl_Group", nullptr,
                    WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                    g_panes[spec.section], nullptr, hInst, nullptr);
                g_groupTitle[gi] = CreateWindowExW(
                    0, L"BUTTON", kGroups[gi].titleText,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0, 0, 0, 0, g_panes[spec.section],
                    (HMENU)(INT_PTR)(5000 + gi), hInst, nullptr);
                SendMessageW(g_groupTitle[gi], WM_SETFONT, (WPARAM)g_font,
                             TRUE);
                Darkize(g_groupTitle[gi]);
            }
        }
        HWND pane = g_panes[spec.section];

        Row row;
        row.spec = &spec;
        int rowIndex = 0;
        for (const Row& r : g_rows)
            if (r.spec->section == spec.section) rowIndex++;

        int y = margin + rowIndex * rowH;
        int x = margin;

        // Checkboxes carry their OWN text — no separate label STATIC. Only
        // non-checkbox rows get the label column. Labels span the full row
        // height (SS_CENTERIMAGE) so descenders never clip.
        bool isBat = wcsstr(spec.key, L"batSignal") != nullptr;
        HWND label = nullptr;
        if (spec.kind != Kind::Chk) {
            label = CreateWindowExW(
                0, L"STATIC", spec.label,
                WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, x, y, labelW, rowH,
                pane, nullptr, hInst, nullptr);
            SendMessageW(label, WM_SETFONT,
                         (WPARAM)(isBat ? g_fontEmoji : g_font), TRUE);
        }
        row.label = label;

        int cx = x + labelW;
        int ctlW = content.right - content.left - labelW -
                   MulDiv(64 + 8, dpi, 96) - 2 * margin;
        int ctlH = MulDiv(spec.kind == Kind::Slider ? 28 : 24, dpi, 96);
        DWORD style = WS_CHILD | WS_VISIBLE;
        HWND ctl = nullptr;
        switch (spec.kind) {
            case Kind::Chk:
                ctl = CreateWindowExW(
                    0, L"BUTTON", spec.label,
                    style | WS_TABSTOP | BS_AUTOCHECKBOX, x,
                    y + (rowH - ctlH) / 2, labelW + ctlW, ctlH, pane,
                    (HMENU)(INT_PTR)(2000 + (int)g_rows.size()), hInst, nullptr);
                break;
            case Kind::Slider:
                ctl = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                      style | WS_TABSTOP | TBS_HORZ |
                                          TBS_NOTICKS | TBS_FIXEDLENGTH |
                                          TBS_BOTH,
                                      cx, y, ctlW - MulDiv(70, dpi, 96), ctlH,
                                      pane,
                                      (HMENU)(INT_PTR)(2000 + (int)g_rows.size()),
                                      hInst, nullptr);
                SendMessageW(ctl, TBM_SETRANGE, TRUE,
                             MAKELPARAM(spec.minV, spec.maxV));
                RouteTrackbarWheel(ctl);
                break;
            case Kind::Combo:
                ctl = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                      style | WS_TABSTOP | CBS_DROPDOWNLIST,
                                      cx, y, MulDiv(220, dpi, 96),
                                      MulDiv(300, dpi, 96), pane,
                                      (HMENU)(INT_PTR)(2000 + (int)g_rows.size()),
                                      hInst, nullptr);
                for (int i = 0; i < spec.optionCount; i++)
                    SendMessageW(ctl, CB_ADDSTRING, 0,
                                 (LPARAM)(spec.displayNames
                                              ? spec.displayNames[i]
                                              : spec.options[i]));
                break;
            case Kind::Color:
                ctl = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                    style | WS_TABSTOP | ES_AUTOHSCROLL, cx, y,
                    MulDiv(110, dpi, 96), ctlH, pane,
                    (HMENU)(INT_PTR)(2000 + (int)g_rows.size()), hInst, nullptr);
                row.swatch = CreateWindowExW(
                    0, L"BUTTON", nullptr,
                    style | BS_OWNERDRAW, cx + MulDiv(116, dpi, 96),
                    y, MulDiv(24, dpi, 96), ctlH, pane,
                    (HMENU)(INT_PTR)ID_COLOR_SWATCH, hInst, nullptr);
                Darkize(row.swatch);
                row.pick = CreateWindowExW(
                    0, L"BUTTON", L"Pick…",
                    style | WS_TABSTOP | BS_OWNERDRAW, cx + MulDiv(146, dpi, 96),
                    y, MulDiv(70, dpi, 96), ctlH, pane,
                    (HMENU)(INT_PTR)ID_COLOR_PICK, hInst, nullptr);
                SendMessageW(row.pick, WM_SETFONT, (WPARAM)g_font, TRUE);
                Darkize(row.pick);
                break;
        }
        if (wcscmp(spec.key, L"Tint.tintPreset") == 0) {
            // The Color row: swatch preview + picker-icon button.
            row.swatch = CreateWindowExW(
                0, L"BUTTON", nullptr, style | BS_OWNERDRAW, cx, y,
                MulDiv(42, dpi, 96), ctlH, pane,
                (HMENU)(INT_PTR)ID_COLOR_SWATCH, hInst, nullptr);
            row.pick = CreateWindowExW(
                0, L"BUTTON", nullptr, style | WS_TABSTOP | BS_OWNERDRAW, cx,
                y, MulDiv(30, dpi, 96), ctlH, pane,
                (HMENU)(INT_PTR)ID_TINT_PICK, hInst, nullptr);
            Darkize(row.pick);
        }
        row.ctl = ctl;
        SendMessageW(ctl, WM_SETFONT, (WPARAM)g_font, TRUE);
        Darkize(ctl);
        if (isBat) SendMessageW(ctl, WM_SETFONT, (WPARAM)g_fontEmoji, TRUE);

        if (spec.kind == Kind::Slider) {
            // Numeric edit next to the slider, two-way synced (id 3000+idx).
            row.value = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
                                                  WS_TABSTOP | ES_NUMBER,
                cx + ctlW - MulDiv(70, dpi, 96), y + MulDiv(4, dpi, 96),
                MulDiv(66, dpi, 96), MulDiv(24, dpi, 96), pane,
                (HMENU)(INT_PTR)(3000 + (int)g_rows.size()), hInst, nullptr);
            SendMessageW(row.value, WM_SETFONT, (WPARAM)g_font, TRUE);
            Darkize(row.value);
            // Native up-down spinner — placed BY HAND in LayoutRows at the
            // value edit's right edge (UDS_ALIGNRIGHT auto-dock didn't track
            // our MoveWindow layout). NO UDS_SETBUDDYINT: it writes
            // thousands separators ("1,440") that would poison the
            // ES_NUMBER parse — UDN_DELTAPOS drives our setter instead.
            row.spin = CreateWindowExW(
                0, UPDOWN_CLASSW, nullptr,
                WS_CHILD | WS_VISIBLE | UDS_ARROWKEYS | UDS_HOTTRACK,
                0, 0, 0, 0, pane,
                (HMENU)(INT_PTR)(4000 + (int)g_rows.size()), hInst, nullptr);
            SendMessageW(row.spin, UDM_SETRANGE32, spec.minV, spec.maxV);
        }

        g_rows.push_back(row);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Profiles + diagnostics panes

// Profiles UI is a disabled PLACEHOLDER for now (Doc 2026-11-12: the
// infrastructure isn't there yet - list/empty-state/name edit removed,
// one editable combo reading "NOT AVAILABLE", buttons disabled). This
// keeps old call sites harmless; the real implementation comes later.
static void RefreshProfileList(HWND parent) {}

static void BuildProfilesPane(HWND parent, const RECT& content) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    // Docked INSIDE the General tab, below its rows (Doc: no Profiles
    // section; the widgets live under General's own rows).
    int dpi = GetDpiForWindow(parent);
    int m = MulDiv(12, dpi, 96);
    int genRows = 0;
    for (const CtlSpec& sp : kSpecs)
        if (sp.section == SEC_GENERAL) genRows++;
    int rowH36 = MulDiv(36, dpi, 96);
    int profTop = m + genRows * rowH36 + m;
    HWND pane = CreateWindowExW(0, L"AbTenebrisControl_Pane", nullptr,
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                content.left, content.top + profTop,
                                content.right - content.left,
                                content.bottom - content.top - profTop, parent,
                                nullptr, hInst, nullptr);
    g_panes[SEC_PROFILES] = pane;

    HWND hint = CreateWindowExW(
        0, L"STATIC",
        L"A profile is a named snapshot of all settings.\n"
        L"Loading one applies it live — the engine picks it up instantly.",
        WS_CHILD | WS_VISIBLE | WS_DISABLED, m, m,
        content.right - content.left - 2 * m,
        MulDiv(36, dpi, 96), pane, nullptr, hInst, nullptr);
    SendMessageW(hint, WM_SETFONT, (WPARAM)g_font, TRUE);
    // Placeholder banner (white — it's the active status; the hint above
    // is the grayed one).
    HWND avail = CreateWindowExW(
        0, L"STATIC", L"*** NOT AVAILABLE YET ***",
        WS_CHILD | WS_VISIBLE, m, m + MulDiv(40, dpi, 96),
        content.right - content.left - 2 * m, MulDiv(18, dpi, 96), pane,
        nullptr, hInst, nullptr);
    SendMessageW(avail, WM_SETFONT, (WPARAM)g_font, TRUE);

    // Profile selector: editable combo (CBS_DROPDOWN = built-in edit box,
    // no custom widget needed) - DISABLED placeholder until the profiles
    // infrastructure lands.
    int rowY = MulDiv(96, dpi, 96);  // well below hint + NOT-AVAILABLE banner
    HWND name = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"COMBOBOX", L"NOT AVAILABLE",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_DISABLED,
        m, rowY, MulDiv(220, dpi, 96), MulDiv(120, dpi, 96), pane,
        (HMENU)(INT_PTR)ID_PROFILE_NAME, hInst, nullptr);
    SendMessageW(name, WM_SETFONT, (WPARAM)g_font, TRUE);
    Darkize(name);
    EnableWindow(name, FALSE);

    struct Btn {
        int id;
        const wchar_t* text;
        int x;
        int w;
    };
    int bY = rowY;
    const Btn btns[] = {
        {ID_PROFILE_SAVE, L"Save Profile", m + MulDiv(232, dpi, 96),
         MulDiv(104, dpi, 96)},
        {ID_PROFILE_APPLY, L"Load", m + MulDiv(344, dpi, 96),
         MulDiv(80, dpi, 96)},
        {ID_PROFILE_DELETE, L"Delete", m + MulDiv(432, dpi, 96),
         MulDiv(80, dpi, 96)}};
    for (const Btn& b : btns) {
        HWND btn = CreateWindowExW(0, L"BUTTON", b.text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                       BS_OWNERDRAW,
                                   b.x, bY, b.w, MulDiv(26, dpi, 96), pane,
                                   (HMENU)(INT_PTR)b.id, hInst, nullptr);
        SendMessageW(btn, WM_SETFONT, (WPARAM)g_font, TRUE);
        Darkize(btn);
        // Placeholder era: profiles don't work yet, buttons are inert.
        EnableWindow(btn, FALSE);
    }
    RefreshProfileList(pane);
}

static void BuildDiagnosticsPane(HWND parent, const RECT& content) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    HWND pane = CreateWindowExW(0, L"AbTenebrisControl_Pane", nullptr,
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                content.left, content.top,
                                content.right - content.left,
                                content.bottom - content.top, parent, nullptr,
                                hInst, nullptr);
    g_panes[SEC_DIAGNOSTICS] = pane;
    int dpi = GetDpiForWindow(parent);
    int m = MulDiv(12, dpi, 96);

    HWND st = CreateWindowExW(
        0, L"STATIC",
        L"Config file (read live by the engine — select and copy):",
        WS_CHILD | WS_VISIBLE, m, m, content.right - content.left - 2 * m,
        MulDiv(20, dpi, 96), pane, nullptr, hInst, nullptr);
    SendMessageW(st, WM_SETFONT, (WPARAM)g_font, TRUE);

    // Readonly edit = selectable/copyable path field.
    std::wstring cfg = GetCfgPath();
    HWND path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", cfg.c_str(),
                                WS_CHILD | WS_VISIBLE | ES_READONLY |
                                    ES_AUTOHSCROLL,
                                m, m + MulDiv(28, dpi, 96),
                                content.right - content.left - 2 * m,
                                MulDiv(24, dpi, 96), pane, nullptr, hInst,
                                nullptr);
    SendMessageW(path, WM_SETFONT, (WPARAM)g_font, TRUE);
    Darkize(path);

    HWND info = CreateWindowExW(
        0, L"STATIC",
        L"Engine: transparent-desktop-icons-spotlight\n"
        L"Control panel: ab-tenebris-control\n"
        L"Verbose probe logging can be enabled on request when reporting a bug.",
        WS_CHILD | WS_VISIBLE, m, m + MulDiv(68, dpi, 96),
        content.right - content.left - 2 * m, MulDiv(72, dpi, 96), pane,
        nullptr, hInst, nullptr);
    SendMessageW(info, WM_SETFONT, (WPARAM)g_font, TRUE);

    HWND btn = CreateWindowExW(0, L"BUTTON", L"Open Config Folder",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                   BS_OWNERDRAW,
                               m, m + MulDiv(148, dpi, 96),
                               MulDiv(170, dpi, 96), MulDiv(28, dpi, 96), pane,
                               (HMENU)(INT_PTR)ID_DIAG_OPENFOLDER, hInst,
                               nullptr);
    SendMessageW(btn, WM_SETFONT, (WPARAM)g_font, TRUE);
    Darkize(btn);
}

////////////////////////////////////////////////////////////////////////////////
// Config write/read on window open/close

static void LoadValuesIntoControls() {
    g_loading = true;
    std::unordered_map<std::wstring, std::wstring> kv;
    std::wstring path = GetCfgPath();
    if (!ReadConfigFile(path, kv) || kv.empty()) {
        // Engine hasn't imported yet: fall back to defaults matching the
        // Windhawk block defaults (the engine will self-import on its side).
        for (const auto& e : kCtrlCfgKeys) kv[e.key] = e.isString ? L"" : L"0";
        kv[L"Toggle.overlayEnabled"] = L"1";      // engine default is TRUE
        kv[L"General.customArrowEnabled"] = L"1";  // engine default is TRUE
    }
    g_initialSnapshot = kv;
    for (auto& row : g_rows) {
        auto it = kv.find(row.spec->key);
        if (it != kv.end()) SetCtlFromValue(row, it->second);
    }
    for (const SidebarToggle& t : kSidebarToggles) {
        auto it = kv.find(t.key);
        g_toggleState[t.key] =
            (it != kv.end() ? _wtoi(it->second.c_str()) : 0) != 0;
    }
    for (const GroupSpec& g : kGroups) {
        auto it = kv.find(g.titleKey);
        g_toggleState[g.titleKey] =
            (it != kv.end() ? _wtoi(it->second.c_str()) : 0) != 0;
        SendMessageW(g_groupTitle[&g - kGroups], BM_SETCHECK,
                     g_toggleState[g.titleKey] ? BST_CHECKED : BST_UNCHECKED,
                     0);
    }
    {
        auto it = kv.find(L"Tint.tintColorHex");
        g_customHex = (it != kv.end()) ? it->second : L"";
    }
    ApplyGroupEnableStates();
    g_loading = false;
}

////////////////////////////////////////////////////////////////////////////////
// Settings window proc

static void SwitchSection(int section) {
    g_curSection = section;
    for (int s = 0; s < SEC_COUNT; s++) {
        if (g_panes[s]) ShowWindow(g_panes[s], s == section ? SW_SHOW : SW_HIDE);
    }
    // The profiles UI rides along with the General tab.
    if (g_panes[SEC_PROFILES])
        ShowWindow(g_panes[SEC_PROFILES],
                   section == SEC_GENERAL ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_sidebar, nullptr, TRUE);
}

static void OnControlChanged() {
    CtrlMarkDirty();
    if (SendMessageW(g_chkPreview, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        // Debounced live write: the engine's watcher applies it ~instantly.
        SetTimer(g_settingsWnd, TIMER_PREVIEW, 150, nullptr);
    }
}

static void ShowColorPicker(HWND hwnd, Row& row) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    // Start from the current hex if parseable.
    wchar_t hex[32] = {};
    GetWindowTextW(row.ctl, hex, 32);
    PCWSTR p = hex;
    while (*p == L' ' || *p == L'#') p++;
    if (wcslen(p) == 6) {
        wchar_t b[3] = {0, 0, 0};
        auto byteAt = [&](int i) {
            b[0] = p[i * 2];
            b[1] = p[i * 2 + 1];
            return (int)wcstoul(b, nullptr, 16);
        };
        cc.rgbResult = RGB(byteAt(0), byteAt(1), byteAt(2));
    }

    if (ChooseColorW(&cc)) {
        wchar_t out[16];
        swprintf_s(out, L"#%02X%02X%02X", GetRValue(cc.rgbResult),
                   GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
        SetWindowTextW(row.ctl, out);
        OnControlChanged();
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInst = GetModuleHandle(nullptr);
            int dpi = GetDpiForWindow(hwnd);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int barTop = rc.bottom - MulDiv(kBottomBarH, dpi, 96);

            // Sidebar (flat — no sunken edge)
            g_sidebar = CreateWindowExW(
                0, L"LISTBOX", nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                    LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
                0, MulDiv(8, dpi, 96), MulDiv(kSidebarW, dpi, 96),
                barTop - MulDiv(8, dpi, 96), hwnd,
                (HMENU)(INT_PTR)ID_SIDEBAR, hInst, nullptr);
            Darkize(g_sidebar);
            ReverseWheel(g_sidebar, /*sidebar=*/true);
            SendMessageW(g_sidebar, WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(g_sidebar, LB_SETITEMHEIGHT, 0,
                         MAKELPARAM(MulDiv(36, dpi, 96), 0));
            for (int s = 0; s < SEC_COUNT; s++)
                SendMessageW(g_sidebar, LB_ADDSTRING, 0,
                             (LPARAM)kSectionNames[s]);
            SendMessageW(g_sidebar, LB_SETCURSEL, SEC_GENERAL, 0);

            // Bottom bar
            HWND preview = CreateWindowExW(
                0, L"BUTTON", L"Preview (live)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,
                barTop + MulDiv(14, dpi, 96), MulDiv(140, dpi, 96),
                MulDiv(22, dpi, 96), hwnd, (HMENU)(INT_PTR)ID_CHK_PREVIEW,
                hInst, nullptr);
            g_chkPreview = preview;
            SendMessageW(preview, WM_SETFONT, (WPARAM)g_font, TRUE);
            Darkize(preview);

            struct Btn {
                int id;
                const wchar_t* text;
                BOOL enabled;
            };
            const Btn btns[] = {
                {ID_BTN_APPLY, L"Apply", FALSE},
                {ID_BTN_SAVE, L"Save", FALSE},
                {ID_BTN_CANCEL, L"Close", TRUE},
            };
            int bx = rc.right - MulDiv(12, dpi, 96);
            for (int i = 3; i-- > 0;) {
                int w = MulDiv(92, dpi, 96);
                bx -= w;
                if (i < 2) bx -= MulDiv(8, dpi, 96);  // gap between buttons
                HWND btn = CreateWindowExW(
                    0, L"BUTTON", btns[i].text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, bx,
                    barTop + MulDiv(10, dpi, 96), w, MulDiv(28, dpi, 96), hwnd,
                    (HMENU)(INT_PTR)btns[i].id, hInst, nullptr);
                SendMessageW(btn, WM_SETFONT, (WPARAM)g_font, TRUE);
                Darkize(btn);
                EnableWindow(btn, btns[i].enabled);
                if (btns[i].id == ID_BTN_APPLY) g_btnApply = btn;
                if (btns[i].id == ID_BTN_SAVE) g_btnSave = btn;
                if (btns[i].id == ID_BTN_CANCEL) g_btnCancel = btn;
            }

            // Content panes
            RECT content = {MulDiv(kSidebarW + 6, dpi, 96),
                    MulDiv(6, dpi, 96),
                            rc.right - MulDiv(10, dpi, 96), barTop};
            BuildSectionPanes(hwnd, content);
            BuildProfilesPane(hwnd, content);
            BuildDiagnosticsPane(hwnd, content);
            LoadValuesIntoControls();
            SwitchSection(SEC_GENERAL);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            // Route by CURSOR, not focus: WM_MOUSEWHEEL is delivered to the
            // focus window (MSDN), so with no pane child focused this top
            // window got it and the pane never saw it. Forward to whichever
            // section pane is under the cursor; PaneProc always eats it, so
            // no bounce-back loop.
            POINT pt;
            GetCursorPos(&pt);
            for (int s = 0; s < SEC_COUNT; s++) {
                HWND pane = g_panes[s];
                if (!pane || !IsWindowVisible(pane)) continue;
                RECT prc;
                GetWindowRect(pane, &prc);
                if (PtInRect(&prc, pt)) {
                    SendMessageW(pane, WM_MOUSEWHEEL, wParam, lParam);
                    return 0;
                }
            }
            return 0;  // cursor not over any pane: eat (no bounce loop)
        }
        case WM_ENTERSIZEMOVE:
            // Freeze the whole window during the drag: zero repaint work per
            // frame (no layout math, no child repaints — nothing to lag).
            g_resizing = true;
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            return 0;
        case WM_EXITSIZEMOVE: {
            // Drag done: thaw, relayout once, paint once.
            g_resizing = false;
            SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
            RelayoutAll(hwnd);
            RedrawWindow(hwnd, nullptr, nullptr,
                         RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN |
                             RDW_UPDATENOW);
            return 0;
        }
        case WM_SIZE: {
            if (g_resizing) return 0;  // frozen; handled on WM_EXITSIZEMOVE
            RelayoutAll(hwnd);
            return 0;
        }
        case WM_NOTIFY: {
            // Spinner arrows on slider value edits (UDN_DELTAPOS): step the
            // REAL value (trackbar is the source of truth), reuse the same
            // setter as the wheel — dirty-state + preview stay consistent.
            if (((LPNMHDR)lParam)->code == UDN_DELTAPOS) {
                int idx = (int)((LPNMHDR)lParam)->idFrom - 4000;
                if (idx >= 0 && idx < (int)g_rows.size() &&
                    g_rows[idx].spec->kind == Kind::Slider) {
                    Row& row = g_rows[idx];
                    LPNMUPDOWN ud = (LPNMUPDOWN)lParam;
                    int v = (int)SendMessageW(row.ctl, TBM_GETPOS, 0, 0);
                    v += ud->iDelta;  // up arrow = value up (standard)
                    if (v < row.spec->minV) v = row.spec->minV;
                    if (v > row.spec->maxV) v = row.spec->maxV;
                    SetCtlFromValue(row, std::to_wstring(v));
                    OnControlChanged();
                }
                return 0;
            }
            // Trackbar custom draw: visible channel + clean square thumb
            // (the native look is a hairline track with an alien thumb).
            LPNMCUSTOMDRAW nmc = (LPNMCUSTOMDRAW)lParam;
            if (nmc->hdr.code == NM_CUSTOMDRAW &&
                nmc->hdr.idFrom >= 2000 &&
                nmc->hdr.idFrom < 2000 + (int)g_rows.size() &&
                nmc->hdr.hwndFrom && g_rows[nmc->hdr.idFrom - 2000].spec
                                         ->kind == Kind::Slider) {
                if (nmc->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (nmc->dwDrawStage == CDDS_ITEMPREPAINT) {
                    switch (nmc->dwItemSpec) {
                        case TBCD_CHANNEL: {
                            // The channel is a thin strip; if the theme hands
                            // us a full-control rect, drawing it would paint
                            // row-height banding across the pane.
                            int h = nmc->rc.bottom - nmc->rc.top;
                            if (h > MulDiv(10, GetDpiForWindow(hwnd), 96))
                                return CDRF_DODEFAULT;
                            FillRect(nmc->hdc, &nmc->rc, g_brushStroke);
                            return CDRF_SKIPDEFAULT;
                        }
                        case TBCD_THUMB: {
                            // Center the thumb vertically in the channel rect.
                            RECT rc = nmc->rc;
                            FillRect(nmc->hdc, &rc, g_brushThumb);
                            FrameRect(nmc->hdc, &rc, g_brushStroke);
                            return CDRF_SKIPDEFAULT;
                        }
                    }
                }
                return CDRF_DODEFAULT;
            }
            break;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
            if (di->CtlType == ODT_BUTTON && di->CtlID == ID_COLOR_SWATCH) {
                // Swatch = the EFFECTIVE tint color: preset table color, or
                // the picked hex when the combo is on Custom.
                for (const Row& r : g_rows) {
                    if (r.swatch != di->hwndItem) continue;
                    wchar_t sel[32] = {};
                    GetWindowTextW(r.ctl, sel, 32);  // combo text = display
                    COLORREF col = RGB(40, 40, 40);
                    bool found = false;
                    for (const TintPresetColor& p : kTintPresetColors) {
                        if (wcscmp(sel, p.name) == 0) {
                            col = p.rgb;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        PCWSTR q = g_customHex.c_str();
                        while (*q == L' ' || *q == L'#') q++;
                        if (wcslen(q) == 6) {
                            wchar_t b[3] = {0, 0, 0};
                            auto byteAt = [&](int i) {
                                b[0] = q[i * 2];
                                b[1] = q[i * 2 + 1];
                                return (int)wcstoul(b, nullptr, 16);
                            };
                            col = RGB(byteAt(0), byteAt(1), byteAt(2));
                        }
                    }
                    HBRUSH fill = CreateSolidBrush(col);
                    FillRect(di->hDC, &di->rcItem, fill);
                    DeleteObject(fill);
                    FrameRect(di->hDC, &di->rcItem, g_brushStroke);
                    return TRUE;
                }
                return TRUE;
            }
            if (di->CtlType == ODT_BUTTON && di->CtlID == ID_TINT_PICK) {
                // Color-picker icon: Segoe MDL2 Assets "Color" glyph E790
                // (system font - nothing shipped with the mod).
                HFONT old = (HFONT)SelectObject(di->hDC, g_fontMdl2);
                SetBkMode(di->hDC, TRANSPARENT);
                SetTextColor(di->hDC, RGB(220, 220, 220));
                DrawTextW(di->hDC, L"", 1, &di->rcItem,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(di->hDC, old);
                FrameRect(di->hDC, &di->rcItem, g_brushStroke);
                return TRUE;
            }
            if (di->CtlType == ODT_BUTTON) {
                // One button language everywhere: #282828 fill, 1px stroke,
                // white text / white@37% when disabled. Square corners.
                HDC hdc = di->hDC;
                RECT rcB = di->rcItem;
                bool disabled = (di->itemState & ODS_DISABLED) != 0;
                bool pressed = (di->itemState & ODS_SELECTED) != 0;
                FillRect(hdc, &rcB, pressed ? g_brushHover : g_brushInput);
                FrameRect(hdc, &rcB, g_brushStroke);
                wchar_t btext[64] = {};
                GetWindowTextW(di->hwndItem, btext, 64);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, disabled ? RGB(150, 150, 150) : kColText);
                HFONT oldF = (HFONT)SelectObject(hdc, g_font);
                DrawTextW(hdc, btext, -1, &rcB,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldF);
                if (di->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rcB);
                return TRUE;
            }
            if (di->CtlType != ODT_LISTBOX || di->hwndItem != g_sidebar) {
                break;
            }
            FillRect(di->hDC, &di->rcItem, g_brushSidebar);
            bool selected = (di->itemState & ODS_SELECTED) != 0;
            // Subtle hover: one step lighter than the sidebar slab; the
            // selected row keeps its accent fill.
            if (!selected && di->itemID == (UINT)g_sidebarHover)
                FillRect(di->hDC, &di->rcItem, g_brushSidebarHover);
            RECT full = di->rcItem;
            int dpi2 = GetDpiForWindow(hwnd);
            if (selected) {
                // Square accent fill, inset 4px — NEVER rounded.
                RECT pill = {full.left + MulDiv(4, dpi2, 96),
                             full.top + MulDiv(4, dpi2, 96),
                             full.right - MulDiv(4, dpi2, 96),
                             full.bottom - MulDiv(4, dpi2, 96)};
                FillRect(di->hDC, &pill, g_brushAccent);
            }
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, selected ? kColText : kColTextSec);
            wchar_t text[64];
            SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)text);
            RECT tr = full;
            tr.left += MulDiv(16, dpi2, 96);
            HFONT old = (HFONT)SelectObject(di->hDC, g_font);
            DrawTextW(di->hDC, text, -1, &tr,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            SelectObject(di->hDC, old);
            // Live enable-checkbox on the right for toggleable sections.
            // (No focus rectangle — Doc's law: no dotted borders.)
            for (const SidebarToggle& t : kSidebarToggles) {
                if (t.section == (int)di->itemID) {
                    RECT box = {full.right - MulDiv(30, dpi2, 96),
                                full.top + (full.bottom - full.top -
                                            MulDiv(16, dpi2, 96)) / 2,
                                full.right - MulDiv(14, dpi2, 96),
                                0};
                    box.bottom = box.top + MulDiv(16, dpi2, 96);
                    HBRUSH boxFill = CreateSolidBrush(RGB(24, 24, 24));
                    FillRect(di->hDC, &box, boxFill);
                    FrameRect(di->hDC, &box, g_brushStroke);
                    DeleteObject(boxFill);
                    if (SidebarToggleOn(t)) {
                        // Check mark: two accent lines.
                        HPEN pen = CreatePen(PS_SOLID, MulDiv(2, dpi2, 96),
                                             RGB(120, 220, 120));
                        HPEN oldPen = (HPEN)SelectObject(di->hDC, pen);
                        int cx1 = box.left + MulDiv(3, dpi2, 96);
                        int cy = box.top + MulDiv(8, dpi2, 96);
                        int cx2 = box.left + MulDiv(6, dpi2, 96);
                        int cy2 = box.top + MulDiv(12, dpi2, 96);
                        int cx3 = box.right - MulDiv(3, dpi2, 96);
                        int cy3 = box.top + MulDiv(4, dpi2, 96);
                        MoveToEx(di->hDC, cx1, cy, nullptr);
                        LineTo(di->hDC, cx2, cy2);
                        LineTo(di->hDC, cx3, cy3);
                        SelectObject(di->hDC, oldPen);
                        DeleteObject(pen);
                    }
                    break;
                }
            }
            return TRUE;
        }
        case WM_CTLCOLORBTN: {
            // Checkboxes paint their themed (lighter) background across their
            // full control width — which IS the whole row. Return the window
            // brush so no banding strips appear behind checkbox rows.
            return (LRESULT)g_brushBg;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND ctl = (HWND)lParam;
            if (ctl == g_sidebar) {
                SetBkColor(hdc, kColSidebar);
                SetTextColor(hdc, kColText);
                return (LRESULT)g_brushSidebar;
            }
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kColText);
            SetBkColor(hdc, kColBg);
            return (LRESULT)g_brushBg;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kColText);
            // The sidebar listbox owns its whole column: paint it with the
            // window bg, or its lighter control fill shows as a two-tone slab
            // below the nav items. Real input lists keep the input fill.
            if ((HWND)lParam == g_sidebar) {
                SetBkColor(hdc, kColBg);
                return (LRESULT)g_brushBg;
            }
            SetBkColor(hdc, kColInputBg);
            return (LRESULT)g_brushInput;
        }
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_brushBg);
            return 1;
        }
        case WM_APP_RELAYOUT:
            LayoutRows(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            // Resize floor: below this the rows would collide.
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            int dpi = GetDpiForWindow(hwnd);
            mmi->ptMinTrackSize.x = 800;
            mmi->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_HSCROLL: {
            // Trackbar movement: sync the value edit + mark dirty.
            if (!g_syncing) {
                for (auto& row : g_rows) {
                    if (row.spec->kind == Kind::Slider &&
                        row.ctl == (HWND)lParam) {
                        int v = (int)SendMessageW(row.ctl, TBM_GETPOS, 0, 0);
                        wchar_t buf[32];
                        swprintf_s(buf, L"%d", v);
                        g_syncing = true;
                        SetWindowTextW(row.value, buf);
                        g_syncing = false;
                        break;
                    }
                }
                OnControlChanged();
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            CtlLog(L"[CMD] id=%d code=%d", id, code);
            // Group-title checkboxes (5000+groupIndex): flip the row-less
            // enable key; members enable/disable in SetRowlessKey.
            if (id >= 5000 &&
                id < 5000 + (int)ARRAYSIZE(kGroups) &&
                code == BN_CLICKED) {
                const GroupSpec& grp = kGroups[id - 5000];
                SetRowlessKey(grp.titleKey, !RowlessOn(grp.titleKey));
                OnControlChanged();
                return 0;
            }
            if (id == ID_SIDEBAR && code == LBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_sidebar, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    // Click on the item's checkbox zone = toggle the enable
                    // key (and stay on the section); click on text = navigate.
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(g_sidebar, &pt);
                    RECT item = {};
                    SendMessageW(g_sidebar, LB_GETITEMRECT, sel, (LPARAM)&item);
                    bool hitBox = pt.x >= item.right - MulDiv(32, GetDpiForWindow(hwnd), 96);
                    for (const SidebarToggle& t : kSidebarToggles) {
                        if (t.section == sel && hitBox) {
                            ToggleSidebarEnable(t.key);
                            return 0;
                        }
                    }
                    SwitchSection(sel);
                }
                return 0;
            }
            // Slider value-edit typed a number (id 3000+rowIndex).
            if (id >= 3000 && id < 3000 + (int)g_rows.size() &&
                code == EN_CHANGE && !g_syncing && !g_loading) {
                CtlLog(L"[ENCHANGE] id=%d", id);
                Row& row = g_rows[id - 3000];
                if (row.spec->kind == Kind::Slider && row.value) {
                    wchar_t buf[16] = {};
                    GetWindowTextW(row.value, buf, 16);
                    if (buf[0]) {
                        int v = _wtoi(buf);
                        if (v < row.spec->minV) v = row.spec->minV;
                        if (v > row.spec->maxV) v = row.spec->maxV;
                        g_syncing = true;
                        SendMessageW(row.ctl, TBM_SETPOS, TRUE, (LPARAM)v);
                    }
                }
                return 0;
            }
            if (id == ID_BTN_APPLY) {
                WriteConfigFromUI();
                ApplyClearedDirty();
                return 0;
            }
            if (id == ID_BTN_SAVE) {
                WriteConfigFromUI();
                ApplyClearedDirty();
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id == ID_BTN_CANCEL) {
                if (g_dirty) {
                    // Revert: restore the file the window opened with; the
                    // engine's watcher un-applies the preview writes.
                    WriteConfigMap(g_initialSnapshot);
                }
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (id == ID_CHK_PREVIEW && code == BN_CLICKED) {
                if (SendMessageW(g_chkPreview, BM_GETCHECK, 0, 0) ==
                        BST_CHECKED &&
                    g_dirty) {
                    WriteConfigFromUI();
                    ApplyClearedDirty();
                }
                return 0;
            }
            if (id == ID_COLOR_PICK && code == BN_CLICKED) {
                for (auto& row : g_rows) {
                    if (row.spec->kind == Kind::Color && row.pick == (HWND)lParam) {
                        ShowColorPicker(hwnd, row);
                        return 0;
                    }
                }
                return 0;
            }
            if ((id == ID_TINT_PICK || id == ID_COLOR_SWATCH) &&
                code == BN_CLICKED) {
                for (auto& row : g_rows) {
                    if (wcscmp(row.spec->key, L"Tint.tintPreset") != 0)
                        continue;
                    if (row.pick != (HWND)lParam &&
                        row.swatch != (HWND)lParam)
                        continue;
                    // Start the dialog from the EFFECTIVE color.
                    wchar_t sel[32] = {};
                    GetWindowTextW(row.ctl, sel, 32);
                    COLORREF start = RGB(0x05, 0x15, 0x29);
                    bool found = false;
                    for (const TintPresetColor& pr : kTintPresetColors) {
                        if (wcscmp(sel, pr.name) == 0) {
                            start = pr.rgb;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        PCWSTR q = g_customHex.c_str();
                        while (*q == L' ' || *q == L'#') q++;
                        if (wcslen(q) == 6) {
                            wchar_t b[3] = {0, 0, 0};
                            auto byteAt = [&](int i) {
                                b[0] = q[i * 2];
                                b[1] = q[i * 2 + 1];
                                return (int)wcstoul(b, nullptr, 16);
                            };
                            start = RGB(byteAt(0), byteAt(1), byteAt(2));
                        }
                    }
                    CHOOSECOLORW cc = {sizeof(cc)};
                    cc.hwndOwner = hwnd;
                    cc.Flags = CC_RGBINIT | CC_FULLOPEN;  // seed + expanded
                    cc.rgbResult = start;
                    static COLORREF s_cust[16] = {};
                    cc.lpCustColors = s_cust;
                    if (ChooseColorW(&cc)) {
                        wchar_t out[16];
                        swprintf_s(out, L"#%02X%02X%02X",
                                   GetRValue(cc.rgbResult),
                                   GetGValue(cc.rgbResult),
                                   GetBValue(cc.rgbResult));
                        g_customHex = out;
                        // Engine contract: unknown preset name = use hex.
                        int idx = (int)SendMessageW(
                            row.ctl, CB_FINDSTRINGEXACT, -1,
                            (LPARAM)L"Custom");
                        if (idx >= 0)
                            SendMessageW(row.ctl, CB_SETCURSEL, idx, 0);
                        OnControlChanged();
                        InvalidateRect(row.swatch, nullptr, TRUE);
                    }
                    return 0;
                }
                return 0;
            }
            if (id == ID_DIAG_OPENFOLDER && code == BN_CLICKED) {
                std::wstring dir = GetCfgDir();
                CreateDirectoryW(dir.c_str(), nullptr);
                ShellExecuteW(hwnd, L"open", dir.c_str(), nullptr, nullptr,
                              SW_SHOWNORMAL);
                return 0;
            }
            if (id >= 2000 && id < 2000 + (int)g_rows.size()) {
                if (code == BN_CLICKED || code == CBN_SELCHANGE ||
                    code == EN_CHANGE) {
                    if (code == CBN_SELCHANGE && g_rows[id - 2000].swatch)
                        InvalidateRect(g_rows[id - 2000].swatch, nullptr,
                                       TRUE);
                    Wh_Log(L"[CTRL] row changed id=%d code=%d", id, code);
                    OnControlChanged();
                }
                return 0;
            }
            // Profiles
            if (id == ID_PROFILE_LIST && code == LBN_SELCHANGE) {
                HWND list = (HWND)lParam;
                int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t name[128];
                    SendMessageW(list, LB_GETTEXT, sel, (LPARAM)name);
                    SetWindowTextW(GetDlgItem(g_panes[SEC_PROFILES],
                                              ID_PROFILE_NAME),
                                   name);
                }
                return 0;
            }
            if (id == ID_PROFILE_SAVE && code == BN_CLICKED) {
                wchar_t name[128] = {};
                GetWindowTextW(
                    GetDlgItem(g_panes[SEC_PROFILES], ID_PROFILE_NAME), name,
                    128);
                // Trim inline (name is a plain array).
                {
                    wchar_t* s = name;
                    while (*s == L' ' || *s == L'\t') s++;
                    wchar_t* e = s + wcslen(s);
                    while (e > s && (e[-1] == L' ' || e[-1] == L'\t')) *--e = 0;
                    if (s != name) memmove(name, s, (wcslen(s) + 1) * sizeof(wchar_t));
                }
                if (!name[0]) {
                    MessageBoxW(hwnd, L"Enter a profile name first.",
                                L"Ab Tenebris", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                std::unordered_map<std::wstring, std::wstring> kv;
                CollectAllValues(kv, &g_initialSnapshot);
                std::wstring dir = GetProfileDir();
                CreateDirectoryW(dir.c_str(), nullptr);
                std::wstring finalPath = dir + L"\\" + name + L".profile";
                // Reuse the config writer by staging the file as settings.cfg
                // format: write via temp map, then copy to profile path.
                std::wstring body;
                WORD bom = 0xFEFF;
                for (const auto& e : kCtrlCfgKeys) {
                    auto it = kv.find(e.key);
                    if (it == kv.end()) continue;
                    body += e.key;
                    body += L'=';
                    body += it->second;
                    body += L"\r\n";
                }
                HANDLE h = CreateFileW(finalPath.c_str(), GENERIC_WRITE, 0,
                                       nullptr, CREATE_ALWAYS, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    DWORD wrote;
                    WriteFile(h, &bom, sizeof(bom), &wrote, nullptr);
                    WriteFile(h, body.data(),
                              (DWORD)(body.size() * sizeof(wchar_t)), &wrote,
                              nullptr);
                    CloseHandle(h);
                }
                RefreshProfileList(g_panes[SEC_PROFILES]);
                return 0;
            }
            if (id == ID_PROFILE_APPLY && code == BN_CLICKED) {
                HWND list = GetDlgItem(g_panes[SEC_PROFILES], ID_PROFILE_LIST);
                int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (sel < 0) {
                    MessageBoxW(hwnd, L"Select a profile to apply.",
                                L"Ab Tenebris", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                wchar_t name[128];
                SendMessageW(list, LB_GETTEXT, sel, (LPARAM)name);
                std::unordered_map<std::wstring, std::wstring> kv;
                ReadConfigFile(GetProfileDir() + L"\\" + name + L".profile",
                               kv);
                if (kv.empty()) {
                    MessageBoxW(hwnd, L"Profile file is empty or missing.",
                                L"Ab Tenebris", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                // Merge: profile values override; keep any keys the profile
                // lacks from the current file (forward compatibility).
                std::unordered_map<std::wstring, std::wstring> cur;
                ReadConfigFile(GetCfgPath(), cur);
                for (auto& e : cur)
                    if (kv.find(e.first) == kv.end()) kv[e.first] = e.second;
                WriteConfigMap(kv);
                g_initialSnapshot = kv;  // cancel baseline = what we applied
                LoadValuesIntoControls();
                ApplyClearedDirty();
                return 0;
            }
            if (id == ID_PROFILE_DELETE && code == BN_CLICKED) {
                HWND list = GetDlgItem(g_panes[SEC_PROFILES], ID_PROFILE_LIST);
                int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t name[128];
                    SendMessageW(list, LB_GETTEXT, sel, (LPARAM)name);
                    std::wstring p = GetProfileDir() + L"\\" + name +
                                     L".profile";
                    if (MessageBoxW(hwnd,
                                    (std::wstring(L"Delete profile \"") +
                                     name + L"\"?")
                                        .c_str(),
                                    L"Ab Tenebris",
                                    MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                        DeleteFileW(p.c_str());
                        RefreshProfileList(g_panes[SEC_PROFILES]);
                    }
                }
                return 0;
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == TIMER_PREVIEW) {
                KillTimer(hwnd, TIMER_PREVIEW);
                WriteConfigFromUI();
            }
            return 0;
        case WM_CLOSE: {
            // Unapplied non-preview changes die with the window (classic
            // contract); preview writes were already applied to the file.
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            g_settingsWnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The native Win32 settings window (the PySide6 experiment is rolled back;
// the next era rebuilds this UI in WebView2 — see WEBVIEW2_PLAN.md).
static void OpenSettingsWindow() {
    if (g_settingsWnd) {
        ShowWindow(g_settingsWnd, SW_RESTORE);
        SetForegroundWindow(g_settingsWnd);
        return;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = g_trayIcon ? g_trayIcon
                          : LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;
    RegisterClassW(&wc);

    int wndW = 960;  // raw px — fixed per Doc; we adjust slowly from here
    int wndH = 700;
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - wndW) / 2;
    int y = work.top + ((work.bottom - work.top) - wndH) / 2;

    // FIXED size per Doc: caption + sysmenu + minimize only — no thick
    // frame, no maximize box. Size adjustments are code changes from here.
    g_settingsWnd = CreateWindowExW(
        0, kWindowClassName, kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
            WS_CLIPCHILDREN,
        x, y, wndW, wndH,
                                    nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_settingsWnd) return;

    ShowWindow(g_settingsWnd, SW_SHOW);
    SetForegroundWindow(g_settingsWnd);
}

////////////////////////////////////////////////////////////////////////////////
// Tray icon

static void ShowTrayContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"Open Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"About");
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0,
                             hwnd, nullptr);
    DestroyMenu(menu);
    // Eat the stray message so the menu doesn't reopen (classic tray quirk).
    MSG flush;
    PeekMessageW(&flush, hwnd, 0, 0, PM_NOREMOVE);

    switch (cmd) {
        case 1:
            OpenSettingsWindow();
            break;
        case 2:
            MessageBoxW(hwnd,
                        L"Ab Tenebris Control Panel\n"
                        L"Companion of the transparent-desktop-icons-spotlight "
                        L"engine mod.\n\n"
                        L"Out of the darkness.",
                        L"Ab Tenebris", MB_OK);
            break;
    }
}

// Tray icon: imageres.dll #96 - the Windows "night light" monitor-with-moon
// icon (Doc's pick, 2026-11-20). Ships with Windows on every machine: no
// local file, no embedding. Index verified visually at 16-48px on this
// build; if a future Windows shuffles indices or the DLL is missing, the
// loader fails cleanly and the generic application icon takes over.
static const int kTrayIconIndex = 96;

static HICON LoadTrayIcon(int size) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH - 20) return nullptr;
    wcscat_s(path, L"\\imageres.dll");
    HICON h = nullptr;
    // Try the small-icon slot, then the large slot (SHDefExtractIcon fills
    // whichever pointer it gets; LOWORD of nIconSize is the large size).
    if (SUCCEEDED(SHDefExtractIconW(path, kTrayIconIndex, 0, nullptr, &h,
                                    (UINT)MAKEWORD(size, 0)))) {
        return h;
    }
    if (SUCCEEDED(SHDefExtractIconW(path, kTrayIconIndex, 0, &h, nullptr,
                                    (UINT)MAKEWORD(size, 0)))) {
        return h;
    }
    return nullptr;
}

static const UINT_PTR kTrayRetryTimer = 7;

static bool AddTrayIcon(HWND hwnd, const wchar_t* reason) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WMAPP_TRAYICON;
    // Load ONCE: re-adds (TaskbarCreated) reuse the handle — the settings
    // window class holds it too, so it must never be replaced or freed.
    if (!g_trayIcon) {
        g_trayIcon = LoadTrayIcon(GetSystemMetrics(SM_CXSMICON));
    }
    if (!g_trayIcon) {
        // Larger size for the settings-window title bar too.
        g_trayIcon = LoadTrayIcon(48);
    }
    g_nid.hIcon = g_trayIcon ? g_trayIcon
                             : (HICON)LoadImage(nullptr, IDI_APPLICATION,
                                                IMAGE_ICON, 0, 0, LR_SHARED);
    wcscpy_s(g_nid.szTip, L"Ab Tenebris");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
    // Log every attempt: the boot-race failure (2026-11-09) was silent.
    CtlLog(L"[TRAY] add (%s) ok=%d pid=%lu", reason, (int)g_trayAdded.load(),
           GetCurrentProcessId());
    return g_trayAdded;
}

static void RemoveTrayIcon() {
    if (g_trayAdded.exchange(false)) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        CtlLog(L"[TRAY] remove pid=%lu", GetCurrentProcessId());
    }
}

// In-process screenshot: BitBlt the settings window's real pixels (children
// included — external PrintWindow renders these unreliably) and save a PNG
// next to the config file.
static void TakeScreenshot() {
    if (!g_settingsWnd) return;
    RECT rc;
    GetWindowRect(g_settingsWnd, &rc);
    // True visible bounds (GetWindowRect includes invisible DWM resize borders
    // that bleed neighboring windows into the capture).
    RECT fb = rc;
    DwmGetWindowAttribute(g_settingsWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fb,
                          sizeof(fb));
    int offX = fb.left - rc.left, offY = fb.top - rc.top;
    int w = fb.right - fb.left, h = fb.bottom - fb.top;
    if (w <= 0 || h <= 0) return;

    HDC wndDc = GetWindowDC(g_settingsWnd);
    HDC mem = CreateCompatibleDC(wndDc);
    HBITMAP bmp = CreateCompatibleBitmap(wndDc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, wndDc, offX, offY, SRCCOPY);

    std::vector<BYTE> pixels((size_t)w * h * 4);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    GetDIBits(mem, bmp, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(g_settingsWnd, wndDc);

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            break;
        if (FAILED(factory->CreateStream(&stream))) break;
        std::wstring dir = GetCfgDir();
        CreateDirectoryW(dir.c_str(), nullptr);
        if (FAILED(stream->InitializeFromFilename(
                (dir + L"\\snap.png").c_str(), GENERIC_WRITE))) break;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                          &encoder))) break;
        if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) break;
        if (FAILED(frame->Initialize(nullptr))) break;
        if (FAILED(frame->SetSize((UINT)w, (UINT)h))) break;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&fmt))) break;
        if (FAILED(frame->WritePixels(h, (UINT)(w * 4), (UINT)pixels.size(),
                                      pixels.data())))
            break;
        frame->Commit();
        encoder->Commit();
        CtlLog(L"[SNAP] saved %dx%d", w, h);
    } while (false);
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
}

////////////////////////////////////////////////////////////////////////////////
// Hidden message window (tray callbacks; later: hotkey, engine pings)

static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
    // Explorer broadcasts "TaskbarCreated" whenever its taskbar comes up
    // (logon AND every explorer restart); icons added before that are lost.
    // Boot race 2026-11-09: this host started 2s before explorer existed and
    // the one-shot add failed silently. Pattern ported from cursor-tail.
    static const UINT s_taskbarCreated =
        RegisterWindowMessageW(L"TaskbarCreated");
    if (s_taskbarCreated && msg == s_taskbarCreated) {
        AddTrayIcon(hwnd, L"TaskbarCreated");
        return 0;
    }
    // Retry timer (desktop-icon-guardian pattern): every 5s while the icon
    // isn't in the tray (taskbar not up yet at boot).
    if (msg == WM_TIMER && wParam == kTrayRetryTimer) {
        if (!g_trayAdded) AddTrayIcon(hwnd, L"retry");
        return 0;
    }
    if (msg == WMAPP_SWITCHSECTION) {
        if (g_settingsWnd) {
            SendMessageW(g_sidebar, LB_SETCURSEL, (WPARAM)lParam, 0);
            SwitchSection((int)lParam);
        }
        return 0;
    }
    if (msg == WMAPP_SCREENSHOT) {
        TakeScreenshot();
        return 0;
    }
    if (msg == WMAPP_TRAYICON) {
        if (LOWORD(lParam) == WM_CONTEXTMENU ||
            LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayContextMenu(hwnd);
        } else if (LOWORD(lParam) == WM_LBUTTONUP ||
                   LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            OpenSettingsWindow();
        }
        return 0;
    }
    switch (msg) {
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The UI thread of the tool process: tray + windows + message loop.
static DWORD WINAPI UIThreadProc(void*) {
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES |
                                                 ICC_BAR_CLASSES |
                                                 ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icc);

    g_font = MakePtFont(10, false);
    g_fontBold = MakePtFont(10, true);
    // 🦇 rows need emoji fallback — Segoe UI alone renders tofu boxes.
    g_fontEmoji = MakePtFont(10, false, L"Segoe UI Emoji");
    g_fontMdl2 = MakePtFont(12, false, L"Segoe MDL2 Assets");
    g_brushBg = CreateSolidBrush(kColBg);
    g_brushSidebar = CreateSolidBrush(kColSidebar);
    g_brushSidebarHover = CreateSolidBrush(RGB(44, 44, 44));
    g_brushInput = CreateSolidBrush(kColInputBg);
    g_brushAccent = CreateSolidBrush(kColAccent);
    g_brushStroke = CreateSolidBrush(RGB(58, 58, 58));
    g_brushHover = CreateSolidBrush(RGB(58, 58, 58));
    g_brushThumb = CreateSolidBrush(RGB(150, 150, 150));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kMessageClassName;
    RegisterClassW(&wc);

    g_msgWnd = CreateWindowExW(0, kMessageClassName, L"AbTenebrisControl",
                               WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                               wc.hInstance, nullptr);
    if (g_msgWnd) {
        AddTrayIcon(g_msgWnd, L"init");
        SetTimer(g_msgWnd, kTrayRetryTimer, 5000, nullptr);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // IsDialogMessage gives Tab/Esc navigation in the settings window.
        if (g_settingsWnd && IsDialogMessageW(g_settingsWnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_settingsWnd) {
        DestroyWindow(g_settingsWnd);
        g_settingsWnd = nullptr;
    }
    if (g_msgWnd) {
        DestroyWindow(g_msgWnd);
        g_msgWnd = nullptr;
    }
    if (g_brushBg) DeleteObject(g_brushBg);
    if (g_brushSidebar) DeleteObject(g_brushSidebar);
    if (g_brushInput) DeleteObject(g_brushInput);
    if (g_brushAccent) DeleteObject(g_brushAccent);
    if (g_brushStroke) DeleteObject(g_brushStroke);
    if (g_brushHover) DeleteObject(g_brushHover);
    if (g_brushThumb) DeleteObject(g_brushThumb);
    if (g_trayIcon) DestroyIcon(g_trayIcon);
    if (SUCCEEDED(hrCom)) CoUninitialize();
    Wh_Log(L"[CTRL] UI thread exit");
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Tool mod callbacks (run only in the dedicated tool process)

static bool WhTool_ModInit() {
    g_ctrlUnloading = false;
    HANDLE thread =
        CreateThread(nullptr, 0, UIThreadProc, nullptr, 0, &g_uiThreadId);
    if (!thread) {
        Wh_Log(L"[CTRL] failed to start UI thread");
        return false;
    }
    CloseHandle(thread);
    Wh_Log(L"[CTRL] tool mod init, uiThreadId=%u", g_uiThreadId);
    return true;
}

static void WhTool_ModUninit() {
    g_ctrlUnloading = true;
    if (g_uiThreadId) {
        PostThreadMessageW(g_uiThreadId, WM_QUIT, 0, 0);
        for (int i = 0; i < 40; ++i) {
            if (!g_settingsWnd && !g_trayAdded) break;
            Sleep(50);
        }
        RemoveTrayIcon();
    }
    Wh_Log(L"[CTRL] tool mod uninit");
}

static void WhTool_ModSettingsChanged() {
    Wh_Log(L"[CTRL] settings changed (no-op)");
}

////////////////////////////////////////////////////////////////////////////////
// Tool-host plumbing: process classification, spawn, watchdog

static void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

static void SpawnToolModProcess() {
    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath,
                              ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR commandLine[MAX_PATH + 2 +
                      (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) -
                      1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath,
               WH_MOD_ID);
    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            Wh_Log(L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t = BOOL(WINAPI*)(
        HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
        WINBOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION,
        PHANDLE);
    CreateProcessInternalW_t pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule,
                                                 "CreateProcessInternalW");
    if (!pCreateProcessInternalW) {
        Wh_Log(L"No CreateProcessInternalW");
        return;
    }

    STARTUPINFO si{
        .cb = sizeof(STARTUPINFO),
        .dwFlags = STARTF_FORCEOFFFEEDBACK,
    };
    PROCESS_INFORMATION pi;
    if (!pCreateProcessInternalW(nullptr, currentProcessPath, commandLine,
                                 nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS,
                                 nullptr, nullptr, &si, &pi, nullptr)) {
        Wh_Log(L"CreateProcess failed (%lu)", GetLastError());
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    Wh_Log(L"[CTRL] tool mod process spawned");
}

static DWORD WINAPI ToolHostWatchdog(void*) {
    // Long-lived lifeguard: 5s poll cadence, respawn after the mutex has
    // been missing for 3 consecutive polls (~15s — longer than any healthy
    // injection gap, so a slow service-spawned host is never double-spawned).
    // Exits only on g_watchdogStop (Wh_ModUninit), never self-expires.
    int absent = 0;
    for (;;) {
        DWORD w = WaitForSingleObject(g_watchdogStop, 5000);
        if (w == WAIT_OBJECT_0) break;
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE,
                                  L"windhawk-tool-mod_" WH_MOD_ID);
        if (mutex) {
            CloseHandle(mutex);
            absent = 0;
            continue;
        }
        absent++;
        if (absent >= 3) {
            CtlLog(L"[CTRL] watchdog: tool host missing ~15s, respawning");
            Wh_Log(L"[CTRL] watchdog respawning tool host");
            absent = 0;
            SpawnToolModProcess();
        }
    }
    CtlLog(L"[CTRL] watchdog exiting (stop event)");
    return 0;
}


////////////////////////////////////////////////////////////////////////////////
// Merged lifecycle entry points (the ONLY Wh_* callbacks Windhawk sees)
////////////////////////////////////////////////////////////////////////////////


static bool IsWindhawkLauncherProcess() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;
    wchar_t* base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    return _wcsicmp(base, L"windhawk.exe") == 0;
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) &&
        sessionId == 0) {
        return FALSE;
    }

    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"-service") == 0 ||
                wcscmp(argv[i], L"-service-start") == 0 ||
                wcscmp(argv[i], L"-service-stop") == 0) {
                isExcluded = true;
                break;
            }
        }
        for (int i = 1; i < argc - 1; i++) {
            if (wcscmp(argv[i], L"-tool-mod") == 0) {
                isToolModProcess = true;
                if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
                    isCurrentToolModProcess = true;
                }
                break;
            }
        }
        LocalFree(argv);
    }
    if (isExcluded) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        // Tool host: tray + settings window. Same single-instance mutex wait
        // as the standalone control mod (old instance hand-over).
        g_isCtrlToolProcess = true;
        g_toolModProcessMutex =
            CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running (%s), waiting for the old instance",
                   WH_MOD_ID);
            bool owned = false;
            // Fail fast: 2s, not 10s. A second host that can't win the
            // mutex within 2s is a duplicate spawn (Windhawk 2.x spawns its
            // own tool host) — exiting keeps Windhawk's "initializing" state
            // from lingering ~20s while the loser waits out the old hand-over.
            for (int i = 0; i < 8 && !owned; i++) {
                if (g_toolModProcessMutex) CloseHandle(g_toolModProcessMutex);
                g_toolModProcessMutex =
                    CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
                if (!g_toolModProcessMutex) {
                    Sleep(250);
                    continue;
                }
                if (GetLastError() != ERROR_ALREADY_EXISTS) {
                    owned = true;
                    break;
                }
                WaitForSingleObject(g_toolModProcessMutex, 250);
            }
            if (!owned) {
                Wh_Log(L"Old tool instance never released the mutex");
                ExitProcess(1);
            }
        }
        CtlLog(L"[CTRL] tool host init, pid=%lu", GetCurrentProcessId());
        if (!WhTool_ModInit()) {
            CtlLog(L"[CTRL] WhTool_ModInit FAILED, exiting");
            ExitProcess(1);
        }
        // Hook the host's entry point so windhawk.exe never actually runs in
        // this process — it exists purely to host our message loop.
        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders =
            (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);
        DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* entryPoint = (BYTE*)dosHeader + entryPointRVA;
        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) {
        // Another mod's tool host: not ours, not the engine's business.
        return FALSE;
    }

    if (IsWindhawkLauncherProcess()) {
        // Plain windhawk.exe (the normal UI app): we only act as the launcher
        // that spawns and watchdogs our dedicated tool host process.
        g_isToolModProcessLauncher = true;
        return TRUE;
    }

    // Everything else that matched @include = explorer.exe family: the engine.
    return Engine_ModInit();
}

void Wh_ModAfterInit() {
    if (g_isToolModProcessLauncher) {
        g_watchdogStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        // Windhawk 2.x spawns the tool host itself; the host only creates
        // its mutex once the mod is INJECTED, which lags the spawn. Grace
        // poll ~3s before deciding nobody else is bringing one up, so we
        // don't race the service into a duplicate host.
        bool hostComing = false;
        for (int i = 0; i < 6 && !hostComing; i++) {
            HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE,
                                  L"windhawk-tool-mod_" WH_MOD_ID);
            if (m) {
                CloseHandle(m);
                hostComing = true;
                break;
            }
            Sleep(500);
        }
        if (hostComing) {
            CtlLog(L"[CTRL] launcher: tool host already coming up, spawn skipped");
        } else {
            CtlLog(L"[CTRL] launcher: no tool host after grace, spawning");
            SpawnToolModProcess();
        }
        CloseHandle(
            CreateThread(nullptr, 0, ToolHostWatchdog, nullptr, 0, nullptr));
    }
    // Engine and tool host need no AfterInit work.
}

void Wh_ModSettingsChanged() {
    if (g_isCtrlToolProcess) {
        WhTool_ModSettingsChanged();
        return;
    }
    if (g_isToolModProcessLauncher) {
        return;
    }
    Engine_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isCtrlToolProcess) {
        CtlLog(L"[CTRL] tool host uninit");
        WhTool_ModUninit();
        return;
    }
    if (g_isToolModProcessLauncher) {
        if (g_watchdogStop) SetEvent(g_watchdogStop);
        return;
    }
    Engine_ModUninit();
}
