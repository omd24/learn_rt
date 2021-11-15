#pragma once

#pragma warning (disable: 26495)    // not initializing struct members

#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS     // -- disable C++17 warning about deprecated old funcs

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <externals/glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <externals/glm/gtx/transform.hpp>
#include <externals/glm/gtx/euler_angles.hpp>
#include <string>
#include <d3d12.h>
#include <comdef.h>
#include <dxgi1_4.h>
#include <dxgiformat.h>
#include <fstream>
#include <externals/dxcapi/dxcapi.use.h>
#include <vector>
#include <array>

using namespace glm;

// -- common d312 #define(s)
#define MAKE_SMART_COM_PTR(_a)  _COM_SMARTPTR_TYPEDEF(_a, __uuidof(_a))
MAKE_SMART_COM_PTR(ID3D12Device5);
MAKE_SMART_COM_PTR(ID3D12GraphicsCommandList4);
MAKE_SMART_COM_PTR(ID3D12CommandQueue);
MAKE_SMART_COM_PTR(IDXGISwapChain3);
MAKE_SMART_COM_PTR(IDXGIFactory4);
MAKE_SMART_COM_PTR(IDXGIAdapter1);
MAKE_SMART_COM_PTR(ID3D12Fence);
MAKE_SMART_COM_PTR(ID3D12CommandAllocator);
MAKE_SMART_COM_PTR(ID3D12Resource);
MAKE_SMART_COM_PTR(ID3D12DescriptorHeap);
MAKE_SMART_COM_PTR(ID3D12Debug);
MAKE_SMART_COM_PTR(ID3D12StateObject);
MAKE_SMART_COM_PTR(ID3D12RootSignature);
MAKE_SMART_COM_PTR(ID3DBlob);
MAKE_SMART_COM_PTR(IDxcBlobEncoding);

// -- interface for demos
struct Demo {
    virtual ~Demo () {}
    virtual void OnLoad (HWND wnd, uint32_t w, uint32_t h) = 0; // called when demo starts
    virtual void OnFrameRender () = 0; // called each frame
    virtual void OnShutdown () = 0; // called when application terminates
};

struct Framework {
    static void Run (
        Demo & demo,
        std::string const & wndtitle,
        uint32_t w = 1280, uint32_t h = 720
    );
};

static constexpr uint32_t DefaultSwapchainBufferCount = 3;

// NOTE(omid): the (#a) inside macro tells the preprocessor to convert the parameter to a string constant.
#define D3D_CALL(a) {HRESULT hr__ = a; if (FAILED(hr__)) {D3DTraceHr(#a, hr__);}}
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define ALIGN_TO(_alignment, _val) (((_val + _alignment - 1) / _alignment) * _alignment)

// -- trace a D3D error and convert the result to a human-readable string
void D3DTraceHr (std::string const & msg, HRESULT hr);

void MsgBox (std::string const & msg);
std::wstring StrToWStr (std::string const & str);
std::string WStrToStr (std::wstring const & wstr);

// -- convert a blob to a string
template <typename BlobType>
std::string ConvertBlobToString (BlobType * blob) {
    std::vector<char> info_log(blob->GetBufferSize() + 1);
    memcpy(info_log.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    info_log[blob->GetBufferSize()] = 0;
    return std::string(info_log.data());
}

