#version 450

layout(push_constant) uniform Draw {
    mat4 viewProjection;
    vec4 origin;
    vec4 fog;
    vec4 params;
    vec4 sun;
} draw;

layout(std140, set = 1, binding = 0) uniform Actor {
    vec4 previous[3];
    vec4 current[3];
    vec4 params;
    vec4 uv;
    vec4 color;
} actor;

vec3 pose(vec3 point) {
    vec4 p = vec4(point, 1.0);
    vec3 from = vec3(dot(actor.previous[0], p), dot(actor.previous[1], p), dot(actor.previous[2], p));
    vec3 to = vec3(dot(actor.current[0], p), dot(actor.current[1], p), dot(actor.current[2], p));
    return mix(from, to, actor.params.x);
}

layout(location = 0) in uvec4 inA;
layout(location = 1) in uvec4 inB;
layout(location = 2) in uvec4 inC;
layout(location = 3) in uvec4 inD;

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out uint outMaterial;
layout(location = 2) out float outShade;
layout(location = 3) out vec3 outRelative;
layout(location = 4) flat out uint outTint;
layout(location = 5) out vec3 outLight;
layout(location = 6) flat out uint outEntity;

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
    const uint cornerOrder[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);
    const float faceShade[7] = float[7](0.9, 0.6, 0.6, 0.5, 1.0, 0.8, 0.8);
    const vec3 normals[7] = vec3[7](vec3(0), vec3(0,-1,0), vec3(0,1,0), vec3(-1,0,0), vec3(1,0,0), vec3(0,0,-1), vec3(0,0,1));
    uint words[12] = uint[12](inA.x,inA.y,inA.z,inA.w,inB.x,inB.y,inB.z,inB.w,inC.x,inC.y,inC.z,inC.w);
    uint corner = cornerOrder[gl_VertexIndex];
    vec3 local;
    for (uint axis = 0u; axis < 3u; ++axis) {
        uint component = corner * 3u + axis;
        uint word = words[component / 2u];
        int value = (component & 1u) != 0u ? int(word) >> 16 : int(word << 16) >> 16;
        local[axis] = float(value) / 16.0;
    }
    vec3 position = draw.origin.xyz + pose(local);
    gl_Position = draw.viewProjection * vec4(position,1);
    gl_Position.y = -gl_Position.y;
    uint uvWord = words[6u+corner];
    outUv = actor.uv.xy + vec2(float(uvWord & 65535u), float(uvWord >> 16)) / 4096.0 * actor.uv.zw;
    outMaterial = floatBitsToUint(actor.params.y);
    vec3 direction = pose(normals[(words[11] & 15u) < 7u ? words[11] & 15u : 0u]) - pose(vec3(0));
    uint axis = 0u;
    for (uint i=1u;i<3u;++i) if (abs(direction[i]) > abs(direction[axis])) axis=i;
    uint shade = direction[axis] == 0.0 ? 0u : axis*2u + (direction[axis]>0.0 ? 2u : 1u);
    outShade = faceShade[shade];
    outRelative = position;
    outTint = 0u;
    outLight = cornerLight(floatBitsToUint(actor.params.w),0u,corner);
    outEntity = (floatBitsToUint(actor.params.z) >> 5u) & 47u;
    if (any(notEqual(actor.uv, vec4(0,0,1,1)))) outEntity |= 16u;
}
