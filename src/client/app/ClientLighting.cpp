#include "client/Client.h"
#include "client/WorldClock.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

/** Keeps environment light, actor sampling and canvas text on the same lightmap inputs. */
void Client::updateWorldLighting(float deltaSeconds)
{
    unsigned precipitationSamples = 0;
    const auto area = seenSessionSnapshot ? (seenSessionSnapshot->cameraBlocks ? seenSessionSnapshot->cameraBlocks : seenSessionSnapshot->nearby) : nullptr;
    if (area && blockAssets && area->dimension == timeState.dimension) {
        static constexpr int Offsets[9][2] = { {0,0}, {-12,0}, {-8,-8}, {0,-12}, {8,-8}, {12,0}, {8,8}, {0,12}, {-8,8} };
        for (int height : { 0, -3, 3 }) {
            for (const auto& offset : Offsets) {
                const int32_t x = int32_t(std::floor(camera.x())) + offset[0];
                const int32_t y = int32_t(std::floor(camera.y())) + height;
                const int32_t z = int32_t(std::floor(camera.z())) + offset[1];
                const int32_t sx = (x >> 4) - area->base[0], sy = (y >> 4) - area->base[1], sz = (z >> 4) - area->base[2];
                if (sx < 0 || sy < 0 || sz < 0 || sx >= NearbyBlocks::Span || sy >= NearbyBlocks::Span || sz >= NearbyBlocks::Span) continue;
                const size_t slot = size_t((sx * NearbyBlocks::Span + sy) * NearbyBlocks::Span + sz);
                if (slot >= area->biomes.size() || !area->biomes[slot]) continue;
                const uint32_t biome = area->biomes[slot]->runtimeId(x & 15, y & 15, z & 15);
                precipitationSamples += blockAssets->biomeTints().colors(biome).precipitation;
            }
        }
    }
    lightingWeather.advance(timeState.rainLevel, timeState.thunderLevel, timeState.dimension, deltaSeconds, precipitationSamples);
    worldLighting = lightmapAt(currentWorldTime(timeState), lightingWeather.fogLevel(), lightingWeather.shownThunder());
    worldLighting.sunrise = lightmapAt(currentWorldTime(timeState), lightingWeather.shownRain(), 0.0f).sunrise;
    std::unordered_set<uint64_t> bolts;
    for (const ActorView& actor : actorViews) {
        if (actor.identifier != "minecraft:lightning_bolt") continue;
        bolts.insert(actor.runtimeId);
        if (!lightningActors.contains(actor.runtimeId)) lightningAt = secondsNow();
    }
    lightningActors = std::move(bolts);
    worldLighting.lightning = timeState.dimension == 0 && lightningAt >= 0.0 && secondsNow() - lightningAt < 0.25;
    worldLighting.brightness = float(std::clamp(menu.brightness(), menu::MinBrightness, menu::MaxBrightness)) / float(menu::MaxBrightness);
    bool darknessActive = false;
    const double effectNow = secondsNow();
    for (const HudEffect& effect : hudState.effects) {
        if (effect.id == 30 && (effect.expires < 0.0 || effect.expires > effectNow)) darknessActive = true;
    }
    const float fade = std::max(deltaSeconds, 0.0f);
    auto approach = [fade](float value, float target) { return value + std::clamp(target - value, -fade, fade); };
    darknessStrength = approach(darknessStrength, darknessActive ? 1.0f : 0.0f);
    visionStrength = approach(visionStrength, nightVisionStrength() > 0.0f ? 1.0f : 0.0f);
    worldLighting.nightVision = serverCamera.playerEffects() ? visionStrength : 0.0f;
    worldLighting.nightVision = std::max(worldLighting.nightVision, visuals.brightness.value_or(0.0f));
    worldLighting.darkness = serverCamera.playerEffects() ? darknessStrength * float(std::clamp(menu.option("darkness", 100), 0, 100)) / 100.0f : 0.0f;
    worldLighting.darknessPulse = std::max(0.0f, std::cos(float(currentWorldTime(timeState)) * 0.07853982f) * 0.45f * worldLighting.darkness);
}

}
