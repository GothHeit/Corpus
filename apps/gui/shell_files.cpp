#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "shell_files.hpp"
#include "ui_util.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <cstring>
#include <map>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace
{
    // An item ID list the shell allocated, freed with it.
    struct IdListFree { void operator()(ITEMIDLIST *list) const { CoTaskMemFree(list); } };
    using IdList = std::unique_ptr<ITEMIDLIST, IdListFree>;

    IdList Parse(const std::wstring &path)
    {
        PIDLIST_ABSOLUTE list = nullptr;
        if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &list, 0, nullptr)))
            return nullptr;
        return IdList(list);
    }

    // The folder part of a path: C:\Pictures for C:\Pictures\a.jpg.
    std::wstring FolderOf(const std::wstring &path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash == 2 && path[1] == L':' ? 3 : slash);
    }

    std::wstring Lowered(std::wstring text)
    {
        if (!text.empty())
            CharLowerBuffW(&text[0], static_cast<DWORD>(text.size()));
        return text;
    }

    // The files grouped by their folder (case aside, as Windows compares paths), in the order they come.
    std::vector<std::pair<std::wstring, std::vector<std::wstring>>> ByFolder(const std::vector<std::string> &paths)
    {
        std::vector<std::pair<std::wstring, std::vector<std::wstring>>> groups;
        std::map<std::wstring, size_t> index;
        for (const std::string &path : paths)
        {
            std::wstring wide = Utf8ToWide(path);
            std::wstring folder = FolderOf(wide);
            auto slot = index.emplace(Lowered(folder), groups.size());
            if (slot.second)
                groups.push_back({ folder, {} });
            groups[slot.first->second].second.push_back(std::move(wide));
        }
        return groups;
    }

    // Hands 'size' bytes to the data object as 'format', which then owns them.
    void SetGlobal(IDataObject *data, CLIPFORMAT format, const void *bytes, size_t size)
    {
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!global)
            return;
        void *target = GlobalLock(global);
        if (!target)
        {
            GlobalFree(global);
            return;
        }
        std::memcpy(target, bytes, size);
        GlobalUnlock(global);

        FORMATETC formatEtc = { format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium = {};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = global;
        if (FAILED(data->SetData(&formatEtc, &medium, TRUE)))
            GlobalFree(global);
    }

    // The shell's own data object for the files, as Explorer makes when files are copied or dragged: it offers them
    // as a file drop (CF_HDROP) and in the shell's formats, which every program that takes files from Explorer reads.
    // Their paths ride along as text, one per line. Null if none of the files is on disk.
    ComPtr<IDataObject> DataObjectOf(const std::vector<std::string> &paths)
    {
        std::vector<IdList> items;
        std::wstring text;
        for (const std::string &path : paths)
        {
            std::wstring wide = Utf8ToWide(path);
            if (IdList item = Parse(wide))
            {
                items.push_back(std::move(item));
                text += (text.empty() ? L"" : L"\r\n") + wide;
            }
        }
        if (items.empty())
            return nullptr;

        // Absolute lists are relative to the desktop, the shell's root, so files from several folders go together.
        std::vector<PCUITEMID_CHILD> children;
        for (const IdList &item : items)
            children.push_back(reinterpret_cast<PCUITEMID_CHILD>(item.get()));
        ComPtr<IDataObject> data;
        if (FAILED(SHCreateDataObject(nullptr, static_cast<UINT>(children.size()), children.data(), nullptr,
                IID_PPV_ARGS(data.GetAddressOf()))))
            return nullptr;
        SetGlobal(data.Get(), CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        return data;
    }
}

bool shell_files::DragOut(HWND hWnd, const std::vector<std::string> &paths)
{
    ComPtr<IDataObject> data = DataObjectOf(paths);
    if (!data)
        return false;
    // The shell's own drop source shows the files' icons under the pointer, as a drag from Explorer does.
    DWORD effect = DROPEFFECT_NONE;
    SHDoDragDrop(hWnd, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
    return true;
}

bool shell_files::CopyToClipboard(HWND, const std::vector<std::string> &paths)
{
    ComPtr<IDataObject> data = DataObjectOf(paths);
    if (!data)
        return false;
    // Pasted in Explorer, the files are copied, never moved.
    DWORD effect = DROPEFFECT_COPY;
    SetGlobal(data.Get(), static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)), &effect, sizeof(effect));
    if (FAILED(OleSetClipboard(data.Get())))
        return false;
    // Rendered onto the clipboard now, so it stays there once Corpus closes.
    OleFlushClipboard();
    return true;
}

size_t shell_files::FolderCount(const std::vector<std::string> &paths)
{
    return ByFolder(paths).size();
}

void shell_files::ShowInExplorer(const std::vector<std::string> &paths)
{
    for (const auto &group : ByFolder(paths))
    {
        // A folder that is gone too opens nothing, rather than a shell error box.
        DWORD attributes = GetFileAttributesW(group.first.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        IdList folder = Parse(group.first);
        if (!folder)
            continue;
        std::vector<IdList> items;
        std::vector<PCUITEMID_CHILD> children;
        for (const std::wstring &path : group.second)
        {
            if (IdList item = Parse(path))
            {
                children.push_back(ILFindLastID(item.get()));
                items.push_back(std::move(item));
            }
        }
        if (!children.empty())
            SHOpenFolderAndSelectItems(folder.get(), static_cast<UINT>(children.size()), children.data(), 0);
        else
        {
            // None of its files is there any more: the folder itself opens.
            SHELLEXECUTEINFOW info = {};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_FLAG_NO_UI;
            info.lpFile = group.first.c_str();
            info.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&info);
        }
    }
}
