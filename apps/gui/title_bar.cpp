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
#include <cstdlib>

namespace
{
    const int kButtonRadius = 6;
    const int kButtonSpacing = 20;
    const int kFirstButtonX = 20;  // centers, from the left edge and the top of the window
    const int kButtonY = 20;
    const int kButtonHitSlop = 2;  // the buttons are small, so clicks just outside still count
    const int kTitleX = 80;
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

void TitleBar::OnHover(POINT pt)
{
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
}

bool TitleBar::OnLButtonDown(POINT pt, bool doubleClick)
{
    if (!PtInRect(&m_bounds, pt))
        return false;

    RECT closeRect = ButtonRect(kClose);
    RECT minRect = ButtonRect(kMinimize);
    RECT maxRect = ButtonRect(kMaximize);

    if (PtInRect(&closeRect, pt))
        DestroyWindow(m_hWnd);
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
    if (!m_maximized)
    {
        GetWindowRect(m_hWnd, &m_restoreRect);

        HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(mon, &mi);

        SetWindowPos(m_hWnd, nullptr,
            mi.rcWork.left, mi.rcWork.top,
            mi.rcWork.right - mi.rcWork.left,
            mi.rcWork.bottom - mi.rcWork.top - 1,
            SWP_NOZORDER);

        SetRoundedCorners(m_hWnd, false);
    }
    else
    {
        SetWindowPos(m_hWnd, nullptr,
            m_restoreRect.left, m_restoreRect.top,
            m_restoreRect.right - m_restoreRect.left,
            m_restoreRect.bottom - m_restoreRect.top,
            SWP_NOZORDER);

        SetRoundedCorners(m_hWnd, true);
    }

    m_maximized = !m_maximized;
}
