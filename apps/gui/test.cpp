#include "window.hpp"
#include "../../include/saving.hpp"
#include "../../include/library.hpp"

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    
    library lib;
    load_library(lib, "libs/sample.json");
    

    Window* pWindow = new Window(lib);

    bool running = true;
    while (running)
    {
        if(!pWindow->ProcessMessages())
        {
            running = false;
        }

        Sleep(10);
    }
    

    return 0;
}