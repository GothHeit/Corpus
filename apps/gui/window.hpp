#ifndef WINDOWS_HPP
#define WINDOWS_HPP

#include <windows.h>
#include <shellapi.h>
#include "renderer.hpp"
#include "title_bar.hpp"
#include "text_field.hpp"
#include "file_grid.hpp"
#include "tag_panel.hpp"

class library;
class file;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// The app window. It owns the renderer and the components, splits the client area between them
// and forwards each Windows message to the component it concerns.
//
//   +--------------------------+
//   | o o o  Corpus            |  TitleBar (both rows)
//   |  [ SearchBar          ]  |
//   |--------------------------|
//   |  FileGrid                |
//   |--------------------------|
//   |  TagPanel                |
//   +--------------------------+
class Window
{
public:
    explicit Window(library &lib);
    ~Window();

    // Runs the message loop until the window is closed.
    void Run();

private:
    friend LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HitTestResizeBorder(LPARAM lParam) const;
    void LayoutComponents();
    void RefreshResults();
    void SelectionChanged();
    void OpenFiles(const std::vector<file*> &files);
    void OnDropFiles(HDROP drop);
    void ApplyTagEdit(const TagEdit &edit);
    void RemoveSelectedFiles();
    bool BeginChange();
    void FinishChange();
    void ReportSaveError();
    void OnPaint();
    void OnHover(POINT pt);
    int Scale(int value) const;

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    library &m_lib;
    Renderer m_renderer;
    bool m_trackingMouse = false;  // asked Windows for WM_MOUSELEAVE

    TitleBar m_titleBar;
    TextField m_searchBar;
    FileGrid m_fileGrid;
    TagPanel m_tagPanel;
};

#endif
