#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tooltip.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include <algorithm>

namespace
{
    const UINT kDelayMs = 500;
    const int kPadding = 8;
    const int kHeight = 24;
    const int kGap = 6;     // between the control and the label
    const int kEdge = 4;    // the label keeps this far from the window's edges
}

void Tooltip::Invalidate() const
{
    if (m_hWnd)
    {
        RECT all;
        GetClientRect(m_hWnd, &all);
        InvalidateRect(m_hWnd, &all, FALSE);
    }
}

void Tooltip::Update(HWND hWnd, const wchar_t *text, const RECT &anchor)
{
    m_hWnd = hWnd;
    const bool same = text ? (m_hasText && m_text == text) : !m_hasText;
    if (same && (!text || EqualRect(&anchor, &m_anchor)))
        return;

    bool wasShown = m_shown;
    KillTimer(hWnd, kTooltipTimer);
    m_shown = false;
    m_dismissed = false;
    m_hasText = text != nullptr;
    m_text = text ? text : L"";
    m_anchor = anchor;
    if (text)
        SetTimer(hWnd, kTooltipTimer, kDelayMs, nullptr);
    if (wasShown)
        Invalidate();
}

void Tooltip::Dismiss(HWND hWnd)
{
    m_hWnd = hWnd;
    KillTimer(hWnd, kTooltipTimer);
    m_dismissed = true;
    if (m_shown)
    {
        m_shown = false;
        Invalidate();
    }
}

void Tooltip::OnTimer(HWND hWnd)
{
    KillTimer(hWnd, kTooltipTimer);
    if (!m_hasText || m_dismissed)
        return;
    m_shown = true;
    Invalidate();
}

void Tooltip::Paint(Renderer &renderer) const
{
    if (!m_shown || !m_hasText)
        return;

    RECT client;
    GetClientRect(m_hWnd, &client);
    const int pad = ScaleForWindow(m_hWnd, kPadding);
    const int height = ScaleForWindow(m_hWnd, kHeight);
    const int gap = ScaleForWindow(m_hWnd, kGap);
    const int edge = ScaleForWindow(m_hWnd, kEdge);
    int width = static_cast<int>(renderer.TextWidth(m_text.c_str(), Font::kCaption) + 0.99f) + 2 * pad;

    // Under the control, centered on it, or above it when the window ends there.
    int left = (m_anchor.left + m_anchor.right - width) / 2;
    left = std::max(edge, std::min(left, static_cast<int>(client.right) - width - edge));
    int top = m_anchor.bottom + gap;
    if (top + height > client.bottom - edge)
        top = m_anchor.top - gap - height;

    RECT box = { left, top, left + width, top + height };
    const float radius = static_cast<float>(ScaleForWindow(m_hWnd, theme::kChipRadius));
    renderer.FillRoundedRect(ToRectF(box), radius, theme::kChip);
    renderer.DrawRoundedRect(ToRectF(box), radius, theme::kStroke, 1.0f);
    RECT label = { box.left + pad, box.top, box.right - pad, box.bottom };
    renderer.DrawTextLine(m_text.c_str(), Font::kCaption, label, theme::kText1);
}
