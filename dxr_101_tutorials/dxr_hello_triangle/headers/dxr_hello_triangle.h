#pragma once

#include "dx_sample.h"
#include "step_timer.h"
#include "rt_hlsl_compat.h"

namespace GlobalRootSignatureParams {
enum Value {
    OutputViewSlot = 0,
    AccelerationStructureSlot,

    COUNT_
};
}
namespace LocalRootSignatureParams {
enum Value {
    ViewportConstantSlot = 0,

    COUNT_
};
}

class DXRHelloTriangle : public DXSample {
private:
    static UINT const FrameCount = 3;

    // -- DXR attributes:
    ComPtr<ID3D12Device5> dxr_device_;
    ComPtr<ID3D12GraphicsCommandList4> dxr_cmdlist_;
    ComPtr<ID3D12StateObject> dxr_state_object_;

    // -- root signature:
    ComPtr<ID3D12RootSignature> raytracing_global_root_sig_;
    ComPtr<ID3D12RootSignature> raytracing_local_root_sig_;

    // -- descriptors:
    ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
    UINT num_descriptors_allocated_;
    UINT descriptor_size_;

    // -- raytracing scene
    RayGenCBuffer raygen_cb_;

    // -- geometry:
    using Index = UINT16;
    struct Vertex { float v1, v2, v3; };
    ComPtr<ID3D12Resource> index_buffer_;
    ComPtr<ID3D12Resource> vertex_buffer_;

    // -- acceleration structure:
    ComPtr<ID3D12Resource> acceleration_structure_;
    ComPtr<ID3D12Resource> blas_;
    ComPtr<ID3D12Resource> tlas_;

    // -- raytracing output:
    ComPtr<ID3D12Resource> raytracing_output_;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_raytracing_output_uav_;
    UINT raytracing_output_uav_descriptor_heap_index_;

    // -- shader table:
    static wchar_t const * hitgroup_name_;
    static wchar_t const * raygen_shader_name_;
    static wchar_t const * closesthit_shader_name_;
    static wchar_t const * miss_shader_name_;
    ComPtr<ID3D12Resource> miss_shadertable_;
    ComPtr<ID3D12Resource> hitgroup_shadertable_;
    ComPtr<ID3D12Resource> raygen_shadertable_;

    // -- app stats
    StepTimer timer_;
public:
    DXRHelloTriangle (UINT w, UINT h, std::wstring name);

    // -- IDeviceNotify
    virtual void OnDeviceLost () override;
    virtual void OnDeviceRestored () override;

    // -- messages
    virtual void OnInit ();
    virtual void OnUpdate ();
    virtual void OnRender ();
    virtual void OnSizeChanged (UINT w, UINT h, bool minimized);
    virtual void OnDestroy ();
    virtual IDXGISwapChain * GetSwapchain () { return device_resources_->GetSwapchain(); }

private:
    void recreate_d3d ();
    void do_raytracing ();
    void create_device_dependent_resources ();
    void create_window_size_dependent_resources ();
    void release_device_dependent_resources ();
    void release_window_size_dependent_resources ();
    void create_raytracing_interfaces ();
    void serialize_and_create_raytracing_root_sig (D3D12_ROOT_SIGNATURE_DESC & desc, ComPtr<ID3D12RootSignature> * root_sig);
    void create_root_signatures ();
    void create_local_root_sig_subobjects (CD3DX12_STATE_OBJECT_DESC * raytracing_pipeline);
    void create_raytracing_pipeline_state_object ();
    void create_descriptor_heap ();
    void create_raytracing_output_resource ();
    void build_geometry ();
    void build_acceleration_structures ();
    void build_shader_tables ();
    void update_for_size_changes (UINT w, UINT h);
    void copy_raytracing_output_to_backbuffer ();
    void calc_frame_stats ();
    UINT allocate_descriptor (D3D12_CPU_DESCRIPTOR_HANDLE * hcpu_descriptor, UINT desciptor_index_to_use = UINT_MAX);
};
