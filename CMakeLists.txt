cmake_minimum_required(VERSION 3.16)
project(Corpus C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release unless asked otherwise: unoptimized, a search over many files runs several times slower and the .exe is bigger.
# Optimized for speed, stripped, with link-time optimization when the toolchain has it.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")
include(CheckIPOSupported)
check_ipo_supported(RESULT corpus_ipo_supported OUTPUT corpus_ipo_output LANGUAGES C CXX)
if(corpus_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

add_library(corpus_core STATIC
    src/database.cpp
    src/file.cpp
    src/file_identity.cpp
    src/folder_import.cpp
    src/library.cpp
    src/saving.cpp
    src/tag.cpp
    src/tag_query.cpp
    src/text.cpp
    third_party/sqlite/sqlite3.c
)
target_include_directories(corpus_core PUBLIC include)

# SQLite is always built optimized: unoptimized it runs several times slower.
set_source_files_properties(third_party/sqlite/sqlite3.c PROPERTIES
    COMPILE_DEFINITIONS "SQLITE_DQS=0;SQLITE_OMIT_LOAD_EXTENSION;SQLITE_DEFAULT_FOREIGN_KEYS=1"
    COMPILE_OPTIONS "-O2"
)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(corpus_core PUBLIC Threads::Threads)
endif()

# litehtml v0.10 (commit 9bc84b8, BSD) with its bundled gumbo HTML parser (Apache 2.0), for the EPUB reader.
# Vendored as is; their warnings are silenced here only, and like SQLite it is always built optimized.
file(GLOB litehtml_sources third_party/litehtml/src/*.cpp third_party/litehtml/src/gumbo/*.c)
add_library(litehtml STATIC ${litehtml_sources})
target_include_directories(litehtml PUBLIC third_party/litehtml/include
    PRIVATE third_party/litehtml/include/litehtml third_party/litehtml/src/gumbo/include
    third_party/litehtml/src/gumbo/include/gumbo)
target_compile_options(litehtml PRIVATE -w -O2)
set_target_properties(litehtml PROPERTIES C_STANDARD 99)

add_executable(corpus-cli apps/cli/main.cpp)
target_link_libraries(corpus-cli PRIVATE corpus_core)
if(WIN32)
    target_link_libraries(corpus-cli PRIVATE shell32)
endif()

# Storage round trip (and an optional load benchmark): corpus-roundtrip-test [work_dir] [file_count]
add_executable(corpus-roundtrip-test apps/cli/roundtrip_test.cpp)
target_link_libraries(corpus-roundtrip-test PRIVATE corpus_core)

# Unit tests for tag queries, filtering, search, edits and removal: corpus-core-test [work_dir]
add_executable(corpus-core-test apps/cli/core_test.cpp)
target_link_libraries(corpus-core-test PRIVATE corpus_core)

# litehtml lays out a short HTML string with a stub container: corpus-litehtml-test
add_executable(corpus-litehtml-test apps/cli/litehtml_test.cpp)
target_link_libraries(corpus-litehtml-test PRIVATE litehtml)

if(WIN32)
    # Static runtime, like corpus-gui, so the tests run on Windows without the MinGW DLLs next to them.
    target_link_options(corpus-roundtrip-test PRIVATE -static-libgcc -static-libstdc++ -static)
    target_link_options(corpus-core-test PRIVATE -static-libgcc -static-libstdc++ -static)
    target_link_options(corpus-litehtml-test PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

if(WIN32)
    enable_language(RC)

    # WIN32: a GUI program (-mwindows), so Windows doesn't open a console window next to it.
    add_executable(corpus-gui WIN32
        apps/gui/main.cpp
        apps/gui/window.cpp
        apps/gui/title_bar.cpp
        apps/gui/text_field.cpp
        apps/gui/file_grid.cpp
        apps/gui/tag_panel.cpp
        apps/gui/tag_sidebar.cpp
        apps/gui/info_pane.cpp
        apps/gui/tooltip.cpp
        apps/gui/context_menu.cpp
        apps/gui/sheet.cpp
        apps/gui/rename_sheet.cpp
        apps/gui/tag_sheet.cpp
        apps/gui/folder_tags_sheet.cpp
        apps/gui/open_with.cpp
        apps/gui/single_instance.cpp
        apps/gui/filter_query.cpp
        apps/gui/import_bar.cpp
        apps/gui/missing_check.cpp
        apps/gui/renderer.cpp
        apps/gui/thumbnails.cpp
        apps/gui/image_decoder.cpp
        apps/gui/book_pages.cpp
        apps/gui/pdf_book.cpp
        apps/gui/cbz_book.cpp
        apps/gui/epub_book.cpp
        apps/gui/text_book.cpp
        apps/gui/html_canvas.cpp
        apps/gui/zip_folder.cpp
        apps/gui/book_reader.cpp
        apps/gui/viewer.cpp
        apps/gui/settings_page.cpp
        apps/gui/licenses.cpp
        apps/gui/video_player.cpp
        apps/gui/ui_util.cpp
        apps/gui/resource.rc
    )
    target_compile_definitions(corpus-gui PRIVATE UNICODE _UNICODE)
    # libmpv's header only: the DLL is loaded at run time, the first time a video opens (see video_player.cpp).
    target_include_directories(corpus-gui PRIVATE third_party/mpv)
    target_link_libraries(corpus-gui PRIVATE corpus_core litehtml d2d1 dwrite dwmapi gdi32 user32 shell32 ole32 windowscodecs uuid
        runtimeobject shcore shlwapi)
    target_link_options(corpus-gui PRIVATE -static-libgcc -static-libstdc++ -static)

    # libmpv-2.dll goes inside corpus-gui.exe when it is there at configure time (tools/get-libmpv.ps1 puts it in
    # build-gui), so Corpus is a single .exe; video_player.cpp writes it out the first time a video opens. It goes in
    # compressed by tools/pack-libmpv.py (a third of the .exe's size otherwise), and is unpacked with the Windows
    # Compression API. Its hash names the copy written out, so a build with another DLL writes its own. Without it
    # (or without Python) the build still works, and videos play only with a libmpv-2.dll next to the .exe.
    set(CORPUS_LIBMPV_DLL "${CMAKE_BINARY_DIR}/libmpv-2.dll" CACHE FILEPATH "libmpv-2.dll to build into corpus-gui.exe")
    find_package(Python3 COMPONENTS Interpreter)
    if(EXISTS "${CORPUS_LIBMPV_DLL}" AND Python3_FOUND)
        file(SHA256 "${CORPUS_LIBMPV_DLL}" libmpv_hash)
        string(SUBSTRING "${libmpv_hash}" 0 16 libmpv_tag)
        set(libmpv_packed "${CMAKE_BINARY_DIR}/libmpv.pack")
        add_custom_command(OUTPUT "${libmpv_packed}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/pack-libmpv.py" "${CORPUS_LIBMPV_DLL}" "${libmpv_packed}"
            DEPENDS "${CORPUS_LIBMPV_DLL}" "${CMAKE_SOURCE_DIR}/tools/pack-libmpv.py"
            COMMENT "Compressing libmpv-2.dll"
            VERBATIM)
        add_custom_target(libmpv_pack DEPENDS "${libmpv_packed}")
        add_dependencies(corpus-gui libmpv_pack)
        set(libmpv_rc "${CMAKE_BINARY_DIR}/libmpv.rc")
        file(WRITE "${libmpv_rc}"
            "#include \"${CMAKE_SOURCE_DIR}/apps/gui/resource.h\"\n"
            "IDR_LIBMPV RCDATA \"${libmpv_packed}\"\n")
        set_source_files_properties("${libmpv_rc}" PROPERTIES OBJECT_DEPENDS "${libmpv_packed}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CORPUS_LIBMPV_DLL}")
        target_sources(corpus-gui PRIVATE "${libmpv_rc}")
        set_source_files_properties(apps/gui/video_player.cpp PROPERTIES COMPILE_DEFINITIONS "CORPUS_LIBMPV_TAG=\"${libmpv_tag}\"")
        message(STATUS "libmpv: building ${CORPUS_LIBMPV_DLL} into corpus-gui.exe")
    elseif(EXISTS "${CORPUS_LIBMPV_DLL}")
        message(WARNING "libmpv: Python 3 not found, so ${CORPUS_LIBMPV_DLL} can't be compressed into corpus-gui.exe, which will play videos only with the DLL next to it")
    else()
        message(STATUS "libmpv: ${CORPUS_LIBMPV_DLL} not found, corpus-gui.exe plays videos only with it next to it")
    endif()
endif()
