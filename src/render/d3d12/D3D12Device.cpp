#include "render/rhi/Device.h"
#include "render/Renderer.h"
#include "client/DebugLog.h"

#include "D3D12Shaders.h"
#include "platform/Window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace kestrel::rhi {

namespace {

constexpr uint32_t FrameCount = 3;
constexpr uint32_t ShaderDescriptorCount = 64;

void check(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
        std::string message = std::string(what) + " failed (HRESULT " + code + ")";
        debugLog("D3D12 " + message);
        throw std::runtime_error(message);
    }
}

D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties {};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC bufferDescription(UINT64 size)
{
    D3D12_RESOURCE_DESC description {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

ComPtr<ID3DBlob> compile(const PipelineDesc& desc, bool vertex)
{
    const char* entry = vertex ? desc.vertexEntry : desc.pixelEntry;
    const char* target = vertex ? "vs_5_0" : "ps_5_0";
    const char* source = desc.library == ShaderLibrary::Ui ? d3d12::UiShader : d3d12::WorldShader;
    size_t size = desc.library == ShaderLibrary::Ui ? sizeof(d3d12::UiShader) - 1 : sizeof(d3d12::WorldShader) - 1;
    if (desc.source) {
        if (desc.source->hlsl.empty()) {
            throw std::runtime_error("The shader has no HLSL, which Direct3D 12 needs");
        }
        source = desc.source->hlsl.data();
        size = desc.source->hlsl.size();
        entry = vertex ? "vs_main" : "ps_main";
    }
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(source, size, "kestrel.hlsl", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        std::string message = "Shader compilation failed";
        if (errors) {
            message += ": ";
            message.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        }
        throw std::runtime_error(message);
    }
    return code;
}

DXGI_FORMAT vertexFormat(VertexFormat format)
{
    switch (format) {
    case VertexFormat::Float:
        return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float2:
        return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::UByte4Norm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::UInt:
        return DXGI_FORMAT_R32_UINT;
    case VertexFormat::UInt4:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_RENDER_TARGET_BLEND_DESC blendState(BlendMode mode)
{
    D3D12_RENDER_TARGET_BLEND_DESC blend {};
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    switch (mode) {
    case BlendMode::None:
        break;
    case BlendMode::Alpha:
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::Premultiplied:
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::Multiply:
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_DEST_COLOR;
        blend.DestBlend = D3D12_BLEND_SRC_COLOR;
        blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    }
    return blend;
}

D3D12_STATIC_SAMPLER_DESC samplerState(SamplerMode mode)
{
    D3D12_STATIC_SAMPLER_DESC sampler {};
    if (mode == SamplerMode::PixelClamp) {
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    } else {
        sampler.Filter = D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return sampler;
}

class D3D12Buffer final : public Buffer {
public:
    D3D12Buffer(ComPtr<ID3D12Resource> created, size_t bytes, bool map = true)
        : resource(std::move(created))
        , bytes(bytes)
    {
        D3D12_RANGE none { 0, 0 };
        if (map) check(resource->Map(0, &none, &memory), "Map");
    }

    void* mapped() override
    {
        return memory;
    }

    size_t size() const override
    {
        return bytes;
    }

    ComPtr<ID3D12Resource> resource;
    size_t bytes = 0;
    void* memory = nullptr;
};

class D3D12Texture final : public Texture {
public:
    const TextureDesc& desc() const override
    {
        return description;
    }

    ComPtr<ID3D12Resource> resource;
    TextureDesc description;
    bool sampled = false;
};

class D3D12Pipeline final : public Pipeline {
public:
    ComPtr<ID3D12PipelineState> state;
    ID3D12RootSignature* signature = nullptr;
    bool actorConstants = false;
};

class D3D12Device;

class D3D12TextureSet final : public TextureSet {
public:
    D3D12TextureSet(D3D12Device& owner, uint32_t first, uint32_t count, bool arrays)
        : owner(owner)
        , first(first)
        , count(count)
        , arrays(arrays)
    {
    }

    void bind(uint32_t slot, const Texture* texture) override;

    D3D12Device& owner;
    uint32_t first = 0;
    uint32_t count = 0;
    bool arrays = false;
};

class D3D12Device final : public Device {
public:
    explicit D3D12Device(Window& window)
        : surfaceWidth(window.width())
        , surfaceHeight(window.height())
    {
        UINT factoryFlags = 0;
#ifndef NDEBUG
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
                break;
            }
        }
        if (!device) {
            throw std::runtime_error("No D3D12 device");
        }
        DXGI_ADAPTER_DESC1 adapterDesc {};
        if (SUCCEEDED(adapter->GetDesc1(&adapterDesc))) {
            int length = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
            if (length > 1) {
                adapterName.resize(static_cast<size_t>(length - 1));
                WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterName.data(), length, nullptr, nullptr);
            }
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");

        DXGI_SWAP_CHAIN_DESC1 scDesc {};
        scDesc.Width = surfaceWidth;
        scDesc.Height = surfaceHeight;
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = FrameCount;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        BOOL allowTearing = FALSE;
        tearing = SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing;
        scDesc.Flags = swapChainFlags();

        HWND hwnd = static_cast<HWND>(window.nativeHandle());
        ComPtr<IDXGISwapChain1> sc1;
        check(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1), "CreateSwapChainForHwnd");
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        check(sc1.As(&swapChain), "IDXGISwapChain3");

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc {};
        rtvDesc.NumDescriptors = FrameCount + 1;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap");
        rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc {};
        srvDesc.NumDescriptors = ShaderDescriptorCount;
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap)), "CreateDescriptorHeap");
        srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc {};
        dsvDesc.NumDescriptors = 1;
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap)), "CreateDescriptorHeap");

        for (uint32_t i = 0; i < FrameCount; ++i) {
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])), "CreateCommandAllocator");
        }
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get(), nullptr, IID_PPV_ARGS(&commandList)), "CreateCommandList");
        commandList->Close();

        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator)), "CreateCommandAllocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadList)), "CreateCommandList");
        uploadList->Close();

        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        createTargets();
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    ~D3D12Device() override
    {
        waitIdle();
        CloseHandle(fenceEvent);
    }

    std::string_view backendName() const override
    {
        return "Direct3D 12";
    }

    const std::string& deviceName() const override
    {
        return adapterName;
    }

    uint32_t width() const override
    {
        return surfaceWidth;
    }

    uint32_t height() const override
    {
        return surfaceHeight;
    }

    uint32_t framesInFlight() const override
    {
        return FrameCount;
    }

    uint32_t frameSlot() const override
    {
        return frameIndex;
    }

    uint64_t completedSubmission() const override
    {
        return completed;
    }

    uint64_t submittedFrames() const override
    {
        return submissions;
    }

    void resize(uint32_t newWidth, uint32_t newHeight) override
    {
        waitIdle();
        for (auto& target : targets) {
            target.Reset();
        }
        check(swapChain->ResizeBuffers(FrameCount, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, swapChainFlags()), "ResizeBuffers");
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        createTargets();
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    void waitIdle() override
    {
        queue->Signal(fence.Get(), ++fenceCounter);
        waitFor(fenceCounter);
    }

    std::unique_ptr<Buffer> createBuffer(size_t size) override
    {
        return std::make_unique<D3D12Buffer>(createUploadBuffer(static_cast<UINT64>(size)), size);
    }

    std::unique_ptr<Buffer> createPersistentBuffer(size_t size) override
    {
        D3D12_HEAP_PROPERTIES heap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC desc = bufferDescription(std::max<size_t>(size, 1));
        ComPtr<ID3D12Resource> resource;
        check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)), "CreateCommittedResource: persistent buffer");
        return std::make_unique<D3D12Buffer>(std::move(resource), size, false);
    }

    void uploadBuffer(Buffer& target, const void* data, size_t bytes) override
    {
        if (!bytes) return;
        auto transfer = beginTransfer(bytes);
        std::memcpy(transfer.mapped, data, bytes);
        auto& buffer = static_cast<D3D12Buffer&>(target);
        transition(transfer.list.Get(), buffer.resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
        transfer.list->CopyBufferRegion(buffer.resource.Get(), 0, transfer.staging.Get(), 0, bytes);
        transition(transfer.list.Get(), buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        queueTransfer(std::move(transfer));
    }

    std::unique_ptr<Texture> createTexture(const TextureDesc& desc) override
    {
        auto texture = std::make_unique<D3D12Texture>();
        texture->description = desc;
        D3D12_RESOURCE_DESC textureDesc {};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = desc.width;
        textureDesc.Height = desc.height;
        textureDesc.DepthOrArraySize = static_cast<UINT16>(desc.layers);
        textureDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture->resource)), "CreateCommittedResource: texture");
        return texture;
    }

    void uploadTexture(Texture& target, const std::vector<TextureData>& data) override
    {
        uploadTextureAsync(target, data);
        waitIdle();
        collectTransfers(UINT32_MAX);
    }

    void uploadTextureAsync(Texture& target, const std::vector<TextureData>& data) override
    {
        if (data.empty()) {
            return;
        }
        auto& texture = static_cast<D3D12Texture&>(target);
        D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(data.size());
        UINT64 stagingSize = 0;
        std::vector<UINT64> offsets(data.size());
        for (size_t i = 0; i < data.size(); ++i) {
            UINT width = data[i].width ? data[i].width : std::max(texture.description.width >> data[i].mip, 1u);
            UINT rows = data[i].height ? data[i].height : std::max(texture.description.height >> data[i].mip, 1u);
            footprints[i].Footprint = { textureDesc.Format, width, rows, 1,
                (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) };
            offsets[i] = stagingSize;
            UINT64 bytes = static_cast<UINT64>(footprints[i].Footprint.RowPitch) * rows;
            stagingSize += (bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
        }

        auto transfer = beginTransfer(static_cast<size_t>(stagingSize));
        auto* mapped = static_cast<uint8_t*>(transfer.mapped);
        auto* list = transfer.list.Get();
        if (texture.sampled) {
            transition(list, texture.resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        }
        for (size_t i = 0; i < data.size(); ++i) {
            UINT index = data[i].mip + data[i].layer * texture.description.mipLevels;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = footprints[i];
            footprint.Offset = offsets[i];
            uint32_t rowWidth = footprint.Footprint.Width;
            uint32_t rowCount = footprint.Footprint.Height;
            size_t sourceStride = data[i].rowBytes ? data[i].rowBytes : size_t(rowWidth) * 4;
            for (uint32_t y = 0; y < rowCount; ++y) {
                std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch, data[i].pixels + static_cast<size_t>(y) * sourceStride, static_cast<size_t>(rowWidth) * 4);
            }
            D3D12_TEXTURE_COPY_LOCATION destination {};
            destination.pResource = texture.resource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = index;
            D3D12_TEXTURE_COPY_LOCATION source {};
            source.pResource = transfer.staging.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            list->CopyTextureRegion(&destination, data[i].x, data[i].y, 0, &source, nullptr);
        }
        transition(list, texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        texture.sampled = true;
        queueTransfer(std::move(transfer));
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override
    {
        auto pipeline = std::make_unique<D3D12Pipeline>();
        pipeline->signature = rootSignature(desc.bindings);
        pipeline->actorConstants = desc.bindings.actorConstants;

        ComPtr<ID3DBlob> vertexShader = compile(desc, true);
        ComPtr<ID3DBlob> pixelShader = compile(desc, false);

        std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
        D3D12_INPUT_CLASSIFICATION classification = desc.vertices.perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        for (const VertexAttribute& attribute : desc.vertices.attributes) {
            layout.push_back({ attribute.semantic, attribute.index, vertexFormat(attribute.format), 0, attribute.offset, classification, desc.vertices.perInstance ? 1u : 0u });
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc {};
        pipelineDesc.pRootSignature = pipeline->signature;
        pipelineDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        pipelineDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        pipelineDesc.BlendState.RenderTarget[0] = blendState(desc.blend);
        pipelineDesc.SampleMask = UINT_MAX;
        pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
        pipelineDesc.DepthStencilState.DepthEnable = TRUE;
        pipelineDesc.DepthStencilState.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pipelineDesc.DepthStencilState.DepthFunc = desc.depthCompare == DepthCompare::Less ? D3D12_COMPARISON_FUNC_LESS : D3D12_COMPARISON_FUNC_LESS_EQUAL;
        pipelineDesc.DepthStencilState.StencilEnable = FALSE;
        pipelineDesc.InputLayout = { layout.data(), static_cast<UINT>(layout.size()) };
        pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDesc.NumRenderTargets = 1;
        pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipelineDesc.SampleDesc.Count = 1;
        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline->state)), "CreateGraphicsPipelineState");
        return pipeline;
    }

    std::unique_ptr<TextureSet> createTextureSet(uint32_t count, bool arrays, SamplerMode) override
    {
        if (nextDescriptor + count > ShaderDescriptorCount) {
            throw std::runtime_error("Out of shader descriptors");
        }
        auto set = std::make_unique<D3D12TextureSet>(*this, nextDescriptor, count, arrays);
        nextDescriptor += count;
        for (uint32_t slot = 0; slot < count; ++slot) {
            writeDescriptor(set->first + slot, nullptr, arrays);
        }
        return set;
    }

    void retire(std::unique_ptr<Buffer> buffer) override
    {
        retired.push_back({ std::move(buffer), fenceCounter + 1 });
    }

    uint64_t beginFrame(float r, float g, float b) override
    {
        waitFor(fenceValues[frameIndex]);
        collectTransfers(4);
        if (slotSubmissions[frameIndex] > completed) {
            completed = slotSubmissions[frameIndex];
        }
        uint64_t done = fence->GetCompletedValue();
        uint32_t released = 0;
        for (size_t index = 0; index < retired.size() && released < 4;) {
            if (retired[index].fenceValue <= done) {
                if (index + 1 != retired.size()) retired[index] = std::move(retired.back());
                retired.pop_back();
                ++released;
            } else {
                ++index;
            }
        }

        actorCursor = 0;
        allocators[frameIndex]->Reset();
        commandList->Reset(allocators[frameIndex].Get(), nullptr);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(FrameCount) * rtvStride;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        const float color[4] = { r, g, b, 1.0f };
        commandList->ClearRenderTargetView(rtv, color, 0, nullptr);
        commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        viewport = { 0.0f, 0.0f, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight), 0.0f, 1.0f };
        D3D12_RECT scissor { 0, 0, static_cast<LONG>(surfaceWidth), static_cast<LONG>(surfaceHeight) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);

        boundSignature = nullptr;
        boundState = nullptr;
        boundTextures = UINT32_MAX;
        active = true;
        return submissions + 1;
    }

    bool recording() const override
    {
        return active;
    }

    void setPipeline(const Pipeline& pipeline) override
    {
        const auto& chosen = static_cast<const D3D12Pipeline&>(pipeline);
        boundActorConstants = chosen.actorConstants;
        if (chosen.signature != boundSignature) {
            commandList->SetGraphicsRootSignature(chosen.signature);
            boundSignature = chosen.signature;
            boundTextures = UINT32_MAX;
        }
        if (chosen.state.Get() != boundState) {
            commandList->SetPipelineState(chosen.state.Get());
            boundState = chosen.state.Get();
        }
    }

    void setConstants(const void* values, uint32_t count) override
    {
        commandList->SetGraphicsRoot32BitConstants(0, count, values, 0);
    }

    void setActorConstants(const void* values, uint32_t count) override
    {
        if (!active || !boundActorConstants || !values || count != 36) return;
        constexpr size_t pageBytes = 1024 * 1024;
        constexpr size_t stride = 256;
        size_t page = actorCursor / pageBytes;
        size_t offset = actorCursor % pageBytes;
        auto& pages = actorPages[frameIndex];
        if (page == pages.size()) pages.push_back(createBuffer(pageBytes));
        std::memcpy(static_cast<uint8_t*>(pages[page]->mapped()) + offset, values, count * sizeof(uint32_t));
        auto& buffer = static_cast<D3D12Buffer&>(*pages[page]);
        commandList->SetGraphicsRootConstantBufferView(2, buffer.resource->GetGPUVirtualAddress() + offset);
        actorCursor += stride;
    }

    void setTextures(const TextureSet& textures) override
    {
        const auto& set = static_cast<const D3D12TextureSet&>(textures);
        if (set.first == boundTextures) {
            return;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE table = srvHeap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += static_cast<UINT64>(srvStride) * set.first;
        commandList->SetGraphicsRootDescriptorTable(1, table);
        boundTextures = set.first;
    }

    void setVertexBuffer(const Buffer& buffer, uint32_t stride, size_t bytes) override
    {
        D3D12_VERTEX_BUFFER_VIEW view {};
        view.BufferLocation = static_cast<const D3D12Buffer&>(buffer).resource->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>(bytes);
        view.StrideInBytes = stride;
        commandList->IASetVertexBuffers(0, 1, &view);
    }

    void setIndexBuffer(const Buffer& buffer, size_t bytes) override
    {
        D3D12_INDEX_BUFFER_VIEW view {};
        view.BufferLocation = static_cast<const D3D12Buffer&>(buffer).resource->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>(bytes);
        view.Format = DXGI_FORMAT_R32_UINT;
        commandList->IASetIndexBuffer(&view);
    }

    void setDepthRange(float maxDepth) override
    {
        if (viewport.MaxDepth == maxDepth) {
            return;
        }
        viewport.MaxDepth = maxDepth;
        commandList->RSSetViewports(1, &viewport);
    }

    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override
    {
        commandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void drawIndexed(uint32_t indexCount) override
    {
        commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
    }

    void endFrame() override
    {
        const bool capturing = captureWanted && recordCapture();
        if (!capturing) {
            transition(commandList.Get(), colorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        transition(commandList.Get(), targets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->CopyResource(targets[frameIndex].Get(), colorBuffer.Get());
        transition(commandList.Get(), targets[frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        transition(commandList.Get(), colorBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->Close();
        ID3D12CommandList* lists[] = { commandList.Get() };
        queue->ExecuteCommandLists(1, lists);
        fenceValues[frameIndex] = ++fenceCounter;
        queue->Signal(fence.Get(), fenceCounter);
        slotSubmissions[frameIndex] = ++submissions;
        active = false;
        if (capturing) {
            finishCapture();
        }
        if (vsync) {
            swapChain->Present(1, 0);
        } else {
            BOOL exclusive = FALSE;
            swapChain->GetFullscreenState(&exclusive, nullptr);
            swapChain->Present(0, tearing && !exclusive ? DXGI_PRESENT_ALLOW_TEARING : 0);
        }
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    void setVsync(bool enabled) override
    {
        vsync = enabled;
    }

    bool requestCapture() override
    {
        captureWanted = true;
        return true;
    }

    bool takeCapture(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) override
    {
        if (capturedPixels.empty()) {
            return false;
        }
        rgba = std::move(capturedPixels);
        capturedPixels.clear();
        width = capturedWidth;
        height = capturedHeight;
        return true;
    }

    bool recordCapture()
    {
        captureWanted = false;
        debugLog("D3D12 recordCapture " + std::to_string(surfaceWidth) + "x" + std::to_string(surfaceHeight));
        D3D12_RESOURCE_DESC description = colorBuffer->GetDesc();
        UINT64 total = 0;
        UINT rowCount = 0;
        UINT64 rowSize = 0;
        device->GetCopyableFootprints(&description, 0, 1, 0, &captureFootprint, &rowCount, &rowSize, &total);
        captureRows = rowCount;
        captureReadback.Reset();
        D3D12_HEAP_PROPERTIES readbackHeap = heapProperties(D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_DESC buffer = bufferDescription(total);
        HRESULT created = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&captureReadback));
        if (FAILED(created)) {
            debugLog("D3D12 capture readback alloc failed");
            return false;
        }
        transition(commandList.Get(), colorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION destination {};
        destination.pResource = captureReadback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = captureFootprint;
        D3D12_TEXTURE_COPY_LOCATION source {};
        source.pResource = colorBuffer.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        captureExtentWidth = surfaceWidth;
        captureExtentHeight = surfaceHeight;
        return true;
    }

    void finishCapture()
    {
        waitFor(fenceValues[frameIndex]);
        uint8_t* mapped = nullptr;
        if (FAILED(captureReadback->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped) {
            debugLog("D3D12 capture map failed");
            captureReadback.Reset();
            return;
        }
        const uint32_t width = captureExtentWidth;
        const uint32_t height = captureExtentHeight;
        capturedPixels.resize(size_t(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* in = mapped + size_t(y) * captureFootprint.Footprint.RowPitch;
            uint8_t* out = capturedPixels.data() + size_t(y) * width * 4;
            std::memcpy(out, in, size_t(width) * 4);
        }
        captureReadback->Unmap(0, nullptr);
        captureReadback.Reset();
        capturedWidth = width;
        capturedHeight = height;
        debugLog("D3D12 captured " + std::to_string(width) + "x" + std::to_string(height));
    }

    /**
     * Points one shader descriptor at a texture, or at nothing of the given
     * kind, once the GPU no longer reads it.
     */
    void writeDescriptor(uint32_t index, const Texture* texture, bool arrays)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view {};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        const auto* chosen = static_cast<const D3D12Texture*>(texture);
        uint32_t mips = chosen ? chosen->description.mipLevels : 1;
        if (arrays) {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipLevels = mips;
            view.Texture2DArray.ArraySize = chosen ? chosen->description.layers : 1;
        } else {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = mips;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(srvStride) * index;
        device->CreateShaderResourceView(chosen ? chosen->resource.Get() : nullptr, &view, handle);
    }

private:
    struct RetiredBuffer {
        std::unique_ptr<Buffer> buffer;
        uint64_t fenceValue = 0;
    };

    ID3D12RootSignature* rootSignature(const BindingLayout& bindings)
    {
        auto key = std::make_tuple(bindings.constantCount, bindings.constantsVertexOnly, bindings.textureCount, static_cast<int>(bindings.sampler), bindings.actorConstants);
        auto found = signatures.find(key);
        if (found != signatures.end()) {
            return found->second.Get();
        }
        D3D12_DESCRIPTOR_RANGE range {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = bindings.textureCount;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER parameters[3] {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = bindings.constantCount;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].ShaderVisibility = bindings.constantsVertexOnly ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler = samplerState(bindings.sampler);
        D3D12_ROOT_SIGNATURE_DESC signatureDesc {};
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[2].Descriptor.ShaderRegister = 1;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        signatureDesc.NumParameters = bindings.actorConstants ? 3 : 2;
        signatureDesc.pParameters = parameters;
        signatureDesc.NumStaticSamplers = 1;
        signatureDesc.pStaticSamplers = &sampler;
        signatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        check(D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "D3D12SerializeRootSignature");
        ComPtr<ID3D12RootSignature> created;
        check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&created)), "CreateRootSignature");
        ID3D12RootSignature* raw = created.Get();
        signatures.emplace(key, std::move(created));
        return raw;
    }

    void createTargets()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < FrameCount; ++i) {
            check(swapChain->GetBuffer(i, IID_PPV_ARGS(&targets[i])), "GetBuffer");
            device->CreateRenderTargetView(targets[i].Get(), nullptr, rtv);
            rtv.ptr += rtvStride;
        }
        createColorBuffer();
        createDepth();
    }

    void createColorBuffer()
    {
        colorBuffer.Reset();
        D3D12_RESOURCE_DESC description {};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = std::max<uint32_t>(surfaceWidth, 1);
        description.Height = std::max<uint32_t>(surfaceHeight, 1);
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear {};
        clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        clear.Color[0] = 0.0f;
        clear.Color[1] = 0.0f;
        clear.Color[2] = 0.0f;
        clear.Color[3] = 1.0f;
        D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&colorBuffer)), "CreateCommittedResource: color buffer");
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(FrameCount) * rtvStride;
        device->CreateRenderTargetView(colorBuffer.Get(), nullptr, rtv);
    }

    void createDepth()
    {
        depthBuffer.Reset();
        D3D12_RESOURCE_DESC description {};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = std::max<uint32_t>(surfaceWidth, 1);
        description.Height = std::max<uint32_t>(surfaceHeight, 1);
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear {};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;

        D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depthBuffer)), "CreateCommittedResource: depth buffer");
        device->CreateDepthStencilView(depthBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    struct Transfer {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> staging;
        void* mapped = nullptr;
        size_t capacity = 0;
        uint64_t fenceValue = 0;
    };
    std::vector<Transfer> transfers;
    std::vector<Transfer> freeTransfers;
    size_t freeTransferBytes = 0;

    void collectTransfers(uint32_t releaseBudget = 0)
    {
        uint64_t done = fence->GetCompletedValue();
        size_t completedBytes = freeTransferBytes;
        for (const auto& transfer : transfers) {
            if (transfer.fenceValue <= done) completedBytes += transfer.capacity;
        }
        constexpr size_t CompletedTransferBudget = 128 * 1024 * 1024;
        for (size_t index = 0; index < transfers.size();) {
            if (transfers[index].fenceValue > done) {
                ++index;
                continue;
            }
            const auto capacity = transfers[index].capacity;
            bool cache = freeTransfers.size() < 32 && capacity <= 64 * 1024 * 1024 && freeTransferBytes <= 64 * 1024 * 1024 - capacity;
            if (!cache && !releaseBudget && completedBytes <= CompletedTransferBudget) {
                ++index;
                continue;
            }
            if (cache) {
                freeTransferBytes += capacity;
                freeTransfers.push_back(std::move(transfers[index]));
            } else {
                completedBytes -= capacity;
                if (releaseBudget) --releaseBudget;
            }
            if (index + 1 != transfers.size()) transfers[index] = std::move(transfers.back());
            transfers.pop_back();
        }
    }

    Transfer beginTransfer(size_t bytes)
    {
        collectTransfers();
        size_t best = freeTransfers.size();
        for (size_t index = 0; index < freeTransfers.size(); ++index) {
            if (freeTransfers[index].capacity < bytes) continue;
            if (best == freeTransfers.size() || freeTransfers[index].capacity < freeTransfers[best].capacity) best = index;
        }
        if (best == freeTransfers.size() && !freeTransfers.empty()) {
            best = 0;
            for (size_t index = 1; index < freeTransfers.size(); ++index) {
                if (freeTransfers[index].capacity > freeTransfers[best].capacity) best = index;
            }
        }
        Transfer transfer;
        if (best != freeTransfers.size()) {
            transfer = std::move(freeTransfers[best]);
            freeTransferBytes -= transfer.capacity;
            if (best + 1 != freeTransfers.size()) freeTransfers[best] = std::move(freeTransfers.back());
            freeTransfers.pop_back();
            check(transfer.allocator->Reset(), "Reset: transfer allocator");
            check(transfer.list->Reset(transfer.allocator.Get(), nullptr), "Reset: transfer list");
            if (transfer.capacity < bytes) {
                transfer.capacity = std::max(bytes, transfer.capacity * 2);
                transfer.staging = createUploadBuffer(transfer.capacity);
                D3D12_RANGE none { 0, 0 };
                check(transfer.staging->Map(0, &none, &transfer.mapped), "Map: transfer");
            }
        } else {
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&transfer.allocator)), "CreateCommandAllocator: transfer");
            check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, transfer.allocator.Get(), nullptr, IID_PPV_ARGS(&transfer.list)), "CreateCommandList: transfer");
            transfer.capacity = std::max<size_t>(bytes, 64 * 1024);
            transfer.staging = createUploadBuffer(transfer.capacity);
            D3D12_RANGE none { 0, 0 };
            check(transfer.staging->Map(0, &none, &transfer.mapped), "Map: transfer");
        }
        return transfer;
    }

    void queueTransfer(Transfer transfer)
    {
        check(transfer.list->Close(), "Close: transfer");
        ID3D12CommandList* lists[] = { transfer.list.Get() };
        queue->ExecuteCommandLists(1, lists);
        transfer.fenceValue = ++fenceCounter;
        check(queue->Signal(fence.Get(), transfer.fenceValue), "Signal: transfer");
        transfers.push_back(std::move(transfer));
    }

    ComPtr<ID3D12Resource> createUploadBuffer(UINT64 size)
    {
        D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC description = bufferDescription(size);
        ComPtr<ID3D12Resource> buffer;
        std::string allocation = "CreateCommittedResource: upload buffer bytes=" + std::to_string(description.Width);
        check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)), allocation.c_str());
        return buffer;
    }

    void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    }

    UINT swapChainFlags() const
    {
        return tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    }

    void waitFor(uint64_t value)
    {
        if (fence->GetCompletedValue() < value) {
            fence->SetEventOnCompletion(value, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    uint32_t surfaceWidth;
    uint32_t surfaceHeight;
    bool tearing = false;
    bool vsync = false;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    std::string adapterName;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12Resource> colorBuffer;
    std::array<ComPtr<ID3D12Resource>, FrameCount> targets;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> allocators;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    ComPtr<ID3D12GraphicsCommandList> uploadList;
    std::map<std::tuple<uint32_t, bool, uint32_t, int, bool>, ComPtr<ID3D12RootSignature>> signatures;
    std::vector<RetiredBuffer> retired;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    std::array<uint64_t, FrameCount> fenceValues {};
    std::array<uint64_t, FrameCount> slotSubmissions {};
    uint64_t fenceCounter = 0;
    uint64_t submissions = 0;
    uint64_t completed = 0;
    D3D12_VIEWPORT viewport {};
    ID3D12RootSignature* boundSignature = nullptr;
    ID3D12PipelineState* boundState = nullptr;
    uint32_t boundTextures = UINT32_MAX;
    bool boundActorConstants = false;
    std::array<std::vector<std::unique_ptr<Buffer>>, FrameCount> actorPages;
    size_t actorCursor = 0;
    bool active = false;
    uint32_t nextDescriptor = 0;
    uint32_t srvStride = 0;
    uint32_t rtvStride = 0;
    uint32_t frameIndex = 0;
    bool captureWanted = false;
    ComPtr<ID3D12Resource> captureReadback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT captureFootprint {};
    UINT captureRows = 0;
    uint32_t captureExtentWidth = 0;
    uint32_t captureExtentHeight = 0;
    std::vector<uint8_t> capturedPixels;
    uint32_t capturedWidth = 0;
    uint32_t capturedHeight = 0;
};

void D3D12TextureSet::bind(uint32_t slot, const Texture* texture)
{
    if (slot >= count) {
        return;
    }
    owner.waitIdle();
    owner.writeDescriptor(first + slot, texture, arrays);
}

}

std::unique_ptr<Device> Device::create(Window& window)
{
    return std::make_unique<D3D12Device>(window);
}

}

namespace kestrel {

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return rhi::createRenderer(rhi::Device::create(window));
}

}
