#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "../../include/backup.hpp"
#include "../../include/database.hpp"
#include "../../include/library.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <shobjidl.h>
#include <ctime>
#include <string>

namespace
{
    // Asks for the file to write ('save') or read the catalog from, with the system's file dialog. Empty if none.
    std::wstring PickCatalogFile(HWND hWnd, bool save)
    {
        ComPtr<IFileDialog> dialog;
        HRESULT created = save
            ? CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))
            : CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(created))
            return L"";

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
        COMDLG_FILTERSPEC filter = { strings::kCatalogFiles, L"*.json" };
        dialog->SetFileTypes(1, &filter);
        dialog->SetDefaultExtension(L"json");
        if (save)
        {
            wchar_t name[64];
            time_t now = time(nullptr);
            tm local = {};
            localtime_s(&local, &now);
            wcsftime(name, 64, L"corpus-catalog-%Y%m%d.json", &local);
            dialog->SetFileName(name);
        }
        if (dialog->Show(hWnd) != S_OK)
            return L"";

        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        std::wstring result;
        if (SUCCEEDED(dialog->GetResult(item.GetAddressOf())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            result = path;
        CoTaskMemFree(path);
        return result;
    }

    // "27 Oct 2026, 03:12": a backup's time (its name has UTC) in the user's time zone.
    std::wstring BackupTime(time_t when)
    {
        tm local = {};
        localtime_s(&local, &when);
        wchar_t text[48];
        wcsftime(text, 48, L"%d %b %Y, %H:%M", &local);
        return text;
    }

    std::wstring SizeText(std::uintmax_t bytes)
    {
        wchar_t text[32];
        if (bytes >= 1024 * 1024)
            swprintf(text, 32, L"%.1f MB", bytes / (1024.0 * 1024.0));
        else
            swprintf(text, 32, L"%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
        return text;
    }
}

// Where the automatic backups are kept; the settings list them.
void Window::SetBackupFolder(std::filesystem::path folder)
{
    m_backupFolder = std::move(folder);
    RefreshBackups();
}

void Window::RefreshBackups()
{
    m_backupFiles.clear();
    std::vector<std::wstring> labels;
    if (!m_backupFolder.empty())
    {
        for (const backup_info &info : list_backups(m_backupFolder))
        {
            m_backupFiles.push_back(info.file);
            labels.push_back(BackupTime(info.when) + L"   " + SizeText(info.size));
        }
    }
    m_settings.SetBackups(std::move(labels));
}

// "Export catalog...": the whole catalog to a file the user picks.
void Window::ExportCatalog()
{
    std::wstring dest = PickCatalogFile(m_hWnd, true);
    if (dest.empty())
        return;
    std::string error;
    if (export_catalog(m_lib, dest, error))
        ShowNotice(strings::kExported);
    else
        ShowNotice(strings::kExportFailed + Utf8ToWide(error), true);
}

// "Import catalog...": merges a catalog file into the library as one change; nothing existing is removed.
void Window::ImportCatalog()
{
    std::wstring source = PickCatalogFile(m_hWnd, false);
    if (source.empty() || !BeginChange())
        return;

    catalog_import result = import_catalog(m_lib, source);
    if (!result.ok)
    {
        // Rolled back in the database; the library in memory may differ from it.
        if (database *db = m_lib.attached())
            db->rollback();
        bool reloaded = ReloadLibrary();
        ShowNotice(strings::kImportFailed + Utf8ToWide(result.error), true);
        if (!reloaded)
            DestroyWindow(m_hWnd);
        return;
    }

    bool saved = FinishChange();
    if (!saved || !IsWindow(m_hWnd))
        return;
    WatchedFoldersChanged({});
    if (result.files_added + result.files_updated + result.tags_added == 0)
    {
        ShowNotice(strings::kImportedNothing);
        return;
    }
    std::wstring text = std::wstring(L"Added ") + FileCount(result.files_added) + L", updated " +
        FileCount(result.files_updated) + L", " + std::to_wstring(result.tags_added) + L" new tags";
    if (result.skipped > 0)
        text += L", " + std::to_wstring(result.skipped) + L" skipped";
    ShowNotice(text);
}

// "Restore" on a backup: asks first, then closes the window so main can put the file in place (the database must
// be closed for that) and start Corpus again.
void Window::AskRestore(size_t index)
{
    if (index >= m_backupFiles.size())
        return;
    std::filesystem::path file = m_backupFiles[index];
    std::wstring when;
    for (const backup_info &info : list_backups(m_backupFolder))
    {
        if (info.file == file)
            when = BackupTime(info.when);
    }

    ConfirmSheet::Question question;
    question.title = strings::kRestoreTitle + when + L"?";
    question.body = strings::kRestoreBody;
    question.confirm = strings::kRestore;
    question.danger = true;
    Ask(std::move(question), [this, file]()
    {
        m_pendingRestore = file;
        DestroyWindow(m_hWnd);
    });
}
