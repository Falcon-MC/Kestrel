#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inLocal;
layout(location = 3) in vec2 inHalfSize;
layout(location = 4) in vec2 inShape;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texel = texture(atlas, inUv);
    float coverage = texel.a;
    if (inShape.x >= 0.0) {
        float radius = inShape.x;
        vec2 q = abs(inLocal) - inHalfSize + radius;
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
        coverage *= clamp(0.5 - distance / max(inShape.y, 1.0), 0.0, 1.0);
    }
    outColor = vec4(inColor.rgb * texel.rgb, inColor.a * coverage);
}
