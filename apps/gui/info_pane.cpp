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
#include "tag_panel.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include <shellapi.h>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <thread>

namespace
{
    const int kWidth = 280;
    const int kMinWidth = 200;        // the pane never gets narrower, even if the grid then has less room
    const int kMinGridRoom = 240;     // the grid keeps at least this much when the window is narrow
    const int kWheelStep = 48;        // pixels scrolled by one notch of the wheel
    const int kScrollbarWidth = 4;
    const int kScrollbarEdgeGap = 3;
    const int kScrollbarMinThumb = 24;
    const int kPadding = 16;
    const int kHeaderHeight = 32;
    const int kCloseSize = 24;
    const int kFieldGap = 14;         // between one field and the next
    const int kChipHeight = 22;
    const int kChipGap = 6;
    const int kChipPadding = 8;
    const wchar_t *const kPending = L"\u2026";  // until the disk answers
    const size_t kMaxStats = 500;     // more files than this and their sizes are not added up

    InfoPane::Stat StatOf(const std::wstring &path)
    {
        InfoPane::Stat stat;
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

InfoPane::~InfoPane()
{
    if (!m_running)
        return;
    std::lock_guard<std::mutex> lock(m_running->mutex);
    m_running->hWnd = nullptr;
}

// Reads the disk for the selection on a thread; a read asked for while another runs waits for it and then runs on
// the paths of the latest request only.
void InfoPane::Request(const std::vector<std::string> &paths)
{
    if (m_running)
    {
        m_queued = true;
        m_queuedPaths = paths;
        return;
    }
    Launch(paths);
}

void InfoPane::Launch(std::vector<std::string> paths)
{
    m_running = std::make_shared<Job>();
    m_running->hWnd = m_hWnd;
    m_running->serial = m_serial;
    m_running->single = m_details.count == 1;
    // Detached: a path on a network drive that doesn't answer can hold it for the network's timeout, and the
    // window must still close meanwhile. It only touches its own copy of the paths and the Job it keeps alive.
    std::thread(Run, m_running, std::move(paths)).detach();
}

void InfoPane::Run(std::shared_ptr<Job> job, std::vector<std::string> paths)
{
    Stat stat;
    unsigned long long total = 0;
    if (job->single)
        stat = StatOf(Utf8ToWide(paths[0]));
    else
    {
        for (const std::string &path : paths)
            total += StatOf(Utf8ToWide(path)).size;
    }

    std::lock_guard<std::mutex> lock(job->mutex);
    job->stat = stat;
    job->total = total;
    job->done = true;
    if (job->hWnd)
        PostMessage(job->hWnd, kInfoStats, 0, 0);
}

void InfoPane::OnStats()
{
    if (!m_running)
        return;
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(m_running->mutex);
        if (!m_running->done)
            return;
        job = m_running;
    }
    m_running.reset();

    if (job->serial == m_serial && m_details.count > 0)
    {
        if (job->single)
        {
            m_details.size = job->stat.found ? FormatSize(job->stat.size) : strings::kInfoUnknown;
            m_details.modified = job->stat.found ? FormatDate(job->stat.modified) : strings::kInfoUnknown;
            m_details.created = job->stat.found ? FormatDate(job->stat.created) : strings::kInfoUnknown;
        }
        else
            m_details.size = FormatSize(job->total);
        Invalidate();
    }
    if (m_queued)
    {
        m_queued = false;
        Launch(std::move(m_queuedPaths));
        m_queuedPaths.clear();
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
    // A narrow window gives the pane what the grid can spare, but not less than its minimum.
    int room = available - ScaleForWindow(hWnd, kMinGridRoom);
    int width = std::min(ScaleForWindow(hWnd, kWidth), std::max(room, ScaleForWindow(hWnd, kMinWidth)));
    return std::max(0, std::min(width, available));
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
    std::vector<std::string> paths;  // what to read from the disk
    bool sameFile = false;           // the one file shown before, whose read is still the right one

    if (files.size() == 1)
    {
        d.path = Utf8ToWide(files[0]->get_path());
        d.name = FileName(d.path);
        d.kind = KindOf(d.path);
        // The disk is read on a thread; until it answers the fields keep what they showed for this very file.
        bool same = m_details.count == 1 && m_details.path == d.path;
        d.size = same ? m_details.size : kPending;
        d.modified = same ? m_details.modified : kPending;
        d.created = same ? m_details.created : kPending;
        sameFile = same;
        if (!same)
            paths.push_back(files[0]->get_path());
        for (const tag *t : files[0]->get_tags())
            d.tags.push_back({ Utf8ToWide(t->get_id()), std::wstring() });
    }
    else if (files.size() > 1)
    {
        d.name = FormatCount(files.size()) + L" " + strings::kInfoFiles;
        if (files.size() <= kMaxStats)
        {
            d.size = m_details.count > 1 ? m_details.size : kPending;
            for (const file *f : files)
                paths.push_back(f->get_path());
        }
        // How many of the files have each tag: the ones every file has are plain chips, the rest say "2/5".
        std::unordered_map<const tag*, size_t> counts;
        for (const file *f : files)
        {
            for (const tag *t : f->get_tags())
                counts[t]++;
        }
        for (const auto &entry : counts)
        {
            std::wstring count = entry.second < files.size() ?
                std::to_wstring(entry.second) + L"/" + std::to_wstring(files.size()) : std::wstring();
            d.tags.push_back({ Utf8ToWide(entry.first->get_id()), count });
        }
    }

    std::sort(d.tags.begin(), d.tags.end(), [](const Details::Tag &a, const Details::Tag &b)
    {
        if (a.count.empty() != b.count.empty())
            return a.count.empty();
        return LessNoCase(a.name, b.name);
    });
    // A different selection starts at the top; the same file with new tags stays where it was.
    if (d.count != m_details.count || d.path != m_details.path)
        m_scrollY = 0;
    m_details = std::move(d);
    // A read under way for another selection is stale from here on.
    if (!sameFile)
        m_serial++;
    if (!paths.empty())
        Request(paths);
    Invalidate();
}

RECT InfoPane::CloseRect() const
{
    int size = Scale(kCloseSize);
    int top = m_bounds.top + Scale(kPadding) / 2 + (Scale(kHeaderHeight) - size) / 2;
    return { m_bounds.right - Scale(kPadding) / 2 - size, top, m_bounds.right - Scale(kPadding) / 2, top + size };
}

// Under the header: where the details scroll.
RECT InfoPane::BodyRect() const
{
    int top = m_bounds.top + Scale(kPadding) / 2 + Scale(kHeaderHeight);
    RECT r = { m_bounds.left, std::min(top, static_cast<int>(m_bounds.bottom)), m_bounds.right, m_bounds.bottom };
    return r;
}

bool InfoPane::OnMouseWheel(POINT pt, int delta)
{
    if (!Contains(pt))
        return false;
    RECT body = BodyRect();
    int max = std::max(0, m_contentHeight - static_cast<int>(body.bottom - body.top));
    int scrolled = std::max(0, std::min(max, m_scrollY - MulDiv(delta, Scale(kWheelStep), WHEEL_DELTA)));
    if (scrolled != m_scrollY)
    {
        m_scrollY = scrolled;
        Invalidate();
    }
    return true;
}

bool InfoPane::OnLButtonDown(POINT pt, bool &close, bool &addTag)
{
    close = addTag = false;
    if (!Contains(pt))
        return false;
    RECT button = CloseRect();
    close = PtInRect(&button, pt) != 0;
    RECT body = BodyRect();
    addTag = PtInRect(&m_addRect, pt) != 0 && PtInRect(&body, pt) != 0;
    return true;
}

void InfoPane::OnHover(POINT pt)
{
    RECT button = CloseRect();
    int id = -1;
    if (m_open && PtInRect(&button, pt))
        id = 0;
    else if (m_open && PtInRect(&m_addRect, pt))
    {
        RECT body = BodyRect();
        if (PtInRect(&body, pt))
            id = 1;
    }
    if (m_closeHover.Set(m_hWnd, kInfoHoverTimer, id))
        Invalidate();
}

bool InfoPane::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    anchor = CloseRect();
    text = strings::kTipInfoClose;
    if (m_open && PtInRect(&anchor, pt))
        return true;
    RECT body = BodyRect();
    if (!m_open || !PtInRect(&body, pt))
        return false;
    for (const ChipTip &tip : m_chipTips)
    {
        if (PtInRect(&tip.rect, pt))
        {
            anchor = tip.rect;
            text = tip.path.c_str();
            return true;
        }
    }
    return false;
}

void InfoPane::OnTimer()
{
    m_closeHover.Step(m_hWnd, kInfoHoverTimer);
    Invalidate();
}

void InfoPane::Paint(Renderer &renderer) const
{
    m_addRect = {};
    m_chipTips.clear();
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
    y += Scale(kHeaderHeight);

    // The body scrolls under the header: what goes past its top is cut off there.
    RECT body = BodyRect();
    renderer.PushClip(body);
    y += Scale(kFieldGap) / 2 - m_scrollY;

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
        m_contentHeight = 0;
        renderer.PopClip();
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
    field(strings::kInfoModified, m_details.modified, Font::kBody, 2);
    field(strings::kInfoCreated, m_details.created, Font::kBody, 2);
    field(strings::kInfoPath, m_details.path, Font::kMono, 6);

    // The tags, as chips that wrap onto more lines.
    text(strings::kInfoTags, Font::kCaption, 1, theme::kText3);
    y += Scale(4);
    if (m_details.tags.empty())
        text(strings::kInfoNoTags, Font::kBody, 1, theme::kText3);
    float chipX = x;
    const float chipHeight = static_cast<float>(Scale(kChipHeight));
    for (const Details::Tag &entry : m_details.tags)
    {
        // Rounded up: the label rectangle is whole pixels, and a text that is a fraction too wide gets an ellipsis.
        float countWidth = entry.count.empty() ? 0.0f : std::ceil(renderer.TextWidth(entry.count, Font::kMono)) + Scale(4);
        // Only the last part of the path shows ("Rock" for "Music/Rock"); the tooltip has all of it.
        const std::wstring name = ShortTagPath(renderer, LeafTagName(entry.name), Font::kCaption, width - countWidth - 2.0f * Scale(kChipPadding));
        float nameWidth = std::ceil(renderer.TextWidth(name, Font::kCaption));
        float w = std::min(width, nameWidth + countWidth + 2.0f * Scale(kChipPadding));
        if (chipX > x && chipX + w > x + width)
        {
            chipX = x;
            y += chipHeight + Scale(kChipGap);
        }
        D2D1_RECT_F chip = { chipX, y, chipX + w, y + chipHeight };
        const float chipRadius = static_cast<float>(Scale(theme::kChipRadius));
        if (entry.count.empty())
            renderer.FillRoundedRect(chip, chipRadius, theme::kChip);
        else
        {
            // On only some of the selected files: a dashed outline, like the strip's.
            float line = Scale(4) / 4.0f;
            renderer.DrawRoundedRect({ chip.left + line / 2, chip.top + line / 2, chip.right - line / 2, chip.bottom - line / 2 },
                chipRadius, theme::kText3, line, true);
        }
        if (name != entry.name)
        {
            RECT rect = { static_cast<LONG>(chip.left), static_cast<LONG>(chip.top), static_cast<LONG>(chip.right), static_cast<LONG>(chip.bottom) };
            m_chipTips.push_back({ rect, entry.name });
        }
        RECT label = { static_cast<LONG>(chipX) + Scale(kChipPadding), static_cast<LONG>(y),
            static_cast<LONG>(chipX + w) - Scale(kChipPadding), static_cast<LONG>(y + chipHeight) };
        renderer.DrawTextLine(name, Font::kCaption, label, theme::kText1);
        if (!entry.count.empty())
        {
            label.left += static_cast<LONG>(nameWidth) + Scale(4);
            renderer.DrawTextLine(entry.count, Font::kMono, label, theme::kText3);
        }
        chipX += w + Scale(kChipGap);
    }

    // The Add tag button, on its own line under the chips: it opens the tag sheet, which adds to the selected files.
    y += chipHeight + Scale(kChipGap) + Scale(kChipGap);
    int buttonWidth = static_cast<int>(renderer.TextWidth(strings::kInfoAddTag, Font::kCaption) + 0.99f) + 2 * Scale(kChipPadding);
    m_addRect = { static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x) + buttonWidth, static_cast<LONG>(y) + Scale(kChipHeight) + Scale(4) };
    D2D1_RECT_F box = ToRectF(m_addRect);
    const float chipRadius = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(box, chipRadius, theme::kControl);
    float addLevel = m_closeHover.Level(1);
    if (addLevel > 0.0f)
        renderer.FillRoundedRect(box, chipRadius, Faded(theme::kHover, addLevel));
    renderer.DrawTextLine(strings::kInfoAddTag, Font::kCaption, m_addRect, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);

    // What was laid out, without the scrolling, and a thumb on the right when it does not fit.
    int content = m_addRect.bottom + m_scrollY - body.top + pad;
    m_contentHeight = content;
    int view = body.bottom - body.top;
    int max = content - view;
    if (max <= 0 || view <= 0)
    {
        if (m_scrollY != 0)
        {
            m_scrollY = 0;
            Invalidate();
        }
    }
    else
    {
        if (m_scrollY > max)
        {
            m_scrollY = max;
            Invalidate();
        }
        int track = view - Scale(kScrollbarEdgeGap);
        int thumbHeight = std::min(track, std::max(Scale(kScrollbarMinThumb), MulDiv(track, view, content)));
        int right = m_bounds.right - Scale(kScrollbarEdgeGap);
        int top = body.top + MulDiv(track - thumbHeight, m_scrollY, max);
        RECT thumb = { right - Scale(kScrollbarWidth), top, right, top + thumbHeight };
        renderer.FillRoundedRect(ToRectF(thumb), static_cast<float>(Scale(theme::kControlRadius)), theme::kScrollThumb);
    }

    renderer.PopClip();
    renderer.PopClip();
}
