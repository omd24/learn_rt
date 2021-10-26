#pragma once

#include "dx_sample_helper.h"
#include "win32_app.h"
#include "device_resources.h"

class DXSample : public DX::IDeviceNotify {
private:
    std::wstring asset_path_;
    std::wstring title_;
protected:
    void SetCustomWindowText (LPCWSTR text);

    // -- viewport dimensions
    UINT width_;
    UINT height_;
    float aspect_ratio_;

    // -- window bounds
    RECT window_bounds_;

    // -- override to be able to start w/o Dx11on12 UI for PIX (PIX doesn't support it)
    bool enable_ui_;

    UINT adapterid_override_;
    std::unique_ptr<DX::DeviceResources> device_resources_;
public:
    DXSample(UINT width, UINT height, std::wstring name);
    virtual ~DXSample();

    virtual void OnInit () = 0;
    virtual void OnUpdate () = 0;
    virtual void OnRender () = 0;
    virtual void OnSizeChanged (UINT w, UINT h, bool minimized) = 0;
    virtual void OnDestroy () = 0;
    // -- event handlers for windows messages:
    virtual void OnKeyDown (UINT8) {}
    virtual void OnKeyUp (UINT8) {}
    virtual void OnWindowMoved (int, int) {}
    virtual void OnMouseMove (UINT, UINT) {}
    virtual void OnLeftButtonDown (UINT, UINT) {}
    virtual void OnLeftButtonUp (UINT, UINT) {}
    virtual void OnDisplayChanged () {}

    virtual void ParseCommandLineArgs (int argc, _In_reads_(argc) WCHAR * argv []);

    // -- accessors:
    UINT GetWidth () const { return width_; }
    UINT GetHeight () const { return height_; }
    WCHAR const * GetTitle () const { return title_.c_str(); }
    RECT GetWindowsBounds () const { return window_bounds_; }
    virtual IDXGISwapChain * GetSwapchain () { return nullptr; }
    DX::DeviceResources * GetDeviceResources () const { return device_resources_.get(); }

    void UpdateForSizeChange (UINT client_w, UINT client_h);
    void SetWindowBounds (int left, int top, int right, int bottom);
    std::wstring GetAssetFullPath (LPCWSTR asset_name);
};

