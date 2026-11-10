#ifndef TAG_EXPORT_HPP
#define TAG_EXPORT_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class library;
class tag;

/// Writes a tag as a Markdown note, for a folder of notes (a vault) to pick up, with a picture of each file next to
/// it. Meant for reference tags ("ref/ysl-blood-luster"), but any tag works. For the tag "ref/x" the file is
/// "x.md":
///
///     # x
///     **Source:** Saint Laurent · **Creator:** … · **Year:** … · **Link:** … · **Status:** devoured · **Rights:** inspiration
///     <the tag's note>
///     **Taken:** …
///     **Refused:** …
///     **Used in:** [[03 jeans]]
///     **Tags:** motif/fang, colour/oxblood
///     ![](x-1.jpg) ![](x-2.png)
///
/// Source, Status and Rights are the last level of the tags under "source/", "status/" and "rights/" that the tag's
/// files carry, Used in the same for "maison/piece/" (dashes and underscores become spaces), Tags every other tag
/// of those files that no other of their tags sits under, minus the tag itself and the four branches above. Lines
/// with nothing to say are left out. Link, Creator, Year, Taken and Refused come from the tag's fields (tag_attrs).
struct tag_export_options
{
    /// Makes the picture of one file: source is the file's path, dest_without_extension where to put it (in the
    /// export folder, no extension yet). Returns the path it really wrote (same folder, any extension) or empty for
    /// "no picture" for this file. Null: pictures (jpg, png, gif, webp, bmp, avif, jxl, tiff) are copied as they are
    /// and other files get none. A front end that can decode images passes its thumbnail maker here.
    using thumbnail_maker = std::function<std::filesystem::path(const std::string &source,
        const std::filesystem::path &dest_without_extension)>;
    thumbnail_maker make_thumbnail;

    /// At most this many pictures per note (the first files of the tag, in library order). 0 is none.
    std::size_t max_pictures = 24;
};

struct tag_export_result
{
    bool ok = false;
    std::string error;
    std::filesystem::path file;   // the .md written
    std::size_t pictures = 0;     // pictures written next to it
    std::size_t pictures_failed = 0;  // files whose picture could not be copied or made (the note still lists the rest)
};

/// The Markdown text of the tag (what export_tag_markdown writes), with the pictures it should list. No file access.
std::string tag_markdown(const library &lib, const tag *t, const std::vector<std::string> &pictures);

/// Writes the tag's note into folder (made if missing), plus its pictures. Replaces a note of the same name; the file
/// is written whole or not at all. The pictures are named "<note name>-<n>.<ext>".
tag_export_result export_tag_markdown(const library &lib, const tag *t, const std::filesystem::path &folder,
    const tag_export_options &options = tag_export_options());

/// Same, with the note's file name (without extension) chosen by the caller instead of the tag's last level.
tag_export_result export_tag_markdown(const library &lib, const tag *t, const std::filesystem::path &folder,
    const tag_export_options &options, const std::string &stem);

/// Exports every tag under branch ("ref" gives ref/..., not "ref" itself) that is a leaf of the tree, i.e. has no tag
/// nested under it. Two tags with the same last level (case aside) get different file names: the second is "leaf (parent)", then
/// "-2", "-3"... Stops at the first failure. Results in tag order.
std::vector<tag_export_result> export_tags_under(const library &lib, const std::string &branch,
    const std::filesystem::path &folder, const tag_export_options &options = tag_export_options());

#endif
