#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "audio_scan.hpp"
#include <algorithm>
#include <thread>

namespace
{
    const size_t kBatch = 32;  // results that make the window take them without waiting for the end
}

void AudioScan::Start(HWND hWnd, std::vector<std::string> paths)
{
    // A path already asked for is not read twice.
    paths.erase(std::remove_if(paths.begin(), paths.end(), [this](const std::string &p) { return !m_asked.insert(p).second; }),
        paths.end());
    if (paths.empty())
        return;
    m_hWnd = hWnd;
    if (m_running)
    {
        m_queued.insert(m_queued.end(), std::make_move_iterator(paths.begin()), std::make_move_iterator(paths.end()));
        return;
    }
    Launch(std::move(paths));
}

void AudioScan::Launch(std::vector<std::string> paths)
{
    m_running = std::make_shared<Shared>();
    m_running->hWnd = m_hWnd;
    // Detached, like the Missing check: a path on a network drive that doesn't answer can hold it for a while, and
    // the window must still close meanwhile. It only touches its own copy of the paths and the Shared it keeps alive.
    std::thread(Run, m_running, std::move(paths)).detach();
}

void AudioScan::Stop()
{
    m_queued.clear();
    m_asked.clear();
    if (!m_running)
        return;
    std::lock_guard<std::mutex> lock(m_running->mutex);
    m_running->hWnd = nullptr;
}

bool AudioScan::Take(std::vector<Result> &out)
{
    if (!m_running)
        return false;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(m_running->mutex);
        out.insert(out.end(), std::make_move_iterator(m_running->results.begin()), std::make_move_iterator(m_running->results.end()));
        m_running->results.clear();
        m_running->posted = false;
        finished = m_running->done;
    }
    for (const Result &r : out)
        m_asked.erase(r.path);
    if (finished)
    {
        m_running.reset();
        if (!m_queued.empty())
        {
            std::vector<std::string> next;
            next.swap(m_queued);
            Launch(std::move(next));
        }
    }
    return !out.empty();
}

void AudioScan::Run(std::shared_ptr<Shared> shared, std::vector<std::string> paths)
{
    // Songs are read in the background; the window and the other threads come first.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    for (std::string &path : paths)
    {
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (!shared->hWnd)
                return;
        }
        Result result;
        result.opened = audio_read_tags_file(path, result.tags);
        result.path = std::move(path);

        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->hWnd)
            return;
        shared->results.push_back(std::move(result));
        if (shared->results.size() >= kBatch && !shared->posted)
        {
            shared->posted = true;
            PostMessage(shared->hWnd, kAudioTags, 0, 0);
        }
    }

    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->done = true;
    if (shared->hWnd && !shared->posted)
    {
        shared->posted = true;
        PostMessage(shared->hWnd, kAudioTags, 0, 0);
    }
}
