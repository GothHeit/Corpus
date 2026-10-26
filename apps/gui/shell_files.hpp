#ifndef SHELL_FILES_HPP
#define SHELL_FILES_HPP

#include <windows.h>
#include <string>
#include <vector>

// Hands files of the library to other programs through the Windows shell, the way Explorer does: dragged out of the
// window, copied to the clipboard, or shown selected in an Explorer window. Paths are UTF-8, as the library has
// them. The files themselves are never moved: what leaves Corpus is a copy or a link.
namespace shell_files
{
    // Starts a drag and drop of the files to wherever the pointer drops them (Explorer, a chat, an editor) and
    // returns once they are dropped or the drag is cancelled. Call it with the left button down. Only copying and
    // linking are offered, so a drop never moves a file away from where the library has it. Returns false if no
    // file could be offered (none is on disk).
    bool DragOut(HWND hWnd, const std::vector<std::string> &paths);

    // Puts the files on the clipboard as Explorer's Copy does, for pasting in Explorer or a chat, with their paths as
    // text for pasting where files can't go. Returns false if nothing could be put there.
    bool CopyToClipboard(HWND hWnd, const std::vector<std::string> &paths);

    // The folders the files are in: one Explorer window to show each, see ShowInExplorer.
    size_t FolderCount(const std::vector<std::string> &paths);
    // Opens an Explorer window on each folder the files are in, with them selected. A file no longer there gets its
    // folder opened, if that is still there.
    void ShowInExplorer(const std::vector<std::string> &paths);
}

#endif
