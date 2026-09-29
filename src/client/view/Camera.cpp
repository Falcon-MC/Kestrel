#include "client/Camera.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 out {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[column * 4 + k];
            }
            out[column * 4 + row] = sum;
        }
    }
    return out;
}

}

void FreeCamera::placeAt(double x, double y, double z, float minecraftYawDegrees, float minecraftPitchDegrees)
{
    constexpr float toRadians = 3.14159265f / 180.0f;
    px = x;
    py = y;
    pz = z;
    yaw = 3.14159265f - minecraftYawDegrees * toRadians;
    pitch = std::clamp(-minecraftPitchDegrees * toRadians, -PitchLimit, PitchLimit);
}

void FreeCamera::setRotation(float minecraftYawDegrees, float minecraftPitchDegrees)
{
    constexpr float toRadians = 3.14159265f / 180.0f;
    yaw = 3.14159265f - minecraftYawDegrees * toRadians;
    pitch = std::clamp(-minecraftPitchDegrees * toRadians, -PitchLimit, PitchLimit);
}

void FreeCamera::update(const InputState& input, const KeyBindings& bindings, float deltaSeconds, bool captured)
{
    if (captured) {
        yaw -= input.mouseDeltaX * LookSensitivity;
        pitch = std::clamp(pitch - input.mouseDeltaY * LookSensitivity, -PitchLimit, PitchLimit);
    }

    float right = float(input.isHeld(bindings.right())) - float(input.isHeld(bindings.left()));
    float up = float(input.isHeld(bindings.up())) - float(input.isHeld(bindings.down()));
    float forward = float(input.isHeld(bindings.forward())) - float(input.isHeld(bindings.back()));
    float length = std::sqrt(right * right + up * up + forward * forward);
    if (!captured || length == 0.0f) {
        return;
    }

    float step = Speed * deltaSeconds / length;
    float sinYaw = std::sin(yaw);
    float cosYaw = std::cos(yaw);
    px += (right * cosYaw - forward * sinYaw) * step;
    py += up * step;
    pz += (-right * sinYaw - forward * cosYaw) * step;
}

void FreeCamera::look(const InputState& input, bool captured)
{
    if (!captured) {
        return;
    }
    yaw -= input.mouseDeltaX * LookSensitivity;
    pitch = std::clamp(pitch - input.mouseDeltaY * LookSensitivity, -PitchLimit, PitchLimit);
}

void FreeCamera::setPosition(double x, double y, double z)
{
    px = x;
    py = y;
    pz = z;
}

float FreeCamera::minecraftYaw() const
{
    return (3.14159265f - yaw) * 180.0f / 3.14159265f;
}

float FreeCamera::minecraftPitch() const
{
    return -pitch * 180.0f / 3.14159265f;
}

void FreeCamera::easeFov(float target, float deltaSeconds)
{
    float blend = 1.0f - std::exp(-deltaSeconds * 10.0f);
    fovScale += (target - fovScale) * blend;
}

std::array<float, 3> FreeCamera::forward() const
{
    float cosPitch = std::cos(pitch);
    return { -std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch };
}

float FreeCamera::halfVerticalTangent(float aspect) const
{
    float vertical = std::min(baseFov * fovScale, 170.0f) * 3.14159265f / 180.0f;
    return std::tan(vertical * 0.5f);
}

Mat4 FreeCamera::viewProjection(float aspect) const
{
    float viewYaw = yaw + (facingSubject ? 3.14159265f : 0.0f);
    float viewPitch = facingSubject ? -pitch : pitch;
    float cosPitch = std::cos(viewPitch);
    float fx = -std::sin(viewYaw) * cosPitch;
    float fy = std::sin(viewPitch);
    float fz = -std::cos(viewYaw) * cosPitch;
    float rx = std::cos(viewYaw);
    float rz = -std::sin(viewYaw);
    float ux = -rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy;

    Mat4 view {
        rx, ux, -fx, 0.0f,
        0.0f, uy, -fy, 0.0f,
        rz, uz, -fz, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    constexpr float nearPlane = 0.05f;
    constexpr float farPlane = 1024.0f;
    float f = 1.0f / halfVerticalTangent(aspect);
    Mat4 projection {
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, farPlane / (nearPlane - farPlane), -1.0f,
        0.0f, 0.0f, nearPlane * farPlane / (nearPlane - farPlane), 0.0f,
    };
    float hurt = std::clamp(hurtProgress, 0.0f, 1.0f);
    float roll = -std::sin(hurt * hurt * hurt * hurt * 3.14159265f) * 14.0f * 3.14159265f / 180.0f;
    float c = std::cos(roll), s = std::sin(roll);
    Mat4 tilt { c, s, 0.0f, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    return multiply(projection, multiply(tilt, view));
}

}
