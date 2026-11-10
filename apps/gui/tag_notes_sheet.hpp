#ifndef TAG_NOTES_SHEET_HPP
#define TAG_NOTES_SHEET_HPP

#include <windows.h>
#include <array>
#include <string>
#include "../../include/tag.hpp"
#include "sheet.hpp"
#include "text_field.hpp"
#include "ui_util.hpp"

class Renderer;

// The sheet that edits what a tag says about itself: a note and five fields (see tag_attrs). Opened from the
// sidebar's tag menu ("Notes...") and by a click on a note in the info pane.
//
//   +--------------------------------------+
//   |  Notes   ref/ysl-blood-luster        |
//   |  Note     [ the fang in the collar ] |
//   |  Creator  [ Saint Laurent          ] |
//   |  Year     [ 1971                   ] |
//   |  Link     [ https://...            ] |
//   |  Taken    [ the cut                ] |
//   |  Refused  [ the lacquer            ] |
//   |                    [Cancel] [Save]   |
//   +--------------------------------------+
//
// Every field is one line. Text with line breaks (the note of two merged tags) shows them as " / " and keeps them
// as they were unless the field is edited. Tab, Shift+Tab and Up/Down move between fields; Enter saves.
class TagNotesSheet : public Sheet
{
public:
    TagNotesSheet();

    // Opens the sheet over 'bounds' for the tag 'name' with its current text.
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::string &name, const tag_attrs &attrs);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    bool IsFocused() const override { return m_open && m_focus >= 0; }
    void SetBounds(const RECT &bounds) override;

    const std::string &TagName() const { return m_tag; }
    // The text in the fields, with the line breaks of untouched fields as they were.
    tag_attrs Attrs() const;

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    void OnMouseMove(POINT pt) override;
    void OnLButtonUp() override;
    void OnCaptureChanged() override;
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnChar(wchar_t ch) override;
    void OnHover(POINT pt) override;

private:
    static constexpr int kRows = 6;

    RECT CardRect() const;
    RECT FieldRect(int row) const;
    RECT ConfirmRect() const;
    RECT CancelRect() const;
    int Scale(int value) const;
    void Layout();
    void FocusRow(int row);

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    int m_hover = -1;   // 0 Cancel, 1 Save
    int m_focus = -1;   // the row with the cursor
    std::string m_tag;
    std::wstring m_title;
    std::array<TextField, kRows> m_fields;
    std::array<std::string, kRows> m_original;  // as stored, with line breaks
    std::array<std::wstring, kRows> m_shown;    // what the field was filled with
};

#endif
