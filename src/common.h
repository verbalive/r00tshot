
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>

#define R00TSHOT_APP_NAME      L"r00tshot"
#define R00TSHOT_APP_VERSION   L"1.0.0"
#define R00TSHOT_APP_BUILD     L"1.0.0.0"

#define R00TSHOT_CLASS_HOST    L"r00tshot.host.window"
#define R00TSHOT_CLASS_OVERLAY L"r00tshot.overlay.window"

#define R00TSHOT_HOTKEY_ID     0x5201u
#define R00TSHOT_TRAY_ID       1u
#define R00TSHOT_MSG_TRAY      (WM_APP + 10u)

#define R00TSHOT_RUN_KEY       L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define R00TSHOT_RUN_VALUE     L"r00tshot"

namespace r00t {

int  IMin(int a, int b);
int  IMax(int a, int b);
int  IClamp(int v, int lo, int hi);

std::wstring ExePath();
std::wstring ExeDir();
bool         MakeDirectoryTree(const std::wstring& dir);
bool         PathExists(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring KnownFolderPath(REFKNOWNFOLDERID id);

std::wstring OutputDirectory();
std::wstring OutputFileName();

struct Surface {
    HDC     dc     = nullptr;
    HBITMAP bitmap = nullptr;
    HBITMAP old    = nullptr;
    void*   bits   = nullptr;
    int     width  = 0;
    int     height = 0;

    bool valid() const { return dc != nullptr && bits != nullptr; }
};

bool SurfaceCreate(Surface& s, int width, int height);
void SurfaceDestroy(Surface& s);

bool CaptureVirtualScreen(Surface& out);

bool CopySurfaceToClipboard(const Surface& s);
bool SaveSurfaceToPng(const Surface& s, const std::wstring& path);

void BoxBlurRegion(const Surface& s, const RECT& r, int radius);
void PixelateRegion(const Surface& s, const RECT& r, int blockSize);

bool OverlayOpen(HINSTANCE instance);
bool OverlayBusy();

void NotifyToast(const std::wstring& title, const std::wstring& text);

}
