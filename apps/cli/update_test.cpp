// Unit tests for the update check's pure parts: versions, the release JSON, SHA256SUMS lines, links.
//
// Usage: corpus-update-test

#include <iostream>
#include <string>

#include "../../include/update_logic.hpp"

using namespace update_check;

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if(!ok)
    {
        std::cout << "FAIL: " << what << '\n';
        failures++;
    }
}

static bool newer(const char *a, const char *b)
{
    int x[3], y[3];
    return parse_version(a, x) && parse_version(b, y) && compare_versions(x, y) > 0;
}

int main()
{
    int v[3];
    check(parse_version("v0.3.0", v) && v[0] == 0 && v[1] == 3 && v[2] == 0, "version with v");
    check(parse_version("10.20.30", v) && v[0] == 10 && v[1] == 20 && v[2] == 30, "version without v");
    for(const char *bad : { "", "v", "1.2", "1.2.3.4", "1.2.x", "v1.2.3-beta", "1..3", "1.2.3 ", "latest", "1234567.0.0" })
        check(!parse_version(bad, v), std::string("refused: '") + bad + "'");
    check(newer("v0.10.0", "0.9.5") && newer("1.0.0", "0.99.99") && newer("0.2.1", "0.2.0"), "newer versions");
    check(!newer("0.2.0", "0.2.0") && !newer("0.1.9", "0.2.0"), "same or older is not newer");

    const std::string json = R"({
      "url": "https://api.github.com/repos/GothHeit/Corpus/releases/1",
      "html_url": "https://github.com/GothHeit/Corpus/releases/tag/v0.3.0",
      "author": { "login": "x", "html_url": "https://github.com/x", "site_admin": false, "list": [1, 2.5, null, true, {"a": []}] },
      "tag_name": "v0.3.0",
      "name": "Corpus é 😀 \"quoted\"",
      "assets": [
        { "url": "u", "name": "Corpus.exe", "size": 33000000, "uploader": { "html_url": "bad" },
          "browser_download_url": "https://github.com/GothHeit/Corpus/releases/download/v0.3.0/Corpus.exe" },
        { "name": "SHA256SUMS", "browser_download_url": "https://github.com/GothHeit/Corpus/releases/download/v0.3.0/SHA256SUMS" },
        { "name": "no-link" }
      ],
      "body": "notes } ] , { \"tag_name\": \"v9.9.9\""
    })";
    release r;
    check(parse_release(json, r), "parse a release");
    check(r.tag == "v0.3.0", "tag_name");
    check(r.page_url == "https://github.com/GothHeit/Corpus/releases/tag/v0.3.0", "html_url is the release's, not the author's");
    check(r.assets.size() == 2, "assets with a name and a link");
    const asset *exe = find_asset(r, "Corpus.exe");
    check(exe && exe->url == "https://github.com/GothHeit/Corpus/releases/download/v0.3.0/Corpus.exe", "asset link");
    check(!find_asset(r, "Corpus-Setup.exe") && !find_asset(r, "no-link"), "missing assets");

    check(!parse_release("", r) && !parse_release("[]", r) && !parse_release("{}", r), "not a release");
    check(!parse_release("{\"tag_name\": \"v1.0.0\"", r), "truncated object");
    check(!parse_release("{\"tag_name\": \"v1.0.0\", \"assets\": [ {\"name\": ", r), "truncated assets");
    check(!parse_release("{\"message\": \"Not Found\"}", r), "an API error has no tag");
    std::string deep = "{\"tag_name\":\"v1.0.0\",\"x\":";
    for(int i = 0; i < 200; i++)
        deep += "[";
    check(!parse_release(deep, r), "absurdly deep nesting is refused");
    check(parse_release("{\"tag_name\":\"v1.0.0\",\"assets\":[]}", r) && r.assets.empty(), "no assets");

    const std::string h1(64, 'a'), h2 = "0123456789ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef";
    const std::string sums = h1 + "  Corpus.exe\n" + h2 + " *Corpus-Setup.exe\r\nshort  x\n";
    std::string hex;
    check(find_hash(sums, "Corpus.exe", hex) && hex == h1, "hash of the first line");
    check(find_hash(sums, "Corpus-Setup.exe", hex) && hex == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "binary-mode line, lower case, CRLF");
    check(!find_hash(sums, "Corpus", hex) && !find_hash(sums, "orpus.exe", hex) && !find_hash(sums, "x", hex), "names match whole");
    check(!find_hash(std::string(63, 'a') + "  Corpus.exe\n", "Corpus.exe", hex), "short digest");
    check(!find_hash(std::string(64, 'g') + "  Corpus.exe\n", "Corpus.exe", hex), "non-hex digest");

    {
        const std::string signed_sums = "version 0.3.0\n" + h1 + "  Corpus.exe\n";
        int cur[3] = { 0, 2, 0 }, same[3] = { 0, 3, 0 }, newer_cur[3] = { 0, 4, 0 };
        check(find_signed_version(signed_sums, v) && v[0] == 0 && v[1] == 3 && v[2] == 0, "signed version line");
        check(update_acceptable(signed_sums, "v0.3.0", cur), "newer signed version matching the tag is accepted");
        check(!update_acceptable(signed_sums, "v0.4.0", cur), "a tag that differs from the signed version is refused");
        check(!update_acceptable(signed_sums, "v0.3.0", same), "the version already running is refused");
        check(!update_acceptable(signed_sums, "v0.3.0", newer_cur), "a signed release older than the running one is refused (replay)");
        check(!update_acceptable(h1 + "  Corpus.exe\n", "v0.3.0", cur), "no signed version line is refused");
        check(!update_acceptable("version 0.3.0\nversion 0.9.0\n", "v0.3.0", cur), "two disagreeing version lines are refused");
        check(!update_acceptable("version 0.3\n", "v0.3.0", cur), "a malformed version line is refused");
    }

    check(is_repository_url("https://github.com/GothHeit/Corpus/releases/tag/v0.3.0", "GothHeit/Corpus"), "release page link");
    check(!is_repository_url("http://github.com/GothHeit/Corpus/releases", "GothHeit/Corpus"), "not http");
    check(!is_repository_url("https://github.com.evil.com/GothHeit/Corpus/x", "GothHeit/Corpus"), "not another host");
    check(!is_repository_url("https://github.com/GothHeit/CorpusX/x", "GothHeit/Corpus"), "not another repository");
    check(!is_repository_url("https://github.com/GothHeit/Corpus/a b", "GothHeit/Corpus"), "no spaces");

    if(failures)
        return 1;
    std::cout << "OK\n";
    return 0;
}
