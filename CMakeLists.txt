cmake_minimum_required(VERSION 3.16)
project(Corpus C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(corpus_core STATIC
    src/database.cpp
    src/file.cpp
    src/file_identity.cpp
    src/library.cpp
    src/saving.cpp
    src/tag.cpp
    src/tag_query.cpp
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

if(WIN32)
    # Static runtime, like corpus-gui, so the tests run on Windows without the MinGW DLLs next to them.
    target_link_options(corpus-roundtrip-test PRIVATE -static-libgcc -static-libstdc++ -static)
    target_link_options(corpus-core-test PRIVATE -static-libgcc -static-libstdc++ -static)
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
        apps/gui/ui_util.cpp
        apps/gui/resource.rc
    )
    target_compile_definitions(corpus-gui PRIVATE UNICODE _UNICODE)
    target_link_libraries(corpus-gui PRIVATE corpus_core dwmapi gdi32 gdiplus user32 shell32 ole32)
    target_link_options(corpus-gui PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
