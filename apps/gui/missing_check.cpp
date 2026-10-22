#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "missing_check.hpp"
#include "../../include/file_identity.hpp"
#include <thread>

void MissingCheck::Start(HWND hWnd, std::vector<std::string> paths)
{
    m_hWnd = hWnd;
    if (m_running)
    {
        m_queued = true;
        m_queuedPaths = std::move(paths);
        return;
    }
    Launch(std::move(paths));
}

void MissingCheck::Launch(std::vector<std::string> paths)
{
    m_running = std::make_shared<Shared>();
    m_running->hWnd = m_hWnd;
    // Detached: a path on a network drive that doesn't answer can hold it for the network's timeout, and the
    // window must still close meanwhile. It only touches its own copy of the paths and the Shared it keeps alive.
    std::thread(Run, m_running, std::move(paths)).detach();
}

void MissingCheck::Stop()
{
    m_queued = false;
    m_queuedPaths.clear();
    if (!m_running)
        return;
    std::lock_guard<std::mutex> lock(m_running->mutex);
    m_running->hWnd = nullptr;
}

bool MissingCheck::Take(std::unordered_set<std::string> &missing)
{
    if (!m_running)
        return false;
    {
        std::lock_guard<std::mutex> lock(m_running->mutex);
        if (!m_running->done)
            return false;
        missing = std::move(m_running->missing);
    }
    m_running.reset();
    if (m_queued)
    {
        m_queued = false;
        Launch(std::move(m_queuedPaths));
        m_queuedPaths.clear();
    }
    return true;
}

void MissingCheck::Run(std::shared_ptr<Shared> shared, std::vector<std::string> paths)
{
    std::unordered_set<std::string> missing;
    for (std::string &path : paths)
    {
        if (file_missing(path))
            missing.insert(std::move(path));
    }

    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->missing = std::move(missing);
    shared->done = true;
    if (shared->hWnd)
        PostMessage(shared->hWnd, kMissingChecked, 0, 0);
}
