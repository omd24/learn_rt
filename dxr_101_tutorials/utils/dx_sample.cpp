#include "stdafx.h"
#include "../dxr_hello_triangle/headers/stdafx.h"

#include "dx_sample.h"

DXSample::DXSample(UINT width, UINT height, std::wstring name) :
    width_(width),
    height_(height),
    window_bounds_{0,0,0,0},
    title_(name),
    aspect_ratio_(0.0f),
    enable_ui_(true),
    adapterid_override_(UINT_MAX)
{
    WCHAR asset_path[512];
    GetAssetsPath(asset_path, _countof(asset_path));
    asset_path_ = asset_path;
    UpdateForSizeChange(width, height);
}
DXSample::~DXSample() {}
void DXSample::SetCustomWindowText (LPCWSTR text) {
    std::wstring window_text = title_ + L": " + text;
    SetWindowText(Win32App::GetHwnd(), window_text.c_str());
}
_Use_decl_annotations_
void DXSample::ParseCommandLineArgs (int argc, _In_reads_(argc) WCHAR * argv []) {
    for (int i = 1; i < argc; ++i) {
        // -- disableui option
        if (_wcsnicmp(argv[i], L"-disableui", wcslen(argv[i])) == 0 ||
            _wcsnicmp(argv[i], L"/disableui", wcslen(argv[i])) == 0
        ) {
            enable_ui_ = false;
        } else if ( // -- forceadapter [id] command
            _wcsnicmp(argv[i], L"-forceadapter", wcslen(argv[i])) == 0 ||
            _wcsnicmp(argv[i], L"/forceadapter", wcslen(argv[i])) == 0
        ) {
            ThrowIfFailed(i + 1 < argc, L"incorrect argument format passed in");
            adapterid_override_ = _wtoi(argv[i + 1]);
            ++i;
        }
    }
}
void DXSample::UpdateForSizeChange (UINT client_w, UINT client_h) {
    width_ = client_w;
    height_ = client_h;
    aspect_ratio_ = static_cast<float>(client_w) / static_cast<float>(client_h);
}
void DXSample::SetWindowBounds (int left, int top, int right, int bottom) {
    window_bounds_.left = static_cast<LONG>(left);
    window_bounds_.top = static_cast<LONG>(top);
    window_bounds_.right = static_cast<LONG>(right);
    window_bounds_.bottom = static_cast<LONG>(bottom);
}
std::wstring DXSample::GetAssetFullPath (LPCWSTR asset_name) {
    return asset_path_ + asset_name;
}
