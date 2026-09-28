#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace kestrel::world {

/**
 * The durability of vanilla tools, weapons and armor, or 0 for items that do
 * not wear out.
 */
int32_t itemMaxDurability(const std::string& identifier);

/**
 * The armor points one vanilla armor piece adds to the armor bar.
 */
int32_t itemArmorPoints(const std::string& identifier);

/**
 * The vanilla armor model texture a piece worn in the given slot (helmet,
 * chestplate, leggings, boots) draws with, without extension, or empty for
 * items that have none, like elytra, heads and pumpkins.
 */
std::string itemArmorTexture(const std::string& identifier, size_t slot);

/**
 * A readable name for an item without a custom name: the identifier without
 * its namespace, words capitalized.
 */
std::string itemDisplayName(const std::string& identifier);

}
