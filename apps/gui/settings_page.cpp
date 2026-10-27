#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "settings_page.hpp"
#include "file_grid.hpp"
#include "licenses.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>

namespace
{
    const int kMargin = 24;         // between the column and the edges of the window
    const int kHeaderTop = 24;
    const int kButtonHeight = 28;
    const int kButtonPadding = 14;  // on each side of the label of the header button
    const int kSectionGap = 24;     // above a section title
    const int kSectionTitle = 20;   // the title's height, and its gap to what it names
    const int kPanelPadding = 16;
    const int kSwitchWidth = 34;
    const int kSwitchHeight = 18;
    const int kKnob = 12;
    const int kSliderWidth = 160;
    const int kSliderTrack = 4;
    const int kSliderKnob = 14;
    const int kTileStep = 16;      // the slider moves in these steps (128 is one of them)
    const int kAboutHeight = 76;
    const int kNameClicks = 5;      // in a row, to open the licenses
    const ULONGLONG kNameClickMs = 3000;
    const int kWheelStep = 60;      // pixels per notch of the wheel
    const int kNameGap = 28;        // above the name of a license
    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 4;
    const int kScrollbarMinThumb = 24;
    const int kStopPadding = 10;    // on each side of "Stop watching"
    const int kStopHeight = 26;
    const int kNameGapInRow = 12;   // between a watched folder's name and its path
    const int kNoteGap = 8;         // between the watched folders and the note under them

    // The last part of a path: "Pictures" for C:\Users\me\Pictures, the drive itself for a drive root.
    std::wstring LastName(const std::wstring &path)
    {
        size_t end = path.size();
        while (end > 1 && (path[end - 1] == L'\\' || path[end - 1] == L'/'))
            end--;
        size_t slash = path.find_last_of(L"\\/", end - 1);
        return slash == std::wstring::npos || slash + 1 >= end ? path : path.substr(slash + 1, end - slash - 1);
    }

    std::wstring ExpandTabs(const std::string &line)
    {
        std::wstring wide = Utf8ToWide(line);
        std::wstring out;
        for (wchar_t ch : wide)
        {
            if (ch == L'\t')
                out += L"    ";
            else if (ch != L'\r')
                out += ch;
        }
        return out;
    }
}

int SettingsPage::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void SettingsPage::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void SettingsPage::Open()
{
    m_open = true;
    m_licenses = false;
    m_nameClicks = 0;
    m_scroll = 0;
    m_settingsScroll = 0;
    m_hover.Reset(m_hWnd, kSettingsHoverTimer);
    Invalidate();
}

void SettingsPage::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_licenses = false;
    m_hover.Reset(m_hWnd, kSettingsHoverTimer);
}

void SettingsPage::SetOpenWithOn(bool on)
{
    m_openWithOn = on;
    Invalidate();
}

void SettingsPage::SetSidebarShown(bool shown)
{
    m_sidebarShown = shown;
    if (m_open)
        Invalidate();
}

void SettingsPage::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    if (m_open && m_licenses)
        LayoutLines();
    if (m_open && !m_licenses)
        ScrollSettingsTo(m_settingsScroll);
}

void SettingsPage::SetBackups(std::vector<std::wstring> labels)
{
    m_backups = std::move(labels);
    m_hover.Reset(m_hWnd, kSettingsHoverTimer);
    if (m_open)
    {
        ScrollSettingsTo(m_settingsScroll);
        Invalidate();
    }
}

void SettingsPage::SetWatchedFolders(std::vector<std::wstring> folders)
{
    m_watched = std::move(folders);
    m_hover.Reset(m_hWnd, kSettingsHoverTimer);
    if (m_open)
    {
        ScrollSettingsTo(m_settingsScroll);
        Invalidate();
    }
}

// The top of the settings as they are scrolled; the licenses scroll on their own, under a header that stays.
int SettingsPage::Top() const
{
    return m_bounds.top - (m_licenses ? 0 : m_settingsScroll);
}

int SettingsPage::ColumnWidth() const
{
    int width = (m_bounds.right - m_bounds.left) - 2 * Scale(kMargin);
    return std::max(0, std::min(width, Scale(theme::kSettingsColumn)));
}

int SettingsPage::ColumnLeft() const
{
    return m_bounds.left + ((m_bounds.right - m_bounds.left) - ColumnWidth()) / 2;
}

// In the header, at the right edge of the column: "Done" on the settings, back to them on the licenses.
RECT SettingsPage::HeaderButtonRect() const
{
    const wchar_t *label = m_licenses ? strings::kSettings : strings::kSettingsDone;
    int width = static_cast<int>(m_renderer->TextWidth(label, Font::kBody) + 0.99f) + 2 * Scale(kButtonPadding);
    int right = ColumnLeft() + ColumnWidth();
    int top = Top() + Scale(kHeaderTop);
    RECT r = { right - width, top, right, top + Scale(kButtonHeight) };
    return r;
}

RECT SettingsPage::SidebarRowRect() const
{
    int top = Top() + Scale(kHeaderTop) + Scale(kButtonHeight) + Scale(kSectionGap) + Scale(kSectionTitle);
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + Scale(theme::kSettingsRow) };
    return r;
}

RECT SettingsPage::TileRowRect() const
{
    int top = SidebarRowRect().bottom + Scale(kSectionGap) + Scale(kSectionTitle);
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + Scale(theme::kSettingsRow) };
    return r;
}

// The slider's track area: at the right end of the size row, as the switches are in the other rows.
RECT SettingsPage::SliderRect() const
{
    RECT row = TileRowRect();
    int right = row.right - Scale(kPanelPadding);
    RECT r = { right - Scale(kSliderWidth), row.top, right, row.bottom };
    return r;
}

void SettingsPage::SetTileSize(int size)
{
    m_tileSize = std::max(FileGrid::kMinTile, std::min(size, FileGrid::kMaxTile));
    if (m_open)
        Invalidate();
}

// Sets the size from where the pointer is along the slider. True when it changed.
bool SettingsPage::SetTileFromPoint(POINT pt)
{
    RECT slider = SliderRect();
    int knob = Scale(kSliderKnob);
    int span = std::max(1, static_cast<int>(slider.right - slider.left) - knob);
    int x = std::max(0, std::min(static_cast<int>(pt.x - slider.left) - knob / 2, span));
    int range = FileGrid::kMaxTile - FileGrid::kMinTile;
    int size = FileGrid::kMinTile + (x * range + span / 2) / span;
    size = ((size - FileGrid::kMinTile + kTileStep / 2) / kTileStep) * kTileStep + FileGrid::kMinTile;
    size = std::max(FileGrid::kMinTile, std::min(size, FileGrid::kMaxTile));
    if (size == m_tileSize)
        return false;
    m_tileSize = size;
    Invalidate();
    return true;
}

// One panel with a row per watched folder, or a row saying there is none.
RECT SettingsPage::WatchedPanelRect() const
{
    int top = TileRowRect().bottom + Scale(kSectionGap) + Scale(kSectionTitle);
    int rows = static_cast<int>(std::max<size_t>(1, m_watched.size()));
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + rows * Scale(theme::kSettingsRow) };
    return r;
}

RECT SettingsPage::WatchedRowRect(size_t index) const
{
    RECT panel = WatchedPanelRect();
    int top = panel.top + static_cast<int>(index) * Scale(theme::kSettingsRow);
    RECT r = { panel.left, top, panel.right, top + Scale(theme::kSettingsRow) };
    return r;
}

// "Stop watching", at the right end of a folder's row.
RECT SettingsPage::StopButtonRect(size_t index) const
{
    RECT row = WatchedRowRect(index);
    int width = static_cast<int>(m_renderer->TextWidth(strings::kStopWatching, Font::kCaption) + 0.99f) + 2 * Scale(kStopPadding);
    int right = row.right - Scale(kPanelPadding) + Scale(kStopPadding);
    int top = (row.top + row.bottom - Scale(kStopHeight)) / 2;
    RECT r = { right - width, top, right, top + Scale(kStopHeight) };
    return r;
}

// The note under the watched folders: up to two lines.
int SettingsPage::WatchedNoteHeight() const
{
    return Scale(kNoteGap) + 2 * static_cast<int>(m_renderer->LineHeight(Font::kCaption) + 0.99f);
}

int SettingsPage::MaxSettingsScroll() const
{
    int bottom = AboutRect().bottom + m_settingsScroll + Scale(kMargin);
    return std::max(0, bottom - static_cast<int>(m_bounds.bottom));
}

void SettingsPage::ScrollSettingsTo(int y)
{
    y = std::max(0, std::min(y, m_renderer ? MaxSettingsScroll() : 0));
    if (y == m_settingsScroll)
        return;
    m_settingsScroll = y;
    Invalidate();
}

RECT SettingsPage::OpenWithRowRect() const
{
    int top = WatchedPanelRect().bottom + WatchedNoteHeight() + Scale(kSectionGap) + Scale(kSectionTitle);
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + Scale(theme::kSettingsRow) };
    return r;
}

// Two rows: export and import of the catalog as a file.
RECT SettingsPage::BackupPanelRect() const
{
    int top = OpenWithRowRect().bottom + Scale(kSectionGap) + Scale(kSectionTitle);
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + 2 * Scale(theme::kSettingsRow) };
    return r;
}

RECT SettingsPage::BackupRowRect(int index) const
{
    RECT panel = BackupPanelRect();
    int top = panel.top + index * Scale(theme::kSettingsRow);
    RECT r = { panel.left, top, panel.right, top + Scale(theme::kSettingsRow) };
    return r;
}

// One row per automatic backup, or a row saying there is none.
RECT SettingsPage::RestorePanelRect() const
{
    int top = BackupPanelRect().bottom + Scale(kSectionGap) + Scale(kSectionTitle);
    int rows = static_cast<int>(std::max<size_t>(1, m_backups.size()));
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + rows * Scale(theme::kSettingsRow) };
    return r;
}

RECT SettingsPage::RestoreRowRect(size_t index) const
{
    RECT panel = RestorePanelRect();
    int top = panel.top + static_cast<int>(index) * Scale(theme::kSettingsRow);
    RECT r = { panel.left, top, panel.right, top + Scale(theme::kSettingsRow) };
    return r;
}

RECT SettingsPage::RestoreButtonRect(size_t index) const
{
    RECT row = RestoreRowRect(index);
    int width = static_cast<int>(m_renderer->TextWidth(strings::kRestore, Font::kCaption) + 0.99f) + 2 * Scale(kStopPadding);
    int right = row.right - Scale(kPanelPadding) + Scale(kStopPadding);
    int top = (row.top + row.bottom - Scale(kStopHeight)) / 2;
    RECT r = { right - width, top, right, top + Scale(kStopHeight) };
    return r;
}

RECT SettingsPage::AboutRect() const
{
    int top = RestorePanelRect().bottom + Scale(kSectionGap) + Scale(kSectionTitle);
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + Scale(kAboutHeight) };
    return r;
}

RECT SettingsPage::AboutNameRect() const
{
    RECT about = AboutRect();
    int width = static_cast<int>(m_renderer->TextWidth(strings::kAppName, Font::kBodyStrong) + 0.99f);
    int height = static_cast<int>(m_renderer->LineHeight(Font::kBodyStrong));
    int left = about.left + Scale(kPanelPadding);
    int top = about.top + Scale(12);
    RECT r = { left, top, left + width, top + height };
    return r;
}

// Where the licenses scroll: under the header, the full width of the page so the scrollbar rests at its edge.
RECT SettingsPage::ListRect() const
{
    int top = m_bounds.top + Scale(kHeaderTop) + Scale(kButtonHeight) + Scale(12);
    RECT r = { m_bounds.left, top, m_bounds.right, std::max<int>(top, m_bounds.bottom) };
    return r;
}

// ---- The licenses ----

void SettingsPage::Build()
{
    if (!m_lines.empty())
        return;
    m_lines.push_back({ Line::kIntro, strings::kLicensesIntro });
    for (const LicenseSection &section : Licenses())
    {
        m_lines.push_back({ Line::kName, section.name });
        m_lines.push_back({ Line::kSummary, section.summary });
        size_t start = 0;
        const std::string &text = section.text;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            m_lines.push_back({ Line::kText, ExpandTabs(text.substr(start, end - start)) });
            start = end + 1;
        }
    }
}

// Heights of the lines for the width of the column. A text line only wraps when it is longer than the column holds
// (the texts are laid out in a fixed-width face), and then DirectWrite says how many lines it takes.
void SettingsPage::LayoutLines()
{
    Build();
    int width = ColumnWidth();
    if (width == m_layoutWidth)
        return;
    m_layoutWidth = width;

    const int mono = static_cast<int>(m_renderer->LineHeight(Font::kMono));
    const int body = static_cast<int>(m_renderer->LineHeight(Font::kBody));
    const int caption = static_cast<int>(m_renderer->LineHeight(Font::kCaption));
    const float charWidth = m_renderer->TextWidth(L"M", Font::kMono);
    const size_t perLine = charWidth > 0.0f ? static_cast<size_t>(width / charWidth) : 1;

    int y = 0;
    for (Line &line : m_lines)
    {
        line.top = y;
        switch (line.kind)
        {
        case Line::kIntro:
            line.height = body + Scale(4);
            break;
        case Line::kName:
            line.top = y += Scale(kNameGap);
            line.height = body + Scale(2);
            break;
        case Line::kSummary:
            line.height = caption + Scale(12);
            break;
        case Line::kText:
            line.height = mono;
            if (line.text.size() > perLine)
            {
                ComPtr<IDWriteTextLayout> layout = m_renderer->Layout(line.text, Font::kMono, static_cast<float>(width), 64,
                    DWRITE_TEXT_ALIGNMENT_LEADING, false);
                DWRITE_TEXT_METRICS metrics = {};
                if (layout && SUCCEEDED(layout->GetMetrics(&metrics)))
                    line.height = std::max(mono, static_cast<int>(metrics.height + 0.99f));
            }
            break;
        }
        y += line.height;
    }
    m_contentHeight = y + Scale(kMargin);
    ScrollTo(m_scroll);
}

int SettingsPage::MaxScroll() const
{
    RECT list = ListRect();
    return std::max(0, m_contentHeight - static_cast<int>(list.bottom - list.top));
}

void SettingsPage::ScrollTo(int y)
{
    y = std::max(0, std::min(y, MaxScroll()));
    if (y == m_scroll)
        return;
    m_scroll = y;
    Invalidate();
}

void SettingsPage::ShowLicenses(bool shown)
{
    m_licenses = shown;
    m_nameClicks = 0;
    m_scroll = 0;
    m_hover.Reset(m_hWnd, kSettingsHoverTimer);
    if (shown)
    {
        m_layoutWidth = -1;
        LayoutLines();
    }
    Invalidate();
}

// ---- Input ----

SettingsPage::Action SettingsPage::OnLButtonDown(POINT pt)
{
    if (!Contains(pt))
        return Action::kNone;

    RECT button = HeaderButtonRect();
    if (PtInRect(&button, pt))
    {
        if (m_licenses)
        {
            ShowLicenses(false);
            return Action::kNone;
        }
        return Action::kClose;
    }
    if (m_licenses)
        return Action::kNone;

    RECT row = SidebarRowRect();
    if (PtInRect(&row, pt))
        return Action::kToggleSidebar;
    RECT tiles = TileRowRect();
    if (PtInRect(&tiles, pt))
    {
        RECT slider = SliderRect();
        InflateRect(&slider, Scale(8), 0);
        if (!PtInRect(&slider, pt))
            return Action::kNone;
        m_dragging = true;
        SetTileFromPoint(pt);
        return Action::kTileSize;
    }
    RECT openWith = OpenWithRowRect();
    if (PtInRect(&openWith, pt))
        return Action::kToggleOpenWith;
    for (size_t i = 0; i < m_watched.size(); i++)
    {
        RECT stop = StopButtonRect(i);
        if (PtInRect(&stop, pt))
        {
            m_stopIndex = i;
            return Action::kStopWatching;
        }
    }

    for (int i = 0; i < 2; i++)
    {
        RECT backupRow = BackupRowRect(i);
        if (PtInRect(&backupRow, pt))
            return i == 0 ? Action::kExport : Action::kImport;
    }
    for (size_t i = 0; i < m_backups.size(); i++)
    {
        RECT restore = RestoreButtonRect(i);
        if (PtInRect(&restore, pt))
        {
            m_restoreIndex = i;
            return Action::kRestore;
        }
    }

    // The way to the licenses: five clicks on the name, close together.
    RECT name = AboutNameRect();
    InflateRect(&name, Scale(4), Scale(4));
    if (PtInRect(&name, pt))
    {
        ULONGLONG now = GetTickCount64();
        m_nameClicks = now - m_lastNameClick <= kNameClickMs ? m_nameClicks + 1 : 1;
        m_lastNameClick = now;
        if (m_nameClicks >= kNameClicks)
            ShowLicenses(true);
    }
    else
        m_nameClicks = 0;
    return Action::kNone;
}

SettingsPage::Action SettingsPage::OnKeyDown(WPARAM key)
{
    if (!m_licenses)
    {
        if (key == VK_ESCAPE || key == VK_RETURN)
            return Action::kClose;
        int step = Scale(kWheelStep);
        int page = std::max(step, static_cast<int>(m_bounds.bottom - m_bounds.top) - step);
        switch (key)
        {
        case VK_UP:    ScrollSettingsTo(m_settingsScroll - step / 3); break;
        case VK_DOWN:  ScrollSettingsTo(m_settingsScroll + step / 3); break;
        case VK_PRIOR: ScrollSettingsTo(m_settingsScroll - page); break;
        case VK_NEXT:  ScrollSettingsTo(m_settingsScroll + page); break;
        case VK_HOME:  ScrollSettingsTo(0); break;
        case VK_END:   ScrollSettingsTo(MaxSettingsScroll()); break;
        }
        return Action::kNone;
    }

    RECT list = ListRect();
    int page = std::max(Scale(kWheelStep), static_cast<int>(list.bottom - list.top) - Scale(kWheelStep));
    switch (key)
    {
    case VK_ESCAPE:
    case VK_BACK:
        ShowLicenses(false);
        break;
    case VK_UP:    ScrollTo(m_scroll - Scale(kWheelStep) / 3); break;
    case VK_DOWN:  ScrollTo(m_scroll + Scale(kWheelStep) / 3); break;
    case VK_PRIOR: ScrollTo(m_scroll - page); break;
    case VK_NEXT:
    case VK_SPACE: ScrollTo(m_scroll + page); break;
    case VK_HOME:  ScrollTo(0); break;
    case VK_END:   ScrollTo(MaxScroll()); break;
    }
    return Action::kNone;
}

SettingsPage::Action SettingsPage::OnMouseMove(POINT pt)
{
    if (!m_dragging)
        return Action::kNone;
    return SetTileFromPoint(pt) ? Action::kTileSize : Action::kNone;
}

void SettingsPage::OnMouseWheel(int delta)
{
    if (m_licenses)
        ScrollTo(m_scroll - delta * Scale(kWheelStep) / WHEEL_DELTA);
    else
        ScrollSettingsTo(m_settingsScroll - delta * Scale(kWheelStep) / WHEEL_DELTA);
}

void SettingsPage::OnHover(POINT pt)
{
    if (!m_open)
        return;
    RECT button = HeaderButtonRect();
    RECT row = SidebarRowRect();
    RECT openWith = OpenWithRowRect();
    RECT tiles = TileRowRect();
    int id = -1;
    if (PtInRect(&button, pt))
        id = 0;
    else if (!m_licenses && PtInRect(&row, pt))
        id = 1;
    else if (!m_licenses && PtInRect(&openWith, pt))
        id = 2;
    else if (!m_licenses && PtInRect(&tiles, pt))
        id = 3;
    for (size_t i = 0; id < 0 && !m_licenses && i < m_watched.size(); i++)
    {
        RECT stop = StopButtonRect(i);
        if (PtInRect(&stop, pt))
            id = 4 + static_cast<int>(i);
    }
    for (int i = 0; id < 0 && !m_licenses && i < 2; i++)
    {
        RECT backupRow = BackupRowRect(i);
        if (PtInRect(&backupRow, pt))
            id = 100 + i;
    }
    for (size_t i = 0; id < 0 && !m_licenses && i < m_backups.size(); i++)
    {
        RECT restore = RestoreButtonRect(i);
        if (PtInRect(&restore, pt))
            id = 110 + static_cast<int>(i);
    }
    if (m_hover.Set(m_hWnd, kSettingsHoverTimer, id))
        Invalidate();
}

void SettingsPage::OnTimer()
{
    m_hover.Step(m_hWnd, kSettingsHoverTimer);
    Invalidate();
}

// ---- Painting ----

void SettingsPage::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;
    renderer.FillRect(ToRectF(m_bounds), theme::kLayer);
    if (m_licenses)
        PaintLicenses(renderer);
    else
    {
        renderer.PushClip(m_bounds);
        PaintSettings(renderer);
        renderer.PopClip();
        int max = MaxSettingsScroll();
        if (max > 0)
            PaintScrollbar(renderer, m_bounds, max + (m_bounds.bottom - m_bounds.top), m_settingsScroll, max);
    }
}

void SettingsPage::PaintHeaderButton(Renderer &renderer, const wchar_t *label) const
{
    RECT button = HeaderButtonRect();
    const float radius = static_cast<float>(Scale(theme::kControlRadius));
    const float line = Scale(4) / 4.0f;
    D2D1_RECT_F box = ToRectF(button);
    renderer.FillRoundedRect(box, radius, theme::kControl);
    float hover = m_hover.Level(0);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));
    renderer.DrawRoundedRect(D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2),
        radius, theme::kStroke, line);
    renderer.DrawTextLine(label, Font::kBody, button, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void SettingsPage::PaintSettings(Renderer &renderer) const
{
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const int left = ColumnLeft();
    const int right = left + ColumnWidth();

    RECT header = { left, Top() + Scale(kHeaderTop), right, Top() + Scale(kHeaderTop) + Scale(kButtonHeight) };
    renderer.DrawTextLine(strings::kSettings, Font::kBodyStrong, header, theme::kText1);
    PaintHeaderButton(renderer, strings::kSettingsDone);

    // Sidebar.
    RECT row = SidebarRowRect();
    RECT title = { left, row.top - Scale(kSectionTitle), right, row.top };
    renderer.DrawTextLine(strings::kSettingsSidebar, Font::kCaptionStrong, title, theme::kText3);
    PaintSwitchRow(renderer, row, strings::kShowSidebar, strings::kShowSidebarKey, m_sidebarShown, m_hover.Level(1));

    // Files: the thumbnail size.
    RECT tiles = TileRowRect();
    RECT filesTitle = { left, tiles.top - Scale(kSectionTitle), right, tiles.top };
    renderer.DrawTextLine(strings::kSettingsFiles, Font::kCaptionStrong, filesTitle, theme::kText3);
    PaintSizeRow(renderer, tiles, m_hover.Level(3));

    PaintWatched(renderer);

    // Windows.
    RECT openWith = OpenWithRowRect();
    RECT windowsTitle = { left, openWith.top - Scale(kSectionTitle), right, openWith.top };
    renderer.DrawTextLine(strings::kSettingsWindows, Font::kCaptionStrong, windowsTitle, theme::kText3);
    PaintSwitchRow(renderer, openWith, strings::kOpenWithRow, nullptr, m_openWithOn, m_hover.Level(2));

    PaintBackup(renderer);

    const int pad = Scale(kPanelPadding);
    // About.
    RECT about = AboutRect();
    RECT aboutTitle = { left, about.top - Scale(kSectionTitle), right, about.top };
    renderer.DrawTextLine(strings::kAbout, Font::kCaptionStrong, aboutTitle, theme::kText3);
    renderer.FillRoundedRect(ToRectF(about), radius, theme::kControl);
    RECT name = AboutNameRect();
    renderer.DrawTextLine(strings::kAppName, Font::kBodyStrong, name, theme::kText1);
    float textWidth = static_cast<float>(about.right - about.left - 2 * pad);
    ComPtr<IDWriteTextLayout> text = renderer.Layout(strings::kAboutText, Font::kCaption, textWidth, 3);
    renderer.DrawLayout(text.Get(), static_cast<float>(about.left + pad), static_cast<float>(name.bottom + Scale(6)), theme::kText2);
}

// The export and import rows, and the automatic backups with a button to restore each.
void SettingsPage::PaintBackup(Renderer &renderer) const
{
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const int pad = Scale(kPanelPadding);
    RECT panel = BackupPanelRect();
    RECT title = { panel.left, panel.top - Scale(kSectionTitle), panel.right, panel.top };
    renderer.DrawTextLine(strings::kSettingsBackup, Font::kCaptionStrong, title, theme::kText3);
    renderer.FillRoundedRect(ToRectF(panel), radius, theme::kControl);
    const wchar_t *labels[2] = { strings::kExportCatalog, strings::kImportCatalog };
    for (int i = 0; i < 2; i++)
    {
        RECT row = BackupRowRect(i);
        if (i > 0)
            renderer.FillRect(D2D1::RectF(static_cast<float>(row.left + pad), static_cast<float>(row.top),
                static_cast<float>(row.right - pad), static_cast<float>(row.top + 1)), theme::kStroke);
        float hover = m_hover.Level(100 + i);
        if (hover > 0.0f)
            renderer.FillRoundedRect(ToRectF(row), radius, Faded(theme::kHover, hover));
        RECT text = { row.left + pad, row.top, row.right - pad, row.bottom };
        renderer.DrawTextLine(labels[i], Font::kBody, text, theme::kText1);
    }

    RECT restorePanel = RestorePanelRect();
    RECT restoreTitle = { restorePanel.left, restorePanel.top - Scale(kSectionTitle), restorePanel.right, restorePanel.top };
    renderer.DrawTextLine(strings::kSettingsRestore, Font::kCaptionStrong, restoreTitle, theme::kText3);
    renderer.FillRoundedRect(ToRectF(restorePanel), radius, theme::kControl);
    if (m_backups.empty())
    {
        RECT text = { restorePanel.left + pad, restorePanel.top, restorePanel.right - pad, restorePanel.bottom };
        renderer.DrawTextLine(strings::kNoBackups, Font::kBody, text, theme::kText3);
    }
    for (size_t i = 0; i < m_backups.size(); i++)
    {
        RECT row = RestoreRowRect(i);
        if (i > 0)
            renderer.FillRect(D2D1::RectF(static_cast<float>(row.left + pad), static_cast<float>(row.top),
                static_cast<float>(row.right - pad), static_cast<float>(row.top + 1)), theme::kStroke);
        RECT button = RestoreButtonRect(i);
        float hover = m_hover.Level(110 + static_cast<int>(i));
        if (hover > 0.0f)
            renderer.FillRoundedRect(ToRectF(button), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kHover, hover));
        renderer.DrawTextLine(strings::kRestore, Font::kCaption, button, hover > 0.5f ? theme::kText1 : theme::kText2,
            DWRITE_TEXT_ALIGNMENT_CENTER);
        RECT text = { row.left + pad, row.top, button.left - Scale(kNameGapInRow), row.bottom };
        renderer.DrawTextLine(m_backups[i], Font::kMono, text, theme::kText1);
    }
}

// A row of the settings: a panel with the setting's name, its shortcut if it has one, and a switch.
void SettingsPage::PaintSwitchRow(Renderer &renderer, const RECT &row, const wchar_t *label, const wchar_t *keys,
    bool on, float hover) const
{
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const int pad = Scale(kPanelPadding);

    D2D1_RECT_F box = ToRectF(row);
    renderer.FillRoundedRect(box, radius, theme::kControl);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));

    RECT track = { row.right - pad - Scale(kSwitchWidth), (row.top + row.bottom - Scale(kSwitchHeight)) / 2, 0, 0 };
    track.right = track.left + Scale(kSwitchWidth);
    track.bottom = track.top + Scale(kSwitchHeight);
    RECT text = { row.left + pad, row.top, track.left, row.bottom };
    renderer.DrawTextLine(label, Font::kBody, text, theme::kText1);
    if (keys)
    {
        RECT key = { row.left + pad, row.top, track.left - Scale(12), row.bottom };
        renderer.DrawTextLine(keys, Font::kMono, key, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // The switch: a violet track and a dark knob on the right when on, a grey track and a pale knob on the left
    // when off.
    renderer.FillRoundedRect(ToRectF(track), radius, on ? theme::kYuri : theme::kChip);
    const int knob = Scale(kKnob);
    const int inset = (Scale(kSwitchHeight) - knob) / 2;
    int knobLeft = on ? track.right - inset - knob : track.left + inset;
    RECT knobRect = { knobLeft, track.top + inset, knobLeft + knob, track.top + inset + knob };
    renderer.FillRoundedRect(ToRectF(knobRect), radius - 1.0f, on ? theme::kOnAccent : theme::kText3);
}

// The watched folders: each one's name and path, and a button to stop watching it; a note under them says what
// watching does.
void SettingsPage::PaintWatched(Renderer &renderer) const
{
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const int pad = Scale(kPanelPadding);
    RECT panel = WatchedPanelRect();
    RECT title = { panel.left, panel.top - Scale(kSectionTitle), panel.right, panel.top };
    renderer.DrawTextLine(strings::kSettingsWatched, Font::kCaptionStrong, title, theme::kText3);
    renderer.FillRoundedRect(ToRectF(panel), radius, theme::kControl);

    if (m_watched.empty())
    {
        RECT text = { panel.left + pad, panel.top, panel.right - pad, panel.bottom };
        renderer.DrawTextLine(strings::kNoWatchedFolders, Font::kBody, text, theme::kText3);
    }
    for (size_t i = 0; i < m_watched.size(); i++)
    {
        RECT row = WatchedRowRect(i);
        if (i > 0)
            renderer.FillRect(D2D1::RectF(static_cast<float>(row.left + pad), static_cast<float>(row.top),
                static_cast<float>(row.right - pad), static_cast<float>(row.top + 1)), theme::kStroke);

        RECT stop = StopButtonRect(i);
        float hover = m_hover.Level(4 + static_cast<int>(i));
        if (hover > 0.0f)
            renderer.FillRoundedRect(ToRectF(stop), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kHover, hover));
        renderer.DrawTextLine(strings::kStopWatching, Font::kCaption, stop, hover > 0.5f ? theme::kText1 : theme::kText2,
            DWRITE_TEXT_ALIGNMENT_CENTER);

        // The name stays whole as long as it can; the path gives way first.
        std::wstring name = LastName(m_watched[i]);
        int space = stop.left - Scale(kNameGapInRow) - (row.left + pad);
        int nameWidth = std::min(space, static_cast<int>(renderer.TextWidth(name, Font::kBody) + 0.99f));
        RECT nameRect = { row.left + pad, row.top, row.left + pad + nameWidth, row.bottom };
        renderer.DrawTextLine(name, Font::kBody, nameRect, theme::kText1);
        RECT pathRect = { nameRect.right + Scale(kNameGapInRow), row.top, stop.left - Scale(kNameGapInRow), row.bottom };
        if (pathRect.right - pathRect.left > Scale(24) && name != m_watched[i])
            renderer.DrawTextLine(m_watched[i], Font::kCaption, pathRect, theme::kText3);
    }

    ComPtr<IDWriteTextLayout> note = renderer.Layout(strings::kWatchedNote, Font::kCaption,
        static_cast<float>(panel.right - panel.left), 2);
    renderer.DrawLayout(note.Get(), static_cast<float>(panel.left), static_cast<float>(panel.bottom + Scale(kNoteGap)), theme::kText3);
}

// A thumb at the right edge of 'area', for content 'content' tall scrolled by 'scroll' of 'max'.
void SettingsPage::PaintScrollbar(Renderer &renderer, const RECT &area, int content, int scroll, int max) const
{
    int trackHeight = area.bottom - area.top - 2 * Scale(kScrollbarEdgeGap);
    int thumb = std::max(Scale(kScrollbarMinThumb), trackHeight * static_cast<int>(area.bottom - area.top) / std::max(1, content));
    int y = area.top + Scale(kScrollbarEdgeGap) + (trackHeight - thumb) * scroll / max;
    int x = area.right - Scale(kScrollbarEdgeGap) - Scale(kScrollbarWidth);
    RECT bar = { x, y, x + Scale(kScrollbarWidth), y + thumb };
    renderer.FillRoundedRect(ToRectF(bar), static_cast<float>(Scale(theme::kControlRadius)), theme::kScrollThumb);
}

// The size row: its name, the size in px, and a slider from the smallest tile to the largest.
void SettingsPage::PaintSizeRow(Renderer &renderer, const RECT &row, float hover) const
{
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const int pad = Scale(kPanelPadding);

    D2D1_RECT_F box = ToRectF(row);
    renderer.FillRoundedRect(box, radius, theme::kControl);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));

    RECT slider = SliderRect();
    RECT text = { row.left + pad, row.top, slider.left, row.bottom };
    renderer.DrawTextLine(strings::kThumbnailSize, Font::kBody, text, theme::kText1);
    RECT value = { row.left + pad, row.top, slider.left - Scale(12), row.bottom };
    renderer.DrawTextLine(std::to_wstring(m_tileSize), Font::kMono, value, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);

    const int knob = Scale(kSliderKnob);
    const int trackHeight = Scale(kSliderTrack);
    const int span = std::max(1, static_cast<int>(slider.right - slider.left) - knob);
    const int knobLeft = slider.left + span * (m_tileSize - FileGrid::kMinTile) / (FileGrid::kMaxTile - FileGrid::kMinTile);
    const int middle = (row.top + row.bottom) / 2;
    RECT track = { slider.left + knob / 2, middle - trackHeight / 2, slider.right - knob / 2, middle - trackHeight / 2 + trackHeight };
    renderer.FillRoundedRect(ToRectF(track), trackHeight / 2.0f, theme::kChip);
    RECT filled = track;
    filled.right = knobLeft + knob / 2;
    renderer.FillRoundedRect(ToRectF(filled), trackHeight / 2.0f, theme::kYuri);
    RECT knobRect = { knobLeft, middle - knob / 2, knobLeft + knob, middle - knob / 2 + knob };
    renderer.FillRoundedRect(ToRectF(knobRect), knob / 2.0f, theme::kText1);
}

void SettingsPage::PaintLicenses(Renderer &renderer) const
{
    const int left = ColumnLeft();
    const int width = ColumnWidth();

    RECT header = { left, m_bounds.top + Scale(kHeaderTop), left + width, m_bounds.top + Scale(kHeaderTop) + Scale(kButtonHeight) };
    renderer.DrawTextLine(strings::kLicenses, Font::kBodyStrong, header, theme::kText1);
    PaintHeaderButton(renderer, strings::kSettings);

    RECT list = ListRect();
    renderer.FillRect(D2D1::RectF(static_cast<float>(list.left), static_cast<float>(list.top - 1),
        static_cast<float>(list.right), static_cast<float>(list.top)), theme::kStroke);
    renderer.PushClip(list);
    for (const Line &line : m_lines)
    {
        int top = list.top + line.top - m_scroll;
        if (top + line.height < list.top)
            continue;
        if (top > list.bottom)
            break;
        RECT r = { left, top, left + width, top + line.height };
        switch (line.kind)
        {
        case Line::kIntro:
            renderer.DrawTextLine(line.text, Font::kBody, r, theme::kText2);
            break;
        case Line::kName:
            renderer.DrawTextLine(line.text, Font::kBodyStrong, r, theme::kText1);
            break;
        case Line::kSummary:
            r.bottom -= Scale(12);
            renderer.DrawTextLine(line.text, Font::kCaption, r, theme::kText3);
            break;
        case Line::kText:
            if (line.text.empty())
                break;
            if (line.height <= static_cast<int>(renderer.LineHeight(Font::kMono)))
                renderer.DrawTextLine(line.text, Font::kMono, r, theme::kText2);
            else
            {
                ComPtr<IDWriteTextLayout> layout = renderer.Layout(line.text, Font::kMono, static_cast<float>(width), 64,
                    DWRITE_TEXT_ALIGNMENT_LEADING, false);
                renderer.DrawLayout(layout.Get(), static_cast<float>(r.left), static_cast<float>(r.top), theme::kText2);
            }
            break;
        }
    }
    renderer.PopClip();

    // The scrollbar, when the texts are longer than the page.
    int max = MaxScroll();
    if (max > 0)
        PaintScrollbar(renderer, list, m_contentHeight, m_scroll, max);
}
