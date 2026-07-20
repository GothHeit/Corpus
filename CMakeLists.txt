cmake_minimum_required(VERSION 3.16)
project(Corpus CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(corpus_core STATIC
    src/file.cpp
    src/library.cpp
    src/saving.cpp
    src/tag.cpp
    src/tag_query.cpp
)
target_include_directories(corpus_core PUBLIC include)

add_executable(corpus-cli apps/cli/main.cpp)
target_link_libraries(corpus-cli PRIVATE corpus_core)

if(WIN32)
    enable_language(RC)

    add_executable(corpus-gui
        apps/gui/test.cpp
        apps/gui/window.cpp
        apps/gui/search_bar.cpp
        apps/gui/ui_font.cpp
        apps/gui/resource.rc
    )
    target_compile_definitions(corpus-gui PRIVATE UNICODE _UNICODE)
    target_link_libraries(corpus-gui PRIVATE corpus_core dwmapi gdi32 gdiplus user32)
endif()
