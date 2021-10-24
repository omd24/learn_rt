#include "stdafx.h"

#include "dxr_hello_triangle.h"
#include "dxr_helper.h"
// compiled shader...
#include "Raytracing.hlsl.h"

using namespace std;
using namespace DX;

wchar_t const * DXRHelloTriangle::hitgroup_name_ = L"MyHitGroup";
wchar_t const * DXRHelloTriangle::raygen_shader_name_ = L"MyRayGenShader";
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

void DXRHelloTriangle::OnUpdate ();
void DXRHelloTriangle::OnRender ();
void DXRHelloTriangle::OnSizeChanged (UINT w, UINT h, bool minimized);
void DXRHelloTriangle::OnDestroy ();

void DXRHelloTriangle::recreate_d3d ();
void DXRHelloTriangle::do_raytracing ();

void DXRHelloTriangle::create_window_size_dependent_resources ();
void DXRHelloTriangle::release_device_dependent_resources ();
void DXRHelloTriangle::release_window_size_dependent_resources ();

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
    D3D12_SHADER_BYTECODE libdxil = CD3D12_SHADER_BYTECODE((void *)g_pRaytracing, _countof(g_pRaytracing));
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
void DXRHelloTriangle::create_descriptor_heap ();
void DXRHelloTriangle::create_raytracing_output_resource ();
void DXRHelloTriangle::build_geometry ();
void DXRHelloTriangle::build_acceleration_structures ();
void DXRHelloTriangle::build_shader_tables ();
void DXRHelloTriangle::update_for_size_changes (UINT w, UINT h);
void DXRHelloTriangle::copy_raytracing_output_to_backbuffer ();
void DXRHelloTriangle::calc_frame_stats ();
UINT DXRHelloTriangle::allocate_descriptor (D3D12_CPU_DESCRIPTOR_HANDLE * hcpu_descriptor, UINT desciptor_index_to_use = UINT_MAX);
