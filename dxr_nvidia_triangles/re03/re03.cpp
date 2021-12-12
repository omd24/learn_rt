#include "re03.hpp"

// =============================================================================================================================================

static IDXGISwapChain3Ptr
create_dxgi_swc (IDXGIFactory4Ptr factory, HWND wnd, U32 w, U32 h, DXGI_FORMAT fmt, ID3D12CommandQueuePtr cmdque) {
    DXGI_SWAP_CHAIN_DESC1 swc_desc = {
        .Width = w,
        .Height = h,
        .Format = fmt,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = DefaultSwapchainBufferCount,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };

    MAKE_SMART_COM_PTR(IDXGISwapChain1);
    IDXGISwapChain1Ptr swc1;
    HRESULT hr = factory->CreateSwapChainForHwnd(cmdque, wnd, &swc_desc, nullptr, nullptr, &swc1);
    if (FAILED(hr)) {
        D3DTraceHr("Failed to creaet swc", hr);
        return nullptr;
    }

    IDXGISwapChain3Ptr ret;
    D3D_CALL(swc1->QueryInterface(IID_PPV_ARGS(&ret)));
    return ret;
}
static ID3D12Device5Ptr
create_device (IDXGIFactory4Ptr factory) {
    IDXGIAdapter1Ptr adptr;
    for (U32 i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adptr); ++i) {
        DXGI_ADAPTER_DESC1 desc = {};
        adptr->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
#if defined(_DEBUG)
        ID3D12DebugPtr dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
            dbg->EnableDebugLayer();
#endif
        ID3D12Device5Ptr ret;
        D3D_CALL(D3D12CreateDevice(adptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&ret)));

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 feat5 = {};
        HRESULT hr = ret->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &feat5, sizeof(feat5));
        if (SUCCEEDED(hr) && feat5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            return ret;
    }
    MsgBox("raytracing not supported on this device");
    exit(1);
    return nullptr;
}
static ID3D12CommandQueuePtr
create_cmdque (ID3D12Device5Ptr dev) {
    ID3D12CommandQueuePtr ret;
    D3D12_COMMAND_QUEUE_DESC desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
    };
    D3D_CALL(dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&ret)));
    return ret;
}
static ID3D12DescriptorHeapPtr
create_descrptr_heap (ID3D12Device5Ptr dev, U32 count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shader_visible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc {
        .Type = type,
        .NumDescriptors = count,
        .Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE
    };
    ID3D12DescriptorHeapPtr ret;
    D3D_CALL(dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ret)));
    return ret;
}
static D3D12_CPU_DESCRIPTOR_HANDLE
create_rtv (ID3D12Device5Ptr dev, ID3D12ResourcePtr resource, ID3D12DescriptorHeapPtr heap, U32 & used_entries, DXGI_FORMAT fmt) {
    D3D12_RENDER_TARGET_VIEW_DESC desc = {
        .Format = fmt,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MipSlice = 0}
    };
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu = heap->GetCPUDescriptorHandleForHeapStart();
    hcpu.ptr += used_entries * dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ++used_entries;
    dev->CreateRenderTargetView(resource, &desc, hcpu);
    return hcpu;
}
static void
resource_barrier (
    ID3D12GraphicsCommandList4Ptr cmdlist, ID3D12ResourcePtr resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after
) {
    D3D12_RESOURCE_BARRIER barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {
            .pResource = resource,
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = before,
            .StateAfter = after
        }
    };
    cmdlist->ResourceBarrier(1, &barrier);
}
static U64
submit_cmdlist (ID3D12GraphicsCommandList4Ptr cmdlist, ID3D12CommandQueuePtr cmdque, ID3D12FencePtr fence, U64 fence_value) {
    cmdlist->Close();
    ID3D12CommandList * cl = cmdlist.GetInterfacePtr();
    cmdque->ExecuteCommandLists(1, &cl);
    ++fence_value;
    cmdque->Signal(fence, fence_value);
    return fence_value;
}


// =============================================================================================================================================


void re03::init_dxr (HWND wnd, U32 w, U32 h) {
    wnd_ = wnd;
    swc_size_ = uvec2(w, h);
#if defined(_DEBUG)
    ID3D12DebugPtr dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif
    IDXGIFactory4Ptr factory;
    D3D_CALL(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    dev_ = create_device(factory);
    cmdque_ = create_cmdque(dev_);
    swc_ = create_dxgi_swc(factory, wnd_, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, cmdque_);

    rtv_heap_.Heap = create_descrptr_heap(dev_, rtv_heap_size_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
    for (U32 i = 0; i < DefaultSwapchainBufferCount; ++i) {
        D3D_CALL(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&FrameObjects[i].CmdAlloc)));
        D3D_CALL(swc_->GetBuffer(i, IID_PPV_ARGS(&FrameObjects[i].SwcBuffer)));
        FrameObjects[i].HCpuRtv =
            create_rtv(dev_, FrameObjects[i].SwcBuffer, rtv_heap_.Heap, rtv_heap_.UsedEntries, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    }
    D3D_CALL(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, FrameObjects[0].CmdAlloc, nullptr, IID_PPV_ARGS(&cmdlist_)));

    D3D_CALL(dev_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}
U32 re03::begin_frame () {
    return swc_->GetCurrentBackBufferIndex();
}
void re03::end_frame (U32 rtv_index) {
    resource_barrier(cmdlist_, FrameObjects[rtv_index].SwcBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    fence_value_ = submit_cmdlist(cmdlist_, cmdque_, fence_, fence_value_);
    swc_->Present(0, 0);

    U32 buffer_index = swc_->GetCurrentBackBufferIndex();

    if (fence_value_ > DefaultSwapchainBufferCount) {
        fence_->SetEventOnCompletion(fence_value_ - DefaultSwapchainBufferCount + 1, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }

    FrameObjects[buffer_index].CmdAlloc->Reset();
    cmdlist_->Reset(FrameObjects[buffer_index].CmdAlloc, nullptr);
}


// =============================================================================================================================================

void re03::OnLoad (HWND wnd, uint32_t w, uint32_t h) {
    init_dxr(wnd, w, h);
}
void re03::OnFrameRender () {
    U32 rtv_index = begin_frame();
    float const cv[4] = {0.4f, 0.6f, 0.2f, 1.0f};
    resource_barrier(cmdlist_, FrameObjects[rtv_index].SwcBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdlist_->ClearRenderTargetView(FrameObjects[rtv_index].HCpuRtv, cv, 0, nullptr);
    end_frame(rtv_index);
}
void re03::OnShutdown () {
    ++fence_value_;
    cmdque_->Signal(fence_, fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
}

int WINAPI
WinMain (_In_ HINSTANCE inst, _In_opt_ HINSTANCE prev, _In_ LPSTR cmdline, _In_ int show_cmd) {
    Framework::Run(re03(), "Rework-03");
}

