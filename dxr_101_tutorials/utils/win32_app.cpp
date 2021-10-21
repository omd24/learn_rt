#include "stdafx.h"
#include "../dxr_hello_triangle/headers/stdafx.h"

#include "win32_app.h"
#include "dx_sampler_helper.h"

using Microsoft::WRL::ComPtr;

HWND Win32App::hwnd_ = nullptr;
bool Win32App::fullscreen_ = false;
RECT Win32App::window_rect_;

LRESULT CALLBACK Win32App::WindowProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    DXSample * sample = reinterpret_cast<DXSample *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        // -- save DXSample * passed in to CreateWindow
        LPCREATESTRUCT create_struct = reinterpret_cast<LPCREATESTRUCT>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
    } return 0;
    case WM_KEYDOWN: {
        if (sample)
            sample->OnKeyDown(static_cast<UINT8>(wparam));
    } return 0;
    case WM_KEYUP:
        if (sample)
            sample->OnKeyUp(static_cast<UINT8>(wparam));
        return 0;
    case WM_SYSKEYDOWN:
        // -- handle ALT+ENTER
        if (VK_RETURN == wparam && lparam & (1 << 29))
            if (sample && sample->GetDeviceResources()->IsTearingSupported()) {
                ToggleFullscreen(sample->GetSwapchain());
                return 0;
            }
        // -- don't return SYSKEYDOWN messages: just send them to default WndProc
        break;
    case WM_PAINT:
        if (sample) {
            sample->OnUpdate();
            sample->OnRender();
        }
        return 0;
    case WM_SIZE:
        if (sample) {
            RECT wnd_rect = {};
            GetWindowRect(hwnd, &wnd_rect);
            sample->SetWindowBounds(wnd_rect.left, wnd_rect.top, wnd_rect.right, wnd_rect.bottom);
            RECT client_rect = {};
            GetClientRect(hwnd, &client_rect);
            sample->OnSizeChanged(client_rect.right, client_rect.left,
                client_rect.bottom - client_rect.top, wparam == SIZE_MINIMIZED);
        }
        return 0;
    case WM_MOVE:
        if (sample) {
            RECT wnd_rect = {};
            GetWindowRect(hwnd, &wnd_rect);
            sample->SetWindowBounds(wnd_rect.left, wnd_rect.top, wnd_rect.right, wnd_rect.bottom);
            int x = (int)(short)LOWORD(lparam);
            int y = (int)(short)HIWORD(lparam);
            sample->OnWindowMoved(x, y);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (sample)
            sample->OnDisplayChanged();
        return 0;
    case WM_MOUSEMOVE:
        if (sample && static_cast<UINT8>(wparam) == MK_LBUTTON) {
            UINT x = LOWORD(lparam);
            UINT y = HIWORD(lparam);
            sample->OnMouseMove(x, y);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        UINT x = LOWORD(lparam);
        UINT y = HIWORD(lparam);
        sample->OnLeftButtonDown(x, y);
    } return 0;
    case WM_LBUTTONUP:
        UINT x = LOWORD(lparam);
        UINT y = HIWORD(lparam);
        sample->OnLeftButtonUp(x, y);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}
int Win32App::Run (DXSample * sample, HINSTANCE inst, int cmdshow) {
    try {
        // -- parse command line paramters
        int argc;
        LPWSTR * argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        sample->ParseCommandLineArgs(argc, argv);
        LocalFree(argv);

        // -- init window class:
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"DXSampleClass";
        RegisterClassEx(&wc);

        RECT R = {0, 0, (LONG)sample->GetWidth(), (LONG)sample->GetHeight()};
        AdjustWindowRect(&R, WS_EX_OVERLAPPEDWINDOW, FALSE);

        // -- create window
        hwnd_ = CreateWindow(
            wc.lpszClassName,
            sample->GetTitle(),
            window_style_,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            R.right- R.left,
            R.bottom - R.top,
            nullptr, nullptr, // no parent, no menus
            inst,
            sample
        );

        sample->OnInit();

        ShowWindow(hwnd_, cmdshow);

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        sample->OnDestroy();
        // -- return wparam part of the WM_QUIT message to Windows OS
        return static_cast<char>(msg.wParam);
    } catch (std::exception & e) {
        OutputDebugString(L"Application hit a problem: ");
        OutputDebugStringA(e.what());
        OutputDebugString(L"\nTerminating...\n");

        sample->OnDestroy();
        return EXIT_FAILURE;
    }
}
void Win32App::ToggleFullscreen (IDXGISwapChain * swapchain) {
    if (fullscreen_) {
        // -- restore window attributes and size
        SetWindowLong(hwnd_, GWL_STYLE, window_style_);
        SetWindowPos(
            hwnd_,
            HWND_NOTOPMOST,
            window_rect_.left,
            window_rect_.top,
            window_rect_.right - window_rect_.left,
            window_rect_.bottom - window_rect_.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE
        );
        ShowWindow(hwnd_, SW_NORMAL);
    } else {
        // -- save window rect for later restore
        GetWindowRect(hwnd_, &window_rect_);

        // -- borderless window
        SetWindowLong(hwnd_, GWL_STYLE,
            window_style_ & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

        RECT fullscreen_rect;
        try {
            if (swapchain) {
                // -- get settings of the currently used display
                ComPtr<IDXGIOutput> output;
                ThrowIfFailed(swapchain->GetContainingOutput(&output));
                DXGI_OUTPUT_DESC desc;
                ThrowIfFailed(output->GetDesc(&desc));
                fullscreen_rect = desc.DesktopCoordinates;
            } else
                throw HrException(S_FALSE);
        } catch (HrException & e) {
            UNREFERENCED_PARAMETER(e);
            // -- get the settings of the primary display
            DEVMODE devmode = {};
            devmode.dmSize = sizeof(devmode);
            EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devmode);

            fullscreen_rect = {
                devmode.dmPosition.x,
                devmode.dmPosition.y,
                devmode.dmPosition.x + (LONG)devmode.dmPelsWidth,
                devmode.dmPosition.y + (LONG)devmode.dmPelsHeight
            };
        }
        SetWindowPos(
            hwnd_,
            HWND_TOPMOST,
            fullscreen_rect.left,
            fullscreen_rect.top,
            fullscreen_rect.right,
            fullscreen_rect.bottom,
            SWP_FRAMECHANGED | SWP_NOACTIVATE
        );
        ShowWindow(hwnd_, SW_MAXIMIZE);
    }
    fullscreen_ = !fullscreen_;
}
void Win32App::SetWindowZOrderToTopMost (bool set_to_topmost) {
    RECT R;
    GetWindowRect(hwnd_, &R);
    SetWindowPos(
        hwnd_,
        (set_to_topmost) ? HWND_TOPMOST : HWND_NOTOPMOST,
        R.left,
        R.top,
        R.right - R.left,
        R.bottom - R.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE
    );
}
