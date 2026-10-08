#version 450

layout(push_constant) uniform Draw {
    mat4 transform;
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main()
{
    gl_Position = draw.transform * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;
    outUv = inUv;
    outColor = inColor;
}
