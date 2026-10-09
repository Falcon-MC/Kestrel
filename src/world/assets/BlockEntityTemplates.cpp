#include "world/BlockAssets.h"

#include "ui/Image.h"
#include "world/BlockEntityModels.h"
#include "world/BlockModels.h"
#include "world/Geometry.h"
#include "world/ConduitState.h"
#include "world/EntityFaceTiles.h"
#include "world/PackSource.h"
#include "world/PistonDisplay.h"
#include "world/PottedPlant.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace kestrel::world {

namespace {

constexpr const char* BedColors[DyeColors] = { "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "silver", "cyan", "purple", "blue", "brown", "green", "red", "black" };
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
    auto faceMaterial = [&](const std::string& path, const EntityImage& source, const EntityFace& face, uint32_t tint) {
        std::string key = "entity|" + path + '|' + std::to_string(face.u) + ',' + std::to_string(face.v) + ',' + std::to_string(face.w) + ',' + std::to_string(face.h) + '|'
            + std::to_string(face.turns) + std::to_string(face.flipU) + std::to_string(face.flipV) + '|' + std::to_string(face.tinted ? tint : 0);
        auto found = materialByKey.find(key);
        if (found != materialByKey.end()) {
            return found->second;
        }
        const bool wind = path == "textures/blocks/conduit_wind_horizontal" || path == "textures/blocks/conduit_wind_vertical";
        const uint32_t frames = wind && source.width == 64 && source.height % 32 == 0 ? std::clamp(source.height / 32, 1u, 64u) : 1;
        std::vector<std::vector<uint8_t>> framePixels;
        std::vector<uint8_t> pixels;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            EntityFace region = face;
            region.v += float(frame * 32);
            framePixels.push_back(sliceEntityFace(source, region, tint));
            pixels.insert(pixels.end(), framePixels.back().begin(), framePixels.back().end());
        }
        std::string content(pixels.begin(), pixels.end());
        auto same = materialByPixels.find(content);
        if (same != materialByPixels.end()) {
            materialByKey.emplace(key, same->second);
            return same->second;
        }
        if (layers.size() + frames > MaxTextureLayers) {
            return DiagnosticMaterial;
        }
        Material material;
        material.layer = static_cast<uint32_t>(layers.size());
        material.frameCount = frames;
        material.ticksPerFrame = wind ? 3 : 1;
        for (auto& frame : framePixels) layers.push_back(std::move(frame));
        overlayLayers.resize(layers.size(), false);
        uint32_t id = static_cast<uint32_t>(materialTable.size());
        materialTable.push_back(material);
        materialByKey.emplace(key, id);
        materialByPixels.emplace(std::move(content), id);
        return id;
    };
    auto retile = [&](const std::string& path, const EntityImage& source, std::vector<ModelQuad> quads) {
        for (auto& quad : quads) {
            std::array<uint16_t, 2> low { 65535, 65535 };
            std::array<uint16_t, 2> high {};
            for (const auto& uv : quad.uvs) {
                for (size_t axis = 0; axis < 2; ++axis) {
                    low[axis] = std::min(low[axis], uv[axis]);
                    high[axis] = std::max(high[axis], uv[axis]);
                }
            }
            EntityFace face;
            face.u = float(low[0]) * source.width / 4096.0f;
            face.v = float(low[1]) * source.height / 4096.0f;
            face.w = float(high[0] - low[0]) * source.width / 4096.0f;
            face.h = float(high[1] - low[1]) * source.height / 4096.0f;
            quad.material = faceMaterial(path, source, face, 0);
            for (auto& uv : quad.uvs) {
                for (size_t axis = 0; axis < 2; ++axis) {
                    const uint32_t span = high[axis] - low[axis];
                    uv[axis] = span ? uint16_t(uint32_t(uv[axis] - low[axis]) * 4096 / span) : 0;
                }
            }
        }
        return quads;
    };
    auto build = [&](const std::string& path, const std::vector<EntityBox>& boxes, float yaw, uint32_t tint, bool detailed = false) -> uint32_t {
        const EntityImage* source = image(path);
        if (!source) {
            return NoModelTemplate;
        }
        std::vector<EntityFace> faces;
        std::vector<ModelQuad> modelQuads = buildEntityQuads(boxes, yaw, [&](const EntityFace& face) {
            if (detailed) {
                faces.push_back(face);
                return uint32_t(faces.size() - 1);
            }
            return faceMaterial(path, *source, face, tint);
        });
        if (detailed) {
            std::vector<ModelQuad> tiles;
            for (const ModelQuad& quad : modelQuads) {
                auto faceTiles = tileEntityFace(quad, faces[quad.material], [&](const EntityFace& face) {
                    return faceMaterial(path, *source, face, tint);
                });
                tiles.insert(tiles.end(), faceTiles.begin(), faceTiles.end());
            }
            modelQuads = std::move(tiles);
        }
        return pushTemplate(modelQuads, 0);
    };

    EntityBox pot;
    pot.min = { 1, 0, 1 };
    pot.max = { 15, 16, 15 };
    for (auto& face : pot.faces) face.present = false;
    pot.faces[models::Down] = pot.faces[models::Up] = EntityFace { 0, 13, 14, 14 };
    auto neckBox = [](std::array<float, 3> low, std::array<float, 3> high, float u, float v, float w, float h, float d) {
        EntityBox box;
        box.min = low;
        box.max = high;
        box.faces = { EntityFace { u, v + d, d, h }, EntityFace { u + d + w, v + d, d, h },
            EntityFace { u + d + w, v, w, d }, EntityFace { u + d, v, w, d },
            EntityFace { u + d, v + d, w, h }, EntityFace { u + d + w + d, v + d, w, h } };
        return box;
    };
    std::vector<EntityBox> potBase {
        pot, neckBox({ 4.1f, 17.1f, 4.1f }, { 11.9f, 19.9f, 11.9f }, 0, 0, 8, 3, 8),
        neckBox({ 4.8f, 15.8f, 4.8f }, { 11.2f, 17.2f, 11.2f }, 0, 5, 6, 1, 6),
    };
    entityTemplates.potBody = build("textures/blocks/decorated_pot_base", potBase, 0, 0);
    std::vector<ModelQuad> blankPot;
    if (entityTemplates.potBody != NoModelTemplate) {
        const auto& body = templates[entityTemplates.potBody];
        blankPot.insert(blankPot.end(), quads.begin() + body.quadStart, quads.begin() + body.quadStart + body.quadCount);
    }
    static constexpr uint32_t PotFaces[4] = { models::North, models::West, models::East, models::South };
    for (size_t pattern = 0; pattern <= PotPatterns.size(); ++pattern) {
        const std::string path = pattern ? "textures/blocks/" + std::string(PotPatterns[pattern - 1]) + "_pottery_pattern"
            : "textures/blocks/decorated_pot_side";
        for (size_t side = 0; side < 4; ++side) {
            EntityBox surface = pot;
            for (auto& face : surface.faces) face.present = false;
            surface.faces[PotFaces[side]] = EntityFace { 1, 0, 14, 16 };
            const uint32_t model = build(path, { surface }, 0, 0);
            entityTemplates.potSides[pattern][side] = model;
            if (!pattern && model != NoModelTemplate) {
                const auto& face = templates[model];
                blankPot.insert(blankPot.end(), quads.begin() + face.quadStart, quads.begin() + face.quadStart + face.quadCount);
            }
        }
    }
    entityTemplates.potBlank = pushTemplate(blankPot, 0);

    static constexpr const char* ChestTextures[ChestKinds] = { "normal", "trapped", "ender", "copper_default", "copper_exposed", "copper_weathered", "copper_oxidized" };
    static constexpr const char* DoubleChestTextures[ChestKinds] = { "double_normal", "trapped_double", "ender", "copper_default_double", "copper_exposed_double", "copper_weathered_double", "copper_oxidized_double" };
    static constexpr const char* SkullTextures[SkullKinds] = {
        "textures/entity/skulls/skeleton", "textures/entity/skulls/wither_skeleton", "textures/entity/skulls/zombie", "textures/entity/skulls/creeper",
        "textures/entity/steve", "textures/entity/piglin/piglin", "textures/entity/dragon/dragon",
    };
    for (size_t kind = 0; kind < ChestKinds; ++kind) {
        std::string singlePath = std::string("textures/entity/chest/") + ChestTextures[kind];
        std::string doublePath = std::string("textures/entity/chest/") + DoubleChestTextures[kind];
        bool doubleTexture = kind != 2;
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            float yaw = 90.0f * float(rotation);
            entityTemplates.chest[kind][rotation] = build(singlePath, chestBoxes(false, 0.0f), yaw, 0);
            entityTemplates.chestBody[kind][rotation] = build(singlePath, chestBodyBoxes(false, 0.0f), yaw, 0);
            for (size_t half = 0; half < 2; ++half) {
                float offset = half == 0 ? 0.0f : -16.0f;
                entityTemplates.doubleChest[kind][half][rotation] = doubleTexture ? build(doublePath, chestBoxes(true, offset), yaw, 0) : entityTemplates.chest[kind][rotation];
                entityTemplates.doubleChestBody[kind][half][rotation] = doubleTexture ? build(doublePath, chestBodyBoxes(true, offset), yaw, 0) : entityTemplates.chestBody[kind][rotation];
            }
        }
        entityTemplates.chestLid[kind] = build(singlePath, chestLidBoxes(false, 0.0f), 0.0f, 0);
        for (size_t half = 0; half < 2; ++half) {
            entityTemplates.doubleChestLid[kind][half] = doubleTexture ? build(doublePath, chestLidBoxes(true, half == 0 ? 0.0f : -16.0f), 0.0f, 0) : entityTemplates.chestLid[kind];
        }
    }
    for (size_t color = 0; color < DyeColors; ++color) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            float yaw = 90.0f * float(rotation);
            for (size_t half = 0; half < 2; ++half) {
                entityTemplates.bed[color][half][rotation] = build(std::string("textures/entity/bed/") + BedColors[color], bedBoxes(half == 1), yaw, 0);
            }
            entityTemplates.wallBanner[color][rotation] = build("textures/entity/banner/banner", bannerBoxes(true), yaw, BannerDyeColors[color]);
        }
        for (uint32_t step = 0; step < FineRotations; ++step) {
            entityTemplates.standingBanner[color][step] = build("textures/entity/banner/banner", bannerBoxes(false), 22.5f * float(step), BannerDyeColors[color]);
        }
    }
    for (size_t wall = 0; wall < 2; ++wall) {
        auto boxes = bannerBoxes(wall != 0);
        EntityBox cloth = boxes.back();
        boxes.pop_back();
        for (uint32_t rotation = 0; rotation < (wall ? 4u : uint32_t(FineRotations)); ++rotation) {
            const uint32_t body = build("textures/entity/banner/banner", boxes, float(rotation) * (wall ? 90 : 22.5f), 0, true);
            if (wall) entityTemplates.wallBannerBody[rotation] = body;
            else entityTemplates.standingBannerBody[rotation] = body;
        }
        for (size_t color = 0; color < DyeColors; ++color) {
            entityTemplates.bannerCloth[wall][color] = build("textures/entity/banner/banner", { cloth }, 0, BannerDyeColors[color], true);
        }
        for (size_t side = 0; side < cloth.faces.size(); ++side) {
            cloth.faces[side].present = side >= models::North;
            cloth.faces[side].tinted = false;
        }
        for (size_t pattern = 0; pattern <= BannerPatternNames.size(); ++pattern) {
            const std::string path = pattern == BannerPatternNames.size() ? "textures/entity/banner/banner_pattern_illager"
                : "textures/entity/banner/banner_" + std::string(BannerPatternNames[pattern].texture);
            entityTemplates.bannerPattern[wall][pattern] = build(path, { cloth }, 0, 0, true);
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
        const std::string suffix = color < DyeColors ? BedColors[color] : "undyed";
        const std::string path = "textures/entity/shulker/shulker_" + suffix;
        const auto boxes = shulkerBoxBoxes();
        entityTemplates.shulkerLid[color] = build(path, { boxes[0] }, 0, 0);
        const EntityImage* source = image(path);
        static constexpr uint32_t Directions[6] = { models::Down, models::Up, models::North, models::South, models::West, models::East };
        for (size_t facing = 0; facing < 6; ++facing) {
            auto bake = [&](const std::vector<EntityBox>& parts) {
                if (!source) return NoModelTemplate;
                auto quads = buildEntityQuads(parts, 0, [&](const EntityFace& face) {
                    return faceMaterial(path, *source, face, 0);
                });
                return pushTemplate(models::orient(std::move(quads), Directions[facing]), 0);
            };
            entityTemplates.shulkerBox[color][facing] = bake(boxes);
            entityTemplates.shulkerBody[color][facing] = bake({ boxes[1] });
        }
    }

    const auto book = enchantingBookBoxes();
    for (size_t part = 0; part < book.size(); ++part) {
        entityTemplates.enchantingBook[part] = build("textures/entity/enchanting_table_book", { book[part] }, 0, 0);
    }

    GeometryLibrary statueGeometry;
    const std::string strawPath = "textures/blocks/straw_bed";
    const EntityImage* straw = image(strawPath);
    for (uint32_t half = 0; half < 2; ++half) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            entityTemplates.strawBed[half][rotation] = straw
                ? pushTemplate(retile(strawPath, *straw, models::strawBed(DiagnosticMaterial, half != 0, rotation)), 0)
                : NoModelTemplate;
        }
    }
    EntityBox conduit;
    conduit.min = { 5, 5, 5 };
    conduit.max = { 11, 11, 11 };
    conduit.faces = { EntityFace { 0, 6, 6, 6 }, EntityFace { 12, 6, 6, 6 },
        EntityFace { 12, 0, 6, 6, 0, false, true }, EntityFace { 6, 0, 6, 6 },
        EntityFace { 6, 6, 6, 6 }, EntityFace { 18, 6, 6, 6 } };
    entityTemplates.conduit = build("textures/blocks/conduit_base", { conduit }, 0, 0);
    const auto activeConduit = activeConduitBoxes();
    static constexpr const char* ConduitTextures[5] = {
        "conduit_cage", "conduit_wind_horizontal", "conduit_wind_vertical", "conduit_closed", "conduit_open",
    };
    for (size_t part = 0; part < activeConduit.size(); ++part) {
        entityTemplates.activeConduit[part] = build(std::string("textures/blocks/") + ConduitTextures[part], { activeConduit[part] }, 0, 0);
    }
    static constexpr const char* StatueFiles[4] = { "copper_golem", "copper_golem_sitting", "copper_golem_running", "copper_golem_star" };
    static constexpr const char* StatueIds[4] = { "geometry.copper_golem", "geometry.copper_golem.sitting", "geometry.copper_golem.running", "geometry.copper_golem.star" };
    static constexpr const char* StatueTextures[4] = { "", "_exposed", "_weathered", "_oxidized" };
    for (const char* file : StatueFiles) {
        auto layers = pack.readArchivedLayers("models/entity", std::string(file) + ".geo.json");
        for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
            statueGeometry.parse(*layer);
        }
    }
    for (size_t oxidation = 0; oxidation < 4; ++oxidation) {
        const std::string path = std::string("textures/entity/copper_golem/copper_golem") + StatueTextures[oxidation];
        const EntityImage* base = image(path);
        EntityImage combined = base ? *base : EntityImage {};
        const EntityImage* eyes = image(std::string("textures/entity/copper_golem/copper_golem_eyes") + StatueTextures[oxidation]);
        if (eyes && combined.width == eyes->width && combined.height == eyes->height && combined.rgba.size() == eyes->rgba.size()) {
            for (size_t pixel = 0; pixel + 3 < combined.rgba.size(); pixel += 4) {
                const uint32_t alpha = eyes->rgba[pixel + 3];
                const uint32_t remaining = uint32_t(combined.rgba[pixel + 3]) * (255 - alpha) / 255;
                const uint32_t total = alpha + remaining;
                for (size_t channel = 0; channel < 3; ++channel) {
                    combined.rgba[pixel + channel] = total ? uint8_t((uint32_t(eyes->rgba[pixel + channel]) * alpha
                        + uint32_t(combined.rgba[pixel + channel]) * remaining) / total) : 0;
                }
                combined.rgba[pixel + 3] = uint8_t(total);
            }
        }
        const EntityImage* source = combined.width ? &combined : nullptr;
        for (size_t pose = 0; pose < 4; ++pose) {
            const Geometry* geometry = statueGeometry.find(StatueIds[pose]);
            for (uint32_t rotation = 0; rotation < 4; ++rotation) {
                auto& model = entityTemplates.copperGolemStatue[oxidation][pose][rotation];
                model = NoModelTemplate;
                if (!source || !geometry || geometry->textureWidth <= 0 || geometry->textureHeight <= 0) {
                    continue;
                }
                BlockTransform transform;
                transform.rotation[1] = -90.0f * float(rotation + 2);
                auto modelQuads = buildGeometryQuads(*geometry, transform, [](const std::string&, int) { return DiagnosticMaterial; });
                model = pushTemplate(retile(path, *source, std::move(modelQuads)), 0);
            }
        }
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

namespace {

/**
 * Which chest a block entity draws: its look, and for the lead half of a
 * double chest which side its partner sits on (0 or 1), -1 for a single
 * chest and -2 for the half its partner draws.
 */
std::pair<size_t, int32_t> chestShape(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position)
{
    uint32_t rotation = visual.variant & 3;
    size_t kind = visual.blockEntity == EntityCopperChest ? CopperChestKind + ((visual.variant >> 2) & 3) : size_t(visual.blockEntity - EntityChest);
    if (visual.blockEntity != EntityEnderChest && data && data->get("pairx") && data->get("pairz")) {
        if (!entityInt(data, "pairlead", 0)) {
            return { kind, -2 };
        }
        int32_t dx = entityInt(data, "pairx", 0) - position[0];
        int32_t dz = entityInt(data, "pairz", 0) - position[2];
        for (uint32_t step = 0; step < rotation; ++step) {
            int32_t turned = dz;
            dz = -dx;
            dx = turned;
        }
        if (dz == 0 && (dx == 1 || dx == -1)) {
            return { kind, dx == 1 ? 0 : 1 };
        }
    }
    return { kind, -1 };
}

bool isChest(const BlockVisual& visual)
{
    return visual.blockEntity == EntityChest || visual.blockEntity == EntityTrappedChest || visual.blockEntity == EntityEnderChest || visual.blockEntity == EntityCopperChest;
}

}

/**
 * The model of a block drawn from its block entity. A double chest is drawn
 * whole by its lead half, so the other half returns no model. A chest whose
 * lid is moving leaves the lid out: it is drawn on its own every frame.
 */
uint32_t BlockAssets::blockEntityTemplate(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position) const
{
    uint32_t rotation = visual.variant & 3;
    switch (visual.blockEntity) {
    case EntityFlowerPot:
        if (auto hash = pottedPlantHash(data)) {
            auto found = entityTemplates.pottedPlants.find(*hash);
            if (found != entityTemplates.pottedPlants.end()) return found->second;
        }
        return visual.modelTemplate;
    case EntityPiston:
        return visual.modelTemplate + (data && data->get(PistonMovingKey) ? 2 : data && pistonProgress(*data) > 0 ? 1 : 0);
    case EntityDecoratedPot:
        return data && data->get(PotMovingKey) ? NoModelTemplate : entityTemplates.potBlank;
    case EntityConduit:
        return data && data->get(ConduitActiveKey) ? NoModelTemplate : entityTemplates.conduit;
    case EntityStrawBed:
        return entityTemplates.strawBed[(visual.variant >> 2) & 1][rotation];
    case EntityChest:
    case EntityTrappedChest:
    case EntityEnderChest:
    case EntityCopperChest: {
        auto [kind, half] = chestShape(visual, data, position);
        bool moving = data && data->get(ChestLidMovingKey);
        if (half == -2) {
            return NoModelTemplate;
        }
        if (half >= 0) {
            return moving ? entityTemplates.doubleChestBody[kind][half][rotation] : entityTemplates.doubleChest[kind][half][rotation];
        }
        return moving ? entityTemplates.chestBody[kind][rotation] : entityTemplates.chest[kind][rotation];
    }
    case EntityBed: {
        uint32_t color = uint32_t(std::clamp(entityInt(data, "color", DefaultBedColor), 0, int32_t(DyeColors) - 1));
        return entityTemplates.bed[color][(visual.variant >> 2) & 1][rotation];
    }
    case EntityStandingBanner:
    case EntityWallBanner: {
        if (data && data->get(BannerMovingKey)) {
            return visual.blockEntity == EntityWallBanner ? entityTemplates.wallBannerBody[rotation]
                : entityTemplates.standingBannerBody[visual.variant & 15];
        }
        uint32_t color = uint32_t(std::clamp(entityInt(data, "Base", DefaultBannerColor), 0, int32_t(DyeColors) - 1));
        return visual.blockEntity == EntityWallBanner ? entityTemplates.wallBanner[color][rotation] : entityTemplates.standingBanner[color][visual.variant & 15];
    }
    case EntityFloorSkull: {
        uint32_t step = uint32_t(std::lround(entityFloat(data, "Rotation") / 22.5f)) & 15;
        return entityTemplates.floorSkull[visual.variant & 15][step];
    }
    case EntityWallSkull:
        return entityTemplates.wallSkull[visual.variant & 15][(visual.variant >> 4) & 3];
    case EntityShulkerBox: {
        const size_t color = std::min<size_t>(visual.variant, DyeColors);
        const size_t facing = size_t(std::clamp(entityInt(data, "facing", 1), 0, 5));
        return data && data->get(ChestLidMovingKey) ? entityTemplates.shulkerBody[color][facing] : entityTemplates.shulkerBox[color][facing];
    }
    case EntityCopperGolemStatue: {
        const size_t pose = size_t(std::clamp(entityInt(data, "Pose", 0), 0, 3));
        return entityTemplates.copperGolemStatue[(visual.variant >> 2) & 3][pose][rotation];
    }
    default:
        return NoModelTemplate;
    }
}

ChestLid BlockAssets::chestLid(const BlockVisual& visual, const Tag* data, const std::array<int32_t, 3>& position) const
{
    if (visual.blockEntity == EntityShulkerBox) {
        return { entityTemplates.shulkerLid[std::min<size_t>(visual.variant, DyeColors)], 0, true,
            uint32_t(std::clamp(entityInt(data, "facing", 1), 0, 5)) };
    }
    if (!isChest(visual)) {
        return {};
    }
    auto [kind, half] = chestShape(visual, data, position);
    if (half == -2) {
        return {};
    }
    return { half >= 0 ? entityTemplates.doubleChestLid[kind][half] : entityTemplates.chestLid[kind], visual.variant & 3 };
}

}
