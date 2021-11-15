#pragma once

#include <demo_framework.hpp>

struct MyDemo : public Demo {
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

    void init_dxr (HWND hwnd, uint32_t w, uint32_t h);
    uint32_t begin_frame ();
    void end_frame (uint32_t rtv_idx);
public:
    void OnLoad (HWND hwnd, uint32_t w, uint32_t h) override;
    void OnFrameRender () override;
    void OnShutdown () override;
};

