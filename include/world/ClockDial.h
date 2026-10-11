#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace kestrel::world {

inline constexpr uint32_t ClockFrames = 64;

inline uint32_t clockFrame(double ticks)
{
    if (!std::isfinite(ticks)) return 0;
    double phase = std::fmod(ticks - 6000.0, 24000.0);
    if (phase < 0.0) phase += 24000.0;
    return uint32_t(phase * ClockFrames / 24000.0) % ClockFrames;
}

inline std::vector<uint8_t> clockDial(const std::vector<uint8_t>& source, uint32_t size, uint32_t frame)
{
    if (!size || source.size() != size_t(size) * size * 4 || frame % ClockFrames == 0) return source;
    auto result = source;
    double angle = (frame % ClockFrames) * 6.283185307179586 / ClockFrames;
    double sine = std::sin(angle), cosine = std::cos(angle);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            size_t at = (size_t(y) * size + x) * 4;
            const uint8_t* original = source.data() + at;
            bool blue = original[2] > original[0] && original[2] > original[1];
            bool sun = original[0] == original[1] && original[0] >= 128 && original[2] == 0;
            if (!original[3] || (!blue && !sun)) continue;
            double px = (x + 0.5) * 16.0 / size - 8.0;
            double py = (y + 0.5) * 16.0 / size - 8.0;
            double rx = px * cosine + py * sine;
            double ry = py * cosine - px * sine;
            bool daylight = ry < 0.0;
            bool sunPixel = std::abs(rx) < 2.0 && std::abs(ry + 3.5) < 2.0
                && (std::abs(rx) < 1.0 || std::abs(ry + 3.5) < 1.0);
            bool moonPixel = std::abs(rx) < 2.0 && std::abs(ry - 3.5) < 2.0
                && (std::abs(rx) < 1.0 || std::abs(ry - 3.5) < 1.0);
            double shade = blue && original[2] < 200 ? 0.8 : 1.0;
            const uint8_t day[] = { 73, 104, 216 };
            const uint8_t night[] = { 24, 26, 48 };
            const uint8_t solar[] = { 255, 255, 0 };
            const uint8_t lunar[] = { 224, 224, 224 };
            const uint8_t* color = sunPixel ? solar : moonPixel ? lunar : daylight ? day : night;
            for (size_t channel = 0; channel < 3; ++channel) result[at + channel] = uint8_t(color[channel] * shade);
        }
    }
    return result;
}

}
