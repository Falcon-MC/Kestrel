#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

layout(location = 0) in uvec2 inQuad;

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out uint outMaterial;
layout(location = 2) out float outShade;
layout(location = 3) out vec3 outRelative;

vec3 quadCorner(uint face, uint corner, vec3 o, float w, float h)
{
    vec3 c[4];
    if (face == 0u) {
        c[0] = o; c[1] = o + vec3(0, 0, w); c[2] = o + vec3(0, h, w); c[3] = o + vec3(0, h, 0);
    } else if (face == 1u) {
        vec3 b = o + vec3(1, 0, 0);
        c[0] = b; c[1] = b + vec3(0, h, 0); c[2] = b + vec3(0, h, w); c[3] = b + vec3(0, 0, w);
    } else if (face == 2u) {
        c[0] = o; c[1] = o + vec3(w, 0, 0); c[2] = o + vec3(w, 0, h); c[3] = o + vec3(0, 0, h);
    } else if (face == 3u) {
        vec3 b = o + vec3(0, 1, 0);
        c[0] = b; c[1] = b + vec3(0, 0, h); c[2] = b + vec3(w, 0, h); c[3] = b + vec3(w, 0, 0);
    } else if (face == 4u) {
        c[0] = o; c[1] = o + vec3(0, h, 0); c[2] = o + vec3(w, h, 0); c[3] = o + vec3(w, 0, 0);
    } else {
        vec3 b = o + vec3(0, 0, 1);
        c[0] = b; c[1] = b + vec3(w, 0, 0); c[2] = b + vec3(w, h, 0); c[3] = b + vec3(0, h, 0);
    }
    return c[corner];
}

vec2 greedyUv(uint face, uint corner, float w, float h, uint flags)
{
    vec2 horizontalStandard[4] = vec2[4](vec2(0, 0), vec2(w, 0), vec2(w, h), vec2(0, h));
    vec2 horizontalTransposed[4] = vec2[4](vec2(0, 0), vec2(0, h), vec2(w, h), vec2(w, 0));
    vec2 verticalStandard[4] = vec2[4](vec2(0, h), vec2(w, h), vec2(w, 0), vec2(0, 0));
    vec2 verticalTransposed[4] = vec2[4](vec2(0, h), vec2(0, 0), vec2(w, 0), vec2(w, h));
    vec2 uv = horizontalStandard[corner];
    if (face == 0u || face == 5u) {
        uv = verticalStandard[corner];
    } else if (face == 1u || face == 4u) {
        uv = verticalTransposed[corner];
    } else if (face == 3u) {
        uv = horizontalTransposed[corner];
    }
    if (flags != 0u) {
        uv = vec2(uv.y, w - uv.x);
    }
    return uv;
}

void main()
{
    const uint cornerOrder[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);
    const float faceShade[6] = float[6](0.6, 0.6, 0.5, 1.0, 0.8, 0.8);
    uint geometry = inQuad.x;
    vec3 localOrigin = vec3(float(geometry & 0x1fu), float((geometry >> 5) & 0x1fu), float((geometry >> 10) & 0x1fu));
    uint face = (geometry >> 15) & 0x7u;
    float width = float(((geometry >> 18) & 0xfu) + 1u);
    float height = float(((geometry >> 22) & 0xfu) + 1u);
    uint corner = cornerOrder[gl_VertexIndex];

    vec3 position = draw.origin.xyz + quadCorner(face, corner, localOrigin, width, height);
    gl_Position = draw.viewProjection * vec4(position, 1.0);
    gl_Position.y = -gl_Position.y;
    outUv = greedyUv(face, corner, width, height, (inQuad.y >> 12) & 1u);
    outMaterial = inQuad.y;
    outShade = faceShade[face];
    outRelative = position;
}
