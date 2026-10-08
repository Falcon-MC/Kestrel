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

/**
 * Whether placing a block takes the place of this one, by its full name.
 */
bool replaceableBlock(std::string_view name);

/**
 * The single box the game outlines and aims at for a block, relative to its
 * cell, from its collision boxes relative to the cell.
 */
world::CollisionBox selectionBounds(const world::BlockAssets& assets, const world::IdMapping& ids, uint32_t value, const std::vector<world::CollisionBox>& boxes);

}
