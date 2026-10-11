#version 450

layout(push_constant) uniform View {
    vec2 viewport;
} view;

layout(location = 0) in vec2 inPosition;
layout(location = 2) in vec4 inColor;
layout(location = 6) in float inDepth;
layout(location = 0) flat out vec4 outColor;

void main()
{
    gl_Position = vec4(inPosition.x / view.viewport.x * 2.0 - 1.0, inPosition.y / view.viewport.y * 2.0 - 1.0, inDepth, 1.0);
    outColor = inColor;
}
