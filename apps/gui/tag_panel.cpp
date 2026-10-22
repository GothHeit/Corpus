#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tag_panel.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/tag.hpp"
#include <algorithm>
#include <unordered_map>

namespace
{
    const int kLabelWidth = 64;      // "123 files", right-aligned before the field
    const int kMinFieldWidth = 160;  // the field takes 40% of the panel, within these bounds
    const int kMaxFieldWidth = 360;
    const int kGap = 12;             // between the label, the field and the chips
    const int kChipHeight = 22;
    const int kChipGap = 6;          // between chips
    const int kChipPadding = 8;      // inside a chip, left of the name and right of the x
    const int kCountGap = 4;         // between the name and the count of a partial chip
    const int kCloseGap = 6;         // between the name and the x
    const int kCloseSize = 7;        // the x
    const int kWheelStep = 40;       // pixels the chips scroll per wheel notch

    bool EqualsNoCase(const std::wstring &a, const std::wstring &b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }

    bool StartsWithNoCase(const std::wstring &s, const std::wstring &prefix)
    {
        return s.size() >= prefix.size() &&
            CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    }

    bool LessNoCase(const std::wstring &a, const std::wstring &b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
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

TagPanel::TagPanel(const library &lib)
    : m_lib(lib), m_field(strings::kTagNoSelection)
{
}

int TagPanel::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void TagPanel::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void TagPanel::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;

    m_field.SetBounds(hWnd, renderer, FieldRect());

    // The DPI may have changed, and with it the size of every chip.
    MeasureChips();
}

// ---- Selection ----

void TagPanel::SetSelection(const std::vector<file*> &files)
{
    m_selectedCount = files.size();
    m_field.SetPlaceholder(files.empty() ? strings::kTagNoSelection : strings::kTagPlaceholder);

    // How many of the selected files have each tag.
    std::unordered_map<const tag*, size_t> counts;
    for (const file *f : files)
    {
        for (const tag *t : f->get_tags())
            counts[t]++;
    }

    m_chips.clear();
    for (const auto &entry : counts)
    {
        bool partial = entry.second < files.size();
        std::wstring count = partial ? std::to_wstring(entry.second) + L"/" + std::to_wstring(files.size()) : std::wstring();
        m_chips.push_back({ Utf8ToWide(entry.first->get_id()), partial, count, 0, 0, 0 });
    }

    // The tags every selected file has come first.
    std::sort(m_chips.begin(), m_chips.end(), [](const Chip &a, const Chip &b)
    {
        if (a.partial != b.partial)
            return !a.partial;
        return LessNoCase(a.name, b.name);
    });

    MeasureChips();
    UpdateSuggestion();
    Invalidate();
}

bool TagPanel::OnEverySelectedFile(const std::wstring &name) const
{
    for (const Chip &chip : m_chips)
    {
        if (chip.name == name)
            return !chip.partial;
    }
    return false;
}

// ---- Chips ----

RECT TagPanel::FieldRect() const
{
    int left = m_bounds.left + Scale(theme::kPageMargin) + Scale(kLabelWidth) + Scale(kGap);
    int width = (m_bounds.right - m_bounds.left) * 2 / 5;
    if (width > Scale(kMaxFieldWidth))
        width = Scale(kMaxFieldWidth);
    if (width < Scale(kMinFieldWidth))
        width = Scale(kMinFieldWidth);
    int top = (m_bounds.top + m_bounds.bottom - Scale(theme::kControlHeight)) / 2;
    RECT r = { left, top, left + width, top + Scale(theme::kControlHeight) };
    return r;
}

RECT TagPanel::ChipArea() const
{
    RECT area = { FieldRect().right + Scale(kGap), m_bounds.top, m_bounds.right - Scale(theme::kPageMargin), m_bounds.bottom };
    if (area.right < area.left)
        area.right = area.left;
    return area;
}

RECT TagPanel::ChipRect(const Chip &chip) const
{
    RECT area = ChipArea();
    int left = area.left + chip.offset - m_chipScroll;
    int top = (area.top + area.bottom - Scale(kChipHeight)) / 2;
    RECT r = { left, top, left + chip.width, top + Scale(kChipHeight) };
    return r;
}

// The x takes the right end of the chip, from halfway between it and the name.
int TagPanel::CloseLeft(const Chip &chip) const
{
    return ChipRect(chip).left + Scale(kChipPadding) + chip.textWidth + Scale(kCloseGap) / 2;
}

// The chip under the point, or -1. 'onClose' tells whether the point is on its x.
int TagPanel::ChipAt(POINT pt, bool &onClose) const
{
    onClose = false;
    RECT area = ChipArea();
    if (!PtInRect(&area, pt))
        return -1;

    for (size_t i = 0; i < m_chips.size(); i++)
    {
        RECT r = ChipRect(m_chips[i]);
        if (PtInRect(&r, pt))
        {
            onClose = pt.x >= CloseLeft(m_chips[i]);
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TagPanel::MeasureChips()
{
    m_chipsWidth = 0;
    if (!m_renderer)
        return;

    int offset = 0;
    for (Chip &chip : m_chips)
    {
        float width = m_renderer->TextWidth(chip.name, Font::kCaption);
        if (chip.partial)
            width += Scale(kCountGap) + m_renderer->TextWidth(chip.count, Font::kMono);
        chip.textWidth = static_cast<int>(width + 0.99f);
        chip.offset = offset;
        chip.width = Scale(kChipPadding) + chip.textWidth + Scale(kCloseGap) + Scale(kCloseSize) + Scale(kChipPadding);
        offset += chip.width + Scale(kChipGap);
    }
    m_chipsWidth = m_chips.empty() ? 0 : offset - Scale(kChipGap);

    ScrollChipsTo(m_chipScroll);
    OnHover(m_hoverPoint);
}

void TagPanel::ScrollChipsTo(int x)
{
    RECT area = ChipArea();
    int maxScroll = m_chipsWidth - (area.right - area.left);
    if (x > maxScroll)
        x = maxScroll;
    if (x < 0)
        x = 0;

    if (x != m_chipScroll)
    {
        m_chipScroll = x;
        OnHover(m_hoverPoint);
        Invalidate();
    }
}

void TagPanel::OnHover(POINT pt)
{
    m_hoverPoint = pt;
    bool onClose = false;
    int chip = ChipAt(pt, onClose);
    if (chip != m_hoverChip || onClose != m_hoverClose)
    {
        m_hoverChip = chip;
        m_hoverClose = onClose;
        Invalidate();
    }
}

// ---- Autocomplete ----

// The word being typed: the text after the last space, while the cursor is at the end.
std::wstring TagPanel::CurrentWord() const
{
    if (!m_field.CursorAtEnd())
        return std::wstring();
    const std::wstring &text = m_field.Query();
    size_t space = text.find_last_of(L' ');
    return space == std::wstring::npos ? text : text.substr(space + 1);
}

void TagPanel::UpdateSuggestion()
{
    m_matches.clear();
    m_matchIndex = 0;

    std::wstring word = CurrentWord();
    if (!word.empty())
    {
        struct Match
        {
            std::wstring name;
            size_t files;
        };
        std::vector<Match> found;
        for (const tag *t : m_lib.get_tags())
        {
            std::wstring name = Utf8ToWide(t->get_id());
            // Tags every selected file already has would change nothing.
            if (name.size() > word.size() && StartsWithNoCase(name, word) && !OnEverySelectedFile(name))
                found.push_back({ name, t->get_files().size() });
        }

        std::sort(found.begin(), found.end(), [](const Match &a, const Match &b)
        {
            if (a.files != b.files)
                return a.files > b.files;
            return LessNoCase(a.name, b.name);
        });
        for (const Match &match : found)
            m_matches.push_back(match.name);
    }

    m_field.SetSuggestion(m_matches.empty() ? std::wstring() : m_matches[0].substr(word.size()));
}

// Replaces the word being typed with the tag it completes to, and starts a new word.
void TagPanel::AcceptSuggestion()
{
    std::wstring text = m_field.Query();
    std::wstring word = CurrentWord();
    text.replace(text.size() - word.size(), word.size(), m_matches[m_matchIndex] + L" ");
    m_field.SetText(text);
    UpdateSuggestion();
}

// The tags typed in the field. A word that differs from an existing tag only in case means that tag, so
// "anime" doesn't create a second tag next to "Anime".
std::vector<std::string> TagPanel::TypedTags() const
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

        std::string name = WideToUtf8(word);
        if (!m_lib.find_tag(name))
        {
            for (const tag *t : m_lib.get_tags())
            {
                if (EqualsNoCase(Utf8ToWide(t->get_id()), word))
                {
                    name = t->get_id();
                    break;
                }
            }
        }
        if (std::find(tags.begin(), tags.end(), name) == tags.end())
            tags.push_back(name);
    }
    return tags;
}

// ---- Input ----

void TagPanel::Focus()
{
    m_field.Focus();
    UpdateSuggestion();
}

void TagPanel::Blur()
{
    m_field.Blur();
}

bool TagPanel::OnLButtonDown(POINT pt, TagEdit &edit)
{
    if (!Contains(pt))
        return false;

    RECT field = FieldRect();
    if (PtInRect(&field, pt))
    {
        m_field.OnLButtonDown(pt);
        UpdateSuggestion();
        return true;
    }

    // A click elsewhere on the panel leaves the field as it is, so typing can go on after clicking a chip.
    bool onClose = false;
    int index = ChipAt(pt, onClose);
    if (index >= 0)
    {
        const Chip &chip = m_chips[index];
        if (onClose)
            edit = { TagEdit::kRemove, { WideToUtf8(chip.name) } };
        else if (chip.partial)
            edit = { TagEdit::kAdd, { WideToUtf8(chip.name) } };
    }
    return true;
}

void TagPanel::OnChar(wchar_t ch)
{
    const std::wstring &text = m_field.Query();
    bool wordStart = m_field.CursorAtEnd() ? (text.empty() || text.back() == L' ') : false;
    if (IsReserved(ch) || (ch == L'-' && wordStart))
    {
        MessageBeep(MB_OK);
        return;
    }
    if (m_field.OnChar(ch))
        UpdateSuggestion();
}

void TagPanel::OnKeyDown(WPARAM key, bool ctrl, TagEdit &edit)
{
    switch (key)
    {
    case VK_RETURN:
    {
        std::vector<std::string> tags = TypedTags();
        if (m_selectedCount == 0 || tags.empty())
        {
            MessageBeep(MB_OK);
            return;
        }
        edit = { TagEdit::kAdd, tags };
        m_field.SetText(std::wstring());
        UpdateSuggestion();
        return;
    }
    case VK_TAB:
        if (!m_matches.empty())
            AcceptSuggestion();
        return;
    case VK_UP:
    case VK_DOWN:
        if (m_matches.size() > 1)
        {
            size_t count = m_matches.size();
            m_matchIndex = (m_matchIndex + (key == VK_DOWN ? 1 : count - 1)) % count;
            m_field.SetSuggestion(m_matches[m_matchIndex].substr(CurrentWord().size()));
        }
        return;
    case VK_ESCAPE:
        Blur();
        return;
    }

    m_field.OnKeyDown(key, ctrl);
    // Moving the cursor changes the word being completed.
    UpdateSuggestion();
}

bool TagPanel::OnMouseWheel(POINT pt, int delta)
{
    if (!Contains(pt))
        return false;
    ScrollChipsTo(m_chipScroll - MulDiv(delta, Scale(kWheelStep), WHEEL_DELTA));
    return true;
}

// ---- Painting ----

void TagPanel::Paint(Renderer &renderer) const
{
    float line = Scale(4) / 4.0f;
    renderer.FillRect(ToRectF(m_bounds), theme::kBase);
    renderer.FillRect(D2D1::RectF(static_cast<float>(m_bounds.left), static_cast<float>(m_bounds.top),
        static_cast<float>(m_bounds.right), m_bounds.top + line), theme::kStroke);

    if (m_selectedCount > 0)
    {
        std::wstring label = m_selectedCount == 1 ? strings::kOneFile : std::to_wstring(m_selectedCount) + L" " + strings::kFiles;
        int left = m_bounds.left + Scale(theme::kPageMargin);
        RECT labelRect = { left, m_bounds.top, left + Scale(kLabelWidth), m_bounds.bottom };
        renderer.DrawTextLine(label, Font::kMono, labelRect, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    m_field.Paint(renderer);

    RECT area = ChipArea();
    if (m_chips.empty() || area.right <= area.left)
        return;

    renderer.PushClip(area);
    const float radius = static_cast<float>(Scale(theme::kChipRadius));

    for (size_t i = 0; i < m_chips.size(); i++)
    {
        const Chip &chip = m_chips[i];
        RECT r = ChipRect(chip);
        if (r.right <= area.left || r.left >= area.right)
            continue;

        bool hover = static_cast<int>(i) == m_hoverChip;
        D2D1_RECT_F box = ToRectF(r);
        if (chip.partial)
        {
            // Clicking a partial chip adds the tag to the rest of the files, so it reacts like a button.
            if (hover && !m_hoverClose)
                renderer.FillRoundedRect(box, radius, theme::kHover);
            D2D1_RECT_F outline = D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2);
            renderer.DrawRoundedRect(outline, radius, theme::kText3, line, true);
        }
        else
        {
            renderer.FillRoundedRect(box, radius, theme::kChip);
        }

        // Up to the x: the measured width can come out a fraction short once drawn.
        RECT text = { r.left + Scale(kChipPadding), r.top, r.right - Scale(kChipPadding) - Scale(kCloseSize), r.bottom };
        renderer.DrawTextLine(chip.name, Font::kCaption, text, theme::kText1);
        if (chip.partial)
        {
            text.left += static_cast<int>(renderer.TextWidth(chip.name, Font::kCaption) + 0.5f) + Scale(kCountGap);
            renderer.DrawTextLine(chip.count, Font::kMono, text, theme::kText3);
        }

        // The x, with a soft square behind it under the pointer.
        float size = static_cast<float>(Scale(kCloseSize));
        float cx = r.right - Scale(kChipPadding) - size / 2.0f;
        float cy = (r.top + r.bottom) / 2.0f;
        bool closeHover = hover && m_hoverClose;
        if (closeHover)
        {
            float pad = static_cast<float>(Scale(3));
            renderer.FillRoundedRect(D2D1::RectF(cx - size / 2 - pad, cy - size / 2 - pad, cx + size / 2 + pad, cy + size / 2 + pad),
                static_cast<float>(Scale(2)), theme::kHover);
        }
        D2D1_COLOR_F color = closeHover ? theme::kText1 : theme::kText2;
        float h = size / 2.0f;
        renderer.DrawLine(D2D1::Point2F(cx - h, cy - h), D2D1::Point2F(cx + h, cy + h), color, 1.2f * line);
        renderer.DrawLine(D2D1::Point2F(cx - h, cy + h), D2D1::Point2F(cx + h, cy - h), color, 1.2f * line);
    }

    renderer.PopClip();
}
