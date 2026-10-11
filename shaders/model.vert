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


vec3 cornerLight(uint light, uint ao, uint corner)
{
    uint levels = (light >> (corner * 8u)) & 0xffu;
    float occlusion = 1.0 - float((ao >> (corner * 4u)) & 7u) * 0.2;
    return vec3(float(levels & 0xfu), float(levels >> 4u), occlusion);
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
    uint shadeFace = min(words[11] & 0xfu, 6u);
    outShade = (inD.y & 0x80010000u) == 0x10000u && (words[11] & 0x10u) == 0u ? (shadeFace == 0u ? 0.9 : shadeFace < 3u ? 0.9 : shadeFace == 3u ? 0.875 : shadeFace == 4u ? 1.0 : 0.95) : faceShade[shadeFace];
    outRelative = position;
    uint rgb = words[11] >> 8;
    outTint = rgb != 0u ? (0x80000000u | rgb) : 0u;
    outLight = cornerLight(inD.x, (inD.y & 0x80000000u) != 0u ? 0u : inD.y, corner);
    outEntity = (words[11] & 0x20u) != 0u ? (words[11] >> 5) & 1839u : 0u;
    if ((words[11] & 0x20000u) != 0u) {
        vec3 points[3];
        const uint normalCorners[3] = uint[3](0u, 1u, 3u);
        for (uint point = 0u; point < 3u; ++point) {
            for (uint axis = 0u; axis < 3u; ++axis) {
                uint component = normalCorners[point] * 3u + axis;
                uint packed = words[component / 2u];
                points[point][axis] = float((component & 1u) != 0u ? (int(packed) >> 16) : (int(packed << 16) >> 16));
            }
        }
#ifdef HAND
        points[0] = vec3(positions[0], positions[1], positions[2]);
        points[1] = vec3(positions[3], positions[4], positions[5]);
        points[2] = vec3(positions[9], positions[10], positions[11]);
#endif
        vec3 normal = cross(points[2] - points[0], points[1] - points[0]);
        float lengthSquared = dot(normal, normal);
        if (lengthSquared > 0.0) {
            normal *= inversesqrt(lengthSquared);
            outShade = (1.0 + normal.y) * 0.275 - normal.x * normal.x * 0.1 + normal.z * normal.z * 0.1 + 0.45;
        }
    }
    if ((outEntity & 256u) != 0u) outLight.z = float((inD.x >> (corner * 8u)) & 255u) / 255.0;
    if (outEntity != 0u) {
        outTint = inD.w == 0u && ((inD.z | outMaterial) & 0x80000000u) != 0u ? inD.z : 0u;
    }
    if (outEntity != 0u && inD.w != 0u) {
        outUv = unpackHalf2x16(inD.z) + outUv * unpackHalf2x16(inD.w);
        outEntity |= 16u;
    }
}
