#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

layout(location = 0) in uvec4 inA;
layout(location = 1) in uvec4 inB;
layout(location = 2) in uvec4 inC;

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out uint outMaterial;
layout(location = 2) out float outShade;
layout(location = 3) out vec3 outRelative;

void main()
{
    const uint cornerOrder[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);
    const float faceShade[7] = float[7](0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8);
    uint words[12] = uint[12](inA.x, inA.y, inA.z, inA.w, inB.x, inB.y, inB.z, inB.w, inC.x, inC.y, inC.z, inC.w);
    uint corner = cornerOrder[gl_VertexIndex];
    vec3 local;
    for (uint i = 0u; i < 3u; ++i) {
        uint component = corner * 3u + i;
        uint word = words[component / 2u];
        int value = (component & 1u) != 0u ? (int(word) >> 16) : (int(word << 16) >> 16);
        local[i] = float(value) / 256.0;
    }
    uint uvWord = words[6u + corner];

    vec3 position = draw.origin.xyz + local;
    gl_Position = draw.viewProjection * vec4(position, 1.0);
    gl_Position.y = -gl_Position.y;
    outUv = vec2(float(uvWord & 0xffffu), float(uvWord >> 16)) / 4096.0;
    outMaterial = words[10];
    outShade = faceShade[min(words[11], 6u)];
    outRelative = position;
}
