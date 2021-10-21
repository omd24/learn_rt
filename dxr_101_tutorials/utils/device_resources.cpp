#include "stdafx.h"

#include "device_resources.h"
#include "win32_app.h"

using namespace DX;
using namespace std;

namespace {
inline DXGI_FORMAT NoSRGB (DXGI_FORMAT fmt) {
    switch (fmt)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    default: return fmt;
    }
}
} // namespace

DeviceResources::DeviceResources (
        DXGI_FORMAT backbuffer_fmt,
        DXGI_FORMAT depstencil_fmt,
        UINT backbuffer_count,
        D3D_FEATURE_LEVEL min_feature_lv,
        UINT flags,
        UINT adapaterid_override
) :
    backbuffer_index_(0),
    fence_values_ {},
    rtv_descriptor_size_(0),
    viewport_ {},
    scissor_rect_ {},
    backbuffer_format_(backbuffer_fmt),
    depthstencil_format_(depstencil_fmt),
    backbuffer_count_(backbuffer_count),
    min_feature_level_(min_feature_lv),
    window_(nullptr),
    feature_level_(D3D_FEATURE_LEVEL_11_0),
    output_size_ {0, 0, 1, 1},
    options_(flags),
    device_notify_(nullptr),
    is_window_visible_(true),
    adapterid_override_(adapaterid_override),
    adapterid_(UINT_MAX)
{
    if (backbuffer_count > MaxBackbufferCount)
        throw out_of_range("backbuffer count too large");

    if (min_feature_lv < D3D_FEATURE_LEVEL_11_0)
        throw out_of_range("min feature level too low");

    if (options_ & RequiredTearingSupport)
        options_ |= AllowTearing;
}
DeviceResources::~DeviceResources () {
    WaitForGpu();
}
void DeviceResources::InitDXGIAdapter () {
    bool debug_dxgi = false;

#if defined(_DEBUG)
    // -- enable debug layer
    // NOTE(omid): Enabling debug layer after device creation invalidates active device 
    {
        ComPtr<ID3D12Debug> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
            debug_controller->EnableDebugLayer();
        else
            OutputDebugStringA("WARNING: Direct3D debug device is not available\n");

        ComPtr<IDXGIInfoQueue> dxgi_info_queue;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue)))) {
            debug_dxgi = true;
            ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgi_factory_)));
            dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
        }
    }
#endif
    if (!debug_dxgi)
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory_)));

    if (options_ & (AllowTearing | RequiredTearingSupport)) {
        BOOL allow_tear = false;

        ComPtr<IDXGIFactory5> factory5;
        HRESULT hr = dxgi_factory_.As(&factory5);
        if (SUCCEEDED(hr))
            hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tear, sizeof(allow_tear));
        if (FAILED(hr) || !allow_tear) {
            OutputDebugStringA("WARNING: Variable refresh rate displays are not supported\n");
            if (options_ & RequiredTearingSupport)
                ThrowIfFailed(false, L"Error: Sample must be run on an OS with tearing support\n");
            options_ &= ~AllowTearing;
        }
    }
    InitAdapter(&adapter_);
}
void DeviceResources::CreateDeviceResources () {
    ThrowIfFailed(D3D12CreateDevice(adapter_.Get(), min_feature_level_, IID_PPV_ARGS(&device_)));
#ifndef NDEBUG
    ComPtr<ID3D12InfoQueue> info_queue;
    if (SUCCEEDED(device_.As(&info_queue))) {
#ifdef _DEBUG
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
        D3D12_MESSAGE_ID hide [] = {
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
        };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = _countof(hide);
        filter.DenyList.pIDList = hide;
        info_queue->AddStorageFilterEntries(&filter);
    }
#endif
    // -- determine max feature level supported by the device:
    static D3D_FEATURE_LEVEL const s_feature_lvs [] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS feat_lvs = {
        _countof(s_feature_lvs), s_feature_lvs, D3D_FEATURE_LEVEL_11_0
    };
    HRESULT hr = device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feat_lvs, sizeof(feat_lvs));
    if (SUCCEEDED(hr))
        feature_level_ = feat_lvs.MaxSupportedFeatureLevel;
    else
        feature_level_ = min_feature_level_;
    //
    // -- create cmdqueue:
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&cmdqueue_)));
    //
    // -- create descriptor heaps for rtvs and dsvs:
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = backbuffer_count_;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)));
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (depthstencil_format_ != DXGI_FORMAT_UNKNOWN) {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
        dsv_heap_desc.NumDescriptors = 1;
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed(device_->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap_)));
    }
    //
    // -- create cmdalloc for each backbuffer and cmdlist for recording commands (close it here):
    for (UINT i = 0; i < backbuffer_count_; ++i)
        ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdallocs_[i])));
    ThrowIfFailed(device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdallocs_[0].Get(),
        nullptr, IID_PPV_ARGS(&cmdlist_))
    );
    ThrowIfFailed(cmdlist_->Close());
    //
    // -- create a fence for tracking gpu execution progress
    ThrowIfFailed(device_->CreateFence(fence_values_[backbuffer_index_], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    ++fence_values_[backbuffer_index_];

    fence_event_.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (!fence_event_.IsValid())
        ThrowIfFailed(E_FAIL, L"CreateEvent failed\n");
}
// -- these resources need to be recreated on window resizing
void DeviceResources::CreateWindowSizeDependentResources () {
    if (!window_)
        ThrowIfFailed(E_HANDLE, L"Call SetWindow with a valid win32 window handle\n");

    WaitForGpu();

    // -- release resources that are tied to swapchain and set fence values to current
    for (UINT i = 0; i < backbuffer_count_; ++i) {
        render_targets_[i].Reset();
        fence_values_[i] = fence_values_[backbuffer_index_];
    }

    // -- determine RT size in px:
    UINT backbuffer_width = max(output_size_.right - output_size_.left, 1);
    UINT backbuffer_height = max(output_size_.bottom - output_size_.top, 1);
    DXGI_FORMAT backbuffer_format = NoSRGB(backbuffer_format_);

    // -- if swapchain exists, resize it otherwise create one:
    if (swapchain_) {
        HRESULT hr = swapchain_->ResizeBuffers(
            backbuffer_count_,
            backbuffer_width,
            backbuffer_height,
            backbuffer_format,
            (options_ & AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
        );
        if (DXGI_ERROR_DEVICE_REMOVED == hr || DXGI_ERROR_DEVICE_RESET == hr) {
#if defined(_DEBUG)
            char buff[64] = {};
            HRESULT hres = DXGI_ERROR_DEVICE_REMOVED == hr ? device_->GetDeviceRemovedReason() : hr;
            sprintf_s(buff, "Device lost on ResizeBuffers: Reason code 0x%08X\n", hres);
            OutputDebugStringA(buff);
#endif
            HandleDeviceLost();
            // -- exit this function for now. HandleDeviceLost will reenter and take care of everything
            return;
        } else
            ThrowIfFailed(hr);
    } else /* create swapchain */ {
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
        swapchain_desc.Width = backbuffer_width;
        swapchain_desc.Height = backbuffer_height;
        swapchain_desc.Format = backbuffer_format;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.BufferCount = backbuffer_count_;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.SampleDesc.Quality = 0;
        swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapchain_desc.Flags = (options_ & AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs_desc = {};
        fs_desc.Windowed = TRUE;

        ComPtr<IDXGISwapChain1> swapchain;
        // -- dxgi doesn't allow creating a swapchain targeting a window with fullscreen style (no border, top-most)
        // -- so temporary remove top-most property, create swapchain, then reset back top-most property
        bool prev_isfullscreen = Win32App::IsFullscreen();
        if (prev_isfullscreen)
            Win32App::SetWindowZOrderToTopMost(false);
        ThrowIfFailed(dxgi_factory_->CreateSwapChainForHwnd(
            cmdqueue_.Get(), window_,
            &swapchain_desc, &fs_desc, nullptr,
            &swapchain
        ));
        if (prev_isfullscreen)
            Win32App::SetWindowZOrderToTopMost(true);
        ThrowIfFailed(swapchain.As(&swapchain_));

        // NOTE(omid): With tearing support enabled, we will handle Alt+Enter ourselves (rather than dxgi) 
        if (IsTearingSupported())
            dxgi_factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    } // end swapchain creation

    // -- create rtvs:
    for (UINT i = 0; i < backbuffer_count_; ++i) {
        ThrowIfFailed(swapchain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])));

        wchar_t name[25] = {};
        swprintf_s(name, L"Render Target %u", i);
        render_targets_[i]->SetName(name);

        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        rtv_desc.Format = backbuffer_format_;
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_rtv(
            rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
            i, rtv_descriptor_size_
        );
        device_->CreateRenderTargetView(render_targets_[i].Get(), &rtv_desc, hcpu_rtv);
    }

    // -- reset index to current backbuffer
    backbuffer_index_ = swapchain_->GetCurrentBackBufferIndex();

    if (depthstencil_format_ != DXGI_FORMAT_UNKNOWN) {
        // -- allocate a 2D surface as depth/stencil buffer and create DSV to it
        D3D12_RESOURCE_DESC depth_stencil_desc = CD3DX12_RESOURCE_DESC::Tex2D(
            depthstencil_format_,
            backbuffer_width,
            backbuffer_height,
            1,  // one texture
            1   // one mipmap level
        );
        depth_stencil_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear_val = {};
        clear_val.Format = depthstencil_format_;
        clear_val.DepthStencil.Depth = 1.0f;
        clear_val.DepthStencil.Stencil = 0;

        ThrowIfFailed(device_->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depth_stencil_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_val,
            IID_PPV_ARGS(&depth_stencil_)
        ));

        depth_stencil_->SetName(L"Depth Stencil");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = depthstencil_format_;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(depth_stencil_.Get(), &dsv_desc, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    }
    // -- set the viewport and scissor rectangle to target the entire window:
    viewport_.TopLeftX = viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(backbuffer_width);
    viewport_.Height = static_cast<float>(backbuffer_height);
    viewport_.MinDepth = D3D12_MIN_DEPTH;
    viewport_.MaxDepth = D3D12_MAX_DEPTH;

    scissor_rect_.left = scissor_rect_.top = 0;
    scissor_rect_.right = backbuffer_width;
    scissor_rect_.bottom = backbuffer_height;

}
// -- called when window is created (or recreated)
void DeviceResources::SetWindow (HWND wnd, int w, int h) {
    window_ = wnd;
    output_size_.left = output_size_.top = 0;
    output_size_.right = w;
    output_size_.bottom = h;
}
// -- called when window is resized
bool DeviceResources::WindowSizeChanged (int w, int h, bool minimized) {
    is_window_visible_ = !minimized;
    if (minimized || 0 == w || 0 == h)
        return false;
    RECT new_rc = {};
    new_rc.left = new_rc.top = 0;
    new_rc.right = w;
    new_rc.bottom = h;
    if (
        new_rc.left == output_size_.left &&
        new_rc.top == output_size_.top &&
        new_rc.right == output_size_.right &&
        new_rc.bottom == output_size_.bottom
    ) {
        return false;
    }
    output_size_ = new_rc;
    CreateWindowSizeDependentResources();
    return true;
}
// -- recreate all device resources
void DeviceResources::HandleDeviceLost () {
    if (device_notify_)
        device_notify_->OnDeviceLost();
    for (UINT i = 0; i < backbuffer_count_; ++i) {
        cmdallocs_[i].Reset();
        render_targets_[i].Reset();
    }
    depth_stencil_.Reset();
    cmdqueue_.Reset();
    cmdlist_.Reset();
    fence_.Reset();
    rtv_heap_.Reset();
    dsv_heap_.Reset();
    swapchain_.Reset();
    device_.Reset();
    dxgi_factory_.Reset();
    adapter_.Reset();
#if defined(_DEBUG)
    {
        ComPtr<IDXGIDebug1> dxgi_debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
            dxgi_debug->ReportLiveObjects(
                DXGI_DEBUG_ALL,
                DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL)
            );
    }
#endif
    InitDXGIAdapter();
    CreateDeviceResources();
    CreateWindowSizeDependentResources();
    if (device_notify_)
        device_notify_->OnDeviceRestored();
}
// -- prepare cmdlist and current render-target for rendering
void DeviceResources::Prepare (D3D12_RESOURCE_STATES before_state) {
    ThrowIfFailed(cmdallocs_[backbuffer_index_]->Reset());
    ThrowIfFailed(cmdlist_->Reset(cmdallocs_[backbuffer_index_].Get(), nullptr));
    if (before_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
        cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        render_targets_[backbuffer_index_].Get(), before_state, D3D12_RESOURCE_STATE_RENDER_TARGET));
}
// -- present the contents of the swapchain to the screen
void DeviceResources::Present (D3D12_RESOURCE_STATES before_state) {
    if (before_state != D3D12_RESOURCE_STATE_PRESENT)
        cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        render_targets_[backbuffer_index_].Get(), before_state, D3D12_RESOURCE_STATE_PRESENT));
    ExecuteCmdlist();
    HRESULT hr;
    if (options_ & AllowTearing) // Recommended to use tearing feature if supported with a sync interval of 0
        hr = swapchain_->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    else // Here first argument puts app to sleep until next vsync (avoiding unnecessary cycles rendering unused frames)
        hr = swapchain_->Present(1 /*block until vsync*/, 0);

    // -- if device was reset we must completely reinit the renderer
    if (DXGI_ERROR_DEVICE_REMOVED == hr || DXGI_ERROR_DEVICE_RESET == hr) {
#if defined(_DEBUG)
        char buff[64] = {};
        HRESULT hres = DXGI_ERROR_DEVICE_REMOVED == hr ? device_->GetDeviceRemovedReason() : hr;
        sprintf_s(buff, "Device lost on present: reason code 0x%08X\n", hres);
        OutputDebugStringA(buff);
#endif
        HandleDeviceLost();
    } else {
        ThrowIfFailed(hr);
        MoveToNextFrame();
    }
}
// -- send cmdlist off to gpu for processing
void DeviceResources::ExecuteCmdlist () {
    ThrowIfFailed(cmdlist_->Close());
    ID3D12CommandList * cmdlists [] = {cmdlist_.Get()};
    cmdqueue_->ExecuteCommandLists(_countof(cmdlists), cmdlists);
}
// -- wait for pending gpu work to complete
void DeviceResources::WaitForGpu () noexcept {
    if (cmdqueue_ && fence_ && fence_event_.IsValid()) {
        // -- schedule a signal command in the gpu queue
        UINT64 fence_value = fence_values_[backbuffer_index_];
        if (SUCCEEDED(cmdqueue_->Signal(fence_.Get(), fence_value))) {
            // -- wait until signal has been process
            if (SUCCEEDED(fence_->SetEventOnCompletion(fence_value /*target*/, fence_event_.Get()))) {
                WaitForSingleObjectEx(fence_event_.Get(), INFINITE, FALSE);
                ++fence_values_[backbuffer_index_];
            }
        }
    }
}
// -- prepare to render next frame
void DeviceResources::MoveToNextFrame () {
    // -- schedule a signal command in the queue
    UINT64 const current_fence_value = fence_values_[backbuffer_index_];
    ThrowIfFailed(cmdqueue_->Signal(fence_.Get(), current_fence_value));
    // -- update backbuffer index
    backbuffer_index_ = swapchain_->GetCurrentBackBufferIndex();
    // -- if next frame is not ready to be rendererd yet, wait until it is ready
    if (fence_->GetCompletedValue() < fence_values_[backbuffer_index_]) {
        ThrowIfFailed(fence_->SetEventOnCompletion(fence_values_[backbuffer_index_], fence_event_.Get()));
        WaitForSingleObjectEx(fence_event_.Get(), INFINITE, FALSE);
    }
    // -- set fence value for the next frame
    fence_values_[backbuffer_index_] = current_fence_value + 1;
}
// -- try to acquire the high-performance adapter supporting D3D12, if not available, try WARP
void DeviceResources::InitAdapter (IDXGIAdapter1 ** adapter_pp) {
    *adapter_pp = nullptr;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIFactory6> factory6;
    HRESULT hr = dxgi_factory_.As(&factory6);
    if (FAILED(hr))
        throw exception("DXGI 1.6 not supported");
    for (
        UINT i = 0;
        DXGI_ERROR_NOT_FOUND !=
        factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        ++i
    ) {
        if (adapterid_override_ != UINT_MAX && i != adapterid_override_)
            continue;
        DXGI_ADAPTER_DESC1 desc;
        ThrowIfFailed(adapter->GetDesc1(&desc));
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;   // don't select basic render driver adapter
        // -- check to see if adapter suppoerts D3D12 but don't create device yet
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), min_feature_level_, __uuidof(ID3D12Device), nullptr))) {
            adapterid_ = i;
            adapter_desc_ = desc.Description;
#if defined(_DEBUG)
            wchar_t buff[256] = {};
            swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n",
                i, desc.VendorId, desc.DeviceId, desc.Description);
            OutputDebugStringW(buff);
#endif
            break;
        }
    }
#if !defined(NDEBUG)
    if (!adapter && adapterid_override_ == UINT_MAX) {
        // try WARP instead
        if (FAILED(dxgi_factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
         throw exception("WARP12 not available. Enable Graphics Tools optional feature");
        OutputDebugStringA("Direct3D Adapter - WARP12\n");
    }
#endif
    if (!adapter)
        if (adapterid_override_ != UINT_MAX)
            throw exception("Unavailable adapter requested");
        else
            throw exception("Unavailable adapter");

    *adapter_pp = adapter.Detach();
}
