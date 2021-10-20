#pragma once

namespace DX {
interface IDeviceNotify {
    virtual void OnDeviceLost () = 0;
    virtual void OnDeviceRestored () = 0;
};

class DeviceResources {
private:
    DeviceResources ();
    void MoveToNextFrame ();
    void InitAdapter (IDXGIAdapter1 ** adapter_pp);
    static const size_t MaxBackbufferCount = 3;

    UINT                    adapterid_override_;
    UINT                    backbuffer_index_;
    ComPtr<IDXGIAdapter1>   adapter_;
    UINT                    adapterid_;
    std::wstring            adapter_desc_;

    // -- d3d objs
    ComPtr<ID3D12Device>                device_;
    ComPtr<ID3D12CommandQueue>          cmdqueue_;
    ComPtr<ID3D12GraphicsCommandList>   cmdlist_;
    ComPtr<ID3D12CommandAllocator>      cmdallocs_[MaxBackbufferCount];

    // -- swapchain objs
    ComPtr<IDXGIFactory4>   dxgi_factory_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12Resource>  render_targets_[MaxBackbufferCount];
    ComPtr<ID3D12Resource>  depth_stencil_;

    // -- presentation fence objs
    ComPtr<ID3D12Fence>             fence_;
    UINT64                          fence_values_[MaxBackbufferCount];
    Microsoft::WRL::Wrappers::Event fence_event_;

    // -- rendering objs
    ComPtr<ID3D12DescriptorHeap>    rtv_heap_;
    ComPtr<ID3D12DescriptorHeap>    dsv_heap_;
    UINT                            rtv_descriptor_size_;
    D3D12_VIEWPORT                  viewport_;
    D3D12_RECT                      scissor_rect_;

    // -- d3d properties
    DXGI_FORMAT backbuffer_format_;
    DXGI_FORMAT depthstencil_format_;
    UINT        backbuffer_count_;
    D3D_FEATURE_LEVEL min_feature_level_;

    // -- cached device properties
    HWND    window_;
    RECT    output_size_;
    bool    is_window_visible_;
    D3D_FEATURE_LEVEL feature_level_;

    unsigned int options_;  // device resource options (flags)

    IDeviceNotify * device_notify_; // can be held directly because it owns the DeviceResources
public:
    static const unsigned int AllowTearing = 0x1;
    static const unsigned int RequiredTearingSupport = 0x2;

    DeviceResources (
        DXGI_FORMAT backbuffer_fmt = DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT depstencil_fmt = DXGI_FORMAT_D32_FLOAT,
        UINT backbuffer_count = 2,
        D3D_FEATURE_LEVEL min_feature_lv = D3D_FEATURE_LEVEL_11_0,
        UINT flags = 0,
        UINT adapaterid_override = UINT_MAX
    );
    ~DeviceResources ();
    void InitDXGIAdapter ();
    void SetAdapterOverride (UINT adapterid) { adapterid_override_ = adapterid; }
    void CreateDeviceResources ();
    void CreateWindowSizeDependentResources ();
    void SetWindow (HWND wnd, int w, int h);
    bool WindowSizeChanged (int w, int h, bool minimized);
    void HandleDeviceLost ();
    void RegisterDeviceNotify (IDeviceNotify * devnotify) {
        device_notify_ = devnotify;

        // NOTE(omid): the function DXGIDeclareAdapterRemovalSupport is an api agnositic function which
        // allows a process to indicate that it's resilient to any of its graphics devices being removed. 
        __if_exists(DXGIDeclareAdapterRemovalSupport) {
            if (devnotify) {
                if (FAILED(DXGIDeclareAdapterRemovalSupport()))
                    OutputDebugString(L"Warning: application failed to declare adapter removal support\n");
            }
        }
    }
    void Prepare (D3D12_RESOURCE_STATES before_state = D3D12_RESOURCE_STATE_PRESENT);
    void Present (D3D12_RESOURCE_STATES before_state = D3D12_RESOURCE_STATE_RENDER_TARGET);
    void ExecuteCmdlist ();
    void WaitForGpu () noexcept;

    // -- device accessors:
    RECT GetOutputSize () const { return output_size_; }
    bool IsWindowVisible () const { return is_window_visible_; }
    bool IsTearingSupported () const { return options_ & AllowTearing; }

    // -- d3d accessors:
    IDXGIAdapter1 * GetAdapter () const { return adapter_.Get(); }
    ID3D12Device * GetDevice () const { return device_.Get(); }
    IDXGIFactory4 * GetFactory () const { return dxgi_factory_.Get(); }
    IDXGISwapChain3 * GetSwapchain () const { return swapchain_.Get(); }
    D3D_FEATURE_LEVEL GetDeviceFeatureLevel () const { return feature_level_; }
    ID3D12Resource * GetRenderTarget () const { return render_targets_[backbuffer_index_].Get(); }
    ID3D12Resource * GetDepthStencil () const { return depth_stencil_.Get(); }
    ID3D12CommandQueue * GetCmdqueue () const { return cmdqueue_.Get(); }
    ID3D12CommandAllocator * GetCmdalloc () const { return cmdallocs_[backbuffer_index_].Get(); }
    ID3D12GraphicsCommandList * GetCmdlist () const { return cmdlist_.Get(); }
    DXGI_FORMAT GetBackbufferFormat () const { return backbuffer_format_; }
    DXGI_FORMAT GetDepthBufferFormat () const { return depthstencil_format_; }
    D3D12_VIEWPORT GetViewport () const { return viewport_; }
    D3D12_RECT GetScissorRect () const { return scissor_rect_; }
    UINT GetCurrentFrameIndex () const { return backbuffer_index_; }
    UINT GetPrevFrameIndex () const { return backbuffer_index_ == 0 ? backbuffer_count_ - 1 : backbuffer_index_ - 1; }
    unsigned int GetDeviceOptions () const { return options_; }
    LPCWSTR GetAdapaterDescription () const { return adapter_desc_.c_str(); }
    UINT GetAdapterId () const {return adapterid_;}

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtvHCpu () const {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
            backbuffer_index_,
            rtv_descriptor_size_
        );
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetDscHCpu () const {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    }
};

} // namespace DX
