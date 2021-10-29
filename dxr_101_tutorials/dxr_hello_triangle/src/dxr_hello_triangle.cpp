#include "stdafx.h"

#include "dxr_hello_triangle.h"
#include "dxr_helper.h"
// compiled shader...
#include "../compiled_shaders/raytracing.hlsl.h"

using namespace std;
using namespace DX;

// NOTE(omid): names from shader code... 
wchar_t const * DXRHelloTriangle::hitgroup_name_ = L"MyHitGroup";
wchar_t const * DXRHelloTriangle::raygen_shader_name_ = L"MyRaygenShader";
wchar_t const * DXRHelloTriangle::closesthit_shader_name_ = L"MyClosestHitShader";
wchar_t const * DXRHelloTriangle::miss_shader_name_ = L"MyMissShader";

DXRHelloTriangle::DXRHelloTriangle (UINT w, UINT h, std::wstring name) :
    DXSample(w, h, name),
    raytracing_output_uav_descriptor_heap_index_(UINT_MAX)
{
    raygen_cb_.viewport = {-1.0f, -1.0f, 1.0f, 1.0f};
    UpdateForSizeChange(w, h);
}

void DXRHelloTriangle::create_device_dependent_resources () {
    // -- init the raytracing pipeline

    // -- create raytracing interfaces: raytracing device, cmdlist
    create_raytracing_interfaces();

    // -- create root sig for shaders
    create_root_signatures();

    // -- create a raytracing PSO which defines the binding of shaders, states and resources to be used during raytracing
    create_raytracing_pipeline_state_object();

    // -- create a heap for descriptors
    create_descriptor_heap();

    // -- build geometry to be used in this sample
    build_geometry();

    // -- build raytracing acceleration structures from generated geometry
    build_acceleration_structures();

    // -- build shader tables, which define shaders and their local root sig args
    build_shader_tables();

    // -- create an output 2D texture to store raytracing result to
    create_raytracing_output_resource();

}
void DXRHelloTriangle::serialize_and_create_raytracing_root_sig (D3D12_ROOT_SIGNATURE_DESC & desc, ComPtr<ID3D12RootSignature> * root_sig) {
    auto dev = device_resources_->GetDevice();
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
        error ? static_cast<wchar_t *>(error->GetBufferPointer()) : nullptr);
    ThrowIfFailed(dev->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*root_sig))));
}
void DXRHelloTriangle::create_root_signatures () {
    // -- global root sig:
    // -- this is a root sig that is shared across all raytracing shaders involved during a DispatchRays() call
    {
        CD3DX12_DESCRIPTOR_RANGE uav_descriptor_range;
        uav_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER root_params[GlobalRootSignatureParams::COUNT_];
        root_params[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &uav_descriptor_range);
        root_params[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
        CD3DX12_ROOT_SIGNATURE_DESC global_root_sig_desc(_countof(root_params), root_params);
        serialize_and_create_raytracing_root_sig(global_root_sig_desc, &raytracing_global_root_sig_);
    }
    // -- local root sig:
    // -- this is the root sig that enables a shader to have unique arguments that come from shader tables
    {
        CD3DX12_ROOT_PARAMETER root_params[LocalRootSignatureParams::COUNT_];
        root_params[LocalRootSignatureParams::ViewportConstantSlot].InitAsConstants(SIZEOF_IN_UINT32(raygen_cb_), 0, 0);
        CD3DX12_ROOT_SIGNATURE_DESC local_root_sig_desc(_countof(root_params), root_params);
        local_root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        serialize_and_create_raytracing_root_sig(local_root_sig_desc, &raytracing_local_root_sig_);
    }
}
// -- local root sig and shader associations:
// -- remember the local root sig is a root sig that enables a shader to have unique args that come from shader tables
void DXRHelloTriangle::create_local_root_sig_subobjects (CD3DX12_STATE_OBJECT_DESC * raytracing_pipeline) {
    // -- hit group and miss shaders in this sample don't use a local root sig so there is no need for association

    // -- local root sig to be used in a raygen shader
    {
        auto local_root_sig = raytracing_pipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        local_root_sig->SetRootSignature(raytracing_local_root_sig_.Get());
        // -- the shader association:
        auto root_sig_assoc = raytracing_pipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        root_sig_assoc->SetSubobjectToAssociate(*local_root_sig);
        root_sig_assoc->AddExport(raygen_shader_name_);
    }
}
// -- IDeviceNotify
void DXRHelloTriangle::OnDeviceLost () {
    release_window_size_dependent_resources();
    release_device_dependent_resources();
}
void DXRHelloTriangle::OnDeviceRestored () {
    create_device_dependent_resources();
    create_window_size_dependent_resources();
}

// -- messages
void DXRHelloTriangle::OnInit () {
    device_resources_ = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_11_0,
        DeviceResources::RequiredTearingSupport,
        adapterid_override_
    );
    device_resources_->RegisterDeviceNotify(this);
    device_resources_->SetWindow(Win32App::GetHwnd(), width_, height_);
    device_resources_->InitDXGIAdapter();

    ThrowIfFailed(IsDXRSupported(device_resources_->GetAdapter()),
        L"ERROR: DXR is not supported by your OS, GPU and/or driver\n\n");

    device_resources_->CreateDeviceResources();
    device_resources_->CreateWindowSizeDependentResources();

    create_device_dependent_resources();
    create_window_size_dependent_resources();
}

void DXRHelloTriangle::OnUpdate () {
    timer_.Tick();
    calc_frame_stats();
}
void DXRHelloTriangle::OnRender () {
    if (!device_resources_->IsWindowVisible())
        return;
    device_resources_->Prepare();
    do_raytracing();
    copy_raytracing_output_to_backbuffer();
    device_resources_->Present(D3D12_RESOURCE_STATE_PRESENT);
}
void DXRHelloTriangle::OnSizeChanged (UINT w, UINT h, bool minimized) {
    if (!device_resources_->WindowSizeChanged(w, h, minimized))
        return;
    update_for_size_changes(w, h);
    release_window_size_dependent_resources();
    create_window_size_dependent_resources();
}
void DXRHelloTriangle::OnDestroy () {
    device_resources_->WaitForGpu();
    OnDeviceLost();
}

void DXRHelloTriangle::recreate_d3d () {
    // -- give gpu a chance to finish its execution in prograss
    try {
        device_resources_->WaitForGpu();
    } catch (HrException &) {
        // -- do nothing, currently attached adapater is unresponsive
    }
    device_resources_->HandleDeviceLost();
}
void DXRHelloTriangle::do_raytracing () {
    auto cmdlist = device_resources_->GetCmdlist();

    using DispatchDesc = D3D12_DISPATCH_RAYS_DESC;
    auto DispatchRays = [&](auto * cmdlist, auto * state_obj, DispatchDesc * dispatch_desc) {
        // -- since each shader table has only one shader record, the stride is same as size
        dispatch_desc->HitGroupTable.StartAddress = hitgroup_shadertable_->GetGPUVirtualAddress();
        dispatch_desc->HitGroupTable.SizeInBytes = hitgroup_shadertable_->GetDesc().Width;
        dispatch_desc->HitGroupTable.StrideInBytes = dispatch_desc->HitGroupTable.SizeInBytes;
        dispatch_desc->MissShaderTable.StartAddress = miss_shadertable_->GetGPUVirtualAddress();
        dispatch_desc->MissShaderTable.SizeInBytes = miss_shadertable_->GetDesc().Width;
        dispatch_desc->MissShaderTable.StrideInBytes = dispatch_desc->MissShaderTable.SizeInBytes;
        dispatch_desc->RayGenerationShaderRecord.StartAddress = raygen_shadertable_->GetGPUVirtualAddress();
        dispatch_desc->RayGenerationShaderRecord.SizeInBytes = raygen_shadertable_->GetDesc().Width;
        dispatch_desc->Width = width_;
        dispatch_desc->Height = height_;
        dispatch_desc->Depth = 1;
        cmdlist->SetPipelineState1(state_obj);
        cmdlist->DispatchRays(dispatch_desc);
    };

    cmdlist->SetComputeRootSignature(raytracing_global_root_sig_.Get());

    // -- bind the heaps and acceleration structure, and then dispatch rays
    DispatchDesc dispatch_desc = {};
    cmdlist->SetDescriptorHeaps(1, descriptor_heap_.GetAddressOf());
    cmdlist->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, hgpu_raytracing_output_uav_);
    cmdlist->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, tlas_->GetGPUVirtualAddress());

    DispatchRays(dxr_cmdlist_.Get(), dxr_state_object_.Get(), &dispatch_desc);
}

void DXRHelloTriangle::create_window_size_dependent_resources () {
    create_raytracing_output_resource();
    // -- for simplicity we just rebuild the shader tables
    build_shader_tables();
}
void DXRHelloTriangle::release_device_dependent_resources () {
    raytracing_global_root_sig_.Reset();
    raytracing_local_root_sig_.Reset();

    dxr_device_.Reset();
    dxr_cmdlist_.Reset();
    dxr_state_object_.Reset();

    descriptor_heap_.Reset();
    num_descriptors_allocated_ = 0;
    raytracing_output_uav_descriptor_heap_index_ = UINT_MAX;
    index_buffer_.Reset();
    vertex_buffer_.Reset();
}
void DXRHelloTriangle::release_window_size_dependent_resources () {
    raygen_shadertable_.Reset();
    miss_shadertable_.Reset();
    hitgroup_shadertable_.Reset();
    raytracing_output_.Reset();
}

// -- create raytracing device and cmdlist
void DXRHelloTriangle::create_raytracing_interfaces () {
    auto dev = device_resources_->GetDevice();
    auto cmdlist = device_resources_->GetCmdlist();
    ThrowIfFailed(dev->QueryInterface(IID_PPV_ARGS(&dxr_device_)), L"Couldn't get DXR interface for the device\n");
    ThrowIfFailed(cmdlist->QueryInterface(IID_PPV_ARGS(&dxr_cmdlist_)), L"Couldn't get DXR interface for the cmdlist\n");
}

// -- create raytracing pipeline state object (RTPSO)
/*
    an rtpso represents a full set of shaders reachabe by a dispach call,
    with all configuration options resolved, such local signatures, and other states
*/
void DXRHelloTriangle::create_raytracing_pipeline_state_object () {
    /*
        create 7 subobjects that combine into a rtpso:
        subobjs need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations,
        Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobj associated with it.
        This sample utilizes default shader association,
        except for the local root sig subobj which has an explicit association (specified purely for demonstration purposes)
        1 - DXIL Library
        1 - Traiangle Hit Group
        1 - Shader Config
        2 - Local Root Sig and Assoc
        1 - Global Root Sig
        1 - Pipeline Config
    */

    CD3DX12_STATE_OBJECT_DESC rtpipeline {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    // -- DXIL Library:
    // -- this contains the shaders and their entry points for the state object
    // -- since shaders are not considered a subobj, they need to be passed in via DXIL subobjs
    auto lib = rtpipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void *)g_praytracing, _countof(g_praytracing));
    lib->SetDXILLibrary(&libdxil);
    // -- define which shader exports to surface from the library
    // -- if no shader exports are defined for a dxil library subobject, all shaders will be surfaced.
    // -- in this sample this could be omitted for convenience since we use all shaders in the library
    {
        lib->DefineExport(raygen_shader_name_);
        lib->DefineExport(closesthit_shader_name_);
        lib->DefineExport(miss_shader_name_);
    }

    // -- Traiangle Hit Group
    // -- a hit group specifies closest hit, any hit, and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB
    // -- in this sample we only use triangle geometry with a closest hit shader so others are not set
    auto hitgroup = rtpipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitgroup->SetClosestHitShaderImport(closesthit_shader_name_);
    hitgroup->SetHitGroupExport(hitgroup_name_);
    hitgroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);


    // -- Shader Config:
    // -- defines max sizes in bytes for ray payload and attribute struct
    auto shader_cfg = rtpipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size = 4 * sizeof(float);  // float4 color
    UINT attrib_size = 2 * sizeof(float);   // float2 barycentrics
    shader_cfg->Config(payload_size, attrib_size);


    // -- Local Root Sig and Assoc
    // -- again, local root sig enables unique arguments from shader tables
    create_local_root_sig_subobjects(&rtpipeline);


    // -- Global Root Sig
    // -- on the contray, this is a root sig that's shared across all shaders invoked during a dispatch call
    auto global_root_sig = rtpipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_sig->SetRootSignature(raytracing_global_root_sig_.Get());


    // -- Pipeline Config
    // -- define max TracyRay() recursion depth
    auto pipecfg = rtpipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    // -- Performance Top: set max recursion depth as low as needed,
    // -- as drivers may apply optimization strategies for low recursion depths
    UINT max_recursion_depth = 1; // only primary rays
    pipecfg->Config(max_recursion_depth);

#if _DEBUG
    PrintStateObjectDesc(rtpipeline);
#endif

    // -- create the state obj
    ThrowIfFailed(dxr_device_->CreateStateObject(rtpipeline, IID_PPV_ARGS(&dxr_state_object_)),
        L"ERROR: couldn't DXR state object\n");
}
void DXRHelloTriangle::create_descriptor_heap () {
    auto dev = device_resources_->GetDevice();
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
    // -- allocate a heap for a single descriptor: 1 UAV to raytracing output texture
    descriptor_heap_desc.NumDescriptors = 1;
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptor_heap_desc.NodeMask = 0;
    dev->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap_));
    NAME_D3D12_OBJECT(descriptor_heap_);

    descriptor_size_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}
// -- allocate a descriptor and return its index:
// -- if the passed descriptor_index is valid it will be used instead of allocating a new one
UINT DXRHelloTriangle::allocate_descriptor (D3D12_CPU_DESCRIPTOR_HANDLE * hcpu_descriptor, UINT descriptor_index_to_use) {
    auto hcpu_descriptor_start = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
    if (descriptor_index_to_use >= descriptor_heap_->GetDesc().NumDescriptors) {
        descriptor_index_to_use = num_descriptors_allocated_;
        ++num_descriptors_allocated_;
    }
    *hcpu_descriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(hcpu_descriptor_start, descriptor_index_to_use, descriptor_size_);
    return descriptor_index_to_use;
}
// -- create 2D output texture for raytracing
void DXRHelloTriangle::create_raytracing_output_resource () {
    auto dev = device_resources_->GetDevice();
    auto backbuffer_format = device_resources_->GetBackbufferFormat();

    // -- create output resource. dimesions and format should match the swapchain
    auto resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(
        backbuffer_format, width_, height_, 1 /*array size*/, 1 /* miplevels*/,
        1 /*sample count*/, 0 /*sample quality*/,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );
    ThrowIfFailed(dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &resource_desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&raytracing_output_)
    ));
    NAME_D3D12_OBJECT(raytracing_output_);

    // -- create UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_uav;
    raytracing_output_uav_descriptor_heap_index_ = allocate_descriptor(&hcpu_uav, raytracing_output_uav_descriptor_heap_index_);
    dev->CreateUnorderedAccessView(raytracing_output_.Get(), nullptr, &uav_desc, hcpu_uav);
    hgpu_raytracing_output_uav_ = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        descriptor_heap_->GetGPUDescriptorHandleForHeapStart(),
        raytracing_output_uav_descriptor_heap_index_, descriptor_size_
    );
}

void DXRHelloTriangle::build_geometry () {
    auto dev = device_resources_->GetDevice();
    Index indices [] = {0, 1, 2};
    float depth_value = 1.0f;
    float offset = 0.7f;
    Vertex vertices [] = {
        // -- the sample raytraces in screen space coordinates
        // -- since DirectX screen space coordiantes are right handed (i.e., Y axis points down aka y increases going down)
        // -- so define vertices in counter clockwise order (it would be clockwise if left handed)
        {0, -offset, depth_value},
        {-offset, offset, depth_value},
        {offset, offset, depth_value},
    };
    AllocateUploadBuffer(dev, vertices, sizeof(vertices), &vertex_buffer_);
    AllocateUploadBuffer(dev, indices, sizeof(indices), &index_buffer_);
}
void DXRHelloTriangle::build_acceleration_structures () {
    auto dev = device_resources_->GetDevice();
    auto cmdlist=  device_resources_->GetCmdlist();
    auto cmdqueue = device_resources_->GetCmdqueue();
    auto cmdalloc = device_resources_->GetCmdalloc();

    // -- reset cmdlist for AS construction
    cmdlist->Reset(cmdalloc, nullptr);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Triangles.IndexBuffer = index_buffer_->GetGPUVirtualAddress();
    geometry_desc.Triangles.IndexCount = static_cast<UINT>(index_buffer_->GetDesc().Width) / sizeof(Index);
    geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometry_desc.Triangles.Transform3x4 = 0;
    geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.VertexCount = static_cast<UINT>(vertex_buffer_->GetDesc().Width) / sizeof(Vertex);
    geometry_desc.Triangles.VertexBuffer.StartAddress = vertex_buffer_->GetGPUVirtualAddress();
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

    // -- mark the geomtery as opaque
    // -- performance tip: mark geometries as opaque whenever applicable as it can be enable imprtant ray processing optimization
    // -- when rays encounter opaque geometries, any anyhit shader won't be executed whether it is present or not
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // -- get required sizes for an AS
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS build_flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS toplevel_inputs = {};
    toplevel_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    toplevel_inputs.Flags = build_flags;
    toplevel_inputs.NumDescs = 1;   // one BLAS instance
    toplevel_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO toplevel_prebuild_info = {};
    dxr_device_->GetRaytracingAccelerationStructurePrebuildInfo(&toplevel_inputs, &toplevel_prebuild_info);
    ThrowIfFailed(toplevel_prebuild_info.ResultDataMaxSizeInBytes > 0);

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomlevel_prebuild_info = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomlevel_inputs = toplevel_inputs;
    bottomlevel_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomlevel_inputs.pGeometryDescs = &geometry_desc;
    dxr_device_->GetRaytracingAccelerationStructurePrebuildInfo(&bottomlevel_inputs, &bottomlevel_prebuild_info);
    ThrowIfFailed(bottomlevel_prebuild_info.ResultDataMaxSizeInBytes > 0);

    ComPtr<ID3D12Resource> scratch_resource;
    AllocateUAVBuffer(
        dev, max(toplevel_prebuild_info.ScratchDataSizeInBytes, bottomlevel_prebuild_info.ScratchDataSizeInBytes),
        &scratch_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        L"ScratchResource"
    );

    // -- allocate resources for acceleration structures:
    // -- acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent)
    // -- default heap means "device local" aka application doesn't need cpu read/write to it,
    // -- so default heaps are OK
    // -- resources that will contain AS must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    // -- and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. N.B., the ALLOW_UNORDERED_ACCESS simply acknowledges both:
    // -- two reasons the UAV access is important:
    // -- (1) sys will be doing this type of access in its implementation of AS builds behind the scene
    // -- (2) from the app pov, synchronization of writes/reads to AS is accomplished through UAV barriers
    {
        D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        AllocateUAVBuffer(dev, bottomlevel_prebuild_info.ResultDataMaxSizeInBytes, &blas_, init_state, L"BLAS");
        AllocateUAVBuffer(dev, toplevel_prebuild_info.ResultDataMaxSizeInBytes, &tlas_, init_state, L"TLAS");
    }

    // -- create an instance desc for the BLAS
    ComPtr<ID3D12Resource> instance_descs;
    D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {};
    instance_desc.Transform[0][0] = instance_desc.Transform[1][1] = instance_desc.Transform[2][2] = 1;
    instance_desc.InstanceMask = 1;
    instance_desc.AccelerationStructure = blas_->GetGPUVirtualAddress();
    AllocateUploadBuffer(dev, &instance_desc, sizeof(instance_desc), &instance_descs, L"InstanceDescs");

    // -- BLAS build desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build_desc = {};
    {
        blas_build_desc.Inputs = bottomlevel_inputs;
        blas_build_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
        blas_build_desc.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    }
    // -- TLAS build desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc = {};
    {
        toplevel_inputs.InstanceDescs = instance_descs->GetGPUVirtualAddress();
        tlas_build_desc.Inputs = toplevel_inputs;
        tlas_build_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
        tlas_build_desc.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    }

    auto BuildAccelerationStructure = [&](auto * raytracing_cmdlist) {
        raytracing_cmdlist->BuildRaytracingAccelerationStructure(&blas_build_desc, 0, nullptr);
        cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(blas_.Get()));
        raytracing_cmdlist->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
    };

    // -- build AS:
    BuildAccelerationStructure(dxr_cmdlist_.Get());

    // -- kick off AS construction:
    device_resources_->ExecuteCmdlist();

    // -- N.B., wait for gpu to finish as the locally created temporary gpu resources will get released once out of scope
    device_resources_->WaitForGpu();
}
// -- shader table encapsulates all shader records which means shaders and arguments for their local root sig
void DXRHelloTriangle::build_shader_tables () {
    auto dev = device_resources_->GetDevice();

    void * raygen_shader_id;
    void * miss_shader_id;
    void * hitgroup_shader_id;

    auto GetShaderIds = [&](auto * state_obj_properties) {
        raygen_shader_id = state_obj_properties->GetShaderIdentifier(raygen_shader_name_);
        miss_shader_id = state_obj_properties->GetShaderIdentifier(miss_shader_name_);
        hitgroup_shader_id = state_obj_properties->GetShaderIdentifier(hitgroup_name_);
    };
    // -- get shader ids:
    UINT shader_id_size;
    {
        ComPtr<ID3D12StateObjectProperties> state_obj_properties;
        ThrowIfFailed(dxr_state_object_.As(&state_obj_properties));
        GetShaderIds(state_obj_properties.Get());
        shader_id_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }
    // -- raygen shader table
    {
        struct RootArguments {
            RayGenCBuffer cb;
        } root_args;
        root_args.cb = raygen_cb_;

        UINT num_shader_records = 1;
        UINT shader_record_size = shader_id_size + sizeof(root_args);
        ShaderTable raygen_shadertable(dev, num_shader_records, shader_record_size, L"RayGenShaderTable");
        raygen_shadertable.PushBack(ShaderRecord(raygen_shader_id, shader_id_size, &root_args, sizeof(root_args)));
        raygen_shadertable_ = raygen_shadertable.GetResource();
    }
    // -- miss shader table
    {
        UINT num_shader_records = 1;
        UINT shader_record_size = shader_id_size;
        ShaderTable miss_shadertable(dev, num_shader_records, shader_record_size, L"MissShaderTable");
        miss_shadertable.PushBack(ShaderRecord(miss_shader_id, shader_id_size));
        miss_shadertable_ = miss_shadertable.GetResource();
    }
    // -- hit group shader table
    {
        UINT num_shader_records = 1;
        UINT shader_record_size = shader_id_size;
        ShaderTable hitgroup_shadertable(dev, num_shader_records, shader_record_size, L"HitGroupShaderTable");
        hitgroup_shadertable.PushBack(ShaderRecord(hitgroup_shader_id, shader_id_size));
        hitgroup_shadertable_ = hitgroup_shadertable.GetResource();
    }
}
// -- update app state with new resolution
void DXRHelloTriangle::update_for_size_changes (UINT w, UINT h) {
    DXSample::UpdateForSizeChange(w, h);
    float border = 0.1f;
    if (width_ <= height_) {
        raygen_cb_.stencil = {
            -1.0f + border, -1.0f + border * aspect_ratio_,
            1.0f - border, 1.0f - border * aspect_ratio_
        };
    } else {
        raygen_cb_.stencil = {
            -1.0f + border / aspect_ratio_, -1.0f + border,
            1.0f - border / aspect_ratio_, 1.0f - border
        };
    }
}
void DXRHelloTriangle::copy_raytracing_output_to_backbuffer () {
    auto cmdlist = device_resources_->GetCmdlist();
    auto render_target = device_resources_->GetRenderTarget();

    // -- Yay! using more than one barrier finally :)
    D3D12_RESOURCE_BARRIER precopy_barriers[2];
    precopy_barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    precopy_barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(raytracing_output_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdlist->ResourceBarrier(_countof(precopy_barriers), precopy_barriers);

    cmdlist->CopyResource(render_target, raytracing_output_.Get());

    D3D12_RESOURCE_BARRIER postcopy_barriers[2];
    postcopy_barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(render_target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postcopy_barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(raytracing_output_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdlist->ResourceBarrier(_countof(postcopy_barriers), postcopy_barriers);
}
// -- compute average fps and million rays per second
void DXRHelloTriangle::calc_frame_stats () {
    static int frame_cnt = 0;
    static double elapsed_time = 0.0f;
    double total_time = timer_.GetTotalSeconds();
    ++frame_cnt;
    // -- compute averages over one second period
    if ((total_time - elapsed_time) >= 1.0f) {
        float diff = static_cast<float>(total_time - elapsed_time);
        float fps = static_cast<float>(frame_cnt) / diff; // normalize to an exact second

        frame_cnt = 0;
        elapsed_time = total_time;

        float million_rays_per_sec = (width_ * height_ * fps) / static_cast<float>(1e6);

        wstringstream window_text;
        window_text << setprecision(2) << fixed
            << L"   fps: " << fps << L"   ~Million Primary Rays per Second: " << million_rays_per_sec
            << L"   gpu[" << device_resources_->GetAdapterId() << L"] " << device_resources_->GetAdapaterDescription();
        SetCustomWindowText(window_text.str().c_str());
    }
}

