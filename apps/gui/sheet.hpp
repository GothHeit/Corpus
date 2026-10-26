#ifndef SHEET_HPP
#define SHEET_HPP

#include <windows.h>
#include <string>
#include "ui_util.hpp"

class Renderer;

// What the sheets have in common: a card in the middle of the window over a dimmed copy of everything else. While
// one is open it takes the mouse and the keyboard (see Window::OpenSheet). Enter or the main button confirms; Esc,
// Cancel or a click outside the card cancels.
class Sheet
{
public:
    enum class Action { kNone, kCancel, kConfirm };

    virtual ~Sheet() = default;

    virtual bool IsOpen() const = 0;
    virtual bool IsFocused() const { return false; }
    virtual void Close() = 0;
    // The window got another size or scale.
    virtual void SetBounds(const RECT &bounds) = 0;
    virtual void Paint(Renderer &renderer) const = 0;
    virtual Action OnLButtonDown(POINT pt, bool doubleClick) = 0;
    virtual Action OnKeyDown(WPARAM key, bool ctrl) = 0;
    virtual void OnChar(wchar_t) {}
    virtual void OnMouseWheel(POINT, int) {}
    virtual void OnMouseMove(POINT) {}
    virtual void OnLButtonUp() {}
    virtual void OnCaptureChanged() {}
    virtual void OnHover(POINT pt) = 0;

protected:
    // The dimmed window and the card on it.
    static void PaintBackdrop(Renderer &renderer, const RECT &bounds);
    static void PaintCard(Renderer &renderer, HWND hWnd, const RECT &card);
    // A button of the card's bottom row: the main one in violet.
    static void PaintButton(Renderer &renderer, HWND hWnd, const RECT &rect, const wchar_t *label, bool main, bool hover);
    // The width of a button for 'label'.
    static int ButtonWidth(Renderer &renderer, HWND hWnd, const wchar_t *label);
    static constexpr int kButtonHeight = 28;
    static constexpr int kButtonGap = 8;
};

#endif
