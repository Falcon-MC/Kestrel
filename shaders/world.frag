#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

layout(set = 0, binding = 0) uniform sampler2DArray blocks;

layout(location = 0) in vec2 inUv;
layout(location = 1) flat in uint inMaterial;
layout(location = 2) in float inShade;
layout(location = 3) in vec3 inRelative;
layout(location = 4) flat in uint inTint;
layout(location = 5) in vec3 inLight;

layout(location = 0) out vec4 outColor;

vec4 sampleMaterial(uint material, vec2 uv)
{
    uint layer = material & 0xfffu;
    uint count = ((material >> 14) & 0x7fu) + 1u;
    uint ticksPerFrame = ((material >> 21) & 0x7ffu) + 1u;
    float timeline = draw.origin.w / float(ticksPerFrame);
    uint frame = uint(timeline) % count;
    vec4 texel = texture(blocks, vec3(uv, float(layer + frame)));
    if (count > 1u && ((material >> 13) & 1u) != 0u) {
        vec4 next = texture(blocks, vec3(uv, float(layer + (frame + 1u) % count)));
        texel = mix(texel, next, fract(timeline));
    }
    return texel;
}

vec3 shadeWorld(vec3 rgb)
{
    float daylight = max(clamp(draw.params.y, 0.0, 1.0), 0.2);
    float channel = max(clamp(inLight.x, 0.0, 1.0), clamp(inLight.y, 0.0, 1.0) * daylight);
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
    vec4 texel = applyTint(sampleMaterial(inMaterial, inUv), inTint);
#ifdef BLEND
    if (texel.a < 0.004) {
        discard;
    }
    outColor = vec4(shadeWorld(texel.rgb) * texel.a, texel.a);
#else
    if (texel.a < 0.5) {
        discard;
    }
    outColor = vec4(shadeWorld(texel.rgb), 1.0);
#endif
}
