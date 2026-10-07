#pragma once

#include <algorithm>
#include <cmath>

namespace kestrel::menu {

struct GuiScaleRange {
    float automatic;
    float fit;
    int minimum;
    int maximum;
};

inline GuiScaleRange guiScaleRange(float width, float height)
{
    float automatic = std::max(1.0f, std::min(std::floor(height / 360.0f), std::floor(width / 660.0f)));
    float fit = std::max(0.01f, std::min(width / 360.0f, height / 240.0f));
    return { automatic, fit, 1 - int(automatic), std::max(0, int(std::floor(fit)) - int(automatic)) };
}

inline float effectiveGuiScale(float width, float height, int modifier, float multiplier)
{
    auto range = guiScaleRange(width, height);
    return std::max(0.01f, std::min((range.automatic + std::clamp(modifier, range.minimum, range.maximum)) * multiplier, range.fit));
}

}
