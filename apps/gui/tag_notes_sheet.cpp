#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tag_notes_sheet.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>

namespace
{
    const int kCardWidth = 480;
    const int kMargin = 24;        // least room between the card and the window's edges
    const int kPadding = 20;
    const int kTitleHeight = 24;
    const int kRowGap = 8;
    const int kLabelWidth = 76;    // the column of labels left of the fields
    const int kTitleGap = 12;
    const int kButtonsGap = 16;

    const wchar_t *const kLabels[] = { strings::kNotesNote, strings::kNotesCreator, strings::kNotesYear,
        strings::kNotesLink, strings::kNotesTaken, strings::kNotesRefused };

    // A line break shows as " / " in a one-line field.
    std::wstring Flatten(const std::string &text)
    {
        std::wstring wide = Utf8ToWide(text);
        std::wstring out;
        for (size_t i = 0; i < wide.size(); i++)
        {
            wchar_t ch = wide[i];
            if (ch == L'\r')
                continue;
            if (ch == L'\n')
            {
                // A blank line between two paragraphs is one separator.
                while (i + 1 < wide.size() && (wide[i + 1] == L'\n' || wide[i + 1] == L'\r'))
                    i++;
                out += L" / ";
            }
            else
                out += ch;
        }
        return out;
    }
}

TagNotesSheet::TagNotesSheet()
    : m_fields{ TextField(L""), TextField(L""), TextField(L""), TextField(L""), TextField(L""), TextField(L"") }
{
    for (TextField &field : m_fields)
        field.EnableDrag();
}

int TagNotesSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

RECT TagNotesSheet::CardRect() const
{
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin));
    int rows = kRows * Scale(theme::kControlHeight) + (kRows - 1) * Scale(kRowGap);
    int height = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kTitleGap) + rows + Scale(kButtonsGap) + Scale(kButtonHeight);
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int top = std::max<int>(m_bounds.top, (m_bounds.top + m_bounds.bottom - height) / 2 - Scale(24));
    return { left, top, left + width, top + height };
}

RECT TagNotesSheet::FieldRect(int row) const
{
    RECT card = CardRect();
    int top = card.top + Scale(kPadding) + Scale(kTitleHeight) + Scale(kTitleGap) +
        row * (Scale(theme::kControlHeight) + Scale(kRowGap));
    return { card.left + Scale(kPadding) + Scale(kLabelWidth), top, card.right - Scale(kPadding),
        top + Scale(theme::kControlHeight) };
}

RECT TagNotesSheet::ConfirmRect() const
{
    RECT card = CardRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kNotesSave);
    int bottom = card.bottom - Scale(kPadding);
    return { card.right - Scale(kPadding) - width, bottom - Scale(kButtonHeight), card.right - Scale(kPadding), bottom };
}

RECT TagNotesSheet::CancelRect() const
{
    RECT confirm = ConfirmRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kCancel);
    return { confirm.left - Scale(kButtonGap) - width, confirm.top, confirm.left - Scale(kButtonGap), confirm.bottom };
}

void TagNotesSheet::Layout()
{
    for (int i = 0; i < kRows; i++)
        m_fields[static_cast<size_t>(i)].SetBounds(m_hWnd, *m_renderer, FieldRect(i));
}

void TagNotesSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::string &name, const tag_attrs &attrs)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_open = true;
    m_hover = -1;
    m_focus = -1;
    m_tag = name;
    m_title = Utf8ToWide(name);
    const std::string *values[kRows] = { &attrs.note, &attrs.creator, &attrs.year, &attrs.url, &attrs.taken, &attrs.refused };
    for (int i = 0; i < kRows; i++)
    {
        size_t at = static_cast<size_t>(i);
        m_original[at] = *values[i];
        m_shown[at] = Flatten(*values[i]);
        m_fields[at].SetText(m_shown[at]);
    }
    Layout();
    FocusRow(0);
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void TagNotesSheet::Close()
{
    if (!m_open)
        return;
    for (TextField &field : m_fields)
        field.Blur();
    m_focus = -1;
    m_open = false;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void TagNotesSheet::SetBounds(const RECT &bounds)
{
    m_bounds = bounds;
    if (m_open)
    {
        for (TextField &field : m_fields)
            field.OnDpiChanged();
        Layout();
    }
}

void TagNotesSheet::FocusRow(int row)
{
    row = (row % kRows + kRows) % kRows;
    for (int i = 0; i < kRows; i++)
    {
        if (i != row)
            m_fields[static_cast<size_t>(i)].Blur();
    }
    m_fields[static_cast<size_t>(row)].Focus();
    m_focus = row;
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

tag_attrs TagNotesSheet::Attrs() const
{
    std::string text[kRows];
    for (size_t i = 0; i < kRows; i++)
    {
        // An untouched field keeps its line breaks; an edited one is what is typed.
        text[i] = m_fields[i].Query() == m_shown[i] ? m_original[i] : WideToUtf8(m_fields[i].Query());
    }
    tag_attrs out;
    out.note = text[0];
    out.creator = text[1];
    out.year = text[2];
    out.url = text[3];
    out.taken = text[4];
    out.refused = text[5];
    return out;
}

void TagNotesSheet::OnHover(POINT pt)
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

void TagNotesSheet::OnMouseMove(POINT pt)
{
    for (TextField &field : m_fields)
        field.OnMouseMove(pt);
}

void TagNotesSheet::OnLButtonUp()
{
    for (TextField &field : m_fields)
        field.OnLButtonUp();
}

void TagNotesSheet::OnCaptureChanged()
{
    for (TextField &field : m_fields)
        field.OnCaptureChanged();
}

TagNotesSheet::Action TagNotesSheet::OnLButtonDown(POINT pt, bool doubleClick)
{
    RECT card = CardRect(), confirm = ConfirmRect(), cancel = CancelRect();
    if (PtInRect(&confirm, pt))
        return Action::kConfirm;
    if (PtInRect(&cancel, pt) || !PtInRect(&card, pt))
        return Action::kCancel;
    for (int i = 0; i < kRows; i++)
    {
        RECT field = FieldRect(i);
        if (PtInRect(&field, pt))
        {
            // The others drop focus first, so only one has a caret.
            for (int j = 0; j < kRows; j++)
            {
                if (j != i)
                    m_fields[static_cast<size_t>(j)].Blur();
            }
            m_fields[static_cast<size_t>(i)].OnLButtonDown(pt, doubleClick);
            m_focus = i;
            InvalidateRect(m_hWnd, &m_bounds, FALSE);
            return Action::kNone;
        }
    }
    return Action::kNone;  // elsewhere on the card: focus stays
}

TagNotesSheet::Action TagNotesSheet::OnKeyDown(WPARAM key, bool ctrl)
{
    if (key == VK_ESCAPE)
        return Action::kCancel;
    if (key == VK_RETURN)
        return Action::kConfirm;
    int row = std::max(m_focus, 0);
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == VK_TAB)
    {
        FocusRow(row + (shift ? -1 : 1));
        return Action::kNone;
    }
    if (key == VK_DOWN || key == VK_UP)
    {
        FocusRow(row + (key == VK_DOWN ? 1 : -1));
        return Action::kNone;
    }
    m_fields[static_cast<size_t>(row)].OnKeyDown(key, ctrl);
    return Action::kNone;
}

void TagNotesSheet::OnChar(wchar_t ch)
{
    if (ch == L'\t' || ch == L'\r' || ch == L'\n')
        return;
    m_fields[static_cast<size_t>(std::max(m_focus, 0))].OnChar(ch);
}

void TagNotesSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    PaintBackdrop(renderer, m_bounds);

    RECT card = CardRect();
    PaintCard(renderer, m_hWnd, card);

    // "Notes" in the title weight, then the tag's name quieter after it.
    RECT title = { card.left + Scale(kPadding), card.top + Scale(kPadding), card.right - Scale(kPadding),
        card.top + Scale(kPadding) + Scale(kTitleHeight) };
    renderer.DrawTextLine(strings::kNotesTitle, Font::kBodyStrong, title, theme::kText1);
    RECT name = title;
    name.left += static_cast<LONG>(renderer.TextWidth(strings::kNotesTitle, Font::kBodyStrong)) + Scale(12);
    renderer.DrawTextLine(m_title, Font::kBody, name, theme::kText3);

    for (int i = 0; i < kRows; i++)
    {
        RECT field = FieldRect(i);
        RECT label = { card.left + Scale(kPadding), field.top, field.left - Scale(8), field.bottom };
        renderer.DrawTextLine(kLabels[i], Font::kCaption, label, theme::kText3);
        m_fields[static_cast<size_t>(i)].Paint(renderer);
    }

    PaintButton(renderer, m_hWnd, CancelRect(), strings::kCancel, false, m_hover == 0);
    PaintButton(renderer, m_hWnd, ConfirmRect(), strings::kNotesSave, true, m_hover == 1);
}
