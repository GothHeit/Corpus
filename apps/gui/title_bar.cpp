#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "title_bar.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <cstdlib>

namespace
{
    const int kButtonRadius = 6;
    const int kButtonSpacing = 20;
    const int kFirstButtonX = 12;

    void FillCircle(Gdiplus::Graphics &graphics, const RECT &rect, COLORREF color)
    {
        Gdiplus::SolidBrush brush(ToGdiplusColor(color));
        graphics.FillEllipse(&brush,
            static_cast<int>(rect.left), static_cast<int>(rect.top),
            static_cast<int>(rect.right - rect.left), static_cast<int>(rect.bottom - rect.top));
    }
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

RECT TitleBar::ButtonRect(int index) const
{
    const int radius = Scale(kButtonRadius);
    int centerX = m_bounds.left + Scale(kFirstButtonX) + index * Scale(kButtonSpacing);
    int centerY = (m_bounds.top + m_bounds.bottom) / 2;

    RECT r = { centerX - radius, centerY - radius, centerX + radius, centerY + radius };
    return r;
}

void TitleBar::Paint(HDC dc) const
{
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    FillCircle(graphics, ButtonRect(kClose), theme::kCloseButton);
    FillCircle(graphics, ButtonRect(kMinimize), theme::kMinimizeButton);
    FillCircle(graphics, ButtonRect(kMaximize), theme::kMaximizeButton);
}

bool TitleBar::OnLButtonDown(POINT pt)
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
