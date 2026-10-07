#include "menu/GuiScale.h"
#include "menu/TitleLayout.h"
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
    for (float scale : { 1.0f, 2.0f, 3.0f, 4.0f }) {
        float width = 1600.0f / scale, height = 900.0f / scale;
        auto layout = titleLayout(width, height);
        float mainRight = width * 0.5f + 74.0f;
        require(layout.dressingCenter - 41.0f >= mainRight, "Dressing room must remain beside the main buttons");
        require(layout.dressingCenter + 41.0f <= width, "Dressing room must remain inside the screen");
        require(layout.logoTop + layout.logoWidth * 0.2f < height * 0.5f + 13.67f, "Title must remain above the main buttons");
        require(layout.dressingTop - 97.67f > layout.logoTop + layout.logoWidth * 0.2f, "Player name must remain below the title");
        require(layout.dressingTop + 24.0f < height, "Dressing room must remain above the footer");
    }
    require(effectiveGuiScale(1920, 1080, 0, 1) == 2, "Zero modifier must retain automatic scale");
    require(effectiveGuiScale(1920, 1080, -1, 1) == 1, "Negative modifier must shrink the GUI");
    require(effectiveGuiScale(1920, 1080, 1, 1) == 3, "Positive modifier must enlarge the GUI");
    require(effectiveGuiScale(1920, 1080, 99, 1) == 4, "Large modifier must fit the window");
    require(effectiveGuiScale(1920, 1080, -99, 1) == 1, "Small modifier must retain a positive scale");
    require(effectiveGuiScale(320, 200, 0, 1) > 0 && effectiveGuiScale(320, 200, 0, 1) < 1, "Small windows must remain usable");
    require(settingsSliderBinding("gui_scale", 0, -1, 2) == 1, "Default modifier must use the correct step index");
    require(settingsSliderValue("gui_scale", 2, -1, 2) == 1, "GUI slider must convert step indices into modifiers");
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
