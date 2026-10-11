#include "render/Lightmap.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

void check(bool value, const char* message)
{
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

int main()
{
    using namespace kestrel;
    LightingWeather weather;
    weather.advance(0, 0, 0, 0, 27);
    weather.advance(1, 1, 0, 0.075, 27);
    check(std::abs(weather.rain - 0.01f) < 0.00001f && std::abs(weather.shownRain() - 0.005f) < 0.00001f, "Weather must advance at 20 ticks and interpolate previous/current levels");
    weather = {};
    weather.advance(1, 0, 0, 0, 1);
    weather.advance(1, 0, 0, 0.05, 1);
    check(std::abs(weather.fogLevel() - 0.001f) < 0.00001f, "One rainy biome sample must contribute its lattice weight to fog");
    weather.advance(1, 0, 0, 0.05, 0);
    check(std::abs(weather.fogLevel() - 0.00099f) < 0.00001f, "Missing biome samples must decay fog without renormalization");
    weather.advance(1, 0, 1, 0, 27);
    check(weather.fogLevel() == 0, "Non-Overworld lighting must hide precipitation fog");
    auto noon = lightmapAt(6000, 0, 0);
    auto midnight = lightmapAt(18000, 0, 0);
    check(noon.skyDarken == 1.0f && midnight.skyDarken == 0.2f, "Day and night must use different lightmap plateaus");
    check(std::abs(lightmapAt(6000, 1, 1).skyDarken - 0.578125f) < 0.00001f, "Rain and thunder must darken independently");
    check(celestialAngle(6000) == 0.0f && celestialAngle(18000) == 0.5f, "Celestial angle must ease around noon and midnight");
    check(celestialAngle(-18000) == celestialAngle(6000), "Negative world times must wrap consistently");
    const auto day = buildLightmap(noon), night = buildLightmap(midnight);
    check(day[0] == 14 && day[1] == 14 && day[2] == 14, "Both ambient stages must survive RGBA8 truncation in unlit cells");
    const unsigned skyOnly = 15 * 16 * 4;
    check(day[skyOnly] == 250 && day[skyOnly + 2] == 250, "Open sky at noon must preserve both ambient stages");
    check(night[skyOnly + 2] > night[skyOnly], "Open sky at night must have a cool tint");
    const unsigned torch = 10 * 4;
    check(night[torch] > night[torch + 1] && night[torch + 1] > night[torch + 2], "Torch light must stay warm across all RGB channels");
    for (unsigned block = 0; block < 16; ++block)
        for (unsigned channel = 0; channel < 3; ++channel)
            check(day[block * 4 + channel] == night[block * 4 + channel], "Time must not alter light without a sky contribution");
    auto bright = midnight;
    bright.brightness = 1;
    const auto gamma = buildLightmap(bright);
    bright.brightness = 0;
    bright.nightVision = 1;
    const auto vision = buildLightmap(bright);
    check(gamma[0] > night[0] && gamma[0] < 255 && vision[0] == 252, "Brightness must apply gamma rather than granting night vision");
    check(vision[torch] == 252 && vision[torch + 2] < 252, "Night vision must preserve channel ratios when normalizing");
    bright = midnight;
    bright.brightness = 1;
    bright.darkness = 1;
    check(buildLightmap(bright) == night, "Darkness must remove the gamma lift before applying its pulse");
    bright.darknessPulse = 0.45f;
    check(buildLightmap(bright)[torch] < night[torch], "Darkness pulse must darken RGB light");
    bright = midnight;
    bright.lightning = true;
    check(buildLightmap(bright)[skyOnly + 2] > night[skyOnly + 2], "A lightning flash must brighten the sky channel");
    const auto unlit = actorLightColor(day, 0);
    check(std::abs(unlit[0] - 14.0f / 255.0f) < 0.00001f, "Actor lookup must clamp at the first texel");
    const auto sample = actorLightColor(day, 0x1a);
    const float expected = float(day[9 * 4] + day[10 * 4] + day[(16 + 9) * 4] + day[(16 + 10) * 4]) / 1020.0f;
    check(std::abs(sample[0] - expected) < 0.00001f, "Actors must use the half-texel light lookup");
    bright.skyDarken = std::numeric_limits<float>::quiet_NaN();
    bright.brightness = std::numeric_limits<float>::infinity();
    for (unsigned pixel = 0; pixel < 256; ++pixel) check(buildLightmap(bright)[pixel * 4 + 3] == 255, "Invalid environment inputs must not invalidate texture alpha");
}
