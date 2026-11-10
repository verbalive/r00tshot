
#include "common.h"

#include <gdiplus.h>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdiplus.lib")
#endif

namespace r00t {

namespace {

inline DWORD GetB(const DWORD c) { return c & 0xFFu; }
inline DWORD GetG(const DWORD c) { return (c >> 8) & 0xFFu; }
inline DWORD GetR(const DWORD c) { return (c >> 16) & 0xFFu; }
inline DWORD PackRGB(int r, int g, int b)
{
    return ((DWORD)(r & 0xFF) << 16) | ((DWORD)(g & 0xFF) << 8) | (DWORD)(b & 0xFF);
}

void BlurPassHorizontal(const DWORD* src, DWORD* dst, int w, int h, int radius)
{
    const int window = radius * 2 + 1;
    for (int y = 0; y < h; ++y) {
        const DWORD* s = src + (size_t)y * (size_t)w;
        DWORD* d = dst + (size_t)y * (size_t)w;

        int sb = 0, sg = 0, sr = 0;
        for (int k = -radius; k <= radius; ++k) {
            const DWORD c = s[IClamp(k, 0, w - 1)];
            sb += (int)GetB(c); sg += (int)GetG(c); sr += (int)GetR(c);
        }
        for (int x = 0; x < w; ++x) {
            d[x] = PackRGB(sr / window, sg / window, sb / window);
            const DWORD drop = s[IClamp(x - radius, 0, w - 1)];
            const DWORD add = s[IClamp(x + radius + 1, 0, w - 1)];
            sb += (int)GetB(add) - (int)GetB(drop);
            sg += (int)GetG(add) - (int)GetG(drop);
            sr += (int)GetR(add) - (int)GetR(drop);
        }
    }
}

void BlurPassVertical(const DWORD* src, DWORD* dst, int w, int h, int radius)
{
    const int window = radius * 2 + 1;
    for (int x = 0; x < w; ++x) {
        int sb = 0, sg = 0, sr = 0;
        for (int k = -radius; k <= radius; ++k) {
            const DWORD c = src[(size_t)IClamp(k, 0, h - 1) * (size_t)w + (size_t)x];
            sb += (int)GetB(c); sg += (int)GetG(c); sr += (int)GetR(c);
        }
        for (int y = 0; y < h; ++y) {
            dst[(size_t)y * (size_t)w + (size_t)x] = PackRGB(sr / window, sg / window, sb / window);
            const DWORD drop = src[(size_t)IClamp(y - radius, 0, h - 1) * (size_t)w + (size_t)x];
            const DWORD add = src[(size_t)IClamp(y + radius + 1, 0, h - 1) * (size_t)w + (size_t)x];
            sb += (int)GetB(add) - (int)GetB(drop);
            sg += (int)GetG(add) - (int)GetG(drop);
            sr += (int)GetR(add) - (int)GetR(drop);
        }
    }
}

RECT ClipToSurface(const Surface& s, const RECT& r)
{
    RECT out;
    out.left = IClamp(r.left, 0, s.width);
    out.top = IClamp(r.top, 0, s.height);
    out.right = IClamp(r.right, 0, s.width);
    out.bottom = IClamp(r.bottom, 0, s.height);
    if (out.right < out.left) out.right = out.left;
    if (out.bottom < out.top) out.bottom = out.top;
    return out;
}

bool GetEncoderClsid(const wchar_t* mime, CLSID* clsid)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;

    std::vector<BYTE> buffer(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;

    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

}

bool SurfaceCreate(Surface& s, int width, int height)
{
    if (width <= 0 || height <= 0) return false;

    HDC mem = CreateCompatibleDC(nullptr);
    if (!mem) return false;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        return false;
    }

    s.old = static_cast<HBITMAP>(SelectObject(mem, bmp));
    s.dc = mem;
    s.bitmap = bmp;
    s.bits = bits;
    s.width = width;
    s.height = height;
    return true;
}

void SurfaceDestroy(Surface& s)
{
    if (s.dc) {
        if (s.old) SelectObject(s.dc, s.old);
        DeleteDC(s.dc);
    }
    if (s.bitmap) DeleteObject(s.bitmap);
    s = Surface();
}

bool CaptureVirtualScreen(Surface& out)
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return false;

    if (!SurfaceCreate(out, vw, vh)) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) {
        SurfaceDestroy(out);
        return false;
    }

    BOOL ok = BitBlt(out.dc, 0, 0, vw, vh, screen, vx, vy, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    ReleaseDC(nullptr, screen);

    if (!ok) {
        SurfaceDestroy(out);
        return false;
    }
    return true;
}

void BoxBlurRegion(const Surface& s, const RECT& r, int radius)
{
    if (!s.valid()) return;

    const RECT area = ClipToSurface(s, r);
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    if (w <= 1 || h <= 1) return;

    if (radius < 1) radius = 1;
    if (radius > 64) radius = 64;

    DWORD* pixels = static_cast<DWORD*>(s.bits);
    const size_t stride = (size_t)s.width;

    std::vector<DWORD> packed((size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) {
        const DWORD* src = pixels + (size_t)(area.top + y) * stride + (size_t)area.left;
        memcpy(packed.data() + (size_t)y * (size_t)w, src, (size_t)w * sizeof(DWORD));
    }

    std::vector<DWORD> scratch(packed.size());
    BlurPassHorizontal(packed.data(), scratch.data(), w, h, radius);
    BlurPassVertical(scratch.data(), packed.data(), w, h, radius);

    for (int y = 0; y < h; ++y) {
        DWORD* dst = pixels + (size_t)(area.top + y) * stride + (size_t)area.left;
        memcpy(dst, packed.data() + (size_t)y * (size_t)w, (size_t)w * sizeof(DWORD));
    }
}

void PixelateRegion(const Surface& s, const RECT& r, int blockSize)
{
    if (!s.valid()) return;

    const RECT area = ClipToSurface(s, r);
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    if (w <= 1 || h <= 1) return;

    if (blockSize < 2) blockSize = 2;
    if (blockSize > 64) blockSize = 64;

    DWORD* pixels = static_cast<DWORD*>(s.bits);
    const size_t stride = (size_t)s.width;

    for (int by = 0; by < h; by += blockSize) {
        const int bh = IMin(blockSize, h - by);
        for (int bx = 0; bx < w; bx += blockSize) {
            const int bw = IMin(blockSize, w - bx);

            DWORD sumB = 0, sumG = 0, sumR = 0;
            for (int y = 0; y < bh; ++y) {
                const DWORD* row = pixels + (size_t)(area.top + by + y) * stride + (size_t)(area.left + bx);
                for (int x = 0; x < bw; ++x) {
                    sumB += GetB(row[x]); sumG += GetG(row[x]); sumR += GetR(row[x]);
                }
            }

            const DWORD count = (DWORD)(bw * bh);
            const DWORD flat = PackRGB((int)(sumR / count), (int)(sumG / count), (int)(sumB / count));

            for (int y = 0; y < bh; ++y) {
                DWORD* row = pixels + (size_t)(area.top + by + y) * stride + (size_t)(area.left + bx);
                for (int x = 0; x < bw; ++x) row[x] = flat;
            }
        }
    }
}

bool CopySurfaceToClipboard(const Surface& s)
{
    if (!s.valid()) return false;

    const size_t imageBytes = (size_t)s.width * 4u * (size_t)s.height;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageBytes);
    if (!mem) return false;

    BYTE* base = static_cast<BYTE*>(GlobalLock(mem));
    if (!base) {
        GlobalFree(mem);
        return false;
    }

    BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(base);
    ZeroMemory(header, sizeof(BITMAPINFOHEADER));
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = s.width;
    header->biHeight = s.height;
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = (DWORD)imageBytes;

    BYTE* dst = base + sizeof(BITMAPINFOHEADER);
    const BYTE* src = static_cast<const BYTE*>(s.bits);
    const size_t rowBytes = (size_t)s.width * 4u;
    for (int y = 0; y < s.height; ++y) {
        memcpy(dst + (size_t)y * rowBytes,
               src + (size_t)(s.height - 1 - y) * rowBytes,
               rowBytes);
    }
    GlobalUnlock(mem);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIB, mem)) {
        CloseClipboard();
        GlobalFree(mem);
        return false;
    }
    CloseClipboard();
    return true;
}

bool SaveSurfaceToPng(const Surface& s, const std::wstring& path)
{
    if (!s.valid() || path.empty()) return false;

    Gdiplus::Bitmap bitmap(s.width, s.height, s.width * 4, PixelFormat32bppRGB,
                           static_cast<BYTE*>(s.bits));
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;

    CLSID encoder = {};
    if (!GetEncoderClsid(L"image/png", &encoder)) return false;

    return bitmap.Save(path.c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

}
