#ifndef CONFIRM_SHEET_HPP
#define CONFIRM_SHEET_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "renderer.hpp"
#include "sheet.hpp"
#include "ui_util.hpp"

// The sheet that asks before a change, in place of a message box: no system sound, and the window keeps running.
//
//   +------------------------------------------+
//   |  Remove 3 files from the library?        |  the question
//   |  The files themselves stay where they    |  what it means, wrapped
//   |  are. Ctrl+Z puts them back.             |
//   |  holiday.jpg  ->  D:\Photos\holiday.jpg  |  details, one line each (optional)
//   |                     [Cancel] [Remove]    |
//   +------------------------------------------+
//
// Enter presses the focused button: the main one, or Cancel when the change can't be taken back (that main
// button is rose). With an alternate, Tab and the arrows go Cancel, alternate, main.
class ConfirmSheet : public Sheet
{
public:
    struct Question
    {
        std::wstring title;
        std::wstring body;
        std::vector<std::wstring> details;
        std::wstring confirm;  // the main button's label
        std::wstring alternate;  // a second way to go on (optional): its button sits between Cancel and the main one
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
    RECT AlternateRect() const;
    RECT CancelRect() const;
    int Scale(int value) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    int m_hover = -1;  // 0 Cancel, 1 the main button, 2 the alternate
    int m_focus = 1;   // the button Enter presses: 0 Cancel, 1 the main one, 2 the alternate
    Question m_question;
    ComPtr<IDWriteTextLayout> m_body;  // the body laid out at the card's width
    int m_bodyHeight = 0;
};

#endif
