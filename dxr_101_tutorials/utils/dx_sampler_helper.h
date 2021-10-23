#pragma once

using Microsoft::WRL::ComPtr;

class HrException : public std::runtime_error {
    inline std::string HrToString (HRESULT hr) {
        char str[64] = {};
        sprintf_s(str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
        return std::string(str);
    }
private:
    HRESULT const hr_;
public:
    HrException (HRESULT hr) : std::runtime_error(HrToString(hr)), hr_(hr) {}
    HRESULT Error () const { return hr_; }
};

#define SAFE_RELEASE(p) if (p) (p)->Release()

inline void ThrowIfFailed (HRESULT hr) {
    if (FAILED(hr))
        throw HrException(hr);
}
inline void ThrowIfFailed (HRESULT hr, wchar_t const * msg) {
    if (FAILED(hr)) {
        OutputDebugString(msg);
        throw HrException(hr);
    }
}
inline void ThrowIfFailed (bool value, wchar_t const * msg) {
    ThrowIfFailed(value ? S_OK : E_FAIL, msg);
}
// -- the annotation _Out_writes_ specifies we have a buffer allocated with the given size
inline void GetAssetsPath (_Out_writes_(path_size) WCHAR * path, UINT path_size) {
    if (path == nullptr)
        throw std::exception();
    DWORD size = GetModuleFileName(nullptr, path, path_size);
    if (0 == size || size == path_size)
        throw std::exception();

    WCHAR * last_slash = wcsrchr(path, L'\\');
    if (last_slash)
        *(last_slash + 1) = L'\0';
}
inline HRESULT ReadDataFromFile (LPCWSTR filename, BYTE ** data, UINT * size) {
    using namespace Microsoft::WRL;

    CREATEFILE2_EXTENDED_PARAMETERS extended_params = {};
    extended_params.dwSize = sizeof(extended_params);
    extended_params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    extended_params.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
    extended_params.dwSecurityQosFlags = SECURITY_ANONYMOUS;
    extended_params.lpSecurityAttributes = nullptr;
    extended_params.hTemplateFile = nullptr;

    Wrappers::FileHandle file(CreateFile2(filename, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &extended_params));
    if (file.Get() == INVALID_HANDLE_VALUE)
        throw std::exception();

    FILE_STANDARD_INFO file_info = {};
    if (!GetFileInformationByHandleEx(file.Get(), FileStandardInfo, &file_info, sizeof(file_info)))
        throw std::exception();

    if (file_info.EndOfFile.HighPart != 0)
        throw std::exception();

    *data = reinterpret_cast<byte *>(malloc(file_info.EndOfFile.LowPart));
    *size = file_info.EndOfFile.LowPart;

    if (!ReadFile(file.Get(), *data, file_info.EndOfFile.LowPart, nullptr, nullptr))
        throw std::exception();

    return S_OK;
}
//
// -- assign name to objs to aid with the debugging
#if defined(_DEBUG) || defined(DBG)
inline void SetName (ID3D12Object * obj, LPWSTR name) {
    obj->SetName(name);
}
inline void SetNameIndexed (ID3D12Object * obj, LPWSTR name, UINT index) {
    WCHAR fullname[50] = {};
    if (swprintf_s(fullname, L"%s[%u]", name, index) > 0)
        obj->SetName(fullname);
}
#else
inline void SetName (ID3D12Object * obj, LPWSTR name) {
}
inline void SetNameIndexed (ID3D12Object * obj, LPWSTR name, UINT index) {
}
#endif
// -- naming helpers for ComPtr<T>
#define NAME_D3D12_OBJECT(x) SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n].Get(), L#x, n)

inline UINT Align (UINT size, UINT alignment) {
    return (size + (alignment - 1)) & ~(alignment - 1);
}
inline UINT CalcCBufferByteSize (UINT byte_size) {
    return Align(byte_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
}
//
// -- standard compile i.e., <d3dcompiler.h> header
#ifdef D3D_COMPILE_STANDARD_FILE_INCLUDE
inline ComPtr<ID3DBlob> CompileShader (
    std::wstring const & filename,
    D3D_SHADER_MACRO const * defines,
    std::string const & entry_point,
    std::string & target
) {
    UINT compile_flags = 0;
#if defined(_DEBUG) || defined(DBG)
    compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr;
    ComPtr<ID3DBlob> byte_code = nullptr;
    ComPtr<ID3DBlob> errors;
    hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry_point.c_str(), target.c_str(), compile_flags, 0, &byte_code, &errors);

    if (errors != nullptr)
        OutputDebugStringA((char *)errors->GetBufferPointer());

    ThrowIfFailed(hr);

    return byte_code;
}
#endif

template <class T>
void ResetComPtrArray (T * comptr_arr) {
    for (auto & i : *comptr_arr)
        i.Reset();
}
template <class T>
void ResetUniquePtrArray (T * uniqueptr_arr) {
    for (auto & i : uniqueptr_arr)
        i.reset();
}

// =====================================================================================

class GpuUploadBuffer {
protected:
    ComPtr<ID3D12Resource> resource_;

    GpuUploadBuffer () {}
    ~GpuUploadBuffer () {
        if (resource_.Get())
            resource_->Unmap(0, nullptr);
    }

    void Allocate (ID3D12Device * dev, UINT buffer_size, LPCWSTR resource_name = nullptr) {
        ThrowIfFailed(dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(buffer_size),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource_)
        ));
        resource_->SetName(resource_name);
    }

    uint8_t * MapCpuWriteOnly () {
        uint8_t * mapped_data;
        CD3DX12_RANGE read_range(0, 0); // we do not intend to read from "upload" buffer
        ThrowIfFailed(resource_->Map(0, &read_range, reinterpret_cast<void **>(&mapped_data)));
        return mapped_data;
    }

public:
    ComPtr<ID3D12Resource> GetResource () { return resource_; }
};

struct D3DBuffer {
    ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE hcpu_;
    D3D12_GPU_DESCRIPTOR_HANDLE hgpu_;
};

template <class T>
class ConstantBuffer : public GpuUploadBuffer {
    uint8_t * mapped_constant_data_;
    UINT aligned_instance_size_;
    UINT num_instances_;
public:
    // -- accessors:
    T Staging;
    T * operator->() { return &Staging; }
    UINT NumInstances () { return num_instances_; }
    D3D12_GPU_VIRTUAL_ADDRESS GetVirturalAddress (UINT instance_index = 0) {
        return resource_->GetGPUVirtualAddress() + instance_index * aligned_instance_size_;
    }

    // -- main methods
    ConstantBuffer () : aligned_instance_size_(0), num_instances_(0), mapped_constant_data_(nullptr) {}

    void Create (ID3D12Device * dev, UINT num_insts = 1, LPCWSTR resource_name = nullptr) {
        num_instances_ = num_insts;
        UINT aligned_size = Align(sizeof(T), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        UINT buffer_size = num_instances_ * aligned_size;
        Allocate(dev, buffer_size, resource_name);
        mapped_constant_data_ = MapCpuWriteOnly();
    }
    void CopyStagingToGpu (UINT instance_index = 0) {
        memcpy(mapped_constant_data_ + instance_index * aligned_instance_size_, &Staging, sizeof(Staging));
    }
};

template <class T>
class StructuredBuffer : public GpuUploadBuffer {
    T * mapped_buffers;
    std::vector<T> staging_;
public:
    // -- accessors:
    T & operator[](UINT elem_index) { return staging_[elem_index]; }
    size_t NumElementsPerInstance () { return staging_.size(); }
    //UINT NumInstances () { return staging_.size(); }
    size_t InstanceSize () { return NumElementsPerInstance() * sizeof(T); }
    D3D12_GPU_VIRTUAL_ADDRESS GpuVirtualAddress (UINT instance_index = 0) {
        return resource_->GetGPUVirtualAddress() + instance_index * InstanceSize();
    }

    // -- performance tip: align structures on sizeof(float4) i.e. 16 bytes boundary
    // -- ref: https://developer.nvidia.com/content/understanding-structured-buffer-performance
    static_assert(sizeof(T) % 16 == 0, L"Align structure buffers on 16-byte boundary for performance reasons");

    StructuredBuffer () : mapped_buffers(nullptr), num_instances_(0) {}

    void Create (ID3D12Device * dev, UINT num_elems, UINT num_insts = 1, LPCWSTR resource_name = nullptr) {
        staging_.resize(num_elems);
        UINT buffer_size = num_insts * num_elems * sizeof(T);
        Allocate(dev, buffer_size, resource_name);
        mapped_buffers = reinterpret_cast<T *>(MapCpuWriteOnly());
    }
    void CopyStagingToGpu (UINT instance_index = 0) {
        memcpy(mapped_buffers + instance_index, &staging_[0], InstanceSize());
    }

};

