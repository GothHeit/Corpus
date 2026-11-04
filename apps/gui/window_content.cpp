#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "../../include/content_index.hpp"
#include "../../include/library.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <cwctype>

// Searching in the text of books: the index and its indexer are made by main (they must be closed before a backup
// is restored), the window starts them, feeds them the library's books and shows what they know.

namespace
{
    const wchar_t *kSettingsKey = L"Software\\Corpus";
    const wchar_t *kContentValue = L"ContentSearch";

    // On unless the user turned it off. Best effort, like the other settings.
    bool LoadContentOn()
    {
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kContentValue, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
            return true;
        return value != 0;
    }

    void SaveContentOn(bool on)
    {
        DWORD value = on ? 1 : 0;
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kContentValue, REG_DWORD, &value, sizeof(value));
    }
}

void Window::SetContent(content_index *index, content_indexer *indexer)
{
    m_contentIndex = index;
    m_indexer = indexer;
    m_contentOn = LoadContentOn();
    if (!m_indexer)
        return;
    HWND hWnd = m_hWnd;
    m_indexer->set_progress_callback([hWnd]() { PostMessage(hWnd, kContentProgress, 0, 0); });
    IndexContent();
    UpdateContentStatus();
}

// Whether a search also looks in the books' text: it is on, the index is there, and it isn't being emptied (which
// holds the index for a moment).
bool Window::ContentSearching() const
{
    return m_contentOn && m_contentIndex && m_contentIndex->is_open() && !m_contentClearing;
}

// Gives the indexer every book of the library; it skips those it already knows and have not changed, so this is
// cheap to call after files were added.
void Window::IndexContent()
{
    if (!m_indexer || !m_contentOn || m_contentClearing)
        return;
    m_indexer->enqueue(m_lib.content_jobs());
    UpdateContentStatus();
}

// The line under the Search rows of the settings.
void Window::UpdateContentStatus()
{
    std::wstring status;
    if (!m_indexer || !m_contentIndex)
        status = strings::kContentUnavailable;
    else if (m_contentClearing)
        status = strings::kContentClearing;
    else if (!m_contentOn)
        status = strings::kContentOff;
    else if (m_indexer->pending() > 0)
        status = strings::kContentIndexing + FormatCount(m_indexer->pending()) + strings::kContentLeft;
    else
    {
        size_t indexed = m_contentIndex->indexed_count();
        if (indexed == 0)
            status = strings::kContentNone;
        else if (indexed == 1)
            status = strings::kContentReadyOne;
        else
            status = FormatCount(indexed) + strings::kContentReady;
    }
    m_settings.SetContentSearch(m_contentOn && m_indexer != nullptr, status);
}

// The "Search inside books" switch.
void Window::ToggleContent()
{
    if (!m_indexer)
        return;
    m_contentOn = !m_contentOn;
    SaveContentOn(m_contentOn);
    if (m_contentOn)
        IndexContent();
    else
        m_indexer->clear();
    UpdateContentStatus();
    // Results gain or lose the books found through their text.
    RefreshResults();
}

// "Clear search index": the index is emptied on a thread of its own (it gives the disk space back, which can take
// a moment), and built again afterwards while the switch is on.
void Window::ClearContentIndex()
{
    if (!m_indexer || !m_contentIndex || m_contentClearing)
        return;
    if (m_contentClear.joinable())
        m_contentClear.join();
    m_indexer->clear();
    m_contentClearing = true;
    UpdateContentStatus();
    RefreshResults();
    HWND hWnd = m_hWnd;
    content_index *index = m_contentIndex;
    std::atomic<bool> *clearing = &m_contentClearing;
    m_contentClear = std::thread([hWnd, index, clearing]()
    {
        index->clear();
        *clearing = false;
        PostMessage(hWnd, kContentProgress, 0, 0);
    });
}

// The indexer stored a batch of books, or the index was cleared: the settings line follows, and a search under way
// shows the books found since.
void Window::OnContentProgress()
{
    if (!IsWindowEnabled(m_hWnd))
    {
        m_contentProgressPending = true;
        return;
    }
    m_contentProgressPending = false;
    if (!m_contentClearing && m_contentClear.joinable())
    {
        m_contentClear.join();
        IndexContent();
    }
    UpdateContentStatus();
    if (ContentSearching() && !m_contentWords.empty())
        RefreshResults();
}

// The plain words of the query (outside the {tag} groups), lowercase and at least as long as the index looks up.
// text_snippet takes them to find the place in a book to quote.
void Window::SetContentWords()
{
    m_contentWords.clear();
    std::wstring query = m_searchBar.Query();
    bool inGroup = false;
    std::wstring word;
    auto flush = [&]()
    {
        if (word.size() >= content_min_word)
            m_contentWords.push_back(WideToUtf8(word));
        word.clear();
    };
    for (wchar_t c : query)
    {
        if (c == L'{')
        {
            flush();
            inGroup = true;
        }
        else if (c == L'}')
            inGroup = false;
        else if (inGroup)
            continue;
        else if (iswspace(c))
            flush();
        else
            word.push_back(static_cast<wchar_t>(towlower(c)));
    }
    flush();
}
