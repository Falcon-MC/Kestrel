#pragma once

#include "platform/Input.h"

#include <array>

namespace kestrel {

using Mat4 = std::array<float, 16>;

class FreeCamera {
public:
    static constexpr float Speed = 24.0f;
    static constexpr float LookSensitivity = 0.002f;
    static constexpr float HorizontalFovDegrees = 90.0f;
    static constexpr float PitchLimit = 89.9f * 3.14159265f / 180.0f;

    void placeAt(double x, double y, double z, float minecraftYawDegrees, float minecraftPitchDegrees);
    void update(const InputState& input, const KeyBindings& bindings, float deltaSeconds, bool captured);
    void look(const InputState& input, bool captured);
    void setPosition(double x, double y, double z);
    float minecraftYaw() const;
    float minecraftPitch() const;

    /**
     * Eases the field of view toward the given multiplier of the base one,
     * the way sprinting and flying widen the view.
     */
    void easeFov(float target, float deltaSeconds);

    void setBaseFov(float degrees)
    {
        baseFov = degrees;
    }
    Mat4 viewProjection(float aspect) const;
    std::array<float, 3> forward() const;

    double x() const
    {
        return px;
    }

    double y() const
    {
        return py;
    }

    double z() const
    {
        return pz;
    }

private:
    double px = 0.0;
    double py = 80.0;
    double pz = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovScale = 1.0f;
    float baseFov = HorizontalFovDegrees;
};

}
