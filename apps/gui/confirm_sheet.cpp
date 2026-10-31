#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "confirm_sheet.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    const int kCardWidth = 460;
    const int kMargin = 24;        // least room between the card and the window's edges
    const int kPadding = 20;
    const int kTitleHeight = 24;
    const int kBodyGap = 6;        // between the question and what it means
    const int kBodyLines = 4;
    const int kDetailGap = 10;     // above the details
    const int kDetailHeight = 20;
    const int kButtonsGap = 20;    // above the buttons
    const int kFocusInset = 2;
}

int ConfirmSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

RECT ConfirmSheet::CardRect() const
{
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin));
    int height = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kButtonsGap) + Scale(kButtonHeight);
    if (m_bodyHeight > 0)
        height += Scale(kBodyGap) + m_bodyHeight;
    if (!m_question.details.empty())
        height += Scale(kDetailGap) + static_cast<int>(m_question.details.size()) * Scale(kDetailHeight);
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int top = std::max<int>(m_bounds.top, (m_bounds.top + m_bounds.bottom - height) / 2 - Scale(40));
    return { left, top, left + width, top + height };
}

RECT ConfirmSheet::ConfirmRect() const
{
    RECT card = CardRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, m_question.confirm.c_str());
    int bottom = card.bottom - Scale(kPadding);
    return { card.right - Scale(kPadding) - width, bottom - Scale(kButtonHeight), card.right - Scale(kPadding), bottom };
}

RECT ConfirmSheet::AlternateRect() const
{
    RECT confirm = ConfirmRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, m_question.alternate.c_str());
    return { confirm.left - Scale(kButtonGap) - width, confirm.top, confirm.left - Scale(kButtonGap), confirm.bottom };
}

RECT ConfirmSheet::CancelRect() const
{
    RECT next = m_question.alternate.empty() ? ConfirmRect() : AlternateRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kCancel);
    return { next.left - Scale(kButtonGap) - width, next.top, next.left - Scale(kButtonGap), next.bottom };
}

// Lays the body out at the width the card has now: its height sets the card's.
void ConfirmSheet::Measure()
{
    m_body.Reset();
    m_bodyHeight = 0;
    if (m_question.body.empty())
        return;
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin)) - 2 * Scale(kPadding);
    m_body = m_renderer->Layout(m_question.body, Font::kBody, static_cast<float>(std::max(width, 1)), kBodyLines);
    DWRITE_TEXT_METRICS metrics = {};
    if (m_body && SUCCEEDED(m_body->GetMetrics(&metrics)))
        m_bodyHeight = static_cast<int>(std::ceil(metrics.height));
    else
        m_body.Reset();
}

void ConfirmSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, Question question)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_question = std::move(question);
    m_open = true;
    m_hover = -1;
    m_focus = m_question.danger ? 0 : 1;
    Measure();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void ConfirmSheet::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_body.Reset();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void ConfirmSheet::SetBounds(const RECT &bounds)
{
    m_bounds = bounds;
    if (m_open)
        Measure();
}

void ConfirmSheet::OnHover(POINT pt)
{
    if (!m_open)
        return;
    RECT cancel = CancelRect(), confirm = ConfirmRect(), alternate = AlternateRect();
    int hover = PtInRect(&cancel, pt) ? 0 : PtInRect(&confirm, pt) ? 1 : !m_question.alternate.empty() && PtInRect(&alternate, pt) ? 2 : -1;
    if (hover != m_hover)
    {
        m_hover = hover;
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
    }
}

ConfirmSheet::Action ConfirmSheet::OnLButtonDown(POINT pt, bool)
{
    RECT card = CardRect(), confirm = ConfirmRect(), cancel = CancelRect();
    if (PtInRect(&confirm, pt))
        return Action::kConfirm;
    RECT alternate = AlternateRect();
    if (!m_question.alternate.empty() && PtInRect(&alternate, pt))
        return Action::kAlternate;
    if (PtInRect(&cancel, pt) || !PtInRect(&card, pt))
        return Action::kCancel;
    return Action::kNone;
}

ConfirmSheet::Action ConfirmSheet::OnKeyDown(WPARAM key, bool)
{
    if (key == VK_ESCAPE)
        return Action::kCancel;
    if (key == VK_RETURN || key == VK_SPACE)
        return m_focus == 1 ? Action::kConfirm : m_focus == 2 ? Action::kAlternate : Action::kCancel;
    if (key == VK_TAB || key == VK_LEFT || key == VK_RIGHT)
    {
        // The buttons left to right: Cancel (0), the alternate (2) if there is one, the main one (1).
        std::vector<int> order = m_question.alternate.empty() ? std::vector<int>{ 0, 1 } : std::vector<int>{ 0, 2, 1 };
        int at = static_cast<int>(std::find(order.begin(), order.end(), m_focus) - order.begin());
        int count = static_cast<int>(order.size());
        at = key == VK_LEFT ? std::max(at - 1, 0) : key == VK_RIGHT ? std::min(at + 1, count - 1) : (at + 1) % count;
        int focus = order[at];
        if (focus != m_focus)
        {
            m_focus = focus;
            InvalidateRect(m_hWnd, &m_bounds, FALSE);
        }
    }
    return Action::kNone;
}

void ConfirmSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    PaintBackdrop(renderer, m_bounds);

    RECT card = CardRect();
    PaintCard(renderer, m_hWnd, card);

    const int padding = Scale(kPadding);
    int y = card.top + padding;
    RECT title = { card.left + padding, y, card.right - padding, y + Scale(kTitleHeight) };
    renderer.DrawTextLine(m_question.title, Font::kBodyStrong, title, theme::kText1);
    y = title.bottom;

    if (m_body)
    {
        y += Scale(kBodyGap);
        renderer.DrawLayout(m_body.Get(), static_cast<float>(card.left + padding), static_cast<float>(y), theme::kText2);
        y += m_bodyHeight;
    }

    if (!m_question.details.empty())
    {
        y += Scale(kDetailGap);
        for (const std::wstring &detail : m_question.details)
        {
            RECT line = { card.left + padding, y, card.right - padding, y + Scale(kDetailHeight) };
            renderer.DrawTextLine(detail, Font::kMono, line, theme::kText3);
            y = line.bottom;
        }
    }

    RECT cancel = CancelRect(), confirm = ConfirmRect();
    PaintButton(renderer, m_hWnd, cancel, strings::kCancel, false, m_hover == 0);
    RECT alternate = AlternateRect();
    if (!m_question.alternate.empty())
        PaintButton(renderer, m_hWnd, alternate, m_question.alternate.c_str(), false, m_hover == 2);
    if (m_question.danger)
    {
        // A change that can't be taken back: rose, the color of what removes.
        const float radius = static_cast<float>(Scale(theme::kChipRadius));
        renderer.FillRoundedRect(ToRectF(confirm), radius, m_hover == 1 ? Mix(theme::kRose, theme::White(1.0f), 0.12f) : theme::kRose);
        renderer.DrawTextLine(m_question.confirm, Font::kBody, confirm, theme::kOnAccent, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    else
        PaintButton(renderer, m_hWnd, confirm, m_question.confirm.c_str(), true, m_hover == 1);

    RECT focused = m_focus == 1 ? confirm : m_focus == 2 ? alternate : cancel;
    const float inset = static_cast<float>(Scale(kFocusInset));
    D2D1_RECT_F ring = ToRectF(focused);
    ring = { ring.left - inset, ring.top - inset, ring.right + inset, ring.bottom + inset };
    renderer.DrawRoundedRect(ring, static_cast<float>(Scale(theme::kChipRadius)) + inset, theme::kYuri, 1.5f);
}
