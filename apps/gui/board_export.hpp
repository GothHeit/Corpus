#ifndef BOARD_EXPORT_HPP
#define BOARD_EXPORT_HPP

#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "board_view.hpp"

// Posted to the window when an export ended: wParam 1 if the PNG was written, 0 if not.
const UINT kBoardExported = WM_APP + 16;

// Draws a board to a PNG on a thread of its own: the files at the sizes and places they have on the canvas, on the
// canvas's own background, 2x the canvas units (less for a very big board, at most 8192 px a side). Pictures are
// decoded again at the size they are drawn, color managed to sRGB, so the export is sharper than the canvas;
// anything else uses the thumbnail Windows has for it. Only the window's thread calls these.
class BoardExporter
{
public:
    ~BoardExporter() { Stop(); }
    bool Running() const { return m_running; }
    // Starts writing 'target'. False if one is running or there is nothing to draw.
    bool Start(HWND hWnd, const std::wstring &target, std::vector<BoardExportItem> items);
    // Waits for the thread (a few seconds at most for a big board) and ends it.
    void Stop();

private:
    static void Run(HWND hWnd, std::wstring target, std::vector<BoardExportItem> items, std::shared_ptr<std::atomic<bool>> cancel,
        std::atomic<bool> *running);

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};

#endif
