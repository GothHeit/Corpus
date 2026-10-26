#ifndef CONFIRM_SHEET_HPP
#define CONFIRM_SHEET_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "renderer.hpp"
#include "sheet.hpp"
#include "ui_util.hpp"

// The sheet that asks before a change: a card in the middle of the window, in place of a Windows message box, so
// there is no system sound and the window keeps running behind it.
//
//   +------------------------------------------+
//   |  Remove 3 files from the library?        |  the question
//   |  The files themselves stay where they    |  what it means, wrapped
//   |  are. Ctrl+Z puts them back.             |
//   |  holiday.jpg  ->  D:\Photos\holiday.jpg  |  details, one line each (optional)
//   |                     [Cancel] [Remove]    |
//   +------------------------------------------+
//
// Enter presses the focused button, which is the main one unless the change can't be taken back (then Cancel);
// Tab and the arrows move the focus between the two. Esc, Cancel or a click outside the card cancels. Such a
// change has its main button in rose.
class ConfirmSheet : public Sheet
{
public:
    struct Question
    {
        std::wstring title;
        std::wstring body;
        std::vector<std::wstring> details;
        std::wstring confirm;  // the main button's label
        bool danger = false;
    };

    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, Question question);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    void SetBounds(const RECT &bounds) override;

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnHover(POINT pt) override;

private:
    void Measure();
    RECT CardRect() const;
    RECT ConfirmRect() const;
    RECT CancelRect() const;
    int Scale(int value) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    int m_hover = -1;  // 0 Cancel, 1 the main button
    int m_focus = 1;   // the button Enter presses: 0 Cancel, 1 the main one
    Question m_question;
    ComPtr<IDWriteTextLayout> m_body;  // the body laid out at the card's width
    int m_bodyHeight = 0;
};

#endif
