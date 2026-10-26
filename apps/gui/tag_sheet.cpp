#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tag_sheet.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/tag.hpp"
#include <algorithm>

namespace
{
    const int kCardWidth = 440;
    const int kMargin = 24;        // least room between the card and the window's edges
    const int kPadding = 20;
    const int kTitleHeight = 24;
    const int kSubtitleHeight = 18;
    const int kFieldGap = 8;
    const int kErrorRoom = 26;     // under the field, for the message
    const int kHeaderHeight = 22;  // the "Matching tags" label
    const int kRowHeight = 26;
    const int kRowInset = 8;
    const int kButtonGap = 16;     // between the rows and the buttons
    const int kRowRadius = 3;

    bool StartsWithNoCase(const std::wstring &s, const std::wstring &prefix)
    {
        return s.size() >= prefix.size() &&
            CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    }

    bool ContainsNoCase(const std::wstring &s, const std::wstring &part)
    {
        return FindNLSStringEx(LOCALE_NAME_INVARIANT, FIND_FROMSTART | LINGUISTIC_IGNORECASE, s.c_str(), static_cast<int>(s.size()),
            part.c_str(), static_cast<int>(part.size()), nullptr, nullptr, nullptr, 0) >= 0;
    }

    // Characters the search can't take in a tag name: braces delimit a query, parentheses group it.
    bool IsReserved(wchar_t ch)
    {
        return ch == L'{' || ch == L'}' || ch == L'(' || ch == L')';
    }

    // Words the query language reads as operators, so a tag with one of these names couldn't be searched.
    bool IsOperator(const std::wstring &word)
    {
        return word == L"AND" || word == L"OR" || word == L"NOT";
    }
}

int TagSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

RECT TagSheet::CardRect() const
{
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin));
    int height = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kSubtitleHeight) + Scale(kFieldGap) + Scale(theme::kControlHeight) +
        Scale(kErrorRoom) + Scale(kHeaderHeight) + kRows * Scale(kRowHeight) + Scale(kButtonGap) + Scale(kButtonHeight);
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int top = std::max<int>(m_bounds.top, (m_bounds.top + m_bounds.bottom - height) / 2 - Scale(40));
    return { left, top, left + width, top + height };
}

RECT TagSheet::FieldRect() const
{
    RECT card = CardRect();
    int top = card.top + Scale(kPadding) + Scale(kTitleHeight) + Scale(kSubtitleHeight) + Scale(kFieldGap);
    return { card.left + Scale(kPadding), top, card.right - Scale(kPadding), top + Scale(theme::kControlHeight) };
}

RECT TagSheet::RowRect(int index) const
{
    RECT card = CardRect();
    RECT field = FieldRect();
    int top = field.bottom + Scale(kErrorRoom) + Scale(kHeaderHeight) + index * Scale(kRowHeight);
    return { card.left + Scale(kPadding) - Scale(kRowInset) / 2, top, card.right - Scale(kPadding) + Scale(kRowInset) / 2, top + Scale(kRowHeight) };
}

RECT TagSheet::ConfirmRect() const
{
    RECT card = CardRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kTagSheetConfirm);
    int bottom = card.bottom - Scale(kPadding);
    return { card.right - Scale(kPadding) - width, bottom - Scale(kButtonHeight), card.right - Scale(kPadding), bottom };
}

RECT TagSheet::CancelRect() const
{
    RECT confirm = ConfirmRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kCancel);
    return { confirm.left - Scale(kButtonGap / 2) - width, confirm.top, confirm.left - Scale(kButtonGap / 2), confirm.bottom };
}

int TagSheet::RowAt(POINT pt) const
{
    for (int i = 0; i < static_cast<int>(m_rows.size()); i++)
    {
        RECT r = RowRect(i);
        if (PtInRect(&r, pt))
            return i;
    }
    return -1;
}

void TagSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::vector<file*> &files)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_open = true;
    m_hover = -1;
    m_onEveryFile.clear();
    if (!files.empty())
    {
        for (const tag *t : files[0]->get_tags())
        {
            if (std::all_of(files.begin(), files.end(), [t](const file *f) { return f->tagged(t); }))
                m_onEveryFile.insert(t);
        }
    }
    m_subtitle = files.size() == 1 ? strings::kOneFile : FormatCount(files.size()) + L" " + strings::kFiles;
    m_field.SetPlaceholder(strings::kTagPlaceholder);
    m_field.SetError(L"");
    m_field.SetText(L"");
    m_field.SetBounds(m_hWnd, renderer, FieldRect());
    m_field.Focus();
    UpdateRows();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void TagSheet::Close()
{
    if (!m_open)
        return;
    m_field.Blur();
    m_open = false;
    m_onEveryFile.clear();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void TagSheet::SetBounds(const RECT &bounds)
{
    m_bounds = bounds;
    if (m_open)
    {
        m_field.OnDpiChanged();
        m_field.SetBounds(m_hWnd, *m_renderer, FieldRect());
    }
}

// ---- Autocomplete ----

// The word being typed: the text after the last space, while the cursor is at the end.
std::wstring TagSheet::CurrentWord() const
{
    if (!m_field.CursorAtEnd())
        return std::wstring();
    const std::wstring &text = m_field.Query();
    size_t space = text.find_last_of(L' ');
    return space == std::wstring::npos ? text : text.substr(space + 1);
}

void TagSheet::UpdateRows()
{
    m_rows.clear();
    std::wstring word = CurrentWord();
    m_matching = !word.empty();

    struct Match
    {
        Row row;
        bool prefix;
    };
    std::vector<Match> found;
    for (const tag *t : m_lib.get_tags())
    {
        std::wstring name = Utf8ToWide(t->get_id());
        // Tags every selected file already has would change nothing.
        if (m_onEveryFile.count(t))
            continue;
        bool prefix = StartsWithNoCase(name, word);
        if (word.empty() || prefix || ContainsNoCase(name, word))
            found.push_back({ { name, t->get_files().size() }, prefix });
    }

    std::sort(found.begin(), found.end(), [](const Match &a, const Match &b)
    {
        if (a.prefix != b.prefix)
            return a.prefix;
        if (a.row.files != b.row.files)
            return a.row.files > b.row.files;
        return LessNoCase(a.row.name, b.row.name);
    });
    for (size_t i = 0; i < found.size() && i < static_cast<size_t>(kRows); i++)
        m_rows.push_back(found[i].row);

    // A word being typed starts with its best match picked, so Tab completes it; before one, nothing is.
    m_pick = m_matching && !m_rows.empty() ? 0 : -1;
    m_hover = -1;

    // The grey rest of the word, only where the row really begins with it.
    std::wstring rest;
    if (m_pick >= 0 && m_rows[m_pick].name.size() > word.size() && StartsWithNoCase(m_rows[m_pick].name, word))
        rest = m_rows[m_pick].name.substr(word.size());
    m_field.SetSuggestion(rest);
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

// Replaces the word being typed with the tag, and starts a new word.
void TagSheet::AcceptRow(size_t index)
{
    if (index >= m_rows.size())
        return;
    std::wstring text = m_field.Query();
    std::wstring word = CurrentWord();
    // With the cursor inside the text there is no word to replace: the tag starts a word of its own.
    if (word.empty() && !text.empty() && text.back() != L' ')
        text += L' ';
    text.replace(text.size() - word.size(), word.size(), m_rows[index].name + L" ");
    m_field.SetText(text);
    m_field.Focus();
    Changed();
}

void TagSheet::Changed()
{
    m_field.SetError(L"");
    UpdateRows();
}

// The tags typed in the field. A word that differs from an existing tag only in case means that tag, so
// "anime" doesn't create a second tag next to "Anime".
std::vector<std::string> TagSheet::Tags() const
{
    std::vector<std::string> tags;
    const std::wstring &text = m_field.Query();
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L' ', start);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring word = text.substr(start, end - start);
        start = end + 1;

        // A leading '-' negates in a search, so it can't start a tag name.
        word.erase(0, word.find_first_not_of(L'-'));
        if (word.empty() || IsOperator(word))
            continue;

        // Pasted text can hold what typing refuses (a(b)): such a word is cleaned up the way folder names are.
        std::string name = WideToUtf8(word);
        if (!is_valid_tag_name(name))
            name = normalize_tag_name(name);
        if (name.empty())
            continue;
        if (const tag *t = m_lib.find_tag(name))
            name = t->get_id();
        if (std::find(tags.begin(), tags.end(), name) == tags.end())
            tags.push_back(name);
    }
    return tags;
}

// ---- Input ----

void TagSheet::OnHover(POINT pt)
{
    if (!m_open)
        return;
    RECT cancel = CancelRect(), confirm = ConfirmRect();
    int hover = PtInRect(&cancel, pt) ? kRows : PtInRect(&confirm, pt) ? kRows + 1 : RowAt(pt);
    if (hover != m_hover)
    {
        m_hover = hover;
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
    }
}

TagSheet::Action TagSheet::OnLButtonDown(POINT pt, bool doubleClick)
{
    RECT card = CardRect(), confirm = ConfirmRect(), cancel = CancelRect();
    if (PtInRect(&confirm, pt))
        return Confirm();
    if (PtInRect(&cancel, pt) || !PtInRect(&card, pt))
        return Action::kCancel;
    int row = RowAt(pt);
    if (row >= 0)
    {
        AcceptRow(static_cast<size_t>(row));
        return Action::kNone;
    }
    RECT field = FieldRect();
    if (PtInRect(&field, pt))
    {
        m_field.OnLButtonDown(pt, doubleClick);
        UpdateRows();
    }
    m_field.Focus();  // the field is the only thing to type in: a click elsewhere on the card keeps it
    return Action::kNone;
}

// Enter and "Add": nothing usable typed says so and keeps the sheet.
TagSheet::Action TagSheet::Confirm()
{
    if (Tags().empty())
    {
        m_field.SetError(strings::kTagSheetEmpty);
        return Action::kNone;
    }
    return Action::kConfirm;
}

TagSheet::Action TagSheet::OnKeyDown(WPARAM key, bool ctrl)
{
    switch (key)
    {
    case VK_ESCAPE:
        return Action::kCancel;
    case VK_RETURN:
        return Confirm();
    case VK_TAB:
        if (m_pick >= 0)
            AcceptRow(static_cast<size_t>(m_pick));
        return Action::kNone;
    case VK_UP:
    case VK_DOWN:
        if (!m_rows.empty())
        {
            int count = static_cast<int>(m_rows.size());
            m_pick = m_pick < 0 ? (key == VK_DOWN ? 0 : count - 1) : (m_pick + (key == VK_DOWN ? 1 : count - 1)) % count;
            std::wstring word = CurrentWord();
            std::wstring rest;
            if (m_rows[m_pick].name.size() > word.size() && StartsWithNoCase(m_rows[m_pick].name, word))
                rest = m_rows[m_pick].name.substr(word.size());
            m_field.SetSuggestion(rest);
            InvalidateRect(m_hWnd, &m_bounds, FALSE);
        }
        return Action::kNone;
    }

    if (m_field.OnKeyDown(key, ctrl))
        Changed();
    else
        UpdateRows();  // moving the cursor changes the word being completed
    return Action::kNone;
}

void TagSheet::OnChar(wchar_t ch)
{
    const std::wstring &text = m_field.Query();
    bool wordStart = m_field.CursorAtEnd() ? (text.empty() || text.back() == L' ') : false;
    // Said under the field rather than with a beep: Corpus makes no sounds.
    if (IsReserved(ch) || (ch == L'-' && wordStart))
    {
        m_field.SetError(strings::kTagCharRefused);
        return;
    }
    if (m_field.OnChar(ch))
        Changed();
}

// ---- Painting ----

void TagSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    PaintBackdrop(renderer, m_bounds);
    RECT card = CardRect();
    PaintCard(renderer, m_hWnd, card);

    RECT title = { card.left + Scale(kPadding), card.top + Scale(kPadding), card.right - Scale(kPadding),
        card.top + Scale(kPadding) + Scale(kTitleHeight) };
    renderer.DrawTextLine(strings::kTagSheetTitle, Font::kBodyStrong, title, theme::kText1);
    RECT subtitle = { title.left, title.bottom, title.right, title.bottom + Scale(kSubtitleHeight) };
    renderer.DrawTextLine(m_subtitle, Font::kCaption, subtitle, theme::kText3);

    m_field.Paint(renderer);

    if (!m_rows.empty())
    {
        RECT field = FieldRect();
        RECT header = { field.left, field.bottom + Scale(kErrorRoom), field.right, field.bottom + Scale(kErrorRoom) + Scale(kHeaderHeight) };
        renderer.DrawTextLine(m_matching ? strings::kTagSheetMatching : strings::kTagSheetBusiest, Font::kCaption, header, theme::kText3);
    }
    for (int i = 0; i < static_cast<int>(m_rows.size()); i++)
    {
        RECT row = RowRect(i);
        D2D1_RECT_F box = ToRectF(row);
        if (i == m_pick)
            renderer.FillRoundedRect(box, static_cast<float>(Scale(kRowRadius)), Faded(theme::kYuri, 0.22f));
        else if (i == m_hover)
            renderer.FillRoundedRect(box, static_cast<float>(Scale(kRowRadius)), theme::kHover);
        RECT text = { row.left + Scale(kRowInset), row.top, row.right - Scale(kRowInset), row.bottom };
        renderer.DrawTextLine(m_rows[i].name, Font::kBody, text, theme::kText1);
        renderer.DrawTextLine(FormatCount(m_rows[i].files), Font::kMono, text, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    PaintButton(renderer, m_hWnd, CancelRect(), strings::kCancel, false, m_hover == kRows);
    PaintButton(renderer, m_hWnd, ConfirmRect(), strings::kTagSheetConfirm, true, m_hover == kRows + 1);
}
