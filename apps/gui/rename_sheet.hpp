#ifndef RENAME_SHEET_HPP
#define RENAME_SHEET_HPP

#include <windows.h>
#include <string>
#include "text_field.hpp"
#include "ui_util.hpp"

class Renderer;

// The small sheet that asks for a file's new name: a card in the middle of the window over a dimmed copy of
// everything else.
//
//   +----------------------------------+
//   |  Rename                          |
//   |  [ holiday.jpg                 ] |
//   |                  [Cancel] [Rename] |
//   +----------------------------------+
//
// While it is open it takes the mouse and the keyboard: Enter or "Rename" confirms, Esc, "Cancel" or a click
// outside the card cancels. A message from the window about a name that can't be used shows under the field.
class RenameSheet
{
public:
    enum class Action { kNone, kCancel, kConfirm };

    RenameSheet() : m_field(L"") { m_field.EnableDrag(); }

    // Opens the sheet over 'bounds' (the client area) with 'name' typed in.
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::wstring &name);
    void Close();
    bool IsOpen() const { return m_open; }
    bool IsFocused() const { return m_open && m_field.IsFocused(); }
    // The window got another size or scale.
    void SetBounds(const RECT &bounds);

    const std::wstring &Name() const { return m_field.Query(); }
    // Tells why the name can't be used; an empty message clears it.
    void SetError(const std::wstring &message) { m_field.SetError(message); }

    void Paint(Renderer &renderer) const;
    Action OnLButtonDown(POINT pt, bool doubleClick);
    void OnMouseMove(POINT pt) { m_field.OnMouseMove(pt); }
    void OnLButtonUp() { m_field.OnLButtonUp(); }
    void OnCaptureChanged() { m_field.OnCaptureChanged(); }
    Action OnKeyDown(WPARAM key, bool ctrl);
    void OnChar(wchar_t ch);
    void OnHover(POINT pt);

private:
    RECT CardRect() const;
    RECT FieldRect() const;
    RECT ConfirmRect() const;
    RECT CancelRect() const;
    int Scale(int value) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    int m_hover = -1;  // 0 Cancel, 1 Rename
    TextField m_field;
};

#endif
