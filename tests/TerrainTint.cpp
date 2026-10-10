#include "render/d3d12/D3D12Shaders.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using Microsoft::WRL::ComPtr;

void check(HRESULT result, const char* operation)
{
    if (FAILED(result)) {
        std::fprintf(stderr, "%s failed: %08lx\n", operation, static_cast<unsigned long>(result));
        std::exit(1);
    }
}

int main()
{
    // Execute the production tint function on WARP so this test needs no physical GPU.
    const std::string source = std::string(kestrel::d3d12::WorldShader) + R"(
float4 vs_tint_test(uint vertex : SV_VertexID) : SV_Position
{
    return float4(vertex == 2 ? 3.0 : -1.0, vertex == 1 ? 3.0 : -1.0, 0.0, 1.0);
}
float4 ps_tint_test(float4 position : SV_Position) : SV_Target
{
    uint test = uint(position.x);
    float alpha = test % 3 == 0 ? 0.0 : test % 3 == 1 ? 0.5 : 1.0;
    uint tint = test < 3 ? 0xc0804020u : test < 6 ? 0x80804020u : test < 9 ? 0u : 0xa0804020u;
    if (test == 12) alpha = 1.0 / 255.0;
    return applyTint(float4(0.8, 0.6, 0.4, alpha), tint);
}
float4 vs_face_test(uint vertex : SV_VertexID) : SV_Position
{
    const uint corners[6] = { 0, 1, 2, 0, 2, 3 };
    const float3 normals[6] = {
        float3(-1, 0, 0), float3(1, 0, 0), float3(0, -1, 0),
        float3(0, 1, 0), float3(0, 0, -1), float3(0, 0, 1)
    };
    uint face = uint(origin.w);
    float3 normal = normals[face] * origin.x;
    float3 right = abs(normal.y) > 0.5 ? float3(1, 0, 0) : float3(normal.z, 0, -normal.x);
    float3 up = cross(normal, right);
    float3 position = quadCorner(face, corners[vertex], float3(-0.5, -0.5, -0.5), 1, 1);
    return float4(dot(position, right), dot(position, up), 0.5, 1);
}
float4 ps_face_test(float4 position : SV_Position) : SV_Target
{
    return float4(1, 1, 1, 1);
}
)";
    auto compile = [&](const char* entry, const char* profile) {
        ComPtr<ID3DBlob> shader;
        ComPtr<ID3DBlob> errors;
        HRESULT result = D3DCompile(source.data(), source.size(), "terrain-tint.hlsl", nullptr, nullptr,
            entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
        if (FAILED(result) && errors) {
            std::fprintf(stderr, "%.*s\n", int(errors->GetBufferSize()), static_cast<const char*>(errors->GetBufferPointer()));
        }
        check(result, entry);
        return shader;
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context), "Create WARP device");
    auto vertexCode = compile("vs_tint_test", "vs_5_0");
    auto pixelCode = compile("ps_tint_test", "ps_5_0");
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader), "Create vertex shader");
    check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader), "Create pixel shader");
    D3D11_TEXTURE2D_DESC description {};
    description.Width = 13;
    description.Height = 1;
    description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
    description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    check(device->CreateTexture2D(&description, nullptr, &target), "Create render target");
    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &view), "Create render target view");
    description.BindFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    check(device->CreateTexture2D(&description, nullptr, &readback), "Create readback");
    ID3D11RenderTargetView* views[] = { view.Get() };
    context->OMSetRenderTargets(1, views, nullptr);
    D3D11_VIEWPORT viewport { 0, 0, 13, 1, 0, 1 };
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    context->PSSetShader(pixelShader.Get(), nullptr, 0);
    context->Draw(3, 0);
    context->CopyResource(readback.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped {};
    check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read tint results");
    const float* pixels = static_cast<const float*>(mapped.pData);
    constexpr std::array<float, 3> color { 0.8f, 0.6f, 0.4f };
    constexpr std::array<float, 3> tint { 128.0f / 255, 64.0f / 255, 32.0f / 255 };
    for (unsigned test = 0; test < 13; ++test) {
        float alpha = test == 12 ? 1.0f / 255 : float(test % 3) / 2;
        for (unsigned channel = 0; channel < 4; ++channel) {
            float expected = channel == 3 ? (test < 3 ? 1.0f : test >= 9 ? (alpha > 0 ? 1.0f : 0.0f) : alpha)
                : (test < 3 || test >= 9) ? color[channel] * (1 - alpha + tint[channel] * alpha)
                : test < 6 ? color[channel] * tint[channel] : color[channel];
            float actual = pixels[test * 4 + channel];
            if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001f) {
                std::fprintf(stderr, "Tint case %u channel %u: expected %.6f, got %.6f\n", test, channel, expected, actual);
                std::exit(1);
            }
        }
    }
    context->Unmap(readback.Get(), 0);

    auto faceVertexCode = compile("vs_face_test", "vs_5_0");
    auto facePixelCode = compile("ps_face_test", "ps_5_0");
    check(device->CreateVertexShader(faceVertexCode->GetBufferPointer(), faceVertexCode->GetBufferSize(), nullptr, &vertexShader), "Create face vertex shader");
    check(device->CreatePixelShader(facePixelCode->GetBufferPointer(), facePixelCode->GetBufferSize(), nullptr, &pixelShader), "Create face pixel shader");
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    context->PSSetShader(pixelShader.Get(), nullptr, 0);
    D3D11_BUFFER_DESC bufferDescription {};
    bufferDescription.ByteWidth = 128;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    check(device->CreateBuffer(&bufferDescription, nullptr, &constants), "Create face constants");
    ID3D11Buffer* constantBuffers[] = { constants.Get() };
    context->VSSetConstantBuffers(0, 1, constantBuffers);
    for (uint32_t face = 0; face < 6; ++face) {
        for (float side : { 1.0f, -1.0f }) {
            std::array<float, 32> data {};
            data[16] = side;
            data[19] = float(face);
            context->UpdateSubresource(constants.Get(), 0, nullptr, data.data(), 0, 0);
            for (bool cull : { false, true }) {
                D3D11_RASTERIZER_DESC raster {};
                raster.FillMode = D3D11_FILL_SOLID;
                raster.CullMode = cull ? D3D11_CULL_BACK : D3D11_CULL_NONE;
                raster.FrontCounterClockwise = TRUE;
                raster.DepthClipEnable = TRUE;
                ComPtr<ID3D11RasterizerState> state;
                check(device->CreateRasterizerState(&raster, &state), "Create face rasterizer");
                context->RSSetState(state.Get());
                const float clear[] = { 0, 0, 0, 0 };
                context->ClearRenderTargetView(view.Get(), clear);
                context->Draw(6, 0);
                context->CopyResource(readback.Get(), target.Get());
                check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read face results");
                bool covered = static_cast<const float*>(mapped.pData)[6 * 4] > 0.5f;
                context->Unmap(readback.Get(), 0);
                if (covered != (!cull || side > 0)) {
                    std::fprintf(stderr, "Face %u side %.0f cull %d: wrong coverage\n", face, side, cull);
                    return 1;
                }
            }
        }
    }
}
