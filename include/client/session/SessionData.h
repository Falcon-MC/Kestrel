#pragma once

#include "client/Session.h"
#include "Protocol/Packets/SetActorDataPacket.h"

#include <cstdint>

namespace kestrel::session {

inline constexpr float EyeHeight = 1.62001f;
inline constexpr double PlayerEyeHeight = 1.62;
inline constexpr int32_t ScaleDataId = 38;

/**
 * The level of one enchantment on an item stack, 0 when it does not carry it.
 */
int32_t enchantmentLevel(const ItemStack& stack, int16_t id);

/**
 * The entity scale carried by a metadata update, or fallback when the update
 * does not set it.
 */
float metadataScale(const EntityDataMap& metadata, float fallback);

/**
 * Copies the entity state a metadata update carries (flags, variants, color,
 * skin id) into the actor, keeping every value the update leaves out.
 */
void applyActorMetadata(const EntityDataMap& metadata, ActorView& actor);

}
