#version 450

layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 timing;
    vec4 params[3];
} k;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// Bright edges that pulse, params[0].rgb is the color.
void main()
{
    vec2 edge = abs(inUv - 0.5) * 2.0;
    float rim = smoothstep(0.75, 1.0, max(edge.x, edge.y));
    float pulse = 0.65 + 0.35 * sin(k.timing.x * 4.0);
    outColor = vec4(k.params[0].rgb * inColor.rgb, rim * pulse * inColor.a);
}
