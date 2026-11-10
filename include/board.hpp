#ifndef BOARD_HPP
#define BOARD_HPP

#include <cstdint>
#include <string>
#include <vector>

/// @brief A file placed on a board: where its centre sits on the canvas, how big it is and what covers what. The
/// file is kept by its database id, so the item survives a reload and needs no pointer fix-ups.
struct board_item
{
    std::int64_t file_id = 0;
    double x = 0.0;      // canvas coordinates of the item's centre
    double y = 0.0;
    double scale = 1.0;  // 1 = the picture's default size on the canvas; > 0
    int z = 0;           // higher covers lower

    bool operator==(const board_item &o) const
    {
        return file_id == o.file_id && x == o.x && y == o.y && scale == o.scale && z == o.z;
    }
};

/// @brief A board: a saved query (the same text the search box takes) plus where some of the files it finds were put.
/// The query decides what belongs; items only remember positions. A file the query finds that has no item is "in the
/// tray"; an item whose file the query no longer finds is kept (so it comes back in place) but not shown.
class board
{
    friend class library;
    friend class database;

    std::int64_t db_id = 0;
    std::string name;
    std::string query;
    std::vector<board_item> items;

    public:

    std::int64_t get_db_id() const { return db_id; }
    const std::string &get_name() const { return name; }
    const std::string &get_query() const { return query; }
    const std::vector<board_item> &get_items() const { return items; }
    /// The item of a file, or nullptr if it was never placed.
    const board_item *find_item(std::int64_t file_id) const
    {
        for(const board_item &i : items)
            if(i.file_id == file_id)
                return &i;
        return nullptr;
    }
};

#endif
