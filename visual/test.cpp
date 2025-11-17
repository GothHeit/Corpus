#include "window.hpp"

int main()
{
    Window* pWindow = new Window();

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