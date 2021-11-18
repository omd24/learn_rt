#include "mydemo.hpp"

static dxc::DxcDllSupport g_dxc_dll_helper;
MAKE_SMART_COM_PTR(IDxcCompiler);
MAKE_SMART_COM_PTR(IDxcLibrary);
MAKE_SMART_COM_PTR(IDxcBlobEncoding);
MAKE_SMART_COM_PTR(IDxcOperationResult);

#pragma region Initializing DXR:

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

#pragma endregion Initializing DXR

#pragma region Creating Acceleration Structure:

static constexpr D3D12_HEAP_PROPERTIES UploadHeapProps = {
    .Type                   = D3D12_HEAP_TYPE_UPLOAD,
    .CPUPageProperty        = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference   = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask       = 0,
    .VisibleNodeMask        = 0
};
static constexpr D3D12_HEAP_PROPERTIES DefaultHeapProps = {
    .Type                   = D3D12_HEAP_TYPE_DEFAULT,
    .CPUPageProperty        = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference   = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask       = 0,
    .VisibleNodeMask        = 0
};
static ID3D12ResourcePtr
create_buffer (
    ID3D12Device5Ptr dev, uint64_t size,
    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES init_state,
    D3D12_HEAP_PROPERTIES const & heap_props
) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Alignment = 0;
    desc.DepthOrArraySize = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Flags = flags;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Width = size;
    desc.Height = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ID3D12ResourcePtr buffer;
    D3D_CALL(dev->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE, &desc, init_state, nullptr, IID_PPV_ARGS(&buffer)));
    return buffer;
}
static ID3D12ResourcePtr
create_triangle_vb (ID3D12Device5Ptr dev) {
    vec3 const vertices [] = {
        vec3(0, 1, 0),
        vec3(0.8f, -0.5f, 0),
        vec3(-0.8f, -0.5f, 0),
    };
    // -- for simplicity, we create vb on an upload heap but in practice a default heap is recommended
    // -- though an upload buffer would be needed for updating data to the vb
    ID3D12ResourcePtr buffer = create_buffer(dev, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ, UploadHeapProps);
    uint8_t * data;
    buffer->Map(0, nullptr, (void **)&data);
    memcpy(data, vertices, sizeof(vertices));
    buffer->Unmap(0, nullptr);
    return buffer;
}
struct AccelerationStructureBuffers {
    ID3D12ResourcePtr Scratch;
    ID3D12ResourcePtr Result;
    ID3D12ResourcePtr InstanceDesc; // only for Top-Level AS
};
static AccelerationStructureBuffers
create_bottom_level_as (
    ID3D12Device5Ptr dev,
    ID3D12GraphicsCommandList4Ptr cmdlist,
    ID3D12ResourcePtr vb
) {
    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc = {};
    geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom_desc.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
    geom_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(vec3);
    geom_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom_desc.Triangles.VertexCount = 3;
    geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geom_desc;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    // -- get the required info (e.g., size) for scratch and AS buffers:
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // -- create the buffers, they need to support UAV,
    // -- and since we're gonna use them immediately, create them with an unordererd-access state:
    AccelerationStructureBuffers buffers = {};
    buffers.Scratch = create_buffer(
        dev,
        info.ScratchDataSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        DefaultHeapProps
    );
    buffers.Result = create_buffer(
        dev,
        info.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        DefaultHeapProps
    );

    // -- now create the bottom-level AS:
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_desc = {};
    blas_desc.Inputs = inputs; // type: BOTTOM_LEVEL
    blas_desc.DestAccelerationStructureData = buffers.Result->GetGPUVirtualAddress();
    blas_desc.ScratchAccelerationStructureData = buffers.Scratch->GetGPUVirtualAddress();

    cmdlist->BuildRaytracingAccelerationStructure(&blas_desc, 0, nullptr);

    // -- we need to insert a UAV barrier before using the AS in raytracing operation:
    D3D12_RESOURCE_BARRIER uav_barrier = {};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = buffers.Result;
    cmdlist->ResourceBarrier(1, &uav_barrier);

    return buffers;
}
static AccelerationStructureBuffers
create_top_level_as (
    ID3D12Device5Ptr dev,
    ID3D12GraphicsCommandList4Ptr cmdlist,
    ID3D12ResourcePtr bottom_level_as,
    uint64_t & tlas_size
) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    // -- get size of TLAS buffers:
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // -- create the buffers:
    AccelerationStructureBuffers buffers = {};
    buffers.Scratch = create_buffer(
        dev,
        info.ScratchDataSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        DefaultHeapProps
    );
    buffers.Result = create_buffer(
        dev,
        info.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        DefaultHeapProps
    );

    // -- output size:
    tlas_size = info.ResultDataMaxSizeInBytes;

    // NOTE(omid): InstanceDesc should be inside a buffer 
    // -- create and map the corresponding buffer:
    buffers.InstanceDesc = create_buffer(
        dev,
        sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        UploadHeapProps
    );
    D3D12_RAYTRACING_INSTANCE_DESC * inst_desc = nullptr;
    buffers.InstanceDesc->Map(0, nullptr, (void **)&inst_desc);

    // -- initialize the InstanceDesc (we only have one instance)
    inst_desc->InstanceID = 0; // this value is exposed to shader via InstanceID()
    inst_desc->InstanceContributionToHitGroupIndex = 0; // this is offset inside shader table
    inst_desc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    mat4 mat; // identity matrix
    memcpy(inst_desc->Transform, &mat, sizeof(inst_desc->Transform));
    inst_desc->AccelerationStructure = bottom_level_as->GetGPUVirtualAddress();
    inst_desc->InstanceMask = 0xff;

    // -- unmap when done:
    buffers.InstanceDesc->Unmap(0, nullptr);

    // -- now create the top-level AS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_desc = {};
    tlas_desc.Inputs = inputs; // type: TOP_LEVEL
    tlas_desc.Inputs.InstanceDescs = buffers.InstanceDesc->GetGPUVirtualAddress();
    tlas_desc.DestAccelerationStructureData = buffers.Result->GetGPUVirtualAddress();
    tlas_desc.ScratchAccelerationStructureData = buffers.Scratch->GetGPUVirtualAddress();

    cmdlist->BuildRaytracingAccelerationStructure(&tlas_desc, 0, nullptr);

    // -- we need to insert a UAV barrier before using the AS in raytracing operation:
    D3D12_RESOURCE_BARRIER uav_barrier = {};
    uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav_barrier.UAV.pResource = buffers.Result;
    cmdlist->ResourceBarrier(1, &uav_barrier);

    return buffers;
}

#pragma endregion Creating Acceleration Structure

#pragma region Creating RTPSO
// -- use dxc library to compile a shader and return the compiled code as a blob
static ID3DBlobPtr
compile_library (WCHAR const * filename, WCHAR const * targetstr) {
    // -- init the helper:
    D3D_CALL(g_dxc_dll_helper.Initialize());
    IDxcCompilerPtr compiler;
    IDxcLibraryPtr library;
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcCompiler, &compiler));
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcLibrary, &library));

    // -- open and read the file:
    std::ifstream shader_file(filename);
    if (false == shader_file.good()) {
        MsgBox("Can't open file " + WStrToStr(std::wstring(filename)));
        return nullptr;
    }
    std::stringstream str_stream;
    str_stream << shader_file.rdbuf();
    std::string shader = str_stream.str();

    // -- create blob from string:
    IDxcBlobEncodingPtr txtblob;
    D3D_CALL(library->CreateBlobWithEncodingFromPinned(
        (LPBYTE)shader.c_str(), (uint32_t)shader.size(),
        0, &txtblob
    ));

    // -- compile the shader (via blob):
    IDxcOperationResultPtr result;
    D3D_CALL(compiler->Compile(
        txtblob, filename, L"", targetstr,
        nullptr, 0, nullptr, 0, nullptr,
        &result
    ));

    // -- verify the result:
    HRESULT hr;
    D3D_CALL(result->GetStatus(&hr));
    if (FAILED(hr)) {
        IDxcBlobEncodingPtr err;
        D3D_CALL(result->GetErrorBuffer(&err));
        std::string log = ConvertBlobToString(err.GetInterfacePtr());
        MsgBox("Compile Error:\n" + log);
        return nullptr;
    }

    MAKE_SMART_COM_PTR(IDxcBlob);
    IDxcBlobPtr blob;
    D3D_CALL(result->GetResult(&blob));
    return blob;
}

#pragma endregion


// ==============================================================================================================
void MyDemo::create_acceleration_structure () {
    vertex_buffer_ = create_triangle_vb(dev_);
    AccelerationStructureBuffers bottom_level_buffers =
        create_bottom_level_as(dev_, cmdlist_, vertex_buffer_);
    AccelerationStructureBuffers top_level_buffers =
        create_top_level_as(dev_, cmdlist_, bottom_level_buffers.Result, tlas_size_);

    // -- we don't have any resource lifetime management so we flush and sync here
    // -- if we had resource lifetime mgmt we could submit list whenever we like
    fence_value_ = submit_cmdlist(cmdlist_, cmdque_, fence_, fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
    uint32_t buffer_index = swapchain_->GetCurrentBackBufferIndex();
    cmdlist_->Reset(FrameObjects_[0].cmdalloc_, nullptr);

    // -- store the AS final (aka result) buffers. The rest of the buffers will be released once we exit the function
    top_level_as_ = top_level_buffers.Result;
    bottom_level_as_ = bottom_level_buffers.Result;
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
    create_acceleration_structure();
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
    Framework::Run(MyDemo(), "Acceleration Structure");
    return(0);
}

