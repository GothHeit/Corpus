#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "info_pane.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include <shellapi.h>
#include <algorithm>
#include <cstdio>

namespace
{
    const int kWidth = 280;
    const int kMinGridRoom = 240;     // the grid keeps at least this much when the window is narrow
    const int kPadding = 16;
    const int kHeaderHeight = 32;
    const int kCloseSize = 24;
    const int kFieldGap = 14;         // between one field and the next
    const int kChipHeight = 22;
    const int kChipGap = 6;
    const int kChipPadding = 8;
    const size_t kMaxStats = 500;     // more files than this and their sizes are not added up

    struct Stat
    {
        bool found = false;
        unsigned long long size = 0;
        FILETIME modified = {};
        FILETIME created = {};
    };

    Stat StatOf(const std::wstring &path)
    {
        Stat stat;
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            stat.found = true;
            stat.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            stat.modified = data.ftLastWriteTime;
            stat.created = data.ftCreationTime;
        }
        return stat;
    }

    std::wstring FormatSize(unsigned long long bytes)
    {
        const wchar_t *units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            unit++;
        }
        wchar_t text[48];
        if (unit == 0)
            swprintf(text, 48, L"%llu %ls", bytes, units[0]);
        else
            swprintf(text, 48, L"%.1f %ls", value, units[unit]);
        return text;
    }

    std::wstring FormatDate(const FILETIME &utc)
    {
        FILETIME local;
        SYSTEMTIME time;
        if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &time))
            return strings::kInfoUnknown;
        wchar_t date[64], clock[32];
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date, 64, nullptr) == 0 ||
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock, 32) == 0)
            return strings::kInfoUnknown;
        return std::wstring(date) + L"  " + clock;
    }

    // What Windows calls the type of the file ("JPEG image"), from its extension alone.
    std::wstring KindOf(const std::wstring &path)
    {
        SHFILEINFOW info = {};
        if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) &&
            info.szTypeName[0])
            return info.szTypeName;
        return strings::kInfoUnknown;
    }

    std::wstring FileName(const std::wstring &path)
    {
        size_t cut = path.find_last_of(L"\\/");
        return cut == std::wstring::npos ? path : path.substr(cut + 1);
    }
}

int InfoPane::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

int InfoPane::Width(HWND hWnd, int available) const
{
    if (!m_open)
        return 0;
    // A narrow window gives the pane what the grid can spare.
    int room = available - ScaleForWindow(hWnd, kMinGridRoom);
    return std::max(0, std::min(ScaleForWindow(hWnd, kWidth), room));
}

void InfoPane::SetOpen(bool open)
{
    m_open = open;
    m_closeHover.Reset(m_hWnd, kInfoHoverTimer);
}

void InfoPane::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_bounds = bounds;
}

void InfoPane::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void InfoPane::SetSelection(const std::vector<file*> &files)
{
    Details d;
    d.count = files.size();

    if (files.size() == 1)
    {
        d.path = Utf8ToWide(files[0]->get_path());
        d.name = FileName(d.path);
        d.kind = KindOf(d.path);
        Stat stat = StatOf(d.path);
        d.size = stat.found ? FormatSize(stat.size) : strings::kInfoUnknown;
        d.modified = stat.found ? FormatDate(stat.modified) : strings::kInfoUnknown;
        d.created = stat.found ? FormatDate(stat.created) : strings::kInfoUnknown;
        for (const tag *t : files[0]->get_tags())
            d.tags.push_back(Utf8ToWide(t->get_id()));
    }
    else if (files.size() > 1)
    {
        d.name = FormatCount(files.size()) + L" " + strings::kInfoFiles;
        if (files.size() <= kMaxStats)
        {
            unsigned long long total = 0;
            for (const file *f : files)
                total += StatOf(Utf8ToWide(f->get_path())).size;
            d.size = FormatSize(total);
        }
        // The tags every file has.
        for (const tag *t : files[0]->get_tags())
        {
            bool onAll = std::all_of(files.begin(), files.end(), [t](const file *f) { return f->tagged(t); });
            if (onAll)
                d.tags.push_back(Utf8ToWide(t->get_id()));
        }
    }

    std::sort(d.tags.begin(), d.tags.end(), LessNoCase);
    m_details = std::move(d);
    Invalidate();
}

RECT InfoPane::CloseRect() const
{
    int size = Scale(kCloseSize);
    int top = m_bounds.top + Scale(kPadding) / 2 + (Scale(kHeaderHeight) - size) / 2;
    return { m_bounds.right - Scale(kPadding) / 2 - size, top, m_bounds.right - Scale(kPadding) / 2, top + size };
}

bool InfoPane::OnLButtonDown(POINT pt, bool &close)
{
    close = false;
    if (!Contains(pt))
        return false;
    RECT button = CloseRect();
    close = PtInRect(&button, pt) != 0;
    return true;
}

void InfoPane::OnHover(POINT pt)
{
    RECT button = CloseRect();
    bool hover = m_open && PtInRect(&button, pt);
    if (m_closeHover.Set(m_hWnd, kInfoHoverTimer, hover ? 0 : -1))
        InvalidateRect(m_hWnd, &button, FALSE);
}

void InfoPane::OnTimer()
{
    m_closeHover.Step(m_hWnd, kInfoHoverTimer);
    RECT button = CloseRect();
    InvalidateRect(m_hWnd, &button, FALSE);
}

void InfoPane::Paint(Renderer &renderer) const
{
    if (!m_open || m_bounds.right <= m_bounds.left)
        return;

    renderer.FillRect(ToRectF(m_bounds), theme::kBase);
    renderer.FillRect({ static_cast<float>(m_bounds.left), static_cast<float>(m_bounds.top),
        static_cast<float>(m_bounds.left + 1), static_cast<float>(m_bounds.bottom) }, theme::kStroke);
    renderer.PushClip(m_bounds);

    const int pad = Scale(kPadding);
    const float x = static_cast<float>(m_bounds.left + pad);
    const float width = static_cast<float>(m_bounds.right - m_bounds.left - 2 * pad);
    float y = static_cast<float>(m_bounds.top + pad / 2);

    // Header: the title and the close button.
    RECT title = { m_bounds.left + pad, static_cast<LONG>(y), m_bounds.right - pad, static_cast<LONG>(y) + Scale(kHeaderHeight) };
    renderer.DrawTextLine(strings::kInfoTitle, Font::kBodyStrong, title, theme::kText1);
    RECT button = CloseRect();
    float level = m_closeHover.Level(0);
    if (level > 0.0f)
        renderer.FillRoundedRect(ToRectF(button), static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kHover, level));
    float cx = (button.left + button.right) / 2.0f, cy = (button.top + button.bottom) / 2.0f, r = Scale(4);
    renderer.DrawLine({ cx - r, cy - r }, { cx + r, cy + r }, theme::kText2, 1.4f);
    renderer.DrawLine({ cx - r, cy + r }, { cx + r, cy - r }, theme::kText2, 1.4f);
    y += Scale(kHeaderHeight) + Scale(kFieldGap) / 2;

    auto text = [&](const std::wstring &s, Font font, int lines, const D2D1_COLOR_F &color)
    {
        ComPtr<IDWriteTextLayout> layout = renderer.Layout(s, font, width, lines);
        if (!layout)
            return;
        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);
        renderer.DrawLayout(layout.Get(), x, y, color);
        y += metrics.height;
    };
    auto field = [&](const wchar_t *label, const std::wstring &value, Font font, int lines)
    {
        if (value.empty())
            return;
        text(label, Font::kCaption, 1, theme::kText3);
        y += Scale(2);
        text(value, font, lines, theme::kText1);
        y += Scale(kFieldGap);
    };

    if (m_details.count == 0)
    {
        text(strings::kInfoNothing, Font::kBody, 3, theme::kText3);
        renderer.PopClip();
        return;
    }

    text(m_details.name, Font::kBodyStrong, 3, theme::kText1);
    if (!m_details.kind.empty())
    {
        y += Scale(2);
        text(m_details.kind, Font::kCaption, 1, theme::kText3);
    }
    y += Scale(kFieldGap);

    field(strings::kInfoSize, m_details.size, Font::kBody, 1);
    field(strings::kInfoModified, m_details.modified, Font::kBody, 1);
    field(strings::kInfoCreated, m_details.created, Font::kBody, 1);
    field(strings::kInfoPath, m_details.path, Font::kMono, 6);

    // The tags, as chips that wrap onto more lines.
    text(strings::kInfoTags, Font::kCaption, 1, theme::kText3);
    y += Scale(4);
    if (m_details.tags.empty())
        text(strings::kInfoNoTags, Font::kBody, 1, theme::kText3);
    float chipX = x;
    const float chipHeight = static_cast<float>(Scale(kChipHeight));
    for (const std::wstring &name : m_details.tags)
    {
        float w = std::min(width, renderer.TextWidth(name, Font::kCaption) + 2.0f * Scale(kChipPadding));
        if (chipX > x && chipX + w > x + width)
        {
            chipX = x;
            y += chipHeight + Scale(kChipGap);
        }
        D2D1_RECT_F chip = { chipX, y, chipX + w, y + chipHeight };
        renderer.FillRoundedRect(chip, static_cast<float>(Scale(theme::kChipRadius)), theme::kChip);
        RECT label = { static_cast<LONG>(chipX) + Scale(kChipPadding), static_cast<LONG>(y),
            static_cast<LONG>(chipX + w) - Scale(kChipPadding), static_cast<LONG>(y + chipHeight) };
        renderer.DrawTextLine(name, Font::kCaption, label, theme::kText1);
        chipX += w + Scale(kChipGap);
    }

    renderer.PopClip();
}
