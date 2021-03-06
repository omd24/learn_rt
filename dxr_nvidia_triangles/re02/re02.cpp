#include "re02.hpp"

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
compile_library (WCHAR const * file, WCHAR const * target) {
    D3D_CALL(g_dxc_dll_helper.Initialize());
    IDxcCompilerPtr compiler;
    IDxcLibraryPtr library;
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcCompiler, &compiler));
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcLibrary, &library));

    std::ifstream in(file);
    if (false == in.good()) {
        MsgBox("cant open file " + WStrToStr(std::wstring(file)));
        return nullptr;
    }

    std::stringstream ss;
    ss << in.rdbuf();
    std::string shader = ss.str();

    IDxcBlobEncodingPtr txtblob;
    D3D_CALL(library->CreateBlobWithEncodingFromPinned(
        (LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &txtblob
    ));

    IDxcOperationResultPtr result;
    D3D_CALL(compiler->Compile(
        txtblob, file, L"", target, nullptr, 0, nullptr, 0, nullptr, &result
    ));

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
static ID3D12RootSignaturePtr
create_root_sig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
    ID3DBlobPtr sig_blob;
    ID3DBlobPtr err_blob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &err_blob);
    if (FAILED(hr)) {
        std::string msg = ConvertBlobToString(err_blob.GetInterfacePtr());
        MsgBox(msg);
        return nullptr;
    }
    ID3D12RootSignaturePtr root_sig;
    D3D_CALL(dev->CreateRootSignature(
        0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
    return root_sig;
}
struct RootSigDesc {
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_ROOT_PARAMETER> root_params;
};
static RootSigDesc
create_raygen_root_sig_desc () {
    RootSigDesc ret = {};
    ret.ranges.resize(2);

    // -- output:
    ret.ranges[0].BaseShaderRegister = 0;
    ret.ranges[0].NumDescriptors = 1;
    ret.ranges[0].RegisterSpace = 0;
    ret.ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ret.ranges[0].OffsetInDescriptorsFromTableStart = 0;

    // -- scene:
    ret.ranges[1].BaseShaderRegister = 0;
    ret.ranges[1].NumDescriptors = 1;
    ret.ranges[1].RegisterSpace = 0;
    ret.ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ret.ranges[1].OffsetInDescriptorsFromTableStart = 1;

    ret.root_params.resize(1);
    ret.root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ret.root_params[0].DescriptorTable.NumDescriptorRanges = (UINT)ret.ranges.size();
    ret.root_params[0].DescriptorTable.pDescriptorRanges = ret.ranges.data();

    ret.desc.NumParameters = (UINT)ret.root_params.size();
    ret.desc.pParameters = ret.root_params.data();
    ret.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return ret;
}
static RootSigDesc
create_triangle_hit_root_sig_desc () {
    RootSigDesc ret = {};
    ret.root_params.resize(1);
    ret.root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    ret.root_params[0].Descriptor.RegisterSpace = 0;
    ret.root_params[0].Descriptor.ShaderRegister = 0;

    ret.desc.NumParameters = 1;
    ret.desc.pParameters = ret.root_params.data();
    ret.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return ret;
}
static RootSigDesc
create_plane_hit_root_sig_desc () {
    RootSigDesc ret = {};

    ret.ranges.resize(1);
    ret.ranges[0].BaseShaderRegister = 0;
    ret.ranges[0].NumDescriptors = 1; // 1 SRV to bind TLAS to shader
    ret.ranges[0].RegisterSpace = 0;
    ret.ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ret.ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ret.root_params.resize(1);
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
    D3D12_STATE_SUBOBJECT StateSubobj = {};
    ID3DBlobPtr ShaderBlob = {};
    std::vector<D3D12_EXPORT_DESC> ExportDescs;
    std::vector<std::wstring> ExportNames;

    DxilLibrary (ID3DBlobPtr blob, WCHAR const * entry_points [], uint32_t entry_point_count)
        : ShaderBlob(blob)
    {
        StateSubobj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        StateSubobj.pDesc = &DxilLibDesc;
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
    // -- compile shader:
    ID3DBlobPtr dxil_lib = compile_library(L"re02.hlsl", L"lib_6_3");
    wchar_t const * entry_points [] = {RayGenShader, MissShader, PlaneChs, TriangleChs, ShadowMiss, ShadowChs};
    return DxilLibrary(dxil_lib, entry_points, _countof(entry_points));
}

struct HitProgram {
    std::wstring ExportName;
    D3D12_HIT_GROUP_DESC Desc;
    D3D12_STATE_SUBOBJECT Subobject;

    HitProgram (LPCWSTR ahs_export, LPCWSTR chs_export, std::wstring const & name) : ExportName(name) {
        Desc = {};
        Desc.AnyHitShaderImport = ahs_export;
        Desc.ClosestHitShaderImport = chs_export;
        Desc.HitGroupExport = ExportName.c_str();

        Subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        Subobject.pDesc = &Desc;
    }
};
struct ExportAssociation {
    D3D12_STATE_SUBOBJECT Subobject = {};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION Association = {};

    ExportAssociation (
        WCHAR const * export_names [], uint32_t export_count,
        D3D12_STATE_SUBOBJECT const * subobj_to_associate
    ) {
        Association.NumExports = export_count;
        Association.pExports = export_names;
        Association.pSubobjectToAssociate = subobj_to_associate;

        Subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        Subobject.pDesc = &Association;
    }
};
struct LocalRootSig {
    ID3D12RootSignaturePtr RootSig;
    ID3D12RootSignature * Interface = nullptr;
    D3D12_STATE_SUBOBJECT Subobject = {};
    LocalRootSig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
        RootSig = create_root_sig(dev, desc);
        Interface = RootSig.GetInterfacePtr();
        Subobject.pDesc = &Interface;
        Subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    }
};
struct GlobalRootSig {
    ID3D12RootSignaturePtr RootSig;
    ID3D12RootSignature * Interface = nullptr;
    D3D12_STATE_SUBOBJECT Subobject = {};
    GlobalRootSig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
        RootSig = create_root_sig(dev, desc);
        Interface = RootSig.GetInterfacePtr();
        Subobject.pDesc = &Interface;
        Subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    }
};
struct ShaderConfig {
    D3D12_RAYTRACING_SHADER_CONFIG ShaderCfg = {};
    D3D12_STATE_SUBOBJECT Subobject = {};
    ShaderConfig (uint32_t max_attrib_size_in_bytes, uint32_t max_payload_size_in_bytes) {
        ShaderCfg.MaxAttributeSizeInBytes = max_attrib_size_in_bytes;
        ShaderCfg.MaxPayloadSizeInBytes = max_payload_size_in_bytes;

        Subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        Subobject.pDesc = &ShaderCfg;
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
    // NOTE(omid):
    /*
        we need 16 subobjects:
        1 for DXIL Library
        3 for Hit Groups (trinagle hitgroup, plane hitgroup, and shadow hitgroup)
        2 for RayGen Root-Signature and the Subobject Association
        2 for Triangle Hit-Program Root-Signature and the Subobject Association
        2 for Plane Hit-Program and Miss-Shader Root-Signature and the Subobject Association
        2 for Shadow-Program and Miss-Shader Root-Signature and the Subobject Association
        2 for Shader Config (shared between all programs). 1 for the config, 1 for the association
        1 for Pipeline Config
        1 for Global Root-Signature
    */
std::array<D3D12_STATE_SUBOBJECT, 16> subobjs;
    uint32_t index = 0;

    // -- creaet the dxil lib:
    DxilLibrary dxil_lib = create_dxil_library();
    subobjs[index++] = dxil_lib.StateSubobj; // 0 Library

    HitProgram tri_hit_prog(nullptr, TriangleChs, TriangleHitGroup);
    subobjs[index++] = tri_hit_prog.Subobject; // 1 Triangle Hit Group

    HitProgram plane_hit_prog(nullptr, PlaneChs, PlaneHitGroup);
    subobjs[index++] = plane_hit_prog.Subobject; // 2 Plane Hit Group

    HitProgram shadow_hit_prog(nullptr, ShadowChs, ShadowHitGroup);
    subobjs[index++] = shadow_hit_prog.Subobject; // 3 Shadow Hit Group

    // -- create the raygen root-sig and association (4,5):
    LocalRootSig raygen_root_sig(dev_, create_raygen_root_sig_desc().desc);
    subobjs[index] = raygen_root_sig.Subobject; // 4 raygen root sig
    uint32_t raygen_root_sig_index  = index++; // 4
    ExportAssociation raygen_root_association(&RayGenShader, 1, &(subobjs[raygen_root_sig_index]));
    subobjs[index++] = raygen_root_association.Subobject; // 5 associate root sig to raygen shader

    // -- create triangle hit-program root-sig and association (6,7):
    LocalRootSig tri_hit_root_sig(dev_, create_triangle_hit_root_sig_desc().desc);
    subobjs[index] = tri_hit_root_sig.Subobject; // 6 hit root sig
    uint32_t tri_hit_root_index = index++; // 6
    ExportAssociation tri_hit_root_assocation(&TriangleChs, 1, &(subobjs[tri_hit_root_index]));
    subobjs[index++] = tri_hit_root_assocation.Subobject; // 7 associate hit root sig to hit-group

    // -- create plane hit-program root-sig and association (8,9):
    LocalRootSig plane_hit_root_sig(dev_, create_plane_hit_root_sig_desc().desc);
    subobjs[index] = plane_hit_root_sig.Subobject; // 8 hit root sig
    uint32_t plane_hit_root_index = index++; // 8
    ExportAssociation plane_hit_root_assocation(&PlaneHitGroup, 1, &(subobjs[plane_hit_root_index]));
    subobjs[index++] = plane_hit_root_assocation.Subobject; // 9 associate hit root sig to hit-group

    // -- create the empty root-sig and associate it with primary miss-shader and shadow-programs (10,11):
    D3D12_ROOT_SIGNATURE_DESC empty_desc = {};
    empty_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    LocalRootSig empty_root_sig(dev_, empty_desc);
    subobjs[index] = empty_root_sig.Subobject; // 10 empty root sig for miss-shader and shadow-programs
    uint32_t empty_root_index = index++; // 10
    WCHAR const * empty_root_exports [] = {MissShader, ShadowChs, ShadowMiss};
    ExportAssociation empty_root_association(empty_root_exports, _countof(empty_root_exports), &subobjs[empty_root_index]);
    subobjs[index++] = empty_root_association.Subobject; // 11 Associate Root Sig to Miss-shader and Shadow-program

    // NOTE(omid): we're using one ShaderConfig sub-obj:
    /*
        although Shadow payload is a different size (4 bytes), there can only be one defined max size per State Object.
        it is completely valid to associate your shaders to multiple ShaderConfig sub-objects if their values are the same,
        but we will only use one here for simplicity
    */
    // -- bind payload size to all programs (12,13):
    ShaderConfig primary_shader_cfg(sizeof(float) * 2, sizeof(float) * 3);
    subobjs[index] = primary_shader_cfg.Subobject; // 12 shader cfg
    uint32_t primary_shader_cfg_index = index++; // 12
    WCHAR const * primary_shader_exports [] = {RayGenShader, MissShader, TriangleChs, PlaneChs, ShadowMiss, ShadowChs};
    ExportAssociation primary_cfg_assoc(primary_shader_exports, _countof(primary_shader_exports), &(subobjs[primary_shader_cfg_index]));
    subobjs[index++] = primary_cfg_assoc.Subobject; // 13 associate shader config to all shaders and hit groups

    // -- create pipeline cfg
    //PipelineConfig cfg(0); // N.B., zero maxTraceRecursionDepth assumes TracyRay intrinsic is not called at all
    PipelineConfig cfg(2); // maxTraceRecursionDepth: 1 for raygen TraceRay call, 1 for "plane_chs" TraceRay call (primary hit)
    subobjs[index++] = cfg.Subobj; // 14

    // -- create global root sig and store the empty signature
    GlobalRootSig root_sig(dev_, {});
    empty_root_sig_ = root_sig.RootSig;
    subobjs[index++] = root_sig.Subobject; // 15

    // -- create RTPSO:
    D3D12_STATE_OBJECT_DESC desc = {};
    desc.NumSubobjects = index; // 16
    desc.pSubobjects = subobjs.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    D3D_CALL(dev_->CreateStateObject(&desc, IID_PPV_ARGS(&rtpso_)));
}

// ==============================================================================================================

void MyDemo::create_shader_table () {
// NOTE(omid):
    /*
        Shader table layout:
        Entry 0     - raygen program
        Entry 1     - Miss program for primary ray
        Entry 2     - Miss program for shadow ray
        Entry 3,4   - Hit programs for triangle 0 (primary followed by shadow)
        Entry 5,6   - Hit programs for plane (primary followed by shadow)
        Entry 7,8   - Hit programs for triangle 1 (primary followed by shadow)
        Entry 9,10  - Hit programs for triangle 2 (primary followed by shadow)

        Note 1:
        All entries in the shader-table must have the same size,
        so we choose it based on the largest entry
        raygen program requires the largest size: sizeof(program identifier) + 8 bytes for a descriptor table

        Note 2:
        The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT aka 32 bytes
    */

    // -- calculate the size and create the buffer:
    shader_table_entry_size_ = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    shader_table_entry_size_ += 8; // hit shader cbuffer descrptr
    shader_table_entry_size_ = ALIGN_TO(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, shader_table_entry_size_);
    uint32_t total_entry_size = 11 * shader_table_entry_size_;

    // -- for simplicity we create the shader-table on upload heap but you would create it on default heap
    shader_table_ = create_buffer(dev_,
        total_entry_size, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, UploadHeapProps);

    // -- map the buffer
    uint8_t * data = nullptr;
    D3D_CALL(shader_table_->Map(0, nullptr, (void **)&data));

    MAKE_SMART_COM_PTR(ID3D12StateObjectProperties);
    ID3D12StateObjectPropertiesPtr rtpso_props;
    rtpso_->QueryInterface(IID_PPV_ARGS(&rtpso_props));

    // -- Entry 0: raygen program ID and descriptor data:
    memcpy(data, rtpso_props->GetShaderIdentifier(RayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint64_t heap_start = srv_uav_heap_->GetGPUDescriptorHandleForHeapStart().ptr;
    *(uint64_t *)(data + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heap_start;

    // -- Entry 1: primary ray miss program:
    memcpy(data + shader_table_entry_size_, rtpso_props->GetShaderIdentifier(MissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // -- Entry 2: shadow ray miss program:
    memcpy(data + shader_table_entry_size_ * 2, rtpso_props->GetShaderIdentifier(ShadowMiss), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // -- Entry 3: Triangle 0 primary ray hit program, (ShaderId and Cbuffer data):
    uint8_t * entry3 = data + shader_table_entry_size_ * 3;
    memcpy(entry3, rtpso_props->GetShaderIdentifier(TriangleHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint8_t * cb_desc = entry3 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // skip ShaderId to get to the location of cbuffer entry
    assert(0 == ((uint64_t)cb_desc % 8)); // root descriptor must be stored at an 8-byte aligned address
    *(D3D12_GPU_VIRTUAL_ADDRESS *)cb_desc = cbuffers_[0]->GetGPUVirtualAddress();

    // -- Entry 4: Triangle 0 shadow ray hit program, (ShaderId only):
    uint8_t * entry4 = data + shader_table_entry_size_ * 4;
    memcpy(entry4, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // -- Entry 5: primary ray, Plane hit program, (ShaderId and the TLAS SRV):
    uint8_t * entry5 = data + shader_table_entry_size_ * 5;
    memcpy(entry5, rtpso_props->GetShaderIdentifier(PlaneHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    *(uint64_t *)(entry5 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = // SRV comes directly after ShaderId
        heap_start + dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // -- Entry 6: shadow ray in plane hit program, (ShaderId only):
    uint8_t * entry6 = data + shader_table_entry_size_ * 6;
    memcpy(entry6, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // -- Entry 7: Triangle 1 primary ray hit programs, (ShaderId and Cbuffer data):
    uint8_t * entry7 = data + shader_table_entry_size_ * 7;
    memcpy(entry7, rtpso_props->GetShaderIdentifier(TriangleHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    cb_desc = entry7 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // adding ShaderId size gets us to the location of cbuffer entry
    assert(0 == ((uint64_t)cb_desc % 8)); // root descriptor must be stored at an 8-byte aligned address
    *(D3D12_GPU_VIRTUAL_ADDRESS *)cb_desc = cbuffers_[1]->GetGPUVirtualAddress();

    // -- Entry 8: Triangle 1 shadow ray hit program, (ShaderId only):
    uint8_t * entry8 = data + shader_table_entry_size_ * 8;
    memcpy(entry8, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // -- Entry 9: Triangle 2 primary ray hit programs, (ShaderId and Cbuffer data):
    uint8_t * entry9 = data + shader_table_entry_size_ * 9;
    memcpy(entry9, rtpso_props->GetShaderIdentifier(TriangleHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    cb_desc = entry9 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // adding ShaderId size gets us to the location of cbuffer entry
    assert(0 == ((uint64_t)cb_desc % 8)); // root descriptor must be stored at an 8-byte aligned address
    *(D3D12_GPU_VIRTUAL_ADDRESS *)cb_desc = cbuffers_[2]->GetGPUVirtualAddress();

    // -- Entry 10: Triangle 2 shadow ray hit program, (ShaderId only):
    uint8_t * entry10 = data + shader_table_entry_size_ * 10;
    memcpy(entry10, rtpso_props->GetShaderIdentifier(ShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

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
    rotation_ += 0.05f;

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

