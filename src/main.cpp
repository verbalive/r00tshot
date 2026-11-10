
#include "common.h"

#include <gdiplus.h>
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

namespace {

HINSTANCE       g_instance = nullptr;
HWND            g_host = nullptr;
HICON           g_icon = nullptr;
HICON           g_iconSmall = nullptr;
bool            g_trayReady = false;
NOTIFYICONDATAW g_tray = {};
ULONG_PTR       g_gdiplusToken = 0;

const UINT kMenuCapture = 1;
const UINT kMenuFolder  = 2;
const UINT kMenuExit    = 3;

HICON CreateAppIcon(int size)
{
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;

    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(mem, color));

    RECT full = { 0, 0, size, size };
    HBRUSH background = CreateSolidBrush(RGB(32, 34, 38));
    FillRect(mem, &full, background);
    DeleteObject(background);

    const int inset = r00t::IMax(2, size / 6);
    const int arm = r00t::IMax(3, size / 4);
    const int thickness = r00t::IMax(2, size / 9);

    HPEN bracket = CreatePen(PS_SOLID, thickness, RGB(0, 130, 226));
    HGDIOBJ oldPen = SelectObject(mem, bracket);
    MoveToEx(mem, inset, inset + arm, nullptr); LineTo(mem, inset, inset); LineTo(mem, inset + arm, inset);
    MoveToEx(mem, size - inset - arm, inset, nullptr); LineTo(mem, size - inset, inset); LineTo(mem, size - inset, inset + arm);
    MoveToEx(mem, size - inset, size - inset - arm, nullptr); LineTo(mem, size - inset, size - inset); LineTo(mem, size - inset - arm, size - inset);
    MoveToEx(mem, inset + arm, size - inset, nullptr); LineTo(mem, inset, size - inset); LineTo(mem, inset, size - inset - arm);
    SelectObject(mem, oldPen);
    DeleteObject(bracket);

    const int dot = r00t::IMax(2, size / 10);
    HBRUSH center = CreateSolidBrush(RGB(236, 238, 242));
    RECT centerRect = { size / 2 - dot, size / 2 - dot, size / 2 + dot, size / 2 + dot };
    FillRect(mem, &centerRect, center);
    DeleteObject(center);

    DWORD* pixels = static_cast<DWORD*>(bits);
    const double radius = size * 0.22;
    const double limit = (double)size - radius;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx = (x < radius) ? (radius - x) : (x > limit ? x - limit : 0.0);
            const double dy = (y < radius) ? (radius - y) : (y > limit ? y - limit : 0.0);
            const bool inside = (dx * dx + dy * dy) <= radius * radius;
            DWORD& pixel = pixels[(size_t)y * (size_t)size + (size_t)x];
            pixel = (pixel & 0x00FFFFFFu) | (inside ? 0xFF000000u : 0x00000000u);
        }
    }

    SelectObject(mem, oldBitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = color;
    iconInfo.hbmMask = mask;
    HICON icon = CreateIconIndirect(&iconInfo);

    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void TrayAdd(HWND hwnd)
{
    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = hwnd;
    g_tray.uID = R00TSHOT_TRAY_ID;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = R00TSHOT_MSG_TRAY;
    g_tray.hIcon = g_icon ? g_icon : LoadIconW(nullptr, IDI_APPLICATION);

    lstrcpynW(g_tray.szTip, L"r00tshot - press Page Down to capture", ARRAYSIZE(g_tray.szTip));

    g_trayReady = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
    if (g_trayReady) {
        g_tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
    }
}

void TrayRemove()
{
    if (g_trayReady) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        g_trayReady = false;
    }
}

void TrayBalloon(const std::wstring& title, const std::wstring& text)
{
    if (!g_trayReady) return;

    NOTIFYICONDATAW data = g_tray;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_NONE;
    lstrcpynW(data.szInfoTitle, title.c_str(), ARRAYSIZE(data.szInfoTitle));
    lstrcpynW(data.szInfo, text.c_str(), ARRAYSIZE(data.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void OpenOutputFolder()
{
    const std::wstring dir = r00t::OutputDirectory();
    if (!r00t::MakeDirectoryTree(dir)) return;
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void CaptureNow()
{
    if (r00t::OverlayBusy()) return;
    if (!r00t::OverlayOpen(g_instance)) {
        TrayBalloon(R00TSHOT_APP_NAME, L"Could not capture the screen.");
    }
}

void ShowTrayMenu(HWND hwnd)
{
    POINT cursor;
    GetCursorPos(&cursor);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kMenuCapture, L"Capture now\tPage Down");
    AppendMenuW(menu, MF_STRING, kMenuFolder, L"Open screenshots folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    SetForegroundWindow(hwnd);
    const UINT command = (UINT)TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                              cursor.x, cursor.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    switch (command) {
    case kMenuCapture: CaptureNow(); break;
    case kMenuFolder:  OpenOutputFolder(); break;
    case kMenuExit:    DestroyWindow(hwnd); break;
    default: break;
    }
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_HOTKEY:
        if (wp == R00TSHOT_HOTKEY_ID) CaptureNow();
        return 0;

    case R00TSHOT_MSG_TRAY: {
        const UINT event = LOWORD(lp);
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        } else if (event == WM_LBUTTONUP || event == NIN_SELECT || event == WM_LBUTTONDBLCLK) {
            CaptureNow();
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}

namespace r00t {

void NotifyToast(const std::wstring& title, const std::wstring& text)
{
    TrayBalloon(title, text);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    g_instance = instance;

    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        g_gdiplusToken = 0;
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\r00tshot.single.instance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND running = FindWindowW(R00TSHOT_CLASS_HOST, nullptr);
        if (running) PostMessageW(running, WM_HOTKEY, R00TSHOT_HOTKEY_ID, 0);
        CloseHandle(single);
        return 0;
    }

    g_icon = CreateAppIcon(32);
    g_iconSmall = CreateAppIcon(16);

    WNDCLASSEXW hostClass = {};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.lpfnWndProc = HostProc;
    hostClass.hInstance = instance;
    hostClass.hIcon = g_icon;
    hostClass.hIconSm = g_iconSmall;
    hostClass.lpszClassName = R00TSHOT_CLASS_HOST;
    if (!RegisterClassExW(&hostClass)) return 1;

    g_host = CreateWindowExW(0, R00TSHOT_CLASS_HOST, R00TSHOT_APP_NAME, 0,
                             0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_host) return 1;

    if (!RegisterHotKey(g_host, R00TSHOT_HOTKEY_ID, 0, VK_NEXT)) {
        MessageBoxW(nullptr,
                    L"r00tshot could not register the Page Down hotkey.\n"
                    L"Another program is already using it.",
                    R00TSHOT_APP_NAME, MB_ICONWARNING | MB_OK);
    }

    TrayAdd(g_host);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(g_host, R00TSHOT_HOTKEY_ID);
    TrayRemove();

    if (g_icon) DestroyIcon(g_icon);
    if (g_iconSmall) DestroyIcon(g_iconSmall);
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (single) CloseHandle(single);
    return 0;
}
