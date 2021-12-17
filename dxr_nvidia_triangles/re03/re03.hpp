#pragma once

#include "demo_framework.hpp"

using U8 = uint8_t;
using U32 = uint32_t;
using U64 = uint64_t;

class re03 : public Demo {
private:

    HWND wnd_;
    ID3D12Device5Ptr dev_;
    ID3D12CommandQueuePtr cmdque_;
    IDXGISwapChain3Ptr swc_;
    uvec2 swc_size_;
    ID3D12GraphicsCommandList4Ptr cmdlist_;
    ID3D12FencePtr fence_;
    HANDLE fence_event_;
    U64 fence_value_;

    struct {
        ID3D12CommandAllocatorPtr CmdAlloc;
        ID3D12ResourcePtr SwcBuffer;
        D3D12_CPU_DESCRIPTOR_HANDLE HCpuRtv;
    } FrameObjects[DefaultSwapchainBufferCount];

    struct HeapData {
        ID3D12DescriptorHeapPtr Heap;
        U32 UsedEntries;
    };

    HeapData rtv_heap_;
    static const U32 rtv_heap_size_ = 3;

    ID3D12ResourcePtr vb_;
    ID3D12ResourcePtr tlas_;
    ID3D12ResourcePtr blas_;
    U64 tlas_size_ = 0;
    
    ID3D12StateObjectPtr pso_;
    ID3D12RootSignaturePtr empty_root_sig_;

    ID3D12ResourcePtr sbt_;
    U32 sbt_entry_size_;

    void init_dxr (HWND wnd, U32 w, U32 h);
    U32 begin_frame ();
    void end_frame (U32 rtv_index);
    void create_ass ();
    void create_pso ();
    void create_sbt ();

public:
    void OnLoad (HWND wnd, uint32_t w, uint32_t h) override;
    void OnFrameRender () override;
    void OnShutdown () override;
};

