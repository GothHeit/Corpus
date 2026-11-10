#ifndef INFO_PANE_HPP
#define INFO_PANE_HPP

#include <windows.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include "ui_util.hpp"

class file;
class Renderer;

// Posted to the window when the sizes and dates the pane asked for are read; the window then calls OnStats.
const UINT kInfoStats = WM_APP + 8;

// Posted when the line of a book's text that matched the search is cut out of the book; the window then calls OnSnippet.
const UINT kInfoSnippet = WM_APP + 15;

// The pane on the right that tells about the selected files, opened from the context menu or with Ctrl+I:
//
//   Info                        x
//   photo.jpg
//   JPEG file
//   Size      2.4 MB
//   Modified  12 Oct 2026
//   Created   3 Jan 2026
//   Path      C:\Pictures\photo.jpg
//   Tags      [anime] [4k]
//             [+ Add tag]
//
// With several files it shows their count, total size and shared tags. Size and dates are read on a thread of
// their own so a network drive that doesn't answer can't freeze the window; an ellipsis shows until they arrive.
class InfoPane
{
public:
    // What the disk says about a file.
    struct Stat
    {
        bool found = false;
        unsigned long long size = 0;
        FILETIME modified = {};
        FILETIME created = {};
    };

    InfoPane() = default;
    InfoPane(const InfoPane &) = delete;
    InfoPane &operator=(const InfoPane &) = delete;
    ~InfoPane();

    // The width the pane takes, in physical pixels, or 0 while it is closed. 'available' is the width of the
    // window to the right of the sidebar: the grid keeps a minimum of it.
    int Width(HWND hWnd, int available) const;

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open);

    void SetBounds(HWND hWnd, const RECT &bounds);
    void OnDpiChanged() { m_closeHover.Reset(m_hWnd, kInfoHoverTimer); }
    void Paint(Renderer &renderer) const;

    // Call it whenever the selection or the tags of the selected files change.
    void SetSelection(const std::vector<file*> &files);

    // Scrolls the body (everything under the header) when it is taller than the pane. Returns true when 'pt' is
    // over the pane, so the wheel does not reach the grid under it.
    bool OnMouseWheel(POINT pt, int delta);

    bool Contains(POINT pt) const { return m_open && PtInRect(&m_bounds, pt) != 0; }
    // Returns true when the click landed on the pane; 'close' is set if it hit the close button, 'addTag' if it hit
    // the "Add tag" button under the tags.
    // 'noteTag' is set to the name of the tag whose note was hit (empty when none was).
    bool OnLButtonDown(POINT pt, bool &close, bool &addTag, std::string &noteTag);
    void OnHover(POINT pt);
    void OnTimer();
    // After kInfoStats: shows the sizes and dates that were read.
    void OnStats();
    // The words of the search and the paths of the files found only through their text (the set belongs to the
    // caller): a single selected file of those shows the line of its text around the first word, read on a thread of
    // its own like the sizes. Call it before SetSelection.
    void SetContentSearch(std::vector<std::string> words, const std::unordered_set<std::string> *hits);
    // After kInfoSnippet: shows the line that was cut.
    void OnSnippet();
    // The label of the icon-only button under 'pt', if there is one, and where that button is.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;

private:
    struct Details
    {
        std::wstring name;
        std::wstring kind;       // "JPEG file"
        std::wstring title;      // a song's own tags, when it has them
        std::wstring artist;
        std::wstring album;
        std::wstring size;
        std::wstring modified;
        std::wstring created;
        std::wstring path;
        std::wstring snippet;    // the text around the search word, for a file found through its text
        struct Tag
        {
            std::wstring name;
            std::wstring count;  // "2/5" on a tag only some of the selected files have, else empty
            std::wstring tip;    // the paths the chip stands for, when they say more than its name
        };
        std::vector<Tag> tags;  // the tags every selected file has first, then those on some
        // The tags of the selected files that have a note or fields, by name.
        struct Note
        {
            std::string tag;     // the tag's name, to open its notes sheet
            std::wstring name;   // as shown (its last level)
            std::wstring text;   // the note, line breaks as spaces
            std::wstring meta;   // "Saint Laurent \u00B7 1971 \u00B7 link", whatever of the fields is filled
        };
        std::vector<Note> notes;
        size_t count = 0;
    };

    // One read of the disk, for the paths of a selection. The thread only gets copies of the paths.
    struct Job
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once the pane is gone
        bool done = false;
        unsigned serial = 0;  // of the selection it was started for
        bool single = false;
        Stat stat;                      // one file
        unsigned long long total = 0;   // several
    };

    // The cutting of a snippet out of one book, on a thread.
    struct SnippetJob
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once the pane is gone
        bool done = false;
        std::string path;
        std::vector<std::string> words;
        std::wstring snippet;
    };
    void RequestSnippet(const std::string &path);
    void LaunchSnippet(std::string path);
    static void RunSnippet(std::shared_ptr<SnippetJob> job);

    void Request(const std::vector<std::string> &paths);
    void Launch(std::vector<std::string> paths);
    static void Run(std::shared_ptr<Job> job, std::vector<std::string> paths);

    int Scale(int value) const;
    RECT CloseRect() const;
    RECT BodyRect() const;
    void Invalidate() const;

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    Details m_details;
    HoverFade m_closeHover;  // 0 the close button, 1 the Add tag button
    unsigned m_serial = 0;               // counts the selections, to tell a stale read
    std::shared_ptr<Job> m_running;      // the read under way, if any
    std::vector<std::string> m_words;
    const std::unordered_set<std::string> *m_hits = nullptr;
    std::shared_ptr<SnippetJob> m_snippetRunning;
    bool m_snippetQueued = false;        // another file (or other words) came meanwhile: its snippet is cut next
    std::string m_snippetPath;           // the file whose snippet the details hold, or are waiting for
    std::vector<std::string> m_snippetWords;
    bool m_queued = false;               // the selection changed meanwhile
    std::vector<std::string> m_queuedPaths;
    // Where the shortened tag chips were drawn last, with their whole path.
    struct ChipTip { RECT rect; std::wstring path; };
    mutable std::vector<ChipTip> m_chipTips;
    mutable int m_scrollY = 0;  // Paint pulls it back when the content got shorter
    mutable int m_contentHeight = 0;  // of the body, as Paint last laid it out
    mutable std::vector<RECT> m_noteRects;  // where Paint drew each note, in step with m_details.notes
    mutable RECT m_addRect = {};  // where Paint drew the Add tag button; empty when there is none
};

#endif
