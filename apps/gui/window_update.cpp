#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "../../include/update_logic.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <shellapi.h>
#include <cstdio>

// Updates: what the Updater found, in the Settings About card, as a dot on the settings cog (and on the sidebar button
// while the sidebar is hidden) and, once per version, a line in the status bar; the weekly switch; "What's new"; and
// "Restart to update". Nothing starts by itself but the weekly check, and only when it was turned on.

namespace
{
    std::wstring Format(const wchar_t *format, const std::wstring &version, int percent = 0)
    {
        wchar_t text[400];
        swprintf(text, 400, format, version.c_str(), percent);
        return text;
    }
}

// The Updater posted that something changed: put it in words for the settings page.
void Window::OnUpdateEvent()
{
    Updater::Snapshot snapshot = m_updater.Get();
    SettingsPage::UpdateView view;
    switch (snapshot.state)
    {
    case Updater::State::kChecking:
        view.kind = SettingsPage::UpdateView::kChecking;
        view.text = strings::kUpdateChecking;
        break;
    case Updater::State::kUpToDate:
        view.kind = SettingsPage::UpdateView::kUpToDate;
        view.text = strings::kUpdateUpToDate;
        break;
    case Updater::State::kAvailable:
        view.kind = SettingsPage::UpdateView::kAvailable;
        view.text = Format(strings::kUpdateAvailable, snapshot.version);
        break;
    case Updater::State::kDownloading:
        view.kind = SettingsPage::UpdateView::kDownloading;
        view.percent = snapshot.percent;
        view.text = Format(strings::kUpdateDownloading, snapshot.version, snapshot.percent);
        break;
    case Updater::State::kReady:
        view.kind = SettingsPage::UpdateView::kReady;
        view.text = Format(strings::kUpdateReady, snapshot.version);
        break;
    case Updater::State::kError:
        view.kind = SettingsPage::UpdateView::kError;
        view.text = snapshot.error;
        break;
    default:
        break;
    }
    m_updatePageUrl = snapshot.pageUrl;
    m_settings.SetUpdate(std::move(view));
    bool waiting = snapshot.state == Updater::State::kAvailable || snapshot.state == Updater::State::kDownloading
        || snapshot.state == Updater::State::kReady;
    m_sidebar.SetUpdateAvailable(waiting);
    if (waiting != m_updateWaiting)
    {
        m_updateWaiting = waiting;
        InvalidateRect(m_hWnd, nullptr, FALSE);  // the title bar's dot
    }
    NoticeUpdate(snapshot);
}

namespace
{
    const wchar_t *const kNoticedValue = L"UpdateNoticed";

    std::wstring NoticedVersion()
    {
        wchar_t text[64] = {};
        DWORD size = sizeof(text);
        if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kNoticedValue, RRF_RT_REG_SZ, nullptr, text, &size) != ERROR_SUCCESS)
            return std::wstring();
        return text;
    }

    void SetNoticedVersion(const std::wstring &version)
    {
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kNoticedValue, REG_SZ, version.c_str(),
            static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
    }
}

// One quiet line in the status bar, once per version (decision D4), when the update was found without the person asking:
// the weekly check, or a download kept from the last run. A version the person already saw in Settings is not announced.
void Window::NoticeUpdate(const Updater::Snapshot &snapshot)
{
    if (snapshot.state != Updater::State::kAvailable && snapshot.state != Updater::State::kReady)
        return;
    if (m_updateDemo != Updater::State::kIdle)
    {
        if (snapshot.found)
            ShowNotice(Format(strings::kUpdateNotice, snapshot.version));
        return;
    }
    if (NoticedVersion() == snapshot.version)
        return;
    if (m_settings.IsOpen())
    {
        SetNoticedVersion(snapshot.version);
        return;
    }
    // A running import has the status bar; the dots still say it, and the next check can.
    if (!snapshot.found || m_importThread.joinable())
        return;
    ShowNotice(Format(snapshot.state == Updater::State::kReady ? strings::kUpdateReadyNotice : strings::kUpdateNotice,
        snapshot.version));
    SetNoticedVersion(snapshot.version);
}

void Window::MarkUpdateSeen(const std::wstring &version)
{
    if (m_updateDemo == Updater::State::kIdle && !version.empty())
        SetNoticedVersion(version);
}

// The hourly look at the weekly check, and the clean-up a little after start.
void Window::OnUpdateTimer(UINT_PTR timer)
{
    if (timer == kUpdateTidyTimer)
    {
        KillTimer(m_hWnd, kUpdateTidyTimer);
        m_updater.Tidy();
        return;
    }
    Updater::State state = m_updater.Get().state;
    bool waiting = state == Updater::State::kAvailable || state == Updater::State::kDownloading || state == Updater::State::kReady;
    if (!waiting && m_updater.DueForAutoCheck())
        m_updater.Check(true);
}

// The weekly switch. Turning it on checks at once when a week has gone by, as that is the first time it is due.
void Window::ToggleAutoUpdate()
{
    bool on = !m_updater.AutoCheck();
    m_updater.SetAutoCheck(on);
    m_settings.SetAutoUpdate(on);
    if (on && m_updater.DueForAutoCheck())
        m_updater.Check(false);
}

// "What's new": the release's page in the browser.
void Window::OpenWhatsNew()
{
    if (m_updatePageUrl.empty() || !update_check::is_repository_url(WideToUtf8(m_updatePageUrl), "GothHeit/Corpus"))
        return;
    ShellExecuteW(m_hWnd, L"open", m_updatePageUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// "Restart to update": closes Corpus the usual way (the catalog is saved), then main installs the download and starts the
// new Corpus. If this copy cannot replace itself, nothing is closed: a sheet says so and offers the folder with the
// download. A download that has gone missing is offered again.
void Window::RestartToUpdate()
{
    Updater::Pending pending = m_updater.GetPending();
    if (pending.file.empty() || GetFileAttributesW(pending.file.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        m_updater.Forget();
        ShowNotice(strings::kUpdateGone, true);
        return;
    }
    std::wstring why;
    if (!Updater::CanApply(pending, why))
    {
        ConfirmSheet::Question question;
        question.title = strings::kUpdateCantApply;
        question.body = strings::kUpdateCantApplyBody;
        question.confirm = strings::kUpdateShowFolder;
        Ask(std::move(question), [this]()
        {
            std::wstring folder = Updater::UpdatesFolder();
            if (!folder.empty() && GetFileAttributesW(folder.c_str()) != INVALID_FILE_ATTRIBUTES)
                ShellExecuteW(m_hWnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
        return;
    }
    m_pendingUpdate = pending;
    m_updateRequested = true;
    PostMessage(m_hWnd, WM_CLOSE, 0, 0);
}
