#ifndef INBOX_CAPTURE_HPP
#define INBOX_CAPTURE_HPP

#include <windows.h>
#include <ole2.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Fast capture into the Inbox, a folder Corpus watches: what a paste (Ctrl+V) or a drop
// brings is saved there as files, so it joins the library like anything saved into a watched folder. Everything
// reads an IDataObject, which is what both the clipboard (OleGetClipboard) and a drop hand over. Nothing touches
// the network: a source URL comes from the data itself.
namespace inbox
{
    // The Inbox folder, created if it isn't there (UTF-8, as 'libraryFolder'). Kept in the registry (InboxFolder) once
    // chosen: a catalog that already has <library folder>\Inbox keeps it, a new one starts with Pictures\Corpus Inbox.
    // A test library: see Movable.
    // Never inside the library folder for a new catalog, so deleting the library can't take the only copy of a picture.
    std::string Folder(const std::string &libraryFolder);
    // Whether the Inbox of this run follows the setting: the real catalog does; a test library (--library, see
    // SetTestLibrary) always has <library folder>\Inbox, so a run on one never touches the real Inbox or its settings.
    bool Movable();
    // Set by main before the window opens: this run is on a library given with --library.
    void SetTestLibrary(bool test);
    // Remembers 'folder' (UTF-8) as the Inbox; false when the registry refused.
    bool SetFolder(const std::string &folder);

    // Whether 'folder' (UTF-8) holds anything at all.
    bool IsEmpty(const std::string &folder);

    // Whether the data holds files (CF_HDROP), and their paths.
    bool HasFiles(IDataObject *data);
    std::vector<std::string> Files(IDataObject *data);

    // Whether the data holds something Capture can save: files, a PNG, a bitmap or the contents of dragged files.
    bool CanCapture(IDataObject *data);

    // Posted to the window by the Worker: wParam 1 when a capture or a move ended (see Worker::Take), 0 while a move
    // goes on (see Worker::Progress).
    const UINT kInboxWork = WM_APP + 20;

    // The largest file a capture saves from dragged or pasted contents (a link to a big video, say). Files copied
    // from a folder have no limit.
    const unsigned long long kMostBytes = 1ull << 30;

    // What a capture or an Inbox move did.
    struct Outcome
    {
        bool move = false;
        // Capture: the files made (UTF-8), and what was left out.
        std::vector<std::string> saved;
        size_t known = 0;    // files already in the library (copying them would make duplicates)
        size_t tooBig = 0;   // over kMostBytes
        size_t refused = 0;  // shortcuts (.lnk, .url, ...), which would run their target without a warning
        size_t failed = 0;   // could not be read or written
        // Move: the folders (UTF-8) and each file moved, as (old path, new path). 'cancelled' when the window closed
        // first: the files moved went back.
        std::string from, to;
        std::vector<std::pair<std::string, std::string>> moved;
        bool cancelled = false;
        bool back = false;  // a MoveBack
    };

    // Saves captures into the Inbox and moves the Inbox, on a thread of its own, one job after the other, so a big
    // drop or a move across drives never holds the window. Only the window's thread calls these.
    class Worker
    {
    public:
        Worker() = default;
        Worker(const Worker &) = delete;
        Worker &operator=(const Worker &) = delete;
        ~Worker() { Stop(); }

        // Takes what the data holds now (names, paths and pictures; big dragged contents stay with their source as a
        // stream, read by the thread) and saves it into 'folder' (UTF-8) on the thread. Files for which 'known' is
        // true aren't copied. Returns what was left out already; 'started' says whether anything is being saved.
        // Files are written under a .part name and renamed when complete, so the folder watcher never takes half a file.
        // What came as file contents (a browser's drag) gets the Mark of the Web, as a download would.
        Outcome Capture(HWND hWnd, IDataObject *data, const std::string &folder,
            const std::function<bool(const std::string &)> &known, bool &started);

        // Moves the files directly inside 'from' into 'to' (both UTF-8), a free name for each that is taken there.
        // What can't be moved (a subfolder, a file in use, a copy whose original can't be deleted) stays.
        void Move(HWND hWnd, const std::string &from, const std::string &to);
        // Puts moved files back where they were, the same way (the catalog couldn't take the move).
        void MoveBack(HWND hWnd, const std::vector<std::pair<std::string, std::string>> &moved);
        bool Moving() const { return m_moves > 0; }
        // Whether a capture or a move hasn't ended yet (its outcome not taken).
        bool Busy() const { return m_pending > 0; }
        // How far the running move got: files moved, out of 'total'.
        void Progress(size_t &done, size_t &total) const;

        // After kInboxWork (wParam 1): what ended since the last call. False if nothing.
        bool Take(std::vector<Outcome> &done);

        // The window is closing: captures stop (half-written files are deleted), a move is taken back, and nothing
        // more is posted.
        void Stop();

    private:
        struct Item;
        struct Job;
        struct Shared;
        void Queue(HWND hWnd, std::unique_ptr<Job> job);
        static void Run(std::shared_ptr<Shared> shared);
        static void RunCapture(Shared &shared, Job &job);
        static void RunMove(Shared &shared, Job &job);

        std::shared_ptr<Shared> m_shared;
        size_t m_pending = 0;  // jobs queued whose outcome isn't taken
        size_t m_moves = 0;    // of which moves
    };

    // Where a captured file came from, or empty. Kept in the alternate data stream "Corpus.Source" of the file
    // itself, so it travels with it and the catalog stays unchanged.
    std::wstring SourceOf(const std::string &path);

    // The window's drop target. Asks the window what to do with a drop: 'wants' says whether the pointer (in client
    // coordinates) is somewhere a drop is taken, and 'drop' receives it.
    class DropTarget : public IDropTarget
    {
    public:
        using WantsFn = std::function<bool(IDataObject*, POINT screen)>;
        using DropFn = std::function<void(IDataObject*, POINT screen)>;
        using LeaveFn = std::function<void()>;
        // 'leave' is called when the drag leaves the window or ends in a drop.
        DropTarget(WantsFn wants, DropFn drop, LeaveFn leave = nullptr)
            : m_wants(std::move(wants)), m_drop(std::move(drop)), m_leave(std::move(leave)) {}

        STDMETHODIMP QueryInterface(REFIID riid, void **object) override;
        STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
        STDMETHODIMP_(ULONG) Release() override
        {
            ULONG refs = --m_refs;
            if (refs == 0)
                delete this;
            return refs;
        }
        STDMETHODIMP DragEnter(IDataObject *data, DWORD keys, POINTL pt, DWORD *effect) override;
        STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD *effect) override;
        STDMETHODIMP DragLeave() override;
        STDMETHODIMP Drop(IDataObject *data, DWORD keys, POINTL pt, DWORD *effect) override;

    private:
        WantsFn m_wants;
        DropFn m_drop;
        LeaveFn m_leave;
        IDataObject *m_data = nullptr;  // the drag in progress, valid between DragEnter and DragLeave/Drop
        std::atomic<ULONG> m_refs{ 1 };
    };
}

#endif
