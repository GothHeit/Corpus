#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "open_with.hpp"
#include "strings.hpp"
#include <windows.h>
#include <shlobj.h>
#include <string>

namespace
{
    // What Windows lists the program under: the key is named after the .exe, as for any program that is not
    // installed.
    const wchar_t *const kExeName = L"Corpus.exe";
    const wchar_t *const kApplicationKey = L"Software\\Classes\\Applications\\Corpus.exe";
    const wchar_t *const kCommandKey = L"Software\\Classes\\Applications\\Corpus.exe\\shell\\open\\command";

    // What earlier versions registered the program as, when the .exe was still called corpus-gui.exe.
    const wchar_t *const kOldExeName = L"corpus-gui.exe";
    const wchar_t *const kOldApplicationKey = L"Software\\Classes\\Applications\\corpus-gui.exe";

    // The types the viewer and the readers show.
    const wchar_t *const kExtensions[] = {
        // Pictures
        L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".gif", L".bmp", L".tif", L".tiff", L".webp", L".heic",
        L".heif", L".avif", L".ico",
        // Videos
        L".mkv", L".mp4", L".m4v", L".mov", L".avi", L".webm", L".wmv", L".asf", L".flv", L".f4v", L".mpg", L".mpeg",
        L".m2v", L".ts", L".mts", L".m2ts", L".vob", L".ogv", L".3gp", L".3g2", L".divx", L".rm", L".rmvb",
        // Books and text
        L".pdf", L".epub", L".cbz", L".txt", L".md", L".markdown", L".html", L".htm",
    };

    std::wstring ExePath()
    {
        wchar_t path[MAX_PATH * 4];
        DWORD size = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
        return size > 0 && size < sizeof(path) / sizeof(path[0]) ? std::wstring(path, size) : std::wstring();
    }

    std::wstring Command()
    {
        return L"\"" + ExePath() + L"\" \"%1\"";
    }

    std::wstring OpenWithKey(const wchar_t *extension)
    {
        return std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithList\\" + kExeName;
    }

    bool SetString(const wchar_t *key, const wchar_t *name, const std::wstring &value)
    {
        return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_SZ, value.c_str(),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    std::wstring CurrentCommand()
    {
        wchar_t buffer[MAX_PATH * 4];
        DWORD size = sizeof(buffer);
        if (RegGetValueW(HKEY_CURRENT_USER, kCommandKey, nullptr, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
            return L"";
        return buffer;
    }

    // Deletes a key only if nothing is left in it: a key this code made for an extension, but that may since have
    // got a default program or other entries from somewhere else.
    void DeleteIfEmpty(const std::wstring &key)
    {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS)
            return;
        DWORD subkeys = 0, values = 0;
        LONG result = RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr,
            nullptr, nullptr, nullptr);
        RegCloseKey(handle);
        if (result == ERROR_SUCCESS && subkeys == 0 && values == 0)
            RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
    }

    // Removes what an earlier version registered under the old .exe name; true if there was anything.
    bool RemoveOldRegistration();

    void NotifyShell()
    {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
}

namespace
{
    bool RemoveOldRegistration()
    {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kOldApplicationKey, 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS)
            return false;
        RegCloseKey(handle);
        for (const wchar_t *extension : kExtensions)
        {
            std::wstring list = std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithList";
            RegDeleteTreeW(HKEY_CURRENT_USER, (list + L"\\" + kOldExeName).c_str());
            DeleteIfEmpty(list);
            DeleteIfEmpty(std::wstring(L"Software\\Classes\\") + extension);
        }
        RegDeleteTreeW(HKEY_CURRENT_USER, kOldApplicationKey);
        return true;
    }
}

bool open_with::IsOn()
{
    return !CurrentCommand().empty();
}

bool open_with::TurnOn()
{
    std::wstring exe = ExePath();
    if (exe.empty())
        return false;

    RemoveOldRegistration();
    bool ok = SetString(kApplicationKey, L"FriendlyAppName", strings::kAppName) &&
        SetString(kCommandKey, nullptr, Command());
    for (const wchar_t *extension : kExtensions)
    {
        // The list of programs offered for the type: a key named after the .exe, with nothing in it.
        std::wstring key = OpenWithKey(extension);
        HKEY handle = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &handle, nullptr) == ERROR_SUCCESS)
            RegCloseKey(handle);
        else
            ok = false;
        // The types Windows is told the program opens.
        ok = RegSetKeyValueW(HKEY_CURRENT_USER, (std::wstring(kApplicationKey) + L"\\SupportedTypes").c_str(), extension,
            REG_SZ, L"", sizeof(wchar_t)) == ERROR_SUCCESS && ok;
    }
    if (!ok)
        TurnOff();
    NotifyShell();
    return ok;
}

void open_with::TurnOff()
{
    for (const wchar_t *extension : kExtensions)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, OpenWithKey(extension).c_str());
        DeleteIfEmpty(std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithList");
        DeleteIfEmpty(std::wstring(L"Software\\Classes\\") + extension);
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, kApplicationKey);
    NotifyShell();
}

void open_with::Refresh()
{
    // Registered under the old name: move it to the new one, so it stays on.
    if (RemoveOldRegistration())
    {
        TurnOn();
        return;
    }
    std::wstring current = CurrentCommand();
    if (!current.empty() && current != Command())
        TurnOn();
}
