#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

// Overlays sit close to the camera and need finer steps than terrain to keep thin outlines steady.
#ifdef OVERLAY
const float PositionScale = 1024.0;
#else
const float PositionScale = 256.0;
#endif

layout(location = 0) in uvec4 inA;
layout(location = 1) in uvec4 inB;
layout(location = 2) in uvec4 inC;
layout(location = 3) in uvec4 inD;
layout(location = 4) in vec4 inGlint;
layout(location = 5) in vec4 inGlintTexture;
layout(location = 6) in vec4 inGlintUvTransform;
#ifdef HAND
layout(location = 7) in vec4 inPositions0;
layout(location = 8) in vec4 inPositions1;
layout(location = 9) in vec4 inPositions2;
#endif

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out uint outMaterial;
layout(location = 2) out float outShade;
layout(location = 3) out vec3 outRelative;
layout(location = 4) flat out uint outTint;
layout(location = 5) out vec3 outLight;
layout(location = 6) flat out uint outEntity;
layout(location = 7) flat out vec4 outActorColor;
layout(location = 8) flat out vec4 outActorOverlay;
layout(location = 9) flat out uvec4 outActorTextures;
layout(location = 10) flat out vec4 outActorGrid0;
layout(location = 11) flat out vec4 outActorGrid1;
layout(location = 12) flat out vec4 outActorGrid2;
layout(location = 13) flat out float outActorDissolve;
layout(location = 14) flat out vec4 outGlint;
layout(location = 15) flat out vec4 outGlintTexture;
layout(location = 16) out vec2 outGlintUv;

const float lightCurve[16] = float[16](
    0.0, 0.01754386, 0.037037037, 0.05882353,
    0.083333336, 0.11111111, 0.14285715, 0.17948718,
    0.22222222, 0.27272728, 0.33333334, 0.4074074,
    0.5, 0.61904764, 0.7777778, 1.0);

vec3 cornerLight(uint light, uint ao, uint corner)
{
    uint levels = (light >> (corner * 8u)) & 0xffu;
    float occlusion = 1.0 - float((ao >> (corner * 2u)) & 3u) * 0.12;
    return vec3(lightCurve[levels & 0xfu], lightCurve[levels >> 4u], occlusion);
}

void main()
{
    outActorColor = vec4(1);
    outActorOverlay = vec4(0);
    outActorTextures = uvec4(0);
    outActorGrid0 = outActorGrid1 = outActorGrid2 = vec4(1);
    outActorDissolve = 0.0;

    const uint cornerOrder[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);
    const float faceShade[7] = float[7](0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8);
    uint words[12] = uint[12](inA.x, inA.y, inA.z, inA.w, inB.x, inB.y, inB.z, inB.w, inC.x, inC.y, inC.z, inC.w);
    uint corner = cornerOrder[gl_VertexIndex];
    vec3 local;
    for (uint i = 0u; i < 3u; ++i) {
        uint component = corner * 3u + i;
        uint word = words[component / 2u];
        int value = (component & 1u) != 0u ? (int(word) >> 16) : (int(word << 16) >> 16);
        local[i] = float(value) / PositionScale;
    }
    if ((inD.y & 0x80000000u) != 0u) {
        for (uint axis = 0u; axis < 3u; ++axis) {
            uint packedOffset = (inD.y >> (axis * 10u)) & 1023u;
            int offset = int(packedOffset << 22u) >> 22;
            local[axis] += float(offset) * (16384.0 / PositionScale);
        }
    }
    uint uvWord = words[6u + corner];

#ifdef HAND
    float positions[12] = float[12](inPositions0.x, inPositions0.y, inPositions0.z, inPositions0.w,
        inPositions1.x, inPositions1.y, inPositions1.z, inPositions1.w,
        inPositions2.x, inPositions2.y, inPositions2.z, inPositions2.w);
    uint component = corner * 3u;
    local = vec3(positions[component], positions[component + 1u], positions[component + 2u]);
#endif
    vec3 position = draw.origin.xyz + local;
    gl_Position = draw.viewProjection * vec4(position, 1.0);
    gl_Position.y = -gl_Position.y;
    outUv = vec2(float(uvWord & 0xffffu), float(uvWord >> 16)) / 4096.0;
    if ((words[11] & 0x10u) != 0u) {
        outUv.y -= fract(draw.origin.w / 32.0);
    }
    outGlintUv = inGlintUvTransform.xy + outUv * inGlintUvTransform.zw;
    outGlint = inGlint;
    outGlintTexture = inGlintTexture;
    outMaterial = words[10];
    outShade = faceShade[min(words[11] & 0xfu, 6u)];
    outRelative = position;
    uint rgb = words[11] >> 8;
    outTint = rgb != 0u ? (0x80000000u | rgb) : 0u;
    outLight = cornerLight(inD.x, (inD.y & 0x80000000u) != 0u ? 0u : inD.y, corner);
    outEntity = (words[11] & 0x20u) != 0u ? (words[11] >> 5) & 815u : 0u;
    if ((outEntity & 256u) != 0u) outLight.z = float((inD.x >> (corner * 8u)) & 255u) / 255.0;
    if (outEntity != 0u) {
        outTint = inD.w == 0u && ((inD.z | outMaterial) & 0x80000000u) != 0u ? inD.z : 0u;
    }
    if (outEntity != 0u && inD.w != 0u) {
        outUv = unpackHalf2x16(inD.z) + outUv * unpackHalf2x16(inD.w);
        outEntity |= 16u;
    }
}
