#include "client/Sky.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr double DayTicks = 24000.0;
constexpr double CloudPeriod = 256.0;
constexpr double CloudBlocksPerTick = 0.03;
constexpr float CloudUnderside = 128.0f;
constexpr float CloudTop = 132.0f;
constexpr float CelestialHalfAngle = 0.075f;
constexpr float Tau = 6.28318530718f;

using Vec3 = std::array<float, 3>;

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

Vec3 mix3(const Vec3& a, const Vec3& b, float t)
{
    return { lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t) };
}

Vec3 toDisplay(const Vec3& linear)
{
    Vec3 out;
    for (int i = 0; i < 3; ++i) {
        float value = std::clamp(linear[i], 0.0f, 1.0f);
        out[i] = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }
    return out;
}

float smoothstep(float edge0, float edge1, float x)
{
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

uint32_t packColor(const Vec3& rgb, float alpha)
{
    auto channel = [](float value) {
        return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return channel(rgb[0]) | (channel(rgb[1]) << 8) | (channel(rgb[2]) << 16) | (channel(alpha) << 24);
}

Vec3 normalize(const Vec3& v)
{
    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return length > 1e-6f ? Vec3 { v[0] / length, v[1] / length, v[2] / length } : Vec3 { 0.0f, 0.0f, 0.0f };
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

void pushQuad(std::vector<SkyVertex>& out, const std::array<SkyVertex, 4>& corners)
{
    static constexpr int Order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int index : Order) {
        out.push_back(corners[index]);
    }
}

void pushCelestial(std::vector<SkyVertex>& out, const Vec3& direction, uint32_t layer)
{
    constexpr float Distance = 400.0f;
    Vec3 right = cross(direction, { 0.0f, 1.0f, 0.0f });
    if (right[0] * right[0] + right[1] * right[1] + right[2] * right[2] < 0.0001f) {
        right = { 0.0f, 0.0f, 1.0f };
    }
    right = normalize(right);
    Vec3 up = normalize(cross(right, direction));
    float extent = Distance * std::sin(CelestialHalfAngle);
    float visibility = smoothstep(-0.04f, 0.02f, direction[1]);
    if (visibility <= 0.0f) {
        return;
    }
    uint32_t color = packColor({ 1.0f, 1.0f, 1.0f }, visibility);
    std::array<SkyVertex, 4> corners;
    static constexpr float Local[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
    for (int i = 0; i < 4; ++i) {
        float lx = Local[i][0];
        float ly = Local[i][1];
        SkyVertex& vertex = corners[i];
        vertex.x = direction[0] * Distance + (right[0] * lx + up[0] * ly) * extent;
        vertex.y = direction[1] * Distance + (right[1] * lx + up[1] * ly) * extent;
        vertex.z = direction[2] * Distance + (right[2] * lx + up[2] * ly) * extent;
        vertex.u = lx * 0.5f + 0.5f;
        vertex.v = ly * -0.5f + 0.5f;
        vertex.layer = layer;
        vertex.color = color;
        vertex.flags = SkyTextured | SkyAdditive;
    }
    pushQuad(out, corners);
}

}

SkyFrame atmosphereAt(double worldTicks, float renderDistance)
{
    if (!std::isfinite(worldTicks)) {
        worldTicks = 0.0;
    }
    double dayTicks = std::fmod(worldTicks, DayTicks);
    if (dayTicks < 0.0) {
        dayTicks += DayTicks;
    }
    float dayFraction = static_cast<float>(dayTicks / DayTicks);
    float angle = dayFraction * Tau;
    Vec3 sun { std::cos(angle), std::sin(angle), 0.0f };
    for (float& component : sun) {
        if (std::abs(component) < 1e-6f) {
            component = 0.0f;
        }
    }

    float daylight = std::clamp(sun[1] * 0.8f + 0.2f, 0.0f, 1.0f);
    float sunrise = std::pow(1.0f - std::abs(sun[1]), 3.0f) * (0.25f + daylight * 0.75f);
    Vec3 zenith = mix3({ 0.004f, 0.008f, 0.03f }, { 0.18f, 0.48f, 0.88f }, daylight);
    Vec3 clearHorizon = mix3({ 0.018f, 0.024f, 0.065f }, { 0.58f, 0.78f, 1.0f }, daylight);
    Vec3 horizon = mix3(clearHorizon, { 1.0f, 0.36f, 0.12f }, sunrise * 0.55f);
    Vec3 fog = mix3(horizon, zenith, 0.18f);

    SkyFrame frame;
    frame.zenith = toDisplay(zenith);
    frame.horizon = toDisplay(horizon);
    frame.fogColor = toDisplay(fog);
    frame.fogEnd = std::clamp(renderDistance - 8.0f, 32.0f, 256.0f);
    frame.fogStart = frame.fogEnd * 0.75f;
    frame.daylight = daylight;
    frame.sunDirection = sun;
    double days = std::floor(worldTicks / DayTicks);
    frame.moonPhase = static_cast<uint32_t>(std::fmod(std::fmod(days, 8.0) + 8.0, 8.0));
    double drift = std::fmod(worldTicks * CloudBlocksPerTick, CloudPeriod);
    frame.cloudOffset = static_cast<float>(drift < 0.0 ? drift + CloudPeriod : drift);
    return frame;
}

std::vector<SkyVertex> buildSkyBackground(const SkyFrame& frame, uint32_t sunLayer, uint32_t moonLayer)
{
    constexpr int Rings = 12;
    constexpr int Segments = 24;
    constexpr float Radius = 500.0f;
    std::vector<SkyVertex> out;
    auto colorAt = [&](float y) {
        Vec3 color = mix3(frame.horizon, frame.zenith, smoothstep(-0.08f, 0.72f, y));
        if (y < -0.08f) {
            color = { color[0] * 0.72f, color[1] * 0.72f, color[2] * 0.72f };
        }
        return packColor(color, 1.0f);
    };
    auto pointAt = [&](int ring, int segment) {
        float elevation = -1.5707963f + 3.1415926f * static_cast<float>(ring) / Rings;
        float azimuth = Tau * static_cast<float>(segment) / Segments;
        Vec3 direction { std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth) };
        SkyVertex vertex;
        vertex.x = direction[0] * Radius;
        vertex.y = direction[1] * Radius;
        vertex.z = direction[2] * Radius;
        vertex.color = colorAt(direction[1]);
        return vertex;
    };
    for (int ring = 0; ring < Rings; ++ring) {
        for (int segment = 0; segment < Segments; ++segment) {
            pushQuad(out, { pointAt(ring, segment), pointAt(ring, segment + 1), pointAt(ring + 1, segment + 1), pointAt(ring + 1, segment) });
        }
    }
    pushCelestial(out, frame.sunDirection, sunLayer);
    pushCelestial(out, { -frame.sunDirection[0], -frame.sunDirection[1], -frame.sunDirection[2] }, moonLayer);
    return out;
}

std::vector<SkyVertex> buildCloudMesh(const std::vector<uint8_t>& mask)
{
    std::vector<SkyVertex> out;
    if (mask.size() != 256 * 256) {
        return out;
    }
    auto filled = [&](int x, int z) {
        return mask[size_t((z & 255) * 256 + (x & 255))] != 0;
    };
    uint32_t color = packColor({ 1.0f, 1.0f, 1.0f }, 0.8f);
    auto vertex = [&](float x, float y, float z, uint32_t normal) {
        SkyVertex result;
        result.x = x;
        result.y = y;
        result.z = z;
        result.color = color;
        result.flags = SkyFogged | (normal << SkyNormalShift);
        return result;
    };

    for (int z = 0; z < 256; ++z) {
        int x = 0;
        while (x < 256) {
            if (!filled(x, z)) {
                ++x;
                continue;
            }
            int start = x;
            while (x < 256 && filled(x, z)) {
                ++x;
            }
            float x0 = static_cast<float>(start);
            float x1 = static_cast<float>(x);
            float z0 = static_cast<float>(z);
            float z1 = z0 + 1.0f;
            pushQuad(out, { vertex(x0, CloudTop, z0, 2), vertex(x0, CloudTop, z1, 2), vertex(x1, CloudTop, z1, 2), vertex(x1, CloudTop, z0, 2) });
            pushQuad(out, { vertex(x0, CloudUnderside, z0, 1), vertex(x1, CloudUnderside, z0, 1), vertex(x1, CloudUnderside, z1, 1), vertex(x0, CloudUnderside, z1, 1) });
        }
    }

    for (int z = 0; z < 256; ++z) {
        for (int side = 0; side < 2; ++side) {
            int dz = side == 0 ? -1 : 1;
            int x = 0;
            while (x < 256) {
                if (!filled(x, z) || filled(x, z + dz)) {
                    ++x;
                    continue;
                }
                int start = x;
                while (x < 256 && filled(x, z) && !filled(x, z + dz)) {
                    ++x;
                }
                float x0 = static_cast<float>(start);
                float x1 = static_cast<float>(x);
                float edge = static_cast<float>(side == 0 ? z : z + 1);
                uint32_t normal = side == 0 ? 5 : 6;
                pushQuad(out, { vertex(x0, CloudUnderside, edge, normal), vertex(x1, CloudUnderside, edge, normal), vertex(x1, CloudTop, edge, normal), vertex(x0, CloudTop, edge, normal) });
            }
        }
    }

    for (int x = 0; x < 256; ++x) {
        for (int side = 0; side < 2; ++side) {
            int dx = side == 0 ? -1 : 1;
            int z = 0;
            while (z < 256) {
                if (!filled(x, z) || filled(x + dx, z)) {
                    ++z;
                    continue;
                }
                int start = z;
                while (z < 256 && filled(x, z) && !filled(x + dx, z)) {
                    ++z;
                }
                float z0 = static_cast<float>(start);
                float z1 = static_cast<float>(z);
                float edge = static_cast<float>(side == 0 ? x : x + 1);
                uint32_t normal = side == 0 ? 3 : 4;
                pushQuad(out, { vertex(edge, CloudUnderside, z0, normal), vertex(edge, CloudUnderside, z1, normal), vertex(edge, CloudTop, z1, normal), vertex(edge, CloudTop, z0, normal) });
            }
        }
    }
    return out;
}

std::vector<std::array<float, 3>> cloudTileOrigins(const SkyFrame& frame, double cameraX, double cameraY, double cameraZ)
{
    std::vector<std::array<float, 3>> origins;
    double baseX = std::floor((cameraX - frame.cloudOffset) / CloudPeriod) * CloudPeriod + frame.cloudOffset;
    double baseZ = std::floor(cameraZ / CloudPeriod) * CloudPeriod;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            origins.push_back({
                static_cast<float>(baseX + dx * CloudPeriod - cameraX),
                static_cast<float>(-cameraY),
                static_cast<float>(baseZ + dz * CloudPeriod - cameraZ),
            });
        }
    }
    return origins;
}

}
