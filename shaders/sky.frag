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
layout(location = 1) flat in uint inLayer;
layout(location = 2) in vec4 inColor;
layout(location = 3) flat in uint inFlags;
layout(location = 4) in vec3 inRelative;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 normals[7] = vec3[7](vec3(0.0), vec3(0.0, -1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0), vec3(0.0, 0.0, 1.0));
    vec4 color = inColor;
    if ((inFlags & 1u) != 0u) {
        vec4 texel = textureLod(blocks, vec3(inUv, float(inLayer)), 0.0);
        color.rgb *= texel.rgb;
        if ((inFlags & 2u) == 0u) {
            color.a *= texel.a;
        }
    }
    uint normal = (inFlags >> 3) & 7u;
    if (normal != 0u) {
        float directional = max(dot(normals[min(normal, 6u)], draw.sun.xyz), 0.0);
        color.rgb *= max(draw.params.y, 0.2) * mix(0.55, 1.0, directional);
    }
    if ((inFlags & 4u) != 0u) {
        float start = clamp(draw.fog.w, 0.0, 255.0);
        float end = clamp(draw.params.x, 0.0, 255.0);
        float range = length(inRelative);
        float amount = end <= start ? (range >= end ? 1.0 : 0.0) : smoothstep(start, end, range);
        color.a *= 1.0 - amount;
    }
    if ((inFlags & 2u) != 0u) {
        outColor = vec4(color.rgb * color.a, 0.0);
    } else {
        outColor = vec4(color.rgb * color.a, color.a);
    }
}
