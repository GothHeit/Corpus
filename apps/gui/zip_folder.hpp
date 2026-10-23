#ifndef ZIP_FOLDER_HPP
#define ZIP_FOLDER_HPP

#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>

// A zip file read through the zip folders of the Windows shell (zipfldr.dll), the "compressed folders" Explorer
// opens: no zip code of our own. Comic book archives (.cbz) and EPUBs are zip files.
//
// An object lives on one thread, which has COM started (single-threaded apartment).
class ZipFolder
{
public:
    struct Entry
    {
        std::wstring name;  // its path inside the archive, folders split by '\'
        Microsoft::WRL::ComPtr<IShellFolder> folder;
        std::unique_ptr<ITEMID_CHILD, void (*)(void *)> id{ nullptr, CoTaskMemFree };
    };

    // Lists every file in the archive at 'path'; false when it can't be opened as one.
    bool Open(const std::wstring &path);
    const std::vector<Entry> &Entries() const { return m_entries; }
    // Reads an entry whole; false past 'maxBytes'.
    bool Read(const Entry &entry, std::vector<BYTE> &bytes, size_t maxBytes) const;

private:
    void List(IShellFolder *folder, const std::wstring &prefix, int depth);

    std::vector<Entry> m_entries;
};

#endif
