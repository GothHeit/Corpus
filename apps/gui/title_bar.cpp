#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "title_bar.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    const int kButtonRadius = 6;
    const int kButtonSpacing = 20;
    const int kFirstButtonX = 20;  // centers, from the left edge and the top of the window
    const int kButtonY = 20;
    const int kButtonHitSlop = 2;  // the buttons are small, so clicks just outside still count
    const int kTitleX = 80;
    const int kSidebarIconWidth = 16;
    const int kSidebarIconHeight = 12;
    const int kSortPadding = 8;     // inside the sort button, on each side
    const int kSortArrow = 10;      // the arrow's room, before the label
    const int kSortArrowGap = 6;    // between the arrow and the label

    enum HoverId { kSidebarHover = 0, kSortHover = 1, kAddHover = 2 };
}

int TitleBar::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void TitleBar::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_bounds = bounds;
}

void TitleBar::SetActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    // The title dims too.
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

RECT TitleBar::ButtonRect(int index) const
{
    const int radius = Scale(kButtonRadius);
    int centerX = m_bounds.left + Scale(kFirstButtonX) + index * Scale(kButtonSpacing);
    int centerY = m_bounds.top + Scale(kButtonY);

    RECT r = { centerX - radius, centerY - radius, centerX + radius, centerY + radius };
    InflateRect(&r, Scale(kButtonHitSlop), Scale(kButtonHitSlop));
    return r;
}

// The three buttons and the gaps between them: as on macOS, hovering any of them shows every symbol.
RECT TitleBar::ButtonsRect() const
{
    RECT first = ButtonRect(kClose);
    RECT last = ButtonRect(kMaximize);
    RECT r = { first.left, first.top, last.right, last.bottom };
    return r;
}

// At the left end of the search row, the same place whether the sidebar is open or hidden.
RECT TitleBar::SidebarButtonRect() const
{
    if (!m_sidebarButton)
        return RECT{};
    int size = Scale(theme::kSidebarToggle);
    int left = m_bounds.left + Scale(theme::kPageMargin);
    int top = m_bounds.top + Scale(theme::kTitleRowHeight) + (Scale(theme::kSearchRowHeight) - size) / 2;
    RECT r = { left, top, left + size, top + size };
    return r;
}

void TitleBar::ShowSidebarButton(bool shown)
{
    if (shown == m_sidebarButton)
        return;
    RECT before = SidebarButtonRect();
    m_sidebarButton = shown;
    m_hover.Reset(m_hWnd, kTitleHoverTimer);
    InvalidateRect(m_hWnd, shown ? nullptr : &before, FALSE);
}

// At the right end of the search row: Add folder last, the sort button before it.
RECT TitleBar::AddButtonRect() const
{
    if (!m_rowButtons)
        return RECT{};
    int size = Scale(theme::kSidebarToggle);
    int right = m_bounds.right - Scale(theme::kPageMargin);
    int top = m_bounds.top + Scale(theme::kTitleRowHeight) + (Scale(theme::kSearchRowHeight) - size) / 2;
    RECT r = { right - size, top, right, top + size };
    return r;
}

RECT TitleBar::SortButtonRect() const
{
    RECT add = AddButtonRect();
    if (IsRectEmpty(&add))
        return RECT{};
    int right = add.left - Scale(theme::kSidebarToggleGap);
    RECT r = { right - m_sortWidth, add.top, right, add.bottom };
    return r;
}

int TitleBar::RowButtonsWidth() const
{
    return m_sortWidth + Scale(theme::kSidebarToggle) + 2 * Scale(theme::kSidebarToggleGap);
}

int TitleBar::SortButtonWidth(HWND hWnd, Renderer &renderer, const std::wstring &label)
{
    int width = 2 * ScaleForWindow(hWnd, kSortPadding) + ScaleForWindow(hWnd, kSortArrow);
    if (!label.empty())
        width += ScaleForWindow(hWnd, kSortArrowGap) + static_cast<int>(std::ceil(renderer.TextWidth(label, Font::kCaption)));
    return std::max(width, ScaleForWindow(hWnd, theme::kSidebarToggle));
}

void TitleBar::SetSort(const std::wstring &label, bool descending, int width)
{
    if (label == m_sortLabel && descending == m_sortDescending && width == m_sortWidth)
        return;
    m_sortLabel = label;
    m_sortDescending = descending;
    m_sortWidth = width;
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void TitleBar::ShowRowButtons(bool shown)
{
    if (shown == m_rowButtons)
        return;
    m_rowButtons = shown;
    m_hover.Reset(m_hWnd, kTitleHoverTimer);
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void TitleBar::OnTimer(UINT_PTR timer)
{
    m_hover.Step(m_hWnd, kTitleHoverTimer);
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

bool TitleBar::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    anchor = SidebarButtonRect();
    text = strings::kTipSidebar;
    if (PtInRect(&anchor, pt))
        return true;
    anchor = AddButtonRect();
    text = strings::kTipAddFolder;
    if (PtInRect(&anchor, pt))
        return true;
    // The sort button says what it is in words, unless the window is too narrow for them.
    anchor = SortButtonRect();
    text = strings::kTipSort;
    return m_sortLabel.empty() && PtInRect(&anchor, pt) != 0;
}

void TitleBar::OnHover(POINT pt)
{
    RECT sidebarButton = SidebarButtonRect(), sortButton = SortButtonRect(), addButton = AddButtonRect();
    int id = PtInRect(&sidebarButton, pt) ? kSidebarHover : PtInRect(&sortButton, pt) ? kSortHover
        : PtInRect(&addButton, pt) ? kAddHover : -1;
    if (m_hover.Set(m_hWnd, kTitleHoverTimer, id))
        InvalidateRect(m_hWnd, &m_bounds, FALSE);

    RECT buttons = ButtonsRect();
    bool hover = PtInRect(&buttons, pt) != 0;
    if (hover == m_hoverButtons)
        return;
    m_hoverButtons = hover;
    InvalidateRect(m_hWnd, &buttons, FALSE);
}

void TitleBar::Paint(Renderer &renderer) const
{
    struct ButtonColors { D2D1_COLOR_F fill, rim; };
    const ButtonColors colors[kButtonCount] =
    {
        { theme::kCloseButton, theme::kCloseRim },
        { theme::kMinimizeButton, theme::kMinimizeRim },
        { theme::kMaximizeButton, theme::kMaximizeRim },
    };

    const float radius = static_cast<float>(Scale(kButtonRadius));
    const float rim = Scale(4) / 4.0f;  // one pixel at 96 DPI, thicker on denser screens
    const float glyph = radius * 0.45f;  // half the size of a symbol

    for (int i = 0; i < kButtonCount; i++)
    {
        RECT r = ButtonRect(i);
        D2D1_POINT_2F c = D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f);

        renderer.FillCircle(c, radius, m_active || m_hoverButtons ? colors[i].fill : theme::kInactiveButton);
        renderer.DrawCircle(c, radius - rim / 2.0f, m_active || m_hoverButtons ? colors[i].rim : theme::kInactiveRim, rim);

        if (!m_hoverButtons)
            continue;

        // x, - and +, like the symbols macOS shows on hover.
        float width = rim * 1.2f;
        if (i == kClose)
        {
            renderer.DrawLine(D2D1::Point2F(c.x - glyph, c.y - glyph), D2D1::Point2F(c.x + glyph, c.y + glyph), theme::kButtonGlyph, width);
            renderer.DrawLine(D2D1::Point2F(c.x - glyph, c.y + glyph), D2D1::Point2F(c.x + glyph, c.y - glyph), theme::kButtonGlyph, width);
        }
        else
        {
            renderer.DrawLine(D2D1::Point2F(c.x - glyph - 1, c.y), D2D1::Point2F(c.x + glyph + 1, c.y), theme::kButtonGlyph, width);
            if (i == kMaximize)
                renderer.DrawLine(D2D1::Point2F(c.x, c.y - glyph - 1), D2D1::Point2F(c.x, c.y + glyph + 1), theme::kButtonGlyph, width);
        }
    }

    // The name sits on the line of the buttons.
    RECT title = { m_bounds.left + Scale(kTitleX), m_bounds.top, m_bounds.right, m_bounds.top + Scale(theme::kTitleRowHeight) };
    renderer.DrawTextLine(strings::kAppName, Font::kBodyStrong, title, m_active ? theme::kText1 : theme::kText3);

    PaintSidebarButton(renderer);
    PaintRowButtons(renderer);
}

// The sort button: an arrow down for the order the key reads best in reverse (newest, largest, Z first), up
// otherwise, then the key; and the Add folder button, a plus. Both on a soft fill while the pointer is over them.
void TitleBar::PaintRowButtons(Renderer &renderer) const
{
    if (!m_rowButtons)
        return;
    float u = Scale(4) / 4.0f;  // one 96-DPI pixel
    float line = 1.5f * u;
    D2D1_COLOR_F color = m_active ? theme::kText2 : theme::kText3;
    float radius = static_cast<float>(Scale(theme::kControlRadius));

    RECT sort = SortButtonRect();
    float hover = m_hover.Level(kSortHover);
    if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(sort), radius, Faded(theme::kHover, hover));
    float x = static_cast<float>(sort.left + Scale(kSortPadding)) + kSortArrow * u / 2;
    if (m_sortLabel.empty())
        x = (sort.left + sort.right) / 2.0f;
    float cy = (sort.top + sort.bottom) / 2.0f;
    float tip = m_sortDescending ? 5.0f * u : -5.0f * u;
    renderer.DrawLine(D2D1::Point2F(x, cy - tip), D2D1::Point2F(x, cy + tip), color, line);
    const D2D1_POINT_2F head[] = { { x - 3.5f * u, cy + tip - (tip > 0 ? 3.5f : -3.5f) * u }, { x, cy + tip },
        { x + 3.5f * u, cy + tip - (tip > 0 ? 3.5f : -3.5f) * u } };
    renderer.DrawPolyline(head, 3, color, line);
    if (!m_sortLabel.empty())
    {
        RECT label = { sort.left + Scale(kSortPadding) + Scale(kSortArrow) + Scale(kSortArrowGap), sort.top,
                       sort.right - Scale(kSortPadding) + Scale(2), sort.bottom };
        renderer.DrawTextLine(m_sortLabel, Font::kCaption, label, m_active ? theme::kText2 : theme::kText3);
    }

    RECT add = AddButtonRect();
    hover = m_hover.Level(kAddHover);
    if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(add), radius, Faded(theme::kHover, hover));
    D2D1_POINT_2F c = D2D1::Point2F((add.left + add.right) / 2.0f, (add.top + add.bottom) / 2.0f);
    float arm = 6.0f * u;
    renderer.DrawLine(D2D1::Point2F(c.x - arm, c.y), D2D1::Point2F(c.x + arm, c.y), color, line);
    renderer.DrawLine(D2D1::Point2F(c.x, c.y - arm), D2D1::Point2F(c.x, c.y + arm), color, line);
}

// A 1.5px line drawing of a window with a pane down its left side, on a soft fill while the pointer is over it.
void TitleBar::PaintSidebarButton(Renderer &renderer) const
{
    if (!m_sidebarButton)
        return;
    RECT r = SidebarButtonRect();
    float hover = m_hover.Level(kSidebarHover);
    if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(r), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kHover, hover));

    float u = Scale(4) / 4.0f;  // one 96-DPI pixel
    float line = 1.5f * u;
    float cx = (r.left + r.right) / 2.0f;
    float cy = (r.top + r.bottom) / 2.0f;
    float w = kSidebarIconWidth * u;
    float h = kSidebarIconHeight * u;
    D2D1_RECT_F frame = D2D1::RectF(cx - w / 2 + line / 2, cy - h / 2 + line / 2, cx + w / 2 - line / 2, cy + h / 2 - line / 2);
    D2D1_COLOR_F color = m_active ? theme::kText2 : theme::kText3;
    renderer.DrawRoundedRect(frame, 2.0f * u, color, line);
    float divider = frame.left + 5.0f * u;
    renderer.DrawLine(D2D1::Point2F(divider, frame.top), D2D1::Point2F(divider, frame.bottom), color, line);

    if (m_mark)
    {
        float radius = 3.0f * u;
        D2D1_POINT_2F dot = D2D1::Point2F(frame.right, frame.top);
        renderer.FillCircle(dot, radius + u, theme::kBase);
        renderer.FillCircle(dot, radius, m_markCaution ? theme::kCaution : theme::kYuri);
    }
}

bool TitleBar::OnLButtonDown(POINT pt, bool doubleClick, Action &action)
{
    if (!PtInRect(&m_bounds, pt))
        return false;

    RECT closeRect = ButtonRect(kClose);
    RECT minRect = ButtonRect(kMinimize);
    RECT maxRect = ButtonRect(kMaximize);
    RECT sidebarRect = SidebarButtonRect();
    RECT sortRect = SortButtonRect();
    RECT addRect = AddButtonRect();

    // The second click of a double-click on the sidebar button toggles it again, rather than maximizing. The second
    // one on the other two does nothing: the first opened a menu or a dialog.
    if (PtInRect(&sidebarRect, pt))
        action = Action::kToggleSidebar;
    else if (PtInRect(&sortRect, pt))
        action = doubleClick ? Action::kNone : Action::kSort;
    else if (PtInRect(&addRect, pt))
        action = doubleClick ? Action::kNone : Action::kAddFolder;
    else if (PtInRect(&closeRect, pt))
        SendMessage(m_hWnd, WM_CLOSE, 0, 0);
    else if (PtInRect(&minRect, pt))
        ShowWindow(m_hWnd, SW_MINIMIZE);
    else if (PtInRect(&maxRect, pt))
        ToggleMaximize();
    else if (doubleClick)
        ToggleMaximize();
    else
        BeginDrag(pt);

    return true;
}

bool TitleBar::OnMouseMove()
{
    if (!m_awaitingDragThreshold)
        return false;

    ContinueMaximizedDrag();
    return true;
}

void TitleBar::OnLButtonUp()
{
    if (m_awaitingDragThreshold)
    {
        m_awaitingDragThreshold = false;
        ReleaseCapture();
    }
}

void TitleBar::BeginDrag(POINT pt)
{
    if (m_maximized)
    {
        m_dragAnchorScreen = pt;
        ClientToScreen(m_hWnd, &m_dragAnchorScreen);
        m_dragAnchorClientY = pt.y;
        m_awaitingDragThreshold = true;
        SetCapture(m_hWnd);
    }
    else
    {
        // Pretend the click hit a native caption so Windows runs its own move loop (and Aero Snap).
        ReleaseCapture();
        SendMessage(m_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
}

void TitleBar::ContinueMaximizedDrag()
{
    POINT screenPt;
    GetCursorPos(&screenPt);

    int dx = screenPt.x - m_dragAnchorScreen.x;
    int dy = screenPt.y - m_dragAnchorScreen.y;

    if (std::abs(dx) <= GetSystemMetrics(SM_CXDRAG) && std::abs(dy) <= GetSystemMetrics(SM_CYDRAG))
        return;

    m_awaitingDragThreshold = false;
    ReleaseCapture();

    // Restore, keeping the cursor at the same relative spot of the title bar.
    RECT beforeRect;
    GetWindowRect(m_hWnd, &beforeRect);
    int beforeWidth = beforeRect.right - beforeRect.left;
    float ratioX = static_cast<float>(m_dragAnchorScreen.x - beforeRect.left) / beforeWidth;

    ToggleMaximize();

    RECT afterRect;
    GetWindowRect(m_hWnd, &afterRect);
    int afterWidth = afterRect.right - afterRect.left;
    int afterHeight = afterRect.bottom - afterRect.top;

    int newLeft = static_cast<int>(m_dragAnchorScreen.x - ratioX * afterWidth);
    int newTop = m_dragAnchorScreen.y - m_dragAnchorClientY;

    SetWindowPos(m_hWnd, nullptr, newLeft, newTop, afterWidth, afterHeight, SWP_NOZORDER);

    SendMessage(m_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void TitleBar::ToggleMaximize()
{
    // Set first: resizing asks the window for its frame (WM_NCCALCSIZE), which depends on it.
    m_maximized = !m_maximized;
    if (m_maximized)
    {
        GetWindowRect(m_hWnd, &m_restoreRect);
        FitMaximized();
        SetRoundedCorners(m_hWnd, false);
    }
    else
    {
        SetWindowPos(m_hWnd, nullptr,
            m_restoreRect.left, m_restoreRect.top,
            m_restoreRect.right - m_restoreRect.left,
            m_restoreRect.bottom - m_restoreRect.top,
            SWP_NOZORDER | SWP_FRAMECHANGED);

        SetRoundedCorners(m_hWnd, true);
        HideEdgeStrips();
    }
}

// Covers the work area of the window's monitor exactly: the whole monitor but its visible taskbars. Along a taskbar
// that hides itself a pixel stays free: a popup window covering every pixel of its monitor counts as a fullscreen
// program, and Windows then never brings the taskbar back when the pointer pushes against that edge. That pixel is
// covered by a click-through strip in the window's colour, so no desktop shows there.
void TitleBar::FitMaximized()
{
    if (!m_maximized)
        return;
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    const RECT &m = mi.rcMonitor;
    RECT r = mi.rcWork;

    struct { UINT edge; LONG RECT::*side; int step; } edges[] = {
        { ABE_LEFT,   &RECT::left,   1 },
        { ABE_TOP,    &RECT::top,    1 },
        { ABE_RIGHT,  &RECT::right,  -1 },
        { ABE_BOTTOM, &RECT::bottom, -1 },
    };
    RECT strips[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        APPBARDATA bar = { sizeof(bar) };
        bar.uEdge = edges[i].edge;
        bar.rc = m;
        if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar))
            continue;
        r.*edges[i].side += edges[i].step;
        strips[i] = m;
        switch (edges[i].edge)
        {
        case ABE_LEFT:   strips[i].right = m.left + 1; break;
        case ABE_TOP:    strips[i].bottom = m.top + 1; break;
        case ABE_RIGHT:  strips[i].left = m.right - 1; break;
        default:         strips[i].top = m.bottom - 1; break;
        }
    }
    SetWindowPos(m_hWnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
        SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowEdgeStrips(strips);
}

// One window per edge that keeps a free pixel. Owned by the main window, so they stack with it and go with it when it
// minimizes or closes. Layered and transparent to the mouse: the pointer goes through them to the taskbar.
void TitleBar::ShowEdgeStrips(const RECT (&rects)[4])
{
    static const wchar_t *kStripClass = L"CorpusEdgeStrip";
    static bool registered = false;
    if (!registered)
    {
        auto channel = [](float c) { return static_cast<BYTE>(c * 255.0f + 0.5f); };
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = kStripClass;
        wc.hbrBackground = CreateSolidBrush(RGB(channel(theme::kBase.r), channel(theme::kBase.g), channel(theme::kBase.b)));
        RegisterClassW(&wc);
        registered = true;
    }
    // Minimized, the window hides its strips; WM_SIZE fits them again when it comes back.
    const bool minimized = IsIconic(m_hWnd) != FALSE;
    for (int i = 0; i < 4; ++i)
    {
        const RECT &r = rects[i];
        if (minimized)
            continue;
        if (IsRectEmpty(&r))
        {
            if (m_strips[i])
                ShowWindow(m_strips[i], SW_HIDE);
            continue;
        }
        if (!m_strips[i])
        {
            m_strips[i] = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                kStripClass, L"", WS_POPUP, 0, 0, 1, 1, m_hWnd, nullptr, GetModuleHandle(nullptr), nullptr);
            if (!m_strips[i])
                continue;
            SetLayeredWindowAttributes(m_strips[i], 0, 255, LWA_ALPHA);
        }
        SetWindowPos(m_strips[i], HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void TitleBar::HideEdgeStrips()
{
    for (HWND strip : m_strips)
        if (strip)
            ShowWindow(strip, SW_HIDE);
}
