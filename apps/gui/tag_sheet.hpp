#ifndef TAG_SHEET_HPP
#define TAG_SHEET_HPP

#include <windows.h>
#include <string>
#include <unordered_set>
#include <vector>
#include "sheet.hpp"
#include "text_field.hpp"
#include "ui_util.hpp"

class file;
class tag;
class library;
class Renderer;

// The sheet that adds tags to the selected files. Ctrl+T, the menu's Add tag, the info pane's "+ Add tag" and the
// strip's all open it:
//
//   +----------------------------------+
//   |  Add tag                         |
//   |  12 files                        |
//   |  [ anime wall|paper            ] |
//   |  Matching tags                   |
//   |   wallpaper                   31 |
//   |   wallpapers_4k                4 |
//   |                  [Cancel] [Add]  |
//   +----------------------------------+
//
// Tags are typed separated by spaces. The word being typed is completed from the tags of the library, the most used
// first: Tab or a click on a row accepts, Up and Down pick another one. Before anything is typed the rows are the
// busiest tags the selected files don't all have. Enter or "Add" confirms.
class TagSheet : public Sheet
{
public:
    explicit TagSheet(const library &lib) : m_lib(lib), m_field(L"") { m_field.EnableDrag(); }

    // Opens the sheet over 'bounds' (the client area) to tag these files.
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, const std::vector<file*> &files);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    bool IsFocused() const override { return m_open && m_field.IsFocused(); }
    void SetBounds(const RECT &bounds) override;

    // The tags typed, as the library names them: a word that differs from a tag only in case means that tag.
    // Empty when there is nothing usable in the field.
    std::vector<std::string> Tags() const;

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    void OnMouseMove(POINT pt) override { m_field.OnMouseMove(pt); }
    void OnLButtonUp() override { m_field.OnLButtonUp(); }
    void OnCaptureChanged() override { m_field.OnCaptureChanged(); }
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnChar(wchar_t ch) override;
    void OnHover(POINT pt) override;

private:
    struct Row
    {
        std::wstring name;
        size_t files;  // how many files of the library have it
    };

    static constexpr int kRows = 5;

    RECT CardRect() const;
    RECT FieldRect() const;
    RECT RowRect(int index) const;
    RECT ConfirmRect() const;
    RECT CancelRect() const;
    int RowAt(POINT pt) const;
    int Scale(int value) const;

    std::wstring CurrentWord() const;
    void UpdateRows();
    void AcceptRow(size_t index);
    void Changed();
    Action Confirm();

    const library &m_lib;
    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    TextField m_field;
    std::wstring m_subtitle;
    std::unordered_set<const tag*> m_onEveryFile;  // the tags every selected file has: adding them changes nothing

    std::vector<Row> m_rows;  // the suggestions, best first
    bool m_matching = false;  // a word is being typed (else the rows are the busiest tags)
    int m_pick = -1;          // the row Tab takes, -1 for none
    int m_hover = -1;         // a row, or kRows for Cancel, kRows + 1 for Add
};

#endif
