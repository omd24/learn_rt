#include "re03.hpp"

void re03::OnLoad (HWND wnd, uint32_t w, uint32_t h) {

}
void re03::OnFrameRender () {

}
void re03::OnShutdown () {

}

int WINAPI
WinMain (_In_ HINSTANCE inst, _In_opt_ HINSTANCE prev, _In_ LPSTR cmdline, _In_ int show_cmd) {
    Framework::Run(re03(), "Rework-03");
}

