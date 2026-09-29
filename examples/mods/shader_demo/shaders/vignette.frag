#version 450

layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 timing;
    vec4 params[3];
} k;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// params[0].x is the strength, params[1].rgb the tint.
void main()
{
    vec2 fromCenter = (inUv - 0.5) * vec2(k.timing.y / k.timing.z, 1.0);
    float shade = smoothstep(0.35, 0.95, length(fromCenter)) * k.params[0].x;
    outColor = vec4(k.params[1].rgb, shade);
}
