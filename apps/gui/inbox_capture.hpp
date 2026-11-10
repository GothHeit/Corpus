#ifndef INBOX_CAPTURE_HPP
#define INBOX_CAPTURE_HPP

#include <windows.h>
#include <ole2.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

// Fast capture into the Inbox, a folder next to the catalog that Corpus watches: what a paste (Ctrl+V) or a drop
// brings is saved there as files, so it joins the library like anything saved into a watched folder. Everything
// reads an IDataObject, which is what both the clipboard (OleGetClipboard) and a drop hand over. Nothing touches
// the network: a source URL comes from the data itself.
namespace inbox
{
    // <library folder>\Inbox, created if it isn't there. 'libraryFolder' is UTF-8; the result too.
    std::string Folder(const std::string &libraryFolder);

    // Whether the data holds files (CF_HDROP), and their paths.
    bool HasFiles(IDataObject *data);
    std::vector<std::string> Files(IDataObject *data);

    // Whether the data holds something Capture can save: files, a PNG, a bitmap or the contents of dragged files.
    bool CanCapture(IDataObject *data);

    // Saves what the data holds into 'folder' (UTF-8) and returns the paths made (UTF-8), none if there was
    // nothing to save. Files are copied; pictures are written as they came (PNG as PNG, a bitmap re-encoded as PNG,
    // dragged file contents under their own names). A source URL found in the data (the CF_HTML SourceURL: header,
    // or the dragged URL) is kept with each picture, see SourceOf.
    std::vector<std::string> Capture(IDataObject *data, const std::string &folder);

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
        DropTarget(WantsFn wants, DropFn drop) : m_wants(std::move(wants)), m_drop(std::move(drop)) {}

        STDMETHODIMP QueryInterface(REFIID riid, void **object) override;
        STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
        STDMETHODIMP_(ULONG) Release() override { return --m_refs; }
        STDMETHODIMP DragEnter(IDataObject *data, DWORD keys, POINTL pt, DWORD *effect) override;
        STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD *effect) override;
        STDMETHODIMP DragLeave() override;
        STDMETHODIMP Drop(IDataObject *data, DWORD keys, POINTL pt, DWORD *effect) override;

    private:
        WantsFn m_wants;
        DropFn m_drop;
        IDataObject *m_data = nullptr;  // the drag in progress, valid between DragEnter and DragLeave/Drop
        std::atomic<ULONG> m_refs{ 1 };
    };
}

#endif
