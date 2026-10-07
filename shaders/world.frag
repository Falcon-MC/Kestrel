#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

layout(set = 0, binding = 0) uniform sampler2DArray blocks;
layout(set = 0, binding = 1) uniform sampler2DArray blocksHigh;
layout(set = 0, binding = 2) uniform sampler2DArray entities;
layout(set = 0, binding = 3) uniform sampler2DArray entitiesHigh;
layout(set = 0, binding = 4) uniform sampler2DArray entities2;
layout(set = 0, binding = 5) uniform sampler2DArray entities3;

layout(location = 0) in vec2 inUv;
layout(location = 1) flat in uint inMaterial;
layout(location = 2) in float inShade;
layout(location = 3) in vec3 inRelative;
layout(location = 4) flat in uint inTint;
layout(location = 5) in vec3 inLight;
layout(location = 6) flat in uint inEntity;
layout(location = 7) flat in vec4 inActorColor;
layout(location = 8) flat in vec4 inActorOverlay;
layout(location = 9) flat in uvec4 inActorTextures;
layout(location = 10) flat in vec4 inActorGrid0;
layout(location = 11) flat in vec4 inActorGrid1;
layout(location = 12) flat in vec4 inActorGrid2;
layout(location = 13) flat in float inActorDissolve;

layout(location = 0) out vec4 outColor;

vec4 sampleLayer(vec2 uv, uint layer)
{
    vec4 low = texture(blocks, vec3(uv, float(min(layer, 2047u))));
    vec4 high = texture(blocksHigh, vec3(uv, float(layer >= 2048u ? layer - 2048u : 0u)));
    return layer >= 2048u ? high : low;
}

/**
 * Entity textures read the nearest texel clamped to the layer, never
 * filtered and never wrapped, so cut-out edges keep a hard border.
 */
vec4 sampleEntity(vec2 uv, uint layer)
{
    uint page = layer >> 11;
    uint index = layer & 2047u;
    ivec2 size = textureSize(entities, 0).xy;
    ivec3 texel = ivec3(clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1), int(index));
    if (page == 0u) return texelFetch(entities, texel, 0);
    if (page == 1u) return texelFetch(entitiesHigh, texel, 0);
    if (page == 2u) return texelFetch(entities2, texel, 0);
    return texelFetch(entities3, texel, 0);
}

vec4 sampleMaterial(uint material, vec2 uv)
{
    uint layer = material & 0x1fffu;
    uint count = ((material >> 15) & 0x3fu) + 1u;
    uint ticksPerFrame = ((material >> 21) & 0x7ffu) + 1u;
    float timeline = draw.origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    vec4 texel = sampleLayer(uv, layer + frame);
    if (count > 1u && ((material >> 14) & 1u) != 0u) {
        vec4 next = sampleLayer(uv, layer + (frame + 1u) % count);
        texel = mix(texel, next, fract(timeline));
    }
    return texel;
}

const uint EndPortalLayer = 0x1fffu;
const int EndPortalLayers = 15;
const vec3 EndPortalBase = vec3(0.02, 0.05, 0.06);
const vec3 EndPortalColors[16] = vec3[16](
    vec3(114.0, 108.0, 193.0), vec3(105.0, 204.0, 159.0), vec3(74.0, 107.0, 213.0), vec3(37.0, 196.0, 184.0),
    vec3(150.0, 143.0, 184.0), vec3(122.0, 219.0, 167.0), vec3(144.0, 219.0, 216.0), vec3(62.0, 221.0, 139.0),
    vec3(32.0, 139.0, 133.0), vec3(133.0, 132.0, 182.0), vec3(105.0, 140.0, 132.0), vec3(42.0, 180.0, 181.0),
    vec3(59.0, 203.0, 168.0), vec3(43.0, 139.0, 216.0), vec3(83.0, 184.0, 132.0), vec3(72.0, 141.0, 157.0));

/**
 * One texel of a 256 texel tile of sparse grey stars, wrapping.
 */
float endStar(vec2 uv)
{
    vec2 cell = floor(fract(uv) * 256.0);
    float seed = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
    if (seed > 0.032) {
        return 0.0;
    }
    return 0.15 + fract(seed * 977.0) * 0.85;
}

/**
 * The end portal surface: a dark base under star layers laid out in screen
 * space, each turned, scaled and drifting on its own, tinted by the palette.
 */
vec3 endPortalColor()
{
    vec4 clip = draw.viewProjection * vec4(inRelative, 1.0);
    vec2 screen = clip.xy / max(abs(clip.w), 0.0001) * 0.5 + 0.5;
    float time = mod(draw.origin.w, 24000000.0) / 24000.0;
    vec3 color = EndPortalBase;
    for (int i = 0; i < EndPortalLayers; ++i) {
        float layer = float(i + 1);
        float angle = radians((layer * layer * 4321.0 + layer * 9.0) * 2.0);
        float scale = (4.5 - layer / 4.0) * 2.0;
        vec2 shifted = screen * 0.5 + 0.25 + vec2(17.0 / layer, (2.0 + layer / 1.5) * time * 1.5);
        vec2 turned = vec2(cos(angle) * shifted.x - sin(angle) * shifted.y, sin(angle) * shifted.x + cos(angle) * shifted.y) * scale;
        color += endStar(turned) * EndPortalColors[i] / 255.0 * 0.6;
    }
    return min(color, vec3(1.0));
}

vec3 fogWorld(vec3 color, bool additive)
{
    vec3 fogColor = additive ? vec3(0.0) : draw.fog.rgb;
    float amount = draw.params.w == 1.0 ? clamp((length(inRelative) - draw.fog.w) / max(draw.params.x - draw.fog.w, 0.0001), 0.0, 1.0)
        : smoothstep(draw.fog.w, draw.params.x, length(inRelative));
    if (draw.params.w == 1.0) {
        vec3 tint = fogColor;
        for (int i = 0; i < 3; ++i) {
            float c = max(color[i], 0.0), f = max(tint[i], 0.0);
            c = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
            f = f <= 0.04045 ? f / 12.92 : pow((f + 0.055) / 1.055, 2.4);
            c = mix(c, f, amount);
            color[i] = c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
        }
        return color;
    }
    return mix(color, fogColor, amount);
}

vec3 shadeWorld(vec3 rgb)
{
    float daylight = max(clamp(draw.params.y, 0.0, 1.0), 0.2);
    float channel = max(clamp(inLight.x, 0.0, 1.0), clamp(inLight.y, 0.0, 1.0) * daylight);
    channel = mix(channel, 1.0, clamp(draw.params.z, 0.0, 1.0));
    float light = mix(0.04, 1.0, channel) * clamp(inLight.z, 0.0, 1.0);
    return fogWorld(rgb * inShade * pow(light, 1.0 / 2.2), false);
}

vec4 applyTint(vec4 texel, uint tint)
{
    if ((tint & 0x80000000u) == 0u) {
        return texel;
    }
    vec3 color = vec3(float((tint >> 16) & 0xffu), float((tint >> 8) & 0xffu), float(tint & 0xffu)) / 255.0;
    if ((tint & 0x40000000u) != 0u) {
        // Overlay alpha selects the tinted region, not surface coverage.
        return vec4(mix(texel.rgb, texel.rgb * color, texel.a), 1.0);
    }
    return vec4(texel.rgb * color, texel.a);
}

vec4 actorTexture(uint layer, vec4 grid, vec2 uv)
{
    vec2 coordinate = clamp(uv, vec2(0), vec2(0.999999)) * grid.xy * grid.zw;
    uvec2 tile = uvec2(floor(coordinate));
    return sampleEntity(fract(coordinate), layer + tile.y * uint(grid.x) + tile.x);
}

vec4 actorSurface(vec2 uv)
{
    vec4 texel = actorTexture(inActorTextures.x, inActorGrid0, uv);
    uint mode = inActorTextures.w;
    if (mode == 2u && texel.a * inActorDissolve < 0.5) discard;
    if ((mode == 3u || mode == 6u) && texel.a < 0.5) discard;
    if (mode == 1u && all(equal(texel, vec4(0)))) discard;
    if (mode == 0u && texel.a < 0.1 && (inEntity & 128u) == 0u) discard;
    if (mode == 4u) texel.rgb = mix(texel.rgb, texel.rgb * inActorColor.rgb, texel.a);
    else if (mode != 5u) texel.rgb *= inActorColor.rgb;
    texel.a *= inActorColor.a;
    if (mode == 5u && inActorTextures.y != 0xffffffffu && inActorTextures.z != 0xffffffffu) {
        vec4 second = actorTexture(inActorTextures.y, inActorGrid1, uv);
        vec4 third = actorTexture(inActorTextures.z, inActorGrid2, uv);
        texel.rgb = mix(mix(texel.rgb, second.rgb, second.a), third.rgb, third.a);
    }
    if (mode != 3u) texel.rgb = mix(texel.rgb, inActorOverlay.rgb, inActorOverlay.a);
    vec3 unlit = fogWorld(texel.rgb, (inEntity & 2u) != 0u);
    vec3 lit = (inEntity & 8u) != 0u ? shadeWorld(texel.rgb) : unlit;
    texel.rgb = mode == 1u ? mix(unlit, lit, texel.a) : lit;
    if (mode != 0u) texel.a = 1.0;
    return texel;
}

void main()
{
#ifdef OVERLAY
    // Outline edges darken what is under them like 40% black, cracks multiply it twice over.
    if (inEntity != 0u) {
        outColor = vec4(0.3, 0.3, 0.3, 1.0);
        return;
    }
    vec4 crack = sampleMaterial(inMaterial, inUv);
    if (crack.a < 0.5) {
        discard;
    }
    outColor = vec4(crack.rgb, 1.0);
#else
    if (inEntity == 0u && (inMaterial & 0x1fffu) == EndPortalLayer) {
        outColor = vec4(endPortalColor(), 1.0);
        return;
    }
    if ((inEntity & 64u) != 0u) {
        vec4 actor = actorSurface((inEntity & 16u) != 0u ? fract(inUv) : inUv);
#ifdef BLEND
        if (actor.a < 0.004) discard;
        outColor = vec4(actor.rgb * actor.a, (inEntity & 2u) != 0u ? 0.0 : actor.a);
#else
        outColor = actor;
#endif
        return;
    }
    vec4 texel = inEntity != 0u ? applyTint(sampleEntity((inEntity & 16u) != 0u ? fract(inUv) : inUv, inMaterial & 0x1fffu), inTint) : applyTint(sampleMaterial(inMaterial, inUv), inTint);
    if ((inEntity & 512u) != 0u && texel.a < 0.5) discard;
    if ((inEntity & 256u) != 0u) texel.rgb *= inLight.z;
    if ((inEntity & 8u) != 0u) texel.rgb = shadeWorld(texel.rgb);
    else if ((inEntity & 32u) != 0u) texel.rgb = fogWorld(texel.rgb, (inEntity & 2u) != 0u);
    if ((inEntity & 4u) != 0u) {
        uint hit = floatBitsToUint(draw.sun.w);
        vec4 flash = hit == 0u ? vec4(1.0, 0.0, 0.0, 0.5) : unpackUnorm4x8(hit);
        texel.rgb = mix(texel.rgb, flash.rgb, flash.a);
    }
#ifdef BLEND
    if (texel.a < 0.004) {
        discard;
    }
    if (inEntity != 0u) {
        outColor = vec4(texel.rgb * texel.a, (inEntity & 2u) != 0u ? 0.0 : texel.a);
        return;
    }
    outColor = vec4(shadeWorld(texel.rgb) * texel.a, texel.a);
#else
    if (inEntity != 0u) {
        if (texel.a < 0.1) {
            discard;
        }
        outColor = texel;
        return;
    }
    if (texel.a < 0.5) {
        discard;
    }
    outColor = vec4(shadeWorld(texel.rgb), 1.0);
#endif
#endif
}
