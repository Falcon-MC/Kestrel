#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kestrel {

/** Inputs to the runtime RGB light texture; brightness and vision are independent. */
struct LightmapInputs {
    float skyDarken = 1.0f;
    std::array<float, 4> sunrise {};
    float brightness = 0.0f;
    float nightVision = 0.0f;
    float darkness = 0.0f;
    float darknessPulse = 0.0f;
    bool lightning = false;
};

/** Eased day angle shared by celestial rendering and lightmap weather transfer. */
inline float celestialAngle(double ticks)
{
    constexpr double Day = 24000.0;
    double phase = std::isfinite(ticks) ? std::fmod(ticks, Day) / Day - 0.25 : -0.25;
    phase -= std::floor(phase);
    float linear = static_cast<float>(phase);
    return linear + (0.5f - 0.5f * std::cos(linear * 3.14159265358979323846f) - linear) / 3.0f;
}

/** Weather and sunrise inputs use the lightmap's twilight curve, separate from sky brightness. */
inline LightmapInputs lightmapAt(double ticks, float rain, float thunder)
{
    constexpr float Tau = 6.28318530717958647692f;
    constexpr float TrigScale = 10430.378f;
    auto unit = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
    const float angle = celestialAngle(ticks);
    const float cosine = std::cos(angle * Tau);
    LightmapInputs input;
    input.skyDarken = 0.2f + 0.8f * std::clamp(cosine * 2.0f + 0.2f, 0.0f, 1.0f)
        * (1.0f - unit(rain) * 0.3125f) * (1.0f - unit(thunder) * 0.3125f);
    // The sunrise band reads the native quantized trig samples, unlike sky darkening.
    const auto sample = [](int index) { return std::sin(float(static_cast<uint16_t>(index)) / TrigScale); };
    const float height = sample(static_cast<int>(angle * Tau * TrigScale + 16384.0f));
    if (std::abs(height) <= 0.4f) {
        const float blend = height * 1.25f + 0.5f;
        const float alpha = 0.01f + 0.99f * sample(static_cast<int>(blend * 3.14159265358979323846f * TrigScale));
        input.sunrise = { 0.7f + 0.3f * blend, 0.2f + 0.7f * blend * blend, 0.2f, alpha * alpha * (1.0f - unit(rain)) };
    }
    return input;
}

/** Renderer-tick weather transitions and the separate precipitation fog accumulator. */
struct LightingWeather {
    bool initialized = false;
    int32_t dimension = 0;
    float rain = 0, thunder = 0, previousRain = 0, previousThunder = 0;
    float fog = 0;
    double remainder = 0;

    void advance(float targetRain, float targetThunder, int32_t targetDimension, double seconds, unsigned precipitationSamples)
    {
        auto unit = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
        targetRain = unit(targetRain); targetThunder = unit(targetThunder);
        if (!initialized || dimension != targetDimension) {
            initialized = true;
            dimension = targetDimension;
            rain = previousRain = targetRain;
            thunder = previousThunder = targetThunder;
            remainder = 0;
            return;
        }
        remainder += std::isfinite(seconds) ? std::clamp(seconds, 0.0, 1.0) : 0.0;
        const unsigned ticks = static_cast<unsigned>(std::floor((remainder + 1e-12) * 20.0));
        remainder = std::max(0.0, remainder - ticks * 0.05);
        precipitationSamples = std::min(precipitationSamples, 27u);
        for (unsigned tick = 0; tick < ticks; ++tick) {
            previousRain = rain; previousThunder = thunder;
            rain += std::clamp(targetRain - rain, -0.01f, 0.01f);
            thunder += std::clamp(targetThunder - thunder, -0.01f, 0.01f);
            float counter = 0;
            const float contribution = dimension == 0 ? previousRain * 0.5f : 0.0f;
            for (unsigned sample = 0; sample < precipitationSamples; ++sample) counter += contribution;
            fog = fog * 0.99f + std::clamp(counter * 0.2f, 0.0f, 1.0f) * 0.01f;
        }
    }

    float shownThunder() const { return previousThunder + (thunder - previousThunder) * float(remainder * 20.0); }
    float shownRain() const { return previousRain + (rain - previousRain) * float(remainder * 20.0); }
    float fogLevel() const { return dimension == 0 ? fog : 0.0f; }
};

using LightmapPixels = std::array<uint8_t, 16 * 16 * 4>;

/** Computes a small RGBA8 table from levels, rather than applying gamma to interpolated levels. */
inline LightmapPixels buildLightmap(const LightmapInputs& input)
{
    LightmapPixels pixels {};
    auto unit = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
    const float daylight = unit(input.skyDarken);
    const float warmth = unit(input.sunrise[3]) * 0.3f;
    const float gamma = std::max(unit(input.brightness) - unit(input.darkness), 0.0f);
    for (unsigned sky = 0; sky < 16; ++sky) {
        for (unsigned block = 0; block < 16; ++block) {
            const float torch = 1.6f * float(block) / float(60 - 3 * block);
            const float skyLevel = float(sky) / float(60 - 3 * sky);
            std::array<float, 3> color { torch, torch * (0.64f + 0.36f * torch), torch * (0.4f + 0.6f * torch * torch) };
            for (unsigned channel = 0; channel < 3; ++channel) {
                float skyColor = input.lightning ? skyLevel : skyLevel * ((0.95f * daylight + 0.05f) * (1.0f - warmth) + unit(input.sunrise[channel]) * warmth);
                if (channel < 2) skyColor *= 0.35f + 0.65f * daylight;
                color[channel] = 0.96f * (color[channel] + skyColor) + 0.03f;
            }
            const float maximum = *std::max_element(color.begin(), color.end());
            const unsigned offset = (sky * 16 + block) * 4;
            for (unsigned channel = 0; channel < 3; ++channel) {
                float value = color[channel];
                if (maximum > 0.0f) value += (value / maximum - value) * unit(input.nightVision);
                value = unit(value - unit(input.darknessPulse));
                const float complement = 1.0f - value;
                const float squared = complement * complement;
                value += (1.0f - squared * squared - value) * gamma;
                pixels[offset + channel] = static_cast<uint8_t>(unit(value * 0.96f + 0.03f) * 255.0f);
            }
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

/** Matches the clamped half-texel actor lookup for text drawn through the world canvas. */
inline std::array<float, 3> actorLightColor(const LightmapPixels& pixels, uint8_t light)
{
    const unsigned block = light & 15, sky = light >> 4;
    const unsigned previousBlock = block ? block - 1 : 0, previousSky = sky ? sky - 1 : 0;
    const std::array<unsigned, 4> samples { previousSky * 16 + previousBlock, previousSky * 16 + block, sky * 16 + previousBlock, sky * 16 + block };
    std::array<float, 3> color {};
    for (unsigned sample : samples)
        for (unsigned channel = 0; channel < 3; ++channel) color[channel] += float(pixels[sample * 4 + channel]) / 1020.0f;
    return color;
}

}
