#include "world/ItemInfo.h"

#include "ui/Localization.h"

#include <array>
#include <cctype>
#include <utility>

namespace kestrel::world {

int32_t itemMaxDurability(const std::string& identifier)
{
    static const std::pair<const char*, int32_t> Exact[] = {
        { "minecraft:bow", 384 },
        { "minecraft:crossbow", 464 },
        { "minecraft:trident", 250 },
        { "minecraft:shield", 336 },
        { "minecraft:fishing_rod", 384 },
        { "minecraft:flint_and_steel", 64 },
        { "minecraft:shears", 238 },
        { "minecraft:elytra", 432 },
        { "minecraft:carrot_on_a_stick", 25 },
        { "minecraft:warped_fungus_on_a_stick", 100 },
        { "minecraft:turtle_helmet", 275 },
        { "minecraft:mace", 500 },
        { "minecraft:brush", 64 },
        { "minecraft:wolf_armor", 64 },
    };
    for (const auto& [name, value] : Exact) {
        if (identifier == name) {
            return value;
        }
    }
    static const std::pair<const char*, int32_t> ToolMaterials[] = {
        { "minecraft:wooden_", 59 },
        { "minecraft:stone_", 131 },
        { "minecraft:iron_", 250 },
        { "minecraft:golden_", 32 },
        { "minecraft:diamond_", 1561 },
        { "minecraft:netherite_", 2031 },
        { "minecraft:copper_", 190 },
    };
    static const char* ToolKinds[] = { "sword", "pickaxe", "axe", "shovel", "hoe", "spear" };
    for (const auto& [prefix, value] : ToolMaterials) {
        if (identifier.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string kind = identifier.substr(std::char_traits<char>::length(prefix));
        for (const char* tool : ToolKinds) {
            if (kind == tool) {
                return value;
            }
        }
    }
    static const std::pair<const char*, int32_t> ArmorMaterials[] = {
        { "minecraft:leather_", 5 },
        { "minecraft:chainmail_", 15 },
        { "minecraft:iron_", 15 },
        { "minecraft:golden_", 7 },
        { "minecraft:diamond_", 33 },
        { "minecraft:netherite_", 37 },
        { "minecraft:copper_", 11 },
    };
    static const std::pair<const char*, int32_t> ArmorPieces[] = {
        { "helmet", 11 },
        { "chestplate", 16 },
        { "leggings", 15 },
        { "boots", 13 },
    };
    for (const auto& [prefix, factor] : ArmorMaterials) {
        if (identifier.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string kind = identifier.substr(std::char_traits<char>::length(prefix));
        for (const auto& [piece, base] : ArmorPieces) {
            if (kind == piece) {
                return base * factor;
            }
        }
    }
    return 0;
}

int32_t itemArmorPoints(const std::string& identifier)
{
    static const std::pair<const char*, std::array<int32_t, 4>> Materials[] = {
        { "minecraft:leather_", { 1, 3, 2, 1 } },
        { "minecraft:chainmail_", { 2, 5, 4, 1 } },
        { "minecraft:iron_", { 2, 6, 5, 2 } },
        { "minecraft:golden_", { 2, 5, 3, 1 } },
        { "minecraft:diamond_", { 3, 8, 6, 3 } },
        { "minecraft:netherite_", { 3, 8, 6, 3 } },
        { "minecraft:copper_", { 2, 4, 3, 1 } },
    };
    static const char* Pieces[] = { "helmet", "chestplate", "leggings", "boots" };
    if (identifier == "minecraft:turtle_helmet") {
        return 2;
    }
    for (const auto& [prefix, points] : Materials) {
        if (identifier.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string kind = identifier.substr(std::char_traits<char>::length(prefix));
        for (size_t piece = 0; piece < 4; ++piece) {
            if (kind == Pieces[piece]) {
                return points[piece];
            }
        }
    }
    return 0;
}

std::string itemArmorTexture(const std::string& identifier, size_t slot)
{
    static const std::pair<const char*, const char*> Materials[] = {
        { "minecraft:leather_", "leather" },
        { "minecraft:chainmail_", "chain" },
        { "minecraft:iron_", "iron" },
        { "minecraft:golden_", "gold" },
        { "minecraft:diamond_", "diamond" },
        { "minecraft:netherite_", "netherite" },
        { "minecraft:copper_", "copper" },
        { "minecraft:turtle_", "turtle" },
    };
    static const char* Pieces[] = { "helmet", "chestplate", "leggings", "boots" };
    if (slot >= std::size(Pieces)) {
        return {};
    }
    for (const auto& [prefix, material] : Materials) {
        if (identifier.rfind(prefix, 0) == 0 && identifier.substr(std::char_traits<char>::length(prefix)) == Pieces[slot]) {
            return std::string("textures/models/armor/") + material + (slot == 2 ? "_2" : "_1");
        }
    }
    return {};
}

std::string itemDisplayName(const std::string& identifier)
{
    std::string name = identifier.substr(identifier.find(':') == std::string::npos ? 0 : identifier.find(':') + 1);
    const ui::Localization& texts = ui::Localization::shared();
    for (const std::string& key : { "item." + name + ".name", "tile." + name + ".name", "item." + identifier + ".name", "tile." + identifier + ".name" }) {
        if (texts.has(key)) {
            return texts.text(key, name);
        }
    }
    bool capital = true;
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
            capital = true;
        } else if (capital) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capital = false;
        }
    }
    return name;
}

}
