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
    vec4 overlay;
    vec4 textures;
    vec4 grid0;
    vec4 grid1;
    vec4 grid2;
    vec4 options;
    vec4 glint;
    vec4 glintTexture;
} actor;

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
layout(location = 4) in vec4 inGlint;
layout(location = 5) in vec4 inGlintTexture;
layout(location = 6) in vec4 inGlintUvTransform;

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

void main()
{
    outActorColor = actor.color;
    outActorOverlay = actor.overlay;
    outActorTextures = floatBitsToUint(actor.textures);
    outActorGrid0 = actor.grid0;
    outActorGrid1 = actor.grid1;
    outActorGrid2 = actor.grid2;
    outActorDissolve = actor.options.w;

    const uint cornerOrder[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);
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
    outGlintUv = vec2(float(uvWord & 65535u), float(uvWord >> 16)) / 4096.0;
    outGlint = actor.glint;
    outGlintTexture = actor.glintTexture;
    outMaterial = floatBitsToUint(actor.params.y);
    vec3 normal = vec3(uintBitsToFloat(inD.x), uintBitsToFloat(inD.y), uintBitsToFloat(inD.z));
    vec3 direction = pose(normal) - pose(vec3(0));
    direction = length(direction) > 0.0 ? normalize(direction) : vec3(0,1,0);
    outShade = (1.0 + direction.y) * 0.275 - direction.x * direction.x * 0.1 + direction.z * direction.z * 0.1 + 0.45 + actor.overlay.a * 0.35;
    outRelative = position;
    outTint = 0u;
    outLight = cornerLight(floatBitsToUint(actor.params.w), 0u, corner);
    outEntity = (floatBitsToUint(actor.params.z) >> 5u) & 255u;
    if (any(notEqual(actor.uv, vec4(0,0,1,1)))) outEntity |= 16u;
}
