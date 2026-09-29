#include "mod/Api.h"

using namespace kestrel::mod;

namespace {

// The same two effects for Direct3D 12 and Metal. The Vulkan versions are the
// GLSL files in shaders/, compiled to SPIR-V by the build.
constexpr const char* Hlsl = R"(
cbuffer Constants : register(b0)
{
    float4x4 transform;
    float4 timing;
    float4 params[3];
};

struct VertexIn
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    output.position = mul(transform, float4(input.position, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

#ifdef GLOW
float4 ps_main(VertexOut input) : SV_Target
{
    float2 edge = abs(input.uv - 0.5) * 2.0;
    float rim = smoothstep(0.75, 1.0, max(edge.x, edge.y));
    float pulse = 0.65 + 0.35 * sin(timing.x * 4.0);
    return float4(params[0].rgb * input.color.rgb, rim * pulse * input.color.a);
}
#else
float4 ps_main(VertexOut input) : SV_Target
{
    float2 fromCenter = (input.uv - 0.5) * float2(timing.y / timing.z, 1.0);
    float shade = smoothstep(0.35, 0.95, length(fromCenter)) * params[0].x;
    return float4(params[1].rgb, shade);
}
#endif
)";

constexpr const char* Metal = R"(
#include <metal_stdlib>
using namespace metal;

struct Constants {
    float4x4 transform;
    float4 timing;
    float4 params[3];
};

struct VertexIn {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VertexOut vs_main(VertexIn in [[stage_in]], constant Constants& k [[buffer(1)]])
{
    VertexOut out;
    out.position = k.transform * float4(in.position, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

#ifdef GLOW
fragment float4 ps_main(VertexOut in [[stage_in]], constant Constants& k [[buffer(1)]])
{
    float2 edge = abs(in.uv - 0.5) * 2.0;
    float rim = smoothstep(0.75, 1.0, max(edge.x, edge.y));
    float pulse = 0.65 + 0.35 * sin(k.timing.x * 4.0);
    return float4(k.params[0].rgb * in.color.rgb, rim * pulse * in.color.a);
}
#else
fragment float4 ps_main(VertexOut in [[stage_in]], constant Constants& k [[buffer(1)]])
{
    float2 fromCenter = (in.uv - 0.5) * float2(k.timing.y / k.timing.z, 1.0);
    float shade = smoothstep(0.35, 0.95, length(fromCenter)) * k.params[0].x;
    return float4(k.params[1].rgb, shade);
}
#endif
)";

ShaderSource effect(bool glow)
{
    ShaderSource source;
    std::string define = glow ? "#define GLOW\n" : "";
    source.hlsl = define + Hlsl;
    source.metal = define + Metal;
#ifdef SHADER_DEMO_SPIRV
    source.spirvVertex = {
#include "quad.vert.spv.inc"
    };
    if (glow) {
        source.spirvFragment = {
#include "glow.frag.spv.inc"
        };
    } else {
        source.spirvFragment = {
#include "vignette.frag.spv.inc"
        };
    }
#endif
    source.blend = glow ? ShaderBlend::Premultiplied : ShaderBlend::Alpha;
    return source;
}

}

/**
 * Two custom shaders: a red vignette that deepens as health drops, drawn
 * under the HUD, and a pulsing outline on the block under the crosshair.
 */
class ShaderDemoMod : public Mod {
public:
    ShaderDemoMod()
        : Mod({ .id = "shader_demo", .name = "Shader Demo", .version = "1.0.0", .author = "Kestrel", .description = "Custom shaders on the HUD and in the world" })
    {
    }

    void onEnable() override
    {
        vignette = shaders().create(effect(false));
        glow = shaders().create(effect(true));
        for (const auto& shader : { vignette, glow }) {
            if (!shader->valid()) {
                log().error("shader failed on " + std::string(shaders().backend()) + ": " + shader->error());
            }
        }

        on<HudRenderEvent>([this](HudRenderEvent& event) {
            if (!player().inWorld() || player().maxHealth() <= 0.0f) {
                return;
            }
            float missing = 1.0f - player().health() / player().maxHealth();
            Canvas& canvas = event.canvas;
            canvas.shader(*vignette, { 0.0f, 0.0f, canvas.width(), canvas.height() }, { 0.25f + missing * 0.6f, 0, 0, 0, 0.6f, 0.0f, 0.0f });
        });

        on<WorldRenderEvent>([this](WorldRenderEvent& event) {
            std::optional<TargetBlock> target = world().targetBlock();
            if (!target) {
                return;
            }
            // A hair bigger than the block so the faces do not fight its own.
            constexpr double Grow = 0.002;
            Vec3 min { target->position.x - Grow, target->position.y - Grow, target->position.z - Grow };
            Vec3 max { target->position.x + 1 + Grow, target->position.y + 1 + Grow, target->position.z + 1 + Grow };
            event.painter.box(*glow, min, max, { 0.3f, 0.8f, 1.0f });
        });
    }

private:
    std::shared_ptr<Shader> vignette;
    std::shared_ptr<Shader> glow;
};

KESTREL_MOD(ShaderDemoMod)
