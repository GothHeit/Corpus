#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "zip_folder.hpp"
#include "../../src/long_path.hpp"
#include <shlwapi.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // The zip folder of the Windows shell (zipfldr.dll).
    const CLSID kCompressedFolder = { 0xE88DCCE0, 0xB7B3, 0x11D1, { 0xA9, 0xF0, 0x00, 0xAA, 0x00, 0x60, 0xFA, 0x31 } };

    std::wstring DisplayName(IShellFolder *folder, PCUITEMID_CHILD id)
    {
        STRRET name = {};
        wchar_t *text = nullptr;
        if (FAILED(folder->GetDisplayNameOf(id, SHGDN_INFOLDER | SHGDN_FORPARSING, &name)) ||
            FAILED(StrRetToStrW(&name, id, &text)))
            return std::wstring();
        std::wstring result = text;
        CoTaskMemFree(text);
        return result;
    }
}

bool ZipFolder::Open(const std::wstring &path)
{
    m_entries.clear();
    PIDLIST_ABSOLUTE id = nullptr;
    // A path past MAX_PATH only opens with the \\?\ prefix.
    if (FAILED(SHParseDisplayName((path.size() < MAX_PATH ? path : long_path(path)).c_str(), nullptr, &id, 0, nullptr)))
        return false;
    ComPtr<IPersistFolder> persist;
    ComPtr<IShellFolder> root;
    HRESULT hr = CoCreateInstance(kCompressedFolder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(persist.GetAddressOf()));
    if (SUCCEEDED(hr))
        hr = persist->Initialize(id);
    CoTaskMemFree(id);
    if (FAILED(hr) || FAILED(persist.As(&root)))
        return false;
    List(root.Get(), std::wstring(), 0);
    return true;
}

// Every file in 'folder' and the folders inside it. Folders a broken archive loops on stop at 'depth'.
void ZipFolder::List(IShellFolder *folder, const std::wstring &prefix, int depth)
{
    ComPtr<IEnumIDList> items;
    if (depth > 16 || folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
            items.GetAddressOf()) != S_OK)
        return;
    PITEMID_CHILD id = nullptr;
    while (items->Next(1, &id, nullptr) == S_OK)
    {
        Entry entry;
        entry.id.reset(id);
        std::wstring name = DisplayName(folder, id);
        SFGAOF attributes = SFGAO_FOLDER;
        // Archives zipped on a Mac carry a "__MACOSX" folder of "._name" files next to the real ones: metadata.
        if (name.empty() || _wcsicmp(name.c_str(), L"__MACOSX") == 0 || name.compare(0, 2, L"._") == 0 ||
            FAILED(folder->GetAttributesOf(1, const_cast<PCUITEMID_CHILD *>(&id), &attributes)))
            continue;
        if (attributes & SFGAO_FOLDER)
        {
            ComPtr<IShellFolder> inner;
            if (SUCCEEDED(folder->BindToObject(id, nullptr, IID_PPV_ARGS(inner.GetAddressOf()))))
                List(inner.Get(), prefix + name + L"\\", depth + 1);
            continue;
        }
        entry.name = prefix + name;
        entry.folder = folder;
        m_entries.push_back(std::move(entry));
    }
}

// A file inside a zip can't be read out of order, so it is read whole, its size known or not.
bool ZipFolder::Read(const Entry &entry, std::vector<BYTE> &bytes, size_t maxBytes) const
{
    bytes.clear();
    ComPtr<IStream> packed;
    if (FAILED(entry.folder->BindToStorage(entry.id.get(), nullptr, IID_PPV_ARGS(packed.GetAddressOf()))))
        return false;
    STATSTG stat = {};
    if (SUCCEEDED(packed->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart <= maxBytes)
        bytes.reserve(static_cast<size_t>(stat.cbSize.QuadPart));
    BYTE buffer[64 * 1024];
    for (;;)
    {
        ULONG read = 0;
        HRESULT hr = packed->Read(buffer, sizeof(buffer), &read);
        if (FAILED(hr) || bytes.size() + read > maxBytes)
            return false;
        bytes.insert(bytes.end(), buffer, buffer + read);
        if (hr != S_OK || read == 0)
            return true;
    }
}
