#include "render/d3d12/D3D12Shaders.h"

#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main()
{
    using namespace kestrel::d3d12;
    for (const char* entry : { "vs_world", "vs_model", "vs_actor", "vs_overlay", "vs_sky", "ps_world", "ps_solid", "ps_blend", "ps_overlay", "ps_sky" }) {
        ID3DBlob* shader = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT result = D3DCompile(WorldShader, sizeof(WorldShader) - 1, "kestrel.hlsl", nullptr, nullptr, entry,
            std::strncmp(entry, "vs_", 3) == 0 ? "vs_5_0" : "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
        if (FAILED(result)) {
            std::fprintf(stderr, "%s: %.*s\n", entry, errors ? int(errors->GetBufferSize()) : 0,
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        }
        if (shader) shader->Release();
        if (errors) errors->Release();
        if (FAILED(result)) return 1;
    }
}
