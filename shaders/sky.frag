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
    vec4 color = inColor;
    if ((inFlags & 1u) != 0u) {
        vec4 texel = textureLod(blocks, vec3(inUv, float(inLayer)), 0.0);
        color.rgb *= texel.rgb;
        if ((inFlags & 2u) == 0u) {
            color.a *= texel.a;
        }
    }
    if ((inFlags & 2u) != 0u) {
        outColor = vec4(color.rgb * color.a, 0.0);
    } else {
        outColor = vec4(color.rgb * color.a, color.a);
    }
}
