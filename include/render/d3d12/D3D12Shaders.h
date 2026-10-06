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
    nointerpolation float4 actorColor : TEXCOORD7;
    nointerpolation float4 actorOverlay : TEXCOORD8;
    nointerpolation uint4 actorTextures : TEXCOORD9;
    nointerpolation float4 actorGrid0 : TEXCOORD10;
    nointerpolation float4 actorGrid1 : TEXCOORD11;
    nointerpolation float4 actorGrid2 : TEXCOORD12;
    nointerpolation float actorDissolve : TEXCOORD13;
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

    WorldOut output = (WorldOut)0;
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
    uint words[12] = { input.a.x, input.a.y, input.a.z, input.a.w, input.b.x, input.b.y, input.b.z, input.b.w, input.c.x, input.c.y, input.c.z, input.c.w };
    uint corner = cornerOrder[input.vertexId];
    float3 local;
    for (uint i = 0; i < 3; ++i) {
        uint component = corner * 3 + i;
        uint word = words[component / 2];
        int value = (component & 1) != 0 ? (int(word) >> 16) : (int(word << 16) >> 16);
        local[i] = float(value) / positionScale;
    }
    if ((input.d.y & 0x80000000u) != 0) {
        for (uint axis = 0; axis < 3; ++axis) {
            int offset = int(((input.d.y >> (axis * 10)) & 1023u) << 22) >> 22;
            local[axis] += float(offset) * (16384.0 / positionScale);
        }
    }
    uint uvWord = words[6 + corner];

    WorldOut output = (WorldOut)0;
    float3 position = origin.xyz + local;
    output.position = mul(viewProjection, float4(position, 1.0));
    output.uv = float2(uvWord & 0xffff, uvWord >> 16) / 4096.0;
    if ((words[11] & 0x10) != 0) {
        output.uv.y -= frac(origin.w / 32.0);
    }
    output.material = words[10];
    static const float faceShade[7] = { 0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8 };
    output.shade = faceShade[min(words[11] & 0xf, 6u)];
    output.relative = position;
    uint rgb = words[11] >> 8;
    output.tint = rgb != 0 ? (0x80000000 | rgb) : 0;
    output.light = cornerLight(input.d.x, (input.d.y & 0x80000000u) != 0 ? 0u : input.d.y, corner);
    output.entity = (words[11] & 0x20) != 0 ? (words[11] >> 5) & 47 : 0;
    if (output.entity != 0) {
        output.tint = input.d.w == 0 && (input.d.z & 0x80000000u) != 0 ? (input.d.z & 0xc0ffffffu) : 0;
    }
    if (output.entity != 0 && input.d.w != 0) {
        float2 uvOffset = float2(f16tof32(input.d.z), f16tof32(input.d.z >> 16));
        float2 uvScale = float2(f16tof32(input.d.w), f16tof32(input.d.w >> 16));
        output.uv = uvOffset + output.uv * uvScale;
        output.entity |= 16;
    }
    return output;
}

cbuffer ActorData : register(b1)
{
    float4 actorPrevious[3];
    float4 actorCurrent[3];
    float4 actorParams;
    float4 actorUv;
    float4 actorColor;
    float4 actorOverlay;
    float4 actorTextures;
    float4 actorGrid0;
    float4 actorGrid1;
    float4 actorGrid2;
    float4 actorOptions;
};

float3 actorPose(float3 position)
{
    float4 p = float4(position, 1);
    return lerp(float3(dot(actorPrevious[0],p),dot(actorPrevious[1],p),dot(actorPrevious[2],p)),
                float3(dot(actorCurrent[0],p),dot(actorCurrent[1],p),dot(actorCurrent[2],p)), actorParams.x);
}

WorldOut vs_actor(ModelIn input)
{
    static const uint corners[6] = {0,1,2,0,2,3};
    static const float shades[7] = {0.9,0.6,0.6,0.5,1.0,0.8,0.8};
    uint words[12] = {input.a.x,input.a.y,input.a.z,input.a.w,input.b.x,input.b.y,input.b.z,input.b.w,input.c.x,input.c.y,input.c.z,input.c.w};
    uint corner = corners[input.vertexId];
    float3 local;
    for (uint axis=0;axis<3;++axis) {
        uint component=corner*3+axis;
        uint word=words[component/2];
        int value=(component & 1) != 0 ? int(word)>>16 : int(word<<16)>>16;
        local[axis]=float(value)/16.0;
    }
    float3 position=origin.xyz+actorPose(local);
    float3 normal = asfloat(input.d.xyz);
    float3 direction = actorPose(normal) - actorPose(float3(0,0,0));
    direction = length(direction) > 0 ? normalize(direction) : float3(0,1,0);
    float shade = (1+direction.y)*0.275-direction.x*direction.x*0.1+direction.z*direction.z*0.1+0.45+actorOverlay.a*0.35;
    uint uvWord=words[6+corner];
    WorldOut output = (WorldOut)0;
    output.position=mul(viewProjection,float4(position,1));
    output.uv=actorUv.xy+float2(uvWord&65535u,uvWord>>16)/4096.0*actorUv.zw;
    output.material=asuint(actorParams.y);
    output.shade=shade;
    output.relative=position;
    output.tint=0;
    output.light=cornerLight(asuint(actorParams.w), 0u, corner);
    output.entity=(asuint(actorParams.z)>>5)&255;
    output.actorColor=actorColor;
    output.actorOverlay=actorOverlay;
    output.actorTextures=asuint(actorTextures);
    output.actorGrid0=actorGrid0; output.actorGrid1=actorGrid1; output.actorGrid2=actorGrid2;
    output.actorDissolve=actorOptions.w;
    if (any(actorUv != float4(0,0,1,1))) output.entity|=16;
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

float3 fogWorld(float3 color, float3 relative, bool additive)
{
    float3 fogColor = additive ? float3(0, 0, 0) : fog.rgb;
    float amount = params.w == 1.0 ? saturate((length(relative) - fog.w) / max(params.x - fog.w, 0.0001))
        : smoothstep(fog.w, params.x, length(relative));
    if (params.w == 1.0) {
        float3 tint = fogColor;
        for (int i = 0; i < 3; ++i) {
            float c = max(color[i], 0.0);
            float f = max(tint[i], 0.0);
            c = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
            f = f <= 0.04045 ? f / 12.92 : pow((f + 0.055) / 1.055, 2.4);
            c = lerp(c, f, amount);
            color[i] = c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
        }
        return color;
    }
    return lerp(color, fogColor, amount);
}

float3 shadeWorld(float3 rgb, float shade, float3 relative, float3 cornerLevels)
{
    float daylight = max(saturate(params.y), 0.2);
    float channel = max(saturate(cornerLevels.x), saturate(cornerLevels.y) * daylight);
    channel = lerp(channel, 1.0, saturate(params.z));
    float light = lerp(0.04, 1.0, channel) * saturate(cornerLevels.z);
    return fogWorld(rgb * shade * pow(light, 1.0 / 2.2), relative, false);
}

float4 applyTint(float4 texel, uint tint)
{
    if ((tint & 0x80000000) == 0) {
        return texel;
    }
    float3 color = float3((tint >> 16) & 0xff, (tint >> 8) & 0xff, tint & 0xff) / 255.0;
    if ((tint & 0x40000000) != 0) {
        return float4(lerp(texel.rgb, texel.rgb * color, texel.a), texel.a > 0.0 ? 1.0 : 0.0);
    }
    return float4(texel.rgb * color, texel.a);
}

static const uint EndPortalLayer = 0x1fffu;
static const int EndPortalLayers = 15;
static const float3 EndPortalBase = float3(0.02, 0.05, 0.06);
static const float3 EndPortalColors[16] = {
    float3(114.0, 108.0, 193.0), float3(105.0, 204.0, 159.0), float3(74.0, 107.0, 213.0), float3(37.0, 196.0, 184.0),
    float3(150.0, 143.0, 184.0), float3(122.0, 219.0, 167.0), float3(144.0, 219.0, 216.0), float3(62.0, 221.0, 139.0),
    float3(32.0, 139.0, 133.0), float3(133.0, 132.0, 182.0), float3(105.0, 140.0, 132.0), float3(42.0, 180.0, 181.0),
    float3(59.0, 203.0, 168.0), float3(43.0, 139.0, 216.0), float3(83.0, 184.0, 132.0), float3(72.0, 141.0, 157.0),
};

float endStar(float2 uv)
{
    float2 cell = floor(frac(uv) * 256.0);
    float seed = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
    if (seed > 0.032) {
        return 0.0;
    }
    return 0.15 + frac(seed * 977.0) * 0.85;
}

float3 endPortalColor(float3 relative)
{
    float4 clip = mul(viewProjection, float4(relative, 1.0));
    float2 screen = clip.xy / max(abs(clip.w), 0.0001) * 0.5 + 0.5;
    float time = fmod(origin.w, 24000000.0) / 24000.0;
    float3 color = EndPortalBase;
    for (int i = 0; i < EndPortalLayers; ++i) {
        float layer = float(i + 1);
        float angle = radians((layer * layer * 4321.0 + layer * 9.0) * 2.0);
        float scale = (4.5 - layer / 4.0) * 2.0;
        float2 shifted = screen * 0.5 + 0.25 + float2(17.0 / layer, (2.0 + layer / 1.5) * time * 1.5);
        float2 turned = float2(cos(angle) * shifted.x - sin(angle) * shifted.y, sin(angle) * shifted.x + cos(angle) * shifted.y) * scale;
        color += endStar(turned) * EndPortalColors[i] / 255.0 * 0.6;
    }
    return min(color, float3(1.0, 1.0, 1.0));
}

bool isEndPortal(WorldOut input)
{
    return input.entity == 0 && (input.material & 0x1fffu) == EndPortalLayer;
}

float4 sampleEntity(uint layer, float2 uv)
{
    uint width, height, layers, levels;
    entities.GetDimensions(0, width, height, layers, levels);
    int2 cell = clamp(int2(floor(uv * float2(width,height))), int2(0,0), int2(width,height)-1);
    int4 at = int4(cell, layer & 2047u, 0);
    uint page = layer >> 11;
    return page == 0u ? entities.Load(at) : page == 1u ? entitiesHigh.Load(at) : page == 2u ? entities2.Load(at) : entities3.Load(at);
}

float4 actorTexture(uint layer, float4 grid, float2 uv)
{
    float2 coordinate = clamp(uv, float2(0,0), float2(0.999999,0.999999)) * grid.xy * grid.zw;
    uint2 tile = uint2(floor(coordinate));
    return sampleEntity(layer + tile.y * uint(grid.x) + tile.x, frac(coordinate));
}

float4 actorSurface(WorldOut input, float2 uv)
{
    float4 texel = actorTexture(input.actorTextures.x, input.actorGrid0, uv);
    uint mode = input.actorTextures.w;
    if (mode == 2u && texel.a * input.actorDissolve < 0.5) discard;
    if (mode == 3u && texel.a < 0.5) discard;
    if (mode == 1u && all(texel == float4(0,0,0,0))) discard;
    if (mode == 0u && texel.a < 0.1 && (input.entity & 128u) == 0u) discard;
    if (mode == 4u) texel.rgb = lerp(texel.rgb, texel.rgb * input.actorColor.rgb, texel.a);
    else if (mode != 5u) texel.rgb *= input.actorColor.rgb;
    texel.a *= input.actorColor.a;
    if (mode == 5u && input.actorTextures.y != 0xffffffffu && input.actorTextures.z != 0xffffffffu) {
        float4 second = actorTexture(input.actorTextures.y, input.actorGrid1, uv);
        float4 third = actorTexture(input.actorTextures.z, input.actorGrid2, uv);
        texel.rgb = lerp(lerp(texel.rgb, second.rgb, second.a), third.rgb, third.a);
    }
    if (mode != 3u) texel.rgb = lerp(texel.rgb, input.actorOverlay.rgb, input.actorOverlay.a);
    float3 unlit = fogWorld(texel.rgb, input.relative, (input.entity & 2u) != 0u);
    float3 lit = (input.entity & 8u) != 0u ? shadeWorld(texel.rgb, input.shade, input.relative, input.light) : unlit;
    texel.rgb = mode == 1u ? lerp(unlit, lit, texel.a) : lit;
    if (mode != 0u) texel.a = 1.0;
    return texel;
}

float4 surfaceTexel(WorldOut input)
{
    if ((input.entity & 64u) != 0u) return actorSurface(input, (input.entity & 16u) != 0u ? frac(input.uv) : input.uv);
    if (input.entity != 0) {
        uint layer = input.material & 0x1fff;
        uint page = layer >> 11;
        uint index = layer & 2047u;
        float2 uv = (input.entity & 16) != 0 ? frac(input.uv) : input.uv;
        uint width, height, elements, levels;
        entities.GetDimensions(0, width, height, elements, levels);
        int2 cell = clamp(int2(floor(uv * float2(width, height))), int2(0, 0), int2(width, height) - 1);
        int4 at = int4(cell, index, 0);
        float4 texel = page == 0 ? entities.Load(at)
            : page == 1 ? entitiesHigh.Load(at)
            : page == 2 ? entities2.Load(at)
            : entities3.Load(at);
        if ((input.entity & 8) != 0) texel.rgb = shadeWorld(texel.rgb, input.shade, input.relative, input.light);
        else if ((input.entity & 32) != 0) texel.rgb = fogWorld(texel.rgb, input.relative, (input.entity & 2) != 0);
        if ((input.entity & 4) != 0) {
            uint hit = asuint(sun.w);
            float4 flash = hit == 0 ? float4(1.0, 0.0, 0.0, 0.5) : float4(hit & 255, (hit >> 8) & 255, (hit >> 16) & 255, hit >> 24) / 255.0;
            texel.rgb = lerp(texel.rgb, flash.rgb, flash.a);
        }
        return applyTint(texel, input.tint);
    }
    return applyTint(sampleMaterial(input.material, input.uv), input.tint);
}

float4 ps_world(WorldOut input) : SV_Target
{
    if (isEndPortal(input)) {
        return float4(endPortalColor(input.relative), 1.0);
    }
    float4 texel = surfaceTexel(input);
    if (input.entity != 0) {
        if ((input.entity & 64u) == 0u && texel.a < 0.1) {
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
    if (isEndPortal(input)) {
        return float4(endPortalColor(input.relative), 1.0);
    }
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
