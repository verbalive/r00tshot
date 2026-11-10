
#include "common.h"

#include <gdiplus.h>

#include <cmath>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

namespace {

using namespace r00t;

enum ToolId {
    TOOL_SELECT = 0,
    TOOL_ARROW,
    TOOL_BLUR,
    TOOL_RECT,
    TOOL_MARKER,
    TOOL_PEN,
    TOOL_PIXELATE,
    TOOL_COUNT
};

enum ActionId { ACT_COPY = 0, ACT_SAVE, ACT_UNDO, ACT_CANCEL, ACT_COUNT };
enum BtnKind  { BTN_TITLE, BTN_TOOL, BTN_COLOR, BTN_ACTION };
enum DragMode { DRAG_NONE, DRAG_NEW_SEL, DRAG_MOVE_SEL, DRAG_RESIZE, DRAG_DRAW };

struct Annotation {
    int                tool  = TOOL_ARROW;
    POINT              a     = { 0, 0 };
    POINT              b     = { 0, 0 };
    std::vector<POINT> path;
    COLORREF           color = RGB(255, 59, 48);
    int                width = 3;
};

struct Button {
    int            kind  = BTN_TOOL;
    RECT           rc    = { 0, 0, 0, 0 };
    int            id    = 0;
    COLORREF       color = 0;
    const wchar_t* label = nullptr;
};

struct UndoState {
    RECT                    sel = { 0, 0, 0, 0 };
    bool                    has = false;
    std::vector<Annotation> annotations;
};

const int kPanelPad    = 8;
const int kBtnHeight   = 30;
const int kBtnPadX     = 11;
const int kBtnGap      = 4;
const int kSectionGap  = 14;
const int kSwatchSize  = 20;
const int kSwatchGap   = 8;
const int kPanelTop    = 14;
const int kPanelHeight = kBtnHeight + kPanelPad * 2;
const int kHandleHalf  = 4;
const int kGrabSlop    = 6;
const int kMinSel      = 12;
const int kChipHeight  = 22;

const int kGlowLayers  = 12;
const int kGlowAlpha   = 96;
const int kChromeMargin = kGlowLayers + 8;

const COLORREF kClrPanel     = RGB(44, 46, 50);
const COLORREF kClrPanelEdge = RGB(92, 96, 103);
const COLORREF kClrBtnHover  = RGB(66, 70, 77);
const COLORREF kClrBtnDown   = RGB(30, 32, 36);
const COLORREF kClrBtnActive = RGB(120, 125, 133);
const COLORREF kClrText      = RGB(236, 238, 241);
const COLORREF kClrTextDim   = RGB(158, 163, 170);
const COLORREF kClrSelLine   = RGB(214, 217, 222);
const COLORREF kClrChip      = RGB(28, 30, 33);

const BYTE kGlowR = 214;
const BYTE kGlowG = 220;
const BYTE kGlowB = 230;

const COLORREF kPalette[5] = {
    RGB(255, 59, 48), RGB(255, 196, 0), RGB(52, 199, 89), RGB(10, 132, 255), RGB(245, 245, 247)
};

const wchar_t* kToolText[TOOL_COUNT] = {
    L"Select", L"Arrow", L"Blur", L"Rectangle", L"Highlighter", L"Pen", L"Pixelate"
};

const wchar_t* kActionText[ACT_COUNT] = { L"Copy", L"Save", L"Undo", L"Cancel" };

const wchar_t* kToolTip[TOOL_COUNT] = {
    L"Select and crop an area  (1)", L"Draw an arrow  (2)", L"Blur a region  (3)",
    L"Draw a rectangle  (4)", L"Draw a highlighter stroke  (5)",
    L"Draw a freehand pen stroke  (6)", L"Pixelate a region  (7)"
};

const wchar_t* kActionTip[ACT_COUNT] = {
    L"Copy to clipboard  (Ctrl+C)", L"Save as PNG  (Ctrl+S)",
    L"Undo  (Ctrl+Z)", L"Cancel  (Esc)"
};

const wchar_t* kHintText = L"Drag to select an area.  Right click or Esc to cancel.";

POINT MakePoint(int x, int y) { POINT p; p.x = x; p.y = y; return p; }

RECT NormalizeRect(const POINT& a, const POINT& b)
{
    RECT r;
    r.left = IMin(a.x, b.x);
    r.top = IMin(a.y, b.y);
    r.right = IMax(a.x, b.x);
    r.bottom = IMax(a.y, b.y);
    return r;
}

int RectWidth(const RECT& r) { return r.right - r.left; }
int RectHeight(const RECT& r) { return r.bottom - r.top; }

bool PointInRect(const RECT& r, const POINT& p)
{
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

void FillRoundedRect(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF edge)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextLine(HDC dc, const RECT& r, const wchar_t* text, COLORREF color,
                  HFONT font, UINT format)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, color);
    RECT box = r;
    DrawTextW(dc, text, -1, &box, format | DT_NOPREFIX);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
}

int MeasureTextWidth(HDC dc, const wchar_t* text, HFONT font)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE size = { 0, 0 };
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, oldFont);
    return size.cx;
}

class Overlay
{
public:
    bool Begin(HINSTANCE instance);
    bool Alive() const { return m_hwnd != nullptr; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Dispatch(UINT msg, WPARAM wp, LPARAM lp);

    void   Repaint();
    void   RepaintRegion(const RECT& region);
    void   InvalidateStrips(const RECT& before, const RECT& after, int margin);
    void   InvalidateChipRestore();
    void   InvalidatePanel();
    void   ResetState();
    void   PaintFrame(HDC dc, const RECT& clip);
    void   PaintToolbar(HDC dc);
    void   PaintSelectionChrome(HDC dc);
    void   PaintTooltip(HDC dc);
    void   PaintHint(HDC dc);
    void   DrawGlow(HDC dc, const RECT& box) const;

    void   BuildToolbar();
    int    ButtonAt(const POINT& pt) const;
    int    HandleAt(const POINT& pt) const;
    COLORREF EffectiveColor() const;
    int    StrokeWidth() const;
    RECT   LiveSelection() const;
    RECT   AnnotationBounds(const Annotation& ann) const;
    RECT   ChipRectFor(const RECT& sel) const;

    void   ApplyEffects(const Surface& surface, int dx, int dy, bool includeLive) const;
    void   PaintEffects(HDC dc, const RECT& dirty, const RECT& sel);
    void   PaintAnnotation(HDC dc, const Annotation& ann, int dx, int dy) const;

    void   OnMouseDown(const POINT& pt);
    void   OnMouseMove(const POINT& pt);
    void   OnMouseUp(const POINT& pt);
    void   OnRightDown(const POINT& pt);
    void   OnKeyDown(WPARAM key);
    void   ActivateButton(int index);
    void   CancelDrag();

    void   PushUndo();
    void   DoUndo();
    void   DoCopy();
    void   DoSave();
    void   Close();
    bool   ComposeOutput(Surface& out) const;

    HWND        m_hwnd = nullptr;
    HINSTANCE   m_instance = nullptr;

    Surface     m_capture;
    Surface     m_dimmed;
    Surface     m_frame;

    HFONT       m_font = nullptr;
    HFONT       m_fontBold = nullptr;

    std::vector<Button>     m_buttons;
    std::vector<Annotation> m_annotations;
    std::vector<UndoState>  m_undo;

    RECT        m_panel = { 0, 0, 0, 0 };
    RECT        m_sel = { 0, 0, 0, 0 };
    bool        m_hasSel = false;

    RECT        m_hintBox = { 0, 0, 0, 0 };
    RECT        m_chipRect = { 0, 0, 0, 0 };
    int         m_chipW = 0;

    DragMode    m_drag = DRAG_NONE;
    POINT       m_dragFrom = { 0, 0 };
    POINT       m_dragTo = { 0, 0 };
    RECT        m_selBackup = { 0, 0, 0, 0 };
    int         m_resizeHandle = -1;

    Annotation  m_live;
    bool        m_hasLive = false;

    int         m_tool = TOOL_SELECT;
    COLORREF    m_color = kPalette[0];
    int         m_hover = -1;
    int         m_pressed = -1;
    bool        m_trackingLeave = false;
    bool        m_regainingFocus = false;
};

Overlay* g_overlay = nullptr;

void ForceForeground(HWND hwnd)
{
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD currentThread = GetCurrentThreadId();

    const bool attached = (foregroundThread != 0 && foregroundThread != currentThread) &&
                          AttachThreadInput(foregroundThread, currentThread, TRUE);

    ShowWindow(hwnd, SW_SHOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attached) AttachThreadInput(foregroundThread, currentThread, FALSE);
}

COLORREF Overlay::EffectiveColor() const
{
    if (m_tool == TOOL_MARKER) {
        return RGB(255, 216, 32);
    }
    return m_color;
}

int Overlay::StrokeWidth() const
{
    switch (m_tool) {
    case TOOL_MARKER: return 16;
    case TOOL_PEN:    return 2;
    case TOOL_RECT:   return 2;
    default:          return 3;
    }
}

bool Overlay::Begin(HINSTANCE instance)
{
    m_instance = instance;

    ResetState();

    if (!CaptureVirtualScreen(m_capture)) return false;

    const int width = m_capture.width;
    const int height = m_capture.height;

    if (!SurfaceCreate(m_dimmed, width, height) || !SurfaceCreate(m_frame, width, height)) {
        SurfaceDestroy(m_capture);
        SurfaceDestroy(m_dimmed);
        SurfaceDestroy(m_frame);
        return false;
    }

    BitBlt(m_dimmed.dc, 0, 0, width, height, m_capture.dc, 0, 0, SRCCOPY);
    Surface shade;
    if (SurfaceCreate(shade, width, height)) {
        DWORD* px = static_cast<DWORD*>(shade.bits);
        const size_t count = (size_t)width * (size_t)height;
        for (size_t i = 0; i < count; ++i) px[i] = 0x6E000000u;

        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(m_dimmed.dc, 0, 0, width, height, shade.dc, 0, 0, width, height, blend);
        SurfaceDestroy(shade);
    }

    m_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontBold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);

    static bool classReady = false;
    if (!classReady) {
        WNDCLASSEXW desc = {};
        desc.cbSize = sizeof(desc);
        desc.lpfnWndProc = WindowProc;
        desc.hInstance = instance;
        desc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        desc.lpszClassName = R00TSHOT_CLASS_OVERLAY;
        if (!RegisterClassExW(&desc)) {
            SurfaceDestroy(m_capture);
            SurfaceDestroy(m_dimmed);
            SurfaceDestroy(m_frame);
            return false;
        }
        classReady = true;
    }

    m_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, R00TSHOT_CLASS_OVERLAY,
                             R00TSHOT_APP_NAME, WS_POPUP,
                             vx, vy, width, height,
                             nullptr, nullptr, instance, this);
    if (!m_hwnd) {
        SurfaceDestroy(m_capture);
        SurfaceDestroy(m_dimmed);
        SurfaceDestroy(m_frame);
        return false;
    }

    BuildToolbar();

    SetWindowPos(m_hwnd, HWND_TOPMOST, vx, vy, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ForceForeground(m_hwnd);
    return true;
}

void Overlay::BuildToolbar()
{
    m_buttons.clear();

    HDC dc = GetDC(nullptr);
    HGDIOBJ oldFont = SelectObject(dc, m_font);

    auto textWidth = [&](const wchar_t* text) {
        SIZE size = { 0, 0 };
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
        return size.cx;
    };

    int x = kPanelPad;

    {
        SelectObject(dc, m_fontBold);
        const int width = textWidth(R00TSHOT_APP_NAME) + kBtnPadX * 2;
        SelectObject(dc, m_font);

        Button title;
        title.kind = BTN_TITLE;
        title.label = R00TSHOT_APP_NAME;
        title.rc = { x, 0, x + width, 0 };
        m_buttons.push_back(title);
        x += width + kSectionGap;
    }

    for (int i = 0; i < TOOL_COUNT; ++i) {
        Button b;
        b.kind = BTN_TOOL;
        b.id = i;
        b.label = kToolText[i];
        const int width = textWidth(b.label) + kBtnPadX * 2;
        b.rc = { x, 0, x + width, 0 };
        m_buttons.push_back(b);
        x += width + kBtnGap;
    }
    x += kSectionGap - kBtnGap;

    for (int i = 0; i < 5; ++i) {
        Button b;
        b.kind = BTN_COLOR;
        b.id = i;
        b.color = kPalette[i];
        b.rc = { x, 0, x + kSwatchSize, 0 };
        m_buttons.push_back(b);
        x += kSwatchSize + kSwatchGap;
    }
    x += kSectionGap - kSwatchGap;

    for (int i = 0; i < ACT_COUNT; ++i) {
        Button b;
        b.kind = BTN_ACTION;
        b.id = i;
        b.label = kActionText[i];
        const int width = textWidth(b.label) + kBtnPadX * 2;
        b.rc = { x, 0, x + width, 0 };
        m_buttons.push_back(b);
        x += width + kBtnGap;
    }

    const int hintWidth = textWidth(kHintText);
    m_hintBox.left = (m_capture.width - hintWidth) / 2;
    m_hintBox.top = kPanelTop + 22;
    m_hintBox.right = m_hintBox.left + hintWidth;
    m_hintBox.bottom = m_hintBox.top + 24;

    SelectObject(dc, oldFont);
    ReleaseDC(nullptr, dc);

    const int panelWidth = x - kBtnGap + kPanelPad;
    const int left = IMax(8, (m_capture.width - panelWidth) / 2);
    const int top = kPanelTop;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        Button& b = m_buttons[i];
        b.rc.left += left;
        b.rc.right += left;
        if (b.kind == BTN_COLOR) {
            b.rc.top = top + kPanelPad + (kBtnHeight - kSwatchSize) / 2;
            b.rc.bottom = b.rc.top + kSwatchSize;
        } else {
            b.rc.top = top + kPanelPad;
            b.rc.bottom = top + kPanelPad + kBtnHeight;
        }
    }
    m_panel = { left, top, left + panelWidth, top + kPanelHeight };
}

int Overlay::ButtonAt(const POINT& pt) const
{
    if (!m_hasSel) return -1;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        if (PointInRect(m_buttons[i].rc, pt)) return (int)i;
    }
    return -1;
}

int Overlay::HandleAt(const POINT& pt) const
{
    if (!m_hasSel) return -1;

    const int midX = (m_sel.left + m_sel.right) / 2;
    const int midY = (m_sel.top + m_sel.bottom) / 2;
    const POINT handles[8] = {
        MakePoint(m_sel.left,  m_sel.top),
        MakePoint(midX,        m_sel.top),
        MakePoint(m_sel.right, m_sel.top),
        MakePoint(m_sel.right, midY),
        MakePoint(m_sel.right, m_sel.bottom),
        MakePoint(midX,        m_sel.bottom),
        MakePoint(m_sel.left,  m_sel.bottom),
        MakePoint(m_sel.left,  midY)
    };

    for (int i = 0; i < 8; ++i) {
        const int dx = pt.x - handles[i].x;
        const int dy = pt.y - handles[i].y;
        if (dx >= -kGrabSlop && dx <= kGrabSlop && dy >= -kGrabSlop && dy <= kGrabSlop) return i;
    }
    return -1;
}

RECT Overlay::LiveSelection() const
{
    if (m_drag == DRAG_NEW_SEL) return NormalizeRect(m_dragFrom, m_dragTo);
    return m_sel;
}

RECT Overlay::ChipRectFor(const RECT& sel) const
{
    const int chipW = m_chipW;
    const int chipH = kChipHeight;

    if (chipW <= 0) return RECT{ 0, 0, 0, 0 };
    if (RectWidth(sel) < chipW + 12 || RectHeight(sel) < chipH + 12) return RECT{ 0, 0, 0, 0 };

    int top = sel.top + 7;
    if (top < m_panel.bottom + 6) top = m_panel.bottom + 6;
    if (top + chipH > sel.bottom - 7) top = sel.bottom - 7 - chipH;
    top = IMax(top, sel.top + 7);

    int left = sel.left + 7;
    left = IClamp(left, sel.left + 7, sel.right - 7 - chipW);

    return RECT{ left, top, left + chipW, top + chipH };
}

RECT Overlay::AnnotationBounds(const Annotation& ann) const
{
    if (ann.path.size() >= 2) {
        RECT r = { ann.path[0].x, ann.path[0].y, ann.path[0].x, ann.path[0].y };
        for (size_t i = 1; i < ann.path.size(); ++i) {
            if (ann.path[i].x < r.left)   r.left = ann.path[i].x;
            if (ann.path[i].y < r.top)    r.top = ann.path[i].y;
            if (ann.path[i].x > r.right)  r.right = ann.path[i].x;
            if (ann.path[i].y > r.bottom) r.bottom = ann.path[i].y;
        }
        return r;
    }
    return NormalizeRect(ann.a, ann.b);
}

void Overlay::Repaint()
{
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Overlay::RepaintRegion(const RECT& region)
{
    if (!m_hwnd) return;

    RECT r = region;
    r.left   = IMax(r.left, 0);
    r.top    = IMax(r.top, 0);
    r.right  = IMin(r.right, m_capture.width);
    r.bottom = IMin(r.bottom, m_capture.height);
    if (r.right <= r.left || r.bottom <= r.top) return;

    InvalidateRect(m_hwnd, &r, FALSE);
}

void Overlay::ResetState()
{
    m_buttons.clear();
    m_annotations.clear();
    m_undo.clear();

    m_panel = { 0, 0, 0, 0 };
    m_sel = { 0, 0, 0, 0 };
    m_hasSel = false;

    m_hintBox = { 0, 0, 0, 0 };
    m_chipRect = { 0, 0, 0, 0 };
    m_chipW = 0;

    m_drag = DRAG_NONE;
    m_dragFrom = { 0, 0 };
    m_dragTo = { 0, 0 };
    m_selBackup = { 0, 0, 0, 0 };
    m_resizeHandle = -1;

    m_live = Annotation();
    m_hasLive = false;

    m_tool = TOOL_SELECT;
    m_color = kPalette[0];
    m_hover = -1;
    m_pressed = -1;
    m_trackingLeave = false;
    m_regainingFocus = false;
}

void Overlay::InvalidateStrips(const RECT& before, const RECT& after, int margin)
{
    RECT box, overlap;
    UnionRect(&box, &before, &after);

    if (!IntersectRect(&overlap, &before, &after)) {
        RECT a = before;
        RECT b = after;
        InflateRect(&a, margin, margin);
        InflateRect(&b, margin, margin);
        RepaintRegion(a);
        RepaintRegion(b);
        return;
    }

    RECT strips[4] = {
        { box.left,  box.top,     box.right,   overlap.top    },
        { box.left,  overlap.bottom, box.right, box.bottom     },
        { box.left,  overlap.top,  overlap.left, overlap.bottom },
        { overlap.right, overlap.top, box.right, overlap.bottom }
    };
    for (int i = 0; i < 4; ++i) {
        InflateRect(&strips[i], margin, margin);
        RepaintRegion(strips[i]);
    }
}

void Overlay::InvalidateChipRestore()
{
    if (m_chipW <= 0) return;

    RECT oldChip = m_chipRect;
    RECT newChip = ChipRectFor(m_sel);
    InflateRect(&oldChip, 4, 4);
    InflateRect(&newChip, 4, 4);
    RepaintRegion(oldChip);
    RepaintRegion(newChip);
}

void Overlay::InvalidatePanel()
{
    RECT r = { 0, m_panel.top - kChromeMargin, m_capture.width,
               m_panel.bottom + 6 + 24 + kChromeMargin };
    RepaintRegion(r);
}

void Overlay::DrawGlow(HDC dc, const RECT& box) const
{
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const int width = RectWidth(box);
    const int height = RectHeight(box);

    for (int i = kGlowLayers; i >= 1; --i) {
        const BYTE alpha = (BYTE)(kGlowAlpha * (kGlowLayers - i + 1) / kGlowLayers);
        Gdiplus::Pen pen(Gdiplus::Color(alpha, kGlowR, kGlowG, kGlowB), 1.0f);
        graphics.DrawRectangle(&pen,
                               (float)(box.left - i), (float)(box.top - i),
                               (float)(width + 2 * i - 1), (float)(height + 2 * i - 1));
    }
}

void Overlay::PaintFrame(HDC target, const RECT& clip)
{
    const int w = m_capture.width;
    const int h = m_capture.height;

    RECT dirty = clip;
    dirty.left   = IMax(dirty.left, 0);
    dirty.top    = IMax(dirty.top, 0);
    dirty.right  = IMin(dirty.right, w);
    dirty.bottom = IMin(dirty.bottom, h);
    if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) return;

    const bool showSel = m_hasSel || m_drag == DRAG_NEW_SEL;
    const RECT sel = LiveSelection();

    BitBlt(target, dirty.left, dirty.top, RectWidth(dirty), RectHeight(dirty),
           m_dimmed.dc, dirty.left, dirty.top, SRCCOPY);

    if (showSel) {
        RECT inter;
        if (IntersectRect(&inter, &dirty, &sel)) {
            BitBlt(target, inter.left, inter.top, RectWidth(inter), RectHeight(inter),
                   m_capture.dc, inter.left, inter.top, SRCCOPY);
        }
    }

    if (showSel) PaintEffects(target, dirty, sel);

    if (showSel) {
        RECT area;
        if (IntersectRect(&area, &dirty, &sel)) {
            int saved = SaveDC(target);
            IntersectClipRect(target, area.left, area.top, area.right, area.bottom);
            for (size_t i = 0; i < m_annotations.size(); ++i) {
                PaintAnnotation(target, m_annotations[i], 0, 0);
            }
            if (m_hasLive) PaintAnnotation(target, m_live, 0, 0);
            RestoreDC(target, saved);
        }
    }

    auto touches = [&](const RECT& r) {
        RECT hit;
        return IntersectRect(&hit, &dirty, &r) != FALSE;
    };

    if (showSel) {
        RECT chrome = sel;
        InflateRect(&chrome, kChromeMargin, kChromeMargin);
        if (touches(chrome)) PaintSelectionChrome(target);
    }

    if (m_hasSel) {
        RECT box = m_panel;
        InflateRect(&box, kChromeMargin, kChromeMargin);
        if (touches(box)) PaintToolbar(target);

        RECT tip = { 0, m_panel.bottom, m_capture.width, m_panel.bottom + 40 };
        if (touches(tip)) PaintTooltip(target);
    } else {
        RECT hint = m_hintBox;
        InflateRect(&hint, kChromeMargin, kChromeMargin);
        if (touches(hint)) PaintHint(target);
    }
}

void Overlay::PaintHint(HDC dc)
{
    DrawTextLine(dc, m_hintBox, kHintText, kClrTextDim, m_font,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Overlay::PaintEffects(HDC dc, const RECT& dirty, const RECT& sel)
{
    std::vector<const Annotation*> effects;
    for (size_t i = 0; i < m_annotations.size(); ++i) {
        const int tool = m_annotations[i].tool;
        if (tool == TOOL_BLUR || tool == TOOL_PIXELATE) effects.push_back(&m_annotations[i]);
    }
    if (m_hasLive && (m_live.tool == TOOL_BLUR || m_live.tool == TOOL_PIXELATE)) {
        effects.push_back(&m_live);
    }

    for (size_t i = 0; i < effects.size(); ++i) {
        const Annotation& ann = *effects[i];

        RECT r = NormalizeRect(ann.a, ann.b);
        r.left   = IMax(r.left, 0);
        r.top    = IMax(r.top, 0);
        r.right  = IMin(r.right, m_capture.width);
        r.bottom = IMin(r.bottom, m_capture.height);
        if (RectWidth(r) < 1 || RectHeight(r) < 1) continue;

        RECT inter;
        if (!IntersectRect(&inter, &dirty, &r)) continue;

        BitBlt(m_frame.dc, r.left, r.top, RectWidth(r), RectHeight(r),
               m_dimmed.dc, r.left, r.top, SRCCOPY);

        RECT bright;
        if (IntersectRect(&bright, &r, &sel)) {
            BitBlt(m_frame.dc, bright.left, bright.top, RectWidth(bright), RectHeight(bright),
                   m_capture.dc, bright.left, bright.top, SRCCOPY);
        }

        const int span = IMin(RectWidth(r), RectHeight(r));
        if (ann.tool == TOOL_BLUR) {
            BoxBlurRegion(m_frame, r, IClamp(span / 5, 4, 48));
        } else {
            PixelateRegion(m_frame, r, IClamp(span / 8, 4, 24));
        }

        BitBlt(dc, inter.left, inter.top, RectWidth(inter), RectHeight(inter),
               m_frame.dc, inter.left, inter.top, SRCCOPY);
    }
}

void Overlay::ApplyEffects(const Surface& surface, int dx, int dy, bool includeLive) const
{
    std::vector<const Annotation*> effects;
    for (size_t i = 0; i < m_annotations.size(); ++i) {
        const int tool = m_annotations[i].tool;
        if (tool == TOOL_BLUR || tool == TOOL_PIXELATE) effects.push_back(&m_annotations[i]);
    }
    if (includeLive && m_hasLive && (m_live.tool == TOOL_BLUR || m_live.tool == TOOL_PIXELATE)) {
        effects.push_back(&m_live);
    }

    for (size_t i = 0; i < effects.size(); ++i) {
        const Annotation& ann = *effects[i];
        RECT r = NormalizeRect(ann.a, ann.b);
        OffsetRect(&r, dx, dy);

        if (ann.tool == TOOL_BLUR) {
            const int span = IMin(RectWidth(r), RectHeight(r));
            BoxBlurRegion(surface, r, IClamp(span / 5, 4, 48));
        } else {
            PixelateRegion(surface, r, IClamp(IMin(RectWidth(r), RectHeight(r)) / 8, 4, 24));
        }
    }
}

void Overlay::PaintAnnotation(HDC dc, const Annotation& ann, int dx, int dy) const
{
    const int ax = ann.a.x + dx;
    const int ay = ann.a.y + dy;
    const int bx = ann.b.x + dx;
    const int by = ann.b.y + dy;

    switch (ann.tool) {
    case TOOL_ARROW: {
        const double angle = atan2((double)(by - ay), (double)(bx - ax));
        const double length = sqrt((double)(bx - ax) * (bx - ax) + (double)(by - ay) * (by - ay));
        if (length < 4.0) break;

        const int head = (int)IMin((double)(12 + ann.width * 3), length - 1.0);
        const double baseX = bx - cos(angle) * head;
        const double baseY = by - sin(angle) * head;

        HPEN pen = CreatePen(PS_SOLID, ann.width, ann.color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, ax, ay, nullptr);
        LineTo(dc, (int)baseX, (int)baseY);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        const double wing = head * 0.45;
        POINT head3[3];
        head3[0] = MakePoint(bx, by);
        head3[1] = MakePoint((int)(baseX - sin(angle) * wing), (int)(baseY + cos(angle) * wing));
        head3[2] = MakePoint((int)(baseX + sin(angle) * wing), (int)(baseY - cos(angle) * wing));

        HBRUSH brush = CreateSolidBrush(ann.color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        Polygon(dc, head3, 3);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
        break;
    }
    case TOOL_RECT: {
        const RECT r = NormalizeRect(ann.a, ann.b);
        HPEN pen = CreatePen(PS_SOLID, ann.width, ann.color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left + dx, r.top + dy, r.right + dx + 1, r.bottom + dy + 1);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        break;
    }
    case TOOL_MARKER:
    case TOOL_PEN: {
        if (ann.path.size() < 2) break;
        HPEN pen = CreatePen(PS_SOLID, ann.width, ann.color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, ann.path[0].x + dx, ann.path[0].y + dy, nullptr);
        for (size_t i = 1; i < ann.path.size(); ++i) {
            LineTo(dc, ann.path[i].x + dx, ann.path[i].y + dy);
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        break;
    }
    case TOOL_BLUR:
    case TOOL_PIXELATE: {
        if (&ann != &m_live) break;
        const RECT r = NormalizeRect(ann.a, ann.b);
        HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left + dx, r.top + dy, r.right + dx + 1, r.bottom + dy + 1);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        break;
    }
    default:
        break;
    }
}

void Overlay::PaintSelectionChrome(HDC dc)
{
    const RECT sel = LiveSelection();
    if (RectWidth(sel) <= 0 || RectHeight(sel) <= 0) return;

    DrawGlow(dc, sel);

    HPEN pen = CreatePen(PS_SOLID, 1, kClrSelLine);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, sel.left, sel.top, sel.right + 1, sel.bottom + 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (!m_hasSel || m_drag == DRAG_NEW_SEL) return;

    const int midX = (sel.left + sel.right) / 2;
    const int midY = (sel.top + sel.bottom) / 2;
    const POINT handles[8] = {
        MakePoint(sel.left,  sel.top),
        MakePoint(midX,      sel.top),
        MakePoint(sel.right, sel.top),
        MakePoint(sel.right, midY),
        MakePoint(sel.right, sel.bottom),
        MakePoint(midX,      sel.bottom),
        MakePoint(sel.left,  sel.bottom),
        MakePoint(sel.left,  midY)
    };

    HBRUSH fill = CreateSolidBrush(kClrSelLine);
    HGDIOBJ oldFill = SelectObject(dc, fill);
    HGDIOBJ handlePen = SelectObject(dc, GetStockObject(NULL_PEN));
    for (int i = 0; i < 8; ++i) {
        Rectangle(dc, handles[i].x - kHandleHalf, handles[i].y - kHandleHalf,
                  handles[i].x + kHandleHalf + 1, handles[i].y + kHandleHalf + 1);
    }
    SelectObject(dc, handlePen);
    SelectObject(dc, oldFill);
    DeleteObject(fill);

    wchar_t label[64] = { 0 };
    wsprintfW(label, L"%d x %d", RectWidth(sel), RectHeight(sel));
    m_chipW = MeasureTextWidth(dc, label, m_fontBold) + 16;

    const RECT chip = ChipRectFor(sel);
    m_chipRect = chip;
    if (RectWidth(chip) > 0) {
        FillRoundedRect(dc, chip, 8, kClrChip, kClrChip);
        DrawTextLine(dc, chip, label, kClrText, m_fontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void Overlay::PaintToolbar(HDC dc)
{
    DrawGlow(dc, m_panel);
    FillRoundedRect(dc, m_panel, 8, kClrPanel, kClrPanelEdge);

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const Button& btn = m_buttons[i];
        const bool hovered = ((int)i == m_hover);
        const bool pressed = ((int)i == m_pressed);

        switch (btn.kind) {
        case BTN_TITLE: {
            RECT label = btn.rc;
            label.left += kBtnPadX;
            DrawTextLine(dc, label, btn.label, kClrText, m_fontBold,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case BTN_TOOL: {
            const bool active = (m_tool == btn.id);
            if (active) {
                FillRoundedRect(dc, btn.rc, 6, kClrBtnActive, kClrBtnActive);
            } else if (hovered || pressed) {
                const COLORREF fill = pressed ? kClrBtnDown : kClrBtnHover;
                FillRoundedRect(dc, btn.rc, 6, fill, fill);
            }
            DrawTextLine(dc, btn.rc, btn.label, active ? RGB(255, 255, 255) : kClrText,
                         active ? m_fontBold : m_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case BTN_COLOR: {
            const bool selected = (m_color == btn.color);
            if (hovered || selected) {
                RECT ring = btn.rc;
                InflateRect(&ring, -2, -2);
                HPEN pen = CreatePen(PS_SOLID, 2, selected ? kClrText : kClrPanelEdge);
                HGDIOBJ oldPen = SelectObject(dc, pen);
                HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Ellipse(dc, ring.left, ring.top, ring.right, ring.bottom);
                SelectObject(dc, oldBrush);
                SelectObject(dc, oldPen);
                DeleteObject(pen);
            }
            RECT dot = btn.rc;
            InflateRect(&dot, -3, -3);
            HBRUSH brush = CreateSolidBrush(btn.color);
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
            break;
        }
        case BTN_ACTION: {
            if (hovered || pressed) {
                const COLORREF fill = pressed ? kClrBtnDown : kClrBtnHover;
                FillRoundedRect(dc, btn.rc, 6, fill, fill);
            }
            DrawTextLine(dc, btn.rc, btn.label, kClrText, m_font,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        default:
            break;
        }
    }
}

void Overlay::PaintTooltip(HDC dc)
{
    if (m_hover < 0 || m_drag != DRAG_NONE || !m_hasSel) return;

    const Button& btn = m_buttons[(size_t)m_hover];
    const wchar_t* text = nullptr;
    if (btn.kind == BTN_TOOL) text = kToolTip[btn.id];
    else if (btn.kind == BTN_ACTION) text = kActionTip[btn.id];
    else return;

    const int textWidth = MeasureTextWidth(dc, text, m_font);
    const int boxWidth = textWidth + 18;
    const int boxHeight = 24;

    int left = (btn.rc.left + btn.rc.right) / 2 - boxWidth / 2;
    left = IClamp(left, 4, IMax(4, m_capture.width - boxWidth - 4));
    RECT box = { left, m_panel.bottom + 6, left + boxWidth, m_panel.bottom + 6 + boxHeight };

    FillRoundedRect(dc, box, 7, kClrChip, kClrPanelEdge);
    DrawTextLine(dc, box, text, kClrText, m_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Overlay::PushUndo()
{
    UndoState state;
    state.sel = m_sel;
    state.has = m_hasSel;
    state.annotations = m_annotations;
    m_undo.push_back(state);
    if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
}

void Overlay::DoUndo()
{
    if (m_undo.empty()) return;
    const UndoState state = m_undo.back();
    m_undo.pop_back();
    m_sel = state.sel;
    m_hasSel = state.has;
    m_annotations = state.annotations;
}

void Overlay::CancelDrag()
{
    if (m_drag == DRAG_MOVE_SEL || m_drag == DRAG_RESIZE) {
        m_sel = m_selBackup;
    }
    m_drag = DRAG_NONE;
    m_hasLive = false;
    m_resizeHandle = -1;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    Repaint();
}

void Overlay::OnMouseDown(const POINT& pt)
{
    const int buttonIndex = ButtonAt(pt);
    if (buttonIndex >= 0) {
        m_pressed = buttonIndex;
        SetCapture(m_hwnd);
        Repaint();
        return;
    }

    if (m_hasSel && PointInRect(m_panel, pt)) return;

    if (m_tool != TOOL_SELECT && m_hasSel && PointInRect(m_sel, pt)) {
        PushUndo();
        m_drag = DRAG_DRAW;
        m_live = Annotation();
        m_live.tool = m_tool;
        m_live.a = pt;
        m_live.b = pt;
        m_live.color = EffectiveColor();
        m_live.width = StrokeWidth();
        m_live.path.push_back(pt);
        m_hasLive = true;
        SetCapture(m_hwnd);
        Repaint();
        return;
    }

    const int handle = HandleAt(pt);
    if (handle >= 0 && m_hasSel) {
        PushUndo();
        m_drag = DRAG_RESIZE;
        m_resizeHandle = handle;
        m_dragFrom = pt;
        m_selBackup = m_sel;
        SetCapture(m_hwnd);
        Repaint();
        return;
    }

    if (m_hasSel && PointInRect(m_sel, pt)) {
        PushUndo();
        m_drag = DRAG_MOVE_SEL;
        m_dragFrom = pt;
        m_selBackup = m_sel;
        SetCapture(m_hwnd);
        Repaint();
        return;
    }

    m_drag = DRAG_NEW_SEL;
    m_dragFrom = pt;
    m_dragTo = pt;
    SetCapture(m_hwnd);
    Repaint();
}

void Overlay::OnMouseMove(const POINT& pt)
{
    if (m_drag == DRAG_NONE) {
        const int hover = ButtonAt(pt);
        if (hover != m_hover) {
            m_hover = hover;
            InvalidatePanel();
        }
        if (!m_trackingLeave) {
            TRACKMOUSEEVENT track = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&track);
            m_trackingLeave = true;
        }
        return;
    }

    const POINT previous = m_dragTo;
    m_dragTo = pt;

    switch (m_drag) {
    case DRAG_NEW_SEL: {
        const RECT before = NormalizeRect(m_dragFrom, previous);
        const RECT after  = NormalizeRect(m_dragFrom, pt);
        InvalidateStrips(before, after, kChromeMargin);
        break;
    }
    case DRAG_MOVE_SEL: {
        const RECT before = m_sel;

        const int dx = pt.x - m_dragFrom.x;
        const int dy = pt.y - m_dragFrom.y;
        const int width = RectWidth(m_selBackup);
        const int height = RectHeight(m_selBackup);
        m_sel.left = IClamp(m_selBackup.left + dx, 0, m_capture.width - width);
        m_sel.top = IClamp(m_selBackup.top + dy, 0, m_capture.height - height);
        m_sel.right = m_sel.left + width;
        m_sel.bottom = m_sel.top + height;

        InvalidateStrips(before, m_sel, kChromeMargin);
        InvalidateChipRestore();
        break;
    }
    case DRAG_RESIZE: {
        const RECT before = m_sel;

        RECT r = m_selBackup;
        const int dx = pt.x - m_dragFrom.x;
        const int dy = pt.y - m_dragFrom.y;
        if (m_resizeHandle == 0 || m_resizeHandle == 6 || m_resizeHandle == 7) r.left += dx;
        if (m_resizeHandle == 2 || m_resizeHandle == 3 || m_resizeHandle == 4) r.right += dx;
        if (m_resizeHandle == 0 || m_resizeHandle == 1 || m_resizeHandle == 2) r.top += dy;
        if (m_resizeHandle == 4 || m_resizeHandle == 5 || m_resizeHandle == 6) r.bottom += dy;

        r.left = IClamp(r.left, 0, m_capture.width);
        r.top = IClamp(r.top, 0, m_capture.height);
        r.right = IClamp(r.right, 0, m_capture.width);
        r.bottom = IClamp(r.bottom, 0, m_capture.height);
        if (RectWidth(r) < kMinSel) r.right = IMin(m_capture.width, r.left + kMinSel);
        if (RectHeight(r) < kMinSel) r.bottom = IMin(m_capture.height, r.top + kMinSel);
        m_sel = r;

        InvalidateStrips(before, m_sel, kChromeMargin);
        InvalidateChipRestore();
        break;
    }
    case DRAG_DRAW: {
        RECT changed = AnnotationBounds(m_live);

        if (m_live.tool == TOOL_PEN || m_live.tool == TOOL_MARKER) {
            const POINT& last = m_live.path.back();
            const int dx = pt.x - last.x;
            const int dy = pt.y - last.y;
            if (dx * dx + dy * dy >= 4) m_live.path.push_back(pt);
        } else {
            m_live.b = pt;
        }

        const RECT next = AnnotationBounds(m_live);
        UnionRect(&changed, &changed, &next);
        InflateRect(&changed, m_live.width + 6, m_live.width + 6);
        RepaintRegion(changed);
        break;
    }
    default:
        break;
    }
}

void Overlay::OnMouseUp(const POINT& pt)
{
    if (m_pressed >= 0) {
        const int index = m_pressed;
        m_pressed = -1;
        if (GetCapture() == m_hwnd) ReleaseCapture();
        ActivateButton(index);
        Repaint();
        return;
    }

    if (m_drag == DRAG_NEW_SEL) {
        const RECT r = NormalizeRect(m_dragFrom, pt);
        if (RectWidth(r) >= 6 && RectHeight(r) >= 6) {
            PushUndo();
            m_sel = r;
            m_hasSel = true;
        } else {
            m_hasSel = false;
        }
    } else if (m_drag == DRAG_DRAW) {
        bool keep = false;
        if (m_live.tool == TOOL_PEN || m_live.tool == TOOL_MARKER) {
            keep = m_live.path.size() > 1;
        } else if (m_live.tool == TOOL_BLUR || m_live.tool == TOOL_PIXELATE) {
            const RECT r = NormalizeRect(m_live.a, m_live.b);
            keep = RectWidth(r) >= 6 && RectHeight(r) >= 6;
        } else {
            const RECT r = NormalizeRect(m_live.a, m_live.b);
            keep = RectWidth(r) >= 4 || RectHeight(r) >= 4;
            m_live.b = pt;
        }
        if (keep) {
            m_annotations.push_back(m_live);
        } else {
            m_undo.pop_back();
        }
    }

    m_drag = DRAG_NONE;
    m_hasLive = false;
    m_resizeHandle = -1;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    Repaint();
}

void Overlay::OnRightDown(const POINT& pt)
{
    (void)pt;
    if (m_drag != DRAG_NONE) {
        CancelDrag();
        return;
    }
    Close();
}

void Overlay::OnKeyDown(WPARAM key)
{
    if (key == VK_ESCAPE) {
        if (m_drag != DRAG_NONE) {
            CancelDrag();
        } else if (m_hasSel) {
            PushUndo();
            m_hasSel = false;
            m_annotations.clear();
            m_tool = TOOL_SELECT;
            Repaint();
        } else {
            Close();
        }
        return;
    }

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (key == VK_RETURN) { DoSave(); return; }
    if (ctrl && key == 'C') { DoCopy(); return; }
    if (ctrl && key == 'S') { DoSave(); return; }
    if (ctrl && key == 'Z') { DoUndo(); Repaint(); return; }

    if (key >= '1' && key < '1' + TOOL_COUNT) {
        const int tool = TOOL_SELECT + (int)(key - '1');
        if (tool == TOOL_SELECT && m_tool == TOOL_SELECT) return;
        m_tool = tool;
        Repaint();
    }
}

void Overlay::ActivateButton(int index)
{
    if (index < 0 || index >= (int)m_buttons.size()) return;
    const Button& btn = m_buttons[(size_t)index];

    if (btn.kind == BTN_TOOL) {
        m_tool = btn.id;
    } else if (btn.kind == BTN_COLOR) {
        m_color = btn.color;
    } else if (btn.kind == BTN_ACTION) {
        switch (btn.id) {
        case ACT_COPY:   DoCopy(); break;
        case ACT_SAVE:   DoSave(); break;
        case ACT_UNDO:   DoUndo(); break;
        case ACT_CANCEL: Close(); break;
        default: break;
        }
    }
}

bool Overlay::ComposeOutput(Surface& out) const
{
    if (!m_hasSel) return false;

    const RECT r = m_sel;
    const int width = RectWidth(r);
    const int height = RectHeight(r);
    if (width < 1 || height < 1) return false;

    if (!SurfaceCreate(out, width, height)) return false;

    BitBlt(out.dc, 0, 0, width, height, m_capture.dc, r.left, r.top, SRCCOPY);

    ApplyEffects(out, -r.left, -r.top, false);

    int saved = SaveDC(out.dc);
    IntersectClipRect(out.dc, 0, 0, width, height);
    for (size_t i = 0; i < m_annotations.size(); ++i) {
        PaintAnnotation(out.dc, m_annotations[i], -r.left, -r.top);
    }
    RestoreDC(out.dc, saved);
    return true;
}

void Overlay::DoCopy()
{
    Surface output;
    if (!ComposeOutput(output)) { Close(); return; }

    const bool ok = CopySurfaceToClipboard(output);
    SurfaceDestroy(output);

    Close();
    NotifyToast(R00TSHOT_APP_NAME,
                ok ? L"Screenshot copied to the clipboard."
                   : L"Screenshot captured, but the clipboard is busy.");
}

void Overlay::DoSave()
{
    Surface output;
    if (!ComposeOutput(output)) { Close(); return; }

    const std::wstring dir = OutputDirectory();
    std::wstring path;
    bool ok = false;
    if (MakeDirectoryTree(dir)) {
        path = JoinPath(dir, OutputFileName());
        ok = SaveSurfaceToPng(output, path);
    }
    SurfaceDestroy(output);

    Close();
    if (ok) {
        NotifyToast(R00TSHOT_APP_NAME, L"Saved to " + path);
    } else {
        NotifyToast(R00TSHOT_APP_NAME, L"Could not write the screenshot to disk.");
    }
}

void Overlay::Close()
{
    if (m_hwnd) DestroyWindow(m_hwnd);
}

LRESULT CALLBACK Overlay::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Overlay* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Overlay*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Dispatch(msg, wp, lp);
}

LRESULT Overlay::Dispatch(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m_hwnd, &ps);
        PaintFrame(dc, ps.rcPaint);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        OnMouseMove(pt);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        OnMouseDown(pt);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        OnMouseUp(pt);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        OnRightDown(pt);
        return 0;
    }
    case WM_MOUSELEAVE:
        m_trackingLeave = false;
        if (m_hover != -1) {
            m_hover = -1;
            InvalidatePanel();
        }
        return 0;

    case WM_SETCURSOR: {
        HCURSOR cursor = LoadCursorW(nullptr, IDC_ARROW);

        if (m_drag == DRAG_MOVE_SEL) {
            cursor = LoadCursorW(nullptr, IDC_SIZEALL);
        } else if (m_drag == DRAG_NEW_SEL || m_drag == DRAG_DRAW) {
            cursor = LoadCursorW(nullptr, IDC_CROSS);
        } else {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            const int handle = HandleAt(pt);
            if (handle >= 0) {
                static const LPCWSTR handleCursors[8] = {
                    IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE,
                    IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE
                };
                cursor = LoadCursorW(nullptr, handleCursors[handle]);
            } else if (m_tool != TOOL_SELECT && m_hasSel && PointInRect(m_sel, pt)) {
                cursor = LoadCursorW(nullptr, IDC_CROSS);
            } else if (m_hasSel && PointInRect(m_panel, pt)) {
                cursor = LoadCursorW(nullptr, IDC_ARROW);
            } else if (m_tool != TOOL_SELECT) {
                cursor = LoadCursorW(nullptr, IDC_CROSS);
            }
        }
        SetCursor(cursor);
        return TRUE;
    }
    case WM_KEYDOWN:
        OnKeyDown(wp);
        return 0;

    case WM_KILLFOCUS:
        if (m_hwnd && !m_regainingFocus) {
            m_regainingFocus = true;
            SetForegroundWindow(m_hwnd);
            m_regainingFocus = false;
        }
        return 0;

    case WM_NCDESTROY: {
        const HWND dying = m_hwnd;
        SurfaceDestroy(m_capture);
        SurfaceDestroy(m_dimmed);
        SurfaceDestroy(m_frame);
        if (m_font) { DeleteObject(m_font); m_font = nullptr; }
        if (m_fontBold) { DeleteObject(m_fontBold); m_fontBold = nullptr; }
        m_buttons.clear();
        m_annotations.clear();
        m_undo.clear();
        m_hwnd = nullptr;
        g_overlay = nullptr;
        SetWindowLongPtrW(dying, GWLP_USERDATA, 0);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

}

namespace r00t {

bool OverlayBusy()
{
    return g_overlay != nullptr && g_overlay->Alive();
}

bool OverlayOpen(HINSTANCE instance)
{
    if (OverlayBusy()) return true;

    static Overlay instanceHolder;
    if (!instanceHolder.Begin(instance)) return false;
    g_overlay = &instanceHolder;
    return true;
}

}
