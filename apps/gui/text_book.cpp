#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_pages.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// Text, Markdown and HTML files read as one long web page, laid out and paged by the EPUB engine (epub_book.cpp).
// A text or Markdown file is turned into HTML here; an HTML file is used as it is, with the pictures and style
// sheets next to it. Whatever the source, it ends up as UTF-8.
namespace
{
    // Longer than this isn't a thing to read in one piece.
    const size_t kMaxTextBytes = 16u * 1024 * 1024;
    // Quotes, lists and emphasis nest by recursion: past this depth the rest is plain text, so a file of thousands
    // of '>' can't run out of stack.
    const int kMaxDepth = 32;
    thread_local int t_depth = 0;

    // One level deeper while it lives.
    struct Nested
    {
        Nested() { t_depth++; }
        ~Nested() { t_depth--; }
        bool TooDeep() const { return t_depth > kMaxDepth; }
    };

    std::string Lowercase(std::string text)
    {
        for (char &c : text)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return text;
    }

    bool ReadWhole(const std::wstring &path, std::string &bytes)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size;
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= static_cast<LONGLONG>(kMaxTextBytes);
        if (ok)
        {
            bytes.resize(static_cast<size_t>(size.QuadPart));
            DWORD read = 0;
            ok = bytes.empty() || (ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) &&
                read == bytes.size());
        }
        CloseHandle(file);
        return ok;
    }

    bool IsUtf8(const std::string &bytes)
    {
        size_t i = 0;
        while (i < bytes.size())
        {
            unsigned char c = static_cast<unsigned char>(bytes[i]);
            size_t more = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : SIZE_MAX;
            if (more == SIZE_MAX || i + more >= bytes.size())
                return false;
            for (size_t k = 1; k <= more; k++)
            {
                if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0) != 0x80)
                    return false;
            }
            i += more + 1;
        }
        return true;
    }

    // The code page a charset name in a <meta> stands for; 0 when it isn't known.
    UINT CodePageOf(const std::string &name)
    {
        unsigned number = 0;
        if (name == "utf-8" || name == "utf8")
            return CP_UTF8;
        if (name == "us-ascii" || name == "ascii" || name == "latin1" || name == "iso-8859-1" || name == "l1")
            return 1252;
        if (sscanf(name.c_str(), "windows-%u", &number) == 1 || sscanf(name.c_str(), "cp%u", &number) == 1)
            return number;
        if (sscanf(name.c_str(), "iso-8859-%u", &number) == 1 && number >= 2 && number <= 15)
            return 28590 + number;
        if (name == "shift_jis" || name == "shift-jis" || name == "sjis")
            return 932;
        if (name == "gbk" || name == "gb2312")
            return 936;
        if (name == "big5")
            return 950;
        if (name == "euc-kr")
            return 949;
        if (name == "koi8-r")
            return 20866;
        return 0;
    }

    std::string CharsetNamed(const std::string &bytes)
    {
        std::string head = Lowercase(bytes.substr(0, 2048));
        size_t at = head.find("charset");
        if (at == std::string::npos)
            return std::string();
        at = head.find('=', at);
        if (at == std::string::npos)
            return std::string();
        at = head.find_first_not_of(" \t\"'", at + 1);
        size_t end = at == std::string::npos ? at : head.find_first_of(" \t\"';>/", at);
        return at == std::string::npos ? std::string() : head.substr(at, end - at);
    }

    std::string FromCodePage(const std::string &bytes, UINT codePage)
    {
        if (bytes.empty())
            return std::string();
        int length = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (length <= 0)
            return std::string();
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), &wide[0], length);
        return WideToUtf8(wide);
    }

    std::string FromUtf32(const std::string &bytes, bool bigEndian)
    {
        std::wstring wide;
        for (size_t i = 4; i + 3 < bytes.size(); i += 4)
        {
            const unsigned char *b = reinterpret_cast<const unsigned char *>(bytes.data() + i);
            unsigned long c = 0;
            for (int k = 0; k < 4; k++)
                c = c << 8 | b[bigEndian ? k : 3 - k];
            if (c >= 0x10000 && c <= 0x10FFFF)
            {
                wide += static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10));
                wide += static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF));
            }
            else
                wide += c < 0x10000 ? static_cast<wchar_t>(c) : L'\xFFFD';
        }
        return WideToUtf8(wide);
    }

    std::string FromUtf16(const std::string &bytes, size_t from, bool bigEndian)
    {
        std::wstring wide;
        for (size_t i = from; i + 1 < bytes.size(); i += 2)
        {
            unsigned char a = static_cast<unsigned char>(bytes[i]);
            unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
            wide += static_cast<wchar_t>(bigEndian ? (a << 8 | b) : (b << 8 | a));
        }
        return WideToUtf8(wide);
    }

    // The text of the file as UTF-8: by its byte order mark, else UTF-8 if it is valid UTF-8 (or UTF-16 if every
    // other byte of its start is 0), else the charset an HTML file names, else the Windows ANSI code page.
    std::string DecodeText(const std::string &bytes, bool html)
    {
        if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
            return bytes.substr(3);
        // UTF-32 first: its little-endian mark starts like UTF-16's.
        if (bytes.compare(0, 4, std::string("\xFF\xFE\0\0", 4)) == 0)
            return FromUtf32(bytes, false);
        if (bytes.compare(0, 4, std::string("\0\0\xFE\xFF", 4)) == 0)
            return FromUtf32(bytes, true);
        if (bytes.compare(0, 2, "\xFF\xFE") == 0)
            return FromUtf16(bytes, 2, false);
        if (bytes.compare(0, 2, "\xFE\xFF") == 0)
            return FromUtf16(bytes, 2, true);
        if (bytes.size() >= 4 && bytes[0] != 0 && bytes[1] == 0 && bytes[2] != 0 && bytes[3] == 0)
            return FromUtf16(bytes, 0, false);
        if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] != 0 && bytes[2] == 0 && bytes[3] != 0)
            return FromUtf16(bytes, 0, true);
        if (IsUtf8(bytes))
            return bytes;
        // A code page Windows can't convert from gives nothing: the ANSI code page then, rather than a blank book.
        UINT codePage = html ? CodePageOf(CharsetNamed(bytes)) : 0;
        std::string text = FromCodePage(bytes, codePage != 0 ? codePage : CP_ACP);
        if (text.empty() && codePage != 0)
            text = FromCodePage(bytes, CP_ACP);
        return text;
    }

    std::string Escape(const std::string &text)
    {
        std::string result;
        result.reserve(text.size() + text.size() / 8);
        for (char c : text)
        {
            switch (c)
            {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += c;
            }
        }
        return result;
    }

    // The page the pieces go in: the reader's look for what the book styles don't give.
    std::string Page(const std::string &body)
    {
        return "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>\n"
            "p { margin: 0 0 0.9em 0; }\n"
            "h1, h2, h3, h4, h5, h6 { margin: 1.2em 0 0.5em 0; }\n"
            "pre { white-space: pre-wrap; margin: 0 0 0.9em 0; }\n"
            "code, pre { font-family: Consolas, monospace; font-size: 0.9em; }\n"
            "blockquote { margin: 0 0 0.9em 0.5em; padding-left: 1em; border-left: 3px solid #888; }\n"
            "ul, ol { margin: 0 0 0.9em 0; }\n"
            "li ul, li ol { margin: 0; }\n"
            "hr { border: 0; border-top: 1px solid #888; margin: 1.2em 0; }\n"
            "</style></head><body>" + body + "</body></html>";
    }

    // ---- Plain text ----

    bool Blank(const std::string &line)
    {
        return line.find_first_not_of(" \t") == std::string::npos;
    }

    std::vector<std::string> Lines(const std::string &text)
    {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            size_t stop = end;
            if (stop > start && text[stop - 1] == '\r')
                stop--;
            lines.push_back(text.substr(start, stop - start));
            start = end + 1;
        }
        return lines;
    }

    // Paragraphs are what blank lines separate. A paragraph whose lines were broken by hand at about the same
    // width (e-mail, Project Gutenberg) is joined to flow with the page; any other keeps its line breaks and
    // indentation (poems, code, tables of figures).
    std::string TextToHtml(const std::string &text)
    {
        std::vector<std::string> lines = Lines(text);
        size_t longest = 0;
        for (const std::string &line : lines)
            longest = std::max(longest, line.size());
        bool wrappedByHand = longest >= 50 && longest <= 110;

        std::string body;
        size_t i = 0;
        while (i < lines.size())
        {
            if (Blank(lines[i]))
            {
                i++;
                continue;
            }
            size_t end = i;
            while (end < lines.size() && !Blank(lines[end]))
                end++;
            bool join = wrappedByHand && end - i > 1;
            for (size_t k = i; k < end && join; k++)
            {
                bool indented = lines[k][0] == ' ' || lines[k][0] == '\t';
                // Every line but the last runs nearly to the margin.
                if (indented || (k + 1 < end && lines[k].size() * 10 < longest * 6))
                    join = false;
            }
            std::string block;
            for (size_t k = i; k < end; k++)
            {
                if (k > i)
                    block += join ? " " : "\n";
                block += Escape(lines[k]);
            }
            body += join ? "<p>" : "<p style=\"white-space: pre-wrap\">";
            body += block;
            body += "</p>\n";
            i = end;
        }
        return Page(body);
    }

    // ---- Markdown ----

    size_t Indent(const std::string &line)
    {
        size_t width = 0;
        for (char c : line)
        {
            if (c == ' ')
                width++;
            else if (c == '\t')
                width += 4 - width % 4;
            else
                break;
        }
        return width;
    }

    // The line without 'width' columns of indentation.
    std::string Unindent(const std::string &line, size_t width)
    {
        size_t column = 0, i = 0;
        while (i < line.size() && column < width && (line[i] == ' ' || line[i] == '\t'))
        {
            column += line[i] == '\t' ? 4 - column % 4 : 1;
            i++;
        }
        return line.substr(i);
    }

    std::string Trim(const std::string &text)
    {
        size_t begin = text.find_first_not_of(" \t");
        if (begin == std::string::npos)
            return std::string();
        return text.substr(begin, text.find_last_not_of(" \t") + 1 - begin);
    }

    // A "---", "***" or "___" line, with spaces allowed between.
    bool IsRule(const std::string &line)
    {
        std::string text = Trim(line);
        if (text.empty() || (text[0] != '-' && text[0] != '*' && text[0] != '_'))
            return false;
        size_t count = 0;
        for (char c : text)
        {
            if (c == text[0])
                count++;
            else if (c != ' ' && c != '\t')
                return false;
        }
        return count >= 3;
    }

    // The heading level of "## Title"; 0 when it isn't one. 'title' gets the text without the closing #s.
    int HeadingLevel(const std::string &line, std::string &title)
    {
        if (Indent(line) >= 4)
            return 0;
        std::string text = Trim(line);
        size_t level = 0;
        while (level < text.size() && text[level] == '#')
            level++;
        if (level == 0 || level > 6 || (level < text.size() && text[level] != ' ' && text[level] != '\t'))
            return 0;
        title = Trim(text.substr(level));
        while (!title.empty() && title.back() == '#')
            title.pop_back();
        title = Trim(title);
        return static_cast<int>(level);
    }

    // The marker of a list item ("- ", "3. "): its width, and whether the list is numbered; 0 when it isn't one.
    size_t ListMarker(const std::string &line, bool &ordered, std::string &number)
    {
        size_t at = Indent(line) >= 4 ? SIZE_MAX : line.find_first_not_of(" \t");
        if (at == std::string::npos || at == SIZE_MAX)
            return 0;
        size_t i = at;
        if ((line[i] == '-' || line[i] == '*' || line[i] == '+') && i + 1 < line.size() &&
            (line[i + 1] == ' ' || line[i + 1] == '\t'))
        {
            ordered = false;
            return i + 2;
        }
        size_t digits = 0;
        while (i + digits < line.size() && isdigit(static_cast<unsigned char>(line[i + digits])) && digits < 9)
            digits++;
        i += digits;
        if (digits > 0 && i + 1 < line.size() && (line[i] == '.' || line[i] == ')') &&
            (line[i + 1] == ' ' || line[i + 1] == '\t'))
        {
            ordered = true;
            number = line.substr(at, digits);
            return i + 2;
        }
        return 0;
    }

    bool IsFence(const std::string &line, char &mark)
    {
        std::string text = Trim(line);
        if (Indent(line) >= 4 || text.size() < 3 || (text[0] != '`' && text[0] != '~') ||
            text.compare(0, 3, std::string(3, text[0])) != 0)
            return false;
        mark = text[0];
        return true;
    }

    // Any file name with a scheme ("https:", "data:") is on the web or not a file at all.
    bool IsLocal(const std::string &url)
    {
        size_t colon = url.find(':');
        return !url.empty() && (colon == std::string::npos || colon > url.find('/'));
    }

    std::string Inline(const std::string &text);

    // The end of the emphasis opened at 'from' with 'mark' (one or two characters); npos when it isn't closed.
    size_t EmphasisEnd(const std::string &text, size_t from, const std::string &mark)
    {
        for (size_t at = from; (at = text.find(mark, at)) != std::string::npos; at++)
        {
            if (at == from || text[at - 1] == ' ' || text[at - 1] == '\\')
                continue;
            // "**" inside "***" or an underscore inside a word closes nothing.
            if (mark[0] == '_' && at + mark.size() < text.size() && isalnum(static_cast<unsigned char>(text[at + mark.size()])))
                continue;
            if (mark.size() == 1 && at + 1 < text.size() && text[at + 1] == mark[0])
            {
                at++;
                continue;
            }
            return at;
        }
        return std::string::npos;
    }

    // Emphasis, code, links and pictures inside a paragraph or heading, escaped. A link is text in the link
    // color (nothing opens it); a picture is the picture of a local file, or else its description.
    std::string Inline(const std::string &text)
    {
        Nested nested;
        if (nested.TooDeep())
            return Escape(text);
        std::string result;
        for (size_t i = 0; i < text.size(); i++)
        {
            char c = text[i];
            if (c == '\\' && i + 1 < text.size() && ispunct(static_cast<unsigned char>(text[i + 1])))
            {
                result += Escape(std::string(1, text[++i]));
            }
            else if (c == '`')
            {
                size_t ticks = 1;
                while (i + ticks < text.size() && text[i + ticks] == '`')
                    ticks++;
                size_t close = text.find(std::string(ticks, '`'), i + ticks);
                if (close == std::string::npos)
                {
                    result.append(ticks, '`');
                    i += ticks - 1;
                    continue;
                }
                result += "<code>" + Escape(Trim(text.substr(i + ticks, close - i - ticks))) + "</code>";
                i = close + ticks - 1;
            }
            else if ((c == '*' || c == '_' || c == '~') && i + 1 < text.size() && text[i + 1] != ' ')
            {
                std::string mark(1, c);
                if (text[i + 1] == c)
                    mark += c;
                // "snake_case" has no emphasis, and a lone "~" is just a tilde.
                bool intraword = c == '_' && i > 0 && isalnum(static_cast<unsigned char>(text[i - 1]));
                size_t from = i + mark.size();
                size_t close = intraword || (c == '~' && mark.size() == 1) || from >= text.size() ||
                    text[from] == ' ' ? std::string::npos : EmphasisEnd(text, from, mark);
                if (close == std::string::npos)
                {
                    result += c;
                    continue;
                }
                const char *tag = c == '~' ? "s" : mark.size() == 2 ? "strong" : "em";
                result += std::string("<") + tag + ">" + Inline(text.substr(from, close - from)) + "</" + tag + ">";
                i = close + mark.size() - 1;
            }
            else if (c == '[' || (c == '!' && i + 1 < text.size() && text[i + 1] == '['))
            {
                bool picture = c == '!';
                size_t open = i + (picture ? 1 : 0);
                size_t depth = 0, close = std::string::npos;
                for (size_t k = open; k < text.size(); k++)
                {
                    if (text[k] == '\\')
                        k++;
                    else if (text[k] == '[')
                        depth++;
                    else if (text[k] == ']' && --depth == 0)
                    {
                        close = k;
                        break;
                    }
                }
                size_t end = close != std::string::npos && close + 1 < text.size() && text[close + 1] == '(' ?
                    text.find(')', close + 2) : std::string::npos;
                if (end == std::string::npos)
                {
                    result += Escape(std::string(1, c));
                    continue;
                }
                std::string label = text.substr(open + 1, close - open - 1);
                std::string url = Trim(text.substr(close + 2, end - close - 2));
                // A title after the address ("url \"title\"") isn't part of it; "<url>" drops its brackets.
                size_t space = url.find_first_of(" \t");
                if (space != std::string::npos)
                    url.resize(space);
                if (url.size() > 1 && url.front() == '<' && url.back() == '>')
                    url = url.substr(1, url.size() - 2);
                if (picture && IsLocal(url))
                    result += "<img src=\"" + Escape(url) + "\" alt=\"" + Escape(label) + "\">";
                else if (picture)
                    result += Escape(label);
                else
                    result += "<a href=\"" + Escape(url) + "\">" + Inline(label) + "</a>";
                i = end;
            }
            else if (c == '<' && text.compare(i, 4, "<br>") == 0)
            {
                result += "<br>";
                i += 3;
            }
            else if (c == '<' && (text.compare(i, 5, "<http") == 0 || text.compare(i, 6, "<mailt") == 0) &&
                text.find('>', i) != std::string::npos)
            {
                size_t close = text.find('>', i);
                result += "<a href=\"" + Escape(text.substr(i + 1, close - i - 1)) + "\">" +
                    Escape(text.substr(i + 1, close - i - 1)) + "</a>";
                i = close;
            }
            else if (c == '\n' && i >= 2 && text[i - 1] == ' ' && text[i - 2] == ' ')
            {
                result += "<br>\n";
            }
            else
                result += Escape(std::string(1, c));
        }
        return result;
    }

    // 'tight' leaves a paragraph without its <p>, as the text of a list item whose items have no blank lines
    // between them.
    std::string Blocks(const std::vector<std::string> &lines, bool tight);

    std::string Paragraph(const std::vector<std::string> &lines, bool tight)
    {
        std::string text;
        for (size_t i = 0; i < lines.size(); i++)
        {
            std::string line = Trim(lines[i]);
            // A hard break is two spaces or a backslash at the end of the line.
            bool breaks = lines[i].size() >= 2 && lines[i].compare(lines[i].size() - 2, 2, "  ") == 0;
            if (!line.empty() && line.back() == '\\' && (line.size() < 2 || line[line.size() - 2] != '\\'))
            {
                line.pop_back();
                breaks = true;
            }
            text += line;
            if (i + 1 < lines.size())
                text += breaks ? "  \n" : "\n";
        }
        return tight ? Inline(text) + "\n" : "<p>" + Inline(text) + "</p>\n";
    }

    std::string List(const std::vector<std::string> &lines, size_t &at, bool tight)
    {
        bool ordered = false;
        std::string number;
        ListMarker(lines[at], ordered, number);
        std::string html = ordered ? "<ol" + (number.empty() || number == "1" ? std::string() : " start=\"" + number + "\"") + ">\n"
                                   : "<ul>\n";
        bool loose = false;
        std::vector<std::vector<std::string>> items;
        size_t base = Indent(lines[at]);
        while (at < lines.size())
        {
            bool nextOrdered = false;
            std::string nextNumber;
            size_t marker = ListMarker(lines[at], nextOrdered, nextNumber);
            if (marker == 0 || Indent(lines[at]) != base || nextOrdered != ordered)
                break;
            // The item: its first line without the marker, and what follows indented past it.
            std::vector<std::string> item = { lines[at].substr(marker) };
            at++;
            while (at < lines.size())
            {
                if (Blank(lines[at]))
                {
                    size_t next = at;
                    while (next < lines.size() && Blank(lines[next]))
                        next++;
                    if (next >= lines.size() || Indent(lines[next]) <= base)
                    {
                        // A blank line before another item makes the list loose.
                        bool o = false;
                        std::string n;
                        if (next < lines.size() && Indent(lines[next]) == base && ListMarker(lines[next], o, n) && o == ordered)
                            loose = true;
                        at = next;
                        break;
                    }
                    loose = true;
                    for (; at < next; at++)
                        item.push_back(std::string());
                    continue;
                }
                bool o = false;
                std::string n;
                if (Indent(lines[at]) > base)
                    item.push_back(Unindent(lines[at], base + 2));
                else if (Indent(lines[at]) <= base && (ListMarker(lines[at], o, n) || IsRule(lines[at])))
                    break;
                else
                    item.push_back(lines[at]);  // a lazy continuation of the item's paragraph
                at++;
            }
            items.push_back(item);
        }
        for (const std::vector<std::string> &item : items)
            html += "<li>" + Blocks(item, tight || !loose) + "</li>\n";
        html += ordered ? "</ol>\n" : "</ul>\n";
        return html;
    }

    std::string Blocks(const std::vector<std::string> &lines, bool tight)
    {
        Nested nested;
        if (nested.TooDeep())
        {
            std::string text;
            for (const std::string &line : lines)
                text += Escape(line) + "\n";
            return "<p>" + text + "</p>\n";
        }
        std::string html;
        size_t i = 0;
        while (i < lines.size())
        {
            const std::string &line = lines[i];
            std::string title;
            char mark = 0;
            bool ordered = false;
            std::string number;
            if (Blank(line))
            {
                i++;
            }
            else if (IsFence(line, mark))
            {
                std::string code;
                for (i++; i < lines.size(); i++)
                {
                    char closing = 0;
                    if (IsFence(lines[i], closing) && closing == mark && Trim(lines[i]).find_first_not_of(mark) == std::string::npos)
                        break;
                    code += Escape(lines[i]) + "\n";
                }
                i++;
                html += "<pre><code>" + code + "</code></pre>\n";
            }
            else if (int level = HeadingLevel(line, title))
            {
                html += "<h" + std::to_string(level) + ">" + Inline(title) + "</h" + std::to_string(level) + ">\n";
                i++;
            }
            else if (IsRule(line) && !ListMarker(line, ordered, number))
            {
                html += "<hr>\n";
                i++;
            }
            else if (Indent(line) >= 4)
            {
                std::string code;
                size_t last = i;
                for (size_t k = i; k < lines.size() && (Indent(lines[k]) >= 4 || Blank(lines[k])); k++)
                {
                    if (!Blank(lines[k]))
                        last = k;
                }
                for (; i <= last; i++)
                    code += Escape(Unindent(lines[i], 4)) + "\n";
                html += "<pre><code>" + code + "</code></pre>\n";
            }
            else if (Trim(line)[0] == '>')
            {
                std::vector<std::string> inner;
                for (; i < lines.size() && !Blank(lines[i]) && (Trim(lines[i])[0] == '>' || !inner.empty()); i++)
                {
                    std::string text = Trim(lines[i]);
                    if (text[0] == '>')
                    {
                        text.erase(0, 1);
                        if (!text.empty() && text[0] == ' ')
                            text.erase(0, 1);
                    }
                    inner.push_back(text);
                }
                html += "<blockquote>\n" + Blocks(inner, false) + "</blockquote>\n";
            }
            else if (ListMarker(line, ordered, number))
            {
                html += List(lines, i, tight);
            }
            else
            {
                std::vector<std::string> paragraph;
                int setext = 0;
                for (; i < lines.size() && !Blank(lines[i]); i++)
                {
                    bool o = false;
                    std::string n;
                    if (!paragraph.empty())
                    {
                        // "Title" over "=====" or "-----" is a heading.
                        std::string rule = Trim(lines[i]);
                        if (!rule.empty() && rule.find_first_not_of('=') == std::string::npos)
                            setext = 1;
                        else if (!rule.empty() && rule.find_first_not_of('-') == std::string::npos)
                            setext = 2;
                        if (setext)
                        {
                            i++;
                            break;
                        }
                        char m = 0;
                        if (HeadingLevel(lines[i], title) || IsFence(lines[i], m) || Trim(lines[i])[0] == '>' ||
                            IsRule(lines[i]) || ListMarker(lines[i], o, n))
                            break;
                    }
                    paragraph.push_back(lines[i]);
                }
                if (setext)
                {
                    std::string text;
                    for (const std::string &part : paragraph)
                        text += (text.empty() ? "" : " ") + Trim(part);
                    html += "<h" + std::to_string(setext) + ">" + Inline(text) + "</h" + std::to_string(setext) + ">\n";
                }
                else
                    html += Paragraph(paragraph, tight);
            }
        }
        return html;
    }

    std::string MarkdownToHtml(const std::string &text)
    {
        return Page(Blocks(Lines(text), false));
    }

    // "C:\Notes\a.md" gives "C:\Notes\".
    std::wstring FolderOf(const std::wstring &path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
    }

    std::string Extension(const std::wstring &path)
    {
        size_t dot = path.find_last_of(L'.');
        size_t slash = path.find_last_of(L"\\/");
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
            return std::string();
        return Lowercase(WideToUtf8(path.substr(dot)));
    }
}

std::unique_ptr<BookPages> OpenTextBook(IWICImagingFactory *wic, const std::wstring &path)
{
    std::string bytes;
    if (!ReadWhole(path, bytes))
        return nullptr;
    std::string extension = Extension(path);
    bool html = extension == ".html" || extension == ".htm";
    std::string text = DecodeText(bytes, html);
    if (html)
        return OpenHtmlBook(wic, std::move(text), FolderOf(path));
    bool markdown = extension == ".md" || extension == ".markdown";
    return OpenHtmlBook(wic, markdown ? MarkdownToHtml(text) : TextToHtml(text), FolderOf(path));
}
