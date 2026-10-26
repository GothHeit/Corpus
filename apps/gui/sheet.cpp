#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "sheet.hpp"
#include "renderer.hpp"
#include "theme.hpp"

namespace
{
    const int kButtonPadding = 16;
    const int kRadius = 6;
}

void Sheet::PaintBackdrop(Renderer &renderer, const RECT &bounds)
{
    renderer.FillRect(ToRectF(bounds), theme::Rgb(0x000000, 0.55f));
}

void Sheet::PaintCard(Renderer &renderer, HWND hWnd, const RECT &card)
{
    D2D1_RECT_F body = ToRectF(card);
    float radius = static_cast<float>(ScaleForWindow(hWnd, kRadius));
    renderer.FillRoundedRect({ body.left - 1, body.top + 2, body.right + 1, body.bottom + 6 }, radius + 2, theme::Rgb(0x000000, 0.20f));
    renderer.FillRoundedRect(body, radius, theme::kBase);
    renderer.DrawRoundedRect(body, radius, theme::White(0.10f), 1.0f);
}

void Sheet::PaintButton(Renderer &renderer, HWND hWnd, const RECT &rect, const wchar_t *label, bool main, bool hover)
{
    const float radius = static_cast<float>(ScaleForWindow(hWnd, theme::kChipRadius));
    D2D1_RECT_F box = ToRectF(rect);
    if (main)
    {
        renderer.FillRoundedRect(box, radius, hover ? Mix(theme::kYuri, theme::White(1.0f), 0.12f) : theme::kYuri);
        renderer.DrawTextLine(label, Font::kBody, rect, theme::kOnAccent, DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    renderer.FillRoundedRect(box, radius, theme::kControl);
    if (hover)
        renderer.FillRoundedRect(box, radius, theme::kHover);
    renderer.DrawTextLine(label, Font::kBody, rect, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
}

int Sheet::ButtonWidth(Renderer &renderer, HWND hWnd, const wchar_t *label)
{
    return static_cast<int>(renderer.TextWidth(label, Font::kBody) + 0.99f) + 2 * ScaleForWindow(hWnd, kButtonPadding);
}
