#ifndef LICENSES_HPP
#define LICENSES_HPP

#include <string>
#include <vector>

// The licenses of the code and fonts inside corpus-gui.exe, for the page hidden in Settings. The texts are built
// into the .exe: the licenses of third_party/ as resources, the rest as strings.
struct LicenseSection
{
    std::wstring name;     // "SQLite"
    std::wstring summary;  // one line: what it is used for and under which license
    std::string text;      // notices, source links and license texts, UTF-8, lines ending in \n
};

// Built once, on first use.
const std::vector<LicenseSection> &Licenses();

#endif
