#include "client/Sky.h"
#include "world/BiomeTints.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.0001f;
}

int main()
{
    kestrel::SkyFrame base;
    base.daylight = 0.5f;
    base.sunDirection = { 0, 1, 0 };
    kestrel::world::BiomeFog fog { 0x204080, 2, 12, false };
    auto frame = kestrel::foggedBy(base, fog, 64, 0);
    require(near(frame.fogStart, 2) && near(frame.fogEnd, 12), "Fixed fog must not scale or transition without transition_fog");
    require(near(frame.fogColor[0], 32.0f / 255) && near(frame.fogColor[2], 128.0f / 255), "Fog color must preserve sRGB channels");
    require(frame.daylight == base.daylight && frame.sunDirection == base.sunDirection, "Fog must preserve the atmosphere's lighting");
    fog.relative = true;
    frame = kestrel::foggedBy(base, fog, 64);
    require(near(frame.fogStart, 128) && near(frame.fogEnd, 768), "Render-relative fog must follow render distance");
    fog.relative = false;
    fog.transition = kestrel::world::BiomeFogTransition { 0xFFFFFF, 0, 0.25f, true, 0.2f, 2, 0.6f, 6 };
    frame = kestrel::foggedBy(base, fog, 64, 0);
    require(near(frame.fogStart, 0.4f) && near(frame.fogEnd, 15.2f), "Initial and target distances must scale independently before interpolation");
    require(near(frame.fogColor[0], 1.0f + (32.0f / 255 - 1.0f) * 0.2f), "Water transition must interpolate colors");
    frame = kestrel::foggedBy(base, fog, 64, 2);
    require(near(frame.fogStart, 1.2f) && near(frame.fogEnd, 13.6f), "Water transition must reach mid_percent at mid_seconds");
    frame = kestrel::foggedBy(base, fog, 64, 4);
    require(near(frame.fogStart, 1.6f) && near(frame.fogEnd, 12.8f), "Second transition interval must interpolate toward the final profile");
    frame = kestrel::foggedBy(base, fog, 64, 100);
    require(near(frame.fogStart, 2) && near(frame.fogEnd, 12), "Water transition must stop at its final profile");
    frame = kestrel::foggedBy(base, fog, 64, std::numeric_limits<float>::quiet_NaN());
    require(std::isfinite(frame.fogEnd) && near(frame.fogEnd, 15.2f), "Invalid elapsed time must not propagate NaNs to rendering");
    fog.transition->midSeconds = fog.transition->maxSeconds = 0;
    frame = kestrel::foggedBy(base, fog, 64, 0);
    require(near(frame.fogEnd, 12), "Zero duration transition must immediately use its final profile");
}
