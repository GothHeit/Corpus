#ifndef PLAY_ORDER_HPP
#define PLAY_ORDER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

// What repeat does when the music runs out, or Next is pressed on the last one.
enum class Repeat
{
    kOff,
    kAll,  // starts the list over, reshuffled when shuffling
    kOne,  // the song ends and starts again; Next and Previous still move on
};

// The order the music of a list plays in: the grid's order, or a shuffle of it. Songs are indices into the viewer's
// list. A shuffle plays every song once before any repeats, and has the song playing first.
class PlayOrder
{
public:
    // The music in the grid's order.
    const std::vector<size_t> &Songs() const { return m_grid; }

    void Seed(uint32_t seed) { m_random.seed(seed); }

    // 'songs' is the music in grid order and 'current' the one playing, which must be among them.
    void Reset(std::vector<size_t> songs, size_t current, bool shuffle)
    {
        m_grid = std::move(songs);
        m_shuffle = shuffle;
        Build(current);
    }

    // Turning shuffle on or off keeps the song playing; off, the grid's order carries on from it.
    void SetShuffle(bool shuffle, size_t current)
    {
        if (shuffle == m_shuffle)
            return;
        m_shuffle = shuffle;
        Build(current);
    }

    // The song after 'current' (or before it), or false at the end of the list. 'wrap' goes round instead; at the
    // end of a shuffle that is a new shuffle, which doesn't start with the song that just played. A 'current' that
    // isn't among the songs (a picture, say) has none before it, and the first song after it.
    bool Next(size_t current, bool wrap, size_t &song)
    {
        Sync(current);
        if (m_order.empty())
            return false;
        if (m_position == kNone)
            m_position = 0;
        else if (m_position + 1 < m_order.size())
            m_position++;
        else if (!wrap)
            return false;
        else
        {
            if (m_shuffle)
                Shuffle(m_order.size() > 1 ? m_order[m_position] : kNone);
            m_position = 0;
        }
        song = m_order[m_position];
        return true;
    }

    bool Previous(size_t current, bool wrap, size_t &song)
    {
        Sync(current);
        if (m_order.empty())
            return false;
        if (m_position != kNone && m_position > 0)
            m_position--;
        else if (!wrap)
            return false;
        else
            m_position = m_order.size() - 1;
        song = m_order[m_position];
        return true;
    }

private:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    // The song playing was changed from elsewhere: find it in the order, or start the order over around it.
    void Sync(size_t current)
    {
        if (m_position < m_order.size() && m_order[m_position] == current)
            return;
        auto at = std::find(m_order.begin(), m_order.end(), current);
        if (at != m_order.end())
            m_position = static_cast<size_t>(at - m_order.begin());
        else
            Build(current);
    }

    // The order around 'current'; with it not among the songs, the position is kNone and a shuffle is of them all.
    void Build(size_t current)
    {
        m_order = m_grid;
        auto at = std::find(m_order.begin(), m_order.end(), current);
        if (at == m_order.end())
        {
            m_position = kNone;
            if (m_shuffle)
                std::shuffle(m_order.begin(), m_order.end(), m_random);
            return;
        }
        m_position = static_cast<size_t>(at - m_order.begin());
        if (!m_shuffle)
            return;
        std::iter_swap(m_order.begin(), at);
        std::shuffle(m_order.begin() + 1, m_order.end(), m_random);
        m_position = 0;
    }

    // Shuffles all, unless 'notFirst' would come first.
    void Shuffle(size_t notFirst)
    {
        std::shuffle(m_order.begin(), m_order.end(), m_random);
        if (m_order.front() == notFirst)
            std::swap(m_order.front(), m_order.back());
    }

    std::vector<size_t> m_grid;
    std::vector<size_t> m_order;
    size_t m_position = kNone;
    bool m_shuffle = false;
    std::mt19937 m_random;
};

#endif
