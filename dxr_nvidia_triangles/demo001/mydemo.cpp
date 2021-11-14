#include "mydemo.hpp"

void MyDemo::OnLoad (HWND hwnd, uint32_t w, uint32_t h) {}
void MyDemo::OnFrameRender () {}
void MyDemo::OnShutdown () {}

int WINAPI WinMain (
    _In_ HINSTANCE inst,
    _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int
) {
    Framework::Run(MyDemo(), "Demo001");
    return(0);
}

