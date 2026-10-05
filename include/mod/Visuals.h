#pragma once

#include "mod/Types.h"

#include <cstdint>
#include <optional>

namespace kestrel::mod {

/**
 * Client side looks a mod can change. None of it reaches the server: the
 * world keeps its real time and weather, and other players still see you as
 * before. Each setting goes back to the game's own value when the mod
 * unloads, and when two mods change the same one, the mod loaded first wins.
 */
class Visuals {
public:
    virtual ~Visuals() = default;

    // Shows this time of day in ticks, 24000 a day; nullopt follows the server again.
    virtual void setTime(std::optional<int64_t> ticks) = 0;
    // Rain and thunder from 0 to 1; nullopt follows the server again.
    virtual void setWeather(std::optional<float> rain, std::optional<float> thunder) = 0;
    // Pushes the fog further out, 1 is normal; large values all but remove it.
    virtual void setFogScale(float scale) = 0;
    // Lifts dark places the way night vision does, from 0 (normal) to 1.
    virtual void setBrightness(float amount) = 0;
    // How much the view jerks when you get hurt, 1 normal, 0 not at all.
    virtual void setHurtCamera(float scale) = 0;
    // The color hurt mobs and players flash, alpha being how strong; nullopt for the game's red.
    virtual void setHitColor(std::optional<Color> color) = 0;
    // Enchantment glint on item icons, in percent like the video settings; nullopt keeps the player's own.
    virtual void setGlint(std::optional<int> strength, std::optional<int> speed) = 0;
    // Dropped items lie still on the ground instead of floating and spinning.
    virtual void setItemPhysics(bool enabled) = 0;
    // How long an arm swing takes: 1 normal, 2 twice as slow.
    virtual void setSwingDuration(float scale) = 0;
    /**
     * Moves and scales the item in your hand in first person. The offset is
     * in the hand's own units: x right, y up, z toward the camera.
     */
    virtual void setHeldItem(Vec3 offset, float scale) = 0;
    // Name tag size, and whether your own shows over you in third person.
    virtual void setNametags(float scale, bool showOwn) = 0;
    // The interface scale, like the video setting; nullopt keeps the player's own.
    virtual void setInterfaceScale(std::optional<float> scale) = 0;
};

}
