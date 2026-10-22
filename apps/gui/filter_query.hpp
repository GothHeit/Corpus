#ifndef FILTER_QUERY_HPP
#define FILTER_QUERY_HPP

#include <string>
#include <unordered_map>

// The tag sidebar and the search bar are two views of one query. The sidebar keeps no state of its own: it reads
// which tags are required or excluded from the search text, and a click on a tag rewrites that text. A tag is
// required by a group of its own, {Tag}, and excluded by {-Tag} (or {NOT Tag}). Tags inside a bigger group, such
// as {a OR b}, aren't shown as either: the sidebar couldn't change one without changing the whole group.
// All text is UTF-8. Names compare the way the search does, ignoring case (see fold_case).
enum class TagState { kOff, kRequired, kExcluded };

// The state of every tag the search text requires or excludes on its own, by folded name. The groups after a
// '{' that isn't closed are not read.
std::unordered_map<std::string, TagState> TagStates(const std::string &query);

// The search text with the tag set to 'state'. Its first group of its own is rewritten in place and any other
// one removed, so the rest of the text stays as typed; a tag that had none gets one at the end, or before the
// first '{' that isn't closed.
std::string WithTagState(const std::string &query, const std::string &name, TagState state);

// Whether the search can name the tag in a group: not with a space, brace or parenthesis in it, a leading '-'
// or the name of an operator (AND, OR, NOT).
bool CanFilterBy(const std::string &name);

#endif
