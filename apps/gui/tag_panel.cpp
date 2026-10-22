#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tag_panel.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/tag.hpp"
#include <algorithm>
#include <unordered_map>

namespace
{
    const int kLabelWidth = 56;      // "123 files", right-aligned before the field
    const int kMinFieldWidth = 160;  // the field takes 40% of the panel, within these bounds
    const int kMaxFieldWidth = 360;
    const int kGap = 8;              // between the label, the field and the chips
    const int kChipGap = 6;          // between chips
    const int kChipInset = 2;        // from the top and bottom of the panel
    const int kChipPadding = 10;     // inside a chip, left of the name and right of the x
    const int kCloseGap = 6;         // between the name and the x
    const int kFontSize = 12;
    const int kWheelStep = 40;       // pixels the chips scroll per wheel notch

    const wchar_t kCloseGlyph[] = L"\u00D7";

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
    : m_lib(lib), m_field(L"select files to tag them")
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

HFONT TagPanel::CreateChipFont() const
{
    return CreateUIFont(Scale(kFontSize));
}

void TagPanel::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_bounds = bounds;

    m_field.SetBounds(hWnd, FieldRect());

    // The DPI may have changed, and with it the size of every chip.
    MeasureChips();
}

// ---- Selection ----

void TagPanel::SetSelection(const std::vector<file*> &files)
{
    m_selectedCount = files.size();
    m_field.SetPlaceholder(files.empty() ? L"select files to tag them" : L"add tags...");

    // How many of the selected files have each tag.
    std::unordered_map<const tag*, size_t> counts;
    for (const file *f : files)
    {
        for (const tag *t : f->get_tags())
            counts[t]++;
    }

    m_chips.clear();
    for (const auto &entry : counts)
        m_chips.push_back({ Utf8ToWide(entry.first->get_id()), entry.second < files.size(), 0, 0, 0 });

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
    int left = m_bounds.left + Scale(kLabelWidth) + Scale(kGap);
    int width = (m_bounds.right - m_bounds.left) * 2 / 5;
    if (width > Scale(kMaxFieldWidth))
        width = Scale(kMaxFieldWidth);
    if (width < Scale(kMinFieldWidth))
        width = Scale(kMinFieldWidth);
    RECT r = { left, m_bounds.top, left + width, m_bounds.bottom };
    return r;
}

RECT TagPanel::ChipArea() const
{
    RECT area = { FieldRect().right + Scale(kGap), m_bounds.top, m_bounds.right, m_bounds.bottom };
    if (area.right < area.left)
        area.right = area.left;
    return area;
}

RECT TagPanel::ChipRect(const Chip &chip) const
{
    RECT area = ChipArea();
    int left = area.left + chip.offset - m_chipScroll;
    RECT r = { left, area.top + Scale(kChipInset), left + chip.width, area.bottom - Scale(kChipInset) };
    return r;
}

void TagPanel::MeasureChips()
{
    m_chipsWidth = 0;
    if (!m_hWnd)
        return;

    HDC dc = GetDC(m_hWnd);
    HFONT font = CreateChipFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SIZE close;
    GetTextExtentPoint32W(dc, kCloseGlyph, 1, &close);

    int offset = 0;
    for (Chip &chip : m_chips)
    {
        SIZE size;
        GetTextExtentPoint32W(dc, chip.name.c_str(), static_cast<int>(chip.name.size()), &size);
        chip.textWidth = size.cx;
        chip.offset = offset;
        chip.width = Scale(kChipPadding) + size.cx + Scale(kCloseGap) + close.cx + Scale(kChipPadding);
        offset += chip.width + Scale(kChipGap);
    }
    m_chipsWidth = m_chips.empty() ? 0 : offset - Scale(kChipGap);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(m_hWnd, dc);

    ScrollChipsTo(m_chipScroll);
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
    RECT area = ChipArea();
    if (PtInRect(&area, pt))
    {
        for (const Chip &chip : m_chips)
        {
            RECT r = ChipRect(chip);
            if (!PtInRect(&r, pt))
                continue;

            // The x takes the right end of the chip, from halfway between it and the name.
            int closeLeft = r.left + Scale(kChipPadding) + chip.textWidth + Scale(kCloseGap) / 2;
            if (pt.x >= closeLeft)
                edit = { TagEdit::kRemove, { WideToUtf8(chip.name) } };
            else if (chip.partial)
                edit = { TagEdit::kAdd, { WideToUtf8(chip.name) } };
            break;
        }
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

void TagPanel::Paint(HDC dc) const
{
    HFONT font = CreateChipFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    if (m_selectedCount > 0)
    {
        std::wstring label = m_selectedCount == 1 ? L"1 file" : std::to_wstring(m_selectedCount) + L" files";
        RECT labelRect = { m_bounds.left, m_bounds.top, m_bounds.left + Scale(kLabelWidth), m_bounds.bottom };
        SetTextColor(dc, theme::kPlaceholderText);
        DrawTextW(dc, label.c_str(), -1, &labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    RECT area = ChipArea();
    if (!m_chips.empty() && area.right > area.left)
    {
        int savedDC = SaveDC(dc);
        IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);

        Gdiplus::Graphics graphics(dc);
        graphics.SetClip(Gdiplus::Rect(area.left, area.top, area.right - area.left, area.bottom - area.top));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush fullBrush(ToGdiplusColor(theme::kChipFill));
        Gdiplus::SolidBrush partialBrush(ToGdiplusColor(theme::kChipPartialFill));
        Gdiplus::Pen partialPen(ToGdiplusColor(theme::kChipFill), static_cast<float>(Scale(1)));

        for (const Chip &chip : m_chips)
        {
            RECT r = ChipRect(chip);
            if (r.right <= area.left || r.left >= area.right)
                continue;

            Gdiplus::GraphicsPath path;
            AddRoundedRect(path, r, (r.bottom - r.top) / 2);
            graphics.FillPath(chip.partial ? &partialBrush : &fullBrush, &path);
            if (chip.partial)
                graphics.DrawPath(&partialPen, &path);

            RECT nameRect = { r.left + Scale(kChipPadding), r.top, r.left + Scale(kChipPadding) + chip.textWidth, r.bottom };
            SetTextColor(dc, chip.partial ? theme::kPlaceholderText : theme::kText);
            DrawTextW(dc, chip.name.c_str(), -1, &nameRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT closeRect = { nameRect.right + Scale(kCloseGap), r.top, r.right - Scale(kChipPadding), r.bottom };
            SetTextColor(dc, theme::kPlaceholderText);
            DrawTextW(dc, kCloseGlyph, 1, &closeRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        RestoreDC(dc, savedDC);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);

    m_field.Paint(dc);
}
