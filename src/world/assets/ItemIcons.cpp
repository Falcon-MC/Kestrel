#include "world/BlockAssets.h"
#include "world/BlockModels.h"

#include "world/assets/BlockRules.h"
#include "world/assets/TextureTools.h"
#include "Core/Json/Json.h"
#include "ui/Image.h"
#include "util/JsonText.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <unordered_map>

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
 * The shield's atlas entry points at its whole entity texture; the icon is
 * its front face, the 12 by 22 texel board, fit to the icon height and
 * centered like the game's front on view of it.
 */
std::vector<uint8_t> shieldIcon(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    constexpr float FaceX = 1.0f, FaceY = 1.0f, FaceWidth = 12.0f, FaceHeight = 22.0f, TextureUnits = 64.0f;
    std::vector<uint8_t> icon(size_t(ItemIconSize) * ItemIconSize * 4, 0);
    float scaleX = float(width) / TextureUnits;
    float scaleY = float(height) / TextureUnits;
    uint32_t drawnWidth = uint32_t(std::lround(ItemIconSize * FaceWidth / FaceHeight));
    uint32_t left = (ItemIconSize - drawnWidth) / 2;
    for (uint32_t y = 0; y < ItemIconSize; ++y) {
        uint32_t sourceY = std::min(height - 1, uint32_t((FaceY + (y + 0.5f) * FaceHeight / ItemIconSize) * scaleY));
        for (uint32_t x = 0; x < drawnWidth; ++x) {
            uint32_t sourceX = std::min(width - 1, uint32_t((FaceX + (x + 0.5f) * FaceWidth / drawnWidth) * scaleX));
            std::copy_n(rgba.data() + (size_t(sourceY) * width + sourceX) * 4, 4, icon.data() + (size_t(y) * ItemIconSize + left + x) * 4);
        }
    }
    return icon;
}

/**
 * The icon a resource pack item definition names under minecraft:icon, as a
 * plain name or as its texture or default texture.
 */
std::string definitionIcon(const json::Value& components)
{
    const json::Value* icon = components.get("minecraft:icon");
    if (!icon) {
        return {};
    }
    if (icon->isString()) {
        return icon->mString;
    }
    if (const json::Value* texture = icon->get("texture"); texture && texture->isString()) {
        return texture->mString;
    }
    const json::Value* textures = icon->get("textures");
    const json::Value* fallback = textures ? textures->get("default") : nullptr;
    return fallback && fallback->isString() ? fallback->mString : std::string();
}

// Turns a standing banner so its front faces the viewer.
constexpr size_t BannerIconStep = 14;

/**
 * The inventory view of a block: looked at from above the south east corner,
 * with the top a rhombus half as tall as it is wide and the sides a little
 * taller than that, the way the game draws them. The cube is as tall as the
 * icon and centered across it.
 */
struct IconProjection {
    static constexpr float SideRatio = 0.6123724f;
    static constexpr float Width = static_cast<float>(ItemIconSize) / (0.5f + SideRatio);
    static constexpr float Margin = (static_cast<float>(ItemIconSize) - Width) * 0.5f;
    static constexpr float Half = Width * 0.5f;
    static constexpr float Quarter = Width * 0.25f;
    static constexpr float Side = Width * SideRatio;

    static std::array<float, 2> project(const std::array<float, 3>& p)
    {
        return { Margin + Half * (1.0f + p[0] - p[2]), Quarter * (p[0] + p[2]) + Side * (1.0f - p[1]) };
    }
};

/**
 * Draws a block as an inventory icon: its top, south and east faces as an
 * isometric cube, shaded brighter on top and darker on the right.
 */
std::vector<uint8_t> isometricIcon(const std::array<const uint8_t*, 3>& faces, const std::array<std::array<uint8_t, 3>, 3>& tints)
{
    using P = IconProjection;
    struct Face {
        std::array<float, 2> origin;
        std::array<float, 2> axisU;
        std::array<float, 2> axisV;
        float shade;
    };
    const std::array<Face, 3> projected { {
        { P::project({ 0.0f, 1.0f, 0.0f }), { P::Half, P::Quarter }, { -P::Half, P::Quarter }, 1.0f },
        { P::project({ 0.0f, 1.0f, 1.0f }), { P::Half, P::Quarter }, { 0.0f, P::Side }, 0.8f },
        { P::project({ 1.0f, 1.0f, 1.0f }), { P::Half, -P::Quarter }, { 0.0f, P::Side }, 0.62f },
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

/**
 * Draws block model quads with the same projection as isometricIcon, keeping the nearest
 * texel per pixel, so slabs and stairs keep their shape instead of showing one flat face.
 * Seen through, every face is blended in from the farthest, so glass shows its far edges
 * and tinted glass stays tinted, the way the game draws them.
 */
template <typename Texture>
std::vector<uint8_t> modelIcon(std::vector<ModelQuad> quads, Texture texture, bool seeThrough = false)
{
    // Models reaching past their block, like a banner, are scaled on screen until they fit.
    constexpr float Size = static_cast<float>(ItemIconSize);
    float left = 0.0f, top = 0.0f, right = Size, bottom = Size;
    for (const ModelQuad& quad : quads) {
        for (const auto& corner : quad.positions) {
            auto at = IconProjection::project({ corner[0] / 256.0f, corner[1] / 256.0f, corner[2] / 256.0f });
            left = std::min(left, at[0]);
            right = std::max(right, at[0]);
            top = std::min(top, at[1]);
            bottom = std::max(bottom, at[1]);
        }
    }
    float fit = Size / std::max(right - left, bottom - top);
    auto project = [&](const std::array<float, 3>& p) {
        auto at = IconProjection::project(p);
        return std::array<float, 2> { (at[0] - (left + right) * 0.5f) * fit + Size * 0.5f, (at[1] - (top + bottom) * 0.5f) * fit + Size * 0.5f };
    };
    std::vector<uint8_t> out(size_t(ItemIconSize) * ItemIconSize * 4, 0);
    std::vector<float> depth(size_t(ItemIconSize) * ItemIconSize, -1.0e9f);
    if (seeThrough) {
        auto nearness = [](const ModelQuad& quad) {
            float sum = 0.0f;
            for (const auto& corner : quad.positions) {
                sum += float(corner[0]) + float(corner[1]) + float(corner[2]);
            }
            return sum;
        };
        std::stable_sort(quads.begin(), quads.end(), [&](const ModelQuad& a, const ModelQuad& b) { return nearness(a) < nearness(b); });
    }
    for (const ModelQuad& quad : quads) {
        uint32_t face = quad.flags & QuadFaceMask;
        bool facing = face == 2 || face == 4 || face == 6;
        if (quad.flags & QuadInward) {
            facing = !facing;
        }
        if (!seeThrough && !facing && !(quad.flags & QuadTwoSided)) {
            continue;
        }
        float shade = face == 2 ? 1.0f : face == 4 ? 0.62f : 0.8f;
        std::array<uint8_t, 3> tint { 255, 255, 255 };
        const uint8_t* pixels = texture(quad.material, tint);
        if (!pixels) {
            continue;
        }
        std::array<std::array<float, 3>, 4> corners {};
        for (size_t c = 0; c < 4; ++c) {
            for (size_t axis = 0; axis < 3; ++axis) {
                corners[c][axis] = quad.positions[c][axis] / 256.0f;
            }
        }
        auto s0 = project(corners[0]);
        auto s1 = project(corners[1]);
        auto s3 = project(corners[3]);
        float ux = s1[0] - s0[0], uy = s1[1] - s0[1];
        float vx = s3[0] - s0[0], vy = s3[1] - s0[1];
        float determinant = ux * vy - uy * vx;
        if (std::abs(determinant) < 1.0e-4f) {
            continue;
        }
        for (uint32_t y = 0; y < ItemIconSize; ++y) {
            for (uint32_t x = 0; x < ItemIconSize; ++x) {
                float px = x + 0.5f - s0[0];
                float py = y + 0.5f - s0[1];
                float a = (px * vy - py * vx) / determinant;
                float b = (ux * py - uy * px) / determinant;
                if (a < 0.0f || a >= 1.0f || b < 0.0f || b >= 1.0f) {
                    continue;
                }
                float z = 0.0f;
                for (size_t axis = 0; axis < 3; ++axis) {
                    z += corners[0][axis] + a * (corners[1][axis] - corners[0][axis]) + b * (corners[3][axis] - corners[0][axis]);
                }
                size_t at = size_t(y) * ItemIconSize + x;
                if (!seeThrough && z <= depth[at]) {
                    continue;
                }
                float u = quad.uvs[0][0] + a * (quad.uvs[1][0] - quad.uvs[0][0]) + b * (quad.uvs[3][0] - quad.uvs[0][0]);
                float v = quad.uvs[0][1] + a * (quad.uvs[1][1] - quad.uvs[0][1]) + b * (quad.uvs[3][1] - quad.uvs[0][1]);
                uint32_t tu = std::min(uint32_t(std::max(0.0f, u) * TextureSize / 4096.0f), TextureSize - 1);
                uint32_t tv = std::min(uint32_t(std::max(0.0f, v) * TextureSize / 4096.0f), TextureSize - 1);
                const uint8_t* texel = pixels + (size_t(tv) * TextureSize + tu) * 4;
                if (seeThrough) {
                    float alpha = texel[3] / 255.0f;
                    if (alpha <= 0.0f) {
                        continue;
                    }
                    uint8_t* pixel = out.data() + at * 4;
                    float below = pixel[3] / 255.0f;
                    float result = alpha + below * (1.0f - alpha);
                    for (int channel = 0; channel < 3; ++channel) {
                        float color = std::clamp(texel[channel] * shade * tint[channel] / 255.0f, 0.0f, 255.0f);
                        pixel[channel] = static_cast<uint8_t>((color * alpha + pixel[channel] * below * (1.0f - alpha)) / result);
                    }
                    pixel[3] = static_cast<uint8_t>(result * 255.0f);
                    continue;
                }
                if (texel[3] < 128) {
                    continue;
                }
                depth[at] = z;
                uint8_t* pixel = out.data() + at * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<uint8_t>(std::clamp(texel[channel] * shade * tint[channel] / 255.0f, 0.0f, 255.0f));
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
void BlockAssets::buildInterfaceAssets(PackSource& pack, const std::vector<std::shared_ptr<const PackFiles>>& packs)
{
    // Legacy items from server packs keep their icon in the resource pack
    // definition; the item registry sends no components for them.
    for (const std::shared_ptr<const PackFiles>& layer : packs) {
        for (const auto& path : layer->paths()) {
            if (path.rfind("items/", 0) != 0 || !path.ends_with(".json")) {
                continue;
            }
            auto content = layer->find(path);
            if (!content) continue;
            std::unique_ptr<json::Value> parsed = json::parse(util::stripJsonComments(*content));
            const json::Value* item = parsed ? parsed->get("minecraft:item") : nullptr;
            const json::Value* description = item ? item->get("description") : nullptr;
            const json::Value* identifier = description ? description->get("identifier") : nullptr;
            const json::Value* components = item ? item->get("components") : nullptr;
            if (!identifier || !identifier->isString() || !components) {
                continue;
            }
            if (std::string icon = definitionIcon(*components); !icon.empty()) {
                itemIconNames.try_emplace(identifier->mString, std::move(icon));
            }
        }
    }
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
            } else if (path.rfind("textures/items/leather_", 0) == 0) {
                dyeItemFiles.emplace(path.substr(std::string("textures/items/").size()), resizeNearest(image.rgba, image.width, image.height, ItemIconSize));
                ui::applyDyeMask(image.rgba, ui::LeatherColor);
            } else if (path == "textures/items/wolf_armor_dyed") {
                dyeItemFiles.emplace("wolf_armor", resizeNearest(image.rgba, image.width, image.height, ItemIconSize));
            }
            found = decoded.emplace(path, std::move(image)).first;
        }
        width = found->second.width;
        height = found->second.height;
        return found->second.rgba.empty() ? nullptr : &found->second.rgba;
    };
    std::vector<std::string> atlases = pack.readTextLayers("textures/item_texture.json");
    for (std::string& archived : pack.readArchivedLayers("textures", "item_texture.json")) {
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
    // Candles keep their item pictures in a folder of their own that no atlas names.
    for (const char* folder : { "candles" }) {
        std::string directory = std::string("textures/items/") + folder;
        for (const std::string& name : pack.archiveEntries(directory)) {
            size_t dot = name.rfind('.');
            std::string stem = name.substr(0, dot);
            if (dot == std::string::npos || name.find('/') != std::string::npos || itemFiles.count(stem)) {
                continue;
            }
            uint32_t width = 0;
            uint32_t height = 0;
            const std::vector<uint8_t>* rgba = load(directory + "/" + stem, width, height);
            if (rgba && width == height) {
                itemFiles.emplace(stem, resizeNearest(*rgba, width, height, ItemIconSize));
            }
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
                if (rgba && identifier == "minecraft:shield" && path.starts_with("textures/entity/")) {
                    variants.push_back(shieldIcon(*rgba, width, height));
                    continue;
                }
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
 * The atlas variant the game draws a potion with for its aux, since potion
 * atlas entries are ordered by effect rather than by aux. Other items use the
 * aux directly.
 */
static int32_t potionVariant(const std::string& shortName, int32_t aux)
{
    static constexpr int8_t Drinkable[] = { 0, 0, 0, 0, 0, 16, 16, 14, 14, 8, 8, 8, 12, 12, 1, 1, 1, 2, 2, 13, 13, 6, 6, 7, 7, 19, 19, 19, 10, 10, 10, 5, 5, 5, 18, 18, 20, 25, 25, 25, 26, 26, 2, 27, 28, 29, 30 };
    static constexpr int8_t Splash[] = { 0, 0, 0, 0, 0, 16, 16, 14, 14, 8, 8, 8, 12, 12, 1, 1, 1, 2, 2, 13, 13, 6, 6, 7, 7, 19, 19, 19, 10, 10, 10, 5, 5, 5, 18, 18, 20, 24, 24, 24, 25, 25, 2, 26, 27, 28, 29 };
    static constexpr int8_t Lingering[] = { 0, 0, 0, 0, 0, 11, 11, 10, 10, 6, 6, 6, 8, 8, 1, 1, 1, 2, 2, 9, 9, 4, 4, 5, 5, 13, 13, 13, 7, 7, 7, 3, 3, 3, 12, 12, 14, 15, 15, 15, 16, 16, 2, 17, 18, 19, 20 };
    const int8_t* table = nullptr;
    size_t size = 0;
    if (shortName == "potion") {
        table = Drinkable;
        size = std::size(Drinkable);
    } else if (shortName == "splash_potion") {
        table = Splash;
        size = std::size(Splash);
    } else if (shortName == "lingering_potion") {
        table = Lingering;
        size = std::size(Lingering);
    } else {
        return aux;
    }
    return aux >= 0 && size_t(aux) < size ? table[aux] : 0;
}

/**
 * The inventory icon of an item: its item texture for the aux variant, or for
 * block items an isometric cube of the block's default state. Empty when the
 * item has neither.
 */
std::vector<uint8_t> BlockAssets::itemIcon(const std::string& identifier, int32_t aux, const std::string& iconHint, std::optional<uint32_t> customColor) const
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
        { "ink_sac", "dye_powder_black" },
        { "cocoa_beans", "dye_powder_brown" },
        { "lapis_lazuli", "dye_powder_blue" },
        { "bone_meal", "dye_powder_white" },
        { "black_dye", "dye_powder_black_new" },
        { "brown_dye", "dye_powder_brown_new" },
        { "blue_dye", "dye_powder_blue_new" },
        { "white_dye", "dye_powder_white_new" },
        { "red_dye", "dye_powder_red" },
        { "green_dye", "dye_powder_green" },
        { "purple_dye", "dye_powder_purple" },
        { "cyan_dye", "dye_powder_cyan" },
        { "light_gray_dye", "dye_powder_silver" },
        { "gray_dye", "dye_powder_gray" },
        { "pink_dye", "dye_powder_pink" },
        { "lime_dye", "dye_powder_lime" },
        { "yellow_dye", "dye_powder_yellow" },
        { "light_blue_dye", "dye_powder_light_blue" },
        { "magenta_dye", "dye_powder_magenta" },
        { "orange_dye", "dye_powder_orange" },
        { "oak_sign", "sign" },
        { "spruce_sign", "sign_spruce" },
        { "birch_sign", "sign_birch" },
        { "jungle_sign", "sign_jungle" },
        { "acacia_sign", "sign_acacia" },
        { "dark_oak_sign", "sign_darkoak" },
        { "crimson_sign", "crimson_sign_item" },
        { "warped_sign", "warped_sign_item" },
        { "lodestone_compass", "lodestonecompass_item" },
        { "bow", "bow_standby" },
        { "crossbow", "crossbow_standby" },
        { "redstone", "redstone_dust" },
        { "book", "book_normal" },
        { "slime_ball", "slimeball" },
        { "minecart", "minecart_normal" },
    };
    // The game gives these items one frame of a shared atlas entry.
    struct Frame {
        const char* item;
        const char* atlas;
        size_t index;
    };
    static const Frame Frames[] = {
        { "bucket", "bucket", 0 },
        { "milk_bucket", "bucket", 1 },
        { "water_bucket", "bucket", 2 },
        { "lava_bucket", "bucket", 3 },
        { "cod_bucket", "bucket", 4 },
        { "salmon_bucket", "bucket", 5 },
        { "tropical_fish_bucket", "bucket", 6 },
        { "pufferfish_bucket", "bucket", 7 },
        { "powder_snow_bucket", "bucket", 8 },
        { "axolotl_bucket", "bucket", 9 },
        { "tadpole_bucket", "bucket", 10 },
        { "sulfur_cube_bucket", "bucket", 11 },
        { "oak_boat", "boat", 0 },
        { "spruce_boat", "boat", 1 },
        { "birch_boat", "boat", 2 },
        { "jungle_boat", "boat", 3 },
        { "acacia_boat", "boat", 4 },
        { "dark_oak_boat", "boat", 5 },
        { "mangrove_boat", "boat", 6 },
        { "bamboo_raft", "boat", 7 },
        { "cherry_boat", "boat", 8 },
        { "pale_oak_boat", "boat", 9 },
        { "poplar_boat", "boat", 10 },
        { "oak_chest_boat", "chest_boat", 0 },
        { "spruce_chest_boat", "chest_boat", 1 },
        { "birch_chest_boat", "chest_boat", 2 },
        { "jungle_chest_boat", "chest_boat", 3 },
        { "acacia_chest_boat", "chest_boat", 4 },
        { "dark_oak_chest_boat", "chest_boat", 5 },
        { "mangrove_chest_boat", "chest_boat", 6 },
        { "bamboo_chest_raft", "chest_boat", 7 },
        { "cherry_chest_boat", "chest_boat", 8 },
        { "pale_oak_chest_boat", "chest_boat", 9 },
        { "poplar_chest_boat", "chest_boat", 10 },
    };
    std::string shortName = identifier.substr(identifier.find(':') == std::string::npos ? 0 : identifier.find(':') + 1);
    std::vector<std::string> names;
    if (!iconHint.empty()) {
        names.push_back(iconHint);
    }
    if (auto named = itemIconNames.find(identifier); named != itemIconNames.end()) {
        names.push_back(named->second);
    }
    if (identifier.find(':') != std::string::npos) {
        names.push_back(identifier);
    }
    names.push_back(shortName);
    for (const auto& [from, to] : Renames) {
        if (shortName == from) {
            names.push_back(to);
        }
    }
    if (iconHint.empty()) {
        for (const Frame& frame : Frames) {
            if (shortName != frame.item) {
                continue;
            }
            auto found = itemTextures.find(std::string("minecraft:") + frame.atlas);
            if (found != itemTextures.end() && frame.index < found->second.size() && !found->second[frame.index].empty()) {
                return found->second[frame.index];
            }
        }
    }
    if (shortName.ends_with("_harness")) {
        names.push_back("harness_" + shortName.substr(0, shortName.size() - std::char_traits<char>::length("_harness")));
    }
    for (const auto& [from, to] : { std::pair<const char*, const char*> { "wooden_", "wood_" }, { "golden_", "gold_" } }) {
        size_t length = std::char_traits<char>::length(from);
        if (shortName.rfind(from, 0) == 0) {
            names.push_back(to + shortName.substr(length));
        }
    }
    for (const std::string& name : names) {
        std::string shortName = name.starts_with("minecraft:") ? name.substr(10) : name;
        if (customColor) {
            if (auto dye = dyeItemFiles.find(shortName); dye != dyeItemFiles.end()) {
                std::vector<uint8_t> pixels = dye->second;
                uint32_t rgb = *customColor;
                ui::applyDyeMask(pixels, { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb) });
                return pixels;
            }
        }
        auto found = itemTextures.find(name.find(':') == std::string::npos ? "minecraft:" + name : name);
        if (found == itemTextures.end() || found->second.empty()) {
            continue;
        }
        int32_t frame = potionVariant(shortName, aux);
        size_t variant = frame >= 0 && size_t(frame) < found->second.size() ? size_t(frame) : 0;
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
    const std::vector<uint8_t>& texels = textureArray.mips[0];
    size_t layerBytes = size_t(TextureSize) * TextureSize * 4;
    auto materialPixels = [&](uint32_t material, std::array<uint8_t, 3>& tint) -> const uint8_t* {
        tint = { 255, 255, 255 };
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
    if (identifier == "minecraft:banner") {
        uint32_t color = uint32_t(std::clamp(aux, 0, int32_t(DyeColors) - 1));
        return modelIcon(templateQuads(entityTemplates.standingBanner[color][BannerIconStep]), materialPixels);
    }
    const BlockVisual* found = itemVisual(identifier);
    if (!found) {
        return {};
    }
    const BlockVisual& look = *found;
    auto facePixels = [&](size_t side, std::array<uint8_t, 3>& tint) {
        return materialPixels(look.faces[side], tint);
    };
    if (look.hasModel() && look.modelTemplate < templates.size()) {
        std::vector<ModelQuad> shape = itemGeometry(identifier);
        std::vector<uint8_t> icon = modelIcon(shape, materialPixels);
        for (size_t alpha = 3; alpha < icon.size(); alpha += 4) {
            if (icon[alpha]) {
                return icon;
            }
        }
    }
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
    if ((look.flags & (FlagTranslucent | FlagCullSame)) && !(look.flags & FlagLeafModel)) {
        auto cube = models::cuboid(look.faces, { 0, 0, 0 }, { 256, 256, 256 });
        return modelIcon({ cube.begin(), cube.end() }, materialPixels, true);
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

std::vector<ModelQuad> BlockAssets::itemGeometry(const std::string& identifier) const
{
    const BlockVisual* look = itemVisual(identifier);
    if (!look || !look->hasModel() || look->modelTemplate >= templates.size()) return {};
    std::string shortName = identifier.substr(identifier.find(':') == std::string::npos ? 0 : identifier.find(':') + 1);
    if (itemFiles.contains(shortName) || itemTextures.contains(identifier)) return {};
    const ModelTemplate& model = templates[look->modelTemplate];
    if (model.flags & TemplatePane) return {};
    // Torches show their flat texture in the inventory, not the model.
    if (rules::modelKind(shortName) == rules::ModelKind::Torch) return {};
    if (model.flags & TemplateWall) return models::wall(look->faces, (1u << 8) | (1u << 2) | (1u << 6));
    if (model.flags & (TemplateFenceWood | TemplateFenceNether)) {
        std::vector<ModelQuad> shape;
        for (int16_t shift : { -96, 96 }) {
            for (ModelQuad quad : models::fencePost(look->faces[models::South])) {
                for (auto& corner : quad.positions) corner[0] = static_cast<int16_t>(corner[0] + shift);
                shape.push_back(quad);
            }
        }
        auto arms = models::fenceArms(look->faces[models::South], 2 | 8);
        shape.insert(shape.end(), arms.begin(), arms.end());
        return shape;
    }
    std::vector<ModelQuad> shape = templateQuads(look->modelTemplate);
    if (look->blockEntity == EntityNone && std::all_of(shape.begin(), shape.end(), [](const ModelQuad& quad) { return (quad.flags & QuadTwoSided) != 0; })) return {};
    return shape;
}

std::vector<ModelQuad> BlockAssets::templateQuads(uint32_t modelTemplate) const
{
    if (modelTemplate >= templates.size()) return {};
    const ModelTemplate& model = templates[modelTemplate];
    size_t end = std::min<size_t>(quads.size(), size_t(model.quadStart) + model.quadCount);
    if (model.quadStart >= end) return {};
    return { quads.begin() + model.quadStart, quads.begin() + end };
}

/**
 * Reads how long each item can be used from the behavior pack's items: the
 * use_modifiers use_duration in seconds, or the older use_duration in ticks.
 * Files that still carry a legacy identifier are stored under the item's
 * current name.
 */
void BlockAssets::loadItemUseDurations(PackSource& behaviors)
{
    static const std::unordered_map<std::string, std::string> legacyNames {
        { "minecraft:appleEnchanted", "minecraft:enchanted_golden_apple" },
        { "minecraft:muttonCooked", "minecraft:cooked_mutton" },
        { "minecraft:muttonRaw", "minecraft:mutton" },
        { "minecraft:clownfish", "minecraft:tropical_fish" },
        { "minecraft:fish", "minecraft:cod" },
        { "minecraft:cooked_fish", "minecraft:cooked_cod" },
    };
    itemUseDurations.clear();
    for (const std::string& entry : behaviors.archiveEntries("items")) {
        if (entry.size() <= 5 || entry.compare(entry.size() - 5, 5, ".json") != 0) {
            continue;
        }
        std::string text;
        if (!behaviors.readArchived("items", entry, text)) {
            continue;
        }
        std::unique_ptr<json::Value> document = json::parse(text);
        const json::Value* item = document ? document->get("minecraft:item") : nullptr;
        const json::Value* description = item ? item->get("description") : nullptr;
        const json::Value* identifier = description ? description->get("identifier") : nullptr;
        const json::Value* components = item ? item->get("components") : nullptr;
        if (!identifier || !identifier->isString() || !components) {
            continue;
        }
        double ticks = 0.0;
        const json::Value* modifiers = components->get("minecraft:use_modifiers");
        const json::Value* seconds = modifiers ? modifiers->get("use_duration") : nullptr;
        if (seconds && seconds->isNumber()) {
            ticks = seconds->mNumber * 20.0;
        } else if (const json::Value* legacy = components->get("minecraft:use_duration"); legacy && legacy->isNumber()) {
            ticks = legacy->mNumber;
        }
        if (!std::isfinite(ticks) || ticks < 1.0) {
            continue;
        }
        std::string name = identifier->string();
        if (auto renamed = legacyNames.find(name); renamed != legacyNames.end()) {
            name = renamed->second;
        }
        itemUseDurations[name] = static_cast<int32_t>(std::lround(ticks));
    }
}

}
