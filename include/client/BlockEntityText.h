#pragma once

#include "client/Inventory.h"

#include <array>
#include <cmath>
#include <string_view>

namespace kestrel {

struct SignText {
    std::string text;
    uint32_t color = 0xFF000000;
    bool glow = false;
};

inline std::array<SignText, 2> signTexts(const Tag& data)
{
    std::array<SignText, 2> sides;
    for (size_t side = 0; side < sides.size(); ++side) {
        const Tag* text = data.get(side == 0 ? "FrontText" : "BackText");
        if (!text || !text->isCompound()) text = side == 0 ? &data : nullptr;
        if (!text) continue;
        const Tag* value = text->get("Text");
        if (value && value->getType() == Tag::Type::String && value->asString().size() <= 4096) {
            sides[side].text = value->asString();
        }
        if (const Tag* color = text->get("SignTextColor"); color && color->getType() == Tag::Type::Int) {
            sides[side].color = uint32_t(color->asInt());
        }
        if (const Tag* glow = text->get("IgnoreLighting"); glow && glow->getType() == Tag::Type::Byte) {
            sides[side].glow = glow->asByte() != 0;
        }
    }
    return sides;
}

struct SignTextPose {
    std::array<float, 3> center { 0.5f, 11.0f / 16.0f, 0.5f };
    std::array<float, 3> right { 1, 0, 0 };
    std::array<float, 3> normal { 0, 0, 1 };
    float depth = 0.5f / 16.0f + 0.001f;
    float pixel = 1.0f / 96.0f;
    float width = 90.0f;
};

inline SignTextPose signTextPose(std::string_view name, int rotation, int facing, bool hanging)
{
    constexpr float Pi = 3.14159265f;
    SignTextPose pose;
    const bool suspended = name.ends_with("hanging_sign");
    const bool wall = name.ends_with("wall_sign") || (suspended && !hanging);
    float yaw = float(rotation & 15) * Pi / 8.0f;
    if (suspended) {
        pose.center[1] = 7.0f / 16.0f;
        pose.pixel = 1.0f / 72.0f;
        pose.width = 60.0f;
    }
    if (wall) {
        yaw = facing == 2 ? Pi : facing == 4 ? Pi * 0.5f : facing == 5 ? -Pi * 0.5f : 0;
        if (!suspended) {
            pose.center[1] = 8.5f / 16.0f;
            if (facing == 2) pose.center[2] = 15.5f / 16.0f;
            else if (facing == 3) pose.center[2] = 0.5f / 16.0f;
            else if (facing == 4) pose.center[0] = 15.5f / 16.0f;
            else if (facing == 5) pose.center[0] = 0.5f / 16.0f;
        }
    }
    pose.right = { std::cos(yaw), 0, std::sin(yaw) };
    pose.normal = { -std::sin(yaw), 0, std::cos(yaw) };
    return pose;
}

}
