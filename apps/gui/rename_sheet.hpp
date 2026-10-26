#ifndef RENAME_SHEET_HPP
#define RENAME_SHEET_HPP

#include <windows.h>
#include <string>
#include "sheet.hpp"
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
class RenameSheet : public Sheet
{
public:
    RenameSheet() : m_field(L"") { m_field.EnableDrag(); }

    // Opens the sheet over 'bounds' (the client area) with 'name' typed in. A file's name is selected without its
    // extension; with 'selectAll' (a tag's) the whole name is. The title and the main button default to
    // "Rename".
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::wstring &name, bool selectAll = false,
        const wchar_t *title = nullptr);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    bool IsFocused() const override { return m_open && m_field.IsFocused(); }
    void SetBounds(const RECT &bounds) override;

    const std::wstring &Name() const { return m_field.Query(); }
    // Tells why the name can't be used; an empty message clears it.
    void SetError(const std::wstring &message) { m_field.SetError(message); }

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    void OnMouseMove(POINT pt) override { m_field.OnMouseMove(pt); }
    void OnLButtonUp() override { m_field.OnLButtonUp(); }
    void OnCaptureChanged() override { m_field.OnCaptureChanged(); }
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnChar(wchar_t ch) override;
    void OnHover(POINT pt) override;

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
    const wchar_t *m_title = nullptr;
    TextField m_field;
};

#endif
