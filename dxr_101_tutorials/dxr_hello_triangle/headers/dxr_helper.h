#pragma once

#define SIZEOF_IN_ALIGNMENT(obj, alignment) ((sizeof(obj) - 1) / alignment + 1)
#define SIZEOF_IN_UINT32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

struct AccelerationStructureBuffers {
    ComPtr<ID3D12Resource>  Scratch;
    ComPtr<ID3D12Resource>  AccelerationStructure;
    ComPtr<ID3D12Resource>  InstanceDesc;   // used only for top-level AS
    UINT64                  ResultDataMaxSizeInBytes;
};

// -- ShaderRecord = {ShaderId, RootArguments}
struct ShaderRecord {
    struct PointerWithSize {
        void * ptr;
        UINT size;
        PointerWithSize () : ptr(nullptr), size(0) {}
        PointerWithSize (void * p, UINT s) : ptr(p), size(s) {};
    };
    PointerWithSize ShaderIdentifier;
    PointerWithSize LocalRootArguments;
    ShaderRecord (void * shader_id_ptr, UINT shader_id_size) :
        ShaderIdentifier(shader_id_ptr, shader_id_size)
    {
    }
    ShaderRecord (void * shader_id_ptr, UINT shader_id_size, void * local_root_args_ptr, UINT local_root_args_size) :
        ShaderIdentifier(shader_id_ptr, shader_id_size),
        LocalRootArguments(local_root_args_ptr, local_root_args_size)
    {
    }
    void CopyTo (void * dest) const {
        uint8_t * byte_dest = static_cast<uint8_t *>(dest);
        memcpy(byte_dest, ShaderIdentifier.ptr, ShaderIdentifier.size);
        if (LocalRootArguments.ptr)
            memcpy(byte_dest + ShaderIdentifier.size, LocalRootArguments.ptr, LocalRootArguments.size);
    }
};
// -- ShaderTable = {ShaderRecord1, ShaderRecord2, ShaderRecord3, ...}
class ShaderTable : public GpuUploadBuffer {
private:
    uint8_t * mapped_shader_records_;   // pointer to push_back aka copy new records to the end of table
    UINT shader_record_size_;

    // -- debug support
    std::wstring name_;
    std::vector<ShaderRecord> shader_records_;

    ShaderTable () {}
public:
    ShaderTable (ID3D12Device * dev, UINT num_shader_records, UINT shader_record_size, LPCWSTR resource_name = nullptr) :
        name_(resource_name)
    {
        shader_record_size_ = Align(shader_record_size, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        shader_records_.reserve(num_shader_records);
        UINT buffer_size = num_shader_records * shader_record_size_;
        Allocate(dev, buffer_size, resource_name);
        mapped_shader_records_ = MapCpuWriteOnly();
    }
    void PushBack (ShaderRecord const & shader_record) {
        ThrowIfFailed(shader_records_.size() < shader_records_.capacity());
        shader_records_.push_back(shader_record);
        shader_record.CopyTo(mapped_shader_records_);
        mapped_shader_records_ += shader_record_size_;
    }
    UINT GetShaderRecordSize () const { return shader_record_size_; }
    // -- pretty print shader records:
    void DebugPrint (std::unordered_map<void *, std::wstring> shdaerid_to_string_map) {
        std::wstringstream wstr;
        wstr << L"|------------------------------------------------------------------------\n";
        wstr << L"|Shader table - " << name_.c_str() << L": "
            << shader_record_size_ << L" | "
            << shader_records_.size() * shader_record_size_ << L" bytes\n";
        for (UINT i = 0; i < shader_records_.size(); ++i) {
            wstr << L"| [" << i << L"]: ";
            wstr << shdaerid_to_string_map[shader_records_[i].ShaderIdentifier.ptr] << L", ";
            wstr << shader_records_[i].ShaderIdentifier.size << L" + " << shader_records_[i].LocalRootArguments.size << L" bytes \n";
        }
        wstr << L"|------------------------------------------------------------------------\n\n";
        OutputDebugStringW(wstr.str().c_str());
    }
};

inline void
AllocateUAVBuffer (
    ID3D12Device * dev, UINT64 buffer_size, ID3D12Resource ** resource_pp,
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON,
    wchar_t const * resource_name = nullptr
) {
    ThrowIfFailed(dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(buffer_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        initial_state,
        nullptr,
        IID_PPV_ARGS(resource_pp)
    ));
    if (resource_name)
        (*resource_pp)->SetName(resource_name);
}

template <class T, size_t N>
void DefineExports (T * obj, LPCWSTR(&exports)[N]) {
    for (UINT i = 0; i < N; ++i)
        obj->DefineExport(exports[i]);
}
template <class T, size_t N, size_t M>
void DefineExports (T * obj, LPCWSTR(&exports)[N][M]) {
    for (UINT i = 0; i < N; ++i)
        for (UINT j = 0; j < M; ++j)
            obj->DefineExport(exports[i][j]);
}

inline void
AllocateUploadBuffer (ID3D12Device * dev, void * data_ptr, UINT64 data_size, ID3D12Resource ** resource_pp, wchar_t const * name = nullptr) {
    ThrowIfFailed(dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(data_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(resource_pp)
    ));
    if (name)
        (*resource_pp)->SetName(name);
    void * mapped_data_ptr;
    (*resource_pp)->Map(0, nullptr, &mapped_data_ptr);
    memcpy(mapped_data_ptr, data_ptr, data_size);
    (*resource_pp)->Unmap(0, nullptr);
}
//
// -- pretty print a state object tree
inline void
PrintStateObjectDesc (D3D12_STATE_OBJECT_DESC const * desc) {
    std::wstringstream wstr;
    wstr << L"\n";
    wstr << L"------------------------------------------------------------------------\n";
    wstr << L"| D3D12 State Object 0x" << static_cast<void const *>(desc) << L": ";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";
    auto ExportTree = [](UINT depth, UINT num_exports, D3D12_EXPORT_DESC const * exports) {
        std::wostringstream woss;
        for (UINT i = 0; i < num_exports; ++i) {
            woss << L"|";
            if (depth > 0)
                for (UINT j = 0; j < 2 * depth - 1; ++j) woss << L" ";
            woss << L" [" << i << L"]: ";
            if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
            woss << exports[i].Name << L"\n";
        }
        return woss.str();
    }; // end of lambda
    for (UINT i = 0; i < desc->NumSubobjects; ++i) {
        wstr << L"| [" << i << L"]: ";
        switch (desc->pSubobjects[i].Type)
        {
        case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
            wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
            wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
            wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) <<
                *static_cast<UINT const *>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY: {
            wstr << L"DXIL Library 0x";
            auto lib = static_cast<D3D12_DXIL_LIBRARY_DESC const *>(desc->pSubobjects[i].pDesc);
            wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
            wstr << ExportTree(1, lib->NumExports, lib->pExports);
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
        {
            wstr << L"Existing Library 0x";
            auto collection = static_cast<D3D12_EXISTING_COLLECTION_DESC const *>(desc->pSubobjects[i].pDesc);
            wstr << collection->pExistingCollection << L"\n";
            wstr << ExportTree(1, collection->NumExports, collection->pExports);
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        {
            wstr << L"Subobject to exports association (Subobject [";
            auto association = static_cast<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION const *>(desc->pSubobjects[i].pDesc);
            UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
            wstr << index << L"])\n";
            for (UINT j = 0; j < association->NumExports; ++j)
                wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        {
            wstr << L"DXIL subobjects to exports association (";
            auto association = static_cast<D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION const *>(desc->pSubobjects[i].pDesc);
            wstr << association->SubobjectToAssociate << L")\n";
            for (UINT j =0; j < association->NumExports; ++j)
                wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
        {
            wstr << L"Raytracing shader config\n";
            auto config = static_cast<D3D12_RAYTRACING_SHADER_CONFIG const *>(desc->pSubobjects[i].pDesc);
            wstr << L"| [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
            wstr << L"| [0]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
        {
            wstr << L"Raytracing Pipeline config\n";
            auto config = static_cast<D3D12_RAYTRACING_PIPELINE_CONFIG const *>(desc->pSubobjects[i].pDesc);
            wstr << L"| [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
        {
            wstr << L"Hit Group (";
            auto hit_group = static_cast<D3D12_HIT_GROUP_DESC const *>(desc->pSubobjects[i].pDesc);
            wstr << (hit_group->HitGroupExport ? hit_group->HitGroupExport : L"[none]") << L")\n";
            wstr << L"| [0]: Any hit import: " << (hit_group->AnyHitShaderImport ? hit_group->AnyHitShaderImport : L"[none]") << L"\n";
            wstr << L"| [0]: Closest hit import: " << (hit_group->ClosestHitShaderImport ? hit_group->ClosestHitShaderImport : L"[none]") << L"\n";
            wstr << L"| [0]: Intersection import: " << (hit_group->IntersectionShaderImport ? hit_group->IntersectionShaderImport : L"[none]") << L"\n";
            break;
        }
        }
        wstr << L"------------------------------------------------------------------------\n";
    }
    wstr << L"\n";
    OutputDebugStringW(wstr.str().c_str());
}

inline bool
IsDXRSupported (IDXGIAdapter1 * adapter) {
    ComPtr<ID3D12Device> test_device;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 feature_support_data = {};
    return SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&test_device)))
        && SUCCEEDED(test_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &feature_support_data, sizeof(feature_support_data)))
        && feature_support_data.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}


