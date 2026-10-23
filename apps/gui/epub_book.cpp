#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_pages.hpp"
#include "html_canvas.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "zip_folder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

// An EPUB is a zip of XHTML chapters with their style sheets and pictures. META-INF/container.xml names the
// package file (.opf), whose spine lists the chapters in reading order. Each chapter is laid out by litehtml as
// one long column the width of a line, then cut into pages between lines; a chapter starts on a page of its own.
// The pages are drawn as light text on the viewer's dark backdrop, whatever colors the book asks for.
namespace
{
    const size_t kMaxFileBytes = 64u * 1024 * 1024;
    // The documents laid out last are kept for drawing their pages: the chapter being read and the one before.
    const size_t kKeptDocuments = 2;
    // The path a page that isn't in a zip goes by, for the links and pictures it names.
    const char *const kPagePath = "page.html";
    // Never smaller than this, so a tiny window still gets a page that holds a line.
    const float kMinPageSide = 120.0f;

    std::string Lowercase(std::string text)
    {
        for (char &c : text)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return text;
    }

    bool StartsWith(const std::string &text, size_t at, const char *prefix)
    {
        size_t length = strlen(prefix);
        if (at + length > text.size())
            return false;
        for (size_t i = 0; i < length; i++)
        {
            char c = text[at + i];
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            if (c != prefix[i])
                return false;
        }
        return true;
    }

    std::string Hex(const D2D1_COLOR_F &color)
    {
        char text[8];
        snprintf(text, sizeof(text), "#%02x%02x%02x", static_cast<unsigned>(std::lround(color.r * 255.0f)),
            static_cast<unsigned>(std::lround(color.g * 255.0f)), static_cast<unsigned>(std::lround(color.b * 255.0f)));
        return text;
    }

    // "&amp;" and the other entities XML has without a DTD.
    std::string DecodeEntities(const std::string &text)
    {
        static const std::pair<const char *, char> kEntities[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
            { "&quot;", '"' }, { "&apos;", '\'' } };
        std::string result;
        for (size_t i = 0; i < text.size(); i++)
        {
            bool replaced = false;
            if (text[i] == '&')
            {
                for (const auto &entity : kEntities)
                {
                    if (text.compare(i, strlen(entity.first), entity.first) == 0)
                    {
                        result += entity.second;
                        i += strlen(entity.first) - 1;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced)
                result += text[i];
        }
        return result;
    }

    // An XML or HTML start or end tag: its name and attributes in lowercase, without namespace prefixes
    // ("opf:item" is "item", "xlink:href" is "href").
    struct Tag
    {
        std::string name;
        std::map<std::string, std::string> attributes;
        size_t begin = 0;  // the '<'
        size_t end = 0;    // past the '>'
        bool closing = false;      // </name>
        bool selfClosing = false;  // <name/>

        std::string Attribute(const char *key) const
        {
            auto it = attributes.find(key);
            return it == attributes.end() ? std::string() : it->second;
        }
    };

    std::string LocalName(const std::string &name)
    {
        size_t colon = name.find(':');
        return Lowercase(colon == std::string::npos ? name : name.substr(colon + 1));
    }

    bool IsNameChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':' || c == '-' ||
            c == '_' || c == '.';
    }

    // The next tag from 'at' on, past comments, CDATA, declarations and processing instructions.
    bool NextTag(const std::string &text, size_t &at, Tag &tag)
    {
        while ((at = text.find('<', at)) != std::string::npos)
        {
            if (text.compare(at, 4, "<!--") == 0)
            {
                size_t end = text.find("-->", at + 4);
                at = end == std::string::npos ? text.size() : end + 3;
                continue;
            }
            if (text.compare(at, 9, "<![CDATA[") == 0)
            {
                size_t end = text.find("]]>", at + 9);
                at = end == std::string::npos ? text.size() : end + 3;
                continue;
            }
            size_t i = at + 1;
            bool closing = i < text.size() && text[i] == '/';
            if (closing)
                i++;
            if (i >= text.size() || !((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z')))
            {
                at++;
                continue;
            }
            size_t nameStart = i;
            while (i < text.size() && IsNameChar(text[i]))
                i++;
            tag = Tag();
            tag.begin = at;
            tag.closing = closing;
            tag.name = LocalName(text.substr(nameStart, i - nameStart));
            // The attributes, up to the '>' that isn't inside quotes.
            for (;;)
            {
                while (i < text.size() && isspace(static_cast<unsigned char>(text[i])))
                    i++;
                if (i >= text.size())
                    return false;
                if (text[i] == '>')
                {
                    tag.end = i + 1;
                    break;
                }
                if (text[i] == '/')
                {
                    tag.selfClosing = true;
                    i++;
                    continue;
                }
                size_t keyStart = i;
                while (i < text.size() && !isspace(static_cast<unsigned char>(text[i])) && text[i] != '=' &&
                    text[i] != '>' && text[i] != '/')
                    i++;
                std::string key = LocalName(text.substr(keyStart, i - keyStart));
                if (i == keyStart)
                {
                    i++;
                    continue;
                }
                tag.selfClosing = false;
                while (i < text.size() && isspace(static_cast<unsigned char>(text[i])))
                    i++;
                std::string value;
                if (i < text.size() && text[i] == '=')
                {
                    i++;
                    while (i < text.size() && isspace(static_cast<unsigned char>(text[i])))
                        i++;
                    if (i < text.size() && (text[i] == '"' || text[i] == '\''))
                    {
                        size_t close = text.find(text[i], i + 1);
                        if (close == std::string::npos)
                            return false;
                        value = text.substr(i + 1, close - i - 1);
                        i = close + 1;
                    }
                    else
                    {
                        size_t valueStart = i;
                        while (i < text.size() && !isspace(static_cast<unsigned char>(text[i])) && text[i] != '>')
                            i++;
                        value = text.substr(valueStart, i - valueStart);
                    }
                }
                tag.attributes[key] = DecodeEntities(value);
            }
            at = tag.end;
            return true;
        }
        at = text.size();
        return false;
    }

    bool IsVoidElement(const std::string &name)
    {
        static const char *const kVoid[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
            "param", "source", "track", "wbr" };
        for (const char *known : kVoid)
        {
            if (name == known)
                return true;
        }
        return false;
    }

    // Makes a chapter's XHTML read right as HTML, which is what litehtml parses: "<a id="p12"/>" would open a link
    // that runs to the end of the chapter, so empty elements written the XML way are closed. A cover drawn as SVG
    // around a picture (litehtml draws no SVG) becomes the picture. The encoding the XML declaration names, if
    // any, goes to 'encoding'.
    std::string PrepareXhtml(const std::string &xhtml, std::string &encoding)
    {
        encoding.clear();
        if (xhtml.compare(0, 5, "<?xml") == 0)
        {
            size_t end = xhtml.find("?>");
            size_t at = xhtml.find("encoding", 0);
            if (end != std::string::npos && at != std::string::npos && at < end)
            {
                size_t quote = xhtml.find_first_of("\"'", at);
                size_t close = quote == std::string::npos ? quote : xhtml.find(xhtml[quote], quote + 1);
                if (close != std::string::npos && close < end)
                    encoding = xhtml.substr(quote + 1, close - quote - 1);
            }
        }

        std::string html;
        html.reserve(xhtml.size() + xhtml.size() / 16);
        size_t copied = 0;
        size_t at = 0;
        Tag tag;
        while (NextTag(xhtml, at, tag))
        {
            if (tag.closing)
                continue;
            if (tag.name == "svg")
            {
                size_t close = std::string::npos;
                for (size_t from = tag.end; (from = xhtml.find("</", from)) != std::string::npos; from += 2)
                {
                    size_t name = from + 2;
                    size_t colon = xhtml.find_first_of(":>", name);
                    if (colon != std::string::npos && xhtml[colon] == ':')
                        name = colon + 1;
                    if (StartsWith(xhtml, name, "svg>"))
                    {
                        close = xhtml.find('>', name) + 1;
                        break;
                    }
                }
                std::string href;
                size_t inner = tag.end;
                Tag image;
                while (close != std::string::npos && NextTag(xhtml, inner, image) && inner <= close)
                {
                    if (!image.closing && image.name == "image")
                    {
                        href = image.Attribute("href");
                        break;
                    }
                }
                if (!href.empty())
                {
                    html.append(xhtml, copied, tag.begin - copied);
                    html += "<img src=\"";
                    for (char c : href)
                        html += c == '"' ? std::string("&quot;") : std::string(1, c);
                    html += "\" alt=\"\" style=\"display: block; margin: 0 auto\">";
                    copied = at = close;
                    continue;
                }
            }
            if (tag.selfClosing && !IsVoidElement(tag.name))
            {
                size_t slash = xhtml.rfind('/', tag.end - 1);
                html.append(xhtml, copied, slash - copied);
                html += "></";
                html += tag.name;
                html += '>';
                copied = tag.end;
            }
        }
        html.append(xhtml, copied, std::string::npos);
        return html;
    }

    // Where the pages of a chapter 'height' tall begin, each at most 'pageHeight': at the top of the first line
    // that would be cut, so a page ends between two lines. A picture or a line taller than a page is cut.
    std::vector<float> Paginate(std::vector<HtmlCanvas::Box> boxes, float height, float pageHeight)
    {
        std::sort(boxes.begin(), boxes.end(), [](const HtmlCanvas::Box &a, const HtmlCanvas::Box &b)
        {
            return a.top < b.top;
        });
        std::vector<float> tops = { 0.0f };
        float top = 0.0f;
        while (top + pageHeight < height - 0.5f)
        {
            float cut = top + pageHeight;
            // Raising the cut above a box can cut another one, beside it: again until none is.
            for (bool moved = true; moved;)
            {
                moved = false;
                for (const HtmlCanvas::Box &box : boxes)
                {
                    if (box.top >= cut)
                        break;
                    if (box.bottom > cut + 0.5f && box.top > top + 0.5f && box.top < cut - 0.5f)
                    {
                        cut = box.top;
                        moved = true;
                    }
                }
            }
            tops.push_back(cut);
            top = cut;
        }
        return tops;
    }

    class EpubBook : public BookPages
    {
    public:
        // A single web page that isn't in a zip (a text, Markdown or HTML file): 'html' is its UTF-8 text, and
        // 'folder' is where the pictures and style sheets it names are looked for.
        EpubBook(IWICImagingFactory *wic, std::string html, std::wstring folder)
            : m_wic(wic), m_page(std::move(html)), m_folder(std::move(folder)), m_loose(true)
        {
            m_chapters.push_back({ kPagePath });
        }

        EpubBook(IWICImagingFactory *wic, ZipFolder zip) : m_wic(wic), m_zip(std::move(zip))
        {
            for (const ZipFolder::Entry &entry : m_zip.Entries())
            {
                std::string name = WideToUtf8(entry.name);
                std::replace(name.begin(), name.end(), '\\', '/');
                m_files.emplace(Lowercase(name), &entry);
            }
        }

        // Reads the package, if there is one: the chapters in reading order. False when it isn't an EPUB or has
        // none, or the text can't be drawn.
        bool Load()
        {
            if (!m_loose && !LoadPackage())
                return false;
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2d.GetAddressOf())) ||
                FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown **>(m_dwrite.GetAddressOf()))))
                return false;
            m_canvas.reset(new HtmlCanvas(m_dwrite.Get(), m_wic, [this](const std::string &path, std::vector<BYTE> &bytes)
            {
                return Read(path, bytes);
            }));
            return true;
        }

        bool LoadPackage()
        {
            std::string container;
            if (!ReadText("META-INF/container.xml", container))
                return false;
            std::string package;
            size_t at = 0;
            Tag tag;
            while (package.empty() && NextTag(container, at, tag))
            {
                if (!tag.closing && tag.name == "rootfile")
                    package = HtmlCanvas::Resolve(std::string(), tag.Attribute("full-path"));
            }
            std::string opf;
            if (package.empty() || !ReadText(package, opf))
                return false;

            // The manifest names every file by id; the spine lists the ids of the chapters.
            std::unordered_map<std::string, std::string> chapters;
            std::vector<std::string> spine;
            at = 0;
            while (NextTag(opf, at, tag))
            {
                if (tag.closing)
                    continue;
                if (tag.name == "item")
                {
                    std::string type = Lowercase(tag.Attribute("media-type"));
                    if (type == "application/xhtml+xml" || type == "text/html" || type == "application/xml")
                        chapters[tag.Attribute("id")] = HtmlCanvas::Resolve(package, tag.Attribute("href"));
                }
                else if (tag.name == "itemref")
                    spine.push_back(tag.Attribute("idref"));
            }
            for (const std::string &id : spine)
            {
                auto chapter = chapters.find(id);
                if (chapter != chapters.end() && !chapter->second.empty() && m_files.count(Lowercase(chapter->second)))
                    m_chapters.push_back({ chapter->second });
            }
            return !m_chapters.empty();
        }

        ~EpubBook() override
        {
            // The documents hand their fonts back to the canvas as they go.
            m_documents.clear();
            m_canvas.reset();
        }

        size_t Count() const override { return m_count; }

        void Layout(UINT width, UINT height, float scale) override
        {
            if (width == 0 || height == 0 || scale <= 0.0f)
                return;
            if (m_count > 0 && width == m_viewWidth && height == m_viewHeight && scale == m_scale)
                return;
            m_viewWidth = width;
            m_viewHeight = height;
            m_scale = scale;
            float viewWidth = width / scale;
            float viewHeight = height / scale;
            m_pageWidth = std::max(kMinPageSide, std::min(viewWidth - 2.0f * theme::kReaderSideMargin,
                static_cast<float>(theme::kReaderMeasure)));
            m_marginLeft = std::max(0.0f, (viewWidth - m_pageWidth) / 2.0f);
            m_pageHeight = std::max(kMinPageSide, viewHeight - theme::kReaderTopMargin - theme::kReaderBottomMargin);
            m_marginTop = std::max(0.0f, std::min(static_cast<float>(theme::kReaderTopMargin), viewHeight - m_pageHeight));

            // The book's colors and margins give way to the reader's; a picture fits on a page.
            char pictures[96];
            snprintf(pictures, sizeof(pictures), "img { max-width: 100%% !important; max-height: %dpx !important; }\n",
                static_cast<int>(m_pageHeight));
            m_userStyles = "* { color: " + Hex(theme::kReaderText) + " !important; background-color: transparent !important; "
                "border-color: " + Hex(theme::kReaderRule) + " !important; }\n"
                "a, a * { color: " + Hex(theme::kReaderLink) + " !important; }\n"
                "html, body { margin: 0 !important; padding: 0 !important; }\n" + pictures;

            m_documents.clear();
            m_count = 0;
            for (size_t i = 0; i < m_chapters.size(); i++)
            {
                Chapter &chapter = m_chapters[i];
                chapter.first = m_count;
                chapter.height = 0.0f;
                chapter.tops.assign(1, 0.0f);
                litehtml::document::ptr document = Document(i);
                if (document)
                {
                    std::vector<HtmlCanvas::Box> boxes;
                    m_canvas->Measure(&boxes);
                    litehtml::position everything(-1e6f, -1e6f, 2e6f, 2e9f);
                    document->draw(0, 0, 0, &everything);
                    m_canvas->Measure(nullptr);
                    chapter.height = document->height();
                    chapter.tops = Paginate(std::move(boxes), chapter.height, m_pageHeight);
                }
                m_count += chapter.tops.size();
            }
        }

        bool Render(size_t index, UINT, UINT, DecodedFrame &out) override
        {
            if (index >= m_count)
                return false;
            size_t c = ChapterOf(index);
            const Chapter &chapter = m_chapters[c];
            size_t page = index - chapter.first;
            float top = chapter.tops[page];
            float bottom = page + 1 < chapter.tops.size() ? chapter.tops[page + 1] : top + m_pageHeight;
            litehtml::document::ptr document = Document(c);
            if (!document || !Target())
                return false;

            m_canvas->SetDocument(chapter.path, m_pageWidth, m_pageHeight, static_cast<float>(theme::kReaderFontSize));
            m_canvas->SetTarget(m_target.Get(), m_scale);
            m_target->BeginDraw();
            m_target->Clear(theme::kReaderPage);
            m_target->SetTransform(D2D1::Matrix3x2F::Translation(m_marginLeft, m_marginTop) *
                D2D1::Matrix3x2F::Scale(m_scale, m_scale));
            // Only down to where the next page begins: a line cut there shows on the next page, not half on both.
            litehtml::position clip(-m_marginLeft, 0.0f, m_pageWidth + 2.0f * m_marginLeft, bottom - top);
            m_target->PushAxisAlignedClip(D2D1::RectF(clip.left(), clip.top(), clip.right(), clip.bottom()),
                D2D1_ANTIALIAS_MODE_ALIASED);
            document->draw(1, 0.0f, -top, &clip);
            m_target->PopAxisAlignedClip();
            HRESULT hr = m_target->EndDraw();
            m_canvas->SetTarget(nullptr, m_scale);
            if (FAILED(hr))
            {
                m_target.Reset();
                m_bitmap.Reset();
                m_canvas->ForgetBitmaps();
                return false;
            }

            out = DecodedFrame();
            out.width = out.sourceWidth = m_viewWidth;
            out.height = out.sourceHeight = m_viewHeight;
            out.pixels.resize(static_cast<size_t>(m_viewWidth) * m_viewHeight * 4);
            return SUCCEEDED(m_bitmap->CopyPixels(nullptr, m_viewWidth * 4, static_cast<UINT>(out.pixels.size()),
                out.pixels.data()));
        }

        // "3:0.25000": the chapter, and how far down it the page begins.
        std::string Location(size_t index) const override
        {
            if (index >= m_count)
                return std::string();
            size_t c = ChapterOf(index);
            const Chapter &chapter = m_chapters[c];
            double fraction = chapter.height > 0.0f ? chapter.tops[index - chapter.first] / chapter.height : 0.0;
            char text[48];
            snprintf(text, sizeof(text), "%zu:%.5f", c, fraction);
            return text;
        }

        bool PageAt(const std::string &location, size_t &page) const override
        {
            unsigned long long c = 0;
            double fraction = 0.0;
            if (m_count == 0 || sscanf(location.c_str(), "%llu:%lf", &c, &fraction) != 2 || c >= m_chapters.size() ||
                !(fraction >= 0.0 && fraction <= 1.0))
                return false;
            const Chapter &chapter = m_chapters[static_cast<size_t>(c)];
            // The page that shows the place: the last one beginning at it or above.
            float y = static_cast<float>(fraction * chapter.height) + 0.5f;
            size_t inside = static_cast<size_t>(std::upper_bound(chapter.tops.begin(), chapter.tops.end(), y) -
                chapter.tops.begin());
            page = chapter.first + (inside > 0 ? inside - 1 : 0);
            return true;
        }

    private:
        struct Chapter
        {
            std::string path;
            std::vector<float> tops;  // where each of its pages begins, in CSS pixels from its top
            float height = 0.0f;
            size_t first = 0;         // its first page in the book
        };

        bool Read(const std::string &path, std::vector<BYTE> &bytes) const
        {
            if (m_loose)
                return ReadLoose(path, bytes);
            auto entry = m_files.find(Lowercase(path));
            return entry != m_files.end() && m_zip.Read(*entry->second, bytes, kMaxFileBytes);
        }

        // The page itself, or a file next to it. The paths are the page's as HtmlCanvas::Resolve makes them: never
        // outside the page's folder, and never with a scheme, so nothing is fetched from the web.
        bool ReadLoose(const std::string &path, std::vector<BYTE> &bytes) const
        {
            if (path == kPagePath)
            {
                bytes.assign(m_page.begin(), m_page.end());
                return true;
            }
            std::wstring file = m_folder;
            for (wchar_t c : Utf8ToWide(path))
                file += c == L'/' ? L'\\' : c;
            HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return false;
            LARGE_INTEGER size;
            bool ok = GetFileSizeEx(handle, &size) && size.QuadPart <= static_cast<LONGLONG>(kMaxFileBytes);
            if (ok)
            {
                bytes.resize(static_cast<size_t>(size.QuadPart));
                DWORD read = 0;
                ok = bytes.empty() || (ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
                    read == bytes.size());
            }
            CloseHandle(handle);
            return ok;
        }

        bool ReadText(const std::string &path, std::string &text) const
        {
            std::vector<BYTE> bytes;
            if (!Read(path, bytes))
                return false;
            text.assign(bytes.begin(), bytes.end());
            return true;
        }

        size_t ChapterOf(size_t index) const
        {
            auto after = std::upper_bound(m_chapters.begin(), m_chapters.end(), index,
                [](size_t page, const Chapter &chapter) { return page < chapter.first; });
            return static_cast<size_t>(after - m_chapters.begin()) - 1;
        }

        // Chapter 'c' laid out the width of a line, kept for a while; nullptr when it can't be read.
        litehtml::document::ptr Document(size_t c)
        {
            for (auto &kept : m_documents)
            {
                if (kept.first == c)
                    return kept.second;
            }
            std::string xhtml;
            if (!ReadText(m_chapters[c].path, xhtml))
                return nullptr;
            std::string encodingName;
            std::string html = m_loose ? xhtml : PrepareXhtml(xhtml, encodingName);
            // A page that isn't in a zip was turned into UTF-8 when it was read.
            litehtml::encoding encoding = encodingName.empty() ? litehtml::encoding::utf_8 :
                litehtml::get_encoding(encodingName);
            if (encoding == litehtml::encoding::null)
                encoding = litehtml::encoding::utf_8;

            m_canvas->SetDocument(m_chapters[c].path, m_pageWidth, m_pageHeight, static_cast<float>(theme::kReaderFontSize));
            litehtml::document::ptr document = litehtml::document::createFromString(
                litehtml::estring(html, encoding, litehtml::confidence::certain), m_canvas.get(), litehtml::master_css,
                m_userStyles);
            if (!document)
                return nullptr;
            document->render(m_pageWidth);
            if (m_documents.size() >= kKeptDocuments)
                m_documents.erase(m_documents.begin());
            m_documents.emplace_back(c, document);
            return document;
        }

        // The bitmap pages are drawn into, the size of the view.
        bool Target()
        {
            if (m_target)
            {
                UINT width = 0, height = 0;
                if (SUCCEEDED(m_bitmap->GetSize(&width, &height)) && width == m_viewWidth && height == m_viewHeight)
                    return true;
                m_target.Reset();
                m_bitmap.Reset();
                m_canvas->ForgetBitmaps();
            }
            if (!m_wic || FAILED(m_wic->CreateBitmap(m_viewWidth, m_viewHeight, GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapCacheOnLoad, m_bitmap.GetAddressOf())))
                return false;
            D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
            if (FAILED(m_d2d->CreateWicBitmapRenderTarget(m_bitmap.Get(), properties, m_target.GetAddressOf())))
            {
                m_bitmap.Reset();
                return false;
            }
            m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            m_canvas->ForgetBitmaps();
            return true;
        }

        IWICImagingFactory *m_wic;
        ZipFolder m_zip;
        std::string m_page;      // a page that isn't in a zip: its text
        std::wstring m_folder;   // and the folder it was in, ending in a backslash
        bool m_loose = false;
        std::unordered_map<std::string, const ZipFolder::Entry *> m_files;  // by lowercase path, '/' between folders
        std::vector<Chapter> m_chapters;
        size_t m_count = 0;

        ComPtr<ID2D1Factory> m_d2d;
        ComPtr<IDWriteFactory> m_dwrite;
        std::unique_ptr<HtmlCanvas> m_canvas;
        std::vector<std::pair<size_t, litehtml::document::ptr>> m_documents;  // the oldest first
        std::string m_userStyles;

        // The view in pixels, and the page in CSS pixels: the text, and the room left and above it.
        UINT m_viewWidth = 0;
        UINT m_viewHeight = 0;
        float m_scale = 1.0f;
        float m_pageWidth = 0.0f;
        float m_pageHeight = 0.0f;
        float m_marginLeft = 0.0f;
        float m_marginTop = 0.0f;

        ComPtr<IWICBitmap> m_bitmap;
        ComPtr<ID2D1RenderTarget> m_target;
    };
}

std::unique_ptr<BookPages> OpenEpubBook(IWICImagingFactory *wic, const std::wstring &path)
{
    ZipFolder zip;
    if (!zip.Open(path))
        return nullptr;
    std::unique_ptr<EpubBook> book(new EpubBook(wic, std::move(zip)));
    if (!book->Load())
        return nullptr;
    return std::unique_ptr<BookPages>(book.release());
}

std::unique_ptr<BookPages> OpenHtmlBook(IWICImagingFactory *wic, std::string html, const std::wstring &folder)
{
    std::unique_ptr<EpubBook> book(new EpubBook(wic, std::move(html), folder));
    if (!book->Load())
        return nullptr;
    return std::unique_ptr<BookPages>(book.release());
}
