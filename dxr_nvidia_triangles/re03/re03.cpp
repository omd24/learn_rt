#include "re03.hpp"

static dxc::DxcDllSupport g_dxc_dll_helper;
MAKE_SMART_COM_PTR(IDxcCompiler);
MAKE_SMART_COM_PTR(IDxcLibrary);
MAKE_SMART_COM_PTR(IDxcBlobEncoding);
MAKE_SMART_COM_PTR(IDxcOperationResult);

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

#pragma region AS Creation:

static D3D12_HEAP_PROPERTIES const UploadHeapProps = {
    .Type = D3D12_HEAP_TYPE_UPLOAD,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask = 0,
    .VisibleNodeMask = 0
};
static D3D12_HEAP_PROPERTIES const DefaultHeapProps = {
    .Type = D3D12_HEAP_TYPE_DEFAULT,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask = 0,
    .VisibleNodeMask = 0
};

static ID3D12ResourcePtr
create_buffer (
    ID3D12Device5Ptr dev, U64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES init_state, D3D12_HEAP_PROPERTIES const & prop
) {
    D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = flags
    };
    ID3D12ResourcePtr ret;
    D3D_CALL(dev->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, init_state, nullptr, IID_PPV_ARGS(&ret)));
    return ret;
}
static ID3D12ResourcePtr
create_triangle_vb (ID3D12Device5Ptr dev) {
    vec3 const vertices [] = {
        vec3(0, 1, 0),
        vec3(0.87f, -.5f, 0),
        vec3(-0.87f, -.5f, 0)
    };
    ID3D12ResourcePtr ret =
        create_buffer(dev, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, UploadHeapProps);
    U8 * data;
    ret->Map(0, nullptr, (void **)&data);
    memcpy(data, vertices, sizeof(vertices));
    ret->Unmap(0, nullptr);
    return ret;
}

struct AsBuffers {
    ID3D12ResourcePtr Scratch;
    ID3D12ResourcePtr Result;
    ID3D12ResourcePtr InstanceDesc;
};

AsBuffers create_blas (ID3D12Device5Ptr dev, ID3D12GraphicsCommandList4Ptr cmdlist, ID3D12ResourcePtr vb) {
    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc = {
        .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
        .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
        .Triangles = {
            .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
            .VertexCount = 3,
            .VertexBuffer = {.StartAddress = vb->GetGPUVirtualAddress(), .StrideInBytes = sizeof(vec3)},
        }
    };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
        .NumDescs = 1,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
        .pGeometryDescs = &geom_desc
    };

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    AsBuffers ret = {
    .Scratch = create_buffer(dev, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, DefaultHeapProps),
    .Result = create_buffer(dev, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, DefaultHeapProps)
    };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_desc = {
        .DestAccelerationStructureData = ret.Result->GetGPUVirtualAddress(),
        .Inputs = inputs,
        .ScratchAccelerationStructureData = ret.Scratch->GetGPUVirtualAddress()
    };
    cmdlist->BuildRaytracingAccelerationStructure(&as_desc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uav = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
        .UAV = {ret.Result}
    };
    cmdlist->ResourceBarrier(1, &uav);

    return ret;
}

AsBuffers create_tlas (ID3D12Device5Ptr dev, ID3D12GraphicsCommandList4Ptr cmdlist, ID3D12ResourcePtr blas, U64 & size) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
        .NumDescs = 1,
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
    };

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    AsBuffers ret = {
    .Scratch = create_buffer(dev, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, DefaultHeapProps),
    .Result = create_buffer(dev, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, DefaultHeapProps)
    };

    size = info.ResultDataMaxSizeInBytes;

    ret.InstanceDesc = create_buffer(dev, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, UploadHeapProps);
    D3D12_RAYTRACING_INSTANCE_DESC * instance_desc;
    ret.InstanceDesc->Map(0, nullptr, (void **)&instance_desc);

    instance_desc->InstanceID = 0;
    instance_desc->InstanceContributionToHitGroupIndex = 0;
    instance_desc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    mat4 m = mat4();
    memcpy(instance_desc->Transform, &m, sizeof(instance_desc->Transform));
    instance_desc->AccelerationStructure = blas->GetGPUVirtualAddress();
    instance_desc->InstanceMask = 0xff;

    ret.InstanceDesc->Unmap(0, nullptr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_desc = {
        .DestAccelerationStructureData = ret.Result->GetGPUVirtualAddress(),
        .Inputs = inputs,
        .ScratchAccelerationStructureData = ret.Scratch->GetGPUVirtualAddress()
    };
    as_desc.Inputs.InstanceDescs = ret.InstanceDesc->GetGPUVirtualAddress();
    cmdlist->BuildRaytracingAccelerationStructure(&as_desc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uav = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
        .UAV = {ret.Result}
    };
    cmdlist->ResourceBarrier(1, &uav);

    return ret;
}

#pragma endregion AS Creation

#pragma region PSO Creation:

static ID3DBlobPtr
comiple_shader (WCHAR const * filename, WCHAR const * target) {
    D3D_CALL(g_dxc_dll_helper.Initialize());
    IDxcCompilerPtr compiler;
    IDxcLibraryPtr library;
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcCompiler, &compiler));
    D3D_CALL(g_dxc_dll_helper.CreateInstance(CLSID_DxcLibrary, &library));

    std::ifstream shader_file(filename);
    if (false == shader_file.good()) {
        MsgBox("cant open shader file");
        return nullptr;
    }

    std::stringstream ss;
    ss << shader_file.rdbuf();
    std::string shader = ss.str();

    IDxcBlobEncodingPtr text_blob;
    D3D_CALL(library->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &text_blob));

    IDxcOperationResultPtr result;
    D3D_CALL(compiler->Compile(text_blob, filename, L"", target, nullptr, 0, nullptr, 0, nullptr, &result));

    HRESULT hr;
    D3D_CALL(result->GetStatus(&hr));
    if (FAILED(hr)) {
        IDxcBlobEncodingPtr err;
        D3D_CALL(result->GetErrorBuffer(&err));
        std::string log = ConvertBlobToString(err.GetInterfacePtr());
        MsgBox("shader compile error:\n" + log);
        return nullptr;
    }

    MAKE_SMART_COM_PTR(IDxcBlob);
    IDxcBlobPtr blob;
    D3D_CALL(result->GetResult(&blob));
    return blob;
}

static ID3D12RootSignaturePtr
create_root_sig (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
    ID3DBlobPtr sig;
    ID3DBlobPtr err;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        MsgBox(ConvertBlobToString(err.GetInterfacePtr()));
        return nullptr;
    }
    ID3D12RootSignaturePtr ret;
    D3D_CALL(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&ret)));
    return ret;
}

struct SigDesc {
    D3D12_ROOT_SIGNATURE_DESC Desc = {};
    std::vector<D3D12_DESCRIPTOR_RANGE> Ranges;
    std::vector<D3D12_ROOT_PARAMETER> RootParams;
};
// -- create ray generation shader - local root signature:
SigDesc create_rgs_lrs_desc () {
    SigDesc ret = {};
    ret.Ranges.resize(2);

    ret.Ranges[0].BaseShaderRegister = 0;
    ret.Ranges[0].NumDescriptors = 1;
    ret.Ranges[0].RegisterSpace = 0;
    ret.Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ret.Ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ret.Ranges[1].BaseShaderRegister = 0;
    ret.Ranges[1].NumDescriptors = 1;
    ret.Ranges[1].RegisterSpace = 0;
    ret.Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ret.Ranges[1].OffsetInDescriptorsFromTableStart = 1;

    ret.RootParams.resize(1);
    ret.RootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ret.RootParams[0].DescriptorTable.NumDescriptorRanges = 2;
    ret.RootParams[0].DescriptorTable.pDescriptorRanges = ret.Ranges.data();

    ret.Desc.NumParameters = 1;
    ret.Desc.pParameters = ret.RootParams.data();
    ret.Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return ret;
}

struct DxilLibSSo {
    D3D12_DXIL_LIBRARY_DESC DxilLibDesc = {};
    D3D12_STATE_SUBOBJECT SSo = {};
    ID3DBlobPtr ShaderBlob;
    std::vector<D3D12_EXPORT_DESC> ExportDesc;
    std::vector<std::wstring> ExportNames;

    DxilLibSSo (ID3DBlobPtr blob, WCHAR const * entry_points [], U32 count) : ShaderBlob(blob) {
        SSo.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        SSo.pDesc = &DxilLibDesc;
        DxilLibDesc = {};
        ExportDesc.resize(count);
        ExportNames.resize(count);
        if (blob) {
            DxilLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
            DxilLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
            DxilLibDesc.NumExports = count;
            DxilLibDesc.pExports = ExportDesc.data();
            for (U32 i = 0; i < count; ++i) {
                ExportNames[i] = entry_points[i];
                ExportDesc[i].Name = ExportNames[i].c_str();
                ExportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
                ExportDesc[i].ExportToRename = nullptr;
            }
        }
    }
};

static const WCHAR * RGS = L"RGS";
static const WCHAR * MissShader = L"Miss";
static const WCHAR * CHS = L"CHS";
static const WCHAR * HitGroup = L"HitGroup";

static DxilLibSSo
create_dxil_lib_sso () {
    ID3DBlobPtr blob = comiple_shader(L"re03.hlsl", L"lib_6_3");
    WCHAR const * entrypoints [] = {RGS, MissShader, CHS};
    return DxilLibSSo(blob, entrypoints, _countof(entrypoints));
}

struct HitProgramSSo {
    std::wstring ExportName;
    D3D12_HIT_GROUP_DESC Desc;
    D3D12_STATE_SUBOBJECT SSo;

    HitProgramSSo (LPCWSTR ahs, LPCWSTR chs, std::wstring const & name) : ExportName(name) {
        Desc = {
            .HitGroupExport = ExportName.c_str(),
            .AnyHitShaderImport = ahs,
            .ClosestHitShaderImport = chs,
        };

        SSo = {
            .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP,
            .pDesc = &Desc
        };
    }
};

struct ExportAssocSSo {
    D3D12_STATE_SUBOBJECT SSo = {};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION Assoc = {};

    ExportAssocSSo (WCHAR const * exportnames [], U32 count, D3D12_STATE_SUBOBJECT const * sso_to_associate) {
        Assoc = {
            .pSubobjectToAssociate = sso_to_associate,
            .NumExports = count,
            .pExports = exportnames,
        };
        SSo = {
            .Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION,
            .pDesc = &Assoc
        };
    }
};

struct LrsSSo {
    ID3D12RootSignaturePtr RootSig;
    ID3D12RootSignature * IPtr = nullptr;
    D3D12_STATE_SUBOBJECT SSo;
    LrsSSo (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
        RootSig = create_root_sig(dev, desc);
        IPtr = RootSig.GetInterfacePtr();
        SSo = {
            .Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE,
            .pDesc = &IPtr
        };
    }
};

struct GrsSSo {
    ID3D12RootSignaturePtr RootSig;
    ID3D12RootSignature * IPtr = nullptr;
    D3D12_STATE_SUBOBJECT SSo;
    GrsSSo (ID3D12Device5Ptr dev, D3D12_ROOT_SIGNATURE_DESC const & desc) {
        RootSig = create_root_sig(dev, desc);
        IPtr = RootSig.GetInterfacePtr();
        SSo = {
            .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
            .pDesc = &IPtr
        };
    }
};

struct ShaderCfgSSo {
    D3D12_RAYTRACING_SHADER_CONFIG Cfg = {};
    D3D12_STATE_SUBOBJECT SSo = {};
    ShaderCfgSSo (U32 max_attrib_size, U32 max_payload_size) {
        Cfg = {
            .MaxPayloadSizeInBytes = max_payload_size,
            .MaxAttributeSizeInBytes = max_attrib_size
        };
        SSo = {
            .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
            .pDesc = &Cfg
        };
    }
};

struct PipelineCfgSSo {
    D3D12_RAYTRACING_PIPELINE_CONFIG Cfg = {};
    D3D12_STATE_SUBOBJECT SSo = {};
    PipelineCfgSSo (U32 max_trace_recursion_depth) {
        Cfg.MaxTraceRecursionDepth = max_trace_recursion_depth;
        SSo = {
            .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
            .pDesc = &Cfg
        };
    }
};


#pragma endregion PSO Creation

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

void re03::create_ass () {
    vb_ = create_triangle_vb(dev_);
    AsBuffers blas_buffers = create_blas(dev_, cmdlist_, vb_);
    AsBuffers tlas_buffers = create_tlas(dev_, cmdlist_, blas_buffers.Result, tlas_size_);

    fence_value_ = submit_cmdlist(cmdlist_, cmdque_, fence_, fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
    cmdlist_->Reset(FrameObjects[0].CmdAlloc, nullptr);

    tlas_ = tlas_buffers.Result;
    blas_ = blas_buffers.Result;
}

void re03::create_pso () {
    D3D12_STATE_SUBOBJECT ssos[10];
    U32 index = 0;

    DxilLibSSo dxil = create_dxil_lib_sso();
    ssos[index++] = dxil.SSo;

    HitProgramSSo hit(nullptr, CHS, HitGroup);
    ssos[index++] = hit.SSo;

    LrsSSo rgs_lrs(dev_, create_rgs_lrs_desc().Desc);
    ssos[index] = rgs_lrs.SSo;
    U32 rgs_lrs_index = index++;
    ExportAssocSSo rgs_lrs_assoc(&RGS, 1, &ssos[rgs_lrs_index]);
    ssos[index++] = rgs_lrs_assoc.SSo;

    D3D12_ROOT_SIGNATURE_DESC empty_desc = {.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE};
    LrsSSo hit_miss_lrs(dev_, empty_desc);
    ssos[index++] = hit_miss_lrs.SSo;
    WCHAR const * miss_hit_exports [] = {MissShader, CHS};
    ExportAssocSSo hit_miss_lrs_assoc(miss_hit_exports, _countof(miss_hit_exports), &hit_miss_lrs.SSo);
    ssos[index++] = hit_miss_lrs_assoc.SSo;

    ShaderCfgSSo shader_cfg(sizeof(float) * 2, sizeof(float) * 1);
    ssos[index++] = shader_cfg.SSo;
    WCHAR const * shader_exports[] = {MissShader, CHS, RGS};
    ExportAssocSSo shader_assoc(shader_exports, _countof(shader_exports), &shader_cfg.SSo);
    ssos[index++] = shader_assoc.SSo;

    PipelineCfgSSo cfg(0);
    ssos[index++] = cfg.SSo;

    GrsSSo grs(dev_, {});
    ssos[index++] = grs.SSo;

    empty_root_sig_ = grs.RootSig;

    // -- create pso:
    D3D12_STATE_OBJECT_DESC desc = {
        .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
        .NumSubobjects = _countof(ssos),
        .pSubobjects = ssos
    };
    D3D_CALL(dev_->CreateStateObject(&desc, IID_PPV_ARGS(&pso_)));
}

// =============================================================================================================================================

void re03::OnLoad (HWND wnd, uint32_t w, uint32_t h) {
    init_dxr(wnd, w, h);
    create_ass();
    create_pso();
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

