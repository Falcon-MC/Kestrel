#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in uint inLayer;
layout(location = 3) in uint inColor;
layout(location = 4) in uint inFlags;

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out uint outLayer;
layout(location = 2) out vec4 outColor;
layout(location = 3) flat out uint outFlags;
layout(location = 4) out vec3 outRelative;

void main()
{
    vec3 position = draw.origin.xyz + inPosition;
    gl_Position = draw.viewProjection * vec4(position, 1.0);
    gl_Position.y = -gl_Position.y;
    outUv = inUv;
    outLayer = inLayer;
    outColor = vec4(float(inColor & 0xffu), float((inColor >> 8) & 0xffu), float((inColor >> 16) & 0xffu), float(inColor >> 24)) / 255.0;
    outFlags = inFlags;
    outRelative = position;
}
