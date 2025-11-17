#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif


#include "window.hpp"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_LBUTTONDOWN:
        ReleaseCapture();
        SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
};

Window::Window()
    : m_hInstance(GetModuleHandle(nullptr))
{
    const wchar_t* CLASS_NAME = L"Kafkas Window Class";

    WNDCLASS wndClass = {};
    wndClass.lpszClassName = CLASS_NAME;
    wndClass.hInstance = m_hInstance;
    wndClass.hIcon = LoadIcon(NULL, IDI_WINLOGO);
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.lpfnWndProc = WindowProc;

    HBRUSH grey59Brush = CreateSolidBrush(RGB(28, 28, 28));
    wndClass.hbrBackground = grey59Brush;

    RegisterClass(&wndClass);

    DWORD style = WS_POPUP;

    int width = 640;
    int height = 480;
    int x = 250;
    int y = 250;

    RECT rect;
    rect.left = 250;
    rect.top = 250;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;

    AdjustWindowRect(&rect, style, false);

    m_hWnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"TITLE",
        style,
        x,
        y,
        width,
        height,
        NULL,
        NULL,
        m_hInstance,
        NULL
    );
    BOOL enable = TRUE;
    
    DwmSetWindowAttribute(
        m_hWnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &enable,
        sizeof(enable)
    );

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);   

}

Window::~Window()
{
    const wchar_t* CLASS_NAME = L"Kafkas Window Class";
    
    UnregisterClass(CLASS_NAME, m_hInstance);

}

bool Window::ProcessMessages()
{
    MSG msg = {};

    while(PeekMessage(&msg, nullptr, 0u, 0u, PM_REMOVE))
    {
        if(msg.message == WM_QUIT)
        {
            return false;
        }
        

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}