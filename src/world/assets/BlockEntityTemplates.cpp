#include "world/BlockAssets.h"

#include "ui/Image.h"
#include "world/BlockEntityModels.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace kestrel::world {

namespace {

constexpr const char* BedColors[DyeColors] = { "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "silver", "cyan", "purple", "blue", "brown", "green", "red", "black" };
constexpr uint32_t BannerColors[DyeColors] = { 0x1D1D21, 0xB02E26, 0x5E7C16, 0x835432, 0x3C44AA, 0x8932B8, 0x169C9C, 0x9D9D97, 0x474F52, 0xF38BAA, 0x80C71F, 0xFED83D, 0x3AB3DA, 0xC74EBD, 0xF9801D, 0xF9FFFE };
constexpr uint32_t DefaultBedColor = 14;
constexpr uint32_t DefaultBannerColor = 15;

int32_t entityInt(const Tag* data, const char* key, int32_t fallback)
{
    const Tag* value = data ? data->get(key) : nullptr;
    if (!value) {
        return fallback;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return value->asByte();
    case Tag::Type::Short:
        return value->asShort();
    case Tag::Type::Int:
        return value->asInt();
    case Tag::Type::Float:
        return static_cast<int32_t>(std::lround(value->asFloat()));
    default:
        return fallback;
    }
}

float entityFloat(const Tag* data, const char* key)
{
    const Tag* value = data ? data->get(key) : nullptr;
    if (!value) {
        return 0.0f;
    }
    return value->getType() == Tag::Type::Float ? value->asFloat() : static_cast<float>(entityInt(data, key, 0));
}

}

/**
 * Cuts every face of the chest, bed, skull and banner models out of their
 * entity textures into block texture layers and bakes one model per look and
 * rotation. Blocks whose texture is missing fall back to the diagnostic cube.
 */
void BlockAssets::buildBlockEntityTemplates(PackSource& pack, std::vector<std::vector<uint8_t>>& layers, std::vector<bool>& overlayLayers, std::map<std::string, uint32_t>& materialByKey,
    const std::function<uint32_t(const std::vector<ModelQuad>&, uint32_t)>& pushTemplate)
{
    std::map<std::string, EntityImage> images;
    std::unordered_map<std::string, uint32_t> materialByPixels;
    auto image = [&](const std::string& path) -> const EntityImage* {
        auto found = images.find(path);
        if (found == images.end()) {
            EntityImage loaded;
            std::string encoded;
            if (pack.readTexture(path, encoded)) {
                ui::decodeImage(encoded, loaded.width, loaded.height, loaded.rgba);
            }
            found = images.emplace(path, std::move(loaded)).first;
        }
        return found->second.width ? &found->second : nullptr;
    };
    auto build = [&](const std::string& path, const std::vector<EntityBox>& boxes, float yaw, uint32_t tint) -> uint32_t {
        const EntityImage* source = image(path);
        if (!source) {
            return NoModelTemplate;
        }
        std::vector<ModelQuad> modelQuads = buildEntityQuads(boxes, yaw, [&](const EntityFace& face) {
            std::string key = "entity|" + path + '|' + std::to_string(face.u) + ',' + std::to_string(face.v) + ',' + std::to_string(face.w) + ',' + std::to_string(face.h) + '|'
                + std::to_string(face.turns) + std::to_string(face.flipU) + std::to_string(face.flipV) + '|' + std::to_string(face.tinted ? tint : 0);
            auto found = materialByKey.find(key);
            if (found != materialByKey.end()) {
                return found->second;
            }
            std::vector<uint8_t> pixels = sliceEntityFace(*source, face, tint);
            std::string content(pixels.begin(), pixels.end());
            auto same = materialByPixels.find(content);
            if (same != materialByPixels.end()) {
                materialByKey.emplace(key, same->second);
                return same->second;
            }
            if (layers.size() >= MaxTextureLayers) {
                return DiagnosticMaterial;
            }
            Material material;
            material.layer = static_cast<uint32_t>(layers.size());
            layers.push_back(std::move(pixels));
            overlayLayers.resize(layers.size(), false);
            uint32_t id = static_cast<uint32_t>(materialTable.size());
            materialTable.push_back(material);
            materialByKey.emplace(key, id);
            materialByPixels.emplace(std::move(content), id);
            return id;
        });
        return pushTemplate(modelQuads, 0);
    };

    static constexpr const char* ChestTextures[ChestKinds] = { "normal", "trapped", "ender", "copper_default", "copper_exposed", "copper_weathered", "copper_oxidized" };
    static constexpr const char* DoubleChestTextures[ChestKinds] = { "double_normal", "trapped_double", "ender", "copper_default_double", "copper_exposed_double", "copper_weathered_double", "copper_oxidized_double" };
    static constexpr const char* SkullTextures[SkullKinds] = {
        "textures/entity/skulls/skeleton", "textures/entity/skulls/wither_skeleton", "textures/entity/skulls/zombie", "textures/entity/skulls/creeper",
        "textures/entity/steve", "textures/entity/piglin/piglin", "textures/entity/dragon/dragon",
    };
    for (size_t kind = 0; kind < ChestKinds; ++kind) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            float yaw = 90.0f * float(rotation);
            entityTemplates.chest[kind][rotation] = build(std::string("textures/entity/chest/") + ChestTextures[kind], chestBoxes(false, 0.0f), yaw, 0);
            std::string doublePath = std::string("textures/entity/chest/") + DoubleChestTextures[kind];
            bool doubleTexture = kind != 2;
            entityTemplates.doubleChest[kind][0][rotation] = doubleTexture ? build(doublePath, chestBoxes(true, 0.0f), yaw, 0) : entityTemplates.chest[kind][rotation];
            entityTemplates.doubleChest[kind][1][rotation] = doubleTexture ? build(doublePath, chestBoxes(true, -16.0f), yaw, 0) : entityTemplates.chest[kind][rotation];
        }
    }
    for (size_t color = 0; color < DyeColors; ++color) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            float yaw = 90.0f * float(rotation);
            for (size_t half = 0; half < 2; ++half) {
                entityTemplates.bed[color][half][rotation] = build(std::string("textures/entity/bed/") + BedColors[color], bedBoxes(half == 1), yaw, 0);
            }
            entityTemplates.wallBanner[color][rotation] = build("textures/entity/banner/banner", bannerBoxes(true), yaw, BannerColors[color]);
        }
        for (uint32_t step = 0; step < FineRotations; ++step) {
            entityTemplates.standingBanner[color][step] = build("textures/entity/banner/banner", bannerBoxes(false), 22.5f * float(step), BannerColors[color]);
        }
    }
    for (size_t kind = 0; kind < SkullKinds; ++kind) {
        auto boxes = kind == 5 ? piglinHeadBoxes : kind == 6 ? dragonHeadBoxes : skullBoxes;
        for (uint32_t step = 0; step < FineRotations; ++step) {
            entityTemplates.floorSkull[kind][step] = build(SkullTextures[kind], boxes(false), 22.5f * float(step), 0);
        }
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            entityTemplates.wallSkull[kind][rotation] = build(SkullTextures[kind], boxes(true), 90.0f * float(rotation), 0);
        }
    }
    for (size_t color = 0; color <= DyeColors; ++color) {
        std::string suffix = color < DyeColors ? BedColors[color] : "undyed";
        entityTemplates.shulkerBox[color] = build("textures/entity/shulker/shulker_" + suffix, shulkerBoxBoxes(), 0.0f, 0);
    }

    for (BlockVisual& visual : visuals) {
        if (visual.blockEntity == EntityNone) {
            continue;
        }
        visual.modelTemplate = blockEntityTemplate(visual, nullptr, {});
        if (visual.modelTemplate == NoModelTemplate) {
            visual.blockEntity = EntityNone;
            visual.flags = FlagDiagnostic;
            ++diagnosticCount;
        }
    }
}

/**
 * The model of a block drawn from its block entity. A double chest is drawn
 * whole by its lead half, so the other half returns no model.
 */
uint32_t BlockAssets::blockEntityTemplate(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position) const
{
    uint32_t rotation = visual.variant & 3;
    switch (visual.blockEntity) {
    case EntityChest:
    case EntityTrappedChest:
    case EntityEnderChest:
    case EntityCopperChest: {
        size_t kind = visual.blockEntity == EntityCopperChest ? CopperChestKind + ((visual.variant >> 2) & 3) : size_t(visual.blockEntity - EntityChest);
        if (visual.blockEntity != EntityEnderChest && data && data->get("pairx") && data->get("pairz")) {
            if (!entityInt(data, "pairlead", 0)) {
                return NoModelTemplate;
            }
            int32_t dx = entityInt(data, "pairx", 0) - position[0];
            int32_t dz = entityInt(data, "pairz", 0) - position[2];
            for (uint32_t step = 0; step < rotation; ++step) {
                int32_t turned = dz;
                dz = -dx;
                dx = turned;
            }
            if (dz == 0 && (dx == 1 || dx == -1)) {
                return entityTemplates.doubleChest[kind][dx == 1 ? 0 : 1][rotation];
            }
        }
        return entityTemplates.chest[kind][rotation];
    }
    case EntityBed: {
        uint32_t color = uint32_t(std::clamp(entityInt(data, "color", DefaultBedColor), 0, int32_t(DyeColors) - 1));
        return entityTemplates.bed[color][(visual.variant >> 2) & 1][rotation];
    }
    case EntityStandingBanner:
    case EntityWallBanner: {
        uint32_t color = uint32_t(std::clamp(entityInt(data, "Base", DefaultBannerColor), 0, int32_t(DyeColors) - 1));
        return visual.blockEntity == EntityWallBanner ? entityTemplates.wallBanner[color][rotation] : entityTemplates.standingBanner[color][visual.variant & 15];
    }
    case EntityFloorSkull: {
        uint32_t step = uint32_t(std::lround(entityFloat(data, "Rotation") / 22.5f)) & 15;
        return entityTemplates.floorSkull[visual.variant & 15][step];
    }
    case EntityWallSkull:
        return entityTemplates.wallSkull[visual.variant & 15][(visual.variant >> 4) & 3];
    case EntityShulkerBox:
        return entityTemplates.shulkerBox[std::min<size_t>(visual.variant, DyeColors)];
    default:
        return NoModelTemplate;
    }
}

}
