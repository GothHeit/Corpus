#include "filter_query.hpp"
#include "../../include/text.hpp"
#include <vector>

namespace
{
    // A {...} group of the search text that is a single tag, required or excluded.
    struct TagGroup
    {
        size_t open;   // the '{'
        size_t close;  // the '}'
        std::string folded;
        TagState state;
    };

    std::string TrimSpaces(const std::string &s)
    {
        size_t first = s.find_first_not_of(' ');
        if (first == std::string::npos)
            return "";
        return s.substr(first, s.find_last_not_of(' ') - first + 1);
    }

    // Reads the groups the way library::search does: a '{' needs a '}' before the next '{'. Stops at the first
    // '{' that isn't closed, and gives its position in 'unclosed' (npos when every one is).
    std::vector<TagGroup> TagGroups(const std::string &query, size_t *unclosed = nullptr)
    {
        if (unclosed)
            *unclosed = std::string::npos;
        std::vector<TagGroup> groups;
        size_t pos = 0;
        while (true)
        {
            size_t open = query.find('{', pos);
            if (open == std::string::npos)
                break;
            size_t close = query.find('}', open);
            if (close == std::string::npos || query.find('{', open + 1) < close)
            {
                if (unclosed)
                    *unclosed = open;
                break;
            }
            pos = close + 1;

            std::string inside = TrimSpaces(query.substr(open + 1, close - open - 1));
            TagState state = TagState::kRequired;
            if (!inside.empty() && inside[0] == '-')
            {
                state = TagState::kExcluded;
                inside = TrimSpaces(inside.substr(1));
            }
            else if (inside.compare(0, 4, "NOT ") == 0)
            {
                state = TagState::kExcluded;
                inside = TrimSpaces(inside.substr(4));
            }
            if (CanFilterBy(inside))
                groups.push_back({ open, close, fold_case(inside), state });
        }
        return groups;
    }

    // Takes the group out with one of the spaces around it, so "a {x} b" becomes "a b". Text on both sides
    // that touched the group stays apart, as the search reads it.
    void RemoveGroup(std::string &query, size_t open, size_t close)
    {
        query.erase(open, close - open + 1);
        bool before = open > 0 && query[open - 1] == ' ';
        bool after = open < query.size() && query[open] == ' ';
        if (before && (after || open == query.size()))
            query.erase(open - 1, 1);
        else if (after && open == 0)
            query.erase(0, 1);
        else if (!before && !after && open > 0 && open < query.size())
            query.insert(open, 1, ' ');
    }
}

std::unordered_map<std::string, TagState> TagStates(const std::string &query)
{
    std::unordered_map<std::string, TagState> states;
    for (const TagGroup &group : TagGroups(query))
        states.emplace(group.folded, group.state);  // the first group of a tag decides, as WithTagState keeps it
    return states;
}

std::string WithTagState(const std::string &query, const std::string &name, TagState state)
{
    std::string group;
    if (state == TagState::kRequired)
        group = "{" + name + "}";
    else if (state == TagState::kExcluded)
        group = "{-" + name + "}";

    std::string folded = fold_case(name);
    std::vector<TagGroup> found;
    size_t unclosed;
    for (const TagGroup &g : TagGroups(query, &unclosed))
    {
        if (g.folded == folded)
            found.push_back(g);
    }

    std::string out = query;
    if (found.empty())
    {
        if (group.empty())
            return out;
        // Before a '{' that isn't closed, where the search still reads it, rather than after the brace.
        if (unclosed != std::string::npos)
            out.insert(unclosed, (unclosed == 0 || out[unclosed - 1] == ' ' ? "" : " ") + group + " ");
        else
            out += (out.empty() || out.back() == ' ' ? "" : " ") + group;
        return out;
    }

    // From the last group back, so the positions of the earlier ones still hold.
    for (size_t i = found.size(); i-- > 0;)
    {
        if (i == 0 && !group.empty())
            out.replace(found[i].open, found[i].close - found[i].open + 1, group);
        else
            RemoveGroup(out, found[i].open, found[i].close);
    }
    return out;
}

bool CanFilterBy(const std::string &name)
{
    if (name.empty() || name[0] == '-' || name == "AND" || name == "OR" || name == "NOT")
        return false;
    return name.find_first_of(" {}()") == std::string::npos;
}
