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
    vec3 color = rgb * inShade * mix(0.25, 1.0, draw.params.y);
    float amount = smoothstep(draw.fog.w, draw.params.x, length(inRelative));
    return mix(color, draw.fog.rgb, amount);
}

void main()
{
    vec4 texel = sampleMaterial(inMaterial, inUv);
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
