#include "render/Renderer.h"

#include "platform/Window.h"
#include "ui/DrawList.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace kestrel {

namespace {

constexpr uint32_t FrameCount = 3;

constexpr char UiShader[] = R"(
cbuffer View : register(b0)
{
    float2 viewport;
};

struct VertexIn
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float2 local : TEXCOORD1;
    float2 halfSize : TEXCOORD2;
    float2 shape : TEXCOORD3;
};

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float2 local : TEXCOORD1;
    float2 halfSize : TEXCOORD2;
    float2 shape : TEXCOORD3;
};

Texture2D atlas : register(t0);
SamplerState atlasSampler : register(s0);

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    output.position = float4(input.position.x / viewport.x * 2.0 - 1.0, 1.0 - input.position.y / viewport.y * 2.0, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.local = input.local;
    output.halfSize = input.halfSize;
    output.shape = input.shape;
    return output;
}

float4 ps_main(VertexOut input) : SV_Target
{
    float4 texel = atlas.Sample(atlasSampler, input.uv);
    float coverage = texel.a;
    if (input.shape.x >= 0.0)
    {
        float radius = input.shape.x;
        float2 q = abs(input.local) - input.halfSize + radius;
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
        coverage *= saturate(0.5 - distance / max(input.shape.y, 1.0));
    }
    return float4(input.color.rgb * texel.rgb, input.color.a * coverage);
}
)";

constexpr char WorldShader[] = R"(
cbuffer Draw : register(b0)
{
    float4x4 viewProjection;
    float4 origin;
    float4 fog;
    float4 params;
    float4 sun;
};

Texture2DArray blocks : register(t0);
Texture2DArray blocksHigh : register(t1);
SamplerState blockSampler : register(s0);

struct WorldIn
{
    uint4 quad : QUAD0;
    uint ao : QUAD1;
    uint vertexId : SV_VertexID;
};

struct WorldOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint material : TEXCOORD1;
    float shade : TEXCOORD2;
    float3 relative : TEXCOORD3;
    nointerpolation uint tint : TEXCOORD4;
    float3 light : TEXCOORD5;
};

static const float lightCurve[16] = {
    0.0, 0.01754386, 0.037037037, 0.05882353,
    0.083333336, 0.11111111, 0.14285715, 0.17948718,
    0.22222222, 0.27272728, 0.33333334, 0.4074074,
    0.5, 0.61904764, 0.7777778, 1.0 };

float3 cornerLight(uint light, uint ao, uint corner)
{
    uint levels = (light >> (corner * 8)) & 0xff;
    float occlusion = 1.0 - float((ao >> (corner * 2)) & 3) * 0.12;
    return float3(lightCurve[levels & 0xf], lightCurve[levels >> 4], occlusion);
}

float3 quadCorner(uint face, uint corner, float3 o, float w, float h)
{
    float3 c[4];
    if (face == 0) {
        c[0] = o; c[1] = o + float3(0, 0, w); c[2] = o + float3(0, h, w); c[3] = o + float3(0, h, 0);
    } else if (face == 1) {
        float3 b = o + float3(1, 0, 0);
        c[0] = b; c[1] = b + float3(0, h, 0); c[2] = b + float3(0, h, w); c[3] = b + float3(0, 0, w);
    } else if (face == 2) {
        c[0] = o; c[1] = o + float3(w, 0, 0); c[2] = o + float3(w, 0, h); c[3] = o + float3(0, 0, h);
    } else if (face == 3) {
        float3 b = o + float3(0, 1, 0);
        c[0] = b; c[1] = b + float3(0, 0, h); c[2] = b + float3(w, 0, h); c[3] = b + float3(w, 0, 0);
    } else if (face == 4) {
        c[0] = o; c[1] = o + float3(0, h, 0); c[2] = o + float3(w, h, 0); c[3] = o + float3(w, 0, 0);
    } else {
        float3 b = o + float3(0, 0, 1);
        c[0] = b; c[1] = b + float3(w, 0, 0); c[2] = b + float3(w, h, 0); c[3] = b + float3(0, h, 0);
    }
    return c[corner];
}

float2 greedyUv(uint face, uint corner, float w, float h, uint flags)
{
    float2 horizontalStandard[4] = { float2(0, 0), float2(w, 0), float2(w, h), float2(0, h) };
    float2 horizontalTransposed[4] = { float2(0, 0), float2(0, h), float2(w, h), float2(w, 0) };
    float2 verticalStandard[4] = { float2(0, h), float2(w, h), float2(w, 0), float2(0, 0) };
    float2 verticalTransposed[4] = { float2(0, h), float2(0, 0), float2(w, 0), float2(w, h) };
    float2 uv = horizontalStandard[corner];
    if (face == 0 || face == 5) {
        uv = verticalStandard[corner];
    } else if (face == 1 || face == 4) {
        uv = verticalTransposed[corner];
    } else if (face == 3) {
        uv = horizontalTransposed[corner];
    }
    if (flags != 0) {
        uv = float2(uv.y, w - uv.x);
    }
    return uv;
}

WorldOut vs_world(WorldIn input)
{
    static const uint cornerOrder[6] = { 0, 1, 2, 0, 2, 3 };
    static const float faceShade[6] = { 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    uint geometry = input.quad.x;
    float3 localOrigin = float3(geometry & 0x1f, (geometry >> 5) & 0x1f, (geometry >> 10) & 0x1f);
    uint face = (geometry >> 15) & 0x7;
    float width = ((geometry >> 18) & 0xf) + 1;
    float height = ((geometry >> 22) & 0xf) + 1;
    uint corner = cornerOrder[input.vertexId];

    WorldOut output;
    float3 position = origin.xyz + quadCorner(face, corner, localOrigin, width, height);
    output.position = mul(viewProjection, float4(position, 1.0));
    output.uv = greedyUv(face, corner, width, height, (input.quad.y >> 12) & 1);
    output.material = input.quad.y;
    output.shade = faceShade[face];
    output.relative = position;
    output.tint = input.quad.z;
    output.light = cornerLight(input.quad.w, input.ao, corner);
    return output;
}

struct ModelIn
{
    uint4 a : MODEL0;
    uint4 b : MODEL1;
    uint4 c : MODEL2;
    uint4 d : MODEL3;
    uint vertexId : SV_VertexID;
};

WorldOut vs_model(ModelIn input)
{
    static const uint cornerOrder[6] = { 0, 1, 2, 0, 2, 3 };
    static const float faceShade[7] = { 0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    uint words[12] = { input.a.x, input.a.y, input.a.z, input.a.w, input.b.x, input.b.y, input.b.z, input.b.w, input.c.x, input.c.y, input.c.z, input.c.w };
    uint corner = cornerOrder[input.vertexId];
    float3 local;
    for (uint i = 0; i < 3; ++i) {
        uint component = corner * 3 + i;
        uint word = words[component / 2];
        int value = (component & 1) != 0 ? (int(word) >> 16) : (int(word << 16) >> 16);
        local[i] = float(value) / 256.0;
    }
    uint uvWord = words[6 + corner];

    WorldOut output;
    float3 position = origin.xyz + local;
    output.position = mul(viewProjection, float4(position, 1.0));
    output.uv = float2(uvWord & 0xffff, uvWord >> 16) / 4096.0;
    if ((words[11] & 0x10) != 0) {
        output.uv.y -= frac(origin.w / 32.0);
    }
    output.material = words[10];
    output.shade = faceShade[min(words[11] & 0xf, 6u)];
    output.relative = position;
    uint rgb = words[11] >> 8;
    output.tint = rgb != 0 ? (0x80000000 | rgb) : 0;
    output.light = cornerLight(input.d.x, input.d.y, corner);
    return output;
}

float4 sampleLayer(float2 uv, uint layer)
{
    float4 low = blocks.Sample(blockSampler, float3(uv, min(layer, 2047u)));
    float4 high = blocksHigh.Sample(blockSampler, float3(uv, layer >= 2048u ? layer - 2048u : 0u));
    return layer >= 2048u ? high : low;
}

float4 sampleMaterial(uint material, float2 uv)
{
    uint layer = material & 0xfff;
    uint count = ((material >> 14) & 0x7f) + 1;
    uint ticksPerFrame = ((material >> 21) & 0x7ff) + 1;
    float timeline = origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    float4 texel = sampleLayer(uv, layer + frame);
    if (count > 1 && ((material >> 13) & 1) != 0) {
        float4 next = sampleLayer(uv, layer + (frame + 1) % count);
        texel = lerp(texel, next, frac(timeline));
    }
    return texel;
}

float3 shadeWorld(float3 rgb, float shade, float3 relative, float3 cornerLevels)
{
    float daylight = max(saturate(params.y), 0.2);
    float channel = max(saturate(cornerLevels.x), saturate(cornerLevels.y) * daylight);
    float light = lerp(0.04, 1.0, channel) * saturate(cornerLevels.z);
    float3 color = rgb * shade * pow(light, 1.0 / 2.2);
    float amount = smoothstep(fog.w, params.x, length(relative));
    return lerp(color, fog.rgb, amount);
}

float4 applyTint(float4 texel, uint tint)
{
    if ((tint & 0x80000000) == 0) {
        return texel;
    }
    float3 color = float3((tint >> 16) & 0xff, (tint >> 8) & 0xff, tint & 0xff) / 255.0;
    if ((tint & 0x40000000) != 0) {
        return float4(lerp(texel.rgb, texel.rgb * color, texel.a), 1.0);
    }
    return float4(texel.rgb * color, texel.a);
}

float4 ps_world(WorldOut input) : SV_Target
{
    float4 texel = applyTint(sampleMaterial(input.material, input.uv), input.tint);
    if (texel.a < 0.5) {
        discard;
    }
    return float4(shadeWorld(texel.rgb, input.shade, input.relative, input.light), 1.0);
}

float4 ps_blend(WorldOut input) : SV_Target
{
    float4 texel = applyTint(sampleMaterial(input.material, input.uv), input.tint);
    if (texel.a < 0.004) {
        discard;
    }
    return float4(shadeWorld(texel.rgb, input.shade, input.relative, input.light) * texel.a, texel.a);
}

struct SkyIn
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
    uint layer : LAYER;
    uint color : COLOR;
    uint flags : FLAGS;
};

struct SkyOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint layer : TEXCOORD1;
    float4 color : TEXCOORD2;
    nointerpolation uint flags : TEXCOORD3;
    float3 relative : TEXCOORD4;
};

SkyOut vs_sky(SkyIn input)
{
    SkyOut output;
    float3 position = origin.xyz + input.position;
    output.position = mul(viewProjection, float4(position, 1.0));
    output.uv = input.uv;
    output.layer = input.layer;
    output.color = float4(input.color & 0xff, (input.color >> 8) & 0xff, (input.color >> 16) & 0xff, input.color >> 24) / 255.0;
    output.flags = input.flags;
    output.relative = position;
    return output;
}

float4 ps_sky(SkyOut input) : SV_Target
{
    float4 color = input.color;
    if ((input.flags & 1) != 0) {
        float4 texel = blocks.SampleLevel(blockSampler, float3(input.uv, input.layer), 0);
        color.rgb *= texel.rgb;
        if ((input.flags & 2) == 0) {
            color.a *= texel.a;
        }
    }
    if ((input.flags & 2) != 0) {
        return float4(color.rgb * color.a, 0.0);
    }
    return float4(color.rgb * color.a, color.a);
}
)";

void check(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        throw std::runtime_error(what);
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

ComPtr<ID3DBlob> compile(const char* source, size_t size, const char* entry, const char* target)
{
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

class D3D12Renderer final : public Renderer {
public:
    explicit D3D12Renderer(Window& window)
        : width(window.width())
        , height(window.height())
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

        D3D12_COMMAND_QUEUE_DESC queueDesc {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");

        DXGI_SWAP_CHAIN_DESC1 scDesc {};
        scDesc.Width = width;
        scDesc.Height = height;
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = FrameCount;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        HWND hwnd = static_cast<HWND>(window.nativeHandle());
        ComPtr<IDXGISwapChain1> sc1;
        check(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1), "CreateSwapChainForHwnd");
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        check(sc1.As(&swapChain), "IDXGISwapChain3");

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc {};
        rtvDesc.NumDescriptors = FrameCount;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap");
        rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc {};
        srvDesc.NumDescriptors = 1 + BlockTexturePages;
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
        createUiPipeline();
        createWorldPipeline();
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    ~D3D12Renderer() override
    {
        waitIdle();
        CloseHandle(fenceEvent);
    }

    void resize(uint32_t newWidth, uint32_t newHeight) override
    {
        waitIdle();
        for (auto& target : targets) {
            target.Reset();
        }
        check(swapChain->ResizeBuffers(FrameCount, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
        width = newWidth;
        height = newHeight;
        createTargets();
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    void uploadUiAtlas(const uint8_t* pixels, uint32_t atlasWidth, uint32_t atlasHeight) override
    {
        waitIdle();
        atlas.Reset();

        D3D12_RESOURCE_DESC textureDesc {};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = atlasWidth;
        textureDesc.Height = atlasHeight;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&atlas)), "CreateCommittedResource");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT rows = 0;
        UINT64 rowSize = 0;
        UINT64 totalSize = 0;
        device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowSize, &totalSize);

        ComPtr<ID3D12Resource> staging = createUploadBuffer(totalSize);
        uint8_t* mapped = nullptr;
        D3D12_RANGE none { 0, 0 };
        check(staging->Map(0, &none, reinterpret_cast<void**>(&mapped)), "Map");
        for (uint32_t y = 0; y < atlasHeight; ++y) {
            std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch, pixels + static_cast<size_t>(y) * atlasWidth * 4, static_cast<size_t>(atlasWidth) * 4);
        }
        staging->Unmap(0, nullptr);

        uploadAllocator->Reset();
        uploadList->Reset(uploadAllocator.Get(), nullptr);

        D3D12_TEXTURE_COPY_LOCATION destination {};
        destination.pResource = atlas.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION source {};
        source.pResource = staging.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;

        uploadList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        transition(uploadList.Get(), atlas.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        uploadList->Close();

        ID3D12CommandList* lists[] = { uploadList.Get() };
        queue->ExecuteCommandLists(1, lists);
        waitIdle();

        D3D12_SHADER_RESOURCE_VIEW_DESC view {};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(atlas.Get(), &view, srvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    void uploadBlockTextures(const BlockTextureUpload& textures) override
    {
        if (textures.layers == 0) {
            return;
        }
        waitIdle();
        for (uint32_t page = 0; page < BlockTexturePages; ++page) {
            uint32_t first = page * BlockTexturePageLayers;
            uint32_t count = textures.layers > first ? std::min(textures.layers - first, BlockTexturePageLayers) : 0;
            uploadBlockTexturePage(textures, page, first, count);
        }
    }

    /**
     * Uploads one page of the block texture layers into its own texture
     * array; an empty page gets a null view so shaders can still bind it.
     */
    void uploadBlockTexturePage(const BlockTextureUpload& textures, uint32_t page, uint32_t first, uint32_t count)
    {
        blockTextures[page].Reset();
        D3D12_SHADER_RESOURCE_VIEW_DESC view {};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2DArray.MipLevels = textures.mipLevels;
        view.Texture2DArray.ArraySize = std::max(count, 1u);
        D3D12_CPU_DESCRIPTOR_HANDLE slot = srvHeap->GetCPUDescriptorHandleForHeapStart();
        slot.ptr += srvStride * (1 + page);
        if (count == 0) {
            device->CreateShaderResourceView(nullptr, &view, slot);
            return;
        }

        D3D12_RESOURCE_DESC textureDesc {};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = textures.size;
        textureDesc.Height = textures.size;
        textureDesc.DepthOrArraySize = static_cast<UINT16>(count);
        textureDesc.MipLevels = static_cast<UINT16>(textures.mipLevels);
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;

        D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&blockTextures[page])), "CreateCommittedResource");

        UINT subresources = count * textures.mipLevels;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresources);
        std::vector<UINT> rows(subresources);
        std::vector<UINT64> rowSizes(subresources);
        UINT64 totalSize = 0;
        device->GetCopyableFootprints(&textureDesc, 0, subresources, 0, footprints.data(), rows.data(), rowSizes.data(), &totalSize);

        ComPtr<ID3D12Resource> staging = createUploadBuffer(totalSize);
        uint8_t* mapped = nullptr;
        D3D12_RANGE none { 0, 0 };
        check(staging->Map(0, &none, reinterpret_cast<void**>(&mapped)), "Map");

        uploadAllocator->Reset();
        uploadList->Reset(uploadAllocator.Get(), nullptr);
        for (uint32_t layer = 0; layer < count; ++layer) {
            for (uint32_t mip = 0; mip < textures.mipLevels; ++mip) {
                UINT index = mip + layer * textures.mipLevels;
                uint32_t side = std::max<uint32_t>(textures.size >> mip, 1);
                const uint8_t* source = textures.mips[mip] + static_cast<size_t>(first + layer) * side * side * 4;
                for (uint32_t y = 0; y < side; ++y) {
                    std::memcpy(mapped + footprints[index].Offset + static_cast<size_t>(y) * footprints[index].Footprint.RowPitch, source + static_cast<size_t>(y) * side * 4, static_cast<size_t>(side) * 4);
                }

                D3D12_TEXTURE_COPY_LOCATION destination {};
                destination.pResource = blockTextures[page].Get();
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = index;

                D3D12_TEXTURE_COPY_LOCATION copySource {};
                copySource.pResource = staging.Get();
                copySource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                copySource.PlacedFootprint = footprints[index];
                uploadList->CopyTextureRegion(&destination, 0, 0, 0, &copySource, nullptr);
            }
        }
        staging->Unmap(0, nullptr);
        transition(uploadList.Get(), blockTextures[page].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        uploadList->Close();

        ID3D12CommandList* lists[] = { uploadList.Get() };
        queue->ExecuteCommandLists(1, lists);
        waitIdle();

        device->CreateShaderResourceView(blockTextures[page].Get(), &view, slot);
    }

    void setChunkMesh(uint64_t id, int32_t originX, int32_t originY, int32_t originZ, const ChunkMeshUpload& mesh) override
    {
        removeChunkMesh(id);
        if (mesh.cubeCount == 0 && mesh.modelCount == 0 && mesh.translucentCubeCount == 0 && mesh.translucentModelCount == 0) {
            return;
        }
        ChunkBuffer chunk;
        const void* sources[4] = { mesh.cubes, mesh.models, mesh.translucentCubes, mesh.translucentModels };
        uint32_t counts[4] = { mesh.cubeCount, mesh.modelCount, mesh.translucentCubeCount, mesh.translucentModelCount };
        for (size_t stream = 0; stream < 4; ++stream) {
            chunk.buffers[stream] = uploadBytes(sources[stream], static_cast<size_t>(counts[stream]) * StreamStride[stream]);
            chunk.counts[stream] = counts[stream];
        }
        chunk.origin = { originX, originY, originZ };
        chunks.emplace(id, std::move(chunk));
    }

    void removeChunkMesh(uint64_t id) override
    {
        auto found = chunks.find(id);
        if (found == chunks.end()) {
            return;
        }
        retire(found->second);
        chunks.erase(found);
    }

    void clearChunkMeshes() override
    {
        for (auto& [id, chunk] : chunks) {
            retire(chunk);
        }
        chunks.clear();
    }

    void beginFrame(float r, float g, float b) override
    {
        waitFor(fenceValues[frameIndex]);
        if (slotFrames[frameIndex].submission > completedReport.submission) {
            completedReport = slotFrames[frameIndex];
        }
        recordedOpaque = 0;
        uint64_t completed = fence->GetCompletedValue();
        std::erase_if(retired, [completed](const RetiredBuffer& entry) {
            return entry.fenceValue <= completed;
        });

        allocators[frameIndex]->Reset();
        commandList->Reset(allocators[frameIndex].Get(), nullptr);

        transition(commandList.Get(), targets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frameIndex) * rtvStride;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        const float color[4] = { r, g, b, 1.0f };
        commandList->ClearRenderTargetView(rtv, color, 0, nullptr);
        commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    }

    void drawWorld(const WorldView& view) override
    {
        if (!blockTextures[0]) {
            return;
        }

        commandList->SetGraphicsRootSignature(worldSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        D3D12_GPU_DESCRIPTOR_HANDLE table = srvHeap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += srvStride;
        commandList->SetGraphicsRootDescriptorTable(1, table);

        D3D12_VIEWPORT viewport { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        D3D12_RECT scissor { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        WorldConstants constants(view);
        auto bindOrigin = [&](float x, float y, float z) {
            constants.setOrigin(x, y, z);
            commandList->SetGraphicsRoot32BitConstants(0, 32, constants.values.data(), 0);
        };
        auto bindVertices = [&](ID3D12Resource* buffer, uint32_t count, uint32_t stride) {
            D3D12_VERTEX_BUFFER_VIEW vertexView {};
            vertexView.BufferLocation = buffer->GetGPUVirtualAddress();
            vertexView.SizeInBytes = count * stride;
            vertexView.StrideInBytes = stride;
            commandList->IASetVertexBuffers(0, 1, &vertexView);
        };
        auto drawStream = [&](const ChunkBuffer& chunk, size_t stream) {
            uint32_t count = chunk.counts[stream];
            if (count == 0) {
                return;
            }
            bindOrigin(static_cast<float>(chunk.origin[0] - view.cameraX), static_cast<float>(chunk.origin[1] - view.cameraY), static_cast<float>(chunk.origin[2] - view.cameraZ));
            bindVertices(chunk.buffers[stream].Get(), count, StreamStride[stream]);
            commandList->DrawInstanced(6, count, 0, 0);
        };

        if (view.backgroundCount) {
            FrameBuffers& buffers = frameBuffers[frameIndex];
            size_t bytes = static_cast<size_t>(view.backgroundCount) * sizeof(SkyVertex);
            ensure(buffers.sky, buffers.skyCapacity, buffers.skyMapped, bytes);
            std::memcpy(buffers.skyMapped, view.background, bytes);
            commandList->SetPipelineState(skyPipeline.Get());
            bindOrigin(0.0f, 0.0f, 0.0f);
            bindVertices(buffers.sky.Get(), view.backgroundCount, sizeof(SkyVertex));
            commandList->DrawInstanced(view.backgroundCount, 1, 0, 0);
        }

        for (size_t stream : { size_t(0), size_t(1) }) {
            commandList->SetPipelineState(stream == 0 ? worldPipeline.Get() : modelPipeline.Get());
            for (const auto& [id, chunk] : chunks) {
                drawStream(chunk, stream);
            }
        }
        recordedOpaque = static_cast<uint32_t>(std::count_if(chunks.begin(), chunks.end(), [](const auto& entry) {
            return entry.second.counts[0] || entry.second.counts[1];
        }));

        std::vector<std::pair<double, const ChunkBuffer*>> ordered;
        for (const auto& [id, chunk] : chunks) {
            if (chunk.counts[2] || chunk.counts[3]) {
                double dx = chunk.origin[0] + 8.0 - view.cameraX;
                double dy = chunk.origin[1] + 8.0 - view.cameraY;
                double dz = chunk.origin[2] + 8.0 - view.cameraZ;
                ordered.emplace_back(dx * dx + dy * dy + dz * dz, &chunk);
            }
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.first > right.first;
        });
        for (const auto& [distance, chunk] : ordered) {
            commandList->SetPipelineState(blendPipeline.Get());
            drawStream(*chunk, 2);
            commandList->SetPipelineState(modelBlendPipeline.Get());
            drawStream(*chunk, 3);
        }
    }

    void drawUi(const ui::DrawList& list) override
    {
        if (list.indices().empty() || !atlas) {
            return;
        }

        FrameBuffers& buffers = frameBuffers[frameIndex];
        size_t vertexBytes = list.vertices().size() * sizeof(ui::UiVertex);
        size_t indexBytes = list.indices().size() * sizeof(uint32_t);
        ensure(buffers.vertices, buffers.vertexCapacity, buffers.vertexMapped, vertexBytes);
        ensure(buffers.indices, buffers.indexCapacity, buffers.indexMapped, indexBytes);
        std::memcpy(buffers.vertexMapped, list.vertices().data(), vertexBytes);
        std::memcpy(buffers.indexMapped, list.indices().data(), indexBytes);

        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->SetPipelineState(uiPipeline.Get());
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        const float viewport[2] = { static_cast<float>(width), static_cast<float>(height) };
        commandList->SetGraphicsRoot32BitConstants(0, 2, viewport, 0);
        commandList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());

        D3D12_VIEWPORT view { 0.0f, 0.0f, viewport[0], viewport[1], 0.0f, 1.0f };
        D3D12_RECT scissor { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        commandList->RSSetViewports(1, &view);
        commandList->RSSetScissorRects(1, &scissor);

        D3D12_VERTEX_BUFFER_VIEW vertexView {};
        vertexView.BufferLocation = buffers.vertices->GetGPUVirtualAddress();
        vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
        vertexView.StrideInBytes = sizeof(ui::UiVertex);

        D3D12_INDEX_BUFFER_VIEW indexView {};
        indexView.BufferLocation = buffers.indices->GetGPUVirtualAddress();
        indexView.SizeInBytes = static_cast<UINT>(indexBytes);
        indexView.Format = DXGI_FORMAT_R32_UINT;

        commandList->IASetVertexBuffers(0, 1, &vertexView);
        commandList->IASetIndexBuffer(&indexView);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawIndexedInstanced(static_cast<UINT>(list.indices().size()), 1, 0, 0, 0);
    }

    void endFrame() override
    {
        transition(commandList.Get(), targets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->Close();

        ID3D12CommandList* lists[] = { commandList.Get() };
        queue->ExecuteCommandLists(1, lists);
        swapChain->Present(0, 0);

        fenceValues[frameIndex] = ++fenceCounter;
        queue->Signal(fence.Get(), fenceCounter);
        slotFrames[frameIndex] = { ++submissions, recordedOpaque };
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }

    uint64_t submittedFrames() const override
    {
        return submissions;
    }

    CompletedFrame completedFrame() const override
    {
        return completedReport;
    }

private:
    static constexpr uint32_t StreamStride[4] = { CubeQuadBytes, ModelQuadBytes, CubeQuadBytes, ModelQuadBytes };

    struct ChunkBuffer {
        std::array<ComPtr<ID3D12Resource>, 4> buffers;
        std::array<uint32_t, 4> counts {};
        std::array<int32_t, 3> origin {};
    };

    struct RetiredBuffer {
        ComPtr<ID3D12Resource> buffer;
        uint64_t fenceValue = 0;
    };

    ComPtr<ID3D12Resource> uploadBytes(const void* data, size_t size)
    {
        if (size == 0) {
            return nullptr;
        }
        ComPtr<ID3D12Resource> buffer = createUploadBuffer(static_cast<UINT64>(size));
        void* mapped = nullptr;
        D3D12_RANGE none { 0, 0 };
        check(buffer->Map(0, &none, &mapped), "Map");
        std::memcpy(mapped, data, size);
        buffer->Unmap(0, nullptr);
        return buffer;
    }

    void retire(ChunkBuffer& chunk)
    {
        for (ComPtr<ID3D12Resource>& buffer : chunk.buffers) {
            if (buffer) {
                retired.push_back({ std::move(buffer), fenceCounter + 1 });
            }
        }
    }

    struct FrameBuffers {
        ComPtr<ID3D12Resource> vertices;
        ComPtr<ID3D12Resource> indices;
        ComPtr<ID3D12Resource> sky;
        size_t vertexCapacity = 0;
        size_t indexCapacity = 0;
        size_t skyCapacity = 0;
        void* vertexMapped = nullptr;
        void* indexMapped = nullptr;
        void* skyMapped = nullptr;
    };

    void createTargets()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < FrameCount; ++i) {
            check(swapChain->GetBuffer(i, IID_PPV_ARGS(&targets[i])), "GetBuffer");
            device->CreateRenderTargetView(targets[i].Get(), nullptr, rtv);
            rtv.ptr += rtvStride;
        }
        createDepth();
    }

    void createUiPipeline()
    {
        D3D12_DESCRIPTOR_RANGE range {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER parameters[2] {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = 2;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC signatureDesc {};
        signatureDesc.NumParameters = 2;
        signatureDesc.pParameters = parameters;
        signatureDesc.NumStaticSamplers = 1;
        signatureDesc.pStaticSamplers = &sampler;
        signatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        check(D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "D3D12SerializeRootSignature");
        check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)), "CreateRootSignature");

        ComPtr<ID3DBlob> vertexShader = compile(UiShader, sizeof(UiShader) - 1, "vs_main", "vs_5_0");
        ComPtr<ID3DBlob> pixelShader = compile(UiShader, sizeof(UiShader) - 1, "ps_main", "ps_5_0");

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc {};
        pipelineDesc.pRootSignature = rootSignature.Get();
        pipelineDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        pipelineDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

        D3D12_RENDER_TARGET_BLEND_DESC& blend = pipelineDesc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pipelineDesc.SampleMask = UINT_MAX;
        pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
        pipelineDesc.DepthStencilState.DepthEnable = FALSE;
        pipelineDesc.DepthStencilState.StencilEnable = FALSE;
        pipelineDesc.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };
        pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDesc.NumRenderTargets = 1;
        pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipelineDesc.SampleDesc.Count = 1;

        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&uiPipeline)), "CreateGraphicsPipelineState");
    }

    void createWorldPipeline()
    {
        D3D12_DESCRIPTOR_RANGE range {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = BlockTexturePages;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER parameters[2] {};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = 32;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler {};
        sampler.Filter = D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC signatureDesc {};
        signatureDesc.NumParameters = 2;
        signatureDesc.pParameters = parameters;
        signatureDesc.NumStaticSamplers = 1;
        signatureDesc.pStaticSamplers = &sampler;
        signatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        check(D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "D3D12SerializeRootSignature");
        check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&worldSignature)), "CreateRootSignature");

        ComPtr<ID3DBlob> vertexShader = compile(WorldShader, sizeof(WorldShader) - 1, "vs_world", "vs_5_0");
        ComPtr<ID3DBlob> pixelShader = compile(WorldShader, sizeof(WorldShader) - 1, "ps_world", "ps_5_0");

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "QUAD", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "QUAD", 1, DXGI_FORMAT_R32_UINT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc {};
        pipelineDesc.pRootSignature = worldSignature.Get();
        pipelineDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        pipelineDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        pipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipelineDesc.SampleMask = UINT_MAX;
        pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
        pipelineDesc.DepthStencilState.DepthEnable = TRUE;
        pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pipelineDesc.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };
        pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDesc.NumRenderTargets = 1;
        pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipelineDesc.SampleDesc.Count = 1;

        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&worldPipeline)), "CreateGraphicsPipelineState");

        ComPtr<ID3DBlob> modelShader = compile(WorldShader, sizeof(WorldShader) - 1, "vs_model", "vs_5_0");
        D3D12_INPUT_ELEMENT_DESC modelLayout[] = {
            { "MODEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "MODEL", 1, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "MODEL", 2, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "MODEL", 3, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };
        pipelineDesc.VS = { modelShader->GetBufferPointer(), modelShader->GetBufferSize() };
        pipelineDesc.InputLayout = { modelLayout, static_cast<UINT>(std::size(modelLayout)) };
        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&modelPipeline)), "CreateGraphicsPipelineState");

        D3D12_RENDER_TARGET_BLEND_DESC& blend = pipelineDesc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        ComPtr<ID3DBlob> blendShader = compile(WorldShader, sizeof(WorldShader) - 1, "ps_blend", "ps_5_0");
        pipelineDesc.PS = { blendShader->GetBufferPointer(), blendShader->GetBufferSize() };
        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&modelBlendPipeline)), "CreateGraphicsPipelineState");
        pipelineDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        pipelineDesc.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };
        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&blendPipeline)), "CreateGraphicsPipelineState");

        ComPtr<ID3DBlob> skyVertex = compile(WorldShader, sizeof(WorldShader) - 1, "vs_sky", "vs_5_0");
        ComPtr<ID3DBlob> skyPixel = compile(WorldShader, sizeof(WorldShader) - 1, "ps_sky", "ps_5_0");
        D3D12_INPUT_ELEMENT_DESC skyLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "LAYER", 0, DXGI_FORMAT_R32_UINT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32_UINT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "FLAGS", 0, DXGI_FORMAT_R32_UINT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        pipelineDesc.VS = { skyVertex->GetBufferPointer(), skyVertex->GetBufferSize() };
        pipelineDesc.PS = { skyPixel->GetBufferPointer(), skyPixel->GetBufferSize() };
        pipelineDesc.InputLayout = { skyLayout, static_cast<UINT>(std::size(skyLayout)) };
        check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&skyPipeline)), "CreateGraphicsPipelineState");
    }

    void createDepth()
    {
        depthBuffer.Reset();
        D3D12_RESOURCE_DESC description {};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = std::max<uint32_t>(width, 1);
        description.Height = std::max<uint32_t>(height, 1);
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear {};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;

        D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depthBuffer)), "CreateCommittedResource");
        device->CreateDepthStencilView(depthBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    ComPtr<ID3D12Resource> createUploadBuffer(UINT64 size)
    {
        D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC description = bufferDescription(size);
        ComPtr<ID3D12Resource> buffer;
        check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)), "CreateCommittedResource");
        return buffer;
    }

    void ensure(ComPtr<ID3D12Resource>& buffer, size_t& capacity, void*& mapped, size_t needed)
    {
        if (capacity >= needed) {
            return;
        }
        capacity = std::max<size_t>({ needed, capacity * 2, 64 * 1024 });
        buffer = createUploadBuffer(capacity);
        D3D12_RANGE none { 0, 0 };
        check(buffer->Map(0, &none, &mapped), "Map");
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

    void waitFor(uint64_t value)
    {
        if (fence->GetCompletedValue() < value) {
            fence->SetEventOnCompletion(value, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    void waitIdle()
    {
        queue->Signal(fence.Get(), ++fenceCounter);
        waitFor(fenceCounter);
    }

    uint32_t width;
    uint32_t height;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    std::array<ComPtr<ID3D12Resource>, FrameCount> targets;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> allocators;
    std::array<FrameBuffers, FrameCount> frameBuffers;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    ComPtr<ID3D12GraphicsCommandList> uploadList;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> uiPipeline;
    ComPtr<ID3D12Resource> atlas;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12RootSignature> worldSignature;
    ComPtr<ID3D12PipelineState> worldPipeline;
    ComPtr<ID3D12PipelineState> modelPipeline;
    ComPtr<ID3D12PipelineState> blendPipeline;
    ComPtr<ID3D12PipelineState> modelBlendPipeline;
    ComPtr<ID3D12PipelineState> skyPipeline;
    std::array<ComPtr<ID3D12Resource>, BlockTexturePages> blockTextures;
    std::unordered_map<uint64_t, ChunkBuffer> chunks;
    std::vector<RetiredBuffer> retired;
    uint32_t srvStride = 0;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    std::array<uint64_t, FrameCount> fenceValues {};
    uint64_t fenceCounter = 0;
    uint64_t submissions = 0;
    uint32_t recordedOpaque = 0;
    std::array<CompletedFrame, FrameCount> slotFrames {};
    CompletedFrame completedReport;
    uint32_t rtvStride = 0;
    uint32_t frameIndex = 0;
};

}

std::unique_ptr<Renderer> Renderer::create(Window& window)
{
    return std::make_unique<D3D12Renderer>(window);
}

}
