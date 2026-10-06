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
    if ((in.d.y & 0x80000000u) != 0u) {
        for (uint axis = 0; axis < 3; ++axis) {
            int offset = int(((in.d.y >> (axis * 10)) & 1023u) << 22) >> 22;
            local[axis] += float(offset) * (16384.0 / positionScale);
        }
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
    out.light = cornerLight(in.d.x, (in.d.y & 0x80000000u) != 0u ? 0u : in.d.y, corner);
    out.entity = (words[11] & 0x20u) != 0u ? (words[11] >> 5) & 47u : 0u;
    if (out.entity != 0u) {
        out.tint = in.d.w == 0u && (in.d.z & 0x80000000u) != 0u ? (in.d.z & 0x80ffffffu) : 0u;
    }
    if (out.entity != 0u && in.d.w != 0u) {
        float2 uvOffset = float2(as_type<half2>(in.d.z));
        float2 uvScale = float2(as_type<half2>(in.d.w));
        out.uv = uvOffset + out.uv * uvScale;
        out.entity |= 16u;
    }
    return out;
}

struct ActorData {
    float4 previous[3];
    float4 current[3];
    float4 params;
    float4 uv;
    float4 color;
};

float3 actorPose(float3 point, constant ActorData& actor)
{
    float4 p=float4(point,1);
    return mix(float3(dot(actor.previous[0],p),dot(actor.previous[1],p),dot(actor.previous[2],p)),
               float3(dot(actor.current[0],p),dot(actor.current[1],p),dot(actor.current[2],p)),actor.params.x);
}

vertex WorldOut actor_vertex(ModelIn in [[stage_in]], uint vertexId [[vertex_id]], constant DrawData& draw [[buffer(1)]], constant ActorData& actor [[buffer(2)]])
{
    const uint corners[6]={0,1,2,0,2,3};
    const float shades[7]={0.9,0.6,0.6,0.5,1.0,0.8,0.8};
    const float3 normals[7]={float3(0),float3(0,-1,0),float3(0,1,0),float3(-1,0,0),float3(1,0,0),float3(0,0,-1),float3(0,0,1)};
    uint words[12]={in.a.x,in.a.y,in.a.z,in.a.w,in.b.x,in.b.y,in.b.z,in.b.w,in.c.x,in.c.y,in.c.z,in.c.w};
    uint corner=corners[vertexId];
    float3 local;
    for(uint axis=0;axis<3;++axis) {
        uint component=corner*3+axis;
        uint word=words[component/2];
        int value=(component&1)!=0 ? int(word)>>16 : int(word<<16)>>16;
        local[axis]=float(value)/16.0;
    }
    float3 position=draw.origin.xyz+actorPose(local,actor);
    float3 direction=actorPose(normals[(words[11]&15u) < 7u ? words[11]&15u : 0u],actor)-actorPose(float3(0),actor);
    uint axis=0;
    for(uint i=1;i<3;++i) if(abs(direction[i])>abs(direction[axis])) axis=i;
    uint shade=direction[axis]==0 ? 0 : axis*2+(direction[axis]>0 ? 2 : 1);
    uint uvWord=words[6+corner];
    WorldOut out;
    out.position=draw.viewProjection*float4(position,1);
    out.uv=actor.uv.xy+float2(float(uvWord&65535u),float(uvWord>>16))/4096.0*actor.uv.zw;
    out.material=as_type<uint>(actor.params.y);
    out.shade=shades[shade];
    out.relative=position;
    out.tint=0;
    out.light=cornerLight(as_type<uint>(actor.params.w),0,corner);
    out.entity=(as_type<uint>(actor.params.z)>>5)&47;
    if(any(actor.uv!=float4(0,0,1,1))) out.entity|=16;
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

float3 fogWorld(constant DrawData& draw, float3 color, float3 relative, bool additive)
{
    float3 fogColor = additive ? float3(0) : draw.fog.rgb;
    float amount = draw.params.w == 1.0 ? clamp((length(relative) - draw.fog.w) / max(draw.params.x - draw.fog.w, 0.0001), 0.0, 1.0)
        : smoothstep(draw.fog.w, draw.params.x, length(relative));
    if (draw.params.w == 1.0) {
        float3 tint = fogColor;
        for (int i = 0; i < 3; ++i) {
            float c = max(color[i], 0.0f), f = max(tint[i], 0.0f);
            c = c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
            f = f <= 0.04045f ? f / 12.92f : pow((f + 0.055f) / 1.055f, 2.4f);
            c = mix(c, f, amount);
            color[i] = c <= 0.0031308f ? c * 12.92f : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
        }
        return color;
    }
    return mix(color, fogColor, amount);
}

float3 shadeWorld(constant DrawData& draw, float3 rgb, float shade, float3 relative, float3 cornerLevels)
{
    float daylight = max(saturate(draw.params.y), 0.2);
    float channel = max(saturate(cornerLevels.x), saturate(cornerLevels.y) * daylight);
    channel = mix(channel, 1.0, saturate(draw.params.z));
    float light = mix(0.04, 1.0, channel) * saturate(cornerLevels.z);
    return fogWorld(draw, rgb * shade * pow(light, 1.0 / 2.2), relative, false);
}

float4 sampleEntity(texture2d_array<float> entities, texture2d_array<float> entitiesHigh, texture2d_array<float> entities2, texture2d_array<float> entities3, sampler blockSampler, float2 uv, uint material)
{
    uint layer = material & 0x1fffu;
    uint page = layer >> 11;
    uint index = layer & 2047u;
    int2 size = int2(entities.get_width(), entities.get_height());
    uint2 cell = uint2(clamp(int2(floor(uv * float2(size))), int2(0), size - 1));
    if (page == 0u) return entities.read(cell, index, 0);
    if (page == 1u) return entitiesHigh.read(cell, index, 0);
    if (page == 2u) return entities2.read(cell, index, 0);
    return entities3.read(cell, index, 0);
}

constant uint EndPortalLayer = 0x1fffu;
constant int EndPortalLayers = 15;
constant float3 EndPortalBase = float3(0.02, 0.05, 0.06);
constant float3 EndPortalColors[16] = {
    float3(114.0, 108.0, 193.0), float3(105.0, 204.0, 159.0), float3(74.0, 107.0, 213.0), float3(37.0, 196.0, 184.0),
    float3(150.0, 143.0, 184.0), float3(122.0, 219.0, 167.0), float3(144.0, 219.0, 216.0), float3(62.0, 221.0, 139.0),
    float3(32.0, 139.0, 133.0), float3(133.0, 132.0, 182.0), float3(105.0, 140.0, 132.0), float3(42.0, 180.0, 181.0),
    float3(59.0, 203.0, 168.0), float3(43.0, 139.0, 216.0), float3(83.0, 184.0, 132.0), float3(72.0, 141.0, 157.0),
};

float endStar(float2 uv)
{
    float2 cell = floor(fract(uv) * 256.0);
    float seed = fract(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
    if (seed > 0.032) {
        return 0.0;
    }
    return 0.15 + fract(seed * 977.0) * 0.85;
}

float3 endPortalColor(constant DrawData& draw, float3 relative)
{
    float4 clip = draw.viewProjection * float4(relative, 1.0);
    float2 screen = clip.xy / max(abs(clip.w), 0.0001) * 0.5 + 0.5;
    float time = fmod(draw.origin.w, 24000000.0) / 24000.0;
    float3 color = EndPortalBase;
    for (int i = 0; i < EndPortalLayers; ++i) {
        float layer = float(i + 1);
        float angle = (layer * layer * 4321.0 + layer * 9.0) * 2.0 * M_PI_F / 180.0;
        float scale = (4.5 - layer / 4.0) * 2.0;
        float2 shifted = screen * 0.5 + 0.25 + float2(17.0 / layer, (2.0 + layer / 1.5) * time * 1.5);
        float2 turned = float2(cos(angle) * shifted.x - sin(angle) * shifted.y, sin(angle) * shifted.x + cos(angle) * shifted.y) * scale;
        color += endStar(turned) * EndPortalColors[i] / 255.0 * 0.6;
    }
    return min(color, float3(1.0));
}

// The hurt flash color packed into sun.w, or the game's red when none is set.
float4 hitFlash(constant DrawData& draw)
{
    uint hit = as_type<uint>(draw.sun.w);
    return hit == 0u ? float4(1.0, 0.0, 0.0, 0.5) : float4(hit & 255u, (hit >> 8) & 255u, (hit >> 16) & 255u, hit >> 24) / 255.0;
}

bool isEndPortal(WorldOut in)
{
    return in.entity == 0 && (in.material & 0x1fffu) == EndPortalLayer;
}

fragment float4 blend_fragment(WorldOut in [[stage_in]], texture2d_array<float> blocks [[texture(0)]], texture2d_array<float> blocksHigh [[texture(1)]], texture2d_array<float> entities [[texture(2)]], texture2d_array<float> entitiesHigh [[texture(3)]], texture2d_array<float> entities2 [[texture(4)]], texture2d_array<float> entities3 [[texture(5)]], sampler blockSampler [[sampler(0)]], constant DrawData& draw [[buffer(1)]])
{
    if (isEndPortal(in)) {
        return float4(endPortalColor(draw, in.relative), 1.0);
    }
    float4 texel = in.entity != 0 ? applyTint(sampleEntity(entities, entitiesHigh, entities2, entities3, blockSampler, (in.entity & 16u) != 0u ? fract(in.uv) : in.uv, in.material), in.tint) : applyTint(sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv), in.tint);
    if ((in.entity & 8u) != 0u) texel.rgb = shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light);
    else if ((in.entity & 32u) != 0u) texel.rgb = fogWorld(draw, texel.rgb, in.relative, (in.entity & 2u) != 0u);
    if ((in.entity & 4u) != 0u) {
        float4 flash = hitFlash(draw);
        texel.rgb = mix(texel.rgb, flash.rgb, flash.a);
    }
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
    if (isEndPortal(in)) {
        return float4(endPortalColor(draw, in.relative), 1.0);
    }
    float4 texel = in.entity != 0 ? applyTint(sampleEntity(entities, entitiesHigh, entities2, entities3, blockSampler, (in.entity & 16u) != 0u ? fract(in.uv) : in.uv, in.material), in.tint) : applyTint(sampleMaterial(blocks, blocksHigh, blockSampler, draw, in.material, in.uv), in.tint);
    if ((in.entity & 8u) != 0u) texel.rgb = shadeWorld(draw, texel.rgb, in.shade, in.relative, in.light);
    else if ((in.entity & 32u) != 0u) texel.rgb = fogWorld(draw, texel.rgb, in.relative, (in.entity & 2u) != 0u);
    if ((in.entity & 4u) != 0u) {
        float4 flash = hitFlash(draw);
        texel.rgb = mix(texel.rgb, flash.rgb, flash.a);
    }
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
