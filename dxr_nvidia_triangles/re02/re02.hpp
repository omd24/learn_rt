#pragma once

#include <demo_framework.hpp>

struct MyDemo : public Demo {
public:
    struct AccelerationStructureBuffers {
        ID3D12ResourcePtr Scratch;
        ID3D12ResourcePtr Result;
        ID3D12ResourcePtr InstanceDesc; // only for Top-Level AS
    };
private:
    ID3D12Device5Ptr dev_;
    ID3D12CommandQueuePtr cmdque_;
    IDXGISwapChain3Ptr swapchain_;
    uvec2 swapchain_size_;  // width and height
    ID3D12GraphicsCommandList4Ptr cmdlist_;
    ID3D12FencePtr fence_;
    HANDLE fence_event_;
    uint64_t fence_value_ = 0;

    HWND hwnd_ = nullptr;

    ID3D12ResourcePtr vertex_buffers_[2]; // 2 geometries, aka 2 meshes (triangle, plane)
    ID3D12ResourcePtr bottom_level_as_[2]; // 2 collections of geometries (a single triangle, both a triangle and a plane)
    AccelerationStructureBuffers top_level_buffers_; // we require all buffers (Scratch, Result, InstDesc) for the refit
    uint64_t tlas_size_ = 0;

    float rotation_ = 0;

    struct {
        ID3D12CommandAllocatorPtr cmdalloc_;
        ID3D12ResourcePtr swapchain_buffer_;
        D3D12_CPU_DESCRIPTOR_HANDLE hcpu_rtv_;
    } FrameObjects_[DefaultSwapchainBufferCount];

    struct HeapData {
        ID3D12DescriptorHeapPtr heap_;
        uint32_t used_entries_ = 0;
    };
    HeapData rtv_heap_;
    static constexpr uint32_t RtvHeapSize = 3; // swapchain buffer count?

    ID3D12StateObjectPtr rtpso_; // raytracing PSO is completely different than graphics and compute PSOs
    ID3D12RootSignaturePtr empty_root_sig_;

    ID3D12ResourcePtr shader_table_;
    uint32_t shader_table_entry_size_ = 0;

    ID3D12ResourcePtr output_;
    ID3D12DescriptorHeapPtr srv_uav_heap_;
    static uint32_t const srv_uav_heap_size_ = 2;

    ID3D12ResourcePtr cbuffers_[3];

    void init_dxr (HWND hwnd, uint32_t w, uint32_t h);
    uint32_t begin_frame ();
    void end_frame (uint32_t rtv_idx);
    void create_acceleration_structures ();
    void create_rtpso (); // create RTPSO which is completely different than graphics and compute PSOs
    void create_shader_table ();
    void create_shader_resources ();
    void create_cbuffers ();

public:

    void OnLoad (HWND hwnd, uint32_t w, uint32_t h) override;
    void OnFrameRender () override;
    void OnShutdown () override;
};

