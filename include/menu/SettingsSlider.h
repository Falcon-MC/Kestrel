#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace kestrel::menu {

inline bool steppedSettingsSlider(std::string_view name)
{
    return name == "gui_scale" || name == "render_distance" || name == "max_framerate";
}

inline double settingsSliderBinding(std::string_view name, int value, int minimum, int maximum)
{
    const int range = maximum - minimum;
    const int offset = std::clamp(value, minimum, maximum) - minimum;
    return steppedSettingsSlider(name) ? offset : range > 0 ? double(offset) / range : 0.0;
}

inline int settingsSliderValue(std::string_view name, double reported, int minimum, int maximum)
{
    if (!std::isfinite(reported)) return minimum;
    const double range = maximum - minimum;
    const double offset = steppedSettingsSlider(name) ? reported : reported * range;
    return minimum + int(std::lround(std::clamp(offset, 0.0, range)));
}

}
