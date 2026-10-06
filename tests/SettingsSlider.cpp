#include "menu/SettingsSlider.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace kestrel::menu;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    require(settingsSliderValue("gamma", 0.8, 0, 100) == 80, "Brightness fraction must produce 80 percent, not 1 percent");
    require(settingsSliderBinding("gamma", 25, 0, 100) == 0.25, "Brightness binding must position the handle at 25 percent");
    require(settingsSliderValue("field_of_view", 0.5, 30, 110) == 70, "FOV must interpolate its nonzero minimum");
    require(settingsSliderBinding("field_of_view", 70, 30, 110) == 0.5, "FOV binding must be normalized");
    require(settingsSliderValue("main_volume", 0.8, 0, 100) == 80, "Volume must use the continuous slider contract");
    require(settingsSliderValue("render_distance", 6, 4, 32) == 10, "Render distance must retain its step index");
    require(settingsSliderBinding("render_distance", 10, 4, 32) == 6, "Stepped binding must retain its index");
    require(settingsSliderValue("max_framerate", 144, 0, 360) == 144, "Frame-rate steps must not be normalized");
    require(settingsSliderValue("gamma", -0.5, 0, 100) == 0 && settingsSliderValue("gamma", 1.5, 0, 100) == 100, "Values must clamp at both ends");
    require(settingsSliderValue("gamma", std::numeric_limits<double>::quiet_NaN(), 0, 100) == 0, "Invalid slider values must be rejected");
}
