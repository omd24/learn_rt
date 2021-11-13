#include "demo_framework.hpp"
#include <locale>   // part of the localization library
#include <codecvt>  // part of the localization library

// -- anynomous namespace scope is til the end of translation unit
namespace {
HWND g_hwnd = nullptr;
static LRESULT CALLBACK msg_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (VK_ESCAPE == wparam) PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}
HWND create_window (std::string const & title, uint32_t & w, uint32_t & h) {
    WCHAR const * class_name = L"DxrNvidiaTriWindowClass";
    DWORD wnd_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

    WNDCLASS wc = {};
    wc.lpfnWndProc = msg_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = class_name;

    if (0 == RegisterClass(&wc)) {
        MsgBox("RegisterClass() failed");
        return nullptr;
    }

    RECT R {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&R, wnd_style, false);

    int wnd_width = R.right - R.left;
    int wnd_height = R.bottom - R.top;

    std::wstring wnd_title = StrToWStr(title);
    HWND hwnd = CreateWindowEx(
        0, class_name,
        wnd_title.c_str(),
        wnd_style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wnd_width, wnd_height,
        nullptr, nullptr,
        wc.hInstance,
        nullptr
    );
    if (nullptr == hwnd) {
        MsgBox("CreateWindowEx() failed");
        return nullptr;
    }
    return hwnd;
}

void msg_loop (Demo & demo) {
    MSG msg;
    while (1) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (WM_QUIT == msg.message) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else
            demo.OnFrameRender();
    }
}

} // end namespace

// -- trace a D3D error and convert the result to a human-readable string
void D3DTraceHr (std::string const & msg, HRESULT hr) {
    // -- search the system message-table resource(s) for a message.
    char hr_msg[512];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr, 0, hr_msg, _countof(hr_msg), nullptr);
    // -- append the system message to the given error "msg"
    std::string error_msg = msg + ".\nError! " + hr_msg;
    MsgBox(error_msg);
}

void MsgBox (std::string const & msg) {
    MessageBoxA(g_hwnd, msg.c_str(), "Error", MB_OK);
}
std::wstring StrToWStr (std::string const & str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
    std::wstring wstr = cvt.from_bytes(str);
    return wstr;
}
std::string WStrToStr (std::wstring const & wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
    std::string str = cvt.to_bytes(wstr);
    return str;
}

void Framework::Run (Demo & demo, std::string const & wndtitle, uint32_t w, uint32_t h) {
    g_hwnd = create_window(wndtitle, w, h);

    // -- calculate client-rect area
    RECT R = {};
    GetClientRect(g_hwnd, &R);
    w = R.right - R.left;
    h = R.bottom - R.top;

    demo.OnLoad(g_hwnd, w, h);

    ShowWindow(g_hwnd, SW_SHOWNORMAL);

    // -- start main loop
    msg_loop(demo);

    // -- cleanup
    demo.OnShutdown();
    DestroyWindow(g_hwnd);
}

