#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inLocal;
layout(location = 3) in vec2 inHalfSize;
layout(location = 4) in vec2 inShape;
layout(location = 5) in vec4 inGlintUv;
layout(location = 6) flat in vec4 inGlintRegion;
layout(location = 7) flat in float inGlintStrength;

layout(location = 0) out vec4 outColor;

vec3 uiGlintTexel(vec2 cell, vec4 region)
{
    vec2 dimensions = vec2(textureSize(atlas, 0));
    vec2 size = round((region.zw - region.xy) * dimensions);
    vec2 at = round(region.xy * dimensions) + floor(fract(cell / size) * size);
    return texelFetch(atlas, ivec2(at), 0).rgb;
}

vec3 uiGlintSample(vec2 uv, vec4 region)
{
    vec2 pixel = fract(uv) * round((region.zw - region.xy) * vec2(textureSize(atlas, 0))) - 0.5;
    vec2 cell = floor(pixel), weight = fract(pixel);
    return mix(mix(uiGlintTexel(cell, region), uiGlintTexel(cell + vec2(1, 0), region), weight.x),
        mix(uiGlintTexel(cell + vec2(0, 1), region), uiGlintTexel(cell + vec2(1, 1), region), weight.x), weight.y);
}

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
    vec3 rgb = inColor.rgb * texel.rgb;
    if (inGlintStrength > 0.0 && texel.a > 0.00390625) {
        vec3 foil = (uiGlintSample(inGlintUv.xy, inGlintRegion) + uiGlintSample(inGlintUv.zw, inGlintRegion)) * vec3(0.38, 0.19, 0.608) * inColor.rgb;
        rgb += foil * foil * inGlintStrength;
    }
    outColor = vec4(rgb, inColor.a * coverage);
}
