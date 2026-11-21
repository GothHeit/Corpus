#include "../include/update_logic.hpp"
#include <cctype>
#include <cstdint>

namespace update_check
{
    bool parse_version(const std::string &text, int out[3])
    {
        size_t i = 0;
        if(i < text.size() && (text[i] == 'v' || text[i] == 'V'))
            i++;
        for(int part = 0; part < 3; part++)
        {
            size_t start = i;
            long value = 0;
            while(i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && i - start < 6)
                value = value * 10 + (text[i++] - '0');
            if(i == start)
                return false;
            out[part] = static_cast<int>(value);
            if(part < 2)
            {
                if(i >= text.size() || text[i] != '.')
                    return false;
                i++;
            }
        }
        return i == text.size();
    }

    int compare_versions(const int a[3], const int b[3])
    {
        for(int i = 0; i < 3; i++)
        {
            if(a[i] != b[i])
                return a[i] < b[i] ? -1 : 1;
        }
        return 0;
    }

    namespace
    {
        // Just enough JSON to find a few strings: it reads values, and skips the ones it doesn't want.
        class scanner
        {
        public:
            explicit scanner(const std::string &text) : s(text) {}

            bool at_end() { space(); return i >= s.size(); }
            bool peek(char c) { space(); return i < s.size() && s[i] == c; }
            bool eat(char c)
            {
                if(!peek(c))
                    return false;
                i++;
                return true;
            }

            bool string(std::string &out)
            {
                space();
                if(i >= s.size() || s[i] != '"')
                    return false;
                i++;
                out.clear();
                while(i < s.size())
                {
                    unsigned char c = static_cast<unsigned char>(s[i++]);
                    if(c == '"')
                        return true;
                    if(c < 0x20)
                        return false;
                    if(c != '\\')
                    {
                        out += static_cast<char>(c);
                        continue;
                    }
                    if(i >= s.size())
                        return false;
                    char e = s[i++];
                    switch(e)
                    {
                    case '"': case '\\': case '/': out += e; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if(!hex4(cp))
                            return false;
                        if(cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u')
                        {
                            i += 2;
                            std::uint32_t low = 0;
                            if(!hex4(low) || low < 0xDC00 || low > 0xDFFF)
                                return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: return false;
                    }
                }
                return false;
            }

            // Any value, to its end.
            bool skip(int depth = 0)
            {
                if(depth > 32)
                    return false;
                space();
                if(i >= s.size())
                    return false;
                char c = s[i];
                if(c == '"')
                {
                    std::string ignored;
                    return string(ignored);
                }
                if(c == '{' || c == '[')
                {
                    char close = c == '{' ? '}' : ']';
                    i++;
                    if(eat(close))
                        return true;
                    for(;;)
                    {
                        if(c == '{')
                        {
                            std::string key;
                            if(!string(key) || !eat(':'))
                                return false;
                        }
                        if(!skip(depth + 1))
                            return false;
                        if(eat(','))
                            continue;
                        return eat(close);
                    }
                }
                size_t start = i;
                while(i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !std::isspace(static_cast<unsigned char>(s[i])))
                    i++;
                return i > start;
            }

        private:
            void space()
            {
                while(i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                    i++;
            }

            bool hex4(std::uint32_t &out)
            {
                if(i + 4 > s.size())
                    return false;
                out = 0;
                for(int k = 0; k < 4; k++)
                {
                    char c = s[i++];
                    out <<= 4;
                    if(c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
                    else if(c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
                    else if(c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
                    else return false;
                }
                return true;
            }

            static void utf8(std::string &out, std::uint32_t cp)
            {
                if(cp < 0x80)
                    out += static_cast<char>(cp);
                else if(cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if(cp < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            const std::string &s;
            size_t i = 0;
        };

        // One element of "assets": takes its name and download link, skips the rest.
        bool read_asset(scanner &sc, asset &out)
        {
            if(!sc.eat('{'))
                return false;
            if(sc.eat('}'))
                return true;
            for(;;)
            {
                std::string key;
                if(!sc.string(key) || !sc.eat(':'))
                    return false;
                if((key == "name" || key == "browser_download_url") && sc.peek('"'))
                {
                    if(!sc.string(key == "name" ? out.name : out.url))
                        return false;
                }
                else if(!sc.skip())
                    return false;
                if(sc.eat(','))
                    continue;
                return sc.eat('}');
            }
        }
    }

    bool parse_release(const std::string &json, release &out)
    {
        out = release();
        scanner sc(json);
        if(!sc.eat('{'))
            return false;
        if(!sc.eat('}'))
        {
            for(;;)
            {
                std::string key;
                if(!sc.string(key) || !sc.eat(':'))
                    return false;
                if((key == "tag_name" || key == "html_url") && sc.peek('"'))
                {
                    if(!sc.string(key == "tag_name" ? out.tag : out.page_url))
                        return false;
                }
                else if(key == "assets" && sc.peek('['))
                {
                    sc.eat('[');
                    if(!sc.eat(']'))
                    {
                        for(;;)
                        {
                            asset a;
                            if(!read_asset(sc, a))
                                return false;
                            if(!a.name.empty() && !a.url.empty())
                                out.assets.push_back(std::move(a));
                            if(sc.eat(','))
                                continue;
                            if(!sc.eat(']'))
                                return false;
                            break;
                        }
                    }
                }
                else if(!sc.skip())
                    return false;
                if(sc.eat(','))
                    continue;
                if(!sc.eat('}'))
                    return false;
                break;
            }
        }
        return !out.tag.empty();
    }

    const asset *find_asset(const release &r, const std::string &name)
    {
        for(const asset &a : r.assets)
        {
            if(a.name == name)
                return &a;
        }
        return nullptr;
    }

    bool find_hash(const std::string &sums, const std::string &name, std::string &hex)
    {
        size_t pos = 0;
        while(pos < sums.size())
        {
            size_t end = sums.find('\n', pos);
            if(end == std::string::npos)
                end = sums.size();
            std::string line = sums.substr(pos, end - pos);
            pos = end + 1;
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            if(line.size() < 64 + 2 + 1)
                continue;
            std::string digest = line.substr(0, 64);
            bool valid = true;
            for(char &c : digest)
            {
                if(!std::isxdigit(static_cast<unsigned char>(c)))
                    valid = false;
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            // Two spaces (text mode) or a space and a star (binary mode) before the name.
            if(!valid || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
                continue;
            if(line.compare(66, std::string::npos, name) == 0)
            {
                hex = digest;
                return true;
            }
        }
        return false;
    }

    bool find_signed_version(const std::string &sums, int out[3])
    {
        bool found = false;
        size_t pos = 0;
        while(pos < sums.size())
        {
            size_t end = sums.find('\n', pos);
            if(end == std::string::npos)
                end = sums.size();
            std::string line = sums.substr(pos, end - pos);
            pos = end + 1;
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            if(line.compare(0, 8, "version ") != 0)
                continue;
            int v[3];
            if(!parse_version(line.substr(8), v))
                return false;
            if(found && compare_versions(v, out) != 0)
                return false;
            out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
            found = true;
        }
        return found;
    }

    bool update_acceptable(const std::string &sums, const std::string &tag, const int current[3])
    {
        int signed_version[3], tagged[3];
        return find_signed_version(sums, signed_version) && parse_version(tag, tagged)
            && compare_versions(signed_version, tagged) == 0 && compare_versions(signed_version, current) > 0;
    }

    bool is_repository_url(const std::string &url, const std::string &repo_path)
    {
        const std::string prefix = "https://github.com/" + repo_path + "/";
        return url.size() > prefix.size() && url.compare(0, prefix.size(), prefix) == 0
            && url.find_first_of(" \"<>\\\r\n") == std::string::npos;
    }
}
