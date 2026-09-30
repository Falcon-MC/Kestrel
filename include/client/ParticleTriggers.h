#pragma once

#include "world/Particles.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace kestrel {

/**
 * The effect a particle level event starts: the vanilla identifier for its
 * event id, or nothing for an event that shows no particles. data is the
 * event's data field, which some events use to pick a color or a block.
 */
std::optional<world::ParticleSpawn> particleForLevelEvent(int32_t eventId, const std::array<double, 3>& position, int32_t data);

/**
 * The effect a SpawnParticleEffect packet names, with the Molang variables
 * the packet carries as JSON.
 */
world::ParticleSpawn particleForSpawnPacket(const std::string& identifier, const std::array<double, 3>& position, const std::string& molangJson);

}
