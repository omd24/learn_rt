#pragma once

#include <demo_framework.hpp>

struct MyDemo : public Demo {
    void OnLoad (HWND hwnd, uint32_t w, uint32_t h) override;
    void OnFrameRender () override;
    void OnShutdown () override;
};

