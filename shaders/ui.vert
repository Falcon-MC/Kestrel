#version 450

layout(push_constant) uniform View {
    vec2 viewport;
} view;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inLocal;
layout(location = 4) in vec2 inHalfSize;
layout(location = 5) in vec2 inShape;
layout(location = 6) in float inDepth;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec2 outLocal;
layout(location = 3) out vec2 outHalfSize;
layout(location = 4) out vec2 outShape;

void main()
{
    gl_Position = vec4(inPosition.x / view.viewport.x * 2.0 - 1.0, inPosition.y / view.viewport.y * 2.0 - 1.0, inDepth, 1.0);
    outUv = inUv;
    outColor = inColor;
    outLocal = inLocal;
    outHalfSize = inHalfSize;
    outShape = inShape;
}
