#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "rename_sheet.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>

namespace
{
    const int kCardWidth = 440;
    const int kMargin = 24;        // least room between the card and the window's edges
    const int kPadding = 20;
    const int kTitleHeight = 24;
    const int kFieldGap = 12;
    const int kErrorRoom = 30;     // under the field, for the message
    const int kButtonHeight = 28;
    const int kButtonPadding = 16;
    const int kButtonGap = 8;
    const int kRadius = 6;
}

int RenameSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

RECT RenameSheet::CardRect() const
{
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin));
    int height = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kFieldGap) + Scale(theme::kControlHeight) +
        Scale(kErrorRoom) + Scale(kButtonHeight);
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int top = std::max<int>(m_bounds.top, (m_bounds.top + m_bounds.bottom - height) / 2 - Scale(40));
    return { left, top, left + width, top + height };
}

RECT RenameSheet::FieldRect() const
{
    RECT card = CardRect();
    int top = card.top + Scale(kPadding) + Scale(kTitleHeight) + Scale(kFieldGap);
    return { card.left + Scale(kPadding), top, card.right - Scale(kPadding), top + Scale(theme::kControlHeight) };
}

RECT RenameSheet::ConfirmRect() const
{
    RECT card = CardRect();
    int width = static_cast<int>(m_renderer->TextWidth(strings::kRenameConfirm, Font::kBody) + 0.99f) + 2 * Scale(kButtonPadding);
    int bottom = card.bottom - Scale(kPadding);
    return { card.right - Scale(kPadding) - width, bottom - Scale(kButtonHeight), card.right - Scale(kPadding), bottom };
}

RECT RenameSheet::CancelRect() const
{
    RECT confirm = ConfirmRect();
    int width = static_cast<int>(m_renderer->TextWidth(strings::kCancel, Font::kBody) + 0.99f) + 2 * Scale(kButtonPadding);
    return { confirm.left - Scale(kButtonGap) - width, confirm.top, confirm.left - Scale(kButtonGap), confirm.bottom };
}

void RenameSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::wstring &name)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_open = true;
    m_hover = -1;
    m_field.SetError(L"");
    m_field.SetText(name);
    m_field.SetBounds(m_hWnd, renderer, FieldRect());
    m_field.Focus();
    // Like Explorer: the name is selected without its extension.
    size_t dot = name.rfind(L'.');
    m_field.Select(0, dot == std::wstring::npos || dot == 0 ? name.size() : dot);
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void RenameSheet::Close()
{
    if (!m_open)
        return;
    m_field.Blur();
    m_open = false;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void RenameSheet::SetBounds(const RECT &bounds)
{
    m_bounds = bounds;
    if (m_open)
    {
        m_field.OnDpiChanged();
        m_field.SetBounds(m_hWnd, *m_renderer, FieldRect());
    }
}

void RenameSheet::OnHover(POINT pt)
{
    if (!m_open)
        return;
    RECT cancel = CancelRect(), confirm = ConfirmRect();
    int hover = PtInRect(&cancel, pt) ? 0 : PtInRect(&confirm, pt) ? 1 : -1;
    if (hover != m_hover)
    {
        m_hover = hover;
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
    }
}

RenameSheet::Action RenameSheet::OnLButtonDown(POINT pt, bool doubleClick)
{
    RECT card = CardRect(), confirm = ConfirmRect(), cancel = CancelRect();
    if (PtInRect(&confirm, pt))
        return Action::kConfirm;
    if (PtInRect(&cancel, pt) || !PtInRect(&card, pt))
        return Action::kCancel;
    m_field.OnLButtonDown(pt, doubleClick);
    m_field.Focus();  // the field is the only thing to type in: a click elsewhere on the card keeps it
    return Action::kNone;
}

RenameSheet::Action RenameSheet::OnKeyDown(WPARAM key, bool ctrl)
{
    if (key == VK_ESCAPE)
        return Action::kCancel;
    if (key == VK_RETURN)
        return Action::kConfirm;
    if (m_field.OnKeyDown(key, ctrl))
        m_field.SetError(L"");
    return Action::kNone;
}

void RenameSheet::OnChar(wchar_t ch)
{
    if (m_field.OnChar(ch))
        m_field.SetError(L"");
}

void RenameSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    renderer.FillRect(ToRectF(m_bounds), theme::Rgb(0x000000, 0.55f));

    RECT card = CardRect();
    D2D1_RECT_F body = ToRectF(card);
    float radius = static_cast<float>(Scale(kRadius));
    renderer.FillRoundedRect({ body.left - 1, body.top + 2, body.right + 1, body.bottom + 6 }, radius + 2, theme::Rgb(0x000000, 0.20f));
    renderer.FillRoundedRect(body, radius, theme::kBase);
    renderer.DrawRoundedRect(body, radius, theme::White(0.10f), 1.0f);

    RECT title = { card.left + Scale(kPadding), card.top + Scale(kPadding), card.right - Scale(kPadding),
        card.top + Scale(kPadding) + Scale(kTitleHeight) };
    renderer.DrawTextLine(strings::kRenameTitle, Font::kBodyStrong, title, theme::kText1);

    m_field.Paint(renderer);

    const float buttonRadius = static_cast<float>(Scale(theme::kChipRadius));
    RECT cancel = CancelRect(), confirm = ConfirmRect();
    D2D1_RECT_F cancelBox = ToRectF(cancel);
    renderer.FillRoundedRect(cancelBox, buttonRadius, theme::kControl);
    if (m_hover == 0)
        renderer.FillRoundedRect(cancelBox, buttonRadius, theme::kHover);
    renderer.DrawTextLine(strings::kCancel, Font::kBody, cancel, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
    renderer.FillRoundedRect(ToRectF(confirm), buttonRadius, m_hover == 1 ? Mix(theme::kYuri, theme::White(1.0f), 0.12f) : theme::kYuri);
    renderer.DrawTextLine(strings::kRenameConfirm, Font::kBody, confirm, theme::kOnAccent, DWRITE_TEXT_ALIGNMENT_CENTER);
}
