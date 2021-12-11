#pragma once

#include "demo_framework.hpp"

class re03 : public Demo {
private:

public:
    void OnLoad (HWND wnd, uint32_t w, uint32_t h) override;
    void OnFrameRender () override;
    void OnShutdown () override;
};

