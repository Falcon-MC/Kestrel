#include "world/BlockAssets.h"
#include "world/BlockTags.h"
#include "world/assets/BlockRules.h"
#include "client/DebugLog.h"
#include "world/assets/TextureTools.h"

#include "Core/Json/Json.h"
#include "Protocol/BlockStateHasher.h"
#include "world/PottedPlant.h"
#include "ui/Image.h"
#include "world/BlockEntityModels.h"
#include "world/BlockModels.h"
#include "world/DoorState.h"
#include "world/Geometry.h"
#include "world/Molang.h"
#include "world/PackSource.h"
#include "util/JsonText.h"
#include "util/Text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <unordered_set>

namespace kestrel::world {

using namespace rules;
using util::contains;
using util::endsWith;
using util::startsWith;
using util::stripJsonComments;

namespace {

// The game paints its grey lily pad texture this green everywhere, whatever the biome.
constexpr uint32_t LilyPadColor = 0x208030;
// Plain water in a cauldron keeps this blue in every biome; only potions and dyes change it.
constexpr uint32_t CauldronWaterColor = 0x3F76E4;

/**
 * Whether a collision or selection box component is on: missing, true or a
 * compound without enabled set to 0.
 */
bool componentEnabled(const Tag* component)
{
    if (!component) {
        return true;
    }
    if (component->getType() == Tag::Type::Byte) {
        return component->asByte() != 0;
    }
    const Tag* enabled = component->getType() == Tag::Type::Compound ? component->get("enabled") : nullptr;
    return !enabled || tagNumber(enabled, 1.0f) != 0.0f;
}

/**
 * The boxes of a collision or selection box component, in block units kept
 * inside the block. The server sends them as a list of min and max corners
 * in sixteenths; a pack writes origin and size, origin measured from the
 * bottom centre. Malformed or empty boxes are dropped, as the server's data
 * is never trusted to be well formed.
 */
std::optional<CollisionBox> componentBox(const Tag* component);

std::vector<CollisionBox> componentBoxes(const Tag* component)
{
    std::vector<CollisionBox> out;
    if (!component || component->getType() != Tag::Type::Compound) {
        return out;
    }
    const Tag* boxes = component->get("boxes");
    if (!boxes || boxes->getType() != Tag::Type::List) {
        if (std::optional<CollisionBox> single = componentBox(component)) {
            out.push_back(*single);
        }
        return out;
    }
    constexpr size_t MaxBoxes = 16;
    for (const Tag& entry : boxes->getList()) {
        if (out.size() >= MaxBoxes || entry.getType() != Tag::Type::Compound) {
            continue;
        }
        constexpr float Nan = std::numeric_limits<float>::quiet_NaN();
        std::array<float, 6> v { tagNumber(entry.get("minX"), Nan), tagNumber(entry.get("minY"), Nan), tagNumber(entry.get("minZ"), Nan),
            tagNumber(entry.get("maxX"), Nan), tagNumber(entry.get("maxY"), Nan), tagNumber(entry.get("maxZ"), Nan) };
        if (!std::all_of(v.begin(), v.end(), [](float value) { return std::isfinite(value); })) {
            continue;
        }
        for (float& value : v) {
            value = std::clamp(value / 16.0f, 0.0f, 1.0f);
        }
        if (v[3] > v[0] && v[4] > v[1] && v[5] > v[2]) {
            out.push_back({ v[0], v[1], v[2], v[3], v[4], v[5] });
        }
    }
    return out;
}

std::optional<CollisionBox> componentBox(const Tag* component)
{
    if (!component || component->getType() != Tag::Type::Compound) {
        return std::nullopt;
    }
    auto triple = [&](const char* name) -> std::optional<std::array<float, 3>> {
        const Tag* list = component->get(name);
        if (!list || list->getType() != Tag::Type::List || list->getList().size() != 3) {
            return std::nullopt;
        }
        std::array<float, 3> out {};
        for (size_t axis = 0; axis < 3; ++axis) {
            out[axis] = tagNumber(&list->getList()[axis], std::numeric_limits<float>::quiet_NaN());
            if (!std::isfinite(out[axis])) {
                return std::nullopt;
            }
        }
        return out;
    };
    std::optional<std::array<float, 3>> origin = triple("origin");
    std::optional<std::array<float, 3>> size = triple("size");
    if (!origin || !size) {
        return std::nullopt;
    }
    constexpr std::array<float, 3> Shift { 8.0f, 0.0f, 8.0f };
    std::array<float, 3> low {};
    std::array<float, 3> high {};
    for (size_t axis = 0; axis < 3; ++axis) {
        low[axis] = std::clamp(((*origin)[axis] + Shift[axis]) / 16.0f, 0.0f, 1.0f);
        high[axis] = std::clamp(((*origin)[axis] + Shift[axis] + (*size)[axis]) / 16.0f, 0.0f, 1.0f);
        if (high[axis] <= low[axis]) {
            return std::nullopt;
        }
    }
    return CollisionBox { low[0], low[1], low[2], high[0], high[1], high[2] };
}

}

bool BlockAssets::customCollision(uint32_t networkValue, bool hashed, const SequentialMap* sequential, const CollisionState*& state) const
{
    state = nullptr;
    if (networkValue == 0xFFFFFFFFu) {
        return false;
    }
    int32_t index = indexOf(networkValue, hashed, sequential);
    size_t vanilla = registry.records().size();
    if (index < 0 || static_cast<size_t>(index) < vanilla || static_cast<size_t>(index) - vanilla >= customStates.size()) {
        return false;
    }
    const CustomState& custom = customStates[static_cast<size_t>(index) - vanilla];
    state = custom.collides ? &custom.collision : nullptr;
    return true;
}

std::optional<CollisionBox> BlockAssets::customSelection(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return std::nullopt;
    }
    int32_t index = indexOf(networkValue, hashed, sequential);
    size_t vanilla = registry.records().size();
    if (index < 0 || static_cast<size_t>(index) < vanilla || static_cast<size_t>(index) - vanilla >= customStates.size()) {
        return std::nullopt;
    }
    return customStates[static_cast<size_t>(index) - vanilla].selection;
}

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
    for (const auto& block : customBlocks) assets->customTags.emplace(block.name, readBlockTags(block.definition));
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

const std::vector<std::string>& BlockAssets::blockTags(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    static const std::vector<std::string> empty;
    int32_t index = indexOf(networkValue, hashed, sequential);
    if (index < 0 || size_t(index) >= registry.records().size() + customStates.size()) return empty;
    const auto& name = nameAt(size_t(index));
    auto custom = customTags.find(name);
    return custom == customTags.end() ? registry.tags(name) : custom->second;
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
    int32_t index = indexOf(networkValue, hashed, sequential);
    if (index < 0) {
        return "unknown #" + std::to_string(networkValue);
    }
    return nameAt(static_cast<size_t>(index)) + " #" + std::to_string(networkValue);
}

std::vector<std::string> BlockAssets::blockNames() const
{
    std::set<std::string> unique;
    auto add = [&](const std::string& name) {
        if (!name.empty()) {
            unique.insert(name.find(':') == std::string::npos ? "minecraft:" + name : name);
        }
    };
    for (const auto& record : registry.records()) {
        add(record.name);
    }
    for (const CustomBlock& custom : customs) {
        add(custom.name);
    }
    return { unique.begin(), unique.end() };
}

std::string BlockAssets::blockName(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return "minecraft:air";
    }
    int32_t index = indexOf(networkValue, hashed, sequential);
    return index < 0 ? std::string() : nameAt(static_cast<size_t>(index));
}

const Tag* BlockAssets::blockStates(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (networkValue == 0xFFFFFFFFu) {
        return nullptr;
    }
    int32_t index = indexOf(networkValue, hashed, sequential);
    if (index < 0) {
        return nullptr;
    }
    size_t at = static_cast<size_t>(index);
    if (at < registry.records().size()) {
        return &registry.records()[at].states;
    }
    return &customStates[at - registry.records().size()].states;
}

int32_t BlockAssets::indexOf(uint32_t networkValue, bool hashed, const SequentialMap* sequential) const
{
    if (!hashed && sequential) {
        return networkValue < sequential->size() ? (*sequential)[networkValue] : -1;
    }
    int32_t index = registry.resolve(networkValue, hashed);
    if (index < 0) {
        auto custom = customByHash.find(networkValue);
        index = custom == customByHash.end() ? -1 : static_cast<int32_t>(custom->second);
    }
    return index;
}

std::optional<CollisionBox> BlockAssets::doorBox(uint32_t value, uint32_t other, bool hashed, const SequentialMap* sequential) const
{
    const BlockVisual& own = visual(value, hashed, sequential);
    if (!(templateFlags(own) & TemplateDoor)) return std::nullopt;
    uint8_t state = own.doorState;
    const BlockVisual& partner = visual(other, hashed, sequential);
    if ((templateFlags(partner) & TemplateDoor) && partner.faces == own.faces
        && ((state ^ partner.doorState) & DoorUpper)) {
        state = resolveDoorState(state, partner.doorState);
    }
    auto [min, max] = models::doorBounds(state & 3, (state >> 2) & 1, (state >> 3) & 1);
    return CollisionBox { min[0] / 256.0f, min[1] / 256.0f, min[2] / 256.0f,
        max[0] / 256.0f, max[1] / 256.0f, max[2] / 256.0f };
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

std::optional<uint32_t> BlockAssets::networkValueForState(uint32_t hash, bool hashed, const SequentialMap* sequential) const
{
    int32_t index = indexOf(hash, true, nullptr);
    if (index < 0) return std::nullopt;
    if (hashed) return hash;
    if (!sequential) return std::nullopt;
    auto found = std::find(sequential->begin(), sequential->end(), index);
    if (found == sequential->end()) return std::nullopt;
    return static_cast<uint32_t>(found - sequential->begin());
}

bool BlockAssets::build(const std::vector<std::shared_ptr<const PackFiles>>& packs, std::string& error)
{
    StartupTimer timer;
    if (!registry.load(error)) {
        return false;
    }
    timer.mark("assets: block registry");

    std::filesystem::path root = PackSource::locateVanilla();
    if (root.empty()) {
        error = "no vanilla resource pack found (install Minecraft Bedrock or set KESTREL_VANILLA_PACK)";
        return false;
    }
    PackSource pack(root);
    pack.setOverlays(packs);

    // Entity models touch nothing the block atlas does, so they load alongside it.
    std::jthread entities([this, &root, &packs] {
        StartupTimer entityTimer;
        PackSource entityPack(root);
        entityPack.setOverlays(packs);
        buildEntityModels(entityPack, packs);
        entityTimer.mark("assets: entity models (parallel)");
    });

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
    // A tint flagged TintOverlay is baked in as an overlay color instead of waiting for a biome.
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

        std::string overlayPath;
        if (textureKey == "open_eyeblossom" && variant == 0) {
            if (const json::Value* entry = terrainEntry(textureKey); entry && terrainVariantCount(*entry) > 1) {
                overlayPath = terrainPath(*entry, 1);
            }
        }

        std::string key = path + '|' + std::to_string(rotate) + '|' + std::to_string(tint) + '|' + std::to_string(flipbook != nullptr) + '|' + std::to_string(tintFlags) + '|' + overlayPath;
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
            bool bakedOverlay = tint && (tintFlags & TintOverlay);
            for (size_t frame : timeline) {
                std::vector<uint8_t> pixels = frames[frame];
                if (!overlayPath.empty()) {
                    const auto& overlays = framesOf(overlayPath);
                    if (!overlays.empty() && overlays.front().size() == pixels.size()) {
                        const auto& overlay = overlays[std::min(frame, overlays.size() - 1)];
                        for (size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
                            const uint32_t alpha = overlay[pixel + 3];
                            const uint32_t remaining = uint32_t(pixels[pixel + 3]) * (255 - alpha) / 255;
                            const uint32_t combined = alpha + remaining;
                            for (size_t channel = 0; channel < 3; ++channel) {
                                pixels[pixel + channel] = combined ? uint8_t((uint32_t(overlay[pixel + channel]) * alpha
                                    + uint32_t(pixels[pixel + channel]) * remaining) / combined) : 0;
                            }
                            pixels[pixel + 3] = uint8_t(combined);
                        }
                    }
                }
                if (bakedOverlay) {
                    applyOverlay(pixels, tint);
                } else if (tint) {
                    applyTint(pixels, tint);
                }
                layers.push_back(std::move(pixels));
                overlayLayers.resize(layers.size(), false);
                overlayLayers.back() = !bakedOverlay && (tintFlags & TintOverlay) != 0;
            }
            material.frameCount = static_cast<uint32_t>(timeline.size());
            material.ticksPerFrame = flipbook ? std::clamp<uint32_t>(flipbook->ticksPerFrame, 1, 2048) : 1;
            material.interpolate = flipbook && flipbook->blendFrames;
            material.tint = bakedOverlay ? 0 : tintFlags;
            id = static_cast<uint32_t>(materialTable.size());
            materialTable.push_back(material);
        }
        materialByKey.emplace(key, id);
        return id;
    };

    uint32_t endPortalMaterial = DiagnosticMaterial;
    auto endPortalFor = [&]() -> uint32_t {
        if (endPortalMaterial == DiagnosticMaterial) {
            Material material;
            material.layer = EndPortalLayer;
            endPortalMaterial = static_cast<uint32_t>(materialTable.size());
            materialTable.push_back(material);
        }
        return endPortalMaterial;
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
        if (textureKey == "daylight_detector_top") {
            return name == "daylight_detector_inverted" ? 1 : 0;
        }
        if (textureKey == "repeater_up" || textureKey == "comparator_up") {
            return startsWith(name, "powered_") || stateInt(states, "output_lit_bit").value_or(0) != 0 ? 1 : 0;
        }
        if (textureKey == "lightning_rod" || endsWith(textureKey, "_lightning_rod")) {
            return stateInt(states, "powered_bit").value_or(0) != 0 ? 1 : 0;
        }
        if (startsWith(textureKey, "pointed_dripstone_") || startsWith(textureKey, "sulfur_spike_")) {
            return stateInt(states, "hanging").value_or(0) ? 0 : 1;
        }
        if (textureKey == "sculk_shrieker_inner_top") {
            return stateInt(states, "can_summon").value_or(0) ? 1 : 0;
        }
        if (startsWith(textureKey, "dried_ghast_")) {
            return size_t(std::clamp(stateInt(states, "rehydration_level").value_or(0), 0, 3));
        }
        if (textureKey == "pitcher_crop_lower_flower") {
            return size_t(std::clamp(stateInt(states, "growth").value_or(0) - 1, 0, 3));
        }
        if (textureKey == "pitcher_crop_upper_flower") {
            return size_t(std::clamp(stateInt(states, "growth").value_or(0) - 3, 0, 1));
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
        if (textureKey == "cocoa") {
            return std::min<size_t>(static_cast<size_t>(std::clamp(stateInt(states, "age").value_or(0), 0, 2)), count - 1);
        }
        if (isCandleName(textureKey)) {
            return stateInt(states, "lit").value_or(0) != 0 ? 1 : 0;
        }
        if (textureKey == "turtle_egg" || startsWith(textureKey, "sniffer_egg_")) {
            std::string cracks = stateString(states, "cracked_state");
            return std::min<size_t>(cracks == "max_cracked" ? 2 : cracks == "cracked" ? 1 : 0, count - 1);
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

    static constexpr Face FaceOrder[] = { Face::West, Face::East, Face::Down, Face::Up, Face::North, Face::South };

    auto carriedMaterial = [&](const std::string& key, bool rotate, size_t variant) {
        uint32_t overlay = 0;
        const json::Value* entry = terrainEntry(key);
        const json::Value* texture = entry ? entry->get("textures") : nullptr;
        if (texture && texture->isArray()) {
            texture = texture->mArray.empty() ? nullptr : texture->mArray[std::min(variant, texture->mArray.size() - 1)].get();
        }
        const json::Value* color = texture && texture->isObject() ? texture->get("overlay_color") : nullptr;
        if (color && color->isString() && color->mString.size() == 7 && color->mString[0] == '#') {
            overlay = static_cast<uint32_t>(std::strtoul(color->mString.c_str() + 1, nullptr, 16));
        }
        return materialFor(key, rotate, variant, overlay, overlay ? TintOverlay : 0);
    };

    // Items draw a block with its carried_textures when blocks.json has them: grass
    // with the plains green, leaves already colored, and so on.
    auto rememberCarried = [&](const BlockRecord& record, const std::string& name, const json::Value* entry, const BlockVisual& visual, Axis axis, std::optional<Face> facing) {
        const json::Value* carried = entry ? entry->get("carried_textures") : nullptr;
        if (!carried || (visual.flags & FlagDiagnostic) || carriedVisuals.contains(record.name)) {
            return;
        }
        BlockVisual look = visual;
        for (int face = 0; face < 6; ++face) {
            bool rotate = false;
            std::string key = resolveTextureKey(carried, FaceOrder[face], axis, facing, rotate);
            uint32_t material = key.empty() ? DiagnosticMaterial : carriedMaterial(key, rotate, variantFor(key, name, record.states));
            if (material == DiagnosticMaterial) {
                return;
            }
            look.faces[face] = material;
        }
        carriedVisuals.emplace(record.name, look);
    };

    timer.mark("assets: blocks.json, terrain and flipbooks");
    visuals.resize(registry.records().size());
    for (size_t i = 0; i < registry.records().size(); ++i) {
        const BlockRecord& record = registry.records()[i];
        std::string name = startsWith(record.name, "minecraft:") ? record.name.substr(10) : record.name;
        BlockVisual& visual = visuals[i];
        visual.powderSnow = name == "powder_snow";
        if (name == "redstone_wire" || name == "redstone_block" || name == "redstone_torch" || name == "unlit_redstone_torch"
            || name == "lever" || name == "daylight_detector" || name == "daylight_detector_inverted" || name == "target"
            || name == "detector_rail" || name == "tripwire_hook" || name == "trapped_chest" || name == "lectern"
            || name == "lightning_rod" || endsWith(name, "_lightning_rod") || endsWith(name, "_button") || contains(name, "pressure_plate")) {
            visual.redstoneConnections = 15;
        } else if (contains(name, "repeater") || contains(name, "comparator")) {
            const std::string cardinal = stateString(record.states, "minecraft:cardinal_direction");
            const uint32_t rotation = cardinal.empty() ? uint32_t(stateInt(record.states, "direction").value_or(0)) : facingRotation(cardinal);
            visual.redstoneConnections = (rotation & 1) ? 10 : 5;
        } else if (name == "observer") {
            static constexpr uint8_t OutputSides[6] = { 0, 0, 4, 1, 2, 8 };
            const int32_t facing = std::clamp(stateInt(record.states, "facing_direction").value_or(0), 0, 5);
            visual.redstoneConnections = OutputSides[facing];
        }
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
        uint32_t tint = (contains(name, "water") || name == "bubble_column") && family == Family::Liquid ? 0xFFFFFFu : 0u;

        const json::Value* entry = blockEntry(name == "bubble_column" ? "water" : name);
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
            auto faceKeyFor = [&](int face) {
                bool rotate = false;
                return textures ? resolveTextureKey(textures, FaceOrder[face], Axis::Y, std::nullopt, rotate) : fallbackKey;
            };
            models::Materials materials {};
            bool complete = true;
            uint32_t fixedTint = name == "waterlily" || name == "lily_pad" ? LilyPadColor : 0;
            if (name == "melon_stem" || name == "pumpkin_stem") {
                const uint32_t growth = uint32_t(std::clamp(stateInt(record.states, "growth").value_or(0), 0, 7));
                fixedTint = ((growth * 32) << 16) | ((255 - growth * 8) << 8) | (growth * 4);
            }
            if (name == "redstone_wire") {
                const float power = float(std::clamp(stateInt(record.states, "redstone_signal").value_or(0), 0, 15)) / 15.0f;
                const uint32_t red = uint32_t((power * 0.6f + (power > 0 ? 0.4f : 0.3f)) * 255);
                const uint32_t green = uint32_t(std::max(0.0f, power * power * 0.7f - 0.5f) * 255);
                fixedTint = (red << 16) | (green << 8);
            }
            for (int face = 0; face < 6; ++face) {
                std::string key = faceKeyFor(face);
                materials[face] = isEndPortalName(name) ? endPortalFor() : key.empty() ? DiagnosticMaterial : materialFor(key, false, variantFor(key, name, record.states), fixedTint, blockTint(name, face));
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
            case ModelKind::Piston: {
                if (!complete) break;
                const int32_t facing = std::clamp(stateInt(record.states, "facing_direction").value_or(1), 0, 5);
                static constexpr uint32_t Sides[6] = { models::Down, models::Up, models::South, models::North, models::East, models::West };
                const uint32_t direction = Sides[facing];
                const uint32_t normal = materialFor("piston_top_normal", false);
                const uint32_t inner = materialFor("piston_top", false);
                materials[models::Up] = materialFor(name == "sticky_piston" ? "piston_top_sticky" : "piston_top_normal", false);
                if (normal == DiagnosticMaterial || inner == DiagnosticMaterial || materials[models::Up] == DiagnosticMaterial) break;
                modelTemplate = intern(keyOf("piston", materials, { direction, inner, normal }), [&] {
                    const auto closed = models::cuboid(materials, { 0, 0, 0 }, { 256, 256, 256 });
                    pushTemplate(models::orient({ closed.begin(), closed.end() }, direction), 0);
                    auto body = models::pistonBody(materials, inner, direction);
                    auto extended = body;
                    const auto head = models::pistonHead(materials, normal, direction, true);
                    extended.insert(extended.end(), head.begin(), head.end());
                    pushTemplate(extended, 0);
                    pushTemplate(body, 0);
                    pushTemplate(models::pistonHead(materials, normal, direction, false), 0);
                });
                visual.blockEntity = EntityPiston;
                variant = uint32_t(facing);
                break;
            }
            case ModelKind::TripwireHook: {
                if (!complete) break;
                const bool attached = flag("attached_bit");
                const bool powered = flag("powered_bit");
                const uint32_t turns = (cardinal() + 2) & 3;
                modelTemplate = intern(keyOf("tripwire_hook", materials, { attached, powered, turns }), [&] {
                    pushTemplate(models::tripwireHook(materials, attached, powered, turns), 0);
                });
                break;
            }
            case ModelKind::RedstoneWire: {
                if (!complete) break;
                modelTemplate = intern(keyOf("redstone_wire", materials, {}), [&] {
                    for (uint32_t mask = 0; mask < 256; ++mask) {
                        pushTemplate(models::redstoneWire(materials[models::Up], materials[models::Down], mask), TemplateRedstoneWire);
                    }
                });
                break;
            }
            case ModelKind::Tripwire: {
                if (!complete) break;
                uint32_t mask = 0;
                static constexpr const char* Connections[] = { "minecraft:connection_north", "minecraft:connection_east", "minecraft:connection_south", "minecraft:connection_west" };
                for (uint32_t side = 0; side < 4; ++side) if (flag(Connections[side])) mask |= 1u << side;
                const bool attached = flag("attached_bit");
                const bool suspended = flag("suspended_bit");
                modelTemplate = intern(keyOf("tripwire", materials, { mask, attached, suspended }), [&] {
                    pushTemplate(models::tripwire(materials[models::Up], mask, attached, suspended), 0);
                });
                break;
            }
            case ModelKind::DriedGhast: {
                const uint32_t tentacles = materialFor("dried_ghast_tentacles", false,
                    variantFor("dried_ghast_tentacles", name, record.states));
                if (!complete || tentacles == DiagnosticMaterial) break;
                const uint32_t turns = cardinal();
                modelTemplate = intern(keyOf("dried_ghast", materials, { tentacles, turns }), [&] {
                    pushTemplate(models::driedGhast(materials, tentacles, turns), 0);
                });
                break;
            }
            case ModelKind::CropStem: {
                if (!complete) break;
                const uint32_t growth = uint32_t(std::clamp(stateInt(record.states, "growth").value_or(0), 0, 7));
                const int32_t facing = stateInt(record.states, "facing_direction").value_or(0);
                modelTemplate = intern(keyOf("crop_stem", materials, { growth, uint32_t(facing) }), [&] {
                    pushTemplate(models::cropStem(materials[models::Up], growth, facing), 0);
                });
                break;
            }
            case ModelKind::PitcherCrop: {
                if (!complete) break;
                const uint32_t growth = uint32_t(std::clamp(stateInt(record.states, "growth").value_or(0), 0, 4));
                const bool upper = flag("upper_block_bit");
                modelTemplate = intern(keyOf("pitcher_crop", materials, { growth, upper }), [&] {
                    pushTemplate(models::pitcherCrop(materials, growth, upper), 0);
                });
                break;
            }
            case ModelKind::SmallDripleaf: {
                if (!complete) break;
                const bool upper = flag("upper_block_bit");
                const uint32_t turns = cardinal();
                modelTemplate = intern(keyOf("small_dripleaf", materials, { upper, turns }), [&] {
                    pushTemplate(models::smallDripleaf(materials, upper, turns), 0);
                });
                break;
            }
            case ModelKind::CoralFan: {
                if (!complete) break;
                const bool wall = contains(name, "wall_fan") || contains(name, "fan_hang");
                const uint32_t direction = uint32_t(stateInt(record.states, "coral_direction").value_or(0)) & 3;
                static constexpr uint32_t WallTurns[4] = { 3, 1, 0, 2 };
                const uint32_t turns = wall ? WallTurns[direction]
                    : uint32_t(stateInt(record.states, "coral_fan_direction").value_or(0)) & 1;
                modelTemplate = intern(keyOf("coral_fan", materials, { wall, turns }), [&] {
                    pushTemplate(models::coralFan(materials[models::Up], wall, turns), 0);
                });
                break;
            }
            case ModelKind::SporeBlossom: {
                if (!complete) break;
                modelTemplate = intern(keyOf("spore_blossom", materials, {}), [&] {
                    pushTemplate(models::sporeBlossom(materials[models::Up], materials[models::Down]), 0);
                });
                break;
            }
            case ModelKind::Sunflower: {
                const bool upper = flag("upper_block_bit");
                const uint32_t front = materialFor("sunflower_additional", false, 0);
                const uint32_t back = materialFor("sunflower_additional", false, 1);
                const uint32_t stem = materials[upper ? models::Up : models::Down];
                if (stem == DiagnosticMaterial || front == DiagnosticMaterial || back == DiagnosticMaterial) break;
                modelTemplate = intern(keyOf("sunflower", materials, { front, back, upper }), [&] {
                    pushTemplate(models::sunflower(stem, front, back, upper), 0);
                });
                break;
            }
            case ModelKind::Chorus: {
                if (!complete) break;
                modelTemplate = intern(keyOf("chorus", materials, {}), [&] {
                    for (uint32_t mask = 0; mask < 64; ++mask) {
                        pushTemplate(models::chorus(materials, mask), TemplateChorus);
                    }
                });
                break;
            }
            case ModelKind::SeaPickle: {
                if (!complete) break;
                const uint32_t count = uint32_t(std::clamp(stateInt(record.states, "cluster_count").value_or(0), 0, 3));
                const bool dead = flag("dead_bit");
                modelTemplate = intern(keyOf("sea_pickle", materials, { count, dead }), [&] {
                    pushTemplate(models::seaPickles(materials[models::Up], count, dead), 0);
                });
                break;
            }
            case ModelKind::SculkShrieker: {
                const uint32_t inner = materialFor("sculk_shrieker_inner_top", false,
                    variantFor("sculk_shrieker_inner_top", name, record.states));
                if (!complete || inner == DiagnosticMaterial) break;
                modelTemplate = intern(keyOf("sculk_shrieker", materials, { inner }), [&] {
                    pushTemplate(models::sculkShrieker(materials, inner), 0);
                });
                break;
            }
            case ModelKind::Dripleaf: {
                if (!complete) break;
                bool head = flag("big_dripleaf_head");
                std::string tiltName = stateString(record.states, "big_dripleaf_tilt");
                uint32_t tilt = tiltName == "full_tilt" ? 3 : tiltName == "partial_tilt" ? 2 : 0;
                uint32_t turns = cardinal();
                modelTemplate = intern(keyOf("big_dripleaf", materials, { head, tilt, turns }), [&] {
                    pushTemplate(models::dripleaf(materials, head, tilt, turns), 0);
                });
                break;
            }
            case ModelKind::SculkSensor: {
                bool active = stateInt(record.states, "sculk_sensor_phase").value_or(0) == 1;
                uint32_t tendril = materialFor(active ? "sculk_sensor_tendril_active" : "sculk_sensor_tendril_inactive", false);
                bool calibrated = name == "calibrated_sculk_sensor";
                uint32_t amethyst = calibrated ? materialFor("calibrated_sculk_sensor_amethyst", false) : DiagnosticMaterial;
                if (!complete || tendril == DiagnosticMaterial || (calibrated && amethyst == DiagnosticMaterial)) {
                    break;
                }
                uint32_t turns = calibrated ? cardinal() : 0;
                modelTemplate = intern(keyOf("sculk_sensor", materials, { tendril, amethyst, turns }), [&] {
                    pushTemplate(models::sculkSensor(materials, tendril, calibrated ? std::optional<uint32_t>(amethyst) : std::nullopt, turns), 0);
                });
                break;
            }
            case ModelKind::Azalea: {
                if (materials[models::North] == DiagnosticMaterial || materials[models::Up] == DiagnosticMaterial || materials[models::East] == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("azalea", materials, {}), [&] {
                    pushTemplate(models::azalea(materials), 0);
                });
                break;
            }
            case ModelKind::CandleCake: {
                std::string candleName = name.substr(0, name.size() - 5);
                const json::Value* candleEntry = blockEntry(candleName);
                const json::Value* candleTextures = candleEntry ? candleEntry->get("textures") : nullptr;
                bool rotate = false;
                std::string candleKey = candleTextures ? resolveTextureKey(candleTextures, FaceOrder[models::Up], Axis::Y, std::nullopt, rotate) : std::string();
                uint32_t candleMaterial = candleKey.empty() ? DiagnosticMaterial : materialFor(candleKey, false, variantFor(candleKey, candleName, record.states));
                if (!complete || candleMaterial == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("candle_cake", materials, { candleMaterial }), [&] {
                    pushTemplate(models::candleCake(materials, candleMaterial), 0);
                });
                break;
            }
            case ModelKind::Honey: {
                if (!complete) {
                    break;
                }
                modelTemplate = intern(keyOf("honey", materials, {}), [&] {
                    pushTemplate(models::honey(materials), 0);
                });
                break;
            }
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
                models::Materials same;
                same.fill(material);
                modelTemplate = intern(keyOf("door", same, {}), [&] {
                    for (uint32_t state = 0; state < 16; ++state) {
                        auto [min, max] = models::doorBounds(state & 3, (state >> 2) & 1, (state >> 3) & 1);
                        auto faces = models::cuboid(same, min, max);
                        pushTemplate({ faces.begin(), faces.end() }, TemplateDoor);
                    }
                });
                visual.doorState = uint8_t(cardinal() | (flag("open_bit") ? 4 : 0)
                    | (flag("door_hinge_bit") ? 8 : 0) | (upper ? DoorUpper : 0));
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
                    int32_t layersHigh = std::clamp(stateInt(record.states, "height").value_or(0), 0, 7);
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
            case ModelKind::Lantern: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                bool hanging = flag("hanging");
                modelTemplate = intern(keyOf("lantern", uniform(models::Up), { hanging }), [&] {
                    pushTemplate(models::lantern(material, hanging), 0);
                });
                break;
            }
            case ModelKind::Candle:
            case ModelKind::TurtleEgg: {
                uint32_t material = materials[models::Up];
                if (material == DiagnosticMaterial) {
                    break;
                }
                uint32_t count = 1;
                if (kind == ModelKind::Candle) {
                    count = static_cast<uint32_t>(std::clamp(stateInt(record.states, "candles").value_or(0), 0, 3)) + 1;
                } else {
                    static constexpr const char* Counts[4] = { "one_egg", "two_egg", "three_egg", "four_egg" };
                    std::string eggs = stateString(record.states, "turtle_egg_count");
                    for (uint32_t index = 0; index < 4; ++index) {
                        if (eggs == Counts[index]) {
                            count = index + 1;
                        }
                    }
                }
                modelTemplate = intern(keyOf(kind == ModelKind::Candle ? "candles" : "turtle_eggs", uniform(models::Up), { count }), [&] {
                    pushTemplate(kind == ModelKind::Candle ? models::candles(material, count) : models::turtleEggs(material, count), 0);
                });
                break;
            }
            case ModelKind::Cauldron: {
                if (!complete) {
                    break;
                }
                std::string liquid = name == "lava_cauldron" ? "lava" : stateString(record.states, "cauldron_liquid");
                uint32_t level = static_cast<uint32_t>(std::clamp(stateInt(record.states, "fill_level").value_or(0), 0, 6));
                uint32_t surface = DiagnosticMaterial;
                if (liquid == "lava" || liquid == "powder_snow") {
                    surface = materialFor(liquid == "lava" ? "still_lava" : "powder_snow", false);
                } else if (std::string waterKey = faceKeyFor(models::West); !waterKey.empty()) {
                    surface = materialFor(waterKey, false, 0, CauldronWaterColor);
                }
                if (surface == DiagnosticMaterial) {
                    level = 0;
                }
                modelTemplate = intern(keyOf("cauldron", materials, { surface, level }), [&] {
                    pushTemplate(models::cauldron(materials, surface, level), 0);
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
                    if (name == "pointed_dripstone" || name == "sulfur_spike") {
                        const std::string thickness = stateString(record.states, "dripstone_thickness");
                        face = thickness == "base" ? models::Up : thickness == "frustum" ? models::Down
                            : thickness == "middle" ? models::South : thickness == "merge" ? models::West : models::North;
                    } else if (name == "sweet_berry_bush") {
                        static constexpr int ByGrowth[4] = { models::Down, models::Up, models::North, models::South };
                        face = ByGrowth[std::clamp(stateInt(record.states, "growth").value_or(0), 0, 3)];
                    } else if (textures->get("up") && textures->get("down") && name != "seagrass") {
                        face = flag("upper_block_bit") ? models::Up : models::Down;
                    }
                }
                uint32_t material = materials[face];
                uint32_t second = material;
                if (name == "seagrass") {
                    std::string type = stateString(record.states, "sea_grass_type");
                    if (type == "double_bot") {
                        material = materials[models::Down];
                        second = materials[models::South];
                    } else if (type == "double_top") {
                        material = materials[models::North];
                        second = materials[models::West];
                    }
                }
                if (material == DiagnosticMaterial || second == DiagnosticMaterial) {
                    break;
                }
                modelTemplate = intern(keyOf("cross", uniform(face), { material, second }), [&] {
                    pushTemplate(models::cross(material, second), 0);
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
            case ModelKind::Chain: {
                uint32_t first = materials[models::Up];
                uint32_t second = materials[models::South];
                if (first == DiagnosticMaterial || second == DiagnosticMaterial) {
                    break;
                }
                std::string axis = stateString(record.states, "pillar_axis");
                uint32_t facing = axis == "x" ? models::East : axis == "z" ? models::South : models::Up;
                modelTemplate = intern(keyOf("chain", { first, second, 0, 0, 0, 0 }, { facing }), [&] {
                    pushTemplate(models::chain(first, second, facing), 0);
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
            case ModelKind::Shape: {
                if (name == "flower_pot") visual.blockEntity = EntityFlowerPot;
                BlockShape shape = blockShape(name, record.states);
                if (name == "lectern") {
                    modelTemplate = intern(keyOf("lectern", materials, { shape.turns }), [&] {
                        pushTemplate(models::lectern(materials, shape.turns), 0);
                    });
                    break;
                }
                std::vector<models::ShapePart> parts;
                std::string shapeKey = "shape";
                bool usable = true;
                for (const ShapeBox& box : shape.boxes) {
                    models::ShapePart shapePart;
                    for (int axis = 0; axis < 3; ++axis) {
                        shapePart.min[axis] = static_cast<int16_t>(box.min[axis] * 16 + box.offset[axis]);
                        shapePart.max[axis] = static_cast<int16_t>(box.max[axis] * 16 + box.offset[axis]);
                    }
                    shapePart.materials = box.texture ? uniform(0) : box.side >= 0 ? uniform(box.side) : materials;
                    if (box.texture) {
                        shapePart.materials.fill(materialFor(box.texture, false, box.textureVariant));
                    }
                    for (size_t side = 0; side < 6; ++side) {
                        if (box.faceSides[side] >= 0) {
                            shapePart.materials[side] = materials[size_t(box.faceSides[side])];
                        }
                    }
                    shapePart.uvs = box.uvs;
                    shapePart.hidden = box.hidden;
                    shapePart.uvSize = box.uvSize;
                    if (box.uvs) {
                        for (const auto& rect : *box.uvs) {
                            shapeKey += '#' + std::to_string(rect[0]) + ',' + std::to_string(rect[1]) + ',' + std::to_string(rect[2]) + ',' + std::to_string(rect[3]);
                        }
                    }
                    shapeKey += '~' + std::to_string(box.hidden) + ':' + std::to_string(box.uvSize);
                    for (uint32_t material : shapePart.materials) {
                        usable &= material != DiagnosticMaterial;
                        shapeKey += ':' + std::to_string(material);
                    }
                    for (int axis = 0; axis < 3; ++axis) {
                        shapeKey += '/' + std::to_string(shapePart.min[axis]) + ',' + std::to_string(shapePart.max[axis]);
                    }
                    parts.push_back(shapePart);
                }
                std::vector<ModelQuad> extra;
                if (shape.crossSide >= 0 && materials[shape.crossSide] != DiagnosticMaterial) {
                    extra = models::cross(materials[shape.crossSide], materials[shape.crossSide]);
                }
                if (shape.planeSide >= 0) {
                    usable &= materials[shape.planeSide] != DiagnosticMaterial;
                    extra = models::flatPlane(materials[shape.planeSide], shape.planeHeight);
                }
                if (!usable || (parts.empty() && extra.empty())) {
                    break;
                }
                shapeKey += "|" + std::to_string(extra.empty() ? 0 : extra.front().material) + "|" + std::to_string(shape.crossSide) + std::to_string(shape.planeSide) + ":" + std::to_string(shape.planeHeight);
                modelTemplate = intern(keyOf(shapeKey, {}, { shape.turns, uint32_t(shape.facing + 1) }), [&] {
                    // Tilted models like a wall grindstone tip over first, then turn like the rest.
                    std::vector<ModelQuad> quads = models::shape(parts, std::move(extra), 0);
                    if (shape.facing >= 0) {
                        quads = models::orient(std::move(quads), uint32_t(shape.facing));
                    }
                    pushTemplate(models::shape({}, std::move(quads), shape.turns), 0);
                });
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
                rememberCarried(record, name, entry, visual, Axis::Y, std::nullopt);
            }
            continue;
        }

        std::optional<Face> facing = stateFacing(record.states);
        bool resolved = true;
        for (int face = 0; face < 6; ++face) {
            bool rotate = false;
            std::string key = textures ? resolveTextureKey(textures, FaceOrder[face], axis, facing, rotate) : fallbackKey;
            visual.faces[face] = isEndPortalName(name) ? endPortalFor() : key.empty() ? DiagnosticMaterial : materialFor(key, rotate, 0, tint, blockTint(name, face));
            resolved &= visual.faces[face] != DiagnosticMaterial;
        }
        if (!resolved) {
            visual.flags = FlagDiagnostic;
            visual.faces.fill(DiagnosticMaterial);
            ++diagnosticCount;
        } else if (name == "end_stone" || name == "chorus_flower") {
            visual.modelTemplate = intern("chorus-neighbour|" + name, [&] {
                pushTemplate({}, name == "end_stone" ? TemplateChorusSupport : TemplateChorusFlower);
            });
        } else if (family == Family::Liquid) {
            visual.liquid = contains(name, "lava") ? 2 : 1;
            visual.liquidLevel = static_cast<uint8_t>(std::clamp(stateInt(record.states, "liquid_depth").value_or(0), 0, 15));
            visual.flags = static_cast<uint8_t>(visual.flags & FlagTranslucent);
        } else {
            rememberCarried(record, name, entry, visual, axis, facing);
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
            const json::Value* legacyEntry = blockEntry(custom.name);
            const json::Value* legacyTextures = legacyEntry ? legacyEntry->get("textures") : nullptr;
            auto instanceMaterial = [&](const std::string& instanceName, int side) -> uint32_t {
                if (!materialMap || materialMap->getType() != Tag::Type::Compound) {
                    if (!legacyTextures) {
                        return DiagnosticMaterial;
                    }
                    bool rotate = false;
                    std::string key = resolveTextureKey(legacyTextures, FaceOrder[side], Axis::Y, std::nullopt, rotate);
                    return key.empty() ? DiagnosticMaterial : materialFor(key, false);
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

            CustomState state { block, std::move(states) };
            const Tag* collision = components.contains("minecraft:collision_box") ? components["minecraft:collision_box"] : nullptr;
            state.collides = componentEnabled(collision);
            state.collision.shape = ShapeCustom;
            state.collision.flags = CollisionSolid;
            if (state.collides) {
                std::vector<CollisionBox> boxes = componentBoxes(collision);
                if (boxes.empty()) {
                    boxes.push_back({ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f });
                }
                state.boxIndex = customBoxes.size();
                state.collision.boxCount = static_cast<uint16_t>(boxes.size());
                customBoxes.insert(customBoxes.end(), boxes.begin(), boxes.end());
            }
            const Tag* selection = components.contains("minecraft:selection_box") ? components["minecraft:selection_box"] : nullptr;
            if (selection && !componentEnabled(selection)) {
                state.selection = CollisionBox {};
            } else if (std::vector<CollisionBox> picked = componentBoxes(selection); !picked.empty()) {
                CollisionBox bounds = picked.front();
                for (const CollisionBox& box : picked) {
                    bounds = { std::min(bounds.minX, box.minX), std::min(bounds.minY, box.minY), std::min(bounds.minZ, box.minZ),
                        std::max(bounds.maxX, box.maxX), std::max(bounds.maxY, box.maxY), std::max(bounds.maxZ, box.maxZ) };
                }
                state.selection = bounds;
            }
            uint32_t index = static_cast<uint32_t>(visuals.size());
            customByHash.emplace(static_cast<uint32_t>(BlockStateHasher::hash(custom.name, state.states)), index);
            customStates.push_back(std::move(state));
            visuals.push_back(visual);
        }
    }

    for (CustomState& state : customStates) {
        // Every box is in place now, so the states can point at them.
        if (state.collides) {
            state.collision.box = &customBoxes[state.boxIndex];
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
    for (uint32_t stage = 0; stage < DestroyStages; ++stage) {
        if (loadImage("textures/environment/destroy_stage_" + std::to_string(stage), width, height, rgba)) {
            destroyStages[stage] = static_cast<uint32_t>(layers.size());
            layers.push_back(tileOf(rgba, width, 0, 0, std::min(width, height)));
        }
    }

    timer.mark("assets: block visuals, textures and models");
    const BlockVisual* emptyPot = nullptr;
    for (size_t index = 0; index < registry.records().size(); ++index) {
        if (registry.records()[index].name == "minecraft:flower_pot") {
            emptyPot = &visuals[index];
            break;
        }
    }
    if (emptyPot && emptyPot->hasModel()) {
        const auto potQuads = templateQuads(emptyPot->modelTemplate);
        for (size_t index = 0; index < registry.records().size(); ++index) {
            const auto& record = registry.records()[index];
            if (!pottablePlant(record.name)) continue;
            auto materials = visuals[index].faces;
            uint32_t leaves = DiagnosticMaterial;
            if (record.name == "minecraft:bamboo" || record.name == "minecraft:bamboo_sapling") {
                materials.fill(materialFor("bamboo_stem", false, 0));
                leaves = materialFor("bamboo_singleleaf", false, 0);
            } else if (record.name == "minecraft:azalea" || record.name == "minecraft:flowering_azalea") {
                const std::string prefix = record.name == "minecraft:azalea" ? "potted_azalea_bush_" : "potted_flowering_azalea_bush_";
                materials.fill(materialFor(prefix + "side", false, 0));
                materials[models::Up] = materialFor(prefix + "top", false, 0);
                leaves = materialFor(prefix + "plant", false, 0);
            }
            if (std::find(materials.begin(), materials.end(), DiagnosticMaterial) != materials.end()) continue;
            auto plant = models::pottedPlant(record.name, materials, leaves);
            auto combined = potQuads;
            combined.insert(combined.end(), plant.begin(), plant.end());
            entityTemplates.pottedPlants.emplace(record.networkHash, pushTemplate(combined, 0));
        }
    }
    buildBlockEntityTemplates(pack, layers, overlayLayers, materialByKey, pushTemplate);
    timer.mark("assets: block entity templates");
    buildInterfaceAssets(pack, packs);
    timer.mark("assets: interface assets");

    std::filesystem::path behaviorRoot = root.parent_path().parent_path() / "behavior_packs" / root.filename();
    PackSource behaviors(behaviorRoot);
    biomes.load(pack, behaviors);
    loadItemUseDurations(behaviors);
    timer.mark("assets: biomes and item durations");

    overlayLayers.resize(layers.size(), false);
    std::vector<bool> cutoutLayers(layers.size(), false);
    std::vector<bool> blendedLayers(layers.size(), false);
    auto classifyMaterial = [&](uint32_t id, bool translucent) {
        if (id >= materialTable.size()) return;
        const Material& material = materialTable[id];
        for (uint32_t frame = 0; frame < material.frameCount; ++frame) {
            size_t layer = size_t(material.layer) + frame;
            if (layer < layers.size()) {
                if (translucent) blendedLayers[layer] = true;
                else cutoutLayers[layer] = true;
            }
        }
    };
    for (const BlockVisual& visual : visuals) {
        bool translucent = (visual.flags & FlagTranslucent) != 0;
        for (uint32_t material : visual.faces) classifyMaterial(material, translucent);
        if (visual.modelTemplate != NoModelTemplate && visual.modelTemplate < templates.size()) {
            const ModelTemplate& model = templates[visual.modelTemplate];
            for (uint32_t i = 0; i < model.quadCount; ++i) classifyMaterial(quads[model.quadStart + i].material, translucent);
        }
    }
    for (size_t layer = 0; layer < layers.size(); ++layer) cutoutLayers[layer] = cutoutLayers[layer] && !blendedLayers[layer];
    buildMips(textureArray, layers, overlayLayers, cutoutLayers);
    timer.mark("assets: mipmaps");
    entities.join();
    timer.mark("assets: waiting for entity models");
    return true;
}

}
