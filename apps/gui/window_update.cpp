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

// Updates: what the Updater found, in the Settings About card and as a dot on the settings button; the weekly switch;
// "What's new"; and "Restart to update". Nothing starts by itself but the weekly check, and only when it was turned on.

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
    m_sidebar.SetUpdateAvailable(snapshot.state == Updater::State::kAvailable || snapshot.state == Updater::State::kDownloading
        || snapshot.state == Updater::State::kReady);
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
// new Corpus. If this copy cannot replace itself, nothing is closed: the folder with the download opens instead.
void Window::RestartToUpdate()
{
    Updater::Pending pending = m_updater.GetPending();
    std::wstring why;
    if (!Updater::CanApply(pending, why))
    {
        MessageBoxW(m_hWnd, why.c_str(), strings::kAppName, MB_OK | MB_ICONINFORMATION);
        std::wstring folder = Updater::UpdatesFolder();
        if (!folder.empty() && GetFileAttributesW(folder.c_str()) != INVALID_FILE_ATTRIBUTES)
            ShellExecuteW(m_hWnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    m_pendingUpdate = pending;
    m_updateRequested = true;
    PostMessage(m_hWnd, WM_CLOSE, 0, 0);
}
