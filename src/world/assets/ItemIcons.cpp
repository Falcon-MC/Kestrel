#include "world/BlockAssets.h"

#include "TextureTools.h"
#include "Core/Json/Json.h"
#include "ui/Image.h"
#include "util/JsonText.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kestrel::world {

namespace {

std::vector<std::string> itemTexturePaths(const json::Value& definition)
{
    std::vector<std::string> paths;
    const json::Value* textures = definition.get("textures");
    if (!textures) {
        return paths;
    }
    auto pathOf = [](const json::Value& value) -> std::string {
        if (value.isString()) {
            return value.mString;
        }
        const json::Value* path = value.isObject() ? value.get("path") : nullptr;
        return path && path->isString() ? path->mString : std::string();
    };
    if (textures->isArray()) {
        for (const auto& entry : textures->mArray) {
            paths.push_back(pathOf(*entry));
        }
    } else {
        paths.push_back(pathOf(*textures));
    }
    return paths;
}

/**
 * Draws a block as an inventory icon: its top, south and east faces as an
 * isometric cube, shaded brighter on top and darker on the right.
 */
std::vector<uint8_t> isometricIcon(const std::array<const uint8_t*, 3>& faces, const std::array<std::array<uint8_t, 3>, 3>& tints)
{
    constexpr float Size = static_cast<float>(ItemIconSize);
    struct Face {
        std::array<float, 2> origin;
        std::array<float, 2> axisU;
        std::array<float, 2> axisV;
        float shade;
    };
    const float h = Size * 0.5f;
    const float q = Size * 0.25f;
    const std::array<Face, 3> projected { {
        { { h, 0.0f }, { h, q }, { -h, q }, 1.0f },
        { { 0.0f, q }, { h, q }, { 0.0f, h }, 0.8f },
        { { h, h }, { h, -q }, { 0.0f, h }, 0.62f },
    } };
    std::vector<uint8_t> out(size_t(ItemIconSize) * ItemIconSize * 4, 0);
    for (size_t face = 0; face < 3; ++face) {
        if (!faces[face]) {
            continue;
        }
        const Face& f = projected[face];
        float determinant = f.axisU[0] * f.axisV[1] - f.axisU[1] * f.axisV[0];
        if (std::abs(determinant) < 1.0e-6f) {
            continue;
        }
        for (uint32_t y = 0; y < ItemIconSize; ++y) {
            for (uint32_t x = 0; x < ItemIconSize; ++x) {
                float px = x + 0.5f - f.origin[0];
                float py = y + 0.5f - f.origin[1];
                float u = (px * f.axisV[1] - py * f.axisV[0]) / determinant;
                float v = (f.axisU[0] * py - f.axisU[1] * px) / determinant;
                if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) {
                    continue;
                }
                uint32_t tu = std::min(uint32_t(u * TextureSize), TextureSize - 1);
                uint32_t tv = std::min(uint32_t(v * TextureSize), TextureSize - 1);
                const uint8_t* texel = faces[face] + (size_t(tv) * TextureSize + tu) * 4;
                if (texel[3] < 128) {
                    continue;
                }
                uint8_t* pixel = out.data() + (size_t(y) * ItemIconSize + x) * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<uint8_t>(std::clamp(texel[channel] * f.shade * tints[face][channel] / 255.0f, 0.0f, 255.0f));
                }
                pixel[3] = 255;
            }
        }
    }
    return out;
}

}

/**
 * Loads every item texture named by item_texture.json or found under
 * textures/items, scaled to the icon size, and indexes the default state of
 * every block by name for block item icons.
 */
void BlockAssets::buildInterfaceAssets(PackSource& pack)
{
    struct Decoded {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
    };
    std::map<std::string, Decoded> decoded;
    auto load = [&](const std::string& path, uint32_t& width, uint32_t& height) -> const std::vector<uint8_t>* {
        auto found = decoded.find(path);
        if (found == decoded.end()) {
            Decoded image;
            std::string encoded;
            if (!pack.readTexture(path, encoded) || !ui::decodeImage(encoded, image.width, image.height, image.rgba) || image.width == 0 || image.height == 0) {
                image = Decoded {};
            }
            found = decoded.emplace(path, std::move(image)).first;
        }
        width = found->second.width;
        height = found->second.height;
        return found->second.rgba.empty() ? nullptr : &found->second.rgba;
    };
    std::vector<std::string> atlases = pack.readTextLayers("textures/item_texture.json");
    if (std::string archived; pack.readArchived("textures", "item_texture.json", archived)) {
        atlases.push_back(std::move(archived));
    }
    for (const std::string& name : pack.archiveEntries("textures/items")) {
        size_t dot = name.rfind('.');
        std::string stem = name.substr(0, dot);
        if (dot == std::string::npos || name.find('/') != std::string::npos || itemFiles.count(stem)) {
            continue;
        }
        uint32_t width = 0;
        uint32_t height = 0;
        const std::vector<uint8_t>* rgba = load("textures/items/" + stem, width, height);
        if (rgba && width == height) {
            itemFiles.emplace(stem, resizeNearest(*rgba, width, height, ItemIconSize));
        }
    }
    for (const std::string& text : atlases) {
        std::unique_ptr<json::Value> parsed = json::parse(util::stripJsonComments(text));
        const json::Value* data = parsed ? parsed->get("texture_data") : nullptr;
        if (!data || !data->isObject()) {
            continue;
        }
        for (const std::string& alias : data->mKeys) {
            std::string identifier = alias.find(':') == std::string::npos ? "minecraft:" + alias : alias;
            if (itemTextures.count(identifier)) {
                continue;
            }
            std::vector<std::vector<uint8_t>> variants;
            for (const std::string& path : itemTexturePaths(*data->get(alias))) {
                uint32_t width = 0;
                uint32_t height = 0;
                const std::vector<uint8_t>* rgba = path.empty() ? nullptr : load(path, width, height);
                if (!rgba || width != height) {
                    variants.emplace_back();
                    continue;
                }
                variants.push_back(resizeNearest(*rgba, width, height, ItemIconSize));
            }
            itemTextures.emplace(identifier, std::move(variants));
        }
    }

    for (const BlockRecord& record : registry.records()) {
        blockByName.try_emplace(record.name, record.networkHash);
    }
}

/**
 * The inventory icon of an item: its item texture for the aux variant, or for
 * block items an isometric cube of the block's default state. Empty when the
 * item has neither.
 */
std::vector<uint8_t> BlockAssets::itemIcon(const std::string& identifier, int32_t aux, const std::string& iconHint) const
{
    static const std::pair<const char*, const char*> Renames[] = {
        { "totem_of_undying", "totem" },
        { "compass", "compass_item" },
        { "recovery_compass", "recovery_compass_item" },
        { "clock", "clock_item" },
        { "golden_apple", "apple_golden" },
        { "enchanted_golden_apple", "apple_golden" },
        { "writable_book", "book_writable" },
        { "written_book", "book_written" },
        { "enchanted_book", "book_enchanted" },
        { "filled_map", "map_filled" },
        { "empty_map", "map_empty" },
        { "glass_bottle", "potion_bottle_empty" },
        { "potion", "potion_bottle_drinkable" },
        { "splash_potion", "potion_bottle_splash" },
        { "lingering_potion", "potion_bottle_lingering" },
        { "fire_charge", "fireball" },
        { "glistering_melon_slice", "melon_speckled" },
        { "melon_slice", "melon" },
        { "cooked_beef", "beef_cooked" },
        { "cooked_chicken", "chicken_cooked" },
        { "cooked_porkchop", "porkchop_cooked" },
        { "cooked_mutton", "mutton_cooked" },
        { "cooked_rabbit", "rabbit_cooked" },
        { "cooked_cod", "fish_cooked" },
        { "cooked_salmon", "fish_salmon_cooked" },
        { "cod", "fish_raw" },
        { "salmon", "fish_salmon_raw" },
        { "tropical_fish", "fish_clownfish_raw" },
        { "pufferfish", "fish_pufferfish_raw" },
        { "porkchop", "porkchop_raw" },
        { "beef", "beef_raw" },
        { "chicken", "chicken_raw" },
        { "mutton", "mutton_raw" },
        { "rabbit", "rabbit_raw" },
        { "experience_bottle", "experience_bottle" },
        { "snowball", "snowball" },
        { "ender_eye", "ender_eye" },
        { "nether_star", "nether_star" },
        { "gunpowder", "gunpowder" },
        { "wheat_seeds", "seeds_wheat" },
        { "pumpkin_seeds", "seeds_pumpkin" },
        { "melon_seeds", "seeds_melon" },
        { "beetroot_seeds", "seeds_beetroot" },
        { "firework_rocket", "fireworks" },
        { "firework_star", "fireworks_charge" },
        { "chest_minecart", "minecart_chest" },
        { "hopper_minecart", "minecart_hopper" },
        { "tnt_minecart", "minecart_tnt" },
        { "command_block_minecart", "minecart_command_block" },
        { "carrot_on_a_stick", "carrot_on_a_stick" },
        { "spider_eye", "spider_eye" },
        { "fermented_spider_eye", "spider_eye_fermented" },
        { "golden_carrot", "carrot_golden" },
        { "turtle_scute", "turtle_shell_piece" },
        { "rabbit_foot", "rabbit_foot" },
    };
    std::string shortName = identifier.substr(identifier.find(':') == std::string::npos ? 0 : identifier.find(':') + 1);
    std::vector<std::string> names;
    if (!iconHint.empty()) {
        names.push_back(iconHint);
    }
    names.push_back(shortName);
    for (const auto& [from, to] : Renames) {
        if (shortName == from) {
            names.push_back(to);
        }
    }
    for (const auto& [from, to] : { std::pair<const char*, const char*> { "wooden_", "wood_" }, { "golden_", "gold_" } }) {
        size_t length = std::char_traits<char>::length(from);
        if (shortName.rfind(from, 0) == 0) {
            names.push_back(to + shortName.substr(length));
        }
    }
    for (const std::string& name : names) {
        auto found = itemTextures.find(name.find(':') == std::string::npos ? "minecraft:" + name : name);
        if (found == itemTextures.end() || found->second.empty()) {
            continue;
        }
        size_t variant = aux >= 0 && size_t(aux) < found->second.size() ? size_t(aux) : 0;
        if (!found->second[variant].empty()) {
            return found->second[variant];
        }
        if (!found->second.front().empty()) {
            return found->second.front();
        }
    }
    for (const std::string& name : names) {
        if (auto file = itemFiles.find(name); file != itemFiles.end()) {
            return file->second;
        }
    }
    const BlockVisual* found = itemVisual(identifier);
    if (!found) {
        return {};
    }
    const BlockVisual& look = *found;
    const std::vector<uint8_t>& texels = textureArray.mips[0];
    size_t layerBytes = size_t(TextureSize) * TextureSize * 4;
    auto facePixels = [&](size_t side, std::array<uint8_t, 3>& tint) -> const uint8_t* {
        tint = { 255, 255, 255 };
        uint32_t material = look.faces[side];
        if (material >= materialTable.size()) {
            return nullptr;
        }
        const Material& entry = materialTable[material];
        if (entry.tint & TintKindMask) {
            tint = { (ItemTint >> 16) & 0xFF, (ItemTint >> 8) & 0xFF, ItemTint & 0xFF };
        }
        size_t offset = size_t(entry.layer) * layerBytes;
        return offset + layerBytes <= texels.size() ? texels.data() + offset : nullptr;
    };
    std::array<std::array<uint8_t, 3>, 3> tints {};
    if (!look.emitsCubeGeometry() || (look.flags & FlagDiagnostic)) {
        const uint8_t* flat = facePixels(5, tints[0]);
        if (!flat) {
            return {};
        }
        std::vector<uint8_t> icon(size_t(ItemIconSize) * ItemIconSize * 4);
        for (uint32_t y = 0; y < ItemIconSize; ++y) {
            for (uint32_t x = 0; x < ItemIconSize; ++x) {
                const uint8_t* texel = flat + (size_t(y * TextureSize / ItemIconSize) * TextureSize + x * TextureSize / ItemIconSize) * 4;
                uint8_t* pixel = icon.data() + (size_t(y) * ItemIconSize + x) * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<uint8_t>(texel[channel] * tints[0][channel] / 255);
                }
                pixel[3] = texel[3];
            }
        }
        return icon;
    }
    std::array<const uint8_t*, 3> faces { facePixels(3, tints[0]), facePixels(5, tints[1]), facePixels(1, tints[2]) };
    return isometricIcon(faces, tints);
}

const BlockVisual* BlockAssets::itemVisual(const std::string& identifier) const
{
    if (auto carried = carriedVisuals.find(identifier); carried != carriedVisuals.end()) {
        return &carried->second;
    }
    auto block = blockByName.find(identifier);
    return block == blockByName.end() ? nullptr : &visual(block->second, true);
}

const BlockVisual* BlockAssets::itemCube(const std::string& identifier) const
{
    const BlockVisual* look = itemVisual(identifier);
    if (!look || !look->emitsCubeGeometry() || (look->flags & FlagDiagnostic)) {
        return nullptr;
    }
    return look;
}

}
