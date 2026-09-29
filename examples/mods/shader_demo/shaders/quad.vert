#version 450

layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 timing;
    vec4 params[3];
} k;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main()
{
    gl_Position = k.transform * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;
    outUv = inUv;
    outColor = inColor;
}
