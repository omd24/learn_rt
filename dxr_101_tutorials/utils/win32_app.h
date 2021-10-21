#pragma once

class DXSample;

class Win32App {
private:
    static HWND hwnd_;
    static bool fullscreen_;
    static RECT window_rect_;
    static UINT const window_style_ = WS_OVERLAPPEDWINDOW;
protected:
    static LRESULT CALLBACK WindowProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
public:
    static int Run (DXSample * sample, HINSTANCE inst, int cmdshow);
    static void ToggleFullscreen (IDXGISwapChain * output_swapchain = nullptr);
    static void SetWindowZOrderToTopMost (bool set_to_topmost);
    static HWND GetHwnd () { return hwnd_; }
    static bool IsFullscreen () {return fullscreen_;}
};

