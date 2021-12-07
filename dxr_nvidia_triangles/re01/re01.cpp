#include "re01.hpp"

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
    desc.Format = fmt;
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
        vec3(0.866f, -0.5f, 0),
        vec3(-0.866f, -0.5f, 0),
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
static ID3D12ResourcePtr
create_plane_vb (ID3D12Device5Ptr dev) {
    vec3 const vertices [] = {
        vec3(-100, -1, -2),
        vec3(100, -1, 100),
        vec3(-100, -1, 100),

        vec3(-100, -1, -2),
        vec3(100, -1, -2),
        vec3(100, -1, 100)
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
static MyDemo::AccelerationStructureBuffers
create_bottom_level_as (
    ID3D12Device5Ptr dev,
    ID3D12GraphicsCommandList4Ptr cmdlist,
    ID3D12ResourcePtr vb [], // each geometry has exactly one VB
    uint32_t vertex_count [],
    uint32_t geometry_count
) {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geom_descs;
    geom_descs.resize(geometry_count);

    for (uint32_t i = 0; i < geometry_count; ++i) {
        geom_descs[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geom_descs[i].Triangles.VertexBuffer.StartAddress = vb[i]->GetGPUVirtualAddress();
        geom_descs[i].Triangles.VertexBuffer.StrideInBytes = sizeof(vec3);
        geom_descs[i].Triangles.VertexCount = vertex_count[i];
        geom_descs[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geom_descs[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = geometry_count;
    inputs.pGeometryDescs = geom_descs.data();
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    // -- get the required info (e.g., size) for scratch and AS buffers:
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // -- create the buffers, they need to support UAV,
    // -- and since we're gonna use them immediately, create them with an unordererd-access state:
    MyDemo::AccelerationStructureBuffers buffers = {};
    buffers.Scratch = create_buffer(
        dev,
        info.ScratchDataSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COMMON,
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
static void
build_top_level_as (
    ID3D12Device5Ptr dev,
    ID3D12GraphicsCommandList4Ptr cmdlist,
    ID3D12ResourcePtr bottom_level_as[2], // a single trinagle, a trinagle plus a plane
    uint64_t & tlas_size,
    float rotation,
    bool update,
    MyDemo::AccelerationStructureBuffers & buffers
) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = 3; // three triangles
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    // -- get size of TLAS buffers:
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    if (update) {
        D3D12_RESOURCE_BARRIER uav_barrier = {};
        uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barrier.UAV.pResource = buffers.Result;
        cmdlist->ResourceBarrier(1, &uav_barrier);
    } else {
        // -- create the buffers:
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
        buffers.InstanceDesc = create_buffer(
            dev,
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 3, // three instances
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            UploadHeapProps
        );
        tlas_size = info.ResultDataMaxSizeInBytes;
    }

    // -- map the InstanceDesc buffer
    D3D12_RAYTRACING_INSTANCE_DESC * inst_desc = nullptr;
    buffers.InstanceDesc->Map(0, nullptr, (void **)&inst_desc);

    assert(inst_desc);

    // -- initialize the InstanceDesc (three instances)
    ZeroMemory(inst_desc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 3);

    mat4 transforms[3];
    transforms[0] = mat4(1.0f);
    mat4 rotation_mat = eulerAngleY(rotation);
    transforms[1] = translate(mat4(1.0f), vec3(-2, 0, 0)) * rotation_mat;
    transforms[2] = translate(mat4(1.0f), vec3(2, 0, 0)) * rotation_mat;

    // -- first instance desc describes "triangle plus plane" instance:
    inst_desc[0].InstanceID = 0;
    inst_desc[0].InstanceContributionToHitGroupIndex = 0;
    inst_desc[0].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    memcpy(inst_desc[0].Transform, &transforms[0], sizeof(inst_desc[0].Transform));
    inst_desc[0].AccelerationStructure = bottom_level_as[0]->GetGPUVirtualAddress();
    inst_desc[0].InstanceMask = 0xff;

    // -- second and third instances each describe just a triangle
    for (uint32_t i = 1; i < 3; ++i) {
        inst_desc[i].InstanceID = i; // this value is exposed to shader via InstanceID()
         // -- InstanceContributionToHitGroupIndex is offset inside shader table,
         // -- it's needed bc we use different cbuffer per instance:
         // -- +1 because the plane takes an additional entry in the shader table (refer to the docx file)
        inst_desc[i].InstanceContributionToHitGroupIndex = (i * 2) + 2;
        inst_desc[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        mat4 mat = transpose(transforms[i]); // glm is column major, DXR expects row major transforms
        memcpy(inst_desc[i].Transform, &mat, sizeof(inst_desc[i].Transform));
        inst_desc[i].AccelerationStructure = bottom_level_as[1]->GetGPUVirtualAddress();
        inst_desc[i].InstanceMask = 0xff;
    }
    // -- unmap when done:
    buffers.InstanceDesc->Unmap(0, nullptr);

    // -- now create the top-level AS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_desc = {};
    tlas_desc.Inputs = inputs; // type: TOP_LEVEL
    tlas_desc.Inputs.InstanceDescs = buffers.InstanceDesc->GetGPUVirtualAddress();
    tlas_desc.DestAccelerationStructureData = buffers.Result->GetGPUVirtualAddress();
    tlas_desc.ScratchAccelerationStructureData = buffers.Scratch->GetGPUVirtualAddress();

    // -- if this is an update operation, set the "Source" buffer and add PERFORM_UPDATE flag
    if (update) {
        tlas_desc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        tlas_desc.SourceAccelerationStructureData = buffers.Result->GetGPUVirtualAddress();
    }

    cmdlist->BuildRaytracingAccelerationStructure(&tlas_desc, 0, nullptr);

    // -- we need to insert a UAV barrier before using the AS in raytracing operation:
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = buffers.Result;
    cmdlist->ResourceBarrier(1, &barrier);
}

#pragma endregion Creating Acceleration Structure

#pragma region Creating State Subobjects:

static ID3DBlobPtr
compile_library (WCHAR const * filename, WCHAR const * target) {
    D3D_CALL(g_dxc_dll_helper.Initialize());
    IDxcCompilerPtr compiler;
    IDxcLibraryPtr library;
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcCompiler, &compiler));
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcLibrary, &library));

    std::ifstream shader_file(filename);
    if (false == shader_file.good()) {
        MsgBox("Can't open file " + WStrToStr(std::wstring(filename)));
        return nullptr;
    }
    std::stringstream str_stream;
    str_stream << shader_file.rdbuf();
    std::string shader = str_stream.str();

    IDxcBlobEncodingPtr txtblob;
    D3D_CALL(library->CreateBlobWithEncodingFromPinned(
        (LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &txtblob
    ));

    IDxcOperationResultPtr result;
    D3D_CALL(compiler->Compile(
        txtblob, filename, L"", target, nullptr, 0, nullptr, 0, nullptr,
        &result
    ));

    HRESULT hr;
    D3D_CALL(result->GetStatus(&hr));
    if (FAILED(hr)) {
        IDxcBlobEncodingPtr err;
        D3D_CALL(result->GetErrorBuffer(&err));
        std::string log = ConvertBlobToString(err.GetInterfacePtr());
        MsgBox("compile error: \n" + log);
        return nullptr;
    }
    MAKE_SMART_COM_PTR(IDxcBlob);
    IDxcBlobPtr blob;
    D3D_CALL(result->GetResult(&blob));
    return blob;
}
static ID3D12RootSignaturePtr
create_root_sig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
    ID3DBlobPtr sig_blob;
    ID3DBlobPtr err_blob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &err_blob);
    if (FAILED(hr)) {
        MsgBox(ConvertBlobToString(err_blob.GetInterfacePtr()));
        return nullptr;
    }
    ID3D12RootSignaturePtr root_sig;
    D3D_CALL(dev->CreateRootSignature(
        0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)
    ));
    return root_sig;
}
struct MyRootSigDesc {
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_ROOT_PARAMETER> root_params;
};
static MyRootSigDesc
create_raygen_root_sig_desc () {
    MyRootSigDesc ret = {};
    ret.ranges.resize(2); // SRV to input scene AS aka TLAS (t0), UAV to output RWTexture2D (u0)

    ret.ranges[0].BaseShaderRegister = 0;
    ret.ranges[0].NumDescriptors = 1;
    ret.ranges[0].RegisterSpace = 0;
    ret.ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ret.ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ret.ranges[1].BaseShaderRegister = 0;
    ret.ranges[1].NumDescriptors = 1;
    ret.ranges[1].RegisterSpace = 0;
    ret.ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ret.ranges[1].OffsetInDescriptorsFromTableStart = 1;

    ret.root_params.resize(1); // one table containing two ranges
    ret.root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ret.root_params[0].DescriptorTable.NumDescriptorRanges = (UINT)ret.ranges.size();
    ret.root_params[0].DescriptorTable.pDescriptorRanges = ret.ranges.data();

    ret.desc.NumParameters = (UINT)ret.root_params.size();
    ret.desc.pParameters = ret.root_params.data();
    ret.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return ret;
}
static MyRootSigDesc
create_tri_hit_root_sig_desc () {
    MyRootSigDesc ret = {};
    ret.root_params.resize(1); // CBV for PerFrame cbuffer (b0)
    ret.root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    ret.root_params[0].Descriptor.RegisterSpace = 0;
    ret.root_params[0].Descriptor.ShaderRegister = 0;

    ret.desc.NumParameters = 1;
    ret.desc.pParameters = ret.root_params.data();
    ret.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return ret;
}
static MyRootSigDesc
create_plane_root_sig_desc () {
    MyRootSigDesc ret = {};
    ret.ranges.resize(1); // SRV to input scene AS aka TLAS (t0)

    ret.ranges[0].BaseShaderRegister = 0;
    ret.ranges[0].NumDescriptors = 1;
    ret.ranges[0].RegisterSpace = 0;
    ret.ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ret.ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ret.root_params.resize(1); // one table containing one range
    ret.root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ret.root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    ret.root_params[0].DescriptorTable.pDescriptorRanges = ret.ranges.data();

    ret.desc.NumParameters = 1;
    ret.desc.pParameters = ret.root_params.data();
    ret.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return ret;
}
struct DxilLibrary {
    D3D12_DXIL_LIBRARY_DESC DxilLibDesc = {};
    D3D12_STATE_SUBOBJECT Subobj = {};
    ID3DBlobPtr ShaderBlob = {};
    std::vector<D3D12_EXPORT_DESC> ExportDescs;
    std::vector<std::wstring> ExportNames;

    DxilLibrary (ID3DBlobPtr blob, WCHAR const * entry_points [], uint32_t entry_point_count)
        : ShaderBlob(blob)
    {
        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        Subobj.pDesc = &DxilLibDesc;
        DxilLibDesc = {};
        ExportDescs.resize(entry_point_count);
        ExportNames.resize(entry_point_count);
        if (blob) {
            DxilLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
            DxilLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
            DxilLibDesc.NumExports = entry_point_count;
            DxilLibDesc.pExports = ExportDescs.data();
            for (uint32_t i = 0; i < entry_point_count; ++i) {
                ExportNames[i] = entry_points[i];
                ExportDescs[i].Name = ExportNames[i].c_str();
                ExportDescs[i].Flags = D3D12_EXPORT_FLAG_NONE;
                ExportDescs[i].ExportToRename = nullptr;
            }
        }
    }
    DxilLibrary () : DxilLibrary(nullptr, nullptr, 0) {}
};


static WCHAR const * RayGenShader = L"raygen";
static WCHAR const * MissShader = L"miss";
static WCHAR const * TriangleChs = L"chs_triangle";
static WCHAR const * PlaneChs = L"chs_plane";
static WCHAR const * TriangleHitGroup = L"hitgroup_triangle";
static WCHAR const * PlaneHitGroup = L"hitgroup_plane";
static WCHAR const * ShadowChs = L"shadow_chs";
static WCHAR const * ShadowMiss = L"shadow_miss";
static WCHAR const * ShadowHitGroup = L"hitgroup_shadow";

static DxilLibrary
create_dxil_library () {
    ID3DBlobPtr shader_blob = compile_library(L"re01.hlsl", L"lib_6_3");
    wchar_t const * entry_points [] = {RayGenShader, MissShader, PlaneChs, TriangleChs, ShadowMiss, ShadowChs};
    return DxilLibrary(shader_blob, entry_points, _countof(entry_points));
}

struct HitProgram {
    std::wstring ExportName;
    D3D12_HIT_GROUP_DESC Desc;
    D3D12_STATE_SUBOBJECT Subobj;
    HitProgram (LPCWSTR ahs, LPCWSTR chs, std::wstring const & name)
        : ExportName(name)
    {
        Desc = {};
        Desc.AnyHitShaderImport = ahs;
        Desc.ClosestHitShaderImport = chs;
        Desc.HitGroupExport = ExportName.c_str();

        Subobj.pDesc = &Desc;
        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    }
};

// -- Associates a subobject defined directly in a state object with shader exports
struct ExportAssociation {
    D3D12_STATE_SUBOBJECT Subobj = {};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION SoToAssoc = {};
    ExportAssociation (
        WCHAR const * export_names[], uint32_t export_count,
        D3D12_STATE_SUBOBJECT const * subobj_to_associate
    ) {
        SoToAssoc.NumExports = export_count;
        SoToAssoc.pExports = export_names;
        SoToAssoc.pSubobjectToAssociate = subobj_to_associate;
        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        Subobj.pDesc = &SoToAssoc;
    }
};

struct LocalRootSig {
    ID3D12RootSignaturePtr RootSig;
    ID3D12RootSignature * Interface = nullptr;
    D3D12_STATE_SUBOBJECT Subobj = {};
    LocalRootSig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
        RootSig = create_root_sig(dev, desc);
        Interface = RootSig.GetInterfacePtr();
        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        Subobj.pDesc = &Interface;
    }
};

struct GlobalRootSig {
    ID3D12RootSignaturePtr RootSig;
    ID3D12RootSignature * Interface = nullptr;
    D3D12_STATE_SUBOBJECT Subobj = {};
    GlobalRootSig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
        RootSig = create_root_sig(dev, desc);
        Interface = RootSig.GetInterfacePtr();
        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        Subobj.pDesc = &Interface;
    }
};

struct ShaderConfig {
    D3D12_RAYTRACING_SHADER_CONFIG ShaderCfg = {};
    D3D12_STATE_SUBOBJECT Subobj = {};
    ShaderConfig (uint32_t max_attrib_size_in_bytes, uint32_t max_payload_size_in_bytes) {
        ShaderCfg.MaxAttributeSizeInBytes = max_attrib_size_in_bytes;
        ShaderCfg.MaxPayloadSizeInBytes = max_payload_size_in_bytes;

        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        Subobj.pDesc = &ShaderCfg;
    }
};

struct PipelineConfig {
    D3D12_RAYTRACING_PIPELINE_CONFIG Cfg = {};
    D3D12_STATE_SUBOBJECT Subobj = {};
    PipelineConfig (uint32_t max_trace_recursion_depth) {
        Cfg.MaxTraceRecursionDepth = max_trace_recursion_depth;

        Subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        Subobj.pDesc = &Cfg;
    }
};

#pragma endregion Creating State Subobjects

// ==============================================================================================================

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
    for (uint32_t i = 0; i < _countof(FrameObjects_); ++i) {
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
    ID3D12DescriptorHeap * heaps [] = {srv_uav_heap_};
    cmdlist_->SetDescriptorHeaps(_countof(heaps), heaps);
    return swapchain_->GetCurrentBackBufferIndex();
}
void MyDemo::end_frame (uint32_t rtv_idx) {
    resource_barrier(cmdlist_, FrameObjects_[rtv_idx].swapchain_buffer_,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    fence_value_ = submit_cmdlist(cmdlist_, cmdque_, fence_, fence_value_);
    swapchain_->Present(0, 0);

    // -- make sure new back buffer is ready
    //if (fence_value_ > DefaultSwapchainBufferCount) {
    //    fence_->SetEventOnCompletion(fence_value_ - DefaultSwapchainBufferCount + 1, fence_event_);
    //    WaitForSingleObject(fence_event_, INFINITE);
    //}

    // -- TLAS resources are not double buffered, 
    // -- so we need incremental sync to update them, aka
    // -- the term  - DefaultSwapchainBufferCount + 1 is not working now:
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);

    // -- prepare cmdlist for next frame:
    uint32_t buffer_idx = swapchain_->GetCurrentBackBufferIndex();
    FrameObjects_[buffer_idx].cmdalloc_->Reset();
    cmdlist_->Reset(FrameObjects_[buffer_idx].cmdalloc_, nullptr);
}

// ==============================================================================================================

void MyDemo::create_acceleration_structures () {
    vertex_buffers_[0] = create_triangle_vb(dev_);
    vertex_buffers_[1] = create_plane_vb(dev_);
    AccelerationStructureBuffers bottom_level_buffers[2];

    // -- first BLAS is for a triangle plus a plane:
    uint32_t vertex_count [] = {3, 6};
    bottom_level_buffers[0] = create_bottom_level_as(dev_, cmdlist_, vertex_buffers_, vertex_count, 2);
    bottom_level_as_[0] = bottom_level_buffers[0].Result;

    // -- second BLAS is for just a single triangle:
    bottom_level_buffers[1] = create_bottom_level_as(dev_, cmdlist_, vertex_buffers_, vertex_count, 1);
    bottom_level_as_[1] = bottom_level_buffers[1].Result;

    // -- create the TLAS:
    build_top_level_as(dev_, cmdlist_, bottom_level_as_, tlas_size_, 0, false, top_level_buffers_);

    // -- we don't have any resource lifetime management so we flush and sync here
    // -- if we had resource lifetime mgmt we could submit list whenever we like
    fence_value_ = submit_cmdlist(cmdlist_, cmdque_, fence_, fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
    uint32_t buffer_index = swapchain_->GetCurrentBackBufferIndex();
    cmdlist_->Reset(FrameObjects_[0].cmdalloc_, nullptr);
}

// ==============================================================================================================

void MyDemo::create_rtpso () {

    // subobjs:
    /*
        1 for DXIL library
        3 for Hit Groups (TRi, Plane, Shadow)
        2 for Rygn root sig and subobj assoc
        2 for Tri hit-prog root sig and subobj assoc
        2 for Plane Hit-prog root sig and subobj assoc
        2 for shadow prog and miss root sig and subobj assoc
        2 for shader config (shared between all progs) and its assoc
        1 for pipeline cfg
        1 for global root sig
    */
    std::array<D3D12_STATE_SUBOBJECT, 16> subobjs;
    uint32_t index = 0;

    DxilLibrary dl = create_dxil_library();
    subobjs[index++] = dl.Subobj;

    HitProgram tri_hp(nullptr, TriangleChs, TriangleHitGroup);
    subobjs[index++] = tri_hp.Subobj;

    HitProgram plane_hp(nullptr, PlaneChs, PlaneHitGroup);
    subobjs[index++] = plane_hp.Subobj;

    HitProgram shadow_hp(nullptr, ShadowChs, ShadowHitGroup);
    subobjs[index++] = shadow_hp.Subobj;

    LocalRootSig rygn(dev_, create_raygen_root_sig_desc().desc);
    subobjs[index] = rygn.Subobj;
    uint32_t rygn_index = index++;
    ExportAssociation rygn_assoc(&RayGenShader, 1, &subobjs[rygn_index]);
    subobjs[index++] = rygn_assoc.Subobj;

    LocalRootSig tri(dev_, create_tri_hit_root_sig_desc().desc);
    subobjs[index] = tri.Subobj;
    uint32_t tri_index = index++;
    ExportAssociation tri_assoc(&TriangleChs, 1, &subobjs[tri_index]);
    subobjs[index++] = tri_assoc.Subobj;

    LocalRootSig plane(dev_, create_plane_root_sig_desc().desc);
    subobjs[index] = plane.Subobj;
    uint32_t plane_index = index++;
    ExportAssociation plane_assoc(&PlaneChs, 1, &subobjs[plane_index]);
    subobjs[index++] = plane_assoc.Subobj;

    D3D12_ROOT_SIGNATURE_DESC empty_dsc = {};
    empty_dsc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    LocalRootSig empty(dev_, empty_dsc);
    subobjs[index] = empty.Subobj;
    uint32_t empty_index = index++;
    WCHAR const * empty_exports[] = {MissShader, ShadowChs, ShadowMiss};
    ExportAssociation empty_assoc(empty_exports, _countof(empty_exports), &subobjs[empty_index]);
    subobjs[index++] = empty_assoc.Subobj;

    ShaderConfig shader_cfg(sizeof(float) * 2, sizeof(float) * 3);
    subobjs[index] = shader_cfg.Subobj;
    uint32_t shader_cfg_index = index++;
    WCHAR const * shader_exports[] = {RayGenShader, MissShader, TriangleChs, PlaneChs, ShadowMiss, ShadowChs};
    ExportAssociation cfg_assoc(shader_exports, _countof(shader_exports), &subobjs[shader_cfg_index]);
    subobjs[index++] = cfg_assoc.Subobj;

    PipelineConfig cfg(2);
    subobjs[index++] = cfg.Subobj;

    GlobalRootSig root_sig(dev_, {});
    empty_root_sig_ = root_sig.RootSig;
    subobjs[index++] = root_sig.Subobj;

    assert(16 == index);
    
    D3D12_STATE_OBJECT_DESC desc = {};
    desc.NumSubobjects = index;
    desc.pSubobjects = subobjs.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    D3D_CALL(dev_->CreateStateObject(&desc, IID_PPV_ARGS(&rtpso_)));
}

// ==============================================================================================================

void MyDemo::create_shader_table () {

    // shader table layout:
    /*
        Entry 0         rygn
        Entry 1         miss0 (primary)
        Entry 2         miss1 (shadow)
        Entry 3,4       tri0
        Entry 5,6       plane
        Entry 7,8       tri1
        Entry 9,10      tri2
    */

    shader_table_entry_size_ = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    shader_table_entry_size_ += 8; // cbuffer descrptr
    shader_table_entry_size_ = ALIGN_TO(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, shader_table_entry_size_);
    uint32_t total_size = 11 * shader_table_entry_size_;

    shader_table_ = create_buffer(dev_, total_size, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, UploadHeapProps);

    uint8_t * data = nullptr;
    D3D_CALL(shader_table_->Map(0, nullptr, (void **)&data));

    MAKE_SMART_COM_PTR(ID3D12StateObjectProperties);
    ID3D12StateObjectPropertiesPtr rtpso_props;
    rtpso_->QueryInterface(IID_PPV_ARGS(&rtpso_props));

    memcpy(data, rtpso_props->GetShaderIdentifier(RayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint64_t heap_start = srv_uav_heap_->GetGPUDescriptorHandleForHeapStart().ptr;
    *(uint64_t *)(data + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heap_start;

    memcpy(data + shader_table_entry_size_, rtpso_props->GetShaderIdentifier(MissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(data + 2 * shader_table_entry_size_, rtpso_props->GetShaderIdentifier(ShadowMiss), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    uint8_t * e3 = data + shader_table_entry_size_ * 3;
    memcpy(e3, rtpso_props->GetShaderIdentifier(TriangleHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint8_t * cb = e3 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    assert(0 == ((uint64_t)cb % 8));
    *(D3D12_GPU_VIRTUAL_ADDRESS *)cb = cbuffers_[0]->GetGPUVirtualAddress();

    uint8_t * e4 = data + shader_table_entry_size_ * 4;
    memcpy(e4, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    uint8_t * e5 = data + shader_table_entry_size_ * 5;
    memcpy(e5, rtpso_props->GetShaderIdentifier(PlaneHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    *(uint64_t *)(e5 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) =
        heap_start + dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    uint8_t * e6 = data + shader_table_entry_size_ * 6;
    memcpy(e6, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    uint8_t * e7 = data + shader_table_entry_size_ * 7;
    memcpy(e7, rtpso_props->GetShaderIdentifier(TriangleHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    cb = e7 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    *(D3D12_GPU_VIRTUAL_ADDRESS *)cb = cbuffers_[1]->GetGPUVirtualAddress();

    uint8_t * e8 = data + shader_table_entry_size_ * 8;
    memcpy(e8, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    uint8_t * e9 = data + shader_table_entry_size_ * 9;
    memcpy(e9, rtpso_props->GetShaderIdentifier(TriangleHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    cb = e9 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    *(D3D12_GPU_VIRTUAL_ADDRESS *)cb = cbuffers_[2]->GetGPUVirtualAddress();

    uint8_t * e10 = data + shader_table_entry_size_ * 10;
    memcpy(e10, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    shader_table_->Unmap(0, nullptr);
}

// ==============================================================================================================

void MyDemo::create_shader_resources () {
    // -- create output resource, with the dimensions and format matching the swapchain
    D3D12_RESOURCE_DESC output_desc = {};
    output_desc.DepthOrArraySize = 1;
    output_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    // NOTE(omid): actually the backbuffer format is SRGB but UAV doesn't support that,
    // -- so we convert to SRGB ourselves in the shader
    output_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    output_desc.Width = swapchain_size_.x;
    output_desc.Height = swapchain_size_.y;
    output_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    output_desc.MipLevels = 1;
    output_desc.SampleDesc.Count = 1;
    output_desc.SampleDesc.Quality = 0;
    D3D_CALL(dev_->CreateCommittedResource(
        &DefaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &output_desc,
        D3D12_RESOURCE_STATE_COPY_SOURCE, // starting as copy-source to simplify the OnFrameRender()
        nullptr,
        IID_PPV_ARGS(&output_)
    ));

    // -- create SRV/UAV descriptor heap:
    // -- we need 2 entries: 1 SRV for scene and 1 UAV for the output
    srv_uav_heap_ = create_descriptor_heap(dev_, 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

    // -- create the UAV: 
    // -- based on the root sig we created UAV should be the first entry (see "create_raygen_root_sig_desc")
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    dev_->CreateUnorderedAccessView(output_, nullptr, &uav_desc, srv_uav_heap_->GetCPUDescriptorHandleForHeapStart());

    // -- create SRV for TLAS right after UAV
    // -- N.B., we're using a different SRV desc here
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.RaytracingAccelerationStructure.Location = top_level_buffers_.Result->GetGPUVirtualAddress();
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_srv = srv_uav_heap_->GetCPUDescriptorHandleForHeapStart();
    hcpu_srv.ptr += dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dev_->CreateShaderResourceView(nullptr, &srv_desc, hcpu_srv);
}

// ==============================================================================================================

void MyDemo::create_cbuffers () {
    // -- the shader code declares each cbuffer with 3 float3, but
    // -- due to HLSL packing rules, we create each cbuffer with 3 float4
    // NOTE(omid): each float3 needs to start on a 16-byte boundary
    vec4 buffer_data [] = {
        // Instance 0
        vec4(1.0f, 0.0f, 0.0f, 1.0f),
        vec4(1.0f, 1.0f, 0.0f, 1.0f),
        vec4(1.0f, 0.0f, 1.0f, 1.0f),

        // Instance B
        vec4(0.0f, 1.0f, 0.0f, 1.0f),
        vec4(0.0f, 1.0f, 1.0f, 1.0f),
        vec4(1.0f, 1.0f, 0.0f, 1.0f),

        // Instance C
        vec4(0.0f, 0.0f, 1.0f, 1.0f),
        vec4(1.0f, 0.0f, 1.0f, 1.0f),
        vec4(0.0f, 1.0f, 1.0f, 1.0f)
    };
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t const buffer_size = sizeof(vec4) * 3;
        cbuffers_[i] = create_buffer(
            dev_,
            sizeof(buffer_data),
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            UploadHeapProps
        );
        uint8_t * data = nullptr;
        D3D_CALL(cbuffers_[i]->Map(0, nullptr, (void **)&data));
        memcpy(data, &buffer_data[i * 3], sizeof(buffer_data));
        cbuffers_[i]->Unmap(0, nullptr);
    }
}

// ==============================================================================================================

void MyDemo::OnLoad (HWND hwnd, uint32_t w, uint32_t h) {
    init_dxr(hwnd, w, h);
    create_acceleration_structures();
    create_rtpso();
    create_shader_resources();
    create_cbuffers();
    create_shader_table();
}
void MyDemo::OnFrameRender () {
    uint32_t rtv_idx = begin_frame();

    build_top_level_as(dev_, cmdlist_, bottom_level_as_, tlas_size_, rotation_, true, top_level_buffers_);
    rotation_ += 0.005f;

    // -- raytrace:
    resource_barrier(cmdlist_, output_, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_DISPATCH_RAYS_DESC desc = {};
    desc.Width = swapchain_size_.x;
    desc.Height = swapchain_size_.y;
    desc.Depth = 1;

    // -- raygen is the first entry in the shader-table:
    desc.RayGenerationShaderRecord.StartAddress = shader_table_->GetGPUVirtualAddress() + 0 * shader_table_entry_size_;
    desc.RayGenerationShaderRecord.SizeInBytes = shader_table_entry_size_;

    // -- miss is the second entry in the shader-table:
    size_t miss_offset = 1 * shader_table_entry_size_;
    desc.MissShaderTable.StartAddress = shader_table_->GetGPUVirtualAddress() + miss_offset;
    desc.MissShaderTable.StrideInBytes = shader_table_entry_size_;
    desc.MissShaderTable.SizeInBytes = shader_table_entry_size_ * 2; // 2 miss entries

    // -- hit is the fourth entry in the shader table:
    size_t hit_offset = 3 * shader_table_entry_size_;
    desc.HitGroupTable.StartAddress = shader_table_->GetGPUVirtualAddress() + hit_offset;
    desc.HitGroupTable.StrideInBytes = shader_table_entry_size_;
    desc.HitGroupTable.SizeInBytes = shader_table_entry_size_ * 8; // 8 hit entries: 4 instance0, 2 instance1, 2 instance2

    // -- bind the empty root sig (because we're not using a global root signature):
    cmdlist_->SetComputeRootSignature(empty_root_sig_);

    // -- dispatch call:
    cmdlist_->SetPipelineState1(rtpso_.GetInterfacePtr());
    cmdlist_->DispatchRays(&desc);

    // -- copy the results to the backbuffer:
    resource_barrier(cmdlist_, output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    resource_barrier(cmdlist_, FrameObjects_[rtv_idx].swapchain_buffer_, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdlist_->CopyResource(FrameObjects_[rtv_idx].swapchain_buffer_, output_);

    /*float const clear_color[4] = {0.4f, 0.6f, 0.2f, 1.0f};
    resource_barrier(cmdlist_, FrameObjects_[rtv_idx].swapchain_buffer_,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdlist_->ClearRenderTargetView(FrameObjects_[rtv_idx].hcpu_rtv_, clear_color, 0, nullptr);*/


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
    Framework::Run(MyDemo(), "Raytracing");
    return(0);
}


