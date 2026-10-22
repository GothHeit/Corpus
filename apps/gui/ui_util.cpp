#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "ui_util.hpp"
#include "theme.hpp"
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
enum DWM_WINDOW_CORNER_PREFERENCE
{
    DWMWCP_DEFAULT = 0,
    DWMWCP_DONOTROUND = 1,
    DWMWCP_ROUND = 2,
    DWMWCP_ROUNDSMALL = 3
};
#endif

int ScaleForWindow(HWND hWnd, int value)
{
    UINT dpi = hWnd ? GetDpiForWindow(hWnd) : 0;
    if (dpi == 0)
        dpi = 96;
    return MulDiv(value, dpi, 96);
}

void SetRoundedCorners(HWND hWnd, bool rounded)
{
    DWM_WINDOW_CORNER_PREFERENCE preference = rounded ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

D2D1_RECT_F ToRectF(const RECT &rect)
{
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                       static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size);
    return out;
}

std::string WideToUtf8(const std::wstring &s)
{
    if (s.empty())
        return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size, nullptr, nullptr);
    return out;
}

bool LessNoCase(const std::wstring &a, const std::wstring &b)
{
    int order = CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.c_str(), static_cast<int>(a.size()),
        b.c_str(), static_cast<int>(b.size()), nullptr, nullptr, 0);
    // 0 means the compare failed: the names are then ordered as if equal, by the tie-break.
    if (order != 0 && order != CSTR_EQUAL)
        return order == CSTR_LESS_THAN;
    return a < b;
}

std::wstring FormatCount(size_t count)
{
    wchar_t separator[8] = L",";
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, 8) == 0)
        separator[0] = L',', separator[1] = L'\0';

    std::wstring digits = std::to_wstring(count);
    std::wstring out;
    for (size_t i = 0; i < digits.size(); i++)
    {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += separator;
        out += digits[i];
    }
    return out;
}

bool AnimationsEnabled()
{
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    return animations != FALSE;
}

D2D1_COLOR_F Faded(const D2D1_COLOR_F &color, float level)
{
    return { color.r, color.g, color.b, color.a * level };
}

D2D1_COLOR_F Mix(const D2D1_COLOR_F &from, const D2D1_COLOR_F &to, float level)
{
    return { from.r + (to.r - from.r) * level, from.g + (to.g - from.g) * level,
             from.b + (to.b - from.b) * level, from.a + (to.a - from.a) * level };
}

// ---- HoverFade ----

bool HoverFade::Set(HWND hWnd, UINT_PTR timer, int id)
{
    if (id == m_current)
        return false;

    // Each starts from where it is now, so moving back and forth never jumps. Only one fade-out is kept: crossing
    // three things within one fade drops the first at once, which is too quick to see.
    float currentLevel = Level(m_current);
    float newLevel = Level(id);
    m_previous = m_current;
    m_previousFrom = currentLevel;
    m_current = id;
    m_currentFrom = newLevel;
    m_start = GetTickCount64();
    m_animated = AnimationsEnabled();
    if (m_animated && hWnd)
        SetTimer(hWnd, timer, theme::kFrameMs, nullptr);
    return true;
}

void HoverFade::Reset(HWND hWnd, UINT_PTR timer)
{
    m_current = -1;
    m_previous = -1;
    if (hWnd)
        KillTimer(hWnd, timer);
}

void HoverFade::Renumber(HWND hWnd, UINT_PTR timer, int current, int previous)
{
    if (current < 0 && previous < 0)
    {
        Reset(hWnd, timer);
        return;
    }
    m_current = current;
    m_previous = previous;
}

void HoverFade::Step(HWND hWnd, UINT_PTR timer)
{
    if (Progress() >= 1.0f)
    {
        m_previous = -1;
        KillTimer(hWnd, timer);
    }
}

float HoverFade::Progress() const
{
    if (!m_animated)
        return 1.0f;
    float t = static_cast<float>(GetTickCount64() - m_start) / theme::kHoverFadeMs;
    return t < 1.0f ? t : 1.0f;
}

float HoverFade::Level(int id) const
{
    if (id < 0)
        return 0.0f;
    if (id == m_current)
        return m_currentFrom + (1.0f - m_currentFrom) * Progress();
    if (id == m_previous)
        return m_previousFrom * (1.0f - Progress());
    return 0.0f;
}
