#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "moved_search.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include <thread>

void MovedSearch::Start(HWND hWnd, const std::string &folder, std::vector<Missing> missing)
{
    Stop();
    m_folder = folder;
    m_running = std::make_shared<Shared>();
    m_running->hWnd = hWnd;
    // Detached, like MissingCheck: a dropped search may still be reading the disk when the window closes. It only
    // touches its own copies and the Shared it keeps alive.
    std::thread(Run, m_running, folder, std::move(missing)).detach();
}

void MovedSearch::Stop()
{
    if (!m_running)
        return;
    {
        std::lock_guard<std::mutex> lock(m_running->mutex);
        m_running->hWnd = nullptr;
    }
    m_running.reset();
    m_folder.clear();
}

bool MovedSearch::Take(std::vector<Found> &found, std::error_code &error)
{
    if (!m_running)
        return false;
    {
        std::lock_guard<std::mutex> lock(m_running->mutex);
        if (!m_running->done)
            return false;
        found = std::move(m_running->found);
        error = m_running->error;
    }
    m_running.reset();
    m_folder.clear();
    return true;
}

void MovedSearch::Run(std::shared_ptr<Shared> shared, std::string folder, std::vector<Missing> missing)
{
    // A library with no database: nothing it does is saved.
    library snapshot;
    for (const Missing &m : missing)
    {
        if (file *f = snapshot.add_file(m.path))
            f->set_identity(m.identity);
    }
    std::error_code error;
    std::vector<Found> found;
    for (const moved_file &moved : snapshot.find_moved(folder, &error))
        found.push_back({ moved.f->get_path(), moved.new_path });

    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->found = std::move(found);
    shared->error = error;
    shared->done = true;
    if (shared->hWnd)
        PostMessage(shared->hWnd, kMovedFound, 0, 0);
}
