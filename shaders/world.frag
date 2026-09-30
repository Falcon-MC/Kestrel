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

layout(location = 0) out vec4 outColor;

vec4 sampleLayer(vec2 uv, uint layer)
{
    vec4 low = texture(blocks, vec3(uv, float(min(layer, 2047u))));
    vec4 high = texture(blocksHigh, vec3(uv, float(layer >= 2048u ? layer - 2048u : 0u)));
    return layer >= 2048u ? high : low;
}

vec4 sampleEntity(vec2 uv, uint layer)
{
    uint page = layer >> 11;
    uint index = layer & 2047u;
    if (page == 0u) return texture(entities, vec3(uv, float(index)));
    if (page == 1u) return texture(entitiesHigh, vec3(uv, float(index)));
    if (page == 2u) return texture(entities2, vec3(uv, float(index)));
    return texture(entities3, vec3(uv, float(index)));
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

vec3 shadeWorld(vec3 rgb)
{
    float daylight = max(clamp(draw.params.y, 0.0, 1.0), 0.2);
    float channel = max(clamp(inLight.x, 0.0, 1.0), clamp(inLight.y, 0.0, 1.0) * daylight);
    channel = mix(channel, 1.0, clamp(draw.params.z, 0.0, 1.0));
    float light = mix(0.04, 1.0, channel) * clamp(inLight.z, 0.0, 1.0);
    vec3 color = rgb * inShade * pow(light, 1.0 / 2.2);
    float amount = smoothstep(draw.fog.w, draw.params.x, length(inRelative));
    return mix(color, draw.fog.rgb, amount);
}

vec4 applyTint(vec4 texel, uint tint)
{
    if ((tint & 0x80000000u) == 0u) {
        return texel;
    }
    vec3 color = vec3(float((tint >> 16) & 0xffu), float((tint >> 8) & 0xffu), float(tint & 0xffu)) / 255.0;
    if ((tint & 0x40000000u) != 0u) {
        return vec4(mix(texel.rgb, texel.rgb * color, texel.a), 1.0);
    }
    return vec4(texel.rgb * color, texel.a);
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
    vec4 texel = inEntity != 0u ? sampleEntity(inUv, inMaterial & 0x1fffu) : applyTint(sampleMaterial(inMaterial, inUv), inTint);
    if ((inEntity & 8u) != 0u) texel.rgb = shadeWorld(texel.rgb);
    if ((inEntity & 4u) != 0u) texel.rgb = mix(texel.rgb, vec3(1.0, 0.0, 0.0), 0.5);
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
