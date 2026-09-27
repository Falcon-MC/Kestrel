#include "world/BlockAssets.h"
#include "BlockRules.h"
#include "TextureTools.h"

#include "Core/Json/Json.h"
#include "Protocol/BlockStateHasher.h"
#include "ui/Image.h"
#include "world/BlockEntityModels.h"
#include "world/BlockModels.h"
#include "world/Geometry.h"
#include "world/Molang.h"
#include "world/PackSource.h"
#include "util/JsonText.h"
#include "util/Text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace kestrel::world {

using namespace rules;
using util::contains;
using util::endsWith;
using util::startsWith;
using util::stripJsonComments;

std::shared_ptr<const BlockAssets> BlockAssets::shared(std::string& error)
{
    static std::mutex mutex;
    static std::shared_ptr<const BlockAssets> instance;
    static std::string failure;
    std::lock_guard<std::mutex> guard(mutex);
    if (!instance && failure.empty()) {
        auto assets = std::shared_ptr<BlockAssets>(new BlockAssets());
        if (assets->build({}, failure)) {
            instance = assets;
        }
    }
    error = failure;
    return instance;
}

std::shared_ptr<const BlockAssets> BlockAssets::create(const std::vector<std::shared_ptr<const PackFiles>>& packs, const std::vector<CustomBlock>& customBlocks, std::string& error)
{
    if (packs.empty() && customBlocks.empty()) {
        return shared(error);
    }
    auto assets = std::shared_ptr<BlockAssets>(new BlockAssets());
    assets->customs = customBlocks;
    if (!assets->build(packs, error)) {
        return nullptr;
    }
    return assets;
}

const std::string& BlockAssets::nameAt(size_t index) const
{
    if (index < registry.records().size()) {
        return registry.records()[index].name;
    }
    return customs[customStates[index - registry.records().size()].block].name;
}

std::shared_ptr<const SequentialMap> BlockAssets::sequentialMap() const
{
    std::unordered_set<std::string> declared;
    for (const CustomBlock& custom : customs) {
        declared.insert(custom.name);
    }

    std::vector<std::pair<uint64_t, int32_t>> entries;
    entries.reserve(registry.records().size() + customStates.size());
    for (size_t i = 0; i < registry.records().size(); ++i) {
        const std::string& name = registry.records()[i].name;
        if (registry.isDataDriven(name) && !declared.contains(name)) {
            continue;
        }
        entries.emplace_back(BlockRegistry::nameHash(name), static_cast<int32_t>(i));
    }
    for (size_t i = 0; i < customStates.size(); ++i) {
        const std::string& name = customs[customStates[i].block].name;
        entries.emplace_back(BlockRegistry::nameHash(name), static_cast<int32_t>(registry.records().size() + i));
    }

    std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    auto map = std::make_shared<SequentialMap>();
    map->reserve(entries.size());
    for (const auto& entry : entries) {
        map->push_back(entry.second);
    }
    return map;
}

std::string BlockAssets::describe(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return "implicit air";
    }
    int32_t index = -1;
    if (!hashed && sequential) {
        if (networkValue < sequential->size()) {
            index = (*sequential)[networkValue];
        }
    } else {
        index = registry.resolve(networkValue, hashed);
        if (index < 0) {
            auto custom = customByHash.find(networkValue);
            index = custom == customByHash.end() ? -1 : static_cast<int32_t>(custom->second);
        }
    }
    if (index < 0) {
        return "unknown #" + std::to_string(networkValue);
    }
    return nameAt(static_cast<size_t>(index)) + " #" + std::to_string(networkValue);
}

std::string BlockAssets::blockName(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return "minecraft:air";
    }
    int32_t index = -1;
    if (!hashed && sequential) {
        if (networkValue < sequential->size()) {
            index = (*sequential)[networkValue];
        }
    } else {
        index = registry.resolve(networkValue, hashed);
        if (index < 0) {
            auto custom = customByHash.find(networkValue);
            index = custom == customByHash.end() ? -1 : static_cast<int32_t>(custom->second);
        }
    }
    return index < 0 ? std::string() : nameAt(static_cast<size_t>(index));
}

const BlockVisual& BlockAssets::visual(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    static const BlockVisual air { FlagAir, {} };
    static const BlockVisual diagnostic { FlagDiagnostic, {} };
    if (networkValue == 0xFFFFFFFFu) {
        return air;
    }
    if (!hashed && sequential) {
        if (networkValue >= sequential->size() || (*sequential)[networkValue] < 0) {
            unresolved.fetch_add(1, std::memory_order_relaxed);
            lastUnresolved.store(networkValue, std::memory_order_relaxed);
            return diagnostic;
        }
        return visuals[static_cast<size_t>((*sequential)[networkValue])];
    }
    int32_t index = registry.resolve(networkValue, hashed);
    if (index < 0 && hashed) {
        auto custom = customByHash.find(networkValue);
        index = custom == customByHash.end() ? -1 : static_cast<int32_t>(custom->second);
    }
    if (index < 0) {
        unresolved.fetch_add(1, std::memory_order_relaxed);
        lastUnresolved.store(networkValue, std::memory_order_relaxed);
        return diagnostic;
    }
    return visuals[static_cast<size_t>(index)];
}

uint32_t BlockAssets::stateHash(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return 0;
    }
    int32_t index = -1;
    if (!hashed && sequential) {
        if (networkValue < sequential->size()) {
            index = (*sequential)[networkValue];
        }
    } else {
        if (hashed) {
            return networkValue;
        }
        index = registry.resolve(networkValue, hashed);
    }
    const std::vector<BlockRecord>& records = registry.records();
    if (index < 0 || static_cast<size_t>(index) >= records.size()) {
        return 0;
    }
    return records[static_cast<size_t>(index)].networkHash;
}

bool BlockAssets::build(const std::vector<std::shared_ptr<const PackFiles>>& packs, std::string& error)
{
    if (!registry.load(error)) {
        return false;
    }

    std::filesystem::path root = PackSource::locateVanilla();
    if (root.empty()) {
        error = "no vanilla resource pack found (install Minecraft Bedrock or set KESTREL_VANILLA_PACK)";
        return false;
    }
    PackSource pack(root);
    pack.setOverlays(packs);

    std::vector<std::unique_ptr<json::Value>> documents;
    std::vector<const json::Value*> blockLayers;
    std::vector<const json::Value*> terrainLayers;
    for (const std::string& text : pack.readTextLayers("blocks.json")) {
        std::unique_ptr<json::Value> parsed = json::parse(stripJsonComments(text));
        if (parsed && parsed->isObject()) {
            blockLayers.push_back(parsed.get());
            documents.push_back(std::move(parsed));
        }
    }
    for (const std::string& text : pack.readTextLayers("textures/terrain_texture.json")) {
        std::unique_ptr<json::Value> parsed = json::parse(stripJsonComments(text));
        const json::Value* data = parsed ? parsed->get("texture_data") : nullptr;
        if (data && data->isObject()) {
            terrainLayers.push_back(data);
            documents.push_back(std::move(parsed));
        }
    }
    if (blockLayers.empty() || terrainLayers.empty()) {
        error = "could not read blocks.json or terrain_texture.json from " + root.string();
        return false;
    }
    auto blockEntry = [&](const std::string& key) -> const json::Value* {
        for (const json::Value* layer : blockLayers) {
            if (const json::Value* found = layer->get(key)) {
                return found;
            }
        }
        return nullptr;
    };
    auto terrainKnown = [&](const std::string& key) {
        for (const json::Value* layer : terrainLayers) {
            if (layer->get(key)) {
                return true;
            }
        }
        return false;
    };

    std::vector<std::vector<uint8_t>> layers { diagnosticTexture() };
    std::map<std::string, uint32_t> materialByKey;
    materialTable.push_back({});

    auto terrainEntry = [&](const std::string& textureKey) -> const json::Value* {
        for (const json::Value* data : terrainLayers) {
            if (const json::Value* entry = data->get(textureKey)) {
                return entry;
            }
        }
        return nullptr;
    };

    std::vector<Flipbook> flipbooks;
    for (const std::string& text : pack.readTextLayers("textures/flipbook_textures.json")) {
        std::unique_ptr<json::Value> parsed = json::parse(stripJsonComments(text));
        if (!parsed || !parsed->isArray()) {
            continue;
        }
        for (const auto& item : parsed->mArray) {
            std::optional<Flipbook> flipbook = parseFlipbook(*item);
            if (!flipbook) {
                continue;
            }
            bool shadowed = std::any_of(flipbooks.begin(), flipbooks.end(), [&](const Flipbook& existing) {
                return existing.atlasTile == flipbook->atlasTile && existing.atlasIndex == flipbook->atlasIndex;
            });
            if (!shadowed) {
                flipbooks.push_back(std::move(*flipbook));
            }
        }
    }

    std::map<std::string, std::vector<std::vector<uint8_t>>> decodedFrames;
    auto framesOf = [&](const std::string& path) -> const std::vector<std::vector<uint8_t>>& {
        auto found = decodedFrames.find(path);
        if (found != decodedFrames.end()) {
            return found->second;
        }
        std::vector<std::vector<uint8_t>> frames;
        std::string encoded;
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        if (pack.readTexture(path, encoded) && ui::decodeImage(encoded, width, height, rgba) && width > 0 && height > 0) {
            frames = sliceFrames(rgba, width, height);
        }
        return decodedFrames.emplace(path, std::move(frames)).first->second;
    };

    std::vector<bool> overlayLayers;
    auto materialFor = [&](const std::string& textureKey, bool rotate, size_t variant = 0, uint32_t tint = 0, uint8_t tintFlags = 0) -> uint32_t {
        const Flipbook* flipbook = nullptr;
        for (const Flipbook& candidate : flipbooks) {
            if (candidate.atlasTile == textureKey && (candidate.atlasIndex < 0 || size_t(candidate.atlasIndex) == variant)) {
                flipbook = &candidate;
                break;
            }
        }
        std::string path;
        if (flipbook) {
            path = flipbook->texturePath;
        } else if (const json::Value* entry = terrainEntry(textureKey)) {
            path = terrainPath(*entry, variant);
        }
        if (path.empty()) {
            return DiagnosticMaterial;
        }

        std::string key = path + '|' + std::to_string(rotate) + '|' + std::to_string(tint) + '|' + std::to_string(flipbook != nullptr) + '|' + std::to_string(tintFlags);
        auto materialFound = materialByKey.find(key);
        if (materialFound != materialByKey.end()) {
            return materialFound->second;
        }

        const std::vector<std::vector<uint8_t>>& frames = framesOf(path);
        uint32_t id = DiagnosticMaterial;
        if (!frames.empty()) {
            Material material;
            material.layer = static_cast<uint32_t>(layers.size());
            material.rotateUv = rotate;
            std::vector<size_t> timeline;
            if (flipbook && !flipbook->frames.empty()) {
                for (uint32_t frame : flipbook->frames) {
                    timeline.push_back(std::min<size_t>(frame, frames.size() - 1));
                }
            } else if (flipbook) {
                for (size_t frame = 0; frame < frames.size(); ++frame) {
                    timeline.push_back(frame);
                }
            } else {
                timeline.push_back(0);
            }
            timeline.resize(std::min<size_t>(timeline.size(), 128));
            for (size_t frame : timeline) {
                std::vector<uint8_t> pixels = frames[frame];
                if (tint) {
                    applyTint(pixels, tint);
                }
                layers.push_back(std::move(pixels));
                overlayLayers.resize(layers.size(), false);
                overlayLayers.back() = (tintFlags & TintOverlay) != 0;
            }
            material.frameCount = static_cast<uint32_t>(timeline.size());
            material.ticksPerFrame = flipbook ? std::clamp<uint32_t>(flipbook->ticksPerFrame, 1, 2048) : 1;
            material.interpolate = flipbook && flipbook->blendFrames;
            material.tint = tintFlags;
            id = static_cast<uint32_t>(materialTable.size());
            materialTable.push_back(material);
        }
        materialByKey.emplace(key, id);
        return id;
    };

    std::map<std::string, uint32_t> templateByKey;
    auto pushTemplate = [&](const std::vector<ModelQuad>& modelQuads, uint32_t flags) {
        templates.push_back({ static_cast<uint32_t>(quads.size()), static_cast<uint32_t>(modelQuads.size()), flags });
        quads.insert(quads.end(), modelQuads.begin(), modelQuads.end());
        return static_cast<uint32_t>(templates.size() - 1);
    };
    auto intern = [&](const std::string& key, const std::function<void()>& build) {
        auto found = templateByKey.find(key);
        if (found != templateByKey.end()) {
            return found->second;
        }
        uint32_t base = static_cast<uint32_t>(templates.size());
        build();
        templateByKey.emplace(key, base);
        return base;
    };
    auto keyOf = [](const std::string& family, const models::Materials& materials, std::initializer_list<uint32_t> parameters) {
        std::string key = family;
        for (uint32_t value : materials) {
            key += ':' + std::to_string(value);
        }
        for (uint32_t value : parameters) {
            key += '/' + std::to_string(value);
        }
        return key;
    };

    auto variantFor = [&](const std::string& textureKey, const std::string& name, const Tag& states) -> size_t {
        const json::Value* entry = terrainEntry(textureKey);
        size_t count = entry ? terrainVariantCount(*entry) : 0;
        if (count <= 1) {
            return 0;
        }
        if (textureKey == "door_lower" || textureKey == "door_upper") {
            static const std::pair<const char*, size_t> doors[] = {
                { "wooden_door", 0 }, { "spruce_door", 1 }, { "birch_door", 2 }, { "jungle_door", 3 },
                { "acacia_door", 4 }, { "dark_oak_door", 5 }, { "iron_door", 6 },
            };
            for (const auto& [door, index] : doors) {
                if (name == door) {
                    return index;
                }
            }
            return 0;
        }
        if (startsWith(textureKey, "double_plant_")) {
            static const std::pair<const char*, size_t> plants[] = {
                { "sunflower", 0 }, { "lilac", 1 }, { "tall_grass", 2 }, { "large_fern", 3 }, { "rose_bush", 4 }, { "peony", 5 },
            };
            for (const auto& [plant, index] : plants) {
                if (name == plant) {
                    return std::min(index, count - 1);
                }
            }
            return 0;
        }
        if (textureKey == "tallgrass" || textureKey == "tallgrass_carried") {
            return name == "fern" ? std::min<size_t>(2, count - 1) : 0;
        }
        std::optional<int32_t> growth = stateInt(states, "growth");
        if (!growth) {
            growth = stateInt(states, "growth_stage");
        }
        if (!growth) {
            growth = stateInt(states, "age");
        }
        if (textureKey == "carrots" || textureKey == "potatoes" || textureKey == "beetroot") {
            static constexpr size_t Stages[8] = { 0, 0, 1, 1, 2, 2, 2, 3 };
            return growth && *growth >= 0 && *growth < 8 ? Stages[*growth] : 0;
        }
        if (textureKey == "melon_stem" || textureKey == "pumpkin_stem") {
            std::optional<int32_t> facingDirection = stateInt(states, "facing_direction");
            return facingDirection && *facingDirection >= 2 ? 1 : 0;
        }
        if (textureKey == "torchflower_crop") {
            return growth && *growth >= 4 ? 1 : 0;
        }
        if (!growth || *growth < 0) {
            return 0;
        }
        size_t value = static_cast<size_t>(*growth);
        return std::min(count >= 8 ? value : value * count / 8, count - 1);
    };

    visuals.resize(registry.records().size());
    for (size_t i = 0; i < registry.records().size(); ++i) {
        const BlockRecord& record = registry.records()[i];
        std::string name = startsWith(record.name, "minecraft:") ? record.name.substr(10) : record.name;
        BlockVisual& visual = visuals[i];
        if (name == "air") {
            airSequential = static_cast<uint32_t>(i);
            airHash = record.networkHash;
        }

        Family family = classify(name);
        switch (family) {
        case Family::Air:
            visual.flags = FlagAir;
            continue;
        case Family::Invisible:
            visual.flags = 0;
            continue;
        case Family::Deferred:
            if (classifyBlockEntity(name, record.states, visual)) {
                visual.flags = FlagModel;
            } else {
                visual.flags = FlagDiagnostic;
                ++diagnosticCount;
            }
            continue;
        case Family::Liquid:
            visual.flags = FlagCubeGeometry | FlagCullSame;
            break;
        case Family::Model:
            visual.flags = FlagModel;
            break;
        case Family::Leaves:
            visual.flags = FlagCubeGeometry | FlagLeafModel;
            break;
        case Family::TransparentCube:
            visual.flags = FlagCubeGeometry | FlagCullSame;
            break;
        case Family::Cube:
            visual.flags = FlagCubeGeometry | FlagOccludesFullFace;
            break;
        }
        if (isTranslucentName(name)) {
            visual.flags |= FlagTranslucent;
            if (visual.flags & FlagOccludesFullFace) {
                visual.flags = static_cast<uint8_t>((visual.flags & ~FlagOccludesFullFace) | FlagCullSame);
            }
        }
        uint32_t tint = contains(name, "water") && family == Family::Liquid ? 0xFFFFFFu : 0u;

        const json::Value* entry = blockEntry(name);
        if (!entry) {
            if (const char* alias = legacyAlias(name)) {
                entry = blockEntry(alias);
            }
        }
        const json::Value* textures = entry ? entry->get("textures") : nullptr;
        std::string fallbackKey;
        if (!textures) {
            std::vector<std::string> candidates { name };
            if (startsWith(name, "hard_")) {
                candidates.push_back(name.substr(5));
            }
            if (startsWith(name, "underwater_")) {
                candidates.push_back(name.substr(11));
            }
            for (const std::string& candidate : candidates) {
                if (const json::Value* candidateEntry = blockEntry(candidate)) {
                    if ((textures = candidateEntry->get("textures"))) {
                        break;
                    }
                }
                if (terrainKnown(candidate)) {
                    fallbackKey = candidate;
                    break;
                }
            }
        }
        Axis axis = stateAxis(record.states);

        if (family == Family::Model) {
            static constexpr Face order[] = { Face::West, Face::East, Face::Down, Face::Up, Face::North, Face::South };
            auto faceKeyFor = [&](int face) {
                bool rotate = false;
                return textures ? resolveTextureKey(textures, order[face], Axis::Y, std::nullopt, rotate) : fallbackKey;
            };
            models::Materials materials {};
            bool complete = true;
            for (int face = 0; face < 6; ++face) {
                std::string key = faceKeyFor(face);
                materials[face] = key.empty() ? DiagnosticMaterial : materialFor(key, false, variantFor(key, name, record.states), 0, blockTint(name, face));
                complete &= materials[face] != DiagnosticMaterial;
            }
            auto uniform = [&](int face) {
                models::Materials same;
                same.fill(materials[face]);
                return same;
            };
            auto flag = [&](const char* key) {
                std::optional<int32_t> value = stateInt(record.states, key);
                return value && *value != 0;
            };
            auto cardinal = [&]() -> uint32_t {
                std::string value = stateString(record.states, "minecraft:cardinal_direction");
                if (value == "south") {
                    return 0;
                }
                if (value == "west") {
                    return 1;
                }
                if (value == "north") {
                    return 2;
                }
                if (value == "east") {
                    return 3;
                }
                std::optional<int32_t> direction = stateInt(record.states, "direction");
                return direction ? static_cast<uint32_t>(*direction & 3) : 0;
            };

            ModelKind kind = modelKind(name);
            uint32_t modelTemplate = NoModelTemplate;
            uint32_t variant = 0;
            switch (kind) {
            case ModelKind::Slab: {
                if (!complete) {
                    break;
                }
                uint32_t half = contains(name, "double") ? 2 : 0;
                if (half == 0 && (stateString(record.states, "minecraft:vertical_half") == "top" || flag("top_slot_bit"))) {
                    half = 1;
                }
                modelTemplate = intern(keyOf("slab", materials, { half }), [&] {
                    pushTemplate(models::slab(materials, half), 0);
                });
                if (half == 2) {
                    visual.flags |= FlagOccludesFullFace;
                }
                break;
            }
            case ModelKind::Stair: {
                if (!complete) {
                    break;
                }
                std::optional<int32_t> weirdo = stateInt(record.states, "weirdo_direction");
                bool upside = flag("upside_down_bit");
                modelTemplate = intern(keyOf("stair", materials, { upside }), [&] {
                    for (uint32_t shape = 0; shape < 5; ++shape) {
                        pushTemplate(models::stair(materials, upside, shape), TemplateStair);
                    }
                });
                // weirdo_direction runs east, west, south, north, unlike the south, west, north, east of direction.
                static constexpr uint32_t WeirdoFacing[4] = { 3, 1, 0, 2 };
                uint32_t facing = WeirdoFacing[static_cast<uint32_t>(weirdo.value_or(0)) & 3];
                variant = ((facing + 2) & 3) | (upside ? 4u : 0u);
                break;
            }
            case ModelKind::Fence: {
                uint32_t material = materials[models::South];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t fenceFlag = name == "nether_brick_fence" ? TemplateFenceNether : TemplateFenceWood;
                modelTemplate = intern(keyOf("fence", uniform(models::South), { fenceFlag }), [&] {
                    pushTemplate(models::fencePost(material), fenceFlag);
                    for (uint32_t mask = 0; mask < 16; ++mask) {
                        pushTemplate(models::fenceArms(material, mask), fenceFlag);
                    }
                });
                break;
            }
            case ModelKind::Pane: {
                uint32_t body = materials[models::North];
                uint32_t edge = materials[models::East];
                if (body == DiagnosticMaterial || edge == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("pane", { body, edge, 0, 0, 0, 0 }, {}), [&] {
                    for (uint32_t mask = 0; mask < 16; ++mask) {
                        pushTemplate(models::pane(body, edge, mask), TemplatePane);
                    }
                });
                break;
            }
            case ModelKind::Wall: {
                if (!complete) {
                    break;
                }
                uint32_t connections = flag("wall_post_bit") ? 1u << 8 : 0u;
                static const std::pair<const char*, uint32_t> sides[] = {
                    { "wall_connection_type_north", 0 },
                    { "wall_connection_type_east", 2 },
                    { "wall_connection_type_south", 4 },
                    { "wall_connection_type_west", 6 },
                };
                for (const auto& [key, shift] : sides) {
                    std::string value = stateString(record.states, key);
                    uint32_t height = value == "short" ? 1 : (value == "tall" ? 2 : 0);
                    connections |= height << shift;
                }
                modelTemplate = intern(keyOf("wall", materials, { connections }), [&] {
                    pushTemplate(models::wall(materials, connections), TemplateWall);
                });
                break;
            }
            case ModelKind::Door: {
                bool upper = flag("upper_block_bit");
                uint32_t material = materials[upper ? models::South : models::Down];
                if (material == DiagnosticMaterial) {
                    break;
                }
                auto [min, max] = models::doorBounds(cardinal(), flag("open_bit"), flag("door_hinge_bit"));
                models::Materials same;
                same.fill(material);
                modelTemplate = intern(keyOf("cuboid", same, { uint32_t(min[0]), uint32_t(min[1]), uint32_t(min[2]), uint32_t(max[0]), uint32_t(max[1]), uint32_t(max[2]) }), [&] {
                    auto faces = models::cuboid(same, min, max);
                    pushTemplate({ faces.begin(), faces.end() }, 0);
                });
                break;
            }
            case ModelKind::Trapdoor: {
                if (!complete) {
                    break;
                }
                std::optional<int32_t> direction = stateInt(record.states, "direction");
                auto [min, max] = models::trapdoorBounds(static_cast<uint32_t>(direction.value_or(0) & 3), flag("open_bit"), flag("upside_down_bit"));
                modelTemplate = intern(keyOf("cuboid", materials, { uint32_t(min[0]), uint32_t(min[1]), uint32_t(min[2]), uint32_t(max[0]), uint32_t(max[1]), uint32_t(max[2]) }), [&] {
                    auto faces = models::cuboid(materials, min, max);
                    pushTemplate({ faces.begin(), faces.end() }, 0);
                });
                break;
            }
            case ModelKind::Gate: {
                if (!complete) {
                    break;
                }
                uint32_t orientation = cardinal();
                bool open = flag("open_bit");
                bool inWall = flag("in_wall_bit");
                bool bamboo = name == "bamboo_fence_gate";
                uint32_t axisFlag = (orientation & 1) == 0 ? TemplateGateAxisZ : TemplateGateAxisX;
                modelTemplate = intern(keyOf("gate", materials, { orientation, open, inWall, bamboo }), [&] {
                    pushTemplate(models::gate(materials, orientation, open, inWall, bamboo), axisFlag);
                });
                break;
            }
            case ModelKind::Carpet:
            case ModelKind::SnowLayer:
            case ModelKind::Farmland:
            case ModelKind::Cake:
            case ModelKind::Cactus:
            case ModelKind::SinkingCube:
            case ModelKind::Chest: {
                if (!complete) {
                    break;
                }
                models::Point min { 0, 0, 0 };
                models::Point max { 256, 256, 256 };
                if (kind == ModelKind::Carpet) {
                    max[1] = 16;
                } else if (kind == ModelKind::SnowLayer) {
                    int32_t layersHigh = std::clamp(stateInt(record.states, "height_in_layers").value_or(0), 0, 7);
                    max[1] = static_cast<int16_t>((layersHigh + 1) * 32);
                } else if (kind == ModelKind::Farmland) {
                    max[1] = 240;
                } else if (kind == ModelKind::SinkingCube) {
                    max[1] = 224;
                } else if (kind == ModelKind::Cake) {
                    int32_t bites = std::clamp(stateInt(record.states, "bite_counter").value_or(0), 0, 6);
                    min = { static_cast<int16_t>(16 + bites * 32), 0, 16 };
                    max = { 240, 128, 240 };
                } else if (kind == ModelKind::Cactus) {
                    min = { 16, 0, 16 };
                    max = { 240, 256, 240 };
                } else {
                    min = { 16, 0, 16 };
                    max = { 240, 224, 240 };
                }
                modelTemplate = intern(keyOf("cuboid", materials, { uint32_t(min[0]), uint32_t(min[1]), uint32_t(min[2]), uint32_t(max[0]), uint32_t(max[1]), uint32_t(max[2]) }), [&] {
                    auto faces = models::cuboid(materials, min, max);
                    if (kind == ModelKind::Cactus) {
                        faces[models::Down].positions = { { { 0, 0, 0 }, { 256, 0, 0 }, { 256, 0, 256 }, { 0, 0, 256 } } };
                        faces[models::Up].positions = { { { 0, 256, 0 }, { 0, 256, 256 }, { 256, 256, 256 }, { 256, 256, 0 } } };
                        faces[models::Down].uvs = { { { 0, 0 }, { 4096, 0 }, { 4096, 4096 }, { 0, 4096 } } };
                        faces[models::Up].uvs = { { { 0, 0 }, { 0, 4096 }, { 4096, 4096 }, { 4096, 0 } } };
                    }
                    pushTemplate({ faces.begin(), faces.end() }, 0);
                });
                break;
            }
            case ModelKind::PressurePlate: {
                if (!complete) {
                    break;
                }
                bool pressed = stateInt(record.states, "redstone_signal").value_or(0) > 0;
                modelTemplate = intern(keyOf("plate", materials, { pressed }), [&] {
                    pushTemplate(models::pressurePlate(materials, pressed), 0);
                });
                break;
            }
            case ModelKind::Button: {
                if (!complete) {
                    break;
                }
                uint32_t orientation = static_cast<uint32_t>(std::clamp(stateInt(record.states, "facing_direction").value_or(1), 0, 5));
                bool pressed = flag("button_pressed_bit");
                modelTemplate = intern(keyOf("button", materials, { orientation, pressed }), [&] {
                    pushTemplate(models::button(materials, orientation, pressed), 0);
                });
                break;
            }
            case ModelKind::Torch: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                std::string direction = stateString(record.states, "torch_facing_direction");
                uint32_t facingIndex = direction == "west" ? 1 : direction == "east" ? 2 : direction == "north" ? 3 : direction == "south" ? 4 : 0;
                modelTemplate = intern(keyOf("torch", uniform(models::Up), { facingIndex }), [&] {
                    pushTemplate(models::torch(material, facingIndex), 0);
                });
                break;
            }
            case ModelKind::Rail: {
                int32_t direction = stateInt(record.states, "rail_direction").value_or(0);
                bool curved = direction >= 6;
                uint32_t material = materials[curved ? models::Up : models::Down];
                if (name != "rail") {
                    material = materials[flag("rail_data_bit") ? models::Up : models::Down];
                }
                if (material == DiagnosticMaterial) {
                    break;
                }
                static constexpr uint32_t Rotations[10] = { 0, 1, 1, 1, 0, 0, 0, 1, 2, 3 };
                modelTemplate = intern(keyOf("flat", { material, 0, 0, 0, 0, 0 }, { 16 }), [&] {
                    pushTemplate(models::flatPlane(material, 16), 0);
                });
                variant = Rotations[std::clamp(direction, 0, 9)];
                break;
            }
            case ModelKind::Lily: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("flat", uniform(models::Up), { 4 }), [&] {
                    pushTemplate(models::flatPlane(material, 4), 0);
                });
                break;
            }
            case ModelKind::Ladder:
            case ModelKind::Vine:
            case ModelKind::Multiface: {
                uint32_t material = materials[models::South];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t sides = 0;
                if (kind == ModelKind::Ladder) {
                    static constexpr uint32_t Attached[6] = { models::South, models::South, models::South, models::North, models::East, models::West };
                    sides = 1u << Attached[std::clamp(stateInt(record.states, "facing_direction").value_or(2), 0, 5)];
                } else if (kind == ModelKind::Vine) {
                    int32_t bits = stateInt(record.states, "vine_direction_bits").value_or(0);
                    static constexpr uint32_t VineSides[4] = { models::South, models::West, models::North, models::East };
                    for (int bit = 0; bit < 4; ++bit) {
                        if (bits & (1 << bit)) {
                            sides |= 1u << VineSides[bit];
                        }
                    }
                } else {
                    int32_t bits = stateInt(record.states, "multi_face_direction_bits").value_or(0);
                    static constexpr uint32_t MultiSides[6] = { models::Down, models::Up, models::South, models::West, models::North, models::East };
                    for (int bit = 0; bit < 6; ++bit) {
                        if (bits & (1 << bit)) {
                            sides |= 1u << MultiSides[bit];
                        }
                    }
                }
                if (sides == 0) {
                    sides = 1u << models::Up;
                }
                modelTemplate = intern(keyOf("planes", uniform(models::South), { sides }), [&] {
                    pushTemplate(models::attachedPlanes(material, sides), 0);
                });
                break;
            }
            case ModelKind::Cross: {
                int face = models::Up;
                if (textures && textures->isObject()) {
                    if (name == "sweet_berry_bush") {
                        static constexpr int ByGrowth[4] = { models::Down, models::Up, models::North, models::South };
                        face = ByGrowth[std::clamp(stateInt(record.states, "growth").value_or(0), 0, 3)];
                    } else if (textures->get("up") && textures->get("down") && name != "seagrass") {
                        face = flag("upper_block_bit") ? models::Up : models::Down;
                    }
                }
                uint32_t material = materials[face];
                if (material == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("cross", uniform(face), {}), [&] {
                    pushTemplate(models::cross(material, material), 0);
                });
                break;
            }
            case ModelKind::Cluster: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                std::string direction = stateString(record.states, "minecraft:block_face");
                static const std::pair<const char*, uint32_t> Sides[] = {
                    { "down", models::Down }, { "up", models::Up }, { "north", models::North },
                    { "south", models::South }, { "west", models::West }, { "east", models::East },
                };
                uint32_t facing = models::Up;
                for (const auto& [label, side] : Sides) {
                    if (direction == label) {
                        facing = side;
                    }
                }
                modelTemplate = intern(keyOf("cluster", uniform(models::Up), { facing }), [&] {
                    pushTemplate(models::orientedCross(material, facing), 0);
                });
                break;
            }
            case ModelKind::Bamboo: {
                uint32_t stem = materials[models::North];
                if (stem == DiagnosticMaterial) {
                    break;
                }
                std::string leafSize = stateString(record.states, "bamboo_leaf_size");
                uint32_t leaves = leafSize == "large_leaves" ? materials[models::Up] : leafSize == "small_leaves" ? materials[models::South] : DiagnosticMaterial;
                bool thick = stateString(record.states, "bamboo_stalk_thickness") == "thick";
                modelTemplate = intern(keyOf("bamboo", { stem, leaves, 0, 0, 0, 0 }, { thick }), [&] {
                    pushTemplate(models::bamboo(stem, leaves, thick), 0);
                });
                break;
            }
            case ModelKind::Sign: {
                uint32_t material = materials[models::South];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t facing = static_cast<uint32_t>(std::clamp(stateInt(record.states, "facing_direction").value_or(2), 0, 5));
                uint32_t rotation = static_cast<uint32_t>(stateInt(record.states, "ground_sign_direction").value_or(0) & 15);
                if (endsWith(name, "standing_sign")) {
                    modelTemplate = intern(keyOf("sign_standing", uniform(models::South), { rotation }), [&] {
                        pushTemplate(models::standingSign(material, rotation), 0);
                    });
                } else if (endsWith(name, "wall_sign")) {
                    modelTemplate = intern(keyOf("sign_wall", uniform(models::South), { facing }), [&] {
                        pushTemplate(models::wallSign(material, facing), 0);
                    });
                } else if (flag("hanging")) {
                    bool attached = flag("attached_bit");
                    modelTemplate = intern(keyOf("sign_ceiling", uniform(models::South), { rotation, attached }), [&] {
                        pushTemplate(models::hangingCeilingSign(material, rotation, attached), 0);
                    });
                } else {
                    modelTemplate = intern(keyOf("sign_hanging_wall", uniform(models::South), { facing }), [&] {
                        pushTemplate(models::hangingWallSign(material, facing), 0);
                    });
                }
                break;
            }
            case ModelKind::None:
                break;
            }

            if (modelTemplate == NoModelTemplate) {
                visual.flags = FlagDiagnostic;
                visual.faces.fill(DiagnosticMaterial);
                ++diagnosticCount;
            } else {
                visual.faces = materials;
                visual.modelTemplate = modelTemplate;
                visual.variant = variant;
            }
            continue;
        }

        std::optional<Face> facing = stateFacing(record.states);
        bool resolved = true;
        for (int face = 0; face < 6; ++face) {
            static constexpr Face order[] = { Face::West, Face::East, Face::Down, Face::Up, Face::North, Face::South };
            bool rotate = false;
            std::string key = textures ? resolveTextureKey(textures, order[face], axis, facing, rotate) : fallbackKey;
            visual.faces[face] = key.empty() ? DiagnosticMaterial : materialFor(key, rotate, 0, tint, blockTint(name, face));
            resolved &= visual.faces[face] != DiagnosticMaterial;
        }
        if (!resolved) {
            visual.flags = FlagDiagnostic;
            visual.faces.fill(DiagnosticMaterial);
            ++diagnosticCount;
        } else if (family == Family::Liquid) {
            visual.liquid = contains(name, "lava") ? 2 : 1;
            visual.liquidLevel = static_cast<uint8_t>(std::clamp(stateInt(record.states, "liquid_depth").value_or(0), 0, 15));
            visual.flags = static_cast<uint8_t>(visual.flags & FlagTranslucent);
        }
    }

    std::unordered_set<std::string> vanillaNames;
    for (const BlockRecord& record : registry.records()) {
        vanillaNames.insert(record.name);
    }
    GeometryLibrary geometries;
    geometries.load(packs);
    static constexpr const char* FaceNames[6] = { "west", "east", "down", "up", "north", "south" };

    for (size_t block = 0; block < customs.size(); ++block) {
        const CustomBlock& custom = customs[block];
        if (vanillaNames.contains(custom.name)) {
            continue;
        }
        for (Tag& states : enumerateCustomStates(custom.definition)) {
            std::map<std::string, const Tag*> components;
            collectComponents(custom.definition.get("components"), components);
            if (const Tag* permutations = custom.definition.get("permutations"); permutations && permutations->getType() == Tag::Type::List) {
                for (const Tag& permutation : permutations->getList()) {
                    const Tag* condition = permutation.get("condition");
                    if (condition && condition->getType() == Tag::Type::String && evaluateCondition(condition->asString(), states)) {
                        collectComponents(permutation.get("components"), components);
                    }
                }
            }

            const Tag* instances = components.contains("minecraft:material_instances") ? components["minecraft:material_instances"] : nullptr;
            const Tag* materialMap = instances ? instances->get("materials") : nullptr;
            const Tag* mappings = instances ? instances->get("mappings") : nullptr;
            bool translucent = false;
            bool cutout = false;
            auto instanceMaterial = [&](const std::string& instanceName, int side) -> uint32_t {
                if (!materialMap || materialMap->getType() != Tag::Type::Compound) {
                    return DiagnosticMaterial;
                }
                std::vector<std::string> candidates;
                if (!instanceName.empty()) {
                    candidates.push_back(instanceName);
                }
                candidates.push_back(FaceNames[side]);
                candidates.push_back("*");
                for (std::string candidate : candidates) {
                    if (mappings && mappings->getType() == Tag::Type::Compound) {
                        if (const Tag* mapped = mappings->get(candidate); mapped && mapped->getType() == Tag::Type::String) {
                            candidate = mapped->asString();
                        }
                    }
                    const Tag* instance = materialMap->get(candidate);
                    const Tag* texture = instance ? instance->get("texture") : nullptr;
                    if (!texture || texture->getType() != Tag::Type::String) {
                        continue;
                    }
                    if (const Tag* method = instance->get("render_method"); method && method->getType() == Tag::Type::String) {
                        translucent |= method->asString() == "blend";
                        cutout |= method->asString() != "opaque";
                    }
                    return materialFor(texture->asString(), false);
                }
                return DiagnosticMaterial;
            };

            std::string geometryName = "minecraft:geometry.full_block";
            if (const Tag* geometry = components.contains("minecraft:geometry") ? components["minecraft:geometry"] : nullptr) {
                if (const Tag* identifier = geometry->get("identifier"); identifier && identifier->getType() == Tag::Type::String) {
                    geometryName = identifier->asString();
                }
            }
            BlockTransform transform;
            if (const Tag* transformation = components.contains("minecraft:transformation") ? components["minecraft:transformation"] : nullptr) {
                transform.rotation = { tagNumber(transformation->get("RX")) * 90.0f, tagNumber(transformation->get("RY")) * 90.0f, tagNumber(transformation->get("RZ")) * 90.0f };
                transform.scale = { tagNumber(transformation->get("SX"), 1.0f), tagNumber(transformation->get("SY"), 1.0f), tagNumber(transformation->get("SZ"), 1.0f) };
                transform.translation = { tagNumber(transformation->get("TX")), tagNumber(transformation->get("TY")), tagNumber(transformation->get("TZ")) };
            }

            BlockVisual visual;
            if (geometryName == "minecraft:geometry.full_block" || components.contains("minecraft:unit_cube")) {
                for (int side = 0; side < 6; ++side) {
                    visual.faces[side] = instanceMaterial({}, side);
                }
                visual.flags = FlagCubeGeometry | (translucent || cutout ? FlagCullSame : FlagOccludesFullFace) | (translucent ? FlagTranslucent : 0);
            } else if (const Geometry* geometry = geometries.find(geometryName)) {
                std::vector<ModelQuad> modelQuads = buildGeometryQuads(*geometry, transform, instanceMaterial);
                visual.flags = FlagModel | (translucent ? FlagTranslucent : 0);
                visual.faces.fill(modelQuads.empty() ? DiagnosticMaterial : modelQuads.front().material);
                visual.modelTemplate = modelQuads.empty() ? NoModelTemplate : pushTemplate(modelQuads, 0);
            }
            if (visual.flags == 0 || (visual.flags & FlagModel && visual.modelTemplate == NoModelTemplate)) {
                visual = BlockVisual { FlagDiagnostic, {} };
                ++diagnosticCount;
            }
            visual.lightFilter = 15;
            if (const Tag* emission = components.contains("minecraft:light_emission") ? components["minecraft:light_emission"] : nullptr) {
                visual.lightEmission = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(tagNumber(emission->get("emission"))), 0, 15));
            }
            if (const Tag* dampening = components.contains("minecraft:light_dampening") ? components["minecraft:light_dampening"] : nullptr) {
                visual.lightFilter = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(tagNumber(dampening->get("lightLevel"), 15.0f)), 0, 15));
            }

            uint32_t index = static_cast<uint32_t>(visuals.size());
            customByHash.emplace(static_cast<uint32_t>(BlockStateHasher::hash(custom.name, states)), index);
            customStates.push_back({ block, std::move(states) });
            visuals.push_back(visual);
        }
    }

    applyBlockLight(registry, visuals);

    auto loadImage = [&](const std::string& path, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba) {
        std::string encoded;
        return pack.readTexture(path, encoded) && ui::decodeImage(encoded, width, height, rgba) && width > 0 && height > 0;
    };
    auto tileOf = [](const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x0, uint32_t y0, uint32_t size) {
        std::vector<uint8_t> tile(size_t(size) * size * 4);
        for (uint32_t y = 0; y < size; ++y) {
            const uint8_t* source = rgba.data() + (size_t(y0 + y) * width + x0) * 4;
            std::copy(source, source + size_t(size) * 4, tile.data() + size_t(y) * size * 4);
        }
        return normalizeTexture(tile, size, size);
    };

    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    if (loadImage("textures/environment/sun", width, height, rgba)) {
        sun = static_cast<uint32_t>(layers.size());
        layers.push_back(tileOf(rgba, width, 0, 0, std::min(width, height)));
    }
    if (loadImage("textures/environment/moon_phases", width, height, rgba) && width >= 4 && height >= 2) {
        uint32_t size = std::min(width / 4, height / 2);
        for (uint32_t phase = 0; phase < 8; ++phase) {
            moonPhases[phase] = static_cast<uint32_t>(layers.size());
            layers.push_back(tileOf(rgba, width, (phase % 4) * size, (phase / 4) * size, size));
        }
    }

    buildBlockEntityTemplates(pack, layers, overlayLayers, materialByKey, pushTemplate);
    buildEntityModels(pack, packs);
    buildInterfaceAssets(pack);

    std::filesystem::path behaviorRoot = root.parent_path().parent_path() / "behavior_packs" / root.filename();
    PackSource behaviors(behaviorRoot);
    biomes.load(pack, behaviors);

    overlayLayers.resize(layers.size(), false);
    buildMips(textureArray, layers, overlayLayers);
    return true;
}

}
