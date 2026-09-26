#pragma once

#include <cstdint>

namespace kestrel::ui {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr uint32_t packed() const
    {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr float right() const
    {
        return x + w;
    }

    constexpr float bottom() const
    {
        return y + h;
    }

    constexpr bool contains(float px, float py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }

    constexpr Rect inset(float d) const
    {
        return { x + d, y + d, w - d * 2.0f, h - d * 2.0f };
    }
};

}
