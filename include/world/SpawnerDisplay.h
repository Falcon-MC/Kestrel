#pragma once

#include "Core/NBT/Tag.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kestrel::world {

struct SpawnerDisplay {
    std::string identifier;
    float width = 0.8f;
    float height = 1.8f;
    float scale = 1.0f;
    int delay = 20;
};

inline SpawnerDisplay spawnerDisplay(const Tag& data, bool trial = false)
{
    SpawnerDisplay display;
    const Tag* spawn = data.get("spawn_data");
    const Tag* name = trial ? (spawn && spawn->isCompound() ? spawn->get("TypeId") : nullptr) : data.get("EntityIdentifier");
    if (name && name->getType() == Tag::Type::String && name->asString().size() <= 256) {
        display.identifier = name->asString();
    }
    auto number = [&](const char* key, float fallback, float limit) {
        const Tag* value = data.get(key);
        return value && value->getType() == Tag::Type::Float && std::isfinite(value->asFloat())
            ? std::clamp(value->asFloat(), 0.01f, limit) : fallback;
    };
    display.width = number("DisplayEntityWidth", display.width, 64);
    display.height = number("DisplayEntityHeight", display.height, 64);
    display.scale = number("DisplayEntityScale", display.scale, 16);
    if (const Tag* delay = data.get("Delay"); delay && delay->getType() == Tag::Type::Short) {
        display.delay = std::max(int(delay->asShort()), 0);
    }
    return display;
}

inline float spawnerDisplayScale(const SpawnerDisplay& display)
{
    return 0.53125f * display.scale / std::max({ 1.0f, display.width, display.height });
}

inline float spawnerDisplaySpin(float previous, double seconds, int delay)
{
    return std::fmod(previous + float(std::clamp(seconds, 0.0, 0.25) * 20.0) * 400.0f / float(std::clamp(delay, 0, 32767) + 200), 360.0f);
}

}
