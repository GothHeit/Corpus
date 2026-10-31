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

    const char *kMpvText = R"(Corpus plays videos with libmpv, the library of the mpv player, which is built from FFmpeg and other libraries. The libmpv-2.dll inside this program is a build made for Corpus with the GPL parts of the player and of FFmpeg switched off, so the whole library is under the GNU Lesser General Public License, version 2.1 or later. It is linked only at run time: Corpus loads the DLL as a separate file, so you can replace it with your own build (Settings, "Use another libmpv-2.dll").

mpv is built with its GPL features off (-Dgpl=false), and FFmpeg without --enable-gpl and --enable-version3. Besides them the DLL holds libopenmpt (BSD 3-Clause), libxml2 (MIT), dav1d (BSD 2-Clause), libjxl (BSD 3-Clause), lcms2 (MIT), libplacebo (LGPL 2.1 or later) and the libraries they need, each under its own license. The notices of the permissive ones follow; the other small libraries in the DLL (zlib, bzip2, the NVIDIA codec headers and the like) are listed with their sources in the recipe below.

Source code:
  Corpus    https://github.com/GothHeit/Corpus
  The recipe that builds libmpv-2.dll is mpv-winbuild-cmake by shinchiro,
  https://github.com/shinchiro/mpv-winbuild-cmake
  with the changes in tools/libmpv-lgpl/recipe-changes.patch of the Corpus repository (they leave out the GPL
  libraries, such as x264 and x265, and turn mpv's GPL option off). From the recipe's package
  files you get the sources of mpv, FFmpeg and every other library inside the DLL.

The GNU Lesser General Public License, version 2.1, whose full text follows these notices, is also at
  https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt

libopenmpt is licensed under the BSD 3-Clause license:

Copyright (c) 2004-2026, OpenMPT Project Developers and Contributors
Copyright (c) 1997-2003, Olivier Lapicque
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenMPT project nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

libxml2 is licensed under the MIT license:

Copyright (C) 1998-2012 Daniel Veillard. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

dav1d is licensed under the BSD 2-Clause license:

Copyright © 2018-2026, VideoLAN and dav1d authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

libjxl is licensed under the BSD 3-Clause license:

Copyright (c) the JPEG XL Project Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

lcms2 (Little CMS) is licensed under the MIT license:

Copyright (c) 1998-2026 Marti Maria Saguer

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

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
        list.push_back({ L"mpv and FFmpeg", L"Plays videos · GNU Lesser General Public License 2.1 or later",
            Block(kMpvText) + Block("The GNU Lesser General Public License, version 2.1:") + Resource(IDR_LICENSE_LGPL) });
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
