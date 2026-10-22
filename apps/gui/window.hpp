#ifndef WINDOWS_HPP
#define WINDOWS_HPP

#include <windows.h>
#include "title_bar.hpp"
#include "search_bar.hpp"
#include "file_grid.hpp"

class library;
class file;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// The app window. It owns the components, splits the client area between them
// and forwards each Windows message to the component it concerns.
//
//   +--------------------------+
//   | TitleBar                 |
//   |  [ SearchBar          ]  |
//   |  FileGrid                |
//   +--------------------------+
class Window
{
public:
    explicit Window(const library &lib);
    ~Window();

    // Runs the message loop until the window is closed.
    void Run();

private:
    friend LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HitTestResizeBorder(LPARAM lParam) const;
    void LayoutComponents();
    void RefreshResults();
    void OpenFiles(const std::vector<const file*> &files);
    void OnPaint();
    int Scale(int value) const;

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    const library &m_lib;
    ULONG_PTR m_gdiplusToken;

    TitleBar m_titleBar;
    SearchBar m_searchBar;
    FileGrid m_fileGrid;
};

#endif
