#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "licenses.hpp"
#include "resource.h"
#include <windows.h>

namespace
{
    // A text built into the .exe as RCDATA; empty if it is missing.
    std::string Resource(int id)
    {
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
        HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
        const char *data = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
        return data ? std::string(data, SizeofResource(nullptr, resource)) : std::string();
    }

    // Keeps the last line of a text apart from what follows.
    std::string Block(std::string text)
    {
        if (!text.empty() && text.back() != '\n')
            text += '\n';
        return text + '\n';
    }

    const char *kMpvText = R"(Corpus plays videos with libmpv, the library of the mpv player, which is built from FFmpeg and other libraries. The libmpv-2.dll inside this program is a prebuilt copy (build 20261018 by shinchiro), used as it is and unmodified.

mpv is free software under the GNU General Public License, version 2 or later; FFmpeg, in this build, under the GNU General Public License too. Both are used here under version 3, whose full text follows these notices. It is also at
  https://www.gnu.org/licenses/gpl-3.0.txt

Source code:
  mpv       https://github.com/mpv-player/mpv
  FFmpeg    https://ffmpeg.org/download.html
  the build https://github.com/shinchiro/mpv-winbuild-cmake
  Corpus    https://github.com/GothHeit/Corpus

The client API header used to talk to libmpv is licensed under the ISC license:

Copyright (C) 2017 the mpv developers

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
)";

    const char *kSqliteText = R"(SQLite is in the public domain. Its authors disclaim copyright to the source code and, in place of a legal notice, give this blessing:

    May you do good and not evil.
    May you find forgiveness for yourself and forgive others.
    May you share freely, never taking more than you give.

https://www.sqlite.org/copyright.html
)";

    // The notices litehtml keeps in some of its files, beyond its own license.
    const char *kLitehtmlNotices = R"(Some files of litehtml carry notices of their own.

url.cpp, url_path.cpp, codepoint.cpp and their headers:

Copyright (C) 2020-2021 Primate Labs Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
   * Neither the names of the copyright holders nor the names of their
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

strtod.cpp:

Copyright (c) 1988-1993 The Regents of the University of California.
Copyright (c) 1994 Sun Microsystems, Inc.

Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies.  The University of California
makes no representations about the suitability of this
software for any purpose.  It is provided "as is" without
express or implied warranty.
)";

    // gumbo's utf8.c is based on a decoder with its own license.
    const char *kGumboNotices = R"(gumbo's utf8.c is based on a decoder by Bjoern Hoehrmann:

Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
)";
}

const std::vector<LicenseSection> &Licenses()
{
    static const std::vector<LicenseSection> sections = []
    {
        std::vector<LicenseSection> list;
        list.push_back({ L"mpv and FFmpeg", L"Plays videos · GNU General Public License",
            Block(kMpvText) + Block("The GNU General Public License, version 3:") + Resource(IDR_LICENSE_GPL) });
        list.push_back({ L"Geist and Geist Mono", L"The fonts of the window · SIL Open Font License 1.1",
            Resource(IDR_LICENSE_GEIST) });
        list.push_back({ L"litehtml", L"Lays out EPUB pages · BSD 3-Clause",
            Block(Resource(IDR_LICENSE_LITEHTML)) + kLitehtmlNotices });
        list.push_back({ L"gumbo", L"Reads EPUB pages, inside litehtml · Apache License 2.0",
            Block(kGumboNotices) + Block("The Apache License 2.0 that covers the rest of gumbo:") + Resource(IDR_LICENSE_GUMBO) });
        list.push_back({ L"SQLite", L"Stores the library · Public domain", kSqliteText });
        return list;
    }();
    return sections;
}
