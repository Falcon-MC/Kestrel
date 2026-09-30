#pragma once

namespace kestrel::metal {

inline constexpr char UiShader[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
    float2 local [[attribute(3)]];
    float2 halfSize [[attribute(4)]];
    float2 shape [[attribute(5)]];
    float depth [[attribute(6)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 local;
    float2 halfSize;
    float2 shape;
};

vertex VertexOut ui_vertex(VertexIn in [[stage_in]], constant float2& viewport [[buffer(1)]])
{
    VertexOut out;
    out.position = float4(in.position.x / viewport.x * 2.0 - 1.0, 1.0 - in.position.y / viewport.y * 2.0, in.depth, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    out.local = in.local;
    out.halfSize = in.halfSize;
    out.shape = in.shape;
    return out;
}

fragment float4 ui_fragment(VertexOut in [[stage_in]], texture2d<float> atlas [[texture(0)]], sampler atlasSampler [[sampler(0)]])
{
    float4 texel = atlas.sample(atlasSampler, in.uv);
    float coverage = texel.a;
    if (in.shape.x >= 0.0) {
        float radius = in.shape.x;
        float2 q = abs(in.local) - in.halfSize + radius;
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
        coverage *= saturate(0.5 - distance / max(in.shape.y, 1.0));
    }
    return float4(in.color.rgb * texel.rgb, in.color.a * coverage);
}
)";

inline constexpr char WorldShader[] = R"(
#include <metal_stdlib>
using namespace metal;

struct WorldIn {
    uint4 quad [[attribute(0)]];
    uint ao [[attribute(1)]];
};

struct DrawData {
    float4x4 viewProjection;
    float4 origin;
    float4 fog;
    float4 params;
    float4 sun;
};

struct WorldOut {
    float4 position [[position]];
    float2 uv;
    uint material [[flat]];
    float shade;
    float3 relative;
    uint tint [[flat]];
    float3 light;
    uint entity [[flat]];
};

constant float lightCurve[16] = {
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

float4 applyTint(float4 texel, uint tint)
{
    if ((tint & 0x80000000u) == 0) {
        return texel;
    }
    float3 color = float3(float((tint >> 16) & 0xff), float((tint >> 8) & 0xff), float(tint & 0xff)) / 255.0;
    if ((tint & 0x40000000u) != 0) {
        return float4(mix(texel.rgb, texel.rgb * color, texel.a), 1.0);
    }
    return float4(texel.rgb * color, texel.a);
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

vertex WorldOut world_vertex(WorldIn in [[stage_in]], uint vertexId [[vertex_id]], constant DrawData& draw [[buffer(1)]])
{
    const uint cornerOrder[6] = { 0, 1, 2, 0, 2, 3 };
    const float faceShade[6] = { 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    uint geometry = in.quad.x;
    float3 localOrigin = float3(geometry & 0x1f, (geometry >> 5) & 0x1f, (geometry >> 10) & 0x1f);
    uint face = (geometry >> 15) & 0x7;
    float width = float(((geometry >> 18) & 0xf) + 1);
    float height = float(((geometry >> 22) & 0xf) + 1);
    uint corner = cornerOrder[vertexId];

    WorldOut out;
    float3 position = draw.origin.xyz + quadCorner(face, corner, localOrigin, width, height);
    out.position = draw.viewProjection * float4(position, 1.0);
    out.uv = greedyUv(face, corner, width, height, (in.quad.y >> 12) & 1);
    out.material = in.quad.y;
    out.shade = faceShade[face];
    out.relative = position;
    out.tint = in.quad.z;
    out.light = cornerLight(in.quad.w, in.ao, corner);
    out.entity = 0;
    return out;
}

struct ModelIn {
    uint4 a [[attribute(0)]];
    uint4 b [[attribute(1)]];
    uint4 c [[attribute(2)]];
    uint4 d [[attribute(3)]];
};

WorldOut placeModel(ModelIn in, uint vertexId, constant DrawData& draw, float positionScale)
{
    const uint cornerOrder[6] = { 0, 1, 2, 0, 2, 3 };
    const float faceShade[7] = { 0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    uint words[12] = { in.a.x, in.a.y, in.a.z, in.a.w, in.b.x, in.b.y, in.b.z, in.b.w, in.c.x, in.c.y, in.c.z, in.c.w };
    uint corner = cornerOrder[vertexId];
    float3 local;
    for (uint i = 0; i < 3; ++i) {
        uint component = corner * 3 + i;
        uint word = words[component / 2];
        int value = (component & 1) != 0 ? (int(word) >> 16) : (int(word << 16) >> 16);
        local[i] = float(value) / positionScale;
    }
    uint uvWord = words[6 + corner];

    WorldOut out;
    float3 position = draw.origin.xyz + local;
    out.position = draw.viewProjection * float4(position, 1.0);
    out.uv = float2(float(uvWord & 0xffff), float(uvWord >> 16)) / 4096.0;
    if ((words[11] & 0x10u) != 0) {
        out.uv.y -= fract(draw.origin.w / 32.0);
    }
    out.material = words[10];
    out.shade = faceShade[min(words[11] & 0xfu, 6u)];
    out.relative = position;
    uint rgb = words[11] >> 8;
    out.tint = rgb != 0 ? (0x80000000u | rgb) : 0u;
    out.light = cornerLight(in.d.x, in.d.y, corner);
    out.entity = (words[11] & 0x20u) != 0u ? (words[11] >> 5) & 15u : 0u;
    return out;
}

vertex WorldOut model_vertex(ModelIn in [[stage_in]], uint vertexId [[vertex_id]], constant DrawData& draw [[buffer(1)]])
{
    return placeModel(in, vertexId, draw, 256.0);
}

vertex WorldOut overlay_vertex(ModelIn in [[stage_in]], uint vertexId [[vertex_id]], constant DrawData& draw [[buffer(1)]])
{
    return placeModel(in, vertexId, draw, 1024.0);
}

float4 sampleLayer(texture2d_array<float> blocks, texture2d_array<float> blocksHigh, sampler blockSampler, float2 uv, uint layer)
{
    float4 low = blocks.sample(blockSampler, uv, min(layer, 2047u));
    float4 high = blocksHigh.sample(blockSampler, uv, layer >= 2048u ? layer - 2048u : 0u);
    return layer >= 2048u ? high : low;
}

float4 sampleMaterial(texture2d_array<float> blocks, texture2d_array<float> blocksHigh, sampler blockSampler, constant DrawData& draw, uint material, float2 uv)
{
    uint layer = material & 0x1fff;
    uint count = ((material >> 15) & 0x3f) + 1;
    uint ticksPerFrame = ((material >> 21) & 0x7ff) + 1;
    float timeline = draw.origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    float4 texel = sampleLayer(blocks, blocksHigh, blockSampler, uv, layer + frame);
    if (count > 1 && ((material >> 14) & 1) != 0) {
        float4 next = sampleLayer(blocks, blocksHigh, blockSampler, uv, layer + (frame + 1) % count);
        texel = mix(texel, next, fract(timeline));
    }
    return texel;
}

float3 shadeWorld(constant DrawData& draw, float3 rgb, float shade, float3 relative, float3 cornerLevels)
{
    float daylight = max(saturate(draw.params.y), 0.2);
    float channel = max(saturate(cornerLevels.x), saturate(cornerLevels.y) * daylight);
    channel = mix(channel, 1.0, saturate(draw.params.z));
    float light = mix(0.04, 1.0, channel) * saturate(cornerLevels.z);
    float3 color = rgb * shade * pow(light, 1.0 / 2.2);
    float amount = smoothstep(draw.fog.w, draw.params.x, length(relative));
    return mix(color, draw.fog.rgb, amount);
}

float4 sampleEntity(texture2d_array<float> entities, texture2d_array<float> entitiesHigh, texture2d_array<float> entities2, texture2d_array<float> entities3, sampler blockSampler, float2 uv, uint material)
{
    uint layer = material & 0x1fffu;
    uint page = layer >> 11;
    uint index = layer & 2047u;
    if (page == 0u) return entities.sample(blockSampler, uv, index);
    if (page == 1u) return entitiesHigh.sample(blockSampler, uv, index);
    if (page == 2u) return entities2.sample(blockSampler, uv, index);
    return entities3.sample(blockSampler, uv, index);
}

fragment float4 blend_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], texture2d_array<float> blocksHigh [[texture(1)]], texture2d_array<float> entities [[texture(2)]], texture2d_array<float> entitiesHigh [[texture(3)]], texture2d_array<float> entities2 [[texture(4)]], texture2d_array<float> entities3 [[texture(5)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 texel = in.entity != 0 ? sampleEntity(entities, entitiesHigh, entities2, entities3, blockSampler, in.uv, in.material) : applyTint(sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv), in.tint);
    if ((in.entity & 8u) != 0u) texel.rgb = shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light);
    if ((in.entity & 4u) != 0u) texel.rgb = mix(texel.rgb, float3(1.0, 0.0, 0.0), 0.5);
    if (texel.a < 0.004) {
        discard_fragment();
    }
    if (in.entity != 0) {
        return float4(texel.rgb * texel.a, (in.entity & 2) != 0 ? 0.0 : texel.a);
    }
    return float4(shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light) * texel.a, texel.a);
}

struct SkyIn {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    uint layer [[attribute(2)]];
    uint color [[attribute(3)]];
    uint flags [[attribute(4)]];
};

struct SkyOut {
    float4 position [[position]];
    float2 uv;
    uint layer [[flat]];
    float4 color;
    uint flags [[flat]];
    float3 relative;
};

vertex SkyOut sky_vertex(SkyIn in [[stage_in]], constant DrawData& draw [[buffer(1)]])
{
    SkyOut out;
    float3 position = draw.origin.xyz + in.position;
    out.position = draw.viewProjection * float4(position, 1.0);
    out.uv = in.uv;
    out.layer = in.layer;
    out.color = float4(float(in.color & 0xff), float((in.color >> 8) & 0xff), float((in.color >> 16) & 0xff), float(in.color >> 24)) / 255.0;
    out.flags = in.flags;
    out.relative = position;
    return out;
}

fragment float4 sky_fragment(SkyOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 color = in.color;
    if ((in.flags & 1) != 0) {
        float4 texel = blocks.sample(blockSampler, in.uv, in.layer, level(0.0));
        color.rgb *= texel.rgb;
        if ((in.flags & 2) == 0) {
            color.a *= texel.a;
        }
    }
    if ((in.flags & 2) != 0) {
        return float4(color.rgb * color.a, 0.0);
    }
    return float4(color.rgb * color.a, color.a);
}

fragment float4 overlay_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], texture2d_array<float> blocksHigh [[texture(1)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    if (in.entity != 0) {
        return float4(0.3, 0.3, 0.3, 1.0);
    }
    float4 crack = sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv);
    if (crack.a < 0.5) {
        discard_fragment();
    }
    return float4(crack.rgb, 1.0);
}

fragment float4 world_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], texture2d_array<float> blocksHigh [[texture(1)]], texture2d_array<float> entities [[texture(2)]], texture2d_array<float> entitiesHigh [[texture(3)]], texture2d_array<float> entities2 [[texture(4)]], texture2d_array<float> entities3 [[texture(5)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    float4 texel = in.entity != 0 ? sampleEntity(entities, entitiesHigh, entities2, entities3, blockSampler, in.uv, in.material) : applyTint(sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv), in.tint);
    if ((in.entity & 8u) != 0u) texel.rgb = shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light);
    if ((in.entity & 4u) != 0u) texel.rgb = mix(texel.rgb, float3(1.0, 0.0, 0.0), 0.5);
    if (in.entity != 0) {
        if (texel.a < 0.1) {
            discard_fragment();
        }
        return texel;
    }
    if (texel.a < 0.5) {
        discard_fragment();
    }
    return float4(shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light), 1.0);
}
)";

}
