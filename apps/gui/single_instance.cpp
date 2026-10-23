#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "single_instance.hpp"
#include <cwctype>

namespace
{
    const wchar_t *const kProp = L"CorpusInstance";
    // How long a second start waits for the first one to open its window: it may be just starting.
    const int kWaitTries = 50;
    const DWORD kWaitMs = 100;
    const DWORD kSendTimeoutMs = 5000;

    struct Search
    {
        std::uint64_t key;
        HWND found;
    };

    BOOL CALLBACK Match(HWND hWnd, LPARAM param)
    {
        Search *search = reinterpret_cast<Search*>(param);
        if (reinterpret_cast<std::uint64_t>(GetPropW(hWnd, kProp)) == search->key)
        {
            search->found = hWnd;
            return FALSE;
        }
        return TRUE;
    }
}

std::uint64_t single_instance::Key(const std::wstring &libraryPath)
{
    // FNV-1a over the path in lower case, as Windows paths are the same whatever their case.
    std::uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : libraryPath)
    {
        hash ^= static_cast<std::uint64_t>(std::towlower(ch));
        hash *= 1099511628211ull;
    }
    return hash;
}

bool single_instance::Claim(std::uint64_t key, HANDLE &owner)
{
    wchar_t name[48];
    wsprintfW(name, L"Local\\Corpus-%08x%08x", static_cast<unsigned>(key >> 32), static_cast<unsigned>(key));
    owner = CreateMutexW(nullptr, FALSE, name);
    return owner && GetLastError() != ERROR_ALREADY_EXISTS;
}

void single_instance::Mark(HWND hWnd, std::uint64_t key)
{
    SetPropW(hWnd, kProp, reinterpret_cast<HANDLE>(key));
}

bool single_instance::Forward(std::uint64_t key, const std::vector<std::wstring> &files)
{
    std::wstring payload;
    for (const std::wstring &file : files)
    {
        payload += file;
        payload += L'\0';
    }
    payload += L'\0';

    for (int i = 0; i < kWaitTries; i++)
    {
        Search search = { key, nullptr };
        EnumWindows(Match, reinterpret_cast<LPARAM>(&search));
        if (search.found)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(search.found, &process);
            // The window may only take the foreground from us, which Explorer just gave it, if we say so.
            AllowSetForegroundWindow(process);
            COPYDATASTRUCT data = { kCopyDataFiles, static_cast<DWORD>(payload.size() * sizeof(wchar_t)), payload.data() };
            DWORD_PTR result = 0;
            return SendMessageTimeoutW(search.found, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                SMTO_ABORTIFHUNG, kSendTimeoutMs, &result) != 0 && result != 0;
        }
        Sleep(kWaitMs);
    }
    return false;
}

std::vector<std::wstring> single_instance::ReadFiles(const COPYDATASTRUCT &data)
{
    std::vector<std::wstring> files;
    if (data.dwData != kCopyDataFiles || !data.lpData)
        return files;
    const wchar_t *text = static_cast<const wchar_t*>(data.lpData);
    size_t count = data.cbData / sizeof(wchar_t);
    size_t start = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (text[i] == L'\0')
        {
            if (i > start)
                files.emplace_back(text + start, i - start);
            start = i + 1;
        }
    }
    return files;
}
