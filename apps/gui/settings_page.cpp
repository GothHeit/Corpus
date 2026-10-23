#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "settings_page.hpp"
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
    const int kAboutHeight = 76;
    const int kNameClicks = 5;      // in a row, to open the licenses
    const ULONGLONG kNameClickMs = 3000;
    const int kWheelStep = 60;      // pixels per notch of the wheel
    const int kNameGap = 28;        // above the name of a license
    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 4;
    const int kScrollbarMinThumb = 24;

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
    int top = m_bounds.top + Scale(kHeaderTop);
    RECT r = { right - width, top, right, top + Scale(kButtonHeight) };
    return r;
}

RECT SettingsPage::SidebarRowRect() const
{
    int top = m_bounds.top + Scale(kHeaderTop) + Scale(kButtonHeight) + Scale(kSectionGap) + Scale(kSectionTitle);
    RECT r = { ColumnLeft(), top, ColumnLeft() + ColumnWidth(), top + Scale(theme::kSettingsRow) };
    return r;
}

RECT SettingsPage::AboutRect() const
{
    int top = SidebarRowRect().bottom + Scale(kSectionGap) + Scale(kSectionTitle);
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

void SettingsPage::OnMouseWheel(int delta)
{
    if (m_licenses)
        ScrollTo(m_scroll - delta * Scale(kWheelStep) / WHEEL_DELTA);
}

void SettingsPage::OnHover(POINT pt)
{
    if (!m_open)
        return;
    RECT button = HeaderButtonRect();
    RECT row = SidebarRowRect();
    int id = -1;
    if (PtInRect(&button, pt))
        id = 0;
    else if (!m_licenses && PtInRect(&row, pt))
        id = 1;
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
        PaintSettings(renderer);
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

    RECT header = { left, m_bounds.top + Scale(kHeaderTop), right, m_bounds.top + Scale(kHeaderTop) + Scale(kButtonHeight) };
    renderer.DrawTextLine(strings::kSettings, Font::kBodyStrong, header, theme::kText1);
    PaintHeaderButton(renderer, strings::kSettingsDone);

    // Sidebar.
    RECT row = SidebarRowRect();
    RECT title = { left, row.top - Scale(kSectionTitle), right, row.top };
    renderer.DrawTextLine(strings::kSettingsSidebar, Font::kCaptionStrong, title, theme::kText3);

    D2D1_RECT_F box = ToRectF(row);
    renderer.FillRoundedRect(box, radius, theme::kControl);
    float hover = m_hover.Level(1);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));

    const int pad = Scale(kPanelPadding);
    RECT track = { row.right - pad - Scale(kSwitchWidth), (row.top + row.bottom - Scale(kSwitchHeight)) / 2, 0, 0 };
    track.right = track.left + Scale(kSwitchWidth);
    track.bottom = track.top + Scale(kSwitchHeight);
    RECT label = { row.left + pad, row.top, track.left, row.bottom };
    renderer.DrawTextLine(strings::kShowSidebar, Font::kBody, label, theme::kText1);
    RECT key = { row.left + pad, row.top, track.left - Scale(12), row.bottom };
    renderer.DrawTextLine(strings::kShowSidebarKey, Font::kMono, key, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);

    // The switch: a violet track and a dark knob on the right when on, a grey track and a pale knob on the left
    // when off.
    renderer.FillRoundedRect(ToRectF(track), radius, m_sidebarShown ? theme::kYuri : theme::kChip);
    const int knob = Scale(kKnob);
    const int inset = (Scale(kSwitchHeight) - knob) / 2;
    int knobLeft = m_sidebarShown ? track.right - inset - knob : track.left + inset;
    RECT knobRect = { knobLeft, track.top + inset, knobLeft + knob, track.top + inset + knob };
    renderer.FillRoundedRect(ToRectF(knobRect), radius - 1.0f, m_sidebarShown ? theme::kOnAccent : theme::kText3);

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
    {
        int trackHeight = list.bottom - list.top - 2 * Scale(kScrollbarEdgeGap);
        int thumb = std::max(Scale(kScrollbarMinThumb), trackHeight * static_cast<int>(list.bottom - list.top) / m_contentHeight);
        int y = list.top + Scale(kScrollbarEdgeGap) + (trackHeight - thumb) * m_scroll / max;
        int x = list.right - Scale(kScrollbarEdgeGap) - Scale(kScrollbarWidth);
        RECT bar = { x, y, x + Scale(kScrollbarWidth), y + thumb };
        renderer.FillRoundedRect(ToRectF(bar), static_cast<float>(Scale(theme::kControlRadius)), theme::kScrollThumb);
    }
}
