#ifndef MUSIC_QUEUE_HPP
#define MUSIC_QUEUE_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// The songs added by hand, which play before the rest of the list does. They are kept as paths: the list the viewer
// steps through is the grid as it was when the viewer opened, and a queued song can come from any filter.
class MusicQueue
{
public:
    const std::vector<std::string> &Paths() const { return m_paths; }
    size_t Size() const { return m_paths.size(); }
    bool Empty() const { return m_paths.empty(); }

    // At the end of the queue.
    void Add(const std::vector<std::string> &paths)
    {
        m_paths.insert(m_paths.end(), paths.begin(), paths.end());
    }

    // Before everything queued, keeping their order: the first of 'paths' plays first.
    void AddNext(const std::vector<std::string> &paths)
    {
        m_paths.insert(m_paths.begin(), paths.begin(), paths.end());
    }

    void Remove(size_t at)
    {
        if (at < m_paths.size())
            m_paths.erase(m_paths.begin() + static_cast<std::ptrdiff_t>(at));
    }

    // The song at 'from' goes to 'to'; the songs between shift by one.
    void Move(size_t from, size_t to)
    {
        if (from >= m_paths.size() || to >= m_paths.size() || from == to)
            return;
        std::string path = std::move(m_paths[from]);
        m_paths.erase(m_paths.begin() + static_cast<std::ptrdiff_t>(from));
        m_paths.insert(m_paths.begin() + static_cast<std::ptrdiff_t>(to), std::move(path));
    }

    void Clear() { m_paths.clear(); }

    // Takes the song at 'at' out of the queue and drops the ones before it, as when it is picked in the rail.
    // Returns false when there is none.
    bool TakeAt(size_t at, std::string &path)
    {
        if (at >= m_paths.size())
            return false;
        path = std::move(m_paths[at]);
        m_paths.erase(m_paths.begin(), m_paths.begin() + static_cast<std::ptrdiff_t>(at) + 1);
        return true;
    }

    bool TakeFirst(std::string &path) { return TakeAt(0, path); }

    // Drops the songs 'keep' says no to, say the ones whose files are gone.
    template <typename Keep> void Filter(Keep keep)
    {
        m_paths.erase(std::remove_if(m_paths.begin(), m_paths.end(), [&](const std::string &p) { return !keep(p); }),
            m_paths.end());
    }

private:
    std::vector<std::string> m_paths;
};

#endif
