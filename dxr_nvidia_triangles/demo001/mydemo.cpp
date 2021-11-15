#include "mydemo.hpp"

static IDXGISwapChain3Ptr
create_swapchain (
    IDXGIFactory4Ptr factory, HWND hwnd,
    uint32_t w, uint32_t h,
    DXGI_FORMAT fmt, ID3D12CommandQueuePtr cmdque
) {
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
    swapchain_desc.BufferCount = DefaultSwapchainBufferCount;
    swapchain_desc.Width = w;
    swapchain_desc.Height = h;
    swapchain_desc.Format = fmt;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SampleDesc.Quality = 0;

    // -- CreateSwapChainForHwnd works with swapchain1
    MAKE_SMART_COM_PTR(IDXGISwapChain1);
    IDXGISwapChain1Ptr swapchain;
    HRESULT hr = factory->CreateSwapChainForHwnd(cmdque, hwnd, &swapchain_desc, nullptr, nullptr, &swapchain);
    if (FAILED(hr)) {
        D3DTraceHr("CreateSwapChainForHwnd() failed!", hr);
        return false;
    }
    IDXGISwapChain3Ptr ret;
    D3D_CALL(swapchain->QueryInterface(IID_PPV_ARGS(&ret)));
    return ret;
}
static ID3D12Device5Ptr
create_device (IDXGIFactory4Ptr factory) {
    // -- find HW adapter
    IDXGIAdapter1Ptr adptr;
    for (uint32_t i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adptr); ++i) {
        DXGI_ADAPTER_DESC1 desc = {};
        adptr->GetDesc1(&desc);
        // -- skip SW adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
#if defined(_DEBUG)
        ID3D12DebugPtr dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
            dbg->EnableDebugLayer();
#endif
        // -- create device
        ID3D12Device5Ptr dev;
        D3D_CALL(D3D12CreateDevice(adptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)));

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 feat5;
        HRESULT hr = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &feat5, sizeof(feat5));
        if (SUCCEEDED(hr) && feat5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            return dev;
    }
    MsgBox("Raytracing is not supported on this device!");
    exit(1);
    return nullptr;
}
static ID3D12CommandQueuePtr
create_cmdque (ID3D12Device5Ptr dev) {
    ID3D12CommandQueuePtr ret;
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    D3D_CALL(dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&ret)));
    return ret;
}
static ID3D12DescriptorHeapPtr
create_descriptor_heap (
    ID3D12Device5Ptr dev,
    uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
    bool shader_visible
) {
    ID3D12DescriptorHeapPtr ret;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = count;
    desc.Type = heap_type;
    desc.Flags = shader_visible ?
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    D3D_CALL(dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ret)));
    return ret;
}
static D3D12_CPU_DESCRIPTOR_HANDLE
create_rtv (
    ID3D12Device5Ptr dev, ID3D12ResourcePtr resource,
    ID3D12DescriptorHeapPtr heap, uint32_t & heap_used_entries, DXGI_FORMAT fmt
) {
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    desc.Format = fmt,
        desc.Texture2D.MipSlice = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv = heap->GetCPUDescriptorHandleForHeapStart();
    hcpu_rtv.ptr += heap_used_entries * dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ++heap_used_entries;
    dev->CreateRenderTargetView(resource, &desc, hcpu_rtv);
    return hcpu_rtv;
}
static void
resource_barrier (
    ID3D12GraphicsCommandList4Ptr cmdlist, ID3D12ResourcePtr resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after
) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    cmdlist->ResourceBarrier(1, &barrier);
}
static uint64_t
submit_cmdlist (
    ID3D12GraphicsCommandList4Ptr cmdlist,
    ID3D12CommandQueuePtr cmdque,
    ID3D12FencePtr fence,
    uint64_t fence_value
) {
    cmdlist->Close();
    ID3D12CommandList * graphics_list = cmdlist.GetInterfacePtr();
    cmdque->ExecuteCommandLists(1, &graphics_list);
    ++fence_value;
    cmdque->Signal(fence, fence_value);
    return fence_value;
}
void MyDemo::init_dxr (HWND hwnd, uint32_t w, uint32_t h) {
    hwnd_ = hwnd;
    swapchain_size_ = uvec2(w, h);

#if defined(_DEBUG)
    ID3D12DebugPtr dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif

    // -- create d3d objects:
    IDXGIFactory4Ptr factory;
    D3D_CALL(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    dev_ = create_device(factory);
    cmdque_ = create_cmdque(dev_);
    swapchain_ = create_swapchain(factory, hwnd, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, cmdque_);

    // -- create RTV descriptor heap
    rtv_heap_.heap_ = create_descriptor_heap(dev_, RtvHeapSize, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

    // -- create [per] frame objects:
    for (uint32_t i = 0; i < DefaultSwapchainBufferCount; ++i) {
        D3D_CALL(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&FrameObjects_[i].cmdalloc_)));
        D3D_CALL(swapchain_->GetBuffer(i, IID_PPV_ARGS(&FrameObjects_[i].swapchain_buffer_)));
        FrameObjects_[i].hcpu_rtv_ = create_rtv(
            dev_, FrameObjects_[i].swapchain_buffer_,
            rtv_heap_.heap_, rtv_heap_.used_entries_,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        );
    }

    // -- create cmdlist
    D3D_CALL(dev_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, FrameObjects_[0].cmdalloc_, nullptr, IID_PPV_ARGS(&cmdlist_)));

    // -- create synchronization objects:
    D3D_CALL(dev_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    fence_event_  = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}
uint32_t MyDemo::begin_frame () {
    return swapchain_->GetCurrentBackBufferIndex();
}
void MyDemo::end_frame (uint32_t rtv_idx) {
    resource_barrier(cmdlist_, FrameObjects_[rtv_idx].swapchain_buffer_,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    fence_value_ = submit_cmdlist(cmdlist_, cmdque_, fence_, fence_value_);
    swapchain_->Present(0, 0);

    // -- make sure new back buffer is ready
    if (fence_value_ > DefaultSwapchainBufferCount) {
        fence_->SetEventOnCompletion(fence_value_ - DefaultSwapchainBufferCount + 1, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }

    // -- prepare cmdlist for next frame:
    uint32_t buffer_idx = swapchain_->GetCurrentBackBufferIndex();
    FrameObjects_[buffer_idx].cmdalloc_->Reset();
    cmdlist_->Reset(FrameObjects_[buffer_idx].cmdalloc_, nullptr);
}

void MyDemo::OnLoad (HWND hwnd, uint32_t w, uint32_t h) {
    init_dxr(hwnd, w, h);
}
void MyDemo::OnFrameRender () {
    uint32_t rtv_idx = begin_frame();
    float const clear_color[4] = {0.4f, 0.6f, 0.2f, 1.0f};
    resource_barrier(cmdlist_, FrameObjects_[rtv_idx].swapchain_buffer_, 
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdlist_->ClearRenderTargetView(FrameObjects_[rtv_idx].hcpu_rtv_, clear_color, 0, nullptr);
    end_frame(rtv_idx);
}
void MyDemo::OnShutdown () {
    // -- wait for the cmdque to finish execution:
    ++fence_value_;
    cmdque_->Signal(fence_, fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
}

int WINAPI WinMain (
    _In_ HINSTANCE inst,
    _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int
) {
    Framework::Run(MyDemo(), "Init DXR");
    return(0);
}

