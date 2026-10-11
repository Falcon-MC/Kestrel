#include "client/Session.h"
#include "client/BlockEntityItems.h"
#include "world/ConduitState.h"
#include "world/EntityDisplayDimensions.h"

#include "Protocol/Packets/ClientboundMapItemDataPacket.h"
#include "Protocol/Packets/MapInfoRequestPacket.h"
#include "Protocol/BlockStateHasher.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr size_t MaxKnownMaps = 512;
constexpr size_t MaxMarkers = 128;
constexpr uint32_t FrameScanInterval = 10;
constexpr int32_t FrameScanChunks = 4;
constexpr int32_t FrameScanSections = 3;
constexpr size_t MaxFrames = 256;

/**
 * The stack an item frame's block entity holds, as its Item compound
 * describes it: name, count, data value, enchantments and the map it shows.
 */
HudItem frameItem(const Tag& item)
{
    HudItem out;
    out.identifier = item.getString("Name", "");
    if (out.identifier.empty()) {
        return out;
    }
    const Tag* count = item.get("Count");
    out.count = count && count->getType() == Tag::Type::Byte ? count->asByte() : 1;
    if (out.count <= 0) {
        out.count = 1;
    }
    const Tag* damage = item.get("Damage");
    out.aux = damage && damage->getType() == Tag::Type::Short ? damage->asShort() : 0;
    out.enchanted = itemHasIntrinsicGlint(out.identifier);
    if (const Tag* tag = item.get("tag"); tag && tag->isCompound()) {
        if (const Tag* map = tag->get("map_uuid"); map && map->getType() == Tag::Type::Long) {
            out.mapId = map->asLong();
        }
        out.enchanted = itemHasGlint(out.identifier, *tag);
    }
    return out;
}

}

/**
 * Collects, every half second, the items held by the item frames around the
 * player, read from their block entities and the frame's facing state.
 */
void Session::tickFrameItems()
{
    const double now = secondsNow();
    auto applyComponentGlint = [&](HudItem& item) {
        if (auto definition = itemDefinitions.getDefinition(item.identifier)) {
            item.enchanted |= itemComponentHasGlint(definition->getComponentData());
        }
    };
    for (const auto& [cell, animation] : pistonAnimations) {
        if (now - animation.start < 0.15 && animation.from != animation.to) {
            frameScanTicks = FrameScanInterval;
            break;
        }
    }
    if (++frameScanTicks < FrameScanInterval || !assets) {
        return;
    }
    frameScanTicks = 0;
    MotionVector feet = motion.position();
    int32_t chunkX = static_cast<int32_t>(std::floor(feet.x)) >> 4;
    int32_t chunkY = static_cast<int32_t>(std::floor(feet.y)) >> 4;
    int32_t chunkZ = static_cast<int32_t>(std::floor(feet.z)) >> 4;
    std::vector<FrameItemView> frames;
    std::vector<ShelfItemView> shelves;
    std::vector<EnchantingBookView> books;
    std::vector<BeaconBeamView> beacons;
    std::vector<ConduitView> conduits;
    std::vector<BannerView> banners;
    std::vector<SignTextView> signs;
    std::vector<SpawnerView> spawners;
    std::vector<VaultItemView> vaults;
    std::vector<PotView> pots;
    std::vector<PistonView> pistons;
    std::vector<MovingBlockView> movingBlocks;
    std::set<std::array<int32_t, 4>> activeCells;
    std::set<std::array<int32_t, 4>> bannerCells;
    std::set<std::array<int32_t, 4>> potCells;
    std::set<std::array<int32_t, 4>> pistonCells;
    auto count = [&] { return frames.size() + shelves.size() + books.size() + beacons.size() + conduits.size() + banners.size() + signs.size() + spawners.size() + vaults.size() + pots.size() + pistons.size() + movingBlocks.size(); };
    std::unordered_map<uint32_t, std::string> names;
    auto nameOf = [&](uint32_t value) -> const std::string& {
        auto found = names.find(value);
        if (found == names.end()) {
            std::string name = assets->blockName(value, ids.hashed, ids.sequential.get());
            if (!name.empty() && name.find(':') == std::string::npos) name.insert(0, "minecraft:");
            found = names.emplace(value, std::move(name)).first;
        }
        return found->second;
    };
    for (int32_t x = chunkX - FrameScanChunks; x <= chunkX + FrameScanChunks && count() < MaxFrames; ++x) {
        for (int32_t z = chunkZ - FrameScanChunks; z <= chunkZ + FrameScanChunks && count() < MaxFrames; ++z) {
            for (int32_t y = chunkY - FrameScanSections; y <= chunkY + FrameScanSections && count() < MaxFrames; ++y) {
                std::shared_ptr<const world::BlockEntityMap> entities = world.store().blockEntities({ current.dimension, x, y, z });
                if (!entities) {
                    continue;
                }
                for (const auto& [index, data] : *entities) {
                    if (count() >= MaxFrames) break;
                    std::string id = data.getString("id", "");
                    const std::array<int32_t, 3> cell { x * 16 + int32_t((index >> 8) & 15), y * 16 + int32_t(index & 15), z * 16 + int32_t((index >> 4) & 15) };
                    if ((id == "Shelf" && !nameOf(blockAt(cell[0], cell[1], cell[2])).ends_with("_shelf"))
                        || (id == "EnchantTable" && nameOf(blockAt(cell[0], cell[1], cell[2])) != "minecraft:enchanting_table")
                        || (id == "Lectern" && nameOf(blockAt(cell[0], cell[1], cell[2])) != "minecraft:lectern")
                        || ((id == "Sign" || id == "HangingSign") && !nameOf(blockAt(cell[0], cell[1], cell[2])).ends_with("sign"))
                        || (id == "MobSpawner" && nameOf(blockAt(cell[0], cell[1], cell[2])) != "minecraft:mob_spawner")
                        || (id == "Banner" && !nameOf(blockAt(cell[0], cell[1], cell[2])).ends_with("banner"))
                        || (id == "Beacon" && nameOf(blockAt(cell[0], cell[1], cell[2])) != "minecraft:beacon")
                        || (id == "Conduit" && nameOf(blockAt(cell[0], cell[1], cell[2])) != "minecraft:conduit")) {
                        continue;
                    }
                    if (id == "PistonArm") {
                        const auto& visual = assets->visual(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get());
                        if (visual.blockEntity != world::EntityPiston) continue;
                        PistonView piston;
                        piston.cell = cell;
                        piston.head = visual.modelTemplate + 3;
                        piston.facing = int(visual.variant);
                        const float progress = world::pistonProgress(data);
                        piston.animation = { progress, progress, now };
                        if (const auto found = pistonAnimations.find(cell); found != pistonAnimations.end()) piston.animation = found->second;
                        pistons.push_back(piston);
                        pistonCells.insert({ current.dimension, cell[0], cell[1], cell[2] });
                        if (!data.get(world::PistonMovingKey)) {
                            Tag updated = data;
                            updated.putByte(world::PistonMovingKey, 1);
                            world.store().setBlockEntity(current.dimension, cell[0], cell[1], cell[2], std::move(updated));
                        }
                    } else if (id == "MovingBlock" && nameOf(blockAt(cell[0], cell[1], cell[2])) == "minecraft:moving_block") {
                        const Tag* state = data.get("movingBlock");
                        const Tag* name = state ? state->get("name") : nullptr;
                        const Tag* properties = state ? state->get("states") : nullptr;
                        if (!name || name->getType() != Tag::Type::String || name->asString().size() > 256
                            || !properties || !properties->isCompound()) continue;
                        MovingBlockView moving;
                        moving.cell = cell;
                        bool valid = true;
                        static constexpr const char* Keys[3] = { "pistonPosX", "pistonPosY", "pistonPosZ" };
                        for (size_t axis = 0; axis < 3; ++axis) {
                            const Tag* position = data.get(Keys[axis]);
                            if (!position || position->getType() != Tag::Type::Int
                                || std::abs(int64_t(position->asInt()) - cell[axis]) > 13) { valid = false; break; }
                            moving.piston[axis] = position->asInt();
                        }
                        if (!valid) continue;
                        const Tag* expanding = data.get("expanding");
                        moving.expanding = expanding && expanding->getType() == Tag::Type::Byte && expanding->asByte() != 0;
                        std::string blockName = name->asString();
                        if (blockName.find(':') == std::string::npos) blockName.insert(0, "minecraft:");
                        const uint32_t hash = uint32_t(BlockStateHasher::hash(blockName, *properties));
                        const auto value = assets->networkValueForState(hash, ids.hashed, ids.sequential.get());
                        if (!value) continue;
                        moving.value = *value;
                        movingBlocks.push_back(moving);
                    } else if (id == "DecoratedPot" && nameOf(blockAt(cell[0], cell[1], cell[2])) == "minecraft:decorated_pot") {
                        PotView pot;
                        pot.cell = cell;
                        pot.patterns = world::potPatterns(data);
                        pot.rotation = float(assets->visual(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get()).variant & 3) * 1.5707963f;
                        if (const auto found = potAnimations.find(cell); found != potAnimations.end()) {
                            pot.animation = found->second.first;
                            pot.animationStart = found->second.second;
                        }
                        pots.push_back(pot);
                        potCells.insert({ current.dimension, cell[0], cell[1], cell[2] });
                        if (!data.get(world::PotMovingKey)) {
                            Tag updated = data;
                            updated.putByte(world::PotMovingKey, 1);
                            world.store().setBlockEntity(current.dimension, cell[0], cell[1], cell[2], std::move(updated));
                        }
                    } else if (id == "Shelf") {
                        ShelfItemView shelf;
                        shelf.cell = cell;
                        shelf.items = shelfItems(data);
                        for (HudItem& item : shelf.items) applyComponentGlint(item);
                        uint32_t value = blockAt(cell[0], cell[1], cell[2]);
                        if (const Tag* states = assets->blockStates(value, ids.hashed, ids.sequential.get())) {
                            const std::string direction = states->getString("minecraft:cardinal_direction", "south");
                            shelf.rotation = direction == "west" ? 1 : direction == "north" ? 2 : direction == "east" ? 3 : 0;
                        }
                        shelves.push_back(std::move(shelf));
                    } else if (id == "Banner") {
                        BannerView banner;
                        banner.cell = cell;
                        const auto& visual = assets->visual(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get());
                        if (visual.blockEntity != world::EntityWallBanner && visual.blockEntity != world::EntityStandingBanner) continue;
                        banner.wall = visual.blockEntity == world::EntityWallBanner;
                        banner.rotation = float(visual.variant & (banner.wall ? 3u : 15u)) * (banner.wall ? 1.5707963f : 0.3926991f);
                        banner.display = world::bannerDisplay(data);
                        banners.push_back(std::move(banner));
                        bannerCells.insert({ current.dimension, cell[0], cell[1], cell[2] });
                        if (!data.get(world::BannerMovingKey)) {
                            Tag updated = data;
                            updated.putByte(world::BannerMovingKey, 1);
                            world.store().setBlockEntity(current.dimension, cell[0], cell[1], cell[2], std::move(updated));
                        }
                    } else if (id == "Conduit") {
                        auto water = [&](int dx, int dy, int dz) {
                            for (uint32_t layer = 0; layer < 2; ++layer) {
                                uint32_t value = blockAt(cell[0] + dx, cell[1] + dy, cell[2] + dz, layer);
                                if (value != world::ImplicitAir && assets->visual(value, ids.hashed, ids.sequential.get()).liquid == 1) return true;
                            }
                            return false;
                        };
                        const int frame = world::conduitFrameCount(water, [&](int dx, int dy, int dz) {
                            return world::conduitFrame(nameOf(blockAt(cell[0] + dx, cell[1] + dy, cell[2] + dz)));
                        });
                        const bool active = frame >= 16;
                        if (active) {
                            conduits.push_back({ cell, frame == 42 });
                            activeCells.insert({ current.dimension, cell[0], cell[1], cell[2] });
                        }
                        if ((data.get(world::ConduitActiveKey) != nullptr) != active) {
                            Tag updated = data;
                            if (active) updated.putByte(world::ConduitActiveKey, 1);
                            else updated.remove(world::ConduitActiveKey);
                            world.store().setBlockEntity(current.dimension, cell[0], cell[1], cell[2], std::move(updated));
                        }
                    } else if (id == "Beacon" && beacons.size() < 32) {
                        bool base = true;
                        for (int dx = -1; dx <= 1 && base; ++dx) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!world::beaconBase(nameOf(blockAt(cell[0] + dx, cell[1] - 1, cell[2] + dz)))) {
                                    base = false;
                                    break;
                                }
                            }
                        }
                        world::DimensionRange range;
                        if (base && world::vanillaDimensionRange(current.dimension, range)) {
                            BeaconBeamView beacon;
                            beacon.cell = cell;
                            beacon.sections = world::beaconSections(cell[1], (range.baseSubChunkY + range.subChunkCount) * 16,
                                [&](int32_t height) {
                                    uint32_t value = blockAt(cell[0], height, cell[2]);
                                    const std::string& name = nameOf(value);
                                    const auto color = world::beaconGlassColor(name);
                                    bool blocked = value != world::ImplicitAir && !color && name != "minecraft:bedrock"
                                        && assets->visual(value, ids.hashed, ids.sequential.get()).lightFilter >= 15;
                                    return world::BeaconColumnBlock { blocked, color };
                                });
                            if (!beacon.sections.empty()) beacons.push_back(std::move(beacon));
                        }
                    } else if (id == "EnchantTable" || id == "Lectern") {
                        EnchantingBookView book;
                        book.cell = cell;
                        book.lectern = id == "Lectern";
                        if (book.lectern) {
                            const Tag* item = data.get("book");
                            if (!item || !item->isCompound() || frameItem(*item).empty()) continue;
                            if (const Tag* states = assets->blockStates(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get())) {
                                const std::string facing = states->getString("minecraft:cardinal_direction", "south");
                                book.rotation = (facing == "west" ? 1 : facing == "north" ? 2 : facing == "east" ? 3 : 0) * 1.5707963f;
                            }
                        } else {
                            const Tag* rotation = data.get("rott");
                            if (rotation && rotation->getType() == Tag::Type::Float && std::isfinite(rotation->asFloat())) {
                                book.rotation = std::remainder(rotation->asFloat(), 6.2831853f);
                            }
                        }
                        books.push_back(book);
                    } else if (id == "MobSpawner" || nameOf(blockAt(cell[0], cell[1], cell[2])) == "minecraft:trial_spawner") {
                        auto display = world::spawnerDisplay(data, id != "MobSpawner");
                        if (id != "MobSpawner") {
                            if (const auto size = world::entityDisplaySize(display.identifier)) {
                                display.width = size->width;
                                display.height = size->height;
                            }
                        }
                        if (!display.identifier.empty()) spawners.push_back({ cell, std::move(display) });
                    } else if (nameOf(blockAt(cell[0], cell[1], cell[2])) == "minecraft:vault") {
                        const Tag* item = data.get("display_item");
                        if (item) {
                            auto displayed = blockEntityItem(*item);
                            applyComponentGlint(displayed);
                            if (!displayed.empty()) vaults.push_back({ cell, std::move(displayed) });
                        }
                    } else if (id == "Sign" || id == "HangingSign") {
                        SignTextView sign;
                        sign.cell = cell;
                        sign.name = nameOf(blockAt(cell[0], cell[1], cell[2]));
                        sign.sides = signTexts(data);
                        if (const Tag* states = assets->blockStates(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get())) {
                            sign.rotation = states->getInt("ground_sign_direction", 0);
                            sign.facing = states->getInt("facing_direction", 2);
                            const Tag* hanging = states->get("hanging");
                            sign.hanging = hanging && hanging->getType() == Tag::Type::Byte && hanging->asByte() != 0;
                        }
                        signs.push_back(std::move(sign));
                    }
                    if (count() >= MaxFrames) {
                        break;
                    }
                    if (id != "ItemFrame" && id != "GlowItemFrame") {
                        continue;
                    }
                    const Tag* item = data.get("Item");
                    if (!item || !item->isCompound()) {
                        continue;
                    }
                    FrameItemView view;
                    view.cell = { x * 16 + int32_t((index >> 8) & 15), y * 16 + int32_t(index & 15), z * 16 + int32_t((index >> 4) & 15) };
                    view.item = frameItem(*item);
                    applyComponentGlint(view.item);
                    if (view.item.empty()) {
                        continue;
                    }
                    if (const Tag* rotation = data.get("ItemRotation")) {
                        view.rotation = rotation->getType() == Tag::Type::Float ? rotation->asFloat() : rotation->getType() == Tag::Type::Byte ? rotation->asByte() * 45.0f : 0.0f;
                    }
                    uint32_t value = blockAt(view.cell[0], view.cell[1], view.cell[2]);
                    if (const Tag* states = assets->blockStates(value, ids.hashed, ids.sequential.get()); states && states->isCompound()) {
                        if (const Tag* facing = states->get("facing_direction"); facing && facing->getType() == Tag::Type::Int) {
                            view.facing = facing->asInt();
                        }
                    }
                    frames.push_back(std::move(view));
                    if (count() >= MaxFrames) {
                        break;
                    }
                }
            }
        }
    }
    for (const auto& cell : activeConduitCells) {
        if (activeCells.contains(cell)) continue;
        const auto entities = world.store().blockEntities({ cell[0], cell[1] >> 4, cell[2] >> 4, cell[3] >> 4 });
        if (!entities) continue;
        const auto index = static_cast<uint16_t>(world::linearIndex(uint32_t(cell[1] & 15), uint32_t(cell[2] & 15), uint32_t(cell[3] & 15)));
        const auto found = entities->find(index);
        if (found == entities->end() || !found->second.get(world::ConduitActiveKey)) continue;
        Tag data = found->second;
        data.remove(world::ConduitActiveKey);
        world.store().setBlockEntity(cell[0], cell[1], cell[2], cell[3], std::move(data));
    }
    activeConduitCells = std::move(activeCells);
    for (const auto& cell : activeBannerCells) {
        if (bannerCells.contains(cell)) continue;
        const auto entities = world.store().blockEntities({ cell[0], cell[1] >> 4, cell[2] >> 4, cell[3] >> 4 });
        if (!entities) continue;
        const auto index = static_cast<uint16_t>(world::linearIndex(uint32_t(cell[1] & 15), uint32_t(cell[2] & 15), uint32_t(cell[3] & 15)));
        const auto found = entities->find(index);
        if (found == entities->end() || !found->second.get(world::BannerMovingKey)) continue;
        Tag data = found->second;
        data.remove(world::BannerMovingKey);
        world.store().setBlockEntity(cell[0], cell[1], cell[2], cell[3], std::move(data));
    }
    activeBannerCells = std::move(bannerCells);
    for (const auto& cell : activePotCells) {
        if (potCells.contains(cell)) continue;
        const auto entities = world.store().blockEntities({ cell[0], cell[1] >> 4, cell[2] >> 4, cell[3] >> 4 });
        if (!entities) continue;
        const auto index = static_cast<uint16_t>(world::linearIndex(uint32_t(cell[1] & 15), uint32_t(cell[2] & 15), uint32_t(cell[3] & 15)));
        const auto found = entities->find(index);
        if (found == entities->end() || !found->second.get(world::PotMovingKey)) continue;
        Tag data = found->second;
        data.remove(world::PotMovingKey);
        world.store().setBlockEntity(cell[0], cell[1], cell[2], cell[3], std::move(data));
    }
    activePotCells = std::move(potCells);
    for (const auto& cell : activePistonCells) {
        if (pistonCells.contains(cell)) continue;
        const auto entities = world.store().blockEntities({ cell[0], cell[1] >> 4, cell[2] >> 4, cell[3] >> 4 });
        if (!entities) continue;
        const auto index = uint16_t(world::linearIndex(uint32_t(cell[1] & 15), uint32_t(cell[2] & 15), uint32_t(cell[3] & 15)));
        const auto found = entities->find(index);
        if (found == entities->end() || !found->second.get(world::PistonMovingKey)) continue;
        Tag data = found->second;
        data.remove(world::PistonMovingKey);
        world.store().setBlockEntity(cell[0], cell[1], cell[2], cell[3], std::move(data));
    }
    activePistonCells = std::move(pistonCells);
    std::erase_if(pistonAnimations, [&](const auto& entry) {
        return !activePistonCells.contains({ current.dimension, entry.first[0], entry.first[1], entry.first[2] }) && now - entry.second.start > 0.2;
    });
    std::erase_if(potAnimations, [&](const auto& entry) {
        return !activePotCells.contains({ current.dimension, entry.first[0], entry.first[1], entry.first[2] });
    });
    std::lock_guard<std::mutex> guard(mutex);
    current.frameItems = std::move(frames);
    current.shelfItems = std::move(shelves);
    current.enchantingBooks = std::move(books);
    current.beaconBeams = std::move(beacons);
    current.conduits = std::move(conduits);
    current.banners = std::move(banners);
    current.signTexts = std::move(signs);
    current.spawners = std::move(spawners);
    current.vaultItems = std::move(vaults);
    current.pots = std::move(pots);
    current.pistons = std::move(pistons);
    current.movingBlocks = std::move(movingBlocks);
}

/**
 * Takes in what the server says about a map: a block of pixels written at
 * an offset, given as ABGR words, and the markers, which replace the old
 * ones whenever they are sent. Each change bumps the map's revision so the
 * renderer knows to redraw it.
 */
void Session::handleMapPacket(const ClientboundMapItemDataPacket& packet)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = maps.find(packet.mUniqueMapId);
    if (found == maps.end()) {
        if (maps.size() >= MaxKnownMaps) {
            return;
        }
        found = maps.emplace(packet.mUniqueMapId, MapView {}).first;
    }
    MapView& map = found->second;
    bool changed = false;
    if (packet.mHasColors && packet.mHasWidth && packet.mHasHeight) {
        int32_t width = packet.mWidth;
        int32_t height = packet.mHeight;
        int32_t startX = packet.mHasXOffset ? packet.mXOffset : 0;
        int32_t startY = packet.mHasYOffset ? packet.mYOffset : 0;
        if (width > 0 && height > 0 && size_t(width) * size_t(height) <= packet.mColors.size()) {
            for (int32_t y = 0; y < height; ++y) {
                int32_t row = startY + y;
                if (row < 0 || row >= MapView::Size) {
                    continue;
                }
                for (int32_t x = 0; x < width; ++x) {
                    int32_t column = startX + x;
                    if (column < 0 || column >= MapView::Size) {
                        continue;
                    }
                    uint32_t abgr = static_cast<uint32_t>(packet.mColors[size_t(y) * size_t(width) + size_t(x)]);
                    uint8_t* pixel = map.pixels.data() + (size_t(row) * MapView::Size + size_t(column)) * 4;
                    pixel[0] = static_cast<uint8_t>(abgr & 0xFF);
                    pixel[1] = static_cast<uint8_t>((abgr >> 8) & 0xFF);
                    pixel[2] = static_cast<uint8_t>((abgr >> 16) & 0xFF);
                    pixel[3] = static_cast<uint8_t>((abgr >> 24) & 0xFF);
                }
            }
            changed = true;
        }
    }
    if (packet.mHasDecorations) {
        map.markers.clear();
        for (const MapDecoration& decoration : packet.mDecorations) {
            if (map.markers.size() >= MaxMarkers) {
                break;
            }
            MapMarker marker;
            marker.image = decoration.mImage;
            marker.rotation = decoration.mRotation & 15;
            marker.x = static_cast<int8_t>(decoration.mXOffset);
            marker.y = static_cast<int8_t>(decoration.mYOffset);
            marker.color = static_cast<uint32_t>(decoration.mColor);
            map.markers.push_back(marker);
        }
        changed = true;
    }
    if (changed || map.revision == 0) {
        map.revision = ++mapRevisions;
    }
}

uint64_t Session::mapRevision(int64_t mapId)
{
    if (mapId == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (auto found = maps.find(mapId); found != maps.end()) {
        return found->second.revision;
    }
    if (requestedMaps.size() < MaxKnownMaps && requestedMaps.insert(mapId).second) {
        pendingMapRequests.push_back(mapId);
    }
    return 0;
}

bool Session::copyMap(int64_t mapId, MapView& out)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = maps.find(mapId);
    if (found == maps.end()) {
        return false;
    }
    out = found->second;
    return true;
}

/**
 * Asks the server for every map the client wanted to show but has not
 * heard of, once each, the way the game requests a map it sees in a hand or
 * a frame.
 */
void Session::sendMapRequests()
{
    std::vector<int64_t> requests;
    {
        std::lock_guard<std::mutex> guard(mutex);
        requests.swap(pendingMapRequests);
    }
    if (!connection) {
        return;
    }
    for (int64_t mapId : requests) {
        MapInfoRequestPacket request;
        request.mUniqueMapId = mapId;
        transmit(request);
    }
}

}
