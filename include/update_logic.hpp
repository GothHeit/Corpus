#ifndef UPDATE_LOGIC_HPP
#define UPDATE_LOGIC_HPP

#include <string>
#include <vector>

/// The parts of the update check that need no network or Windows: reading versions, the few fields of GitHub's
/// "latest release" answer, and the lines of a SHA256SUMS file. The program's updater (apps/gui/updater.cpp) does the
/// downloading and the checking of signatures and hashes.
namespace update_check
{
    /// "v1.2.3" or "1.2.3" (nothing after the third number: "1.2.3-beta" is refused). False if it isn't one.
    bool parse_version(const std::string &text, int out[3]);

    /// Negative, zero or positive: a older than, the same as or newer than b.
    int compare_versions(const int a[3], const int b[3]);

    struct asset
    {
        std::string name;
        std::string url;    // browser_download_url
    };

    struct release
    {
        std::string tag;        // tag_name, "v0.3.0"
        std::string page_url;   // html_url, the release page
        std::vector<asset> assets;
    };

    /// Reads tag_name, html_url and the name and browser_download_url of each asset from the JSON of
    /// GET /repos/<owner>/<repo>/releases/latest, with a small scanner that skips everything else. False when the text
    /// isn't a JSON object or has no tag_name.
    bool parse_release(const std::string &json, release &out);

    /// The asset with that exact name, or null.
    const asset *find_asset(const release &r, const std::string &name);

    /// The hash written for 'name' in a SHA256SUMS file ("<64 hex>  <name>", or with " *" for a binary mode mark), in
    /// lower case. False if the file has no valid line for it.
    bool find_hash(const std::string &sums, const std::string &name, std::string &hex);

    /// The "version X.Y.Z" line of a SHA256SUMS file, which the signature covers: the release the files belong to, so an
    /// older signed release can't be passed off as a newer one. False if there is no such line (or more than one that
    /// disagree).
    bool find_signed_version(const std::string &sums, int out[3]);

    /// Whether an update may be offered: the signed version equals the release tag's and is newer than the running one.
    bool update_acceptable(const std::string &sums, const std::string &tag, const int current[3]);

    /// Whether a link is one the program is willing to open or download from: https on github.com, under
    /// /<owner>/<repo>/ ('repo_path' is "GothHeit/Corpus").
    bool is_repository_url(const std::string &url, const std::string &repo_path);
}

#endif
