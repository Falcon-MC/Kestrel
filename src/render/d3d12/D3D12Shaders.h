#pragma once

namespace kestrel::d3d12 {

inline constexpr char UiShader[] = R"(
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
    float depth : TEXCOORD4;
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
    output.position = float4(input.position.x / viewport.x * 2.0 - 1.0, 1.0 - input.position.y / viewport.y * 2.0, input.depth, 1.0);
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

inline constexpr char WorldShader[] = R"(
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
Texture2DArray entities : register(t2);
Texture2DArray entitiesHigh : register(t3);
Texture2DArray entities2 : register(t4);
Texture2DArray entities3 : register(t5);
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
    nointerpolation uint entity : TEXCOORD6;
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
    output.entity = 0;
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

WorldOut placeModel(ModelIn input, float positionScale)
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
        local[i] = float(value) / positionScale;
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
    output.entity = (words[11] & 0x20) != 0 ? (words[11] >> 5) & 15 : 0;
    return output;
}

WorldOut vs_model(ModelIn input)
{
    return placeModel(input, 256.0);
}

WorldOut vs_overlay(ModelIn input)
{
    return placeModel(input, 1024.0);
}

float4 sampleLayer(float2 uv, uint layer)
{
    float4 low = blocks.Sample(blockSampler, float3(uv, min(layer, 2047u)));
    float4 high = blocksHigh.Sample(blockSampler, float3(uv, layer >= 2048u ? layer - 2048u : 0u));
    return layer >= 2048u ? high : low;
}

float4 sampleMaterial(uint material, float2 uv)
{
    uint layer = material & 0x1fff;
    uint count = ((material >> 15) & 0x3f) + 1;
    uint ticksPerFrame = ((material >> 21) & 0x7ff) + 1;
    float timeline = origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    float4 texel = sampleLayer(uv, layer + frame);
    if (count > 1 && ((material >> 14) & 1) != 0) {
        float4 next = sampleLayer(uv, layer + (frame + 1) % count);
        texel = lerp(texel, next, frac(timeline));
    }
    return texel;
}

float3 shadeWorld(float3 rgb, float shade, float3 relative, float3 cornerLevels)
{
    float daylight = max(saturate(params.y), 0.2);
    float channel = max(saturate(cornerLevels.x), saturate(cornerLevels.y) * daylight);
    channel = lerp(channel, 1.0, saturate(params.z));
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

float4 surfaceTexel(WorldOut input)
{
    if (input.entity != 0) {
        uint layer = input.material & 0x1fff;
        uint page = layer >> 11;
        uint index = layer & 2047u;
        float4 texel = page == 0 ? entities.Sample(blockSampler, float3(input.uv, index))
            : page == 1 ? entitiesHigh.Sample(blockSampler, float3(input.uv, index))
            : page == 2 ? entities2.Sample(blockSampler, float3(input.uv, index))
            : entities3.Sample(blockSampler, float3(input.uv, index));
        if ((input.entity & 8) != 0) texel.rgb = shadeWorld(texel.rgb, input.shade, input.relative, input.light);
        if ((input.entity & 4) != 0) texel.rgb = lerp(texel.rgb, float3(1.0, 0.0, 0.0), 0.5);
        return texel;
    }
    return applyTint(sampleMaterial(input.material, input.uv), input.tint);
}

float4 ps_world(WorldOut input) : SV_Target
{
    float4 texel = surfaceTexel(input);
    if (input.entity != 0) {
        if (texel.a < 0.1) {
            discard;
        }
        return texel;
    }
    if (texel.a < 0.5) {
        discard;
    }
    return float4(shadeWorld(texel.rgb, input.shade, input.relative, input.light), 1.0);
}

float4 ps_blend(WorldOut input) : SV_Target
{
    float4 texel = surfaceTexel(input);
    if (texel.a < 0.004) {
        discard;
    }
    if (input.entity != 0) {
        return float4(texel.rgb * texel.a, (input.entity & 2) != 0 ? 0.0 : texel.a);
    }
    return float4(shadeWorld(texel.rgb, input.shade, input.relative, input.light) * texel.a, texel.a);
}

float4 ps_overlay(WorldOut input) : SV_Target
{
    if (input.entity != 0) {
        return float4(0.3, 0.3, 0.3, 1.0);
    }
    float4 crack = sampleMaterial(input.material, input.uv);
    if (crack.a < 0.5) {
        discard;
    }
    return float4(crack.rgb, 1.0);
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

}
